// SPDX-License-Identifier: AGPL-3.0-only
#include "panel_section.h"

#include "panel_style.h"

#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>

namespace proton::app::panel {

namespace {

QFont titleFont(QFont font)
{
    font.setBold(true);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::PercentageSpacing, 110);
    return font;
}

} // namespace

PanelSection::PanelSection(const QString& title, QWidget* parent) : QWidget(parent), title_(title)
{
    const QFontMetrics metrics(titleFont(font()));
    setContentsMargins(8, metrics.height() + 4, 8, 8);
}

void PanelSection::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QFont font = titleFont(this->font());
    const QFontMetrics metrics(font);
    const double top = metrics.height() / 2.0;
    const QRectF outline = QRectF(rect()).adjusted(1, top, -1, -1);
    painter.setPen(QPen(kSectionLine, 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(outline, 6, 6);

    // The title, on a patch of panel that breaks the outline.
    const int width = metrics.horizontalAdvance(title_) + 12;
    const QRectF label(12, 0, width, metrics.height());
    painter.fillRect(label, kPanel);
    painter.setFont(font);
    painter.setPen(kLettering);
    painter.drawText(label, Qt::AlignCenter, title_);
}

PanelFace::PanelFace(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
}

void PanelFace::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), kPanelEdge);
    QLinearGradient face(0, 0, 0, height());
    face.setColorAt(0.0, kPanel.lighter(112));
    face.setColorAt(1.0, kPanel.darker(108));
    painter.setPen(QPen(kPanel.lighter(135), 1.0));
    painter.setBrush(face);
    painter.drawRoundedRect(QRectF(rect()).adjusted(2.5, 2.5, -2.5, -2.5), 10, 10);
}

} // namespace proton::app::panel
