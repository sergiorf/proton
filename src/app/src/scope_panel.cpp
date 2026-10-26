// SPDX-License-Identifier: AGPL-3.0-only
#include "scope_panel.h"

#include "panel/knob_widget.h"
#include "panel/lit_button.h"
#include "panel/panel_style.h"
#include "scope_widget.h"

#include "proton/core/format.h"
#include "proton/instruments/knob.h"
#include "proton/instruments/knob_steps.h"

#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
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

QString couplingName(Scope::Coupling coupling)
{
    switch (coupling) {
    case Scope::Coupling::Dc:
        return QStringLiteral("DC");
    case Scope::Coupling::Ac:
        return QStringLiteral("AC");
    case Scope::Coupling::Gnd:
        return QStringLiteral("GND");
    }
    return {};
}

// The input connectors under the Vertical section: a BNC socket per channel, ringed in its colour,
// with the net it is wired to on the sheet (ADR 0009).
class Connectors : public QWidget
{
public:
    explicit Connectors(QWidget* parent) : QWidget(parent) {}

    void setNets(const QString& channel1, const QString& channel2)
    {
        nets_ = {channel1, channel2};
        update();
    }

    QSize sizeHint() const override { return {200, 70}; }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QFontMetrics metrics(font());
        const double r = 12.0;
        for (std::size_t c = 0; c < Scope::kChannels; ++c) {
            const QPointF centre(width() * (0.25 + 0.5 * static_cast<double>(c)), r + 4);
            // The socket: a metal ring with its bayonet studs, the insulator and the pin.
            painter.setPen(QPen(kChannelColours[c], 2.0));
            painter.setBrush(QColor(170, 174, 180));
            painter.drawEllipse(centre, r, r);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(140, 144, 150));
            painter.drawEllipse(centre + QPointF(-r, 0), 2.2, 2.2);
            painter.drawEllipse(centre + QPointF(r, 0), 2.2, 2.2);
            painter.setBrush(QColor(28, 28, 30));
            painter.drawEllipse(centre, r * 0.58, r * 0.58);
            painter.setBrush(QColor(215, 190, 120));
            painter.drawEllipse(centre, r * 0.18, r * 0.18);

            painter.setPen(kChannelColours[c]);
            painter.drawText(QRectF(centre.x() - 50, centre.y() + r + 2, 100, metrics.height()),
                             Qt::AlignCenter, QStringLiteral("CH%1").arg(c + 1));
            QString net = nets_[c];
            if (net.isEmpty()) {
                net = tr("open");
            } else if (net == QStringLiteral("0")) {
                net = tr("ground");
            }
            painter.setPen(panel::kDimLettering);
            painter.drawText(QRectF(centre.x() - 60, centre.y() + r + 2 + metrics.height(), 120,
                                    metrics.height()),
                             Qt::AlignCenter, tr("on %1").arg(net));
        }
    }

private:
    std::array<QString, Scope::kChannels> nets_;
};

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
    runButtons->addStretch();

    auto* horizontal = new panel::PanelSection(tr("Horizontal"), this);
    timePerDiv_ = new KnobWidget(tr("Time/div"),
                                 shrinkingSteps(ScopeFrontEnd::kMinTimePerDiv,
                                                ScopeFrontEnd::kMaxTimePerDiv, config_.timePerDiv),
                                 KnobWidget::Size::Large, horizontal);
    horizontalPosition_ = new KnobWidget(
        tr("Position"),
        Knob::continuous(-Scope::kMaxTriggerPosition, Scope::kMaxTriggerPosition, 0.1, 0.0, 0.0),
        KnobWidget::Size::Small, horizontal);
    horizontalPosition_->setAccessibleName(tr("Horizontal position"));
    auto* horizontalLayout = new QVBoxLayout(horizontal);
    horizontalLayout->addWidget(timePerDiv_, 0, Qt::AlignHCenter);
    horizontalLayout->addWidget(horizontalPosition_, 0, Qt::AlignHCenter);

    auto* trigger = new panel::PanelSection(tr("Trigger"), this);
    triggerLevel_ = new KnobWidget(
        tr("Level"),
        Knob::continuous(-Scope::kMaxTriggerLevel, Scope::kMaxTriggerLevel, 0.1, 0.0, 0.0),
        KnobWidget::Size::Large, trigger);
    for (std::size_t c = 0; c < Scope::kChannels; ++c) {
        triggerSource_[c] = new LitButton(tr("CH%1").arg(c + 1), kChannelColours[c], trigger);
        triggerSource_[c]->setAccessibleName(tr("Trigger CH%1").arg(c + 1));
    }
    slope_ = new LitButton(tr("Slope"), panel::kLampWhite, trigger);
    auto_ = new LitButton(tr("Auto"), panel::kLampWhite, trigger);
    normal_ = new LitButton(tr("Normal"), panel::kLampWhite, trigger);
    force_ = new LitButton(tr("Force"), panel::kLampSingle, trigger);
    auto* triggerLayout = new QGridLayout(trigger);
    triggerLayout->addWidget(triggerLevel_, 0, 0, 1, 2, Qt::AlignHCenter);
    triggerLayout->addWidget(triggerSource_[0], 1, 0);
    triggerLayout->addWidget(triggerSource_[1], 1, 1);
    triggerLayout->addWidget(slope_, 2, 0, 1, 2);
    triggerLayout->addWidget(auto_, 3, 0);
    triggerLayout->addWidget(normal_, 3, 1);
    triggerLayout->addWidget(force_, 4, 0, 1, 2);

    auto* vertical = new panel::PanelSection(tr("Vertical"), this);
    auto* verticalLayout = new QGridLayout(vertical);
    for (std::size_t c = 0; c < Scope::kChannels; ++c) {
        ChannelControls& controls = channels_[c];
        const QString name = tr("CH%1").arg(c + 1);
        controls.shown = new LitButton(name, kChannelColours[c], vertical);
        controls.scale = new KnobWidget(
            tr("%1 Scale").arg(name),
            shrinkingSteps(ScopeFrontEnd::kMinVoltsPerDiv, ScopeFrontEnd::kMaxVoltsPerDiv, 1.0),
            KnobWidget::Size::Large, vertical);
        controls.scale->setVariableAllowed(true);
        controls.position = new KnobWidget(
            tr("%1 Position").arg(name),
            Knob::continuous(-ScopeFrontEnd::kMaxOffset, ScopeFrontEnd::kMaxOffset, 0.1, 0.0, 0.0),
            KnobWidget::Size::Small, vertical);
        controls.coupling = new LitButton(QStringLiteral("DC"), kChannelColours[c], vertical);
        controls.coupling->setAccessibleName(tr("%1 coupling").arg(name));
        controls.scale->setAccent(kChannelColours[c]);
        controls.position->setAccent(kChannelColours[c]);
        // The channel button with its coupling beside it, then the scale and position knobs.
        auto* buttons = new QHBoxLayout;
        buttons->addWidget(controls.shown);
        buttons->addWidget(controls.coupling);
        const int column = static_cast<int>(c);
        verticalLayout->addLayout(buttons, 0, column, Qt::AlignHCenter);
        verticalLayout->addWidget(controls.scale, 1, column, Qt::AlignHCenter);
        verticalLayout->addWidget(controls.position, 2, column, Qt::AlignHCenter);
    }
    connectors_ = new Connectors(this);
    connectors_->setMinimumSize(connectors_->sizeHint()); // never squeezed out of the column

    // The controls beside the screen, as on a bench scope: run buttons, then Horizontal and
    // Trigger side by side, then Vertical over the connectors.
    auto* upper = new QHBoxLayout;
    upper->addWidget(horizontal);
    upper->addWidget(trigger);
    auto* side = new QVBoxLayout;
    side->addLayout(runButtons);
    side->addLayout(upper);
    side->addWidget(vertical);
    side->addWidget(connectors_);
    side->addStretch();

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(14);
    layout->addWidget(screen_, 1);
    layout->addLayout(side);

    // Knobs.
    connect(timePerDiv_, &KnobWidget::valueChanged, this, [this](double value) {
        config_.timePerDiv = value;
        apply();
    });
    connect(horizontalPosition_, &KnobWidget::valueChanged, this, [this](double value) {
        config_.triggerPosition = value;
        apply();
    });
    for (std::size_t c = 0; c < Scope::kChannels; ++c) {
        const ChannelControls& controls = channels_[c];
        connect(controls.shown, &LitButton::clicked, this, [this, c] {
            config_.channels[c].shown = !config_.channels[c].shown;
            apply();
        });
        connect(controls.scale, &KnobWidget::valueChanged, this, [this, c](double value) {
            config_.channels[c].voltsPerDiv = value;
            apply();
        });
        connect(controls.scale, &KnobWidget::variableChanged, this, [this, c](bool variable) {
            screen_->setVariable(c, variable);
            showKnobs();
        });
        connect(controls.position, &KnobWidget::valueChanged, this, [this, c](double value) {
            config_.channels[c].offset = -value;
            apply();
        });
        connect(controls.coupling, &LitButton::clicked, this, [this, c] {
            Scope::Coupling& coupling = config_.channels[c].coupling;
            switch (coupling) {
            case Scope::Coupling::Dc:
                coupling = Scope::Coupling::Ac;
                break;
            case Scope::Coupling::Ac:
                coupling = Scope::Coupling::Gnd;
                break;
            case Scope::Coupling::Gnd:
                coupling = Scope::Coupling::Dc;
                break;
            }
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
    connect(force_, &LitButton::clicked, this, [this] { Q_EMIT forced(); });
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

void ScopePanel::setInputs(const QString& channel1, const QString& channel2)
{
    static_cast<Connectors*>(connectors_)->setNets(channel1, channel2);
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
    Q_EMIT changed();
}

// Every control from config_.
void ScopePanel::showKnobs()
{
    timePerDiv_->setValue(config_.timePerDiv);
    timePerDiv_->setCaption(engineering(config_.timePerDiv, 1, "s") + QStringLiteral("/div"));
    horizontalPosition_->setStep(0.1); // a tenth of a division a click
    horizontalPosition_->setValue(config_.triggerPosition);
    // The trigger's place as a time from the centre of the screen.
    horizontalPosition_->setCaption(
        engineering(config_.triggerPosition * config_.timePerDiv, 3, "s"));
    for (std::size_t c = 0; c < Scope::kChannels; ++c) {
        const Scope::ChannelConfig& channel = config_.channels[c];
        ChannelControls& controls = channels_[c];
        controls.shown->setLit(channel.shown);
        controls.scale->setValue(channel.voltsPerDiv);
        // A variable scale has three digits: it is between the 1-2-5 steps.
        controls.scale->setCaption(
            engineering(channel.voltsPerDiv, controls.scale->variable() ? 3 : 1, "V") +
            QStringLiteral("/div"));
        controls.position->setStep(channel.voltsPerDiv / 10.0);
        controls.position->setValue(-channel.offset);
        controls.position->setCaption(engineering(-channel.offset, 3, "V"));
        controls.coupling->setText(couplingName(channel.coupling));
        controls.coupling->setLit(channel.coupling != Scope::Coupling::Dc);
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
    // Force is lit while the scope waits for a trigger, when pressing it does something.
    force_->setLit(!stopped && !lastTriggered_.value_or(false));

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
        Q_EMIT changed();
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
    Q_EMIT changed();
    Q_EMIT armed();
    showState();
}

} // namespace proton::app
