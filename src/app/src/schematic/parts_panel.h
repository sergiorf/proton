// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "schematic_view.h"

#include <QIcon>
#include <QPoint>
#include <QString>
#include <QTabWidget>

#include <utility>
#include <vector>

class QListWidget;
class QListWidgetItem;

namespace proton::app::sheet {

// The parts panel beside the sheet (PLAN.md M4.4, ADR 0009): every component and instrument the
// student can place, a tab per group (passives, sources, instruments) down its left edge, each a
// column of icons drawn with the sheet's own symbols and named underneath. Clicking one chooses
// it, and the sheet's Place tool then places it on every click; the choice clears when the sheet
// goes back to another tool.
class PartsPanel : public QTabWidget {
    Q_OBJECT

public:
    explicit PartsPanel(QWidget* parent = nullptr);

    // Every entry, in panel order, with its name and icon: for the Place menu too.
    std::vector<SchematicView::Placing> entries() const;
    QString name(const SchematicView::Placing& what) const;
    QIcon icon(const SchematicView::Placing& what) const;
    // Brings `what`'s tab to the front and returns the widget and point to click to choose it, for
    // the smoke check that places every entry by hand; no widget when there is no such entry.
    std::pair<QWidget*, QPoint> locate(const SchematicView::Placing& what);

    // Shows the sheet's tool: the entry being placed, or none.
    void showTool(SchematicView::Tool tool, const SchematicView::Placing& placing);

Q_SIGNALS:
    void chosen(const SchematicView::Placing& what);

private:
    struct Entry
    {
        QListWidget* list = nullptr;
        QListWidgetItem* item = nullptr;
        SchematicView::Placing what;
    };

    QListWidget* addGroup(const QString& name);
    void addEntry(QListWidget* list, const QString& name, SchematicView::Placing what);
    const Entry* find(const SchematicView::Placing& what) const;

    std::vector<Entry> entries_;
};

} // namespace proton::app::sheet
