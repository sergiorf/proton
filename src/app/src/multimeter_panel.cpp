// SPDX-License-Identifier: AGPL-3.0-only
#include "multimeter_panel.h"

#include "panel/lit_button.h"
#include "panel/panel_style.h"
#include "panel/segment_display.h"
#include "panel/selector_widget.h"

#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <utility>

namespace proton::app {

namespace {

using instruments::Multimeter;

constexpr int kDisplayCells = 6; // sign and five digit places: "-1.9999", "-199.99"
const QString kDashes = QStringLiteral("-----");

// The dial's positions, in order, and the modes they select.
constexpr std::array<Multimeter::Mode, 2> kModes = {Multimeter::Mode::DcVoltage,
                                                    Multimeter::Mode::AcVoltage};

std::size_t positionOf(Multimeter::Mode mode)
{
    for (std::size_t i = 0; i < kModes.size(); ++i) {
        if (kModes[i] == mode) {
            return i;
        }
    }
    return 0;
}

// The input jacks: + (red) and COM (black), each with the net it is on (ADR 0009).
class Jacks : public QWidget
{
public:
    explicit Jacks(QWidget* parent) : QWidget(parent) {}

    void setNets(const QString& plus, const QString& com)
    {
        plus_ = plus;
        com_ = com;
        update();
    }

    QSize sizeHint() const override { return {200, 74}; }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QFontMetrics metrics(font());
        const double r = 13.0;
        const std::array<std::pair<QString, QColor>, 2> jacks = {
            std::pair{QStringLiteral("COM"), QColor(30, 30, 32)},
            std::pair{QStringLiteral("+ V"), QColor(200, 40, 35)}};
        for (std::size_t i = 0; i < jacks.size(); ++i) {
            const QPointF c(width() * (0.3 + 0.4 * static_cast<double>(i)), r + 6);
            painter.setPen(QPen(panel::kPanelEdge, 2.0));
            painter.setBrush(jacks[i].second);
            painter.drawEllipse(c, r, r);
            painter.setBrush(QColor(10, 10, 10));
            painter.drawEllipse(c, r * 0.4, r * 0.4);
            painter.setPen(panel::kLettering);
            painter.drawText(QRectF(c.x() - 50, c.y() + r + 2, 100, metrics.height()),
                             Qt::AlignCenter, jacks[i].first);
            const QString& on = i == 0 ? com_ : plus_;
            QString net = on;
            if (on.isEmpty()) {
                net = tr("open");
            } else if (on == QStringLiteral("0")) {
                net = tr("ground");
            }
            painter.setPen(panel::kDimLettering);
            painter.drawText(
                QRectF(c.x() - 60, c.y() + r + 2 + metrics.height(), 120, metrics.height()),
                Qt::AlignCenter, tr("on %1").arg(net));
        }
    }

private:
    QString plus_;
    QString com_;
};

} // namespace

MultimeterPanel::MultimeterPanel(QWidget* parent) : panel::PanelFace(parent)
{
    config_ = Multimeter::sanitise(config_);
    build();
    showSettings();
}

void MultimeterPanel::build()
{
    display_ = new panel::SegmentDisplay(kDisplayCells, this);
    display_->setMaximumHeight(150);
    // The DC symbol (a line over a dashed line, U+2393) and a tilde for AC, as printed on meters.
    mode_ = new panel::SelectorWidget(
        tr("Function"), {tr("V %1 DC").arg(QChar(0x2393)), tr("V ~ AC")}, 60.0, this);
    mode_->setMaximumSize(280, 230);
    range_ = new panel::LitButton(tr("Range"), panel::kLampWhite, this);
    auto_ = new panel::LitButton(tr("Auto"), panel::kLampWhite, this);
    auto* jacks = new Jacks(this);
    jacks_ = jacks;

    auto* buttons = new QVBoxLayout;
    buttons->addStretch();
    buttons->addWidget(range_);
    buttons->addWidget(auto_);
    buttons->addStretch();

    auto* middle = new QHBoxLayout;
    middle->addWidget(mode_, 1);
    middle->addLayout(buttons);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(10);
    layout->addWidget(display_);
    layout->addLayout(middle);
    layout->addStretch();
    layout->addWidget(jacks_);

    connect(mode_, &panel::SelectorWidget::positionChanged, this, [this](std::size_t position) {
        config_.mode = kModes[std::min(position, kModes.size() - 1)];
        apply();
    });
    connect(range_, &panel::LitButton::clicked, this, [this] {
        config_.range = Multimeter::nextRange(config_.range, last_ ? last_->range : 0.0);
        apply();
    });
    connect(auto_, &panel::LitButton::clicked, this, [this] {
        if (config_.range != Multimeter::Range::Auto) {
            config_.range = Multimeter::Range::Auto;
            apply();
        }
    });
}

void MultimeterPanel::setConfig(const Multimeter::Config& config, const QString& plus,
                                const QString& com)
{
    config_ = Multimeter::sanitise(config);
    static_cast<Jacks*>(jacks_)->setNets(plus, com);
    clear();
    showSettings();
}

void MultimeterPanel::showReading(const instruments::DmmReading& reading)
{
    last_ = reading;
    display_->setReading(QString::fromStdString(reading.display.text),
                         QString::fromStdString(reading.display.unit));
}

void MultimeterPanel::clear()
{
    last_.reset();
    display_->setReading(kDashes, QStringLiteral("V"));
}

QString MultimeterPanel::displayText() const
{
    return display_->text();
}

QString MultimeterPanel::displayUnit() const
{
    return display_->unit();
}

// The student changed a setting: a new gate starts, so the display waits for its reading.
void MultimeterPanel::apply()
{
    showSettings();
    Q_EMIT changed();
}

void MultimeterPanel::showSettings()
{
    mode_->setPosition(positionOf(config_.mode));
    const bool automatic = config_.range == Multimeter::Range::Auto;
    auto_->setLit(automatic);
    range_->setLit(!automatic);
    QStringList annunciators;
    if (automatic) {
        annunciators << tr("AUTO");
    }
    annunciators << (config_.mode == Multimeter::Mode::AcVoltage ? tr("AC") : tr("DC"));
    display_->setAnnunciators(annunciators);
}

} // namespace proton::app
