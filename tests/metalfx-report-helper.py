#!/usr/bin/env python3
"""tests/metalfx-report-helper.py -- the python half of tests/check-metalfx-report.sh (TASK114).

  python3 -I metalfx-report-helper.py <dir holding mfx.py> <command> [args]

Loads mfx.py from the given directory (so a mutated copy can be tested) and exercises
its functions with synthetic inputs and a fake engine. Needs no engine, no game data.
"""
import importlib.util
import os
import sys


def load(d):
    spec = importlib.util.spec_from_file_location("mfx", os.path.join(d, "mfx.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def ctx_for(m, engine, width=64, height=48):
    c = m.Ctx()
    c.bin, c.basedir, c.width, c.height, c.demo, c.timeout = engine, "/nonexistent", width, height, "demo1.dm2", 5
    return c


def mutate(src, dst, pairs):
    """Copy src to dst with each old string (which must occur exactly once) replaced."""
    with open(src) as f:
        text = f.read()
    for i in range(0, len(pairs), 2):
        old, new = pairs[i], pairs[i + 1]
        if text.count(old) != 1:
            print("mutant text occurs %d times, not once: %r" % (text.count(old), old))
            return 1
        text = text.replace(old, new)
    with open(dst, "w") as f:
        f.write(text)
    return 0


def main(argv):
    if argv[0] == "mutate":
        return mutate(argv[1], argv[2], argv[3:])
    m = load(argv[0])
    cmd, a = argv[1], argv[2:]
    if cmd == "write-link":
        # a symbolic link as the file to write: refused, never written through
        d = a[0]
        os.symlink(os.path.join(d, "target"), os.path.join(d, "l"))
        try:
            m.write_file(os.path.join(d, "l"), b"x")
        except m.Refuse:
            print("refused")
            return 0
        except OSError:
            print("refused")
            return 0
        print("wrote")
        return 1
    if cmd == "accepted":
        ok, why = m.accepted([("r_metalfx_interp", a[0])], {"r_metalfx_interp": a[1]})
        print("ok" if ok else "no: " + why)
        return 0
    if cmd == "accepted-missing":
        ok, why = m.accepted([("r_rt_denoiser", "1")], {})
        print("ok" if ok else "no: " + why)
        return 0
    if cmd == "probe":
        try:
            cv, _ = m.probe(ctx_for(m, a[0]), [], os.path.join(a[1], "probe.log"))
            print("ok %d cvars" % len(cv))
        except RuntimeError as e:
            print("error: %s" % e)
        return 0
    if cmd == "timed":
        try:
            fps, _ = m.timed_run(ctx_for(m, a[0]), [], [], os.path.join(a[1], "t.log"), False)
            print("ok %.1f" % fps)
        except RuntimeError as e:
            print("error: %s" % e)
        return 0
    if cmd == "crop":
        img = m.read_tga(a[0])
        with open(a[1], "wb") as f:
            f.write(m.crop_png(img, 0, 0, 16, 12, 2))
        return 0
    if cmd == "make-host":
        # make-host DIR HOST FPS_LOW FPS_HIGH TGA [other]
        d, host, lo, hi, tga = a[:5]
        os.makedirs(os.path.join(d, "crops"))
        img = m.read_tga(tga)
        for name in ("world__native", "world__fx1-s50"):
            with open(os.path.join(d, "crops", name + ".png"), "wb") as f:
                f.write(m.crop_png(img, 0, 0, 16, 12, 1 + len(host) % 3))
        runs = [float(lo) + (float(hi) - float(lo)) * i / 4 for i in range(5)]
        base = [m.fnum(x, 1) for x in runs]
        rows = []
        for oid, scale in (("native", 1.0), ("fx1-s50", 1.4)):
            fps = ",".join(m.fnum(x * scale, 1) for x in runs)
            rows.append((oid, "world", "ok", "r_metalfx=0" if oid == "native" else "r_metalfx=1 r_metalfx_scale=0.50", fps,
                         "1.10,1.20,1.30,1.40,1.50", "3.0000,3.2000,3.1000", "" if oid == "native" else "0.4000,0.4500,0.4200",
                         "" if oid == "native" else "640x360", "" if oid == "native" else "1280x720", "1280x720", "12",
                         *(["", "", "", "", ""] if oid == "native" else ["0.010000", "0.020000", "0.300000", "0.950000", "0.900000"]), ""))
        m.tsv_write(os.path.join(d, "results.tsv"), m.RESULT_COLS, rows)
        m.tsv_write(os.path.join(d, "skipped.tsv"), m.SKIPPED_COLS,
                    [("fx3-s50", "*", "unsupported: r_metalfx 3 is not in r_metalfx_modes 3")])
        m.tsv_write(os.path.join(d, "meta.tsv"), ["key", "value"],
                    [("host", host), ("date", "20260101"), ("commit", "deadbeef"), ("tree", "clean"), ("device", "Test GPU"),
                     ("family", "apple9"), ("modes", "3"), ("size", "1280x720"), ("fps_runs", "5"), ("gpu_runs", "3"),
                     ("seed", "1"), ("every", "60"), ("uptime_start", "up 1 day, load averages: 1.00 1.00 1.00"),
                     ("uptime_end", "up 1 day, load averages: 1.10 1.00 1.00")])
        return 0
    print("unknown command " + cmd)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
