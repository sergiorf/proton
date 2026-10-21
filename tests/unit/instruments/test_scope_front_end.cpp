// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/scope_front_end.h"
#include "proton/test/test.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

using proton::instruments::ScopeFrontEnd;
using proton::instruments::ScopeSamples;
using Config = ScopeFrontEnd::Config;
using ChannelConfig = ScopeFrontEnd::ChannelConfig;
using Coupling = ScopeFrontEnd::Coupling;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kPi = std::numbers::pi;

struct Signal
{
    std::vector<double> time;
    std::vector<double> values;
};

// Deterministic pseudo-random numbers in [0, 1).
class Random
{
public:
    double next()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<double>(state_ >> 8) / 16777216.0;
    }

private:
    std::uint32_t state_ = 12345;
};

// Samples `f` on [start, end] with random steps between `minStep` and `maxStep`.
Signal sampleIrregular(const std::function<double(double)>& f, double start, double end,
                       double minStep, double maxStep)
{
    Signal signal;
    Random random;
    for (double t = start; t < end; t += minStep + (maxStep - minStep) * random.next()) {
        signal.time.push_back(t);
        signal.values.push_back(f(t));
    }
    signal.time.push_back(end);
    signal.values.push_back(f(end));
    return signal;
}

// Samples `f` at the first `count` sample times of a front end that starts at t = 0, with up to
// three random points between each two, so the grid is irregular but holds every sample time.
Signal sampleOnClock(const std::function<double(double)>& f, double interval, int count)
{
    Signal signal;
    Random random;
    for (int k = 0; k < count; ++k) {
        const double t = static_cast<double>(k) * interval;
        signal.time.push_back(t);
        signal.values.push_back(f(t));
        const int extra = static_cast<int>(4.0 * random.next());
        for (int j = 0; j < extra; ++j) {
            const double between = t + interval * (0.2 + 0.2 * static_cast<double>(j));
            signal.time.push_back(between);
            signal.values.push_back(f(between));
        }
    }
    return signal;
}

// A unit step: 0 at t = 0, rising linearly to 1 over kRamp, then 1 on a random grid up to `end`.
constexpr double kRamp = 1e-12;

Signal unitStep(double end, double minStep, double maxStep)
{
    Signal signal = sampleIrregular([](double) { return 1.0; }, kRamp, end, minStep, maxStep);
    signal.time.insert(signal.time.begin(), 0.0);
    signal.values.insert(signal.values.begin(), 0.0);
    return signal;
}

// The error state e = z - x at time t of a one-pole low-pass z with time constant tau, driven by
// unitStep(): e(kRamp) is the exact response to the ramp, then e decays as exp(-t / tau).
double stepState(double tau, double t)
{
    const double r = kRamp / tau;
    return std::expm1(-r) / r * std::exp(-(t - kRamp) / tau);
}

double tau(double corner)
{
    return 1.0 / (2.0 * kPi * corner);
}

Config singleChannel(double timePerDiv, const ChannelConfig& channel)
{
    Config config;
    config.timePerDiv = timePerDiv;
    config.channels[0] = channel;
    return config;
}

ChannelConfig channelWith(Coupling coupling, bool bandwidthLimit, double bandwidth = 20e6)
{
    ChannelConfig channel;
    channel.coupling = coupling;
    channel.bandwidthLimit = bandwidthLimit;
    channel.bandwidth = bandwidth;
    return channel;
}

ChannelConfig quantised(double voltsPerDiv, double offset, int adcBits)
{
    ChannelConfig channel;
    channel.voltsPerDiv = voltsPerDiv;
    channel.offset = offset;
    channel.quantise = true;
    channel.adcBits = adcBits;
    return channel;
}

// Feeds a signal into channel 1, channel 2 left without a probe.
std::vector<double> run(ScopeFrontEnd& scope, const Signal& signal)
{
    scope.feed(signal.time, signal.values, {});
    return scope.takeSamples().values[0];
}

double maxAbs(const std::vector<double>& values)
{
    double result = 0.0;
    for (const double v : values) {
        result = std::max(result, std::fabs(v));
    }
    return result;
}

} // namespace

TEST(options_off_pass_the_input_through_exactly)
{
    ScopeFrontEnd scope; // DC, no bandwidth limit, no quantisation
    const auto f = [](double t) { return 0.7 + 2.5 * std::sin(2 * kPi * 300.0 * t); };
    const Signal signal = sampleOnClock(f, scope.sampleInterval(), 1001);
    const std::vector<double> samples = run(scope, signal);
    CHECK_EQ(samples.size(), 1001u);
    for (std::size_t k = 0; k < samples.size(); ++k) {
        CHECK_EQ(samples[k], f(static_cast<double>(k) * scope.sampleInterval()));
    }
}

TEST(between_points_the_input_is_linear)
{
    ScopeFrontEnd scope;
    const double time[] = {0.0, 0.01};
    const double values[] = {0.0, 1.0};
    scope.feed(time, values, {});
    const std::vector<double> samples = scope.takeSamples().values[0];
    CHECK(samples.size() >= 1000u);
    for (std::size_t k = 0; k < samples.size(); ++k) {
        CHECK_NEAR(samples[k], scope.sampleTime(static_cast<long long>(k)) / 0.01, 1e-14);
    }
}

TEST(sample_rate_follows_the_timebase)
{
    for (const double timePerDiv : {1e-3, 2e-6, 0.5}) {
        Config config;
        config.timePerDiv = timePerDiv;
        const ScopeFrontEnd scope(config);
        // fs = N / (10 x time/div)
        CHECK_NEAR(1.0 / scope.sampleInterval(),
                   ScopeFrontEnd::kSamplesPerScreen / (10.0 * timePerDiv), 1e-9 / timePerDiv);
    }
}

TEST(sample_times_count_from_the_first_point)
{
    ScopeFrontEnd scope;
    const double dt = scope.sampleInterval();
    const double time[] = {0.25, 0.25 + 2.5 * dt};
    const double values[] = {1.0, 1.0};
    scope.feed(time, values, {});
    const ScopeSamples samples = scope.takeSamples();
    CHECK_EQ(samples.first, 0);
    CHECK_EQ(samples.values[0].size(), 3u); // at 0, dt and 2 dt after the first point
    CHECK_EQ(scope.sampleTime(0), 0.25);
    CHECK_NEAR(scope.sampleTime(2), 0.25 + 2.0 * dt, 1e-16);
}

TEST(ac_coupling_blocks_dc)
{
    ScopeFrontEnd scope(singleChannel(1e-3, channelWith(Coupling::Ac, false)));
    const std::vector<double> samples =
        run(scope, sampleIrregular([](double) { return 3.0; }, 0.0, 0.01, 1e-7, 1e-4));
    CHECK(samples.size() >= 1000u);
    CHECK_EQ(maxAbs(samples), 0.0);
}

TEST(ac_coupling_step_response_matches_closed_form)
{
    // High-pass: y = exp(-t / a) for a unit step, a = 1 / (2 pi 10 Hz).
    const double a = tau(ScopeFrontEnd::kAcCouplingCorner);
    ScopeFrontEnd scope(singleChannel(10e-3, channelWith(Coupling::Ac, false)));
    const std::vector<double> samples = run(scope, unitStep(0.1, 1e-6, 5e-3));
    CHECK(samples.size() >= 1000u);
    CHECK_EQ(samples[0], 0.0);
    for (std::size_t k = 1; k < samples.size(); ++k) {
        const double t = scope.sampleTime(static_cast<long long>(k));
        CHECK_NEAR(samples[k], -stepState(a, t), 1e-13);
        CHECK_NEAR(samples[k], std::exp(-t / a), 1e-9);
    }
}

TEST(bandwidth_limit_step_response_matches_closed_form)
{
    // Low-pass: y = 1 - exp(-t / b) for a unit step, b = 1 / (2 pi 1 kHz).
    const double b = tau(1e3);
    ScopeFrontEnd scope(singleChannel(100e-6, channelWith(Coupling::Dc, true, 1e3)));
    const std::vector<double> samples = run(scope, unitStep(1e-3, 1e-8, 5e-5));
    CHECK(samples.size() >= 1000u);
    for (std::size_t k = 1; k < samples.size(); ++k) {
        const double t = scope.sampleTime(static_cast<long long>(k));
        CHECK_NEAR(samples[k], 1.0 + stepState(b, t), 1e-13);
        CHECK_NEAR(samples[k], 1.0 - std::exp(-t / b), 1e-8);
    }
}

TEST(ac_coupling_with_bandwidth_limit_matches_closed_form)
{
    // Band-pass from the two poles: y = a / (a - b) (exp(-t / a) - exp(-t / b)).
    const double a = tau(ScopeFrontEnd::kAcCouplingCorner);
    const double b = tau(1e3);
    ScopeFrontEnd scope(singleChannel(10e-3, channelWith(Coupling::Ac, true, 1e3)));
    const std::vector<double> samples = run(scope, unitStep(0.1, 1e-7, 5e-3));
    CHECK(samples.size() >= 1000u);
    for (std::size_t k = 1; k < samples.size(); ++k) {
        const double t = scope.sampleTime(static_cast<long long>(k));
        CHECK_NEAR(samples[k], a / (a - b) * (stepState(b, t) - stepState(a, t)), 1e-13);
        CHECK_NEAR(samples[k], a / (a - b) * (std::exp(-t / a) - std::exp(-t / b)), 1e-8);
    }
}

TEST(a_sine_above_nyquist_aliases)
{
    // 1 ms/div samples at 100 kHz, so a 99 kHz sine shows as 1 kHz, phase inverted:
    // sin(2 pi (fs - 1 kHz) k / fs) = -sin(2 pi 1 kHz k / fs).
    ScopeFrontEnd scope;
    CHECK_NEAR(1.0 / scope.sampleInterval(), 100e3, 1e-6);
    const auto sine = [](double t) { return std::sin(2 * kPi * 99e3 * t); };
    // The input is linear between points; steps of at most 20 ns keep that within 3e-5.
    const std::vector<double> samples = run(scope, sampleIrregular(sine, 0.0, 2e-3, 2e-9, 2e-8));
    CHECK(samples.size() >= 200u);
    for (std::size_t k = 0; k < samples.size(); ++k) {
        const double t = scope.sampleTime(static_cast<long long>(k));
        CHECK_NEAR(samples[k], -std::sin(2 * kPi * 1e3 * t), 1e-4);
    }
}

TEST(quantised_samples_are_exact_multiples_of_the_lsb)
{
    for (const double voltsPerDiv : {1.0, 0.3}) {
        ScopeFrontEnd scope(singleChannel(1e-3, quantised(voltsPerDiv, 0.0, 8)));
        const double lsb = 8.0 * voltsPerDiv / 256.0;
        const auto f = [voltsPerDiv](double t) {
            return 3.0 * voltsPerDiv * std::sin(2 * kPi * 230.0 * t);
        };
        const std::vector<double> samples =
            run(scope, sampleOnClock(f, scope.sampleInterval(), 1001));
        CHECK_EQ(samples.size(), 1001u);
        for (std::size_t k = 0; k < samples.size(); ++k) {
            CHECK_EQ(std::round(samples[k] / lsb) * lsb, samples[k]);
            const double input = f(static_cast<double>(k) * scope.sampleInterval());
            CHECK(std::fabs(samples[k] - input) <= 0.5 * lsb * (1.0 + 1e-12));
        }
    }
}

TEST(quantisation_steps_count_from_the_offset)
{
    ScopeFrontEnd scope(singleChannel(1e-3, quantised(0.5, 1.2, 6)));
    const double lsb = 8.0 * 0.5 / 64.0;
    const std::vector<double> samples = run(
        scope, sampleIrregular([](double t) { return 1.2 + std::sin(2 * kPi * 100.0 * t); }, 0.0,
                               0.01, 1e-6, 1e-5));
    for (const double s : samples) {
        const double steps = (s - 1.2) / lsb;
        CHECK_NEAR(steps, std::round(steps), 1e-9);
    }
}

TEST(quantised_full_scale_is_the_lowest_and_highest_code)
{
    // 4 bits over 8 divisions of 1 V: codes -8 .. 7, so -4 V .. 3.5 V.
    ScopeFrontEnd scope(singleChannel(1e-3, quantised(1.0, 0.0, 4)));
    const double time[] = {0.0, scope.sampleInterval()};
    const double values[] = {-50.0, 50.0};
    scope.feed(time, values, {});
    const std::vector<double> samples = scope.takeSamples().values[0];
    CHECK_EQ(samples.size(), 2u);
    CHECK_EQ(samples[0], -4.0);
    CHECK_EQ(samples[1], 3.5);
}

TEST(samples_are_clipped_to_the_screen)
{
    // 1 V/div with 2 V at the centre: the 8-division screen spans -2 V .. 6 V.
    ChannelConfig channel;
    channel.offset = 2.0;
    ScopeFrontEnd scope(singleChannel(1e-3, channel));
    const auto f = [](double t) { return 10.0 * std::sin(2 * kPi * 100.0 * t); };
    const std::vector<double> samples = run(scope, sampleOnClock(f, scope.sampleInterval(), 1001));
    CHECK_EQ(*std::min_element(samples.begin(), samples.end()), -2.0);
    CHECK_EQ(*std::max_element(samples.begin(), samples.end()), 6.0);
    for (std::size_t k = 0; k < samples.size(); ++k) {
        const double input = f(static_cast<double>(k) * scope.sampleInterval());
        CHECK_EQ(samples[k], std::clamp(input, -2.0, 6.0));
    }
}

TEST(channels_are_independent)
{
    Config config;
    config.channels[1] = channelWith(Coupling::Ac, false);
    config.channels[1].voltsPerDiv = 0.1;
    ScopeFrontEnd scope(config);
    const double dt = scope.sampleInterval();
    const double time[] = {0.0, dt, 2.0 * dt};
    const double ch1[] = {1.0, 1.0, 1.0};
    const double ch2[] = {5.0, 5.0, 5.1};
    scope.feed(time, ch1, ch2);
    const ScopeSamples samples = scope.takeSamples();
    CHECK_EQ(samples.values[0].size(), 3u);
    CHECK_EQ(samples.values[1].size(), 3u);
    CHECK_EQ(samples.values[0][2], 1.0);
    CHECK_EQ(samples.values[1][1], 0.0); // DC blocked
    CHECK_NEAR(samples.values[1][2], 0.1, 1e-4); // the 100 mV step passes, decaying slowly
}

TEST(a_channel_without_a_probe_reads_zero)
{
    ScopeFrontEnd scope;
    const double time[] = {0.0, scope.sampleInterval()};
    const double values[] = {1.0, 2.0};
    scope.feed(time, {}, values);
    const ScopeSamples samples = scope.takeSamples();
    CHECK_EQ(samples.values[0].size(), 2u);
    CHECK_EQ(maxAbs(samples.values[0]), 0.0);
    CHECK_EQ(samples.values[1][1], 2.0);
}

TEST(scope_chunking_does_not_change_samples)
{
    Config config;
    config.timePerDiv = 2e-3;
    config.channels[0] = channelWith(Coupling::Ac, true, 2e3);
    config.channels[0].quantise = true;
    config.channels[1] = channelWith(Coupling::Dc, true, 5e3);
    const auto f = [](double t) { return 0.4 + std::sin(2 * kPi * 400.0 * t); };
    const Signal signal = sampleIrregular(f, 0.0, 0.05, 1e-7, 3e-4);

    ScopeFrontEnd whole(config);
    whole.feed(signal.time, signal.values, signal.values);
    const ScopeSamples expected = whole.takeSamples();

    ScopeFrontEnd chunked(config);
    std::vector<double> got0;
    std::vector<double> got1;
    const std::span<const double> time(signal.time);
    const std::span<const double> values(signal.values);
    for (std::size_t i = 0; i < time.size(); i += 37) {
        const std::size_t n = std::min<std::size_t>(37, time.size() - i);
        chunked.feed(time.subspan(i, n), values.subspan(i, n), values.subspan(i, n));
        const ScopeSamples part = chunked.takeSamples();
        CHECK_EQ(part.first, static_cast<long long>(got0.size()));
        got0.insert(got0.end(), part.values[0].begin(), part.values[0].end());
        got1.insert(got1.end(), part.values[1].begin(), part.values[1].end());
    }
    CHECK(got0 == expected.values[0]);
    CHECK(got1 == expected.values[1]);
    CHECK(got0.size() >= 2500u);
}

TEST(scope_skips_points_that_do_not_move_time_forward)
{
    ScopeFrontEnd scope;
    const double dt = scope.sampleInterval();
    const double time[] = {kNaN, 0.0, dt, dt, 0.5 * dt, kNaN, 2.0 * dt, kInf, -kInf};
    const double values[] = {50.0, 1.0, 1.0, 50.0, 50.0, 50.0, 1.0, 50.0, 50.0};
    scope.feed(time, values, {});
    const std::vector<double> samples = scope.takeSamples().values[0];
    CHECK_EQ(samples.size(), 3u);
    CHECK_EQ(maxAbs(samples), 1.0);
    CHECK_EQ(scope.sampleTime(0), 0.0);
}

TEST(scope_configure_restarts_acquisition)
{
    ScopeFrontEnd scope;
    scope.feed(std::vector<double>{0.0, 0.01}, std::vector<double>{1.0, 1.0}, {});
    scope.configure(singleChannel(1e-3, channelWith(Coupling::Ac, false)));
    const ScopeSamples dropped = scope.takeSamples();
    CHECK(dropped.values[0].empty());
    // The clock and the AC coupling start again at the next point: 7 V is its steady state.
    scope.feed(std::vector<double>{0.5, 0.5 + scope.sampleInterval()},
               std::vector<double>{7.0, 7.0}, {});
    const ScopeSamples samples = scope.takeSamples();
    CHECK_EQ(samples.first, 0);
    CHECK_EQ(samples.values[0].size(), 2u);
    CHECK_EQ(maxAbs(samples.values[0]), 0.0);
    CHECK_EQ(scope.sampleTime(0), 0.5);
}

TEST(scope_defaults_are_ideal)
{
    const ScopeFrontEnd scope;
    CHECK_EQ(scope.config().timePerDiv, 1e-3);
    for (const ChannelConfig& channel : scope.config().channels) {
        CHECK_EQ(channel.voltsPerDiv, 1.0);
        CHECK_EQ(channel.offset, 0.0);
        CHECK_EQ(channel.coupling, Coupling::Dc);
        CHECK(!channel.bandwidthLimit);
        CHECK(!channel.quantise);
    }
}

TEST(scope_settings_are_sanitised)
{
    Config config;
    config.timePerDiv = 0.0;
    ChannelConfig& c = config.channels[0];
    c.voltsPerDiv = 1e6;
    c.offset = kNaN;
    c.coupling = static_cast<Coupling>(5);
    c.bandwidth = 1.0;
    c.adcBits = 64;
    config.channels[1].voltsPerDiv = -1.0;
    config.channels[1].offset = -1e9;
    config.channels[1].bandwidth = kInf;
    config.channels[1].adcBits = 0;

    const Config s = ScopeFrontEnd::sanitise(config);
    CHECK_EQ(s.timePerDiv, ScopeFrontEnd::kMinTimePerDiv);
    CHECK_EQ(s.channels[0].voltsPerDiv, ScopeFrontEnd::kMaxVoltsPerDiv);
    CHECK_EQ(s.channels[0].offset, 0.0);
    CHECK_EQ(s.channels[0].coupling, Coupling::Dc);
    CHECK_EQ(s.channels[0].bandwidth, ScopeFrontEnd::kMinBandwidth);
    CHECK_EQ(s.channels[0].adcBits, ScopeFrontEnd::kMaxAdcBits);
    CHECK_EQ(s.channels[1].voltsPerDiv, ScopeFrontEnd::kMinVoltsPerDiv);
    CHECK_EQ(s.channels[1].offset, -ScopeFrontEnd::kMaxOffset);
    CHECK_EQ(s.channels[1].bandwidth, 20e6);
    CHECK_EQ(s.channels[1].adcBits, ScopeFrontEnd::kMinAdcBits);

    config.timePerDiv = kNaN;
    CHECK_EQ(ScopeFrontEnd::sanitise(config).timePerDiv, 1e-3);
    config.timePerDiv = 1e3;
    CHECK_EQ(ScopeFrontEnd::sanitise(config).timePerDiv, ScopeFrontEnd::kMaxTimePerDiv);

    const ScopeFrontEnd scope(config);
    CHECK(scope.config() == ScopeFrontEnd::sanitise(config));
}
