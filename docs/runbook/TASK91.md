# Sprites, beams, shadows, dynamic lights and flash blobs in `ref_metal` (TASK91, N8)

N8 is judged on the new scenario `dlights` (`world` plus `cl_lights 1`),
on `models` (complete: sprites, beams and null models drawn) and its
scratch copies with stencil shadows, and on N7's set unchanged
(`docs/plans/ref-metal-native.md`, section 23). References are
`gl1-metal` records in a private store; `dlights` and `models` are
recorded with `PINS_SEED`, which needs the trace (about 4 to 6 MB a
record, so leave `PINS_TRACE` unset):

```sh
make client game ref_metal && make ref_gl1 WITH_METAL_SHIM=yes
export PINS_STORE=<private store> PINS_BASEDIR=<game data> PINS_SEED=1   # one seed for every record
MTL_DEBUG_LAYER=1 scripts/pins.sh record gl1-metal dlights               # and again with --run 2
MTL_DEBUG_LAYER=1 scripts/pins.sh record metal dlights
scripts/pins.sh compare gl1-metal dlights --run 2 --ref gl1-metal --ref-id <the first> --threshold 0 --bad-threshold 0
scripts/pins.sh compare metal dlights --ref gl1-metal --ref-id <each reference> --threshold 0.005 --bad-threshold 0.001
# the same with PINS_DEMO=demo2.dm2 PINS_EVERY=7 (89 frames, the animated lightstyles), and for models
# world, skywater, world-fullbright, hud2d as in N6; cinematic as in N2
# scratch copies, deleted after: models plus "set r_shadows 1" and "set gl1_stencilshadow 1" (and 0),
# dlights plus "set gl1_flashblend 1" (or by hand, below), dlights plus "set gl1_dynamic 0" (the control, recorded seeded)
```

`dlights` is not deterministic unseeded: the muzzle-flash and
explosion lights take a random radius from the engine PRNG (`randk() &
31` in `cl_effects.c`), which is seeded from OS entropy. Unseeded, two
`gl1-metal` records differ on the frames such a light reaches (on
`demo1.dm2` frames 5, 8, 10, 14, 16, 18, 19, 20, 21; on `demo2.dm2`
every 7th, 22 of 89), by up to score 0.009399 and 0.012701, bad
0.176774 and 0.305941 over five records (ten pairs) of each demo, and
`metal` differs from any reference on exactly those frames by as much
as the references differ among themselves. No unseeded threshold
guards the lightstyle update: the accept value first committed,
`--threshold 0.02 --bad-threshold 0.5`, passes the `gl1_dynamic 0`
control below on all five lightstyle frames. So the scenario is
recorded with `PINS_SEED` and judged only seeded, at `--threshold
0.005 --bad-threshold 0.001`; the seed is part of the pin id, so the
reference and the candidate are recorded with the same seed and the
reference is named with `--ref-id`. The first measurement, made
without a trace, fixed the PRNG with a scratch copy of the tree whose
`Qcommon_Init` (`src/common/frame.c`) called `randk_seed_fixed(91)`
instead of `randk_seed()` (its pin ids begin with `nogit`; never
committed); `PINS_SEED` gives the same result without a copy.

Seeded, at `8852cfc9` (`PINS_SEED=1`, every record with
`MTL_DEBUG_LAYER=1` and its trace): the two `gl1-metal` records of
`dlights` were identical byte for byte, 23 of 23 frames of `demo1.dm2`
and 89 of 89 of `demo2.dm2` every 7th, and so were the two of
`models` (23 of 23). `metal` against each of the two references, at
`--threshold 0.005 --bad-threshold 0.001`: `dlights` on `demo1.dm2` 23
of 23 ok, score 0.000000 and bad 0.000000 on every frame (14 frames
not byte-identical, by 5 levels at most); on `demo2.dm2` every 7th 89
of 89 ok, worst score 0.000001 and bad 0.000013 (frames 12, 15 and 61
hold 1 to 4 pixels more than 8 levels off; 42 frames not
byte-identical); `models` 23 of 23 ok, worst score 0.000039, bad
0.000589 (N7's residue).

The animated lightstyles: on `demo2.dm2` at `PINS_EVERY=7` frames 51,
61, 63, 67 and 69 show one (N5) and no dynamic light at all (the
trace's refdef holds none), so they are identical between `gl1-metal`
records with or without a seed. The control is `metal` with
`gl1_dynamic 0`, recorded seeded from a scratch copy of `dlights` plus
that line, against the seeded reference, compared frame by frame with
`scripts/pins/framediff.c` (the two records are of different
scenarios): over at the tight values on 32 of 89 frames: 29 of the 37
frames whose refdef holds a dynamic light (score 0.000492 to 0.062769;
on the other 8 the light lights nothing in view, 3 levels at most), and
three of the five lightstyle frames: 61 (score 0.000105, bad
0.001130: 3,934 pixels differ, by up to 27 levels), 67 (0.000595,
0.006882: 27,473
pixels, up to 24) and 69 (0.000447, 0.004303: 20,850 pixels, up to
32), N5's numbers. Frames 51 and 63 stay under: without the update 184
pixels of 51 (score 0.000001) and 80,449 of 63 (score 0.000467) differ,
each by exactly one level, and the bad fraction counts only pixels
more than 8 levels off. Without the update a lightmap keeps its first
build, every style at 1.0 (`LM_BeginBuildingLightmaps`, as gl1's), and
the styles of the surfaces in view on those two captures light them
within one level of it; `metal` with the update against `metal`
without it gives the same one-level difference (178 and 80,449
pixels), so that is the update's whole effect there, not a renderer
difference. At these values the update is not visible on 51 and 63:
the bad fraction never counts a one-level change, and the scores are
under 0.005 by a factor of ten or more. At the old accept value the
control is over on 13 frames, all with a dynamic light (score 0.020
to 0.063), none of the five.

Which frames are judged: all of them. N5's rule that excluded a frame
whose `metal_stats` line counted surfaces "drawn without their dynamic
light" is gone with the count (the line now ends with the dynamic lights
left out for a float that is not finite or out of bounds, 0 on every
frame of the game's demos); `dlights` and `models` draw no particle, so
only `particles` waits for N9. The counts come from a hand run of
`metal` per scenario, as in N2: `PINS_DRYRUN=1`'s command line of the
record without `+set pins_record trace.q2r`, a scratch `pins_dir`, plus
`+set metal_stats 1`, under `MTL_DEBUG_LAYER=1`. At `9a47f6e0` on
`demo1.dm2` both `models` and `dlights` printed one line per captured
frame, 23 each (and one more at shutdown), every one reading `0 draws
skipped`, `23 pipelines, 0 created after init` and `skipped surfaces: 0
sky, 0 warped, 0 translucent, 0 flowing; 0 dynamic lights left out;
skipped entities: 0 sprites, 0 beams, 0 null models, 0 translucent brush
models, 0 refused`: no frame skips a surface, an entity or a light.

The flash-blob copy needs no scenario file: run the engine by hand with
the seeded command line of `dlights` (`PINS_SEED=1 PINS_DRYRUN=1
scripts/pins.sh record <renderer> dlights`, the trace kept, as the seed
needs it) plus `+set gl1_flashblend 1` and a scratch `pins_dir`, once
for `gl1-metal` and once for `metal`, and compare the frames pairwise
with `framediff` (`cc -O2 -o <scratch>/framediff
scripts/pins/framediff.c -lm`) at `--threshold 0.005 --bad-threshold
0.001`; delete the traces after. At `9a47f6e0`: 23 of 23 ok, score
0.000000 and bad 0.000000 on every frame (5 levels off at most), and the
`metal` frames differ from a seeded `metal` record without
`gl1_flashblend` on the 10 frames that hold a light (score up to
0.045524).

The stencil shadows: the scratch `models` copies with `r_shadows 1` show
the shadows (up to 6,609 pixels of a frame against `models`); with
`gl1_stencilshadow 1` against `0` both renderers change the same number
of pixels on every frame but the PRNG's frame 7, which shows that the
stencil test and the clear to 1 work as on the shim.

Map changes and `vid_restart` under the validation layer, with the
states N8 adds: a config beside the binary (in `release/baseq2/`,
deleted after) holding `set r_shadows 1`, `set gl1_stencilshadow 1`,
`map base1`, `wait 80`, `screenshot`, `vid_restart`, `wait 80`, `set
gl1_flashblend 1`, `map base2`, the same, both off, `map base1`, the
same, `quit`, run with `MTL_DEBUG_LAYER=1 release/quake2 -datadir <game
data> -portable +set vid_renderer metal ... +set metal_stats 1 +exec
<the config>`: four teardowns at `0 live objects`, no validation
message.

The timedemo, `demo1.dm2`: the same engine binary copied into two
scratch directories, one beside `ref_metal` of the card's base and one
beside this card's, the pin harness's command line (`PINS_DRYRUN=1`)
with a scratch `pins_dir`, runs alternated; the frame rate is the line
`689 frames, ... fps` of the log. The display did not pace these runs on
this host (about 1,400 to 1,650 fps), unlike N7's.

Measured on the build VM, sources `28d1c8f8` to `a48d01a8` (the same
code), load averages 4 to 9, and the seeded records at `8852cfc9`: the
numbers of the design document's table in section 23; every
`engine.log` holds only `Metal API Validation Enabled` and, for
`metal`, the teardown with `0 live objects`.
