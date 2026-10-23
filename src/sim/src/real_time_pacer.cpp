// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/sim/real_time_pacer.h"

#include <algorithm>

namespace proton::sim {

void RealTimePacer::start(double wallNow, double simStart)
{
    wallOrigin_ = wallNow;
    simOrigin_ = simStart;
    windowWall_ = wallNow;
    windowSim_ = simStart;
    speed_ = 1.0;
}

double RealTimePacer::due(double wallNow) const
{
    return simOrigin_ + std::max(0.0, wallNow - wallOrigin_);
}

void RealTimePacer::fed(double wallNow, double fedTo)
{
    // Slip rather than let a simulator that recovers race ahead to catch up.
    if (due(wallNow) - fedTo > kMaxLag) {
        wallOrigin_ = wallNow;
        simOrigin_ = fedTo + kMaxLag;
    }
    const double window = wallNow - windowWall_;
    if (window >= kSpeedWindow) {
        speed_ = std::min(1.0, (fedTo - windowSim_) / window);
        windowWall_ = wallNow;
        windowSim_ = fedTo;
    }
}

} // namespace proton::sim
