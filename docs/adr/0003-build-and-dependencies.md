# ADR 0003: Build, toolchain and dependencies

- **Status:** accepted
- **Date:** 2026-10-20
- **Plan task:** M0.4 (baseline from M0.1 to M0.3)

## Context

Proton is built by one developer, targets Windows first, and must be easy to build from a fresh
clone. The threading code (simulation worker, ring buffer) is the riskiest code, so it needs
sanitizers, including TSan. Every dependency must satisfy the license rule of
[ADR 0006](0006-licensing.md) and is recorded in [LICENSING.md](../LICENSING.md).

## Decision

| Area | Choice | Why |
|---|---|---|
| Language | C++20 | `std::span`, concepts, designated initialisers for `Config` structs |
| Compiler | MSVC (Visual Studio 2022 or newer); clang on Linux CI for sanitizers | Windows target; MSVC has ASan but no TSan or UBSan |
| Build | CMake (3.25 or newer) with `CMakePresets.json` | Standard, works in VS Code and Visual Studio |
| Qt | Current Qt 6 LTS, Widgets only; official installer locally, `install-qt-action` in CI | Building Qt from vcpkg takes hours; nothing else needs a package manager |
| Test framework | Own minimal harness in `tests/support` | Zero dependencies (ARCHITECTURE.md section 10) |
| ngspice | Pinned official Windows shared-library release, fetched by script with SHA-256 check, loaded at runtime (`LoadLibrary`) | App starts without it (mock fallback), can reload after a fatal ngspice error, clear license boundary; details in ADR 0002 (M1.1) |
| CI | GitHub Actions: Windows MSVC jobs (Debug, Release) and Linux clang jobs (ASan + UBSan, TSan) without Qt | The Linux jobs have no Qt installed, so they also enforce [ADR 0001](0001-qt-free-layers.md) |

Build rules that follow from it:

- Presets `windows-msvc-debug`, `windows-msvc-release`, `linux-clang-asan` and
  `linux-clang-tsan`; build trees go to `build/<preset>`.
- Warnings `/W4 /permissive-` (MSVC) and `-Wall -Wextra -Wpedantic` (clang), carried by the
  `proton_warnings` interface target that every Proton target links privately, so third-party
  code is not affected. CI configures with `-DPROTON_WARNINGS_AS_ERRORS=ON`.
- Sanitizers are chosen with `PROTON_SANITIZE` and apply to the whole build, tests included.
- No package manager. Qt comes from the official installer; ngspice from a fetch script into
  `third_party/ngspice` (not committed). Any new dependency needs approval and an entry in
  [LICENSING.md](../LICENSING.md).
- Repository checks run as CTest tests on every preset, for example `spdx_headers`.

## Alternatives considered

- **vcpkg or Conan for everything.** Reproducible, but building Qt takes hours and nothing else
  needs a package manager.
- **A third-party test framework (GoogleTest, Catch2, doctest).** All permissively licensed, but
  the harness Proton needs is small, and a dependency-free one keeps the Linux and sanitizer
  builds trivial.
- **ngspice built from source and linked statically.** Couples Proton's build and process to
  ngspice: a fatal ngspice error would end the app, and the license boundary is less clear.
- **MSVC only.** No TSan and no UBSan; the threading code would go untested under the tools that
  matter most for it.

## Consequences

- A fresh clone builds and tests with only CMake and a compiler until Qt arrives in M2.1.
- Two compilers keep the code portable and catch more warnings, at the cost of a slower CI.
- `src/app` is built only on Windows CI (from M2.1), the Linux jobs never see Qt.
- The ngspice pin, fetch script and runtime loading are specified in ADR 0002.
