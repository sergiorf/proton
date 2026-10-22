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

### Fatal errors and memory (M1.4)

Measured with ngspice 46 on Windows 11, private committed memory of the process
(`PrivateUsage`), by the `ngspice_soak` test (Debug and Release give the same within noise) and
by scratch probes calling the DLL directly.

- **What is fatal.** Errors found while ngspice reads the netlist: a missing `.include` or `.lib`
  file, unmatched `.subckt` and `.ends`, a `.param` or expression it cannot evaluate, and a
  `quit` in a `.control` block. ngspice prints "ngspice.dll cannot recover and awaits to be reset
  or detached", calls `ControlledExit` (unload flag false) on the calling thread from inside
  `ngSpice_Circ`, which then returns 1. No exit on ngspice's background thread was found, but the
  engine handles one: it waits up to 2 s for the thread to end before freeing the DLL, and
  otherwise leaves ngspice dead with an error instead of freeing code that still runs.
  Proton's netlists (`schematic::buildNetlist()`) write none of these constructs.
- **Recovery.** `ngSpice_Reset()`, then `FreeLibrary`, then `LoadLibrary` and `ngSpice_Init()`
  again on the next `load()`. A scratch probe ran 480 exit-and-reload cycles over four kinds of
  fatal netlist, each followed by a valid run, without a failure. `ngSpice_Reset()` alone,
  without the reload, crashes the next run (access violation), so it is only a clean-up before
  freeing the DLL.
- **Memory per reload.** `ngspice.dll` links its C runtime statically, and that runtime allocates
  from the process heap, so whatever ngspice has not freed stays when the DLL goes. Freeing the
  DLL alone left about 125 KiB per reload; with `ngSpice_Reset()` first, 3 to 4.5 KiB per
  fatal-error cycle (the reload and a valid run together).
- **Memory per load.** Over 1000 load/reset cycles after 100 warm-up cycles, memory grows
  linearly: 0.5 to 0.6 KiB per cycle when each run ends at its stop time, 1.1 KiB when each run is
  halted with `bg_halt` (the probe). The growth does not depend on the netlist: 4 lines, 22 lines, or
  4 KB of comments all give 0.5 KiB per `ngSpice_Circ` followed by `remcirc` and `destroy all`.
  It is ngspice's own leak. Capping the command history (`set history`) does not change it, and a
  reload every 100 loads only cuts it from 1.1 to 0.7 KiB per load, so the engine does not
  reload periodically.
- **Decision: accepted.** Growth is about 1 MiB per thousand runs: a run started every second for
  four hours is about 15 MiB. The `ngspice_soak` test fails above 2 KiB per cycle and 16 KiB per
  reload, which catches anything worse (such as plots no longer being freed). If live changes
  restart runs far more often than that (D3, M1.5), the helper process below gives the memory
  back by ending the process.
- **`bg_halt` takes about 114 ms**, nearly all of it a sleep inside ngspice (it polls its thread
  in 100 ms steps); a run's first point takes about 17 ms. Halting is part of every `load()` of a
  running engine, so this goes into the latency budget of M1.5.

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
  needs inter-process streaming. Reloading the DLL is enough to survive fatal errors (M1.4); kept
  as the fallback for ngspice's per-load leak and for crashes that are not a controlled exit.

## Consequences

- A fresh clone needs `tools/fetch-ngspice.ps1` before the build has ngspice; without it the
  build and every non-ngspice test still work, on Windows and Linux.
- Linux builds never have ngspice. Engine code that needs it is Windows only until a Linux
  loading path is added.
- CI downloads from SourceForge only when the cache misses (cache key: the fetch script, so a new
  pin invalidates it).
- Updating ngspice means a new pin and SHA-256 in the script, a check that the DLL loads on its
  own, a new license review here, and the M1.3 validation tests passing.
- A process that runs ngspice grows by about 1 KiB per run and 4 KiB per fatal error (M1.4,
  above).
