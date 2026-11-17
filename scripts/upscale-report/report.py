"""upscale-report: compare the upscale_textures backends (TASK167).

Runs each backend's `upscale_textures <factor> <backend> force` in a copy of
the engine outside every git work tree, times it over several runs, and
writes one self-contained HTML report: the times per backend as a range, the
lines each run printed (a fallback to scale2x among them), and matched crops
of a fixed list of textures, the same region from every backend. The report
holds images made from the player's game data: it stays on this machine and
is never committed, uploaded or attached (docs/plans/high-resolution.md,
section 6). The command refuses an output inside a git work tree.

  report.py check-out DIR
  report.py run --out DIR --basedir DATA [--engine-dir DIR] [--factor 2|4]
                [--runs N] [--backends scale2x,metalfx,vt] [--renderer soft]
                [--names a,b,...] [--bound S]
"""

import argparse
import base64
import html
import os
import shutil
import statistics
import struct
import subprocess
import sys
import time

BACKENDS = ("scale2x", "metalfx", "vt")

# The fixed list: output names looked for first, in this order; a name a
# backend did not write is replaced by one of the evenly spaced names every
# backend wrote (so the list is the same for every backend of one run).
NAMES = (
    "textures/e1u1/floor1_3.png",
    "textures/e1u1/metal1_1.png",
    "textures/e1u1/wndow1_2.png",
    "textures/e1u2/basemap.png",
    "env/unit1_ft.png",
    "models/monsters/soldier/skin.png",
    "models/weapons/v_shotg/skin.png",
    "players/male/grunt.png",
)
COUNT = 8
CROP = 96          # the crop's side in output texels, shown at 2x zoom
MAX_EMBED = 4 << 20  # a PNG larger than this is not embedded


def die(msg, code=2):
    print("upscale-report: " + msg, file=sys.stderr)
    sys.exit(code)


def git_tree(path):
    """The work tree path is in (a .git entry in it or above), or None."""
    p = os.path.realpath(path)
    while True:
        if os.path.lexists(os.path.join(p, ".git")):
            return p
        up = os.path.dirname(p)
        if up == p:
            return None
        p = up


def check_out(out):
    if not out:
        die("--out is empty")
    if ".." in out.replace("\\", "/").split("/"):
        die("refusing an output path with '..': " + out)
    # the nearest directory that is there decides
    probe = os.path.abspath(out)
    while not os.path.isdir(probe):
        up = os.path.dirname(probe)
        if up == probe:
            break
        probe = up
    tree = git_tree(probe)
    if tree:
        die("refusing to write inside a git work tree (%s): %s" % (tree, out))
    if os.path.lexists(out) and (os.path.islink(out) or not os.path.isdir(out)):
        die("the output is there and is not a directory: " + out)
    return os.path.realpath(out) if os.path.exists(out) else os.path.abspath(out)


def png_size(data):
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", data[16:24])


def load():
    try:
        return os.getloadavg()[0]
    except OSError:
        return -1.0


def sysinfo():
    def run(cmd):
        try:
            return subprocess.run(cmd, capture_output=True, text=True,
                                  timeout=10).stdout.strip()
        except (OSError, subprocess.SubprocessError):
            return "?"
    return {
        "model": run(["sysctl", "-n", "hw.model"]),
        "chip": run(["sysctl", "-n", "machdep.cpu.brand_string"]),
        "os": run(["sw_vers", "-productVersion"]),
    }


def engine_run(eng, game, args, basedir, renderer, bound):
    """One engine process: its wall time and the lines it printed."""
    cmd = [os.path.join(eng, "quake2"), "-portable", "+set", "basedir", basedir,
           "+set", "game", game, "+set", "vid_renderer", renderer,
           "+set", "vid_fullscreen", "0", "+set", "s_initsound", "0"] + args + ["+quit"]
    t0 = time.monotonic()
    try:
        p = subprocess.run(cmd, cwd=eng, capture_output=True, text=True,
                           errors="replace", timeout=bound)
    except subprocess.TimeoutExpired:
        die("an engine run outlived its %d s bound: %s" % (bound, " ".join(args)), 1)
    wall = time.monotonic() - t0
    lines = [ln for ln in (p.stdout + p.stderr).splitlines()
             if ln.startswith("upscale_textures:") or "AddressSanitizer" in ln
             or "runtime error" in ln]
    return wall, p.returncode, lines


def pick_names(want, written):
    common = sorted(set.intersection(*written)) if written else []
    names = [n for n in want if n in common]
    rest = [n for n in common if n not in names]
    step = max(1, len(rest) // max(1, COUNT - len(names) + 1))
    for n in rest[step - 1::step]:
        if len(names) >= COUNT:
            break
        names.append(n)
    return names


def listing(root):
    out = set()
    for d, _dirs, files in os.walk(root):
        for f in files:
            if f.endswith(".png"):
                out.add(os.path.relpath(os.path.join(d, f), root))
    return out


def figure(path, factor):
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return "<td>not written</td>"
    size = png_size(data)
    if not size:
        return "<td>not a PNG</td>"
    if len(data) > MAX_EMBED:
        return "<td>%dx%d, %d bytes: too large to embed</td>" % (size + (len(data),))
    w, h = size
    uri = "data:image/png;base64," + base64.b64encode(data).decode("ascii")
    c = min(CROP, w, h)
    # the crop: the same region of every backend's image, at its centre
    x, y = (w - c) // 2, (h - c) // 2
    return ('<td><div class="crop" style="width:%dpx;height:%dpx;'
            'background-image:url(%s);background-size:%dpx %dpx;'
            'background-position:-%dpx -%dpx"></div>'
            '<div class="cap">%dx%d, crop %d,%d %dx%d at 2x, %d bytes</div></td>'
            % (2 * c, 2 * c, uri, 2 * w, 2 * h, 2 * x, 2 * y, w, h, x, y, c, c, len(data)))


def run(a):
    out = check_out(a.out)
    if a.factor not in (2, 4):
        die("--factor is 2 or 4")
    if a.runs < 1:
        die("--runs is at least 1")
    backends = [b for b in a.backends.split(",") if b]
    for b in backends:
        if b not in BACKENDS:
            die("no such backend: " + b)
    src = os.path.realpath(a.engine_dir)
    if not os.path.isfile(os.path.join(src, "quake2")):
        die("no quake2 in " + src)
    if not os.path.isdir(a.basedir):
        die("no game data directory: " + a.basedir)
    os.makedirs(out, exist_ok=True)
    eng = os.path.join(out, "engine")
    # a copy of the binaries: -portable writes beside it, outside the work tree
    os.makedirs(eng, exist_ok=True)
    for f in os.listdir(src):
        if f == "quake2" or f.endswith((".dylib", ".so")):
            shutil.copy2(os.path.join(src, f), os.path.join(eng, f))
    if git_tree(eng):
        die("the engine copy is inside a git work tree: " + eng)

    info = sysinfo()
    info["load_start"] = load()
    times = {"start-up": []}
    lines = {}
    rcs = {}
    for k in range(a.runs):
        wall, rc, _l = engine_run(eng, "up-start", [], a.basedir, a.renderer, a.bound)
        times["start-up"].append(wall)
        for b in backends:
            wall, rc, got = engine_run(eng, "up-" + b,
                                       ["+upscale_textures", str(a.factor), b, "force"],
                                       a.basedir, a.renderer, a.bound)
            times.setdefault(b, []).append(wall)
            rcs.setdefault(b, []).append(rc)
            if k == 0:
                lines[b] = got
            print("upscale-report: run %d, %s: %.3f s, exit %d" % (k + 1, b, wall, rc))
    info["load_end"] = load()

    written = [listing(os.path.join(eng, "up-" + b)) for b in backends]
    names = pick_names(a.names.split(",") if a.names else NAMES, written)

    def rng(v):
        return "%.3f to %.3f s (median %.3f)" % (min(v), max(v), statistics.median(v))

    start = statistics.median(times["start-up"])
    rows = []
    for b in ["start-up"] + backends:
        v = times[b]
        cmdv = [max(0.0, t - start) for t in v] if b != "start-up" else None
        rows.append("<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>" % (
            b, rng(v), rng(cmdv) if cmdv else "", " ".join("%.3f" % t for t in v),
            " ".join(str(r) for r in rcs.get(b, [])) or ""))
    said = "".join("<h3>%s</h3><pre>%s</pre>" % (b, html.escape("\n".join(lines[b]) or "(nothing)"))
                   for b in backends)
    figs = []
    for n in names:
        cells = "".join(figure(os.path.join(eng, "up-" + b, n), a.factor) for b in backends)
        figs.append("<tr><th>%s</th>%s</tr>" % (html.escape(n), cells))
    head = "".join("<th>%s</th>" % b for b in backends)
    doc = """<!doctype html><html><head><meta charset="utf-8">
<title>upscale_textures backends</title>
<style>
body{font:14px -apple-system,sans-serif;margin:16px;background:#fff;color:#111}
table{border-collapse:collapse}td,th{border:1px solid #ccc;padding:4px;vertical-align:top;text-align:left}
.crop{image-rendering:pixelated;background-repeat:no-repeat}.cap{font-size:11px;color:#555}
pre{background:#f4f4f4;padding:6px;white-space:pre-wrap}
</style></head><body>
<h1>upscale_textures backends, %dx</h1>
<p>Local only: made from the player's own data; never commit, upload or attach it.</p>
<p>%s, %s, macOS %s. Renderer %s. %d runs per backend, each with force, alternated.
Load average (1 min): %.2f at the start, %.2f at the end.</p>
<h2>Time per backend (process wall time; command = process less the median start-up)</h2>
<table><tr><th>backend</th><th>process</th><th>command</th><th>each run</th><th>exit</th></tr>%s</table>
<h2>What each backend's first run said</h2>%s
<h2>Matched crops</h2>
<table><tr><th>name</th>%s</tr>%s</table>
</body></html>
""" % (a.factor, html.escape(info["model"]), html.escape(info["chip"]), html.escape(info["os"]),
       html.escape(a.renderer), a.runs, info["load_start"], info["load_end"], "".join(rows),
       said, head, "".join(figs))
    path = os.path.join(out, "upscale-report-%dx.html" % a.factor)
    with open(path, "w") as f:
        f.write(doc)
    print("upscale-report: load %.2f at the start, %.2f at the end" % (info["load_start"], info["load_end"]))
    for b in ["start-up"] + backends:
        print("upscale-report: %s: %s" % (b, rng(times[b])))
    print("upscale-report: wrote %s (%d bytes, %d names)" % (path, len(doc), len(names)))
    return 0


def main(argv):
    if len(argv) >= 2 and argv[0] == "check-out":
        print(check_out(argv[1]))
        return 0
    if not argv or argv[0] != "run":
        print(__doc__)
        return 2
    p = argparse.ArgumentParser(prog="upscale-report run")
    p.add_argument("--out", required=True)
    p.add_argument("--basedir", required=True)
    p.add_argument("--engine-dir", default="release")
    p.add_argument("--factor", type=int, default=2)
    p.add_argument("--runs", type=int, default=3)
    p.add_argument("--backends", default=",".join(BACKENDS))
    p.add_argument("--renderer", default="soft")
    p.add_argument("--names", default="")
    p.add_argument("--bound", type=int, default=600)
    return run(p.parse_args(argv[1:]))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
