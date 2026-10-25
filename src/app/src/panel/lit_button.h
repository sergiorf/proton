// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QAbstractButton>
#include <QColor>

namespace proton::app::panel {

// A push button on an instrument's front panel with a lamp behind its label: lit in its lamp colour
// when on, dark when off. Clicked, toggled and keyboard-operated like any QAbstractButton; whether
// it is lit is set by its owner (setLit), since on a real instrument the lamp shows the
// instrument's state, not the button's.
class LitButton : public QAbstractButton {
    Q_OBJECT

public:
    LitButton(const QString& text, const QColor& lamp, QWidget* parent = nullptr);

    void setLit(bool lit);
    bool lit() const { return lit_; }
    void setLamp(const QColor& lamp);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor lamp_;
    bool lit_ = false;
};

} // namespace proton::app::panel
