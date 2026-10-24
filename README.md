# proton
Proton is a desktop-based virtual electronics laboratory that lets users design, simulate, test, and troubleshoot circuits using realistic instruments such as oscilloscopes, multimeters, signal generators, and RF equipment for education and prototyping.

## Build and run

The app runs on Windows. You need:

- Visual Studio 2022 or newer, with the "Desktop development with C++" workload
- CMake 3.25 or newer
- Qt 6.8 or newer (6.12 recommended) from the
  [official installer](https://www.qt.io/download-qt-installer-oss), with the MSVC 2022 64-bit kit

Tell the build where Qt is, once (then open a new terminal):

```
setx QT_ROOT_DIR C:\Qt\6.12.0\msvc2022_64
```

Fetch the pinned ngspice simulator, once:

```
powershell -ExecutionPolicy Bypass -File tools/fetch-ngspice.ps1
```

Build, test and run:

```
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
build\windows-msvc-debug\src\app\Debug\proton.exe
```

Use `windows-msvc-release` in place of `windows-msvc-debug` for an optimised build (the app is
then in `src\app\Release`). In VS Code, with the CMake Tools extension, pick the
`windows-msvc-debug` preset and launch the `proton` target.

On Linux, everything but the app builds and tests, including under the sanitizers; see
[CONTRIBUTING.md](CONTRIBUTING.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Outside contributions need a signed Contributor License
Agreement.

## License

Proton is free software under the [GNU Affero General Public License v3.0 only](LICENSE)
(AGPL-3.0-only). A commercial license is available for use that the AGPL does not allow, such as
shipping a closed-source product built on Proton; contact the copyright holder. Dependency
licenses are listed in [docs/LICENSING.md](docs/LICENSING.md).
