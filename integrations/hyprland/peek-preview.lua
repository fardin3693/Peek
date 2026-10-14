-- Peek Quick Look trigger (P-UX1): Hyprland side of the keyboard-first workflow.
--
-- Manual-install snippet, not applied by any build step. It codifies the
-- Task C block already live in ~/.config/hypr/bindings.lua (lab-proven in
-- experiments/task-c/, 23/23 checks): a global chord that forwards only to
-- the exact focused Nautilus window. Anything else focused is a no-op.
--
-- Deliberately NOT Space: a global plain-Space bind would intercept typing
-- in every application, and Omarchy already binds Super+Space (menu),
-- Alt+Space (Spotlight), Super+Shift+Ctrl+Space (theme menu) and friends.
-- See docs/quick-look-trigger.md for the full conflict analysis, install
-- verification, and rollback.
--
-- Pair with integrations/nautilus/scripts-accels (<Control><Alt>F12 Peek)
-- and the existing integrations/nautilus/peek-nautilus.py Script. No daemon,
-- polling, clipboard scraping, or IPC is involved.

-- Peek file preview: global trigger forwards to the focused
-- Nautilus window only. Anything else focused → no-op. No Space binding.
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
end, { description = "Peek file preview" })
