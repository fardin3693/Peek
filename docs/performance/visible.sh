#!/usr/bin/env bash
# P-PERF2 visible-preview harness — measurement only, no app changes.
#
# Signals (all on the real Hyprland/Wayland session, native Qt platform):
#   native_cli_version : `peek --version` with the ambient desktop env
#       (QT_QPA_PLATFORM=wayland;xcb, gtk3 theme) — launch -> exit.
#   CTOR_MS            temp probe: validator + PreviewWindow ctor
#       (synchronous decode for text/image/PDF; paused controls for media).
#   FIRST_PAINT        temp probe: first QEvent::Paint on any widget after
#       show(). First Qt paint request — NOT proof of pixels on screen.
#   OPENWINDOW         Hyprland socket2 `openwindow` event for the probe
#       window (compositor mapped the surface). A mapped surface may still
#       show an empty frame — the harness screenshots at openwindow and
#       +500ms and reports the non-dominant-color fraction of each shot.
#
# The probe (probe_firstpaint.cpp) is compiled to /tmp/peek-perf2/ at run
# time from the unmodified Peek sources. It is never added to CMake/CTest,
# never shipped, and never runs outside this benchmark. No readiness
# protocol, daemon, IPC, polling loop, or runtime dependency is added to Peek.
#
# Effect on the live desktop: one small preview window pops up per
# iteration (~40 total) and is closed by SIGTERM after ~1.2 s. Screenshots
# go to /tmp, are reduced to two fractions, and are deleted immediately.
#
# Usage: docs/performance/visible.sh [--peek PATH] [--outdir DIR]
set -u
PEEK=./build/release/peek
OUTDIR=docs/performance/results-visible
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
for f in sample.txt sample.png sample.pdf sample.wav sample.mp4; do
    [ -f "$FIXDIR/$f" ] || { echo "missing fixture $FIXDIR/$f" >&2; exit 2; }
done

# --- build the temp probe (mirrors the release compile, output to /tmp) ---
# NOTE: -mno-direct-extern-access is required: Qt's CMake targets supply it
# and plain g++ links fail on Qt's protected symbols without it.
TMPD=$(mktemp -d /tmp/peek-perf2-XXXXXX)
g++ -DPEEK_VERSION=\"0.0.1\" -DPEEK_WITH_MULTIMEDIA -DPEEK_WITH_PDF \
    -DQT_CORE_LIB -DQT_GUI_LIB -DQT_MULTIMEDIAWIDGETS_LIB -DQT_MULTIMEDIA_LIB \
    -DQT_NETWORK_LIB -DQT_NO_DEBUG -DQT_WIDGETS_LIB \
    -I src -isystem /usr/include/qt6/QtWidgets -isystem /usr/include/qt6 \
    -isystem /usr/include/qt6/QtCore -isystem /usr/lib/qt6/mkspecs/linux-g++ \
    -isystem /usr/include/qt6/QtGui -isystem /usr/include/qt6/QtMultimedia \
    -isystem /usr/include/qt6/QtNetwork -isystem /usr/include/qt6/QtMultimediaWidgets \
    -isystem /usr/include/poppler/cpp -isystem /usr/include/poppler \
    -O2 -DNDEBUG -std=c++17 -mno-direct-extern-access \
    -o "$TMPD/probe" docs/performance/probe_firstpaint.cpp \
    src/preview_request.cpp src/preview_window.cpp src/text_preview.cpp \
    src/text_provider.cpp src/image_provider.cpp src/video_preview.cpp \
    src/audio_preview.cpp src/audio_provider.cpp src/pdf_preview.cpp \
    src/pdf_provider.cpp \
    /usr/lib/libQt6MultimediaWidgets.so.6.11.2 /usr/lib/libQt6Widgets.so.6.11.2 \
    /usr/lib/libQt6Multimedia.so.6.11.2 /usr/lib/libQt6Network.so.6.11.2 \
    /usr/lib/libQt6Gui.so.6.11.2 /usr/lib/libGLX.so /usr/lib/libOpenGL.so \
    /usr/lib/libQt6Core.so.6.11.2 /usr/lib/libpoppler-cpp.so || exit 2
echo "probe built: $TMPD/probe"

export PEEK_BIN="$PEEK" PEEK_FIXDIR="$FIXDIR" PEEK_OUTDIR="$OUTDIR" PEEK_PROBE="$TMPD/probe"
python3 - <<'EOF'
import json, os, socket, struct, subprocess, time, signal, statistics, zlib

peek = os.environ["PEEK_BIN"]
fix = os.environ["PEEK_FIXDIR"]
outdir = os.environ["PEEK_OUTDIR"]
probe = os.environ["PEEK_PROBE"]
sig = os.environ["HYPRLAND_INSTANCE_SIGNATURE"]
rt = os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
sockpath = f"{rt}/hypr/{sig}/.socket2.sock"
# Native session env: ambient platform (wayland;xcb) and theme. No offscreen.
env = dict(os.environ)
env.pop("QT_QPA_PLATFORM", None)  # never force a platform here; unset only if WE set it

def stats(xs):
    xs = sorted(xs)
    n = len(xs)
    def pct(p):
        k = (n - 1) * p / 100.0
        f, c = int(k), min(int(k) + 1, n - 1)
        return xs[f] + (xs[c] - xs[f]) * (k - f)
    return {"n": n, "min": xs[0], "p50": statistics.median(xs),
            "p90": pct(90), "max": xs[-1]}

# --- (a) native CLI timing, same wait4 method as bench.sh ---
print("=== native CLI: peek --version (ambient Wayland session) ===")
walls, rss = [], []
for i in range(22):
    t0 = time.clock_gettime(time.CLOCK_MONOTONIC)
    p = subprocess.Popen([peek, "--version"], stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, env=env)
    _, status, ru = os.wait4(p.pid, 0)
    t1 = time.clock_gettime(time.CLOCK_MONOTONIC)
    assert os.waitstatus_to_exitcode(status) == 0
    p.stdout.read(); p.stderr.read()
    if i > 0:
        walls.append((t1 - t0) * 1000.0); rss.append(float(ru.ru_maxrss))
sw, sr = stats(walls), stats(rss)
print(f"native_cli_version: wall_ms n={sw['n']} min={sw['min']:.1f} p50={sw['p50']:.1f} "
      f"p90={sw['p90']:.1f} max={sw['max']:.1f} | peakRSS_KiB p50={sr['p50']:.0f} range={sr['min']:.0f}-{sr['max']:.0f}")
with open(f"{outdir}/native_cli_version.csv", "w") as f:
    f.write("wall_ms,peak_rss_kb\n")
    for w, r in zip(walls, rss):
        f.write(f"{w:.3f},{r:.0f}\n")

# --- PNG shot analysis (stdlib only): fraction of pixels NOT the dominant RGB ---
def nonblank_fraction(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, w, h, bitd, ctype, idat = 8, 0, 0, 0, 0, b""
    while pos < len(data):
        (ln,) = struct.unpack(">I", data[pos:pos + 4]); typ = data[pos + 4:pos + 8]
        if typ == b"IHDR":
            w, h, bitd, ctype = struct.unpack(">IIBB", data[pos + 8:pos + 18])[:4]
        elif typ == b"IDAT":
            idat += data[pos + 8:pos + 8 + ln]
        elif typ == b"IEND":
            break
        pos += 12 + ln
    if bitd != 8 or ctype not in (2, 6) or w == 0 or h == 0:
        return None  # unsupported PNG shape; report, don't guess
    ch = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * ch
    out = bytearray(w * h * 3)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        flt = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        if flt == 1:
            for i in range(ch, stride): line[i] = (line[i] + line[i - ch]) & 255
        elif flt == 2:
            for i in range(stride): line[i] = (line[i] + prev[i]) & 255
        elif flt == 3:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif flt == 4:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                b = prev[i]; c = prev[i - ch] if i >= ch else 0
                q = a + b - c
                pa, pb, pc = abs(q - a), abs(q - b), abs(q - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        elif flt != 0:
            return None
        for x in range(w):
            o = (y * w + x) * 3; s = x * ch
            out[o:o + 3] = line[s:s + 3]
        prev = line
    hist = {}
    for i in range(0, len(out), 3):
        k = bytes(out[i:i + 3])
        hist[k] = hist.get(k, 0) + 1
    return 1.0 - max(hist.values()) / (w * h)

def window_geom(pid):
    try:
        clients = json.loads(subprocess.run(["hyprctl", "clients", "-j"],
                                            capture_output=True, timeout=10).stdout)
    except Exception:
        return None
    for c in clients:
        if c.get("pid") == pid and "Peek" in c.get("title", ""):
            x, y = c["at"]; w, h = c["size"]
            return f"{x},{y} {w}x{h}"
    return None

def shot_nonblank(geom, tag):
    path = f"/tmp/peek-perf2-{tag}.png"
    try:
        r = subprocess.run(["grim", "-g", geom, path],
                           capture_output=True, timeout=15)
        if r.returncode != 0 or not os.path.exists(path):
            return "GRIM_FAIL"
        v = nonblank_fraction(path)
        return "UNSUPPORTED_PNG" if v is None else round(v, 4)
    except Exception:
        return "SHOT_ERROR"
    finally:
        try: os.remove(path)
        except OSError: pass

# --- (b) visible runs per fixture ---
print("=== visible runs (native Wayland; 1 warmup + 7 samples each) ===")
summary = {}
cases = [("sample.txt", "visible_text"), ("sample.png", "visible_image"),
         ("sample.pdf", "visible_pdf"), ("sample.wav", "visible_audio"),
         ("sample.mp4", "visible_video")]
for fn, name in cases:
    rows = []
    for i in range(8):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(sockpath); s.settimeout(12)
        t0 = time.clock_gettime(time.CLOCK_MONOTONIC)
        penv = dict(env, PEEK_T0_NS=str(time.clock_gettime_ns(time.CLOCK_MONOTONIC)))
        # t0 (float, above) and PEEK_T0_NS (int, here) are consecutive clock
        # reads: FIRST_PAINT and openwindow share the same zero within ~1us.
        p = subprocess.Popen([probe, "--", f"{fix}/{fn}"], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, env=penv)
        ctor_ms, paint_ms, open_ms = None, None, None
        buf = b""
        import select
        fd = p.stdout.fileno()
        opened = False
        deadline = t0 + 12.0
        leftover = b""
        while time.clock_gettime(time.CLOCK_MONOTONIC) < deadline:
            # drain socket2 events
            s.settimeout(0.05)
            try:
                while True:
                    chunk = s.recv(4096)
                    if not chunk: break
                    leftover += chunk
                    while b"\n" in leftover:
                        line, leftover = leftover.split(b"\n", 1)
                        t = line.decode(errors="replace")
                        if t.startswith("openwindow>>") and "Peek — " in t:
                            open_ms = (time.clock_gettime(time.CLOCK_MONOTONIC) - t0) * 1000.0
                            opened = True
            except socket.timeout:
                pass
            # drain probe stdout (nonblocking-ish via select)
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                chunk = os.read(fd, 4096)
                if chunk:
                    buf += chunk
                    for line in buf.decode(errors="replace").splitlines():
                        if line.startswith("CTOR_MS"):
                            ctor_ms = float(line.split()[1])
                        elif line.startswith("FIRST_PAINT"):
                            paint_ms = float(line.split()[1])
            if paint_ms is not None and opened:
                break
            if p.poll() is not None and paint_ms is not None:
                break
        geom = window_geom(p.pid)
        shot0 = shot_nonblank(geom, f"{name}-{i}-t0") if geom and opened else "NO_GEOM"
        time.sleep(0.5)
        shot5 = shot_nonblank(geom, f"{name}-{i}-t5") if geom and opened else "NO_GEOM"
        if p.poll() is None:
            p.send_signal(signal.SIGTERM)
            try: p.wait(timeout=10)
            except subprocess.TimeoutExpired: p.kill(); p.wait()
        s.close()
        rows.append((ctor_ms, paint_ms, open_ms, shot0, shot5, geom is not None))
        tag = "WARMUP" if i == 0 else f"iter{i}"
        print(f"  {name} {tag}: ctor={ctor_ms} paint={paint_ms} open={open_ms} shot0={shot0} shot500ms={shot5}")
    with open(f"{outdir}/{name}.csv", "w") as f:
        f.write("iter,ctor_ms,first_paint_ms,openwindow_ms,shot0_nonblank,shot500ms_nonblank,geom_ok\n")
        for i, r in enumerate(rows):
            f.write(f"{i},{r[0]},{r[1]},{r[2]},{r[3]},{r[4]},{r[5]}\n")
    keep = [r for r in rows[1:] if r[0] is not None and r[1] is not None and r[2] is not None]
    if keep:
        sc = stats([r[0] for r in keep]); sp = stats([r[1] for r in keep]); so = stats([r[2] for r in keep])
        s0 = [r[3] for r in keep if isinstance(r[3], float)]; s5 = [r[4] for r in keep if isinstance(r[4], float)]
        import statistics as st
        print(f"{name}: ctor_ms p50={sc['p50']:.0f} [{sc['min']:.0f}-{sc['max']:.0f}] | "
              f"first_paint_ms p50={sp['p50']:.0f} [{sp['min']:.0f}-{sp['max']:.0f}] | "
              f"openwindow_ms p50={so['p50']:.0f} [{so['min']:.0f}-{so['max']:.0f}] | "
              f"shots_ok={len(s0)}/{len(keep)} nonblank@open p50={st.median(s0) if s0 else float('nan'):.3f} "
              f"@+500ms p50={st.median(s5) if s5 else float('nan'):.3f}")
        summary[name] = (sc, sp, so)
    else:
        print(f"{name}: NO COMPLETE SAMPLES")
print("raw CSVs in", outdir)
EOF
echo "probe dir (delete after review): $TMPD"
