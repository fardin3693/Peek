# Peek

Peek is an open-source, lightweight file previewer for Linux, inspired by macOS Quick Look. The goal is to select a file in your file manager, press a key, and get an instant floating preview, then dismiss it just as quickly.

Target platform: Linux desktops, developed first on Omarchy (Arch Linux, Hyprland, Wayland).

## Status

**Milestone 1: environment setup and project scaffolding.**

Implemented:

- CMake/Qt 6 Widgets build with a `peek` executable.
- The executable initializes Qt, prints a startup message and exits. It has `--help` and `--version`.
- CTest smoke tests (the program starts, `--version` works).
- Debug, Release and ASan/UBSan presets.

Not implemented yet: the preview window, file-format handlers, file-manager integration.

## Dependencies

Required:

| Tool | Notes |
| --- | --- |
| C++17 compiler | GCC or Clang |
| CMake >= 3.21 | Arch: `cmake` |
| Ninja | Arch: `ninja` (presets use the Ninja generator) |
| Qt 6 (>= 6.2) Widgets | Arch: `qt6-base` |
| Qt 6 Wayland platform plugin | Arch: `qt6-wayland` (for native Wayland sessions) |

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
./build/release/peek

# Debug (development)
cmake --preset debug
cmake --build --preset debug
./build/debug/peek

# Run tests
ctest --preset debug
```

Without presets:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Development workflow

- Develop and debug with the `debug` preset; use `release` to judge performance.
- Run the `asan` preset (`cmake --preset asan && cmake --build --preset asan && ctest --preset asan`) before submitting changes that touch memory handling.
- Format with `clang-format -i src/*.cpp`.
- Static analysis: configure any preset (it exports `compile_commands.json`), then run `clang-tidy -p build/debug src/main.cpp`.
- Keep changes small and update `docs/architecture.md` when module boundaries change.

See [docs/architecture.md](docs/architecture.md) for the intended design.

## License

Not yet chosen.
