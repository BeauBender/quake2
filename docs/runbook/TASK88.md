# Lightmaps in `ref_metal` (TASK88, N5)

N5 is judged on `world` itself (lightmaps on), with a scratch copy of it
at `gl1_overbrightbits 2` and `world-fullbright`, `hud2d` and `cinematic`
as the regressions (`docs/plans/ref-metal-native.md`, section 20). The
references are `gl1-metal` records at the card's own commit, in a private
store, without traces:

```sh
make client game ref_metal && make ref_gl1 WITH_METAL_SHIM=yes
export PINS_STORE=<private store> PINS_BASEDIR=<game data> PINS_TRACE=0
MTL_DEBUG_LAYER=1 scripts/pins.sh record gl1-metal world [--run 2]
scripts/pins.sh compare gl1-metal world --ref gl1-metal --run 2 --threshold 0 --bad-threshold 0
MTL_DEBUG_LAYER=1 scripts/pins.sh record metal world [--run 2]
scripts/pins.sh compare metal world --ref gl1-metal [--run 2] --threshold 0.005 --bad-threshold 0.001
# the overbright copy: world.cfg plus "set gl1_overbrightbits 2" as an
# untracked scenarios/<name>.cfg, both renderers, the same compare; delete it after
# world-fullbright as in N4, hud2d as in N3, cinematic as in N2
make ASAN=1 client ref_metal game
PINS_BIN=debug/quake2 MTL_DEBUG_LAYER=1 scripts/pins.sh record metal world
scripts/pins.sh compare metal world --ref gl1-metal --cand-id <the ASAN record's id> ...
```

The Darwin build has no header dependencies, so after a header edit
remove `build/release/ref_metal` (or `make -B`) before building, or the
objects keep the old declarations.

Which frames are judged: the rule of N4 (a frame on which `metal_stats 1`
reports a skipped sky, warped, translucent or flowing surface is
excluded). N5 also excluded a frame whose line ended in a count above 0
of surfaces `drawn without their dynamic light`; N8 adds the light and
removes that count and that rule (the N8 subsection below). The counts
come from a hand run as in N4. `metal_stats 1` prints its
line into the notify area, so the frames of that hand run differ from a
record's (about 1.5 % of the pixels, every frame): take the counts and
the frames from separate runs, never compare the hand run's frames. On
`demo1.dm2` frames 19 to 22 are excluded (sky and translucent surfaces), none for dynamic lights
(`cl_lights 0`).

`world` on `demo1.dm2` has no visible surface with an animated
lightstyle, so it never exercises the per-frame lightmap updates (the
`uploads` count of `metal_stats` stays at its value after registration).
The evidence for the updates is recorded with `PINS_DEMO=demo2.dm2
PINS_EVERY=7` (the second demo of the game data, whose visible lights
flicker; 89 frames) into another private store, both renderers, and
compared at the same thresholds. Frames 51, 61, 63, 67 and 69 are the
ones that show an animated style: they differ when `metal` runs with
`gl1_dynamic 0` (a scratch copy of `world` plus `set gl1_dynamic 0`,
deleted after) and match `gl1-metal` with the updates on. The correctness
review's numbers, three hand runs: with the updates off 61 bad 0.001130,
67 bad 0.006882, 69 bad 0.004303 (over), 51 and 63 differing under the
thresholds; with them on bad 0.000000, and 0.000013 on 61. Frames 8, 12,
15 and 26 are over on both runs and excluded by the rule (3 to 9 warped
surfaces skipped each). At the default cadence (every 30th frame) no
captured frame of `demo2.dm2` shows an animated style: that run passes
the same with `gl1_dynamic 0`, so it proves nothing about the updates.

A record is accepted on the conditions of N2. The few differing pixels
of the judged frames are N4's mip-level residue; to tell it from a
lightmap difference, run both renderers by hand with `+set
gl_texturemode GL_NEAREST` (and `GL_LINEAR`): the judged frames must then
be identical pixel for pixel.

On the build VM, source `83982904` (a record at `20d47f5c` byte-identical), load averages 3.1 to 11.6: two
`gl1-metal` records of `world` identical; three release records and one
`ASAN=1` record of `metal world` with the same numbers, frames 0 to 20 ok
(0 to 6 differing pixels each, none with either filter above), frames 21
and 22 over and excluded; the overbright copy the same; `demo2.dm2` at the
default cadence about 400 region uploads, every frame the rule leaves ok
(which, as above, does not show the updates); `world-fullbright` as
in N4; `hud2d` 23 and `cinematic` 12 frames at score 0 and bad 0; every
log holding only `Metal API Validation Enabled` and `0 live objects`;
every captured frame `15 pipelines, 0 created after init`.

Re-measured at `5642f91e` (the rework after review) in a private store,
`PINS_TRACE=0`, `MTL_DEBUG_LAYER=1`: `demo2.dm2` with `PINS_EVERY=7`,
`metal` against `gl1-metal`, frames 8, 12, 15 and 26 over and excluded,
the 85 others ok; the control with `gl1_dynamic 0` over on 61 (bad
0.001130), 67 (0.006882) and 69 (0.004303), under the thresholds but
differing on 51 and 63, the same numbers as the review's. Counts from
separate `metal_stats 1` hand runs: 190 uploads at the first capture, 586
at the last with the updates, 193 without. Each `engine.log` holds only
`Metal API Validation Enabled` and the teardown with `0 live objects`.
