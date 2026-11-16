# TASK154: the quake2 bench adapter and the first cases

The bench harness's core (TASK153, [`docs/plans/bench-harness.md`](../plans/bench-harness.md))
knows no game. This card adds the quake2 adapter, `scripts/bench/adapters/quake2.sh`, and 32
cases in `scripts/bench/cases/`, one per measured cell of FIX51, TASK131, TASK57 and TASK111, so
that each of their timings can be measured again with one command. The commands and the cases
are on the user page: [Timings with bench.sh](../../doc/100_renderpins.md#timings-with-benchsh).
The shared runbook's method still holds for the frames:
[how a run works](../plans/metal-port-harness.md#how-a-run-works) and the
[pin arguments](../plans/metal-port-harness.md#pin-arguments).

## The adapter

- **Preflight.** The case's settings are checked word by word (a fixed list; an unknown word, a
  bad value, a word given twice, a cvar the adapter sets itself or one that picks a directory are
  refused), then its `requires` against the host: `physical-gpu`, `raytracing`, `metalfx` and
  `display-hz>=N` come from `scripts/bench/adapters/quake2-caps.m`, a probe the preflight builds
  with the host's `cc` that asks Metal and the display what `ref_metal` asks (the default
  device, `supportsRaytracing`, MetalFX's spatial scaler `supportsDevice`, the main display's
  refresh rate), and from `kern.hv_vmm_present`. A capability the host lacks refuses the case
  with its reason. Then `MTL_DEBUG_LAYER` must be unset (a timing is made without the validation
  layer), the game data must hold `baseq2/`, the screen must be unlocked (`ioreg -n Root -d1`
  holds no `CGSSessionScreenIsLocked`) and no other `quake2` may run (`pgrep -x quake2`). The
  last two are checked again before every run.
- **Build.** `git archive` of the commit into a build cache outside the repository, one export per
  inputs id, then `make -j<ncpu> <build_flags> <targets>` in it, bounded at an hour. The
  measurement gets a copy of `release/`. `scripts/pins.sh` does not export or build a commit (it
  runs this checkout's build or `PINS_BIN`), so there was nothing to reuse. On the M6 a build from
  scratch, the vendored SDL3 included, took 8 s; a cached one is `make`'s no-op.
- **Run.** `-datadir <game data> -portable`, the pin scenario's `set` lines when the case names
  one (read from the measured commit), then the adapter's own (`vid_renderer`, the size,
  `vid_fullscreen`, `vid_highdpiaware`, `r_vsync`, `s_initsound 0`, `timedemo`, `vid_maxfps`),
  the case's `cvar.*` words, `pins_dir`, `pins_record` and `pins_seed` for a seeded case, and
  `+demomap`. The engine gets TERM `seconds=` after launch and quits at its next frame, so its
  teardown prints the totals; the core's `bench_bounded` stops the whole group at the case's wall
  time. `config.cfg` beside the binary is removed before every run, so no run inherits the last
  one's cvars. A seeded run writes its trace into the run's scratch directory, removed after the
  run; no frame is captured. The log must show the case's renderer as the last `ref_` library
  loaded (and the shim's `GL_RENDERER` for `gl1-metal`), and `Metal supportsRaytracing: yes` or
  `MetalFX supportsDevice: spatial yes` when the case requires them, else the run fails.
- **Metrics.** The timedemo line (`fps`), the totals at the renderer's close (`frames`,
  `no_drawable`, `presented`, `skipped`, `failed`, from `metal_stats` or the shim's `mgl:` line),
  the MetalFX scaler's GPU time per timed view (`scaler_ms`, its p90 and count), and the
  `r_rt_stats 2` pass times (`rts_ms`, `rts_traced_ms`, `rtao_ms`). The window is in frames:
  `skip=` drops the first lines of a kind, `after_frame=` keeps the pass lines of frames numbered
  above it. Each line is found by its prefix anywhere on a log line, since console text does not
  always end in a newline before the next print (the first scaler line follows the precache
  text on the same line).
- **Inputs.** `Makefile`, `src`, `third_party`, every top-level `*.make`, and the case's pin
  scenario file, so a scenario edit gives new inputs.

**Not printed today, not measured.** FIX51's *presented a second* over a window of seconds (from
1 s after the window's first frame) and its count of frames that found *every present slot held*
came from a probe that was never committed. The engine prints neither a time per frame nor that
count, so the `present-*` cases read the timedemo's rendered rate and the totals over the whole
run instead. Measuring the rest needs a stats line in the engine, a card of its own; no engine
code changed here.

## One file per cell

A case is one measurement: its name keys the store and the lock label, its hash covers its
whole content, and `bench.sh` takes no parameter that picks a cell. So the size, the display
sync, the mode and the scale cannot be keys of one family file without a change to the core,
and each cell has its own file: 12 `present-*` (two renderers, three sizes, `r_vsync` 0 and 1),
4 `rt-shadows-gpu-*` and 4 `rt-ao-gpu-*` (two sizes, with and without entities), and 12
`metalfx-scaler-*` (two outputs, the spatial and temporal scalers, three scales). TASK111's
mode 3 is left out: without a ray-traced effect it runs as mode 2 and measures the same pass.

## Reproductions

**Host and store.** `Apple M6, Apple M6 GPU, macOS 27.2.1, physical`, token `h-e159846095d1`
(`bench.sh host`), 2026-11-16 from 06:33 to 06:53 UTC. The baselines are in this card's store,
`<artifacts>/TASK154/bench-store/` (`BENCH_STORE`), the builds in
`<artifacts>/TASK154/bench-builds/`, the engine logs in `<artifacts>/TASK154/logs/`
(`BENCH_Q2_LOGS`) and the lock state in `<artifacts>/TASK154/bench-state/`, all outside the
repository. Every command ran inside the host's `heavy.sh` (its timing lock, so nothing else
heavy ran beside it), with `bench.sh`'s own lock taken inside it; the two locks do not yet
exclude each other. Three runs a case after one warm-up run. A result is cited as
`<host class>, <case>, <inputs>`.

| Family | Case at commit (inputs) | Runs (median) | The card's stated range | Load before, after |
|---|---|---|---|---|
| presents | `present-metal-1920x1080-vsync0` at `d7cef009` (`i-3456a9224df4`) | 7252.6 to 7655.6 fps (7329.8) | FIX51 round 3, after: 6439.3 to 6754.9 | 3.19, 3.25 |
| presents | `present-gl1-metal-1920x1080-vsync0` at `d7cef009` (`i-0b81d221c522`) | 3645.5 to 5103.7 fps (4175.8) | FIX51 round 3, after: 4532.9 to 4956.8 | 2.23, 5.86 |
| shadow pass | `rt-shadows-gpu-1920x1080` at `96b9c661` (`i-8dbe10b9a0c0`) | 1.677 to 1.682 ms (1.682) | TASK131, head: 1.649 to 1.677 (1.662) | 3.06, 2.31 |
| AO pass | `rt-ao-gpu-1920x1080` at `27cdcf83` (`i-ae5c4fb6991c`) | 3.655 to 3.660 ms (3.656) | TASK57 review round 1, head: 3.666 to 3.704 (3.678) | 2.03, 3.02 |
| MetalFX scaler | `metalfx-scaler-1920x1080-spatial-0.5` at `c9ecf7d7` (`i-95c413d78233`) | 0.741 ms in all three (0.741) | TASK111: 0.80 to 0.84 (0.83), with display sync | 2.90, 3.19 |

(The one-minute load, `sysctl -n vm.loadavg`, at the command's start and end.)

- **Presents, `ref_metal`.** 7 to 13 percent above FIX51's range. FIX51's rates are all from its
  probe builds ("every rate is from the probe builds"): the probe printed a time for every frame
  and every present, a cost of its own at 0.14 ms a frame, and this build of `d7cef009` has no
  probe. The demo's 689 frames are those of FIX51's validation runs. One drift run of the stored
  baseline afterwards read 7829.5, inside it (`drift=inside`, bounds 6519.62 to 8388.58).
  Presented over the whole 8 s run: 3969 to 4015 frames, of 107,927 to 113,145 rendered.
- **Presents, the shim.** The range overlaps FIX51's and is wider: at about 4,000 frames a second
  the 689 frames take 0.1 to 0.2 s, so one stall moves a run's rate by a quarter, and the load
  rose to 5.86 by the batch's end. The rendered rate of a timedemo this short is a coarse number
  at either build; the steady own-pace rate needs the per-frame time (above).
- **Shadow pass.** Inside: the run range touches the top of TASK131's, and the median is 0.02 ms
  (1.2 percent) above its median. The window is TASK55's warm frames, the frames numbered past
  100: 311 a run, of which 283 traced a ray, TASK131's own count of 283. The p90, 2.147 to 2.195
  ms, is just above TASK131's 2.068 to 2.137. A first batch counted the lines past the first 100
  instead (06:37 to 06:39, 229 lines a run, 1.721 to 1.733 ms): the pass prints a line only on a
  frame it runs, so that window starts later in the demo and holds other frames. The case was
  changed to `after_frame=100` and measured again; the first batch stays in the store under the
  old case hash.
- **AO pass.** 0.011 ms (0.3 percent) under TASK57's range, inside the case's drift tolerance,
  over the same 587 warm frames a run. The p90 is lower, 4.183 to 4.399 ms against 5.263 to
  5.353: TASK57's runs were pin records, which capture every 30th frame (a screenshot readback)
  while the host's other lanes ran; these capture nothing on a quiet host.
- **MetalFX scaler.** With TASK111's method, display sync on at 60 frames a second, the cell read
  1.454 to 1.587 ms (median 1.502, 06:45 to 06:47, load 3.22 to 3.97), about twice TASK111's
  and 9 percent apart between runs. With display sync off and `vid_maxfps 300`, the GPU busy at
  about 300 frames a second, the same cell read 0.741 ms in all three runs, in two batches (a
  scratch copy of the case at 06:47, then the committed case at 06:49). So the scaler's GPU time
  follows the GPU's clock: idle between 60 Hz frames on a quiet host it clocks down and a pass
  takes longer; kept busy it repeats to the microsecond. TASK111 measured at 60 Hz on a host that
  other lanes kept busy (load 2.1 to 7.0), which sits between the two. The cases take display
  sync off so that they repeat; TASK111's 0.80 to 0.84 is not reached by either setting on the
  quiet host, and its relative figures (the cost falls with the scale, the temporal scaler costs
  more at 0.75) are to be read again from these cases, not from its absolute ones.

**A host without the capability.** With a capability file of a virtual machine
(`BENCH_Q2_CAPS`: `physical-gpu=no`, `raytracing=no`), `bench.sh baseline put
rt-shadows-gpu-5120x2880 96b9c661` printed

```
bench: quake2: the case requires physical-gpu, and this host has no
bench: quake2: the case requires raytracing, and this host has no
bench: the adapter refused the run in its preflight (status 1)
```

and exited 3 before any build. `tests/check-bench-quake2.sh` makes the same refusal in the gate
with a stubbed capability file and stand-ins for `ioreg` and `pgrep`.

**Every case measured.** All 32 cases have a stored baseline, on the same host class (token
`h-e159846095d1`, an Apple M6 with a physical GPU, a 60 Hz display), each run through `bench.sh
baseline put` inside `heavy.sh`, one lock hold a case, three runs a case after one warm-up, on
2026-11-16 from 07:01 to 07:31 UTC (the 27 cases added after the first five). Each family used the
commit of its reproduction above: `d7cef009` for the presents, `96b9c661` for the shadow pass,
`27cdcf83` for the AO pass and `c9ecf7d7` for the scaler. No case failed or refused, and none
needed a fix. (Two cases keep an older hash directory beside the one listed, the first
batches of the shadow and scaler reproductions above.) Paths are relative to `<artifacts>`. The metric is the one the case names (`fps`,
`rts_ms`, `rtao_ms`, `scaler_ms`); a result is cited as `<host class>, <case>, <inputs>`.

| Case | Commit | Range (median) | Stored result |
|---|---|---|---|
| `metalfx-scaler-1920x1080-spatial-0.34` | `c9ecf7d7` | 0.718 to 0.718 ms (0.718) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-95c413d78233/metalfx-scaler-1920x1080-spatial-0.34/c-7326a4079281` |
| `metalfx-scaler-1920x1080-spatial-0.5` | `c9ecf7d7` | 0.741 to 0.741 ms (0.741) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-95c413d78233/metalfx-scaler-1920x1080-spatial-0.5/c-83e3231051bd bench-store/h-e159846095d1/i-95c413d78233/metalfx-scaler-1920x1080-spatial-0.5/c-976a3154a66d` |
| `metalfx-scaler-1920x1080-spatial-0.75` | `c9ecf7d7` | 0.797 to 0.797 ms (0.797) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-95c413d78233/metalfx-scaler-1920x1080-spatial-0.75/c-263f767f8a2a` |
| `metalfx-scaler-1920x1080-temporal-0.34` | `c9ecf7d7` | 0.704 to 0.706 ms (0.704) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-95c413d78233/metalfx-scaler-1920x1080-temporal-0.34/c-b1a8640a15eb` |
| `metalfx-scaler-1920x1080-temporal-0.5` | `c9ecf7d7` | 0.689 to 0.691 ms (0.691) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-95c413d78233/metalfx-scaler-1920x1080-temporal-0.5/c-bbd867813852` |
| `metalfx-scaler-1920x1080-temporal-0.75` | `c9ecf7d7` | 0.787 to 0.79 ms (0.788) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-95c413d78233/metalfx-scaler-1920x1080-temporal-0.75/c-f2c10397d565` |
| `metalfx-scaler-5120x2880-spatial-0.34` | `c9ecf7d7` | 2.96 to 3.01 ms (3.001) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-95c413d78233/metalfx-scaler-5120x2880-spatial-0.34/c-d23ee4b2dcab` |
| `metalfx-scaler-5120x2880-spatial-0.5` | `c9ecf7d7` | 3.02 to 3.045 ms (3.044) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-95c413d78233/metalfx-scaler-5120x2880-spatial-0.5/c-9bd6cfd82b8b` |
| `metalfx-scaler-5120x2880-spatial-0.75` | `c9ecf7d7` | 3.126 to 3.156 ms (3.139) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-95c413d78233/metalfx-scaler-5120x2880-spatial-0.75/c-fe639134c0ed` |
| `metalfx-scaler-5120x2880-temporal-0.34` | `c9ecf7d7` | 1.898 to 1.927 ms (1.912) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-95c413d78233/metalfx-scaler-5120x2880-temporal-0.34/c-c6c6767ddca2` |
| `metalfx-scaler-5120x2880-temporal-0.5` | `c9ecf7d7` | 2.752 to 2.758 ms (2.753) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-95c413d78233/metalfx-scaler-5120x2880-temporal-0.5/c-45d17c55ac70` |
| `metalfx-scaler-5120x2880-temporal-0.75` | `c9ecf7d7` | 3.956 to 4.022 ms (3.986) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-95c413d78233/metalfx-scaler-5120x2880-temporal-0.75/c-0cd5c2fce5cc` |
| `present-gl1-metal-1920x1080-vsync0` | `d7cef009` | 3645.5 to 5103.7 fps (4175.8) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-0b81d221c522/present-gl1-metal-1920x1080-vsync0/c-a6b53a2b6f59` |
| `present-gl1-metal-1920x1080-vsync1` | `d7cef009` | 60.2 to 60.2 fps (60.2) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-0b81d221c522/present-gl1-metal-1920x1080-vsync1/c-7941888ce5d6` |
| `present-gl1-metal-5120x2880-vsync0` | `d7cef009` | 619 to 628.6 fps (624.1) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-0b81d221c522/present-gl1-metal-5120x2880-vsync0/c-b00551d2c46c` |
| `present-gl1-metal-5120x2880-vsync1` | `d7cef009` | 60.2 to 60.2 fps (60.2) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-0b81d221c522/present-gl1-metal-5120x2880-vsync1/c-7c84a8f65ccb` |
| `present-gl1-metal-640x480-vsync0` | `d7cef009` | 6043.9 to 6207.2 fps (6097.3) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-0b81d221c522/present-gl1-metal-640x480-vsync0/c-f6d80ce46cf9` |
| `present-gl1-metal-640x480-vsync1` | `d7cef009` | 60.2 to 60.2 fps (60.2) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-0b81d221c522/present-gl1-metal-640x480-vsync1/c-d5a436784b28` |
| `present-metal-1920x1080-vsync0` | `d7cef009` | 7252.6 to 7655.6 fps (7329.8) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-3456a9224df4/present-metal-1920x1080-vsync0/c-b866b78c817b` |
| `present-metal-1920x1080-vsync1` | `d7cef009` | 60 to 60 fps (60) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-3456a9224df4/present-metal-1920x1080-vsync1/c-6692016b2b4c` |
| `present-metal-5120x2880-vsync0` | `d7cef009` | 1794.3 to 1837.3 fps (1813.2) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-3456a9224df4/present-metal-5120x2880-vsync0/c-dfa98b265aee` |
| `present-metal-5120x2880-vsync1` | `d7cef009` | 60 to 60 fps (60) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-3456a9224df4/present-metal-5120x2880-vsync1/c-cad4c6597e35` |
| `present-metal-640x480-vsync0` | `d7cef009` | 7329.8 to 8402.4 fps (7829.5) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-3456a9224df4/present-metal-640x480-vsync0/c-de4aff6a1c52` |
| `present-metal-640x480-vsync1` | `d7cef009` | 60 to 60 fps (60) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-3456a9224df4/present-metal-640x480-vsync1/c-eb279c0a5cad` |
| `rt-ao-gpu-1920x1080-entities` | `27cdcf83` | 3.695 to 3.702 ms (3.699) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-0160a5665bf6/rt-ao-gpu-1920x1080-entities/c-c7a4ae203cc1` |
| `rt-ao-gpu-1920x1080` | `27cdcf83` | 3.655 to 3.66 ms (3.656) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-ae5c4fb6991c/rt-ao-gpu-1920x1080/c-bd69c7347b0b` |
| `rt-ao-gpu-5120x2880-entities` | `27cdcf83` | 9.823 to 9.965 ms (9.908) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-0160a5665bf6/rt-ao-gpu-5120x2880-entities/c-7dd83ea53624` |
| `rt-ao-gpu-5120x2880` | `27cdcf83` | 9.8315 to 9.9435 ms (9.8405) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-ae5c4fb6991c/rt-ao-gpu-5120x2880/c-182347510b0e` |
| `rt-shadows-gpu-1920x1080-entities` | `96b9c661` | 1.765 to 1.777 ms (1.776) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-17fea350c5a3/rt-shadows-gpu-1920x1080-entities/c-29053b54f775` |
| `rt-shadows-gpu-1920x1080` | `96b9c661` | 1.677 to 1.682 ms (1.682) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-8dbe10b9a0c0/rt-shadows-gpu-1920x1080/c-58bacfe95325 bench-store/h-e159846095d1/i-8dbe10b9a0c0/rt-shadows-gpu-1920x1080/c-a1e761cde258` |
| `rt-shadows-gpu-5120x2880-entities` | `96b9c661` | 6.506 to 6.978 ms (6.924) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-17fea350c5a3/rt-shadows-gpu-5120x2880-entities/c-afcdadd3f958` |
| `rt-shadows-gpu-5120x2880` | `96b9c661` | 6.776 to 6.849 ms (6.822) | `<artifacts>/TASK154/bench-store/h-e159846095d1/i-8dbe10b9a0c0/rt-shadows-gpu-5120x2880/c-907568ecc2ae` |

Reading the numbers:

- **`vsync1` presents** read 60 frames a second (the gl1 shim 60.2) at every size: with display
  sync on the timedemo is paced by the display, so these cells record that the renderer keeps up
  at 60 Hz (no drawable-less frames, every frame presented) and cannot show a regression in its
  cost. The `vsync0` cells carry the cost. Their ordering is the plausible one: the rate falls
  with the pixel count (7,830, 7,330 and 1,813 for `ref_metal`; 6,097, 4,176 and 624 for the
  shim), and `ref_metal` is ahead of the shim at every size.
- **5120x2880 `ref_metal` and shim presents** have many frames without a drawable (3,710 of
  6,956 for the shim, 9,985 of 13,776 for `ref_metal`): the rendered rate there counts frames
  the display never received. The presented count over the run is the other half
  of the cell.
- **Ray-traced cases.** The 5120x2880 shadow pass costs about four times the 1920x1080 one
  (6.8 against 1.7 ms) and the AO pass about 2.7 times (9.8 against 3.7 ms). The entities scenes
  cost a little more (shadows 1.78 and 6.92 ms, AO 3.70 and 9.91 ms). The widest spread is
  `rt-shadows-gpu-5120x2880-entities`, 6.506 to 6.978 ms, 7 percent between runs (the median is
  near the top of it).
- **Scaler cases.** At 1920x1080 the cost is flat near 0.7 to 0.8 ms and falls a little with the
  scale; at 5120x2880 the spatial scaler costs 3.0 to 3.1 ms at any scale, and the temporal
  scaler costs more at 0.75 (3.99 ms) and less at 0.5 and 0.34 (2.75 and 1.91 ms), the shape
  TASK111 reported (the cost falls with the scale, the temporal scaler costs more at 0.75),
  at a different absolute level, as the MetalFX paragraph above explains.

## Checks

`tests/check-bench-quake2.sh` (new): every quake2 case loads and validates, the settings and
preflight refusals with their reasons, the refusal through `bench.sh` exiting 3 before a build,
the run against a stand-in engine (its argument list in order, TERM after `seconds=`, the saved
configuration removed, the scratch directory removed, a wrong renderer, a missing support line
and a failing exit refused), the metrics of a synthetic log, the engine's own format strings in
`src/`, the inputs paths, and one mutant (the requires check dropped) that must fail.
