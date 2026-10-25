// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

namespace proton::app {

class MainWindow;

// The app's smoke checks for CI (ARCHITECTURE.md section 10: the UI has smoke tests only). Each
// runs inside the app's event loop and ends it: exit code 0 when the check passes, 1 otherwise,
// with what failed on stderr.

// Runs every example in menu order, switching to the next once the multimeter has read, the
// scope has shown a frame and a second and a half has passed, as a student choosing them from the
// menu would. Prints the frame rate shown and the scope's paint times (ADR 0004).
void smokeExamples(MainWindow& window);

// Operates every control of the scope's front panel with real input events (wheel, arrow keys,
// drag, click to push, button clicks) on the first example, and checks that each changes the
// scope's settings or state as it should (PLAN.md M2.4).
void smokePanel(MainWindow& window);

} // namespace proton::app
