// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/instruments/scope_front_end.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace proton::instruments {

// One acquisition of the oscilloscope: kSamplesPerScreen samples per channel with the trigger at
// triggerX divisions right of the centre of the screen (the centre unless the horizontal position
// moved it).
struct ScopeFrame
{
    double triggerTime = 0.0; // simulation time of the trigger, s
    bool triggered = false;   // false for an Auto or forced frame taken without a trigger
    double triggerX = 0.0;    // divisions right of the screen centre where the trigger is
    // Time of the first sample relative to triggerTime, s. Sample i is at
    // startTime + i x sampleInterval, which is triggerX + that / (time/div) divisions across the
    // screen: every sample lies within it, +- 5 divisions.
    double startTime = 0.0;
    double sampleInterval = 0.0; // s
    std::array<std::vector<double>, ScopeFrontEnd::kChannels> values;
};

// A two-channel oscilloscope (ARCHITECTURE.md section 6): the front end (ScopeFrontEnd) followed
// by an edge trigger and the record.
//
// Trigger. It looks at the samples of the source channel, after the ADC, so a level beyond that
// channel's screen never triggers. For a rising edge it arms when a sample is below
// level - hysteresis, then fires at the first sample at or above level; a falling edge is the
// mirror image. The hysteresis is in divisions of the source channel. The trigger time is placed
// between that sample and the one before by linear interpolation, so it is accurate to a fraction
// of a sample and a periodic signal stands still on the screen.
//
// Record. The trigger is triggerPosition divisions right of the screen centre (the horizontal
// position knob), so the record has more samples before it than after when that is positive. An
// acquisition first fills the part of the record before the trigger; only then does the trigger
// arm. After the trigger it takes the rest and gives the frame. The next acquisition starts
// `deadTime` later (rounded up to whole samples), so triggers during the rest of the record, the
// dead time or the next acquisition's part before the trigger are missed, as on a real instrument.
//
// Modes. Normal gives a frame only on a trigger. Auto does too, but if no trigger comes within
// kAutoTimeoutScreens screens of simulation time after the trigger arms, it takes an untriggered
// frame from the timeout on. Single is Normal that stops after one frame; configure() starts it
// again. force() takes an untriggered frame from the next sample on, as a bench scope's Force
// button does, in any mode while the scope waits.
//
// Times are simulation times only. Settings are sanitised as ScopeFrontEnd does: unknown enum
// values become the default, non-finite numbers become the default, and numbers are clamped to
// the ranges given by the constants below.
class Scope
{
public:
    using ChannelConfig = ScopeFrontEnd::ChannelConfig;
    using Coupling = ScopeFrontEnd::Coupling;

    enum class Mode {
        Auto,
        Normal,
        Single,
    };

    enum class TriggerSource {
        Channel1,
        Channel2,
    };

    enum class Slope {
        Rising,
        Falling,
    };

    static constexpr std::size_t kChannels = ScopeFrontEnd::kChannels;
    static constexpr int kSamplesPerScreen = ScopeFrontEnd::kSamplesPerScreen;
    static constexpr long long kAutoTimeoutScreens = 2;

    static constexpr double kMaxTriggerLevel = 200.0; // V, either way
    static constexpr double kMaxHysteresis = 1.0;     // divisions
    static constexpr double kMaxDeadTime = 10.0;      // s
    // The trigger stays on the screen: up to half the screen either way of the centre.
    static constexpr double kMaxTriggerPosition = ScopeFrontEnd::kHorizontalDivisions / 2.0;

    struct Trigger
    {
        TriggerSource source = TriggerSource::Channel1;
        double level = 0.0; // V at the probe tip of the source channel
        Slope slope = Slope::Rising;
        double hysteresis = 0.1; // divisions of the source channel

        friend bool operator==(const Trigger&, const Trigger&) = default;
    };

    struct Config
    {
        double timePerDiv = 1e-3; // s
        std::array<ChannelConfig, kChannels> channels{};
        Trigger trigger;
        Mode mode = Mode::Auto;
        double deadTime = 0.0; // s of simulation time between acquisitions
        double triggerPosition = 0.0; // divisions right of the screen centre where the trigger is

        friend bool operator==(const Config&, const Config&) = default;
    };

    // The settings as the scope would apply them.
    static Config sanitise(const Config& config);

    Scope() { configure(Config{}); }
    explicit Scope(const Config& config) { configure(config); }

    // Applies sanitised settings and restarts acquisition: the acquisition in progress and any
    // unread frame are dropped, and the front end starts again at the next point fed.
    void configure(const Config& config);
    const Config& config() const { return config_; }

    // Time between samples, s, as ScopeFrontEnd::sampleInterval().
    double sampleInterval() const { return frontEnd_.sampleInterval(); }

    // Feeds simulation output, as ScopeFrontEnd::feed().
    void feed(std::span<const double> time, std::span<const double> channel1,
              std::span<const double> channel2);

    // The newest frame not yet popped, if any acquisition has ended since the last call. Like a
    // display, the scope keeps only the newest: older unread frames are dropped.
    std::optional<ScopeFrame> popFrame();

    // True once Single has taken its frame: the scope ignores further input until configure().
    bool stopped() const { return state_ == State::Stopped; }

    // Takes an untriggered frame as soon as the part before the trigger is full, while the scope
    // waits; nothing once Single has stopped. configure() drops a force not yet taken.
    void force();

private:
    enum class State {
        Waiting,   // filling the pre-trigger half, armed, or in the dead time before it
        Recording, // taking the post-trigger half
        Stopped,   // Single, after its frame
    };

    void process(const std::array<double, kChannels>& values);
    void search(long long index);
    void startRecord(long long index, double fraction, bool triggered);
    void finishRecord(long long index);

    Config config_;
    ScopeFrontEnd frontEnd_;
    double hysteresis_ = 0.0; // V
    long long deadSamples_ = 0;
    long long pre_ = 0; // samples of the record up to and including the one before the trigger
    double triggerX_ = 0.0;
    bool forced_ = false;

    State state_ = State::Waiting;
    long long next_ = 0;             // index of the next sample from the front end
    long long acquisitionStart_ = 0; // index of the first sample of the acquisition
    bool armed_ = false;
    // The trigger: between sample triggerIndex_ and the next, at triggerFraction_ of the way.
    long long triggerIndex_ = 0;
    double triggerFraction_ = 0.0;
    bool triggered_ = false;

    // The newest samples, at least the last kSamplesPerScreen of them.
    std::array<std::vector<double>, kChannels> history_;
    std::optional<ScopeFrame> pending_;
};

} // namespace proton::instruments
