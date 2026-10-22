# ADR 0002: Simulation backend: ngspice as a runtime-loaded shared library

- **Status:** accepted
- **Date:** 2026-10-22
- **Plan task:** M1.1

## Context

Proton needs a real circuit simulator behind `ISimEngine` (ARCHITECTURE.md section 8). ngspice
is mature, mostly BSD-licensed, and ships an official Windows shared library with a C API
(`sharedspice.h`) that runs transient analyses in a background thread and streams every accepted
time point through a callback.

What is known about the library and the releases:

- **Global state.** libngspice keeps the circuit, plots and options in process-wide globals. One
  instance per process; a second `NgspiceEngine` in the same process would share and corrupt it.
- **Background thread.** `bg_run` runs the analysis on ngspice's own thread. The data callback is
  called on that thread for every accepted time point; returning from it is what lets ngspice go
  on, so blocking in it is the only backpressure there is.
- **Fatal errors.** On some errors ngspice calls the `ControlledExit` callback and its state is
  unusable afterwards. The only way back is to unload and reload the DLL.
- **ngspice-47** (2026-08-11, the newest release) has an official DLL, `ngspice-47_dll_64.7z`,
  that imports `sndfile.dll` and `samplerate.dll`, which the archive does not contain:
  `LoadLibrary` fails with error 126 (module not found). They ship only in the full GUI package.
- **ngspice-46**'s official DLL imports only Windows system DLLs and `libomp140.x86_64.dll`
  (Microsoft's LLVM-based OpenMP runtime), which is in the same archive. It loads as it is.
- **Licenses** (the `COPYING` file of the release). Most of ngspice is Modified BSD. Compiled into
  `ngspice.dll` as well: KLU (LGPLv2), numparam (LGPLv2 or later), the sparse solver (MIT-style),
  OSDI (MPL-2.0), XSPICE (public domain), `ndev` (public domain). The XSPICE code model
  `table.cm` is GPLv2 or later; it is a separate file in `lib/ngspice`, not part of the DLL.
  `tclspice.c` (LGPLv2) is not part of the shared library.

## Decision

- **Pinned release:** ngspice-46, the official 64-bit shared library `ngspice-46_dll_64.7z` from
  SourceForge (`ng-spice-rework/old-releases/46/`), SHA-256
  `5C03F25709D6EA7D1E58A82EBE7C1E6EB5177F266787466BF0C716791BA26E4C`.
- **Fetching:** `tools/fetch-ngspice.ps1` downloads the archive, refuses it if the SHA-256 differs,
  extracts it (7-Zip if installed, otherwise the `tar.exe` that ships with Windows) and keeps only
  `ngspice.dll`, `libomp140.x86_64.dll` and `sharedspice.h` in `third_party/ngspice` (git-ignored),
  plus a `VERSION` stamp. Nothing else from the archive is kept: no code models, so `table.cm`
  (GPL) can never be shipped by accident. The script does nothing if the pinned version is already
  in place; `-Force` fetches again.
- **Build:** `cmake/ProtonNgspice.cmake` finds the header and DLLs (Windows only) and sets
  `PROTON_NGSPICE_FOUND`. Nothing links against ngspice: code includes the header through
  `proton::ngspice_headers` and copies the DLLs next to its binary with
  `proton_copy_ngspice_dlls(<target>)`. Without a fetch the build succeeds without ngspice;
  `-DPROTON_REQUIRE_NGSPICE=ON` (set in the Windows CI jobs) makes a missing fetch a configure
  error. The `ngspice_dll` test loads the DLL from its own directory, resolves the API and runs a
  command.
- **Runtime loading:** `NgspiceEngine` (M1.2) loads `ngspice.dll` with `LoadLibrary` and resolves
  the API with `GetProcAddress`. The app starts without it (mock fallback) and can unload and
  reload it after a fatal error (M1.4).
- **Constraints the engine follows:** one instance per process, enforced by a guard; the data
  callback copies points into a bounded queue and blocks while it is full; a `ControlledExit`
  marks the engine dead until the DLL is reloaded; tests that use ngspice run each executable in
  its own process.
- **Licensing:** the code inside `ngspice.dll` is accepted under Proton's dependency rule
  ([LICENSING.md](../LICENSING.md)): BSD, MIT-style and public domain are permissive; the LGPL
  parts are used through dynamic loading of an unmodified library; the MPL-2.0 part (OSDI) is
  file-level copyleft, which an unmodified, separately loaded binary meets by shipping the license
  and pointing to the ngspice source, so it is treated like the LGPL parts. `libomp140.x86_64.dll`
  is redistributable with Visual Studio. Packages (M5.5) ship ngspice's `COPYING` and say where its
  source is.

### Found while writing the engine (M1.2)

- **No way to skip init files.** `ngSpice_nospinit()` and `ngSpice_nospiceinit()` are exported,
  and the header says to call them before `ngSpice_Init()`, but in ngspice 46 both crash there
  (they set a variable before `ngSpice_Init()` has set up ngspice's state). The engine does not
  call them, so ngspice reads `spinit` (not shipped: it warns that it cannot find it) and a user's
  `.spiceinit` if there is one. This is a hole in determinism on a machine that has a
  `.spiceinit`; revisit with the next ngspice pin.
- **Ignored lines.** ngspice drops a netlist line it cannot read with a warning ("'r1 in' is not
  a valid resistor instance line, ignored!") and simulates the rest. The engine treats that
  warning as a load error, like an `Error` line.
- **`BGThreadRunning` is inverted.** ngspice 46 calls it with `false` when its background thread
  starts and `true` when it ends, the opposite of what the header says.
- **`bg_halt` waits** for the background thread to stop before it returns ("Background thread
  stopped with timeout = 1"); how long that takes is measured with the live-change latency in
  M1.5.

## Alternatives considered

- **ngspice-47 plus `sndfile.dll` and `samplerate.dll` from the full package.** Newest release, but
  a second, larger archive to pin and two more shipped dependencies (libsndfile is LGPL-2.1,
  libsamplerate BSD-2-Clause) for audio features Proton does not use. Revisit when an official DLL
  package loads on its own.
- **Building ngspice from source.** Full control (could leave out OSDI and the LGPL parts), but a
  C toolchain and an autotools or Visual Studio project in Proton's build for no gain in the MVP.
- **Linking `ngspice.lib` at build time.** Simpler calls, but the app would not start without the
  DLL and could not reload it after a fatal error (ADR 0003).
- **Running ngspice in a helper process.** Survives every crash and allows several instances, but
  needs inter-process streaming. Kept as the fallback if reloading the DLL turns out not to be
  enough (PLAN.md risks, M1.4).

## Consequences

- A fresh clone needs `tools/fetch-ngspice.ps1` before the build has ngspice; without it the
  build and every non-ngspice test still work, on Windows and Linux.
- Linux builds never have ngspice. Engine code that needs it is Windows only until a Linux
  loading path is added.
- CI downloads from SourceForge only when the cache misses (cache key: the fetch script, so a new
  pin invalidates it).
- Updating ngspice means a new pin and SHA-256 in the script, a check that the DLL loads on its
  own, a new license review here, and the M1.3 validation tests passing.
- Memory growth over repeated runs is measured in M1.4 and recorded here.
