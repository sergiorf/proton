// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/sim/sim_engine.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace proton::sim {

// A deterministic stand-in for a SPICE engine, for tests, the headless demo and CI
// (ARCHITECTURE.md section 8). It does not read the netlist's elements: it always simulates a
// sine source driving an RC low-pass,
//
//     v(in)  = offset + amplitude x sin(2 pi frequency t)
//     v(out) = v(in) through R in series and C to ground,
//
// starting, as SPICE does, from the DC operating point: v(out) = v(in) = offset at t = 0.
//
// Like SPICE it picks its own uneven time steps: each step is drawn between minStep and maxStep
// from a fixed pseudo-random sequence that starts again at load() and reset(). The grid does not
// depend on how the run is split into advance() calls, and the same settings and calls give
// bit-identical chunks on every run.
//
// v(out) is advanced over each step with the exact response of the RC to an input that is linear
// across the step, so it differs from the analytic answer only by that linearisation, which
// shrinks with the square of the step.
//
// For tests of error handling, the run fails when it passes `failAt` seconds of simulation time
// (never, by default): advance() then gives the points up to failAt and error() is kFailure.
//
// Settings are sanitised: non-finite numbers become the default, numbers are clamped to the
// ranges given by the constants below, maxStep is at least minStep, and failAt is at least 0.
class MockEngine final : public ISimEngine
{
public:
    static constexpr const char* kInputSignal = "v(in)";
    static constexpr const char* kOutputSignal = "v(out)";
    static constexpr const char* kFailure = "mock engine: simulated failure";

    static constexpr double kMaxVoltage = 1e3;    // V, amplitude and offset, either way
    static constexpr double kMinFrequency = 1e-3; // Hz
    static constexpr double kMaxFrequency = 1e9;
    static constexpr double kMinResistance = 1e-3; // ohm
    static constexpr double kMaxResistance = 1e12;
    static constexpr double kMinCapacitance = 1e-15; // F
    static constexpr double kMaxCapacitance = 1.0;
    static constexpr double kMinStep = 1e-12; // s
    static constexpr double kMaxStep = 1.0;

    struct Config
    {
        double amplitude = 1.0;    // V peak
        double offset = 0.0;       // V
        double frequency = 1e3;    // Hz
        double resistance = 1e3;   // ohm
        double capacitance = 1e-7; // F: with 1 kohm, tau = 100 us, corner 1.59 kHz
        double minStep = 1e-6;     // s
        double maxStep = 1e-5;     // s
        double failAt = std::numeric_limits<double>::infinity(); // s

        friend bool operator==(const Config&, const Config&) = default;
    };

    // The settings as the engine would apply them.
    static Config sanitise(const Config& config);

    MockEngine() : MockEngine(Config{}) {}
    explicit MockEngine(const Config& config);

    const Config& config() const { return config_; }

    // Any netlist with text in it loads; an empty or blank one is an error.
    bool load(std::string_view netlist) override;
    const std::string& error() const override { return error_; }
    const std::vector<std::string>& signalNames() const override { return names_; }
    SimChunk advance(double duration) override;
    void reset() override;

private:
    double input(double t) const;
    double nextStep();

    Config config_;
    double tau_ = 0.0;
    std::string error_;
    std::vector<std::string> names_;
    bool loaded_ = false;
    bool failed_ = false;

    double end_ = 0.0;      // the run has been simulated up to here
    double nextTime_ = 0.0; // the next grid point
    bool started_ = false;  // the point at t = 0 has been given
    double lastTime_ = 0.0;
    double lastInput_ = 0.0;
    double lag_ = 0.0; // v(out) - v(in) at lastTime_
    std::uint32_t random_ = 0;
};

} // namespace proton::sim
