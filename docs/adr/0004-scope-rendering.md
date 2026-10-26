# ADR 0004: Scope rendering with `QPainter`, no Qt Charts

- **Status:** accepted
- **Date:** 2026-10-25
- **Plan task:** M2.3

## Context

The oscilloscope's screen shows a graticule of 10 x 8 divisions, two traces of 1000 samples each
(`Scope::kSamplesPerScreen`), and markers for the trigger level, the trigger position and each
channel's ground. A new frame can arrive on every 16 ms UI tick, so drawing has to fit in a
fraction of a 60 Hz frame on a student's laptop, next to the simulation threads.

What is available:

- **Qt Charts, Qt Graphs:** GPL-only in Qt's open-source edition, so Proton cannot use them under
  its dual license ([LICENSING.md](../LICENSING.md#qt), ADR 0006).
- **Third-party plot widgets:** QCustomPlot is GPL; Qwt is LGPL with exceptions. Either would be a
  new dependency for what a scope needs, which is not a general chart: fixed divisions instead of
  axes, no zoom or legend, its own markers.
- **`QPainter` on a `QWidget`:** in Qt Widgets, which Proton already uses; software-rasterised.
- **`QOpenGLWidget` or `QRhi`:** GPU drawing, for many more points or persistence effects, at the
  price of a second drawing path and driver differences between student machines.

### Measurements

`ScopeWidget` (`QPainter`, antialiasing on the markers only) times every paint; `proton
--smoke-examples` prints the mean and the longest. Release build, Windows 11, ngspice 46, the
window at its default 1280 x 800 with the scope docked on top, so the screen is narrower than 1000
pixels and traces are drawn as min/max columns, one per pixel.

| Example | Pumps per second | Frames shown per second | Paint, mean | Paint, longest |
|---|---|---|---|---|
| RC low-pass, 200 us/div | 52 to 62 | 30 to 55 | 0.96 to 1.49 ms | 1.83 to 2.78 ms |
| Voltage divider, 200 us/div, Auto | 62 | 25 to 39 | 1.05 to 1.49 ms | 1.50 to 2.63 ms |
| RC high-pass, 200 us/div | 62 | 45 to 58 | 1.04 to 1.29 ms | 2.06 to 2.78 ms |

A paint costs about a sixteenth of a 60 Hz frame. Fewer frames are shown than pumps run because
the simulator does not produce a new frame for every tick: at 200 us/div (a 2 us step, ADR 0007)
ngspice ran at 0.21 to 0.27 x real time in the app and 0.17 x in the headless paced test on the
same machine that day, so a tick of 16 ms brings 3 to 4 ms of simulation, about one screen's worth
with its pre-trigger half. The voltage divider shows fewest: its flat traces never trigger, and in
Auto mode each frame waits two screens for the timeout. A first coarse `QTimer` of 16 ms fired
only about 30 times a second on Windows, on every other 15.6 ms system tick; the pump uses a
precise timer.

## Decision

- The scope's screen is a `QWidget` drawn with `QPainter` (`src/app`, `ScopeWidget`). No chart
  library.
- The geometry is Qt-free and tested in `src/instruments` (`scope_display.h`): volts and times to
  divisions, the trigger level's height, and **min/max decimation**. When the screen has fewer
  pixel columns than samples, each column is drawn as a vertical line from the lowest to the
  highest sample it covers, including the last sample of the column before, so the trace has no
  gaps and a one-sample spike is never lost. With a pixel per sample or more, the samples are drawn
  as a polyline. Either way a frame costs the same whatever the record length.
- The widget repaints when a frame arrives or a setting changes, and keeps the last frame between
  them, so a slow simulation shows a still trace, not a blank screen.
- The UI pump runs on a precise 16 ms timer.

## Alternatives considered

- **Qt Charts or Qt Graphs:** ruled out by their license.
- **QCustomPlot, Qwt:** QCustomPlot is GPL; Qwt would be a new dependency built around axes and
  plotting features a scope does not have, while the scope still needs its own markers and
  decimation.
- **`QOpenGLWidget` / `QRhi` now:** not needed at about 1 ms per paint, and a second rendering path
  to keep working on every student's GPU driver. It stays the way out if persistence, more channels
  or much longer records make `QPainter` too slow (ARCHITECTURE.md section 11).
- **`QGraphicsView` with items:** per-item bookkeeping for what is redrawn whole on every frame.

## Consequences

- No new dependency; drawing code is plain Qt Widgets, and what it draws is tested without Qt.
- Frames reach the screen at the simulator's pace: the display keeps up with 60 new frames per
  second, but at fast timebases ngspice gives fewer (ADR 0007). Making the simulator faster is
  separate work: the engine's per-point overhead and ngspice's background mode (ADR 0007, M4.7).
- The front panels (M2.4) and the multimeter's display (M2.5) follow the same pattern: Qt-free
  formatting and geometry below, a thin `QPainter` widget in `src/app`.
- Revisit with `QRhi` if a paint passes a few milliseconds: persistence, 4 channels or long records
  (after the MVP, ROADMAP).
