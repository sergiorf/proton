// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "panel/panel_section.h"

#include "proton/instruments/function_generator.h"

#include <QString>

#include <array>

class QLineEdit;

namespace proton::app {

namespace panel {
class KnobWidget;
class LitButton;
} // namespace panel

// The function generator's front panel (PLAN.md M2.6), laid out like a generic bench generator: a
// display of the settings on top, the waveform buttons, the frequency, amplitude and offset knobs
// each with a field to type a value into, the output button and the output jacks.
//
// - Waveform: Sine, Square, Triangle, DC, one lit at a time.
// - Frequency: a knob on a log scale, a hundredth of a decade per click, faster when turned fast,
//   so a sweep for a corner frequency is smooth at any frequency.
// - Amplitude (V peak, open circuit) and offset: 10 mV per click, faster when turned fast; pushing
//   the offset knob sets 0 V.
// - Each field takes a value as SPICE writes it ("2.2k", "500m", "1.5") and applies it on Enter;
//   what the generator cannot do is clamped, as FunctionGenerator::sanitise does.
// - Output: lit while on. Off opens the output (FunctionGenerator::kOffResistance).
//
// Every change goes to the bench, where it rebuilds the netlist and restarts the run (decision D3,
// ADR 0005); several changes within a UI tick restart once. The panel holds the generator's
// settings and says when the student changes them (changed()).
class GeneratorPanel : public panel::PanelFace {
    Q_OBJECT

public:
    explicit GeneratorPanel(QWidget* parent = nullptr);

    // Shows `config` and the net the output is on (empty: not connected) without emitting
    // changed(): after loading an example.
    void setConfig(const instruments::FunctionGenerator::Config& config, const QString& net);
    const instruments::FunctionGenerator::Config& config() const { return config_; }

Q_SIGNALS:
    void changed();

private:
    void build();
    void showSettings();
    void apply();

    instruments::FunctionGenerator::Config config_;
    QString net_;

    QWidget* display_ = nullptr;
    std::array<panel::LitButton*, 4> waveforms_{};
    panel::KnobWidget* frequency_ = nullptr;
    panel::KnobWidget* amplitude_ = nullptr;
    panel::KnobWidget* offset_ = nullptr;
    QLineEdit* frequencyEntry_ = nullptr;
    QLineEdit* amplitudeEntry_ = nullptr;
    QLineEdit* offsetEntry_ = nullptr;
    panel::LitButton* output_ = nullptr;
};

} // namespace proton::app
