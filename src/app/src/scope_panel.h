// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "panel/panel_section.h"

#include "proton/instruments/scope.h"

#include <QString>

#include <array>
#include <optional>

namespace proton::app {

namespace panel {
class KnobWidget;
class LitButton;
} // namespace panel

class ScopeWidget;

// The oscilloscope's front panel (PLAN.md M2.4, M4.5), laid out like a generic two-channel bench
// scope, original and brand-neutral: the screen on the left; on the right, Run/Stop and Single at
// the top, the Horizontal and Trigger sections side by side below them, then the Vertical section
// with a column per channel, and the input connectors under it.
//
// - Horizontal: the time/div knob, 1-2-5 steps with its scale printed round it, clockwise faster;
//   the position knob, which moves the trigger across the screen, clockwise to the right (pushed,
//   back to the centre).
// - Vertical, per channel: the channel button, lit in its colour while the trace is shown; the
//   scale knob (V/div, 1-2-5 steps, clockwise larger; pushed, it goes variable between the steps
//   and VAR is marked on the screen, pushed again back onto a step); the position knob (clockwise
//   moves the trace up; pushed, it centres the channel's ground); and the coupling button, which
//   goes DC, AC, GND and round, lit when not DC.
// - Trigger: the level knob (pushed, it goes to the centre of the source channel's screen), CH1 or
//   CH2 as source, the slope, Auto or Normal, and Force, lit while the scope waits for a trigger,
//   which takes a frame at once (forced()).
// - The connectors: CH1 and CH2, each showing the net it is wired to on the sheet (setInputs).
//
// Position is the scope model's offset with its sign turned (offset is the voltage at the centre
// of the screen, so raising the trace lowers it). Fine knobs step a tenth of a division of their
// channel, faster when turned fast; Shift turns any knob finer, Ctrl coarser.
//
// Run/Stop and Single work as on a digital scope. Stop freezes the screen on the last frame; Run
// shows new frames again, in the trigger mode chosen. Single takes one triggered frame and then
// stops: the scope's own Single mode. Run after Single goes back to that mode. Run/Stop is lit
// green while running and red when stopped; Single is lit while it waits for its trigger.
//
// The panel holds the scope's settings and says when the student changes them (changed()); the
// owner applies them to the bench, where a change restarts acquisition, as turning a knob does
// (ARCHITECTURE.md section 5).
class ScopePanel : public panel::PanelFace {
    Q_OBJECT

public:
    explicit ScopePanel(QWidget* parent = nullptr);

    // Shows `config` on the knobs without emitting changed(): after loading an example.
    void setConfig(const instruments::Scope::Config& config);
    const instruments::Scope::Config& config() const { return config_; }
    // The nets CH1 and CH2 are wired to, shown under the connectors; empty: open.
    void setInputs(const QString& channel1, const QString& channel2);

    // A frame from the bench's scope; shown unless the screen is stopped. `stopped` is
    // Scope::stopped(), true once Single has taken its frame.
    void showFrame(instruments::ScopeFrame frame, bool stopped);
    // Clears the screen: a new circuit.
    void clear();

    const ScopeWidget& screen() const { return *screen_; }

    // Frames shown since the panel was made, for the frame rate.
    long long framesShown() const { return framesShown_; }

Q_SIGNALS:
    // The student changed a knob; config() has the new settings.
    void changed();
    // The student pressed Single: the scope starts a single acquisition again, even if its
    // settings did not change. Follows changed() when they did.
    void armed();
    // The student pressed Force: the scope takes a frame at once (Scope::force).
    void forced();

private:
    struct ChannelControls
    {
        panel::LitButton* shown = nullptr;
        panel::KnobWidget* scale = nullptr;
        panel::KnobWidget* position = nullptr;
        panel::LitButton* coupling = nullptr;
    };

    void build();
    void showKnobs();
    void showState();
    void apply();
    void run();
    void stop();
    void single();

    instruments::Scope::Config config_;
    instruments::Scope::Mode runMode_ = instruments::Scope::Mode::Auto; // Run's mode
    bool frozen_ = false; // Stop: the screen keeps its frame
    bool lastStopped_ = false;
    std::optional<bool> lastTriggered_;
    long long framesShown_ = 0;

    ScopeWidget* screen_ = nullptr;
    QWidget* connectors_ = nullptr;
    panel::KnobWidget* timePerDiv_ = nullptr;
    panel::KnobWidget* horizontalPosition_ = nullptr;
    std::array<ChannelControls, instruments::Scope::kChannels> channels_{};
    panel::KnobWidget* triggerLevel_ = nullptr;
    std::array<panel::LitButton*, instruments::Scope::kChannels> triggerSource_{};
    panel::LitButton* slope_ = nullptr;
    panel::LitButton* auto_ = nullptr;
    panel::LitButton* normal_ = nullptr;
    panel::LitButton* force_ = nullptr;
    panel::LitButton* runStop_ = nullptr;
    panel::LitButton* single_ = nullptr;
};

} // namespace proton::app
