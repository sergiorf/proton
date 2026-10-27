// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QIcon>

namespace proton::app {

// The menus' and toolbars' icons (PLAN.md M4.7): Proton's own, drawn with QPainter like the part
// symbols, so there is no icon set to depend on. Simple line drawings in the palette's text colour,
// so they read on a light or a dark theme, with colour only where it means something: Run green,
// Pause amber, Stop red, wires blue.
enum class Icon {
    New,
    Open,
    Save,
    ExportImage,
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    Delete,
    Rotate,
    Mirror,
    Properties,
    ZoomIn,
    ZoomOut,
    ZoomFit,
    Grid,
    Select,
    Wire,
    Label,
    Junction,
    Run,
    Pause,
    Stop,
};

// `which`, drawn at 16, 24 and 32 pixels, each at twice that for high-density screens. Qt draws
// the disabled state from it.
QIcon icon(Icon which);

} // namespace proton::app
