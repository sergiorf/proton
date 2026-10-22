// SPDX-License-Identifier: AGPL-3.0-only
// The fetched ngspice DLL is next to the test binary and is a working libngspice (M1.1).
// Windows only: built when CMake found third_party/ngspice.
#include "proton/test/test.h"

#include <ngspice/sharedspice.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace {

std::filesystem::path exeDir()
{
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n < buf.size()) {
            buf.resize(n);
            return std::filesystem::path(buf).parent_path();
        }
        buf.resize(buf.size() * 2);
    }
}

// Loaded once per process: ngspice keeps global state, and M1.2 owns its lifetime.
HMODULE ngspice()
{
    static const HMODULE module = LoadLibraryW((exeDir() / L"ngspice.dll").c_str());
    return module;
}

template <typename Fn>
Fn* resolve(const char* name)
{
    return reinterpret_cast<Fn*>(reinterpret_cast<void*>(GetProcAddress(ngspice(), name)));
}

std::string g_output;

int onSendChar(char* text, int /*id*/, void* /*user*/)
{
    g_output += text;
    g_output += '\n';
    return 0;
}

} // namespace

TEST(header_matches_the_fetched_version)
{
    CHECK_EQ(std::string_view(NGSPICE_PACKAGE_VERSION), std::string_view(PROTON_NGSPICE_VERSION));
}

TEST(dll_loads_from_the_test_directory)
{
    CHECK(ngspice() != nullptr);
}

TEST(dll_exports_the_shared_library_api)
{
    CHECK(resolve<decltype(ngSpice_Init)>("ngSpice_Init") != nullptr);
    CHECK(resolve<decltype(ngSpice_Command)>("ngSpice_Command") != nullptr);
    CHECK(resolve<decltype(ngSpice_Circ)>("ngSpice_Circ") != nullptr);
    CHECK(resolve<decltype(ngSpice_running)>("ngSpice_running") != nullptr);
    CHECK(resolve<decltype(ngGet_Vec_Info)>("ngGet_Vec_Info") != nullptr);
    CHECK(resolve<decltype(ngSpice_CurPlot)>("ngSpice_CurPlot") != nullptr);
    CHECK(resolve<decltype(ngSpice_AllVecs)>("ngSpice_AllVecs") != nullptr);
}

TEST(dll_initialises_and_runs_a_command)
{
    auto* init = resolve<decltype(ngSpice_Init)>("ngSpice_Init");
    auto* command = resolve<decltype(ngSpice_Command)>("ngSpice_Command");
    CHECK(init != nullptr);
    CHECK(command != nullptr);
    if (init == nullptr || command == nullptr) {
        return;
    }
    CHECK_EQ(init(onSendChar, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr), 0);
    char echo[] = "echo proton-m1.1";
    CHECK_EQ(command(echo), 0);
    CHECK(g_output.find("proton-m1.1") != std::string::npos);
}
