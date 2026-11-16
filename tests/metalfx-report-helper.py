#!/usr/bin/env python3
"""tests/metalfx-report-helper.py -- the python half of tests/check-metalfx-report.sh (TASK114).

  python3 -I metalfx-report-helper.py <dir holding mfx.py> <command> [args]

Loads mfx.py from the given directory (so a mutated copy can be tested) and exercises
its functions with synthetic inputs and a fake engine. Needs no engine, no game data.
"""
import importlib.util
import os
import shutil
import signal
import sys


def load(d):
    spec = importlib.util.spec_from_file_location("mfx", os.path.join(d, "mfx.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def ctx_for(m, engine, width=64, height=48):
    c = m.Ctx()
    c.bin, c.basedir, c.width, c.height, c.demo, c.timeout = engine, "/nonexistent", width, height, "demo1.dm2", 5
    c.cfg = m.CfgGuard(engine)
    c.defaults = {}
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
    if cmd == "ext-b64":
        ok = m.external_refs('<img src="data:image/png;base64,AAAA//bbCC+/==">')
        bad = m.external_refs('<img src="https://x.test/a.png"><img src="data:image/gif;base64,AA">')
        print("b64 refs %d, outside refs %d" % (len(ok), len(bad)))
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
                         ("9.50,60.00,1.30,1.40,1.50" if host.startswith("hostb") else "1.10,1.20,1.30,1.40,1.50"), "3.0000,3.2000,3.1000", "" if oid == "native" else "0.4000,0.4500,0.4200",
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
    if cmd == "defaults":
        # a=[cvarlist file]: the defaults an engine run sets first, and the order of the +set arguments
        text = open(a[0]).read()
        cv = m.parse_cvarlist(text)
        opts = [{"id": "x", "set": [("r_metalfx", "1"), ("r_metalfx_interp", "1")]}, {"id": "y", "set": [("r_rt_denoiser", "1")]}]
        c = ctx_for(m, "/bin/true")
        c.defaults = m.default_cvars(cv, m.parse_noset(text), opts, {"rt": {"r_rt_shadows", "timedemo"}}, m.parse_archived(text))
        args = m.engine_args(c, [("r_rt_shadows", "1")], [("r_rt_denoiser", "1")], [])
        final = {}
        for i in range(len(args) - 2):
            if args[i] == "+set":
                final[args[i + 1]] = args[i + 2]
        want = {"r_metalfx": "0", "r_metalfx_interp": "0", "r_rt_shadows": "1", "r_rt_denoiser": "1", "r_rt_arch": "2"}
        bad = [k for k in want if final.get(k) != want[k]]
        bad += [k for k in ("r_rt_fixed", "r_metalfx_modes", "timedemo", "r_rt_other") if k in final]
        print("ok" if not bad else "leak: " + " ".join(bad))
        c.defaults = dict(("r_rt_x%d" % i, "0") for i in range(60))
        try:
            m.engine_args(c, [], [], [])
            print("no limit")
        except RuntimeError:
            print("limit")
        # one option over the limit costs its own row only: native and the others are kept
        c.defaults = dict(("r_rt_x%d" % i, "0") for i in range(25))
        opts = [("native", [("r_metalfx", "0")]), ("small", [("r_metalfx", "1")]),
                ("wide", [("r_wide%d" % i, "1") for i in range(12)])]
        for k in range(1, 40):  # the widest option that still fits without metal_stats: the GPU run adds it
            sets = [("r_wide%d" % i, "1") for i in range(k)]
            try:
                m.engine_args(c, [], sets, ["+demomap", c.demo])
            except RuntimeError:
                break
            edge = sets
        try:
            m.engine_args(c, [], edge + [("metal_stats", "1")], ["+demomap", c.demo])
            print("edge fits")
        except RuntimeError:
            opts.append(("edge", edge))
        kept, dropped = m.fit_options(c, [], opts)
        print("kept " + ",".join(k for k, _ in kept) + " dropped " + ",".join(k for k, _ in dropped))
        return 0
    if cmd == "cfg-both":
        # cfg-both BINDIR: config.cfg and config.cfg.mfx-saved both exist at hold(): neither is deleted
        d = os.path.join(a[0], "baseq2")
        os.makedirs(d, exist_ok=True)
        for n, t in (("config.cfg.mfx-saved", "owner\n"), ("config.cfg", "played since\n")):
            with open(os.path.join(d, n), "w") as f:
                f.write(t)
        g = m.CfgGuard(os.path.join(a[0], "quake2"))
        g.hold()
        g.restore()
        texts = {}
        for n in os.listdir(d):
            with open(os.path.join(d, n)) as f:
                texts[n] = f.read()
        print("config.cfg " + repr(texts.get("config.cfg")))
        print("kept %s" % repr(texts.get(os.path.basename(g.kept)) if g.kept else None))
        print("files %d" % len(texts))
        return 0
    if cmd == "cfg-run":
        # cfg-run ENGINE LOGDIR: two engine runs through the guard; the second must not see the first
        eng, logd = a
        cfgp = os.path.join(os.path.dirname(eng), "baseq2", "config.cfg")
        with open(cfgp) as f:
            owner = f.read()
        c = ctx_for(m, eng)
        try:
            c.cfg.hold()
            m.timed_run(c, [("r_rt_shadows", "1")], [], os.path.join(logd, "r1.log"), False)
            m.timed_run(c, [], [], os.path.join(logd, "r2.log"), False)
        finally:
            c.cfg.restore()
        r1, r2 = m.read_log(os.path.join(logd, "r1.log")), m.read_log(os.path.join(logd, "r2.log"))
        print("first run sees the owner's config" if "set owner_cvar" in r1 else "first run clean")
        print("second run sees the first's" if "set r_rt_shadows" in r2 else "second run clean")
        with open(cfgp) as f:
            print("restored" if f.read() == owner and not os.path.lexists(cfgp + ".mfx-saved") else "not restored")
        return 0
    if cmd == "cfg-recover":
        # cfg-recover BINDIR: the owner's file is at config.cfg.mfx-saved after a kill; the next hold puts it back
        d = os.path.join(a[0], "baseq2")
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "config.cfg.mfx-saved"), "w") as f:
            f.write("owner\n")
        with open(os.path.join(d, "config.cfg"), "w") as f:
            f.write("a run's leftovers\n")
        g = m.CfgGuard(os.path.join(a[0], "quake2"))
        g.hold()
        g.restore()
        with open(os.path.join(d, "config.cfg")) as f:
            print("recovered" if f.read() == "owner\n" else "lost")
        return 0
    if cmd == "frames":
        # frames DIR_A DIR_B: frame pairing by number across records of different scenario names
        da, db = a
        os.makedirs(da)
        os.makedirs(db)
        for n in ("000060", "000120", "000180"):
            open(os.path.join(da, "mfxr-world-native-%s.tga" % n), "w").close()
            open(os.path.join(db, "mfxr-world-fx1-s75-%s.tga" % n), "w").close()
        ka, kb = m.frames_by_index(da), m.frames_by_index(db)
        print("paired" if sorted(ka) == sorted(kb) and len(ka) == 3 else "not paired")
        print("order " + " ".join(m.frame_keys({"1200": 1, "180": 1, "60": 1})))
        return 0
    if cmd == "stats":
        lines = ["ref_metal: metal_stats: metalfx temporal: input 640x360, output 1280x720 at 0,0, reset first size, scaler 0.400 ms",
                 "ref_metal: metal_stats: metalfx temporal: input 640x360, output 1280x720 at 0,0, reset none, scaler not timed",
                 "ref_metal: metal_stats: metalfx spatial: input 640x360, output 1280x720 at 0,0, reset none, scaler 0.500 ms"]
        st = m.STATS_RE.findall("\n".join(lines))
        print("matched %d timed %d" % (len(st), len([x for x in st if x[4]])))
        return 0
    if cmd == "probe-opts":
        # probe-opts ENGINE LOGDIR: an option whose probe fails is skipped, the next one still runs
        opts = [{"id": "bad", "set": [("r_metalfx_interp", "0")]}, {"id": "good", "set": [("r_metalfx", "0")]}]
        ok, sk = m.probe_options(ctx_for(m, a[0]), opts, [("bad", "world"), ("good", "world")], a[1])
        print("ok %s skipped %s" % (",".join(sorted(ok)), ";".join("%s:%s" % (s[0], s[2][:13]) for s in sk)))
        return 0
    if cmd == "record-env":
        # record-env SCENDIR BIN: PINS_BIN reaches pins.sh and the scratch scenario goes where pins.sh reads it
        seen = {}

        def fake_sh(cmdl, cwd=None, env=None, timeout=None):
            seen["bin"] = env.get("PINS_BIN", "<unset>")
            seen["there"] = [f for f in os.listdir(a[0]) if f.startswith("mfxr-")]
            return 1, "stub"
        m.sh = fake_sh
        c = ctx_for(m, a[1])
        c.scen_dir = c.pins_scen_dir = a[0]
        c.scratch_cfgs, c.store, c.basedir, c.every, c.seed = [], a[0], "/b", 60, 1
        try:
            m.record(c, "world", [], [("r_metalfx", "1")], "t")
        except RuntimeError:
            pass
        print("PINS_BIN %s scratch %s left %s" % (seen.get("bin"), seen.get("there"), [f for f in os.listdir(a[0]) if f.startswith("mfxr-")]))
        return 0
    if cmd == "clear-scratch":
        d = a[0]
        open(os.path.join(d, "mfxr-a-b.cfg"), "w").close()
        open(os.path.join(d, "keep.cfg"), "w").close()
        m.clear_scratch_cfgs(d)
        print("left " + " ".join(sorted(os.listdir(d))))
        return 0
    if cmd == "read-crop":
        # read-crop BASE: names that are paths, a link at the file, a link at the crops folder, a FIFO
        b = a[0]
        os.makedirs(os.path.join(b, "ha", "crops"))
        os.makedirs(os.path.join(b, "secret"))
        for n in ("holiday__y.png", "real.png"):
            with open(os.path.join(b, "secret", n), "wb") as f:
                f.write(b"SECRET")
        with open(os.path.join(b, "ha", "crops", "world__y.png"), "wb") as f:
            f.write(b"CROP")
        os.symlink(os.path.join(b, "secret", "real.png"), os.path.join(b, "ha", "crops", "world__lnk.png"))
        os.mkfifo(os.path.join(b, "ha", "crops", "world__fifo.png"))
        os.makedirs(os.path.join(b, "hb"))
        os.symlink(os.path.join(b, "secret"), os.path.join(b, "hb", "crops"))
        res = [m.read_crop(os.path.join(b, "ha"), "world", "y"),
               m.read_crop(os.path.join(b, "ha"), "../../secret/holiday", "y"),
               m.read_crop(os.path.join(b, "ha"), "/" + b.lstrip("/") + "/secret/holiday", "y"),
               m.read_crop(os.path.join(b, "ha"), "world", "lnk"),
               m.read_crop(os.path.join(b, "ha"), "world", "fifo"),
               m.read_crop(os.path.join(b, "hb"), "holiday", "y")]
        print(" ".join(repr(r) for r in res))
        return 0
    if cmd == "read-fifo":
        # read-fifo HOSTDIR: a FIFO as results.tsv is refused at once, never waited on
        def on_alarm(sig, frm):
            raise TimeoutError("hung")
        signal.signal(signal.SIGALRM, on_alarm)
        signal.alarm(10)
        try:
            m.load_host(a[0])
            print("loaded")
        except TimeoutError:
            print("hung")
        except ValueError as e:
            print("refused: " + str(e)[-40:])
        return 0
    if cmd == "craft-host":
        # craft-host SRC DST SCENARIO OPTION: a copy of a host whose results.tsv has one more row (a status ok one)
        src, dst, sc, op = a
        shutil.copytree(src, dst)
        rows = m.read_tsv(os.path.join(dst, "results.tsv"), m.RESULT_COLS)
        r = dict(rows[0])
        r["scenario"], r["option"] = sc, op
        m.tsv_write(os.path.join(dst, "results.tsv"), m.RESULT_COLS, [[x[c] for c in m.RESULT_COLS] for x in rows + [r]])
        return 0
    if cmd == "stale-crop":
        # stale-crop SRC DST: a rerun in which fx1-s50 did not run, beside the crop an earlier run left for it
        src, dst = a
        shutil.copytree(src, dst)
        rows = [r for r in m.read_tsv(os.path.join(dst, "results.tsv"), m.RESULT_COLS) if r["option"] == "native"]
        m.tsv_write(os.path.join(dst, "results.tsv"), m.RESULT_COLS, [[x[c] for c in m.RESULT_COLS] for x in rows])
        m.tsv_write(os.path.join(dst, "skipped.tsv"), m.SKIPPED_COLS, [("fx1-s50", "*", "unsupported: rerun")])
        with open(os.path.join(dst, "crops", "world__fx1-s50.png"), "wb") as f:
            f.write(b"STALE-CROP-BYTES")
        return 0
    print("unknown command " + cmd)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
