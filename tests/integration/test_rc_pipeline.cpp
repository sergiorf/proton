// SPDX-License-Identifier: AGPL-3.0-only
// The whole M0 pipeline on the RC example: circuit -> netlist -> MockEngine on the worker
// thread -> scope and multimeters, checked against the analytic answers.
#include "proton/sim/mock_engine.h"
#include "proton/spike/pipeline.h"
#include "proton/test/test.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

using proton::instruments::DmmReading;
using proton::instruments::ScopeFrame;
using proton::spike::PipelineResult;
using proton::spike::PipelineSettings;
using proton::spike::RcExample;

namespace {

constexpr double kPi = std::numbers::pi;

// The default example: 1 kHz, 1 V around 0.5 V, into R = 1k, C = 100n.
constexpr double kOffset = 0.5;
constexpr double kAmplitude = 1.0;
constexpr double kOmega = 2.0 * kPi * 1e3;
constexpr double kTau = 1e3 * 100e-9;

// Steady state of the RC low-pass: gain 1 / sqrt(1 + (w tau)^2), phase -atan(w tau).
const double kGain = 1.0 / std::sqrt(1.0 + kOmega * kTau * kOmega * kTau); // 0.8467
const double kPhase = std::atan(kOmega * kTau);                            // 32.1 degrees

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Compares with tests/golden/integration/<name>.cir. On a mismatch the actual text is written to
// the build tree, ready to inspect or copy over the golden file.
void checkGolden(const std::string& name, const std::string& actual)
{
    const std::string expected =
        readFile(std::filesystem::path(PROTON_GOLDEN_DIR) / (name + ".cir"));
    if (actual != expected) {
        const std::filesystem::path dir(PROTON_GOLDEN_ACTUAL_DIR);
        std::filesystem::create_directories(dir);
        std::ofstream(dir / (name + ".cir"), std::ios::binary) << actual;
    }
    CHECK_EQ(actual, expected);
}

PipelineResult runDefault(const PipelineSettings& settings)
{
    return proton::spike::run(RcExample{}, settings);
}

PipelineSettings defaults()
{
    return proton::spike::defaultSettings(RcExample{});
}

// Distance from `t` to the nearest whole number of periods (1 ms).
double offPeriod(double t)
{
    const double period = 2.0 * kPi / kOmega;
    return std::fabs(t - std::round(t / period) * period);
}

bool sameReadings(const std::vector<DmmReading>& a, const std::vector<DmmReading>& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].time != b[i].time || a[i].value != b[i].value || a[i].range != b[i].range) {
            return false;
        }
    }
    return true;
}

bool sameFrames(const std::vector<ScopeFrame>& a, const std::vector<ScopeFrame>& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].triggerTime != b[i].triggerTime || a[i].startTime != b[i].startTime ||
            a[i].triggered != b[i].triggered || a[i].values != b[i].values) {
            return false;
        }
    }
    return true;
}

double parse(const std::string& text)
{
    double value = 0.0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

} // namespace

TEST(netlist_matches_the_golden_file)
{
    const PipelineResult result = runDefault(defaults());
    CHECK(result.ok());
    CHECK(result.diagnostics.empty());
    checkGolden("rc_example", result.netlist);
}

TEST(the_run_reaches_the_duration)
{
    const PipelineSettings settings = defaults();
    const PipelineResult result = runDefault(settings);
    CHECK(result.ok());
    CHECK(result.lastTime <= settings.duration);
    CHECK(result.lastTime > settings.duration - 1e-4);
    CHECK_EQ(result.dcReadings.size(), 3u); // gates end at 0.1, 0.2 and 0.3 s
    CHECK_EQ(result.acReadings.size(), 3u);
    CHECK(result.frames.size() > 100u);
}

TEST(multimeter_reads_the_analytic_dc_and_ac_values)
{
    const PipelineResult result = runDefault(defaults());
    CHECK(result.ok());
    // After the first gate, which holds the start-up transient: DC is the offset, AC the RMS of
    // the attenuated sine.
    for (std::size_t i = 1; i < result.dcReadings.size(); ++i) {
        CHECK_NEAR(result.dcReadings[i].value, kOffset, 1e-4);
        CHECK_EQ(result.dcReadings[i].display.text, std::string("0.5000"));
    }
    for (std::size_t i = 1; i < result.acReadings.size(); ++i) {
        CHECK_NEAR(result.acReadings[i].value, kAmplitude * kGain / std::sqrt(2.0), 1e-3);
        CHECK_EQ(result.acReadings[i].display.unit, std::string("V"));
    }
}

TEST(scope_frames_are_triggered_stable_and_show_the_rc_response)
{
    const PipelineResult result = runDefault(defaults());
    CHECK(result.ok());
    CHECK(!result.frames.empty());
    for (const ScopeFrame& frame : result.frames) {
        CHECK(frame.triggered);
        CHECK_EQ(frame.values[0].size(), 1000u);
        // CH1 rises through the offset at every whole period: the trigger lands there to a
        // fraction of a sample, so the trace stands still.
        CHECK(offPeriod(frame.triggerTime) < 0.02 * frame.sampleInterval);
        for (std::size_t i = 0; i < frame.values[0].size(); ++i) {
            const double t = frame.startTime + static_cast<double>(i) * frame.sampleInterval;
            CHECK_NEAR(frame.values[0][i], kOffset + kAmplitude * std::sin(kOmega * t), 2e-3);
            CHECK_NEAR(frame.values[1][i],
                       kOffset + kAmplitude * kGain * std::sin(kOmega * t - kPhase), 2e-3);
        }
    }
}

TEST(two_runs_are_identical)
{
    const PipelineResult a = runDefault(defaults());
    const PipelineResult b = runDefault(defaults());
    CHECK(sameFrames(a.frames, b.frames));
    CHECK(sameReadings(a.dcReadings, b.dcReadings));
    CHECK(sameReadings(a.acReadings, b.acReadings));
}

TEST(the_result_does_not_depend_on_chunks_or_queue)
{
    PipelineSettings small = defaults();
    small.worker.chunkDuration = 0.1e-3;
    small.worker.queueCapacity = 2;
    const PipelineResult a = runDefault(defaults());
    const PipelineResult b = runDefault(small);
    CHECK(a.ok() && b.ok());
    CHECK_EQ(a.lastTime, b.lastTime);
    CHECK(sameFrames(a.frames, b.frames));
    CHECK(sameReadings(a.dcReadings, b.dcReadings));
    CHECK(sameReadings(a.acReadings, b.acReadings));
}

TEST(an_invalid_circuit_stops_at_validate)
{
    RcExample example;
    example.capacitance = "0";
    const PipelineResult result = proton::spike::run(example, defaults());
    CHECK(!result.ok());
    CHECK(!result.diagnostics.empty());
    CHECK_EQ(result.error, result.diagnostics.front().message);
    CHECK(result.netlist.empty());
    CHECK(result.frames.empty());
}

TEST(only_a_sine_generator_runs_on_the_mock)
{
    RcExample example;
    example.generator.waveform = proton::instruments::FunctionGenerator::Waveform::Square;
    const PipelineResult result = proton::spike::run(example, defaults());
    CHECK(!result.ok());
    CHECK(!result.netlist.empty());
}

TEST(an_engine_failure_ends_the_run_and_keeps_what_came_before)
{
    PipelineSettings settings = defaults();
    settings.failAt = 0.15;
    const PipelineResult failed = runDefault(settings);
    CHECK_EQ(failed.error, std::string(proton::sim::MockEngine::kFailure));
    CHECK(failed.lastTime <= 0.15);
    CHECK(failed.lastTime > 0.149);
    CHECK_EQ(failed.dcReadings.size(), 1u); // the gate at 0.1 s

    // What came before the failure is what a full run gives up to then.
    const PipelineResult full = runDefault(defaults());
    CHECK(!failed.frames.empty());
    CHECK(failed.frames.size() < full.frames.size());
    for (std::size_t i = 0; i < failed.frames.size(); ++i) {
        CHECK(failed.frames[i].values == full.frames[i].values);
    }
}

TEST(csv_has_a_header_and_one_row_per_sample_that_reads_back_exactly)
{
    const PipelineResult result = runDefault(defaults());
    CHECK(!result.frames.empty());
    const ScopeFrame& frame = result.frames.back();
    std::istringstream csv(proton::spike::frameCsv(frame));
    std::string line;
    std::getline(csv, line);
    CHECK_EQ(line, std::string("t_rel_s,ch1_v_in,ch2_v_out"));
    std::size_t rows = 0;
    while (std::getline(csv, line)) {
        const std::size_t a = line.find(',');
        const std::size_t b = line.find(',', a + 1);
        CHECK(a != std::string::npos && b != std::string::npos);
        const double t = frame.startTime + static_cast<double>(rows) * frame.sampleInterval;
        CHECK_EQ(parse(line.substr(0, a)), t);
        CHECK_EQ(parse(line.substr(a + 1, b - a - 1)), frame.values[0][rows]);
        CHECK_EQ(parse(line.substr(b + 1)), frame.values[1][rows]);
        ++rows;
    }
    CHECK_EQ(rows, frame.values[0].size());
}

TEST(report_has_the_readings_and_the_frame)
{
    const RcExample example;
    const PipelineSettings settings = defaults();
    const PipelineResult result = proton::spike::run(example, settings);
    const std::string text = proton::spike::report(example, settings, result);
    CHECK(text.find("# dmm v(out) DC V: 0.5000 V\n") != std::string::npos);
    CHECK(text.find("# dmm v(out) AC V: 0.598") != std::string::npos);
    CHECK(text.find("triggered by CH1 rising at 0.5 V") != std::string::npos);
    CHECK(text.find("t_rel_s,ch1_v_in,ch2_v_out\n") != std::string::npos);
    CHECK(text.find("#   Vgen1 in 0 SIN(500m 1 1k)\n") != std::string::npos);
}
