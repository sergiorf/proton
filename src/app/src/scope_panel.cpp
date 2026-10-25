// SPDX-License-Identifier: AGPL-3.0-only
#include "scope_panel.h"

#include "scope_widget.h"

#include "proton/core/format.h"
#include "proton/instruments/knob_steps.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QString>
#include <QVBoxLayout>
#include <QVariant>

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace proton::app {

namespace {

using instruments::Scope;
using instruments::ScopeFrontEnd;

QString text(const std::string& s)
{
    return QString::fromStdString(s);
}

// A combo box of the 1-2-5 steps from `min` to `max`, labelled in engineering notation.
QComboBox* steppedKnob(double min, double max, const char* unit, QWidget* parent)
{
    auto* box = new QComboBox(parent);
    for (const double step : instruments::oneTwoFiveSteps(min, max)) {
        box->addItem(text(core::formatEngineering(step, 1, unit)) + QStringLiteral("/div"), step);
    }
    return box;
}

// Selects the entry of a stepped knob nearest `value`.
void selectStep(QComboBox* box, double value, double min, double max)
{
    const double step = instruments::turnOneTwoFive(value, 0, min, max);
    for (int i = 0; i < box->count(); ++i) {
        if (box->itemData(i).toDouble() == step) {
            box->setCurrentIndex(i);
            return;
        }
    }
}

// The entry of a combo box whose data is `value`.
template <typename Enum>
void selectEnum(QComboBox* box, Enum value)
{
    box->setCurrentIndex(box->findData(static_cast<int>(value)));
}

template <typename Enum>
Enum currentEnum(const QComboBox* box)
{
    return static_cast<Enum>(box->currentData().toInt());
}

QDoubleSpinBox* voltageBox(QWidget* parent)
{
    auto* box = new QDoubleSpinBox(parent);
    box->setRange(-ScopeFrontEnd::kMaxOffset, ScopeFrontEnd::kMaxOffset);
    box->setDecimals(3);
    box->setSuffix(QStringLiteral(" V"));
    box->setKeyboardTracking(false);
    return box;
}

} // namespace

ScopePanel::ScopePanel(QWidget* parent) : QWidget(parent)
{
    config_ = Scope::sanitise(config_);
    build();
    showKnobs();
}

void ScopePanel::build()
{
    screen_ = new ScopeWidget(this);

    auto* horizontal = new QGroupBox(tr("Horizontal"), this);
    auto* horizontalLayout = new QFormLayout(horizontal);
    timePerDiv_ = steppedKnob(ScopeFrontEnd::kMinTimePerDiv, ScopeFrontEnd::kMaxTimePerDiv, "s",
                              horizontal);
    horizontalLayout->addRow(tr("Time"), timePerDiv_);

    std::vector<QGroupBox*> verticals;
    for (std::size_t c = 0; c < Scope::kChannels; ++c) {
        auto* group = new QGroupBox(tr("CH%1").arg(c + 1), this);
        auto* layout = new QFormLayout(group);
        ChannelControls& controls = channels_[c];
        controls.voltsPerDiv = steppedKnob(ScopeFrontEnd::kMinVoltsPerDiv,
                                           ScopeFrontEnd::kMaxVoltsPerDiv, "V", group);
        controls.position = voltageBox(group);
        controls.position->setToolTip(tr("The voltage at the centre of the screen"));
        controls.coupling = new QComboBox(group);
        controls.coupling->addItem(tr("DC"), static_cast<int>(Scope::Coupling::Dc));
        controls.coupling->addItem(tr("AC"), static_cast<int>(Scope::Coupling::Ac));
        layout->addRow(tr("Scale"), controls.voltsPerDiv);
        layout->addRow(tr("Position"), controls.position);
        layout->addRow(tr("Coupling"), controls.coupling);
        verticals.push_back(group);
    }

    auto* trigger = new QGroupBox(tr("Trigger"), this);
    auto* triggerLayout = new QFormLayout(trigger);
    triggerSource_ = new QComboBox(trigger);
    triggerSource_->addItem(tr("CH1"), static_cast<int>(Scope::TriggerSource::Channel1));
    triggerSource_->addItem(tr("CH2"), static_cast<int>(Scope::TriggerSource::Channel2));
    triggerLevel_ = voltageBox(trigger);
    triggerLevel_->setRange(-Scope::kMaxTriggerLevel, Scope::kMaxTriggerLevel);
    triggerSlope_ = new QComboBox(trigger);
    triggerSlope_->addItem(tr("Rising"), static_cast<int>(Scope::Slope::Rising));
    triggerSlope_->addItem(tr("Falling"), static_cast<int>(Scope::Slope::Falling));
    triggerMode_ = new QComboBox(trigger);
    triggerMode_->addItem(tr("Auto"), static_cast<int>(Scope::Mode::Auto));
    triggerMode_->addItem(tr("Normal"), static_cast<int>(Scope::Mode::Normal));
    triggerLayout->addRow(tr("Source"), triggerSource_);
    triggerLayout->addRow(tr("Level"), triggerLevel_);
    triggerLayout->addRow(tr("Slope"), triggerSlope_);
    triggerLayout->addRow(tr("Mode"), triggerMode_);

    runStop_ = new QPushButton(tr("Run / Stop"), this);
    single_ = new QPushButton(tr("Single"), this);
    runState_ = new QLabel(this);
    runState_->setAlignment(Qt::AlignCenter);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(runStop_);
    buttons->addWidget(single_);

    auto* knobs = new QVBoxLayout;
    knobs->addWidget(horizontal);
    for (QGroupBox* group : verticals) {
        knobs->addWidget(group);
    }
    knobs->addWidget(trigger);
    knobs->addLayout(buttons);
    knobs->addWidget(runState_);
    knobs->addStretch();

    readout_ = new QLabel(this);
    auto* display = new QVBoxLayout;
    display->addWidget(screen_, 1);
    display->addWidget(readout_);

    auto* layout = new QHBoxLayout(this);
    layout->addLayout(display, 1);
    layout->addLayout(knobs);

    const auto knob = [this] {
        readKnobs();
        emit changed();
    };
    connect(timePerDiv_, &QComboBox::currentIndexChanged, this, knob);
    for (ChannelControls& controls : channels_) {
        connect(controls.voltsPerDiv, &QComboBox::currentIndexChanged, this, knob);
        connect(controls.position, &QDoubleSpinBox::valueChanged, this, knob);
        connect(controls.coupling, &QComboBox::currentIndexChanged, this, knob);
    }
    connect(triggerSource_, &QComboBox::currentIndexChanged, this, knob);
    connect(triggerLevel_, &QDoubleSpinBox::valueChanged, this, knob);
    connect(triggerSlope_, &QComboBox::currentIndexChanged, this, knob);
    connect(triggerMode_, &QComboBox::currentIndexChanged, this, knob);
    connect(runStop_, &QPushButton::clicked, this, [this] {
        if (frozen_ || config_.mode == Scope::Mode::Single) {
            run();
        } else {
            stop();
        }
    });
    connect(single_, &QPushButton::clicked, this, &ScopePanel::single);
}

void ScopePanel::setConfig(const Scope::Config& config)
{
    config_ = Scope::sanitise(config);
    if (config_.mode != Scope::Mode::Single) {
        runMode_ = config_.mode;
    }
    frozen_ = false;
    showKnobs();
}

void ScopePanel::showFrame(instruments::ScopeFrame frame, bool stopped)
{
    lastStopped_ = stopped;
    if (frozen_) {
        return;
    }
    lastTriggered_ = frame.triggered;
    screen_->setFrame(std::move(frame));
    ++framesShown_;
    showRunState();
}

void ScopePanel::clear()
{
    screen_->clear();
    lastTriggered_.reset();
    lastStopped_ = false;
    showRunState();
}

void ScopePanel::readKnobs()
{
    config_.timePerDiv = timePerDiv_->currentData().toDouble();
    for (std::size_t c = 0; c < Scope::kChannels; ++c) {
        config_.channels[c].voltsPerDiv = channels_[c].voltsPerDiv->currentData().toDouble();
        config_.channels[c].offset = channels_[c].position->value();
        config_.channels[c].coupling = currentEnum<Scope::Coupling>(channels_[c].coupling);
    }
    config_.trigger.source = currentEnum<Scope::TriggerSource>(triggerSource_);
    config_.trigger.level = triggerLevel_->value();
    config_.trigger.slope = currentEnum<Scope::Slope>(triggerSlope_);
    runMode_ = currentEnum<Scope::Mode>(triggerMode_);
    if (config_.mode != Scope::Mode::Single) {
        config_.mode = runMode_;
    }
    updateSteps();
    screen_->setConfig(config_);
    showRunState();
}

void ScopePanel::showKnobs()
{
    const QSignalBlocker blockTime(timePerDiv_);
    selectStep(timePerDiv_, config_.timePerDiv, ScopeFrontEnd::kMinTimePerDiv,
               ScopeFrontEnd::kMaxTimePerDiv);
    for (std::size_t c = 0; c < Scope::kChannels; ++c) {
        const ChannelControls& controls = channels_[c];
        const QSignalBlocker blockScale(controls.voltsPerDiv);
        const QSignalBlocker blockPosition(controls.position);
        const QSignalBlocker blockCoupling(controls.coupling);
        selectStep(controls.voltsPerDiv, config_.channels[c].voltsPerDiv,
                   ScopeFrontEnd::kMinVoltsPerDiv, ScopeFrontEnd::kMaxVoltsPerDiv);
        controls.position->setValue(config_.channels[c].offset);
        selectEnum(controls.coupling, config_.channels[c].coupling);
    }
    const QSignalBlocker blockSource(triggerSource_);
    const QSignalBlocker blockLevel(triggerLevel_);
    const QSignalBlocker blockSlope(triggerSlope_);
    const QSignalBlocker blockMode(triggerMode_);
    selectEnum(triggerSource_, config_.trigger.source);
    triggerLevel_->setValue(config_.trigger.level);
    selectEnum(triggerSlope_, config_.trigger.slope);
    selectEnum(triggerMode_, runMode_);
    updateSteps();
    screen_->setConfig(config_);
    showRunState();
}

// Position and level step by a tenth of a division of their channel, as a fine knob would.
void ScopePanel::updateSteps()
{
    for (std::size_t c = 0; c < Scope::kChannels; ++c) {
        channels_[c].position->setSingleStep(config_.channels[c].voltsPerDiv / 10.0);
    }
    const std::size_t source = config_.trigger.source == Scope::TriggerSource::Channel2 ? 1 : 0;
    triggerLevel_->setSingleStep(config_.channels[source].voltsPerDiv / 10.0);
}

void ScopePanel::showRunState()
{
    QString state;
    if (frozen_ || (config_.mode == Scope::Mode::Single && lastStopped_)) {
        state = tr("STOP");
    } else if (config_.mode == Scope::Mode::Single) {
        state = tr("SINGLE: waiting for a trigger");
    } else if (!lastTriggered_) {
        state = tr("RUN: waiting for a trigger");
    } else {
        state = *lastTriggered_ ? tr("RUN: triggered") : tr("RUN: auto, not triggered");
    }
    runState_->setText(state);

    const auto perDiv = [](double value, const char* unit) {
        return text(core::formatEngineering(value, 1, unit)) + QStringLiteral("/div");
    };
    const std::size_t source = config_.trigger.source == Scope::TriggerSource::Channel2 ? 1 : 0;
    readout_->setText(
        tr("CH1 %1 %2    CH2 %3 %4    %5    Trigger CH%6 %7 %8")
            .arg(perDiv(config_.channels[0].voltsPerDiv, "V"),
                 config_.channels[0].coupling == Scope::Coupling::Ac ? tr("AC") : tr("DC"),
                 perDiv(config_.channels[1].voltsPerDiv, "V"),
                 config_.channels[1].coupling == Scope::Coupling::Ac ? tr("AC") : tr("DC"),
                 perDiv(config_.timePerDiv, "s"))
            .arg(source + 1)
            .arg(config_.trigger.slope == Scope::Slope::Rising ? tr("rising") : tr("falling"),
                 text(core::formatEngineering(config_.trigger.level, 3, "V"))));
}

void ScopePanel::run()
{
    frozen_ = false;
    if (config_.mode == Scope::Mode::Single) {
        config_.mode = runMode_;
        emit changed();
    }
    showRunState();
}

void ScopePanel::stop()
{
    frozen_ = true;
    showRunState();
}

void ScopePanel::single()
{
    frozen_ = false;
    lastStopped_ = false;
    config_.mode = Scope::Mode::Single;
    screen_->setConfig(config_);
    emit changed();
    emit armed();
    showRunState();
}

} // namespace proton::app
