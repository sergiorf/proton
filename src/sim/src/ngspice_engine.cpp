// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/sim/ngspice_engine.h"

#include "proton/core/spice_number.h"

#include <ngspice/sharedspice.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>
#include <utility>

namespace proton::sim {

namespace {

// The ngspice API, resolved when the library is loaded. ngspice keeps its state in the library, so
// it is initialised on each load, with callbacks that reach the engine through g_engine. The
// library stays loaded until a fatal error: then it is freed and loaded again, which gives ngspice
// fresh state.
struct Api
{
    decltype(ngSpice_Init)* init = nullptr;
    decltype(ngSpice_Init_Sync)* initSync = nullptr;
    decltype(ngSpice_Command)* command = nullptr;
    decltype(ngSpice_Circ)* circ = nullptr;
    decltype(ngSpice_Reset)* reset = nullptr;
};

// How long recover() waits for ngspice's thread to end after an exit on it.
constexpr std::chrono::seconds kThreadEndTimeout{2};

// How long a halt waits for ngspice to stop at the halt source before it falls back to bg_halt.
// ngspice stops at its next accepted point, so only a step that takes this long falls back.
constexpr std::chrono::seconds kHaltTimeout{1};

int g_ident = 0; // ngspice's id for the one instance, passed to ngSpice_Init_Sync

// The shared library through the platform's loader: ngspice.dll on Windows (ADR 0002),
// libngspice.so on Linux, where it serves the sanitizer CI jobs (ADR 0008).
#ifdef _WIN32
using Module = HMODULE;
constexpr const char* kLibraryName = "ngspice.dll";
#else
using Module = void*;
constexpr const char* kLibraryName = "libngspice.so";
#endif

Module g_module = nullptr;              // the loaded library, while g_api is set
std::optional<Api> g_api;               // used only by the engine that owns ngspice
std::atomic<bool> g_taken{false};       // an NgspiceEngine exists
std::atomic<NgspiceEngine*> g_engine{}; // where ngspice's callbacks go

std::filesystem::path exeDir()
{
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n < buf.size()) {
            buf.resize(n);
            return std::filesystem::path(buf).parent_path();
        }
        buf.resize(buf.size() * 2);
    }
#else
    std::error_code ec;
    return std::filesystem::read_symlink("/proc/self/exe", ec).parent_path();
#endif
}

// Loads the library at `path`, or returns nullptr with the loader's reason in `why`.
Module openLibrary(const std::filesystem::path& path, std::string& why)
{
#ifdef _WIN32
    // The altered search path, for an absolute path, finds the DLLs ngspice.dll needs next to it.
    const Module module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr) {
        why = "error " + std::to_string(GetLastError());
    }
#else
    const Module module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (module == nullptr) {
        const char* error = dlerror();
        why = error != nullptr ? error : "dlopen failed";
    }
#endif
    return module;
}

void closeLibrary(Module module)
{
#ifdef _WIN32
    FreeLibrary(module);
#else
    dlclose(module);
#endif
}

// Paths as UTF-8, both ways: path::string() would throw for a character outside the ANSI code page.
std::filesystem::path fromUtf8(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

template <typename Fn>
Fn* resolve(Module module, const char* name)
{
#ifdef _WIN32
    return reinterpret_cast<Fn*>(reinterpret_cast<void*>(GetProcAddress(module, name)));
#else
    return reinterpret_cast<Fn*>(dlsym(module, name));
#endif
}

std::string toLower(std::string_view text)
{
    std::string result(text);
    for (char& c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

// SPICE's name for an ngspice vector: node `out` is "v(out)", branch `v1#branch` is "i(v1)".
std::string signalName(std::string_view vector)
{
    std::string name = toLower(vector);
    constexpr std::string_view kBranch = "#branch";
    if (name.size() > kBranch.size() && name.ends_with(kBranch)) {
        return "i(" + name.substr(0, name.size() - kBranch.size()) + ")";
    }
    if (name.find('(') != std::string::npos) {
        return name;
    }
    return "v(" + name + ")";
}

// Whether a line ngspice wrote to stderr reports an error. A netlist line ngspice cannot read is
// only a warning ("... is not a valid resistor instance line, ignored!"), but simulating the
// circuit without it would be wrong, so that is an error too.
bool reportsError(std::string_view line)
{
    return line.find("rror") != std::string_view::npos ||
           line.find("ignored") != std::string_view::npos;
}

int command(const Api& api, std::string_view text)
{
    std::string buf(text);
    return api.command(buf.data());
}

// The vectors of the halt source, which are not signals.
bool isHaltVector(std::string_view vector)
{
    const std::string name = toLower(vector);
    return name == toLower(NgspiceEngine::kHaltNode) ||
           name == toLower(NgspiceEngine::kHaltSource) + "#branch";
}

} // namespace

// ngspice calls these on its own thread (data, background thread state) or on the thread that
// called into it (text output). They reach the one engine through g_engine.
struct NgspiceEngine::Callbacks
{
    static int sendChar(char* text, int /*id*/, void* /*user*/)
    {
        NgspiceEngine* engine = g_engine.load(std::memory_order_acquire);
        if (engine == nullptr || text == nullptr) {
            return 0;
        }
        // Output arrives as "stdout ..." or "stderr ...". Only stderr carries errors.
        constexpr std::string_view kStderr = "stderr ";
        const std::string_view line(text);
        if (line.starts_with(kStderr)) {
            const std::lock_guard lock(engine->mutex_);
            engine->output_.append(line.substr(kStderr.size()));
            engine->output_ += '\n';
        }
        return 0;
    }

    static int sendStat(char* /*text*/, int /*id*/, void* /*user*/) { return 0; }

    // The value of an EXTERNAL source. The only one is the halt source: 1 V asks ngspice to stop.
    static int sourceValue(double* value, double /*time*/, char* /*name*/, int /*id*/,
                           void* /*user*/)
    {
        NgspiceEngine* engine = g_engine.load(std::memory_order_acquire);
        if (value != nullptr) {
            const bool halt =
                engine != nullptr && engine->haltSource_.load(std::memory_order_relaxed);
            *value = halt ? 1.0 : 0.0;
        }
        return 0;
    }

    // The vector names come with the first point too (sendData), so this has nothing to do.
    static int sendInitData(pvecinfoall /*all*/, int /*id*/, void* /*user*/) { return 0; }

    static int controlledExit(int /*status*/, NG_BOOL /*unload*/, NG_BOOL /*quit*/, int /*id*/,
                              void* /*user*/)
    {
        NgspiceEngine* engine = g_engine.load(std::memory_order_acquire);
        if (engine != nullptr) {
            const std::lock_guard lock(engine->mutex_);
            engine->exited_ = true;
            engine->changed_.notify_all();
        }
        return 0;
    }

    // Called by ngspice with true when its background thread ends, false when it starts.
    static int backgroundThread(NG_BOOL exited, int /*id*/, void* /*user*/)
    {
        NgspiceEngine* engine = g_engine.load(std::memory_order_acquire);
        if (engine != nullptr) {
            const std::lock_guard lock(engine->mutex_);
            engine->running_ = !exited;
            engine->changed_.notify_all();
        }
        return 0;
    }

    static int sendData(pvecvaluesall all, int count, int /*id*/, void* /*user*/)
    {
        NgspiceEngine* engine = g_engine.load(std::memory_order_acquire);
        if (engine == nullptr || all == nullptr || count <= 0) {
            return 0;
        }
        std::unique_lock lock(engine->mutex_);
        NgspiceEngine& e = *engine;
        if (e.halting_) {
            return 0;
        }
        if (!e.initialised_) {
            // The first point of the run sets the signals: the scale vector is time, every other
            // vector but the halt source's is a signal, in ngspice's order.
            e.columns_.assign(1, -1);
            e.runNames_.clear();
            for (int i = 0; i < count; ++i) {
                const vecvalues& v = *all->vecsa[i];
                const std::string_view name = v.name != nullptr ? v.name : "";
                if (v.is_scale) {
                    e.columns_[0] = i;
                } else if (!isHaltVector(name)) {
                    e.columns_.push_back(i);
                    e.runNames_.push_back(signalName(name));
                }
            }
            if (e.columns_[0] < 0) {
                return 0; // not a transient run: nothing to stream
            }
            e.slots_ = e.config_.queueCapacity;
            e.queue_.assign(e.slots_ * e.columns_.size(), 0.0);
            e.initialised_ = true;
        }
        if (static_cast<std::size_t>(count) < e.columns_.size()) {
            return 0;
        }
        const double time = all->vecsa[e.columns_[0]]->creal;
        if (e.haveTime_ && !(time > e.queuedTime_)) {
            return 0; // keep time strictly increasing
        }
        e.changed_.wait(
            lock, [&] { return e.halting_ || e.pausing_ || e.count_ < e.config_.queueCapacity; });
        if (e.halting_) {
            return 0;
        }
        const std::size_t stride = e.columns_.size();
        if (e.count_ == e.slots_) {
            // Pausing: ngspice gives a point or two more before it stops, and none may be lost.
            std::vector<double> grown(e.slots_ * 2 * stride, 0.0);
            for (std::size_t k = 0; k < e.count_; ++k) {
                const double* from = e.queue_.data() + ((e.head_ + k) % e.slots_) * stride;
                std::copy(from, from + stride, grown.data() + k * stride);
            }
            e.queue_ = std::move(grown);
            e.slots_ *= 2;
            e.head_ = 0;
        }
        const std::size_t slot = (e.head_ + e.count_) % e.slots_;
        double* point = e.queue_.data() + slot * stride;
        for (std::size_t c = 0; c < stride; ++c) {
            point[c] = all->vecsa[e.columns_[c]]->creal;
        }
        ++e.count_;
        e.haveTime_ = true;
        e.queuedTime_ = time;
        e.changed_.notify_all();
        return 0;
    }
};

NgspiceEngine::Config NgspiceEngine::sanitise(const Config& config)
{
    const Config defaults;
    Config result;
    result.stopTime =
        std::clamp(std::isfinite(config.stopTime) ? config.stopTime : defaults.stopTime,
                   kMinStopTime, kMaxStopTime);
    result.maxStep = std::clamp(std::isfinite(config.maxStep) ? config.maxStep : defaults.maxStep,
                                kMinMaxStep, kMaxMaxStep);
    result.maxStep = std::min(result.maxStep, result.stopTime);
    result.queueCapacity = std::clamp(config.queueCapacity, kMinQueueCapacity, kMaxQueueCapacity);
    result.libraryPath = config.libraryPath;
    return result;
}

NgspiceEngine::NgspiceEngine(const Config& config) : config_(sanitise(config))
{
    owner_ = !g_taken.exchange(true, std::memory_order_acq_rel);
    if (owner_) {
        g_engine.store(this, std::memory_order_release);
    }
}

NgspiceEngine::~NgspiceEngine()
{
    if (!owner_) {
        return;
    }
    halt();
    unload();
    recover();
    g_engine.store(nullptr, std::memory_order_release);
    g_taken.store(false, std::memory_order_release);
}

bool NgspiceEngine::load(std::string_view netlist)
{
    halt();
    unload();
    loaded_ = false;
    names_.clear();
    error_.clear();
    netlist_.clear();

    if (!owner_) {
        error_ = "ngspice: another NgspiceEngine exists; ngspice allows only one per process";
        return false;
    }
    if (std::all_of(netlist.begin(), netlist.end(),
                    [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; })) {
        error_ = "ngspice: the netlist is empty";
        return false;
    }
    if (!recover()) {
        error_ = "ngspice: exited after a fatal error and its thread did not end, so it cannot be "
                 "reloaded";
        return false;
    }
    if (!g_api) {
        std::filesystem::path path = exeDir() / kLibraryName;
        if (!config_.libraryPath.empty()) {
            std::error_code ec;
            path = std::filesystem::absolute(fromUtf8(config_.libraryPath), ec);
        }
        std::string why;
        const Module module = openLibrary(path, why);
        if (module == nullptr) {
            error_ = "ngspice: cannot load " + toUtf8(path) + " (" + why + ")";
            return false;
        }
        Api api;
        api.init = resolve<decltype(ngSpice_Init)>(module, "ngSpice_Init");
        api.command = resolve<decltype(ngSpice_Command)>(module, "ngSpice_Command");
        api.circ = resolve<decltype(ngSpice_Circ)>(module, "ngSpice_Circ");
        api.reset = resolve<decltype(ngSpice_Reset)>(module, "ngSpice_Reset");
        api.initSync = resolve<decltype(ngSpice_Init_Sync)>(module, "ngSpice_Init_Sync");
        if (api.init == nullptr || api.command == nullptr || api.circ == nullptr ||
            api.reset == nullptr || api.initSync == nullptr) {
            closeLibrary(module);
            error_ = "ngspice: " + toUtf8(path) + " does not export the shared-library API";
            return false;
        }
        // ngSpice_nospinit() and ngSpice_nospiceinit() would keep init files out, but in
        // ngspice 46 they crash before ngSpice_Init() (ADR 0002), so ngspice reads them if any.
        api.init(&Callbacks::sendChar, &Callbacks::sendStat, &Callbacks::controlledExit,
                 &Callbacks::sendData, &Callbacks::sendInitData, &Callbacks::backgroundThread,
                 nullptr);
        // The halt source is EXTERNAL: without a callback for it ngspice would exit.
        api.initSync(&Callbacks::sourceValue, nullptr, nullptr, &g_ident, nullptr);
        g_module = module;
        g_api = api;
    }
    netlist_ = netlist;
    if (!start()) {
        halt();
        unload();
        netlist_.clear();
        return false;
    }
    loaded_ = true;
    return true;
}

bool NgspiceEngine::start()
{
    // The netlist line by line, then the halt source and the analysis.
    std::vector<std::string> lines;
    std::string_view rest = netlist_;
    while (!rest.empty()) {
        const std::size_t eol = rest.find('\n');
        std::string_view line = rest.substr(0, eol);
        rest = eol == std::string_view::npos ? std::string_view{} : rest.substr(eol + 1);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        lines.emplace_back(line);
    }
    lines.push_back(std::string(kHaltSource) + " " + kHaltNode + " 0 DC 0 EXTERNAL");
    // ngspice keeps no plot: every point still reaches the data callback, but none is stored, so
    // a run commits no memory for its points (ngspice would otherwise reserve stopTime / maxStep
    // points of every vector when the run starts). Decision D6, ADR 0007.
    lines.push_back(".save none");
    const std::string step = core::formatSpiceNumber(config_.maxStep);
    lines.push_back(".tran " + step + " " + core::formatSpiceNumber(config_.stopTime) + " 0 " +
                    step);
    lines.push_back(".end");
    std::vector<char*> circuit;
    for (std::string& line : lines) {
        circuit.push_back(line.data());
    }
    circuit.push_back(nullptr);

    {
        const std::lock_guard lock(mutex_);
        output_.clear();
        head_ = 0;
        count_ = 0;
        initialised_ = false;
        halting_ = false;
        pausing_ = false;
        haveTime_ = false;
        runNames_.clear();
        running_ = true; // until ngspice's thread says it ended
    }
    finished_ = false;
    end_ = 0.0;
    lastTime_ = 0.0;
    haltSource_.store(false, std::memory_order_relaxed);

    // A fatal error in the netlist makes ngspice call controlledExit() from inside circ(), on this
    // thread, and circ() then fails.
    const bool parsed = g_api->circ(circuit.data()) == 0;
    {
        std::unique_lock lock(mutex_);
        if (!parsed || reportsError(output_)) {
            running_ = false;
            error_ = errorMessage();
            return false;
        }
    }
    // ngspice stops at the first accepted point after the engine raises the halt source.
    command(*g_api, std::string("stop when v(") + kHaltNode + ") > 0.5");
    if (command(*g_api, "bg_run") != 0) {
        const std::lock_guard lock(mutex_);
        running_ = false;
        error_ = errorMessage();
        return false;
    }
    // The run has started when its first point, the operating point at t = 0, is queued.
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [&] { return count_ > 0 || !running_ || exited_; });
    if (count_ == 0) {
        error_ = errorMessage();
        return false;
    }
    names_ = runNames_;
    return true;
}

void NgspiceEngine::halt()
{
    if (!g_api || !owner_) {
        return;
    }
    {
        const std::lock_guard lock(mutex_);
        if (!running_ || exited_) {
            return; // after an exit ngspice takes no commands; recover() waits for its thread
        }
        halting_ = true; // a callback blocked on a full queue returns, and later ones drop
        changed_.notify_all();
    }
    stopThread();
}

void NgspiceEngine::stopThread()
{
    // Called with halting_ or pausing_ set, so no callback blocks on a full queue.
    haltSource_.store(true, std::memory_order_relaxed);
    {
        std::unique_lock lock(mutex_);
        changed_.wait_for(lock, kHaltTimeout, [&] { return !running_ || exited_; });
        if (exited_) {
            haltSource_.store(false, std::memory_order_relaxed);
            return; // after an exit ngspice takes no commands; recover() waits for its thread
        }
    }
    // Joins the thread that stopped at the halt source, at once; or, if it did not stop, halts it
    // the slow way (ADR 0002).
    command(*g_api, "bg_halt");
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [&] { return !running_ || exited_; });
    haltSource_.store(false, std::memory_order_relaxed);
}

std::string NgspiceEngine::alter(std::string_view change)
{
    if (!loaded_ || finished_ || !g_api || !owner_) {
        return "ngspice: no run to change";
    }
    {
        const std::lock_guard lock(mutex_);
        if (!running_ || exited_) {
            return "ngspice: the run has ended";
        }
        pausing_ = true; // a callback blocked on a full queue returns, keeping its point
        changed_.notify_all();
    }
    stopThread();
    {
        const std::lock_guard lock(mutex_);
        if (exited_) {
            return "ngspice: the run has ended"; // advance() reports the fatal error
        }
        output_.clear();
    }
    command(*g_api, "alter " + std::string(change));
    std::string why;
    {
        const std::lock_guard lock(mutex_);
        if (reportsError(output_)) {
            why = "ngspice: " + firstError();
        }
        output_.clear(); // a later failure of the run reports its own error, not this one
        pausing_ = false;
        running_ = true; // until ngspice's thread says it ended
    }
    if (command(*g_api, "bg_resume") != 0) {
        const std::lock_guard lock(mutex_);
        running_ = false;
        changed_.notify_all();
        return "ngspice: the run did not resume: " + firstError();
    }
    return why;
}

void NgspiceEngine::unload()
{
    if (!g_api || !owner_) {
        return;
    }
    {
        const std::lock_guard lock(mutex_);
        if (exited_) {
            return; // recover() frees the whole DLL instead
        }
    }
    // Frees the circuit and every stored plot, so repeated runs do not accumulate memory.
    command(*g_api, "remcirc");
    command(*g_api, "destroy all");
}

bool NgspiceEngine::recover()
{
    if (!g_api || !owner_) {
        return true;
    }
    {
        std::unique_lock lock(mutex_);
        if (!exited_) {
            return true;
        }
        // An exit on ngspice's own thread ends that thread. Freeing the DLL while it still runs
        // would pull its code away under it, so ngspice stays dead if the thread does not end.
        if (!changed_.wait_for(lock, kThreadEndTimeout, [&] { return !running_; })) {
            return false;
        }
        exited_ = false;
    }
    // ngspice's C runtime allocates from the process heap, so freeing the DLL alone would leave
    // its memory behind (about 125 KiB per reload); ngSpice_Reset() frees most of it first.
    // Called without the reload, it crashes the next run. The next load() loads ngspice afresh.
    g_api->reset();
    closeLibrary(g_module);
    g_module = nullptr;
    g_api.reset();
    return true;
}

SimChunk NgspiceEngine::advance(double duration)
{
    SimChunk chunk;
    if (!loaded_ || finished_ || !std::isfinite(duration) || !(duration > 0.0)) {
        return chunk;
    }
    end_ += duration;
    chunk.signals.resize(names_.size());

    std::unique_lock lock(mutex_);
    const std::size_t stride = columns_.size();
    for (;;) {
        bool popped = false;
        while (count_ > 0) {
            const double* point = queue_.data() + head_ * stride;
            if (point[0] > end_) {
                break;
            }
            chunk.time.push_back(point[0]);
            for (std::size_t s = 1; s < stride; ++s) {
                chunk.signals[s - 1].push_back(point[s]);
            }
            lastTime_ = point[0];
            head_ = (head_ + 1) % slots_;
            --count_;
            popped = true;
        }
        if (popped) {
            changed_.notify_all(); // room for ngspice
        }
        if (count_ > 0) {
            return chunk; // the next point is after this chunk
        }
        if (!running_ || exited_) {
            break; // the run is over and every point has been given
        }
        changed_.wait(lock, [&] { return count_ > 0 || !running_ || exited_; });
    }

    finished_ = true;
    if (exited_) {
        error_ = errorMessage();
    } else if (lastTime_ >= config_.stopTime * (1 - 1e-9)) {
        error_ = "ngspice: the run reached its stop time of " +
                 core::formatSpiceNumber(config_.stopTime) + "s";
    } else {
        error_ = "ngspice: the run stopped at t = " + core::formatSpiceNumber(lastTime_) +
                 "s: " + firstError();
    }
    return chunk;
}

void NgspiceEngine::reset()
{
    if (!loaded_) {
        return;
    }
    const std::string netlist = netlist_;
    load(netlist);
}

std::string NgspiceEngine::firstError() const
{
    // Called with mutex_ held. ngspice's first error line and the two after it, which often say
    // what the error is about ("Error on line 3 ...:", the line, "could not find a valid
    // modelname"), or all it said.
    constexpr int kLines = 3;
    std::string message;
    int lines = 0;
    std::string_view rest = output_;
    while (!rest.empty() && lines < kLines) {
        const std::size_t eol = rest.find('\n');
        const std::string_view line = rest.substr(0, eol);
        rest = eol == std::string_view::npos ? std::string_view{} : rest.substr(eol + 1);
        if (lines > 0 || reportsError(line)) {
            if (lines > 0) {
                message += ' ';
            }
            message += line;
            ++lines;
        }
    }
    if (!message.empty()) {
        return message;
    }
    std::string all = output_;
    while (!all.empty() && std::isspace(static_cast<unsigned char>(all.back())) != 0) {
        all.pop_back();
    }
    return all.empty() ? "no simulation output" : all;
}

std::string NgspiceEngine::errorMessage() const
{
    // Called with mutex_ held.
    return (exited_ ? "ngspice: fatal error: " : "ngspice: ") + firstError();
}

} // namespace proton::sim
