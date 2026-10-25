// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/instruments/selector.h"

#include <QStringList>
#include <QWidget>

namespace proton::app::panel {

// A rotary selector switch on a front panel, such as a multimeter's mode dial (PLAN.md M2.5): a
// pointer knob with its positions printed around it. The logic is instruments::Selector; this
// widget draws it and turns input into clicks: mouse wheel, vertical drag, arrow keys, or a click
// on a position's label to go straight there.
//
// positionChanged() is emitted only for the student's input, never for setPosition().
class SelectorWidget : public QWidget {
    Q_OBJECT

public:
    SelectorWidget(const QString& label, const QStringList& positions, double span,
                   QWidget* parent = nullptr);

    std::size_t position() const { return selector_.position(); }
    void setPosition(std::size_t position);

    QSize sizeHint() const override { return {200, 170}; }
    QSize minimumSizeHint() const override { return {170, 150}; }

Q_SIGNALS:
    void positionChanged(std::size_t position);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QPointF centre() const;
    double radius() const;
    QPointF labelAt(std::size_t position) const;
    void changed(bool moved);

    QString label_;
    QStringList positions_;
    instruments::Selector selector_;
    int dragFrom_ = 0;
    bool dragging_ = false;
    int wheelRemainder_ = 0;
};

} // namespace proton::app::panel
