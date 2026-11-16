# Sky, water, translucent and flowing surfaces in `ref_metal` (TASK89, N6)

N6 is judged on `world` and `skywater` from `demo1.dm2` at the tight values
with no frame excluded (`docs/plans/ref-metal-native.md`, section 21),
`skywater` from `water.dm2` at 0.02 and 0.03, and `world-fullbright`,
`hud2d` and `cinematic` as the regressions. References are `gl1-metal`
records at the card's own commit, in a private store, without traces:

```sh
make client game ref_metal && make ref_gl1 WITH_METAL_SHIM=yes
export PINS_STORE=<private store> PINS_BASEDIR=<game data> PINS_TRACE=0
MTL_DEBUG_LAYER=1 scripts/pins.sh record gl1-metal world
MTL_DEBUG_LAYER=1 scripts/pins.sh record metal world
scripts/pins.sh compare metal world --ref gl1-metal --threshold 0.005 --bad-threshold 0.001
# skywater, world-fullbright and hud2d the same; cinematic as in N2 (PINS_DEMO=idlog.cin PINS_EVERY=7)
make ref_soft && scripts/pins.sh make-demo water   # a demo of 97 to 124 messages
PINS_DEMO=water.dm2 scripts/pins.sh record gl1-metal skywater [--run 2]
PINS_DEMO=water.dm2 scripts/pins.sh record metal skywater
PINS_DEMO=water.dm2 scripts/pins.sh compare metal skywater --ref gl1-metal --threshold 0.02 --bad-threshold 0.03
make ASAN=1 client ref_metal game        # then PINS_BIN=debug/quake2, and --cand-id on the compare
```

A header edit leaves `release/quake2` older than a header under `src/`,
which `record` refuses: remove `release/quake2` and run `make client` (or
`make -B`). `make-demo` needs `ref_soft`. The record picks the latest
reference of a scenario, so a compare after records of another demo names
`--ref-id` (and `--cand-id`) of the one it means. A debug (`ASAN=1`)
engine needs the demo beside it (`debug/baseq2/demos/water.dm2`).

No frame of `world` is excluded now: `metal_stats 1` (a hand run with
`PINS_DRYRUN=1`'s command line plus `+set metal_stats 1` and a scratch
`pins_dir`) reports `0 sky, 0 warped, 0 translucent, 0 flowing` on every
line of `demo1.dm2` and `water.dm2`.

`demo1.dm2` and `demo2.dm2` show no opaque flowing surface, so the scroll is
judged by a scratch experiment that is never committed: a line after
`out->texinfo = loadmodel->texinfo + ti;` in `Mod_LoadFaces` of both
`gl1_model.c` and `rm_model.c` (`if (ti % 5 == 0) out->texinfo->flags |=
SURF_FLOWING;`), both renderers rebuilt and recorded into a scratch store,
the compare as above, the lines reverted. On this host it drew 66,451
flowing surfaces over `demo1.dm2` and `world` and `world-fullbright`
scored 0 on all 23 frames. The first pin of `world` after the code was
written showed what a hand-made comparison would not: frame 21 differed in
the warped water only (0.000660, bad 0.004626); printing both renderers'
coordinates for the same vertices (a `fprintf` in each, reverted) showed the
same indices and half the turbulence table, which gl1's `RI_Init` halves
in place. After that every frame is at 0.

Measured on the build VM, branch commit `316d2b62` (later commits add only host tests, comments, docs and the review fixes named in section 21), load averages 14 to 28, every
record with `MTL_DEBUG_LAYER=1`: the numbers of the design document's
table in section 21; every `engine.log` holds only `Metal API Validation
Enabled` and the teardown with `0 live objects`.
