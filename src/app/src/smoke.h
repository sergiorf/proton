// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

namespace proton::app {

class MainWindow;

// The app's smoke checks for CI (ARCHITECTURE.md section 10: the UI has smoke tests only). Each
// runs inside the app's event loop and ends it: exit code 0 when the check passes, 1 otherwise,
// with what failed on stderr.

// Runs every example in menu order, switching to the next once the multimeter has read, the
// scope has shown a frame and a second and a half has passed, as a student choosing them from the
// menu would. Checks that the meter's display shows its reading as the meter formats it (as
// proton-spike prints it) and that the voltage divider reads 2.500 V. Prints the readings, the
// frame rate shown and the scope's paint times (ADR 0004).
void smokeExamples(MainWindow& window);

// Operates every control of the scope's and the multimeter's front panels with real input events
// (wheel, arrow keys, drag, click to push, button clicks) on the first example, and checks that
// each changes the instrument's settings or state as it should (PLAN.md M2.4, M2.5).
void smokePanel(MainWindow& window);

// Turns the generator's frequency knob one click on the running RC example and measures the
// wall-clock time until the scope shows the first frame of the restarted run (PLAN.md M2.6). It
// prints the time and passes when it is under kLatencyBound: generous, since CI runners are slow;
// ADR 0005 holds the numbers from a quiet machine.
void smokeLatency(MainWindow& window);

// Loads the voltage divider for its bench (the multimeter on `out`, DC V), clears the sheet and
// draws the divider again with the editor's own operations, as a student would by hand (PLAN.md
// M4.2): V1, R1, R2 and ground placed, wires routed between pins, `in` and `out` labelled. Passes
// when the multimeter then reads 2.500 V, as on the built-in divider.
void smokeEditor(MainWindow& window);

} // namespace proton::app
