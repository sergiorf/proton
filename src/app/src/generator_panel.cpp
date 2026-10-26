// SPDX-License-Identifier: AGPL-3.0-only
#include "generator_panel.h"

#include "panel/knob_widget.h"
#include "panel/lit_button.h"
#include "panel/panel_style.h"

#include "proton/core/format.h"
#include "proton/core/spice_number.h"
#include "proton/instruments/knob.h"

#include <QComboBox>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace proton::app {

namespace {

using instruments::DigitEntry;
using instruments::FunctionGenerator;
using Setting = GeneratorPanel::Setting;
using Waveform = FunctionGenerator::Waveform;

constexpr std::array<Waveform, 4> kWaveforms = {Waveform::Sine, Waveform::Square,
                                                Waveform::Triangle, Waveform::Dc};
constexpr std::array<Setting, 3> kSettings = {Setting::Frequency, Setting::Amplitude,
                                              Setting::Offset};

// The digit entries: the frequency to the millihertz, its cursor up to the 10 MHz digit, picking
// its prefix from the value; the voltages to the millivolt in volts, the cursor up to the tens.
DigitEntry frequencyEntry(double value)
{
    return DigitEntry(-3, 7, FunctionGenerator::kMinFrequency, FunctionGenerator::kMaxFrequency,
                      value, 2);
}

DigitEntry voltageEntry(double min, double value)
{
    return DigitEntry(-3, 1, min, FunctionGenerator::kMaxVoltage, value, -1, 0);
}

// A unit list's choices and what each is worth.
struct Unit
{
    const char* name;
    double factor;
};
constexpr std::array<Unit, 3> kFrequencyUnits = {{{"Hz", 1.0}, {"kHz", 1e3}, {"MHz", 1e6}}};
constexpr std::array<Unit, 2> kVoltageUnits = {{{"mV", 1e-3}, {"V", 1.0}}};

// The unit a field shows `value` in: the largest that keeps the number at 1 or more, the plain
// unit for zero.
template <std::size_t N>
std::size_t unitFor(const std::array<Unit, N>& units, double value)
{
    for (std::size_t i = units.size(); i-- > 0;) {
        if (value == 0.0 ? units[i].factor == 1.0 : std::fabs(value) >= units[i].factor) {
            return i;
        }
    }
    return 0;
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

// The generator's display: the settings in a blue backlit LCD, as a bench generator shows them,
// each digit entry's cursor lit for the setting in use, and a click on a digit moving the cursor
// there (onDigit).
class Display : public QWidget
{
public:
    explicit Display(QWidget* parent) : QWidget(parent)
    {
        setMinimumHeight(96);
        setCursor(Qt::PointingHandCursor);
        setAccessibleName(tr("Generator display"));
        setToolTip(tr("Click a digit to turn it with its knob"));
    }

    struct Shown
    {
        FunctionGenerator::Config config;
        QString net;
        std::array<DigitEntry::Text, 3> texts; // by Setting
        Setting active = Setting::Frequency;
    };

    void show(Shown shown)
    {
        shown_ = std::move(shown);
        update();
    }

    // The centre of the character at `index` of `setting`'s text on the display; none if it is
    // not shown. From the display's size, so it holds before the display is first painted.
    std::optional<QPointF> charCentre(Setting setting, std::size_t index) const
    {
        const Place& place = layout().places[static_cast<std::size_t>(setting)];
        if (!place.shown) {
            return std::nullopt;
        }
        return QPointF(place.left + (static_cast<double>(index) + 0.5) * place.charWidth,
                       place.top + place.height / 2.0);
    }

    std::function<void(Setting, std::size_t)> onDigit;

    QSize sizeHint() const override { return {360, 110}; }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        const QPointF at = event->position();
        const Layout shape = layout();
        for (const Setting setting : kSettings) {
            const Place& place = shape.places[static_cast<std::size_t>(setting)];
            if (!place.shown || at.y() < place.top || at.y() > place.top + place.height ||
                at.x() < place.left) {
                continue;
            }
            const auto index = static_cast<std::size_t>((at.x() - place.left) / place.charWidth);
            if (index < shown_.texts[static_cast<std::size_t>(setting)].text.size() && onDigit) {
                onDigit(setting, index);
                return;
            }
        }
    }

    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(panel::kPanelEdge);
        painter.drawRoundedRect(QRectF(rect()), 8, 8);
        const Layout shape = layout();
        painter.setBrush(QColor(22, 44, 78));
        painter.drawRoundedRect(shape.glass, 4, 4);

        const QColor lit(170, 220, 255);
        const QColor dim(110, 150, 190);
        const FunctionGenerator::Config& config = shown_.config;
        const QFontMetricsF smallMetrics(shape.small);

        // The top line: the waveform and the output.
        painter.setFont(shape.small);
        painter.setPen(dim);
        painter.drawText(QPointF(shape.inner.left(), shape.inner.top() + smallMetrics.ascent()),
                         waveformName(config.waveform));
        painter.setPen(config.output ? QColor(120, 230, 140) : dim);
        const QString state = config.output ? tr("OUTPUT ON") : tr("OUTPUT OFF");
        painter.drawText(QPointF(shape.inner.right() - smallMetrics.horizontalAdvance(state),
                                 shape.inner.top() + smallMetrics.ascent()),
                         state);

        // The main line, the frequency or the level for DC, and the bottom line's settings.
        for (const Setting setting : kSettings) {
            const Place& place = shape.places[static_cast<std::size_t>(setting)];
            if (place.shown) {
                draw(painter, place, setting, place.big ? lit : dim);
            }
        }
        painter.setFont(shape.small);
        painter.setPen(dim);
        for (const auto& [x, text] : shape.labels) {
            painter.drawText(QPointF(x, shape.bottomTop + smallMetrics.ascent()), text);
        }
    }

private:
    // Where a setting's text is drawn.
    struct Place
    {
        bool shown = false;
        bool big = false; // the main line
        QFont font;
        double left = 0.0;
        double top = 0.0;
        double charWidth = 0.0;
        double height = 0.0;
    };

    // Where everything on the display goes, from its size: drawn, clicked and found the same way.
    struct Layout
    {
        QRectF glass;
        QRectF inner;
        QFont small;
        double bottomTop = 0.0;
        std::array<Place, 3> places{};               // by Setting
        std::vector<std::pair<double, QString>> labels; // the bottom line's words, by x
    };

    Layout layout() const
    {
        Layout shape;
        shape.glass = QRectF(rect()).adjusted(6, 6, -6, -6);
        shape.inner = shape.glass.adjusted(12, 6, -12, -6);
        QFont big = font();
        big.setFamily(QStringLiteral("Consolas"));
        big.setStyleHint(QFont::Monospace);
        big.setPixelSize(static_cast<int>(std::clamp(shape.glass.height() * 0.3, 16.0, 30.0)));
        shape.small = big;
        shape.small.setPixelSize(
            static_cast<int>(std::clamp(shape.glass.height() * 0.16, 11.0, 16.0)));
        const QFontMetricsF bigMetrics(big);
        const QFontMetricsF smallMetrics(shape.small);
        const auto place = [&](Setting setting, const QFont& font, double left, double top,
                               bool isBig) {
            const QFontMetricsF metrics(font);
            Place& p = shape.places[static_cast<std::size_t>(setting)];
            p = {true, isBig, font, left, top, metrics.horizontalAdvance(QLatin1Char('0')),
                 metrics.height()};
            const auto length = static_cast<double>(
                QString::fromStdString(shown_.texts[static_cast<std::size_t>(setting)].text)
                    .size());
            return left + p.charWidth * length;
        };

        const bool dc = shown_.config.waveform == Waveform::Dc;
        place(dc ? Setting::Offset : Setting::Frequency, big, shape.inner.left(),
              shape.inner.center().y() - bigMetrics.height() / 2.0, true);

        // The bottom line: amplitude and offset, and the net the output is on.
        shape.bottomTop = shape.inner.bottom() - smallMetrics.height();
        double x = shape.inner.left();
        const auto label = [&](const QString& text) {
            shape.labels.emplace_back(x, text);
            x += smallMetrics.horizontalAdvance(text);
        };
        if (!dc) {
            label(tr("AMPL "));
            x = place(Setting::Amplitude, shape.small, x, shape.bottomTop, false);
            label(tr(" pk   OFFS "));
            x = place(Setting::Offset, shape.small, x, shape.bottomTop, false);
            label(QStringLiteral("   "));
        }
        label(tr("on %1").arg(shown_.net.isEmpty() ? tr("nothing") : shown_.net));
        return shape;
    }

    // Draws `setting`'s text at `place`, its cursor's digit lit when it is the active setting.
    void draw(QPainter& painter, const Place& place, Setting setting, const QColor& colour) const
    {
        const DigitEntry::Text& text = shown_.texts[static_cast<std::size_t>(setting)];
        if (setting == shown_.active) {
            const QRectF cell(place.left + static_cast<double>(text.cursor) * place.charWidth,
                              place.top, place.charWidth, place.height);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(170, 220, 255, 60));
            painter.drawRect(cell);
            painter.setBrush(QColor(170, 220, 255));
            painter.drawRect(QRectF(cell.left(), cell.bottom() - 2.0, cell.width(), 2.0));
        }
        painter.setFont(place.font);
        painter.setPen(setting == shown_.active ? QColor(200, 235, 255) : colour);
        painter.drawText(QPointF(place.left, place.top + QFontMetricsF(place.font).ascent()),
                         QString::fromStdString(text.text));
    }

    Shown shown_;
};

QLineEdit* entryField(const QString& name, QWidget* parent)
{
    auto* field = new QLineEdit(parent);
    field->setAccessibleName(name);
    field->setAlignment(Qt::AlignCenter);
    field->setMaximumWidth(72);
    field->setToolTip(
        QObject::tr("Type a number in the unit beside it and press Enter, or choose the unit"));
    field->setStyleSheet(QStringLiteral("QLineEdit { background: #1c1e21; color: #d6d9dd; "
                                        "border: 1px solid #54595f; border-radius: 4px; }"));
    return field;
}

template <std::size_t N>
QComboBox* unitList(const QString& name, const std::array<Unit, N>& units, QWidget* parent)
{
    auto* list = new QComboBox(parent);
    list->setAccessibleName(name);
    for (const Unit& unit : units) {
        list->addItem(QString::fromLatin1(unit.name), unit.factor);
    }
    return list;
}

// The number typed into `field` in the unit chosen in `unit`, or nothing if it is not one.
std::optional<double> typed(const QLineEdit* field, const QComboBox* unit)
{
    const std::optional<double> number =
        core::parseSpiceNumber(field->text().trimmed().toStdString());
    if (!number) {
        return std::nullopt;
    }
    return *number * unit->currentData().toDouble();
}

// `value` in the unit chosen for it: the field's text, and the list set to the unit.
template <std::size_t N>
void showIn(QLineEdit* field, QComboBox* list, const std::array<Unit, N>& units, double value)
{
    const std::size_t unit = unitFor(units, value);
    list->setCurrentIndex(static_cast<int>(unit));
    field->setText(QString::number(core::roundToSignificant(value / units[unit].factor, 10), 'g',
                                   10));
}

} // namespace

GeneratorPanel::GeneratorPanel(QWidget* parent)
    : panel::PanelFace(parent),
      frequencyDigits_(frequencyEntry(1e3)),
      amplitudeDigits_(voltageEntry(0.0, 1.0)),
      offsetDigits_(voltageEntry(-FunctionGenerator::kMaxVoltage, 0.0))
{
    config_ = FunctionGenerator::sanitise(config_);
    frequencyDigits_.setValue(config_.frequency);
    amplitudeDigits_.setValue(config_.amplitude);
    offsetDigits_.setValue(config_.offset);
    build();
    showSettings();
}

DigitEntry& GeneratorPanel::entry(Setting setting)
{
    switch (setting) {
    case Setting::Frequency:
        return frequencyDigits_;
    case Setting::Amplitude:
        return amplitudeDigits_;
    case Setting::Offset:
        return offsetDigits_;
    }
    return frequencyDigits_;
}

const DigitEntry& GeneratorPanel::digits(Setting setting) const
{
    return const_cast<GeneratorPanel*>(this)->entry(setting);
}

QPoint GeneratorPanel::digitPosition(Setting setting, int exponent) const
{
    const DigitEntry::Text text = digits(setting).text({});
    for (std::size_t i = 0; i < text.exponents.size(); ++i) {
        if (text.exponents[i] == std::optional<int>(exponent)) {
            if (const auto centre = static_cast<Display*>(display_)->charCentre(setting, i)) {
                return centre->toPoint();
            }
        }
    }
    return {};
}

void GeneratorPanel::build()
{
    auto* display = new Display(this);
    display->onDigit = [this](Setting setting, std::size_t index) { selectDigit(setting, index); };
    display_ = display;

    auto* waveformRow = new QHBoxLayout;
    for (std::size_t i = 0; i < kWaveforms.size(); ++i) {
        waveforms_[i] = new panel::LitButton(waveformName(kWaveforms[i]), panel::kLampWhite, this);
        waveformRow->addWidget(waveforms_[i]);
        connect(waveforms_[i], &panel::LitButton::clicked, this, [this, i] {
            config_.waveform = kWaveforms[i];
            if (config_.waveform == Waveform::Dc && active_ != Setting::Offset) {
                active_ = Setting::Offset; // DC has only its level to set
            }
            apply();
        });
    }

    const auto knob = [this](const QString& name) {
        auto* widget = new panel::KnobWidget(name, instruments::Knob::continuous(0, 1, 1, 0, 0),
                                             panel::KnobWidget::Size::Large, this);
        widget->setEncoder();
        widget->setToolTip(tr("%1: turn to change the lit digit; Left and Right move to "
                              "another digit")
                               .arg(name));
        return widget;
    };
    frequency_ = knob(tr("Frequency"));
    amplitude_ = knob(tr("Amplitude"));
    offset_ = knob(tr("Offset"));
    frequencyEntry_ = entryField(tr("Frequency entry"), this);
    amplitudeEntry_ = entryField(tr("Amplitude entry"), this);
    offsetEntry_ = entryField(tr("Offset entry"), this);
    frequencyUnit_ = unitList(tr("Frequency unit"), kFrequencyUnits, this);
    amplitudeUnit_ = unitList(tr("Amplitude unit"), kVoltageUnits, this);
    offsetUnit_ = unitList(tr("Offset unit"), kVoltageUnits, this);

    auto* knobs = new QGridLayout;
    const std::array<std::pair<QLineEdit*, QComboBox*>, 3> fields = {
        std::pair{frequencyEntry_, frequencyUnit_}, std::pair{amplitudeEntry_, amplitudeUnit_},
        std::pair{offsetEntry_, offsetUnit_}};
    const std::array<panel::KnobWidget*, 3> knobWidgets = {frequency_, amplitude_, offset_};
    for (std::size_t i = 0; i < kSettings.size(); ++i) {
        const int column = static_cast<int>(i);
        knobs->addWidget(knobWidgets[i], 0, column, Qt::AlignHCenter);
        auto* typing = new QHBoxLayout;
        typing->setSpacing(4);
        typing->addWidget(fields[i].first);
        typing->addWidget(fields[i].second);
        knobs->addLayout(typing, 1, column, Qt::AlignHCenter);

        const Setting setting = kSettings[i];
        connect(knobWidgets[i], &panel::KnobWidget::turned, this,
                [this, setting](int clicks) { turn(setting, clicks); });
        connect(knobWidgets[i], &panel::KnobWidget::cursorMoved, this,
                [this, setting](int places) { moveCursor(setting, places); });
        // A typed number applies on Enter, or when its unit is chosen, as a unit key does.
        const auto enter = [this, setting, field = fields[i].first, unit = fields[i].second] {
            if (const std::optional<double> value = typed(field, unit)) {
                active_ = setting;
                switch (setting) {
                case Setting::Frequency:
                    config_.frequency = *value;
                    break;
                case Setting::Amplitude:
                    config_.amplitude = *value;
                    break;
                case Setting::Offset:
                    config_.offset = *value;
                    break;
                }
                apply();
            } else {
                showSettings(); // not a number: show the setting again
            }
        };
        connect(fields[i].first, &QLineEdit::returnPressed, this, enter);
        connect(fields[i].second, &QComboBox::activated, this, enter);
    }

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

    connect(output_, &panel::LitButton::clicked, this, [this] {
        config_.output = !config_.output;
        apply();
    });
}

void GeneratorPanel::turn(Setting setting, int clicks)
{
    active_ = setting;
    DigitEntry& digits = entry(setting);
    if (!digits.turn(clicks)) {
        showSettings(); // at the end of the range: the cursor is lit all the same
        return;
    }
    switch (setting) {
    case Setting::Frequency:
        config_.frequency = digits.value();
        break;
    case Setting::Amplitude:
        config_.amplitude = digits.value();
        break;
    case Setting::Offset:
        config_.offset = digits.value();
        break;
    }
    apply();
}

void GeneratorPanel::moveCursor(Setting setting, int places)
{
    active_ = setting;
    entry(setting).moveCursor(places);
    showSettings();
}

void GeneratorPanel::selectDigit(Setting setting, std::size_t index)
{
    if (!entry(setting).setCursorAt(index)) {
        return;
    }
    active_ = setting;
    // The knob of that setting takes the arrow keys from here.
    const std::array<panel::KnobWidget*, 3> knobWidgets = {frequency_, amplitude_, offset_};
    knobWidgets[static_cast<std::size_t>(setting)]->setFocus(Qt::MouseFocusReason);
    showSettings();
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
    // The digit entries follow the settings, as clamped, their cursors where the student left them.
    frequencyDigits_.setValue(config_.frequency);
    amplitudeDigits_.setValue(config_.amplitude);
    offsetDigits_.setValue(config_.offset);
    const bool dc = config_.waveform == Waveform::Dc;
    if (dc && active_ != Setting::Offset) {
        active_ = Setting::Offset;
    }
    static_cast<Display*>(display_)->show(
        {config_,
         net_,
         {frequencyDigits_.text("Hz"), amplitudeDigits_.text("V"), offsetDigits_.text("V")},
         active_});
    for (std::size_t i = 0; i < kWaveforms.size(); ++i) {
        waveforms_[i]->setLit(config_.waveform == kWaveforms[i]);
    }
    frequency_->setCaption(QString::fromStdString(frequencyDigits_.text("Hz").text));
    amplitude_->setCaption(QString::fromStdString(amplitudeDigits_.text("V").text) + tr(" pk"));
    offset_->setCaption(QString::fromStdString(offsetDigits_.text("V").text));
    frequency_->setEnabled(!dc);
    amplitude_->setEnabled(!dc);
    frequencyEntry_->setEnabled(!dc);
    amplitudeEntry_->setEnabled(!dc);
    frequencyUnit_->setEnabled(!dc);
    amplitudeUnit_->setEnabled(!dc);
    showIn(frequencyEntry_, frequencyUnit_, kFrequencyUnits, config_.frequency);
    showIn(amplitudeEntry_, amplitudeUnit_, kVoltageUnits, config_.amplitude);
    showIn(offsetEntry_, offsetUnit_, kVoltageUnits, config_.offset);
    output_->setLit(config_.output);
}

} // namespace proton::app
