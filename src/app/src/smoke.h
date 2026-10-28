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

// Triggers every action of the menus through its menu, and those on a toolbar through their
// toolbar button too, on the first example, checking what each does (PLAN.md M4.7): File (an
// example, export image answered with a temporary file, new), Edit (undo, redo, cut, copy,
// paste, delete, select all, rotate, mirror, properties of a part and of an instrument whose
// open panel follows its new name), View, Place (every part, instrument and tool), Options, Help,
// Simulate > Instruments and the Window menu. Then Run, Pause (time stands still), Run on from
// the pause without a restart, Pause, Stop, Run from t = 0 and Stop, and Quit.
void smokeMenus(MainWindow& window);

// The Select tool beyond one click (PLAN.md M4.10), with real mouse and key events on an empty
// sheet: a box drag selects what lies wholly inside it (not a wire half outside), Shift and a
// second box add, Delete removes the selection and Escape clears it; a right-click selects the
// item under it and opens its menu; and every entry of the right-click menu works on a part
// (Rotate, Mirror, Properties, Delete), an instrument (Open panel too), a wire and empty sheet
// (Select all, Paste).
void smokeSelection(MainWindow& window);

// Project files and the start page (PLAN.md M4.11), through the menus, the toolbar and the start
// page with their file dialogs answered: the start page lists every example and loads one; Save on
// an example asks where and writes the file, the title then naming it; after changing a part's
// value and an instrument's setting, Save writes them without asking; File > New, then Open
// brings back the same scene, settings and netlist; Open recent and the start page's recent list
// reopen it; and a file that is not a project is refused with a message, the sheet unchanged.
void smokeFiles(MainWindow& window);

// Makes each mistake a drawing can have on an empty sheet, through the editor's edits (PLAN.md
// M4.9), and checks that its diagnostic is marked at the right place with its message in the
// tooltip there: no ground in the bar along the sheet's top; an invalid value, a zero value, an
// invalid and a duplicate designator on their parts; an invalid and a reserved net name on the
// label and the net; a label on ground on that label; a single connection and a terminal wired to
// nothing on that pin or terminal; no DC path on the floating net's wire and pin. Then hovers a
// marked terminal and an empty spot with real tooltip events.
void smokeDiagnostics(MainWindow& window);

// The usability session (PLAN.md M4.8), as a student would do it with the panels: maximise the
// window, load the RC low-pass from File > Examples, check that its panels open side by side
// when the screen has room for them, set the generator to 1.592 kHz with the cursor and the knob,
// turn the scope to 50 us/div, press Run, and read the corner: the multimeter shows -3 dB of the
// generator's 1 V peak (0.500 V) and the scope's CH2 rises through 0 V 45 degrees after CH1.
void smokeSession(MainWindow& window);

// The DC power supply (PLAN.md M5.1), drawn by hand: psu1 from the parts panel with CH1 across a
// 1k resistor to ground, its panel opened from the symbol, Run. Passes when CH1's displays show
// 5.000 V and V/R, 0.0050 A, without LIMIT; then, the limit typed down to 1 mA, LIMIT lights; and
// with CH1's Output switched off the current display reads 0.0000 A.
void smokeSupply(MainWindow& window);

} // namespace proton::app
