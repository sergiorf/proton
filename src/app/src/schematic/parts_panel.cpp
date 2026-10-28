// SPDX-License-Identifier: AGPL-3.0-only
#include "parts_panel.h"

#include "symbols.h"

#include "proton/schematic/scene_edit.h"

#include <QColor>
#include <QIcon>
#include <QListWidget>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSignalBlocker>

#include <algorithm>
#include <type_traits>
#include <variant>

namespace proton::app::sheet {

namespace {

using schematic::ComponentKind;
using schematic::InstrumentKind;

constexpr int kIcon = 48;      // icon side, pixels
constexpr int kCellWidth = 92; // a grid cell: the icon and its name under it, whole
constexpr int kCellHeight = 82;

const QColor kTile(250, 250, 247); // the sheet's paper, so the icon looks as the part will
const QColor kInk(30, 34, 40);

// `what`'s symbol on a light tile, scaled to fit with a margin, at the screen's pixel density.
QIcon symbolIcon(const SchematicView::Placing& what, qreal pixelRatio)
{
    const schematic::Placement origin{};
    const schematic::Box box = std::visit(
        [&](auto kind) {
            using Kind = decltype(kind);
            if constexpr (std::is_same_v<Kind, ComponentKind>) {
                return schematic::bounds(schematic::PlacedPart{kind, {}, {}, origin});
            } else {
                return schematic::bounds(schematic::PlacedInstrument{kind, {}, origin});
            }
        },
        what);
    QPixmap pixmap(static_cast<int>(kIcon * pixelRatio), static_cast<int>(kIcon * pixelRatio));
    pixmap.setDevicePixelRatio(pixelRatio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(kTile);
    painter.drawRoundedRect(QRectF(0.5, 0.5, kIcon - 1.0, kIcon - 1.0), 6.0, 6.0);

    const double margin = 6.0;
    const double width = std::max(box.right - box.left, 1);
    const double height = std::max(box.bottom - box.top, 1);
    const double scale = std::min((kIcon - 2 * margin) / width, (kIcon - 2 * margin) / height);
    painter.translate(kIcon / 2.0, kIcon / 2.0);
    painter.scale(scale, scale);
    painter.translate(-(box.left + box.right) / 2.0, -(box.top + box.bottom) / 2.0);
    QPen pen(kInk, 1.6 / scale); // 1.6 pixels at any scale
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    std::visit([&](auto kind) { drawSymbol(painter, kind); }, what);
    return QIcon(pixmap);
}

} // namespace

PartsPanel::PartsPanel(QWidget* parent) : QTabWidget(parent)
{
    // The tabs down the left edge, so the panel is one icon wide.
    setTabPosition(QTabWidget::West);
    setDocumentMode(true);
    setUsesScrollButtons(false);

    QListWidget* passives = addGroup(tr("Passives"));
    addEntry(passives, tr("Resistor"), ComponentKind::Resistor);
    addEntry(passives, tr("Capacitor"), ComponentKind::Capacitor);
    addEntry(passives, tr("Inductor"), ComponentKind::Inductor);
    QListWidget* sources = addGroup(tr("Sources"));
    addEntry(sources, tr("Voltage source"), ComponentKind::VoltageSource);
    addEntry(sources, tr("Current source"), ComponentKind::CurrentSource);
    addEntry(sources, tr("Ground"), ComponentKind::Ground);
    QListWidget* instruments = addGroup(tr("Instruments"));
    addEntry(instruments, tr("Function generator"), InstrumentKind::Generator);
    addEntry(instruments, tr("DC power supply"), InstrumentKind::Supply);
    addEntry(instruments, tr("Oscilloscope"), InstrumentKind::Scope);
    addEntry(instruments, tr("Multimeter"), InstrumentKind::Multimeter);
}

QListWidget* PartsPanel::addGroup(const QString& name)
{
    auto* list = new QListWidget(this);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(kIcon, kIcon));
    list->setGridSize(QSize(kCellWidth, kCellHeight));
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setWordWrap(true);
    list->setTextElideMode(Qt::ElideNone);
    list->setUniformItemSizes(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setFocusPolicy(Qt::NoFocus); // keys stay with the sheet: R turns what is being placed
    connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        for (const Entry& entry : entries_) {
            if (entry.item == item) {
                Q_EMIT chosen(entry.what);
                return;
            }
        }
    });
    addTab(list, name);
    return list;
}

void PartsPanel::addEntry(QListWidget* list, const QString& name, SchematicView::Placing what)
{
    auto* item = new QListWidgetItem(symbolIcon(what, devicePixelRatioF()), name, list);
    item->setToolTip(tr("%1: click, then click the sheet to place; R turns it").arg(name));
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    entries_.push_back({list, item, what});
}

const PartsPanel::Entry* PartsPanel::find(const SchematicView::Placing& what) const
{
    const auto found =
        std::ranges::find_if(entries_, [&](const Entry& entry) { return entry.what == what; });
    return found != entries_.end() ? &*found : nullptr;
}

std::vector<SchematicView::Placing> PartsPanel::entries() const
{
    std::vector<SchematicView::Placing> result;
    for (const Entry& entry : entries_) {
        result.push_back(entry.what);
    }
    return result;
}

QString PartsPanel::name(const SchematicView::Placing& what) const
{
    const Entry* entry = find(what);
    return entry != nullptr ? entry->item->text() : QString();
}

QIcon PartsPanel::icon(const SchematicView::Placing& what) const
{
    const Entry* entry = find(what);
    return entry != nullptr ? entry->item->icon() : QIcon();
}

std::pair<QWidget*, QPoint> PartsPanel::locate(const SchematicView::Placing& what)
{
    const Entry* entry = find(what);
    if (entry == nullptr) {
        return {nullptr, {}};
    }
    setCurrentWidget(entry->list);
    entry->list->scrollToItem(entry->item);
    return {entry->list->viewport(), entry->list->visualItemRect(entry->item).center()};
}

void PartsPanel::showTool(SchematicView::Tool tool, const SchematicView::Placing& placing)
{
    for (const Entry& entry : entries_) {
        const QSignalBlocker blocker(entry.list);
        entry.list->clearSelection();
    }
    if (tool != SchematicView::Tool::Place) {
        return;
    }
    if (const Entry* entry = find(placing)) {
        const QSignalBlocker blocker(entry->list);
        setCurrentWidget(entry->list);
        entry->item->setSelected(true);
    }
}

} // namespace proton::app::sheet
