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
whole run, like the totals line. With the cvar `0`, nothing is read, kept
or printed; no frame changes. The shim does not read `metal_stats` today
(its `mgl:` totals line is always printed); it reads it once, when its
context is made, for this line only.

**The window's start.** A new cvar, `metal_rate_start` (seconds, default
`1`, FIX51's value), is the time from the first frame ended to the
window's first sample: the first frame that ends at least that long after
the first frame ended begins the window, and the last frame ended before
the close ends it. A run shorter than the start has no window and prints
`0.0` over `0.00 s`. It is read once at the renderer's start.

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
  in the shim. It includes the held frames.

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

Screen locked for all of it (`CGSSessionScreenIsLocked` 1), so only what a
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

**Timing: pending, screen locked.** FIX51's round-4 presented rates
(`ref_metal` 1920x1080 windowed: 404.9 to 409.6 a second; the shim at
1920x1080: 424.4 to 440.8) were not reproduced through `bench.sh`: that
needs the unlocked screen, a 4-minute pre-warm in the same lock hold,
builds alternated and the per-run snapshot of the shared runbook. When it
is done, run `present-metal-1920x1080-vsync0` and
`present-gl1-metal-1920x1080-vsync0` and compare `presented_rate`; the
cases there use `pace=timedemo` and the stock `vid_maxfps`, where FIX51's
used its own pace and `vid_maxfps 999`, so a difference is expected and
should be explained, not hidden.

**Baselines.** The `present-gl1-metal-*-vsync0` cases now judge
`presented_rate`, and their stored baselines hold none: each needs a new
baseline, recorded on an unlocked screen. No stored set was replaced
(`--replace` was not used on any). The other 9 `present-*` cases keep
`fps` as headline; their baselines remain valid, and `presented_rate`
shows as a second metric from the next run.

**Files beyond the card's list.** `src/client/refresh/metal/header/rmb.h`
(`rmb_stats_t.present_held`), `src/client/refresh/metal/common/mtl_rate.h`
(new), `src/client/refresh/gl1/metal/mgl_frame.c` and `mgl_internal.h` (the
shim has no access to cvars, so `mgl_frame.c` reads `metal_stats` and
`metal_rate_start` through the renderer's import table and hands them to
`mgl_layer_create` in `mgl_config_t`), `tests/metal/rm_host_test.c`,
`tests/check-bench-quake2.sh`.
