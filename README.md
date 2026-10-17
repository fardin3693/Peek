# Peek

Peek is an open-source, lightweight file previewer for Linux, inspired by macOS Quick Look. The goal is to select a file in your file manager, press a key, and get an instant floating preview, then dismiss it just as quickly.

Target platform: Linux desktops, developed first on Omarchy (Arch Linux, Hyprland, Wayland).

## Status

**Native previews for image, PDF, Markdown, plain text, video, audio, and ZIP archive files, plus a minimal Nautilus selection handoff.**

Peek is a native C++17 application built on Qt 6 Widgets (see [docs/architecture.md](docs/architecture.md)). `peek PATH` validates a local file or directory and opens one decorated preview window; Escape or the ordinary window close action dismisses it and exits Peek. With no arguments, Peek prints version/platform information and brief usage, then exits successfully. `--help` and `--version` remain available.

For files, the window dispatches by extension in a fixed order — PDF, then Markdown, then plain text, then audio, then video, then archive, then image — and each handler owns its status message. A file claimed by one handler never falls through to an unrelated preview. Media playback streams through the Qt Multimedia backend and never loads whole files into memory.

### Image

- Decoded with Qt's `QImageReader` (EXIF auto-transform). Which formats can be read depends on the installed Qt image plugins; PNG, JPEG, and WebP are covered by automated tests.
- The window shows the file name, absolute path, dimensions, and detected format. The image scales to the window while preserving aspect ratio (`KeepAspectRatio`, fast previews during resize with one debounced smooth pass) and updates on resize.
- Large images are downscaled at decode time (longest side capped at 2048 px via the shared bounded decoder; size-unknown formats get a bounded request plus a post-decode shrink) and the initial window is capped at 1024×768.
- Files no image plugin can read show `No image preview is available for this file.`; unreadable or corrupt images show `Could not load an image preview of this file (…).` instead of crashing.

### PDF (requires the Poppler backend)

- A file counts as PDF only when it has a `.pdf` suffix (case-insensitive) **and** a `%PDF-` magic header; a bare extension never suffices.
- Renders pages on demand at up to 2048 px on the longest side (DPI capped at 300). Only the current page is rendered at open time; later pages render in a background thread with a `loading…` status while work is pending. The status line reads `PDF document — page X of N — W × H px`.
- Compact previous/next controls with a `Page X of N` indicator (previous disabled on the first page, next on the last); plain `Left`/`Right`/`PageUp`/`PageDown` keys turn pages. The full list lives in [docs/keyboard-shortcuts.md](docs/keyboard-shortcuts.md).
- Zoom in/out in 25% steps from 25% to 400% of the fitted size (100% fills the preview area) with a percentage readout, plus `Fit page` and `Fit width` modes; zooming rescales the cached page image and never reloads the document. Display pixmaps are capped at 4096 px on the longest side. A 3-entry page-image cache keeps recent pages; stale renders from rapid navigation are discarded before rasterizing.
- Password-protected, corrupt, empty, and otherwise unreadable documents stay on the PDF status line (for example, `Could not load a PDF preview of this file (password-protected (password required)).`); they never fall through to the image preview.
- Trust note: Poppler/FFmpeg are trusted decoder C/C++ code decoding untrusted input — parser CVEs keep arriving upstream. PDF preview is an opt-out risk surface: configure with `-DPEEK_WITH_PDF=OFF` to disable it, and track Poppler security releases.

### Plain text

- Recognized by extension only (case-insensitive): `txt json jsonl yaml yml toml csv tsv log xml html css c h cpp hpp py sh lua js ts rs`. Markdown (`md markdown`) is rendered as formatted Markdown, not plain text. Unknown extensions are never decoded as text.
- Limits: at most 2 MiB (checked before reading, and the read itself is bounded so a file that grows mid-read still cannot exceed it); strict UTF-8 only; embedded NUL bytes rejected; a leading UTF-8 BOM is stripped. Empty files succeed and report `Text file — empty (N bytes)`.
- Shown read-only in a fixed-width, horizontally scrolling view with a `Text file — L lines — B bytes` status line. Text is never interpreted as rich text or HTML, and never executed.

### Markdown (formatted, no new dependencies)

- Recognized by extension only (case-insensitive): `md markdown`. Rendered through Qt's built-in Markdown support (`QTextDocument::setMarkdown`, GitHub dialect) into a read-only view with a `Markdown file — L lines — B bytes` status line; empty files report `Markdown file — empty (N bytes)`. Markdown files never render as plain text, and invalid/unreadable Markdown stays on the Markdown status line (`Could not load a Markdown preview of this file (…).`).
- Supported: headings, paragraphs, ordered/unordered lists, bold/italic, inline code and fenced code blocks, blockquotes, links (shown, never followed), horizontal rules, and GitHub-style tables. Unsupported: raw embedded HTML (shown escaped as literal text, never rendered), task-list checkboxes, footnotes, strikethrough extensions beyond Qt's dialect, embedded images fetched from the network, and automatic link opening.
- Security: treated as untrusted input — loaded through the same bounded 2 MiB, strict-UTF-8, NUL-rejecting path as plain text; rendered with `MarkdownNoHTML` so raw HTML cannot inject markup or scripts; the view never follows links (`openLinks`/`openExternalLinks` off, search paths limited to the document directory) and never fetches remote images or stylesheets (non-local resource loads resolve empty; local images render only from inside the document directory through the shared 2048 px bounded decoder — relative URLs resolve against the document, never the working directory, and absolute `file:` URLs outside it render as placeholders).
- Same 640×480 window sizing as text; Escape dismissal unchanged.

### Video (streaming, requires the Qt Multimedia backend)

- Recognized by extension only (case-insensitive): `mp4 mkv webm mov avi m4v`. Unknown extensions are never treated as video.
- Streams through `QMediaPlayer` + `QVideoWidget` + `QAudioOutput`; never loads the whole file into memory. Starts paused with a `Video preview — paused.` status line, a Play/Pause button, a seek slider, and an `m:ss / m:ss` position indicator. Closing the window (or destroying the player) stops playback and clears the source; video and audio players are independent instances.
- Unsupported codecs, missing backends, and invalid files stay on the video controls as `Could not play this video (…)` — never a crash, never a fallback to an unrelated preview. Actual codec support depends on the Qt FFmpeg backend at runtime.

### Audio (streaming, requires the Qt Multimedia backend)

- Recognized by extension only (case-insensitive): `mp3 wav flac ogg opus m4a aac wma`. Unknown extensions stay on their existing preview paths. The audio and video extension sets are disjoint (`m4a` ≠ `m4v`).
- Same streaming `QMediaPlayer` + `QAudioOutput` backend as video but with no video output: a centered label plus Play/Pause button, seek slider, and `m:ss / m:ss` duration indicator, starting paused. Closing stops playback and clears the source. Failures stay on the audio controls as `Could not play this audio (…)`; without the backend, audio extensions report `No audio preview is available for this file (Qt Multimedia is not installed).`

### Archive (ZIP listing, no new dependencies)

- Recognized by extension only (case-insensitive): `zip`. Unknown extensions stay on their existing preview paths. No archive library is used: listing parses only the end-of-central-directory record and the central directory with Qt Core, so entry contents are never decompressed and no backend is needed.
- Shows a read-only table in archive order (name, file/directory type, uncompressed and compressed sizes) with a `ZIP archive — N entries — T bytes uncompressed — S bytes compressed` status line; empty archives report `ZIP archive — empty (0 entries)`. Directories show no sizes; encrypted entries are listed with an `(encrypted)` marker and never decrypted.
- Security: archives are treated as untrusted input — entry contents are never read, extracted, executed, or opened, and nothing is written to the filesystem, so archived paths cannot escape anywhere. Corrupt, truncated, ZIP64, multi-disk, and over-limit archives stay on the archive status line (`Could not load an archive preview of this file (…).`). Limits: at most 10000 entries enumerated (larger archives show `showing first L of N`), central directory capped at 8 MiB, entry names capped at 4096 bytes, totals accumulated in saturating 64-bit integers.
- Same 640×480 window sizing as text; Escape dismissal unchanged. TAR, 7z, and RAR are not supported (libarchive remains the candidate backend if broader format support is ever requested).

### Unsupported files and directories

- Directories show the `Directory preview not implemented yet.` placeholder; their contents are not scanned.
- Files matching no handler reach the image path: anything `QImageReader` cannot read shows `No image preview is available for this file.`
- Without the Multimedia backend, media extensions are still recognized and report `No video/audio preview is available for this file (Qt Multimedia is not installed).` instead of reaching the image path with a misleading message. Without the Poppler backend, `pdf_preview.cpp` is not compiled and `.pdf` files fall through to the text/image/fallback path (see Installation).

### Not yet implemented

Office documents and syntax highlighting; Space-to-preview/toggle and single-instance behavior (the Ctrl+Alt+Shift+F12 trigger in [docs/quick-look-trigger.md](docs/quick-look-trigger.md) reuses this window as-is: repeats stack, nothing re-targets). The window is an ordinary decorated top-level widget, not yet a compositor-managed floating Quick Look window. Positioning and focus behavior on a real Wayland desktop still need manual verification; audible PipeWire output and real-time duration readouts likewise need a Wayland-session check. Only ZIP archives are listed; TAR, 7z, and RAR stay unsupported.

Peek reads file contents only to decode the selected file for its preview (image pixels, PDF pages, text bytes, media stream, archive listing headers); it never enumerates directories. Python is not required by the native application; Python 3 (standard library only) is used by the optional Nautilus Script.

## Dependencies

Required:

| Tool | Notes |
| --- | --- |
| C++17 compiler | GCC or Clang |
| CMake >= 3.21 | Arch: `cmake` |
| Ninja | Arch: `ninja` (presets use the Ninja generator) |
| Qt 6 (>= 6.2) Widgets | Arch: `qt6-base` |
| Qt 6 Wayland platform plugin | Arch: `qt6-wayland` (for native Wayland sessions) |

Optional preview backends (auto-detected at configure time; absence disables the feature, never fails the build):

| Feature | Dependency | CMake switch | When disabled |
| --- | --- | --- | --- |
| PDF preview | Poppler C++ (`poppler-cpp` via pkg-config) | `-DPEEK_WITH_PDF=OFF` | `pdf_preview.cpp` is not compiled and `.pdf` files fall through to the text/image/fallback path |
| Video and audio preview | Qt Multimedia + MultimediaWidgets (FFmpeg backend at runtime) | `-DPEEK_WITH_MULTIMEDIA=OFF` | Audio/video extensions are still recognized but show `No audio/video preview is available for this file (Qt Multimedia is not installed).` |

The switches force a feature off even when its dependency is present. Leaving them `ON` (the default) enables the feature only when the dependency is actually found. The `--help` description and the no-argument summary line list whichever categories are compiled in (verified: full build reports "image files, PDFs, videos, audio files, text files and directories").

Test-only dependencies (discovered only when `PEEK_BUILD_TESTS=ON`, the default):

| Tool | Used by |
| --- | --- |
| Qt 6 Test (included in Arch's `qt6-base`) | Native `peek_preview` suite |
| Python >= 3.9 (Arch: `python`) | Nautilus handoff (`peek_nautilus`) and Task C lab guard suites |

FFmpeg is also test-only and optional: the audio/video suites synthesize tiny fixtures with `ffmpeg` at test time and skip cleanly when it is absent. To build the native application without tests or a Python requirement, configure with `-DPEEK_BUILD_TESTS=OFF`.

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

# Run the Debug suite
ctest --preset debug

# Release suite (same tests, optimized build)
cmake --preset release
cmake --build --preset release
ctest --preset release

# Sanitizer suite (AddressSanitizer + UndefinedBehaviorSanitizer)
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

Sanitizer leak checks are not on by default: run `env ASAN_OPTIONS=detect_leaks=1 ctest --preset asan` for the leak pass (needs an environment that permits LeakSanitizer's process inspection; do not claim leak checks passed if it cannot run). An ASan pass without `detect_leaks=1` is not a leak pass. `tests/lsan.supp` narrowly suppresses only the known Qt-internal `QVideoWindow::event` leak from a *shown* `QVideoWidget` on the offscreen platform (probe-verified Peek-independent; audio previews create no `QVideoWidget` and are unaffected) — Peek-owned leaks still fail the gate.

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

Failures report a diagnostic on stderr and create no preview window. Existence/type validation does not guarantee readability or that a path will remain unchanged; decoding re-reads the file and reports failures in the window (image, PDF, text, and media handlers) instead of crashing.

## Nautilus selection handoff (diagnostic fallback)

See [docs/nautilus-script.md](docs/nautilus-script.md) for user-local installation and Omarchy acceptance tests. The Script belongs at `~/.local/share/nautilus/scripts/Peek` on the default desktop setup. It decodes only local `file:` URIs and uses the first item in Nautilus-provided order for multiple selections; directories show the directory placeholder, and image, PDF, Markdown, text, video, audio, and archive files render through the same native preview window as the CLI. The keyboard trigger is user-local configuration documented in [docs/quick-look-trigger.md](docs/quick-look-trigger.md) (Ctrl+Alt+Shift+F12 forwarding to this Script; installed manually, never by the build). There is no Space-to-preview binding, daemon, single-instance behavior, selection cache, polling, clipboard, or desktop configuration change involved.

## Development workflow

- Develop and debug with the `debug` preset; use `release` to judge performance.
- Run the `asan` preset (`cmake --preset asan && cmake --build --preset asan && env ASAN_OPTIONS=detect_leaks=1 ctest --preset asan`) before submitting changes that touch memory handling. LeakSanitizer needs an environment that permits its process inspection; do not claim leak checks passed if it cannot run.
- Tests use `QT_QPA_PLATFORM=offscreen` and an empty `QT_QPA_PLATFORMTHEME` only in child test processes. They do not require a desktop session or change desktop configuration. Valid-CLI subprocess tests check event-loop lifetime and then terminate the child; leak checks for graceful widget destruction are covered by the native test process, not these terminated children.
- Format with `clang-format -i src/*.cpp src/*.h tests/*.cpp`.
- Static analysis: configure any preset (it exports `compile_commands.json`), then run `clang-tidy -p build/debug src/main.cpp`. The `peek_tidy` build target (`cmake --build --preset debug --target peek_tidy`) runs the same committed `.clang-tidy` config as a pre-submit gate. Developer rebuilds use `ccache` automatically when it is installed (CMake `CMAKE_CXX_COMPILER_LAUNCHER`, never a hard requirement).
- Keep changes small and update `docs/architecture.md` when module boundaries change.

See [docs/architecture.md](docs/architecture.md) for the implemented design and planned direction.

## License

Peek is licensed under the **GNU General Public License version 3 only** (see [`LICENSE`](LICENSE), SPDX-License-Identifier: `GPL-3.0-only`). Copyright (C) 2026 fardin3693.

GPLv3 permits commercial use, but redistribution and derivative works are subject to its terms: conveying copies or modified versions requires making the source available and licensing the work under the same license. Private modifications that are never distributed carry no publication obligation.
