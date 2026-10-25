// SPDX-License-Identifier: AGPL-3.0-only
#include "segment_display.h"

#include "panel_style.h"

#include "proton/instruments/seven_segment.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace proton::app::panel {

namespace {

using instruments::DisplayCell;

const QColor kLcd(170, 184, 160);      // the LCD glass
const QColor kSegmentOn(28, 32, 30);   // a lit (dark) segment
const QColor kSegmentOff(160, 174, 151); // an unlit segment, faintly visible

// The polygon of one segment of a digit in a cell of `w` x `h` pixels at `origin`, slanted a
// little as LCD digits are. `t` is the segment thickness.
QPolygonF segmentShape(int segment, QPointF origin, double w, double h, double t)
{
    const double slant = 0.12 * w; // the top is this much further right than the bottom
    const auto at = [&](double x, double y) {
        return origin + QPointF(x + slant * (1.0 - y / h), y);
    };
    const double half = h / 2.0;
    const double g = t * 0.25; // gap between segments
    auto horizontal = [&](double y) {
        return QPolygonF{at(t / 2 + g, y), at(t + g, y - t / 2), at(w - t - g, y - t / 2),
                         at(w - t / 2 - g, y), at(w - t - g, y + t / 2), at(t + g, y + t / 2)};
    };
    auto vertical = [&](double x, double y0, double y1) {
        return QPolygonF{at(x, y0 + g), at(x + t / 2, y0 + t / 2 + g), at(x + t / 2, y1 - t / 2 - g),
                         at(x, y1 - g), at(x - t / 2, y1 - t / 2 - g), at(x - t / 2, y0 + t / 2 + g)};
    };
    switch (segment) {
    case 0: return horizontal(t / 2);                  // a
    case 1: return vertical(w - t / 2, t / 2, half);   // b
    case 2: return vertical(w - t / 2, half, h - t / 2); // c
    case 3: return horizontal(h - t / 2);              // d
    case 4: return vertical(t / 2, half, h - t / 2);   // e
    case 5: return vertical(t / 2, t / 2, half);       // f
    default: return horizontal(half);                  // g
    }
}

} // namespace

SegmentDisplay::SegmentDisplay(int cells, QWidget* parent)
    : QWidget(parent), cells_(std::max(1, cells))
{
    setAccessibleName(tr("Display"));
}

void SegmentDisplay::setReading(const QString& text, const QString& unit)
{
    if (text != text_ || unit != unit_) {
        text_ = text;
        unit_ = unit;
        setAccessibleDescription(text + QLatin1Char(' ') + unit);
        update();
    }
}

void SegmentDisplay::setAnnunciators(const QStringList& annunciators)
{
    if (annunciators != annunciators_) {
        annunciators_ = annunciators;
        update();
    }
}

void SegmentDisplay::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // The bezel and the glass.
    painter.setPen(Qt::NoPen);
    painter.setBrush(kPanelEdge);
    painter.drawRoundedRect(QRectF(rect()), 8, 8);
    const QRectF glass = QRectF(rect()).adjusted(6, 6, -6, -6);
    QLinearGradient shade(glass.topLeft(), glass.bottomLeft());
    shade.setColorAt(0.0, kLcd.lighter(106));
    shade.setColorAt(1.0, kLcd.darker(106));
    painter.setBrush(shade);
    painter.drawRoundedRect(glass, 4, 4);

    // Annunciators along the top, the unit at the right.
    QFont small = font();
    small.setBold(true);
    small.setPixelSize(static_cast<int>(std::clamp(glass.height() * 0.11, 10.0, 15.0)));
    painter.setFont(small);
    painter.setPen(kSegmentOn);
    const QRectF top = glass.adjusted(10, 4, -10, 0);
    painter.drawText(top, Qt::AlignLeft | Qt::AlignTop, annunciators_.join(QStringLiteral("  ")));
    QFont unitFont = font();
    unitFont.setBold(true);
    unitFont.setPixelSize(static_cast<int>(std::clamp(glass.height() * 0.24, 14.0, 30.0)));

    // The digits fill what is left, keeping a digit's proportions.
    const double unitWidth = glass.width() * 0.16;
    const QRectF digits = glass.adjusted(10, glass.height() * 0.22, -unitWidth, -glass.height() * 0.1);
    const double cell = std::min(digits.width() / cells_, digits.height() * 0.62);
    const double h = cell / 0.62;
    const double w = cell * 0.72;
    const double t = std::max(2.0, w * 0.17);
    const double left = digits.right() - cell * cells_;
    const double topY = digits.center().y() - h / 2;

    painter.setPen(Qt::NoPen); // segments have no outline: an unlit one must stay faint
    const std::vector<DisplayCell> layout =
        instruments::layoutDisplay(text_.toStdString(), static_cast<std::size_t>(cells_));
    for (int i = 0; i < cells_; ++i) {
        const QPointF origin(left + cell * i + (cell - w) / 2, topY);
        const DisplayCell& c = layout[static_cast<std::size_t>(i)];
        for (int s = 0; s < 7; ++s) {
            painter.setBrush(((c.segments >> s) & 1) != 0 ? kSegmentOn : kSegmentOff);
            painter.drawPolygon(segmentShape(s, origin, w, h, t));
        }
        painter.setBrush(c.point ? kSegmentOn : kSegmentOff);
        painter.drawEllipse(QPointF(origin.x() + w + t * 0.6, topY + h - t / 2), t * 0.45,
                            t * 0.45);
    }

    painter.setFont(unitFont);
    painter.setPen(kSegmentOn);
    painter.drawText(QRectF(glass.right() - unitWidth, digits.top(), unitWidth - 8, digits.height()),
                     Qt::AlignRight | Qt::AlignBottom, unit_);
}

} // namespace proton::app::panel
