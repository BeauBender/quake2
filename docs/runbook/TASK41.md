# `gl1` references and the `soft` against `gl1` thresholds (TASK41, card P3)

Host: the build VM, macOS arm64, `ref_gl1` through Apple's software
OpenGL (engine log: `GL_VENDOR: Apple Inc.`, `GL_RENDERER: Apple Software
Renderer`, `GL_VERSION: 2.1 APPLE-24.0.2`), 640x480, the default `PINS_EVERY`
30 except `cinematic` (`PINS_DEMO=idlog.cin PINS_EVERY=7`). The `gl1` frames
are therefore references for the shim's arithmetic, not for a graphics card;
a recording on accelerated hardware has a different `GL_RENDERER` and has to
be measured again, not assumed equal to these.

Method, per scenario: four `gl1` records (`record gl1 <scenario>`, then
`--run 2` to `--run 4`) and three `soft` records, from one build, one after
the other. Every pair is compared with `compare --cand-id <dir> --ref <r>
--ref-id <dir>`: the 6 `gl1`/`gl1` pairs, the 3 `soft`/`soft` pairs and all
12 `soft`/`gl1` pairs. Scores are per frame (mean difference, and the
fraction of pixels more than 8/255 apart in some channel); the ranges below
run over all frames of all pairs of the group. Every record exited 0. Load
is the 1-minute load average from `uptime` at the end of each record and at
the start of each compare (the machine was shared with other work all the
time; the range given is over all runs of the row).
`skywater` was measured with `water.dm2` made by `make-demo water` (100
messages) and with the stock `demo1.dm2`.

| Scenario | Demo | Frames | `gl1`/`gl1` noise (score, bad) | `soft`/`soft` noise (score, bad) | `soft`/`gl1` range (score, bad), worst frame | Proposed `soft` against `gl1` threshold | Load |
|---|---|---|---|---|---|---|---|
| `world` | `demo1.dm2` | 23 | exact: 0, 0 in 6 of 6 pairs | exact: 0, 0 in 3 of 3 | 0.013180 to 0.081482; 0.132624 to 0.922676 | none: not meaningful (below) | 11.6 to 21.3 |
| `hud2d` (with `gl_polyblend 0`, TASK82) | `demo1.dm2` | 23 | exact: 0, 0 in 6 of 6 | exact in 3 of 3 | 0.461199 to 0.471755; 0.990553 to 0.998561 | none: not meaningful (below) | 23.2 to 32.1 |
| `models` | `demo1.dm2` | 23 | not exact in 6 of 6: worst 0.000826, 0.004583 | not exact in 3 of 3: worst 0.000150, 0.000345 | 0.015337 to 0.090930; 0.179834 to 0.925937 | none: not meaningful | 10.9 to 20.9 |
| `particles` | `demo1.dm2` | 23 | not exact in 6 of 6: worst 0.010232, 0.172093 | not exact in 3 of 3: worst 0.011482, 0.130306 | 0.028070 to 0.141715; 0.408568 to 0.998324 | none: not meaningful | 16.2 to 30.6 |
| `skywater` | `water.dm2` | 4 | not exact in 3 of 6 pairs: worst 0.002562, 0.022972 | exact in 3 of 3 | 0.030261 to 0.033948; 0.539687 to 0.673857 | `--threshold 0.034 --bad-threshold 0.68` (below; on the accelerated references re-measured as 0.031 / 0.66, see TASK83) | 16.7 to 19.5 |
| `skywater` | `demo1.dm2` | 23 | exact: 0, 0 in 6 of 6 | exact in 3 of 3 | identical to `world`: 0.013180 to 0.081482; 0.132624 to 0.922676 | none: not meaningful | 16.2 to 26.0 |
| `cinematic` | `idlog.cin`, every 7th | 12 | exact: 0, 0 in 6 of 6 | exact in 3 of 3 | 0 to 0.018680; 0 to 0.322956 (frame 0 is black and exact) | `--threshold 0.02 --bad-threshold 0.35` | 17.2 to 23.8 |

Each cell of the `soft`/`gl1` column is "score range; bad-fraction range":
the smallest and the largest per-frame value over all frames of all 12
pairs (the largest is the worst frame). All records are from the build of
commit `885e70fa` (the base of this work), except the `hud2d` row: TASK82
added `set gl_polyblend 0` to the scenario (see "The screen flash and
`hud2d`" below) and measured the row again the same way, 4 `gl1` and 3
`soft` records of commit `74205ac9`, all 21 pairs compared. `soft` has no
such cvar, so its frames are byte-identical to the old scenario's (score 0
on all 23 frames against a `soft` record of it), and the four flash frames
now darken in `soft` only, which moves the worst `soft`/`gl1` score from
0.464962 to 0.471755.

Renderer string of every `gl1` number above: `Apple Software Renderer`.

The `models` row comes from two measurement sets (the original one at
1-minute load 10.9 to 20.0 and a re-measurement by the review of this card,
4 `gl1` and 3 `soft` records of the same build, load 11.2 to 20.9); the
worst values in it, 0.000826 for `gl1`/`gl1` and 0.090930 for `soft`/`gl1`,
come from the second set (the first had 0.000260 and 0.090400). Every
column of the table holds sample maxima of a scenario driven by the engine's
PRNG, not bounds: a further record can exceed them.

## Where an agreement threshold means something

Two scenarios have one: `cinematic` and `skywater` with `water.dm2`.

`cinematic`: its `soft`/`gl1` frames agree to score 0.0187 and bad fraction
0.323 at worst, and the proposal keeps the default score threshold 0.02 and
sets the bad fraction at 0.35, following the convention of "Thresholds
measured for nondeterministic scenarios" (the default where the worst value
is under it, otherwise the worst value rounded up, never widened to make a
pin pass). The bad fraction is loose, so both values reject a wrong picture:
black against the `gl1` frames at `--threshold 0.02 --bad-threshold 0.35`
passes frame 0 (which is black) and is over on frames 1 to 11, with score
0.049713 to 0.117941 and bad fraction 0.969580 to 1.000000 (the review of
this card, `framediff`, load 10.3). The score margin against 0.02 is small
(7 percent), so a third renderer that is smoother or sharper than either of
these may land over it; that would be a finding, not a threshold to move.

`skywater` with `water.dm2`: the worst `soft`/`gl1` values are score
0.033948 and bad fraction 0.673857, so the threshold is the worst value
rounded up, `--threshold 0.034 --bad-threshold 0.68` (on the accelerated
references the re-measured value is 0.031 / 0.66, see "`gl1` references
from an accelerated OpenGL (TASK83)"). It rejects a black
frame: made with `framediff --block <gl1 frame> <out> 0 0 640 480 0` from
each of the 4 frames of one `gl1` record and scored with the comparator,
black scores 0.044689, 0.052517, 0.052069 and 0.041041 with bad fraction
0.767340, 0.767340, 0.994681 and 0.993538, over on all four frames at both
values (1-minute load 7.4, this card's rework). The margin is small on the
score (0.041 against 0.034) and rests on 4 frames, so it is a starting
value for a candidate that is not `soft`, not a proof that a wrong picture
cannot pass.

For the others no agreement threshold is meaningful. `soft` and `gl1` draw
different pictures of the same scene: bad fractions run from 0.13 to 0.92
on `world`, 0.18 to 0.93 on `models`, 0.41 to 0.998 on `particles` and 0.99
on `hud2d`, and `skywater` with `demo1.dm2` is identical to `world`. A
black frame against the `gl1` frames, scored with the comparator by the
review of this card (load 10.3), is over the threshold on only some frames
when the threshold is wide enough to pass every `soft` frame: at
`--threshold 0.081482 --bad-threshold 0.922676` (the worst `world` values)
it passes 15 of 23 frames of `world` and is over on 8 (score 0.037498 to
0.148949, bad fraction 0.617708 to 0.998796 over the frames); at 0.090930
and 0.925937 (the worst `models` values) it passes 15 of 23 frames of
`models` and is over on 8. A record as a whole fails when any frame is over,
so an all-black record is still rejected, but such a threshold cannot tell
a wrong picture on most single frames, and the `soft`/`gl1` ranges overlap
the black-frame ranges. The `particles` black frame (score 0.0484 to
0.2121) is an estimate from a throwaway script, not the comparator, and
was not re-measured. `hud2d` is a different case: its reason is the clear
colour (below), and black scores 0.496704 to 0.499510 there (against the 23
frames of the TASK82 `gl1` record, `framediff --block`, load 14.5), above the
worst `soft`/`gl1` value 0.471755, so a threshold that passes `soft` rejects
every black frame; the `soft`/`gl1` difference there is a property of the
scenario, not of the picture. So a third renderer cannot be judged against
`soft` by these numbers, and the judge for the shim is `gl1`, not `soft`.
Causes, each checked by looking at a pair of frames (downscaled to PNG in
scratch, one pair per cause):

- Colour depth: `soft` draws from the 8-bit palette, 93 to 174 distinct
  colours per frame in the frames looked at, and `gl1` 1923 to 28318 (true
  colour). Seen in a zoomed crop of `world` frame 6: the `soft` pixels
  are blocky and speckled, the `gl1` ones smooth.
- Filtering: on `cinematic` frame 3, zoomed 5 times, `soft` shows the
  picture as hard-edged blocks and `gl1` as bilinear-smoothed ones. The
  `gl1` frame is also slightly brighter here (mean RGB 51.9, 20.5, 10.1
  against 45.0, 16.5, 7.7), the opposite of `world`.
- Lighting and tint: on every `world` frame looked at (7 of 23, and 2 of
  4 on the water demo) `gl1` is darker and redder, for example `world`
  frame 6 mean RGB 39.0, 33.6, 26.9 on `soft` against 30.6, 21.2, 9.5 on
  `gl1`, and the underwater frame of the water demo shows the `soft`
  green-grey tint as a dark red-brown on `gl1`. The cause of the tint was
  not isolated.
- Clear colour in `hud2d`: with `r_drawworld 0` `soft` leaves a uniform
  grey (26, 26, 26) and `gl1` its own clear colour (255, 0, 128, set in
  `R_ResetClearColor` in `gl1_main.c`), 304,338 and 304,308 of 307,200
  pixels. That is the whole of the 0.46 score (measured before the
  `gl_polyblend 0` change; the re-measured range of 0.461199 to 0.471755
  above includes the `soft`-only pickup darkening, see the note there, and
  the pixel counts hold for a frame without a pickup); the status bar and
  crosshair are the same size in both.
- Not isolated: on `world` frame 21 the cloud pattern of the sky and the
  top of the tower differ in shape between the renderers; the cause was
  not found.

## `gl1` against `gl1`: the floor for the Metal shim

The thresholds the shim is judged at are `gl1-metal` against `gl1`. The
starting values are in the table below, and the first shim card that draws
each scenario confirms or revises them (`hud2d` and `cinematic`: confirmed
by TASK82). The
`gl1`/`gl1` column is their lower bound: a `gl1-metal` threshold under it would fail two
records of `gl1` itself. `world`, `hud2d`, `cinematic` and `skywater` from
`demo1.dm2` are exact, so any difference there is a real one. `models`
(worst bad fraction 0.004583, close to the default 0.005 that
`models` is accepted at), `particles` and `skywater` from `water.dm2`
are not exact between two `gl1` records, so for those the floor is the
worst value in the column, and a Metal candidate is judged against at least
two `gl1` references and the noise of each.
These are numbers from Apple's software renderer; TASK83 measured them
again on accelerated hardware (below). The worst values in the column are sample maxima of a
PRNG-driven scenario, not bounds (`models` was measured twice and the
second set was worse), so a floor is a lower bound on what to accept, not
the number to accept at.

Accept `gl1-metal` against `gl1` at (confirmed by TASK83 against `gl1`
references from an accelerated OpenGL, `Apple M6`; the numbers are in
"`gl1` references from an accelerated OpenGL (TASK83)" below):

| Scenario | Accept at | Why |
|---|---|---|
| `world` | `--threshold 0.02 --bad-threshold 0.005` (the defaults) | `gl1`/`gl1` is exact; confirmed by TASK83: three `gl1-metal` records against two accelerated `gl1` records pass on all 23 frames of all 6 pairs, worst score 0.000010, bad 0.000033; also with `gl1_multitexture 0` (worst 0.000011, 0.000052) |
| `hud2d` | the defaults | exact; confirmed by TASK82 (with `gl_polyblend 0`): three `gl1-metal` records against four `gl1` records, score 0 and bad 0 on all 23 frames of all 12 pairs; exact against the accelerated references too (TASK83) |
| `cinematic` | the defaults | exact; confirmed by TASK82: three `gl1-metal` records against the `gl1` reference, score 0 to 0.000191 and bad 0 on all 12 frames (bilinear rounding), the same in each record; exact against the accelerated references (TASK83) |
| `skywater` with `demo1.dm2` | the defaults | exact; confirmed by TASK83 as for `world` (the frames are the same) |
| `models` | the defaults, 0.02 and 0.005 | confirmed by TASK83: accelerated `gl1`/`gl1` worst 0.000824 and 0.001058, and `gl1-metal` against it at most 0.000792 and 0.001068 in 6 pairs, a fifth of the default bad fraction |
| `particles` | `--threshold 0.02 --bad-threshold 0.25` (the `// nondeterministic:` line) | confirmed by TASK83: accelerated `gl1`/`gl1` worst 0.009974 and 0.185570, `gl1-metal` against it at most 0.010591 and 0.151660 in 6 pairs, with the flash drawn |
| `skywater` with `water.dm2` | `--threshold 0.02 --bad-threshold 0.03` (the `// nondeterministic:` line) | indicative only (TASK83): accelerated `gl1`/`gl1` exact on a 144-message demo that `make-demo water` refused and that was renamed by hand, `gl1-metal` against it at most 0.000004 and 0.000013 in 6 pairs; not acceptance evidence until `make-demo water` accepts a demo on that host (#232) |

The `// nondeterministic:` lines of the scenario files are unchanged:
`gl1` shows the same nondeterminism as `soft` (`models`, `particles`,
and the water demo), inside the numbers they already carry for `particles`
and `skywater` (`gl1`/`gl1` 0.010232 and 0.172093; 0.002562 and 0.022972,
under `--bad-threshold 0.03`), and just under their default for `models`.
