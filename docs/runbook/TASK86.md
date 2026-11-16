# Pics, characters and the HUD in `ref_metal` (TASK86, N3)

N3 is judged on `hud2d`, the first non-uniform scenario compared with the
shim, with `cinematic` as the regression. The references are `gl1-metal`
records made at the card's own commit (the 2D code is `ref_metal`'s;
nothing the shim links changed). Both modules stay built side by side;
a private store keeps the 1 GB traces out of the owner's:

```sh
make client game ref_metal && make ref_gl1 WITH_METAL_SHIM=yes
export PINS_STORE=<private store> PINS_BASEDIR=<game data>
MTL_DEBUG_LAYER=1 scripts/pins.sh record gl1-metal hud2d
MTL_DEBUG_LAYER=1 scripts/pins.sh record metal hud2d [--run 2]
scripts/pins.sh compare metal hud2d --ref gl1-metal [--run 2] --threshold 0.005 --bad-threshold 0.001
# cinematic as in N2: PINS_DEMO=idlog.cin PINS_EVERY=7, the same three commands
make ASAN=1 client ref_metal game
PINS_BIN=debug/quake2 PINS_TRACE=0 MTL_DEBUG_LAYER=1 scripts/pins.sh record metal hud2d
scripts/pins.sh compare metal hud2d --ref gl1-metal --cand-id <the ASAN record's id> ...
```

The `cinematic` trace is 377 MB from `gl1-metal` and 738 MB from `metal`:
delete the `trace.q2r` files when the records are accepted (`PINS_TRACE=0`
records none; the pins need no trace to compare). The `ASAN=1` pin id
carries the binary's checksum (`-b...`), so its compare names it with
`--cand-id`. `metal_stats 1` is read from the engine started by hand with
`PINS_DRYRUN=1`'s command line plus `+set metal_stats 1` and a scratch
`pins_dir`, as in N2: each captured frame prints one line, and the
accepted ones read `12 pipelines, 0 created after init`.

A record is accepted on the conditions of N2. `hud2d` draws the status
bar, the crosshair and the numbers over the `r_clear` colour, so a frame
that is one colour (N1's) or whose HUD sits in the wrong row order shows
as a large score; a frame differing in 2 of 23 appears when the batched
quads are not drawn before the frame ends (the mutant `RM_Flush2D`
removed from `EndFrame`: worst score 0.0134, worst bad 0.0315).

On the build VM, source `308d8073`, load averages 17.7 to 26.1: three
release records and one `ASAN=1` record of `metal hud2d`, each 23 frames
ok at score 0 and bad 0; `metal cinematic` 12 frames ok at score 0 and
bad 0 (one record); four `hud2d` logs holding only `Metal API Validation
Enabled` and `0 live objects`, `0 unreleased`; every captured `hud2d`
frame `0 created after init`.
