# Brush entities, alias models and the view weapon in `ref_metal` (TASK90, N7)

N7 is judged on `models` at the tight values (`--threshold 0.005
--bad-threshold 0.001`, stricter than the card's defaults) against two
`gl1-metal` records, with `world`, `skywater`, `world-fullbright`,
`hud2d` and `cinematic` as the regressions, and `skywater` from
`water.dm2` at N6's 0.02 and 0.03 against two `gl1-metal` records, at the
default cadence and with `PINS_EVERY=5` (`docs/plans/ref-metal-native.md`,
section 22). References are `gl1-metal` records in a private store,
without traces:

```sh
make client game ref_metal && make ref_gl1 WITH_METAL_SHIM=yes
export PINS_STORE=<private store> PINS_BASEDIR=<game data> PINS_TRACE=0
MTL_DEBUG_LAYER=1 scripts/pins.sh record gl1-metal models
MTL_DEBUG_LAYER=1 scripts/pins.sh record gl1-metal models --run 2
MTL_DEBUG_LAYER=1 scripts/pins.sh record metal models [--run 2]
scripts/pins.sh compare metal models [--run 2] --ref gl1-metal --ref-id <each reference> --threshold 0.005 --bad-threshold 0.001
# world, skywater, world-fullbright, hud2d as in N6; cinematic as in N2 (PINS_DEMO=idlog.cin PINS_EVERY=7)
make ref_soft && scripts/pins.sh make-demo water   # once, as in N6
PINS_DEMO=water.dm2 scripts/pins.sh record gl1-metal skywater [--run 2]
PINS_DEMO=water.dm2 scripts/pins.sh record metal skywater
PINS_DEMO=water.dm2 scripts/pins.sh compare metal skywater --ref gl1-metal --ref-id <each reference> --threshold 0.02 --bad-threshold 0.03
# the same three with PINS_EVERY=5 as well (19 frames; --cand-id names the record of that cadence)
make ASAN=1 client ref_metal game        # then PINS_BIN=debug/quake2, and --cand-id on the compare
```

A header edit (or a `git checkout` of one) leaves `release/quake2` older
than a header under `src/`, which `record` refuses: remove
`release/quake2` and run `make client`.

Which frames are judged: those on which `metal_stats 1` (a hand run with
`PINS_DRYRUN=1`'s command line, `+set metal_stats 1` and a scratch
`pins_dir`) reports no skipped sprite, beam or null model, and no skipped
surface. On `demo1.dm2` that is every frame: the explosions are alias
models, and the line reads `skipped entities: 0 sprites, 0 beams, 0 null
models, 0 translucent brush models, 0 refused` throughout. Frame 7 holds
the explosion whose yaw and frame come from the engine's PRNG; two
`gl1-metal` records differ there (0.000304, bad 0.000410), and the
comparison passes at the tight values against either record all the
same.

The few differing pixels of every frame (1 to 409) are the mip level
chosen for single pixels of the models. To tell them from a lerp, light
or colour difference, run both renderers by hand with `+set
gl_texturemode GL_NEAREST` and again with `GL_LINEAR`: the frames must
then be identical. To compare the arithmetic itself, print both
renderers' vertices for the same frame (a scratch `fprintf` after
`R_DrawAliasFrameLerp` in `gl1_mesh.c` and after the emit in
`rm_mesh.c`, keyed on `r_framecount`, both reverted after): N7's run
found every position, texture coordinate and colour byte identical.

The brush entities are seen by a scratch build that skips
`R_DrawBrushModel` (in `R_DrawEntity`, reverted after): its frames 0, 9,
11, 13, 16 and 17 differ from the record (doors and panels). The view
weapon's mirror and depth range are judged by a hand run of both
renderers with `+set hand 1 +set gl1_overbrightbits 2 +set r_gunfov 60`.

Map changes and `vid_restart` under the validation layer: a config in the
binary's `baseq2/` (the build's own directory, deleted after) holding
`map base1`, 80 `wait`, `vid_restart`, 80 `wait`, `map base2`, and so on,
ending in `quit`, run with `MTL_DEBUG_LAYER=1 release/quake2 -datadir
<game data> -portable +set vid_renderer metal ... +set metal_stats 1 +exec
<the config>`: each teardown line must read `0 live objects`, and no
validation message may appear.

The timedemo of `demo1.dm2` ran at the display's 120 fps for both
renderers on this host today, whatever `r_vsync` and `vid_maxfps` said,
so before and after were compared by the process's CPU time
(`/usr/bin/time -p`), alternating the two `ref_metal` builds next to the
same engine. That time leaves out the GPU's: what the alias pipelines
cost on the GPU is not measured by it.

Measured on the build VM, sources `41bcf6f2` (the pins) and `c3d3257a`
(the `ASAN=1` record), load averages 3 to 11: the numbers of the design
document's table in section 22; every `engine.log` holds only `Metal API
Validation Enabled` and the teardown with `0 live objects`.
