// SPDX-License-Identifier: AGPL-3.0-only
#include "knob_widget.h"

#include "panel_style.h"

#include "proton/core/format.h"

#include <QEnterEvent>
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

using Speed = instruments::Knob::Speed;

constexpr int kNotch = 120;         // QWheelEvent units per wheel notch
constexpr int kKnurls = 24;         // ridges around the cap
constexpr double kPushTravel = 3.0; // pixels a click may move and still be a push
constexpr int kPageClicks = 5;
constexpr int kScaleRoom = 22;      // pixels round a stepped knob for its ticks and values
constexpr int kLargeScaleRoom = 27; // a large one has more values round it

// Shift turns fine, Ctrl coarse.
Speed speedOf(Qt::KeyboardModifiers modifiers)
{
    if ((modifiers & Qt::ShiftModifier) != 0) {
        return Speed::Fine;
    }
    if ((modifiers & Qt::ControlModifier) != 0) {
        return Speed::Coarse;
    }
    return Speed::Normal;
}

QPointF direction(double degrees)
{
    const double a = degrees * std::numbers::pi / 180.0;
    return {std::sin(a), -std::cos(a)};
}

// A step of a 1-2-5 scale that starts a decade (1, 10, 100 m, ...).
bool startsDecade(double value)
{
    if (!(value > 0.0)) {
        return false;
    }
    const double mantissa = value / std::pow(10.0, std::floor(std::log10(value) + 1e-9));
    return std::fabs(mantissa - 1.0) < 1e-6;
}

// A decade's value as printed on a scale: "10m", "1µ", "1".
QString scaleText(double value)
{
    return QString::fromStdString(core::formatEngineering(value, 1)).remove(QLatin1Char(' '));
}

} // namespace

KnobWidget::KnobWidget(const QString& label, instruments::Knob knob, Size size, QWidget* parent)
    : QWidget(parent), label_(label), knob_(std::move(knob)), size_(size)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::OpenHandCursor);
    setAccessibleName(label);
    setToolTip(tr("%1: drag, scroll or use the arrow keys (Shift fine, Ctrl coarse); click to "
                  "push")
                   .arg(label));
}

void KnobWidget::setValue(double value)
{
    knob_.setValue(value);
    update();
}

void KnobWidget::setEncoder()
{
    encoder_ = true;
    pushable_ = false;
    // A value of its own only to count clicks by: far more than a student turns it.
    knob_ = instruments::Knob::continuous(-1e9, 1e9, 1.0, 0.0, 0.0);
    encoderLast_ = 0.0;
    encoderRemainder_ = 0.0;
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

int KnobWidget::scaleRoom() const
{
    if (!knob_.isStepped() || knob_.stepCount() < 2) {
        return 0;
    }
    return size_ == Size::Large ? kLargeScaleRoom : kScaleRoom;
}

QPointF KnobWidget::centre() const
{
    return {width() / 2.0, 6.0 + scaleRoom() + diameter() / 2.0};
}

QSize KnobWidget::sizeHint() const
{
    const QFontMetrics metrics(font());
    const int text = metrics.height() * 2 + 4;
    const int room = scaleRoom();
    const int width =
        std::max({diameter() + 12 + 2 * room, metrics.horizontalAdvance(label_) + 8, 84});
    // The scale's lowest marks are a little below the cap's centre line; the label goes under.
    return {width, 6 + room + diameter() + room / 2 + 6 + text};
}

void KnobWidget::changed(bool changed)
{
    if (!changed) {
        return;
    }
    update();
    Q_EMIT valueChanged(knob_.value());
    if (encoder_) {
        // Whole clicks; a fine turn's tenths add up to one.
        encoderRemainder_ += knob_.value() - encoderLast_;
        encoderLast_ = knob_.value();
        const double whole = std::trunc(encoderRemainder_ + std::copysign(1e-9, encoderRemainder_));
        encoderRemainder_ -= whole;
        if (whole != 0.0) {
            Q_EMIT turned(static_cast<int>(whole));
        }
    }
}

void KnobWidget::pushKnob()
{
    if (!pushable_) {
        return;
    }
    const bool wasVariable = knob_.variable();
    changed(knob_.push());
    if (knob_.variable() != wasVariable) {
        update();
        Q_EMIT variableChanged(knob_.variable());
    }
}

void KnobWidget::drawScale(QPainter& painter, QPointF centre, double radius) const
{
    QFont font = painter.font();
    font.setPixelSize(8);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    for (std::size_t i = 0; i < knob_.stepCount(); ++i) {
        const double value = knob_.stepValue(i);
        const bool decade = startsDecade(value);
        const QPointF dir = direction(knob_.markAngle(i));
        painter.setPen(QPen(decade ? kLettering : kDimLettering, decade ? 1.4 : 1.0));
        painter.drawLine(centre + dir * (radius + 3), centre + dir * (radius + (decade ? 9 : 6)));
        if (decade) {
            const QString text = scaleText(value);
            const QPointF at = centre + dir * (radius + scaleRoom() * 0.68);
            const double w = metrics.horizontalAdvance(text);
            painter.setPen(kDimLettering);
            painter.drawText(QRectF(at.x() - w / 2 - 1, at.y() - metrics.height() / 2.0, w + 2,
                                    metrics.height()),
                             Qt::AlignCenter, text);
        }
    }
}

void KnobWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const double d = diameter();
    const QPointF centre = this->centre();
    const double r = d / 2.0;

    if (scaleRoom() > 0) {
        painter.save();
        drawScale(painter, centre, r);
        painter.restore();
    }

    // Shadow, rim, then the cap with a light from the top left.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 90));
    painter.drawEllipse(centre + QPointF(1.5, 2.5), r + 1, r + 1);
    painter.setBrush(accent_.isValid() ? accent_ : kKnobRim);
    painter.drawEllipse(centre, r, r);
    QRadialGradient cap(centre - QPointF(r * 0.35, r * 0.35), r * 1.4);
    cap.setColorAt(0.0, kKnobBody.lighter(hovered_ ? 210 : 170));
    cap.setColorAt(1.0, kKnobBody);
    painter.setBrush(cap);
    painter.drawEllipse(centre, r - 3, r - 3);

    // Knurls turn with the cap, so the knob visibly rotates.
    const double turn = knob_.angle();
    painter.setPen(QPen(kKnobBody.darker(160), 1.2));
    for (int i = 0; i < kKnurls; ++i) {
        const QPointF dir = direction(turn + 360.0 * i / kKnurls);
        painter.drawLine(centre + dir * (r - 3), centre + dir * (r - 7));
    }
    // The pointer: a line from near the centre to near the edge.
    const QPointF dir = direction(turn);
    painter.setPen(QPen(kPointer, size_ == Size::Large ? 3.0 : 2.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(centre + dir * (r * 0.25), centre + dir * (r - 8));

    // A light ring when the mouse is over it: this can be turned. The focus ring over it.
    painter.setBrush(Qt::NoBrush);
    if (hovered_ || dragging_) {
        painter.setPen(QPen(QColor(255, 255, 255, 70), 2.0));
        painter.drawEllipse(centre, r + 1.5, r + 1.5);
    }
    if (hasFocus()) {
        painter.setPen(QPen(kFocus, 1.5));
        painter.drawEllipse(centre, r + 3, r + 3);
    }
    if (knob_.variable()) {
        QFont font = painter.font();
        font.setPixelSize(8);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(kLampSingle);
        painter.drawText(QRectF(centre.x() - 20, centre.y() + r * 0.3, 40, 10), Qt::AlignCenter,
                         tr("VAR"));
    }

    // The label printed on the panel, and the setting under it.
    painter.setFont(font());
    const QFontMetrics metrics(font());
    QRectF text(0, centre.y() + r + scaleRoom() / 2.0 + 6, width(), metrics.height());
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
    setCursor(Qt::ClosedHandCursor);
    setFocus(Qt::MouseFocusReason);
}

void KnobWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_) {
        return;
    }
    const QPoint at = event->position().toPoint();
    dragTravel_ += std::abs(at.y() - dragFrom_.y()) + std::abs(at.x() - dragFrom_.x());
    // How the move turns the knob is instruments::Knob's, tested there.
    const QPointF c = centre();
    const instruments::Knob::Point from{static_cast<double>(dragFrom_.x()),
                                        static_cast<double>(dragFrom_.y())};
    const instruments::Knob::Point to{static_cast<double>(at.x()), static_cast<double>(at.y())};
    const bool turned =
        knob_.dragBetween(from, to, {c.x(), c.y()}, dragMode_, speedOf(event->modifiers()));
    dragFrom_ = at;
    changed(turned);
}

void KnobWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !dragging_) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    dragging_ = false;
    setCursor(Qt::OpenHandCursor);
    knob_.endDrag();
    if (dragTravel_ <= kPushTravel) {
        pushKnob();
    }
    update();
}

void KnobWidget::wheelEvent(QWheelEvent* event)
{
    // With Shift held, some platforms turn the wheel into a horizontal one.
    const QPoint delta = event->angleDelta();
    wheelRemainder_ += delta.y() != 0 ? delta.y() : delta.x();
    const int clicks = wheelRemainder_ / kNotch;
    wheelRemainder_ -= clicks * kNotch;
    if (clicks != 0) {
        // An encoder moves a digit: a fast turn should not skip it ahead.
        const double since = sinceWheel_.isValid() && !encoder_
                                 ? static_cast<double>(sinceWheel_.elapsed()) / 1000.0
                                 : -1.0;
        sinceWheel_.start();
        changed(knob_.turnAccelerated(clicks, since, speedOf(event->modifiers())));
    }
    event->accept();
}

void KnobWidget::keyPressEvent(QKeyEvent* event)
{
    const Speed speed = speedOf(event->modifiers());
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
        if (encoder_) {
            Q_EMIT cursorMoved(event->key() == Qt::Key_Left ? 1 : -1);
        } else {
            changed(knob_.turn(event->key() == Qt::Key_Right ? 1 : -1, speed));
        }
        break;
    case Qt::Key_Up:
        changed(knob_.turn(1, speed));
        break;
    case Qt::Key_Down:
        changed(knob_.turn(-1, speed));
        break;
    case Qt::Key_PageUp:
        changed(knob_.turn(kPageClicks, speed));
        break;
    case Qt::Key_PageDown:
        changed(knob_.turn(-kPageClicks, speed));
        break;
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        pushKnob();
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void KnobWidget::enterEvent(QEnterEvent* event)
{
    hovered_ = true;
    update();
    QWidget::enterEvent(event);
}

void KnobWidget::leaveEvent(QEvent* event)
{
    hovered_ = false;
    update();
    QWidget::leaveEvent(event);
}

} // namespace proton::app::panel
