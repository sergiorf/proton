// SPDX-License-Identifier: AGPL-3.0-only
#include "scope_widget.h"

#include "proton/instruments/scope_display.h"

#include <QColor>
#include <QElapsedTimer>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace proton::app {

namespace {

using instruments::kHalfHeightDivisions;
using instruments::kHalfWidthDivisions;
using instruments::Scope;

const QColor kBackground(12, 14, 16);
const QColor kGrid(70, 76, 82);
const std::array<QColor, Scope::kChannels> kTraceColours = {QColor(240, 220, 40),
                                                            QColor(40, 210, 230)};

constexpr int kMinorTicks = 5; // per division, on the centre axes

// Divisions to pixels inside `screen`.
class Mapping
{
public:
    explicit Mapping(const QRectF& screen)
        : screen_(screen), xScale_(screen.width() / (2 * kHalfWidthDivisions)),
          yScale_(screen.height() / (2 * kHalfHeightDivisions))
    {}

    double x(double divisions) const { return screen_.center().x() + divisions * xScale_; }
    double y(double divisions) const { return screen_.center().y() - divisions * yScale_; }
    // `divisions` vertically, held at the edge of the screen.
    double clampedY(double divisions) const
    {
        return y(std::clamp(divisions, -kHalfHeightDivisions, kHalfHeightDivisions));
    }

private:
    QRectF screen_;
    double xScale_;
    double yScale_;
};

// The screen: the largest 10:8 rectangle inside `area`, leaving room for the edge markers.
QRectF screenRect(const QRectF& area)
{
    constexpr double kMargin = 14.0;
    const QRectF inner = area.adjusted(kMargin, kMargin, -kMargin, -kMargin);
    const double aspect = kHalfWidthDivisions / kHalfHeightDivisions;
    double width = inner.width();
    double height = width / aspect;
    if (height > inner.height()) {
        height = inner.height();
        width = height * aspect;
    }
    QRectF rect(0, 0, width, height);
    rect.moveCenter(inner.center());
    return rect;
}

void drawGraticule(QPainter& painter, const QRectF& screen, const Mapping& map)
{
    painter.setPen(QPen(kGrid, 1.0, Qt::DotLine));
    for (int i = 1; i < 2 * static_cast<int>(kHalfWidthDivisions); ++i) {
        const double x = map.x(i - kHalfWidthDivisions);
        painter.drawLine(QPointF(x, screen.top()), QPointF(x, screen.bottom()));
    }
    for (int i = 1; i < 2 * static_cast<int>(kHalfHeightDivisions); ++i) {
        const double y = map.y(i - kHalfHeightDivisions);
        painter.drawLine(QPointF(screen.left(), y), QPointF(screen.right(), y));
    }
    painter.setPen(QPen(kGrid, 1.0));
    painter.drawRect(screen);
    // Minor ticks along the centre axes.
    const double tick = 4.0;
    for (int i = 0; i <= 2 * static_cast<int>(kHalfWidthDivisions) * kMinorTicks; ++i) {
        const double x = map.x(static_cast<double>(i) / kMinorTicks - kHalfWidthDivisions);
        painter.drawLine(QPointF(x, map.y(0) - tick), QPointF(x, map.y(0) + tick));
    }
    for (int i = 0; i <= 2 * static_cast<int>(kHalfHeightDivisions) * kMinorTicks; ++i) {
        const double y = map.y(static_cast<double>(i) / kMinorTicks - kHalfHeightDivisions);
        painter.drawLine(QPointF(map.x(0) - tick, y), QPointF(map.x(0) + tick, y));
    }
}

void drawTrace(QPainter& painter, const QRectF& screen, const Mapping& map,
               const instruments::ScopeFrame& frame, std::size_t channel,
               const Scope::Config& config)
{
    const std::vector<double>& values = frame.values[channel];
    if (values.empty()) {
        return;
    }
    const Scope::ChannelConfig& vertical = config.channels[channel];
    painter.setPen(QPen(kTraceColours[channel], 1.0));
    const int columns = static_cast<int>(screen.width());
    if (columns >= static_cast<int>(values.size())) {
        QPolygonF line;
        line.reserve(static_cast<qsizetype>(values.size()));
        for (std::size_t i = 0; i < values.size(); ++i) {
            line.append(QPointF(map.x(instruments::sampleX(frame, i, config.timePerDiv)),
                                map.y(instruments::toDivisions(values[i], vertical))));
        }
        painter.drawPolyline(line);
        return;
    }
    // One column per pixel across the samples' span.
    const double left = map.x(instruments::sampleX(frame, 0, config.timePerDiv));
    const double right = map.x(instruments::sampleX(frame, values.size() - 1, config.timePerDiv));
    const std::vector<instruments::TraceColumn> decimated =
        instruments::decimate(values, std::max(1, static_cast<int>(right - left)));
    const double width = (right - left) / static_cast<double>(decimated.size());
    for (std::size_t c = 0; c < decimated.size(); ++c) {
        const double x = left + (static_cast<double>(c) + 0.5) * width;
        painter.drawLine(QPointF(x, map.y(instruments::toDivisions(decimated[c].min, vertical))),
                         QPointF(x, map.y(instruments::toDivisions(decimated[c].max, vertical))));
    }
}

// A small triangle with its tip at `tip`, pointing `direction` (unit vector).
void drawMarker(QPainter& painter, QPointF tip, QPointF direction, const QColor& colour)
{
    constexpr double kSize = 9.0;
    const QPointF normal(-direction.y(), direction.x());
    const QPointF base = tip - direction * kSize;
    const QPolygonF triangle = {tip, base + normal * (kSize / 2), base - normal * (kSize / 2)};
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawPolygon(triangle);
    painter.setBrush(Qt::NoBrush);
}

} // namespace

ScopeWidget::ScopeWidget(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    config_ = Scope::sanitise(config_);
}

void ScopeWidget::setConfig(const Scope::Config& config)
{
    config_ = Scope::sanitise(config);
    update();
}

void ScopeWidget::setFrame(instruments::ScopeFrame frame)
{
    frame_ = std::move(frame);
    update();
}

void ScopeWidget::clear()
{
    frame_.reset();
    update();
}

void ScopeWidget::paintEvent(QPaintEvent* /*event*/)
{
    QElapsedTimer timer;
    timer.start();
    paint();
    const double ms = static_cast<double>(timer.nsecsElapsed()) * 1e-6;
    ++paints_;
    paintTotalMs_ += ms;
    paintMaxMs_ = std::max(paintMaxMs_, ms);
}

void ScopeWidget::paint()
{
    QPainter painter(this);
    painter.fillRect(rect(), kBackground);
    const QRectF screen = screenRect(QRectF(rect()));
    const Mapping map(screen);
    drawGraticule(painter, screen, map);

    if (frame_) {
        painter.save();
        painter.setClipRect(screen);
        // CH2 first, so CH1 is on top where they overlap.
        drawTrace(painter, screen, map, *frame_, 1, config_);
        drawTrace(painter, screen, map, *frame_, 0, config_);
        painter.restore();
    }

    painter.setRenderHint(QPainter::Antialiasing);
    // Ground markers on the left edge, pointing in.
    for (std::size_t c = 0; c < Scope::kChannels; ++c) {
        const double y = map.clampedY(instruments::toDivisions(0.0, config_.channels[c]));
        drawMarker(painter, QPointF(screen.left(), y), QPointF(1, 0), kTraceColours[c]);
    }
    // Trigger level on the right edge, in the source channel's colour, pointing in.
    const std::size_t source = config_.trigger.source == Scope::TriggerSource::Channel2 ? 1 : 0;
    drawMarker(painter, QPointF(screen.right(), map.clampedY(instruments::triggerLevelY(config_))),
               QPointF(-1, 0), kTraceColours[source]);
    // Trigger position at the centre of the top edge, pointing down.
    drawMarker(painter, QPointF(map.x(0), screen.top()), QPointF(0, 1), QColor(255, 140, 40));
}

} // namespace proton::app
