// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/schematic/diagnostic_place.h"
#include "proton/schematic/scene.h"
#include "proton/schematic/scene_edit.h"
#include "proton/schematic/validate.h"

#include <QGraphicsView>
#include <QImage>
#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

class QAction;
class QGraphicsScene;
class QLabel;
class QMenu;
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
//   and instruments to move them, their wires routed again square at every step of the drag
//   (schematic::moveSelection, PLAN.md M4.12); a drag that starts on empty sheet draws a box and
//   selects what lies wholly inside it (schematic::within; Shift adds); double-click a part to
//   edit its designator and value, a label to rename it, or an instrument to open its front panel
//   (instrumentOpened()). R turns the selected parts and instruments, Delete removes the
//   selection, Escape clears it. Right-click opens contextMenu() there.
//   Wiring with the same tool, as desktop circuit simulators do: a pin or terminal under the mouse
//   is ringed and the cursor is a crosshair; a press there starts a wire that follows the mouse on
//   a route round the symbols (schematic::routeAround). Released elsewhere after a drag, or
//   clicked again, it connects to the pin, terminal or wire there (ending on a wire's middle taps
//   it); a click on empty sheet fixes a bend. Escape or right-click drops it. A wire dragged by
//   its middle moves sideways, its neighbours stretching (schematic::dragSegment), unless that
//   would join it to another net; the ends of a selected wire, and free wire ends, drag to another
//   pin, terminal or wire, or off into empty sheet to disconnect (schematic::dragWireEnd).
// - Wire: click where a wire starts, then where it bends or ends; each click adds the route there
//   (horizontal, then vertical) and goes on from it. Double-click, right-click or Escape ends it.
// - Label: click on a net to name it.
// - Junction: click to put a junction dot there, connecting wires that cross at it.
// - Place a part or an instrument: it follows the mouse; R turns it; each click places one, a part
//   with the next designator and a default value, an instrument with the next name of its kind.
// Escape goes back to Select. Ctrl and the wheel zoom; the wheel scrolls. toolChanged() tells the
// toolbar and the parts panel which tool is on.
//
// The Edit and View menus (PLAN.md M4.7) work through the public operations below: the selection
// (selectAll, rotate, mirror, deleteSelection, editProperties), a clipboard of the view's own
// (copy, cut and paste a part of the drawing, schematic::extract and schematic::paste), zoom and
// the grid. The menus' keys (R, Del) reach the view as those operations, not as key presses.
//
// Every edit emits edited(), after which scene() is the new drawing, and is one step on
// undoStack() (PLAN.md M4.3): a drag, a turn of the selection, a wire drawn from a pin or one
// Wire tool click is one step, its wires tidied first (schematic::tidyWires). A step
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
        Junction,
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
                     const std::vector<std::size_t>& instruments = {},
                     const std::vector<std::size_t>& junctions = {});
    void setPartText(std::size_t index, const std::string& designator, const std::string& value);
    void renameLabel(std::size_t index, const std::string& name);
    // Renames instrument `index`, emitting instrumentRenamed() before edited(). Returns false, and
    // changes nothing, for an empty name or one another instrument has.
    bool renameInstrument(std::size_t index, const std::string& name);
    void addJunction(schematic::Point at);
    // Empties the sheet, as an edit that can be undone: there are no project files to reopen yet.
    void clearSheet();

    // The selection, as Select leaves it.
    schematic::Selection selection() const;
    bool hasSelection() const { return !selection().empty(); }
    // The actions of the right-click menu (PLAN.md M4.10): `onItems` on a part, instrument, wire,
    // label or junction, `onSheet` on empty sheet. The app passes its Edit menu's actions, so the
    // menu shows their shortcuts and enabled state.
    void setContextActions(const QList<QAction*>& onItems, const QList<QAction*>& onSheet);
    // The right-click menu at grid point `p`, as a right-click there opens it with the Select
    // tool: on an item, that item selected first unless it already is (a selection it is part of
    // stays), then Open panel for an instrument and the item actions; on empty sheet, the
    // selection cleared and the sheet actions.
    std::unique_ptr<QMenu> contextMenu(schematic::Point p);

    // Selects these items, or everything drawn, with the Select tool.
    void select(const schematic::Selection& selection);
    void selectAll();
    // Turns the selection a quarter turn, or the part being placed with the Place tool.
    void rotate();
    // Mirrors the selected parts and instruments left to right.
    void mirror();
    void deleteSelection();
    // The selection to the clipboard; cut deletes it as one step.
    void copy();
    void cut();
    // Adds the clipboard to the drawing as one step and selects it: under the mouse when it is
    // over the sheet, otherwise a little below and right of where it was copied from.
    void paste();
    bool canPaste() const { return clip_.has_value(); }
    // Opens the properties of the one selected item: a part's designator and value, an
    // instrument's name, a label's net name. False when the selection is not one such item.
    bool editProperties();
    bool canEditProperties() const;

    // Zoom by a step, and to show the whole drawing; the zoom is the view's scale, 2 at start.
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    // Shows the whole drawing in `area`, a rectangle of the viewport, centred there and zoomed
    // out if it must be, never in: the part of the sheet the instruments' panels leave free.
    void fitInto(const QRect& area);
    double zoom() const;
    void setGridShown(bool shown);
    bool gridShown() const { return gridShown_; }
    // The drawing alone on white, without grid, selection or tools, at `pixelsPerUnit` pixels
    // per grid unit with a margin around it: for File > Export image. Null for an empty sheet.
    QImage image(double pixelsPerUnit = 24.0) const;

    // A diagnostic where it is on the sheet.
    struct Mark
    {
        schematic::Severity severity = schematic::Severity::Error;
        schematic::DiagnosticCode code = schematic::DiagnosticCode::NoGround;
        QString message;
        schematic::DiagnosticPlace place; // empty: about the whole drawing
    };

    // The drawing's diagnostics (PLAN.md M4.9), `conversion` being convert() of scene(): each is
    // marked where schematic::locate() puts it, under the drawing, red for an error and amber for
    // a warning, with its message in a tooltip there; those about the whole drawing (no ground)
    // show in a bar along the top of the sheet. Shown until the next call.
    void setDiagnostics(const std::vector<schematic::Diagnostic>& diagnostics,
                        const schematic::Conversion& conversion);
    const std::vector<Mark>& marks() const { return marks_; }
    // What the tooltip at grid point `p` says: "Error: ..." or "Warning: ..." for each diagnostic
    // marked there, in order; empty where there is none.
    QStringList diagnosticsAt(schematic::Point p) const;
    // The bar's text; empty, and the bar hidden, when no diagnostic is about the whole drawing.
    QString sheetDiagnostics() const;

Q_SIGNALS:
    void edited();
    // The student double-clicked instrument `name`.
    void instrumentOpened(const QString& name);
    // Instrument `from` is about to be called `to`; edited() follows.
    void instrumentRenamed(const QString& from, const QString& to);
    void toolChanged(Tool tool);
    void selectionChanged();
    void clipboardChanged();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    bool viewportEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

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
    // Whether anything is drawn at `p`, and whether what selectAt() would pick there is selected.
    bool itemAt(schematic::Point p) const;
    bool selectedAt(schematic::Point p) const;
    void editPart(std::size_t index);
    void editLabel(std::size_t index);
    void editInstrument(std::size_t index);
    void zoomBy(double factor);
    // The bar along the sheet's top, as wide as the sheet and as tall as its text.
    void placeSheetBar();

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
    std::set<std::size_t> selectedJunctions_;

    std::optional<schematic::Scene> clip_; // the clipboard
    schematic::Point clipOrigin_;          // top left of what was copied, to paste beside it
    int pastes_ = 0;                       // pastes since the copy, each a step further
    bool gridShown_ = true;

    // What a press with the Select tool started, until its release.
    enum class Drag {
        None,
        Selection, // the selected items
        Segment,   // a wire, sideways
        End,       // a wire's end
    };
    // A wire drawn from a pin with the Select tool: what is fixed of it, and where it goes on from.
    struct PinWire
    {
        std::vector<schematic::Wire> fixed;
        schematic::Point last;
        bool pressed = false; // the button is still down from the press that started it
    };

    // The wires a drag moved, found again in scene_ after tidying.
    void reselectWires(const std::vector<schematic::Wire>& selected);
    // Whether a wire ending at `p` would connect there: a pin, a terminal, a wire, a junction.
    bool connectsAt(schematic::Point p) const;
    // The wire whose end at `p` a press there drags: a selected wire's, or a free end's.
    std::optional<std::size_t> draggableEndAt(schematic::Point p) const;
    // The drawing with the fixed part of the wire being drawn from a pin.
    schematic::Scene withPinWire() const;
    void finishPinWire(schematic::Point at);
    void updateCursor();

    std::optional<schematic::Point> wireStart_; // the Wire tool's current start
    std::optional<schematic::Point> dragFrom_;  // Select: where a drag began
    Drag drag_ = Drag::None;
    schematic::Scene dragBase_;     // the drawing when the drag began
    std::vector<std::size_t> dragWires_; // the wires selected when a Selection drag began
    std::size_t dragWire_ = 0;      // the wire a Segment or End drag holds
    schematic::Point dragEnd_;      // the end an End drag holds
    std::optional<PinWire> pinWire_;
    std::optional<schematic::Point> boxFrom_;   // Select: where a selection box began
    QList<QAction*> itemActions_;
    QList<QAction*> sheetActions_;

    std::vector<Mark> marks_;
    QLabel* sheetBar_ = nullptr; // the diagnostics about the whole drawing
};

} // namespace proton::app::sheet
