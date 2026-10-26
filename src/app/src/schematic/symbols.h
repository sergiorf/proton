// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/schematic/scene.h"

#include <QPointF>

#include <vector>

class QPainter;

namespace proton::app::sheet {

// Proton's component symbols (PLAN.md M4.2): original drawings in a common textbook style, in
// grid units around a part's origin, with the pins where schematic::pinOffsets() puts them.
// `painter` is already scaled to grid units and its pen set; the symbol is drawn in the part's own
// frame, before its placement turns and moves it.
void drawSymbol(QPainter& painter, schematic::ComponentKind kind);

// Proton's instrument symbols (ADR 0009): original, brand-neutral boxes the size of
// schematic::bounds(), drawn the same way: a generator with a sine on its face, a scope with a
// screen, a multimeter with a display and a dial, and a connector at each terminal. The
// terminals' names are drawn by the caller, upright, at terminalLabelOffsets().
void drawSymbol(QPainter& painter, schematic::InstrumentKind kind);
// Where each terminal's name goes, inside the box beside its connector, in the order of
// schematic::terminalNames().
std::vector<QPointF> terminalLabelOffsets(schematic::InstrumentKind kind);

// Turns `painter` into the frame of `placement`: moved to its position, turned, then mirrored, the
// same transformation schematic::place() applies to pin offsets.
void enterPlacement(QPainter& painter, const schematic::Placement& placement);

} // namespace proton::app::sheet
