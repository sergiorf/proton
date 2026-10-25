# ADR 0007: Time model: pacing, maximum step, endless runs (decision D6)

- **Status:** accepted
- **Date:** 2026-10-24
- **Plan task:** M1.7, decision D6

## Context

A real bench runs in real time: an LED blinking at 1 Hz blinks once a second, and a capacitor
charging with a time constant of 2 s takes seconds to charge. Proton's simulator runs at whatever
speed it can, and three questions follow (ARCHITECTURE.md section 13, ADR 0005):

- **Pacing.** Is simulation time tied to wall-clock time, and how, given that instruments must
  only ever see simulation time?
- **Maximum step.** ngspice picks its own steps, at most `maxStep`. Too large and the scope draws
  straight lines between far-apart points; too small and the simulation is slow. Who chooses it?
- **End of a run.** A run had a stop time of 10 s, and ngspice reserved memory for the whole run
  when it started. A bench runs for as long as the student works.

All figures below are for the RC example (1 kHz sine, 1 V around 0.5 V, into 1 k and 100 n),
Release build, Windows 11, ngspice 46, measured with a scratch probe calling the DLL and with the
`ngspice_soak` and pipeline tests.

### Memory

ngspice stores every point of every vector in its plot. With a print step (`tstep`) equal to the
maximum step, as the engine wrote `.tran`, it reserves `stopTime / tstep` points of every vector
when the run starts; with a larger print step it grows as the run goes. Either way a run costs
8 bytes per vector per point.

| Run | Memory |
|---|---|
| 1 s at 1 us, `tstep` 1 us, 4 vectors | +30.6 MiB at the start |
| 1 s at 1 us, `tstep` 10 ms | +32 MiB by the end, growing |
| 10 s at 1 us, as the engine ran it (6 vectors with the halt source) | +459 MiB at the start |
| 1 s at 1 us, `.save none` | +0 MiB |
| 10 s at 1 us, `.save none` (10^7 points) | +0 MiB |
| stop time 10^6 s, `.save none`, halted after 2 s | +0.4 MiB, flat |

With `.save none`, ngspice stores nothing, and the data callback still receives every vector at
every point, with the same values. `.options interp` stores only one point per print step (3 MiB
for 1 s at 10 us) and still sends every point. A `.tran` start time (`tstart`) is no help: ngspice
sends no point before it. With `.save none` in the engine, every ngspice test passes unchanged
(halt through the breakpoint, `alter`, fatal-error recovery, validation, latency), and the leak
per load stays about 1 KiB (ADR 0002).

### Speed

Simulated seconds per wall-clock second, as the engine runs ngspice (background thread, halt
source and breakpoint, `.save none`):

| Max step | Scope timebase it serves | Speed |
|---|---|---|
| 1 us | 100 us/div | x0.40 |
| 2 us | 200 us/div (the pipeline's default) | x0.78; x0.36 to x0.56 through the engine, worker and instruments |
| 10 ms | 1 s/div | x47 |

A foreground `run` with the same netlist is about 2.4 times faster than the background run, and
storing the plot made every run about 2.5 times slower again; the engine's own share is about
30 %. Waking the consumer once per chunk instead of once per point saved about 12 % (prototyped,
not kept). So the simulator is slower than real time at fast timebases and far faster at slow
ones. At a fast timebase this is invisible: one screen at 100 us/div is 1 ms of simulation and
takes about 2.5 ms, so frames still come far faster than 60 per second. At a slow one it is the
whole problem: unpaced, a 1 Hz blink would blink 47 times a second.

### Continuing a run from its state

A run can be continued as a new run that starts at t = 0 from the state where the last one ended:
`.ic` of the node voltages followed the single run of an RC step to 3.4e-7 V (and `uic` with
`ic=` on the capacitor to 1.7e-6 V), and a sine continues without a jump through the phase
argument of `SIN`. Not needed for endless runs; kept in mind for decision D7.

## Decision

**Decision D6 (2026-10-24):**

1. **Pacing.** The consumer of a run feeds its instruments only the output up to the simulation
   time that is due: wall-clock time since the start (`sim::RealTimePacer`). Later output waits,
   and a full ring holds the simulator back. Wall-clock time is passed in, so the pacer reads no
   clock; instruments still see simulation time only, and pacing changes when output is fed,
   never what it is. A simulator slower than real time is fed whatever it gives; once it is more
   than 0.1 s behind, what is due slips, so a recovering simulator never races ahead. The pacer
   reports the simulation's speed as a fraction of real time (measured over 0.5 s) for the UI.
2. **Maximum step.** One sample interval of the fastest scope on the bench: time/div / 100, as
   `Scope::sampleInterval()`. Changing it restarts the run, so a timebase change that changes the
   step restarts the circuit, like a generator knob (D3).
3. **Endless runs.** The engine adds `.save none` to every run, so ngspice keeps no plot, and the
   default stop time is `NgspiceEngine::kMaxStopTime` (10^6 s, over eleven days). Runs are not
   cut into windows.

## Alternatives considered

- **Runs as windows** that restart from their last state when they end (the proposed default):
  bounded memory without `.save none`, but a seam every few seconds, and a general "continue from
  state" (every capacitor voltage and inductor current, every source's phase) is a project of its
  own. Not needed once ngspice keeps no plot.
- **A large print step** or `.options interp`: smaller reservation, but memory still grows with
  the run.
- **Pacing in `SimWorker`** (it waits before advancing past wall-clock time): one place for all
  consumers, but the worker would read the clock, and the run would buffer up to a ring of
  chunks ahead anyway. The consumer already decides what it feeds and when; pacing there keeps
  the worker and the engine free of wall-clock time.
- **No pacing**, a "slower than real time" indicator only: simple, but slow circuits play many
  times too fast, which is exactly what a student would notice.
- **A fixed small maximum step** (1 us): correct at every timebase, but x0.4 real time even at
  1 s/div, where the simulator could run 47 times faster than needed.

## Consequences

- The bench (M2.2) takes the pacer, the maximum-step rule and the restart on a step change from
  `tools/proton-spike`, where they live until then (`proton-spike --realtime`, `maxStepFor()`).
- ARCHITECTURE.md section 4: instruments are fed in step with wall-clock time by their consumer;
  a timebase change that changes the step restarts the run.
- Nothing reads ngspice's plot: `NgspiceEngine` must get every value through the data callback.
  A later feature that wants ngspice's own vectors (a `print`, a measurement in ngspice) would
  have to save them explicitly.
- Real time at fast timebases is out of reach for now (x0.4 at 100 us/div); it does not show as
  long as frames come fast enough, and the pacer's speed tells the UI when it does. The engine's
  per-point overhead and ngspice's background mode are where to look if it matters (M2.7).
- A timebase change that restarts the run loses the circuit's state, as a generator change does;
  D7 revisits keeping state.
