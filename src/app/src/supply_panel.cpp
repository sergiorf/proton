// SPDX-License-Identifier: AGPL-3.0-only
#include "supply_panel.h"

#include "panel/knob_widget.h"
#include "panel/lit_button.h"
#include "panel/panel_style.h"
#include "panel/segment_display.h"

#include "proton/core/spice_number.h"
#include "proton/instruments/knob.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QStringList>
#include <QVBoxLayout>

#include <optional>
#include <string>

namespace proton::app {

namespace {

using instruments::DcPowerSupply;

constexpr int kDisplayCells = 6; // sign and five digits: "-12.000", "0.0050"
const QString kDashes = QStringLiteral("-----");
const QString kLimit = QStringLiteral("LIMIT");

constexpr double kVoltageStep = 0.01; // V per click
constexpr double kLimitStep = 1e-3;   // A per click

QString text(const std::string& s)
{
    return QString::fromStdString(s);
}

// Where a terminal is: its net, "ground" for node 0, "open" for none.
QString netName(const std::string& net)
{
    if (net.empty()) {
        return SupplyPanel::tr("open");
    }
    return net == "0" ? SupplyPanel::tr("ground") : text(net);
}

QLineEdit* entryField(const QString& name, const QString& unit, QWidget* parent)
{
    auto* field = new QLineEdit(parent);
    field->setAccessibleName(name);
    field->setAlignment(Qt::AlignCenter);
    field->setMaximumWidth(72);
    field->setToolTip(SupplyPanel::tr("Type a number in %1 and press Enter").arg(unit));
    field->setStyleSheet(QStringLiteral("QLineEdit { background: #1c1e21; color: #d6d9dd; "
                                        "border: 1px solid #54595f; border-radius: 4px; }"));
    return field;
}

// The number typed into `field`, or nothing if it is not one.
std::optional<double> typed(const QLineEdit* field)
{
    return core::parseSpiceNumber(field->text().trimmed().toStdString());
}

} // namespace

SupplyPanel::SupplyPanel(QWidget* parent) : panel::PanelFace(parent)
{
    config_ = DcPowerSupply::sanitise(config_);
    build();
    clear();
    showSettings();
}

void SupplyPanel::build()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(14);
    for (std::size_t c = 0; c < kChannels; ++c) {
        layout->addWidget(buildColumn(c), 1);
    }
}

QWidget* SupplyPanel::buildColumn(std::size_t channel)
{
    Column& column = columns_[channel];
    auto* section = new panel::PanelSection(tr("CH%1").arg(channel + 1), this);
    const QString prefix = tr("CH%1 ").arg(channel + 1);

    column.volts = new panel::SegmentDisplay(kDisplayCells, section);
    column.amps = new panel::SegmentDisplay(kDisplayCells, section);
    column.volts->setAccessibleName(prefix + tr("voltage display"));
    column.amps->setAccessibleName(prefix + tr("current display"));
    for (panel::SegmentDisplay* display : {column.volts, column.amps}) {
        display->setMaximumHeight(110);
    }

    const DcPowerSupply::ChannelConfig& set = config_.channels[channel];
    column.voltage = new panel::KnobWidget(
        tr("Voltage"),
        instruments::Knob::continuous(0.0, DcPowerSupply::kMaxVoltage, kVoltageStep, set.voltage,
                                      0.0),
        panel::KnobWidget::Size::Large, section);
    column.limit = new panel::KnobWidget(
        tr("Current limit"),
        instruments::Knob::continuous(DcPowerSupply::kMinCurrentLimit,
                                      DcPowerSupply::kMaxCurrentLimit, kLimitStep,
                                      set.currentLimit, DcPowerSupply::kMinCurrentLimit),
        panel::KnobWidget::Size::Large, section);
    for (panel::KnobWidget* knob : {column.voltage, column.limit}) {
        knob->setPushable(false);
    }
    column.voltage->setToolTip(tr("Voltage: 10 mV a click, Shift finer, Ctrl coarser"));
    column.limit->setToolTip(tr("Current limit: 1 mA a click, Shift finer, Ctrl coarser"));
    column.voltageEntry = entryField(prefix + tr("voltage entry"), tr("volts"), section);
    column.limitEntry = entryField(prefix + tr("current limit entry"), tr("amps"), section);
    column.output = new panel::LitButton(tr("Output"), panel::kLampRun, section);
    column.output->setAccessibleName(prefix + tr("output"));
    column.terminals = new QLabel(section);
    column.terminals->setAlignment(Qt::AlignCenter);
    column.terminals->setStyleSheet(
        QStringLiteral("color: %1;").arg(panel::kDimLettering.name()));

    auto* knobs = new QGridLayout;
    knobs->addWidget(column.voltage, 0, 0, Qt::AlignHCenter);
    knobs->addWidget(column.limit, 0, 1, Qt::AlignHCenter);
    auto* voltageTyping = new QHBoxLayout;
    voltageTyping->addWidget(column.voltageEntry);
    voltageTyping->addWidget(new QLabel(QStringLiteral("V"), section));
    auto* limitTyping = new QHBoxLayout;
    limitTyping->addWidget(column.limitEntry);
    limitTyping->addWidget(new QLabel(QStringLiteral("A"), section));
    knobs->addLayout(voltageTyping, 1, 0, Qt::AlignHCenter);
    knobs->addLayout(limitTyping, 1, 1, Qt::AlignHCenter);

    auto* bottom = new QHBoxLayout;
    bottom->addWidget(column.terminals, 1);
    bottom->addWidget(column.output);

    auto* layout = new QVBoxLayout(section);
    layout->setSpacing(8);
    layout->addWidget(column.volts);
    layout->addWidget(column.amps);
    layout->addLayout(knobs);
    layout->addStretch();
    layout->addLayout(bottom);

    connect(column.voltage, &panel::KnobWidget::valueChanged, this, [this, channel](double v) {
        config_.channels[channel].voltage = v;
        apply();
    });
    connect(column.limit, &panel::KnobWidget::valueChanged, this, [this, channel](double a) {
        config_.channels[channel].currentLimit = a;
        apply();
    });
    connect(column.voltageEntry, &QLineEdit::returnPressed, this, [this, channel] {
        if (const std::optional<double> v = typed(columns_[channel].voltageEntry)) {
            config_.channels[channel].voltage = *v;
            apply();
        } else {
            showSettings(); // not a number: show the setting again
        }
    });
    connect(column.limitEntry, &QLineEdit::returnPressed, this, [this, channel] {
        if (const std::optional<double> a = typed(columns_[channel].limitEntry)) {
            config_.channels[channel].currentLimit = *a;
            apply();
        } else {
            showSettings();
        }
    });
    connect(column.output, &panel::LitButton::clicked, this, [this, channel] {
        config_.channels[channel].output = !config_.channels[channel].output;
        apply();
    });
    return section;
}

void SupplyPanel::setConfig(const DcPowerSupply::Config& config, const Terminals& terminals)
{
    config_ = DcPowerSupply::sanitise(config);
    terminals_ = terminals;
    clear();
    showSettings();
}

void SupplyPanel::showReading(const instruments::SupplyReading& reading)
{
    for (std::size_t c = 0; c < kChannels; ++c) {
        Column& column = columns_[c];
        const instruments::SupplyChannelReading& channel = reading.channels[c];
        column.volts->setReading(text(channel.voltageText), QStringLiteral("V"));
        column.amps->setReading(text(channel.currentText), QStringLiteral("A"));
        column.overLimit = channel.overLimit;
        column.amps->setAnnunciators(channel.overLimit ? QStringList{kLimit} : QStringList{});
    }
}

void SupplyPanel::clear()
{
    for (Column& column : columns_) {
        column.volts->setReading(kDashes, QStringLiteral("V"));
        column.amps->setReading(kDashes, QStringLiteral("A"));
        column.overLimit = false;
        column.amps->setAnnunciators({});
    }
}

QString SupplyPanel::voltageText(std::size_t channel) const
{
    return columns_.at(channel).volts->text();
}

QString SupplyPanel::currentText(std::size_t channel) const
{
    return columns_.at(channel).amps->text();
}

bool SupplyPanel::limitLit(std::size_t channel) const
{
    return columns_.at(channel).overLimit;
}

panel::KnobWidget* SupplyPanel::voltageKnob(std::size_t channel) const
{
    return columns_.at(channel).voltage;
}

QLineEdit* SupplyPanel::voltageEntry(std::size_t channel) const
{
    return columns_.at(channel).voltageEntry;
}

QLineEdit* SupplyPanel::limitEntry(std::size_t channel) const
{
    return columns_.at(channel).limitEntry;
}

panel::LitButton* SupplyPanel::outputButton(std::size_t channel) const
{
    return columns_.at(channel).output;
}

// The student changed a setting: clamp it as the supply would, show it, tell the owner. The run
// restarts, so the displays wait for its first reading.
void SupplyPanel::apply()
{
    config_ = DcPowerSupply::sanitise(config_);
    showSettings();
    clear();
    Q_EMIT changed();
}

void SupplyPanel::showSettings()
{
    for (std::size_t c = 0; c < kChannels; ++c) {
        Column& column = columns_[c];
        const DcPowerSupply::ChannelConfig& set = config_.channels[c];
        column.voltage->setValue(set.voltage);
        column.limit->setValue(set.currentLimit);
        column.voltage->setCaption(
            tr("%1 V").arg(text(DcPowerSupply::formatVoltage(set.voltage))));
        column.limit->setCaption(tr("%1 A").arg(QString::number(set.currentLimit, 'f', 3)));
        column.voltageEntry->setText(QString::number(set.voltage, 'g', 6));
        column.limitEntry->setText(QString::number(set.currentLimit, 'g', 6));
        column.output->setLit(set.output);
        column.terminals->setText(tr("+ on %1    %2 on %3")
                                      .arg(netName(terminals_[c].plus), QChar(0x2212),
                                           netName(terminals_[c].minus)));
    }
}

} // namespace proton::app
