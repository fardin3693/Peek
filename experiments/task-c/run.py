#!/usr/bin/env python3
<<<<<<< HEAD
"""Bounded, opt-in Task C lab. Never changes the parent compositor's bindings."""

import argparse
=======
"""Opt-in, bounded nested Wayland lab; never binds keys on the live desktop."""

import argparse
import hashlib
>>>>>>> d7d7b68 (the last zed ai commit.)
import json
import os
from pathlib import Path
import shutil
import signal
<<<<<<< HEAD
=======
import stat
>>>>>>> d7d7b68 (the last zed ai commit.)
import subprocess
import sys
import tempfile
import time

REPO = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
<<<<<<< HEAD


def run(argv, env=None):
    return subprocess.run(argv, env=env, check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10).stdout


def stop(process):
    if process.poll() is None:
=======
CLASS = "org.gnome.Nautilus"


def run(argv, env=None):
    result = subprocess.run(argv, env=env, check=False, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
    if result.returncode:
        raise RuntimeError(f"{argv!r}: {result.stderr or result.stdout}")
    return result.stdout


def stop(process):
    if process and process.poll() is None:
>>>>>>> d7d7b68 (the last zed ai commit.)
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


<<<<<<< HEAD
def outer():
    for program in ("Hyprland", "hyprctl", "nautilus", "wtype", "dbus-run-session"):
        if not shutil.which(program):
            raise RuntimeError(f"Missing {program}; no packages will be installed.")
    parent_env = os.environ.copy()
    parent_binds = run(["hyprctl", "-j", "binds"])
    # Capture the parent socket before replacing XDG_RUNTIME_DIR. No input goes to it.
=======
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
>>>>>>> d7d7b68 (the last zed ai commit.)
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
<<<<<<< HEAD
        # Unix socket names are limited to 107 bytes. Keep all files in the checkout
        # but address this private directory through a short, supervisor-owned FD.
=======
        # Socket names allow only 107 bytes. A short supervisor-owned directory FD
        # keeps all artifacts under build/ without requiring files in /tmp or HOME.
>>>>>>> d7d7b68 (the last zed ai commit.)
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
<<<<<<< HEAD
=======
        # No activation directories: cannot start the real desktop's services.
>>>>>>> d7d7b68 (the last zed ai commit.)
        bus_config.write_text(
            '<busconfig><type>session</type><listen>unix:tmpdir=' + str(root / "run") +
            '</listen><auth>EXTERNAL</auth><policy context="default">'
            '<allow own="*"/><allow send_destination="*"/><allow receive_sender="*"/>'
            '</policy></busconfig>')
<<<<<<< HEAD
        print("Starting test-owned nested session; live desktop bindings/config are untouched.", flush=True)
        process = subprocess.Popen(
            ["dbus-run-session", "--config-file", str(bus_config), "--",
             sys.executable, "-B", str(__file__), "--inside"], env=env, start_new_session=True)
        try:
            status = process.wait(timeout=120)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            os.killpg(process.pid, signal.SIGTERM)
            process.wait(timeout=10)
            raise
        finally:
            # Kill only this lab's process group, including Script grandchildren.
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            os.close(runtime_fd)
            assert run(["hyprctl", "-j", "binds"]) == parent_binds, "Parent binding table changed!"
        if status:
            raise RuntimeError(f"Nested lab exited {status}.")


def inside():
    root = Path(os.environ["PEEK_LAB_ROOT"])
    child_env = os.environ.copy()
    compositor_log = (root / "hyprland.log").open("w")
    compositor = subprocess.Popen(["Hyprland", "--config", str(HERE / "nested.lua")],
                                  stdout=compositor_log, stderr=subprocess.STDOUT)
    nautilus = None
    try:
        # Bounded compositor-readiness observation, never selection discovery/polling.
        deadline = time.monotonic() + 15
        instance = None
=======
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
>>>>>>> d7d7b68 (the last zed ai commit.)
        while time.monotonic() < deadline and compositor.poll() is None:
            sockets = list((root / "run/hypr").glob("*/.socket.sock"))
            if sockets:
                instance = sockets[0].parent.name
                break
            time.sleep(0.1)
        if not instance:
<<<<<<< HEAD
            raise RuntimeError("Nested compositor did not start:\n" + (root / "hyprland.log").read_text()[-6000:])
        child_env["HYPRLAND_INSTANCE_SIGNATURE"] = instance
        # This socket is explicitly owned by the child, unlike the inherited parent socket.
        child_env["WAYLAND_DISPLAY"] = "wayland-1"

        def ctl(*args):
            return run(["hyprctl", "--instance", instance, *args], child_env)

        time.sleep(0.5)
        print("NESTED MONITORS", ctl("-j", "monitors"), flush=True)
        print("NESTED CONFIG ERRORS", ctl("configerrors"), flush=True)
=======
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

        def wait_client(predicate):
            deadline = time.monotonic() + 10
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

        def expect_path(path, before, label):
            actual = records("argv.jsonl")
            if len(actual) != before + 1 or actual[-1] != ["--", str(path)]:
                raise RuntimeError(f"{label}: expected one fresh argv for {path!s}; got {actual[before:]!r}")
            print("PASS:", label, flush=True)

        def select(path):
            run(["nautilus", "--select", str(path)], env)
            time.sleep(0.15)
            # A remote CLI request has no user activation token on Wayland. The
            # test explicitly focuses the owning fixture window; the bind never does.
            focus(wait_client(lambda w: w["class"] == CLASS and w["title"] == path.parent.name))

        time.sleep(0.3)
        if ctl("configerrors").strip():
            raise RuntimeError("Private compositor config errors: " + ctl("configerrors"))
>>>>>>> d7d7b68 (the last zed ai commit.)
        script_dir = root / "data/nautilus/scripts"
        script_dir.mkdir(parents=True)
        shutil.copy2(REPO / "integrations/nautilus/peek-nautilus.py", script_dir / "Peek")
        accel_dir = root / "config/nautilus"
        accel_dir.mkdir(parents=True)
<<<<<<< HEAD
        (accel_dir / "scripts-accels").write_text("<Control><Alt>F12 Peek\n")
        bin_dir = root / "bin"
        bin_dir.mkdir()
        recorder = bin_dir / "peek-recorder"
        recorder.write_text(
            "#!" + sys.executable + "\n"
            "import json, os, sys\n"
            "from pathlib import Path\n"
            "root = Path(os.environ['PEEK_LAB_ROOT'])\n"
            "fd = os.open(root/'argv.jsonl', os.O_WRONLY|os.O_CREAT|os.O_APPEND, 0o600)\n"
            "os.write(fd, (json.dumps(sys.argv[1:])+'\\n').encode()); os.close(fd)\n")
        recorder.chmod(0o700)
        notifier = bin_dir / "notify-send"
        notifier.write_text(
            "#!" + sys.executable + "\n"
            "import json, os, sys\n"
            "from pathlib import Path\n"
            "fd=os.open(Path(os.environ['PEEK_LAB_ROOT'])/'errors.jsonl', os.O_WRONLY|os.O_CREAT|os.O_APPEND, 0o600)\n"
            "os.write(fd,(json.dumps(sys.argv[1:])+'\\n').encode()); os.close(fd)\n")
        notifier.chmod(0o700)
        child_env.update(PATH=str(bin_dir) + ":/usr/bin:/bin", PEEK_EXECUTABLE=str(recorder))
        folder = root / "fixtures"
=======
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
>>>>>>> d7d7b68 (the last zed ai commit.)
        folder.mkdir()
        first = folder / "01 café ' quote.pdf"
        second = folder / "02 日本 image.png"
        first.touch()
        second.touch()
<<<<<<< HEAD
        log = (root / "nautilus.log").open("w")
        nautilus = subprocess.Popen(["nautilus", "--select", str(first)], env=child_env,
                                    stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            clients = json.loads(ctl("-j", "clients"))
            if clients:
                break
            time.sleep(0.1)
        if not clients:
            raise RuntimeError("No private Nautilus window:\n" + (root / "nautilus.log").read_text())
        time.sleep(1)
        print("NESTED CLIENTS", ctl("-j", "clients"), flush=True)
        run(["wtype", "-M", "ctrl", "-M", "alt", "-P", "F12", "-p", "F12", "-m", "alt", "-m", "ctrl"], child_env)
        time.sleep(0.5)
        print("DIRECT ACCEL ARGV", (root / "argv.jsonl").read_text() if (root / "argv.jsonl").exists() else "<absent>", flush=True)
        run(["wtype", "-M", "ctrl", "-M", "alt", "-M", "shift", "-P", "F12", "-p", "F12", "-m", "shift", "-m", "alt", "-m", "ctrl"], child_env)
        time.sleep(0.5)
        print("FORWARD ARGV", (root / "argv.jsonl").read_text() if (root / "argv.jsonl").exists() else "<absent>", flush=True)
        print("NAUTILUS LOG", (root / "nautilus.log").read_text()[-4000:], flush=True)
        assert (root / "argv.jsonl").exists(), "No actual Script invocation occurred."
        assert len((root / "argv.jsonl").read_text().splitlines()) == 2
    finally:
        if nautilus:
            stop(nautilus)
        stop(compositor)
        compositor_log.close()
=======
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
            window = wait_client(lambda w: w["class"] == CLASS)
            time.sleep(1.2)  # Directory/menu initialization; no selection bridge is sampled.
            # New-window mapping establishes focus; do not warp the pointer here.
            return process

        nautilus = start_nautilus(first)
        before = len(records("argv.jsonl"))
        chord(direct=True)
        expect_path(first, before, "dedicated Nautilus accelerator → actual Task B Script")
        before += 1
        chord()
        expect_path(first, before, "Hyprland forwarding → exact focused Nautilus window")
        for index in range(4):
            select(first)
            before = len(records("argv.jsonl"))
            # Arrow changes selection and trigger immediately follows in one input stream.
            chord("-k", "Right")
            expect_path(second, before, f"immediate selection change then trigger {index + 1}/4")
        select(first)
        before = len(records("argv.jsonl"))
        chord(hold=True)
        expect_path(first, before, "600ms held trigger produces one invocation")
        before = len(records("argv.jsonl"))
        chord(modifiers_first=True)
        expect_path(first, before, "qualifying modifiers released before F12")
        before = len(records("argv.jsonl"))
        chord("-M", "ctrl", "-k", "a", "-m", "ctrl")
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
            expect_path(path, before, f"two-window focus alternation {index + 1}/4")
        focus(window_one)
        before = len(records("argv.jsonl"))
        chord("-M", "ctrl", "-k", "l", "-m", "ctrl")
        expect_path(second, before, "location-field focus still invokes Script (documented limitation)")
        keys("-k", "Escape")

        run(["nautilus", "--new-window", str(empty)], env)
        empty_window = wait_client(lambda w: w["class"] == CLASS and w["title"] == "empty")
        time.sleep(0.4)
        focus(empty_window)
        before = len(records("argv.jsonl"))
        errors_before = len(records("errors.jsonl"))
        chord()
        if len(records("argv.jsonl")) != before or len(records("errors.jsonl")) != errors_before + 1:
            raise RuntimeError("Empty selection did not fail without launching Peek.")
        if "No selection" not in records("errors.jsonl")[-1][-1]:
            raise RuntimeError("Missing no-selection diagnostic.")
        print("PASS: no-selection failure, no native launch", flush=True)

        (root / "native-mode").touch()
        select(first)
        before = len(records("argv.jsonl"))
        chord()
        expect_path(first, before, "actual native Peek launched with selected special-character path")
        peek_window = wait_client(lambda w: w["class"] == "peek")
        focus(peek_window)
        before = len(records("argv.jsonl"))
        chord()
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
        expect_path(broken, before, "broken symlink selection reaches native validation")
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
        if len(records("argv.jsonl")) != before or len(records("errors.jsonl")) != errors_before + 1:
            raise RuntimeError("Missing executable did not fail clearly.")
        if "Cannot launch Peek" not in records("errors.jsonl")[-1][-1]:
            raise RuntimeError("Missing launch-failure diagnostic.")
        print("PASS: keyboard-triggered executable launch failure", flush=True)

        # Removing only the lab's file cannot change the already-loaded process table.
        accelerator.unlink()
        errors_before = len(records("errors.jsonl"))
        chord()
        if len(records("errors.jsonl")) != errors_before + 1:
            raise RuntimeError("Accelerator process-lifetime behavior changed unexpectedly.")
        print("PASS: accelerator changes do not reload a running Nautilus process", flush=True)
        stop(nautilus)
        nautilus = start_nautilus(first)
        errors_before = len(records("errors.jsonl"))
        chord()
        if len(records("errors.jsonl")) != errors_before:
            raise RuntimeError("Missing accelerator still activated the Script in a fresh process.")
        print("PASS: missing accelerator in fresh process gives no handoff", flush=True)
        print("LAB PASSED. No permanent shortcut installed.", flush=True)
    except Exception:
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
>>>>>>> d7d7b68 (the last zed ai commit.)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
<<<<<<< HEAD
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    inside() if args.inside else outer()
=======
    parser.add_argument("--manual", action="store_true", help="include a bounded 90-second physical-key test")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    inside(args.manual) if args.inside else outer(args.manual)
>>>>>>> d7d7b68 (the last zed ai commit.)
