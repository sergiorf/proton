// SPDX-License-Identifier: AGPL-3.0-only
#include "icons.h"

#include <QColor>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPolygonF>

namespace proton::app {

namespace {

constexpr double kBox = 24.0; // every icon is drawn in a 24 x 24 box
constexpr double kLine = 1.7;

const QColor kWire(20, 90, 160);
const QColor kRun(40, 150, 60);
const QColor kPause(215, 150, 20);
const QColor kStop(200, 45, 40);

// The line and fill colours follow the palette, so the icons read on a light or a dark theme.
QColor ink()
{
    return QGuiApplication::palette().color(QPalette::WindowText);
}

QColor paper()
{
    return QGuiApplication::palette().color(QPalette::Base);
}

QPen pen(const QColor& colour, double width = kLine)
{
    QPen p(colour, width);
    p.setCapStyle(Qt::RoundCap);
    p.setJoinStyle(Qt::RoundJoin);
    return p;
}

// A sheet of paper with its top right corner folded, at (x, y), w x h.
void page(QPainter& p, double x, double y, double w, double h)
{
    const double fold = w * 0.32;
    QPolygonF outline;
    outline << QPointF(x, y) << QPointF(x + w - fold, y) << QPointF(x + w, y + fold)
            << QPointF(x + w, y + h) << QPointF(x, y + h);
    p.setBrush(paper());
    p.drawPolygon(outline);
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(QPolygonF() << QPointF(x + w - fold, y) << QPointF(x + w - fold, y + fold)
                               << QPointF(x + w, y + fold));
}

// An arrow head at `tip`, pointing along `angle` degrees (0 to the right, 90 down).
void arrowHead(QPainter& p, QPointF tip, double angle, double size = 4.5)
{
    p.save();
    p.translate(tip);
    p.rotate(angle);
    p.drawPolyline(QPolygonF() << QPointF(-size, -size * 0.8) << QPointF(0, 0)
                               << QPointF(-size, size * 0.8));
    p.restore();
}

// A magnifying glass, its lens centred at (10, 10).
void magnifier(QPainter& p)
{
    p.drawEllipse(QPointF(10, 10), 6.5, 6.5);
    p.setPen(pen(ink(), 3.0));
    p.drawLine(QPointF(14.8, 14.8), QPointF(20.5, 20.5));
    p.setPen(pen(ink()));
}

// A quarter-turn arrow round (12, 12): clockwise, or anticlockwise when mirrored by the caller.
void turnArrow(QPainter& p)
{
    QPainterPath arc;
    arc.arcMoveTo(QRectF(4, 5, 16, 16), 160);
    arc.arcTo(QRectF(4, 5, 16, 16), 160, -150);
    p.drawPath(arc);
    arrowHead(p, arc.currentPosition(), 100);
}

void draw(QPainter& p, Icon which)
{
    p.setPen(pen(ink()));
    p.setBrush(Qt::NoBrush);
    switch (which) {
    case Icon::New:
        page(p, 5, 3, 14, 18);
        break;
    case Icon::Open:
        // A folder, its front flap open.
        p.drawPolyline(QPolygonF() << QPointF(3, 18) << QPointF(3, 6) << QPointF(9, 6)
                                   << QPointF(11, 8) << QPointF(19, 8) << QPointF(19, 11));
        p.setBrush(paper());
        p.drawPolygon(QPolygonF() << QPointF(3, 18) << QPointF(6.5, 11) << QPointF(21.5, 11)
                                  << QPointF(18, 18));
        p.setBrush(Qt::NoBrush);
        break;
    case Icon::Save:
        // An arrow down into a tray.
        p.drawLine(QPointF(12, 3.5), QPointF(12, 14));
        arrowHead(p, QPointF(12, 14.5), 90);
        p.drawPolyline(QPolygonF() << QPointF(4, 14) << QPointF(4, 20) << QPointF(20, 20)
                                   << QPointF(20, 14));
        break;
    case Icon::ExportImage:
        p.drawRect(QRectF(3, 5, 18, 14));
        p.drawPolyline(QPolygonF() << QPointF(5, 17) << QPointF(10, 11) << QPointF(13, 14)
                                   << QPointF(15.5, 12) << QPointF(19, 17));
        p.drawEllipse(QPointF(16, 8.8), 1.4, 1.4);
        break;
    case Icon::Undo:
    case Icon::Redo: {
        if (which == Icon::Redo) {
            p.translate(kBox, 0);
            p.scale(-1, 1);
        }
        QPainterPath path;
        path.moveTo(5, 9);
        path.lineTo(14, 9);
        path.cubicTo(QPointF(22, 9), QPointF(22, 19), QPointF(14, 19));
        path.lineTo(9, 19);
        p.drawPath(path);
        arrowHead(p, QPointF(4.5, 9), 180);
        break;
    }
    case Icon::Cut:
        p.drawEllipse(QPointF(7, 18), 3, 3);
        p.drawEllipse(QPointF(17, 18), 3, 3);
        p.drawLine(QPointF(9.2, 15.8), QPointF(17, 3));
        p.drawLine(QPointF(14.8, 15.8), QPointF(7, 3));
        break;
    case Icon::Copy:
        page(p, 3, 3, 12, 15);
        page(p, 9, 7, 12, 15);
        break;
    case Icon::Paste:
        p.drawRoundedRect(QRectF(4, 5, 16, 17), 1.5, 1.5);
        p.setBrush(paper());
        p.drawRoundedRect(QRectF(8.5, 2.5, 7, 4.5), 1, 1);
        p.setBrush(Qt::NoBrush);
        for (const double y : {11.0, 14.5, 18.0}) {
            p.drawLine(QPointF(8, y), QPointF(16, y));
        }
        break;
    case Icon::Delete:
        p.setPen(pen(kStop, 2.4));
        p.drawLine(QPointF(6, 6), QPointF(18, 18));
        p.drawLine(QPointF(18, 6), QPointF(6, 18));
        break;
    case Icon::Rotate:
        turnArrow(p);
        break;
    case Icon::Mirror: {
        QPen axis = pen(ink(), 1.2);
        axis.setDashPattern({2.0, 2.0});
        p.setPen(axis);
        p.drawLine(QPointF(12, 2), QPointF(12, 22));
        p.setPen(pen(ink()));
        p.setBrush(ink());
        p.drawPolygon(QPolygonF() << QPointF(9.5, 6) << QPointF(9.5, 18) << QPointF(3, 18));
        p.setBrush(paper());
        p.drawPolygon(QPolygonF() << QPointF(14.5, 6) << QPointF(14.5, 18) << QPointF(21, 18));
        break;
    }
    case Icon::Properties:
        p.drawRoundedRect(QRectF(3, 4, 18, 16), 1.5, 1.5);
        for (const double y : {9.0, 12.5, 16.0}) {
            p.drawLine(QPointF(6, y), QPointF(9, y));
            p.drawLine(QPointF(11, y), QPointF(18, y));
        }
        break;
    case Icon::ZoomIn:
    case Icon::ZoomOut:
        magnifier(p);
        p.drawLine(QPointF(7, 10), QPointF(13, 10));
        if (which == Icon::ZoomIn) {
            p.drawLine(QPointF(10, 7), QPointF(10, 13));
        }
        break;
    case Icon::ZoomFit:
        for (const QPointF corner : {QPointF(3, 3), QPointF(21, 3), QPointF(21, 21),
                                     QPointF(3, 21)}) {
            const double dx = corner.x() < 12 ? 5 : -5;
            const double dy = corner.y() < 12 ? 5 : -5;
            p.drawPolyline(QPolygonF() << corner + QPointF(dx, 0) << corner
                                       << corner + QPointF(0, dy));
        }
        p.drawRect(QRectF(8, 8, 8, 8));
        break;
    case Icon::Grid:
        p.setPen(pen(ink(), 1.2));
        for (const double v : {4.0, 9.33, 14.67, 20.0}) {
            p.drawLine(QPointF(v, 4), QPointF(v, 20));
            p.drawLine(QPointF(4, v), QPointF(20, v));
        }
        break;
    case Icon::Select: {
        p.setBrush(paper());
        p.drawPolygon(QPolygonF() << QPointF(6, 3) << QPointF(6, 19) << QPointF(10, 15)
                                  << QPointF(13, 21.5) << QPointF(15.5, 20.3)
                                  << QPointF(12.5, 14) << QPointF(18, 14));
        break;
    }
    case Icon::Wire:
        p.setPen(pen(kWire, 2.0));
        p.drawPolyline(QPolygonF() << QPointF(4, 17) << QPointF(12, 17) << QPointF(12, 7)
                                   << QPointF(20, 7));
        p.setBrush(kWire);
        p.drawEllipse(QPointF(4, 17), 1.8, 1.8);
        p.drawEllipse(QPointF(20, 7), 1.8, 1.8);
        break;
    case Icon::Label:
        p.drawPolygon(QPolygonF() << QPointF(3, 8) << QPointF(15, 8) << QPointF(20, 12)
                                  << QPointF(15, 16) << QPointF(3, 16));
        p.setPen(pen(kWire, 2.0));
        p.drawLine(QPointF(20, 12), QPointF(23, 12));
        break;
    case Icon::Junction:
        p.setPen(pen(kWire, 2.0));
        p.drawLine(QPointF(3, 12), QPointF(21, 12));
        p.drawLine(QPointF(12, 12), QPointF(12, 21));
        p.setBrush(kWire);
        p.drawEllipse(QPointF(12, 12), 3.2, 3.2);
        break;
    case Icon::Run:
        p.setPen(pen(kRun.darker(130), 1.2));
        p.setBrush(kRun);
        p.drawPolygon(QPolygonF() << QPointF(6, 4) << QPointF(20, 12) << QPointF(6, 20));
        break;
    case Icon::Pause:
        p.setPen(pen(kPause.darker(130), 1.2));
        p.setBrush(kPause);
        p.drawRect(QRectF(6, 4.5, 4, 15));
        p.drawRect(QRectF(14, 4.5, 4, 15));
        break;
    case Icon::Stop:
        p.setPen(pen(kStop.darker(130), 1.2));
        p.setBrush(kStop);
        p.drawRect(QRectF(5.5, 5.5, 13, 13));
        break;
    }
}

} // namespace

QIcon icon(Icon which)
{
    QIcon result;
    for (const int size : {16, 24, 32}) {
        // Disabled, faded: Qt's own greying hardly shows on a dark theme.
        for (const QIcon::Mode mode : {QIcon::Normal, QIcon::Disabled}) {
            QPixmap pixmap(2 * size, 2 * size);
            pixmap.setDevicePixelRatio(2.0);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setOpacity(mode == QIcon::Disabled ? 0.3 : 1.0);
            painter.scale(size / kBox, size / kBox);
            draw(painter, which);
            painter.end();
            result.addPixmap(pixmap, mode);
        }
    }
    return result;
}

} // namespace proton::app
