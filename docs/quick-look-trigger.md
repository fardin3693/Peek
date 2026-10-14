# Peek Quick Look trigger (P-UX1)

Keyboard-first launch for Peek from Nautilus on Omarchy/Hyprland/Wayland.
This codifies the Task C trigger already installed on this desktop — it adds
no daemon, polling, clipboard scraping, IPC, or new runtime dependency, and
no build step touches desktop configuration.

## The trigger

Press **Ctrl+Alt+Shift+F12** with a Nautilus window focused:

```text
physical Ctrl+Alt+Shift+F12
  → Hyprland bind (integrations/hyprland/peek-preview.lua)
  → send_key_state Ctrl+Alt+F12 down/up at the exact focused Nautilus window
  → Nautilus script accelerator (integrations/nautilus/scripts-accels)
  → ~/.local/share/nautilus/scripts/Peek (integrations/nautilus/peek-nautilus.py)
  → argv [peek, --, PATH] (existing native CLI, unchanged)
  → one PreviewWindow; Escape or window close dismisses it (exit 0)
```

Right-click **Scripts → Peek** keeps working as the diagnostic fallback
(see `docs/nautilus-script.md`).

## Why this chord and not Space

Space is the desired Quick Look key, but it is not safe here:

- A **global** plain-Space bind would intercept the Spacebar in every
  application (typing, games, terminals). It must stay unbound.
- Verified live (`hyprctl -j binds`, `omarchy menu keybindings --print`):
  Omarchy already binds Super+Space (Omarchy menu), Alt+Space (Spotlight),
  Shift+Alt+Space (Spotlight reminder), Super+Shift+Ctrl+Space (theme menu),
  Super+Alt+Space (apps menu), Super+Shift+Space (top bar), and
  Super+Ctrl+Space (background switcher). No plain-Space global bind exists,
  and none is added by this task.
- A **Nautilus-local** bare-Space script accelerator was deliberately not
  adopted: it risks shadowing Nautilus's own selection/activation keys and
  firing while typing in rename/search/location entries. The Task C lab left
  Space unbound and proved the dedicated F12 chord instead (23/23 checks in
  `experiments/task-c/`).
- Ctrl+Alt+F12 (the Nautilus-side accel, without Shift) has no global binding
  and no Nautilus built-in use; the accelerator file is Nautilus-scoped, so it
  cannot fire from other applications. Ctrl+Alt+Shift+F12 has exactly one
  global binding: this trigger.

## Interaction contract

- **Selected file reaches Peek:** the adapter reads
  `NAUTILUS_SCRIPT_SELECTED_URIS` fresh at each invocation and hands the
  first URI in Nautilus-provided order to the unchanged CLI (`peek -- PATH`).
- **Escape dismisses:** `PreviewWindow` binds Escape to close; closing the
  only window ends the process with exit 0. No toggle: pressing the trigger
  while a preview is focused is a documented no-op guard (see below), so
  dismissal is always explicit.
- **Second file while a preview is open:** a second trigger press starts a
  second, independent process/window (stacking, verified 3-press stacking in
  the Task C lab). There is deliberately **no re-targeting** of the running
  preview: that would need IPC or a persistent daemon, which is out of scope
  for this task. Close (Escape) the first window, or close each window in turn.
- **Missing paths:** no selection, malformed/remote URIs → adapter exits 2
  with stderr plus best-effort `notify-send`, launching nothing. Broken
  symlinks/inaccessible paths that pass the adapter → native validation exits
  1 with a stderr diagnostic, opening no window.
- **Multiple selections:** exactly one window for the first item in
  Nautilus-provided order; an informational stderr note records the count.
  Later items are ignored, never queued.
- **Trigger pressed in an unrelated application:** the Hyprland guard checks
  `class == "org.gnome.Nautilus"` (and not XWayland) on the active window and
  returns early otherwise — a silent no-op. The Nautilus accelerator itself is
  also Nautilus-scoped, so Ctrl+Alt+F12 pressed elsewhere never reaches Peek.
  There is no global shortcut to break and no focus stealing.

## Floating and focus

No compositor change was needed: this desktop already floats and centers all
windows (`windowrule = float, match:class .*` in `hyprland.conf` plus
`o.window(".*", { float = true, center = true })`), so the ordinary
decorated `PreviewWindow` appears as a centered floating preview. Activation
follows the compositor's normal focus policy; the lab verified native Escape
dismissal and no-forward-from-Peek-focus on a real (nested) compositor.

## Manual installation (this desktop already has it)

These are manual instructions, not an installation performed by any agent or
build. On this checkout the Task C block is already live; verify equivalence
instead of duplicating:

```sh
# Bind block + forwarding guard live in the user config?
grep -q 'Peek file preview' ~/.config/hypr/bindings.lua && echo BIND-PRESENT
grep -q 'send_key_state' ~/.config/hypr/bindings.lua && echo FORWARD-PRESENT
# Compositor actually loaded it (exactly one global F12 binding)?
hyprctl -j binds | python3 -c \
  "import json,sys; b=[x for x in json.load(sys.stdin) if x.get('key')=='F12']; \
   assert len(b)==1 and b[0]['description']=='Peek file preview', b; print('BIND-LOADED')"
# Accelerator + Script byte-identical to the repo?
diff integrations/nautilus/scripts-accels ~/.config/nautilus/scripts-accels \
  && echo ACCEL-EQUIVALENT
diff integrations/nautilus/peek-nautilus.py \
  ~/.local/share/nautilus/scripts/Peek && echo SCRIPT-EQUIVALENT
```

Fresh install (only with the owner's explicit approval — never scripted):

```sh
# 1. Append integrations/hyprland/peek-preview.lua to ~/.config/hypr/bindings.lua
#    (or require() it from bindings.lua), then:
hyprctl reload
# 2. Place the accelerator file where Nautilus reads it:
install -d ~/.config/nautilus
cp -i integrations/nautilus/scripts-accels ~/.config/nautilus/scripts-accels
# 3. Install the Script itself (see docs/nautilus-script.md), then restart
#    Nautilus yourself:
nautilus -q
```

Script accelerators register lazily and spawns are async: after install,
restart Nautilus and allow a beat before the first trigger press.

Rollback (owner-approved only): remove the bind block → `hyprctl reload` →
delete `~/.config/nautilus/scripts-accels` → `nautilus -q`. Right-click
Scripts → Peek keeps working throughout.

## Verification status

- Automated suites (`ctest --preset debug|release|asan`, plus the new
  `peek_ux1_trigger_guard`) cover CLI, previews, the adapter, and these
  trigger files' syntax/shape. They do not prove menu discovery, live focus,
  or physical-key behavior.
- Nested-compositor lab (`experiments/task-c/`, 23/23) proved forwarding,
  focus alternation, stacking, no-op-when-unfocused, and Escape dismissal —
  but with synthetic keys; one **physical** Ctrl+Alt+Shift+F12 press on the
  live desktop remains the acceptance test (see "Manual acceptance" below).
- Never claim a real-session result that was not genuinely performed with a
  physical keyboard on this compositor.

### Manual acceptance (physical keyboard, live desktop)

1. Focus a Nautilus window, select a file, press Ctrl+Alt+Shift+F12: one
   centered floating preview opens for that file.
2. Press Escape: the preview closes, no residue process (`ps` shows no peek).
3. Repeat with an image, a PDF, a text file, a video, and an audio file.
4. With a preview open, select another file and trigger again: a second
   window stacks (known limitation, not re-targeting). Close both.
5. Focus a terminal/editor and press the chord: nothing happens.
6. Right-click Scripts → Peek still opens the selection (fallback intact).

## Remaining limitations

- **No second-file re-targeting.** Each trigger spawns an independent
  process/window; repeats stack instead of replacing. Fixing this needs a
  single-instance design (IPC/daemon) and is separately scoped — not this task.
- **No Space trigger.** Documented above; revisiting needs proof that neither
  global typing nor Nautilus built-ins regress.
- The chord is four keys and F12-centric: ergonomic, but chosen for
  conflict-freedom over comfort. Any friendlier chord needs the same
  conflict audit first.
