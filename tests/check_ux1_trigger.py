#!/usr/bin/env python3
"""Regression guard for the P-UX1 Quick Look trigger files.

Validates the versioned trigger integration without touching the live
desktop (no Hyprland/Nautilus/config changes, no key synthesis):

- integrations/hyprland/peek-preview.lua: no conflict markers, parses as
  Lua (luac -p when available), binds exactly CTRL+ALT+SHIFT+F12, guards on
  the focused Nautilus window class, forwards Ctrl+Alt+F12 down/up, and
  binds no Space key in code (comments excluded).
- integrations/nautilus/scripts-accels: byte-exact single accelerator line
  whose modifiers/key match what the Lua side forwards.
- docs/quick-look-trigger.md: present, so the contract stays documented.

Standard library only. Mirrors tests/check_taskc_lab.py.
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
LUA = REPO / "integrations" / "hyprland" / "peek-preview.lua"
ACCELS = REPO / "integrations" / "nautilus" / "scripts-accels"
DOC = REPO / "docs" / "quick-look-trigger.md"

EXPECTED_ACCELS = "<Control><Alt>F12 Peek\n"


def check_lua():
    """Return failure descriptions for the Hyprland snippet."""
    failures = []
    try:
        text = LUA.read_text(encoding="utf-8")
    except OSError as error:
        return [f"{LUA}: cannot read snippet: {error}"]
    for number, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if stripped.startswith(("<<<<<<<", ">>>>>>>")) or stripped == "=======":
            failures.append(f"{LUA}:{number}: unresolved merge marker: {stripped[:32]!r}")
    if 'hl.bind("CTRL + ALT + SHIFT + F12"' not in text:
        failures.append(f"{LUA}: must bind exactly CTRL + ALT + SHIFT + F12")
    if "org.gnome.Nautilus" not in text:
        failures.append(f"{LUA}: must guard on the focused Nautilus window class")
    if "send_key_state" not in text:
        failures.append(f"{LUA}: must forward via send_key_state")
    if '"down"' not in text or '"up"' not in text:
        failures.append(f"{LUA}: must forward both down and up key states")
    if 'mods = "CTRL ALT"' not in text or 'key = "F12"' not in text:
        failures.append(f"{LUA}: must forward mods CTRL ALT + key F12")
    code = "\n".join(
        line.split("--", 1)[0] for line in text.splitlines()
    )
    if re.search(r"(?i)\bspace\b", code):
        failures.append(f"{LUA}: code must not bind Space (conflicts; see doc)")
    luac = shutil.which("luac")
    if luac:
        try:
            subprocess.run([luac, "-p", str(LUA)], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                           text=True, timeout=30)
        except (OSError, subprocess.CalledProcessError,
                subprocess.TimeoutExpired) as error:
            failures.append(f"{LUA}: Lua syntax check failed: {error}")
    return failures


def check_accels():
    """Return failure descriptions for the Nautilus accelerator file."""
    failures = []
    try:
        raw = ACCELS.read_bytes()
    except OSError as error:
        return [f"{ACCELS}: cannot read accelerator file: {error}"]
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as error:
        return [f"{ACCELS}: must be UTF-8: {error}"]
    if text != EXPECTED_ACCELS:
        failures.append(
            f"{ACCELS}: must be exactly {EXPECTED_ACCELS!r}, got {text!r}")
    for number, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if stripped.startswith(("<<<<<<<", ">>>>>>>")) or stripped == "=======":
            failures.append(
                f"{ACCELS}:{number}: unresolved merge marker: {stripped[:32]!r}")
    return failures


def check_doc():
    """Return failure descriptions for the trigger contract doc."""
    if not DOC.is_file():
        return [f"{DOC}: missing interaction contract"]
    return []


def main():
    failures = check_lua() + check_accels() + check_doc()
    if failures:
        for failure in failures:
            print(f"check_ux1_trigger: {failure}", file=sys.stderr)
        return 1
    print(f"check_ux1_trigger: trigger files consistent "
          f"({LUA.name}, {ACCELS.name}, {DOC.name})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
