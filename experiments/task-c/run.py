#!/usr/bin/env python3
"""Opt-in, bounded nested Wayland lab; never binds keys on the live desktop."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time

REPO = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
CLASS = "org.gnome.Nautilus"


def run(argv, env=None):
    result = subprocess.run(argv, env=env, check=False, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
    if result.returncode:
        raise RuntimeError(f"{argv!r}: {result.stderr or result.stdout}")
    return result.stdout


def stop(process):
    if process and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def user_files():
    home = Path.home()
    paths = list((home / ".config/hypr").rglob("*.lua"))
    paths += list((home / ".config/hypr").rglob("*.conf"))
    paths += [home / ".config/nautilus/scripts-accels", home / ".local/share/nautilus/scripts/Peek"]
    return {str(path): hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
            for path in paths}


def outer(manual):
    for program in ("Hyprland", "hyprctl", "nautilus", "wtype", "dbus-run-session"):
        if not shutil.which(program):
            raise RuntimeError(f"Missing {program}; no packages will be installed.")
    if not (REPO / "build/debug/peek").is_file():
        raise RuntimeError("Build the Debug native executable before running this lab.")
    parent_env = os.environ.copy()
    parent_binds = run(["hyprctl", "-j", "binds"])
    before_files = user_files()
    display = Path(parent_env["WAYLAND_DISPLAY"])
    if not display.is_absolute():
        display = Path(parent_env["XDG_RUNTIME_DIR"]) / display
    with tempfile.TemporaryDirectory(prefix="task-c-", dir=REPO / "build") as temporary:
        root = Path(temporary)
        env = parent_env.copy()
        for key in ("HYPRLAND_INSTANCE_SIGNATURE", "WAYLAND_SOCKET", "DISPLAY", "NOTIFY_SOCKET"):
            env.pop(key, None)
        for key, name in (("HOME", "home"), ("XDG_CONFIG_HOME", "config"),
                          ("XDG_DATA_HOME", "data"), ("XDG_CACHE_HOME", "cache"),
                          ("XDG_STATE_HOME", "state"), ("XDG_RUNTIME_DIR", "run")):
            path = root / name
            path.mkdir(mode=0o700)
            env[key] = str(path)
        # Socket names allow only 107 bytes. A short supervisor-owned directory FD
        # keeps all artifacts under build/ without requiring files in /tmp or HOME.
        runtime_fd = os.open(root / "run", os.O_RDONLY | os.O_DIRECTORY)
        env["XDG_RUNTIME_DIR"] = f"/proc/{os.getpid()}/fd/{runtime_fd}"
        env.update(WAYLAND_DISPLAY=str(display), LIBSEAT_BACKEND="seatd",
                   SEATD_SOCK=str(root / "run/absent-seatd.sock"),
                   HYPRLAND_NO_SD_VARS="1", HYPRLAND_NO_SD_NOTIFY="1", HYPRLAND_NO_RT="1",
                   GSETTINGS_BACKEND="memory", NAUTILUS_DISABLE_PLUGINS="TRUE",
                   GTK_A11Y="none", GSK_RENDERER="cairo", GIO_USE_VFS="local",
                   GIO_USE_VOLUME_MONITOR="unix", QT_QPA_PLATFORM="wayland", QT_QPA_PLATFORMTHEME="",
                   DBUS_SYSTEM_BUS_ADDRESS="unix:path=" + str(root / "run/absent-system-bus"),
                   PEEK_LAB_ROOT=str(root), PYTHONDONTWRITEBYTECODE="1")
        bus_config = root / "bus.conf"
        # No activation directories: cannot start the real desktop's services.
        bus_config.write_text(
            '<busconfig><type>session</type><listen>unix:tmpdir=' + str(root / "run") +
            '</listen><auth>EXTERNAL</auth><policy context="default">'
            '<allow own="*"/><allow send_destination="*"/><allow receive_sender="*"/>'
            '</policy></busconfig>')
        print("Starting test-owned nested session; no live-desktop bindings/config changes.", flush=True)
        argv = ["dbus-run-session", "--config-file", str(bus_config), "--",
                sys.executable, "-B", str(__file__), "--inside"]
        if manual:
            argv.append("--manual")
        process = None
        try:
            process = subprocess.Popen(argv, env=env, start_new_session=True)
            status = process.wait(timeout=240)
            if status:
                raise RuntimeError(f"Nested lab exited {status}.")
        finally:
            if process:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
            os.close(runtime_fd)
            if run(["hyprctl", "-j", "binds"]) != parent_binds:
                raise RuntimeError("Parent binding table changed during experiment; no automatic rollback attempted.")
            if user_files() != before_files:
                raise RuntimeError("User config/installed Script changed during experiment; no automatic rollback attempted.")
            print("UNCHANGED: parent bindings and user Hyprland/Nautilus config; test artifacts cleaned.", flush=True)


def inside(manual):
    root = Path(os.environ["PEEK_LAB_ROOT"])
    # Hidden --inside is not a general-purpose live-session entry point.
    if not root.is_relative_to(REPO / "build") or not root.name.startswith("task-c-"):
        raise RuntimeError("Refusing a non-test-owned lab root.")
    env = os.environ.copy()
    compositor = nautilus = None
    logs = []
    try:
        compositor_log = (root / "hyprland.log").open("w")
        logs.append(compositor_log)
        compositor = subprocess.Popen(["Hyprland", "--config", str(HERE / "nested.lua")],
                                      stdout=compositor_log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 15
        instance = None
        # Bounded readiness observation for test orchestration, not selection polling.
        while time.monotonic() < deadline and compositor.poll() is None:
            sockets = list((root / "run/hypr").glob("*/.socket.sock"))
            if sockets:
                instance = sockets[0].parent.name
                break
            time.sleep(0.1)
        if not instance:
            raise RuntimeError("Nested compositor did not start:\n" + (root / "hyprland.log").read_text()[-4000:])
        env["HYPRLAND_INSTANCE_SIGNATURE"] = instance
        displays = [p for p in (root / "run").glob("wayland-*") if stat.S_ISSOCK(p.stat().st_mode)]
        if len(displays) != 1:
            raise RuntimeError("Cannot uniquely identify the test compositor's Wayland socket.")
        env["WAYLAND_DISPLAY"] = displays[0].name

        def ctl(*args):
            return run(["hyprctl", "--instance", instance, *args], env)

        def clients():
            return json.loads(ctl("-j", "clients"))

        def wait_client(predicate, timeout=10):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                matches = [w for w in clients() if predicate(w)]
                if matches:
                    return matches[0]
                time.sleep(0.1)
            raise RuntimeError("Expected private window did not appear.")

        def focus(window):
            if json.loads(ctl("-j", "activewindow")).get("address") == window["address"]:
                return
            selector = json.dumps("address:" + window["address"])
            ctl("dispatch", "hl.dsp.focus({window=" + selector + "})")
            if json.loads(ctl("-j", "activewindow")).get("address") != window["address"]:
                raise RuntimeError("Failed to focus the exact private window.")

        def keys(*args):
            run(["wtype", *args], env)

        def chord(*prefix, direct=False, hold=False, modifiers_first=False):
            args = [*prefix, "-M", "ctrl", "-M", "alt"]
            if not direct:
                args += ["-M", "shift"]
            args += ["-P", "F12"]
            if hold:
                args += ["-s", "600"]
            releases = (["-m", "shift"] if not direct else []) + ["-m", "alt", "-m", "ctrl"]
            args += releases + ["-p", "F12"] if modifiers_first else ["-p", "F12"] + releases
            keys(*args)
            time.sleep(0.6)

        def records(name):
            path = root / name
            return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

        def await_growth(reader, before, timeout=10):
            # Script spawns are async; poll for the record instead of
            # assuming a fixed delivery latency. Test orchestration only.
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if len(reader()) > before:
                    return True
                time.sleep(0.2)
            return len(reader()) > before

        def await_argv(before, timeout=10):
            return await_growth(lambda: records("argv.jsonl"), before, timeout)

        def await_errors(before, timeout=10):
            return await_growth(lambda: records("errors.jsonl"), before, timeout)

        def quiet_wait(seconds=3):
            # Absence proofs need a bound: nothing may arrive within this window.
            time.sleep(seconds)

        def expect_path(path, before, label):
            actual = records("argv.jsonl")
            if len(actual) != before + 1 or actual[-1] != ["--", str(path)]:
                raise RuntimeError(f"{label}: expected one fresh argv for {path!s}; got {actual[before:]!r}")
            # Late repeats or queued presses must not add a second launch.
            time.sleep(1.5)
            actual = records("argv.jsonl")
            if len(actual) != before + 1:
                raise RuntimeError(f"{label}: unstable, {len(actual) - before} invocations landed")
            print("PASS:", label, flush=True)

        def select(path):
            run(["nautilus", "--select", str(path)], env)
            time.sleep(0.15)
            # A remote CLI request has no user activation token on Wayland. The
            # test explicitly focuses the owning fixture window; the bind never does.
            focus(wait_client(lambda w: w["class"] == CLASS and w["title"] == path.parent.name))

        time.sleep(0.3)
        # The IPC socket appears before the server accepts connections;
        # bounded retry for test orchestration, not selection polling.
        config_errors = None
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            try:
                config_errors = ctl("configerrors")
                break
            except RuntimeError:
                time.sleep(0.5)
        if config_errors is None:
            raise RuntimeError("Nested compositor IPC did not respond:\n" +
                               (root / "hyprland.log").read_text()[-4000:])
        if config_errors.strip():
            raise RuntimeError("Private compositor config errors: " + config_errors)
        script_dir = root / "data/nautilus/scripts"
        script_dir.mkdir(parents=True)
        shutil.copy2(REPO / "integrations/nautilus/peek-nautilus.py", script_dir / "Peek")
        accel_dir = root / "config/nautilus"
        accel_dir.mkdir(parents=True)
        accelerator = accel_dir / "scripts-accels"
        accelerator.write_text("<Control><Alt>F12 Peek\n")
        bin_dir = root / "bin"
        bin_dir.mkdir()
        recorder = bin_dir / "peek-recorder"
        native = REPO / "build/debug/peek"
        recorder.write_text(
            "#!" + sys.executable + "\n"
            "import json, os, sys\nfrom pathlib import Path\n"
            "root = Path(os.environ['PEEK_LAB_ROOT'])\n"
            "fd = os.open(root/'argv.jsonl', os.O_WRONLY|os.O_CREAT|os.O_APPEND, 0o600)\n"
            "os.write(fd, (json.dumps(sys.argv[1:])+'\\n').encode()); os.close(fd)\n"
            "if (root/'native-mode').exists():\n"
            "    native = " + repr(str(native)) + "\n"
            "    os.execv(native, [native] + sys.argv[1:])\n")
        recorder.chmod(0o700)
        notifier = bin_dir / "notify-send"
        notifier.write_text(
            "#!" + sys.executable + "\nimport json, os, sys\nfrom pathlib import Path\n"
            "fd=os.open(Path(os.environ['PEEK_LAB_ROOT'])/'errors.jsonl', os.O_WRONLY|os.O_CREAT|os.O_APPEND, 0o600)\n"
            "os.write(fd,(json.dumps(sys.argv[1:])+'\\n').encode()); os.close(fd)\n")
        notifier.chmod(0o700)
        env.update(PATH=str(bin_dir) + ":/usr/bin:/bin", PEEK_EXECUTABLE=str(recorder))
        folder = root / "fixtures-one"
        folder.mkdir()
        first = folder / "01 café ' quote.pdf"
        second = folder / "02 日本 image.png"
        first.touch()
        second.touch()
        folder_two = root / "fixtures-two"
        folder_two.mkdir()
        third = folder_two / "third.txt"
        third.touch()
        empty = root / "empty"
        empty.mkdir()
        nautilus_log = (root / "nautilus.log").open("w")
        logs.append(nautilus_log)

        def start_nautilus(path):
            process = subprocess.Popen(["nautilus", "--select", str(path)], env=env,
                                       stdout=nautilus_log, stderr=subprocess.STDOUT)
            # Cold start in a sterile bus can stall on Tracker/volume lookups.
            window = wait_client(lambda w: w["class"] == CLASS, timeout=30)
            time.sleep(1.2)  # Directory/menu initialization; no selection bridge is sampled.
            # New-window mapping establishes focus; do not warp the pointer here.
            return process

        nautilus = start_nautilus(first)
        # New-window mapping does not guarantee keyboard focus; the bind and
        # the Nautilus accelerator both need the exact fixture window focused.
        focus(wait_client(lambda w: w["class"] == CLASS and w["title"] == "fixtures-one"))
        before = len(records("argv.jsonl"))
        # Nautilus registers script accelerators lazily while building its
        # scripts menu, and spawns land asynchronously: send one chord, wait
        # for it to land, and only then consider a retry. Overshoot fails
        # loudly instead of masking a double-fire.
        attempts = 0
        while True:
            attempts += 1
            chord(direct=True)
            await_argv(before)
            landed = len(records("argv.jsonl")) - before
            if landed == 1:
                break
            if landed > 1:
                raise RuntimeError(f"direct accelerator fired {landed}x for {attempts} chord(s)")
            if attempts >= 6:
                break
            time.sleep(0.5)
        expect_path(first, before, "dedicated Nautilus accelerator → actual Task B Script")
        before += 1
        chord()
        await_argv(before)
        expect_path(first, before, "Hyprland forwarding → exact focused Nautilus window")
        for index in range(4):
            select(first)
            before = len(records("argv.jsonl"))
            # Arrow changes selection and trigger immediately follows in one input stream.
            chord("-k", "Right")
            await_argv(before)
            expect_path(second, before, f"immediate selection change then trigger {index + 1}/4")
        select(first)
        before = len(records("argv.jsonl"))
        chord(hold=True)
        await_argv(before)
        expect_path(first, before, "600ms held trigger produces one invocation")
        before = len(records("argv.jsonl"))
        chord(modifiers_first=True)
        await_argv(before)
        expect_path(first, before, "qualifying modifiers released before F12")
        before = len(records("argv.jsonl"))
        chord("-M", "ctrl", "-k", "a", "-m", "ctrl")
        await_argv(before)
        expect_path(first, before, "multiple selection uses first model-ordered item")

        select(second)
        window_one = json.loads(ctl("-j", "activewindow"))
        run(["nautilus", "--new-window", str(folder_two)], env)
        window_two = wait_client(lambda w: w["class"] == CLASS and w["address"] != window_one["address"])
        time.sleep(0.5)
        focus(window_two)
        keys("-M", "ctrl", "-k", "a", "-m", "ctrl")
        for index in range(4):
            window, path = (window_one, second) if index % 2 == 0 else (window_two, third)
            focus(window)
            before = len(records("argv.jsonl"))
            chord()
            await_argv(before)
            expect_path(path, before, f"two-window focus alternation {index + 1}/4")
        focus(window_one)
        before = len(records("argv.jsonl"))
        chord("-M", "ctrl", "-k", "l", "-m", "ctrl")
        await_argv(before)
        expect_path(second, before, "location-field focus still invokes Script (documented limitation)")
        keys("-k", "Escape")

        run(["nautilus", "--new-window", str(empty)], env)
        empty_window = wait_client(lambda w: w["class"] == CLASS and w["title"] == "empty")
        time.sleep(0.4)
        focus(empty_window)
        before = len(records("argv.jsonl"))
        errors_before = len(records("errors.jsonl"))
        chord()
        await_errors(errors_before)
        if len(records("argv.jsonl")) != before or len(records("errors.jsonl")) != errors_before + 1:
            raise RuntimeError("Empty selection did not fail without launching Peek.")
        if "No selection" not in records("errors.jsonl")[-1][-1]:
            raise RuntimeError("Missing no-selection diagnostic.")
        print("PASS: no-selection failure, no native launch", flush=True)

        (root / "native-mode").touch()
        select(first)
        before = len(records("argv.jsonl"))
        chord()
        await_argv(before)
        expect_path(first, before, "actual native Peek launched with selected special-character path")
        peek_window = wait_client(lambda w: w["class"] == "peek")
        focus(peek_window)
        before = len(records("argv.jsonl"))
        chord()
        quiet_wait()
        if len(records("argv.jsonl")) != before:
            raise RuntimeError("Forwarded from non-Nautilus focus!")
        print("PASS: Peek/non-Nautilus focus does not forward or toggle", flush=True)
        keys("-k", "Escape")
        time.sleep(0.3)
        if any(w["address"] == peek_window["address"] for w in clients()):
            raise RuntimeError("Native Escape dismissal failed.")
        print("PASS: actual native Escape dismissal", flush=True)

        broken = folder / "03 broken-link"
        broken.symlink_to(folder / "missing-target")
        select(broken)
        before = len(records("argv.jsonl"))
        errors_before = len(records("errors.jsonl"))
        chord()
        await_argv(before)
        expect_path(broken, before, "broken symlink selection reaches native validation")
        await_errors(errors_before)
        if len(records("errors.jsonl")) != errors_before + 1:
            raise RuntimeError("Native rejection did not report an error.")
        print("PASS: native path rejection surfaced through adapter", flush=True)

        if manual:
            select(first)
            print("MANUAL: focus the nested Nautilus window and use Ctrl+Alt+Shift+F12.\n"
                  "Only test fixtures are used. Ctrl+C in this terminal undoes the lab;\n"
                  "automatic cleanup also occurs after 90 seconds.", flush=True)
            time.sleep(90)

        stop(nautilus)
        env["PEEK_EXECUTABLE"] = str(root / "missing-peek")
        nautilus = start_nautilus(first)
        before = len(records("argv.jsonl"))
        errors_before = len(records("errors.jsonl"))
        chord()
        await_errors(errors_before)
        if len(records("argv.jsonl")) != before or len(records("errors.jsonl")) != errors_before + 1:
            raise RuntimeError("Missing executable did not fail clearly.")
        if "Cannot launch Peek" not in records("errors.jsonl")[-1][-1]:
            raise RuntimeError("Missing launch-failure diagnostic.")
        print("PASS: keyboard-triggered executable launch failure", flush=True)

        # Removing only the lab's file cannot change the already-loaded process table.
        accelerator.unlink()
        errors_before = len(records("errors.jsonl"))
        chord()
        await_errors(errors_before)
        if len(records("errors.jsonl")) != errors_before + 1:
            raise RuntimeError("Accelerator process-lifetime behavior changed unexpectedly.")
        print("PASS: accelerator changes do not reload a running Nautilus process", flush=True)
        stop(nautilus)
        nautilus = start_nautilus(first)
        errors_before = len(records("errors.jsonl"))
        chord()
        quiet_wait()
        if len(records("errors.jsonl")) != errors_before:
            raise RuntimeError("Missing accelerator still activated the Script in a fresh process.")
        print("PASS: missing accelerator in fresh process gives no handoff", flush=True)
        print("LAB PASSED. No permanent shortcut installed.", flush=True)
    except Exception:
        hypo = root / "hyprland.log"
        if hypo.exists():
            print("PRIVATE COMPOSITOR DIAGNOSTICS:\n" + hypo.read_text()[-2500:], flush=True)
        path = root / "nautilus.log"
        if path.exists():
            print("PRIVATE NAUTILUS DIAGNOSTICS:\n" + path.read_text()[-2500:], flush=True)
        errors = root / "errors.jsonl"
        if errors.exists():
            print("PRIVATE ERROR RECORDS:", errors.read_text(), flush=True)
        if instance:
            active = json.loads(ctl("-j", "activewindow"))
            print("PRIVATE END FOCUS", {k: active.get(k) for k in ("class", "title", "address")}, flush=True)
        raise
    finally:
        stop(nautilus)
        stop(compositor)
        for log in logs:
            log.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manual", action="store_true", help="include a bounded 90-second physical-key test")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    inside(args.manual) if args.inside else outer(args.manual)
