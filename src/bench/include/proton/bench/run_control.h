// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/bench/bench.h"
#include "proton/schematic/circuit.h"
#include "proton/schematic/validate.h"
#include "proton/sim/real_time_pacer.h"
#include "proton/sim/sim_chunk.h"
#include "proton/sim/sim_engine.h"
#include "proton/sim/sim_worker.h"

#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace proton::bench {

// Runs a circuit with the bench's instruments (ARCHITECTURE.md sections 4 and 5.1): it owns the
// Bench, the engine on its SimWorker thread, and the pacing to wall-clock time (decision D6, ADR
// 0007). All of it is for one thread, the UI thread; the only thing that crosses to the simulation
// thread is the worker's ring.
//
// Changes are declarative. setCircuit() and bench().configure() only record what the bench should
// run; the next pump() compares that with the running run and restarts when the circuit with the
// bench's contributions, or the engine's longest step, differs (decision D3, ADR 0005). So any
// number of changes between two pumps cost one restart: a knob dragged across a UI tick restarts
// once. Changes that do neither (a trigger level, a moved probe) apply to the running run.
//
// A restart validates the circuit, builds its netlist and starts it from t = 0 on the worker. The
// worker and its engine are kept from run to run while the longest step stays the same, and
// otherwise made anew by the engine factory, after the old engine is gone (ngspice allows one
// engine per process).
//
// Wall-clock time is passed in as seconds from any fixed epoch, so RunControl reads no clock and
// is tested with any time it is given. With realTime off, the output is fed as fast as it comes.
class RunControl
{
public:
    // Makes the engine for a run whose longest step is `maxStep` seconds, or returns nullptr and
    // sets `error` to why there is none ("this build has no ngspice").
    using EngineFactory =
        std::function<std::unique_ptr<sim::ISimEngine>(double maxStep, std::string& error)>;

    enum class State {
        Idle,    // not started, or stopped
        Invalid, // validate() found errors: diagnostics() says which, nothing runs
        Failed,  // the engine could not be made, the netlist did not load, or the run failed
        Running,
    };

    struct Config
    {
        sim::SimWorker::Config worker;
        bool realTime = true; // feed the output in step with wall-clock time
    };

    explicit RunControl(EngineFactory factory) : RunControl(std::move(factory), Config{}) {}
    RunControl(EngineFactory factory, const Config& config);
    ~RunControl(); // stops the run

    RunControl(const RunControl&) = delete;
    RunControl& operator=(const RunControl&) = delete;

    // The bench and its instruments; configure() it to turn knobs or move probes, pop its frames
    // and readings after pump().
    Bench& bench() { return bench_; }
    const Bench& bench() const { return bench_; }

    // The circuit to run, without the instruments; they are added by the bench.
    void setCircuit(schematic::Circuit circuit) { circuit_ = std::move(circuit); }
    const schematic::Circuit& circuit() const { return circuit_; }

    // Runs from the next pump() on, until stop(). Stopping ends the run; starting again restarts.
    void start() { wanted_ = true; }
    void stop();

    // Applies the changes since the last pump (one restart at most), then feeds the bench the
    // output of the run that is due at `wallNow`, up to simulation time `limit`. Output past
    // either waits for a later pump. Feeds at most a ring of chunks per call, so it always
    // returns. While running it notices the end of a failed run, after feeding what came before.
    void pump(double wallNow, double limit = std::numeric_limits<double>::infinity());

    State state() const { return state_; }
    // Why the state is Invalid (the first error) or Failed. Empty otherwise.
    const std::string& error() const { return error_; }
    // validate()'s findings for the circuit of the last restart, warnings included.
    const std::vector<schematic::Diagnostic>& diagnostics() const { return diagnostics_; }
    // The netlist of the last restart; empty when it was Invalid.
    const std::string& netlist() const { return netlist_; }
    // The probes of the running run whose nets the simulation does not have.
    const std::vector<ProbeError>& probeErrors() const { return bench_.probeErrors(); }

    // Every point of the run up to this simulation time has been fed; -infinity before the first.
    double fedThrough() const { return fedThrough_; }
    // Simulation time of the last point fed, 0 before the first.
    double lastTime() const { return lastTime_; }
    // The pacer's simulation speed as a fraction of real time (RealTimePacer::speed); 1 with
    // realTime off.
    double speed() const;
    // Restarts so far, failed ones included: a count that tests and the UI can watch.
    std::size_t restarts() const { return restarts_; }

private:
    void restart(double wallNow, schematic::Circuit circuit, double maxStep);
    void fail(std::string error);
    void feed(double wallNow, double limit);

    EngineFactory factory_;
    Config config_;
    Bench bench_;
    schematic::Circuit circuit_;
    bool wanted_ = false;

    // What the last restart ran: the circuit with the bench's contributions, and the step.
    std::optional<schematic::Circuit> runCircuit_;
    double runMaxStep_ = 0.0;

    State state_ = State::Idle;
    std::string error_;
    std::vector<schematic::Diagnostic> diagnostics_;
    std::string netlist_;
    std::size_t restarts_ = 0;

    std::unique_ptr<sim::SimWorker> worker_;
    double workerMaxStep_ = 0.0; // the step worker_'s engine was made for
    sim::RealTimePacer pacer_;
    std::optional<sim::SimChunk> held_; // a chunk partly fed; the rest is not due yet
    std::size_t next_ = 0;              // first point of held_ not fed
    double fedThrough_ = 0.0;
    double lastTime_ = 0.0;
};

} // namespace proton::bench
