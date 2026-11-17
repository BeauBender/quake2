# The presented rate over a window, printed by `ref_metal` and the shim (TASK161)

[FIX51](FIX51.md#round-4-measurements) judged its present path by the
frames a second that were presented over a steady window of seconds, and by
the frames that found every present slot held. Its probe builds printed
those; the engine did not, so the `present-*` cases of
[TASK154](TASK154.md) could time only the rendered rate (the timedemo's
`fps`) and the totals at the renderer's close. This card makes the engine
print them. The shared runbook's method applies:
[how a run works](../plans/metal-port-harness.md#how-a-run-works).

## Design

**The line, and the cvar that asks for it.** With `metal_stats` not 0,
each renderer prints one more line at its close, after the existing
totals line:

```
ref_metal: metal_rate: <r> presented a second over <s> s from <w> s, <p> presented, <h> with every present slot held, <n> without a drawable
mgl: rate: <r> presented a second over <s> s from <w> s, <p> presented, <h> with every present slot held, <n> without a drawable
```

`<r>` is the presented frames a second over the window, `<s>` the window's
length in seconds, `<w>` the seconds after the first frame at which it
began, `<p>` the frames presented in it. `<h>` and `<n>` count over the
whole run, like the totals line. With the cvar `0`, no clock is read, no sample is taken and no rate line is
printed; what still runs is the two held counters (`rmb_counts.present_held`,
`count_held`), the window's reset and the two `Cvar_Get` calls, none of which
waits, presents or draws, so no frame changes. The shim does not read `metal_stats` today
(its `mgl:` totals line is always printed); it reads it once, when its
context is made, for this line only.

**The window's start.** A new cvar, `metal_rate_start` (seconds, default
`1`, FIX51's value), is the time from the first frame ended to the
window's first sample: the first frame that ends at least that long after
the first frame ended begins the window, and the last frame ended before
the close ends it. A run shorter than the start has no window and prints
`0.0` over `0.00 s`. It is read once at the renderer's start (`RI_Init`, and the shim's context
creation), so a later `set` does not move the window or its `<w>` field.

**What is counted.** Presented is `frames - no_drawable`, as the adapter
already derives it; the window holds the difference between the two counts
at its last and its first sample, over the time between them.

- *Every present slot held* (`<h>`): a frame with display sync off whose
  `rmb_present_take` (ref_metal, `rmb_device.m`) or `mgl_present_take` (the
  shim, `mgl_layer.m`) found no free present slot (all `RMB_PRESENTS` or
  `MGL_PRESENTS` held by frames or presents not yet complete). The frame is
  then drawn into its own target and not presented. A take that returns
  nothing for another reason (display sync on, a target that cannot be
  made) is not counted here.
- *No drawable* (`<n>`): the counter the totals line already prints
  (`no_drawable`): `rmb_counts.no_drawable` in ref_metal, `count_no_drawable`
  in the shim. It includes the held frames: `<h>` is a subset of `<n>`, so the two
  columns are not added.
- *A frame skipped* (a begin refused, nothing drawn or presented) counts in
  neither total and is not a sample, in both renderers: ref_metal takes a
  sample only when its frames count moved, the shim returns before its own,
  so both windows end on the last frame really ended.

The arithmetic (the window, the rate, the line's text) is one header-only
file shared by both renderers, `src/client/refresh/metal/common/mtl_rate.h`,
host-tested in `tests/metal/rm_host_test.c`. The clock is
`clock_gettime_nsec_np(CLOCK_UPTIME_RAW)`, read at each frame's end only
while `metal_stats` is on. ref_metal samples in `RI_EndFrame` (`rm_main.c`)
after `rmb_frame_end`, from `rmb_stats`; the shim in `mgl_layer_present`.

**The adapter.** `scripts/bench/adapters/quake2.sh` reads the line (either
prefix) and reports `metric.presented_rate` (the rate), `metric.rate_window_s`,
`metric.rate_presented`, and `metric.held` (frames with every slot held),
each when the line is in the log and its window is longer than 0 s. The
`present-*` cases get `presented_rate` as a metric; the `gl1-metal`
`vsync0` cases make it the headline `metric=` (the one `drift` and
`compare` judge) and keep `fps` as a secondary metric, because FIX68's cap
makes that shim render fewer frames nobody sees (a
rendered-rate drop that is not a presented-rate drop is not a regression).

**A new headline needs a new baseline.** A stored baseline holds no
presented rate, so a case whose headline changes has none to compare with.
Recording one needs timing, which needs an unlocked screen. See
[Evidence](#evidence).

## Evidence

The first four checks below ran with the screen locked (`CGSSessionScreenIsLocked` 1), so only what a
locked screen allows ([pins are byte-identical locked](../plans/metal-port-harness.md#how-a-run-works),
the line's text) was done. Load average before the first run: 2.91.

**With `metal_stats` off, no frame changes.** Live pin records of the
`world` scenario from this branch's build (`d1c6e507`), compared at
`--threshold 0 --bad-threshold 0` against a stored record of the same
renderer: `metal` against `7d4d3f8edfd8-2420249005-r11`, `gl1-metal`
against `ec11574ccde4-b914420486-393463212`. Both: 23 frames, 0 failed,
worst difference 0, worst bad fraction 0.

**With it on, the line prints** (a 9 s timedemo of demo1.dm2, 1280x720
windowed, `r_vsync 0`, locked screen, so the windows were hidden and
few frames were presented; the counts say nothing about the present path):

```
ref_metal: metal_rate: 120.0 presented a second over 7.91 s from 1.00 s, 950 presented, 0 with every present slot held, 144077 without a drawable
mgl: rate: 120.1 presented a second over 7.92 s from 1.00 s, 951 presented, 0 with every present slot held, 87840 without a drawable
```

**Timing, screen unlocked.** The screen was unlocked
(`CGSSessionScreenIsLocked` 0), checked before and after each batch; no
batch ended locked. Two batches, each one lock hold: a 4-minute full-load
pre-warm (23 back-to-back timedemo runs of demo1.dm2 at 1920x1080 windowed,
`r_vsync 0`, the renderer of the batch, from a release build of this
branch's head `e6b55eae`), then the `bench.sh baseline put` runs
(1 warmup and 3 runs a case, builds from the bench cache, 8 s a run, metal_stats 1). No
thermal or performance warning in any snapshot (`pmset -g therm`), low-power
mode off, no swap, memory free 83 percent. Load average at the starts and
ends of the `put`s: 3.7 to 5.5 (a batch's first read, before the pre-warm,
2.5 and 3.2). Frequencies are from the no-root probe of TASK166
(`freq.c`, built into a scratch directory, not part of this branch), weighted
MHz of the active time and the share of it at the top state, over each
`put` (its 4 runs, the build and the gaps between runs included, so the
GPU's figures are lower than a run's own; the pre-warm's are the
steady-load figures):

| Window | P cores (top 4788 MHz) | E cores (top 2940) | GPU (top 1735) |
|---|---|---|---|
| Pre-warm, metal | 4639 MHz, 55.5 percent | 1154, 5.9 | 1690, 86.8 |
| Pre-warm, gl1-metal | 4650, 62.3 | 1340, 14.7 | 1615, 45.2 |
| `put` present-metal-1920x1080 | 4483, 24.6 | 1551, 25.6 | 1657, 79.3 |
| `put` FIX51-matched scratch case | 4717, 72.6 | 1187, 6.2 | 502, 0 (idle in the window) |
| `put` present-gl1-metal-640x480 | 4686, 70.3 | 1921, 37.3 | 1080, 43.2 |
| `put` present-gl1-metal-1920x1080 | 4673, 56.6 | 1496, 23.1 | 1548, 10.7 |
| `put` present-gl1-metal-5120x2880 | 4046, 45.5 | 1545, 21.3 | 1662, 77.4 |

The GPU figure of the FIX51-matched row (502 MHz, 0 percent at the top) is
the probe reading the lowest state with the GPU active 90.7 percent of the
window; it is recorded as read and not explained (the same cell as the row
above it, only the pace differs).

**Deviations from the protocol, stated.** This branch's harness has no
built-in pre-warm or per-run snapshot (TASK166 is not merged): the snapshot
and the frequency record are taken before and after each `put` (its four
runs), not before and after each run. The first snapshot after each
pre-warm read a GPU utilization of 75 percent (the utilization lags the end
of the pre-warm; the next `put`'s snapshot, one second after a `put` ended,
read 0 to 11 percent), so the first `put` of each batch started with that lag in its
"before" figure, not with a busy GPU; the batches were not repeated. Other
processes in the snapshots: WindowServer (up to 57 percent at the lagged
reads), the owner's terminal and an agent, `searchpartyd` once (57 percent),
`mediaanalysisd` once (9 percent) and a browser content process;
no `backupd` and no `mds*` in a top 5. Builds were not alternated (one build
a case: no before and after).

**FIX51's cell, `ref_metal` at 1920x1080 windowed** (round 4: 404.9 to
409.6 presented a second, rendered 995.1 to 995.2, load 3.1 to 3.8), two
ways, stored in this card's own store (not in the M6 store):

| | Presented a second | Window | Rendered a second | Frames with every slot held | Load (before; after) |
|---|---|---|---|---|---|
| `present-metal-1920x1080-vsync0` as it is (`pace=timedemo`, stock `vid_maxfps`) | 464.8 median, 463.1 to 465.1 | 7.22 to 7.25 s | about 13,800 (110,455 frames in 8 s; 6,890 fps in the timedemo's 0.1 s) | 0 | 3.7; 4.6 |
| the same, matched to FIX51 (`pace=own`, `vid_maxfps 999`; scratch case, not committed) | 476.1 median, 472.8 to 494.9 | 7.20 to 7.26 s | about 1,016 (8,132 frames in 8 s) | 0 | 4.6; 4.1 |

Neither is inside FIX51's range: the matched run is 15 to 22 percent above
it (its lowest run, 472.8, is 15 percent above FIX51's highest, 409.6), the
case as it is 13 to 14 percent above. Explanation, and what was and was not
checked:

- Matched, the run is FIX51's (own pace, `vid_maxfps 999`, 1920x1080
  windowed, `r_vsync 0`, one-second start, a window of 7.2 s where FIX51's
  was about 7 s, all demo); the rendered rate (1,016) is the cap's, as in
  FIX51 (995). The presented rate is the window server's pace of handing
  drawables back (no frame found every present slot held), and FIX51 itself
  measured that pace to move by up to a third over minutes with the host
  state ([the windowed baseline
  moved](FIX51.md#round-4-measurements): 230.8 to 424.6 a second for the same
  build on one afternoon). A 13 to 22 percent difference is inside that
  movement, in the other direction from its slow phase.
- Two things are not the same as FIX51's: the build (this branch's head,
  with the later commits of main, against round 4's `df2acd46` with a probe)
  and the host's state (load 3.7 to 4.6 here; the window server's
  utilization and the frequencies were not recorded in FIX51). The counting
  is not the difference: the whole-run figure (3,750 presented over 8 s,
  469 a second) agrees with the window's. Round 4's build was not re-measured
  in this batch (its probe build is not in the bench's cache and its
  commit has no `metal_rate` line), so the difference is explained, not
  isolated to the window server and not to the build.
- The case as it is presents fewer frames a second than the matched one
  (464.8 against 476.1) while rendering about 14 times as many frames (the
  timedemo line reports the demo's 689 frames in 0.1 s, and the run's 110,455
  frames over 8 s are mostly not presented: `no_drawable` 96.7 percent). Why
  the engine renders on after the demo ends was not investigated here. The
  rendered rate of the case as it is says nothing about what is shown, which
  is why the headline is the presented rate.

**Baselines** (`bench.sh baseline put`, no `--replace`, each a new set:
inputs `i-34c93291e155`, this branch's head `e6b55eae`, the M6 store; no
stored set was overwritten), the three `present-gl1-metal-*-vsync0` cases,
headline `presented_rate`, 3 runs after 1 warmup:

| Case | Presented a second, median | Range | Held (frames, whole run) | Rendered (fps) | Window |
|---|---|---|---|---|---|
| `present-gl1-metal-640x480-vsync0` | 589.5 | 519.4 to 608.7 | 0 | 3,497.5 (3,427.9 to 3,704.3) | 7.20 to 7.24 s |
| `present-gl1-metal-1920x1080-vsync0` | 461.8 | 454.9 to 498.9 | 0 | 3,219.6 (3,075.9 to 3,607.3) | 7.16 to 7.19 s |
| `present-gl1-metal-5120x2880-vsync0` | 521.9 | 516.4 to 527.9 | 1 to 5 (median 2) | 573.2 (568.5 to 573.7) | 5.78 to 5.84 s |

Quiet notes: the 640x480 `put` ran with the lagged 75 percent GPU
reading in its "before" snapshot (above); the 1920x1080 and 5120x2880
"before" snapshots read 0 percent. The 640x480 range is wide (a spread of
15 percent), the window server's pace at a small size, as in FIX51's round
3. The 5120x2880 window is 5.8 s, not 7.2: its demo takes 1.2 s of the 8 s
and the window begins one second after the first frame. The shim's
1920x1080 presented rate (461.8) against `ref_metal`'s (464.8) is the same
cell, the same host state and two renderers within 1 percent; FIX51's round
4 had the shim 4 to 8 percent above `ref_metal` there.

The other 9 `present-*` cases keep `fps` as headline; their stored baselines
remain valid, and `presented_rate` shows as a second metric from the next
run. The two raw logs and snapshots of the batches are kept outside the
repository.

**Files beyond the card's list.** `src/client/refresh/metal/header/rmb.h`
(`rmb_stats_t.present_held`), `src/client/refresh/metal/common/mtl_rate.h`
(new), `src/client/refresh/gl1/metal/mgl_frame.c` and `mgl_internal.h` (the
shim has no access to cvars, so `mgl_frame.c` reads `metal_stats` and
`metal_rate_start` through the renderer's import table and hands them to
`mgl_layer_create` in `mgl_config_t`), `tests/metal/rm_host_test.c`,
`tests/check-bench-quake2.sh`.
