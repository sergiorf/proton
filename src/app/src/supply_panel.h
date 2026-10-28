// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "panel/panel_section.h"

#include "proton/instruments/dc_power_supply.h"

#include <QString>

#include <array>
#include <cstddef>

class QLabel;
class QLineEdit;

namespace proton::app {

namespace panel {
class KnobWidget;
class LitButton;
class SegmentDisplay;
} // namespace panel

// The DC power supply's front panel (PLAN.md M5.1), laid out like a generic two-channel bench
// supply: one column per channel, each with its voltage and current displays on top, the VOLTAGE
// and CURRENT LIMIT knobs below them, each with a field to type a value into, the channel's Output
// button and its + and - terminals with the nets they are on.
//
// - The displays show what the channel delivers (instruments::SupplyReading): the voltage across
//   its terminals and the current out of +, dashes before the first reading of a run. The current
//   display lights LIMIT while the current goes beyond the limit; the limit does not regulate
//   (CC/CV comes after the MVP).
// - VOLTAGE turns in 10 mV clicks and CURRENT LIMIT in 1 mA clicks, a tenth of that with Shift and
//   ten times with Ctrl (instruments::Knob); each knob's caption shows its setting.
// - Output: lit while on. Off opens the output (DcPowerSupply::kOffResistance).
//
// Every change goes to the bench, where it rebuilds the netlist and restarts the run, as a
// generator knob does (decision D3). The panel holds the supply's settings and says when the
// student changes them (changed()).
class SupplyPanel : public panel::PanelFace {
    Q_OBJECT

public:
    static constexpr std::size_t kChannels = instruments::DcPowerSupply::kChannels;
    using Terminals = std::array<instruments::DcPowerSupply::Terminals, kChannels>;

    explicit SupplyPanel(QWidget* parent = nullptr);

    // Shows `config` and the nets of each channel's terminals without emitting changed(): after
    // loading an example. A new configuration clears the displays.
    void setConfig(const instruments::DcPowerSupply::Config& config, const Terminals& terminals);
    const instruments::DcPowerSupply::Config& config() const { return config_; }

    void showReading(const instruments::SupplyReading& reading);
    // Dashes until the next reading: a new circuit or new settings.
    void clear();

    // What channel `channel` shows, and its controls, for the smoke checks.
    QString voltageText(std::size_t channel) const;
    QString currentText(std::size_t channel) const;
    bool limitLit(std::size_t channel) const;
    panel::KnobWidget* voltageKnob(std::size_t channel) const;
    QLineEdit* voltageEntry(std::size_t channel) const;
    QLineEdit* limitEntry(std::size_t channel) const;
    panel::LitButton* outputButton(std::size_t channel) const;

Q_SIGNALS:
    void changed();

private:
    struct Column
    {
        panel::SegmentDisplay* volts = nullptr;
        panel::SegmentDisplay* amps = nullptr;
        panel::KnobWidget* voltage = nullptr;
        panel::KnobWidget* limit = nullptr;
        QLineEdit* voltageEntry = nullptr;
        QLineEdit* limitEntry = nullptr;
        panel::LitButton* output = nullptr;
        QLabel* terminals = nullptr;
        bool overLimit = false;
    };

    void build();
    QWidget* buildColumn(std::size_t channel);
    void showSettings();
    void apply();

    instruments::DcPowerSupply::Config config_;
    Terminals terminals_{};
    std::array<Column, kChannels> columns_{};
};

} // namespace proton::app
