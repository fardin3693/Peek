#!/usr/bin/env bash
# P-START1 diagnostic driver — measurement only, no app changes.
#
# Builds two temp diagnostics to /tmp (never CMake/CTest/shipped):
#   probe_timeline : unmodified Peek sources + per-stage monotonic stamps
#                    (MAIN/QAPP/PARSE/VALIDATE/SUPPORT_*/CTOR/SHOW/PAINT)
#   control        : minimal QApplication+QLabel+show (shared-floor control)
# Then runs: per-fixture native timelines, one offscreen timeline, theme
# A/B on native --version, /usr/bin/true spawn floor, plugin inventory,
# and mapped-library breadth. Parent adds Hyprland socket2 openwindow times.
#
# Usage: docs/performance/diag_start1.sh [--peek PATH] [--outdir DIR]
set -u
PEEK=./build/release/peek
OUTDIR=docs/performance/results-start1
while [ $# -gt 0 ]; do
    case "$1" in
        --peek) PEEK="$2"; shift 2 ;;
        --outdir) OUTDIR="$2"; shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done
if [ ! -x "$PEEK" ]; then echo "peek not executable: $PEEK" >&2; exit 2; fi
mkdir -p "$OUTDIR"
FIXDIR=docs/performance/results/fixtures

QTLIBS="/usr/lib/libQt6MultimediaWidgets.so.6.11.2 /usr/lib/libQt6Widgets.so.6.11.2 \
/usr/lib/libQt6Multimedia.so.6.11.2 /usr/lib/libQt6Network.so.6.11.2 \
/usr/lib/libQt6Gui.so.6.11.2 /usr/lib/libGLX.so /usr/lib/libOpenGL.so \
/usr/lib/libQt6Core.so.6.11.2 /usr/lib/libpoppler-cpp.so"
QTDEFS='-DPEEK_VERSION="0.0.1" -DPEEK_WITH_MULTIMEDIA -DPEEK_WITH_PDF
-DQT_CORE_LIB -DQT_GUI_LIB -DQT_MULTIMEDIAWIDGETS_LIB -DQT_MULTIMEDIA_LIB
-DQT_NETWORK_LIB -DQT_NO_DEBUG -DQT_WIDGETS_LIB'
QTINC="-I src -isystem /usr/include/qt6/QtWidgets -isystem /usr/include/qt6 \
-isystem /usr/include/qt6/QtCore -isystem /usr/lib/qt6/mkspecs/linux-g++ \
-isystem /usr/include/qt6/QtGui -isystem /usr/include/qt6/QtMultimedia \
-isystem /usr/include/qt6/QtNetwork -isystem /usr/include/qt6/QtMultimediaWidgets \
-isystem /usr/include/poppler/cpp -isystem /usr/include/poppler"

TMPD=$(mktemp -d /tmp/peek-start1-XXXXXX)
# shellcheck disable=SC2086
g++ $QTDEFS $QTINC -O2 -DNDEBUG -std=c++17 -mno-direct-extern-access \
    -o "$TMPD/timeline" docs/performance/probe_timeline.cpp \
    src/preview_request.cpp src/preview_window.cpp src/text_preview.cpp \
    src/text_provider.cpp src/image_provider.cpp src/video_preview.cpp \
    src/audio_preview.cpp src/audio_provider.cpp src/pdf_preview.cpp \
    src/pdf_provider.cpp $QTLIBS || exit 2
# shellcheck disable=SC2086
g++ -DQT_CORE_LIB -DQT_GUI_LIB -DQT_NO_DEBUG -DQT_WIDGETS_LIB \
    -isystem /usr/include/qt6/QtWidgets -isystem /usr/include/qt6 \
    -isystem /usr/include/qt6/QtCore -isystem /usr/lib/qt6/mkspecs/linux-g++ \
    -isystem /usr/include/qt6/QtGui \
    -O2 -DNDEBUG -std=c++17 -mno-direct-extern-access \
    -o "$TMPD/control" docs/performance/control_qwidget.cpp \
    /usr/lib/libQt6Widgets.so.6.11.2 /usr/lib/libQt6Gui.so.6.11.2 \
    /usr/lib/libQt6Core.so.6.11.2 || exit 2
echo "built: $TMPD/timeline $TMPD/control"

export PEEK_BIN="$PEEK" PEEK_FIXDIR="$FIXDIR" PEEK_OUTDIR="$OUTDIR" PEEK_TMPD="$TMPD"
python3 - <<'EOF'
import os, socket, subprocess, time, signal, statistics, select

peek = os.environ["PEEK_BIN"]
fix = os.environ["PEEK_FIXDIR"]
outdir = os.environ["PEEK_OUTDIR"]
tmpd = os.environ["PEEK_TMPD"]
sig = os.environ["HYPRLAND_INSTANCE_SIGNATURE"]
rt = os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
sockpath = f"{rt}/hypr/{sig}/.socket2.sock"
env_native = dict(os.environ)
env_native.pop("QT_QPA_PLATFORM", None)

def stats(xs):
    xs = sorted(xs); n = len(xs)
    return {"n": n, "min": xs[0], "p50": statistics.median(xs), "max": xs[-1]}

def run_gui(binary, args, title_frag, iters=6, extra_env=None):
    """Spawn a GUI probe; return list of dicts {STAGE_*: ms, OPEN: ms|None}."""
    rows = []
    for i in range(iters):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(sockpath); s.settimeout(12)
        t0 = time.clock_gettime(time.CLOCK_MONOTONIC)
        penv = dict(env_native, PEEK_T0_NS=str(time.clock_gettime_ns(time.CLOCK_MONOTONIC)))
        if extra_env: penv.update(extra_env)
        p = subprocess.Popen([binary, *args], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, env=penv)
        stages, opened, buf, leftover = {}, None, b"", b""
        deadline = t0 + 12.0
        fd = p.stdout.fileno()
        while time.clock_gettime(time.CLOCK_MONOTONIC) < deadline:
            s.settimeout(0.05)
            try:
                while True:
                    chunk = s.recv(4096)
                    if not chunk: break
                    leftover += chunk
                    while b"\n" in leftover:
                        line, leftover = leftover.split(b"\n", 1)
                        t = line.decode(errors="replace")
                        if t.startswith("openwindow>>") and title_frag in t:
                            opened = (time.clock_gettime(time.CLOCK_MONOTONIC) - t0) * 1000.0
            except socket.timeout:
                pass
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                chunk = os.read(fd, 4096)
                if chunk:
                    buf += chunk
                    for line in buf.decode(errors="replace").splitlines():
                        parts = line.split()
                        if len(parts) == 2 and (parts[0].startswith("STAGE_") or parts[0].startswith("SUPPORT_")):
                            try: stages[parts[0]] = float(parts[1])
                            except ValueError: pass
            if "STAGE_PAINT" in stages and opened is not None:
                break
        if p.poll() is None:
            p.send_signal(signal.SIGTERM)
            try: p.wait(timeout=10)
            except subprocess.TimeoutExpired: p.kill(); p.wait()
        s.close()
        stages["OPEN"] = opened
        rows.append(stages)
        print(f"  iter{i}: " + " ".join(f"{k}={round(v,1) if v is not None else 'None'}" for k, v in sorted(stages.items())))
    return rows

def summarize(rows, skip=1):
    keys = sorted({k for r in rows[skip:] for k in r if r[k] is not None and isinstance(r[k], float)})
    out = {}
    for k in keys:
        xs = [r[k] for r in rows[skip:] if isinstance(r[k], float)]
        if xs: out[k] = stats(xs)
    return out

print("=== timeline: native Wayland, 1 warmup + 5 samples ===")
cases = [("sample.txt", "tl_text"), ("sample.pdf", "tl_pdf"),
         ("sample.png", "tl_image"), ("sample.wav", "tl_audio"),
         ("sample.mp4", "tl_video"), ("fallback.bin", "tl_fallback")]
for fn, name in cases:
    print(f"-- {fn}")
    rows = run_gui(f"{tmpd}/timeline", ["--", f"{fix}/{fn}"], "Peek — ")
    with open(f"{outdir}/{name}.csv", "w") as f:
        keys = ["STAGE_MAIN", "STAGE_QAPP", "STAGE_PARSE", "STAGE_VALIDATE",
                "SUPPORT_TEXT", "SUPPORT_PDF", "SUPPORT_AUDIO", "SUPPORT_VIDEO",
                "SUPPORT_IMAGE", "STAGE_CTOR", "STAGE_SHOW", "STAGE_PAINT", "OPEN"]
        f.write("iter," + ",".join(keys) + "\n")
        for i, r in enumerate(rows):
            f.write(str(i) + "," + ",".join(str(r.get(k, "")) for k in keys) + "\n")
    s = summarize(rows)
    if s:
        seg = lambda a, b: (s[b]['p50'] - s[a]['p50']) if a in s and b in s else float('nan')
        print(f"{name} p50: MAIN={s['STAGE_MAIN']['p50']:.1f} QAPP={s['STAGE_QAPP']['p50']:.1f} "
              f"PARSE={s['STAGE_PARSE']['p50']:.1f} VAL={s['STAGE_VALIDATE']['p50']:.1f} "
              f"CTOR={s['STAGE_CTOR']['p50']:.1f} SHOW={s['STAGE_SHOW']['p50']:.1f} "
              f"PAINT={s['STAGE_PAINT']['p50']:.1f} OPEN={s.get('OPEN', {}).get('p50', float('nan')):.1f} | "
              f"dQAPP={seg('STAGE_MAIN','STAGE_QAPP'):.0f} dCTOR={seg('STAGE_VALIDATE','STAGE_CTOR'):.0f} "
              f"dSHOW_PAINT={seg('STAGE_SHOW','STAGE_PAINT'):.0f} dPAINT_OPEN={seg('STAGE_PAINT','OPEN'):.0f}")

print("=== timeline: offscreen text (platform contrast, no OPEN) ===")
env_off = dict(env_native, QT_QPA_PLATFORM="offscreen", QT_QPA_PLATFORMTHEME="")
rows = run_gui(f"{tmpd}/timeline", ["--", f"{fix}/sample.txt"], "Peek — ", extra_env=env_off)
s = summarize(rows)
if s:
    print(f"tl_text_offscreen p50: MAIN={s['STAGE_MAIN']['p50']:.1f} QAPP={s['STAGE_QAPP']['p50']:.1f} "
          f"CTOR={s['STAGE_CTOR']['p50']:.1f} PAINT={s['STAGE_PAINT']['p50']:.1f}")

print("=== control: minimal QLabel window, native (1+5) ===")
rows = run_gui(f"{tmpd}/control", [], "PeekCtl", iters=6)
with open(f"{outdir}/tl_control.csv", "w") as f:
    f.write("iter,STAGE_CTOR,STAGE_SHOW,STAGE_PAINT,OPEN\n")
    for i, r in enumerate(rows):
        f.write(f"{i},{r.get('STAGE_CTOR','')},{r.get('STAGE_SHOW','')},{r.get('STAGE_PAINT','')},{r.get('OPEN','')}\n")
s = summarize(rows)
if s:
    print(f"control p50: CTOR={s['STAGE_CTOR']['p50']:.1f} SHOW={s['STAGE_SHOW']['p50']:.1f} "
          f"PAINT={s['STAGE_PAINT']['p50']:.1f} OPEN={s.get('OPEN', {}).get('p50', float('nan')):.1f}")

print("=== theme A/B: native --version, gtk3 vs empty theme (N=11 each) ===")
for theme, label in [("gtk3", "theme_gtk3"), ("", "theme_empty")]:
    walls = []
    for i in range(12):
        e = dict(env_native, QT_QPA_PLATFORMTHEME=theme)
        t0 = time.clock_gettime(time.CLOCK_MONOTONIC)
        p = subprocess.Popen([peek, "--version"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=e)
        _, status, _ = os.wait4(p.pid, 0)
        t1 = time.clock_gettime(time.CLOCK_MONOTONIC)
        assert os.waitstatus_to_exitcode(status) == 0
        p.stdout.read(); p.stderr.read()
        if i > 0: walls.append((t1 - t0) * 1000.0)
    st = stats(walls)
    print(f"{label}: n={st['n']} min={st['min']:.1f} p50={st['p50']:.1f} max={st['max']:.1f}")
    with open(f"{outdir}/{label}.csv", "w") as f:
        f.write("wall_ms\n" + "\n".join(f"{w:.3f}" for w in walls) + "\n")

print("=== spawn floor: fork/exec /usr/bin/true (N=11) ===")
walls = []
for i in range(12):
    t0 = time.clock_gettime(time.CLOCK_MONOTONIC)
    p = subprocess.Popen(["/usr/bin/true"])
    _, status, _ = os.wait4(p.pid, 0)
    t1 = time.clock_gettime(time.CLOCK_MONOTONIC)
    if i > 0: walls.append((t1 - t0) * 1000.0)
st = stats(walls)
print(f"spawn_true: n={st['n']} min={st['min']:.2f} p50={st['p50']:.2f} max={st['max']:.2f}")

print("=== plugin inventory (QT_DEBUG_PLUGINS=1, --version) ===")
for plat, label in [(None, "native"), ("offscreen", "offscreen")]:
    e = dict(env_native, QT_DEBUG_PLUGINS="1")
    if plat: e["QT_QPA_PLATFORM"] = plat; e["QT_QPA_PLATFORMTHEME"] = ""
    p = subprocess.run([peek, "--version"], capture_output=True, env=e, timeout=30)
    libs = sorted({l.split("checking")[1].strip().strip('"') for l in p.stderr.decode(errors="replace").splitlines() if "checking" in l and ".so" in l} |
                  {l.rsplit(" ", 1)[-1].strip().strip('"') for l in p.stderr.decode(errors="replace").splitlines() if "loaded library" in l})
    print(f"plugins_{label}: {len(libs)} libraries")
    with open(f"{outdir}/plugins_{label}.txt", "w") as f:
        f.write("\n".join(libs) + "\n")
    for l in libs[:40]: print(f"  {l}")

print("=== mapped-library breadth: held text preview vs control ===")
for binary, args, label in [(peek, ["--", f"{fix}/sample.txt"], "maps_peek"),
                            (f"{tmpd}/control", [], "maps_control")]:
    p = subprocess.Popen([binary, *args], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env_native)
    time.sleep(2.0)
    sos = set()
    try:
        with open(f"/proc/{p.pid}/maps") as f:
            for line in f:
                if ".so" in line and "/" in line:
                    sos.add(line.rsplit(" ", 1)[-1].strip())
    finally:
        if p.poll() is None:
            p.send_signal(signal.SIGTERM)
            try: p.wait(timeout=10)
            except subprocess.TimeoutExpired: p.kill(); p.wait()
    print(f"{label}: {len(sos)} mapped .so files")
    with open(f"{outdir}/{label}.txt", "w") as f:
        f.write("\n".join(sorted(sos)) + "\n")
print("CSVs in", outdir)
EOF
echo "tmpdir (delete after review): $TMPD"
