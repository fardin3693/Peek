# Preview-Provider Contract (implemented)

This document describes the minimal provider interface Peek implements: text,
Markdown, image, PDF, audio and archive providers follow it, with video still
dispatched inline (see source of truth below). The original proposal text is
preserved where it still matches the code; where the current code leaves a
question open, the question is recorded instead of answered by guessing.

Goals: keep the contract C++17 / Qt 6 with no new dependencies, preserve the
existing preview behavior exactly, and avoid plugin loaders, runtime
registries, async frameworks, caches, and daemons.

## 1. Provider responsibility

A provider answers one question — "is this file mine?" — and, if so, renders
its preview into the existing preview surface. Concretely, a provider:

- decides support from the validated `PreviewRequest` (path string plus the
  `isDirectory` flag from `validatePreviewPath()`), without decoding content;
- on a claim, builds its view widgets, sets the shared status label text, and
  owns teardown of any resources it acquired (today: `PreviewWindow`'s
  destructor stops each `QMediaPlayer` and clears its source,
  `src/preview_window.cpp` lines ~62–79).

Two phases, not one universal parser: a cheap `supports()` check first, then
`render()` only for the single claiming provider. This mirrors what the code
already does (`isPdfFile()` before `showPdfFile()`, `isTextFile()` before
`showTextFile()`, and so on) and keeps identification free of decode costs.

Proposed shape (illustrative, not declared anywhere):

```cpp
enum class Support { No, Yes };

struct Provider {
    const char *name;                                   // e.g. "pdf", "text"
    Support (*supports)(const QString &path);           // cheap, bounded (see §2)
    // Renders into the shared layout, sets statusLabel, returns true when a
    // preview is displayed. A render failure still returns control with a
    // status message set; it never throws and never re-dispatches (see §6).
    bool (*render)(const QString &path, QLabel *statusLabel, QVBoxLayout *layout);
};
```

Function pointers (or small stateless structs) are deliberate: providers need
no identity, no state, and no base class with virtual dispatch. Anything
needing per-preview state (players, pixmaps) already lives in `PreviewWindow`
members today and stays there until a later step justifies moving it.

## 2. Identification: extensions plus bounded signatures

- Default rule: case-insensitive extension match, as `isTextFile()`
  (`src/text_preview.cpp` lines ~78–91, 24 extensions), `isAudioFile()`
  (8 extensions), and `isVideoFile()` (6 extensions) do today via
  `QFileInfo(path).suffix().toLower()`. Unknown extensions are never claimed.
- Bounded signature checks are allowed where the extension alone is
  insufficient, but capped at a few header bytes read read-only: `isPdfFile()`
  requires the `.pdf` suffix **and** a 5-byte `%PDF-` magic read
  (`src/pdf_preview.cpp` lines ~30–40). That 5-byte open is the template for
  any future signature check.
- Forbidden at identification time: full decodes, whole-file reads, MIME
  database probing, and directory enumeration. Opening a file merely to ask
  "who can show this?" must stay an exception limited to fixed-size magic
  reads, never a general probe.

## 3. Priority and dispatch

Dispatch stays a deterministic, compile-time-ordered list — a fixed array of
`Provider` entries, not a runtime registry and not scored matching. Order is
today's verified chain (`src/preview_window.cpp` lines ~121–163):

1. PDF (`PEEK_WITH_PDF` only), 2. text, 3. audio, 4. video, 5. image
   (`QImageReader` probe), 6. generic fallback message.

Rules the dispatch layer enforces:

- First `Yes` wins; a claimed file is never offered to a later provider.
- A provider that is compiled out (optional backend disabled) is absent from
  the list; ordering of the rest is unchanged.
- Render failure does not re-dispatch (see §6); the claiming provider's
  message stands.
- Audio/video extension sets are disjoint (`m4a` vs `m4v`), so their relative
  order is arbitrary but must stay fixed once written down.
- The directory placeholder (`Directory preview not implemented yet.`) stays
  in the dispatch layer, not in any provider: it is not a file format.

## 4. Optional dependencies

Providers requiring an optional backend exist only when it is enabled:

- PDF provider: compiled only with `PEEK_WITH_PDF`; without Poppler,
  `pdf_preview.cpp` is not built and `.pdf` files fall through to the
  text/image/fallback path (current behavior, verified in the constructor's
  `#ifdef` at lines ~127–132).
- Audio/video providers: extension matching (`isAudioFile`/`isVideoFile`) is
  backend-independent and always built, but playback widgets exist only with
  `PEEK_WITH_MULTIMEDIA`. Without the backend, the dispatch layer keeps
  today's explicit unsupported messages (`No audio/video preview is available
  for this file (Qt Multimedia is not installed).`, lines ~146–160) instead of
  reaching the image path with a misleading message.

Design decision: the "backend missing" message is a dispatch-layer stub, not
a provider capability. A provider is either present (backend available) or
absent (compiled out); there is no `BackendMissing` support level, so no
provider code ever runs without its dependency.

## 5. Resource safety

The contract codifies today's bounds as per-provider obligations:

- Text: pre-check size plus a bounded limit+1 read, capped at
  `kMaxTextFileBytes` (2 MiB, `src/text_preview.h` lines ~8–11), so mid-read
  growth cannot escape the cap.
- Images and PDF: longest decode/render side capped at 2048 px
  (`kMaxDecodeDimension`, `kMaxRenderPixels` with DPI capped at 300 and page
  boxes pre-rejected in `src/pdf_preview.cpp` lines ~17–26); initial window
  capped at 1024×768.
- Media: streaming `QMediaPlayer` only — whole-file loads are forbidden;
  playback starts paused; close/destruction stops the player and clears the
  source before widgets are torn down.
- Providers acquire only what they render and release it on window close or
  destruction; no cross-provider shared player state (audio and video keep
  independent players today precisely so one cannot interfere with the other).

Future async path without requiring it now: `render()` stays synchronous in
the first implementation. The seam left open is teardown — whatever a future
provider starts, it must be able to stop and release through the same
destruction path that today stops players and clears sources. No threads,
cancellation tokens, or task framework are part of this contract.

## 6. Error handling: unsupported vs recognized-but-invalid

Two outcomes, handled differently:

- **Unsupported** (`supports() == No` from every provider): dispatch continues
  down the chain to the generic fallback (`No image preview is available for
  this file.`). Reaching here means no provider recognized the file.
- **Recognized but invalid** (claimed, then render fails — corrupt image,
  locked PDF, bad UTF-8, unplayable media): the claiming provider sets its
  status message (`Could not load/play … (reason).`) and dispatch **stops**.
  A failed preview is never silently handed to a different handler; today's
  media providers encode this by always claiming (`showVideoFile` /
  `showAudioFile` return true unconditionally, `src/preview_window.h` lines
  ~62–74).

Status-message ownership stays with the provider in both cases; the dispatch
layer never rewrites a provider's message.

## 7. Testing

Each provider plus the dispatch layer gets the same coverage the current
suites already establish (`tests/test_preview.cpp`, `tests/check_cli.cmake`):

- Identification tables: per-provider extension matrices including upper- and
  mixed-case spellings and negatives (unknown extensions, cross-provider
  near-misses such as `m4a` vs `m4v`), mirroring today's
  `audioExtension`-style row tests.
- Signature tests: PDF magic present/absent, unreadable file at
  identification time (must return `No`, never crash).
- Backend configurations: full build plus `PEEK_WITH_PDF=OFF` and
  `PEEK_WITH_MULTIMEDIA=OFF` builds asserting the unsupported/fall-through
  behavior of §4.
- Malformed input: corrupt images, locked/empty PDFs, NUL bytes and invalid
  UTF-8 in text, invalid media — each asserting the claiming provider's
  message and no fall-through (see §6).
- Cleanup: open/close and repeated-preview tests asserting players stop,
  sources clear, and no leaks beyond the known Qt-internal `QVideoWindow`
  suppression (`tests/lsan.supp`).
- Dispatch order: disjointness/regression tests asserting a file claimed by
  one provider never renders as another, and that message/widget object names
  (`fileNameLabel`, `statusLabel`, `textView`, `playButton`, …) are unchanged.

## 8. Migration plan (behavior preserved at every step)

1. **Extract, don't change.** Move each `showXFile()` body out of
   `PreviewWindow` into a same-signature free function per provider
   (`showPdfFile` → pdf provider render, etc.). Dispatch code, messages,
   window sizes, and object names stay byte-identical. Run the full suite.
2. **Pair supports() with render().** Give the image path an explicit
   `supports()` (see open question Q1 below) so every entry has the
   two-phase shape; keep the existing `if (!handled && …)` chain calling the
   pairs in the same order. Run the full suite, including the `OFF`-backend
   configurations.
3. **Replace the chain with the list.** Swap the hard-coded `if` chain for a
   loop over a compile-time-ordered `Provider` array plus the dispatch-layer
   stubs of §4 and the directory branch of §3. No message, size, or ordering
   change. Run the full suite (debug, release, asan with `detect_leaks=1`).

Each step is independently committable and revertible; at no point does the
window show anything different for any existing fixture.

## Open questions (not guessed)

- **Q1 — image identification.** The image path has no `isImageFile()`; its
  "support check" is the `QImageReader::canRead()` probe inside
  `showImageFile()`, which may read file bytes. Options: (a) keep the probe
  as the image provider's `supports()` with a documented bounded-read
  exception; (b) build a runtime extension allowlist from installed Qt image
  plugins (verified list is logged in `docs/architecture.md`) and probe only
  on allowlist hit. Needs a decision before step 2.
- **Q2 — per-provider window sizing.** Today the dispatch layer sizes the
  window (image fitted 1024×768; text/media fixed 640×480;
  `src/preview_window.cpp` lines ~170–180). Should providers request a size
  class, or does sizing stay a dispatch-layer policy forever? Left open.
- **Q3 — status-text vocabulary.** Messages are currently per-handler free
  text. Should the contract standardize prefixes (`No … preview is
  available` vs `Could not load/play …`) or keep them provider-owned?
  Standardizing would churn golden test strings; keeping them avoids churn.
