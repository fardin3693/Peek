-- Test-owned nested compositor only. Never source this in the live desktop.
hl.monitor({ output = "WAYLAND-1", mode = "1280x720@60", position = "0x0", scale = 1 })
hl.config({
    animations = { enabled = false },
    -- wtype uploads a compact virtual keymap; resolve its symbols, not US keycodes.
    input = { kb_layout = "us", follow_mouse = 0, resolve_binds_by_sym = true },
    xwayland = { enabled = false },
})

-- Exact focused window, not the first class match among several Nautilus windows.
-- Explicit down/up avoids send_shortcut's press/release-context ambiguity.
hl.bind("CTRL + ALT + SHIFT + F12", function()
    local window = hl.get_active_window()
    if not window or window.class ~= "org.gnome.Nautilus" or window.xwayland then
        return
    end
    for _, state in ipairs({ "down", "up" }) do
        hl.dispatch(hl.dsp.send_key_state({
            mods = "CTRL ALT", key = "F12", state = state, window = window,
        }))
    end
end, { description = "Peek Task C isolated experiment" })
