#!/usr/bin/env python3
"""Invocation-time Nautilus selection handoff to the native Peek CLI."""

import html
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from urllib.parse import unquote_to_bytes, urlsplit


class SelectionError(ValueError):
    """The selection or executable configuration cannot be handed off."""


def uri_to_path(uri):
    """Decode a local file URI once, without resolving or normalizing its path."""
    # urlsplit otherwise silently strips some controls, changing the requested path.
    if any(ord(char) <= 0x20 or ord(char) == 0x7F for char in uri):
        raise SelectionError("Malformed URI: whitespace/control characters must be percent-encoded.")
    if "?" in uri or "#" in uri:
        raise SelectionError("File URIs with queries or fragments are not supported.")
    try:
        parts = urlsplit(uri)
    except ValueError as error:
        raise SelectionError(f"Malformed selection URI: {error}") from error
    if parts.scheme != "file" or parts.netloc.lower() not in ("", "localhost"):
        raise SelectionError("Only local file URIs are supported; remote selections are not previewed.")
    if not parts.path.startswith("/"):
        raise SelectionError("The selected file URI must contain an absolute local path.")
    if re.search(r"%(?![0-9a-fA-F]{2})", parts.path):
        raise SelectionError("Malformed percent escape in the selected file URI.")
    try:
        path = unquote_to_bytes(parts.path).decode("utf-8", errors="strict")
    except (UnicodeDecodeError, UnicodeEncodeError) as error:
        raise SelectionError("The selected file URI must encode a valid UTF-8 filename.") from error
    if "\0" in path:
        raise SelectionError("The selected file URI contains a NUL byte.")
    return path


def selected_path(selection):
    """Use the first item in Nautilus-provided order, not click or sorted order."""
    if not selection or selection == "\n":
        raise SelectionError("No selection. Select a local file or directory in Nautilus first.")
    uris = selection.split("\n")
    if uris[-1] == "":
        uris.pop()  # Nautilus may terminate its newline-delimited list with a newline.
    return uri_to_path(uris[0]), len(uris)


def find_executable():
    """Use an explicit absolute override, a user-local build, or the GUI's PATH."""
    if "PEEK_EXECUTABLE" in os.environ:
        executable = os.environ["PEEK_EXECUTABLE"]
        if not Path(executable).is_absolute():
            raise SelectionError("PEEK_EXECUTABLE must be an absolute executable path (not a command).")
        return executable
    candidate = Path.home() / ".local" / "bin" / "peek"
    if candidate.is_file():
        return str(candidate)
    executable = shutil.which("peek")
    if executable:
        return executable
    raise SelectionError("Cannot find Peek. Place it at ~/.local/bin/peek or on Nautilus's PATH.")


def report_error(message):
    """Always emit stderr; desktop notifications are optional and best-effort."""
    print(f"peek-nautilus: {message}", file=sys.stderr)
    notifier = shutil.which("notify-send")
    if notifier:
        try:
            subprocess.run(
                [notifier, "--app-name=Peek", "--urgency=critical", "--",
                 "Peek selection handoff", html.escape(message)],
                check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                timeout=5, shell=False,
            )
        except (OSError, subprocess.TimeoutExpired):
            pass


def main():
    try:
        path, count = selected_path(os.environ.get("NAUTILUS_SCRIPT_SELECTED_URIS"))
    except SelectionError as error:
        report_error(str(error))
        return 2
    if count > 1:
        print(f"peek-nautilus: {count} items selected; using only the first in Nautilus-provided order.",
              file=sys.stderr)
    try:
        executable = find_executable()
    except SelectionError as error:
        report_error(str(error))
        return 1
    try:
        # Wait for normal dismissal so native startup/path failures remain visible.
        # No shell, detached service, cached selection, or filesystem validation here.
        result = subprocess.run(
            [executable, "--", path], check=False, stderr=subprocess.PIPE,
            text=True, errors="replace", shell=False,
        )
    except OSError as error:
        report_error(f"Cannot launch Peek at {executable!r}: {error}")
        return 1
    if result.returncode != 0:
        detail = result.stderr.strip()
        message = f"Peek failed (exit status {result.returncode}) for {path!r}."
        if detail:
            message += f"\n{detail}"
        report_error(message)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
