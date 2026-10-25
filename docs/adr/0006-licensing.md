# ADR 0006: Licensing: AGPL-3.0-only plus a commercial license

- **Status:** accepted
- **Date:** 2026-10-20
- **Plan task:** M0.4, decision D2

## Context

Proton should be free software that students and schools can use, study and change. Its author
also wants to be able to sell it to companies that cannot accept a copyleft license, for example
to embed it in a closed product or offer it as a hosted service. Proton depends on Qt (LGPL or
commercial) and ngspice (mostly BSD-3-Clause), and will add more dependencies over time.

## Decision

- Proton is licensed under **AGPL-3.0-only**. The AGPL rather than the GPL, so a modified Proton
  offered over a network must also publish its source. "Only", so the terms cannot change through
  a later license version without a new decision.
- The copyright holder offers a separate **commercial license** (dual licensing).
- **Outside contributions require a signed Contributor License Agreement** that lets the
  copyright holder license them under both licenses.
- **Dependency rule:** every dependency shipped with Proton must have a permissive license, or
  the LGPL with dynamic linking. GPL and AGPL dependencies are never allowed. Every dependency
  is approved before it is added and recorded in [LICENSING.md](../LICENSING.md).
- **Original models only:** component models are generic and written for Proton, never copied
  from a vendor.
- Every source file starts with `SPDX-License-Identifier: AGPL-3.0-only`, checked by the
  `spdx_headers` test.

## Alternatives considered

- **GPL-3.0.** Copyleft, but a modified Proton could be run as a hosted service without sharing
  its source, which weakens the commercial license.
- **A permissive license (MIT, Apache-2.0).** Simplest for contributors, but leaves nothing to
  sell and lets closed forks ship without giving back.
- **Accepting GPL dependencies.** Would be fine for the AGPL edition, but the commercial edition
  could not ship them; one such dependency would end dual licensing.

## Consequences

- Qt is used only under the LGPL with dynamic linking, and only modules available under the LGPL
  (no Qt Charts); see [LICENSING.md](../LICENSING.md#qt).
- ngspice is loaded at runtime as an unmodified official library; the pinned release's license
  review goes into ADR 0002.
- Some useful libraries are ruled out. The roadmap already notes this for microcontroller
  emulation: simavr and QEMU are GPL, so a permissively licensed emulator is needed.
- Contributions take an extra step (the CLA), which may discourage some contributors.
- Packages must ship the license texts of every shipped dependency (M6.5).
