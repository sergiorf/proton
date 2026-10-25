// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/bench/engines.h"

#ifdef PROTON_HAVE_NGSPICE
#include "proton/sim/ngspice_engine.h"
#endif

#include <memory>
#include <string>

namespace proton::bench {

bool haveNgspice()
{
#ifdef PROTON_HAVE_NGSPICE
    return true;
#else
    return false;
#endif
}

RunControl::EngineFactory ngspiceEngines()
{
    return [](double maxStep, std::string& error) -> std::unique_ptr<sim::ISimEngine> {
#ifdef PROTON_HAVE_NGSPICE
        (void)error;
        sim::NgspiceEngine::Config config;
        config.maxStep = maxStep;
        return std::make_unique<sim::NgspiceEngine>(config);
#else
        (void)maxStep;
        error = "This build of Proton has no simulator: fetch ngspice with "
                "tools/fetch-ngspice.ps1 and build again.";
        return nullptr;
#endif
    };
}

} // namespace proton::bench
