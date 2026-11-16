#!/usr/bin/env python3
"""scripts/metalfx-report/mfx.py -- the MetalFX comparison report (TASK114).

Run through scripts/metalfx-report.sh. Standard library only (python3 -I).

  mfx.py run     --out DIR --basedir DIR [--size WxH] [--runs N] [--gpu-runs N] ...
  mfx.py report  --dir RESULTS [--other RESULTS]... [--html FILE]
  mfx.py plan    --options FILE --cvarlist FILE --scenarios-dir DIR [--scenarios a,b]
  mfx.py check-out PATH

A results directory holds meta.tsv (key, value), results.tsv (one row per option and
scenario, columns RESULT_COLS), skipped.tsv (option, scenario, reason) and crops/ (PNG,
local only). The HTML is built from such directories; a second host's directory is
passed with --other and is compared by its numbers only (its crops are never read).
"""
import argparse
import base64
import fcntl
import html
import os
import pty
import re
import select
import shutil
import signal
import stat
import struct
import subprocess
import sys
import tempfile
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

RESULT_COLS = [
    "option", "scenario", "status", "cvars", "fps_runs", "load_runs", "gpu_frame_ms_runs",
    "scaler_ms_runs", "input_size", "output_size", "capture_size", "frames", "score_mean",
    "score_max", "bad_max", "ssim_mean", "ssim_min", "note",
]
SKIPPED_COLS = ["option", "scenario", "reason"]
STD_SCENARIOS = ["world", "models", "particles", "skywater"]
NAME_RE = re.compile(r"^[a-z0-9_][a-z0-9_-]*$")
CVAR_RE = re.compile(r"^[a-z0-9_]+$")
VALUE_RE = re.compile(r"^[A-Za-z0-9_.-]+$")
PATH_OK_RE = re.compile(r"^[A-Za-z0-9_./-]+$")
LOAD_QUIET = 8.0  # a 1-minute load above this at a run start marks its timings "measured under load"
GATING_PREFIXES = ("r_rt_", "r_metalfx")  # the cvars whose absence stops a scenario from running
MAX_ENGINE_ARGS = 127  # the engine refuses an argc over 128 (argproc.c MAX_NUM_ARGVS), its own name being the first
MAX_MODE = 30  # r_metalfx_modes is a bit set, one bit per mode


class Refuse(Exception):
    """A guard refused; the message says what it bounds."""


# ---------------------------------------------------------------- the output path

def git_env():
    """The environment for a git run: no GIT_* variable, no user or system config."""
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env["GIT_CONFIG_GLOBAL"] = os.devnull
    env["GIT_CONFIG_SYSTEM"] = os.devnull
    env["GIT_CONFIG_NOSYSTEM"] = "1"
    return env


def _git_says_inside(path):
    """True when git reports path inside a work tree. Run with no git environment
    and no user or system config, so the caller's GIT_DIR and the like cannot
    change the answer. A git that cannot run is an error, never 'outside'."""
    env = git_env()
    try:
        p = subprocess.run(["git", "-C", path, "rev-parse", "--is-inside-work-tree"], env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    except OSError as e:
        raise Refuse("git cannot run (%s): the output path cannot be checked" % e)
    out = p.stdout.strip()
    if p.returncode == 0:
        return out == "true"
    if "not a git repository" in p.stderr:
        return False
    raise Refuse("git failed (%s): the output path cannot be checked" % p.stderr.strip()[:200])


def check_out_path(path, file=False):
    """The physical output path, or Refuse. Refused: a relative path, a symbolic link at any
    component (so the path is already physical), a component that exists and is not a directory,
    a place inside a git work tree (a .git entry at it or any ancestor, or git saying so), and
    a directory that cannot be written."""
    if not path or "\0" in path or not os.path.isabs(path):
        raise Refuse("the output path must be absolute: %r" % path)
    parts = [p for p in path.split("/") if p not in ("", ".")]
    if ".." in parts:
        raise Refuse("the output path must not contain '..'")
    cur = "/"
    deepest = "/"  # the deepest component that exists
    for i, part in enumerate(parts):
        cur = os.path.join(cur, part)
        try:
            st = os.lstat(cur)
        except FileNotFoundError:
            break
        except OSError as e:
            raise Refuse("cannot inspect %s: %s" % (cur, e))
        if stat.S_ISLNK(st.st_mode):
            raise Refuse("%s is a symbolic link: the output path must be physical, with no link on the way" % cur)
        if file and i == len(parts) - 1 and stat.S_ISREG(st.st_mode):
            break  # an earlier report of the same name, to be replaced
        if not stat.S_ISDIR(st.st_mode):
            raise Refuse("%s exists and is not a directory" % cur)
        deepest = cur
    # a git work tree: a .git at any level from the deepest existing directory up
    d = deepest
    while True:
        if os.path.lexists(os.path.join(d, ".git")):
            raise Refuse("%s is inside a git work tree (it holds .git): the report holds frames of the owner's game data and stays out of every repository" % path)
        up = os.path.dirname(d)
        if up == d:
            break
        d = up
    if _git_says_inside(deepest):
        raise Refuse("%s is inside a git work tree: the report stays out of every repository" % path)
    if not os.access(deepest, os.W_OK | os.X_OK):
        raise Refuse("%s is not writable" % deepest)
    return "/" + "/".join(parts)


def make_dirs_physical(path):
    """mkdir -p for a path check_out_path accepted, one component at a time, each
    re-inspected without following links."""
    cur = "/"
    for part in [p for p in path.split("/") if p]:
        cur = os.path.join(cur, part)
        try:
            os.mkdir(cur)
        except FileExistsError:
            pass
        st = os.lstat(cur)
        if stat.S_ISLNK(st.st_mode) or not stat.S_ISDIR(st.st_mode):
            raise Refuse("%s is not a plain directory" % cur)


def write_file(path, data):
    """Write bytes to a new or existing regular file, never through a symbolic link."""
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW, 0o644)
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            raise Refuse("%s is not a regular file" % path)
        os.ftruncate(fd, 0)
        with os.fdopen(fd, "wb", closefd=False) as f:
            f.write(data)
    finally:
        os.close(fd)


def write_atomic(path, data):
    """Write bytes to a new file in path's directory and rename it over path, so a hard
    link at path is replaced and never written through. A symbolic link at path, or
    anything that is not a regular file, is refused."""
    try:
        st = os.lstat(path)
    except FileNotFoundError:
        st = None
    if st is not None and not stat.S_ISREG(st.st_mode):
        raise Refuse("%s exists and is not a regular file" % path)
    d = os.path.dirname(path)
    fd, tmpname = tempfile.mkstemp(prefix=".mfx-", suffix=".tmp", dir=d)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        os.chmod(tmpname, 0o644)
        os.rename(tmpname, path)
    except BaseException:
        try:
            os.unlink(tmpname)
        except OSError:
            pass
        raise


# ---------------------------------------------------------------- cvars and the plan

CVARLIST_RE = re.compile(r'^[ *][ U][ S][ L-] ([A-Za-z0-9_]+) "(.*)"$')


def parse_cvarlist(text):
    """name -> value from the engine's own cvarlist command."""
    out = {}
    for line in text.splitlines():
        m = CVARLIST_RE.match(line.rstrip("\r"))
        if m:
            out[m.group(1)] = m.group(2)
    return out


NOSET_RE = re.compile(r'^[ *][ U][ S]- ([A-Za-z0-9_]+) "')
RENDERER_REPORTED = ("r_metalfx_modes",)  # written by the renderer, not a setting


ARCHIVED_RE = re.compile(r'^\*[ U][ S][ L-] ([A-Za-z0-9_]+) "')


def parse_archived(text):
    """The names of the cvars the engine's cvarlist marks as archived: a '*' in the first column
    (Cvar_List_f, src/common/cvar.c, prints it for a cvar with CVAR_ARCHIVE)."""
    return set(m.group(1) for m in (ARCHIVED_RE.match(l.rstrip("\r")) for l in text.splitlines()) if m)


def default_cvars(base_cv, noset, options, scenarios, archived):
    """cvar -> its default, set first by every engine run: the cvars an option sets, those a scenario
    sets that gate a feature, and the gating cvars the probe's cvarlist marks as archived (the only
    ones config.cfg could carry from a run to the next). Every other cvar starts at its default in a
    new engine process, and each +set costs three of the engine's 128 arguments. Only a cvar the
    engine reports and lets be set."""
    names = set(c for c in base_cv if c.startswith(GATING_PREFIXES) and c in archived)
    for o in options:
        names.update(k for k, _ in o["set"])
    for s in scenarios.values():
        names.update(c for c in s if c.startswith(GATING_PREFIXES))
    return dict((c, base_cv[c]) for c in sorted(names) if c in base_cv and c not in noset and c not in RENDERER_REPORTED
                and CVAR_RE.match(c) and VALUE_RE.match(base_cv[c]))


def parse_noset(text):
    """The names of the cvars the engine's cvarlist marks as not settable."""
    return set(m.group(1) for m in (NOSET_RE.match(l.rstrip("\r")) for l in text.splitlines()) if m)


def parse_set(s):
    """'a=1 b=0.5' -> [(a, 1), (b, 0.5)] as strings, validated."""
    out = []
    for tok in s.split():
        if "=" not in tok:
            raise ValueError("not cvar=value: %r" % tok)
        k, v = tok.split("=", 1)
        if not CVAR_RE.match(k) or not VALUE_RE.match(v):
            raise ValueError("bad cvar or value: %r" % tok)
        out.append((k, v))
    return out


def load_options(path):
    opts = []
    with open(path) as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line.strip() or line.startswith("#"):
                continue
            cols = line.split("\t")
            if len(cols) != 4:
                raise ValueError("%s:%d: want 4 tab-separated columns, got %d" % (path, n, len(cols)))
            oid, label, group, sets = cols
            if not NAME_RE.match(oid) or oid == "native":
                raise ValueError("%s:%d: bad option id %r" % (path, n, oid))
            if group not in ("all", "rt", "std"):
                raise ValueError("%s:%d: scenarios must be all, std or rt" % (path, n))
            opts.append({"id": oid, "label": label, "group": group, "set": parse_set(sets)})
    ids = [o["id"] for o in opts]
    if len(ids) != len(set(ids)):
        raise ValueError("%s: duplicate option id" % path)
    return opts


def scenario_cvars(path):
    """The cvars a scenario file sets."""
    names = set()
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("set "):
                parts = line.split()
                if len(parts) == 3:
                    names.add(parts[1])
    return names


def plan(options, cvars, scenarios):
    """Which (option, scenario) rows run, and what is skipped and why. Pure.

    cvars is the engine's own report of the cvars it has (name -> value, from
    cvarlist, with no option set: setting an unknown cvar would create it);
    scenarios maps a scenario name to the cvars its file sets. A scenario runs
    when every cvar it sets exists. An option runs when every cvar it sets
    exists and, for r_metalfx, its mode is a bit of r_metalfx_modes. The
    value's read-back is checked afterwards by accepted()."""
    rows, skipped = [], []
    live = []
    for sc in sorted(scenarios):
        # only a cvar of a renderer feature gates a scenario; the others (the software renderer's, say)
        # are set for the renderers that have them and are harmless to the rest
        gone = sorted(c for c in scenarios[sc] if c not in cvars and c.startswith(GATING_PREFIXES))
        if gone:
            skipped.append(("*", sc, "absent cvar in the scenario: " + " ".join(gone)))
        else:
            live.append(sc)
    modes = None
    if "r_metalfx_modes" in cvars:
        try:
            modes = int(float(cvars["r_metalfx_modes"]))
        except ValueError:
            modes = None
    for o in options:
        gone = [k for k, _ in o["set"] if k not in cvars]
        if gone:
            skipped.append((o["id"], "*", "absent: the engine reports no cvar " + " ".join(gone)))
            continue
        bad = None
        for k, v in o["set"]:
            if k == "r_metalfx":
                try:
                    m = int(float(v))
                except ValueError:
                    m = -1
                if modes is None:
                    bad = "r_metalfx_modes is not reported"
                elif not (0 <= m <= MAX_MODE) or not ((modes >> m) & 1):
                    bad = "r_metalfx %s is not in r_metalfx_modes %d" % (v, modes)
        if bad:
            skipped.append((o["id"], "*", "unsupported: " + bad))
            continue
        for sc in live:
            if o["group"] == "rt" and not sc.startswith("rt-"):
                continue
            if o["group"] == "std" and sc not in STD_SCENARIOS:
                continue
            rows.append((o["id"], sc))
    return rows, skipped


def same_value(asked, got):
    try:
        return abs(float(asked) - float(got)) < 1e-6
    except ValueError:
        return asked == got


def accepted(sets, readback):
    """(True, '') when every cvar reads back as asked, else (False, why)."""
    for k, v in sets:
        if k not in readback:
            return False, "the engine does not report %s" % k
        if not same_value(v, readback[k]):
            return False, "%s %s reads %s" % (k, v, readback[k])
    return True, ""


# ---------------------------------------------------------------- images

def read_tga(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 18 or data[2] != 2:
        raise ValueError("%s: not an uncompressed truecolor TGA" % path)
    w, h = struct.unpack("<HH", data[12:16])
    bpp = data[16] // 8
    if w < 1 or h < 1 or bpp not in (3, 4):
        raise ValueError("%s: unsupported size or depth" % path)
    off = 18 + data[0]
    need = w * h * bpp
    if len(data) < off + need:
        raise ValueError("%s: short" % path)
    px = data[off:off + need]
    bottom_up = not (data[17] & 0x20)
    rows = [px[y * w * bpp:(y + 1) * w * bpp] for y in range(h)]
    if bottom_up:
        rows.reverse()
    return w, h, bpp, rows  # rows top to bottom, B G R (A)


def luma_row(row, bpp):
    return [(row[i] * 114 + row[i + 1] * 587 + row[i + 2] * 299) // 1000 for i in range(0, len(row), bpp)]


def choose_window(a, b, ww, wh, step=16):
    """The window of the most change between two frames (a, b from read_tga), as the
    mean absolute luma difference over a sparse grid; first window on a tie."""
    w, h, bpp, ra = a
    _, _, bpp2, rb = b
    la = {}
    best, bx, by = -1.0, 0, 0
    ys = list(range(0, h, 2))
    diffs = {}
    for y in ys:
        ya, yb = luma_row(ra[y], bpp), luma_row(rb[y], bpp2)
        diffs[y] = [abs(p - q) for p, q in zip(ya, yb)]
    for y0 in range(0, max(1, h - wh + 1), step):
        for x0 in range(0, max(1, w - ww + 1), step):
            tot, n = 0, 0
            for y in ys:
                if y0 <= y < y0 + wh:
                    seg = diffs[y][x0:x0 + ww:2]
                    tot += sum(seg)
                    n += len(seg)
            score = tot / n if n else 0.0
            if score > best:
                best, bx, by = score, x0, y0
    return bx, by


def png_bytes(w, h, rows_rgb):
    raw = b"".join(b"\0" + r for r in rows_rgb)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def crop_png(img, x0, y0, ww, wh, scale):
    w, h, bpp, rows = img
    out = []
    for y in range(y0, min(h, y0 + wh)):
        r = rows[y]
        line = bytearray()
        for x in range(x0, min(w, x0 + ww)):
            b, g, rr = r[x * bpp], r[x * bpp + 1], r[x * bpp + 2]
            line += bytes((rr, g, b)) * scale
        out.extend([bytes(line)] * scale)
    cw = min(w, x0 + ww) - x0
    return png_bytes(cw * scale, len(out), out)


# ---------------------------------------------------------------- engine runs

TIMEDEMO_RE = re.compile(r"(\d+) frames, ([0-9.]+) seconds: ([0-9.]+) fps")
GPU_FRAME_RE = re.compile(r"ref_metal: gpu_ms ([0-9.]+) frame")
STATS_RE = re.compile(r"metal_stats: metalfx [a-z ]+: input (\d+)x(\d+), output (\d+)x(\d+) at [0-9,]+, reset [a-z ]+, scaler (?:([0-9.]+) ms|not timed)")
LOAD_RE = re.compile(r"load averages?: *([0-9.]+)")
VALIDATION_ON = "Metal API Validation Enabled"


def median(v):
    s = sorted(v)
    n = len(s)
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2.0


class Ctx:
    cfg = None
    defaults = {}


class CfgGuard:
    """The engine's portable config.cfg (<binary dir>/baseq2/config.cfg). The engine reads it at
    start and rewrites it at exit, so a cvar one run set would reach the next run, and the owner's
    file would end holding the last run's settings. hold() moves the owner's file aside (renamed to
    config.cfg.mfx-saved beside it), clear() removes what a run left before the next run starts,
    restore() puts the owner's file back, and the generator calls it in a finally and from a
    SIGTERM/SIGHUP handler (SIGINT unwinds as KeyboardInterrupt). A SIGKILL or a power cut cannot be
    caught: it leaves the owner's file at config.cfg.mfx-saved, and the next run puts it back before
    it does anything else (or rename it by hand). That reading is only right under the run locks
    (cmd_run): with one run per binary, a .mfx-saved is always a killed run's. When a config.cfg
    exists beside it, that newer file is kept as config.cfg.mfx-newer, never deleted."""

    def __init__(self, binpath):
        self.path = os.path.join(os.path.dirname(os.path.abspath(binpath)), "baseq2", "config.cfg")
        self.saved = self.path + ".mfx-saved"
        self.held = False
        self.recovered = False
        self.kept = None

    def _remove(self, p):
        try:
            st = os.lstat(p)
        except FileNotFoundError:
            return
        if stat.S_ISDIR(st.st_mode):
            raise Refuse("%s is a directory" % p)
        os.unlink(p)

    def hold(self):
        # cmd_run holds the run locks, so a .mfx-saved here belongs to a run that was killed
        if os.path.lexists(self.saved):
            if os.path.lexists(self.path):
                # the owner played after the kill: that newer file is kept under a new name, never deleted
                if os.path.isdir(self.path) and not os.path.islink(self.path):
                    raise Refuse("%s is a directory" % self.path)
                n, newer = 0, self.path + ".mfx-newer"
                while os.path.lexists(newer):
                    n += 1
                    newer = "%s.mfx-newer.%d" % (self.path, n)
                os.rename(self.path, newer)
                self.kept = newer
            os.rename(self.saved, self.path)
            self.recovered = True
        if os.path.lexists(self.path):
            if os.path.isdir(self.path) and not os.path.islink(self.path):
                raise Refuse("%s is a directory" % self.path)
            os.rename(self.path, self.saved)
        self.held = True

    def clear(self):
        self._remove(self.path)

    def restore(self):
        if not self.held:
            return
        self._remove(self.path)
        if os.path.lexists(self.saved):
            os.rename(self.saved, self.path)
        self.held = False


def sh(cmd, cwd=None, env=None, timeout=None):
    p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       universal_newlines=True, timeout=timeout)
    return p.returncode, p.stdout


LOCK_LABEL = "metalfx-report"
LOCK_HELD = False  # set by lock_self when this process runs under its own hold of the host lock


def lock_self(argv):
    """The host lock (docs/plans/bench-harness.md, "The host lock"). Unless it is off, runs this script
    again with argv as one hold of "scripts/bench/bench.sh lock run metalfx-report", so that every
    scripts/pins.sh record and every timed engine run of the report is under the one hold: a record
    passes straight through (BENCH_LOCK_TOKEN), its time limit counts the record alone and not a wait
    for the lock, and no other heavy job on this host shares the machine with the timed runs. Off, with
    one line on stderr that names why, with BENCH_LOCK=0, on GitHub Actions (CI=true and
    GITHUB_ACTIONS=true), with no scripts/bench/bench.sh in this tree, and with no directory for its
    state (BENCH_STATE, XDG_CACHE_HOME and HOME all unset); pins.sh, which inherits the environment and
    sits in the same tree, is then off as well. The run under the lock comes back here with
    BENCH_LOCK_ENTRY=metalfx-report (cleared at once, so that nothing below inherits it) and goes on."""
    global LOCK_HELD
    e = os.environ
    entry = e.pop("BENCH_LOCK_ENTRY", "")
    if entry == LOCK_LABEL and e.get("BENCH_LOCK_TOKEN"):
        LOCK_HELD = True
        return
    bench = os.path.join(ROOT, "scripts", "bench", "bench.sh")
    if e.get("BENCH_LOCK") == "0":
        off = "BENCH_LOCK=0"
    elif e.get("CI") == "true" and e.get("GITHUB_ACTIONS") == "true":
        off = "CI (GITHUB_ACTIONS=true)"
    elif not os.path.isfile(bench):
        off = "no bench core (scripts/bench/bench.sh)"
    elif not (e.get("BENCH_STATE") or e.get("XDG_CACHE_HOME") or e.get("HOME")):
        off = "no state directory (BENCH_STATE, XDG_CACHE_HOME and HOME unset)"
    else:
        cmd = ["sh", bench, "lock", "run", LOCK_LABEL, "--", "env", "BENCH_LOCK_ENTRY=" + LOCK_LABEL,
               sys.executable, "-I", os.path.abspath(__file__)] + list(argv)
        sys.stdout.flush()
        sys.stderr.flush()
        os.execv("/bin/sh", cmd)
    print("metalfx-report: the host lock is off: " + off, file=sys.stderr)


def lock_stop_wait(env=None):
    """The seconds sh_group gives a stopped command between TERM and KILL, from BENCH_STOP_GRACE as the
    host lock reads it (whole seconds, default 10; one that is not counts as the default: bench.sh
    refuses it and runs nothing). Under this run's own hold (LOCK_HELD) pins.sh passes straight through
    and is the command itself, and this run is the command of the lock shell, which stops it with TERM
    and KILLs it after the lock's grace (scripts/bench/core/lock.sh, bench_lock_grace: twice
    BENCH_STOP_GRACE, and at least BENCH_STOP_GRACE + 5): the wait is BENCH_STOP_GRACE, the grace of a
    stopper nested in the lock, so that this run's KILL of pins.sh lands first and nothing outlives the
    lock. Otherwise pins.sh may be the lock shell of a hold of its own: the wait is that lock's grace,
    plus 5 s for the lock shell to reap its command and free the lock."""
    g = (os.environ if env is None else env).get("BENCH_STOP_GRACE", "")
    g = int(g) if re.match(r"[0-9]+\Z", g) else 10
    if LOCK_HELD:
        return g
    return max(2 * g, g + 5) + 5


STOP_SIGNALS = (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)


def stop_group(p, wait):
    """Stops the process group p leads: TERM to the group, up to <wait> seconds for p to end (its
    output read meanwhile), then KILL to the group. A second SIGINT, SIGTERM or SIGHUP meanwhile is held
    until the stop is over, so it cannot cut the wait short."""
    held = signal.pthread_sigmask(signal.SIG_BLOCK, STOP_SIGNALS)
    try:
        try:
            os.killpg(p.pid, signal.SIGTERM)
        except OSError:
            pass
        ended = False
        try:
            p.communicate(timeout=wait)
            ended = True
        except Exception:  # the wait ran out (or the pipe failed): KILL
            pass
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except OSError:
            pass
        if not ended:
            try:
                p.communicate(timeout=5)  # bounded: a process that left the group may hold the pipe
            except Exception:
                pass
    finally:
        signal.pthread_sigmask(signal.SIG_SETMASK, held)


def sh_group(cmd, cwd=None, env=None, timeout=None):
    """Like sh(), with the command in a process group of its own, which is stopped (stop_group) on
    any way out: a SIGTERM/SIGHUP (SystemExit from the handler), Ctrl-C or the timeout. TERM first,
    KILL only after lock_stop_wait. Under this run's own hold of the host lock, scripts/pins.sh passes
    straight through and is the command itself. Without it, pins.sh runs itself under the lock
    (scripts/bench/bench.sh lock run), so the command's pid is the lock shell, and the run under it,
    the engine included, is in a process group of the lock's own, which only the lock shell's TERM trap
    stops before it frees the lock: a KILL first would end the lock shell alone and leave that run
    going, the lock held. So a command that starts the engine (scripts/pins.sh) leaves nothing
    running."""
    p = subprocess.Popen(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         universal_newlines=True, start_new_session=True)
    try:
        out, _ = p.communicate(timeout=timeout)
    except BaseException:
        stop_group(p, lock_stop_wait(env))
        raise
    return p.returncode, out


def load1():
    rc, out = sh(["uptime"])
    m = LOAD_RE.search(out)
    if rc != 0 or not m:
        raise RuntimeError("uptime gave no load average: %r" % out)
    return m.group(1), out.strip()


def engine_args(ctx, scenario_sets, extra_sets, tail):
    a = ["-datadir", ctx.basedir, "-portable"]
    for k, v in sorted(ctx.defaults.items()):  # first: every cvar a row sets starts at its default
        a += ["+set", k, v]
    for k, v in scenario_sets + extra_sets:
        a += ["+set", k, v]
    a += ["+set", "vid_renderer", "metal", "+set", "vid_fullscreen", "0", "+set", "r_mode", "-1",
          "+set", "r_customwidth", str(ctx.width), "+set", "r_customheight", str(ctx.height),
          "+set", "s_initsound", "0", "+set", "vid_highdpiaware", "0", "+set", "r_vsync", "0"]
    a += tail
    if len(a) > MAX_ENGINE_ARGS:
        raise RuntimeError("the engine command line would have %d arguments (the limit is %d)" % (len(a), MAX_ENGINE_ARGS))
    return a


def run_engine(ctx, args, log, until=None, timeout=120, extra_env=None, debug=True):
    """Run the engine in the foreground, its console in log. With until (a regex),
    the engine, which does not quit after a demo, is ended once the log matches it
    (only the process started here is signalled). Returns the exit status, or None
    when ended by this function."""
    env = dict(os.environ)
    env.pop("MTL_DEBUG_LAYER", None)
    if debug:
        env["MTL_DEBUG_LAYER"] = "1"
    env.update(extra_env or {})
    # The engine's console goes through a pseudo-terminal: written to a file its stdio is fully
    # buffered and the timedemo line would appear only at exit.
    master, slave = pty.openpty()
    buf = bytearray()
    status, why = None, None
    p = None
    try:
        if ctx.cfg is not None:
            ctx.cfg.clear()  # the run starts from the engine's defaults, not from what the last run saved
        p = subprocess.Popen([ctx.bin] + args, cwd=ROOT, env=env, stdin=subprocess.DEVNULL, stdout=slave,
                             stderr=slave, close_fds=True)
        os.close(slave)
        slave = -1
        t0 = time.time()
        matched_at = None
        while True:
            r, _, _ = select.select([master], [], [], 0.2)
            if r:
                try:
                    data = os.read(master, 65536)
                except OSError:
                    data = b""  # the engine closed its side
                if data:
                    buf += data
            rc = p.poll()
            if rc is not None:
                status = rc
                try:  # what is left in the pipe
                    while select.select([master], [], [], 0.2)[0]:
                        data = os.read(master, 65536)
                        if not data:
                            break
                        buf += data
                except OSError:
                    pass
                break
            if until and matched_at is None and re.search(until, buf.decode("utf-8", "replace")):
                matched_at = time.time()
            if matched_at is not None and time.time() - matched_at > 0.8:
                why = "ended"
                break
            if time.time() - t0 > timeout:
                why = "timeout"
                break
        if why:
            p.terminate()
            try:
                p.wait(5)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
    finally:
        if p is not None and p.poll() is None:  # an interrupt must not leave the engine running
            p.kill()
            p.wait()
        if slave >= 0:
            os.close(slave)
        os.close(master)
        with open(log, "wb") as f:
            f.write(bytes(buf).replace(b"\r\n", b"\n"))
    if why == "timeout":
        raise RuntimeError("engine run timed out after %ds (%s)" % (timeout, log))
    return status


def read_log(log):
    with open(log, "rb") as f:
        return f.read().decode("utf-8", "replace")


def check_validation(text):
    """Metal API Validation must be on, and say nothing else about validation."""
    if VALIDATION_ON not in text:
        raise RuntimeError("the Metal validation layer was not enabled (no %r in the engine's output)" % VALIDATION_ON)
    for line in text.splitlines():
        low = line.lower()
        if CVARLIST_RE.match(line):
            continue  # a cvar named for validation (r_validation) is a cvar, not a message
        if "validation" in low and VALIDATION_ON.lower() not in low:
            raise RuntimeError("a validation message: " + line.strip()[:200])


def fit_options(ctx, sc_sets, opts):
    """(kept, dropped): the (id, sets) of opts whose widest engine run (the GPU one, which adds
    metal_stats) fits the engine's argument limit, and (id, reason) for those that do not. One
    option over the limit costs only its own row."""
    kept, dropped = [], []
    for oid, sets in opts:
        try:
            engine_args(ctx, sc_sets, sets + [("metal_stats", "1")], ["+demomap", ctx.demo])
            kept.append((oid, sets))
        except RuntimeError as e:
            dropped.append((oid, "arguments: %s" % e))
    return kept, dropped


def probe(ctx, sets, log):
    """One engine run that loads a map, waits a few frames and asks the engine for
    its own cvar list. Returns the parsed list and the engine's text."""
    args = engine_args(ctx, [], sets, ["+demomap", ctx.demo] + ["+wait"] * 8 + ["+cvarlist", "+quit"])
    st = run_engine(ctx, args, log, timeout=120)
    text = read_log(log)
    if st != 0:
        raise RuntimeError("the probe run exited with status %s (%s)" % (st, log))
    cv = parse_cvarlist(text)
    if not cv:
        raise RuntimeError("the probe run printed no cvar list (%s)" % log)
    check_validation(text)
    return cv, text


def timed_run(ctx, sc_sets, sets, log, gpu):
    args = engine_args(ctx, sc_sets, sets + ([("metal_stats", "1")] if gpu else []), ["+demomap", ctx.demo])
    # timing runs go without the validation layer, which slows the GPU work they measure
    run_engine(ctx, args, log, until=r"\d+ frames, [0-9.]+ seconds: [0-9.]+ fps", timeout=ctx.timeout, debug=False,
               extra_env={"MTL_GPU_TIME": "1"} if gpu else None)
    text = read_log(log)
    m = TIMEDEMO_RE.search(text)
    if not m:
        raise RuntimeError("no timedemo result (%s)" % log)
    return float(m.group(3)), text


def parse_scenario(path):
    out = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("set "):
                p = line.split()
                if len(p) == 3:
                    out.append((p[1], p[2]))
    return out


# ---------------------------------------------------------------- the run

def load_note(loads):
    """The note of a row whose timing runs started under a busy machine."""
    if loads and max(loads) > LOAD_QUIET:
        return "measured under load (1-minute load up to %.1f at a run start)" % max(loads)
    return ""


def tsv_write(path, header, rows):
    lines = ["\t".join(header)]
    for r in rows:
        cells = [str(c) for c in r]
        for c in cells:
            if "\t" in c or "\n" in c:
                raise ValueError("a tab or newline in a result cell: %r" % c)
        lines.append("\t".join(cells))
    write_file(path, ("\n".join(lines) + "\n").encode())


def fnum(v, nd=4):
    return ("%." + str(nd) + "f") % v


def csv_floats(v, nd=3):
    return ",".join(fnum(x, nd) for x in v)


def diff_frames(ctx, a, b):
    rc, out = sh([ctx.framediff, a, b])
    if rc not in (0, 2):
        raise RuntimeError("framediff failed (exit %d): %s" % (rc, out.strip()[:200]))
    m = re.match(r"score=([0-9.]+) max=\d+ bad=([0-9.]+)", out)
    if not m:
        raise RuntimeError("framediff output not understood: " + out[:200])
    rc, out = sh([ctx.ssim, a, b])
    if rc != 0:
        raise RuntimeError("ssim failed (exit %d): %s" % (rc, out.strip()[:200]))
    s = re.match(r"ssim=([0-9.-]+) min=([0-9.-]+)", out)
    if not s:
        raise RuntimeError("ssim output not understood: " + out[:200])
    return float(m.group(1)), float(m.group(2)), float(s.group(1)), float(s.group(2))


def record(ctx, scen_name, base_sets, sets, tag):
    """Record a pin of the scenario with the option's cvars; returns the pin directory."""
    name = "mfxr-%s-%s" % (scen_name, tag)
    if not NAME_RE.match(name):
        raise RuntimeError("bad scenario name " + name)
    cfg = os.path.join(ctx.pins_scen_dir, name + ".cfg")
    body = "// scratch scenario of scripts/metalfx-report.sh, deleted when it ends\n"
    with open(os.path.join(ctx.scen_dir, scen_name + ".cfg")) as f:
        body += f.read()
    if not body.endswith("\n"):
        body += "\n"
    for k, v in sets:
        body += "set %s %s\n" % (k, v)
    with open(cfg, "w") as f:
        f.write(body)
    ctx.scratch_cfgs.append(cfg)
    env = dict(os.environ)
    env.update({"PINS_STORE": ctx.store, "PINS_BASEDIR": ctx.basedir, "PINS_WIDTH": str(ctx.width),
                "PINS_HEIGHT": str(ctx.height), "PINS_EVERY": str(ctx.every), "PINS_SEED": str(ctx.seed),
                "PINS_TIMEOUT": "600", "MTL_DEBUG_LAYER": "1", "PINS_BIN": ctx.bin})
    try:
        rc, out = sh_group([os.path.join(ROOT, "scripts", "pins.sh"), "record", "metal", name], cwd=ROOT, env=env, timeout=900)
    finally:
        try:
            os.remove(cfg)
        except OSError:
            pass
        if cfg in ctx.scratch_cfgs:
            ctx.scratch_cfgs.remove(cfg)
    if rc != 0:
        raise RuntimeError("pins.sh record %s failed: %s" % (name, out.strip()[-300:]))
    dirs = [d for d in sorted(os.listdir(os.path.join(ctx.store, name, "metal")))
            if os.path.isfile(os.path.join(ctx.store, name, "metal", d, "complete"))]
    if len(dirs) != 1:
        raise RuntimeError("expected one complete pin of %s, found %d" % (name, len(dirs)))
    d = os.path.join(ctx.store, name, "metal", dirs[0])
    check_validation(read_log(os.path.join(d, "engine.log")))
    return d


def frames_by_index(d):
    """The captured frames of a pin, by their frame number (the file name is
    <scenario>-<number>.tga, and the scenario differs between records)."""
    out = {}
    for f in sorted(os.listdir(d)):
        if f.endswith(".tga"):
            k = f[:-4].rsplit("-", 1)[-1]
            if not k.isdigit() or k in out:
                raise RuntimeError("%s: cannot tell the frame number of %s" % (d, f))
            out[k] = os.path.join(d, f)
    return out


def frame_keys(frames):
    """The frame numbers of frames_by_index, in numeric order."""
    return sorted(frames, key=int)


def engine_tree(binpath):
    """(tree, why): the source tree a binary was built in, taken from its place
    <tree>/release/quake2 under a directory with a Makefile and src/; else (None, why)."""
    real = os.path.realpath(binpath)
    rel = os.path.dirname(real)
    if os.path.basename(real) != "quake2" or os.path.basename(rel) != "release":
        return None, "the binary is not <tree>/release/quake2"
    tree = os.path.dirname(rel)
    if not (os.path.isfile(os.path.join(tree, "Makefile")) and os.path.isdir(os.path.join(tree, "src"))):
        return None, "no Makefile and src/ above the binary"
    return tree, ""


def check_build(tree):
    """Refuse when make says the engine or the renderer of this tree is out of date."""
    rc, _ = sh(["make", "-C", tree, "-q", "release/quake2", "release/ref_metal.dylib"])
    if rc != 0:
        raise Refuse("the build in %s is out of date (or make cannot say): run make client game ref_metal there first" % tree)


def engine_commit(tree):
    """(commit, 'clean'|'dirty') of the tree the engine was built from, or ('unknown', 'unknown')."""
    if tree is None:
        return "unknown", "unknown"
    env = git_env()
    p = subprocess.run(["git", "-C", tree, "rev-parse", "--short=8", "HEAD"], env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, universal_newlines=True)
    commit = p.stdout.strip()
    if p.returncode != 0 or not re.match(r"^[0-9a-f]{8}$", commit):
        return "unknown", "unknown"
    p = subprocess.run(["git", "-C", tree, "status", "--porcelain", "--", "src", "Makefile"], env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    if p.returncode != 0:
        raise RuntimeError("git status failed: " + p.stderr[:200])
    return commit, ("dirty" if p.stdout.strip() else "clean")


def prepare_rdir(out, host, date, replace):
    """The results directory of this run, made. An existing one is refused without replace;
    with it the earlier run's crops and result files are removed first, so nothing of it is
    left to be reported as this run's."""
    rdir = os.path.join(out, "metalfx-%s-%s" % (host, date))
    if os.path.lexists(rdir) and not replace:
        raise Refuse("%s exists; pass --replace" % rdir)
    make_dirs_physical(out)
    make_dirs_physical(rdir)
    crops = os.path.join(rdir, "crops")
    make_dirs_physical(crops)
    for d, names in ((crops, os.listdir(crops)), (rdir, ["results.tsv", "skipped.tsv", "meta.tsv"])):
        for n in names:
            f = os.path.join(d, n)
            try:
                st = os.lstat(f)
            except FileNotFoundError:
                continue
            if not stat.S_ISREG(st.st_mode):
                raise Refuse("%s is not a regular file: it is not removed" % f)
            os.unlink(f)
    return rdir


def clear_scratch_cfgs(scen_dir):
    """Remove the scratch scenarios of a run that was killed before it cleaned up."""
    for f in os.listdir(scen_dir):
        if f.startswith("mfxr-"):
            p = os.path.join(scen_dir, f)
            if not stat.S_ISREG(os.lstat(p).st_mode):
                raise Refuse("%s is in the way and is not a regular file" % p)
            os.unlink(p)


def probe_options(ctx, options, rows, work):
    """(ok_opts, skipped): the options of rows whose values the engine reads back as asked. An
    option whose probe run fails is skipped with the reason, and the others go on."""
    ok_opts, skipped, byid = {}, [], {o["id"]: o for o in options}
    for oid in sorted({r[0] for r in rows}, key=[o["id"] for o in options].index):
        o = byid[oid]
        try:
            cv, _ = probe(ctx, o["set"], os.path.join(work, "probe-%s.log" % oid))
        except RuntimeError as e:
            skipped.append((oid, "*", ("probe failed: %s" % e).replace("\t", " ").replace("\n", " ")[:300]))
            continue
        good, why = accepted(o["set"], cv)
        if good:
            ok_opts[oid] = o
        else:
            skipped.append((oid, "*", "unsupported: " + why))
    return ok_opts, skipped


def take_lock(path, what):
    """A non-blocking exclusive flock on the directory at path; returns its fd. The lock is on a
    directory, so no lock file exists to be left in a checkout. A lock that is held, or cannot be
    taken, is a refusal. The fd is not inherited by the engine or pins.sh."""
    try:
        fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    except OSError as e:
        raise Refuse("cannot open %s to lock %s: %s" % (path, what, e))
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError as e:
        os.close(fd)
        raise Refuse("another run holds %s (%s), or it cannot be locked (%s): one run per %s at a time" % (what, path, e, what))
    return fd


def cmd_run(a, argv=None):
    ctx = Ctx()
    out = check_out_path(a.out)
    if not PATH_OK_RE.match(a.basedir) or not os.path.isdir(os.path.join(a.basedir, "baseq2")):
        raise Refuse("--basedir must be a plain path holding baseq2/")
    ctx.basedir = a.basedir
    m = re.match(r"^(\d+)x(\d+)$", a.size)
    if not m:
        raise Refuse("--size must be WxH")
    ctx.width, ctx.height = int(m.group(1)), int(m.group(2))
    ctx.bin = a.bin or os.environ.get("PINS_BIN") or os.path.join(ROOT, "release", "quake2")
    ctx.demo = "demo1.dm2"
    ctx.timeout = a.timeout
    ctx.every = a.every
    ctx.seed = a.seed
    ctx.scen_dir = os.path.join(ROOT, "scripts", "pins", "scenarios")
    ctx.pins_scen_dir = ctx.scen_dir  # pins.sh reads its scenarios from here (scripts/pins.sh scen_dir)
    ctx.scratch_cfgs = []
    ctx.framediff, ctx.ssim = a.framediff, a.ssim
    for t in (ctx.bin, ctx.framediff, ctx.ssim):
        if not os.access(t, os.X_OK):
            raise Refuse("%s is not an executable" % t)
    # what follows (the freshness check's make, the engine runs, the records) runs under one hold of the
    # host lock; the checks above are cheap, and a refusal there does not wait for the lock
    if argv is not None:
        lock_self(argv)
    # the freshness check and the commit are those of the tree the binary was built in; a binary
    # that does not sit in such a tree is not checked, and says so in the report's meta
    ctx.tree, why = engine_tree(ctx.bin)
    if ctx.tree is not None:
        check_build(ctx.tree)
        ctx.build_note = "checked"
    else:
        ctx.build_note = "not checked: " + why
        print("metalfx-report: the build of %s is not checked for freshness (%s); its commit is recorded as unknown" % (ctx.bin, why),
              file=sys.stderr)
    # one run per engine binary directory (its config.cfg is held aside) and one per checkout's scenarios
    # folder (the scratch scenarios); held until the finally has put the config back
    bindir = os.path.join(os.path.dirname(os.path.abspath(ctx.bin)), "baseq2")
    try:
        os.makedirs(bindir, exist_ok=True)
    except OSError as e:
        raise Refuse("cannot create %s: %s" % (bindir, e))
    fds = []
    try:
        fds.append(take_lock(bindir, "the engine binary directory"))
        fds.append(take_lock(ctx.pins_scen_dir, "the scenarios folder of this checkout"))
        return _cmd_run_locked(a, ctx, out)
    finally:
        for fd in fds:
            os.close(fd)


def _cmd_run_locked(a, ctx, out):
    clear_scratch_cfgs(ctx.pins_scen_dir)
    host = a.host
    if not re.match(r"^[a-z0-9][a-z0-9-]*$", host):
        raise Refuse("--host-name must match [a-z0-9][a-z0-9-]*")
    date = time.strftime("%Y%m%d")
    rdir = prepare_rdir(out, host, date, a.replace)
    work = tempfile.mkdtemp(prefix=".work-", dir=out)
    ctx.store = os.path.join(work, "store")
    os.mkdir(ctx.store)
    errors = []
    ctx.cfg = CfgGuard(ctx.bin)
    old = {}

    def on_signal(signum, frame):
        raise SystemExit(128 + signum)  # unwinds through the finally below
    for s in (signal.SIGTERM, signal.SIGHUP):
        old[s] = signal.signal(s, on_signal)
    try:
        ctx.cfg.hold()
        if ctx.cfg.recovered:
            print("metalfx-report: the owner's config.cfg of an earlier killed run was put back from " + ctx.cfg.saved, file=sys.stderr)
        if ctx.cfg.kept:
            print("metalfx-report: a newer config.cfg was there as well; it is kept as %s, and the saved one is now %s" % (ctx.cfg.kept, ctx.cfg.path), file=sys.stderr)
        return _run(a, ctx, rdir, work, host, date, errors)
    finally:
        try:
            ctx.cfg.restore()
        finally:
            for s, h in old.items():
                signal.signal(s, h)
            for c in ctx.scratch_cfgs:
                try:
                    os.remove(c)
                except OSError:
                    pass
            shutil.rmtree(work, ignore_errors=True)


def _run(a, ctx, rdir, work, host, date, errors):
    commit, dirty = engine_commit(ctx.tree)

    # what the engine reports: its cvars (nothing set, so none is created by asking)
    base_cv, text = probe(ctx, [], os.path.join(work, "probe-base.log"))
    dm = re.search(r"Metal device: (.+)", text)
    fm = re.search(r"Metal supportsFamily \([^)]*\): (\S+)", text)
    device = dm.group(1).strip() if dm else "unknown"
    family = fm.group(1).strip() if fm else "unknown"
    load_start, uptime_start = load1()

    options = load_options(os.path.join(HERE, "options.tsv"))
    names = [s for s in a.scenarios.split(",") if s] if a.scenarios else \
        STD_SCENARIOS + sorted(f[:-4] for f in os.listdir(ctx.scen_dir) if f.startswith("rt-") and f.endswith(".cfg"))
    scen = {}
    for s in names:
        if not NAME_RE.match(s) or not os.path.isfile(os.path.join(ctx.scen_dir, s + ".cfg")):
            raise Refuse("no scenario " + s)
        scen[s] = scenario_cvars(os.path.join(ctx.scen_dir, s + ".cfg"))
    if a.only:
        keep = set(a.only.split(","))
        options = [o for o in options if o["id"] in keep]
    ctx.defaults = default_cvars(base_cv, parse_noset(text), load_options(os.path.join(HERE, "options.tsv")), scen, parse_archived(text))
    rows, skipped = plan(options, base_cv, scen)

    # the read-back: an option runs only when the engine reports each value as asked
    ok_opts, more = probe_options(ctx, options, rows, work)
    skipped += more
    rows = [r for r in rows if r[0] in ok_opts]
    live_scen = [s for s in scen if not any(x[0] == "*" and x[1] == s for x in skipped)]
    results = {}  # (option, scenario) -> dict

    for sc in live_scen:
        sc_sets = parse_scenario(os.path.join(ctx.scen_dir, sc + ".cfg"))
        opts = [("native", [("r_metalfx", "0")])] + [(o, ok_opts[o]["set"]) for o in
                [r[0] for r in rows if r[1] == sc]]
        opts, over = fit_options(ctx, sc_sets, opts)
        for oid, why in over:
            skipped.append((oid, sc, why.replace("\t", " ").replace("\n", " ")[:300]))
        if not opts or opts[0][0] != "native":
            errors.append("%s: the native run does not fit the engine's argument limit" % sc)
            results[("native", sc)] = {"error": "the native run does not fit the engine's argument limit"}
            continue
        row = {oid: {"fps": [], "load": [], "gpu": [], "scaler": [], "in": "", "out": ""} for oid, _ in opts}
        try:
            for rnd in range(a.runs):  # alternated: native and every option in each round
                for oid, sets in opts:
                    ld, _ = load1()
                    fps, _ = timed_run(ctx, sc_sets, sets, os.path.join(work, "fps.log"), False)
                    row[oid]["fps"].append(fps)
                    row[oid]["load"].append(float(ld))
            for rnd in range(a.gpu_runs):
                for oid, sets in opts:
                    _, text = timed_run(ctx, sc_sets, sets, os.path.join(work, "gpu.log"), True)
                    fr = [float(x) for x in GPU_FRAME_RE.findall(text)]
                    if not fr:
                        raise RuntimeError("no GPU frame time lines for %s %s" % (sc, oid))
                    row[oid]["gpu"].append(median(fr))
                    st = STATS_RE.findall(text)
                    if st:
                        timed = [float(x[4]) for x in st if x[4]]
                        if timed:
                            row[oid]["scaler"].append(median(timed))
                        row[oid]["in"] = "%sx%s" % (st[-1][0], st[-1][1])
                        row[oid]["out"] = "%sx%s" % (st[-1][2], st[-1][3])
            # the quality: one seeded record of each, against the native record
            pins = {}
            for oid, sets in opts:
                pins[oid] = record(ctx, sc, sc_sets, sets, oid)
            nat = frames_by_index(pins["native"])
            if not nat:
                raise RuntimeError("the native record of %s holds no frames" % sc)
            nkeys = frame_keys(nat)
            for oid, sets in opts:
                fr = frames_by_index(pins[oid])
                if sorted(fr) != sorted(nat):
                    raise RuntimeError("%s %s: its frames (%d) are not the native record's (%d)" % (sc, oid, len(fr), len(nat)))
                t0 = read_tga_header(fr[frame_keys(fr)[0]])
                row[oid]["capture"] = "%dx%d" % t0
                row[oid]["frames"] = len(fr)
                if oid == "native":
                    continue
                sc_l, bad_l, ss_l = [], [], []
                for k in frame_keys(fr):
                    s, b, ss, _ = diff_frames(ctx, nat[k], fr[k])
                    sc_l.append(s); bad_l.append(b); ss_l.append(ss)
                row[oid]["q"] = (sum(sc_l) / len(sc_l), max(sc_l), max(bad_l), sum(ss_l) / len(ss_l), min(ss_l))
            # matched crops of the same frame
            mid = nkeys[len(nkeys) // 2]
            nimg = read_tga(nat[mid])
            first = [o for o, _ in opts if o != "native"]
            ww, wh = min(nimg[0], 200), min(nimg[1], 150)
            x0 = y0 = 0
            if first:
                x0, y0 = choose_window(nimg, read_tga(frames_by_index(pins[first[0]])[mid]), ww, wh)
            for oid, _ in opts:
                img = nimg if oid == "native" else read_tga(frames_by_index(pins[oid])[mid])
                write_file(os.path.join(rdir, "crops", "%s__%s.png" % (sc, oid)), crop_png(img, x0, y0, ww, wh, 2))
            write_file(os.path.join(rdir, "crops", "%s.txt" % sc),
                       ("frame\t%s\nwindow\t%d,%d,%d,%d\n" % (mid, x0, y0, ww, wh)).encode())
            shutil.rmtree(os.path.join(ctx.store), ignore_errors=True)
            os.mkdir(ctx.store)
        except (RuntimeError, ValueError, OSError, subprocess.SubprocessError) as e:
            errors.append("%s: %s" % (sc, e))
            for oid, _ in opts:
                results[(oid, sc)] = {"error": str(e)}
            continue
        for oid, sets in opts:
            results[(oid, sc)] = {"row": row[oid], "sets": sets}

    load_end, uptime_end = load1()
    out_rows = []
    for (oid, sc), r in sorted(results.items(), key=lambda kv: (kv[0][1], kv[0][0] != "native", kv[0][0])):
        if "error" in r:
            out_rows.append((oid, sc, "error", "", "", "", "", "", "", "", "", "", "", "", "", "", "", r["error"][:300].replace("\t", " ").replace("\n", " ")))
            continue
        w, q = r["row"], r["row"].get("q")
        out_rows.append((oid, sc, "ok", " ".join("%s=%s" % kv for kv in r["sets"]), csv_floats(w["fps"], 1),
                         ",".join("%.2f" % x for x in w["load"]), csv_floats(w["gpu"], 4), csv_floats(w["scaler"], 4),
                         w["in"], w["out"], w["capture"], w["frames"],
                         *(["", "", "", "", ""] if q is None else [fnum(x, 6) for x in q]), load_note(w["load"])))
    tsv_write(os.path.join(rdir, "results.tsv"), RESULT_COLS, out_rows)
    tsv_write(os.path.join(rdir, "skipped.tsv"), SKIPPED_COLS, skipped)
    meta = [("host", host), ("date", date), ("commit", commit), ("tree", dirty), ("build", ctx.build_note), ("device", device), ("family", family),
            ("modes", base_cv.get("r_metalfx_modes", "")), ("size", "%dx%d" % (ctx.width, ctx.height)),
            ("fps_runs", a.runs), ("gpu_runs", a.gpu_runs), ("seed", ctx.seed), ("every", ctx.every),
            ("uptime_start", uptime_start), ("uptime_end", uptime_end), ("errors", len(errors))]
    tsv_write(os.path.join(rdir, "meta.tsv"), ["key", "value"], meta)
    html_path = os.path.join(os.path.dirname(rdir), os.path.basename(rdir) + ".html")
    build_report(rdir, [a2 for a2 in (a.other or [])], html_path)
    print("results: " + rdir)
    print("report:  " + html_path)
    for e in errors:
        print("error: " + e, file=sys.stderr)
    return 1 if errors else 0


def read_tga_header(path):
    with open(path, "rb") as f:
        h = f.read(18)
    if len(h) != 18 or h[2] != 2:
        raise ValueError("%s: not an uncompressed truecolor TGA" % path)
    return struct.unpack("<HH", h[12:16])


# ---------------------------------------------------------------- the report

def read_regular(path):
    """The text of a regular file. Anything else (a FIFO, a device, a directory, a symbolic link) is
    refused, and opening never waits: a FIFO in a results folder must not stop the report."""
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    except OSError as e:
        raise ValueError("%s: cannot be read as a file of results: %s" % (path, e))
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            raise ValueError("%s: not a regular file" % path)
        with os.fdopen(fd, "r", closefd=False) as f:
            return f.read()
    finally:
        os.close(fd)


def read_tsv(path, cols):
    lines = read_regular(path).split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    if not lines or lines[0].split("\t") != cols:
        raise ValueError("%s: the header is not %s" % (path, cols))
    rows = []
    for n, line in enumerate(lines[1:], 2):
        c = line.split("\t")
        if len(c) != len(cols):
            raise ValueError("%s:%d: want %d columns, got %d" % (path, n, len(cols), len(c)))
        rows.append(dict(zip(cols, c)))
    return rows


def load_host(d):
    meta = {}
    for r in read_tsv(os.path.join(d, "meta.tsv"), ["key", "value"]):
        meta[r["key"]] = r["value"]
    for k in ("host", "date", "commit", "device", "family", "modes"):
        if k not in meta:
            raise ValueError("%s: meta.tsv has no %s" % (d, k))
    res = read_tsv(os.path.join(d, "results.tsv"), RESULT_COLS)
    skp = read_tsv(os.path.join(d, "skipped.tsv"), SKIPPED_COLS)
    for r in res:
        # names that choose a file (a crop) or a row are names, never paths; a status is one the generator writes
        if not NAME_RE.match(r["option"]) or not NAME_RE.match(r["scenario"]):
            raise ValueError("%s: results.tsv names an option or scenario that is not a plain name: %r %r" % (d, r["option"], r["scenario"]))
        if r["status"] not in ("ok", "error"):
            raise ValueError("%s: results.tsv has the status %r" % (d, r["status"]))
        for k in ("fps_runs", "gpu_frame_ms_runs", "scaler_ms_runs", "load_runs"):
            for x in [x for x in r[k].split(",") if x]:
                float(x)  # a number or the file is refused
    for s in skp:
        if not NAME_RE.match(s["option"]) and s["option"] != "*":
            raise ValueError("%s: skipped.tsv names an option that is not a plain name: %r" % (d, s["option"]))
        if not NAME_RE.match(s["scenario"]) and s["scenario"] != "*":
            raise ValueError("%s: skipped.tsv names a scenario that is not a plain name: %r" % (d, s["scenario"]))
    return {"dir": d, "meta": meta, "res": res, "skipped": skp}


def read_crop(rdir, sc, oid):
    """The bytes of the crop of this host's own results folder for a scenario and option, or None.
    Only a plain file directly inside <rdir>/crops, under a name made of two plain names: no
    link at the crops folder or the file, nothing else."""
    if not NAME_RE.match(sc) or not NAME_RE.match(oid):
        return None
    cdir = os.path.join(rdir, "crops")
    try:
        if not stat.S_ISDIR(os.lstat(cdir).st_mode):
            return None
        fd = os.open(os.path.join(cdir, "%s__%s.png" % (sc, oid)), os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    except OSError:
        return None
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            return None
        with os.fdopen(fd, "rb", closefd=False) as f:
            return f.read()
    finally:
        os.close(fd)


def floats(s):
    return [float(x) for x in s.split(",") if x]


def rng(v, nd=1, unit=""):
    if not v:
        return "not measured"
    lo, hi = min(v), max(v)
    t = ("%.*f" % (nd, lo)) if lo == hi else ("%.*f to %.*f" % (nd, lo, nd, hi))
    return "%s%s (%d runs)" % (t, unit, len(v))


EXTERNAL_PATTERNS = [
    r"(?i)(https?:)?//[a-z0-9]", r"(?i)<link\b", r"(?i)@import", r"(?i)<script[^>]*\bsrc\s*=",
    r"(?i)<iframe\b", r"(?i)<object\b", r"(?i)<embed\b", r"(?i)\bsrcset\s*=",
    r"(?i)\burl\(\s*(?!['\"]?data:)", r"(?i)\bhref\s*=\s*(?!['\"]?#)",
    r"(?i)\bsrc\s*=\s*(?!['\"]?data:image/png;base64,)", r"(?i)<base\b", r"(?i)\bformaction\b",
]


def external_refs(doc):
    """What in the document points outside it: every match of a pattern above."""
    found = []
    # the bytes of an embedded PNG are base64 and may hold "//"; the data: prefix itself is checked below
    doc = re.sub(r'(src="data:image/png;base64,)[A-Za-z0-9+/=]*', r"\1", doc)
    for p in EXTERNAL_PATTERNS:
        for m in re.finditer(p, doc):
            found.append(doc[m.start():m.start() + 40])
    return found


CSS = """
:root{color-scheme:light dark;--bg:#fbfbfa;--fg:#1c1c1e;--mut:#636366;--line:#d1d1d6;--card:#fff;--acc:#0a60c8}
@media (prefers-color-scheme:dark){:root{--bg:#1c1c1e;--fg:#f2f2f7;--mut:#a1a1a6;--line:#3a3a3c;--card:#2c2c2e;--acc:#6cb2ff}}
body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 -apple-system,system-ui,sans-serif}
main{max-width:1180px;margin:0 auto;padding:16px}
h1{font-size:22px;margin:.2em 0}h2{font-size:17px;margin:1.6em 0 .4em;border-bottom:1px solid var(--line);padding-bottom:4px}
table{border-collapse:collapse;width:100%;background:var(--card);font-size:13px}
th,td{border:1px solid var(--line);padding:4px 7px;text-align:left;vertical-align:top}
th{font-weight:600}td.n{font-variant-numeric:tabular-nums}.mut{color:var(--mut)}
.scroll{overflow-x:auto}.strip{display:flex;flex-wrap:wrap;gap:10px}
figure{margin:0;background:var(--card);border:1px solid var(--line);padding:6px}
figure img{display:block;max-width:100%;image-rendering:pixelated}figcaption{font-size:12px;margin-top:4px}
code{font:12px ui-monospace,monospace}
"""

NOT_MEASURED = [
    ("Input sample to present delay, with and without frame generation",
     "No hook gives it. It needs a timestamp at the input sample and one at the present, in the renderer, "
     "printed on a stats line. Frame generation itself is not built in ref_metal yet (r_metalfx_interp reads 0)."),
    ("Rendered and presented frame rates, and the interpolator's GPU time",
     "Needs the interpolator and a present counter apart from the rendered-frame counter (a metal_stats field)."),
    ("Shimmer and ghosting on a still camera",
     "Needs a scenario with a held camera (a demo that stops, or a console command that freezes the view) "
     "and a few hundred frames captured in a row; the pin harness's cadence of one frame in 60 does not give consecutive frames."),
    ("Generated-frame crops from strafing, explosions and particles",
     "Needs frame generation and a capture of presented, not rendered, frames."),
    ("GPU time of a ray-traced denoiser pass",
     "The frame GPU time covers it; a per-pass time needs a stats field like the scaler's."),
]


def esc(s):
    return html.escape(str(s), quote=True)


def host_cell(h, scenario, oid):
    """(html of the cells of one host for one option and scenario)."""
    for r in h["res"]:
        if r["option"] == oid and r["scenario"] == scenario:
            if r["status"] != "ok":
                return ['<td colspan="4" class="mut">error: %s</td>' % esc(r["note"])], r
            fps = rng(floats(r["fps_runs"]), 1)
            gpu = rng(floats(r["gpu_frame_ms_runs"]), 3, " ms")
            sc = floats(r["scaler_ms_runs"])
            if oid == "native":
                q = "reference"
            elif r["ssim_mean"]:
                q = "SSIM %.4f (min %.4f), score %.4f (max %.4f), bad max %.4f" % (
                    float(r["ssim_mean"]), float(r["ssim_min"]), float(r["score_mean"]), float(r["score_max"]), float(r["bad_max"]))
            else:
                q = "not measured"
            size = "%s to %s" % (r["input_size"], r["output_size"]) if r["input_size"] else ("%s, native" % r["capture_size"])
            extra = ("; scaler pass %s" % rng(sc, 3, " ms")) if sc else ""
            loaded = load_note(floats(r["load_runs"]))
            if loaded:
                fps += " [" + loaded + "]"
            return ['<td class="n">%s</td><td class="n">%s%s</td><td class="n">%s</td><td>%s</td>' % (
                esc(fps), esc(gpu), esc(extra), esc(size), esc(q))], r
    for s in h["skipped"]:
        if s["option"] == oid and s["scenario"] in ("*", scenario):
            return ['<td colspan="4" class="mut">not run: %s</td>' % esc(s["reason"])], None
        if s["option"] == "*" and s["scenario"] == scenario:
            return ['<td colspan="4" class="mut">not run: %s</td>' % esc(s["reason"])], None
    return ['<td colspan="4" class="mut">no row</td>'], None


def build_report(rdir, others, out_html):
    local = load_host(rdir)
    hosts = [local] + [load_host(o) for o in others]
    labels = []
    for i, h in enumerate(hosts):
        m = h["meta"]
        labels.append("%s (%s)" % (m["host"], "this host" if i == 0 else "numbers only"))
    scenarios = sorted({r["scenario"] for h in hosts for r in h["res"]})
    optorder = []
    for h in hosts:
        for r in h["res"]:
            if r["option"] not in optorder:
                optorder.append(r["option"])
    for h in hosts:  # an option another host could not run still has its row, saying why
        for s in h["skipped"]:
            if s["option"] != "*" and s["option"] not in optorder:
                optorder.append(s["option"])
    optorder.sort(key=lambda o: (o != "native", o))
    label_of = {"native": "native size, r_metalfx 0 (reference)"}
    try:
        for o in load_options(os.path.join(HERE, "options.tsv")):
            label_of[o["id"]] = o["label"]
    except (OSError, ValueError):
        pass
    p = []
    m0 = local["meta"]
    p.append("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">")
    p.append("<title>MetalFX report %s %s</title><style>%s</style></head><body><main>" % (esc(m0["host"]), esc(m0["date"]), CSS))
    p.append("<h1>MetalFX report: %s, %s</h1>" % (esc(m0["host"]), esc(m0["date"])))
    p.append("<p class=\"mut\">Local only. The crops are made from the owner's game data and are not to be committed, uploaded or attached anywhere. "
             "Numbers from other hosts are compared by value; their images are never read.</p>")
    p.append("<h2>Hosts</h2><div class=\"scroll\"><table><tr><th>Host</th><th>Device</th><th>Family</th><th>r_metalfx_modes</th><th>Engine commit</th><th>Size</th><th>Runs (fps / GPU)</th><th>Load at start</th><th>Load at end</th></tr>")
    for h, lab in zip(hosts, labels):
        m = h["meta"]
        p.append("<tr><td>%s</td><td>%s</td><td>%s</td><td class=\"n\">%s</td><td><code>%s %s</code></td><td>%s</td><td>%s / %s</td><td><code>%s</code></td><td><code>%s</code></td></tr>" % (
            esc(lab), esc(m["device"]), esc(m["family"]), esc(m["modes"]), esc(m["commit"]), esc(m.get("tree", "")),
            esc(m.get("size", "")), esc(m.get("fps_runs", "")), esc(m.get("gpu_runs", "")),
            esc(m.get("uptime_start", "")), esc(m.get("uptime_end", ""))))
    p.append("</table></div>")
    for sc in scenarios:
        p.append("<h2>Scenario %s</h2><div class=\"scroll\"><table><tr><th rowspan=\"2\">Option</th>" % esc(sc))
        for lab in labels:
            p.append("<th colspan=\"4\">%s</th>" % esc(lab))
        p.append("</tr><tr>" + "".join("<th>timedemo fps</th><th>frame GPU time</th><th>internal to output</th><th>quality against native</th>" for _ in hosts) + "</tr>")
        for oid in optorder:
            if not any(r["option"] == oid and r["scenario"] == sc for h in hosts for r in h["res"]) and \
               not any(s["option"] == oid for h in hosts for s in h["skipped"]):
                continue
            p.append("<tr><td>%s<br><code>%s</code></td>" % (esc(label_of.get(oid, oid)), esc(oid)))
            for h in hosts:
                cells, _ = host_cell(h, sc, oid)
                p.append("".join(cells))
            p.append("</tr>")
        p.append("</table></div>")
    p.append("<h2>Skipped options</h2><p class=\"mut\">An option the engine does not report, or reports without accepting its value, is not run.</p><div class=\"scroll\"><table><tr><th>Host</th><th>Option</th><th>Scenario</th><th>Reason</th></tr>")
    for h, lab in zip(hosts, labels):
        for s in h["skipped"]:
            p.append("<tr><td>%s</td><td><code>%s</code></td><td>%s</td><td>%s</td></tr>" % (esc(lab), esc(s["option"]), esc(s["scenario"]), esc(s["reason"])))
    p.append("</table></div>")
    p.append("<h2>Matched crops (this host only)</h2>")
    for sc in scenarios:
        figs = []
        for oid in optorder:
            # a crop is embedded only for a row of this host that ran (a skipped or failed row of a
            # rerun may have a crop of an earlier run beside it)
            if not any(r["option"] == oid and r["scenario"] == sc and r["status"] == "ok" for r in local["res"]):
                continue
            png = read_crop(rdir, sc, oid)
            if png is not None:
                b64 = base64.b64encode(png).decode()
                figs.append("<figure><img alt=\"%s, %s\" src=\"data:image/png;base64,%s\"><figcaption>%s</figcaption></figure>" % (esc(sc), esc(oid), b64, esc(oid)))
        if figs:
            info = ""
            tx = os.path.join(rdir, "crops", sc + ".txt")
            try:
                info = " ".join(read_regular(tx).split())
            except ValueError:
                info = ""
            p.append("<h3>%s <span class=\"mut\">%s</span></h3><div class=\"strip\">%s</div>" % (esc(sc), esc(info), "".join(figs)))
    p.append("<h2>Not measured here</h2><table><tr><th>Measure</th><th>What would give it</th></tr>")
    for a, b in NOT_MEASURED:
        p.append("<tr><td>%s</td><td>%s</td></tr>" % (esc(a), esc(b)))
    p.append("</table><p class=\"mut\">Each number is a range over the runs named in its cell; the load average and the engine commit are in the hosts table. "
             "Quality compares the same seeded frames of the same renderer at native size with r_metalfx 0: score and bad fraction are framediff's, SSIM is scripts/metalfx-report/ssim.c's.</p>")
    p.append("</main></body></html>\n")
    doc = "".join(p)
    ext = external_refs(doc)
    if ext:
        raise Refuse("the report would refer outside itself: %r" % ext[:3])
    outp = check_out_path(os.path.abspath(out_html), file=True)
    make_dirs_physical(os.path.dirname(outp))
    write_atomic(outp, doc.encode("utf-8"))
    return outp


# ---------------------------------------------------------------- the command line

def main(argv):
    ap = argparse.ArgumentParser(prog="mfx.py")
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--out", required=True)
    r.add_argument("--basedir", required=True)
    r.add_argument("--size", default="1280x720")
    r.add_argument("--runs", type=int, default=5)
    r.add_argument("--gpu-runs", type=int, default=3)
    r.add_argument("--every", type=int, default=60)
    r.add_argument("--timeout", type=int, default=180, help="seconds a timedemo run may take")
    r.add_argument("--seed", type=int, default=1)
    r.add_argument("--scenarios", default="")
    r.add_argument("--only", default="")
    r.add_argument("--other", action="append")
    r.add_argument("--host-name", dest="host", required=True)
    r.add_argument("--bin", default="")
    r.add_argument("--framediff", required=True)
    r.add_argument("--ssim", required=True)
    r.add_argument("--replace", action="store_true")
    b = sub.add_parser("report")
    b.add_argument("--dir", required=True)
    b.add_argument("--other", action="append")
    b.add_argument("--html", required=True)
    pl = sub.add_parser("plan")
    pl.add_argument("--options", required=True)
    pl.add_argument("--cvarlist", required=True)
    pl.add_argument("--scenarios-dir", required=True)
    pl.add_argument("--scenarios", default="")
    c = sub.add_parser("check-out")
    c.add_argument("path")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "check-out":
            print(check_out_path(a.path))
            return 0
        if a.cmd == "plan":
            with open(a.cvarlist) as f:
                cv = parse_cvarlist(f.read())
            names = [s for s in a.scenarios.split(",") if s]
            scen = {s: scenario_cvars(os.path.join(a.scenarios_dir, s + ".cfg")) for s in names}
            rows, skipped = plan(load_options(a.options), cv, scen)
            for o, s in rows:
                print("run\t%s\t%s" % (o, s))
            for o, s, why in skipped:
                print("skip\t%s\t%s\t%s" % (o, s, why))
            return 0
        if a.cmd == "report":
            print(build_report(a.dir, a.other or [], a.html))
            return 0
        if a.cmd == "run":
            return cmd_run(a, argv)
    except Refuse as e:
        print("metalfx-report: refused: %s" % e, file=sys.stderr)
        return 3
    except (RuntimeError, ValueError, OSError, subprocess.SubprocessError) as e:
        print("metalfx-report: error: %s" % e, file=sys.stderr)
        return 1
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
