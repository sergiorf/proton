// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/schematic/scene.h"

#include <QGraphicsView>
#include <QPoint>
#include <QString>

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

class QGraphicsScene;
class QUndoStack;

namespace proton::app::sheet {

class SheetItem;

// The schematic editor's sheet (PLAN.md M4.2, M4.4): a QGraphicsView showing a schematic::Scene on
// a grid, with tools to edit it. The scene is the model; the view paints it (SheetItem) and turns
// mouse and keys into edits through schematic/scene_edit.h, so what an edit does is tested without
// Qt. Instruments are drawn and edited like parts (ADR 0009).
//
// Tools:
// - Select: click a part, instrument, wire or label to select it (Shift adds); drag selected parts
//   and instruments to move them, their wire ends following; double-click a part to edit its
//   designator and value, a label to rename it, or an instrument to open its front panel
//   (instrumentOpened()). R turns the selected parts and instruments, Delete removes the
//   selection.
// - Wire: click where a wire starts, then where it bends or ends; each click adds the route there
//   (horizontal, then vertical) and goes on from it. Double-click, right-click or Escape ends it.
// - Label: click on a net to name it.
// - Place a part or an instrument: it follows the mouse; R turns it; each click places one, a part
//   with the next designator and a default value, an instrument with the next name of its kind.
// Escape goes back to Select. Ctrl and the wheel zoom; the wheel scrolls. toolChanged() tells the
// toolbar and the parts panel which tool is on.
//
// Every edit emits edited(), after which scene() is the new drawing, and is one step on
// undoStack() (PLAN.md M4.3): a drag, a turn of the selection or one wire click is one step. A step
// holds the scene before and after it, so undo restores the earlier drawing exactly, whatever the
// edit was; a scene is small, so a copy per step costs little. An edit that changes nothing is no
// step. Undo and redo emit edited() too, and clear the selection, whose indices may no longer fit.
class SchematicView : public QGraphicsView {
    Q_OBJECT

public:
    enum class Tool {
        Select,
        Wire,
        Label,
        Place,
    };

    // What the Place tool places.
    using Placing = std::variant<schematic::ComponentKind, schematic::InstrumentKind>;

    explicit SchematicView(QWidget* parent = nullptr);

    // Shows `scene`, replacing the drawing and starting a new undo history; no edited().
    void setScene(const schematic::Scene& scene);
    const schematic::Scene& scene() const { return scene_; }

    void setTool(Tool tool);
    // The Place tool with `what`.
    void placeKind(Placing what);
    Tool tool() const { return tool_; }
    const Placing& placing() const { return placing_; }

    // Where grid point `p` is in the viewport, and back, for the smoke checks and for placing a
    // front panel beside its instrument.
    QPoint viewPoint(schematic::Point p) const;
    schematic::Point gridPoint(const QPoint& viewPosition) const;

    QUndoStack& undoStack() { return *undo_; }

    // Edits as the tools make them, for the smoke checks and for the menus; each is one undo step.
    void placePart(schematic::ComponentKind kind, schematic::Point at,
                   schematic::Rotation rotation = schematic::Rotation::R0,
                   const std::string& value = {});
    void placeInstrument(schematic::InstrumentKind kind, schematic::Point at,
                         schematic::Rotation rotation = schematic::Rotation::R0);
    void drawWire(schematic::Point from, schematic::Point to);
    void addLabel(const std::string& name, schematic::Point at);
    void moveItems(const std::vector<std::size_t>& parts,
                   const std::vector<std::size_t>& instruments, schematic::Point offset);
    void rotateItems(const std::vector<std::size_t>& parts,
                     const std::vector<std::size_t>& instruments);
    void removeItems(const std::vector<std::size_t>& parts, const std::vector<std::size_t>& wires,
                     const std::vector<std::size_t>& labels,
                     const std::vector<std::size_t>& instruments = {});
    void setPartText(std::size_t index, const std::string& designator, const std::string& value);
    void renameLabel(std::size_t index, const std::string& name);
    // Empties the sheet, as an edit that can be undone: there are no project files to reopen yet.
    void clearSheet();

Q_SIGNALS:
    void edited();
    // The student double-clicked instrument `name`.
    void instrumentOpened(const QString& name);
    void toolChanged(Tool tool);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private:
    friend class SceneStep;

    // Records the edit made to scene_ since the last step as a new step called `text`.
    void commit(const QString& text);
    // Shows `scene` as undo or redo leave it.
    void restore(const schematic::Scene& scene);
    void clearSelection();
    void refresh();
    void finishWire();
    void selectAt(schematic::Point p, bool add);
    void editPart(std::size_t index);
    void editLabel(std::size_t index);

    schematic::Scene scene_;
    schematic::Scene committed_; // scene_ as the last undo step left it
    QUndoStack* undo_ = nullptr;
    QGraphicsScene* canvas_ = nullptr;
    SheetItem* sheet_ = nullptr;

    Tool tool_ = Tool::Select;
    Placing placing_ = schematic::ComponentKind::Resistor;
    schematic::Rotation placingRotation_ = schematic::Rotation::R0;
    schematic::Point cursor_;

    std::set<std::size_t> selectedParts_;
    std::set<std::size_t> selectedWires_;
    std::set<std::size_t> selectedLabels_;
    std::set<std::size_t> selectedInstruments_;

    std::optional<schematic::Point> wireStart_; // the Wire tool's current start
    std::optional<schematic::Point> dragFrom_;  // Select: where a drag of the selection began
};

} // namespace proton::app::sheet
