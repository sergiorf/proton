// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/sim/sim_engine.h"

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
// A run ends at stopTime: advance() gives the points up to it, then error() is "... reached its
// stop time ..." and the run is over. A run that ngspice abandons (a singular matrix, a time step
// too small) gives the points before the failure and ngspice's error message.
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

    struct Config
    {
        double stopTime = 10.0;           // s of simulation time; the run ends there
        double maxStep = 1e-6;            // s, the longest step ngspice may take
        std::size_t queueCapacity = 4096; // time points ngspice may run ahead of advance()
        // The DLL to load, UTF-8. Empty: ngspice.dll in the directory of the running executable.
        // Only the first engine to load ngspice uses it: the DLL then stays for the process.
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

    // The data callbacks of ngspice's thread; called only from the .cpp file.
    struct Callbacks;

private:
    bool start();
    void halt();
    void unload();
    std::string firstError() const;

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
    std::size_t head_ = 0;              // first queued point
    std::size_t count_ = 0;             // queued points
    std::vector<int> columns_;          // ngspice vector index for time, then for each signal
    std::vector<std::string> runNames_; // the signals of this run, set by its first point
    bool initialised_ = false;          // the first point of this run set columns_ and runNames_
    bool running_ = false;              // ngspice's background thread runs
    bool halting_ = false;              // drop points and do not block: the run is being halted
    bool haveTime_ = false;             // queuedTime_ holds the time of the last queued point
    double queuedTime_ = 0.0;
    std::string output_;  // ngspice's stderr since the run started
    bool exited_ = false; // ngspice asked to exit; it is unusable until reloaded (M1.4)
};

} // namespace proton::sim
