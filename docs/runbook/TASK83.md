# `gl1` references from an accelerated OpenGL (TASK83)

Host: the bare-metal host, an Apple M6 Mac on macOS 27.2.1, 640x480, the
default `PINS_EVERY` 30 except `cinematic` (`PINS_DEMO=idlog.cin
PINS_EVERY=7`). `ref_gl1` here is hardware OpenGL: the engine log of every
`gl1` record reads `GL_VENDOR: Apple`, `GL_RENDERER: Apple M6`, `GL_VERSION:
2.1 Metal - 91.7`. The shim's log reads `GL_RENDERER: Metal shim on Apple M6
(gl1-metal)` and `ref_metal`'s `Metal device: Apple M6`. Every record and
compare ran in a pin store outside the repository (`$PINS_STORE`) from the
owner's game data (`$PINS_BASEDIR`), one engine at a time; nothing derived
from either is committed. 1-minute load average 2.0 to 4.7 throughout.

**How the references were validated.** They were recorded on a desktop in
use. A first pass (engine commit `f6bb1927`) was discarded whole: the
owner typed while an engine window had focus, and three `gl1-metal` records
of a scratch copy of `particles` rendered 9560, 794 and 1064 frames where
`demo1.dm2` renders 689 (a key during demo playback brings up the menu,
which pauses the local server while the client keeps rendering; the engine
log shows nothing of it). The second pass, engine commit `de1da7fd`, every
number below, was recorded after the owner had left the keyboard; the
display's screen saver came on during it, between engine runs. Each record
of it was accepted only when (a) the engine exited 0 and `pins.sh` wrote
`complete`, (b) `timedemo.txt` gives the rendered-frame count of a clean
run (689 for `demo1.dm2`, 139 for the water demo, none for the cinematic)
and the manifest the matching capture count (23, 5, 12), and (c) a second
record of the same renderer and scenario agrees with it at that scenario's
floor (exact for every scenario without the PRNG; `models` and `particles`
inside their known noise, below). All 65 records of the second pass met all
three. The harness does not keep input out of a run: `pins.sh record`
opens a normal window and nothing in the scenarios or the command line
disables keys, so a record on a desktop in use is checked this way.

**`gl1` against `gl1`** (two records per scenario, so one pair each; the
column says how many of its frames are exact):

| Scenario | Demo | Frames | `gl1`/`gl1` | Floor for `gl1-metal` |
|---|---|---|---|---|
| `world` | `demo1.dm2` | 23 | exact, 23 of 23 | exact |
| `world-fullbright` | `demo1.dm2` | 23 | exact | exact |
| `hud2d` | `demo1.dm2` | 23 | exact | exact |
| `models` | `demo1.dm2` | 23 | 22 exact; frame 7 score 0.000824, bad 0.001058 | 0.000824, 0.001058 |
| `particles` | `demo1.dm2` | 23 | 7 exact, 9 over the defaults; worst score 0.009974 (frame 12), bad 0.185570 (frame 16) | 0.009974, 0.185570 |
| `skywater` | `demo1.dm2` | 23 | exact | exact |
| `skywater` | `water.dm2` (144 messages) | 5 | exact | exact |
| `cinematic` | `idlog.cin`, every 7th | 12 | exact | exact |

Scratch copies (untracked scenario files, deleted after): `world` plus
`gl1_multitexture 0`, `world` plus `gl_polyblend 0`: exact; `particles`
plus `gl_polyblend 0`: worst 0.015223, 0.187988 (the store also holds an
unused scratch scenario, `world-mt0-pb0`, with records of the discarded
first pass only). `soft`/`soft` for the
comparison: `world` and the water demo exact, `models` 0.000342 and
0.000752, `particles` 0.010346 and 0.106465.

So the accelerated `gl1` is deterministic everywhere the engine's PRNG is
not involved, as Apple's software renderer was, and its `models` noise
(bad 0.001058) is far under the default 0.005, where the software
renderer's was at 0.004583.

**`soft` against the accelerated `gl1`.** `world` 0 of 23 frames pass at the
defaults, worst score 0.077802 (frame 7) and bad 0.922132 (frame 21);
`models` 0.087157 and 0.925456; `particles` 0.092917 and 0.924736 (each
the worst of all 4 pairs of 2 `soft` and 2 `gl1` records). The
"not meaningful" finding of TASK41 stands for these. The water demo, all
4 pairs of 2 `soft` and 2 `gl1` records identical frame for frame: score
0.027971 to 0.030022, bad 0.507620 to 0.657751 (frames 0 to 2 from above,
0.51; frames 3 and 4 from below, 0.65 and 0.66). The threshold re-measured
for this demo, by TASK41's rule (the worst value rounded up), is
`--threshold 0.031 --bad-threshold 0.66` (the old 0.034 and 0.68 were
measured on another make of the demo against the software renderer). A
black frame made from each `gl1` frame with `framediff --block` scores
0.041046 to 0.051072, bad 0.767523 to 0.994030: over on all 5 frames at
the new values. The threshold is tied to this make of the demo and these
references, like the old one.

**The water demo on this host.** `make-demo water` refused all 10 demos it
made (5 in each pass): 141 to 145 messages against the 97 to 124 the
script states, 22 to 23 seconds each at load 2.0 to 4.4. The engine
renders N - 5 frames from N messages here too (144 messages, 139 frames),
so the demo captures 5 frames, not 4: 0 to 2 from above the surface and 3
and 4 from below (looked at in both passes). The demo the references were
recorded from is the second pass's last make (144 messages, `cksum`
1705733582, 14994 bytes), refused by `make-demo` and renamed by hand beside
the binary, as in TASK50, so every number measured from it in this section is indicative and
not acceptance evidence (#232 tracks the fix). The range was not recalibrated: a demo of 141 to
145 messages puts the move under water at about rendered frame 60
(TASK70: 0.44 to 0.45 of the frames), on the third capture, so widening
the range would make the split depend on the make; the holds of
`water.cfg` have to change, which is a change of the script, not of this
card.

**The screen flash (`gl_polyblend`).** The accelerated `gl1` draws the
blend over the whole screen, as `gl1-metal` does. Against the same scenario
with `gl_polyblend 0`, on `world` (`cl_blend 0`, so only the alpha of the
blend) the flash changes frames 3, 5, 8, 11 and 16, and by the same
amount in both renderers: frame 5 score 0.014745 and bad 0.114456 in `gl1`,
0.014745 and 0.114460 in `gl1-metal`; frame 8 0.026542 and 0.673590 in
both; frame 11 0.007494 and 0.008271; frame 16 0.013039 and 0.050732
against 0.013038 and 0.050729; frame 3 0.000779, bad 0. With the flash,
`gl1-metal` passes `world` and `particles` against `gl1` (below), and with
`gl_polyblend 0` the result is the same. Decision: the 3D scenarios keep
the flash (`gl_polyblend` stays at its default 1); a correct reference now
judges it. `hud2d` keeps its `gl_polyblend 0` (TASK82: the clear colour,
not the flash, is what that scenario judges).

**`gl1_multitexture 0`.** On the accelerated `gl1` the classic two-pass
path draws `world` within rounding of the multitexture path: 20 of 23
frames differ by at most 1/255 per channel, worst score 0.000417, bad
0.000007 (frames 17, 18 and 21 hold a few pixels up to 25 apart); the
streaks of the software renderer are gone. `gl1-metal` with
`gl1_multitexture 0` against `gl1` with it: all 23 frames pass at the
defaults in 4 pairs, worst score 0.000011, bad 0.000052 (frame 21), and
`gl1-metal` is exact run to run. Decision: no scenario is added. The two
paths give the same picture to within the rounding the default thresholds
already pass, so a second `world` would pin the same frames; the classic
path is judged with a scratch copy, as here, when its code changes.

**`compare gl1-metal <scenario> --ref gl1`** (commit `de1da7fd`, the
shim of S1 to S6 as merged; three `gl1-metal` records of each 3D
scenario, each against both `gl1` records, 6 pairs; every shim log `mgl:
... 0 without a drawable, 0 skipped, 0 draws past the vertex ring` and no
`not implemented` line):

| Card | Scenario | Accepted at | Frames passing | Worst score, bad (frame) | `gl1-metal` run to run |
|---|---|---|---|---|---|
| TASK49 (S3) | `world` | the defaults | 138 of 138 (23 per pair) | 0.000010, 0.000033 (21) | exact |
| TASK49 (S3) | `world` with `gl1_multitexture 0` (scratch) | the defaults | 92 of 92 (4 pairs) | 0.000011, 0.000052 (21) | exact |
| TASK50 (S4) | `skywater` from `demo1.dm2` | the defaults | 138 of 138 | 0.000010, 0.000033 (21) | exact |
| TASK50 (S4) | `skywater` from `water.dm2` | `--threshold 0.02 --bad-threshold 0.03` | 30 of 30; also all at the defaults | 0.000004, 0.000013 | exact |
| TASK51 (S5) | `models` | the defaults | 138 of 138 | 0.000792, 0.001068 (7) | 22 exact, frame 7 at most 0.000608, 0.001038 (3 pairs) |
| TASK51 (S5) | `models` with `r_shadows 1`, `gl1_stencilshadow 1` (scratch, below) | the defaults | 92 of 92 (4 pairs) | 0.000070, 0.000078 (7) | 22 exact, frame 7 0.000070, 0.000078 |
| TASK52 (S6) | `particles` | `--threshold 0.02 --bad-threshold 0.25` | 138 of 138 | 0.010591, 0.151660 | 7 exact, worst 0.011907, 0.168597 (frame 8, 3 pairs) |
| TASK52 (S6) | `particles` with `gl_polyblend 0` (scratch) | the same | 92 of 92 | 0.015115, 0.199079 | 7 exact |

The worst values of `models` and `particles` against `gl1` lie at or just
above the `gl1`/`gl1` sample maximum of the one `gl1` pair of the same
scenario, and far under the threshold: `models` bad 0.001068 against
0.001058 (threshold 0.005); `particles` score 0.010591 against 0.009974
(threshold 0.02), bad 0.151660 against 0.185570 (threshold 0.25); the
`gl_polyblend 0` copy score 0.015115 against 0.015223, bad 0.199079
against 0.187988 (threshold 0.25). These maxima are sample maxima of a
PRNG-driven scenario, not bounds.

**Stencil shadows (`models` with `r_shadows 1` and `gl1_stencilshadow
1`).** Recorded in a second round of the same card, at the same engine
commit `de1da7fd` and on the same host (1-minute load 2.3 to 2.7): a scratch copy of
`models.cfg` with those two lines appended, in a throwaway work tree of
that commit, not committed (scenario `models-stencil` in the store). Two
`gl1` records, then `ref_gl1` rebuilt with `WITH_METAL_SHIM=yes` and two
`gl1-metal` records. All four exited 0 and wrote `complete`, rendered 689
frames (`timedemo.txt`) and captured 23 with no `FAILED` line, as the
`models` records do; the shim logs `mgl: 707 frames, 0 without a drawable,
0 skipped, 0 draws past the vertex ring` and no `not implemented` line. The
shadows are drawn: against the plain `models` record of `gl1`, 19 of 23
frames differ (worst bad 0.008001, frame 19). `gl1`/`gl1` is exact on all
23 frames. `compare gl1-metal models-stencil --ref gl1` exits 0; over all 4
pairs, 92 of 92 frames pass at the defaults, worst score 0.000070 and bad
0.000078 (frame 7); `gl1-metal` run to run 22 exact, frame 7 0.000070 and
0.000078. Met, no finding.

The 2D scenarios against the new references, two
`gl1-metal` records each: `hud2d` and `cinematic` exact on every frame,
`world-fullbright` 92 of 92 at the defaults, worst 0.000031 and 0.000072.

Verdicts: TASK49 (S3) met, with `gl1_multitexture` 1 and 0. TASK50 (S4)
met for `world` and `skywater` from `demo1.dm2`; the sky (frame 21) and
the translucent surface pass. The water half of S4 stays open: the shim
passes all 30 comparisons on the 144-message demo (6 pairs, 5 frames each,
at `--bad-threshold 0.03` and at the defaults, worst score 0.000004, bad
0.000013), but that demo was refused by `make-demo water` and renamed by
hand, so by [TASK50's rule](TASK50.md) the numbers are indicative and not
acceptance evidence. It is accepted when `make-demo water` accepts a demo
on this host, tracked by #232 ("`make-demo water` refuses every demo on a
fast host"). TASK51 (S5) met for `models` and for `models` with
`gl1_stencilshadow 1` (above); the plain-shadow copy of TASK51
(`gl1_stencilshadow 0`) was not recorded again. TASK52 (S6) met at the
`particles` value; frames 5, 8, 11 and 16 (the flash) and 13 pass. No
frame of any of these is over its threshold, so there is no finding on any
of the four cards. The faults TASK49 to TASK52 attributed to
the software renderer (dropped edge polygons, the classic-path streaks,
the wedge-shaped flash) do not appear against an accelerated reference.

**`ref_metal` against the new references** (two `metal` records of each
scenario it draws at `de1da7fd`, `world` with lightmaps since N5; each against
both `gl1` records, 4 pairs; every log `0 live objects`): `hud2d` and
`cinematic` exact on every frame; `world` frames 0 to 18 (the frames N4 and
N5 judge) worst 0.000008 and 0.000016, frames 19, 20 and 22 pass, frame 21
over at score 0.049744 and bad 0.264007; `world-fullbright` frames 0 to 18
worst 0.000031 and 0.000072, frame 21 over at 0.055007 and 0.266637.
Frame 21 holds the sky and the translucent surface that `ref_metal` skips
until N6 (the exclusion rule of N4), so it was that card's work, not a
fault. N6 (TASK89) has since merged; frame 21 was not measured against
the accelerated references after it. `metal` is exact run to run on all four.
