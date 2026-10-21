// SPDX-License-Identifier: AGPL-3.0-only
// proton-spike: runs the RC example through the whole pipeline on the mock engine and prints the
// multimeter readings and the last scope frame as CSV (ARCHITECTURE.md section 10).
//
//     proton-spike [--duration <seconds>] > rc.csv
//
// The duration is a SPICE number ("0.5", "500m"). The exit code is 0 when the run reached the
// duration and gave a triggered frame and both readings, 1 otherwise, 2 for bad arguments.

#include "proton/core/spice_number.h"
#include "proton/spike/pipeline.h"

#include <iostream>
#include <optional>
#include <string_view>

namespace {

constexpr const char* kUsage = "usage: proton-spike [--duration <seconds>]\n";

} // namespace

int main(int argc, char** argv)
{
    const proton::spike::RcExample example;
    proton::spike::PipelineSettings settings = proton::spike::defaultSettings(example);

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << kUsage;
            return 0;
        }
        if (arg == "--duration" && i + 1 < argc) {
            const std::optional<double> duration = proton::core::parseSpiceNumber(argv[++i]);
            if (!duration || !(*duration > 0.0)) {
                std::cerr << "proton-spike: --duration needs a positive number of seconds\n";
                return 2;
            }
            settings.duration = *duration;
            continue;
        }
        std::cerr << "proton-spike: unknown argument '" << arg << "'\n" << kUsage;
        return 2;
    }

    const proton::spike::PipelineResult result = proton::spike::run(example, settings);
    std::cout << proton::spike::report(example, settings, result);
    const bool complete = result.ok() && !result.frames.empty() && result.frames.back().triggered &&
                          !result.dcReadings.empty() && !result.acReadings.empty();
    if (!complete) {
        std::cerr << "proton-spike: "
                  << (result.ok() ? "the run gave no triggered frame or no reading" : result.error)
                  << '\n';
        return 1;
    }
    return 0;
}
