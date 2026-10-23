// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

namespace proton::sim {

// Paces simulation output to wall-clock time (decision D6, ADR 0007), so that a 1 Hz blink takes a
// second on screen, as on a real bench. The consumer of a run (the bench, proton-spike) feeds its
// instruments only the output up to due(now) and keeps the rest; a full ring then holds the
// simulator back. Instruments still see simulation time only: pacing changes when output is fed,
// never what it is.
//
// Wall-clock time is passed in as seconds from any fixed epoch, so the pacer itself reads no clock
// and is tested exactly.
//
// A simulator slower than real time is fed whatever it gives. Once it is more than kMaxLag behind,
// what is due slips, so that a simulator that recovers does not race ahead to catch up. speed()
// says how fast the simulation went, as a fraction of real time.
class RealTimePacer
{
public:
    // How far the simulation may fall behind wall-clock time before what is due slips, s.
    static constexpr double kMaxLag = 0.1;
    // Wall-clock time over which speed() is measured, s.
    static constexpr double kSpeedWindow = 0.5;

    // Starts pacing: simulation time `simStart` is due at wall-clock time `wallNow`.
    void start(double wallNow, double simStart = 0.0);

    // The simulation time due at `wallNow`: output up to it may be fed, later output waits.
    double due(double wallNow) const;

    // Tells the pacer that output has been fed up to simulation time `fedTo`, at `wallNow`.
    void fed(double wallNow, double fedTo);

    // Simulation time per wall-clock time over the last whole kSpeedWindow, at most 1: 1 while
    // the simulator keeps up, and until the first window ends.
    double speed() const { return speed_; }

private:
    double wallOrigin_ = 0.0;
    double simOrigin_ = 0.0;
    double windowWall_ = 0.0;
    double windowSim_ = 0.0;
    double speed_ = 1.0;
};

} // namespace proton::sim
