// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/schematic/scene.h"

class QPainter;

namespace proton::app::sheet {

// Proton's component symbols (PLAN.md M4.2): original drawings in a common textbook style, in
// grid units around a part's origin, with the pins where schematic::pinOffsets() puts them.
// `painter` is already scaled to grid units and its pen set; the symbol is drawn in the part's own
// frame, before its placement turns and moves it.
void drawSymbol(QPainter& painter, schematic::ComponentKind kind);

// Turns `painter` into the frame of `placement`: moved to its position, turned, then mirrored, the
// same transformation schematic::place() applies to pin offsets.
void enterPlacement(QPainter& painter, const schematic::Placement& placement);

} // namespace proton::app::sheet
