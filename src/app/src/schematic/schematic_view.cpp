// SPDX-License-Identifier: AGPL-3.0-only
#include "schematic_view.h"

#include "symbols.h"

#include "proton/schematic/scene_edit.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QHelpEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QCursor>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QToolTip>
#include <QUndoCommand>
#include <QUndoStack>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace proton::app::sheet {

namespace {

using schematic::ComponentKind;
using schematic::InstrumentKind;
using schematic::PlacedInstrument;
using schematic::Placement;
using schematic::PlacedPart;
using schematic::Point;
using schematic::Rotation;
using schematic::Scene;
using schematic::Wire;

constexpr double kGrid = 12.0;    // pixels per grid unit at 100 %
constexpr int kSheetHalf = 200;   // the sheet runs from -kSheetHalf to kSheetHalf grid units
constexpr double kLine = 0.14;    // pen width, grid units
constexpr int kMajor = 5;         // grid units between the darker lines
constexpr double kStartZoom = 2.0; // a first-year circuit fills the sheet at this size
constexpr double kMinZoom = 0.25;
constexpr double kMaxZoom = 6.0;
constexpr double kZoomStep = 1.25; // View > Zoom in and out

const QColor kPaper(250, 250, 247);
const QColor kGridMinor(232, 234, 237); // a line every grid unit
const QColor kGridMajor(208, 212, 218); // a line every kMajor units
const QColor kInk(30, 34, 40);
const QColor kWireInk(20, 90, 160);
const QColor kSelected(230, 120, 20);
const QColor kBand(230, 120, 20, 30); // inside the selection box
const QColor kGhost(30, 34, 40, 90);
const QColor kLabelInk(150, 40, 40);
const QColor kErrorMark(220, 40, 40, 80);    // under what an error is about
const QColor kWarningMark(235, 160, 0, 100); // under what a warning is about
const QColor kErrorBar(190, 35, 35);

QPointF at(Point p)
{
    return {static_cast<double>(p.x), static_cast<double>(p.y)};
}

// The drawing's bounds in scene pixels, with a margin of a few grid units for the names drawn
// above and beside the symbols.
QRectF framed(const schematic::Box& box)
{
    return {(box.left - 3) * kGrid, (box.top - 3) * kGrid, (box.right - box.left + 6) * kGrid,
            (box.bottom - box.top + 6) * kGrid};
}

Rotation turned(Rotation r)
{
    return static_cast<Rotation>((static_cast<int>(r) + 1) % 4);
}

QString text(std::string_view s)
{
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

} // namespace

// Paints the whole drawing: wires, parts with their names and values, labels, junction dots, and
// the tools' previews. One item, so the scene stays the only model.
class SheetItem : public QGraphicsItem
{
public:
    explicit SheetItem(const SchematicView& view) : view_(view) {}

    QRectF boundingRect() const override
    {
        const double half = kSheetHalf * kGrid;
        return {-half, -half, 2 * half, 2 * half};
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* /*option*/,
               QWidget* /*widget*/) override
    {
        painter->setRenderHint(QPainter::Antialiasing);
        painter->scale(kGrid, kGrid);
        const Scene& scene = view_.scene();
        const auto pen = [](const QColor& colour) {
            QPen p(colour, kLine);
            p.setCapStyle(Qt::RoundCap);
            p.setJoinStyle(Qt::RoundJoin);
            return p;
        };

        for (const SchematicView::Mark& mark : view_.marks()) {
            drawMark(*painter, scene, mark);
        }
        for (std::size_t i = 0; i < scene.wires.size(); ++i) {
            painter->setPen(pen(selectedWires.contains(i) ? kSelected : kWireInk));
            painter->drawLine(at(scene.wires[i].from), at(scene.wires[i].to));
        }
        for (std::size_t i = 0; i < scene.parts.size(); ++i) {
            drawPart(*painter, scene.parts[i], pen(selectedParts.contains(i) ? kSelected : kInk),
                     dragOffset && selectedParts.contains(i) ? *dragOffset : Point{0, 0});
        }
        for (std::size_t i = 0; i < scene.instruments.size(); ++i) {
            const bool selected = selectedInstruments.contains(i);
            drawInstrument(*painter, scene.instruments[i], pen(selected ? kSelected : kInk),
                           dragOffset && selected ? *dragOffset : Point{0, 0});
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(kWireInk);
        for (const Point dot : schematic::junctionDots(scene)) {
            painter->drawEllipse(at(dot), 0.28, 0.28);
        }
        painter->setBrush(kSelected);
        for (const std::size_t i : selectedJunctions) {
            if (i < scene.junctions.size()) {
                painter->drawEllipse(at(scene.junctions[i]), 0.36, 0.36);
            }
        }
        painter->setBrush(Qt::NoBrush);
        for (std::size_t i = 0; i < scene.labels.size(); ++i) {
            drawLabel(*painter, scene.labels[i], selectedLabels.contains(i));
        }

        // Previews.
        if (ghost) {
            drawPart(*painter, *ghost, pen(kGhost), {0, 0});
        }
        if (ghostInstrument) {
            drawInstrument(*painter, *ghostInstrument, pen(kGhost), {0, 0});
        }
        painter->setPen(pen(kGhost));
        for (const Wire& wire : wirePreview) {
            painter->drawLine(at(wire.from), at(wire.to));
        }
        if (band) {
            QPen dashed(kSelected, kLine / 2.0, Qt::DashLine);
            painter->setPen(dashed);
            painter->setBrush(kBand);
            painter->drawRect(QRectF(band->left, band->top, band->right - band->left,
                                     band->bottom - band->top));
        }
    }

    std::set<std::size_t> selectedParts;
    std::set<std::size_t> selectedWires;
    std::set<std::size_t> selectedLabels;
    std::set<std::size_t> selectedInstruments;
    std::set<std::size_t> selectedJunctions;
    std::optional<PlacedPart> ghost;     // the part being placed
    std::optional<PlacedInstrument> ghostInstrument; // the instrument being placed
    std::vector<Wire> wirePreview;        // the wire being drawn
    std::optional<Point> dragOffset;      // how far the selected parts are being dragged
    std::optional<schematic::Box> band;   // the selection box being drawn

private:
    // A diagnostic's mark, under the drawing: a broad stroke along its wires, a halo round its
    // symbols and labels, a dot on its pins.
    static void drawMark(QPainter& painter, const Scene& scene, const SchematicView::Mark& mark)
    {
        const QColor colour =
            mark.severity == schematic::Severity::Error ? kErrorMark : kWarningMark;
        const schematic::DiagnosticPlace& place = mark.place;
        painter.save();
        QPen stroke(colour, 0.7);
        stroke.setCapStyle(Qt::RoundCap);
        painter.setPen(stroke);
        for (const std::size_t i : place.wires) {
            if (i < scene.wires.size()) {
                painter.drawLine(at(scene.wires[i].from), at(scene.wires[i].to));
            }
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(colour);
        const auto halo = [&](const schematic::Box& box) {
            painter.drawRoundedRect(QRectF(box.left - 0.5, box.top - 0.5,
                                           box.right - box.left + 1.0, box.bottom - box.top + 1.0),
                                    0.5, 0.5);
        };
        for (const std::size_t i : place.parts) {
            if (i < scene.parts.size()) {
                halo(schematic::bounds(scene.parts[i]));
            }
        }
        for (const std::size_t i : place.instruments) {
            if (i < scene.instruments.size()) {
                halo(schematic::bounds(scene.instruments[i]));
            }
        }
        for (const std::size_t i : place.labels) {
            if (i < scene.labels.size()) {
                // Round the stem and the name drawLabel() draws, about 0.7 units a letter.
                const schematic::NetLabel& label = scene.labels[i];
                const double width = 0.6 + 0.75 * static_cast<double>(label.name.size());
                painter.drawRoundedRect(QRectF(label.position.x - 0.4, label.position.y - 2.1,
                                               width, 2.5),
                                        0.4, 0.4);
            }
        }
        for (const Point pin : place.pins) {
            painter.drawEllipse(at(pin), 0.6, 0.6);
        }
        painter.restore();
    }

    static void drawPart(QPainter& painter, PlacedPart part, const QPen& pen, Point offset)
    {
        part.placement.position.x += offset.x;
        part.placement.position.y += offset.y;
        painter.save();
        painter.setPen(pen);
        enterPlacement(painter, part.placement);
        drawSymbol(painter, part.kind);
        painter.restore();

        if (part.kind == ComponentKind::Ground) {
            return;
        }
        // Designator and value, upright, beside the symbol.
        const schematic::Box box = schematic::bounds(part);
        const bool upright = box.bottom - box.top > box.right - box.left;
        painter.save();
        painter.setPen(pen.color());
        QFont font = painter.font();
        font.setPixelSize(12);
        painter.setFont(font);
        painter.scale(1.0 / kGrid, 1.0 / kGrid);
        // Upright parts: both to the right. Lying parts: the designator above, the value below.
        const QString designator = QString::fromStdString(part.designator);
        const QString value = QString::fromStdString(part.value);
        if (upright) {
            const QPointF anchor((box.right + 0.6) * kGrid, (box.top + 1.6) * kGrid);
            painter.drawText(anchor, designator);
            painter.drawText(anchor + QPointF(0, 13), value);
        } else {
            const double x = (box.left + 0.4) * kGrid;
            painter.drawText(QPointF(x, (box.top - 0.4) * kGrid), designator);
            painter.drawText(QPointF(x, (box.bottom + 1.3) * kGrid), value);
        }
        painter.restore();
    }

    // An instrument's symbol, its terminals' names upright inside it, and its name above it.
    static void drawInstrument(QPainter& painter, PlacedInstrument instrument, const QPen& pen,
                               Point offset)
    {
        instrument.placement.position.x += offset.x;
        instrument.placement.position.y += offset.y;
        painter.save();
        painter.setPen(pen);
        enterPlacement(painter, instrument.placement);
        drawSymbol(painter, instrument.kind);
        QFont font = painter.font();
        font.setPixelSize(8);
        painter.setFont(font);
        const std::vector<std::string_view> names = schematic::terminalNames(instrument.kind);
        const std::vector<QPointF> anchors = terminalLabelOffsets(instrument.kind);
        for (std::size_t t = 0; t < names.size() && t < anchors.size(); ++t) {
            painter.save();
            painter.translate(anchors[t]);
            painter.rotate(-90.0 * static_cast<int>(instrument.placement.rotation));
            painter.scale(1.0 / kGrid, 1.0 / kGrid);
            painter.drawText(QRectF(-20.0, -6.0, 40.0, 12.0), Qt::AlignCenter, text(names[t]));
            painter.restore();
        }
        painter.restore();

        const schematic::Box box = schematic::bounds(instrument);
        painter.save();
        painter.setPen(pen.color());
        QFont nameFont = painter.font();
        nameFont.setPixelSize(12);
        painter.setFont(nameFont);
        painter.scale(1.0 / kGrid, 1.0 / kGrid);
        painter.drawText(QPointF((box.left + 0.2) * kGrid, (box.top - 0.4) * kGrid),
                         text(instrument.name));
        painter.restore();
    }

    static void drawLabel(QPainter& painter, const schematic::NetLabel& label, bool selected)
    {
        painter.save();
        painter.setPen(QPen(selected ? kSelected : kLabelInk, kLine));
        painter.drawLine(at(label.position), at(label.position) + QPointF(0.0, -0.8));
        painter.scale(1.0 / kGrid, 1.0 / kGrid);
        QFont font = painter.font();
        font.setPixelSize(12);
        font.setBold(true);
        painter.setFont(font);
        const QPointF corner((label.position.x + 0.2) * kGrid, (label.position.y - 0.9) * kGrid);
        painter.drawText(corner, QString::fromStdString(label.name));
        painter.restore();
    }

    const SchematicView& view_;
};

// One undo step: the scene before and after an edit.
class SceneStep : public QUndoCommand
{
public:
    SceneStep(SchematicView& view, const QString& text, Scene before, Scene after)
        : QUndoCommand(text), view_(view), before_(std::move(before)), after_(std::move(after))
    {
    }

    void undo() override { view_.restore(before_); }

    void redo() override
    {
        // QUndoStack::push() redoes the step, but the view already shows the edit, with the
        // selection the student may go on with (R again).
        if (pushed_) {
            view_.restore(after_);
        }
        pushed_ = true;
    }

private:
    SchematicView& view_;
    Scene before_;
    Scene after_;
    bool pushed_ = false;
};

SchematicView::SchematicView(QWidget* parent) : QGraphicsView(parent)
{
    undo_ = new QUndoStack(this);
    canvas_ = new QGraphicsScene(this);
    sheet_ = new SheetItem(*this);
    canvas_->addItem(sheet_);
    canvas_->setSceneRect(sheet_->boundingRect());
    QGraphicsView::setScene(canvas_);
    setRenderHint(QPainter::Antialiasing);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    scale(kStartZoom, kStartZoom);
    centerOn(5 * kGrid, 3 * kGrid);

    // The diagnostics' tooltips show while an instrument's panel has the focus, too.
    viewport()->setAttribute(Qt::WA_AlwaysShowToolTips);
    sheetBar_ = new QLabel(viewport());
    sheetBar_->setObjectName(QStringLiteral("sheet-diagnostics"));
    sheetBar_->setWordWrap(true);
    sheetBar_->setTextFormat(Qt::PlainText);
    sheetBar_->setContentsMargins(10, 6, 10, 6);
    sheetBar_->setStyleSheet(QStringLiteral("color: white; background: %1;").arg(kErrorBar.name()));
    sheetBar_->setAttribute(Qt::WA_TransparentForMouseEvents); // clicks reach the sheet
    sheetBar_->hide();
}

void SchematicView::setScene(const Scene& scene)
{
    scene_ = scene;
    committed_ = scene;
    undo_->clear();
    clearSelection();
    wireStart_.reset();
    refresh();
    // Centre the drawing, instruments included.
    std::vector<schematic::Box> boxes;
    for (const PlacedPart& part : scene_.parts) {
        boxes.push_back(schematic::bounds(part));
    }
    for (const PlacedInstrument& instrument : scene_.instruments) {
        boxes.push_back(schematic::bounds(instrument));
    }
    if (!boxes.empty()) {
        schematic::Box all = boxes.front();
        for (const schematic::Box& box : boxes) {
            all = {std::min(all.left, box.left), std::min(all.top, box.top),
                   std::max(all.right, box.right), std::max(all.bottom, box.bottom)};
        }
        centerOn((all.left + all.right) * kGrid / 2.0, (all.top + all.bottom) * kGrid / 2.0);
    }
}

void SchematicView::setTool(Tool tool)
{
    tool_ = tool;
    wireStart_.reset();
    dragFrom_.reset();
    if (tool != Tool::Select) {
        clearSelection();
    }
    setCursor(tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    refresh();
    Q_EMIT toolChanged(tool);
}

void SchematicView::placeKind(Placing what)
{
    placing_ = what;
    placingRotation_ = Rotation::R0;
    setTool(Tool::Place);
}

QPoint SchematicView::viewPoint(Point p) const
{
    return mapFromScene(QPointF(p.x * kGrid, p.y * kGrid));
}

void SchematicView::placePart(ComponentKind kind, Point where, Rotation rotation,
                              const std::string& value)
{
    PlacedPart part{kind, schematic::nextDesignator(scene_, kind),
                    value.empty() ? schematic::defaultValue(kind) : value,
                    Placement{where, rotation, false}};
    const QString text =
        part.designator.empty() ? tr("Place ground")
                                : tr("Place %1").arg(QString::fromStdString(part.designator));
    scene_.parts.push_back(std::move(part));
    commit(text);
}

void SchematicView::placeInstrument(InstrumentKind kind, Point where, Rotation rotation)
{
    PlacedInstrument instrument{kind, schematic::nextInstrumentName(scene_, kind),
                                Placement{where, rotation, false}};
    const QString name = text(instrument.name);
    scene_.instruments.push_back(std::move(instrument));
    commit(tr("Place %1").arg(name));
}

void SchematicView::drawWire(Point from, Point to)
{
    for (const Wire& wire : schematic::route(from, to)) {
        scene_.wires.push_back(wire);
    }
    commit(tr("Draw wire"));
}

void SchematicView::addLabel(const std::string& name, Point where)
{
    scene_.labels.push_back({name, where});
    commit(tr("Label %1").arg(QString::fromStdString(name)));
}

void SchematicView::moveItems(const std::vector<std::size_t>& parts,
                              const std::vector<std::size_t>& instruments, Point offset)
{
    for (const std::size_t i : parts) {
        if (i < scene_.parts.size()) {
            const Point position = scene_.parts[i].placement.position;
            schematic::movePart(scene_, i, {position.x + offset.x, position.y + offset.y});
        }
    }
    for (const std::size_t i : instruments) {
        if (i < scene_.instruments.size()) {
            const Point position = scene_.instruments[i].placement.position;
            schematic::moveInstrument(scene_, i, {position.x + offset.x, position.y + offset.y});
        }
    }
    commit(tr("Move"));
}

void SchematicView::rotateItems(const std::vector<std::size_t>& parts,
                                const std::vector<std::size_t>& instruments)
{
    for (const std::size_t i : parts) {
        schematic::rotatePart(scene_, i);
    }
    for (const std::size_t i : instruments) {
        schematic::rotateInstrument(scene_, i);
    }
    commit(tr("Rotate"));
}

void SchematicView::removeItems(const std::vector<std::size_t>& parts,
                                const std::vector<std::size_t>& wires,
                                const std::vector<std::size_t>& labels,
                                const std::vector<std::size_t>& instruments,
                                const std::vector<std::size_t>& junctions)
{
    schematic::remove(scene_, parts, wires, labels, instruments, junctions);
    clearSelection();
    commit(tr("Delete"));
}

bool SchematicView::renameInstrument(std::size_t index, const std::string& name)
{
    if (index >= scene_.instruments.size() || name.empty() ||
        schematic::instrumentNameTaken(scene_, name, index)) {
        return false;
    }
    const QString from = text(scene_.instruments[index].name);
    if (from == text(name)) {
        return true;
    }
    Q_EMIT instrumentRenamed(from, text(name));
    scene_.instruments[index].name = name;
    commit(tr("Rename %1 to %2").arg(from, text(name)));
    return true;
}

void SchematicView::addJunction(Point where)
{
    if (schematic::junctionAt(scene_, where)) {
        return;
    }
    scene_.junctions.push_back(where);
    commit(tr("Place junction"));
}

schematic::Selection SchematicView::selection() const
{
    return {{selectedParts_.begin(), selectedParts_.end()},
            {selectedWires_.begin(), selectedWires_.end()},
            {selectedLabels_.begin(), selectedLabels_.end()},
            {selectedInstruments_.begin(), selectedInstruments_.end()},
            {selectedJunctions_.begin(), selectedJunctions_.end()}};
}

void SchematicView::select(const schematic::Selection& selection)
{
    if (tool_ != Tool::Select) {
        setTool(Tool::Select);
    }
    selectedParts_ = {selection.parts.begin(), selection.parts.end()};
    selectedWires_ = {selection.wires.begin(), selection.wires.end()};
    selectedLabels_ = {selection.labels.begin(), selection.labels.end()};
    selectedInstruments_ = {selection.instruments.begin(), selection.instruments.end()};
    selectedJunctions_ = {selection.junctions.begin(), selection.junctions.end()};
    refresh();
}

void SchematicView::selectAll()
{
    const auto all = [](std::size_t n) {
        std::vector<std::size_t> indices(n);
        for (std::size_t i = 0; i < n; ++i) {
            indices[i] = i;
        }
        return indices;
    };
    select({all(scene_.parts.size()), all(scene_.wires.size()), all(scene_.labels.size()),
            all(scene_.instruments.size()), all(scene_.junctions.size())});
}

void SchematicView::rotate()
{
    if (tool_ == Tool::Place) {
        placingRotation_ = turned(placingRotation_);
        refresh();
    } else if (tool_ == Tool::Select &&
               !(selectedParts_.empty() && selectedInstruments_.empty())) {
        rotateItems({selectedParts_.begin(), selectedParts_.end()},
                    {selectedInstruments_.begin(), selectedInstruments_.end()});
    }
}

void SchematicView::mirror()
{
    if (tool_ != Tool::Select || (selectedParts_.empty() && selectedInstruments_.empty())) {
        return;
    }
    for (const std::size_t i : selectedParts_) {
        schematic::mirrorPart(scene_, i);
    }
    for (const std::size_t i : selectedInstruments_) {
        schematic::mirrorInstrument(scene_, i);
    }
    commit(tr("Mirror"));
}

void SchematicView::deleteSelection()
{
    if (tool_ == Tool::Select && hasSelection()) {
        const schematic::Selection s = selection();
        removeItems(s.parts, s.wires, s.labels, s.instruments, s.junctions);
    }
}

void SchematicView::copy()
{
    if (!hasSelection()) {
        return;
    }
    clip_ = schematic::extract(scene_, selection());
    const std::optional<schematic::Box> box = schematic::bounds(*clip_);
    clipOrigin_ = box ? Point{box->left, box->top} : Point{};
    pastes_ = 0;
    Q_EMIT clipboardChanged();
}

void SchematicView::cut()
{
    if (!hasSelection()) {
        return;
    }
    copy();
    const schematic::Selection s = selection();
    schematic::remove(scene_, s.parts, s.wires, s.labels, s.instruments, s.junctions);
    clearSelection();
    commit(tr("Cut"));
}

void SchematicView::paste()
{
    if (!clip_) {
        return;
    }
    // Under the mouse, or a step further below and right of the copy with each paste.
    const QPoint mouse = viewport()->mapFromGlobal(QCursor::pos());
    Point to;
    if (viewport()->rect().contains(mouse) && underMouse()) {
        to = gridPoint(mouse);
    } else {
        ++pastes_;
        to = {clipOrigin_.x + 2 * pastes_, clipOrigin_.y + 2 * pastes_};
    }
    const schematic::Selection added =
        schematic::paste(scene_, *clip_, {to.x - clipOrigin_.x, to.y - clipOrigin_.y});
    clearSelection();
    commit(tr("Paste"));
    select(added);
}

bool SchematicView::canEditProperties() const
{
    const std::size_t count = selectedParts_.size() + selectedWires_.size() +
                              selectedLabels_.size() + selectedInstruments_.size() +
                              selectedJunctions_.size();
    if (count != 1 || !selectedWires_.empty() || !selectedJunctions_.empty()) {
        return false;
    }
    return selectedParts_.empty() ||
           scene_.parts[*selectedParts_.begin()].kind != ComponentKind::Ground;
}

bool SchematicView::editProperties()
{
    if (!canEditProperties()) {
        return false;
    }
    if (!selectedParts_.empty()) {
        editPart(*selectedParts_.begin());
    } else if (!selectedInstruments_.empty()) {
        editInstrument(*selectedInstruments_.begin());
    } else {
        editLabel(*selectedLabels_.begin());
    }
    return true;
}

void SchematicView::zoomBy(double factor)
{
    const double target = std::clamp(zoom() * factor, kMinZoom, kMaxZoom);
    const ViewportAnchor anchor = transformationAnchor();
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    scale(target / zoom(), target / zoom());
    setTransformationAnchor(anchor);
}

void SchematicView::zoomIn()
{
    zoomBy(kZoomStep);
}

void SchematicView::zoomOut()
{
    zoomBy(1.0 / kZoomStep);
}

void SchematicView::zoomToFit()
{
    const std::optional<schematic::Box> box = schematic::bounds(scene_);
    if (!box) {
        setTransform(QTransform::fromScale(kStartZoom, kStartZoom));
        centerOn(0, 0);
        return;
    }
    const QRectF area = framed(*box);
    fitInView(area, Qt::KeepAspectRatio);
    if (zoom() > kMaxZoom || zoom() < kMinZoom) {
        setTransform(QTransform::fromScale(std::clamp(zoom(), kMinZoom, kMaxZoom),
                                           std::clamp(zoom(), kMinZoom, kMaxZoom)));
        centerOn(area.center());
    }
}

void SchematicView::fitInto(const QRect& area)
{
    const std::optional<schematic::Box> box = schematic::bounds(scene_);
    if (!box || area.width() <= 0 || area.height() <= 0) {
        return;
    }
    const QRectF drawing = framed(*box);
    const double scale =
        std::clamp(std::min({zoom(), area.width() / drawing.width(),
                             area.height() / drawing.height()}),
                   kMinZoom, kMaxZoom);
    setTransform(QTransform::fromScale(scale, scale));
    // centerOn() puts a point at the viewport's centre; shifted, the drawing's centre lands on
    // the area's.
    centerOn(drawing.center() + QPointF(viewport()->rect().center() - area.center()) / scale);
}

void SchematicView::setDiagnostics(const std::vector<schematic::Diagnostic>& diagnostics,
                                   const schematic::Conversion& conversion)
{
    marks_.clear();
    for (const schematic::Diagnostic& diagnostic : diagnostics) {
        marks_.push_back({diagnostic.severity, diagnostic.code,
                          QString::fromStdString(diagnostic.message),
                          schematic::locate(scene_, conversion, diagnostic)});
    }
    const QString bar = sheetDiagnostics();
    sheetBar_->setText(bar);
    sheetBar_->setVisible(!bar.isEmpty());
    placeSheetBar();
    sheet_->update();
}

namespace {

QString describe(const SchematicView::Mark& mark)
{
    return (mark.severity == schematic::Severity::Error ? SchematicView::tr("Error: %1")
                                                        : SchematicView::tr("Warning: %1"))
        .arg(mark.message);
}

} // namespace

QStringList SchematicView::diagnosticsAt(Point p) const
{
    QStringList messages;
    for (const Mark& mark : marks_) {
        if (!mark.place.empty() && schematic::contains(scene_, mark.place, p)) {
            messages << describe(mark);
        }
    }
    return messages;
}

QString SchematicView::sheetDiagnostics() const
{
    QStringList messages;
    for (const Mark& mark : marks_) {
        if (mark.place.empty()) {
            messages << describe(mark);
        }
    }
    return messages.join(QLatin1Char('\n'));
}

bool SchematicView::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        const auto* help = static_cast<QHelpEvent*>(event);
        const QStringList messages = diagnosticsAt(gridPoint(help->pos()));
        if (messages.isEmpty()) {
            QToolTip::hideText();
            event->ignore();
        } else {
            QToolTip::showText(help->globalPos(), messages.join(QLatin1Char('\n')), viewport());
        }
        return true;
    }
    return QGraphicsView::viewportEvent(event);
}

void SchematicView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    placeSheetBar();
}

void SchematicView::placeSheetBar()
{
    const int width = viewport()->width();
    sheetBar_->setGeometry(0, 0, width, sheetBar_->heightForWidth(width));
}

double SchematicView::zoom() const
{
    return transform().m11();
}

void SchematicView::setGridShown(bool shown)
{
    gridShown_ = shown;
    viewport()->update();
}

QImage SchematicView::image(double pixelsPerUnit) const
{
    const std::optional<schematic::Box> box = schematic::bounds(scene_);
    if (!box) {
        return {};
    }
    constexpr int kMargin = 3; // grid units, room for the names
    const int left = box->left - kMargin;
    const int top = box->top - kMargin;
    const QSize size(static_cast<int>((box->right - box->left + 2 * kMargin) * pixelsPerUnit),
                     static_cast<int>((box->bottom - box->top + 2 * kMargin) * pixelsPerUnit));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.scale(pixelsPerUnit / kGrid, pixelsPerUnit / kGrid);
    painter.translate(-left * kGrid, -top * kGrid);
    SheetItem drawing(*this); // nothing selected, no tool's preview
    drawing.paint(&painter, nullptr, nullptr);
    return image;
}

void SchematicView::setPartText(std::size_t index, const std::string& designator,
                                const std::string& value)
{
    if (index >= scene_.parts.size()) {
        return;
    }
    scene_.parts[index].designator = designator;
    scene_.parts[index].value = value;
    commit(tr("Edit %1").arg(QString::fromStdString(designator)));
}

void SchematicView::renameLabel(std::size_t index, const std::string& name)
{
    if (index >= scene_.labels.size()) {
        return;
    }
    scene_.labels[index].name = name;
    commit(tr("Rename label %1").arg(QString::fromStdString(name)));
}

void SchematicView::clearSheet()
{
    scene_ = Scene{};
    clearSelection();
    wireStart_.reset();
    commit(tr("New sheet"));
}

Point SchematicView::gridPoint(const QPoint& viewPosition) const
{
    const QPointF p = mapToScene(viewPosition) / kGrid;
    return {static_cast<int>(std::lround(p.x())), static_cast<int>(std::lround(p.y()))};
}

void SchematicView::commit(const QString& text)
{
    refresh();
    if (scene_ == committed_) {
        return;
    }
    undo_->push(new SceneStep(*this, text, committed_, scene_));
    committed_ = scene_;
    Q_EMIT edited();
}

void SchematicView::restore(const Scene& scene)
{
    scene_ = scene;
    committed_ = scene;
    clearSelection();
    dragFrom_.reset();
    sheet_->dragOffset.reset();
    refresh();
    Q_EMIT edited();
}

void SchematicView::clearSelection()
{
    selectedParts_.clear();
    selectedWires_.clear();
    selectedLabels_.clear();
    selectedInstruments_.clear();
    selectedJunctions_.clear();
}

void SchematicView::refresh()
{
    // Selections point into the scene; drop indices it no longer has.
    const auto prune = [](std::set<std::size_t>& set, std::size_t size) {
        std::erase_if(set, [size](std::size_t i) { return i >= size; });
    };
    prune(selectedParts_, scene_.parts.size());
    prune(selectedWires_, scene_.wires.size());
    prune(selectedLabels_, scene_.labels.size());
    prune(selectedInstruments_, scene_.instruments.size());
    prune(selectedJunctions_, scene_.junctions.size());
    const bool changed =
        sheet_->selectedParts != selectedParts_ || sheet_->selectedWires != selectedWires_ ||
        sheet_->selectedLabels != selectedLabels_ ||
        sheet_->selectedInstruments != selectedInstruments_ ||
        sheet_->selectedJunctions != selectedJunctions_;
    sheet_->selectedParts = selectedParts_;
    sheet_->selectedWires = selectedWires_;
    sheet_->selectedLabels = selectedLabels_;
    sheet_->selectedInstruments = selectedInstruments_;
    sheet_->selectedJunctions = selectedJunctions_;
    sheet_->ghost.reset();
    sheet_->ghostInstrument.reset();
    sheet_->wirePreview.clear();
    if (tool_ == Tool::Place) {
        const Placement placement{cursor_, placingRotation_, false};
        if (const auto* kind = std::get_if<ComponentKind>(&placing_)) {
            sheet_->ghost = PlacedPart{*kind, {}, {}, placement};
        } else {
            sheet_->ghostInstrument =
                PlacedInstrument{std::get<InstrumentKind>(placing_), {}, placement};
        }
    }
    if (tool_ == Tool::Wire && wireStart_) {
        sheet_->wirePreview = schematic::route(*wireStart_, cursor_);
    }
    sheet_->update();
    if (changed) {
        Q_EMIT selectionChanged();
    }
}

void SchematicView::selectAt(Point p, bool add)
{
    if (!add) {
        clearSelection();
    }
    // Junctions, labels and wires are thin, instruments and parts large: try them in that order;
    // instruments are drawn over parts.
    if (const auto junction = schematic::junctionAt(scene_, p)) {
        selectedJunctions_.insert(*junction);
    } else if (const auto label = schematic::labelAt(scene_, p)) {
        selectedLabels_.insert(*label);
    } else if (const auto wire = schematic::wireAt(scene_, p)) {
        selectedWires_.insert(*wire);
    } else if (const auto instrument = schematic::instrumentAt(scene_, p)) {
        selectedInstruments_.insert(*instrument);
    } else if (const auto part = schematic::partAt(scene_, p)) {
        selectedParts_.insert(*part);
    }
}

bool SchematicView::itemAt(Point p) const
{
    return schematic::junctionAt(scene_, p) || schematic::labelAt(scene_, p) ||
           schematic::wireAt(scene_, p) || schematic::instrumentAt(scene_, p) ||
           schematic::partAt(scene_, p);
}

bool SchematicView::selectedAt(Point p) const
{
    // In selectAt()'s order.
    if (const auto junction = schematic::junctionAt(scene_, p)) {
        return selectedJunctions_.contains(*junction);
    }
    if (const auto label = schematic::labelAt(scene_, p)) {
        return selectedLabels_.contains(*label);
    }
    if (const auto wire = schematic::wireAt(scene_, p)) {
        return selectedWires_.contains(*wire);
    }
    if (const auto instrument = schematic::instrumentAt(scene_, p)) {
        return selectedInstruments_.contains(*instrument);
    }
    const auto part = schematic::partAt(scene_, p);
    return part && selectedParts_.contains(*part);
}

void SchematicView::setContextActions(const QList<QAction*>& onItems,
                                      const QList<QAction*>& onSheet)
{
    itemActions_ = onItems;
    sheetActions_ = onSheet;
}

std::unique_ptr<QMenu> SchematicView::contextMenu(Point p)
{
    auto menu = std::make_unique<QMenu>(this);
    menu->setObjectName(QStringLiteral("sheet-menu"));
    if (!itemAt(p)) {
        clearSelection();
        refresh();
        menu->addActions(sheetActions_);
        return menu;
    }
    if (!selectedAt(p)) {
        selectAt(p, false);
    }
    refresh(); // the app's actions follow the selection before the menu shows them
    // An instrument's own entry, unless a wire, label or junction over it is what was clicked.
    const auto instrument = schematic::instrumentAt(scene_, p);
    if (instrument && !schematic::junctionAt(scene_, p) && !schematic::labelAt(scene_, p) &&
        !schematic::wireAt(scene_, p)) {
        const QString name = text(scene_.instruments[*instrument].name);
        QAction* open = menu->addAction(tr("&Open panel"),
                                        this, [this, name] { Q_EMIT instrumentOpened(name); });
        open->setObjectName(QStringLiteral("sheet-open-panel"));
        menu->addSeparator();
    }
    menu->addActions(itemActions_);
    return menu;
}

void SchematicView::finishWire()
{
    wireStart_.reset();
    refresh();
}

void SchematicView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    const Point p = gridPoint(event->position().toPoint());
    cursor_ = p;
    if (event->button() == Qt::RightButton) {
        if (tool_ == Tool::Wire && wireStart_) {
            finishWire();
        } else if (tool_ != Tool::Select) {
            setTool(Tool::Select);
        } else {
            contextMenu(p)->exec(event->globalPosition().toPoint());
        }
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    switch (tool_) {
    case Tool::Select: {
        const bool add = (event->modifiers() & Qt::ShiftModifier) != 0;
        if (!itemAt(p)) {
            // Empty sheet: a selection box from here.
            if (!add) {
                clearSelection();
            }
            boxFrom_ = p;
            refresh();
            break;
        }
        // A press on a selected part or instrument drags the selection.
        const auto onSelection = [&] {
            if (schematic::junctionAt(scene_, p) || schematic::labelAt(scene_, p) ||
                schematic::wireAt(scene_, p)) {
                return false;
            }
            if (const auto instrument = schematic::instrumentAt(scene_, p)) {
                return selectedInstruments_.contains(*instrument);
            }
            const auto part = schematic::partAt(scene_, p);
            return part && selectedParts_.contains(*part);
        };
        if (!(onSelection() && !add)) {
            selectAt(p, add);
        }
        if (onSelection()) {
            dragFrom_ = p;
        }
        refresh();
        break;
    }
    case Tool::Wire:
        if (wireStart_ && *wireStart_ != p) {
            drawWire(*wireStart_, p);
        }
        wireStart_ = p;
        refresh();
        break;
    case Tool::Label: {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Net label"), tr("Net name:"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            addLabel(name.trimmed().toStdString(), p);
        }
        break;
    }
    case Tool::Junction:
        addJunction(p);
        break;
    case Tool::Place:
        if (const auto* kind = std::get_if<ComponentKind>(&placing_)) {
            placePart(*kind, p, placingRotation_);
        } else {
            placeInstrument(std::get<InstrumentKind>(placing_), p, placingRotation_);
        }
        break;
    }
}

void SchematicView::mouseMoveEvent(QMouseEvent* event)
{
    QGraphicsView::mouseMoveEvent(event);
    const Point p = gridPoint(event->position().toPoint());
    if (p == cursor_) {
        return;
    }
    cursor_ = p;
    if (tool_ == Tool::Select && boxFrom_) {
        sheet_->band = schematic::Box{std::min(boxFrom_->x, p.x), std::min(boxFrom_->y, p.y),
                                      std::max(boxFrom_->x, p.x), std::max(boxFrom_->y, p.y)};
        sheet_->update();
        return;
    }
    if (tool_ == Tool::Select && dragFrom_) {
        sheet_->dragOffset = Point{p.x - dragFrom_->x, p.y - dragFrom_->y};
        sheet_->update();
        return;
    }
    refresh();
}

void SchematicView::mouseReleaseEvent(QMouseEvent* event)
{
    QGraphicsView::mouseReleaseEvent(event);
    const Point p = gridPoint(event->position().toPoint());
    if (tool_ == Tool::Select && boxFrom_) {
        // What the box holds joins the selection, which the press cleared unless Shift was down.
        const schematic::Selection boxed = schematic::within(
            scene_, {std::min(boxFrom_->x, p.x), std::min(boxFrom_->y, p.y),
                     std::max(boxFrom_->x, p.x), std::max(boxFrom_->y, p.y)});
        boxFrom_.reset();
        sheet_->band.reset();
        selectedParts_.insert(boxed.parts.begin(), boxed.parts.end());
        selectedWires_.insert(boxed.wires.begin(), boxed.wires.end());
        selectedLabels_.insert(boxed.labels.begin(), boxed.labels.end());
        selectedInstruments_.insert(boxed.instruments.begin(), boxed.instruments.end());
        selectedJunctions_.insert(boxed.junctions.begin(), boxed.junctions.end());
        refresh();
        return;
    }
    if (tool_ != Tool::Select || !dragFrom_) {
        return;
    }
    const Point offset{p.x - dragFrom_->x, p.y - dragFrom_->y};
    dragFrom_.reset();
    sheet_->dragOffset.reset();
    if (offset == Point{0, 0}) {
        refresh();
        return;
    }
    moveItems({selectedParts_.begin(), selectedParts_.end()},
              {selectedInstruments_.begin(), selectedInstruments_.end()}, offset);
}

void SchematicView::mouseDoubleClickEvent(QMouseEvent* event)
{
    const Point p = gridPoint(event->position().toPoint());
    if (tool_ == Tool::Wire) {
        finishWire();
        return;
    }
    if (tool_ != Tool::Select) {
        return;
    }
    if (const auto label = schematic::labelAt(scene_, p)) {
        editLabel(*label);
    } else if (const auto instrument = schematic::instrumentAt(scene_, p)) {
        Q_EMIT instrumentOpened(text(scene_.instruments[*instrument].name));
    } else if (const auto part = schematic::partAt(scene_, p)) {
        editPart(*part);
    }
}

void SchematicView::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        if (tool_ == Tool::Wire && wireStart_) {
            finishWire();
        } else if (tool_ == Tool::Select) {
            boxFrom_.reset();
            sheet_->band.reset();
            clearSelection();
            refresh();
        } else {
            setTool(Tool::Select);
        }
        return;
    // In the app the Edit menu's shortcuts take R and Delete first, to the same operations.
    case Qt::Key_R:
        rotate();
        return;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        deleteSelection();
        return;
    default:
        QGraphicsView::keyPressEvent(event);
    }
}

void SchematicView::wheelEvent(QWheelEvent* event)
{
    if ((event->modifiers() & Qt::ControlModifier) == 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }
    const double factor = std::pow(1.0015, event->angleDelta().y());
    const double target = zoom() * factor;
    if (target > kMinZoom && target < kMaxZoom) {
        scale(factor, factor);
    }
    event->accept();
}

void SchematicView::drawBackground(QPainter* painter, const QRectF& rect)
{
    // Squared paper: a light line every grid unit, where parts snap, and a darker one every
    // kMajor units. Far out, only the darker lines, then none.
    painter->fillRect(rect, kPaper);
    const double pixelsPerUnit = transform().m11() * kGrid;
    if (!gridShown_ || pixelsPerUnit * kMajor < 8.0) {
        return;
    }
    const int left = static_cast<int>(std::floor(rect.left() / kGrid));
    const int right = static_cast<int>(std::ceil(rect.right() / kGrid));
    const int top = static_cast<int>(std::floor(rect.top() / kGrid));
    const int bottom = static_cast<int>(std::ceil(rect.bottom() / kGrid));
    const auto lines = [&](bool major) {
        std::vector<QLineF> result;
        const auto wanted = [&](int i) { return (i % kMajor == 0) == major; };
        for (int x = left; x <= right; ++x) {
            if (wanted(x)) {
                result.emplace_back(x * kGrid, rect.top(), x * kGrid, rect.bottom());
            }
        }
        for (int y = top; y <= bottom; ++y) {
            if (wanted(y)) {
                result.emplace_back(rect.left(), y * kGrid, rect.right(), y * kGrid);
            }
        }
        return result;
    };
    if (pixelsPerUnit >= 6.0) {
        painter->setPen(QPen(kGridMinor, 0));
        const std::vector<QLineF> minor = lines(false);
        painter->drawLines(minor.data(), static_cast<int>(minor.size()));
    }
    painter->setPen(QPen(kGridMajor, 0));
    const std::vector<QLineF> major = lines(true);
    painter->drawLines(major.data(), static_cast<int>(major.size()));
}

void SchematicView::editPart(std::size_t index)
{
    const PlacedPart& part = scene_.parts[index];
    if (part.kind == ComponentKind::Ground) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Edit %1").arg(QString::fromStdString(part.designator)));
    auto* layout = new QFormLayout(&dialog);
    auto* designator = new QLineEdit(QString::fromStdString(part.designator), &dialog);
    auto* value = new QLineEdit(QString::fromStdString(part.value), &dialog);
    value->setToolTip(tr("As SPICE writes it: 4.7k, 100n, 10u"));
    layout->addRow(tr("Designator"), designator);
    layout->addRow(tr("Value"), value);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    value->setFocus();
    value->selectAll();
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    setPartText(index, designator->text().trimmed().toStdString(),
                value->text().trimmed().toStdString());
}

void SchematicView::editInstrument(std::size_t index)
{
    const PlacedInstrument& instrument = scene_.instruments[index];
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Rename %1").arg(text(instrument.name)), tr("Instrument name:"),
        QLineEdit::Normal, text(instrument.name), &ok);
    if (!ok) {
        return;
    }
    const std::string wanted = name.trimmed().toStdString();
    if (!renameInstrument(index, wanted)) {
        QMessageBox::warning(this, tr("Rename"),
                             wanted.empty()
                                 ? tr("An instrument needs a name.")
                                 : tr("Another instrument is called %1.").arg(text(wanted)));
    }
}

void SchematicView::editLabel(std::size_t index)
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Net label"), tr("Net name:"), QLineEdit::Normal,
        QString::fromStdString(scene_.labels[index].name), &ok);
    if (ok && !name.trimmed().isEmpty()) {
        renameLabel(index, name.trimmed().toStdString());
    }
}

} // namespace proton::app::sheet
