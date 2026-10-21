// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace proton::instruments {

// Samples produced by the front end, one vector per channel, all of the same length.
struct ScopeSamples
{
    long long first = 0; // index of the first sample, counted from the first point fed
    std::array<std::vector<double>, 2> values;
};

// The front end of a two-channel oscilloscope: everything between the probe tips and the trigger
// (ARCHITECTURE.md section 6). The channels share the timebase; each has its own vertical
// settings. Per channel, in the order of a real instrument:
//
// 1. Analog front end, in continuous time: DC or AC coupling (a one-pole high-pass with its
//    corner at kAcCouplingCorner) and an optional one-pole bandwidth limit. The signal is taken
//    as linear between the points it is fed and each filter is solved in closed form over every
//    step, so the result is exact for any time grid and does not depend on how it is chunked.
//    The filters start in steady state at the first value fed: AC coupling starts charged to it.
// 2. Sampling at fs = kSamplesPerScreen / (kHorizontalDivisions x timePerDiv), from the first
//    point fed after configure(). The continuous signal is evaluated exactly at each sample time,
//    so a signal above fs / 2 aliases as it would on hardware.
// 3. ADC: clipping to the screen, offset +- kVerticalDivisions / 2 divisions (offset is the
//    voltage at the centre of the screen), and optional quantisation to `adcBits` over the
//    kVerticalDivisions divisions: a sample is then offset + code x LSB.
//
// Bandwidth limit and quantisation are off by default. With both off and DC coupling, a sample
// on the screen is the input exactly: the point fed at its time, or the line between the two
// points around it. Samples are in volts at the probe tip.
//
// Settings are sanitised: unknown enum values become the default, non-finite numbers become the
// default, and numbers are clamped to the ranges given by the constants below.
class ScopeFrontEnd
{
public:
    enum class Coupling {
        Dc,
        Ac,
    };

    static constexpr std::size_t kChannels = 2;
    static constexpr int kSamplesPerScreen = 1000;
    static constexpr int kHorizontalDivisions = 10;
    static constexpr int kVerticalDivisions = 8;
    static constexpr double kAcCouplingCorner = 10.0; // Hz, as a 1 Mohm input with its capacitor

    static constexpr double kMinTimePerDiv = 1e-9; // s
    static constexpr double kMaxTimePerDiv = 10.0;
    static constexpr double kMinVoltsPerDiv = 1e-3; // V
    static constexpr double kMaxVoltsPerDiv = 10.0;
    static constexpr double kMaxOffset = 100.0;  // V, either way
    static constexpr double kMinBandwidth = 1e3; // Hz: 100 times the AC corner
    static constexpr double kMaxBandwidth = 1e9;
    static constexpr int kMinAdcBits = 4;
    static constexpr int kMaxAdcBits = 16;

    struct ChannelConfig
    {
        double voltsPerDiv = 1.0;
        double offset = 0.0; // V at the centre of the screen
        Coupling coupling = Coupling::Dc;
        bool bandwidthLimit = false;
        double bandwidth = 20e6; // Hz, -3 dB, when bandwidthLimit is on
        bool quantise = false;
        int adcBits = 8; // when quantise is on

        friend bool operator==(const ChannelConfig&, const ChannelConfig&) = default;
    };

    struct Config
    {
        double timePerDiv = 1e-3; // s
        std::array<ChannelConfig, kChannels> channels{};

        friend bool operator==(const Config&, const Config&) = default;
    };

    // The settings as the front end would apply them.
    static Config sanitise(const Config& config);

    ScopeFrontEnd() { configure(Config{}); }
    explicit ScopeFrontEnd(const Config& config) { configure(config); }

    // Applies sanitised settings and restarts acquisition: untaken samples are dropped, and the
    // sample clock and the filters start again at the next point fed.
    void configure(const Config& config);
    const Config& config() const { return config_; }

    // Time between samples, s: kHorizontalDivisions x timePerDiv / kSamplesPerScreen.
    double sampleInterval() const { return interval_; }
    // Simulation time of sample `index`. Meaningful once a point has been fed.
    double sampleTime(long long index) const
    {
        return origin_ + static_cast<double>(index) * interval_;
    }

    // Feeds simulation output: `channel1[i]` and `channel2[i]` are the probe voltages at
    // `time[i]`. An empty channel span is a channel with no probe and reads 0 V; otherwise extra
    // elements of the longer spans are ignored. Time continues from the previous call. Points
    // that do not move time forward are skipped.
    void feed(std::span<const double> time, std::span<const double> channel1,
              std::span<const double> channel2);

    // The samples produced since the last call. They accumulate until taken.
    ScopeSamples takeSamples();

private:
    // Error states e = z - x of the one-pole low-passes z on the piecewise-linear input x.
    struct FilterState
    {
        double ac = 0.0;
        double bandwidth = 0.0;
    };

    struct Channel
    {
        double tauAc = 0.0;
        double tauBandwidth = 0.0;
        double lsb = 0.0; // V, 0 when not quantising
        double lastValue = 0.0;
        FilterState state;
    };

    // The state of channel `index` after a step of `dt` from the last point, in which the input
    // rises linearly by `dx`.
    FilterState advance(std::size_t index, double dt, double dx) const;
    double sample(std::size_t index, double x, const FilterState& state) const;
    void step(double t, const std::array<double, kChannels>& x);

    Config config_;
    std::array<Channel, kChannels> channels_{};
    double interval_ = 0.0;
    bool started_ = false;
    double origin_ = 0.0;
    double lastTime_ = 0.0;
    long long nextSample_ = 0;
    ScopeSamples pending_;
};

} // namespace proton::instruments
