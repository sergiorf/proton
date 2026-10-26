// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/instruments/knob.h"

#include <QElapsedTimer>
#include <QPoint>
#include <QString>
#include <QWidget>

namespace proton::app::panel {

// A rotary knob on an instrument's front panel: a knurled cap with a pointer and its label printed
// below. The logic is instruments::Knob; this widget draws it and turns input into clicks:
//
// - mouse wheel: one click per notch, accelerated for a continuous knob turned fast;
// - drag: round the knob's centre, as a hand turns a knob, one click per Knob::kDegreesPerClick
//   (clockwise across the top and down the right side); near the centre, or with the straight
//   drag mode, up or right clockwise and down or left anticlockwise, one click per
//   Knob::kPixelsPerClick of the two together;
// - a click without a drag pushes the knob (a continuous knob goes to its reset value, a knob with
//   a variable mode goes into or out of it);
// - keyboard, when focused: Up / Right and Down / Left one click, Page Up / Page Down five,
//   Space or Enter push.
//
// Shift with any of them turns fine, a tenth of a click; Ctrl coarse, ten (Knob::Speed). A stepped
// knob has its scale printed round it: a tick per step, longer at each decade, with the decade's
// value. Hovered, the knob shows a light ring and an open hand, closed while it is dragged.
//
// valueChanged() is emitted only for the student's input, never for setValue().
class KnobWidget : public QWidget {
    Q_OBJECT

public:
    enum class Size {
        Small,
        Large,
    };
    // How a drag turns every knob (instruments::Knob::dragBetween): an app-wide preference,
    // round by default.
    using DragMode = instruments::Knob::DragMode;

    KnobWidget(const QString& label, instruments::Knob knob, Size size, QWidget* parent = nullptr);

    static void setDragMode(DragMode mode) { dragMode_ = mode; }
    static DragMode dragMode() { return dragMode_; }

    double value() const { return knob_.value(); }
    void setValue(double value);
    void setStep(double step) { knob_.setStep(step); }
    void setReset(double reset) { knob_.setReset(reset); }
    // Whether a push does anything (a frequency knob has nothing to reset to). On by default.
    void setPushable(bool pushable) { pushable_ = pushable; }
    // A stepped knob's variable mode (Knob::setVariableAllowed), and whether it is on.
    void setVariableAllowed(bool allowed) { knob_.setVariableAllowed(allowed); }
    bool variable() const { return knob_.variable(); }
    // A second line under the label, for the current setting ("200 µs/div").
    void setCaption(const QString& caption);
    // A coloured ring around the cap, for a knob that belongs to a scope channel.
    void setAccent(const QColor& colour);
    bool hovered() const { return hovered_; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

Q_SIGNALS:
    void valueChanged(double value);
    // A push switched the variable mode on or off.
    void variableChanged(bool variable);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    int diameter() const;
    // Room round the cap for the printed scale; none for a continuous knob.
    int scaleRoom() const;
    QPointF centre() const;
    void changed(bool changed);
    void pushKnob();
    void drawScale(QPainter& painter, QPointF centre, double radius) const;

    static inline DragMode dragMode_ = DragMode::Circular;

    QString label_;
    QString caption_;
    QColor accent_;
    instruments::Knob knob_;
    Size size_;
    QPoint dragFrom_;
    double dragTravel_ = 0.0; // pixels moved since the press
    bool dragging_ = false;
    bool hovered_ = false;
    bool pushable_ = true;
    QElapsedTimer sinceWheel_;
    int wheelRemainder_ = 0; // eighths of a degree not yet a notch
};

} // namespace proton::app::panel
