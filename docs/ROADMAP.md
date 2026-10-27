# Proton roadmap

This file tracks what Proton supports and in what order. How the pieces fit is in
[ARCHITECTURE.md](ARCHITECTURE.md); the MVP's milestones (M0 to M5) and tasks are in
[PLAN.md](PLAN.md).

## MVP

**Goal:** a student can build the circuits of a typical first-year electronics lab, power and drive
them from bench instruments, and measure them the way they would on a real bench. Measurements are
ideal (no noise, no probe loading, see ARCHITECTURE.md section 1); components are not: an op-amp
saturates at its rails and a diode drops about 0.6 V, otherwise circuits teach the wrong thing.

### Instruments

The instruments look like generic bench instruments, because recognising a real bench is part of
what a student learns: a front panel with a screen, labelled sections, rotary knobs that step
like real ones (1-2-5 for time/div and V/div) and lit buttons. The styling is original and
brand-neutral: no manufacturer's layout, logo or trade dress (PLAN.md, M2.4). Instruments sit on
the schematic as parts, wired to the circuit through their terminals, and double-clicking one opens
its front panel (ADR 0009).

| Instrument | MVP scope | State |
|---|---|---|
| DC power supply | 2 channels (enough for +/- rails), set voltage, current limit as a plain value | planned (M5.1) |
| Function generator | sine, square, triangle, DC, offset, output impedance, output on/off | model done (M0), front panel done (M2.6) |
| Function generator | frequency, amplitude and offset set digit by digit; unit lists on the entry fields | done (M4.6) |
| Oscilloscope | 2 channels, timebase, V/div, DC/AC coupling, edge trigger, Auto / Normal / Single | model done (M0), screen and front panel done (M2.4) |
| Oscilloscope | channel on/off, horizontal position, GND coupling, force trigger, fine V/div; a bench scope's panel layout | done (M4.5) |
| All instruments | knobs with coarse and fine, circular drag, scale marks, hover feedback | done (M4.5) |
| Multimeter | DC V, AC V (true RMS), auto/fixed range | model done (M0), front panel done (M2.5) |
| Multimeter | DC current | planned (M5.2) |
| Multimeter | resistance | planned (M5.3) |

How the multimeter measures what it cannot read directly from a node voltage:

- **Current:** the meter is inserted in series and contributes a 0 V source to the netlist (the
  same `Fragment` mechanism as the generator); the reading is that source's current. As on a real
  bench, measuring current means breaking the circuit.
- **Resistance:** with the circuit's sources zeroed, the meter injects a test current between its
  two nets and reads the voltage (an operating-point run).

### Circuit elements

All models are generic and original, never vendor models (see `docs/LICENSING.md`).

| Group | Elements |
|---|---|
| Passives | resistor, capacitor, inductor, potentiometer |
| Sources inside the circuit | ground, DC voltage source (battery), DC current source |
| Semiconductors | diode, LED (forward drop by colour), Zener, NPN and PNP BJT, N- and P-channel MOSFET |
| Op-amp | one generic model: finite gain, gain-bandwidth product, rail saturation; optional "ideal" mode |
| Switches | SPST switch (voltage-controlled switch in SPICE) |

The potentiometer and the switch are the elements a student changes while the circuit runs. Unlike
the generator's knobs, which restart the run with the new netlist (decision D3, ADR 0005), they
must keep the circuit's state, so a switched RC charges from where it was (decision D7).

### Done when

Each of these circuits can be built, powered and measured end to end, with results matching the
analytic answer:

- voltage divider and Ohm's-law checks (V, I, R on the multimeter)
- RC charging and discharging
- RC, RL and RLC filters
- half- and full-wave rectifier with smoothing capacitor
- Zener regulator
- transistor switch driving an LED
- common-emitter amplifier
- inverting and non-inverting op-amp amplifiers, integrator, Schmitt trigger

## After the MVP

- **Bode plotter:** frequency response from an ngspice AC analysis in one sweep. Cheap to build and
  the most useful next instrument for design work.
- **Spectrum analyzer:** the spectrum of a node, from an FFT of the transient run, sampled exactly
  as the scope samples. A front panel like a bench analyser: centre and span (or start and stop),
  resolution bandwidth, reference level, dB per division and window. Tested against analytic
  answers: a sine is one line at its frequency and level, a square's odd harmonics fall as 1/n.
- **Function generator:** pulse with duty cycle.
- **Oscilloscope:** cursors, measurements, averaging, FFT/math; holdoff; delayed timebase; non-edge
  triggers; sin(x)/x display interpolation; 4 channels. (Optional quantisation and bandwidth
  limit are already in the model, off by default.)
- **DC power supply:** CC/CV behaviour.
- **Multimeter:** diode test, continuity.
- **Components:** transformer / coupled inductors, comparator, voltage regulator.
- **Projects:** a folder of several sheets with a project browser beside the sheet, as in desktop
  circuit simulators; a sheet used in another as a subcircuit, a block with its own pins. Each
  sheet keeps the project file format of PLAN.md M4.11, so the MVP's single-file projects open in
  it.

## Long-term directions

The direction is **embedded, mixed-signal**: a microcontroller running the student's own code, wired
to an analog circuit and measured with the bench instruments. Existing tools are strong on one
side only (Wokwi and Tinkercad on the MCU, Multisim on analog); a real analog bench with real
firmware is the gap Proton fills. The steps below are in order; each builds on the previous one.

### 1. Educator layer

What gets Proton adopted by a course rather than by one student, and cheap because `Circuit` is
plain data and instruments are Qt-free.

- Lab exercises: a circuit, instructions and the expected measurements.
- Automatic grading by a headless grader running the same instrument models.
- Troubleshooting mode: hidden faults applied to the circuit (ARCHITECTURE.md section 12).

### 2. Digital and logic analyzer

The stepping stone to microcontrollers, and useful on its own.

- Gates, flip-flops, counters, 555 timer, using ngspice mixed-signal (XSPICE) support.
- Logic analyzer instrument: digital probes, timing view, protocol decoding (UART, SPI, I2C) later.

### 3. Microcontroller co-simulation

The differentiator: PWM on the scope, the firmware's ADC reading checked against the multimeter,
timing bugs debugged against real signals.

- An MCU element running student firmware on an emulator, its pins coupled to the circuit.
- Architecture change: today the simulator streams results one way. An MCU closes the loop
  (firmware reads a pin, decides, drives a pin while SPICE is running), so it needs co-simulation
  that keeps the emulator and SPICE in step. ngspice's shared library supports caller-driven
  external sources (`GetVSRCData`), so this is feasible without a new engine.
- Licensing: the emulator must have a permissive license, so the commercial edition can ship it
  (see `docs/LICENSING.md`). simavr and QEMU are GPL and therefore ruled out; Renode (MIT) or an
  emulator of our own qualify.

### 4. RF, lumped circuits only

- VNA on lumped circuits: AC small-signal sweep turned into S-parameters, Smith chart. A natural
  extension of the Bode plotter.
- Spectrum analyzer at RF: the FFT analyser from after the MVP (above), over higher frequency
  ranges.
- Distributed RF (transmission lines, microstrip, harmonic balance) needs different solvers and is
  well served by existing tools (Qucs, ADS). Only on clear demand.

### DSP

Not a separate step. Frequency-domain views come with the scope FFT and the spectrum analyzer;
digital signal processing comes with step 3, written as firmware and observed on the analog bench.
A standalone DSP tool would compete with MATLAB, Python and GNU Radio for no gain.

## Out of scope

- Noise and probe or input impedance loading. The bench is an ideal reference: a reading measures
  the circuit, not the measurement setup.
- CPU design from gates. Logisim, Digital and Nand2Tetris cover it, and it does not use Proton's
  analog strength. Running code on a CPU is step 3 above.
