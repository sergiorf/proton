// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/test/test.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <exception>
#include <vector>

namespace proton::test {

namespace {

struct TestCase
{
    std::string_view name;
    std::string_view file;
    int line;
    TestFn fn;
};

// Function-local static: tests register during static initialisation, in any order.
std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

std::string_view g_currentTest;
int g_currentFailures = 0;
FailureCapture* g_capture = nullptr;

template <typename T>
std::string formatShortest(T value)
{
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

} // namespace

bool registerTest(std::string_view name, std::string_view file, int line, TestFn fn)
{
    registry().push_back({name, file, line, fn});
    return true;
}

FailureCapture::FailureCapture() : previous_(g_capture)
{
    g_capture = this;
}

FailureCapture::~FailureCapture()
{
    g_capture = previous_;
}

void recordFailure(FailureCapture& capture, const std::string& report)
{
    ++capture.failures_;
    capture.text_ += report;
}

namespace detail {

void reportFailure(std::string_view file, int line, const std::string& message)
{
    std::string report;
    report += file;
    report += ':';
    report += std::to_string(line);
    report += ": error in test '";
    report += g_currentTest;
    report += "': ";
    report += message;
    report += '\n';

    if (g_capture != nullptr) {
        recordFailure(*g_capture, report);
        return;
    }
    ++g_currentFailures;
    std::fputs(report.c_str(), stdout);
    std::fflush(stdout);
}

std::string formatFloat(double value)
{
    return formatShortest(value);
}

std::string formatFloat(float value)
{
    return formatShortest(value);
}

void checkTrue(bool ok, const char* expression, const char* file, int line)
{
    if (ok) {
        return;
    }
    std::string message = "CHECK(";
    message += expression;
    message += ") failed";
    reportFailure(file, line, message);
}

void checkNear(double value, double expected, double tolerance, const char* valueText,
               const char* expectedText, const char* toleranceText, const char* file, int line)
{
    // value == expected covers equal infinities, whose difference is NaN.
    if (value == expected || std::fabs(value - expected) <= tolerance) {
        return;
    }
    std::string message = "CHECK_NEAR(";
    message += valueText;
    message += ", ";
    message += expectedText;
    message += ", ";
    message += toleranceText;
    message += ") failed\n    value:      ";
    message += formatFloat(value);
    message += "\n    expected:   ";
    message += formatFloat(expected);
    message += "\n    tolerance:  ";
    message += formatFloat(tolerance);
    message += "\n    difference: ";
    message += formatFloat(value - expected);
    reportFailure(file, line, message);
}

} // namespace detail

int runTests(int argc, char** argv)
{
    std::vector<TestCase> tests = registry();
    // Registration order is source order within a file; order the files for determinism.
    std::stable_sort(tests.begin(), tests.end(),
                     [](const TestCase& a, const TestCase& b) { return a.file < b.file; });

    std::vector<std::string_view> names(argv + 1, argv + argc);
    if (names.size() == 1 && names[0] == "--list") {
        for (const TestCase& test : tests) {
            std::printf("%.*s\n", static_cast<int>(test.name.size()), test.name.data());
        }
        return 0;
    }
    if (tests.empty()) {
        std::printf("no tests registered\n");
        return 1;
    }
    for (std::string_view name : names) {
        const bool known = std::any_of(tests.begin(), tests.end(),
                                       [&](const TestCase& test) { return test.name == name; });
        if (!known) {
            std::printf("unknown test '%.*s' (use --list)\n", static_cast<int>(name.size()),
                        name.data());
            return 1;
        }
    }

    int run = 0;
    int failed = 0;
    for (const TestCase& test : tests) {
        if (!names.empty() && std::find(names.begin(), names.end(), test.name) == names.end()) {
            continue;
        }
        g_currentTest = test.name;
        g_currentFailures = 0;
        try {
            test.fn();
        } catch (const std::exception& e) {
            detail::reportFailure(test.file, test.line,
                                  std::string("unexpected exception: ") + e.what());
        } catch (...) {
            detail::reportFailure(test.file, test.line, "unexpected exception of unknown type");
        }
        ++run;
        if (g_currentFailures > 0) {
            ++failed;
        }
        std::printf("[%s] %.*s\n", g_currentFailures > 0 ? "FAIL" : " OK ",
                    static_cast<int>(test.name.size()), test.name.data());
    }

    std::printf("%d tests, %d failed\n", run, failed);
    std::fflush(stdout);
    return failed > 0 ? 1 : 0;
}

} // namespace proton::test
