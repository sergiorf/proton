// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/sim/sim_engine.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace proton::sim {

// ngspice through its shared-library API (ARCHITECTURE.md section 8, ADR 0002). Windows only, and
// built only when ngspice was fetched.
//
// ngspice.dll is loaded at runtime on the first load(), so a program that never uses this engine
// does not need the DLL, and a missing DLL is a load() error. load() sends the netlist line by line
// with a `.tran` analysis to stopTime, and starts the run on ngspice's background thread. Its data
// callback copies every accepted time point into a bounded queue and blocks while the queue is
// full, so ngspice runs at most queueCapacity points ahead of advance(). Points whose time is not
// after the previous one are dropped, so chunk times are strictly increasing.
//
// ngspice keeps its circuit in process-wide state, so only one NgspiceEngine may exist at a time:
// while one does, every other one fails to load(). Like every engine it is used from one thread at
// a time; the queue is the only thing it shares with ngspice's thread.
//
// Signal names follow SPICE: node `out` is "v(out)", the current through voltage source `V1` is
// "i(v1)", all lower case, in ngspice's order.
//
// Halting. ngspice's own `bg_halt` sleeps 100 ms on Windows before it looks at its thread (ADR
// 0002), so the engine does not use it to stop a run. It adds a voltage source of its own to every
// netlist, `Vproton_halt proton_halt 0 DC 0 EXTERNAL`, on a node of its own, and a breakpoint
// `stop when v(proton_halt) > 0.5`. To halt, it raises that source through ngspice's external
// source callback: ngspice stops at its next accepted point and its thread ends, in well under a
// millisecond (ADR 0005). The node touches nothing else, so the points of the circuit are the
// same as without it; its two vectors are left out of the signals. `bg_halt` remains the fallback
// if the thread does not stop within kHaltTimeout.
//
// A run ends at stopTime: advance() gives the points up to it, then error() is "... reached its
// stop time ..." and the run is over. A run that ngspice abandons (a singular matrix, a time step
// too small) gives the points before the failure and ngspice's error message.
//
// Some errors are fatal to ngspice (a missing .include file, unmatched .subckt and .ends, a
// .param it cannot evaluate): it calls its exit callback and its state is unusable until the DLL
// is reloaded (ADR 0002). The load() or advance() that meets one fails with "ngspice: fatal
// error: ..." and ngspice's message; the next load() frees the DLL and loads it afresh.
//
// Settings are sanitised: non-finite numbers become the default, numbers are clamped to the ranges
// given by the constants below, and maxStep is at most stopTime.
class NgspiceEngine final : public ISimEngine
{
public:
    static constexpr double kMinStopTime = 1e-9; // s
    static constexpr double kMaxStopTime = 1e6;
    static constexpr double kMinMaxStep = 1e-15; // s
    static constexpr double kMaxMaxStep = 1.0;
    static constexpr std::size_t kMinQueueCapacity = 1; // points
    static constexpr std::size_t kMaxQueueCapacity = std::size_t{1} << 20;
    // The source and node the engine adds to halt ngspice: no netlist may use these names.
    static constexpr const char* kHaltSource = "Vproton_halt";
    static constexpr const char* kHaltNode = "proton_halt";

    struct Config
    {
        double stopTime = 10.0;           // s of simulation time; the run ends there
        double maxStep = 1e-6;            // s, the longest step ngspice may take
        std::size_t queueCapacity = 4096; // time points ngspice may run ahead of advance()
        // The DLL to load, UTF-8. Empty: ngspice.dll in the directory of the running executable.
        // Used when the DLL is not loaded yet: it then stays loaded for the process, until a
        // fatal error unloads it.
        std::string libraryPath;

        friend bool operator==(const Config&, const Config&) = default;
    };

    // The settings as the engine would apply them.
    static Config sanitise(const Config& config);

    NgspiceEngine() : NgspiceEngine(Config{}) {}
    explicit NgspiceEngine(const Config& config);
    ~NgspiceEngine() override; // halts the run and frees ngspice's circuit

    const Config& config() const { return config_; }

    bool load(std::string_view netlist) override;
    const std::string& error() const override { return error_; }
    const std::vector<std::string>& signalNames() const override { return names_; }
    SimChunk advance(double duration) override;
    void reset() override;

    // A live change in place, the prototype measured for decision D3, which chose restarting
    // instead (ADR 0005), so nothing uses it yet. Pauses the run at its next point, runs
    // ngspice's `alter <change>` (for example "@vgen1[sin] = [ 500m 1 2k ]") and resumes it. The
    // run goes on from where it was with the new value: advance() loses and repeats no point.
    // Returns an empty string if it worked, otherwise why not; the run goes on either way, unless
    // it had already ended.
    std::string alter(std::string_view change);

    // The data callbacks of ngspice's thread; called only from the .cpp file.
    struct Callbacks;

private:
    bool start();
    void halt();
    void stopThread();
    void unload();
    bool recover();
    std::string firstError() const;
    std::string errorMessage() const;

    Config config_;
    bool owner_ = false; // this is the one instance
    std::string netlist_;
    std::string error_;
    std::vector<std::string> names_;
    bool loaded_ = false;
    bool finished_ = false; // the run ended; advance() gives no more points
    double end_ = 0.0;      // advance() has given every point up to here
    double lastTime_ = 0.0; // of the last point given

    // Shared with ngspice's thread, under mutex_.
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<double> queue_;         // stride 1 + names_.size(): time, then each signal
    std::size_t slots_ = 0;             // points queue_ holds; queueCapacity unless it grew
    std::size_t head_ = 0;              // first queued point
    std::size_t count_ = 0;             // queued points
    std::vector<int> columns_;          // ngspice vector index for time, then for each signal
    std::vector<std::string> runNames_; // the signals of this run, set by its first point
    bool initialised_ = false;          // the first point of this run set columns_ and runNames_
    bool running_ = false;              // ngspice's background thread runs
    bool halting_ = false;              // drop points and do not block: the run is being halted
    bool pausing_ = false;  // keep points without blocking, growing the queue: alter() pauses
    bool haveTime_ = false; // queuedTime_ holds the time of the last queued point
    double queuedTime_ = 0.0;
    std::string output_;  // ngspice's stderr since the run started
    bool exited_ = false; // ngspice asked to exit; it is unusable until recover() reloads it

    // Read by ngspice's thread on every evaluation of the halt source, so not under mutex_.
    std::atomic<bool> haltSource_{false};
};

} // namespace proton::sim
