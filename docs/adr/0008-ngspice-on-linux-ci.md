# ADR 0008: ngspice on Linux for the sanitizer CI jobs (decision D9)

- **Status:** accepted
- **Date:** 2026-10-24
- **Plan task:** M1.8, decision D9

## Context

The sanitizers run only on Linux: MSVC has ASan but no TSan or UBSan (ADR 0003). `NgspiceEngine`
holds Proton's most delicate threading code, a queue shared with ngspice's own thread, a halt
source read on that thread, and pausing for `alter`, yet until now it was built only on Windows
(ADR 0002), so no sanitizer ever saw it. Running it on Linux needs a Linux libngspice, which is a
new dependency, if only for CI.

What is known:

- **Source release.** `ngspice-46.tar.gz` from SourceForge, the release ADR 0002 pins, SHA-256
  `a0d1699af1940b06649276dcd6ff5a566c8c0cad01b2f7b5e99dedbb4d64c19b`; its MD5 matches the one
  SourceForge publishes. It ships its `configure` script and generated parsers, so a C compiler and
  make are enough: `configure --with-ngshared` builds `libngspice.so` in about 80 s on 8 cores,
  with the same features as the Windows DLL (KLU, XSPICE, OSDI, OpenMP through GCC's `libgomp`).
- **Distribution packages** (`libngspice0-dev`) carry whatever version the distribution ships,
  older than 46 on the CI runner's Ubuntu. The engine and its tests depend on ngspice 46's
  behaviour (the inverted `BGThreadRunning`, which lines it warns about, tolerances measured on it).
- **Loading.** The engine's only Windows-specific code is loading the library (`LoadLibraryExW`,
  `GetProcAddress`, `FreeLibrary`, the executable's directory); `dlopen`, `dlsym`, `dlclose` and
  `/proc/self/exe` do the same on Linux. After a fatal error, `dlclose` and `dlopen` give ngspice
  fresh state as `FreeLibrary` and `LoadLibrary` do: the fatal-error recovery tests pass.
- **Results** (WSL, Ubuntu, gcc's sanitizers): every ngspice test passes under TSan with no report.
  Under ASan, LeakSanitizer reports ngspice's known per-load leak (ADR 0002): every leaked
  allocation is made by ngspice's allocator, `tmalloc`, or by `libgomp`. A leak planted in the
  engine's data callback, which runs with ngspice's frames below it, is still reported with
  suppressions on those two.

## Decision

**Decision D9 (2026-10-24):** the Linux CI jobs build the pinned ngspice 46 from its source release.

- `tools/fetch-ngspice.sh` downloads the source, refuses it if the SHA-256 differs, builds it with
  `--with-ngshared`, and keeps only `lib/libngspice.so` and `include/ngspice/sharedspice.h` in
  `third_party/ngspice` (no code models). CI caches the result, keyed on the script, and
  configures with `PROTON_REQUIRE_NGSPICE=ON`, so every CI job runs the ngspice tests.
- `NgspiceEngine` loads `libngspice.so` from the executable's directory on Linux, as it loads
  `ngspice.dll` on Windows.
- The ASan preset suppresses leaks allocated through `tmalloc` and `libgomp`
  (`tests/lsan.supp`), never whole modules.
- The memory soak test stays Windows only: it reads the process's committed memory, which under
  the sanitizers measures their allocators.
- The Linux library is a CI tool. It is never shipped; Proton on Linux is not a product yet.

## Alternatives considered

- **The distribution's package:** no build, but a different ngspice version, so tests would need
  to follow two versions' behaviour, and an Ubuntu upgrade could change it underneath CI.
- **Building ngspice with the sanitizers too:** would also check ngspice's own code, which is not
  Proton's to fix, at the cost of a longer, more fragile build. Proton's side of the exchange is
  what needs checking.
- **Deferring:** the engine's threading would stay unchecked until a crash shows up in the app.

## Consequences

- A cache miss costs the Linux jobs a minute or two to build ngspice.
- `tools/fetch-ngspice.ps1` replaces `third_party/ngspice` as a whole, so in a checkout shared
  between Windows and WSL it removes the Linux library; run `tools/fetch-ngspice.sh` again.
- Updating the ngspice pin now means both scripts and both hashes (ADR 0002's checklist).
- Linux becomes a working development platform for everything but the app, ngspice included.
