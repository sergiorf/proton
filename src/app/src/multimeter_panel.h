// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "panel/panel_section.h"

#include "proton/instruments/multimeter.h"

#include <QString>

#include <optional>

namespace proton::app {

namespace panel {
class LitButton;
class SegmentDisplay;
class SelectorWidget;
} // namespace panel

// The multimeter's front panel (PLAN.md M2.5), laid out like a generic bench multimeter: the LCD
// on top (4.5 digits, the AUTO, DC and AC annunciators and the unit), the mode dial, the RANGE and
// AUTO buttons, and the input jacks with the net each is on.
//
// - Mode dial: DC V, AC V and DC A. In DC A the meter is in series with the circuit, so turning
//   the dial to or from it changes the circuit and restarts the run (PLAN.md M5.2). Resistance
//   arrives with M5.3.
// - RANGE: from auto range it holds the range the meter is on; then each press goes up one range
//   of the mode, wrapping round (Multimeter::nextRange). AUTO goes back to auto range; it is lit
//   while auto. Turning the dial between V and A goes back to auto range.
//
// The display shows each reading as the meter formats it (DmmReading::display), so it reads the
// same as proton-spike for the same circuit. Before the first reading of a run it shows dashes.
// The panel holds the meter's settings and says when the student changes them (changed()); the
// owner applies them to the bench, where a change starts a new gate.
class MultimeterPanel : public panel::PanelFace {
    Q_OBJECT

public:
    explicit MultimeterPanel(QWidget* parent = nullptr);

    // Shows `config` and the nets of the + and COM inputs without emitting changed(): after
    // loading an example. A new configuration clears the display.
    void setConfig(const instruments::Multimeter::Config& config, const QString& plus,
                   const QString& com);
    const instruments::Multimeter::Config& config() const { return config_; }

    void showReading(const instruments::DmmReading& reading);
    // Dashes until the next reading: a new circuit.
    void clear();

    // What the display shows, for the smoke checks.
    QString displayText() const;
    QString displayUnit() const;

Q_SIGNALS:
    void changed();

private:
    void build();
    void showSettings();
    void apply();

    instruments::Multimeter::Config config_;
    std::optional<instruments::DmmReading> last_;

    panel::SegmentDisplay* display_ = nullptr;
    panel::SelectorWidget* mode_ = nullptr;
    panel::LitButton* range_ = nullptr;
    panel::LitButton* auto_ = nullptr;
    QWidget* jacks_ = nullptr;
};

} // namespace proton::app
