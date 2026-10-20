// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Minimal dependency-free test harness (ARCHITECTURE.md section 10).
//
//     TEST(sine_rms_is_amplitude_over_sqrt2)
//     {
//         CHECK_NEAR(rms(sine), 0.70710678, 1e-6);
//     }
//
// Checks are non-fatal: a failing check prints file, line and both values, and the test goes on.
// Link proton::test_support, which provides main(). The executable runs every test, or only the
// tests named on the command line (`--list` prints the names), and exits non-zero if any check
// failed or a test threw.

#include <concepts>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace proton::test {

using TestFn = void (*)();

// Adds a test to the registry; used by TEST. Returns true so it can initialise a static.
bool registerTest(std::string_view name, std::string_view file, int line, TestFn fn);

// Runs the tests selected by the command line and returns the process exit code.
int runTests(int argc, char** argv);

// While alive, failed checks are recorded here instead of being printed and counted against the
// running test. Used by the harness self-tests to check that checks fail and what they print.
class FailureCapture
{
public:
    FailureCapture();
    ~FailureCapture();
    FailureCapture(const FailureCapture&) = delete;
    FailureCapture& operator=(const FailureCapture&) = delete;

    int failures() const { return failures_; }
    const std::string& text() const { return text_; }

private:
    friend void recordFailure(FailureCapture& capture, const std::string& report);

    int failures_ = 0;
    std::string text_;
    FailureCapture* previous_;
};

namespace detail {

void reportFailure(std::string_view file, int line, const std::string& message);

// Shortest text that reads back as the same value.
std::string formatFloat(double value);
std::string formatFloat(float value);

template <typename T>
concept Streamable = requires(std::ostream& os, const T& value) { os << value; };

template <typename T>
concept CharType = std::same_as<T, char> || std::same_as<T, signed char> ||
                   std::same_as<T, unsigned char> || std::same_as<T, wchar_t> ||
                   std::same_as<T, char8_t> || std::same_as<T, char16_t> ||
                   std::same_as<T, char32_t>;

// Integers that std::cmp_equal accepts: no bool, no character types.
template <typename T>
concept PlainInteger = std::integral<T> && !std::same_as<T, bool> && !CharType<T>;

template <typename T>
std::string toString(const T& value)
{
    if constexpr (std::is_same_v<T, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_same_v<T, char>) {
        return std::string{'\'', value, '\''};
    } else if constexpr (std::is_same_v<T, signed char> || std::is_same_v<T, unsigned char>) {
        return std::to_string(static_cast<int>(value));
    } else if constexpr (std::is_floating_point_v<T>) {
        if constexpr (std::is_same_v<T, float>) {
            return formatFloat(value);
        } else {
            return formatFloat(static_cast<double>(value));
        }
    } else if constexpr (std::is_enum_v<T>) {
        return toString(static_cast<std::underlying_type_t<T>>(value));
    } else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
        if constexpr (std::is_pointer_v<T>) {
            if (value == nullptr) {
                return "nullptr";
            }
        }
        std::string text{'"'};
        text += std::string_view{value};
        text += '"';
        return text;
    } else if constexpr (Streamable<T>) {
        std::ostringstream os;
        os << value;
        return os.str();
    } else {
        return "<unprintable>";
    }
}

template <typename A, typename B>
bool equal(const A& a, const B& b)
{
    if constexpr (PlainInteger<A> && PlainInteger<B>) {
        return std::cmp_equal(a, b); // no signed/unsigned surprises
    } else {
        return a == b;
    }
}

void checkTrue(bool ok, const char* expression, const char* file, int line);

template <typename A, typename B>
void checkEq(const A& value, const B& expected, const char* valueText, const char* expectedText,
             const char* file, int line)
{
    if (equal(value, expected)) {
        return;
    }
    std::string message = "CHECK_EQ(";
    message += valueText;
    message += ", ";
    message += expectedText;
    message += ") failed\n    value:    ";
    message += toString(value);
    message += "\n    expected: ";
    message += toString(expected);
    reportFailure(file, line, message);
}

void checkNear(double value, double expected, double tolerance, const char* valueText,
               const char* expectedText, const char* toleranceText, const char* file, int line);

} // namespace detail
} // namespace proton::test

#define TEST(name)                                                                                 \
    static void proton_test_##name();                                                              \
    [[maybe_unused]] static const bool proton_test_registered_##name =                             \
        ::proton::test::registerTest(#name, __FILE__, __LINE__, &proton_test_##name);              \
    static void proton_test_##name()

#define CHECK(...)                                                                                 \
    ::proton::test::detail::checkTrue(static_cast<bool>(__VA_ARGS__), #__VA_ARGS__, __FILE__,      \
                                      __LINE__)

#define CHECK_EQ(value, expected)                                                                  \
    ::proton::test::detail::checkEq((value), (expected), #value, #expected, __FILE__, __LINE__)

// Passes when |value - expected| <= tolerance. NaN never passes; equal infinities do.
#define CHECK_NEAR(value, expected, tolerance)                                                     \
    ::proton::test::detail::checkNear((value), (expected), (tolerance), #value, #expected,         \
                                      #tolerance, __FILE__, __LINE__)
