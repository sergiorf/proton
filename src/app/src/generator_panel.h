// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "panel/panel_section.h"

#include "proton/instruments/digit_entry.h"
#include "proton/instruments/function_generator.h"

#include <QPoint>
#include <QString>

#include <array>

class QComboBox;
class QLineEdit;

namespace proton::app {

namespace panel {
class KnobWidget;
class LitButton;
} // namespace panel

// The function generator's front panel (PLAN.md M2.6, M4.6), laid out like a generic bench
// generator: a display of the settings on top, the waveform buttons, the frequency, amplitude and
// offset knobs each with a field and a unit list to type a value into, the output button.
//
// - Waveform: Sine, Square, Triangle, DC, one lit at a time.
// - Frequency, amplitude (V peak, open circuit) and offset are entered digit by digit, as on a
//   bench generator (instruments::DigitEntry): each has a cursor under one digit of the display,
//   moved with Left and Right on its knob or by clicking the digit, and its knob turns that digit,
//   carrying into the next, so 1.592 kHz is set exactly. The cursor's digit is lit for the
//   setting last used; the frequency to the millihertz, the voltages to the millivolt.
// - Each field takes a number in the unit chosen beside it ("2.2" with kHz) and applies it on
//   Enter, or when a unit is chosen, as a generator's unit keys do; what the generator cannot do
//   is clamped, as FunctionGenerator::sanitise does.
// - Output: lit while on. Off opens the output (FunctionGenerator::kOffResistance).
//
// Every change goes to the bench, where it rebuilds the netlist and restarts the run (decision D3,
// ADR 0005); several changes within a UI tick restart once. The panel holds the generator's
// settings and says when the student changes them (changed()).
class GeneratorPanel : public panel::PanelFace {
    Q_OBJECT

public:
    enum class Setting {
        Frequency,
        Amplitude,
        Offset,
    };

    explicit GeneratorPanel(QWidget* parent = nullptr);

    // Shows `config` and the net the output is on (empty: not connected) without emitting
    // changed(): after loading an example.
    void setConfig(const instruments::FunctionGenerator::Config& config, const QString& net);
    const instruments::FunctionGenerator::Config& config() const { return config_; }

    // The digit entry of `setting`, and the setting whose cursor is lit.
    const instruments::DigitEntry& digits(Setting setting) const;
    Setting active() const { return active_; }
    // The display, and where on it the digit of `setting` at `exponent` is, for the smoke check
    // that clicks it; a null point if it is not shown.
    QWidget* display() const { return display_; }
    QPoint digitPosition(Setting setting, int exponent) const;

Q_SIGNALS:
    void changed();

private:
    void build();
    void showSettings();
    void apply();
    instruments::DigitEntry& entry(Setting setting);
    void turn(Setting setting, int clicks);
    void moveCursor(Setting setting, int places);
    void selectDigit(Setting setting, std::size_t index);

    instruments::FunctionGenerator::Config config_;
    QString net_;
    instruments::DigitEntry frequencyDigits_;
    instruments::DigitEntry amplitudeDigits_;
    instruments::DigitEntry offsetDigits_;
    Setting active_ = Setting::Frequency;

    QWidget* display_ = nullptr;
    std::array<panel::LitButton*, 4> waveforms_{};
    panel::KnobWidget* frequency_ = nullptr;
    panel::KnobWidget* amplitude_ = nullptr;
    panel::KnobWidget* offset_ = nullptr;
    QLineEdit* frequencyEntry_ = nullptr;
    QLineEdit* amplitudeEntry_ = nullptr;
    QLineEdit* offsetEntry_ = nullptr;
    QComboBox* frequencyUnit_ = nullptr;
    QComboBox* amplitudeUnit_ = nullptr;
    QComboBox* offsetUnit_ = nullptr;
    panel::LitButton* output_ = nullptr;
};

} // namespace proton::app
