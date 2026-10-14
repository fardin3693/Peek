#!/usr/bin/env bash
# P-PERF1 baseline benchmark — measurement only, no app changes.
#
# What it measures (all under QT_QPA_PLATFORM=offscreen unless noted):
#   cli_version / cli_noargs : process launch -> exit wall time (ms) via
#       CLOCK_MONOTONIC around fork/exec, plus per-process peak RSS (KiB)
#       via os.wait4 ru_maxrss. Reliable: the process exits by itself.
#   preview_*_rss            : launch peek FILE offscreen, hold the live
#       process, sample /proc/<pid>/status VmHWM (peak) + VmRSS (steady),
#       then SIGTERM. Reliable for memory; wall-to-exit is harness hold
#       time, NOT preview latency (never present it as such).
#
# What it does NOT measure:
#   launch -> first VISIBLE preview (pixels on screen). Peek emits no
#   stdout/signal when the window maps, and offscreen maps nothing. That
#   cell is UNMEASURED; see docs/performance/baseline-*.md manual procedure.
#
# Cache conditions: warm-cache only. This script never drops page cache
# (needs root) and never claims to. The first iteration of each case is a
# warmup and is excluded from statistics.
#
# Requirements: bash, python3 (stdlib only), /proc, ffmpeg (fixture gen only).
# No new dependency is added to Peek; nothing here ships in the binary or
# runs at preview time. Not registered in CTest — run by hand.
#
# Usage: docs/performance/bench.sh [--peek PATH] [--outdir DIR] [--reps N] [--hold SECS]
set -u
PEEK=./build/debug/peek
OUTDIR=docs/performance/results
REPS=21
HOLD_SECS=3
while [ $# -gt 0 ]; do
    case "$1" in
        --peek) PEEK="$2"; shift 2 ;;
        --outdir) OUTDIR="$2"; shift 2 ;;
        --reps) REPS="$2"; shift 2 ;;
        --hold) HOLD_SECS="$2"; shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done
if [ ! -x "$PEEK" ]; then echo "peek not executable: $PEEK" >&2; exit 2; fi
mkdir -p "$OUTDIR"
FIXDIR="$OUTDIR/fixtures"
mkdir -p "$FIXDIR"

# --- fixtures (deterministic, generated) ---
python3 - "$FIXDIR" <<'EOF'
import sys
d = sys.argv[1]
open(f"{d}/sample.txt", "w").write(("The quick brown fox jumps over the lazy dog 0123456789\n" * 2000))
open(f"{d}/empty.txt", "w").write("")
open(f"{d}/fallback.bin", "wb").write(b"\x00\x01\x02\x03binary\xff\xfe" * 64)
# minimal one-page PDF (same construction as tests/test_preview.cpp)
stream = b"BT /F1 24 Tf 72 720 Td (Hello Peek) Tj ET"
objs = [b"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
        b"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n",
        b"3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] "
        b"/Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>\nendobj\n",
        b"4 0 obj\n<< /Length " + str(len(stream)).encode() + b" >>\nstream\n" + stream + b"\nendstream\nendobj\n",
        b"5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n"]
pdf = b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n"
offs = []
for o in objs:
    offs.append(len(pdf)); pdf += o
xref = len(pdf)
pdf += b"xref\n0 " + str(len(objs) + 1).encode() + b"\n0000000000 65535 f \n"
pdf += b"".join(("%10d 00000 n \n" % o).encode() for o in offs)
pdf += b"trailer\n<< /Size " + str(len(objs) + 1).encode() + b" /Root 1 0 R >>\nstartxref\n" + str(xref).encode() + b"\n%%EOF\n"
open(f"{d}/sample.pdf", "wb").write(pdf)
print("fixtures written to", d)
EOF
# image + audio + video via ffmpeg (fixture generation only, not a Peek dep)
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc2=size=1920x1080:rate=1:duration=1" -frames:v 1 "$FIXDIR/sample.png"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "sine=frequency=440:duration=1:sample_rate=44100" -c:a pcm_s16le "$FIXDIR/sample.wav"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc2=size=640x480:rate=10:duration=2" -f lavfi -i "sine=frequency=440:duration=2:sample_rate=44100" -shortest -c:v libx264 -pix_fmt yuv420p -c:a aac "$FIXDIR/sample.mp4"
ls -l "$FIXDIR"

export PEEK_REPS="$REPS" PEEK_HOLD="$HOLD_SECS" PEEK_BIN="$PEEK" PEEK_FIXDIR="$FIXDIR" PEEK_OUTDIR="$OUTDIR"
python3 - <<'EOF'
import os, subprocess, time, signal, statistics

peek = os.environ["PEEK_BIN"]
fix = os.environ["PEEK_FIXDIR"]
outdir = os.environ["PEEK_OUTDIR"]
reps = int(os.environ["PEEK_REPS"])
hold = float(os.environ["PEEK_HOLD"])
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QPA_PLATFORMTHEME="")

def stats(xs):
    xs = sorted(xs)
    n = len(xs)
    def pct(p):
        k = (n - 1) * p / 100.0
        f, c = int(k), min(int(k) + 1, n - 1)
        return xs[f] + (xs[c] - xs[f]) * (k - f)
    return {"n": n, "min": xs[0], "p50": statistics.median(xs),
            "p90": pct(90), "max": xs[-1]}

def cli_case(args, name):
    walls, rss = [], []
    with open(f"{outdir}/{name}.csv", "w") as f:
        f.write("iter,wall_ms,peak_rss_kb,rc\n")
        for i in range(reps + 1):  # iter 0 = warmup, excluded
            t0 = time.clock_gettime(time.CLOCK_MONOTONIC)
            p = subprocess.Popen([peek, *args], stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, env=env)
            _, status, ru = os.wait4(p.pid, 0)
            t1 = time.clock_gettime(time.CLOCK_MONOTONIC)
            rc = os.waitstatus_to_exitcode(status)
            p.stdout.read(); p.stderr.read()
            w, r = (t1 - t0) * 1000.0, float(ru.ru_maxrss)
            f.write(f"{i},{w:.3f},{r:.0f},{rc}\n")
            assert rc == 0, (name, i, rc)
            if i > 0:
                walls.append(w); rss.append(r)
    return walls, rss

def rss_case(path, name):
    hwm, steady = [], []
    with open(f"{outdir}/{name}.csv", "w") as f:
        f.write("iter,vmhwm_kb,vmrss_kb\n")
        for i in range(8):  # iter 0 = warmup, excluded (7 samples)
            p = subprocess.Popen([peek, "--", path], stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL, env=env)
            try:
                time.sleep(hold)
                if p.poll() is not None:
                    f.write(f"{i},EXITED_EARLY,rc={p.returncode}\n")
                    print(f"WARN {name} iter{i}: exited early rc={p.returncode}")
                    continue
                st = {}
                with open(f"/proc/{p.pid}/status") as sf:
                    for line in sf:
                        k, _, v = line.partition(":")
                        if k in ("VmHWM", "VmRSS"):
                            st[k] = float(v.strip().split()[0])
                f.write(f"{i},{st['VmHWM']:.0f},{st['VmRSS']:.0f}\n")
                if i > 0:
                    hwm.append(st["VmHWM"]); steady.append(st["VmRSS"])
            finally:
                if p.poll() is None:
                    p.send_signal(signal.SIGTERM)
                    try:
                        p.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        p.kill(); p.wait()
    return hwm, steady

print("=== CLI: launch -> exit (warm, offscreen) ===")
for args, name in [(["--version"], "cli_version"), ([], "cli_noargs")]:
    w, r = cli_case(args, name)
    sw, sr = stats(w), stats(r)
    print(f"{name}: wall_ms n={sw['n']} min={sw['min']:.1f} p50={sw['p50']:.1f} "
          f"p90={sw['p90']:.1f} max={sw['max']:.1f} | peakRSS_KiB p50={sr['p50']:.0f} range={sr['min']:.0f}-{sr['max']:.0f}")

print(f"=== preview RSS (warm, offscreen, hold {hold}s, SIGTERM) ===")
for fn, name in [("sample.txt", "preview_text_rss"), ("sample.png", "preview_image_rss"),
                 ("sample.pdf", "preview_pdf_rss"), ("sample.wav", "preview_audio_rss"),
                 ("sample.mp4", "preview_video_rss")]:
    h, s = rss_case(f"{fix}/{fn}", name)
    if h:
        sh, ss = stats(h), stats(s)
        print(f"{name}: VmHWM_KiB n={sh['n']} min={sh['min']:.0f} p50={sh['p50']:.0f} "
              f"max={sh['max']:.0f} | VmRSS_KiB p50={ss['p50']:.0f} range={ss['min']:.0f}-{ss['max']:.0f}")
    else:
        print(f"{name}: NO SAMPLES (process exited early?)")
print("raw CSVs in", outdir)
EOF
