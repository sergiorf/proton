// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/dc_power_supply.h"
#include "proton/schematic/netlist.h"
#include "proton/schematic/validate.h"
#include "proton/test/test.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

using proton::instruments::DcPowerSupply;
using proton::instruments::SpiceFragment;
using proton::instruments::SupplyReading;
using Config = DcPowerSupply::Config;
using Terminals = DcPowerSupply::Terminals;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

std::string joined(const SpiceFragment& fragment)
{
    std::string text;
    for (const std::string& line : fragment.lines) {
        text += line;
        text += '\n';
    }
    return text;
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Compares with tests/golden/instruments/<name>.cir, as the generator's golden tests do.
void checkGolden(const std::string& name, const SpiceFragment& fragment)
{
    const std::string actual = joined(fragment);
    const std::string expected =
        readFile(std::filesystem::path(PROTON_GOLDEN_DIR) / (name + ".cir"));
    if (actual != expected) {
        const std::filesystem::path dir(PROTON_GOLDEN_ACTUAL_DIR);
        std::filesystem::create_directories(dir);
        std::ofstream(dir / (name + ".cir"), std::ios::binary) << actual;
    }
    CHECK_EQ(actual, expected);
}

// CH1 at 5 V between vcc and ground, CH2 at 12 V between pos and neg.
Config twoChannels()
{
    Config config;
    config.channels[0].voltage = 5.0;
    config.channels[1].voltage = 12.0;
    return config;
}

const std::array<Terminals, 2> kWired = {Terminals{"vcc", "0"}, Terminals{"pos", "neg"}};

// A load held from t = 0 for `duration` and one point more, so a gate ending at `duration` ends
// whatever the rounding of the times, a point every `step`: each channel at its voltage,
// delivering `current[c]`.
struct Held
{
    std::vector<double> time;
    std::array<std::vector<double>, 2> voltage;
    std::array<std::vector<double>, 2> current;
};

Held held(double duration, double step, std::array<double, 2> volts, std::array<double, 2> amps)
{
    Held result;
    for (long long i = 0;; ++i) {
        const double t = static_cast<double>(i) * step;
        if (t > duration + step * 1.5) {
            break;
        }
        result.time.push_back(t);
        for (std::size_t c = 0; c < 2; ++c) {
            result.voltage[c].push_back(volts[c]);
            result.current[c].push_back(amps[c]);
        }
    }
    return result;
}

void feed(DcPowerSupply& supply, const Held& load)
{
    supply.feed(load.time, {std::span<const double>(load.voltage[0]), load.voltage[1]},
                {std::span<const double>(load.current[0]), load.current[1]});
}

} // namespace

TEST(defaults_are_5_v_and_1_a_with_the_outputs_on)
{
    const Config config = DcPowerSupply().config();
    for (const DcPowerSupply::ChannelConfig& channel : config.channels) {
        CHECK_EQ(channel.voltage, 5.0);
        CHECK_EQ(channel.currentLimit, 1.0);
        CHECK(channel.output);
    }
}

TEST(settings_are_sanitised)
{
    Config config;
    config.channels[0].voltage = 45.0;
    config.channels[0].currentLimit = 1e-6;
    config.channels[1].voltage = -3.0;
    config.channels[1].currentLimit = 10.0;
    config.channels[1].output = false;
    Config result = DcPowerSupply::sanitise(config);
    CHECK_EQ(result.channels[0].voltage, DcPowerSupply::kMaxVoltage);
    CHECK_EQ(result.channels[0].currentLimit, DcPowerSupply::kMinCurrentLimit);
    CHECK_EQ(result.channels[1].voltage, 0.0);
    CHECK_EQ(result.channels[1].currentLimit, DcPowerSupply::kMaxCurrentLimit);
    CHECK(!result.channels[1].output);

    config.channels[0].voltage = kNaN;
    config.channels[0].currentLimit = kInf;
    result = DcPowerSupply::sanitise(config);
    CHECK_EQ(result.channels[0].voltage, 5.0);
    CHECK_EQ(result.channels[0].currentLimit, 1.0);
}

TEST(golden_two_channels)
{
    checkGolden("dc_power_supply", DcPowerSupply(twoChannels()).toSpice("psu1", kWired));
}

// An output switched off is open: 0 V behind kOffResistance, its source still there to read.
TEST(golden_output_off)
{
    Config config = twoChannels();
    config.channels[1].output = false;
    checkGolden("dc_power_supply_off", DcPowerSupply(config).toSpice("psu1", kWired));
}

TEST(a_channel_without_both_terminals_contributes_nothing)
{
    const DcPowerSupply supply(twoChannels());
    const SpiceFragment one = supply.toSpice("psu1", {Terminals{"vcc", "0"}, Terminals{"pos", ""}});
    CHECK_EQ(one.lines.size(), 2u);
    CHECK_EQ(one.lines[1], std::string("Vpsu1_1 vcc 0 DC 5"));
    CHECK(supply.toSpice("psu1", {Terminals{}, Terminals{"", "neg"}}).lines.empty());
}

TEST(source_names_follow_the_instance_and_channel)
{
    CHECK_EQ(DcPowerSupply::sourceName("psu1", 0), std::string("Vpsu1_1"));
    CHECK_EQ(DcPowerSupply::sourceName("psu2", 1), std::string("Vpsu2_2"));
}

TEST(fragment_passes_validate_and_reaches_the_netlist)
{
    using namespace proton::schematic;
    for (const bool output : {true, false}) {
        Config config = twoChannels();
        config.channels[0].output = output;
        config.channels[1].output = output;
        Circuit circuit;
        circuit.components = {
            {ComponentKind::Resistor, "R1", {"vcc", "0"}, "1k"},
            {ComponentKind::Resistor, "R2", {"pos", "neg"}, "1k"},
            {ComponentKind::Resistor, "R3", {"neg", "0"}, "1k"},
            {ComponentKind::Ground, "GND1", {"0"}, ""},
        };
        circuit.fragments = {{"psu1", DcPowerSupply(config).toSpice("psu1", kWired).lines}};
        CHECK(validate(circuit).empty());
        CHECK(buildNetlist(circuit).find("Vpsu1_2 ") != std::string::npos);
    }
}

// The PLAN.md M5.1 check on the model: 5 V across 1k delivers V/R, 5 mA, shown as 0.0050 A.
TEST(a_resistor_load_reads_v_over_r)
{
    DcPowerSupply supply(twoChannels());
    feed(supply, held(DcPowerSupply::kGateTime, 1e-3, {5.0, 12.0}, {5.0 / 1e3, 12.0 / 4.7e3}));
    const std::optional<SupplyReading> reading = supply.popReading();
    CHECK(reading.has_value());
    if (!reading) {
        return;
    }
    CHECK_NEAR(reading->time, DcPowerSupply::kGateTime, 1e-15);
    CHECK_NEAR(reading->channels[0].voltage, 5.0, 1e-12);
    CHECK_NEAR(reading->channels[0].current, 5e-3, 1e-15);
    CHECK_EQ(reading->channels[0].voltageText, std::string("5.000"));
    CHECK_EQ(reading->channels[0].currentText, std::string("0.0050"));
    CHECK(!reading->channels[0].overLimit);
    CHECK_EQ(reading->channels[1].voltageText, std::string("12.000"));
    CHECK_EQ(reading->channels[1].currentText, std::string("0.0026"));
    CHECK(!supply.popReading().has_value());
}

TEST(a_current_beyond_the_limit_is_flagged)
{
    Config config = twoChannels();
    config.channels[0].currentLimit = 0.1;
    DcPowerSupply supply(config);
    // 5 V across 10 ohm: 0.5 A, over CH1's 0.1 A limit; CH2 under its 1 A.
    feed(supply, held(DcPowerSupply::kGateTime, 1e-3, {5.0, 12.0}, {0.5, 0.012}));
    const std::optional<SupplyReading> reading = supply.popReading();
    CHECK(reading && reading->channels[0].overLimit);
    CHECK(reading && !reading->channels[1].overLimit);
    CHECK(reading && reading->channels[0].currentText == "0.5000");
}

// A 10 ms spike of 2 A in a gate of 50 mA: the mean stays under the 1 A limit, the flag still
// goes up, and the next gate without the spike is clear again.
TEST(a_peak_within_a_gate_is_flagged_and_the_next_gate_clears)
{
    Held load = held(2 * DcPowerSupply::kGateTime, 1e-3, {5.0, 0.0}, {0.05, 0.0});
    for (std::size_t i = 40; i <= 50; ++i) {
        load.current[0][i] = 2.0;
    }
    // One gate at a time: only the newest unread reading is kept.
    DcPowerSupply stepped;
    const std::size_t half = 102; // t = 0 .. 0.101 s
    stepped.feed(std::span(load.time).first(half),
                 {std::span<const double>(load.voltage[0]).first(half),
                  std::span<const double>(load.voltage[1]).first(half)},
                 {std::span<const double>(load.current[0]).first(half),
                  std::span<const double>(load.current[1]).first(half)});
    const std::optional<SupplyReading> first = stepped.popReading();
    CHECK(first && first->channels[0].overLimit);
    CHECK(first && first->channels[0].current < 1.0);
    stepped.feed(std::span(load.time).subspan(half),
                 {std::span<const double>(load.voltage[0]).subspan(half),
                  std::span<const double>(load.voltage[1]).subspan(half)},
                 {std::span<const double>(load.current[0]).subspan(half),
                  std::span<const double>(load.current[1]).subspan(half)});
    const std::optional<SupplyReading> second = stepped.popReading();
    CHECK(second && !second->channels[0].overLimit);
    CHECK(second && second->channels[0].currentText == "0.0500");
}

// The mean over the gate of a current ramping linearly from 0 to 1 A on an uneven grid is 0.5 A
// exactly, wherever the points fall.
TEST(the_mean_is_exact_on_an_uneven_grid)
{
    DcPowerSupply supply;
    const std::vector<double> time = {0.0, 0.003, 0.04, 0.041, 0.077, 0.1};
    std::vector<double> amps;
    for (const double t : time) {
        amps.push_back(t / DcPowerSupply::kGateTime);
    }
    const std::vector<double> volts(time.size(), 5.0);
    supply.feed(time, {std::span<const double>(volts), volts}, {std::span<const double>(amps), amps});
    const std::optional<SupplyReading> reading = supply.popReading();
    CHECK(reading && std::abs(reading->channels[0].current - 0.5) < 1e-12);
}

TEST(configure_drops_the_gate_in_progress)
{
    DcPowerSupply supply;
    feed(supply, held(0.05, 1e-3, {5.0, 5.0}, {0.01, 0.01}));
    supply.configure(supply.config());
    feed(supply, held(0.05, 1e-3, {5.0, 5.0}, {0.01, 0.01}));
    CHECK(!supply.popReading().has_value());
}

TEST(displays_write_fixed_decimals_and_overload)
{
    CHECK_EQ(DcPowerSupply::formatVoltage(5.0), std::string("5.000"));
    CHECK_EQ(DcPowerSupply::formatVoltage(29.9996), std::string("30.000"));
    CHECK_EQ(DcPowerSupply::formatVoltage(-0.0001), std::string("0.000"));
    CHECK_EQ(DcPowerSupply::formatVoltage(-12.5), std::string("-12.500"));
    CHECK_EQ(DcPowerSupply::formatVoltage(150.0), std::string("OL"));
    CHECK_EQ(DcPowerSupply::formatCurrent(0.00499996), std::string("0.0050"));
    CHECK_EQ(DcPowerSupply::formatCurrent(3.0), std::string("3.0000"));
    CHECK_EQ(DcPowerSupply::formatCurrent(12.0), std::string("OL"));
    CHECK_EQ(DcPowerSupply::formatCurrent(kNaN), std::string("OL"));
}
