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
import subprocess
import sys
import time


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
        # a=[cvarlist file]: the defaults a run's console script sets first, and the order of its lines
        text = open(a[0]).read()
        cv = m.parse_cvarlist(text)
        opts = [{"id": "x", "set": [("r_metalfx", "1"), ("r_metalfx_interp", "1")]}, {"id": "y", "set": [("r_rt_denoiser", "1")]}]
        c = ctx_for(m, "/bin/true")
        c.defaults = m.default_cvars(cv, m.parse_noset(text), opts, {"rt": {"r_rt_shadows", "timedemo"}}, m.parse_archived(text))
        args = m.engine_args(c, [("r_rt_shadows", "1")], [("r_rt_denoiser", "1")], [])
        final = {}
        for line in m.run_cfg_text(c, [("r_rt_shadows", "1")], [("r_rt_denoiser", "1")]).split("\n"):
            if line.startswith("set "):
                final[line.split()[1]] = line.split()[2]
        want = {"r_metalfx": "0", "r_metalfx_interp": "0", "r_rt_shadows": "1", "r_rt_denoiser": "1", "r_rt_arch": "2"}
        bad = [k for k in want if final.get(k) != want[k]]
        bad += [k for k in ("r_rt_fixed", "r_metalfx_modes", "timedemo", "r_rt_other") if k in final]
        bad += [k for k in final if k in args]  # a cvar of the script is not also an argument
        print("ok" if not bad else "leak: " + " ".join(bad))
        return 0
    if cmd == "cfg-many":
        # cfg-many BINDIR N: defaults and a scenario of N cvars in all: the engine's argument count (its own name
        # included), the +exec, the +set pairs left on the command line, and what reaches the console script
        d = os.path.join(a[0], "baseq2")
        os.makedirs(d, exist_ok=True)
        n = int(a[1])
        c = ctx_for(m, os.path.join(a[0], "quake2"))
        c.defaults = dict(("r_rt_x%d" % i, "0") for i in range(n // 2))
        sc = [("r_rt_y%d" % i, "1") for i in range(n - n // 2)]
        try:
            args = m.engine_args(c, sc, [("r_metalfx", "2"), ("metal_stats", "1")], ["+demomap", c.demo], write=True)
        except RuntimeError as e:
            print("limit: %s" % e)
            return 0
        sets = [args[i + 1] for i in range(len(args)) if args[i] == "+set"]
        text = open(os.path.join(d, "mfx-run.cfg")).read()
        full = (all(("set r_rt_x%d 0\n" % i) in text for i in range(n // 2)) and
                all(("set r_rt_y%d 1\n" % i) in text for i in range(n - n // 2)) and
                "set r_metalfx 2\n" in text and "set metal_stats 1\n" in text)
        print("args %d" % (len(args) + 1))
        print("exec %d" % args.count("+exec"))
        print("script %s" % ("complete" if full else "short"))
        print("pairs %s" % ("leaked" if any(k.startswith("r_rt_") for k in sets) else "fixed only"))
        # more cvars than the engine has arguments: the same count
        c.defaults = dict(("r_rt_x%d" % i, "0") for i in range(200))
        print("wide %d" % (len(m.engine_args(c, sc, [], ["+demomap", c.demo])) + 1))
        # a command line over the limit is still an error
        try:
            m.engine_args(c, [], [], ["+wait"] * 130)
            print("no limit")
        except RuntimeError:
            print("limit")
        return 0
    if cmd == "cfg-unsafe":
        # cfg-unsafe BINDIR: a value or name that cannot be written into a console line is refused with its cvar
        # name, and a plain one is not
        d = os.path.join(a[0], "baseq2")
        os.makedirs(d, exist_ok=True)
        c = ctx_for(m, os.path.join(a[0], "quake2"))
        for what, sc in (("quote", [("r_rt_q", 'a"b')]), ("semicolon", [("r_rt_s", "1;quit")]), ("newline", [("r_rt_n", "1\nquit")]),
                         ("space", [("r_rt_p", "1 2")]), ("empty", [("r_rt_e", "")]), ("name", [("r_rt_q;quit", "1")]),
                         ("tailname", [("r_rt_t\n", "1")]), ("tailvalue", [("r_rt_t", "1\n")]),
                         ("fixed", [("vid_renderer", "gl1")]), ("width", [("r_customwidth", "1")]), ("plain", [("r_rt_ok", "-0.5")])):
            try:
                m.engine_args(c, sc, [], [], write=True)
                print("%s accepted" % what)
            except m.Refuse as e:
                print("%s refused: %s" % (what, e))
        return 0
    if cmd == "cfg-link":
        # cfg-link BINDIR: a hard link at the console script's path is replaced by the new script, and what it
        # links to is left alone
        d = os.path.join(a[0], "baseq2")
        os.makedirs(d, exist_ok=True)
        victim = os.path.join(a[0], "victim")
        with open(victim, "w") as f:
            f.write("owner data\n")
        cfg = os.path.join(d, "mfx-run.cfg")
        if os.path.lexists(cfg):
            os.unlink(cfg)
        os.link(victim, cfg)
        c = ctx_for(m, os.path.join(a[0], "quake2"))
        m.engine_args(c, [("r_rt_ok", "1")], [], [], write=True)
        print("victim " + ("intact" if open(victim).read() == "owner data\n" else "overwritten"))
        print("script " + ("written" if "set r_rt_ok 1\n" in open(cfg).read() else "missing"))
        return 0
    if cmd == "cfg-restore-order":
        # cfg-restore-order BINDIR: the script cannot be removed (a directory is in its place): the owner's config
        # is back all the same
        d = os.path.join(a[0], "baseq2")
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "config.cfg"), "w") as f:
            f.write("set owner_cvar \"7\"\n")
        g = m.CfgGuard(os.path.join(a[0], "quake2"))
        g.hold()
        os.makedirs(os.path.join(d, "mfx-run.cfg"))
        try:
            g.restore()
            print("restore returned")
        except Exception as e:
            print("restore raised %s" % type(e).__name__)
        back = os.path.join(d, "config.cfg")
        print("owner config " + ("back" if os.path.isfile(back) and "owner_cvar" in open(back).read() else "not back"))
        print("saved " + ("left" if os.path.lexists(back + ".mfx-saved") else "gone"))
        return 0
    if cmd == "cfg-script-gone":
        # cfg-script-gone BINDIR: the run's console script goes when the held config is restored, and one a killed
        # run left goes at the next hold
        d = os.path.join(a[0], "baseq2")
        os.makedirs(d, exist_ok=True)
        cfg = os.path.join(d, "mfx-run.cfg")
        with open(cfg, "w") as f:
            f.write("set stale 1\n")
        g = m.CfgGuard(os.path.join(a[0], "quake2"))
        g.hold()
        print("after hold " + ("gone" if not os.path.exists(cfg) else "left"))
        with open(cfg, "w") as f:
            f.write("set run 1\n")
        g.restore()
        print("after restore " + ("gone" if not os.path.exists(cfg) else "left"))
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
    if cmd == "check-run":
        # check-run RDIR OPTION SCENARIO: what an end-to-end run wrote for the option's quality and crops
        rd, op, sc = a
        rows = dict(((r["option"], r["scenario"]), r) for r in m.read_tsv(os.path.join(rd, "results.tsv"), m.RESULT_COLS))
        r = rows.get((op, sc))
        if r is None:
            print("no row")
            return 0
        print("status " + r["status"])
        print("ssim_less_than_one " + ("yes" if r["ssim_mean"] and float(r["ssim_mean"]) < 1 else "no"))
        with open(os.path.join(rd, "crops", "%s__%s.png" % (sc, op)), "rb") as f:
            x = f.read()
        with open(os.path.join(rd, "crops", "%s__native.png" % sc), "rb") as f:
            n = f.read()
        print("crop_differs " + ("yes" if x != n else "no"))
        with open(os.path.join(rd, "crops", "%s.txt" % sc)) as f:
            print("index " + " ".join(f.read().split()))
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
        m.sh_group = fake_sh
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
    if cmd == "stop-wait":
        # the seconds sh_group gives a stopped command between TERM and KILL, from BENCH_STOP_GRACE as the
        # host lock reads it: unset, 1, not whole seconds, 30, 08 and 011 (a leading zero is not whole seconds), 999999999, then 10 and 20 digits (more than 9 digits is not whole seconds, as the core reads it); then the same under the generator's own hold
        envs = ({}, {"BENCH_STOP_GRACE": "1"}, {"BENCH_STOP_GRACE": "x"}, {"BENCH_STOP_GRACE": "30"}, {"BENCH_STOP_GRACE": "08"}, {"BENCH_STOP_GRACE": "011"}, {"BENCH_STOP_GRACE": "999999999"}, {"BENCH_STOP_GRACE": "1234567890"}, {"BENCH_STOP_GRACE": "99999999999999999999"})
        free = " ".join(str(m.lock_stop_wait(e)) for e in envs)
        m.LOCK_HELD = True
        print(free + " / " + " ".join(str(m.lock_stop_wait(e)) for e in envs))
        return 0
    if cmd == "group-timeout":
        # group-timeout TREE SECONDS: sh_group on TREE/scripts/pins.sh record past its timeout; the
        # TimeoutExpired comes back once the command's group is stopped
        t0 = time.time()
        try:
            m.sh_group([os.path.join(a[0], "scripts", "pins.sh"), "record", "metal", "x"], cwd=a[0], timeout=float(a[1]))
        except subprocess.TimeoutExpired:
            print("timed out, back after %.1f s" % (time.time() - t0))
            return 0
        print("no timeout")
        return 1
    print("unknown command " + cmd)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
