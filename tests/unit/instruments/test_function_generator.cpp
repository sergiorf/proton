// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/function_generator.h"
#include "proton/schematic/netlist.h"
#include "proton/schematic/validate.h"
#include "proton/test/test.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

using proton::instruments::FunctionGenerator;
using proton::instruments::SpiceFragment;
using Config = FunctionGenerator::Config;
using Waveform = FunctionGenerator::Waveform;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

Config makeConfig(Waveform waveform, double frequency, double amplitude, double offset,
                  double outputImpedance)
{
    Config config;
    config.waveform = waveform;
    config.frequency = frequency;
    config.amplitude = amplitude;
    config.offset = offset;
    config.outputImpedance = outputImpedance;
    return config;
}

// Field by field, so a failure names the setting.
void checkConfig(const Config& actual, const Config& expected)
{
    CHECK_EQ(actual.waveform, expected.waveform);
    CHECK_EQ(actual.frequency, expected.frequency);
    CHECK_EQ(actual.amplitude, expected.amplitude);
    CHECK_EQ(actual.offset, expected.offset);
    CHECK_EQ(actual.outputImpedance, expected.outputImpedance);
    CHECK_EQ(actual.output, expected.output);
}

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

// Compares with tests/golden/instruments/<name>.cir, one fragment line per line. On a mismatch
// the actual text is written to the build tree, ready to inspect or copy over the golden file.
void checkGolden(const std::string& name, const Config& config)
{
    const FunctionGenerator generator(config);
    const std::string actual = joined(generator.toSpice("gen1", "out", "0"));
    const std::string expected =
        readFile(std::filesystem::path(PROTON_GOLDEN_DIR) / (name + ".cir"));
    if (actual != expected) {
        const std::filesystem::path dir(PROTON_GOLDEN_ACTUAL_DIR);
        std::filesystem::create_directories(dir);
        std::ofstream(dir / (name + ".cir"), std::ios::binary) << actual;
    }
    CHECK_EQ(actual, expected);
}

} // namespace

TEST(golden_sine)
{
    checkGolden("function_generator_sine", makeConfig(Waveform::Sine, 1e3, 1.0, 0.0, 50.0));
}

TEST(golden_sine_with_offset)
{
    checkGolden("function_generator_sine_offset",
                makeConfig(Waveform::Sine, 50.0, 0.25, -1.5, 600.0));
}

TEST(golden_square)
{
    checkGolden("function_generator_square", makeConfig(Waveform::Square, 1e3, 2.5, 1.0, 50.0));
}

TEST(golden_triangle)
{
    checkGolden("function_generator_triangle", makeConfig(Waveform::Triangle, 1e3, 1.0, 0.0, 50.0));
}

TEST(golden_dc)
{
    checkGolden("function_generator_dc", makeConfig(Waveform::Dc, 1e3, 1.0, 3.3, 50.0));
}

TEST(golden_ideal_output_has_no_resistor)
{
    checkGolden("function_generator_ideal", makeConfig(Waveform::Sine, 1e3, 1.0, 0.0, 0.0));
}

// Output off opens the output: 0 V behind kOffResistance, whatever the waveform and impedance.
TEST(golden_output_off)
{
    Config config = makeConfig(Waveform::Square, 1e3, 2.5, 1.0, 0.0);
    config.output = false;
    checkGolden("function_generator_off", config);
}

TEST(output_off_still_leaves_a_dc_path_so_the_circuit_validates)
{
    // An RC low-pass whose only DC path to ground from `in` is the generator: with the output open
    // and nothing else, SPICE could not solve `in`. The leak through kOffResistance keeps it
    // solvable without loading the circuit.
    Config config;
    config.output = false;
    proton::schematic::Circuit circuit;
    circuit.components = {
        {proton::schematic::ComponentKind::Resistor, "R1", {"in", "out"}, "1k"},
        {proton::schematic::ComponentKind::Capacitor, "C1", {"out", "0"}, "100n"},
    };
    circuit.fragments = {{"gen1", FunctionGenerator(config).toSpice("gen1", "in", "0").lines}};
    CHECK(!proton::schematic::hasErrors(proton::schematic::validate(circuit)));
}

TEST(the_output_switch_survives_sanitising)
{
    Config config;
    config.output = false;
    CHECK(!FunctionGenerator::sanitise(config).output);
    CHECK(FunctionGenerator::sanitise(Config{}).output);
}

// 1/3 kHz has no exact period; the times are rounded to 12 significant digits.
TEST(golden_derived_times_are_rounded)
{
    checkGolden("function_generator_square_3k", makeConfig(Waveform::Square, 3e3, 1.0, 0.0, 50.0));
}

TEST(fragment_uses_the_given_names_and_nets)
{
    const FunctionGenerator generator;
    const SpiceFragment fragment = generator.toSpice("gen2", "drive", "gnd");
    CHECK_EQ(joined(fragment), "* function generator gen2\n"
                               "Vgen2 gen2_int gnd SIN(0 1 1k)\n"
                               "Rgen2 gen2_int drive 50\n");
}

TEST(same_settings_give_the_same_fragment)
{
    const Config config = makeConfig(Waveform::Triangle, 7e3, 0.3, 0.1, 75.0);
    CHECK(FunctionGenerator(config).toSpice("gen1", "a", "0") ==
          FunctionGenerator(config).toSpice("gen1", "a", "0"));
}

TEST(fragment_passes_validate_and_reaches_the_netlist)
{
    using namespace proton::schematic;
    for (const Waveform waveform :
         {Waveform::Sine, Waveform::Square, Waveform::Triangle, Waveform::Dc}) {
        for (const double impedance : {50.0, 0.0}) {
            const FunctionGenerator generator(makeConfig(waveform, 1e3, 1.0, 0.0, impedance));
            Circuit circuit;
            circuit.components = {
                {ComponentKind::Resistor, "R1", {"out", "gnd"}, "1k"},
                {ComponentKind::Ground, "GND1", {"gnd"}, ""},
            };
            circuit.fragments = {{"gen1", generator.toSpice("gen1", "out", "gnd").lines}};
            CHECK(validate(circuit).empty());
            CHECK(buildNetlist(circuit).find("Vgen1 ") != std::string::npos);
        }
    }
}

TEST(default_config_is_a_1khz_1v_sine_behind_50_ohm)
{
    const FunctionGenerator generator;
    checkConfig(generator.config(), makeConfig(Waveform::Sine, 1e3, 1.0, 0.0, 50.0));
}

TEST(valid_settings_pass_unchanged)
{
    const Config config = makeConfig(Waveform::Square, 12.5e3, 4.0, -6.0, 600.0);
    checkConfig(FunctionGenerator::sanitise(config), config);
}

TEST(configure_sanitises)
{
    FunctionGenerator generator;
    generator.configure(makeConfig(Waveform::Triangle, 1e9, 20.0, 1.0, -5.0));
    checkConfig(generator.config(), makeConfig(Waveform::Triangle, 10e6, 10.0, 0.0, 0.0));
}

TEST(non_finite_values_become_defaults)
{
    checkConfig(FunctionGenerator::sanitise(makeConfig(Waveform::Sine, kNaN, kNaN, kNaN, kNaN)),
                Config{});
    checkConfig(FunctionGenerator::sanitise(makeConfig(Waveform::Sine, kInf, -kInf, kInf, kInf)),
                Config{});
}

TEST(frequency_is_clamped)
{
    const auto frequency = [](double value) {
        return FunctionGenerator::sanitise(makeConfig(Waveform::Sine, value, 1.0, 0.0, 50.0))
            .frequency;
    };
    CHECK_EQ(frequency(0.0), FunctionGenerator::kMinFrequency);
    CHECK_EQ(frequency(-1e3), FunctionGenerator::kMinFrequency);
    CHECK_EQ(frequency(1e-3), 1e-3);
    CHECK_EQ(frequency(10e6), 10e6);
    CHECK_EQ(frequency(20e6), FunctionGenerator::kMaxFrequency);
}

TEST(amplitude_is_clamped)
{
    const auto amplitude = [](double value) {
        return FunctionGenerator::sanitise(makeConfig(Waveform::Sine, 1e3, value, 0.0, 50.0))
            .amplitude;
    };
    CHECK_EQ(amplitude(-1.0), 0.0);
    CHECK_EQ(amplitude(0.0), 0.0);
    CHECK_EQ(amplitude(10.0), 10.0);
    CHECK_EQ(amplitude(11.0), FunctionGenerator::kMaxVoltage);
}

TEST(offset_keeps_the_output_within_the_voltage_limit)
{
    const auto offset = [](Waveform waveform, double amplitude, double value) {
        return FunctionGenerator::sanitise(makeConfig(waveform, 1e3, amplitude, value, 50.0))
            .offset;
    };
    CHECK_EQ(offset(Waveform::Sine, 4.0, 6.0), 6.0);
    CHECK_EQ(offset(Waveform::Sine, 4.0, 8.0), 6.0);
    CHECK_EQ(offset(Waveform::Square, 4.0, -8.0), -6.0);
    CHECK_EQ(offset(Waveform::Triangle, 10.0, 1.0), 0.0);
    // DC has no swing, so the amplitude does not limit it.
    CHECK_EQ(offset(Waveform::Dc, 10.0, 10.0), 10.0);
    CHECK_EQ(offset(Waveform::Dc, 10.0, -12.0), -10.0);
}

TEST(output_impedance_is_clamped)
{
    const auto impedance = [](double value) {
        return FunctionGenerator::sanitise(makeConfig(Waveform::Sine, 1e3, 1.0, 0.0, value))
            .outputImpedance;
    };
    CHECK_EQ(impedance(-50.0), 0.0);
    CHECK_EQ(impedance(0.0), 0.0);
    CHECK_EQ(impedance(1e6), 1e6);
    CHECK_EQ(impedance(1e9), FunctionGenerator::kMaxOutputImpedance);
}

TEST(unknown_waveform_becomes_sine)
{
    const Config config = makeConfig(static_cast<Waveform>(42), 1e3, 1.0, 0.0, 50.0);
    CHECK_EQ(FunctionGenerator::sanitise(config).waveform, Waveform::Sine);
}
