#!/usr/bin/env python3
# scripts/pins/rtvar.py -- the ray-tracing variation capture of scripts/rt-variations.sh (TASK173).
# Python 3 standard library only. Reads the presets and views of scripts/pins/rt-variations.txt,
# records every view's base scenario under every preset with scripts/pins.sh record (one scratch
# scenario each, scripts/pins/scenarios/rtvar-*.cfg, git-ignored and removed after its record), and
# writes, per view: a captioned contact sheet, a difference clip (off, then each preset) and the
# full-size frames, as PNG (the clip an animated PNG). The images are made with evidence.py's own
# encoders and 5x7 font. Nothing here edits pins.sh or a committed scenario.
#
#   rtvar.py check [--file F]                         parse the presets file, print its counts
#   rtvar.py plan  [--file F] [--views a,b] [--presets a,b]
#   rtvar.py run   --out DIR --framediff BIN [--file F] [--views a,b] [--presets a,b] [--seed N]
#                  [--size WxH] [--bin PATH] [--replace] [--clip-ms N]
#   rtvar.py selftest                                 no engine, no game data
#
# run refuses an --out inside a git work tree (scripts/metalfx-report/mfx.py check_out_path, called
# here in-process, and the path it returns is the one used), and so does pins.sh for the store,
# which is <out>/store (so no view may be named store). A complete record of the same pin id already
# in the store is used again unless --replace is given, so a run can be split by --views. Every check
# runs before anything is removed or created.
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(1, os.path.join(ROOT, "scripts", "metalfx-report"))
import evidence as ev  # noqa: E402
import mfx  # noqa: E402  (check_out_path, sh_group, git_env)

PINS = os.path.join(ROOT, "scripts", "pins.sh")
MFX = os.path.join(ROOT, "scripts", "metalfx-report", "mfx.py")
SCEN_DIR = os.path.join(HERE, "scenarios")
DEFAULT_FILE = os.path.join(HERE, "rt-variations.txt")
SCRATCH = "rtvar-"
NAME_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
BASE_RE = re.compile(r"^[a-z0-9_][a-z0-9_-]*$")
CVAR_RE = re.compile(r"^(r_rt_[a-z0-9_]+|r_fullbright)$")
VALUE_RE = re.compile(r"^-?[0-9]+(\.[0-9]+)?$")
CAPTION_RE = re.compile(r"^[A-Za-z0-9 .,:;()+=/_'-]+$")
DEMO_RE = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]*\.dm2$")
VALIDATION_ON = "Metal API Validation Enabled"
CVARLIST_RE = re.compile(r'^[ *][ U][ S][ L-] ([A-Za-z0-9_]+) "(.*)"$')
RESERVED_VIEWS = ("store",)  # <out>/store is the pin store: a view folder there would be removed by --replace
OUT_BAD_RE = re.compile(r"[\s\x00-\x1f\x7f]")  # pins.sh's pin directory is read back as one word
RECORD_TIMEOUT = 1200  # seconds for one record, the host-lock wait included (pins.sh takes the lock itself)

# the glyphs the captions use beyond evidence.py's font
ev.FONT[","] = "000000000C0408"
ev.FONT[";"] = "000C0C000C0408"
ev.FONT["'"] = "0C040800000000"


class Bad(Exception):
    pass


def parse(path):
    """(presets, views) of the file: presets a list of (name, caption, [(cvar, value)]), views a
    list of (name, base, demo, every, capture), capture an int or "auto:<preset>"."""
    presets, views, seen = [], [], set()
    with open(path) as f:
        lines = f.read().splitlines()
    for n, line in enumerate(lines, 1):
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        where = "%s:%d" % (os.path.basename(path), n)
        fields = [x.strip() for x in s.split("|")]
        head = fields[0].split()
        if len(fields) != 3 or len(head) != 2:
            raise Bad("%s: want '<preset|view> <name> | ... | ...'" % where)
        kind, name = head
        if not NAME_RE.match(name):
            raise Bad("%s: bad name %r" % (where, name))
        if (kind, name) in seen:
            raise Bad("%s: %s %s is named twice" % (where, kind, name))
        seen.add((kind, name))
        if kind == "preset":
            caption, words = fields[1], fields[2].split()
            if not CAPTION_RE.match(caption):
                raise Bad("%s: bad caption %r" % (where, caption))
            if len(words) % 2:
                raise Bad("%s: the cvars of %s are not <cvar> <value> pairs" % (where, name))
            sets = list(zip(words[0::2], words[1::2]))
            for k, v in sets:
                if not CVAR_RE.match(k) or not VALUE_RE.match(v):
                    raise Bad("%s: %s: bad cvar or value %r %r" % (where, name, k, v))
            if len({k for k, _ in sets}) != len(sets):
                raise Bad("%s: %s sets a cvar twice" % (where, name))
            presets.append((name, caption, sets))
        elif kind == "view":
            if name in RESERVED_VIEWS:
                raise Bad("%s: the view name %s is reserved: <out>/%s is the pin store" % (where, name, name))
            base = fields[1].split()
            if len(base) != 3 or not BASE_RE.match(base[0]) or not DEMO_RE.match(base[1]) or \
                    not re.match(r"^[1-9][0-9]{0,3}$", base[2]):
                raise Bad("%s: %s: want '<base scenario> <demo>.dm2 <every>'" % (where, name))
            cap = fields[2]
            if re.match(r"^[0-9]{1,6}$", cap):
                cap = int(cap)
            elif not re.match(r"^auto:[a-z0-9][a-z0-9-]*$", cap):
                raise Bad("%s: %s: the capture is a number or auto:<preset>" % (where, name))
            views.append((name, base[0], base[1], int(base[2]), cap))
        else:
            raise Bad("%s: unknown entry %r" % (where, kind))
    names = [p[0] for p in presets]
    if "off" not in names or dict((p[0], p[2]) for p in presets)["off"]:
        raise Bad("the presets need an 'off' that sets no cvar")
    if not views:
        raise Bad("no view")
    for v in views:
        if isinstance(v[4], str) and v[4][5:] not in names:
            raise Bad("view %s: no preset %s" % (v[0], v[4][5:]))
        if isinstance(v[4], str) and v[4][5:] == "off":
            raise Bad("view %s: auto:off compares off with itself" % v[0])
    return presets, views


def select(items, want, what):
    if not want:
        return list(items)
    names = [x for x in want.split(",") if x]
    have = {x[0]: x for x in items}
    for n in names:
        if n not in have:
            raise Bad("no %s %s" % (what, n))
    return [x for x in items if x[0] in names]


def plan(presets, views, want_views, want_presets, scen_dir=SCEN_DIR):
    """(groups, records): groups maps (base, demo, every) to its name (its first view's) and views;
    records is a list of (group key, preset, scratch scenario name), off first."""
    views = select(views, want_views, "view")
    chosen = select(presets, want_presets, "preset")
    names = {p[0] for p in chosen}
    for v in views:  # off and the preset an auto capture names are always recorded
        names.add("off")
        if isinstance(v[4], str):
            names.add(v[4][5:])
    chosen = [p for p in presets if p[0] in names]
    groups = {}
    for v in views:
        key = (v[1], v[2], v[3])
        if key not in groups:
            if not os.path.isfile(os.path.join(scen_dir, v[1] + ".cfg")):
                raise Bad("view %s: no scenario %s" % (v[0], v[1]))
            groups[key] = (v[0], [])
        groups[key][1].append(v)
    records = []
    for key, (gname, _) in groups.items():
        for p in chosen:
            records.append((key, p, "%s%s-%s" % (SCRATCH, gname, p[0])))
    return groups, records


def scenario_body(base_cfg, sets):
    with open(base_cfg) as f:
        body = "// scratch scenario of scripts/rt-variations.sh (TASK173), removed after its record\n" + f.read()
    if not body.endswith("\n"):
        body += "\n"
    return body + "".join("set %s %s\n" % kv for kv in sets)


def pins_env(a, key):
    env = dict(os.environ)
    env.update({"PINS_STORE": a.store, "PINS_DEMO": key[1], "PINS_EVERY": str(key[2]),
                "PINS_WIDTH": str(a.w), "PINS_HEIGHT": str(a.h), "PINS_SEED": str(a.seed),
                "MTL_DEBUG_LAYER": "1"})
    env.pop("PINS_DRYRUN", None)
    env.pop("PINS_TRACE", None)
    if a.bin:
        env["PINS_BIN"] = a.bin
    return env


def check_validation(log, label):
    with open(log, errors="replace") as f:
        text = f.read()
    if VALIDATION_ON not in text:
        raise Bad("%s: the Metal validation layer was not enabled" % label)
    for line in text.splitlines():
        if CVARLIST_RE.match(line):
            continue
        if "validation" in line.lower() and VALIDATION_ON.lower() not in line.lower():
            raise Bad("%s: a validation message: %s" % (label, line.strip()[:200]))


def record(a, key, scen):
    """The pin directory of one record, recorded now or found complete in the store. Each pins.sh
    runs in a process group of its own (mfx.sh_group), stopped whole on a timeout or a signal: the
    engine under it included, not only the lock shell. The timeout counts from the start of pins.sh,
    the host-lock wait included: pins.sh takes the lock itself, so this side cannot tell when it is
    taken."""
    env = pins_env(a, key)
    dry = dict(env, PINS_DRYRUN="1")
    rc, out = mfx.sh_group([PINS, "record", "metal", scen], env=dry, timeout=120)
    m = re.search(r"\+set pins_dir (\S+)", out)
    if rc != 0 or not m:
        raise Bad("pins.sh record %s (dry run) failed: %s" % (scen, out.strip()[-300:]))
    d = m.group(1)
    rel = os.path.relpath(d, a.out)
    if os.path.isfile(os.path.join(d, "complete")) and not a.replace:
        print("rt-variations: %s: using the complete record %s" % (scen, rel))
    else:
        cmd = [PINS, "record", "metal", scen] + (["--replace"] if a.replace else [])
        rc, out = mfx.sh_group(cmd, env=env, timeout=RECORD_TIMEOUT)
        if rc != 0:
            out = out.strip()  # the reason leads, the path after it: keep both ends of a long message
            raise Bad("pins.sh record %s failed: %s" % (scen, out if len(out) <= 700 else out[:350] + " ... " + out[-350:]))
        print("rt-variations: %s: %s" % (scen, out.strip().splitlines()[0] if out.strip() else "recorded"))
    if not os.path.isfile(os.path.join(d, "complete")):
        raise Bad("%s: the record is not complete" % rel)
    check_validation(os.path.join(d, "engine.log"), rel)
    return d


def frames(d):
    """The frames of a pin by capture number (<scenario>-<number>.tga)."""
    out = {}
    for f in os.listdir(d):
        m = re.match(r"^.*-([0-9]+)\.tga$", f)
        if m:
            out[int(m.group(1))] = os.path.join(d, f)
    return out


def score(framediff, x, y):
    p = subprocess.run([framediff, x, y, "--threshold", "1", "--bad-threshold", "1"], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, universal_newlines=True)
    m = re.match(r"^score=([0-9.]+)", p.stdout)
    if p.returncode not in (0, 2) or not m:
        raise Bad("framediff %s %s failed: %s" % (x, y, (p.stdout + p.stderr).strip()[:200]))
    return float(m.group(1))


def wrap(text, width):
    lines, cur = [], ""
    for w in text.split():
        t = (cur + " " + w).strip()
        if len(t) > width and cur:
            lines.append(cur)
            cur = w
        else:
            cur = t
    if cur:
        lines.append(cur)
    return [x[:width] for x in lines]


def captioned(img, name, caption, scale):
    """The image under a caption bar: the preset's name, then its caption over up to three lines."""
    w, h, _ = img
    per = max(1, (w - 6) // ((ev.FONT_W + 1) * scale))
    lines = [name.upper()] + wrap(caption, per)[:3]
    lh = ev.FONT_H * scale + 3 * scale
    bar = 4 + 4 * lh
    out = ev.blank(w, h + bar, (24, 24, 24))
    for i, t in enumerate(lines):
        ev.draw_text(out, 3, 3 + i * lh, t, scale, (255, 220, 120) if i == 0 else (255, 255, 255))
    ev.paste(out, img, 0, bar)
    return out


def sheet(panels, cols=4, gap=6):
    pw, ph = panels[0][0], panels[0][1]
    cols = min(cols, len(panels))
    rows = (len(panels) + cols - 1) // cols
    out = ev.blank(cols * pw + (cols - 1) * gap, rows * ph + (rows - 1) * gap, (48, 48, 48))
    for i, p in enumerate(panels):
        ev.paste(out, p, (i % cols) * (pw + gap), (i // cols) * (ph + gap))
    return out


def write(path, data):
    with open(path, "wb") as f:
        f.write(data)


def out_path(path):
    """The output folder a run writes to: the very path mfx.check_out_path checked and returned
    (no whitespace trimmed, no second spelling), after a refusal of any whitespace or control
    character in it."""
    if OUT_BAD_RE.search(path):
        raise Bad("the output path must hold no whitespace or control character")
    try:
        return mfx.check_out_path(path)
    except mfx.Refuse as e:
        raise Bad("the output path is refused: %s" % e)


def write_index(a, names, lines):
    write(os.path.join(a.out, "index-%s.txt" % "-".join(names)), ("\n".join(lines) + "\n").encode())


def cmd_run(a):
    a.out = out_path(a.out)
    m = re.match(r"^([1-9][0-9]{1,4})x([1-9][0-9]{1,4})$", a.size)
    if not m:
        raise Bad("--size is <width>x<height>")
    a.w, a.h = int(m.group(1)), int(m.group(2))
    if not re.match(r"^(0|[1-9][0-9]{0,8})$", str(a.seed)):
        raise Bad("--seed is 1 to 9 digits")
    presets, views = parse(a.file)
    groups, records = plan(presets, views, a.views, a.presets)
    sel = [v for _, (_, vs) in groups.items() for v in vs]
    a.store = os.path.join(a.out, "store")
    if os.path.lexists(a.store) and (os.path.islink(a.store) or not os.path.isdir(a.store)):
        raise Bad("<out>/store is not a directory")
    gone = []  # every check first, then the removals
    for v in sel:
        vd = os.path.join(a.out, v[0])
        if os.path.lexists(vd):
            if not a.replace:
                raise Bad("<out>/%s exists; pass --replace" % v[0])
            if os.path.islink(vd) or not os.path.isdir(vd):
                raise Bad("<out>/%s is not a directory" % v[0])
            gone.append(vd)
    for vd in gone:
        shutil.rmtree(vd)
    os.makedirs(a.store, exist_ok=True)
    pins = {}
    index = ["rt-variations: seed %s, %dx%d, presets file %s" % (a.seed, a.w, a.h, os.path.basename(a.file))]
    done = []
    try:
        run_groups(a, groups, records, pins, index, done)
    except BaseException:
        if done:  # the index of the views that were written before the failure
            write_index(a, done, index + ["stopped: the run failed after these views; the rest were not written"])
        raise
    write_index(a, done, index)
    return 0


def run_groups(a, groups, records, pins, index, done):
    """One group at a time: its records, then its views, so a later group's failure keeps the
    earlier views. A view's index lines join index, and its name done, once the view is written."""
    for key, (gname, vs) in groups.items():
        for k, pr, scen in records:
            if k != key:
                continue
            cfg = os.path.join(SCEN_DIR, scen + ".cfg")
            try:
                fd = os.open(cfg, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o644)
            except FileExistsError:
                raise Bad("%s is left from an earlier run that was killed; remove it and run again"
                          % os.path.relpath(cfg, ROOT))
            try:
                with os.fdopen(fd, "w") as f:
                    f.write(scenario_body(os.path.join(SCEN_DIR, key[0] + ".cfg"), pr[2]))
                pins[(key, pr[0])] = record(a, key, scen)
            finally:
                os.remove(cfg)
        chosen = [pr for k, pr, _ in records if k == key]
        off = frames(pins[(key, "off")])
        for v in vs:
            caps = sorted(off)
            for pr in chosen:
                caps = [c for c in caps if c in frames(pins[(key, pr[0])])]
            if not caps:
                raise Bad("view %s: no capture every record holds" % v[0])
            if isinstance(v[4], int):
                if v[4] not in caps:
                    raise Bad("view %s: no capture %d (the records hold %d to %d)" % (v[0], v[4], caps[0], caps[-1]))
                cap, why = v[4], "capture %d, named" % v[4]
            else:
                other = frames(pins[(key, v[4][5:])])
                best = max(caps, key=lambda c: (score(a.framediff, off[c], other[c]), -c))
                cap, why = best, "capture %d, where %s differs most from off" % (best, v[4][5:])
            vd = os.path.join(a.out, v[0])
            os.makedirs(os.path.join(vd, "full"))
            scale = 2 if a.w >= 600 else 1
            f = max(1, (a.w + 639) // 640)
            panels, clip = [], []
            lines = ["view %s: %s %s every %d, %s" % (v[0], key[0], key[1], key[2], why)]
            for i, pr in enumerate(chosen):
                img = ev.read_tga(frames(pins[(key, pr[0])])[cap])
                write(os.path.join(vd, "full", "%02d-%s.png" % (i, pr[0])), ev.encode_png(img))
                panels.append(captioned(ev.downscale(img, f), pr[0], pr[1], 1 if f > 1 else scale))
                clip.append(captioned(img, pr[0], pr[1], scale))
                d = score(a.framediff, off[cap], frames(pins[(key, pr[0])])[cap])
                lines.append("  %02d %-14s diff %.6f  %s  [%s]  %s" % (i, pr[0], d, pr[1],
                             " ".join("%s %s" % kv for kv in pr[2]), os.path.relpath(pins[(key, pr[0])], a.out)))
            write(os.path.join(vd, "sheet.png"), ev.encode_png(sheet(panels)))
            write(os.path.join(vd, "clip.png"), ev.encode_apng(clip, a.clip_ms))
            index.extend(lines)
            done.append(v[0])
            print("rt-variations: %s: <out>/%s/sheet.png, clip.png, %d frames in full/" % (v[0], v[0], len(chosen)))


def cmd_plan(a):
    presets, views = parse(a.file)
    groups, records = plan(presets, views, a.views, a.presets)
    for key, (gname, vs) in groups.items():
        for v in vs:
            print("view %s %s %s %d %s" % (v[0], key[0], key[1], key[2], v[4]))
    for key, pr, scen in records:
        print("record %s %s %s" % (scen, key[1], " ".join("%s=%s" % kv for kv in pr[2]) or "-"))
    return 0


def cmd_check(a):
    presets, views = parse(a.file)
    groups, records = plan(presets, views, None, None)
    print("rt-variations: %d presets, %d views, %d records" % (len(presets), len(views), len(records)))
    return 0


# ---- selftest: no engine, no game data ------------------------------------------------------
# The real scripts/pins.sh record runs in a copy of the scripts (not a git work tree, so its engine
# id is "nogit"), with the harness's stub-engine protocol (pins.sh's fake_engine): the stub takes
# the +set pairs, writes three 64x48 captures and their manifest lines into pins_dir, and logs its
# command line. A capture's grey level is 16 + capture x h, h a checksum of the r_rt_* pairs (0 for
# none), so presets differ from off and more so in later captures.
STUB = r'''#!/bin/bash
rt=""; d=""; s=""; r=""; rec=""
echo "$*" >> "%(log)s"
m=""
while [ $# -gt 0 ]; do case $1 in +demomap) m=$2;; pins_dir) d=$2;; pins_scenario) s=$2;; pins_record) rec=$2;; vid_renderer) r=$2;; r_rt_*) rt="$rt $1=$2";; esac; shift; done
[ "${MTL_DEBUG_LAYER:-}" = 1 ] && echo "Metal API Validation Enabled"
%(extra)s
h=0; [ -z "$rt" ] || h=$(( $(printf '%%s' "$rt" | cksum | cut -d' ' -f1) %% 50 + 1 ))
for c in 0 1 2; do
    v=$(( 16 + c * h ))
    f=$(printf '%%s-%%06d.tga' "$s" "$c")
    { printf '\000\000\002\000\000\000\000\000\000\000\000\000\100\000\060\000\030\000'
      head -c 9216 /dev/zero | tr '\000' "\\$(printf %%03o "$v")"; } > "$d/$f"
    echo "$f $r 64x48" >> "$d/manifest.txt"
done
%(frames)s
# the trace pins.sh record text-checks (FIX157), written beside the frames as the real engine writes
# pins_record: the one the selftest made for this demo
if [ -n "$rec" ]; then cp "%(trace)s/$([ "$m" = demo1.dm2 ] && echo demo1 || echo none).q2r" "$d/$rec"; fi
exit 0
'''

# The timedemo line pins.sh checks (FIX48): the "// frames <demo> <count>" header of the scenario
# the stub runs, printed as pins.sh's own fake engine prints it. A scenario with no header for the
# demo gets no line: pins.sh then checks nothing and says so on stderr.
STUB_FRAMES = '''n=$(sed -n "s|^// frames $m \\([0-9][0-9]*\\)$|\\1|p" "%(scen)s/$s.cfg" 2>/dev/null)
[ -z "$n" ] || printf '%%s frames, 0.3 seconds: 2296.7 fps\\n\\n' "$n"'''

TEST_FILE = """# a test presets file
preset off | off |
preset a | the sun, a test | r_rt_sun 1 r_rt_sunfloor 1
preset b | AO, a test | r_rt_ao 1 r_rt_ao_seed 1
view v1 | world demo1.dm2 30 | auto:a
view v2 | world demo1.dm2 30 | 1
view v3 | dlights other.dm2 10 | auto:b
"""

CARD_PRESETS = ["off", "shadows", "shadows-x", "sun", "sun-x", "ao", "ao-x", "water", "water-x", "all", "all-x",
                "shadows-show1", "shadows-show2", "sun-show1", "sun-show2", "ao-show1", "ao-show2", "water-show1",
                "water-show2", "sunfloor-025", "sunfloor-050", "sunfloor-075", "sunfloor-100", "preview"]


def read_index(path):
    """{view: (its capture line, {preset: diff})} of an index file."""
    out, cur = {}, None
    with open(path) as f:
        for line in f.read().splitlines():
            m = re.match(r"^view ([a-z0-9-]+): \S+ \S+ every [0-9]+, (capture .*)$", line)
            if m:
                cur = out.setdefault(m.group(1), (m.group(2), {}))
                continue
            m = re.match(r"^  [0-9]{2} (\S+) +diff ([0-9.]+)  ", line)
            if m and cur:
                cur[1][m.group(1)] = float(m.group(2))
    return out


def pixdiff(x, y):
    """The summed absolute difference of two TGA frames' pixels."""
    return sum(abs(p - q) for p, q in zip(ev.read_tga(x)[2], ev.read_tga(y)[2]))


def cmd_selftest(a):
    import tempfile
    fails = []

    def ok(cond, what):
        if not cond:
            fails.append(what)
        return cond

    def refused(text, what):
        p = os.path.join(tmp, "bad.txt")
        with open(p, "w") as f:
            f.write(text)
        try:
            parse(p)
            fails.append("parse must refuse " + what)
        except Bad:
            pass

    def keep(d):  # a folder holding a file that must survive
        os.makedirs(d)
        with open(os.path.join(d, "keep"), "w") as f:
            f.write("keep\n")

    def kept(d):
        return os.path.isfile(os.path.join(d, "keep"))

    tmp = os.path.realpath(tempfile.mkdtemp(prefix="rtvar."))
    try:
        # the committed presets: every preset the card names, the views it needs, the cvars documented
        presets, views = parse(DEFAULT_FILE)
        pd = {p[0]: dict(p[2]) for p in presets}
        for n in CARD_PRESETS:
            ok(n in pd, "the presets file has no preset " + n)
        ok({"outdoor", "indoor", "water", "dlights"} <= {v[0] for v in views}, "the four views are named")
        for n, sets in pd.items():
            if sets.get("r_rt_ao") == "1":
                ok(sets.get("r_rt_ao_seed") == "1", "preset %s fixes the AO noise" % n)
            ok("r_fullbright" not in sets, "preset %s does not use r_fullbright (it turns the RT terms off)" % n)
        for n, f in (("025", "0.25"), ("050", "0.5"), ("075", "0.75"), ("100", "1")):
            ok(pd.get("sunfloor-" + n, {}).get("r_rt_sunfloor") == f, "sunfloor-%s sets r_rt_sunfloor %s" % (n, f))
        pv = pd.get("preview", {})
        ok(float(pv.get("r_rt_flatlight", "0")) > 0 and all(pv.get(c) == "1" for c in
           ("r_rt_shadows", "r_rt_sun", "r_rt_ao", "r_rt_water")), "preview: a flat light with every RT term on")
        cap = [p[1] for p in presets if p[0] == "preview"]
        ok(cap and cap[0].startswith("preview: no traced bounce light, looks harsh") and "TASK174" in cap[0],
           "the preview's caption says it is a preview")
        with open(os.path.join(ROOT, "doc", "040_cvarlist.md")) as f:
            doc = f.read()
        for c in sorted({k for p in presets for k, _ in p[2]}):
            ok("**%s**" % c in doc, "cvar %s is documented in doc/040_cvarlist.md" % c)
        groups, records = plan(presets, views, None, None)
        ok(len(records) == len(groups) * len(presets), "every view's records cover every preset")
        # refusals of the parser
        good = "preset off | off |\nview v | world demo1.dm2 30 | 1\n"
        refused("view v | world demo1.dm2 30 | 1\n", "a file without off")
        refused("preset off | off | r_rt_sun 1\n" + good[19:], "an off that sets a cvar")
        refused(good + "preset x | x | vid_renderer soft\n", "a cvar outside r_rt_*")
        refused(good + "preset x | x | r_rt_sun 1;quit\n", "a value that is not a number")
        refused(good + "preset x | x | r_rt_sun\n", "a cvar without a value")
        refused(good + "preset x | x | r_rt_sun 1 r_rt_sun 0\n", "a cvar set twice")
        refused(good + "preset off | again |\n", "a name used twice")
        refused(good + "preset X | x | r_rt_sun 1\n", "a name with a capital")
        refused(good + 'preset x | say "hi" | r_rt_sun 1\n', "a caption with a quote")
        refused(good + "view w | world ../demo1.dm2 30 | 1\n", "a demo with a path")
        refused(good + "view w | world demo1.dm2 30 | auto:nope\n", "an auto capture of no preset")
        refused(good + "view w | world demo1.dm2 30 | auto:off\n", "auto:off")
        refused(good + "frob x | x | x\n", "an unknown entry")
        refused(good + "view store | world demo1.dm2 30 | 1\n", "a view named store (the pin store)")

        # the tree copy, the stub engine and the game-data stand-in
        tree = os.path.join(tmp, "tree")
        for d in ("scripts/pins", "scripts/metalfx-report"):
            os.makedirs(os.path.join(tree, d))
        shutil.copy2(PINS, os.path.join(tree, "scripts"))
        shutil.copy2(os.path.join(ROOT, "scripts", "rt-variations.sh"), os.path.join(tree, "scripts"))
        shutil.copy2(MFX, os.path.join(tree, "scripts", "metalfx-report"))
        for f in ("framediff.c", "evidence.py", "rtvar.py"):
            shutil.copy2(os.path.join(HERE, f), os.path.join(tree, "scripts", "pins"))
        shutil.copytree(SCEN_DIR, os.path.join(tree, "scripts", "pins", "scenarios"),
                        ignore=shutil.ignore_patterns(SCRATCH + "*"))
        tfile = os.path.join(tmp, "t.txt")
        with open(tfile, "w") as f:
            f.write(TEST_FILE)
        os.makedirs(os.path.join(tmp, "gd", "baseq2"))
        log = os.path.join(tmp, "calls.log")

        # the traces the stub writes: the real charscan (built from the real refrecord.c, copied into the
        # tree as pins.sh's text check builds it there) makes one that draws exactly what demo1's table
        # declares, and one that draws no text, for a demo with no table (FIX157)
        for src in ("tests/refrecord/charscan.c", "src/client/vid/refrecord.c"):
            os.makedirs(os.path.join(tree, os.path.dirname(src)), exist_ok=True)
            shutil.copy2(os.path.join(ROOT, src), os.path.join(tree, src))
        for src in ("src/client/vid/header", "src/common/header"):  # what refrecord.h includes
            shutil.copytree(os.path.join(ROOT, src), os.path.join(tree, src))
        shutil.copytree(os.path.join(HERE, "text"), os.path.join(tree, "scripts", "pins", "text"))
        traces = os.path.join(tmp, "traces")
        os.makedirs(traces)
        scan = os.path.join(tmp, "charscan")
        empty = os.path.join(tmp, "empty.txt")
        open(empty, "w").close()
        ok(subprocess.run(["cc", "-std=gnu99", "-O1", "-DYQ2OSTYPE=\"pins\"", "-DYQ2ARCH=\"pins\"", "-o", scan,
                           os.path.join(tree, "tests/refrecord/charscan.c"),
                           os.path.join(tree, "src/client/vid/refrecord.c"), "-lm"]).returncode == 0, "charscan must build")
        for n, tab in (("demo1", os.path.join(HERE, "text", "demo1.dm2-every30.txt")), ("none", empty)):
            ok(subprocess.run([scan, "--make-table", os.path.join(traces, n + ".q2r"), tab]).returncode == 0,
               "the %s stub trace must be made" % n)

        def stub(name, extra="", frames=None):
            p = os.path.join(tmp, name)
            fr = (STUB_FRAMES if frames is None else frames) % {"scen": os.path.join(tree, "scripts", "pins", "scenarios")}
            with open(p, "w") as f:
                f.write(STUB % {"log": log, "extra": extra, "frames": fr, "trace": traces})
            os.chmod(p, 0o755)
            return p

        # the caller's git hook variables and git config never reach a run (mfx.git_env)
        env = dict(mfx.git_env(), PINS_BIN=stub("quake2"), PINS_BASEDIR=os.path.join(tmp, "gd"), BENCH_LOCK="0")
        for k in ("PINS_SEED", "PINS_STORE", "PINS_DEMO", "PINS_EVERY", "PINS_WIDTH", "PINS_HEIGHT", "MTL_DEBUG_LAYER"):
            env.pop(k, None)
        tool = os.path.join(tree, "scripts", "rt-variations.sh")

        def run(args, e=env):
            p = subprocess.run(["/bin/bash", tool, "run", "--file", tfile] + args, env=e, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, universal_newlines=True, timeout=600)
            return p.returncode, p.stdout

        def calls():
            if not os.path.exists(log):
                return []
            with open(log) as f:
                return f.read().splitlines()

        out = os.path.join(tmp, "out")
        rc, text = run(["--out", out])
        ok(rc == 0, "the stub run must pass: " + text[-600:])
        c = calls()
        ok(len(c) == 6, "two record groups of three presets are six engine runs, got %d" % len(c))
        ok(all("+set pins_seed 1 " in x + " " for x in c), "every record is seeded (PINS_SEED 1 by default)")
        ok(sum("+demomap demo1.dm2" in x and "+set pins_capture 30 " in x + " " for x in c) == 3 and
           sum("+demomap other.dm2" in x and "+set pins_capture 10 " in x + " " for x in c) == 3,
           "each group's records play its demo at its cadence")
        ok(any("r_rt_sunfloor 1" in x for x in c) and any("r_rt_ao_seed 1" in x for x in c),
           "the presets' cvars reach the engine")
        ok(not [f for f in os.listdir(os.path.join(tree, "scripts", "pins", "scenarios")) if f.startswith(SCRATCH)],
           "no scratch scenario is left behind")
        for v, n in (("v1", 3), ("v2", 3), ("v3", 3)):
            vd = os.path.join(out, v)
            ok(os.path.isfile(os.path.join(vd, "sheet.png")) and os.path.isfile(os.path.join(vd, "clip.png")),
               "%s: a sheet and a clip" % v)
            full = sorted(os.listdir(os.path.join(vd, "full"))) if os.path.isdir(os.path.join(vd, "full")) else []
            ok(full == ["00-off.png", "01-a.png", "02-b.png"], "%s: the full-size frames, off first: %s" % (v, full))
        st = os.path.join(out, "store")
        # bin/ holds the scanner pins.sh builds (FIX157)
        ok(sorted(x for x in os.listdir(st) if x != "bin") == sorted(SCRATCH + g + "-" + p for g in ("v1", "v3") for p in ("off", "a", "b")),
           "the store holds one record per group and preset")

        def pin(scen):
            d = os.path.join(st, scen, "metal")
            return os.path.join(d, os.listdir(d)[0])

        def png(path):
            with open(path, "rb") as f:
                return ev.decode_png(f.read())[0]

        if not fails:
            # v1 auto:a takes capture 2 (the stub's difference grows with the capture), v2 names capture 1
            ok(png(os.path.join(out, "v1", "full", "01-a.png"))[2] ==
               ev.read_tga(frames(pin(SCRATCH + "v1-a"))[2])[2], "v1: auto:a shows the capture that differs most")
            ok(png(os.path.join(out, "v2", "full", "01-a.png"))[2] ==
               ev.read_tga(frames(pin(SCRATCH + "v1-a"))[1])[2], "v2: the named capture 1")
            ok(png(os.path.join(out, "v1", "full", "00-off.png"))[2] ==
               ev.read_tga(frames(pin(SCRATCH + "v1-off"))[2])[2], "v1: off is the off record's frame")
            w, h, _ = png(os.path.join(out, "v3", "sheet.png"))
            ok(w == 3 * 64 + 2 * 6 and h > 48, "v3: three captioned panels side by side, got %dx%d" % (w, h))
            with open(os.path.join(out, "v1", "clip.png"), "rb") as f:
                clip = f.read()
            i = clip.find(b"acTL")
            ok(i > 0 and int.from_bytes(clip[i + 4:i + 8], "big") == 3, "v1: the clip is off, then each preset")
            # the index: off differs from itself by exactly 0, each other preset (the stub's pair, a and
            # b) by more on every view, and an auto view names the capture where its preset differs most
            # from off, worked out here from the frames themselves, not from framediff
            idx = [f for f in os.listdir(out) if f.startswith("index-")]
            ok(idx == ["index-v1-v2-v3.txt"], "one index file, named after the views: %s" % idx)
            got = read_index(os.path.join(out, idx[0])) if idx else {}
            ok(sorted(got) == ["v1", "v2", "v3"], "the index names every view: %s" % sorted(got))
            for v, scen, auto in (("v1", "v1", "a"), ("v2", "v1", None), ("v3", "v3", "b")):
                why, diffs = got.get(v, ("", {}))
                ok(diffs.get("off") == 0.0, "%s: off's diff is exactly 0: %s" % (v, diffs.get("off")))
                ok(all(diffs.get(n, 0.0) > 0.0 for n in ("a", "b")), "%s: a and b differ from off: %s" % (v, diffs))
                if auto:
                    o, x = frames(pin(SCRATCH + scen + "-off")), frames(pin(SCRATCH + scen + "-" + auto))
                    best = max(sorted(o), key=lambda c: (pixdiff(o[c], x[c]), -c))
                    ok(best == 2 and why == "capture %d, where %s differs most from off" % (best, auto),
                       "%s: the capture where %s differs most (%d): %r" % (v, auto, best, why))
                else:
                    ok(why == "capture 1, named", "%s: the named capture: %r" % (v, why))
        # an existing view is refused without --replace, replaced with it; a complete record is used again
        n = len(calls())
        rc, text = run(["--out", out])
        ok(rc != 0 and "pass --replace" in text and len(calls()) == n, "an existing view is refused: " + text[-300:])
        shutil.rmtree(os.path.join(out, "v1"))
        rc, text = run(["--out", out, "--views", "v1"])
        ok(rc == 0 and len(calls()) == n and "using the complete record" in text,
           "a complete record is used again: " + text[-300:])
        rc, text = run(["--out", out, "--views", "v3", "--presets", "b", "--replace"])
        ok(rc == 0 and len(calls()) == n + 2, "--replace records off and b again: " + text[-300:])
        n = len(calls())
        # a view named store is refused, and --replace never reaches the pin store
        sfile = os.path.join(tmp, "s.txt")
        with open(sfile, "w") as f:
            f.write(TEST_FILE + "view store | world demo1.dm2 30 | 1\n")
        before = sorted(os.listdir(st))
        rc, text = run(["--file", sfile, "--out", out, "--views", "store", "--replace"])
        ok(rc != 0 and os.path.isdir(st) and sorted(os.listdir(st)) == before and len(calls()) == n,
           "a view named store is refused before anything is removed: " + text[-300:])
        # a trailing space next to a link: <P>/out is a link to a folder holding v1, <P>/out<space> is
        # absent; the run is refused, nothing behind the link is removed and nothing is created
        lp, behind = os.path.join(tmp, "lp"), os.path.join(tmp, "behind")
        keep(os.path.join(behind, "v1"))
        os.makedirs(lp)
        os.symlink(behind, os.path.join(lp, "out"))
        rc, text = run(["--out", os.path.join(lp, "out") + " ", "--replace"])
        ok(rc != 0 and kept(os.path.join(behind, "v1")) and os.listdir(lp) == ["out"] and len(calls()) == n,
           "an --out with a trailing space next to a link is refused before anything is removed: " + text[-300:])
        # an abbreviated --ou after the --out rt-variations.sh checked does not stand in for it
        victim, good_out = os.path.join(tmp, "victim"), os.path.join(tmp, "o-abbrev")
        keep(os.path.join(victim, "v1"))
        rc, text = run(["--out", good_out, "--ou", victim, "--replace"])
        ok(rc != 0 and kept(os.path.join(victim, "v1")) and not os.path.lexists(good_out) and len(calls()) == n,
           "an abbreviated --ou is refused before anything is removed: " + text[-300:])
        # a later view group's failing record keeps the earlier groups' views and their index
        late = os.path.join(tmp, "o-late")
        rc, text = run(["--out", late], dict(env, PINS_BIN=stub("late", 'case $s in %sv3-*) exit 1;; esac' % SCRATCH)))
        ok(rc != 0, "a run whose later group fails must fail: " + text[-300:])
        for v in ("v1", "v2"):
            vd = os.path.join(late, v)
            full = sorted(os.listdir(os.path.join(vd, "full"))) if os.path.isdir(os.path.join(vd, "full")) else []
            ok(os.path.isfile(os.path.join(vd, "sheet.png")) and os.path.isfile(os.path.join(vd, "clip.png")) and
               full == ["00-off.png", "01-a.png", "02-b.png"], "%s is kept after a later group failed: %s" % (v, full))
        ok(not os.path.lexists(os.path.join(late, "v3")), "the failing view's folder is absent")
        idx = sorted(f for f in os.listdir(late) if f.startswith("index-")) if os.path.isdir(late) else []
        got, stopped = {}, False
        if idx == ["index-v1-v2.txt"]:
            got = read_index(os.path.join(late, idx[0]))
            with open(os.path.join(late, idx[0])) as f:
                stopped = "\nstopped: " in f.read()
        ok(idx == ["index-v1-v2.txt"] and sorted(got) == ["v1", "v2"] and stopped,
           "the index of the views written before the failure: %s %s" % (idx, sorted(got)))
        # the output path: refused inside a git work tree, relative, and nothing runs
        repo = os.path.join(tmp, "repo")
        os.makedirs(repo)
        g = subprocess.run(["git", "init", "-q", repo], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           env=mfx.git_env())
        ok(g.returncode == 0, "git init")
        n = len(calls())
        for bad, what in ((os.path.join(repo, "x"), "inside a git work tree"), ("rel/out", "relative"),
                          (os.path.join(tree, "..", "out9"), "with ..")):
            rc, text = run(["--out", bad])
            ok(rc != 0 and len(calls()) == n and not os.path.exists(os.path.join(repo, "x")),
               "an output path %s is refused before anything runs: %s" % (what, text[-200:]))
        # a stub that writes no trace is refused by the real record (FIX157)
        rc, text = run(["--out", os.path.join(tmp, "o-notrace")], dict(env, PINS_BIN=stub("notrace", 'rec=""')))
        ok(rc != 0 and "wrote no trace.q2r" in text and "incomplete" in text, "a stub with no trace is refused: " + text[-300:])
        # the timedemo line pins.sh checks: a stub that prints none, or a wrong count, is refused
        for name, fr, why in (("nolines", "true", "printed no rendered-frame count"),
                              ("wrongn", "printf '5 frames, 0.3 seconds: 2296.7 fps\\n\\n'", "rendered 5 frames")):
            rc, text = run(["--out", os.path.join(tmp, "o-" + name)], dict(env, PINS_BIN=stub(name, "", fr)))
            ok(rc != 0 and why in text and "incomplete" in text, "the %s stub is refused (%s): %s" % (name, why, text[-300:]))
        # a scratch scenario an earlier run left is named, relative to the checkout, in one line
        left = os.path.join(tree, "scripts", "pins", "scenarios", SCRATCH + "v1-off.cfg")
        with open(left, "w") as f:
            f.write("// left\n")
        rc, text = run(["--out", os.path.join(tmp, "o-left")])
        os.remove(left)
        ok(rc != 0 and "File exists" not in text and "earlier run" in text and tmp not in text and
           "scripts/pins/scenarios/" + SCRATCH + "v1-off.cfg is left" in text, "a left scratch scenario is named: " + text[-300:])
        # a failing engine and a validation message fail the run, and no scratch scenario is left
        for name, extra in (("fail", "exit 1"), ("valid", 'echo "-[MTLDebugDevice validation error] boom"')):
            rc, text = run(["--out", os.path.join(tmp, "o-" + name)], dict(env, PINS_BIN=stub(name, extra)))
            ok(rc != 0, "a run with the %s stub must fail" % name)
            ok(not [f for f in os.listdir(os.path.join(tree, "scripts", "pins", "scenarios")) if f.startswith(SCRATCH)],
               "%s: no scratch scenario is left behind" % name)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    for f in fails:  # no local path in the output: the scratch folder is named <tmp>
        for t in (tmp, os.path.realpath(tempfile.gettempdir()), tempfile.gettempdir()):
            f = f.replace(t, "<tmp>")
        print("  FAIL: " + f)
    print("rt-variations selftest: %d failed" % len(fails))
    return 1 if fails else 0


def main(argv):
    import argparse
    # no abbreviation: --ou must not stand in for --out past rt-variations.sh's exact --out match
    ap = argparse.ArgumentParser(prog="rtvar.py", allow_abbrev=False)
    sub = ap.add_subparsers(dest="cmd")
    for c in ("check", "plan", "run"):
        s = sub.add_parser(c, allow_abbrev=False)
        s.add_argument("--file", default=DEFAULT_FILE)
        if c != "check":
            s.add_argument("--views")
            s.add_argument("--presets")
        if c == "run":
            s.add_argument("--out", required=True)
            s.add_argument("--framediff", required=True)
            s.add_argument("--seed", default="1")
            s.add_argument("--size", default="640x480")
            s.add_argument("--bin")
            s.add_argument("--replace", action="store_true")
            s.add_argument("--clip-ms", type=int, default=1000)
    sub.add_parser("selftest", allow_abbrev=False)
    a = ap.parse_args(argv)
    if not a.cmd:
        ap.print_help()
        return 1
    try:
        return {"check": cmd_check, "plan": cmd_plan, "run": cmd_run, "selftest": cmd_selftest}[a.cmd](a)
    except (Bad, OSError, ValueError, subprocess.TimeoutExpired) as e:
        print("rt-variations: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
