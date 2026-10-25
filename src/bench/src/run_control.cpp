// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/bench/run_control.h"

#include "proton/schematic/netlist.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace proton::bench {

namespace {

constexpr double kNotYet = -std::numeric_limits<double>::infinity();

} // namespace

RunControl::RunControl(EngineFactory factory, const Config& config)
    : factory_(std::move(factory)), config_(config), fedThrough_(kNotYet)
{
    config_.worker = sim::SimWorker::sanitise(config.worker);
}

RunControl::~RunControl() = default; // the worker stops its run

void RunControl::stop()
{
    wanted_ = false;
    if (worker_) {
        worker_->stop();
    }
    runCircuit_.reset();
    held_.reset();
    state_ = State::Idle;
    error_.clear();
}

void RunControl::pump(double wallNow, double limit)
{
    if (!wanted_) {
        return;
    }
    schematic::Circuit circuit = circuit_;
    bench_.contributeTo(circuit);
    const double maxStep = bench_.maxStep();
    if (!runCircuit_ || circuit != *runCircuit_ || maxStep != runMaxStep_) {
        restart(wallNow, std::move(circuit), maxStep);
    }
    if (state_ == State::Running) {
        feed(wallNow, limit);
    }
}

double RunControl::speed() const
{
    return config_.realTime ? pacer_.speed() : 1.0;
}

void RunControl::restart(double wallNow, schematic::Circuit circuit, double maxStep)
{
    ++restarts_;
    if (worker_) {
        worker_->stop();
    }
    held_.reset();
    fedThrough_ = kNotYet;
    lastTime_ = 0.0;
    error_.clear();
    netlist_.clear();
    diagnostics_ = schematic::validate(circuit);
    for (schematic::Diagnostic& diagnostic : bench_.check(circuit)) {
        diagnostics_.push_back(std::move(diagnostic));
    }
    runCircuit_ = circuit;
    runMaxStep_ = maxStep;
    state_ = State::Running;

    const auto firstError =
        std::ranges::find(diagnostics_, schematic::Severity::Error, &schematic::Diagnostic::severity);
    if (firstError != diagnostics_.end()) {
        state_ = State::Invalid;
        error_ = firstError->message;
        return;
    }
    netlist_ = schematic::buildNetlist(circuit);

    if (!worker_ || maxStep != workerMaxStep_) {
        worker_.reset(); // the old engine goes before the new one is made
        std::string error;
        std::unique_ptr<sim::ISimEngine> engine = factory_(maxStep, error);
        if (!engine) {
            fail(error.empty() ? "no simulator" : error);
            return;
        }
        worker_ = std::make_unique<sim::SimWorker>(std::move(engine), config_.worker);
        workerMaxStep_ = maxStep;
    }
    if (!worker_->start(netlist_)) {
        fail(worker_->error());
        return;
    }
    bench_.restart();
    bench_.attach(worker_->signalNames());
    pacer_.start(wallNow);
}

void RunControl::fail(std::string error)
{
    state_ = State::Failed;
    error_ = std::move(error);
}

void RunControl::feed(double wallNow, double limit)
{
    const double due = config_.realTime ? std::min(limit, pacer_.due(wallNow)) : limit;
    for (std::size_t popped = 0; popped < config_.worker.queueCapacity;) {
        if (!held_) {
            // A run that failed pushed every chunk before saying so: read before popping, so an
            // empty ring after a failure means everything has been fed.
            const bool live = worker_->running();
            held_ = worker_->tryPop();
            next_ = 0;
            if (!held_) {
                if (!live) {
                    fail(worker_->error());
                }
                break;
            }
            ++popped;
        }
        const std::vector<double>& time = held_->time;
        const auto first = time.begin() + static_cast<std::ptrdiff_t>(next_);
        const std::size_t end = static_cast<std::size_t>(std::upper_bound(first, time.end(), due) -
                                                         time.begin());
        bench_.feed(*held_, next_, end);
        if (end > next_) {
            lastTime_ = time[end - 1];
            fedThrough_ = lastTime_;
        }
        next_ = end;
        if (next_ < time.size()) {
            fedThrough_ = due; // the rest is past what is due
            break;
        }
        held_.reset();
    }
    if (config_.realTime) {
        pacer_.fed(wallNow, lastTime_);
    }
}

} // namespace proton::bench
