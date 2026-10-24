# Contributing to Proton

Thanks for helping build Proton. This file covers the rules every change follows. The design
behind them is in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), the order of work in
[docs/PLAN.md](docs/PLAN.md), and the reasoning for big decisions in [docs/adr/](docs/adr/).

## Contributor License Agreement

Proton is dual licensed: AGPL-3.0-only for everyone, and a commercial license sold by the
copyright holder ([docs/LICENSING.md](docs/LICENSING.md), [ADR 0006](docs/adr/0006-licensing.md)).
Offering the commercial license is only possible if the copyright holder may license every line
of Proton that way. So **outside contributions need a signed Contributor License Agreement (CLA)
before they can be merged**. The CLA grants the copyright holder the right to license your
contribution under both licenses; you keep the copyright in your work. The maintainer sends the
agreement on your first pull request. Pull requests without a CLA are not merged.

## Layer rules

Proton is split into layers ([ARCHITECTURE.md section 2](docs/ARCHITECTURE.md#2-layers-and-dependency-rules),
[ADR 0001](docs/adr/0001-qt-free-layers.md)):

```
src/app (Qt)  ->  src/bench (M2.2)  ->  src/sim, src/instruments, src/schematic  ->  src/core
```

- **No Qt below `src/app`.** Every other library builds and tests without Qt. CI enforces it: the
  Linux job has no Qt installed.
- **`instruments` never depends on `sim`.** Instruments consume plain `(time, values)` arrays.
- **`schematic` never depends on `instruments`.** Instrument contributions to the netlist arrive as
  plain `Fragment` data.
- **Only `SpscRing<SimChunk>` crosses from the simulation thread to the UI thread.** Instruments
  live on the UI thread and have no locks. ngspice's own thread hands points to the engine through
  a queue private to `NgspiceEngine`.
- **Determinism.** The same circuit and settings give the same netlist text and the same frames.
- **Simulation time only.** Instruments never read the wall clock.
- Logic goes below the UI, where it is testable. The UI is a thin view.

A change that needs to break one of these rules needs an ADR first.

## Licensing rules

- Every source file starts with the SPDX line, `// SPDX-License-Identifier: AGPL-3.0-only` in
  C++ and `# SPDX-License-Identifier: AGPL-3.0-only` in CMake, PowerShell, shell, Python and
  YAML. The `spdx_headers` test (`tools/check-spdx.cmake`) fails on any file without it.
- Never add a GPL or AGPL dependency: only permissive licenses, or LGPL linked dynamically. Ask
  before adding any dependency, and record it in the table in
  [docs/LICENSING.md](docs/LICENSING.md) in the same change.
- Component models are generic and original. Never copy a vendor model or a vendor's parameter
  set.

## Style

- C++20. Format with the repository's `.clang-format` (LLVM based, 4-space indent, 100 columns);
  `.editorconfig` covers the other files (LF line endings, final newline).
- Names: types `PascalCase`, functions and variables `camelCase`, private members with a trailing
  underscore (`failures_`), test names `snake_case`. Code lives in `proton::<module>` and public
  headers in `src/<module>/include/proton/<module>/`.
- Keep builds warning-free: `/W4 /permissive-` on MSVC, `-Wall -Wextra -Wpedantic` on clang. CI
  builds with warnings as errors. Every target links `proton_warnings` privately.
- Comments say why, not what. Write them in plain English.

## Tests

- Build and test with the CMake presets (see the "Build and test" section of
  [CLAUDE.md](CLAUDE.md)): `windows-msvc-debug` and `windows-msvc-release` on Windows,
  `linux-clang-asan` and `linux-clang-tsan` on Linux. Run `tools/fetch-ngspice.ps1` on Windows
  (ADR 0002) or `bash tools/fetch-ngspice.sh` on Linux (ADR 0008) once before configuring to build
  the ngspice tests. The Windows presets build the Qt app too: set `QT_ROOT_DIR` to a Qt 6.8 or newer
  MSVC kit (see CLAUDE.md).
- The app has a smoke test only (`app_starts`: it opens the main window and closes it). Logic
  goes below `src/app`, where it is tested without Qt.
- One test executable per module under `tests/unit/<module>/`, registered with `add_test`, linking
  `proton::test_support` (`TEST`, `CHECK`, `CHECK_EQ`, `CHECK_NEAR`) and `proton_warnings`
  privately.
- New instruments and components: tests first, checked against analytic results (RC corner at
  -3 dB, sine RMS 0.7071), not by eyeballing a plot.
- Schematic changes come with golden netlist tests.
- Threading code must pass under TSan (`linux-clang-tsan`).

## Pull requests

- Work through [docs/PLAN.md](docs/PLAN.md) tasks in order; a task is done when its "Done when"
  holds and CI is green. Mark it done in the plan in the same change, or right after CI passes.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) stays high level (layers, dependencies, threads,
  contracts, invariants and why): update it in the same change only when one of those changes.
  Details of a class go in its header comment, measurements in an ADR. Keep
  [docs/ROADMAP.md](docs/ROADMAP.md)'s feature states in sync with the code.
- Record significant decisions as an ADR in [docs/adr/](docs/adr/), starting from the template.
- CI ([.github/workflows/ci.yml](.github/workflows/ci.yml)) runs every Windows and Linux preset on
  each pull request; all jobs must pass.
