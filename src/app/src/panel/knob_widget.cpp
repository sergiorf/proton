// SPDX-License-Identifier: AGPL-3.0-only
#include "knob_widget.h"

#include "panel_style.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <utility>

namespace proton::app::panel {

namespace {

constexpr int kNotch = 120;         // QWheelEvent units per wheel notch
constexpr int kKnurls = 24;         // ridges around the cap
constexpr double kPushTravel = 3.0; // pixels a click may move and still be a push
constexpr int kPageClicks = 5;

} // namespace

KnobWidget::KnobWidget(const QString& label, instruments::Knob knob, Size size, QWidget* parent)
    : QWidget(parent), label_(label), knob_(std::move(knob)), size_(size)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::SizeVerCursor);
    setAccessibleName(label);
    setToolTip(tr("%1: drag, scroll or use the arrow keys; click to push").arg(label));
}

void KnobWidget::setValue(double value)
{
    knob_.setValue(value);
    update();
}

void KnobWidget::setCaption(const QString& caption)
{
    if (caption != caption_) {
        caption_ = caption;
        update();
    }
}

void KnobWidget::setAccent(const QColor& colour)
{
    accent_ = colour;
    update();
}

int KnobWidget::diameter() const
{
    return size_ == Size::Large ? 54 : 38;
}

QSize KnobWidget::sizeHint() const
{
    const QFontMetrics metrics(font());
    const int text = metrics.height() * 2 + 4;
    const int width = std::max({diameter() + 12, metrics.horizontalAdvance(label_) + 8, 84});
    return {width, diameter() + 12 + text};
}

void KnobWidget::changed(bool changed)
{
    if (changed) {
        update();
        Q_EMIT valueChanged(knob_.value());
    }
}

void KnobWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const double d = diameter();
    const QPointF centre(width() / 2.0, 6.0 + d / 2.0);
    const double r = d / 2.0;

    // Shadow, rim, then the cap with a light from the top left.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 90));
    painter.drawEllipse(centre + QPointF(1.5, 2.5), r + 1, r + 1);
    painter.setBrush(accent_.isValid() ? accent_ : kKnobRim);
    painter.drawEllipse(centre, r, r);
    QRadialGradient cap(centre - QPointF(r * 0.35, r * 0.35), r * 1.4);
    cap.setColorAt(0.0, kKnobBody.lighter(170));
    cap.setColorAt(1.0, kKnobBody);
    painter.setBrush(cap);
    painter.drawEllipse(centre, r - 3, r - 3);

    // Knurls turn with the cap, so the knob visibly rotates.
    const double turn = knob_.angle() * std::numbers::pi / 180.0;
    painter.setPen(QPen(kKnobBody.darker(160), 1.2));
    for (int i = 0; i < kKnurls; ++i) {
        const double a = turn + 2.0 * std::numbers::pi * i / kKnurls;
        const QPointF dir(std::sin(a), -std::cos(a));
        painter.drawLine(centre + dir * (r - 3), centre + dir * (r - 7));
    }
    // The pointer: a line from near the centre to near the edge.
    const QPointF dir(std::sin(turn), -std::cos(turn));
    painter.setPen(QPen(kPointer, size_ == Size::Large ? 3.0 : 2.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(centre + dir * (r * 0.25), centre + dir * (r - 8));

    if (hasFocus()) {
        painter.setPen(QPen(kFocus, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(centre, r + 3, r + 3);
    }

    // The label printed on the panel, and the setting under it.
    const QFontMetrics metrics(font());
    QRectF text(0, centre.y() + r + 6, width(), metrics.height());
    painter.setPen(kLettering);
    painter.drawText(text, Qt::AlignHCenter | Qt::AlignTop, label_);
    if (!caption_.isEmpty()) {
        text.translate(0, metrics.height());
        painter.setPen(kDimLettering);
        painter.drawText(text, Qt::AlignHCenter | Qt::AlignTop, caption_);
    }
}

void KnobWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    dragging_ = true;
    dragFrom_ = event->position().toPoint();
    dragTravel_ = 0.0;
    setFocus(Qt::MouseFocusReason);
}

void KnobWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_) {
        return;
    }
    const QPoint at = event->position().toPoint();
    const int dy = dragFrom_.y() - at.y(); // upward turns up
    dragTravel_ += std::abs(dy) + std::abs(at.x() - dragFrom_.x());
    dragFrom_ = at;
    changed(knob_.drag(dy));
}

void KnobWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !dragging_) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    dragging_ = false;
    knob_.endDrag();
    if (dragTravel_ <= kPushTravel && pushable_) {
        changed(knob_.push());
    }
}

void KnobWidget::wheelEvent(QWheelEvent* event)
{
    wheelRemainder_ += event->angleDelta().y();
    const int clicks = wheelRemainder_ / kNotch;
    wheelRemainder_ -= clicks * kNotch;
    if (clicks != 0) {
        const double since =
            sinceWheel_.isValid() ? static_cast<double>(sinceWheel_.elapsed()) / 1000.0 : -1.0;
        sinceWheel_.start();
        changed(knob_.turnAccelerated(clicks, since));
    }
    event->accept();
}

void KnobWidget::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Right:
        changed(knob_.turn(1));
        break;
    case Qt::Key_Down:
    case Qt::Key_Left:
        changed(knob_.turn(-1));
        break;
    case Qt::Key_PageUp:
        changed(knob_.turn(kPageClicks));
        break;
    case Qt::Key_PageDown:
        changed(knob_.turn(-kPageClicks));
        break;
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        changed(pushable_ && knob_.push());
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

} // namespace proton::app::panel
