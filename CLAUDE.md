# Proton

Virtual electronics bench for students: build a circuit, power and drive it from bench
instruments, measure it as on a real bench. C++ / Qt 6 Widgets, ngspice as the first simulator.

Read before non-trivial work:
- docs/ARCHITECTURE.md: layers, data flow, threads, the instrument contract
- docs/ROADMAP.md: MVP scope and long-term direction
- docs/PLAN.md: MVP milestones and tasks; work through tasks in order, each to its "Done when"
- CONTRIBUTING.md, docs/LICENSING.md, docs/adr/

## Build and test

CMake presets; build trees go to `build/<preset>`.

```
cmake --preset windows-msvc-debug          # configure (also: windows-msvc-release)
cmake --build --preset windows-msvc-debug  # build
ctest --preset windows-msvc-debug          # run all tests
```

On Windows, `powershell -ExecutionPolicy Bypass -File tools/fetch-ngspice.ps1` once before
configuring fetches the pinned ngspice into `third_party/ngspice`; without it the build skips ngspice.
On Linux, `bash tools/fetch-ngspice.sh` builds the same release from source instead (ADR 0008).

Linux (clang + Ninja, Qt-free): `linux-clang-asan` (ASan + UBSan) and `linux-clang-tsan`, same
three commands. CI (`.github/workflows/ci.yml`) runs both Windows presets and both Linux presets
on every push to main and every pull request, adding `-DPROTON_WARNINGS_AS_ERRORS=ON` at configure
time; keep builds warning-free (`/W4 /permissive-`, `-Wall -Wextra -Wpedantic`).

<!-- TODO (M0.15): run the headless demo -->

New test executables: one per module under `tests/unit/<module>/`, registered with `add_test`,
linking `proton::test_support` (the harness in `tests/support`: `TEST`, `CHECK`, `CHECK_EQ`,
`CHECK_NEAR`, provides `main`) and `proton_warnings` privately like every Proton target.

## Working on PLAN.md tasks

- One task per session. Never start the next task.
- A task is done only when its "Done when" line holds, tests pass in CI, and
  ARCHITECTURE.md / ROADMAP.md still match the code.
- On completion, append `*(done YYYY-MM-DD)*` to the task heading in PLAN.md
  (or `*(done locally YYYY-MM-DD, CI pending M0.x)*` if a later CI task is needed).
- Resolve open decisions in the PLAN.md Decisions table with me before starting the task that needs them.
- One branch per task, named like `m1.2-ngspice-engine`. Commit only when Done-when holds.

## Hard rules

- No Qt below `src/app`. Everything else builds and tests without Qt (CI enforces it).
- `instruments` never depends on `sim`; it consumes plain `(time, values)` arrays.
- `schematic` never depends on `instruments`; instrument contributions arrive as `Fragment` data.
- Only `SpscRing<SimChunk>` crosses from the simulation thread to the UI thread. Instruments live
  on the UI thread and have no locks. ngspice's own thread hands points to the engine through a
  queue private to `NgspiceEngine`.
- Determinism: same circuit and settings give the same netlist text and the same frames.
- Instruments use simulation time only, never wall-clock time.
- Component models are generic and original. Never copy a vendor model.
- Proton is AGPL-3.0-only with a commercial dual license. Never add a GPL or AGPL dependency:
  only permissive licenses, or LGPL linked dynamically. Ask before adding any dependency, and
  record it in docs/LICENSING.md.
- Every source file starts with `// SPDX-License-Identifier: AGPL-3.0-only`.

## Product decisions

- Measurements are ideal: no noise, no probe or input loading. Instruments model how they are
  operated (timebase, trigger, coupling, ranges, clipping), not how they degrade.
  Quantisation and bandwidth limits are optional and default to ideal.
- Components are not ideal: op-amps saturate at the rails, diodes have a forward drop.
- Out of scope: noise, probe loading, CPU design from gates.

## How to work here

- Logic goes below the UI, where it is testable. The UI is a thin view.
- New instrument or component: tests first, checked against analytic results (RC corner at
  -3 dB, sine RMS 0.7071), not by eyeballing a plot.
- Schematic changes come with golden netlist tests.
- ARCHITECTURE.md stays high level: layers, dependencies, threads, contracts, invariants and
  why. Update it in the same change only when one of those changes, not after every task. How a
  class works goes in its header comment; measurements and the choices they support go in an
  ADR; what a test checks stays in the test; task history stays in PLAN.md.
- Keep ROADMAP.md's feature states in sync with the code, in the same change.
