// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/instruments/scope.h"

#include <QWidget>

#include <array>
#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace proton::app {

class ScopeWidget;

// The oscilloscope's front panel: the screen (ScopeWidget) and its knobs. Horizontal: time/div.
// Per channel: V/div, position (the voltage at the centre of the screen) and DC/AC coupling.
// Trigger: source, level, slope, Auto or Normal. Run/Stop and Single.
//
// The panel holds the scope's settings and says when the student changes them (changed()); the
// owner applies them to the bench, where a change restarts acquisition, as turning a knob does
// (ARCHITECTURE.md section 5). The stepped knobs follow the 1-2-5 sequence.
//
// Run/Stop and Single work as on a digital scope. Stop freezes the screen on the last frame; Run
// shows new frames again, in the trigger mode chosen. Single takes one triggered frame and then
// stops: the scope's own Single mode. Run after Single goes back to that mode.
class ScopePanel : public QWidget {
    Q_OBJECT

public:
    explicit ScopePanel(QWidget* parent = nullptr);

    // Shows `config` on the knobs without emitting changed(): after loading an example.
    void setConfig(const instruments::Scope::Config& config);
    const instruments::Scope::Config& config() const { return config_; }

    // A frame from the bench's scope; shown unless the screen is stopped. `stopped` is
    // Scope::stopped(), true once Single has taken its frame.
    void showFrame(instruments::ScopeFrame frame, bool stopped);
    // Clears the screen: a new circuit.
    void clear();

    const ScopeWidget& screen() const { return *screen_; }

    // Frames shown since the panel was made, for the frame rate.
    long long framesShown() const { return framesShown_; }

signals:
    // The student changed a knob; config() has the new settings.
    void changed();
    // The student pressed Single: the scope starts a single acquisition again, even if its
    // settings did not change. Follows changed() when they did.
    void armed();

private:
    struct ChannelControls
    {
        QComboBox* voltsPerDiv = nullptr;
        QDoubleSpinBox* position = nullptr;
        QComboBox* coupling = nullptr;
    };

    void build();
    void readKnobs();
    void showKnobs();
    void updateSteps();
    void showRunState();
    void run();
    void stop();
    void single();

    instruments::Scope::Config config_;
    instruments::Scope::Mode runMode_ = instruments::Scope::Mode::Auto; // Run's mode
    bool frozen_ = false;   // Stop: the screen keeps its frame
    bool lastStopped_ = false;
    std::optional<bool> lastTriggered_;
    long long framesShown_ = 0;

    ScopeWidget* screen_ = nullptr;
    QComboBox* timePerDiv_ = nullptr;
    std::array<ChannelControls, instruments::Scope::kChannels> channels_{};
    QComboBox* triggerSource_ = nullptr;
    QDoubleSpinBox* triggerLevel_ = nullptr;
    QComboBox* triggerSlope_ = nullptr;
    QComboBox* triggerMode_ = nullptr;
    QPushButton* runStop_ = nullptr;
    QPushButton* single_ = nullptr;
    QLabel* runState_ = nullptr;
    QLabel* readout_ = nullptr;
};

} // namespace proton::app
