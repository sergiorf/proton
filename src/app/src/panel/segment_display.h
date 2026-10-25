// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

namespace proton::app::panel {

// A bench multimeter's LCD (PLAN.md M2.5): 7-segment digits with decimal points, as
// instruments::layoutDisplay() lays the text out, and annunciators, the small fixed labels an LCD
// lights ("AUTO", "DC", "AC") on the left and the unit on the right. Unlit segments show faintly,
// as on a real LCD.
class SegmentDisplay : public QWidget {
    Q_OBJECT

public:
    explicit SegmentDisplay(int cells, QWidget* parent = nullptr);

    // The reading as the meter writes it ("0.5902", "-123.40", "OL") and its unit ("V", "mV").
    void setReading(const QString& text, const QString& unit);
    const QString& text() const { return text_; }
    const QString& unit() const { return unit_; }
    // The annunciators to light, left to right.
    void setAnnunciators(const QStringList& annunciators);

    QSize sizeHint() const override { return {360, 120}; }
    QSize minimumSizeHint() const override { return {240, 84}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int cells_;
    QString text_;
    QString unit_;
    QStringList annunciators_;
};

} // namespace proton::app::panel
