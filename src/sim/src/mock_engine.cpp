// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/sim/mock_engine.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace proton::sim {

namespace {

constexpr std::uint32_t kSeed = 12345;

double finiteOr(double value, double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

double clampFinite(double value, double fallback, double low, double high)
{
    return std::clamp(finiteOr(value, fallback), low, high);
}

// Advances the lag e = y - x of a one-pole low-pass y' = (x - y) / tau over a step of `dt` in
// which x rises linearly by `dx`. Exact: e' = -e / tau - dx / dt.
double decay(double e, double dt, double dx, double tau)
{
    const double r = dt / tau;
    const double kept = std::exp(-r);
    const double gain = r > 0.0 ? -std::expm1(-r) / r : 1.0; // (1 - kept) / r, also for tiny r
    return e * kept - dx * gain;
}

bool blank(std::string_view text)
{
    return std::ranges::all_of(text, [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
    });
}

} // namespace

MockEngine::Config MockEngine::sanitise(const Config& config)
{
    const Config defaults;
    Config result;
    result.amplitude = clampFinite(config.amplitude, defaults.amplitude, -kMaxVoltage, kMaxVoltage);
    result.offset = clampFinite(config.offset, defaults.offset, -kMaxVoltage, kMaxVoltage);
    result.frequency =
        clampFinite(config.frequency, defaults.frequency, kMinFrequency, kMaxFrequency);
    result.resistance =
        clampFinite(config.resistance, defaults.resistance, kMinResistance, kMaxResistance);
    result.capacitance =
        clampFinite(config.capacitance, defaults.capacitance, kMinCapacitance, kMaxCapacitance);
    result.minStep = clampFinite(config.minStep, defaults.minStep, kMinStep, kMaxStep);
    result.maxStep =
        std::max(result.minStep, clampFinite(config.maxStep, defaults.maxStep, kMinStep, kMaxStep));
    return result;
}

MockEngine::MockEngine(const Config& config)
    : config_(sanitise(config)), tau_(config_.resistance * config_.capacitance)
{}

bool MockEngine::load(std::string_view netlist)
{
    loaded_ = false;
    names_.clear();
    if (blank(netlist)) {
        error_ = "empty netlist";
        return false;
    }
    error_.clear();
    names_ = {kInputSignal, kOutputSignal};
    loaded_ = true;
    reset();
    return true;
}

SimChunk MockEngine::advance(double duration)
{
    SimChunk chunk;
    if (!loaded_ || !std::isfinite(duration) || !(duration > 0.0)) {
        return chunk;
    }
    chunk.signals.resize(names_.size());
    std::vector<double>& in = chunk.signals[0];
    std::vector<double>& out = chunk.signals[1];

    end_ += duration;
    while (nextTime_ <= end_) {
        const double t = nextTime_;
        const double x = input(t);
        if (started_) {
            lag_ = decay(lag_, t - lastTime_, x - lastInput_, tau_);
        }
        started_ = true;
        lastTime_ = t;
        lastInput_ = x;
        chunk.time.push_back(t);
        in.push_back(x);
        out.push_back(x + lag_);
        nextTime_ = t + nextStep();
    }
    return chunk;
}

void MockEngine::reset()
{
    end_ = 0.0;
    nextTime_ = 0.0;
    started_ = false;
    lastTime_ = 0.0;
    lastInput_ = 0.0;
    lag_ = 0.0; // the DC operating point: v(out) = v(in)
    random_ = kSeed;
}

double MockEngine::input(double t) const
{
    return config_.offset +
           config_.amplitude * std::sin(2.0 * std::numbers::pi * config_.frequency * t);
}

double MockEngine::nextStep()
{
    // A 32-bit linear congruential generator: integer arithmetic, the same on every platform.
    random_ = random_ * 1664525u + 1013904223u;
    const double u = static_cast<double>(random_ >> 8) / 16777216.0; // [0, 1)
    return config_.minStep + (config_.maxStep - config_.minStep) * u;
}

} // namespace proton::sim
