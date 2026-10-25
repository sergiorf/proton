// SPDX-License-Identifier: AGPL-3.0-only
#include "scope_panel.h"

#include "panel/knob_widget.h"
#include "panel/lit_button.h"
#include "panel/panel_style.h"
#include "scope_widget.h"

#include "proton/core/format.h"
#include "proton/instruments/knob.h"
#include "proton/instruments/knob_steps.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QString>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace proton::app {

namespace {

using instruments::Knob;
using instruments::Scope;
using instruments::ScopeFrontEnd;
using panel::KnobWidget;
using panel::LitButton;

const std::array<QColor, Scope::kChannels> kChannelColours = {panel::kChannel1, panel::kChannel2};

QString engineering(double value, int digits, const char* unit)
{
    return QString::fromStdString(core::formatEngineering(value, digits, unit));
}

// A 1-2-5 knob from `min` to `max` that turns clockwise towards smaller values: a larger trace.
Knob shrinkingSteps(double min, double max, double initial)
{
    std::vector<double> steps = instruments::oneTwoFiveSteps(min, max);
    std::reverse(steps.begin(), steps.end());
    return Knob::stepped(std::move(steps), initial);
}

std::size_t sourceIndex(const Scope::Config& config)
{
    return config.trigger.source == Scope::TriggerSource::Channel2 ? 1 : 0;
}

} // namespace

ScopePanel::ScopePanel(QWidget* parent) : panel::PanelFace(parent)
{
    config_ = Scope::sanitise(config_);
    build();
    showKnobs();
}

void ScopePanel::build()
{
    screen_ = new ScopeWidget(this);

    runStop_ = new LitButton(tr("Run / Stop"), panel::kLampRun, this);
    single_ = new LitButton(tr("Single"), panel::kLampSingle, this);
    auto* runButtons = new QHBoxLayout;
    runButtons->addWidget(runStop_);
    runButtons->addWidget(single_);

    auto* horizontal = new panel::PanelSection(tr("Horizontal"), this);
    timePerDiv_ = new KnobWidget(tr("Time/div"),
                                 shrinkingSteps(ScopeFrontEnd::kMinTimePerDiv,
                                                ScopeFrontEnd::kMaxTimePerDiv, config_.timePerDiv),
                                 KnobWidget::Size::Large, horizontal);
    auto* horizontalLayout = new QHBoxLayout(horizontal);
    horizontalLayout->addWidget(timePerDiv_, 0, Qt::AlignHCenter);

    auto* vertical = new panel::PanelSection(tr("Vertical"), this);
    auto* verticalLayout = new QGridLayout(vertical);
    for (std::size_t c = 0; c < Scope::kChannels; ++c) {
        ChannelControls& controls = channels_[c];
        const QString name = tr("CH%1").arg(c + 1);
        controls.scale = new KnobWidget(
            tr("%1 Scale").arg(name),
            shrinkingSteps(ScopeFrontEnd::kMinVoltsPerDiv, ScopeFrontEnd::kMaxVoltsPerDiv, 1.0),
            KnobWidget::Size::Large, vertical);
        controls.position = new KnobWidget(
            tr("%1 Position").arg(name),
            Knob::continuous(-ScopeFrontEnd::kMaxOffset, ScopeFrontEnd::kMaxOffset, 0.1, 0.0, 0.0),
            KnobWidget::Size::Small, vertical);
        controls.ac = new LitButton(tr("%1 AC").arg(name), kChannelColours[c], vertical);
        controls.scale->setAccent(kChannelColours[c]);
        controls.position->setAccent(kChannelColours[c]);
        const int column = static_cast<int>(c);
        verticalLayout->addWidget(controls.scale, 0, column, Qt::AlignHCenter);
        verticalLayout->addWidget(controls.position, 1, column, Qt::AlignHCenter);
        verticalLayout->addWidget(controls.ac, 2, column, Qt::AlignHCenter);
    }

    auto* trigger = new panel::PanelSection(tr("Trigger"), this);
    triggerLevel_ = new KnobWidget(
        tr("Level"),
        Knob::continuous(-Scope::kMaxTriggerLevel, Scope::kMaxTriggerLevel, 0.1, 0.0, 0.0),
        KnobWidget::Size::Large, trigger);
    for (std::size_t c = 0; c < Scope::kChannels; ++c) {
        triggerSource_[c] = new LitButton(tr("CH%1").arg(c + 1), kChannelColours[c], trigger);
    }
    slope_ = new LitButton(tr("Slope"), panel::kLampWhite, trigger);
    auto_ = new LitButton(tr("Auto"), panel::kLampWhite, trigger);
    normal_ = new LitButton(tr("Normal"), panel::kLampWhite, trigger);
    auto* triggerLayout = new QGridLayout(trigger);
    triggerLayout->addWidget(triggerLevel_, 0, 0, 1, 2, Qt::AlignHCenter);
    triggerLayout->addWidget(triggerSource_[0], 1, 0);
    triggerLayout->addWidget(triggerSource_[1], 1, 1);
    triggerLayout->addWidget(slope_, 2, 0, 1, 2);
    triggerLayout->addWidget(auto_, 3, 0);
    triggerLayout->addWidget(normal_, 3, 1);

    auto* controls = new QVBoxLayout;
    controls->addLayout(runButtons);
    controls->addWidget(horizontal);
    controls->addWidget(vertical);
    controls->addWidget(trigger);
    controls->addStretch();

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(14);
    layout->addWidget(screen_, 1);
    layout->addLayout(controls);

    // Knobs.
    connect(timePerDiv_, &KnobWidget::valueChanged, this, [this](double value) {
        config_.timePerDiv = value;
        apply();
    });
    for (std::size_t c = 0; c < Scope::kChannels; ++c) {
        connect(channels_[c].scale, &KnobWidget::valueChanged, this, [this, c](double value) {
            config_.channels[c].voltsPerDiv = value;
            apply();
        });
        connect(channels_[c].position, &KnobWidget::valueChanged, this, [this, c](double value) {
            config_.channels[c].offset = -value;
            apply();
        });
        connect(channels_[c].ac, &LitButton::clicked, this, [this, c] {
            Scope::Coupling& coupling = config_.channels[c].coupling;
            coupling = coupling == Scope::Coupling::Ac ? Scope::Coupling::Dc : Scope::Coupling::Ac;
            apply();
        });
    }
    connect(triggerLevel_, &KnobWidget::valueChanged, this, [this](double value) {
        config_.trigger.level = value;
        apply();
    });
    for (std::size_t c = 0; c < Scope::kChannels; ++c) {
        connect(triggerSource_[c], &LitButton::clicked, this, [this, c] {
            config_.trigger.source =
                c == 0 ? Scope::TriggerSource::Channel1 : Scope::TriggerSource::Channel2;
            apply();
        });
    }
    connect(slope_, &LitButton::clicked, this, [this] {
        config_.trigger.slope = config_.trigger.slope == Scope::Slope::Rising
                                    ? Scope::Slope::Falling
                                    : Scope::Slope::Rising;
        apply();
    });
    connect(auto_, &LitButton::clicked, this, [this] {
        runMode_ = Scope::Mode::Auto;
        if (config_.mode != Scope::Mode::Single) {
            config_.mode = runMode_;
        }
        apply();
    });
    connect(normal_, &LitButton::clicked, this, [this] {
        runMode_ = Scope::Mode::Normal;
        if (config_.mode != Scope::Mode::Single) {
            config_.mode = runMode_;
        }
        apply();
    });
    connect(runStop_, &LitButton::clicked, this, [this] {
        if (frozen_ || config_.mode == Scope::Mode::Single) {
            run();
        } else {
            stop();
        }
    });
    connect(single_, &LitButton::clicked, this, &ScopePanel::single);
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
    showState();
}

void ScopePanel::clear()
{
    screen_->clear();
    lastTriggered_.reset();
    lastStopped_ = false;
    showState();
}

// The student changed a setting in config_: show it and tell the owner.
void ScopePanel::apply()
{
    showKnobs();
    emit changed();
}

// Every control from config_.
void ScopePanel::showKnobs()
{
    timePerDiv_->setValue(config_.timePerDiv);
    timePerDiv_->setCaption(engineering(config_.timePerDiv, 1, "s") + QStringLiteral("/div"));
    for (std::size_t c = 0; c < Scope::kChannels; ++c) {
        const Scope::ChannelConfig& channel = config_.channels[c];
        ChannelControls& controls = channels_[c];
        controls.scale->setValue(channel.voltsPerDiv);
        controls.scale->setCaption(engineering(channel.voltsPerDiv, 1, "V") +
                                   QStringLiteral("/div"));
        controls.position->setStep(channel.voltsPerDiv / 10.0);
        controls.position->setValue(-channel.offset);
        controls.position->setCaption(engineering(-channel.offset, 3, "V"));
        controls.ac->setLit(channel.coupling == Scope::Coupling::Ac);
    }
    const std::size_t source = sourceIndex(config_);
    const Scope::ChannelConfig& sourceChannel = config_.channels[source];
    triggerLevel_->setStep(sourceChannel.voltsPerDiv / 10.0);
    triggerLevel_->setReset(sourceChannel.offset); // the centre of the source's screen
    triggerLevel_->setValue(config_.trigger.level);
    triggerLevel_->setCaption(engineering(config_.trigger.level, 3, "V"));
    triggerLevel_->setAccent(kChannelColours[source]);
    for (std::size_t c = 0; c < Scope::kChannels; ++c) {
        triggerSource_[c]->setLit(c == source);
    }
    const bool rising = config_.trigger.slope == Scope::Slope::Rising;
    slope_->setText(rising ? tr("Slope: rising") : tr("Slope: falling"));
    auto_->setLit(runMode_ == Scope::Mode::Auto);
    normal_->setLit(runMode_ == Scope::Mode::Normal);
    screen_->setConfig(config_);
    showState();
}

void ScopePanel::showState()
{
    const bool singleDone = config_.mode == Scope::Mode::Single && lastStopped_;
    const bool stopped = frozen_ || singleDone;
    runStop_->setLamp(stopped ? panel::kLampStop : panel::kLampRun);
    runStop_->setLit(true);
    single_->setLit(config_.mode == Scope::Mode::Single && !lastStopped_ && !frozen_);

    if (stopped) {
        screen_->setStatus(tr("Stop"), panel::kLampStop);
    } else if (!lastTriggered_) {
        screen_->setStatus(config_.mode == Scope::Mode::Single ? tr("Single: waiting")
                                                               : tr("Waiting"),
                           panel::kLampSingle);
    } else if (*lastTriggered_) {
        screen_->setStatus(tr("Trig'd"), panel::kLampRun);
    } else {
        screen_->setStatus(tr("Auto"), panel::kLampSingle);
    }
}

void ScopePanel::run()
{
    frozen_ = false;
    if (config_.mode == Scope::Mode::Single) {
        config_.mode = runMode_;
        emit changed();
    }
    showState();
}

void ScopePanel::stop()
{
    frozen_ = true;
    showState();
}

void ScopePanel::single()
{
    frozen_ = false;
    lastStopped_ = false;
    lastTriggered_.reset();
    config_.mode = Scope::Mode::Single;
    screen_->setConfig(config_);
    emit changed();
    emit armed();
    showState();
}

} // namespace proton::app
