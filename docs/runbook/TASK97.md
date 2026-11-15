# Frame rates on a physical GPU (TASK97)

Every earlier frame time in this runbook comes from the build VM, whose
OpenGL is a software renderer and whose Metal device is paravirtualised.
These come from a bare-metal Apple Silicon Mac, taken with `timedemo` over
the stock `demo1.dm2` (689 frames a run). Nothing here is a pin: no frame
was recorded or looked at.

**Host.** Model `Mac18,5`, GPU and Metal device "Apple M6", 32 GiB, macOS
27.2.1 (build 26B131), SDL 3.4.20 (cocoa), the display's current mode
2560x1440@60 (`Real display mode:` in every log; its list runs from
5120x2880 down). `ref_gl1` reports `GL_VENDOR: Apple`, `GL_RENDERER: Apple
M6`, `GL_VERSION: 2.1 Metal - 91.7`; `gl1-metal` reports `Metal shim on
Apple M6 (gl1-metal)`, `GL_VERSION: 1.4 mgl`. Source: `main` at 41745a59
(so `ref_metal` has the lightmapped world, sky, water and translucent
surfaces of TASK87 to TASK89 and draws no entities, dynamic lights or
particles, as `rm_main.c` at that commit says; TASK90 (#244) and TASK91
(#202) have since added brush entities, alias models and the view weapon,
then sprites, beams, stencil shadows, dynamic lights and flash blobs, so the
`ref_metal` column is of a lighter renderer than `main`'s and is not a
like-for-like comparison).

**Method.** One release build of the `quake2` binary, `ref_soft` and
`ref_metal`, and two builds of `ref_gl1.dylib`, the plain one and the
`WITH_METAL_SHIM=yes` one; between `gl1` and `gl1-metal` only that dylib was
swapped. Each run is a fresh engine started in the
foreground under `timeout 20`:

```sh
release/quake2 -datadir $PINS_BASEDIR -portable +set vid_renderer <gl1|soft|metal> \
  +set vid_fullscreen 0 +set r_mode -1 +set r_customwidth W +set r_customheight H \
  +set r_vsync 0 +set s_initsound 0 +set timedemo 1 +demomap demo1.dm2
```

The `frames, ... fps` line of the log is the number. The engine does not
quit after the demo, so every run ended at the timeout. Renderers were
alternated run by run (soft, gl1, gl1-metal, metal, then again; one such
pass is a round). Windowed only: `vid_fullscreen 1` falls back to 960x540
on this host. Every log printed `Drawable size:` equal to the requested
size. That line prints the requested size when the window is not high-DPI
aware, so it does not show whether the compositor scales the window. No
number below is a native-resolution number: 1920x1080 is the largest 16:9
size that fits the 2560x1440 display windowed (the engine's 1920x1200 and
1600x1200 modes were not tried), and the display's native size was not
measured.

**Results** (fps, sorted, `r_vsync 0`; ranges here, the values of the
larger batches below the table):

| Size | `ref_soft` | `ref_gl1` | `gl1-metal` | `ref_metal` | Runs |
|---|---|---|---|---|---|
| 640x480 | 253.6 to 318.1 | 2304.3 to 2600.0 | 273.8 to 328.1 | 237.2 to 317.7 | 6 |
| 1920x1080 | 60.0 (all six) | 1882.5 to 2126.5 | 60.1 (all six) | 60.0 (all six) | 6 |
| 1280x720 | 294.4 to 306.0 | not run | not run | 294.3 to 326.2 | 3 |
| 1600x900 | 284.7 to 287.8 | 2180.4 to 2237.0 | 245.5 to 252.7 | 277.5 to 283.4 | 3 of 5 |

Runs is the number of rounds the row is made of. 640x480: 6 rounds, plus
one earlier round left out as a warm-up. 1920x1080: 6 rounds. 1280x720: 3
rounds, `ref_soft` and `ref_metal` only. 1600x900: 5 rounds were run and
the row uses the last three; the first two were discarded as overlapping a
gate run by another lane (`scripts/test.sh`, 18:00:56Z to about 18:05Z);
the log times fit that for the first round but do not settle it for the
second. Their values are given below.

640x480 values: soft 253.6 267.5 276.6 297.1 306.2 318.1; gl1 2304.3
2426.1 2426.1 2434.6 2443.3 2600.0; gl1-metal 273.8 274.4 274.5 295.5
309.4 328.1; metal 237.2 244.4 290.7 310.1 313.3 317.7.

1920x1080 values: gl1 1882.5 1887.7 1903.3 1930.0 2032.4 2126.5; the other
three are 60.0 (`ref_soft`, `ref_metal`) or 60.1 (`gl1-metal`) in all six.

1600x900 values (used three, then the two discarded rounds): soft 284.7
286.0 287.8, discarded 277.6 280.1; gl1 2180.4 2215.4 2237.0, discarded
2187.3 2201.3; gl1-metal 245.5 246.5 252.7, discarded 202.8 255.0; metal
277.5 277.6 283.4, discarded 245.4 272.7. Including the discarded rounds
widens the ranges to 277.6 to 287.8, 2180.4 to 2237.0, 202.8 to 255.0 and
245.4 to 283.4.

1280x720 values: soft 294.4 299.8 306.0; metal 294.3 296.5 326.2.

`gl1` ran 0.3 s (0.3 to 0.4 s at 1080) for the 689 frames; the others 2.1 to 2.9 s
at 640x480 and 11.5 s at 1920x1080. The batch script waited until no
other `quake2`, `make`, `cc` or `clang` and no `scripts/test.sh` ran
before each run. The one-minute load average at each run start was printed
to the console and not kept in the logs, so it is recorded here only as
noted at the time: 1.9 to 2.6 over the 640x480 and 1920x1080 batches, 2.1
to 2.4 over 720, and 2.0 to 4.7 over the 1600x900 batches, where one stretch
went to 4.7 and another to 3.0 to 3.5 while only this lane's engines were
meant to be running, so those rows carry that spread; which round started
at which load is not recoverable.

**Not measured**, against the card's scope (as written by TASK97; TASK108
(#259) has since measured every item below, the native-resolution number
included (5120x2880 fullscreen), see "Re-measurement (TASK108)" after the
Findings, and what it still did not measure is listed there):
no native-resolution number (`vid_fullscreen 1` falls back to 960x540 on
this host and the largest 16:9 windowed size that fits is 1920x1080); the
`water` harness demo was not run (about 100 frames, too short for a stable
rate); the 1280x720 and 1600x900 rows have three runs, not five or more;
`ref_gl1` and the Metal-backed `gl1` were not run at 1280x720; fullscreen
was not run.

**What the VM cannot reproduce.** All of it: `ref_gl1` at 2000 fps and
more against a software GL, any hardware Metal rate, the 60 fps ceiling
below, and the ratio of the Metal renderers to Apple's own OpenGL. The
build VM capped once: the TASK90 section records the `demo1.dm2` timedemo
of `ref_metal` running at that display's 120 fps whatever `r_vsync` and
`vid_maxfps` said (the window size is not given there), while the TASK91
section records the same timedemo on that VM unpaced, at about 1,400 to
1,650 fps, so the cap is not constant there. It is a lead for Finding 3,
not shown to have the same cause.

**Findings** (cards filed, none fixed here; Finding 1 is FIX51 (#238),
Finding 3 is FIX52 (#239)):

1. `gl1-metal` and `ref_metal` are about 8 times slower than `ref_gl1` at
   640x480 (273.8 to 328.1 and 237.2 to 317.7 fps against 2304.3 to
   2600.0), far outside the run-to-run range, and no faster than the CPU
   `ref_soft` (253.6 to 318.1). Both sit near 3 to 4 ms a frame whatever
   the window size up to 1600x900 (3 runs; no Metal-backed `gl1` at
   1280x720), which points at a per-frame fixed cost and not at drawing.
   `ref_soft` shows the same flat 3 to 4 ms a frame up to 1600x900 (253.6
   to 318.1, 294.4 to 306.0 and 284.7 to 287.8 fps at 640x480, 1280x720 and
   1600x900), so it is not an independent CPU baseline, and the fixed cost
   is common to the `CAMetalLayer` present path on this host: a lead, not a
   diagnosis (FIX52 (#239) diagnoses part of that cost). On the build VM the
   same path runs `ref_metal` at about 1,400 to 1,650 fps (TASK91 section),
   so the cost is not inherent to it. That `ref_soft` is slower
   than `ref_gl1` is expected for
   a CPU renderer and is not a finding.
2. The `ref_gl1` number may be a CPU-submit rate: Apple's GL queues frames
   and the timer stops when the demo ends, without waiting for the GPU, so
   the effect is bounded by the frames still queued at the end unless the
   driver does not throttle. It was not checked against a GPU timestamp, so
   treat it as an upper bound. `gl_finish 1`, a switch the engine has,
   would bound it in a later run.
3. At 1920x1080 windowed, `ref_soft`, `gl1-metal` and `ref_metal` all run
   at 60.0 to 60.1 fps with `r_vsync 0`, while 1600x900 and below are
   uncapped and `ref_gl1` is not capped at all. The three capped renderers
   are exactly the three that present through a `CAMetalLayer` (`ref_soft`
   through SDL's Metal renderer, with display sync off); `ref_gl1`, the
   only uncapped one, presents through OpenGL. So the cause is likely
   common to the `CAMetalLayer` present path at that size. Not diagnosed
   here (FIX52 (#239) carries the diagnosis); until it is fixed, no 1080p
   rate for these three says anything about the renderer.
