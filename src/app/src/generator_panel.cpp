// SPDX-License-Identifier: AGPL-3.0-only
#include "generator_panel.h"

#include "panel/knob_widget.h"
#include "panel/lit_button.h"
#include "panel/panel_style.h"

#include "proton/core/format.h"
#include "proton/core/spice_number.h"
#include "proton/instruments/knob.h"

#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

namespace proton::app {

namespace {

using instruments::FunctionGenerator;
using instruments::Knob;
using Waveform = FunctionGenerator::Waveform;

constexpr std::array<Waveform, 4> kWaveforms = {Waveform::Sine, Waveform::Square,
                                                Waveform::Triangle, Waveform::Dc};
constexpr double kDecadesPerClick = 0.01;
constexpr double kVoltsPerClick = 0.01;

QString engineering(double value, int digits, const char* unit)
{
    return QString::fromStdString(core::formatEngineering(value, digits, unit));
}

QString waveformName(Waveform waveform)
{
    switch (waveform) {
    case Waveform::Sine:
        return QObject::tr("Sine");
    case Waveform::Square:
        return QObject::tr("Square");
    case Waveform::Triangle:
        return QObject::tr("Triangle");
    case Waveform::Dc:
        return QObject::tr("DC");
    }
    return {};
}

// The generator's display: the settings in a blue backlit LCD, as a bench generator shows them.
class Display : public QWidget
{
public:
    explicit Display(QWidget* parent) : QWidget(parent) { setMinimumHeight(86); }

    void show(const FunctionGenerator::Config& config, const QString& net)
    {
        config_ = config;
        net_ = net;
        update();
    }

    QSize sizeHint() const override { return {320, 96}; }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(panel::kPanelEdge);
        painter.drawRoundedRect(QRectF(rect()), 8, 8);
        const QRectF glass = QRectF(rect()).adjusted(6, 6, -6, -6);
        painter.setBrush(QColor(22, 44, 78));
        painter.drawRoundedRect(glass, 4, 4);

        QFont big = font();
        big.setFamily(QStringLiteral("Consolas"));
        big.setStyleHint(QFont::Monospace);
        big.setPixelSize(static_cast<int>(std::clamp(glass.height() * 0.3, 16.0, 30.0)));
        QFont small = big;
        small.setPixelSize(static_cast<int>(std::clamp(glass.height() * 0.17, 11.0, 16.0)));
        const QColor lit(170, 220, 255);
        const QColor dim(110, 150, 190);

        const QRectF inner = glass.adjusted(12, 6, -12, -6);
        painter.setFont(small);
        painter.setPen(dim);
        const QString state = config_.output ? tr("OUTPUT ON") : tr("OUTPUT OFF");
        painter.drawText(inner, Qt::AlignLeft | Qt::AlignTop, waveformName(config_.waveform));
        painter.setPen(config_.output ? QColor(120, 230, 140) : dim);
        painter.drawText(inner, Qt::AlignRight | Qt::AlignTop, state);

        painter.setFont(big);
        painter.setPen(lit);
        const QString main = config_.waveform == Waveform::Dc
                                 ? engineering(config_.offset, 4, "V")
                                 : engineering(config_.frequency, 4, "Hz");
        painter.drawText(inner, Qt::AlignLeft | Qt::AlignVCenter, main);

        painter.setFont(small);
        painter.setPen(dim);
        const QString detail =
            config_.waveform == Waveform::Dc
                ? tr("on %1").arg(net_.isEmpty() ? tr("nothing") : net_)
                : tr("%1 pk   offset %2   on %3")
                      .arg(engineering(config_.amplitude, 3, "V"),
                           engineering(config_.offset, 3, "V"),
                           net_.isEmpty() ? tr("nothing") : net_);
        painter.drawText(inner, Qt::AlignLeft | Qt::AlignBottom, detail);
    }

private:
    FunctionGenerator::Config config_;
    QString net_;
};

QLineEdit* entryField(const QString& name, QWidget* parent)
{
    auto* field = new QLineEdit(parent);
    field->setAccessibleName(name);
    field->setAlignment(Qt::AlignCenter);
    field->setMaximumWidth(96);
    field->setToolTip(QObject::tr("Type a value (for example 2.2k or 500m) and press Enter"));
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

GeneratorPanel::GeneratorPanel(QWidget* parent) : panel::PanelFace(parent)
{
    config_ = FunctionGenerator::sanitise(config_);
    build();
    showSettings();
}

void GeneratorPanel::build()
{
    auto* display = new Display(this);
    display_ = display;

    auto* waveformRow = new QHBoxLayout;
    for (std::size_t i = 0; i < kWaveforms.size(); ++i) {
        waveforms_[i] = new panel::LitButton(waveformName(kWaveforms[i]), panel::kLampWhite, this);
        waveformRow->addWidget(waveforms_[i]);
        connect(waveforms_[i], &panel::LitButton::clicked, this, [this, i] {
            config_.waveform = kWaveforms[i];
            apply();
        });
    }

    frequency_ = new panel::KnobWidget(
        tr("Frequency"),
        Knob::continuous(std::log10(FunctionGenerator::kMinFrequency),
                         std::log10(FunctionGenerator::kMaxFrequency), kDecadesPerClick,
                         std::log10(config_.frequency), 0.0),
        panel::KnobWidget::Size::Large, this);
    frequency_->setPushable(false);
    amplitude_ = new panel::KnobWidget(
        tr("Amplitude"),
        Knob::continuous(0.0, FunctionGenerator::kMaxVoltage, kVoltsPerClick, config_.amplitude,
                         0.0),
        panel::KnobWidget::Size::Large, this);
    amplitude_->setPushable(false);
    offset_ = new panel::KnobWidget(
        tr("Offset"),
        Knob::continuous(-FunctionGenerator::kMaxVoltage, FunctionGenerator::kMaxVoltage,
                         kVoltsPerClick, config_.offset, 0.0),
        panel::KnobWidget::Size::Large, this);
    frequencyEntry_ = entryField(tr("Frequency entry"), this);
    amplitudeEntry_ = entryField(tr("Amplitude entry"), this);
    offsetEntry_ = entryField(tr("Offset entry"), this);

    auto* knobs = new QGridLayout;
    knobs->addWidget(frequency_, 0, 0, Qt::AlignHCenter);
    knobs->addWidget(amplitude_, 0, 1, Qt::AlignHCenter);
    knobs->addWidget(offset_, 0, 2, Qt::AlignHCenter);
    knobs->addWidget(frequencyEntry_, 1, 0, Qt::AlignHCenter);
    knobs->addWidget(amplitudeEntry_, 1, 1, Qt::AlignHCenter);
    knobs->addWidget(offsetEntry_, 1, 2, Qt::AlignHCenter);

    output_ = new panel::LitButton(tr("Output"), panel::kLampRun, this);
    auto* outputRow = new QHBoxLayout;
    outputRow->addStretch();
    outputRow->addWidget(output_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(10);
    layout->addWidget(display_);
    layout->addLayout(waveformRow);
    layout->addLayout(knobs);
    layout->addStretch();
    layout->addLayout(outputRow);

    connect(frequency_, &panel::KnobWidget::valueChanged, this, [this](double decades) {
        config_.frequency = std::pow(10.0, decades);
        apply();
    });
    connect(amplitude_, &panel::KnobWidget::valueChanged, this, [this](double volts) {
        config_.amplitude = volts;
        apply();
    });
    connect(offset_, &panel::KnobWidget::valueChanged, this, [this](double volts) {
        config_.offset = volts;
        apply();
    });
    const auto entry = [this](QLineEdit* field, double FunctionGenerator::Config::*setting) {
        connect(field, &QLineEdit::returnPressed, this, [this, field, setting] {
            if (const std::optional<double> value = typed(field)) {
                config_.*setting = *value;
                apply();
            } else {
                showSettings(); // not a number: show the setting again
            }
        });
    };
    entry(frequencyEntry_, &FunctionGenerator::Config::frequency);
    entry(amplitudeEntry_, &FunctionGenerator::Config::amplitude);
    entry(offsetEntry_, &FunctionGenerator::Config::offset);
    connect(output_, &panel::LitButton::clicked, this, [this] {
        config_.output = !config_.output;
        apply();
    });
}

void GeneratorPanel::setConfig(const FunctionGenerator::Config& config, const QString& net)
{
    config_ = FunctionGenerator::sanitise(config);
    net_ = net;
    showSettings();
}

// The student changed a setting: clamp it as the generator would, show it, tell the owner.
void GeneratorPanel::apply()
{
    config_ = FunctionGenerator::sanitise(config_);
    showSettings();
    Q_EMIT changed();
}

void GeneratorPanel::showSettings()
{
    static_cast<Display*>(display_)->show(config_, net_);
    for (std::size_t i = 0; i < kWaveforms.size(); ++i) {
        waveforms_[i]->setLit(config_.waveform == kWaveforms[i]);
    }
    frequency_->setValue(std::log10(config_.frequency));
    amplitude_->setValue(config_.amplitude);
    offset_->setValue(config_.offset);
    frequency_->setCaption(engineering(config_.frequency, 4, "Hz"));
    amplitude_->setCaption(engineering(config_.amplitude, 3, "V") + tr(" pk"));
    offset_->setCaption(engineering(config_.offset, 3, "V"));
    const bool dc = config_.waveform == Waveform::Dc;
    frequency_->setEnabled(!dc);
    amplitude_->setEnabled(!dc);
    frequencyEntry_->setEnabled(!dc);
    amplitudeEntry_->setEnabled(!dc);
    frequencyEntry_->setText(QString::fromStdString(core::formatSpiceNumber(config_.frequency)));
    amplitudeEntry_->setText(QString::fromStdString(core::formatSpiceNumber(config_.amplitude)));
    offsetEntry_->setText(QString::fromStdString(core::formatSpiceNumber(config_.offset)));
    output_->setLit(config_.output);
}

} // namespace proton::app
