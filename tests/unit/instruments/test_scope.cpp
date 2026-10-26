// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/scope.h"
#include "proton/test/test.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using proton::instruments::Scope;
using proton::instruments::ScopeFrame;
using Config = Scope::Config;
using Mode = Scope::Mode;
using Slope = Scope::Slope;
using TriggerSource = Scope::TriggerSource;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kPi = std::numbers::pi;
constexpr std::size_t kRecord = Scope::kSamplesPerScreen;

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

struct Signal
{
    std::vector<double> time;
    std::vector<double> channel1;
    std::vector<double> channel2;
};

// Samples `f1` and `f2` on [0, end) with random steps between `minStep` and `maxStep`.
Signal sampleIrregular(const std::function<double(double)>& f1,
                       const std::function<double(double)>& f2, double end, double minStep,
                       double maxStep)
{
    Signal signal;
    Random random;
    for (double t = 0.0; t < end; t += minStep + (maxStep - minStep) * random.next()) {
        signal.time.push_back(t);
        signal.channel1.push_back(f1(t));
        signal.channel2.push_back(f2(t));
    }
    return signal;
}

double zero(double)
{
    return 0.0;
}

// Feeds `signal` in chunks of `chunk` points and collects every frame, popping after each chunk.
std::vector<ScopeFrame> run(Scope& scope, const Signal& signal, std::size_t chunk = 64)
{
    std::vector<ScopeFrame> frames;
    for (std::size_t i = 0; i < signal.time.size(); i += chunk) {
        const std::size_t n = std::min(chunk, signal.time.size() - i);
        scope.feed(std::span(signal.time).subspan(i, n), std::span(signal.channel1).subspan(i, n),
                   std::span(signal.channel2).subspan(i, n));
        if (std::optional<ScopeFrame> frame = scope.popFrame()) {
            frames.push_back(std::move(*frame));
        }
    }
    return frames;
}

Config withTrigger(Mode mode, double level, Slope slope = Slope::Rising)
{
    Config config;
    config.mode = mode;
    config.trigger.level = level;
    config.trigger.slope = slope;
    return config;
}

// Points [begin, end) of `signal`.
Signal slice(const Signal& signal, std::size_t begin, std::size_t end)
{
    Signal part;
    part.time.assign(signal.time.begin() + static_cast<std::ptrdiff_t>(begin),
                     signal.time.begin() + static_cast<std::ptrdiff_t>(end));
    part.channel1.assign(signal.channel1.begin() + static_cast<std::ptrdiff_t>(begin),
                         signal.channel1.begin() + static_cast<std::ptrdiff_t>(end));
    part.channel2.assign(signal.channel2.begin() + static_cast<std::ptrdiff_t>(begin),
                         signal.channel2.begin() + static_cast<std::ptrdiff_t>(end));
    return part;
}

// Distance from `t` to the nearest time in {first + n x period}.
double distanceToGrid(double t, double first, double period)
{
    const double n = std::round((t - first) / period);
    return std::fabs(t - (first + n * period));
}

} // namespace

TEST(defaults_and_sanitising)
{
    const Scope scope;
    CHECK(scope.config() == Config{});
    CHECK(scope.config().mode == Mode::Auto);
    CHECK_EQ(scope.config().deadTime, 0.0);

    Config bad;
    bad.timePerDiv = kNaN;
    bad.trigger.source = static_cast<TriggerSource>(7);
    bad.trigger.slope = static_cast<Slope>(7);
    bad.trigger.level = 1e6;
    bad.trigger.hysteresis = -1.0;
    bad.mode = static_cast<Mode>(7);
    bad.deadTime = kNaN;
    const Config fixed = Scope::sanitise(bad);
    CHECK_EQ(fixed.timePerDiv, Config{}.timePerDiv);
    CHECK(fixed.trigger.source == TriggerSource::Channel1);
    CHECK(fixed.trigger.slope == Slope::Rising);
    CHECK_EQ(fixed.trigger.level, Scope::kMaxTriggerLevel);
    CHECK_EQ(fixed.trigger.hysteresis, 0.0);
    CHECK(fixed.mode == Mode::Auto);
    CHECK_EQ(fixed.deadTime, 0.0);

    bad.trigger.level = kNaN;
    bad.trigger.hysteresis = 5.0;
    bad.deadTime = 1e3;
    const Config clamped = Scope::sanitise(bad);
    CHECK_EQ(clamped.trigger.level, 0.0);
    CHECK_EQ(clamped.trigger.hysteresis, Scope::kMaxHysteresis);
    CHECK_EQ(clamped.deadTime, Scope::kMaxDeadTime);
}

TEST(triggered_sine_is_stable_to_a_fraction_of_a_sample_across_100_frames)
{
    // 1.234 kHz, so the crossings fall at a different place between samples in every frame.
    const double amplitude = 2.0;
    const double offset = 0.5;
    const double omega = 2.0 * kPi * 1234.0;
    const double period = 2.0 * kPi / omega;
    const auto f = [&](double t) { return offset + amplitude * std::sin(omega * t); };
    const double level = offset + 0.3 * amplitude;
    const double phase = std::asin(0.3); // of the rising crossing of the level

    Scope scope(withTrigger(Mode::Normal, level));
    const double dt = scope.sampleInterval();
    const Signal signal = sampleIrregular(f, zero, 1.3, 0.1 * dt, 0.5 * dt);
    const std::vector<ScopeFrame> frames = run(scope, signal);
    CHECK(frames.size() >= 100u);

    double minStart = kInf;
    double maxStart = -kInf;
    for (const ScopeFrame& frame : frames) {
        CHECK(frame.triggered);
        CHECK_EQ(frame.values[0].size(), kRecord);
        CHECK_EQ(frame.sampleInterval, dt);
        // The trigger is at a rising crossing of the level, to a hundredth of a sample.
        CHECK(distanceToGrid(frame.triggerTime, phase / omega, period) < 0.01 * dt);
        // Every sample sits where the sine, aligned on the trigger, says it should: the trace
        // stands still on the screen.
        for (std::size_t i = 0; i < kRecord; ++i) {
            const double t = frame.startTime + static_cast<double>(i) * dt;
            CHECK_NEAR(frame.values[0][i], offset + amplitude * std::sin(phase + omega * t),
                       2e-3 * amplitude);
        }
        minStart = std::min(minStart, frame.startTime);
        maxStart = std::max(maxStart, frame.startTime);
    }
    // The sub-sample position really varies: without it the trace would jitter by a sample.
    CHECK(maxStart - minStart > 0.5 * dt);
}

TEST(trigger_is_at_the_centre_of_the_screen)
{
    Scope scope(withTrigger(Mode::Normal, 0.25));
    const double dt = scope.sampleInterval();
    const auto f = [](double t) { return std::sin(2.0 * kPi * 70.0 * t); };
    const std::vector<ScopeFrame> frames = run(scope, sampleIrregular(f, zero, 0.1, dt, 3 * dt));
    CHECK(!frames.empty());
    for (const ScopeFrame& frame : frames) {
        const double half = static_cast<double>(kRecord / 2);
        CHECK(frame.startTime > -half * dt - 1e-15);
        CHECK(frame.startTime <= (1.0 - half) * dt + 1e-15);
        // The level is crossed between the two samples around the centre.
        CHECK(frame.values[0][kRecord / 2 - 1] < 0.25);
        CHECK(frame.values[0][kRecord / 2] >= 0.25);
    }
}

TEST(falling_slope_triggers_on_falling_crossings)
{
    const double omega = 2.0 * kPi * 1234.0;
    const auto f = [&](double t) { return std::sin(omega * t); };
    Scope scope(withTrigger(Mode::Normal, 0.0, Slope::Falling));
    const double dt = scope.sampleInterval();
    const std::vector<ScopeFrame> frames = run(scope, sampleIrregular(f, zero, 0.2, dt / 4, dt));
    CHECK(frames.size() >= 10u);
    for (const ScopeFrame& frame : frames) {
        // Falling zero crossings of sin are at odd multiples of pi.
        CHECK(distanceToGrid(frame.triggerTime, kPi / omega, 2.0 * kPi / omega) < 0.01 * dt);
        CHECK(frame.values[0][kRecord / 2 - 1] > 0.0);
        CHECK(frame.values[0][kRecord / 2] <= 0.0);
    }
}

TEST(trigger_follows_the_source_channel)
{
    const double omega = 2.0 * kPi * 1234.0;
    const auto f1 = [](double t) { return std::sin(2.0 * kPi * 777.0 * t); };
    const auto f2 = [&](double t) { return std::sin(omega * t); };
    Config config = withTrigger(Mode::Normal, 0.0);
    config.trigger.source = TriggerSource::Channel2;
    Scope scope(config);
    const double dt = scope.sampleInterval();
    const std::vector<ScopeFrame> frames = run(scope, sampleIrregular(f1, f2, 0.2, dt / 4, dt));
    CHECK(frames.size() >= 10u);
    for (const ScopeFrame& frame : frames) {
        CHECK(distanceToGrid(frame.triggerTime, 0.0, 2.0 * kPi / omega) < 0.01 * dt);
    }
}

TEST(normal_gives_no_frame_when_the_level_is_outside_the_signal)
{
    const auto f = [](double t) { return std::sin(2.0 * kPi * 1000.0 * t); };
    Scope scope(withTrigger(Mode::Normal, 1.5));
    const double dt = scope.sampleInterval();
    const std::vector<ScopeFrame> frames = run(scope, sampleIrregular(f, zero, 0.2, dt / 2, dt));
    CHECK(frames.empty());
}

TEST(auto_gives_untriggered_frames_when_the_level_is_outside_the_signal)
{
    const auto f = [](double t) { return std::sin(2.0 * kPi * 1000.0 * t); };
    Scope scope(withTrigger(Mode::Auto, 1.5));
    const double dt = scope.sampleInterval();
    const std::vector<ScopeFrame> frames = run(scope, sampleIrregular(f, zero, 0.2, dt / 2, dt));
    CHECK(!frames.empty());
    for (const ScopeFrame& frame : frames) {
        CHECK(!frame.triggered);
        CHECK_EQ(frame.values[0].size(), kRecord);
        CHECK_EQ(frame.startTime, (1.0 - static_cast<double>(kRecord / 2)) * dt);
        // An untriggered frame still shows the signal as it was.
        for (std::size_t i = 0; i < kRecord; i += 97) {
            const double t = frame.triggerTime + frame.startTime + static_cast<double>(i) * dt;
            CHECK_NEAR(frame.values[0][i], f(t), 1e-3);
        }
    }
    // The first forced frame comes after the pre-trigger half and the timeout, plus the
    // post-trigger half: the first trigger time is the timeout.
    const double timeout =
        (static_cast<double>(kRecord / 2 - 1) +
         static_cast<double>(Scope::kAutoTimeoutScreens * static_cast<long long>(kRecord))) *
        dt;
    CHECK_NEAR(frames.front().triggerTime, timeout, 1e-12);
}

TEST(auto_triggers_when_the_level_is_inside_the_signal)
{
    const double omega = 2.0 * kPi * 1234.0;
    const auto f = [&](double t) { return std::sin(omega * t); };
    Scope scope(withTrigger(Mode::Auto, 0.0));
    const double dt = scope.sampleInterval();
    const std::vector<ScopeFrame> frames = run(scope, sampleIrregular(f, zero, 0.2, dt / 4, dt));
    CHECK(frames.size() >= 10u);
    for (const ScopeFrame& frame : frames) {
        CHECK(frame.triggered);
        CHECK(distanceToGrid(frame.triggerTime, 0.0, 2.0 * kPi / omega) < 0.01 * dt);
    }
}

TEST(single_stops_after_one_frame)
{
    const auto f = [](double t) { return std::sin(2.0 * kPi * 1234.0 * t); };
    Scope scope(withTrigger(Mode::Single, 0.0));
    const double dt = scope.sampleInterval();
    const Signal signal = sampleIrregular(f, zero, 0.2, dt / 4, dt);
    const std::vector<ScopeFrame> frames = run(scope, signal);
    CHECK_EQ(frames.size(), 1u);
    CHECK(frames.front().triggered);
    CHECK(scope.stopped());

    // configure() starts it again.
    scope.configure(scope.config());
    CHECK(!scope.stopped());
    CHECK_EQ(run(scope, signal).size(), 1u);
}

TEST(single_waits_for_a_trigger)
{
    const auto f = [](double t) { return std::sin(2.0 * kPi * 1000.0 * t); };
    Scope scope(withTrigger(Mode::Single, 1.5));
    const double dt = scope.sampleInterval();
    CHECK(run(scope, sampleIrregular(f, zero, 0.2, dt / 2, dt)).empty());
    CHECK(!scope.stopped());
}

TEST(hysteresis_ignores_wiggles_smaller_than_it)
{
    // A 0.3 V sine around the level: it crosses the level but never goes 0.5 V below it.
    const auto f = [](double t) { return 0.3 * std::sin(2.0 * kPi * 1234.0 * t); };
    Config config = withTrigger(Mode::Normal, 0.0);
    config.trigger.hysteresis = 0.5; // divisions of 1 V
    Scope wide(config);
    const double dt = wide.sampleInterval();
    const Signal signal = sampleIrregular(f, zero, 0.2, dt / 4, dt);
    CHECK(run(wide, signal).empty());

    config.trigger.hysteresis = 0.2;
    Scope narrow(config);
    CHECK(!run(narrow, signal).empty());
}

TEST(dead_time_separates_acquisitions)
{
    // 10 kHz at 1 ms/div: a rising crossing every 0.1 ms, much more often than the scope can
    // take frames. Each frame ends half a screen after its trigger; the next acquisition starts
    // after the dead time, fills half a screen and triggers on the next crossing.
    const double period = 1e-4;
    const auto f = [&](double t) { return std::sin(2.0 * kPi * t / period); };
    for (const double deadTime : {0.0, 3.3e-3}) {
        Config config = withTrigger(Mode::Normal, 0.0);
        config.deadTime = deadTime;
        Scope scope(config);
        const double dt = scope.sampleInterval();
        const std::vector<ScopeFrame> frames =
            run(scope, sampleIrregular(f, zero, 0.3, dt / 4, dt), 16);
        CHECK(frames.size() >= 10u);
        const double screen = static_cast<double>(kRecord) * dt;
        for (std::size_t i = 1; i < frames.size(); ++i) {
            const double gap = frames[i].triggerTime - frames[i - 1].triggerTime;
            // The previous trigger can be up to a sample after its sample.
            CHECK(gap >= screen + deadTime - dt);
            CHECK(gap <= screen + deadTime + period + 2.0 * dt);
        }
    }
}

TEST(frames_do_not_depend_on_chunking)
{
    const auto f = [](double t) { return std::sin(2.0 * kPi * 1234.0 * t); };
    Scope a(withTrigger(Mode::Normal, 0.2));
    Scope b(withTrigger(Mode::Normal, 0.2));
    const double dt = a.sampleInterval();
    const Signal signal = sampleIrregular(f, zero, 0.1, dt / 4, dt);
    const std::vector<ScopeFrame> chunked = run(a, signal, 7);
    const std::vector<ScopeFrame> whole = run(b, signal, signal.time.size());
    CHECK(!chunked.empty());
    CHECK_EQ(whole.size(), 1u); // only the newest frame is kept
    CHECK_EQ(chunked.back().triggerTime, whole.back().triggerTime);
    CHECK_EQ(chunked.back().startTime, whole.back().startTime);
    CHECK(chunked.back().values == whole.back().values);
}

TEST(configure_drops_the_acquisition_and_unread_frame)
{
    const auto f = [](double t) { return std::sin(2.0 * kPi * 1234.0 * t); };
    Scope scope(withTrigger(Mode::Normal, 0.0));
    const double dt = scope.sampleInterval();
    const Signal signal = sampleIrregular(f, zero, 0.05, dt / 4, dt);
    scope.feed(signal.time, signal.channel1, signal.channel2);
    scope.configure(scope.config());
    CHECK(!scope.popFrame().has_value());
}

// The horizontal position knob (M4.5): the trigger sits triggerPosition divisions right of the
// screen centre, and the trace still crosses the level there.
TEST(the_trigger_position_moves_the_trigger_across_the_screen)
{
    Config config = withTrigger(Mode::Normal, 0.25);
    config.triggerPosition = 2.5;
    Scope scope(config);
    const double dt = scope.sampleInterval();
    const auto f = [](double t) { return std::sin(2.0 * kPi * 70.0 * t); };
    const std::vector<ScopeFrame> frames = run(scope, sampleIrregular(f, zero, 0.1, dt, 3 * dt));
    CHECK(!frames.empty());
    // 100 samples per division: the trigger is 250 samples right of the centre.
    const std::size_t at = kRecord / 2 + 250;
    for (const ScopeFrame& frame : frames) {
        CHECK_EQ(frame.triggerX, 2.5);
        CHECK(frame.values[0][at - 1] < 0.25);
        CHECK(frame.values[0][at] >= 0.25);
        // The trigger time is between those two samples.
        CHECK(frame.startTime > -static_cast<double>(at) * dt - 1e-15);
        CHECK(frame.startTime <= (1.0 - static_cast<double>(at)) * dt + 1e-15);
    }
}

TEST(the_trigger_position_stays_on_the_screen)
{
    Config config;
    config.triggerPosition = 9.0;
    CHECK_EQ(Scope::sanitise(config).triggerPosition, 5.0);
    config.triggerPosition = -9.0;
    CHECK_EQ(Scope::sanitise(config).triggerPosition, -5.0);
    config.triggerPosition = kNaN;
    CHECK_EQ(Scope::sanitise(config).triggerPosition, 0.0);
    // At the very edge the trigger is on the last sample that still has one after it.
    config = withTrigger(Mode::Normal, 0.0);
    config.triggerPosition = 5.0;
    Scope scope(config);
    const double dt = scope.sampleInterval();
    const auto f = [](double t) { return std::sin(2.0 * kPi * 70.0 * t); };
    const std::vector<ScopeFrame> frames = run(scope, sampleIrregular(f, zero, 0.1, dt, 3 * dt));
    CHECK(!frames.empty());
    for (const ScopeFrame& frame : frames) {
        CHECK(frame.triggerX <= 5.0 && frame.triggerX > 4.9);
        CHECK_EQ(frame.values[0].size(), kRecord);
    }
}

// A channel switched off is not drawn (scope_display), but it is still acquired and can trigger.
TEST(a_hidden_channel_can_still_be_the_trigger_source)
{
    const double omega = 2.0 * kPi * 1234.0;
    const auto f2 = [&](double t) { return std::sin(omega * t); };
    Config config = withTrigger(Mode::Normal, 0.0);
    config.trigger.source = TriggerSource::Channel2;
    config.channels[1].shown = false;
    Scope scope(config);
    const double dt = scope.sampleInterval();
    const std::vector<ScopeFrame> frames = run(scope, sampleIrregular(zero, f2, 0.2, dt / 4, dt));
    CHECK(frames.size() >= 10u);
    for (const ScopeFrame& frame : frames) {
        CHECK(frame.triggered);
        CHECK(distanceToGrid(frame.triggerTime, 0.0, 2.0 * kPi / omega) < 0.01 * dt);
        CHECK_EQ(frame.values[1].size(), kRecord);
    }
}

// Force (M4.5): one frame at once, untriggered, while Normal or Single waits for a trigger.
TEST(force_takes_a_frame_without_a_trigger)
{
    const auto f = [](double t) { return std::sin(2.0 * kPi * 1000.0 * t); };
    for (const Mode mode : {Mode::Normal, Mode::Single}) {
        Scope scope(withTrigger(mode, 1.5)); // never crossed
        const double dt = scope.sampleInterval();
        const Signal signal = sampleIrregular(f, zero, 0.2, dt / 2, dt);
        const std::size_t half = signal.time.size() / 2;
        CHECK(run(scope, slice(signal, 0, half)).empty());
        scope.force();
        const std::vector<ScopeFrame> frames = run(scope, slice(signal, half, signal.time.size()));
        CHECK_EQ(frames.size(), 1u); // one frame, then waiting again (Normal) or stopped (Single)
        if (!frames.empty()) {
            CHECK(!frames.front().triggered);
            // From the first sample after the press: after the last point fed before it.
            CHECK(frames.front().triggerTime > signal.time[half - 1]);
            // It shows the signal as it was.
            const ScopeFrame& frame = frames.front();
            for (std::size_t i = 0; i < kRecord; i += 97) {
                const double t = frame.triggerTime + frame.startTime + static_cast<double>(i) * dt;
                CHECK_NEAR(frame.values[0][i], f(t), 1e-3);
            }
        }
        CHECK_EQ(scope.stopped(), mode == Mode::Single);
    }
}

TEST(configure_drops_a_pending_force)
{
    const auto f = [](double t) { return std::sin(2.0 * kPi * 1000.0 * t); };
    Scope scope(withTrigger(Mode::Normal, 1.5));
    const double dt = scope.sampleInterval();
    scope.force();
    scope.configure(scope.config());
    CHECK(run(scope, sampleIrregular(f, zero, 0.1, dt / 2, dt)).empty());
}
