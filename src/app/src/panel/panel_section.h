// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QString>
#include <QWidget>

namespace proton::app::panel {

// A labelled section of a front panel (Vertical, Horizontal, Trigger): an outline printed on the
// panel with its title set into the top edge. Lay out its controls with a layout on it as usual;
// its contents margins leave room for the outline and the title.
class PanelSection : public QWidget {
    Q_OBJECT

public:
    explicit PanelSection(const QString& title, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString title_;
};

// The face of an instrument's front panel, the parent of its sections and screen: dark grey with a
// bevelled edge.
class PanelFace : public QWidget {
    Q_OBJECT

public:
    explicit PanelFace(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace proton::app::panel
