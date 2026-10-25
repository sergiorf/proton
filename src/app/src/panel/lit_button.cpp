// SPDX-License-Identifier: AGPL-3.0-only
#include "lit_button.h"

#include "panel_style.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>

#include <algorithm>

namespace proton::app::panel {

LitButton::LitButton(const QString& text, const QColor& lamp, QWidget* parent)
    : QAbstractButton(parent), lamp_(lamp)
{
    setText(text);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName(text);
}

void LitButton::setLit(bool lit)
{
    if (lit != lit_) {
        lit_ = lit;
        update();
    }
}

void LitButton::setLamp(const QColor& lamp)
{
    lamp_ = lamp;
    update();
}

QSize LitButton::sizeHint() const
{
    const QFontMetrics metrics(font());
    return {std::max(56, metrics.horizontalAdvance(text()) + 20), metrics.height() + 14};
}

void LitButton::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF body = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
    const double radius = 5.0;

    // A pressed button sits a little lower.
    const QRectF face = isDown() ? body.translated(0, 1) : body;
    painter.setPen(QPen(kPanelEdge, 1.2));
    QLinearGradient fill(face.topLeft(), face.bottomLeft());
    if (lit_) {
        fill.setColorAt(0.0, lamp_.lighter(125));
        fill.setColorAt(1.0, lamp_.darker(115));
    } else {
        fill.setColorAt(0.0, kButtonOff.lighter(120));
        fill.setColorAt(1.0, kButtonOff.darker(110));
    }
    painter.setBrush(fill);
    painter.drawRoundedRect(face, radius, radius);

    if (hasFocus()) {
        painter.setPen(QPen(kFocus, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(body.adjusted(-0.5, -0.5, 0.5, 0.5), radius + 1, radius + 1);
    }

    // Dark lettering on a lit lamp, light on a dark one.
    painter.setPen(lit_ ? QColor(25, 25, 25) : kLettering);
    painter.drawText(face, Qt::AlignCenter, text());
}

} // namespace proton::app::panel
