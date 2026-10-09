# Nautilus selection handoff (Task B)

This is the minimal selection bridge, **not the final Peek interaction**. Use
Nautilus's right-click **Scripts → Peek** menu to diagnose handoff. No Hyprland
shortcut, Space forwarding, window toggle, renderer, daemon, polling, selection
cache, or clipboard communication is installed or implemented.

## Contract

- Read `NAUTILUS_SCRIPT_SELECTED_URIS` only when Nautilus invokes the Script.
  Nautilus supplies a newline-delimited URI list; an optional final newline is
  accepted. Do not use raw newline-delimited filesystem paths: a filename itself
  may contain a newline, which a URI represents as `%0A`.
- **Multiple selections use only the first item in Nautilus-provided order.**
  The Script neither sorts nor infers click order. It emits an informational
  stderr message. If the first item is unsupported, it fails rather than picking
  another item. Later items are ignored.
- Accept only absolute local `file:` URIs with an empty authority or `localhost`.
  Reject `smb:`, `sftp:`, `trash:`, HTTP(S), and nonlocal `file://HOST/...` URIs;
  do not download files or reinterpret a remote URI as a local path.
- Decode percent escapes exactly once as UTF-8. Spaces, Unicode, quotes, `+`,
  percent signs, shell metacharacters, and encoded newlines/tabs remain literal
  filename data. Reject malformed escapes, invalid UTF-8, NUL, unencoded ASCII
  whitespace/controls, queries, and fragments. A literal `?` or `#` must be
  percent-encoded. Non-UTF-8 Unix filename bytes are explicitly unsupported by
  this adapter; they are not silently replaced.
- Launch the native executable with the argv array `[executable, "--", path]`,
  never a shell command. Preserve symlink paths and `.`/`..` components.
  The adapter does not inspect file contents, stat selections, or scan folders.
- Directories are passed to Peek and show Task A's directory placeholder.
  Native Peek remains responsible for existence/type checks, broken symlinks,
  unsupported filesystem objects, and the Qt window. Missing or changed paths
  produce its diagnostic.
- The Script waits for window dismissal. Normal dismissal exits `0`; absent,
  malformed, or unsupported selection exits `2`; executable discovery/spawn or
  any nonzero native exit returns `1`. Native diagnostics are included in errors.
  Each invocation starts a separate native process/window.
- Errors always go to stderr and also use a best-effort critical notification
  through `notify-send`, if available. Notification failure never replaces the
  original error; its subprocess has a five-second timeout. The interactive
  native preview has no timeout. Notification bodies escape markup.

Executable lookup order:

1. `PEEK_EXECUTABLE`, if set: a single **absolute executable path**, not a command,
   not a URI, with no `~`/environment expansion or appended options. Empty or
   relative overrides are errors. This is useful for terminal diagnostics.
2. `~/.local/bin/peek`, if it is a file (including a valid symlink).
3. `peek` on the PATH inherited by Nautilus. A GUI session's PATH need not match
   your terminal's PATH; the explicit user-local candidate avoids that reliance.

## Dependencies on Omarchy / Arch

- Nautilus (Arch package `nautilus`), with built-in Scripts support.
- Python >= 3.9 (Arch package `python`), available as `python3` in Nautilus's PATH;
  the Script uses `#!/usr/bin/env python3`. No pip packages, PyGObject, or
  `nautilus-python` extension are required.
- The existing native Peek build and Qt 6 Widgets runtime (`qt6-base`), plus
  `qt6-wayland` for native Wayland use. Build dependencies are in the main README.
- Recommended for visible errors from the menu: `notify-send` (`libnotify`) and
  a working desktop notification service (normally provided by Omarchy).
  Without it, run terminal diagnostics below to see stderr.

Read-only dependency checks:

```sh
/usr/bin/python3 --version
command -v python3
nautilus --version
command -v notify-send
pacman -Q python nautilus libnotify qt6-base qt6-wayland
```

Do not install packages or change desktop/global configuration as part of this
step without approval. These dependencies were present on the development
machine when Task B was checked; another desktop must verify them.

## User-local installation

**These are manual instructions, not an installation performed by the agent.**
On this checkout, run from `/home/fardin/Projects/Peek` after building Debug:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

install -d "$HOME/.local/bin" "$HOME/.local/share/nautilus/scripts"
ln -s /home/fardin/Projects/Peek/build/debug/peek "$HOME/.local/bin/peek"
cp -i integrations/nautilus/peek-nautilus.py "$HOME/.local/share/nautilus/scripts/Peek"
chmod +x "$HOME/.local/share/nautilus/scripts/Peek"
```

The exact Script location on this Omarchy desktop is
`/home/fardin/.local/share/nautilus/scripts/Peek`.
Nautilus uses `$XDG_DATA_HOME/nautilus/scripts` when `XDG_DATA_HOME` is customized;
otherwise it defaults to `~/.local/share/nautilus/scripts`. The inspected setting
was `/home/fardin/.local/share`, matching the commands above. Adjust the Script
destination if your setting differs; the native binary still belongs at
`~/.local/bin/peek`.

`ln -s` deliberately does not replace an existing `peek`; if it fails because
one exists, inspect it and decide whether to reuse it. `cp -i` prompts before
replacing an existing Script. There is no `sudo` or system-wide installation.
The symlink uses this checkout's Debug build; rebuilding updates the target,
while moving/deleting the checkout breaks it. For a Release build, use
`build/release/peek` as the symlink target instead, or manually place that binary
at `~/.local/bin/peek`. Recopy the Script after adapter changes.

Open Nautilus, select a local file, and choose **right-click → Scripts → Peek**.
If the menu entry does not appear, browse to the Scripts folder in Nautilus and
reopen the context menu; check that `Peek` is executable. If needed, close and
reopen Nautilus yourself after saving any file operations. No agent-initiated
Nautilus restart or compositor reload is needed.

## Testing

### Automated (no desktop changes)

```sh
# Focused suite, including real native rejection cases:
ctest --test-dir build/debug -R '^peek_nautilus$' --output-on-failure

# Full regression suite, preserving all Milestone 1 and Task A tests:
ctest --preset debug

# Adapter-only tests; native rejection tests skip without PEEK_TEST_EXECUTABLE:
/usr/bin/python3 -B tests/test_nautilus.py
```

CTest discovers Python only when `PEEK_BUILD_TESTS=ON`. A tests-disabled native
build has no Python requirement. Tests cover one-time URI conversion, selection
ordering, literal argv passing to a temporary executable, shell-injection
inertness, per-invocation fresh selection, directory handoff, launch failures,
error reporting/notification failures, and waiting for child dismissal. Native
error integration is offscreen with an empty Qt platform theme, like the other
suites; tests never invoke your real notification service or change GUI config.

### Terminal diagnostics on this checkout

This bypasses the menu to isolate the bridge from Nautilus. Image files now
render in the native preview; other files show the fallback message and
directories the placeholder. Dismiss with Escape or ordinary close:

```sh
env PEEK_EXECUTABLE=/home/fardin/Projects/Peek/build/debug/peek \
    NAUTILUS_SCRIPT_SELECTED_URIS=file:///home/fardin/Projects/Peek/README.md \
    /usr/bin/python3 integrations/nautilus/peek-nautilus.py

# No selection: diagnostic/notification, exit 2, no native preview.
env -u NAUTILUS_SCRIPT_SELECTED_URIS \
    /usr/bin/python3 integrations/nautilus/peek-nautilus.py

# Remote selection: diagnostic/notification, exit 2, no native preview.
env NAUTILUS_SCRIPT_SELECTED_URIS=smb://server/share/file.txt \
    /usr/bin/python3 integrations/nautilus/peek-nautilus.py

# Missing executable: diagnostic/notification, exit 1.
env PEEK_EXECUTABLE=/nonexistent/peek \
    NAUTILUS_SCRIPT_SELECTED_URIS=file:///home/fardin/Projects/Peek/README.md \
    /usr/bin/python3 integrations/nautilus/peek-nautilus.py
```

These deliberately do not override your Qt platform/theme settings; inherited
Wayland/theme behavior is part of real desktop acceptance. Terminal-only
environment overrides do not configure Nautilus persistently.

### Manual Nautilus acceptance checklist

1. Select `README.md`, invoke **Scripts → Peek**, and verify its exact native
   name/path labels. Dismiss with Escape and ordinary close on separate runs.
2. In a test-owned folder, try names with spaces, Unicode, single/double quotes,
   `+`, `%`, `#`, and shell metacharacters. Verify the displayed path matches and
   no text is treated as a command. Encoded newline/tab handling is also covered
   automatically if awkward to create or select in the GUI.
3. Select another file and invoke again: verify the fresh selection, not the
   preceding file. Multiple selection must produce one window for the first
   Nautilus-provided item, not one per file. Do not assume selection-click order.
4. Select a directory: verify the directory placeholder, with no content scan.
5. Verify visible errors using the terminal commands above. If Nautilus offers
   the Script for a remote selection, verify rejection there too. No-selection
   Scripts availability and remote menu availability may vary by Nautilus view;
   the Script's error paths are tested independently.
6. Confirm visible launch and native path errors (e.g. a broken symlink selected
   in Nautilus). Check terminal stderr if notifications are unavailable or hidden.

Automated tests do **not** prove menu discovery, Nautilus's real selection order,
notification visibility, GUI-session PATH, or compositor focus/placement policy.
Task B must stop for review after this diagnostic handoff. Task C and any final
keyboard-triggered UX require separate approval.

Nautilus's environment-variable contract and default Script directory are also
summarized in the [Nautilus Scripts guide](https://wiki.ubuntu.com/Nautilus_scripts).
