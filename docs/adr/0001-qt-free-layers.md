# ADR 0001: Qt-free layers below the UI

- **Status:** accepted
- **Date:** 2026-10-20
- **Plan task:** M0.4

## Context

Proton needs a desktop UI, and Qt 6 Widgets is the chosen toolkit. Qt is convenient everywhere:
`QString`, `QVector`, signals and slots, `QThread`, timers. Using it throughout would be easy at
first, but:

- The value of Proton is in the instrument models, the circuit model and the simulation pipeline.
  They must be tested against analytic answers, quickly and often, and under ASan, UBSan and TSan.
  Sanitizer builds of Qt are slow to set up, and TSan reports false positives in uninstrumented
  Qt code.
- Qt is a large dependency under the LGPL ([ADR 0006](0006-licensing.md)). Keeping it in one
  layer keeps the license boundary small and obvious.
- The same models should serve other front ends: the headless `proton-spike` demo, test
  harnesses, and later a headless grader for lab exercises. None of these want Qt.
- Signals, timers and Qt threads in the models would bring wall-clock time and hidden
  concurrency into code that must be deterministic and run on simulation time only.

## Decision

Only `src/app` may include Qt headers or link Qt libraries. Every other layer (`src/core`,
`src/schematic`, `src/instruments`, `src/sim`, `src/bench` from M3, and `tools/proton-spike`) is
plain C++20 with the standard library only, and builds and tests without Qt installed.

The layers depend on each other as in
[ARCHITECTURE.md section 2](../ARCHITECTURE.md#2-layers-and-dependency-rules): `app` and `tools`
on `bench` (directly on the three libraries until M3), `bench` on `sim`, `instruments` and
`schematic`, and those three on `core` only. In addition, `instruments` never depends on `sim`,
and `schematic` never depends on `instruments`.

CI enforces the Qt rule: the Linux jobs (`linux-clang-asan`, `linux-clang-tsan`) build and test
everything except `src/app` on a runner that has no Qt installed, and fail if Qt is found.

## Alternatives considered

- **Qt everywhere.** Least code at first, but ties the models to Qt, makes sanitizer runs hard,
  and spreads the LGPL boundary across the codebase.
- **Qt Core below the UI, Widgets only in the app.** Still brings Qt's types, event loop and
  threads into the models, and still needs Qt in every test build.

## Consequences

- The models need their own small pieces of infrastructure that Qt would otherwise provide: the
  `SpscRing` that hands simulation chunks to the UI thread, formatting, and the test harness in
  `tests/support`.
- `src/app` converts at the boundary (for example `std::string` to `QString`) and owns all
  timers; it pumps simulation chunks into the instruments on a UI timer.
- Everything below the UI is tested on both Windows and Linux, with sanitizers, without Qt.
- A future UI technology (QML, a web front end) replaces only `src/app`.
