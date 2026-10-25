// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QColor>

// The colours of Proton's instrument front panels (PLAN.md M2.4): a generic, brand-neutral bench
// instrument in dark grey with light lettering, shared by every panel and its widgets.
namespace proton::app::panel {

inline const QColor kPanel(54, 57, 62);        // the panel's face
inline const QColor kPanelEdge(34, 36, 40);    // bezels and section frames
inline const QColor kSectionLine(84, 89, 96);  // section outlines
inline const QColor kLettering(214, 217, 221); // labels printed on the panel
inline const QColor kDimLettering(150, 155, 162);
inline const QColor kKnobBody(42, 44, 48);
inline const QColor kKnobRim(96, 100, 106);
inline const QColor kPointer(235, 236, 238);
inline const QColor kFocus(90, 160, 230);
inline const QColor kButtonOff(70, 73, 78);

// Lamp colours of lit buttons.
inline const QColor kLampRun(70, 200, 90);
inline const QColor kLampStop(220, 60, 50);
inline const QColor kLampSingle(240, 150, 40);
inline const QColor kLampWhite(225, 228, 232);

// The scope's channel colours, used for its traces and its channel controls alike.
inline const QColor kChannel1(240, 220, 40);
inline const QColor kChannel2(40, 210, 230);

} // namespace proton::app::panel
