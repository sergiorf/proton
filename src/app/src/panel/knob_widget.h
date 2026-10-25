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
// - drag up or down: one click per Knob::kPixelsPerClick;
// - a click without a drag pushes the knob (a continuous knob goes to its reset value);
// - keyboard, when focused: Up / Right and Down / Left one click, Page Up / Page Down five,
//   Space or Enter push.
//
// valueChanged() is emitted only for the student's input, never for setValue().
class KnobWidget : public QWidget {
    Q_OBJECT

public:
    enum class Size {
        Small,
        Large,
    };

    KnobWidget(const QString& label, instruments::Knob knob, Size size, QWidget* parent = nullptr);

    double value() const { return knob_.value(); }
    void setValue(double value);
    void setStep(double step) { knob_.setStep(step); }
    void setReset(double reset) { knob_.setReset(reset); }
    // Whether a push does anything (a frequency knob has nothing to reset to). On by default.
    void setPushable(bool pushable) { pushable_ = pushable; }
    // A second line under the label, for the current setting ("200 µs/div").
    void setCaption(const QString& caption);
    // A coloured ring around the cap, for a knob that belongs to a scope channel.
    void setAccent(const QColor& colour);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

Q_SIGNALS:
    void valueChanged(double value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    int diameter() const;
    void changed(bool changed);

    QString label_;
    QString caption_;
    QColor accent_;
    instruments::Knob knob_;
    Size size_;
    QPoint dragFrom_;
    double dragTravel_ = 0.0; // pixels moved since the press
    bool dragging_ = false;
    bool pushable_ = true;
    QElapsedTimer sinceWheel_;
    int wheelRemainder_ = 0; // eighths of a degree not yet a notch
};

} // namespace proton::app::panel
