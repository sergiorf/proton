// SPDX-License-Identifier: AGPL-3.0-only
#include "schematic_view.h"

#include "symbols.h"

#include "proton/schematic/scene_edit.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <vector>

namespace proton::app::sheet {

namespace {

using schematic::ComponentKind;
using schematic::Placement;
using schematic::PlacedPart;
using schematic::Point;
using schematic::Rotation;
using schematic::Scene;
using schematic::Wire;

constexpr double kGrid = 12.0;    // pixels per grid unit at 100 %
constexpr int kSheetHalf = 200;   // the sheet runs from -kSheetHalf to kSheetHalf grid units
constexpr double kLine = 0.14;    // pen width, grid units

const QColor kPaper(250, 250, 247);
const QColor kGridDot(200, 202, 206);
const QColor kInk(30, 34, 40);
const QColor kWireInk(20, 90, 160);
const QColor kSelected(230, 120, 20);
const QColor kGhost(30, 34, 40, 90);
const QColor kLabelInk(150, 40, 40);

QPointF at(Point p)
{
    return {static_cast<double>(p.x), static_cast<double>(p.y)};
}

Rotation turned(Rotation r)
{
    return static_cast<Rotation>((static_cast<int>(r) + 1) % 4);
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

        for (std::size_t i = 0; i < scene.wires.size(); ++i) {
            painter->setPen(pen(selectedWires.contains(i) ? kSelected : kWireInk));
            painter->drawLine(at(scene.wires[i].from), at(scene.wires[i].to));
        }
        for (std::size_t i = 0; i < scene.parts.size(); ++i) {
            drawPart(*painter, scene.parts[i], pen(selectedParts.contains(i) ? kSelected : kInk),
                     dragOffset && selectedParts.contains(i) ? *dragOffset : Point{0, 0});
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(kWireInk);
        for (const Point dot : schematic::junctionDots(scene)) {
            painter->drawEllipse(at(dot), 0.28, 0.28);
        }
        painter->setBrush(Qt::NoBrush);
        for (std::size_t i = 0; i < scene.labels.size(); ++i) {
            drawLabel(*painter, scene.labels[i], selectedLabels.contains(i));
        }

        // Previews.
        if (ghost) {
            drawPart(*painter, *ghost, pen(kGhost), {0, 0});
        }
        painter->setPen(pen(kGhost));
        for (const Wire& wire : wirePreview) {
            painter->drawLine(at(wire.from), at(wire.to));
        }
    }

    std::set<std::size_t> selectedParts;
    std::set<std::size_t> selectedWires;
    std::set<std::size_t> selectedLabels;
    std::optional<PlacedPart> ghost;      // the part being placed
    std::vector<Wire> wirePreview;        // the wire being drawn
    std::optional<Point> dragOffset;      // how far the selected parts are being dragged

private:
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

SchematicView::SchematicView(QWidget* parent) : QGraphicsView(parent)
{
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
    scale(2.0, 2.0); // a first-year circuit fills the sheet at this size
    centerOn(5 * kGrid, 3 * kGrid);
}

void SchematicView::setScene(const Scene& scene)
{
    scene_ = scene;
    selectedParts_.clear();
    selectedWires_.clear();
    selectedLabels_.clear();
    wireStart_.reset();
    refresh();
    // Centre the drawing.
    if (!scene_.parts.empty()) {
        schematic::Box all = schematic::bounds(scene_.parts.front());
        for (const PlacedPart& part : scene_.parts) {
            const schematic::Box box = schematic::bounds(part);
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
        selectedParts_.clear();
        selectedWires_.clear();
        selectedLabels_.clear();
    }
    setCursor(tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    refresh();
}

void SchematicView::placeKind(ComponentKind kind)
{
    placing_ = kind;
    placingRotation_ = Rotation::R0;
    setTool(Tool::Place);
}

void SchematicView::placePart(ComponentKind kind, Point where, Rotation rotation,
                              const std::string& value)
{
    PlacedPart part{kind, schematic::nextDesignator(scene_, kind),
                    value.empty() ? schematic::defaultValue(kind) : value,
                    Placement{where, rotation, false}};
    scene_.parts.push_back(std::move(part));
    commit();
}

void SchematicView::drawWire(Point from, Point to)
{
    for (const Wire& wire : schematic::route(from, to)) {
        scene_.wires.push_back(wire);
    }
    commit();
}

void SchematicView::addLabel(const std::string& name, Point where)
{
    scene_.labels.push_back({name, where});
    commit();
}

void SchematicView::clearSheet()
{
    setScene(Scene{});
    Q_EMIT edited();
}

Point SchematicView::gridPoint(const QPoint& viewPosition) const
{
    const QPointF p = mapToScene(viewPosition) / kGrid;
    return {static_cast<int>(std::lround(p.x())), static_cast<int>(std::lround(p.y()))};
}

void SchematicView::commit()
{
    refresh();
    Q_EMIT edited();
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
    sheet_->selectedParts = selectedParts_;
    sheet_->selectedWires = selectedWires_;
    sheet_->selectedLabels = selectedLabels_;
    sheet_->ghost.reset();
    sheet_->wirePreview.clear();
    if (tool_ == Tool::Place) {
        sheet_->ghost = PlacedPart{placing_, {}, {}, Placement{cursor_, placingRotation_, false}};
    }
    if (tool_ == Tool::Wire && wireStart_) {
        sheet_->wirePreview = schematic::route(*wireStart_, cursor_);
    }
    sheet_->update();
}

void SchematicView::selectAt(Point p, bool add)
{
    if (!add) {
        selectedParts_.clear();
        selectedWires_.clear();
        selectedLabels_.clear();
    }
    // Labels and wires are thin, parts large: try them in that order.
    if (const auto label = schematic::labelAt(scene_, p)) {
        selectedLabels_.insert(*label);
    } else if (const auto wire = schematic::wireAt(scene_, p)) {
        selectedWires_.insert(*wire);
    } else if (const auto part = schematic::partAt(scene_, p)) {
        selectedParts_.insert(*part);
    }
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
        } else {
            setTool(Tool::Select);
        }
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    switch (tool_) {
    case Tool::Select: {
        const bool add = (event->modifiers() & Qt::ShiftModifier) != 0;
        const auto part = schematic::partAt(scene_, p);
        if (part && selectedParts_.contains(*part) && !add) {
            dragFrom_ = p; // drag what is selected
        } else {
            selectAt(p, add);
            if (part && selectedParts_.contains(*part)) {
                dragFrom_ = p;
            }
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
    case Tool::Place:
        placePart(placing_, p, placingRotation_);
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
    if (tool_ != Tool::Select || !dragFrom_) {
        return;
    }
    const Point p = gridPoint(event->position().toPoint());
    const Point offset{p.x - dragFrom_->x, p.y - dragFrom_->y};
    dragFrom_.reset();
    sheet_->dragOffset.reset();
    if (offset == Point{0, 0}) {
        refresh();
        return;
    }
    for (const std::size_t i : selectedParts_) {
        const Point position = scene_.parts[i].placement.position;
        schematic::movePart(scene_, i, {position.x + offset.x, position.y + offset.y});
    }
    commit();
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
        } else {
            setTool(Tool::Select);
        }
        return;
    case Qt::Key_R:
        if (tool_ == Tool::Place) {
            placingRotation_ = turned(placingRotation_);
            refresh();
        } else if (tool_ == Tool::Select && !selectedParts_.empty()) {
            for (const std::size_t i : selectedParts_) {
                schematic::rotatePart(scene_, i);
            }
            commit();
        }
        return;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (tool_ == Tool::Select &&
            !(selectedParts_.empty() && selectedWires_.empty() && selectedLabels_.empty())) {
            schematic::remove(scene_, {selectedParts_.begin(), selectedParts_.end()},
                              {selectedWires_.begin(), selectedWires_.end()},
                              {selectedLabels_.begin(), selectedLabels_.end()});
            selectedParts_.clear();
            selectedWires_.clear();
            selectedLabels_.clear();
            commit();
        }
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
    const double zoom = transform().m11() * factor;
    if (zoom > 0.25 && zoom < 6.0) {
        scale(factor, factor);
    }
    event->accept();
}

void SchematicView::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->fillRect(rect, kPaper);
    if (transform().m11() * kGrid < 6.0) {
        return; // too far out for a readable grid
    }
    painter->setPen(QPen(kGridDot, 0));
    const int left = static_cast<int>(std::floor(rect.left() / kGrid));
    const int right = static_cast<int>(std::ceil(rect.right() / kGrid));
    const int top = static_cast<int>(std::floor(rect.top() / kGrid));
    const int bottom = static_cast<int>(std::ceil(rect.bottom() / kGrid));
    std::vector<QPointF> dots;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            dots.emplace_back(x * kGrid, y * kGrid);
        }
    }
    painter->drawPoints(dots.data(), static_cast<int>(dots.size()));
}

void SchematicView::editPart(std::size_t index)
{
    PlacedPart& part = scene_.parts[index];
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
    part.designator = designator->text().trimmed().toStdString();
    part.value = value->text().trimmed().toStdString();
    commit();
}

void SchematicView::editLabel(std::size_t index)
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Net label"), tr("Net name:"), QLineEdit::Normal,
        QString::fromStdString(scene_.labels[index].name), &ok);
    if (ok && !name.trimmed().isEmpty()) {
        scene_.labels[index].name = name.trimmed().toStdString();
        commit();
    }
}

} // namespace proton::app::sheet
