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
| Qt 6 (Core, Gui, Widgets; Svg and Network, which `windeployqt` adds for plugins) | UI in `src/app` | LGPL-3.0-only (open-source option) | Dynamically linked DLLs, deployed with `windeployqt` | Qt 6.8 or newer, from M2.1 |
| ngspice 46 | Circuit simulation in `src/sim` | Modified BSD (BSD-3-Clause) for most of the code; see below | Shared library loaded at runtime (`LoadLibrary`) | fetched by `tools/fetch-ngspice.ps1` (M1.1) |
| LLVM OpenMP runtime (`libomp140.x86_64.dll`) | Required by `ngspice.dll` | Visual Studio redistributable terms (LLVM-based) | DLL shipped next to `ngspice.dll`, from the same ngspice archive | fetched with ngspice (M1.1) |
| Microsoft C++ runtime | Runtime of MSVC-built binaries | Visual Studio redistributable terms | System library DLLs | planned, M6.5 |

Build, test and CI only (not shipped):

| Tool | Used for | License |
|---|---|---|
| CMake | Build system, `tools/check-spdx.cmake` | BSD-3-Clause |
| MSVC (Visual Studio 2022 or newer) | Windows compiler | Proprietary (Microsoft) |
| clang / LLVM, including the sanitizer runtimes | Linux CI compiler, ASan, UBSan, TSan | Apache-2.0 WITH LLVM-exception |
| Ninja | Linux CI build tool | Apache-2.0 |
| GitHub Actions: `actions/checkout` | CI | MIT |
| GitHub Actions: `actions/cache` | Caching ngspice in CI | MIT |
| ngspice 46, built from source on Linux (`libngspice.so`) | Running `NgspiceEngine` under the sanitizers in the Linux CI jobs; built by `tools/fetch-ngspice.sh` (ADR 0008) | As the shipped ngspice 46 above, the same release and license review |
| GNU OpenMP runtime (`libgomp`), GCC | Building that library, and its OpenMP runtime | GPL-3.0 with the GCC Runtime Library Exception (runtime); GPL-3.0 (compiler), CI only |
| GitHub Actions: `jurplel/install-qt-action` | Installing Qt in CI | MIT |

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
also keeps the boundary between Proton and ngspice clear.

[ADR 0002](adr/0002-simulation-backend.md) pins ngspice 46 and records the review of its `COPYING`
file. Compiled into `ngspice.dll` besides the BSD code: MIT-style and public-domain parts, KLU and
numparam (LGPL, fine through dynamic loading), and OSDI (MPL-2.0, file-level copyleft, accepted
for the same unmodified, dynamically loaded library). The XSPICE code model `table.cm` is GPL: it
is a separate file, and `tools/fetch-ngspice.ps1` keeps no code models at all, so it is never
shipped. Only `ngspice.dll` and the `libomp140.x86_64.dll` it needs are shipped. The package ships
ngspice's `COPYING` and says where the ngspice 46 source can be obtained.

On Linux, `tools/fetch-ngspice.sh` builds `libngspice.so` from the same release's source, only for
the sanitizer CI jobs ([ADR 0008](adr/0008-ngspice-on-linux-ci.md)). It is never shipped, and the
script keeps no code models either.

## Component models

Every component model in Proton (diodes, LEDs, Zeners, transistors, the op-amp and anything
later) is **generic and original**: parameters chosen to give typical, textbook behaviour, never
copied from a manufacturer's model or datasheet model file. Vendor SPICE models often carry
licenses that forbid redistribution or modification, and the models are generic on purpose so a
student learns the behaviour rather than one part number. When a model is added (M6), its
parameters are listed here as original.

## Artwork

The app icon (`src/app/resources/proton.png` and `proton.ico`) was generated with Claude for the
copyright holder, who holds the rights to it under Anthropic's terms and ships it under Proton's
licenses. It is not a third-party asset. Purely AI-generated images may not be protected by
copyright everywhere; that limits what Proton can stop others doing with the icon, not what Proton
may do with it.

## Packaging checklist

A Proton package (M6.5) contains:

- the AGPL-3.0 text and a note on the commercial license;
- the license text of every shipped dependency in the table above;
- for ngspice: its `COPYING` file and where its source can be obtained;
- for Qt: the LGPL-3.0 text, the Qt version and where its source can be obtained, and the Qt DLLs
  as separate, replaceable files.
