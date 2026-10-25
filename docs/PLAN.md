# Proton MVP implementation plan

Scope: the MVP in [ROADMAP.md](ROADMAP.md), built from scratch, Windows only, by one developer
working with Claude Code. Design and rules are in [ARCHITECTURE.md](ARCHITECTURE.md); this file
says in what order things get built and how each step is known to be done.

## How to use this plan

- Tasks are numbered `M<milestone>.<n>` and sized for one working session. Do them in order inside
  a milestone; a task may only rely on tasks before it.
- Every task ends with a **Done when** line. A task is done when that holds, its tests pass in CI,
  and ARCHITECTURE.md / ROADMAP.md are still true (update them in the same change).
- ARCHITECTURE.md stays high level: layers, dependencies, threads, contracts, invariants and why.
  Change it only when one of those changes. How a class works goes in its header comment,
  measurements and the choices they support in an ADR, and task history only here.
- When a task is done, append a marker to its heading: *(done YYYY-MM-DD)*. If everything holds
  except a CI check that a later task sets up, mark it *(done locally YYYY-MM-DD, CI pending
  M0.x)* and update the marker once that CI run is green.
- Each milestone ends with an exit check: something you can run and see, not only tests.
- Decisions are marked **Decision** and listed in the table at the end. Open ones have a proposed
  default, so work is never blocked; confirm or change it before the task starts.

## Milestones at a glance

| Milestone | Result | Depends on |
|---|---|---|
| M0 Foundations | Build, CI, and the whole pipeline running headless on the mock engine | - |
| M1 ngspice | Real simulation, validated against analytic answers; latency measured; time model decided | M0 |
| M2 First UI | Qt-free bench core; Qt app: 2-channel scope, multimeter, generator on built-in example circuits | M1 |
| M3 Bench model | Any number of instruments, DC supply, DMM current and resistance | M2 |
| M4 Schematic editor | Draw circuits, probe them, save and load projects | M3 |
| M5 Components and release | Full MVP component set, acceptance suite, Windows package | M4 |

After M5 the MVP "done when" list in the roadmap holds and a zip can be handed to a student.

## Technical baseline

Proposed defaults, recorded as ADRs in M0.4 and M1.1.

| Area | Choice | Why |
|---|---|---|
| Language | C++20 | `std::span`, concepts, designated initialisers for `Config` structs |
| Compiler | MSVC (Visual Studio 2022 or newer), plus clang on Linux CI for sanitizers | Windows target; MSVC has ASan but no TSan or UBSan |
| Build | CMake with `CMakePresets.json` | Standard, works in VS Code and Visual Studio |
| Qt | Current Qt 6 LTS, Widgets only, official installer locally, `install-qt-action` in CI | Building Qt from vcpkg takes hours; nothing else needs a package manager |
| Test framework | Own minimal harness in `tests/support` | Zero dependencies, as ARCHITECTURE.md section 10 says |
| ngspice | Pinned official Windows shared-library release, fetched by script with SHA-256 check, **loaded at runtime** (`LoadLibrary`) | App starts without it (mock fallback), can reload after a fatal ngspice error, clear license boundary |
| CI | GitHub Actions: Windows MSVC job (everything), Linux clang job (Qt-free libraries with ASan/UBSan and TSan) | The Linux job has no Qt installed, so it also enforces "no Qt below `src/app`" |

**Decision D1, hosting (decided):** GitHub with GitHub Actions.
**Decision D2, Proton's own license (decided):** AGPL-3.0-only, plus a separate commercial license
sold by the copyright holder (dual licensing). Consequences: every dependency shipped with Proton
must have a permissive or LGPL (dynamically linked) license so the commercial edition can ship it
too; outside contributions need a Contributor License Agreement.

### Repository layout

```
CMakeLists.txt, CMakePresets.json, cmake/      build, warnings, sanitizer options
src/core/          SpscRing, formatting, SPICE numbers
src/schematic/     Circuit, Scene (M4), validate, buildNetlist
src/instruments/   Scope, Multimeter, FunctionGenerator, PowerSupply, DSP helpers
src/sim/           ISimEngine, SimChunk, MockEngine, NgspiceEngine, SimWorker
src/bench/         Bench model and run control (M2.2, generalised in M3)
src/app/           Qt Widgets UI (only place Qt is allowed)
tools/proton-spike/  headless demo
tools/fetch-ngspice.ps1
tests/support/     test harness
tests/unit/<module>/, tests/integration/, tests/golden/
examples/          example projects (JSON from M4 on)
third_party/ngspice/  fetched, git-ignored
```

CMake option `PROTON_BUILD_APP` (default ON on Windows, OFF in the Linux CI job) controls whether
`src/app` and Qt are part of the build.

## M0 Foundations: the headless pipeline on the mock engine

Everything below the UI, tested, with no external dependencies. At the end, a circuit becomes a
netlist, the mock engine "simulates" it on a worker thread, and the scope and multimeter produce
frames and readings that tests check against analytic values.

**M0.1 Repository skeleton and build.** *(done 2026-10-20)* CMake project and presets: `windows-msvc-debug`,
`windows-msvc-release`, `linux-clang-asan` (ASan + UBSan), `linux-clang-tsan`. Warning flags
(`/W4 /permissive-` on MSVC, `-Wall -Wextra -Wpedantic` on clang) and warnings-as-errors in CI.
`.clang-format`, `.editorconfig`, `.gitignore`. One placeholder library with one test.
*Done when:* `cmake --preset windows-msvc-debug`, build and `ctest --preset windows-msvc-debug`
pass, and the CLAUDE.md "Build and test" section lists these commands.

**M0.2 Test harness.** *(done 2026-10-20)* `tests/support`: test registration macro, `CHECK`, `CHECK_EQ`,
`CHECK_NEAR(value, expected, tolerance)` with a readable failure message, non-zero exit code on
failure, one executable per module registered with CTest.
*Done when:* harness self-tests pass, and a deliberately failing test is shown to fail with file,
line and both values printed.

**M0.3 CI.** *(done 2026-10-20)* GitHub Actions workflow: Windows job (configure, build, test, Qt-free for now) and
Linux job (`linux-clang-asan` and `linux-clang-tsan`, Qt not installed).
*Done when:* both jobs are green on a pull request.

**M0.4 Contributor docs and ADRs.** *(done 2026-10-20)* `CONTRIBUTING.md` (layer rules from ARCHITECTURE.md section 2,
style, test rules, CLA requirement for outside contributions), `docs/LICENSING.md` (Proton under
AGPL-3.0-only with a commercial option; the dependency rule from D2 with a table of every
dependency and its license; Qt under LGPL with dynamic linking; ngspice license; original
component models only), `docs/adr/` with a template, ADR 0001 (Qt-free layers below the UI),
ADR 0003 (build and dependencies: the baseline table above) and ADR 0006 (licensing, D2). Every
source file starts with `// SPDX-License-Identifier: AGPL-3.0-only`.
*Done when:* the CLAUDE.md links to these files resolve, and a CI check fails on a source file
without the SPDX line.

**M0.5 core.** *(done 2026-10-20)* `SpscRing<T>`: fixed power-of-two capacity, move-only elements, acquire/release
ordering, `tryPush` / `tryPop`. Formatting: engineering notation with SI prefixes, fixed
significant digits, 4.5-digit (19999-count) meter display, overload text.
*Done when:* unit tests pass, and a two-thread stress test (10^6 items, order and count checked)
passes under TSan.

**M0.6 schematic: units and Circuit.** *(done 2026-10-21)* SPICE number parsing and formatting with SPICE quirks
(`m` milli, `meg` mega, case-insensitive, trailing units ignored as in `10uF`). `Circuit` with
`ComponentKind` resistor, capacitor, inductor, DC voltage source, DC current source, ground;
opaque `Fragment` lines from instruments.
*Done when:* a table-driven test covers every SI prefix, the `m`/`meg` trap and invalid input.

**M0.7 schematic: validate().** *(done 2026-10-21)* Diagnostics for: no ground; duplicate designators across components
and fragments, case-insensitive; unparsable or zero values; nets with a single connection; nets
with no DC path to ground (capacitors treated as open), which ngspice would otherwise report as a
singular matrix.
*Done when:* one test per diagnostic, plus a clean circuit producing none.

**M0.8 schematic: buildNetlist().** *(done 2026-10-21)* Pure function from `Circuit` to netlist text: stable element
order, fragments appended, no analysis line (the engine adds `.tran` / `.op`).
*Done when:* golden-file tests in `tests/golden` pass, and building the same circuit twice gives
byte-identical text.

**M0.9 instruments: FunctionGenerator.** *(done 2026-10-21)* `Config` (waveform sine / square / triangle / DC,
frequency, amplitude, offset, output impedance 50 ohm default), sanitising of impossible values,
`toSpice()` as a source plus a series resistor (`SIN`, `PULSE` with finite edges for square and
triangle).
*Done when:* golden fragment tests pass for each waveform.

**M0.10 instruments: Multimeter (voltage).** *(done 2026-10-21)* DC V (mean over the gate time) and AC V (true RMS,
DC removed); auto and fixed ranges; 4.5-digit display with overload.
*Done when:* analytic tests pass: DC of a known ramp, true RMS of a sine is A/sqrt(2) and of a
square is A; auto-range picks the expected range and changes range at the expected thresholds.

**M0.11 instruments: Scope front end.** *(done 2026-10-21)* Two channels sharing timebase and trigger from the start.
Per channel: V/div, offset, DC/AC coupling (one-pole high-pass in closed form on irregular
steps); optional one-pole bandwidth limit and optional ADC quantisation, both **off by default**;
resampling to the sample clock `fs = N / (10 x time/div)`; clipping to the screen range.
*Done when:* tests pass for: exact pass-through with options off; filter matches its closed-form
step response on a random irregular grid; a sine above Nyquist aliases to the expected frequency;
quantised samples are exact multiples of the LSB when quantisation is on.

**M0.12 instruments: Scope trigger and record.** *(done 2026-10-21)* Edge trigger (source channel, level, slope,
hysteresis, sub-sample position), pre/post-trigger record with the trigger at screen centre,
Auto / Normal / Single, dead time between acquisitions, output `ScopeFrame`.
*Done when:* a triggered sine is stable to sub-sample accuracy across 100 frames; Normal mode
gives no frame when the level is outside the signal and Auto does; Single stops after one frame.

**M0.13 sim: engine interface and MockEngine.** *(done 2026-10-21)* `ISimEngine` (`load`, `advance`, `reset`),
`SimChunk`, `indexOfSignal`. `MockEngine`: deterministic sine and RC low-pass on an irregular time
grid.
*Done when:* the mock's RC output matches the analytic answer, and two runs are bit-identical.

**M0.14 sim: SimWorker.** *(done 2026-10-22)* Simulation thread owning the engine, pushing `SimChunk`s into an
`SpscRing`, waiting when full (backpressure), clean stop and restart, errors reported through
`error()`.
*Done when:* stop/restart in a loop (1000 times) passes under TSan, and an engine error reaches
`error()` without crashing.

**M0.15 Integration test and headless demo.** *(done 2026-10-22)* `tests/integration`: circuit -> netlist -> mock engine
-> worker -> scope and multimeter. `tools/proton-spike`: runs that pipeline and prints readings and
a frame as CSV.
*Done when:* the integration test passes on both CI jobs, and `proton-spike` output plotted in any
tool shows the expected RC response.

**M0 exit check:** *(met 2026-10-22)* `proton-spike` prints a correct DMM reading and a stable, triggered scope frame
for the RC example.

## M1 ngspice: real simulation

**M1.1 ADR 0002 and fetching ngspice.** *(done 2026-10-22)* Pin an official ngspice Windows shared-library release;
`tools/fetch-ngspice.ps1` downloads it into `third_party/ngspice` and checks its SHA-256; CMake
finds the header and copies the DLL next to binaries. ADR 0002 records the version, runtime
loading, and the constraints below.
*Done when:* a fresh clone plus the script plus a build gives a working DLL next to the tests.

**M1.2 NgspiceEngine.** *(done 2026-10-22)* Runtime loading via `LoadLibrary` / `GetProcAddress`; init with callbacks;
load the netlist line by line plus `.tran`; run in ngspice's background thread; the data callback
streams accepted time points into a bounded queue and blocks when it is full; vector names map to
signal names; halt and reset. A guard enforces one instance per process.
*Done when:* an RC netlist streams chunks whose time points are strictly increasing, and loading a
broken netlist returns an error instead of crashing.

**M1.3 Validation against analytic answers.** *(done 2026-10-22)* ngspice tests, each test executable in its own
process: RC step response (time constant), RC low-pass driven by a sine at the corner (-3 dB,
-45 degrees), voltage divider, generator fragment driving a load through its output impedance.
*Done when:* all match the analytic answers within a stated tolerance, and the integration test
also runs with ngspice.

**M1.4 Robustness.** *(done 2026-10-22)* ngspice can call its exit callback on fatal errors. Handle it: mark the
engine dead, unload and reload the DLL, report through `SimWorker::error()`. Run 1000 load/reset
cycles and watch memory.
*Done when:* a netlist that makes ngspice fail fatally leaves the process running and a following
valid run succeeds; memory growth over the 1000 cycles is recorded in ADR 0002 (fixed if it
grows without bound).

**M1.5 Latency and live changes.** *(done 2026-10-23)* Measure circuit-to-frame latency for the chunk and queue
defaults. Prototype the two ways of changing a value while running: restart the run, or halt,
`alter` and resume. Measure both on a generator frequency change.
**Decision D3:** restart or alter for live changes (generator knobs, and later potentiometer and
switch). Default: whichever keeps the change under about 100 ms, restart if both do (simpler).
*Done when:* ADR 0005 records the measurements and the decision.

**M1.6 Engine choice in the demo.** *(done 2026-10-23)* `proton-spike --engine mock|ngspice`.
*Done when:* both engines give the same DMM reading on the RC example within tolerance.

**M1.7 Time model.** *(done 2026-10-24)* How simulation time reaches the student, headless and Qt-free: pacing
against wall-clock time (or not), the engine's maximum step derived from the bench's fastest
timebase, and what happens when a run reaches its stop time without reserving memory for an
endless run up front. Measure memory and wall-clock speed on the RC example at 100 us/div and
1 s/div.
**Decision D6:** the time model. Default: the bench runs in real time when the simulator keeps up
and shows a "slower than real time" indicator when it does not; `maxStep` is a fixed fraction of
the smallest time/div (so a time/div change restarts the run); a run is a window of a few seconds
that restarts from its last state (or, if that is not possible, from t = 0) when it ends.
*Done when:* an ADR records the measurements and D6, the defaults no longer commit memory for a
10 s run at 1 us steps, and a headless test shows a paced run following wall-clock time within a
stated tolerance.

**M1.8 ngspice under sanitizers.** *(done 2026-10-24)* A Linux path for `NgspiceEngine` (`dlopen`), used by CI so the
engine's threading code runs under TSan and ASan/UBSan.
**Decision D9:** whether to add a Linux libngspice for CI and where it comes from (a dependency,
so it needs approval and a line in LICENSING.md). Default: build the pinned ngspice 46 from source
in CI, cached; not shipped.
*Done when:* the ngspice engine tests run on `linux-clang-tsan` and `linux-clang-asan` in CI.

**M1 exit check:** *(met 2026-10-23)* `proton-spike --engine ngspice` shows the RC example with readings matching the
analytic answer.

## M2 First UI

The first time it looks like a bench. Circuits are built in code from a small list of examples;
the schematic editor comes in M4.

**M2.1 App shell.** *(done 2026-10-25)* `src/app` with `QMainWindow`, `PROTON_BUILD_APP`, `windeployqt` as a post-build
step, CI Windows job with Qt installed.
*Done when:* the app builds and starts in CI (a smoke test that opens and closes the window) and
locally from VS Code.

**M2.2 Bench core (Qt-free).** `src/bench` with one scope, one multimeter and one generator
(ARCHITECTURE.md section 5.1, generalised in M3.1): `contributeTo`, `attach`, `feed`, and run
control (start, restart on a circuit change with changes coalesced, the time model from D6).
`proton-spike` moves onto it. In `src/app`, a 16 ms UI timer pumps the bench, and an example menu
(RC low-pass, voltage divider, RC high-pass) loads circuits. Without ngspice the app says so and
does not simulate: the mock engine ignores the circuit, so it stays in tests and `proton-spike`.
*Done when:* headless bench tests cover a run, a restart and a missing net; choosing an example in
the app runs it, and switching examples restarts cleanly.

**M2.3 ScopeWidget and faceplate.** ADR 0004 (custom `QPainter`, no Qt Charts). Graticule, two
traces, trigger level and position markers; faceplate with time/div, V/div, position, coupling,
trigger source / level / slope / mode, Run / Stop / Single.
*Done when:* every knob changes the display as on a real scope, and the trace stays stable at
60 frames per second on the RC example (with the time model from D6).

**M2.4 Multimeter display and faceplate.** Mode (DC V, AC V), range (auto or fixed), probe binding
to a net of the example.
*Done when:* readings match `proton-spike` for the same example.

**M2.5 Generator faceplate.** Waveform, frequency, amplitude, offset, output on/off, using the
live-change method from D3.
*Done when:* turning the frequency updates the scope within the latency measured in M1.5.

**M2.6 Usability pass.** Use the app for a scripted session (measure the RC corner frequency with
the scope and the generator) and fix what gets in the way.
*Done when:* the session is possible without reading code, and fixes are in.

**M2 exit check:** measure the RC corner frequency in the app: input and output on two scope
channels, sweep the generator, read -3 dB on the multimeter.

## M3 Bench model

Implements ARCHITECTURE.md section 5.1 and the remaining MVP instruments.

**M3.1 Any number of instruments.** Generalise the M2.2 bench: a list of instrument instances with
unique names, connections to nets, per-instance frames and readings; `validate()` rejects duplicate
element names across fragments of several instruments.
*Done when:* headless bench tests cover two scopes and two meters on one circuit, and the app
behaves as in M2.

**M3.2 Instrument panels.** Add and remove instrument instances; one dockable panel per instance.
*Done when:* two scopes and two meters can be open at once, each bound to its own instance.

**M3.3 DC power supply.** Two channels, set voltage, output on/off, current limit as a plain value:
the panel shows the current drawn and flags when it exceeds the limit (CC/CV behaviour comes after
the MVP). `toSpice()` contributes a voltage source per channel; current is read from the source's
branch current.
*Done when:* a resistor load shows V/R on the supply's display; exceeding the limit shows the flag.

**M3.4 Multimeter DC current.** The meter in current mode is a two-terminal series element that
contributes a 0 V source; the reading is that source's branch current. The circuit must be broken
to insert it, as on a real bench.
*Done when:* current in a series circuit matches Ohm's law, and the voltage across the meter is
zero.

**M3.5 Multimeter resistance.** Starts with an ADR for decision D8 (analyses beyond transient and
ngspice process isolation). Adds an operating-point analysis to the engine as D8 decides
(ARCHITECTURE.md section 8 gets updated). The meter zeroes all sources in a copy of the circuit
(voltage sources become shorts, current sources open), injects a test current sized for the range
and reads the voltage.
**Decision D4 (decided):** ngspice allows one instance per process, so the resistance run cannot
happen while the transient run is going. Resistance mode only reads while the bench's sources are
off and otherwise shows "power off to measure", which is also correct practice on a real bench.
*Done when:* series, parallel and divider networks read their analytic resistance, and the
powered case behaves as decided in D4.

**M3.6 Probe binding by net name.** Pick a net for each scope channel, meter input and source
output from the circuit's net list; errors from `attach` are shown in the panel.
*Done when:* binding a probe to a missing net shows a clear message instead of a flat line.

**M3 exit check:** on the voltage divider example, power it from the supply, measure V, I and R
with the multimeter, and watch two nodes on the scope.

## M4 Schematic editor and project files

**M4.1 Scene model.** Plain data, Qt-free, in `src/schematic`: placed parts (position, rotation,
mirror), wires, junctions, labels. Conversion from scene to `Circuit` extracts nets from connected
wires and pins. Net labels are the first user-named nets, so `validate()` now rejects the names
the ngspice engine reserves (`Vproton_halt`, `proton_halt`, ADR 0005).
*Done when:* headless tests convert scenes with crossings, junctions and labels into the expected
nets, and a reserved name is a diagnostic.

**M4.2 Editor view.** `QGraphicsScene` / `QGraphicsView`: grid with snapping, place, move, rotate,
delete, draw wires, edit values; original component symbols.
*Done when:* the voltage divider and RC examples can be drawn by hand and simulate like the
built-in ones.

**M4.3 Undo and redo.** `QUndoStack` commands for every edit.
*Done when:* any sequence of edits undoes back to the empty sheet and redoes to the same scene.

**M4.4 Probe tool and instrument wiring.** Click a net to attach a scope channel, meter input or
source output; the series ammeter is placed as a part in the scene.
*Done when:* every instrument connection from M3 can be made in the editor without typing a net
name.

**M4.5 Diagnostics in the editor.** `validate()` results highlighted on the parts and nets they
refer to, with the message on hover.
*Done when:* each M0.7 diagnostic is shown at the right place.

**M4.6 Project files.** Versioned JSON with scene, instrument instances, settings and connections;
save, load, and a migration hook keyed on the version. The M2 built-in examples move to
`examples/` as project files.
*Done when:* save then load reproduces the same scene, netlist and instrument settings
(round-trip test), and the examples load from the menu.

**M4.7 Potentiometer and switch.** Both component kinds, with live changes that keep the
circuit's state, using the method from D7 (a potentiometer as two resistors whose split changes; a
switch as an element whose resistance changes between on and off). A restart (D3) would not do:
it computes a fresh operating point, so a switched RC would start already charged.
**Decision D7:** how a change keeps the circuit's state. Default: `alter` of the element's
resistance on the running simulation (`NgspiceEngine::alter()` and `SimWorker::resume()`, the
M1.5 prototype); the generator keeps restarting (D3).
*Done when:* turning a potentiometer in a divider changes the multimeter reading live, and
toggling a switch shows an RC charging and discharging on the scope, matching `1 - exp(-t / tau)`
from the moment of the toggle.

**M4 exit check:** draw the RC low-pass from an empty sheet, wire the generator and scope, save,
reopen, and measure the corner frequency.

## M5 Components and release

**M5.1 Diodes.** Generic original models: diode, LED by colour (forward drop per colour), Zener
(breakdown voltage as the value). Parameters documented in `docs/LICENSING.md` as original.
These are the first elements that need `.model` lines (and, for the op-amp in M5.3, a
`.subckt`): `buildNetlist()` gains a model section, written once per model used and in a stable
order, and `validate()` learns the new element letters.
*Done when:* tests check forward drop at a known current and Zener regulation at a known load,
and golden netlists cover the model section.

**M5.2 Transistors.** Generic NPN and PNP BJT, generic N- and P-channel MOSFET.
*Done when:* tests check a BJT's current gain in a bias circuit and a MOSFET switching a load
on and off at the expected gate voltage.

**M5.3 Op-amp.** One subcircuit: finite open-loop gain, single-pole gain-bandwidth product,
output saturating at the supply rails; an "ideal" mode with very high gain and no bandwidth limit,
still clamped to the rails.
**Decision D5:** default values of the generic op-amp. Default: gain 100 dB, GBW 1 MHz, rails
reached within 1 V (a classic general-purpose part).
*Done when:* an inverting amplifier has the expected gain and -3 dB point, and it clips at the
rails when overdriven.

**M5.4 Acceptance suite.** Each circuit from the roadmap's "Done when" list as a project in
`examples/`, plus a headless test that runs it through the bench and checks the instrument
readings against the analytic answer.
*Done when:* all roadmap circuits pass on CI.

**M5.5 Windows package.** `windeployqt`, the ngspice DLL and all license texts in a zip (an
installer can come later); a "Getting started" section in the README.
*Done when:* the zip runs on a clean Windows install (Windows Sandbox) and the M4 exit check can
be repeated from it.

**M5 exit check (MVP):** a student can unzip Proton, open an example or draw a circuit, and
complete every circuit on the roadmap's list with the bench instruments.

## Risks

| Risk | Where | Mitigation |
|---|---|---|
| ngspice fatal errors end the library's state | M1.4 | The engine resets, unloads and reloads the DLL (done in M1.4, ADR 0002); a crash that is not a controlled exit would need a helper process (bigger change, ADR) |
| One ngspice instance per process | M1.3, M3.5 | One process per ngspice test; D4 for resistance |
| Restarting on every knob turn feels sluggish | M1.5, M2.5 | Measured in M1.5: at most 22 ms to the first new frame (ADR 0005); a slow circuit visibly starting over is revisited in M2.6 with the alter prototype |
| A restart loses the circuit's state (a switched RC starts charged) | M4.7 | Switches and potentiometers keep state (D7) |
| Simulation runs at the simulator's speed, not the student's; long runs reserve memory up front | M1.7, M2 | Decided in M1.7 (D6, ADR 0007): pacing by the consumer, endless runs without a plot; fast timebases stay slower than real time (x0.4 at 100 us/div) |
| The engine's threading code is never run under TSan | M1.8 | Done in M1.8: ngspice 46 built on Linux, every ngspice test under TSan and ASan (ADR 0008) |
| One ngspice per process blocks resistance while running, the Bode plotter and a grader | M3.5, after the MVP | D8 decides analyses and process isolation together |
| A user's `.spiceinit` changes ngspice's behaviour (ADR 0002) | M5.5 | Keep ngspice from reading it in the package, or test that it does not matter |
| ngspice memory growth over many runs | M1.4 | Measured in M1.4: about 1 KiB per run, accepted (ADR 0002). Reloading the DLL does not give it back; a helper process would |
| Convergence failures on student circuits | M4, M5 | `validate()` catches the common causes first; ngspice errors shown as diagnostics, never a crash |
| Scope too slow to draw at 60 fps | M2.3 | Draw decimated min/max columns; `QRhi` only if still too slow (ADR 0004) |

## Decisions

| ID | Question | Answer | Status | Needed by |
|---|---|---|---|---|
| D1 | Hosting and CI | GitHub, GitHub Actions | decided | M0.3 |
| D2 | Proton's own license | AGPL-3.0-only plus a commercial license; dependencies permissive or LGPL only | decided | M0.4 |
| D3 | Restart or `alter` for live changes | Restart: both fit far under 100 ms, restart is simpler (ADR 0005). Changes that must keep state: D7 | decided | M1.5 |
| D4 | Resistance while the circuit is powered | Only with sources off; otherwise "power off to measure" | decided | M3.5 |
| D5 | Generic op-amp values | Proposed: 100 dB gain, 1 MHz GBW, rails within 1 V | open | M5.3 |
| D6 | Time model: pacing, maximum step, end of a run | Paced to wall-clock time by the consumer, slipping when behind; `maxStep` one sample of the fastest timebase, a change restarts; endless runs with `.save none` (ADR 0007) | decided | M1.7 |
| D7 | Live changes that keep the circuit's state (switch, potentiometer) | Proposed: `alter` on the running simulation; the generator keeps restarting | open | M4.7 |
| D8 | Analyses beyond transient; ngspice in a helper process | Proposed: decide together in an ADR at the start of M3.5 | open | M3.5 |
| D9 | Linux libngspice for sanitizer CI | The pinned ngspice 46 built from source in CI, cached, not shipped (ADR 0008) | decided | M1.8 |
