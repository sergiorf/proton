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

Linux (clang + Ninja, Qt-free): `linux-clang-asan` (ASan + UBSan) and `linux-clang-tsan`, same
three commands. CI adds `-DPROTON_WARNINGS_AS_ERRORS=ON` at configure time; keep builds
warning-free (`/W4 /permissive-`, `-Wall -Wextra -Wpedantic`).

<!-- TODO (M0.15): run the headless demo -->

New test executables: one per module under `tests/unit/<module>/`, registered with `add_test`,
linking `proton_warnings` privately like every Proton target.

## Hard rules

- No Qt below `src/app`. Everything else builds and tests without Qt (CI enforces it).
- `instruments` never depends on `sim`; it consumes plain `(time, values)` arrays.
- `schematic` never depends on `instruments`; instrument contributions arrive as `Fragment` data.
- Only `SpscRing<SimChunk>` crosses threads. Instruments live on the UI thread and have no locks.
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
- Keep ARCHITECTURE.md and ROADMAP.md in sync with what the code does. Update them in the
  same change.
