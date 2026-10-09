#!/usr/bin/env python3
"""Bounded, opt-in Task C lab. Never changes the parent compositor's bindings."""

import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time

REPO = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def run(argv, env=None):
    return subprocess.run(argv, env=env, check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10).stdout


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def outer():
    for program in ("Hyprland", "hyprctl", "nautilus", "wtype", "dbus-run-session"):
        if not shutil.which(program):
            raise RuntimeError(f"Missing {program}; no packages will be installed.")
    parent_env = os.environ.copy()
    parent_binds = run(["hyprctl", "-j", "binds"])
    # Capture the parent socket before replacing XDG_RUNTIME_DIR. No input goes to it.
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
        # Unix socket names are limited to 107 bytes. Keep all files in the checkout
        # but address this private directory through a short, supervisor-owned FD.
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
        bus_config.write_text(
            '<busconfig><type>session</type><listen>unix:tmpdir=' + str(root / "run") +
            '</listen><auth>EXTERNAL</auth><policy context="default">'
            '<allow own="*"/><allow send_destination="*"/><allow receive_sender="*"/>'
            '</policy></busconfig>')
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
        while time.monotonic() < deadline and compositor.poll() is None:
            sockets = list((root / "run/hypr").glob("*/.socket.sock"))
            if sockets:
                instance = sockets[0].parent.name
                break
            time.sleep(0.1)
        if not instance:
            raise RuntimeError("Nested compositor did not start:\n" + (root / "hyprland.log").read_text()[-6000:])
        child_env["HYPRLAND_INSTANCE_SIGNATURE"] = instance
        # This socket is explicitly owned by the child, unlike the inherited parent socket.
        child_env["WAYLAND_DISPLAY"] = "wayland-1"

        def ctl(*args):
            return run(["hyprctl", "--instance", instance, *args], child_env)

        time.sleep(0.5)
        print("NESTED MONITORS", ctl("-j", "monitors"), flush=True)
        print("NESTED CONFIG ERRORS", ctl("configerrors"), flush=True)
        script_dir = root / "data/nautilus/scripts"
        script_dir.mkdir(parents=True)
        shutil.copy2(REPO / "integrations/nautilus/peek-nautilus.py", script_dir / "Peek")
        accel_dir = root / "config/nautilus"
        accel_dir.mkdir(parents=True)
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
        folder.mkdir()
        first = folder / "01 café ' quote.pdf"
        second = folder / "02 日本 image.png"
        first.touch()
        second.touch()
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


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    inside() if args.inside else outer()
