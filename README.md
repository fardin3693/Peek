# Peek

Peek is an open-source, lightweight file previewer for Linux, inspired by macOS Quick Look. The goal is to select a file in your file manager, press a key, and get an instant floating preview, then dismiss it just as quickly.

Target platform: Linux desktops, developed first on Omarchy (Arch Linux, Hyprland, Wayland).

## Status

**Milestone 2, Task A: native CLI and placeholder preview window.**

Implemented:

- CMake/Qt 6 Widgets build with a `peek` executable.
- `peek PATH` validates a local file or directory and opens a small native Qt window showing its name, absolute path, and placeholder status.
- Escape or the ordinary window close action dismisses the window and exits Peek.
- With no arguments, Peek prints startup information and brief usage, then exits successfully. `--help` and `--version` remain available.
- CTest smoke tests plus native path-validation, widget-dismissal, and CLI argument/error tests.
- Debug, Release and ASan/UBSan presets.

Not implemented yet: file rendering, file-format handlers, Nautilus/Hyprland integration, Space-to-preview/toggle, and single-instance behavior. The window is an ordinary decorated top-level widget, not yet a compositor-managed floating Quick Look window. Positioning and focus behavior on a real Wayland desktop still need manual verification.

Peek does not read file contents or enumerate directories in this milestone. Python is not required by the application.

## Dependencies

Required:

| Tool | Notes |
| --- | --- |
| C++17 compiler | GCC or Clang |
| CMake >= 3.21 | Arch: `cmake` |
| Ninja | Arch: `ninja` (presets use the Ninja generator) |
| Qt 6 (>= 6.2) Widgets | Arch: `qt6-base` |
| Qt 6 Wayland platform plugin | Arch: `qt6-wayland` (for native Wayland sessions) |

Tests additionally require Qt 6 Test (included in Arch's `qt6-base`). It is discovered only when `PEEK_BUILD_TESTS=ON` (the default). To build the application without tests, configure with `-DPEEK_BUILD_TESTS=OFF`.

Optional development tools (none are required to build):

| Tool | Purpose | Arch package |
| --- | --- | --- |
| clang-format | Formatting (`.clang-format`) | `clang` |
| clang-tidy | Static analysis | `clang` |
| GDB | Debugging | `gdb` |
| ASan / UBSan | Memory and UB diagnostics (ship with GCC and Clang) | `gcc` |
| Valgrind | Extra memory diagnostics | `valgrind` |
| cppcheck | Extra static analysis | `cppcheck` |
| ccache | Faster rebuilds | `ccache` |

## Build and run

```sh
# Release (ordinary performance testing)
cmake --preset release
cmake --build --preset release
./build/release/peek README.md

# Debug (development)
cmake --preset debug
cmake --build --preset debug
./build/debug/peek README.md

# Run tests
ctest --preset debug
```

Without presets:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## CLI contract

```sh
peek /absolute/path/to/file
peek "relative path with spaces.txt"
peek -- "-filename.txt"
peek --help
peek --version
```

Exactly one path is accepted. Relative paths are prefixed with the invocation's working directory without lexically removing `.` or `..`; cleaning those components could change the meaning of a path through a symlink. Valid symlinks are followed for type validation, but their path is retained for display. Directories show a distinct placeholder; their contents are not scanned. All supported filenames are passed as argv data, including quotes, Unicode, shell metacharacters, and embedded newlines. Quote operands appropriately in your shell; `--` ends option parsing, including for filenames that match Qt options such as `-platform`. Peek does not accept Qt's command-line startup options; choose a Qt platform through `QT_QPA_PLATFORM` when needed.

Remote URIs are not supported; operands are filesystem paths. No file-manager selection is inferred when no path is supplied. Each valid invocation starts its own window/process and stays running until that window is dismissed.

| Exit code | Meaning |
| --- | --- |
| `0` | Normal window dismissal, no-argument informational startup, help, or version |
| `1` | Missing/inaccessible path, broken symlink, or unsupported filesystem object (e.g. FIFO/device/socket) |
| `2` | CLI usage error: empty operand, multiple operands, or unknown option |

Failures report a diagnostic on stderr and create no preview window. Existence/type validation does not guarantee readability or that a path will remain unchanged; rendering must revalidate access when it is implemented.

## Development workflow

- Develop and debug with the `debug` preset; use `release` to judge performance.
- Run the `asan` preset (`cmake --preset asan && cmake --build --preset asan && env ASAN_OPTIONS=detect_leaks=1 ctest --preset asan`) before submitting changes that touch memory handling. LeakSanitizer needs an environment that permits its process inspection; do not claim leak checks passed if it cannot run.
- Tests use `QT_QPA_PLATFORM=offscreen` and an empty `QT_QPA_PLATFORMTHEME` only in child test processes. They do not require a desktop session or change desktop configuration. Valid-CLI subprocess tests check event-loop lifetime and then terminate the child; leak checks for graceful widget destruction are covered by the native test process, not these terminated children.
- Format with `clang-format -i src/*.cpp src/*.h tests/*.cpp`.
- Static analysis: configure any preset (it exports `compile_commands.json`), then run `clang-tidy -p build/debug src/main.cpp`.
- Keep changes small and update `docs/architecture.md` when module boundaries change.

See [docs/architecture.md](docs/architecture.md) for the intended design.

## License

Not yet chosen.
