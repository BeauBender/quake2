# The world without lightmaps in `ref_metal` (TASK87, N4)

N4 is judged on the new scenario `world-fullbright` (`world` plus
`r_fullbright 1`), with `hud2d` and `cinematic` as the regressions
(`docs/plans/ref-metal-native.md`, section 19). The references are
`gl1-metal` records at the card's own commit, in a private store, without
traces:

```sh
make client game ref_metal && make ref_gl1 WITH_METAL_SHIM=yes
export PINS_STORE=<private store> PINS_BASEDIR=<game data> PINS_TRACE=0
MTL_DEBUG_LAYER=1 scripts/pins.sh record gl1-metal world-fullbright [--run 2]
scripts/pins.sh compare gl1-metal world-fullbright --ref gl1-metal --run 2 --threshold 0 --bad-threshold 0
MTL_DEBUG_LAYER=1 scripts/pins.sh record metal world-fullbright [--run 2]
scripts/pins.sh compare metal world-fullbright --ref gl1-metal [--run 2] --threshold 0.005 --bad-threshold 0.001
make ASAN=1 client ref_metal game
PINS_BIN=debug/quake2 MTL_DEBUG_LAYER=1 scripts/pins.sh record metal world-fullbright
scripts/pins.sh compare metal world-fullbright --ref gl1-metal --cand-id <the ASAN record's id> ...
# hud2d as in N3; cinematic as in N2 (PINS_DEMO=idlog.cin PINS_EVERY=7)
```

A header edit (`header/rmb.h`, `header/local.h`) leaves `release/quake2`
and the shim's `ref_gl1.dylib` older than a header under `src/`, which
`record` refuses; `make -B` of the client and of the shim build is the
plain way, as in N2.

Which frames are judged: until N6 the world has no sky, warped,
translucent or flowing surfaces, so a frame on which `metal_stats 1`
reports one of them skipped is excluded (design section 11). The counts
are read from the engine started by hand with `PINS_DRYRUN=1`'s command
line plus `+set metal_stats 1` and a scratch `pins_dir`, as in N2: each
captured frame prints one line ending in `skipped surfaces: N sky, N
warped, N translucent, N flowing`, the Nth line belonging to frame N - 1.
The lines are printed to the console, so the hand-run frames carry them
as notify text in their top rows; compare only the records. On
`demo1.dm2` (one map, `base2`) frames 19 to 22 are excluded (sky and
translucent surfaces); frames 0 to 18 skip none.

A record is accepted on the conditions of N2. With the world drawn, the
screen blend becomes visible: the scenarios leave `gl_polyblend` at 1, and
a scratch run of `metal` with `+set gl_polyblend 0` puts frames 5, 8, 11
and 16 over the threshold (bad 0.84 to 0.98) and frame 3 at score
0.004944, and compared pixel by pixel with a `metal` record of the same
build only those five differ, so the blend's quad is judged on 5 of the 19
judged frames (3, 5, 8, 11, 16).

On the build VM, source `04c99db2`, load averages 2.7 to 10.9: two
`gl1-metal` records identical (score 0, bad 0 on 23 frames); three release
records and one `ASAN=1` record of `metal world-fullbright` with the same
numbers, frames 0 to 20 ok (15 of the 19 judged frames at 0, four with 2
to 6 differing pixels, bad at most 0.000013: with `gl_texturemode
GL_NEAREST` and with `GL_LINEAR` both renderers match on every judged
frame, so the cause is the mip level chosen at a transition under
`GL_LINEAR_MIPMAP_NEAREST`, section 19), frames 21 and 22 over and
excluded (the sky and the translucent water, N6's); `hud2d` 23 and
`cinematic` 12 frames at score 0 and bad 0; every log holding only
`Metal API Validation Enabled` and `0 live objects`; every captured frame
`14 pipelines, 0 created after init`.
