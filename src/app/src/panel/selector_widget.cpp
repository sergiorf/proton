// SPDX-License-Identifier: AGPL-3.0-only
#include "selector_widget.h"

#include "panel_style.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace proton::app::panel {

namespace {

constexpr int kNotch = 120; // QWheelEvent units per wheel notch

QPointF direction(double degrees)
{
    const double a = degrees * std::numbers::pi / 180.0;
    return {std::sin(a), -std::cos(a)};
}

} // namespace

SelectorWidget::SelectorWidget(const QString& label, const QStringList& positions, double span,
                               QWidget* parent)
    : QWidget(parent), label_(label), positions_(positions),
      selector_(static_cast<std::size_t>(std::max<qsizetype>(positions.size(), 1)), span)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::SizeVerCursor);
    setAccessibleName(label);
    setToolTip(tr("%1: drag, scroll, use the arrow keys or click a position").arg(label));
}

void SelectorWidget::setPosition(std::size_t position)
{
    if (selector_.setPosition(position)) {
        update();
    }
}

QPointF SelectorWidget::centre() const
{
    return {width() / 2.0, height() * 0.58};
}

double SelectorWidget::radius() const
{
    return std::min(width(), height()) * 0.22;
}

QPointF SelectorWidget::labelAt(std::size_t position) const
{
    return centre() + direction(selector_.angleOf(position)) * (radius() + 26);
}

void SelectorWidget::changed(bool moved)
{
    if (moved) {
        update();
        Q_EMIT positionChanged(selector_.position());
    }
}

void SelectorWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF c = centre();
    const double r = radius();

    // Position marks and labels, the selected one bright.
    const QFontMetrics metrics(font());
    for (std::size_t i = 0; i < selector_.positions(); ++i) {
        const QPointF dir = direction(selector_.angleOf(i));
        const bool selected = i == selector_.position();
        painter.setPen(QPen(selected ? kLettering : kDimLettering, 2.0));
        painter.drawLine(c + dir * (r + 4), c + dir * (r + 10));
        const QPointF at = labelAt(i);
        const QString text = static_cast<qsizetype>(i) < positions_.size()
                                 ? positions_[static_cast<qsizetype>(i)]
                                 : QString();
        const QRectF box(at.x() - 40, at.y() - metrics.height() / 2.0, 80, metrics.height());
        painter.setPen(selected ? kLettering : kDimLettering);
        painter.drawText(box, Qt::AlignCenter, text);
    }

    // The knob: a cap with a raised bar that points at the position.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 90));
    painter.drawEllipse(c + QPointF(1.5, 2.5), r + 1, r + 1);
    painter.setBrush(kKnobRim);
    painter.drawEllipse(c, r, r);
    QRadialGradient cap(c - QPointF(r * 0.35, r * 0.35), r * 1.4);
    cap.setColorAt(0.0, kKnobBody.lighter(170));
    cap.setColorAt(1.0, kKnobBody);
    painter.setBrush(cap);
    painter.drawEllipse(c, r - 3, r - 3);
    const QPointF dir = direction(selector_.angle());
    const QPointF normal(-dir.y(), dir.x());
    const QPolygonF bar = {c + dir * (r - 4) + normal * 4, c - dir * (r - 4) + normal * 4,
                           c - dir * (r - 4) - normal * 4, c + dir * (r - 4) - normal * 4};
    painter.setBrush(kKnobBody.lighter(140));
    painter.drawPolygon(bar);
    painter.setPen(QPen(kPointer, 3.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(c + dir * (r * 0.45), c + dir * (r - 6));

    if (hasFocus()) {
        painter.setPen(QPen(kFocus, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(c, r + 3, r + 3);
    }

    painter.setPen(kLettering);
    painter.drawText(QRectF(0, height() - metrics.height() - 2, width(), metrics.height()),
                     Qt::AlignCenter, label_);
}

void SelectorWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    const QPointF at = event->position();
    // A click on a label goes straight to its position.
    for (std::size_t i = 0; i < selector_.positions(); ++i) {
        const QPointF d = at - labelAt(i);
        if (std::hypot(d.x(), d.y()) < 18.0) {
            changed(selector_.setPosition(i));
            return;
        }
    }
    dragging_ = true;
    dragFrom_ = static_cast<int>(at.y());
}

void SelectorWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_) {
        return;
    }
    const int y = static_cast<int>(event->position().y());
    const int dy = dragFrom_ - y;
    dragFrom_ = y;
    changed(selector_.drag(dy));
}

void SelectorWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        selector_.endDrag();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void SelectorWidget::wheelEvent(QWheelEvent* event)
{
    wheelRemainder_ += event->angleDelta().y();
    const int clicks = wheelRemainder_ / kNotch;
    wheelRemainder_ -= clicks * kNotch;
    changed(clicks != 0 && selector_.turn(clicks));
    event->accept();
}

void SelectorWidget::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Right:
        changed(selector_.turn(1));
        break;
    case Qt::Key_Down:
    case Qt::Key_Left:
        changed(selector_.turn(-1));
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

} // namespace proton::app::panel
