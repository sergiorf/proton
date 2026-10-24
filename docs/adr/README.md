# Architecture decision records

Each ADR records one significant decision: the problem, what was decided, and what follows from
it. ADRs are not rewritten when a decision changes; a new ADR supersedes the old one, and the old
one's status says so. Start a new ADR from [0000-template.md](0000-template.md), with the next
free number.

| ADR | Title | Status |
|---|---|---|
| [0001](0001-qt-free-layers.md) | Qt-free layers below the UI | accepted |
| [0002](0002-simulation-backend.md) | Simulation backend: ngspice as a runtime-loaded shared library | accepted |
| [0003](0003-build-and-dependencies.md) | Build, toolchain and dependencies | accepted |
| 0004 | Scope rendering with `QPainter`, no Qt Charts | planned, M2.3 |
| [0005](0005-live-changes.md) | Live value changes: restart or `alter` (decision D3) | accepted |
| [0006](0006-licensing.md) | Licensing: AGPL-3.0-only plus a commercial license | accepted |
| [0007](0007-time-model.md) | Time model: pacing, maximum step, endless runs (decision D6) | accepted |
| [0008](0008-ngspice-on-linux-ci.md) | ngspice on Linux for the sanitizer CI jobs (decision D9) | accepted |

Numbers are reserved in the order of [PLAN.md](../PLAN.md), so some are written before others.
