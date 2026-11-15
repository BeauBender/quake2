# Re-measurement (TASK108)

TASK108 (#259) repeats the open cases above on the engine source of
`462f8d3e` (the runs were built from a worktree one documentation commit
on, `7085c8f1`; `git diff --stat 462f8d3e 7085c8f1 -- src third_party
Makefile` is empty, so `462f8d3e` is the engine commit of every row). It has
FIX52 (#254) at `8065c3ec`, TASK90 (#244) and TASK91 (#258), and predates
TASK92 (#286, N9), so the measured `ref_metal` draws no particles although
`demo1.dm2` has them; the other three renderers are unaffected by TASK92. The
host, the launch line (TASK97's, except that it passes `+set basedir
/path/to/q2data` instead of `-datadir`, sets `r_vsync` as the last `+set`, and
ends each run with a 45 s bounded wait in place of `timeout 20`), the
alternation (soft, gl1, gl1-metal, metal, then again; five
rounds per row) and the `frames, ... fps` log line are as above; `ref_gl1`
is `GL_RENDERER: Apple M6`, `GL_VERSION: 2.1 Metal - 91.7`, the Metal-backed
`gl1` `Metal shim on Apple M6 (gl1-metal)`, `GL_VERSION: 1.4 mgl`,
`ref_metal` `Metal device: Apple M6`. Builds: `make -j4`, `make ref_metal`,
then `make ref_gl1 WITH_METAL_SHIM=yes` into a second copy of `release/` (so
`ref_gl1` and `gl1-metal` run from two directories, not by swapping one
dylib).

**The first batch ran with the screen locked.** The host session's screen
was locked while the first batch ran (the owner reported it; afterwards
`ioreg -n Root -d1` printed `CGSSessionScreenIsLocked`, and the lock state
was not recorded at the time). Its results are not measurements and no
number from it is used below. What it logged, for the record: all 30 runs
of each of `ref_soft`, the Metal-backed `gl1` and `ref_metal` read 119.9 to
121.2 fps at every size from 640x480 to 2560x1440 and on the `water` demo,
and every `vid_fullscreen 1` request other than the desktop mode failed
with `Couldn't switch to exclusive fullscreen: CGDisplaySwitchToMode():
Unknown Error` (seven cases, then `Reverting to windowed r_mode 4
(640x480).`). The switch failures are explained by the lock (FIX57 (#281) was filed for
them: with the screen unlocked the switches work, below); the locked
1280x720 `ref_soft` fullscreen case was not repeated unlocked (1280x720 is
not among the fullscreen sizes run here). The flat 120 is not wholly the
same: unlocked, the 640x480 and 1280x720 windowed rows still read about 120
(below), while 1600x900 and 1920x1080 windowed and every fullscreen row do
not. The `water` rows are not comparable (below).

The runs below were taken with the screen unlocked: `CGSSessionScreenIsLocked`
read 0 (`ioreg -n Root -d1 | grep -c CGSSessionScreenIsLocked` printed 0)
immediately before every run, kept in the per-run table beside the load.
Before each run no other `quake2` was running (`pgrep -x quake2`) and the
one-minute load average (`uptime`) was below 4; the load range of each row
is in the table. Every run is a fresh engine in the foreground, killed after
45 s (the engine does not quit after the demo). The logs and the per-run
table are kept outside the repository.

**Results** (fps, five runs each, `r_vsync 0` as the last `+set`,
`timedemo 1`, `+demomap demo1.dm2` unless said, `vid_fullscreen 0` unless
said; the engine commit is `462f8d3e` for every row, 689 frames a run, `Real
display mode: 2560x1440@60.00` for every windowed row; "drawable" is the
log's `Drawable size:` line, in pixels, which prints the requested size and
so does not show the size the window or display got, TASK98; the native claim
below rests on `Real display mode:`):

| Size and mode | display mode | drawable | `ref_soft` | `ref_gl1` | `gl1-metal` | `ref_metal` | Load at start (20 runs) | Lock |
|---|---|---|---|---|---|---|---|---|
| 640x480 windowed | 2560x1440@60.00 | 640x480 | 119.8 to 120.0 | 2443.3 to 2570.9 | 120.2 to 120.2 | 120.0 to 120.1 | 1.97 to 3.64 | 0 |
| 1280x720 windowed | 2560x1440@60.00 | 1280x720 | 120.0 to 120.0 | 2166.7 to 2327.7 | 120.1 to 120.2 | 120.0 to 120.1 | 2.32 to 3.69 | 0 |
| 1600x900 windowed | 2560x1440@60.00 | 1600x900 | 197.7 to 321.2 | 2087.9 to 2194.3 | 67.1 to 96.1 | 62.3 to 75.2 | 1.92 to 3.62 | 0 |
| 1920x1080 windowed | 2560x1440@60.00 | 1920x1080 | 163.1 to 225.6 | 1808.4 to 1997.1 | 60.1 to 60.1 | 60.0 to 60.0 | 1.72 to 3.75 | 0 |
| 1920x1080 `vid_fullscreen 1` | 1920x1080@60.00 | 1920x1080 | 279.6 to 284.8 | 2312.1 to 2514.6 | 273.0 to 317.2 | 313.3 to 362.3 | 2.20 to 3.67 | 0 |
| 5120x2880 `vid_fullscreen 1` | 5120x2880@60.00 | 5120x2880 | 44.3 to 45.0 | 602.8 to 661.9 | 283.7 to 402.0 | 349.0 to 389.5 | 2.61 to 3.92 | 0 |
| `water.dm2`, 1280x720 windowed, 93 frames | 2560x1440@60.00 | 1280x720 | 120.0 to 120.8 | 3206.9 to 4428.6 | 121.3 to 121.4 | 120.0 to 121.3 | 1.94 to 3.35 | 0 |

Values in run order (rounds 1 to 5):

- 640x480: soft 119.9 119.8 120.0 119.9 119.9; gl1 2443.3 2460.7 2460.7
  2469.5 2570.9; gl1-metal 120.2 in all five; metal 120.1 120.0 120.0
  120.0 120.0.
- 1280x720: soft 120.0 in all five; gl1 2281.5 2208.3 2166.7 2327.7
  2273.9; gl1-metal 120.2 120.2 120.1 120.1 120.1; metal 120.0 120.0
  120.0 120.0 120.1.
- 1600x900: soft 197.7 321.2 316.6 319.0 219.5; gl1 2087.9 2194.3 2107.0
  2094.2 2146.4; gl1-metal 67.1 88.7 73.0 96.1 93.3; metal 66.5 70.2
  75.2 62.3 74.6.
- 1920x1080 windowed: soft 163.1 225.6 183.2 206.3 199.3; gl1 1957.4
  1997.1 1847.2 1963.0 1808.4; gl1-metal 60.1 in all five; metal 60.0 in
  all five.
- 1920x1080 fullscreen: soft 283.5 282.6 284.8 279.6 284.2; gl1 2312.1
  2460.7 2505.5 2514.6 2505.5; gl1-metal 302.6 287.7 273.0 304.3 317.2;
  metal 314.5 341.9 324.5 313.3 362.3.
- 5120x2880 fullscreen: soft 44.3 44.6 44.8 45.0 44.9; gl1 649.4 652.5
  661.9 643.9 602.8; gl1-metal 393.0 313.8 283.7 402.0 396.4; metal 389.5
  366.5 379.2 349.0 356.6.
- `water` windowed: soft 120.0 120.8 120.3 120.2 120.2; gl1 3206.9 3875.0
  4428.6 4043.5 3576.9; gl1-metal 121.4 121.4 121.3 121.4 121.3; metal
  121.3 120.0 120.2 121.3 120.8.

**First-pass fullscreen probes** (one run per renderer, eight runs, taken
before the five-round rows; they are in the 28 and 28 logs counted below and
in no row above; `r_vsync 0`, `lock` 0; fps, one-minute load at start):

| Mode | `ref_soft` | `ref_gl1` | `gl1-metal` | `ref_metal` |
|---|---|---|---|---|
| 5120x2880 `vid_fullscreen 1` | 45.2 (3.68) | 667.6 (3.79) | 300.6 (3.82) | 349.9 (3.48) |
| 1920x1080 `vid_fullscreen 1` | 283.5 (3.66) | 2281.5 (3.60) | 292.9 (3.38) | 328.1 (2.88) |

Five of the eight started above a load of 3.5 (3.68, 3.79, 3.82, 3.66,
3.60). Five of the cells lie inside the five-round ranges. Three fall
outside: 5120x2880 `ref_soft` 45.2 (above the 44.3 to 45.0 range) and
`ref_gl1` 667.6 (above 602.8 to 661.9), and 1920x1080 `ref_gl1` 2281.5
(below 2312.1 to 2514.6). With the probes the six-run ranges are 44.3 to
45.2, 602.8 to 667.6 and 2281.5 to 2514.6. The rows exclude the first pass
because it is one run per cell, taken before the alternation of five rounds
began, and the probe 667.6 and 45.2 are each the maximum of the six, the
2281.5 the minimum.

Runs that started at a one-minute load above 3.5, and what they do to their
row: 5120x2880 `metal` round 1 (3.71, 389.5) is the row maximum; 5120x2880
`gl1-metal` rounds 2 (3.92, 313.8) and 5 (3.77, 396.4) and `metal` rounds 3
(3.52, 379.2) and 5 (3.74, 356.6) lie inside their rows; 1920x1080
fullscreen `soft` round 3 (3.58, 284.8) is the row maximum and `gl1` round 5
(3.67, 2505.5) is inside; 1600x900 `gl1-metal` round 4 (3.58, 96.1) is the
row maximum, round 3 (3.62, 73.0) and round 5 (3.60, 93.3) are inside, and
`metal` round 2 (3.61, 70.2) and `soft` round 3 (3.54, 316.6) are inside;
the 1920x1080 windowed `gl1-metal` round 1 (3.75, 60.1), the 640x480
`gl1-metal` round 5 (3.64, 120.2) and the 1280x720 `gl1-metal` round 5 (3.69,
120.1, equal to the row minimum, shared with rounds 3 and 4) do not move
their rows. The one-minute average is a trailing mean. The 5120x2880 loads
(2.61 to 3.92) are the highest of any row, and the largest `gl1-metal` spread
(283.7 to 402.0, the minimum from round 3 at 3.41) does not follow them.

**Probes** (one run each, so not rows; `r_vsync 1` as the last `+set`, and
`+cvarlist r_vsync` in each log, which printed `*    r_vsync "1"`; `lock`
0 before each):

| Mode | `ref_soft` | `ref_gl1` | `gl1-metal` | `ref_metal` | Load at start |
|---|---|---|---|---|---|
| 1920x1080 windowed (round 1) | 60.0 | 60.0 | 60.1 | 60.0 | 2.05 to 2.70 |
| 1920x1080 windowed (round 2) | 60.0 | 60.0 | 60.1 | 60.0 | 2.02 to 2.53 |
| 1920x1080 `vid_fullscreen 1` | 59.9 | 60.0 | 60.1 | 60.0 | 1.44 to 1.87 |
| 5120x2880 `vid_fullscreen 1` | 46.7 | 60.0 | 60.1 | 60.0 | 1.80 to 2.54 |

The first windowed round (round 1) was taken without `+cvarlist`, so its
logs hold no `r_vsync` line; round 2 repeated it with `+cvarlist r_vsync` in
the logs.
With vsync on every renderer that can reach 60 reads 59.9 to 60.1 at both
sizes. At 5120x2880 `ref_soft` stays at 46.7, below the refresh, as it was
(44.3 to 45.0) without vsync.

**The native size (Scope 1) works.** With the screen unlocked
`+set vid_fullscreen 1 +set r_mode -1 +set r_customwidth 5120 +set
r_customheight 2880` switches the display to the 5120x2880 mode, on all four
renderers, with no `Couldn't switch` or `SetVideoMode failed` line in any
of the 28 logs of it. Each log says `User requested 5120x2880@-1.000000,
setting closest mode 5120x2880@60.000000`, `Real display mode:
5120x2880@60.00` and `Drawable size: 5120x2880`. That is the card's
native-resolution number, the 5120x2880 row above (5120x2880 is the panel's
size in pixels, `Mode  0: 5120x2880@60.00`; the desktop mode is 2560x1440
points). The same holds at 1920x1080 (`Real display mode: 1920x1080@60.00`,
`Drawable size: 1920x1080`, 28 logs). The two sizes show that the earlier
run's fullscreen switch failure was the locked session.

The 640x480 request still ends at 960x540. The display lists 21 modes
(`SDL display modes:` in the log), `Mode  0: 5120x2880@60.00` to `Mode 20:
960x540@60.00`, all `@60.00` and all 16:9: 5120x2880, 4096x2304, 3840x2160,
3200x1800, 2880x1620, 2560x1440, 2048x1152, 1920x1080, 1600x900, 1440x810,
1280x720, 1024x576 and 960x540 (eight of the sizes are listed twice). There
is no 640x480, no 4:3 or 16:10 mode and nothing below 960x540. The engine
asks SDL for the closest mode, `SDL_GetClosestFullscreenDisplayMode(
displays[last_display], w, h, 0, true, &closestMode)`
(`src/client/vid/glimp_sdl3.c:238`, the branch for `vid_rate` not above 0;
line 216 is the same call with `vid_rate->value`), and prints `User
requested %ix%i@%f, setting closest mode %ix%i@%f` (245). SDL's choice is in
`third_party/SDL3/src/video/SDL_video.c`, `SDL_GetClosestFullscreenDisplayMode`
(1373): the modes are walked from the largest down; the walk ends when the
request is wider than a mode (`if (w > mode->w)`, 1403 to 1405) and a mode
is skipped when it is not tall enough (`if (h > mode->h) continue`, 1407 to
1410); of the rest, a later (smaller) mode replaces the one found unless the
found mode's aspect ratio is strictly closer to the request's (1417 to
1420). The request is 4:3 and every listed mode is 16:9, so that test never
skips, and the walk ends on the smallest mode at least 640 wide and 480
high, 960x540, which is also the smallest mode in the list. So 960x540 is
what SDL3 returns for this list; nothing in that selection is a defect.
Unlocked, the four 640x480 requests (one per renderer) logged `User
requested 640x480@-1.000000, setting closest mode 960x540@60.000000`, `Real
display mode: 960x540@60.00`, no switch error, and `Drawable size:
640x480` (that the drawable then stays at the requested size is FIX45 (#230),
TASK98 Finding 1). Those four runs are single runs from the first unlocked
pass (`ref_soft` 255.5, `ref_gl1` 2823.8, `gl1-metal` 183.9, `ref_metal`
208.0 fps, load 2.46 to 3.62), probes, not a row.

**The 60 fps cap on the `CAMetalLayer` renderers (FIX53 (#250)).** At 1920x1080
windowed with `r_vsync 0`, `gl1-metal` and `ref_metal` read 60.1 and 60.0 in
all five runs, and `+cvarlist r_vsync` printed `*    r_vsync "0"` in a
`ref_metal` 1920x1080 windowed run (`logs/cvarlist-vs0.log`) that still read
60.0. So with the cvar read as 0 the cap is still there. That is the cap
FIX53 (#250) describes, and the data here goes no further than that: the
cap holds at every windowed 1920x1080 run of the two (ten runs) and at
neither fullscreen size with `r_vsync 0` (1920x1080: 273.0 to 317.2 and
313.3 to 362.3; 5120x2880: 283.7 to 402.0 and 349.0 to 389.5). `ref_soft` is not capped
windowed at that size now: 163.1 to 225.6 fps, the effect FIX52 (#254) was for
(it read 60.0 in all six TASK97 runs). The FIX52 section at the end of this
file has the probe of where the windowed cap starts.

Two more windowed ceilings are in the table, neither explained here:
640x480 and 1280x720 read 119.8 to 120.2 for `ref_soft`, `gl1-metal` and
`ref_metal` (they read the same in the locked first batch, so the lock is
not the cause of those two rows). The ~120 is new against TASK97 (237.2 to
328.1 at 640x480 and 294.3 to 326.2 at 1280x720 for the same renderers) and
against the TASK92 section's frame-time table (`skywater`, `demo1.dm2`: 1285
to 1398 fps for `metal` and 816 to 984 for `gl1-metal`, `r_vsync 0`, taken at a
load of 7.8 to 13.4 on that section's host), both much higher. FIX51 (#238) now
carries these data, for the windowed ceilings and the 1600x900 spread.
The 1600x900 row reads neither: `ref_soft`
197.7 to 321.2 (the runs split into 197.7 and 219.5 against 316.6 to 321.2),
`gl1-metal` 67.1 to 96.1 and `ref_metal` 62.3 to 75.2, against TASK97's
284.7 to 287.8, 245.5 to 252.7 and 277.5 to 283.4 at the same size. The cause
was not found; no card yet.

**`ref_gl1`.** It is not capped: 2443.3 to 2570.9 at 640x480, 1808.4 to
1997.1 at 1920x1080 windowed and 602.8 to 661.9 at 5120x2880 fullscreen. TASK97
had 2304.3 to 2600.0 and 1882.5 to 2126.5 at the first two sizes, which these
overlap. TASK97's qualification stands (Finding 2): the number may be a
CPU-submit rate, an upper bound; it was not checked against a GPU timestamp,
and `gl_finish 1` was not tried.

**`water` spread (Scope 3).** `water.dm2` was made again by `scripts/pins.sh
make-demo water` in each of the two release copies (it printed `(98
messages, 97 to 124 wanted)` for both; the first run's demo had 124
messages), because the first run's copy was removed with its build; this one plays 93
frames in the timedemo, not 119, so it is not the same demo as the first
batch's and the rows are not comparable with it. A first attempt of the five
rounds ran before the demo existed and read `0 frames` (`ERROR: Couldn't
open demos/water.dm2`); its rows are in `results.csv` as
`water1280x720-nodemo` and are not measurements. `ref_gl1` gave 3206.9,
3875.0, 4428.6, 4043.5 and 3576.9 fps, range 3206.9 to 4428.6; the log
prints `0.0 seconds`. The timer counts whole milliseconds
(`src/client/cl_network.c`, the timedemo end), so the five runs took 29, 24,
21, 23 and 26 ms (a 1 ms step is 3 to 5 percent of a run), and a `ref_gl1`
rate from this demo is good to that and no finer. The three `CAMetalLayer`
renderers gave 120.0 to 121.4 (their runs last 0.8 s, so the 1 ms step is
about 0.1 percent). No run of the row started above a load of 3.5 (1.94 to
3.35). The demo is long enough to show the ceiling and too short to say more
about `ref_gl1` than the range above.

**Not measured, now.** The 1600x900 windowed spread, the 120 fps windowed
ceiling at 640x480 and 1280x720, and the windowed 60 fps cap itself are
seen, not explained. `ref_gl1` against a GPU timestamp (`gl_finish 1`). A
windowed timedemo above 1920x1080, or at the non-16:9 modes 1920x1200,
1600x1200 and 2560x1080 (booted only). Any fullscreen mode other than the
desktop, 1920x1080 and 5120x2880 modes and the 640x480 request (one run each
for the last). The `r_vsync 1` rows are one run each. Runs with no
`r_vsync` readback in the log (no `+cvarlist`): the 1920x1080 windowed row
(20 runs), the 5120x2880 fullscreen row (20 runs), the first windowed `r_vsync
1` probe round (4) and the twelve first-pass fullscreen probes (5120x2880,
1920x1080, 640x480); every other run's log has the `r_vsync` line, with the
value set. The desktop-size fullscreen
row of the first batch (2560x1440, which switches no mode) was not repeated
unlocked. The `water` demo is 93 frames, not the first batch's 119. The
lock state was read once per run before launch, not during it, and the owner
may have been at the machine.
