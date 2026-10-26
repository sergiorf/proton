// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

namespace proton::app {

class MainWindow;

// The app's smoke checks for CI (ARCHITECTURE.md section 10: the UI has smoke tests only). Each
// runs inside the app's event loop and ends it: exit code 0 when the check passes, 1 otherwise,
// with what failed on stderr.

// Presses Run, then runs every example in menu order, switching to the next once the multimeter has read, the
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

// Draws two circuits on an empty sheet as a student would by hand (PLAN.md M4.2, M4.4): every part
// and instrument is placed by clicking it in the parts panel and then clicking the sheet, and wired
// with wires only, no labels. A 5 V divider of two 1k resistors with dmm1 across the lower one and
// dmm2 on its top; gen1 driving a 1k load. scope1 has CH1 on the divider's middle and CH2 on the
// generator's output, scope2 has CH1 on the generator's output. All four panels are opened from
// their symbols at once. Passes when each shows its own instance's signal: dmm1 2.500 V, dmm2
// 5.000 V, scope1 a flat 2.5 V on CH1 and the generator's sine on CH2, scope2 that sine on CH1.
void smokeEditor(MainWindow& window);

// Makes every kind of edit on an empty sheet (place a part and an instrument, wire, label, move,
// rotate, edit a value, rename a label, delete, new sheet), then undoes them one by one with the Edit menu's Undo,
// checking the scene against the one before each edit down to the empty sheet, and redoes them
// back to the last scene the same way (PLAN.md M4.3).
void smokeUndo(MainWindow& window);

} // namespace proton::app
