# Peek keyboard shortcuts

All shortcuts are window-local: they work while a preview window is focused
and never act globally. Peek otherwise binds just `Escape`, and the Hyprland
trigger uses Super chords (see [quick-look-trigger.md](quick-look-trigger.md)),
so the plain keys below cannot conflict with an existing application, window,
or compositor shortcut.

## Every preview

| Keys | Action |
| --- | --- |
| `Escape` | Close the preview window |

## PDF preview (requires the Poppler backend)

| Keys | Action |
| --- | --- |
| `Left` / `PageUp` | Previous page (disabled on the first page) |
| `Right` / `PageDown` | Next page (disabled on the last page) |
| `+` / `=` | Zoom in one 25 % step |
| `-` | Zoom out one 25 % step |
| `0` | Fit page to the preview area |

Notes:

- Zoom percentages are relative to the fitted size: 100 % fills the preview
  area, adjustable from 25 % to 400 %. Entering `Fit page` or `Fit width`
  restarts the zoom at 100 %, so the next step is always relative to what is
  on screen.
- `Fit width` has a button but no shortcut.
- In a PDF preview the arrow keys turn pages by design (Quick Look feel);
  panning a zoomed page stays on the scrollbars and the mouse.
