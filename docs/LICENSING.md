# Licensing

This file says how Proton is licensed, which licenses its dependencies may have, and what that
means when Proton is packaged. The decision and its reasoning are in
[ADR 0006](adr/0006-licensing.md). This is engineering guidance, not legal advice.

## Proton

Proton is licensed under the [GNU Affero General Public License v3.0 only](../LICENSE)
(AGPL-3.0-only). Every source file says so on its first line:

```
// SPDX-License-Identifier: AGPL-3.0-only
```

(`#` instead of `//` in CMake, PowerShell, shell, Python and YAML files). The `spdx_headers` test
checks it on every build.

The copyright holder also sells a **commercial license** for uses the AGPL does not allow, such as
shipping a closed-source product built on Proton or running a modified Proton as a service without
publishing the changes. Dual licensing only works if the copyright holder may license all of
Proton's code, so outside contributions need a signed Contributor License Agreement (see
[CONTRIBUTING.md](../CONTRIBUTING.md#contributor-license-agreement)).

## The dependency rule

Everything shipped with Proton must be shippable in the commercial edition too. So a dependency
that ends up in a Proton binary or package must have:

- a **permissive** license (MIT, BSD, Apache-2.0, zlib, Boost and similar), or
- the **LGPL**, used through **dynamic linking only**, so the user can replace the library.

**GPL and AGPL dependencies are never allowed**, whatever the version and however they are linked,
because the commercial edition could not ship them. Ask before adding any dependency, and add it
to the table below in the same change. Build tools and CI services that do not end up in what is
shipped are listed separately; their licenses do not reach Proton's binaries.

## Dependencies

Shipped with Proton:

| Dependency | Used for | License | How it is used | Status |
|---|---|---|---|---|
| Qt 6 (Core, Gui, Widgets) | UI in `src/app` | LGPL-3.0-only (open-source option) | Dynamically linked DLLs, deployed with `windeployqt` | planned, M2.1 |
| ngspice | Circuit simulation in `src/sim` | Modified BSD (BSD-3-Clause) for most of the code; see below | Shared library loaded at runtime (`LoadLibrary`) | planned, M1.1 |
| Microsoft C++ runtime | Runtime of MSVC-built binaries | Visual Studio redistributable terms | System library DLLs | planned, M5.5 |

Build, test and CI only (not shipped):

| Tool | Used for | License |
|---|---|---|
| CMake | Build system, `tools/check-spdx.cmake` | BSD-3-Clause |
| MSVC (Visual Studio 2022 or newer) | Windows compiler | Proprietary (Microsoft) |
| clang / LLVM, including the sanitizer runtimes | Linux CI compiler, ASan, UBSan, TSan | Apache-2.0 WITH LLVM-exception |
| Ninja | Linux CI build tool | Apache-2.0 |
| GitHub Actions: `actions/checkout` | CI | MIT |
| GitHub Actions: `jurplel/install-qt-action` | Installing Qt in CI | MIT (planned, M2.1) |

Proton's test harness (`tests/support`) is Proton's own code, so the tests have no third-party
dependency.

## Qt

Proton uses Qt under the LGPL-3.0. That is compatible with both of Proton's licenses as long as:

- Qt is **linked dynamically** (the official Qt DLLs, never a static Qt build);
- Qt itself is not modified, or any modification of Qt is published under the LGPL;
- the package ships the LGPL-3.0 text and Qt's third-party notices, says which Qt version is used
  and where its source code can be obtained, and lets the user replace the Qt DLLs with their own
  build of the same version.

Only Qt modules available under the LGPL may be used. Some modules are **GPL-only** in the
open-source edition (for example Qt Charts, Qt Data Visualization, Qt Graphs and Qt Virtual
Keyboard) and are therefore ruled out; this is one reason the scope draws with `QPainter` instead
of Qt Charts (ADR 0004, M2.3). Check a module's license on qt.io before using it.

## ngspice

Most of ngspice is under the modified BSD license (BSD-3-Clause), which is permissive. Some parts
of its source tree come from other projects under other licenses, listed in the `COPYING` file of
each release. Proton loads the official shared library at runtime and does not modify it, which
also keeps the boundary between Proton and ngspice clear. When the release is pinned (M1.1),
ADR 0002 records its version and the review of its `COPYING` file; if any part of that build is
not permissive or LGPL, it must not be shipped. The package ships ngspice's license texts.

## Component models

Every component model in Proton (diodes, LEDs, Zeners, transistors, the op-amp and anything
later) is **generic and original**: parameters chosen to give typical, textbook behaviour, never
copied from a manufacturer's model or datasheet model file. Vendor SPICE models often carry
licenses that forbid redistribution or modification, and the models are generic on purpose so a
student learns the behaviour rather than one part number. When a model is added (M5), its
parameters are listed here as original.

## Packaging checklist

A Proton package (M5.5) contains:

- the AGPL-3.0 text and a note on the commercial license;
- the license text of every shipped dependency in the table above;
- for Qt: the LGPL-3.0 text, the Qt version and where its source can be obtained, and the Qt DLLs
  as separate, replaceable files.
