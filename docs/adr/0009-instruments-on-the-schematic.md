# ADR 0009: Instruments as parts on the schematic (decision D10)

- **Status:** accepted
- **Date:** 2026-10-25; amended 2026-10-26 (the parts panel in point 1, panels in windows of their
  own in point 4)
- **Plan task:** M3.1, M4.1 to M4.10, decision D10

## Context

Proton is a bench: a student connects instruments to a circuit and operates them as on a real
bench (ROADMAP.md), and the instruments look like generic bench instruments (M2.4). How an
instrument is connected is part of what the student learns: which net a probe is on, where the
generator's common goes, that an ammeter breaks the circuit.

The plan had instruments as dock panels outside the schematic, connected by picking a net (M3.6)
or clicking one with a probe tool (M4.4); only the ammeter was a part. Connections were invisible
on the sheet.

How other simulators do it:

| Tool | Where an instrument lives | How it connects |
|---|---|---|
| NI Multisim | A symbol on the schematic; double-clicking it opens its front panel in a window | Ordinary schematic wires to its terminals |
| Labcenter Proteus | A symbol on the schematic, its panel pops up while simulating | Wires to its pins |
| Tinkercad Circuits | A realistic part on the breadboard | Probe leads dragged to holes or pins |
| LTspice, Falstad, EveryCircuit | No instruments, one waveform viewer | Clicking a node plots it |

## Decision

**Decision D10 (2026-10-25):** instruments are parts on the schematic, as in Multisim.

1. Every instrument instance is a part on the sheet: an original, brand-neutral instrument symbol
   with terminals, placed from the parts list like a component, with a unique instance name
   (`scope1`, `dmm1`, `gen1`, `psu1`). The parts list is a panel on the left of the sheet, with
   the components and the instruments in groups.
2. Terminals, and what they measure or drive:
   - **Scope:** `CH1` and `CH2`, each measuring its net against circuit ground (single-ended, as a
     bench scope whose ground clip is earthed). Differential probes come later.
   - **Multimeter:** `+` and `COM`. Voltage is `v(+) - v(COM)`, so the meter floats as a handheld
     one does; current flows through it in series from `+` to `COM` (M5.2); resistance is measured
     between them (M5.3).
   - **Function generator:** `+` and `COM`; the output appears between them, and `COM` may be on
     any net.
   - **DC power supply:** `+` and `-` per channel.
3. Terminals are wired with ordinary schematic wires. The scene-to-`Circuit` conversion gives each
   instrument terminal its net exactly as it does a component pin, and the bench's connections are
   those net names: the bench model keeps taking connections as nets.
4. Double-clicking an instrument opens its front panel (the M2.4 widgets) in a window floating
   over the sheet, as Multisim's instrument windows do, so a student can keep several beside the
   circuit. It is a window of its own, not a dock: dragged over the main window, a dock snaps into
   it, which gets in the way of a panel kept beside the circuit. There is one panel per instance,
   and deleting the symbol removes the instrument and its panel.
5. An instrument terminal with no wire is a warning on the instrument ("CH2 of scope1 is not
   connected") and reads 0 V.
6. Until the editor exists (M4), the built-in examples place their instruments in code with the
   same instances and terminals.

Instruments are not components of `Circuit`: the scene holds them as parts, the conversion reports
their terminal nets, and source instruments still reach the netlist as `Fragment`s from the bench
(ARCHITECTURE.md sections 5.1 and 7).

## Alternatives considered

- **Dock panels with click-to-probe** (the previous plan, LTspice's way): simplest editor, but the
  connections are nowhere on the sheet, a student cannot see or check them, and the ground and
  common of an instrument have no place to go.
- **Free leads dragged to points** (Tinkercad): closest to clipping a probe onto a real circuit,
  but it needs leads drawn over the schematic outside its wire model, and on a schematic, unlike a
  breadboard, a wire already says the same thing. A lead drawing style can come later on top of
  this model.

## Consequences

- M3.1 generalises the bench to named instances with terminals; the multimeter measures between
  `+` and `COM` instead of one net against ground, and the generator's reference is its `COM` net.
- M4.1 adds instrument parts to the scene model, M4.2 their symbols, and M4.4 placing, wiring and
  opening them. The editor comes right after M3.1, so no interim way to add instruments or show
  their connections is built first.
- `schematic` still does not depend on `instruments`: in the scene an instrument part is plain
  data (kind, instance name, terminal names and positions), and its settings and model live in the
  bench.
- `validate()` (or the scene conversion) reports unconnected instrument terminals. Instance names
  share one name space with designators and fragments.
- Project files (M4.10) store instruments as parts of the scene, with their settings.
- Measuring current means wiring the meter in series, which on a schematic is breaking a wire:
  the lesson a real bench teaches.
