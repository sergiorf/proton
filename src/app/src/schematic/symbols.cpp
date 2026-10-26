// SPDX-License-Identifier: AGPL-3.0-only
#include "symbols.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

#include <cmath>

namespace proton::app::sheet {

namespace {

using schematic::ComponentKind;
using schematic::InstrumentKind;

void line(QPainter& painter, double x1, double y1, double x2, double y2)
{
    painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
}

void resistor(QPainter& painter)
{
    line(painter, -2.0, 0.0, -1.2, 0.0);
    line(painter, 1.2, 0.0, 2.0, 0.0);
    QPolygonF zigzag;
    zigzag << QPointF(-1.2, 0.0);
    constexpr int kPeaks = 6;
    for (int i = 0; i < kPeaks; ++i) {
        const double x = -1.2 + 2.4 * (i + 0.5) / kPeaks;
        zigzag << QPointF(x, i % 2 == 0 ? -0.5 : 0.5);
    }
    zigzag << QPointF(1.2, 0.0);
    painter.drawPolyline(zigzag);
}

void capacitor(QPainter& painter)
{
    line(painter, -2.0, 0.0, -0.25, 0.0);
    line(painter, 0.25, 0.0, 2.0, 0.0);
    line(painter, -0.25, -0.9, -0.25, 0.9);
    line(painter, 0.25, -0.9, 0.25, 0.9);
}

void inductor(QPainter& painter)
{
    line(painter, -2.0, 0.0, -1.6, 0.0);
    line(painter, 1.6, 0.0, 2.0, 0.0);
    QPainterPath coil(QPointF(-1.6, 0.0));
    for (int i = 0; i < 4; ++i) {
        const double left = -1.6 + 0.8 * i;
        coil.arcTo(QRectF(left, -0.4, 0.8, 0.8), 180.0, -180.0);
    }
    painter.drawPath(coil);
}

void source(QPainter& painter, bool current)
{
    line(painter, 0.0, -2.0, 0.0, -1.0);
    line(painter, 0.0, 1.0, 0.0, 2.0);
    painter.drawEllipse(QPointF(0.0, 0.0), 1.0, 1.0);
    if (current) {
        // The current flows from the + pin through the source to the other: an arrow downwards.
        line(painter, 0.0, -0.6, 0.0, 0.6);
        painter.drawPolyline(QPolygonF{QPointF(-0.3, 0.25), QPointF(0.0, 0.6), QPointF(0.3, 0.25)});
    } else {
        line(painter, -0.25, -0.5, 0.25, -0.5); // +
        line(painter, 0.0, -0.75, 0.0, -0.25);
        line(painter, -0.25, 0.5, 0.25, 0.5); // -
    }
}

void ground(QPainter& painter)
{
    line(painter, 0.0, 0.0, 0.0, 0.8);
    line(painter, -1.0, 0.8, 1.0, 0.8);
    line(painter, -0.6, 1.2, 0.6, 1.2);
    line(painter, -0.2, 1.6, 0.2, 1.6);
}

// A sine across [left, right] about `y`, `height` from peak to peak, one period.
void sine(QPainter& painter, double left, double right, double y, double height)
{
    QPolygonF wave;
    constexpr int kPoints = 24;
    for (int i = 0; i <= kPoints; ++i) {
        const double phase = 6.283185307179586 * i / kPoints;
        wave << QPointF(left + (right - left) * i / kPoints, y - height / 2.0 * std::sin(phase));
    }
    painter.drawPolyline(wave);
}

// The instrument's case: its box, rounded, in a light fill.
void instrumentCase(QPainter& painter, double left, double top, double right, double bottom)
{
    painter.save();
    painter.setBrush(QColor(236, 238, 241));
    painter.drawRoundedRect(QRectF(QPointF(left, top), QPointF(right, bottom)), 0.4, 0.4);
    painter.restore();
}

void connectors(QPainter& painter, InstrumentKind kind)
{
    painter.save();
    painter.setBrush(painter.pen().color());
    for (const schematic::Point p : schematic::terminalOffsets(kind)) {
        painter.drawEllipse(QPointF(p.x, p.y), 0.22, 0.22);
    }
    painter.restore();
}

void generator(QPainter& painter)
{
    instrumentCase(painter, -4.0, -2.0, 2.0, 2.0);
    sine(painter, -3.4, -0.6, 0.0, 1.4);
}

void scope(QPainter& painter)
{
    instrumentCase(painter, -3.0, -3.0, 4.0, 3.0);
    painter.drawRect(QRectF(QPointF(-1.0, -2.4), QPointF(3.4, 1.0)));
    sine(painter, -0.7, 3.1, -0.7, 1.6);
    painter.drawEllipse(QPointF(0.2, 1.9), 0.35, 0.35);
    painter.drawEllipse(QPointF(1.4, 1.9), 0.35, 0.35);
    painter.drawEllipse(QPointF(2.6, 1.9), 0.35, 0.35);
}

void multimeter(QPainter& painter)
{
    instrumentCase(painter, -2.0, -3.0, 2.0, 2.0);
    painter.drawRect(QRectF(QPointF(-1.5, -2.5), QPointF(1.5, -1.2)));
    painter.drawEllipse(QPointF(0.0, -0.1), 0.6, 0.6);
    painter.drawLine(QPointF(0.0, -0.1), QPointF(0.35, -0.55));
}

} // namespace

void drawSymbol(QPainter& painter, InstrumentKind kind)
{
    switch (kind) {
    case InstrumentKind::Generator:
        generator(painter);
        break;
    case InstrumentKind::Scope:
        scope(painter);
        break;
    case InstrumentKind::Multimeter:
        multimeter(painter);
        break;
    }
    connectors(painter, kind);
}

std::vector<QPointF> terminalLabelOffsets(InstrumentKind kind)
{
    switch (kind) {
    case InstrumentKind::Generator:
        return {{1.0, -1.0}, {0.9, 1.0}};
    case InstrumentKind::Scope:
        return {{-2.1, -1.0}, {-2.1, 1.0}};
    case InstrumentKind::Multimeter:
        return {{-1.0, 1.3}, {1.0, 1.3}};
    }
    return {};
}

void drawSymbol(QPainter& painter, ComponentKind kind)
{
    switch (kind) {
    case ComponentKind::Resistor:
        resistor(painter);
        break;
    case ComponentKind::Capacitor:
        capacitor(painter);
        break;
    case ComponentKind::Inductor:
        inductor(painter);
        break;
    case ComponentKind::VoltageSource:
        source(painter, false);
        break;
    case ComponentKind::CurrentSource:
        source(painter, true);
        break;
    case ComponentKind::Ground:
        ground(painter);
        break;
    }
}

void enterPlacement(QPainter& painter, const schematic::Placement& placement)
{
    painter.translate(placement.position.x, placement.position.y);
    painter.rotate(90.0 * static_cast<int>(placement.rotation));
    if (placement.mirrored) {
        painter.scale(-1.0, 1.0);
    }
}

} // namespace proton::app::sheet
