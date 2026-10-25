# ADR 0005: Live value changes: restart or `alter` (decision D3)

- **Status:** accepted
- **Date:** 2026-10-23
- **Plan task:** M1.5, decision D3

## Context

A bench is used while it runs: a student turns the generator's frequency knob and expects the
scope to follow, as on a real bench. Later the same goes for a potentiometer and a switch. With
ngspice (ADR 0002) there are two ways to change a value in a running simulation:

- **Restart:** build the netlist with the new value, halt the run, load the new circuit, start
  again from t = 0 (`SimWorker::start()`).
- **Alter:** halt the run where it is, change the instance parameter with ngspice's `alter`, and
  resume from the same simulation time (`SimWorker::stop()`, `NgspiceEngine::alter()`,
  `SimWorker::resume()`, prototyped in this task).

PLAN.md sets the rule: whichever keeps a change under about 100 ms, and restart if both do,
because it is simpler. Both were measured on a generator frequency change.

### Halting ngspice without `bg_halt`

Both ways halt ngspice first. ngspice 46's `bg_halt` sets its interrupt flag and then sleeps
before it looks at its thread: `Sleep(100)` on Windows (`usleep(10000)` elsewhere), so every
halt took 102 to 115 ms (a scratch probe calling the DLL directly; ADR 0002 measured 114 ms).
That alone broke the budget for both ways. ngspice also refuses every other command while its
thread runs ("cannot execute ..., type bg_halt first"), so a breakpoint cannot be added during a
run.

What works: the engine adds to every netlist a voltage source of its own on a node of its own,
`Vproton_halt proton_halt 0 DC 0 EXTERNAL`, and before `bg_run` the breakpoint
`stop when v(proton_halt) > 0.5`. An EXTERNAL source takes its value from a callback
(`ngSpice_Init_Sync`, `GetVSRCData`); the engine answers 1 V when it wants ngspice to stop.
ngspice then stops at its next accepted point and its thread ends by itself; `bg_halt`, which
only joins the ended thread now, returns at once. Measured with the probe:

| | Time |
|---|---|
| `bg_halt` of a running thread | 102 to 115 ms |
| halt through the halt source, ngspice running freely | 0.04 to 0.06 ms, 2 more points |
| halt through the halt source, ngspice blocked on a full queue | 0.11 to 0.14 ms, 1 more point |
| `bg_halt` after that | 0.2 to 0.9 ms |
| `alter @vgen1[sin] = [ ... ]` and `bg_resume`, to the next point | 0.2 to 6 ms |

The halt source's node touches nothing else in the circuit, so the circuit's points are
unchanged: over 20 009 points of the RC example, time and `v(out)` were bit-identical with and
without it, and the M1.3 validation tests pass unchanged. Its two vectors (`v(proton_halt)`,
`i(vproton_halt)`) are left out of the signals. It costs: the RC example simulated about 20 %
slower in the probe (each accepted point evaluates the breakpoint; the RC example is tiny, so
this is the worst case for a cost per point), and memory per load grew from 0.5 to 0.6 KiB to
0.9 KiB (`ngspice_soak`). `bg_halt` remains the fallback if ngspice does not stop within 1 s.

A second cost was in `SimWorker`: when the ring was full, its thread waited for room with
`sleep_for(100 us)`, which lasts a whole timer tick (about 15.6 ms) on Windows, so `stop()` took
15 ms. It now waits on a condition variable that the stop request wakes: 0.2 to 0.5 ms.

### Measurements

`ngspice_latency` (tests/integration) runs the RC example (1 V sine around 0.5 V into 1 k and
100 n) on a `SimWorker` with its defaults, 1 ms chunks and 64 of them, and the engine's queue of
4096 points. A scope at 200 us/div, Normal, triggered on CH1 rising, is fed every chunk as soon
as it is popped. Latency is wall-clock time from the request to the first scope frame whose CH1
is the new sine everywhere. Each change goes from 1 kHz to 2 kHz or back, 10 times, in two
states: the scope keeping up with the run, and nothing pumped for 400 ms so the ring and the
engine's queue are full. Release build, Windows 11, ngspice 46, after both fixes above; medians,
with maxima in brackets, in ms.

| maxStep | Change | State | `stop()` | change applied | first new frame | old frames first |
|---|---|---|---|---|---|---|
| 1 us | first start (loads the DLL) | | | 6.0 | 17.6 | |
| 1 us | restart | keeping up | 3.4 | 4.0 | 14.5 (17.5) | 0 |
| 1 us | restart | queues full | 0.5 | 3.3 | 19.0 (21.9) | 0 |
| 1 us | alter | keeping up | 5.2 | 5.4 | 28.4 (33.0) | 1 |
| 1 us | alter | queues full | 0.5 | 0.9 | 15.4 (18.8) | 28 |
| 10 us | first start | | | 0.8 | 2.6 | |
| 10 us | restart | keeping up | 0.6 | 1.4 | 3.2 (5.4) | 0 |
| 10 us | restart | queues full | 0.2 | 2.1 | 4.9 (7.4) | 0 |
| 10 us | alter | keeping up | 0.5 | 0.7 | 3.3 (4.2) | 1 |
| 10 us | alter | queues full | 0.2 | 0.7 | 4.2 (9.1) | 43 |

Where the time goes:

- **The frame itself.** A frame needs the pre-trigger half of the screen, a trigger, and the
  post-trigger half: about 2.5 ms of simulation time here. At 1 us steps ngspice simulates the RC
  example at about a third of real time (2.8 ms of wall-clock time per ms), so the frame takes
  about 10 ms; at 10 us, about 1 ms. This is the same for both ways and grows with the circuit.
- **`stop()` while the scope keeps up** waits for the chunk being simulated: up to one
  `chunkDuration` of simulation time (3 to 5 ms at 1 us steps).
- **Loading** (halt, `remcirc`, `destroy all`, `ngSpice_Circ`, `bg_run`, first point): 1 to 4 ms.
  The first load, which loads the DLL, 6 ms.
- **Alter keeps the old points.** Up to a full ring (64 ms of simulation time) and the engine's
  queue (4096 points: 4 ms at 1 us steps, 41 ms at 10 us) were simulated before the change and
  are still on their way: 28 and 43 old frames above. The headless pump plays them through in a
  few ms. Paced to real time (ARCHITECTURE.md section 13, an open UX question), they would be
  shown for as long as they last: up to about 105 ms of old signal after a knob turn. Restart
  drops them.

The app adds up to one UI tick (16 ms, M2.2) and the paint to every figure.

### What else differs

- **State.** Restart begins again from the DC operating point at t = 0: every capacitor and
  inductor starts over, and the scope starts a new acquisition. A real circuit keeps its state
  when a knob turns. For the RC example (tau = 100 us) the start-up is over before the first frame;
  for a slow circuit (tau of seconds) the student would watch it charge again after every knob
  turn. Alter keeps the state; the generator's sine jumps in phase at the change (ngspice's
  `SIN` is a function of absolute time). The phase could be kept continuous through `SIN`'s phase
  argument, as most real generators do.
- **Scope of each.** `alter` changes instance parameters: a generator's frequency, amplitude and
  offset, a resistor's value (a potentiometer, a switch as a resistor). It cannot change a
  source's kind (sine to square is `SIN` to `PULSE`) or the topology (an output impedance of 0
  writes no resistor), so alter still needs restart as a fallback. Restart covers everything with
  one path.
- **Memory.** Every load leaks about 0.9 KiB in ngspice (ADR 0002). Restarting on every step of
  a knob dragged at 30 steps per second for ten minutes is about 16 MB; coalescing changes to one
  per UI tick bounds it. ngspice also reserves its plot for the whole run when the run starts:
  `stopTime / maxStep` points times 8 bytes per vector (probe: 313 MB committed at the start of a
  100 s run at 10 us steps, flat after). Restart starts a new, short run each time; one long run
  that is altered keeps it all.
- **Determinism.** A restart gives the same frames as starting with that netlist, whenever it
  happens. An alter takes effect at whatever simulation time ngspice had reached, which depends
  on thread timing: two sessions with the same knob turns can differ. Both are deterministic for
  a given netlist without live changes.

## Decision

**Restart** (D3, decided 2026-10-23). Both ways keep a generator change far below 100 ms (at most 22 ms to the
first new frame with restart, 33 ms with alter, headless), so the rule picks restart: one code
path for every change, frames that depend only on the netlist, no buffered old signal when the
bench is paced to real time, and memory bounded per run.

With it:

- `NgspiceEngine` halts through the halt source, not `bg_halt`, so loading over a running run
  takes milliseconds.
- `SimWorker`'s wait for room in the ring ends at once on a stop request.
- `SimWorker::resume()` and `NgspiceEngine::alter()` are the prototype of the alter way, tested,
  and available if live changes need it later (a potentiometer turned on a slow circuit).

## Alternatives considered

- **Alter** for parameter changes, restart for the rest: keeps the circuit's state and phase (with
  the phase argument), as a real bench does, at the price of two paths, timing-dependent frames,
  stale signal under real-time pacing, and one long run's plot memory.
- **Halting with `bg_halt`:** 102 to 115 ms per halt on Windows, over the budget for both ways
  before anything else happens.
- **Run segments:** a `stop when time > T` breakpoint set before each run, moved on at every stop.
  Works without a callback, but each segment costs a thread start and command parsing (about 3 ms
  per 1 ms segment in the probe), and a halt waits for the segment to end.
- **The generator as an EXTERNAL source:** its waveform computed by Proton in the source callback,
  so a knob changes it with no halt at all. The fastest, but the generator would no longer be
  plain SPICE (`toSpice()`, golden netlists), ngspice would not know its edges (no breakpoints at
  the corners of a square wave), and it does not help a potentiometer or a switch.

## Consequences

- M2.6 (generator front panel) rebuilds the netlist and calls `SimWorker::start()` on each change,
  coalescing changes to at most one per UI tick, and restarts the scope's acquisition.
- Netlists may not use the names `Vproton_halt` and `proton_halt`; `validate()` should reject a
  designator or net with those names when user-named nets arrive (M4).
- Endless runs are still open: a run ends at `stopTime` (10 s by default) and its plot is
  reserved up front. The bench (M2.2) needs a policy: a long stop time with a larger `.tran`
  step, or a restart when the run ends.
- If a slow circuit's restart start-up is a problem in practice (M2.7), revisit with the alter
  prototype for that case.
