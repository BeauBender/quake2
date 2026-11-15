# Particles, the parity sweep and frame time in `ref_metal` (TASK92, N9)

References: `gl1-metal` records at the card's base, `e5fb3969`, in a private store, unseeded
without a trace and seeded (`PINS_SEED=1`) with one. The scratch copies `particles-pp0` and
`particles-sq` are `particles.cfg` plus `set gl1_pointparameters 0` or `set gl1_particle_square 1`,
put in `scripts/pins/scenarios/` for the records and deleted after (never committed):

```sh
make client game ref_metal && make ref_gl1 WITH_METAL_SHIM=yes
export PINS_STORE=<private store> PINS_BASEDIR=<game data>
PINS_SEED=1 MTL_DEBUG_LAYER=1 scripts/pins.sh record gl1-metal particles   # and --run 2; the same for the two copies
PINS_SEED=1 MTL_DEBUG_LAYER=1 scripts/pins.sh record metal particles
PINS_SEED=1 scripts/pins.sh compare metal particles --ref gl1-metal --ref-id <each reference> --threshold 0.005 --bad-threshold 0.001
# the control: PINS_BIN=<an engine copy beside the base's ref_metal.dylib>, record metal, compare with --cand-id
# every other scenario as in N8 (models and skywater from water.dm2 against two records)
```

The sweep, one source (`77c46ced` plus the change, pin id `77c46ced6e72-d3752008510`; that source is the one of commit
`786790e9`: `git diff 77c46ced 786790e9 -- src Makefile | cksum` gives 3752008510), 17:58 to
17:59 local, every record with the validation layer, every `engine.log` holding only `Metal API
Validation Enabled` and, for `metal`, `teardown: 0 live objects`, `0 unreleased`, `0 unmatched`:

| Scenario | Value (design section 11) | `metal` against each reference |
|---|---|---|
| `hud2d` | 0.005, 0.001 | 23 of 23, score 0, bad 0 |
| `cinematic` (`idlog.cin`, every 7th) | 0.005, 0.001 | 12 of 12, 0, 0 |
| `world` | 0.005, 0.001 | 23 of 23, 0, 0 |
| `world-fullbright` | 0.005, 0.001 | 23 of 23, score 0, bad at most 0.000013 |
| `skywater`, `demo1.dm2` | 0.005, 0.001 | 23 of 23, 0, 0 |
| `skywater`, `water.dm2`, two records | 0.02, 0.03 | 4 of 4 each, 0, 0 |
| `models`, two records | 0.02, 0.005 | 23 of 23 each (worst 0.000728, bad 0.001019; 0.000457, 0.001051); at 0.005, 0.001 22 of 23 each, frame 7 (the PRNG's explosion; the references differ there by 0.000312, bad 0.000456) |
| `models` seeded, two records | 0.005, 0.001 | 23 of 23 each, worst 0.000039, bad 0.000589 |
| `dlights` seeded, `demo1.dm2`, two records | 0.005, 0.001 | 23 of 23 each, 0, 0 |
| `dlights` seeded, `demo2.dm2` every 7th, two records | 0.005, 0.001 | 89 of 89 each, worst 0.000001, bad 0.000013 |
| `particles` seeded, two records | 0.005, 0.001 | 23 of 23 each, worst 0.000039, bad 0.000589 (frame 2, N7's residue) |
| `particles-pp0` seeded, two records | 0.005, 0.001 | 23 of 23 each, the same |
| `particles-sq` seeded, two records | 0.005, 0.001 | 23 of 23 each, the same |
| `particles` unseeded, two records | 0.02, 0.25 (the card's) | 23 of 23 each (worst 0.009542, bad 0.148773; 0.009659, 0.141947); at 0.005, 0.001 14 of 23 over each; the two references differ by up to 0.010331, bad 0.174274 |
| control: the base's `ref_metal` (no particles), seeded | 0.005, 0.001 | over on 11 (`particles`), 10 (`-pp0`) and 12 (`-sq`) of 23 against each reference, worst 0.009926, 0.006995, 0.011398; at 0.02, 0.25 all 23 pass |
| `gl1-metal` seeded record 2 against record 1 | 0, 0 | `particles`, `-pp0`, `-sq`: 23 of 23 byte-identical |

The two copies are drawn differently from `particles`: `metal` frames of each differ from the
`metal` `particles` frames on 16 of 23.

`metal_stats 1` (a hand run with the seeded record's command line, a scratch `pins_dir`, `+set
metal_stats 1`, the validation layer), `particles` and both copies: 23 frame lines and the one at
shutdown, every one `0 draws skipped`, `0 failed command buffers`, `24 pipelines, 0 created after
init`, `0 particles refused`, no surface, light or entity skipped; teardown `0 live objects`.

The particle texture: a config beside the binary (deleted after) with `map base1`, `imagelist`,
`set gl1_particle_square 1`, `imagelist`, `map base2`, `imagelist`, `vid_restart`, `imagelist`,
`set gl1_particle_square 0`, `map base1`, `imagelist`, `quit`, run with `+set gl1_pointparameters
0` and with `1` under the validation layer: `***particle***` in every list, two teardowns at `0
live objects` per run, every `metal_stats` line `24 pipelines, 0 created after init`.

The frame time: the record's command line (`PINS_DRYRUN=1`, unseeded, no trace), a scratch
`pins_dir`, no validation layer, `+set r_vsync 0` for both renderers (with the default the display
held both to 60 fps while it was in use), `metal` then `gl1-metal`, three rounds per scenario; the
`fps` of `timedemo.txt`'s line:

| Scenario | `metal` fps | `gl1-metal` fps |
|---|---|---|
| `hud2d` | 1398 to 1555 | 1097 to 1213 |
| `world` | 1281 to 1528 | 799 to 982 |
| `world-fullbright` | 987 to 1338 | 696 to 944 |
| `skywater`, `demo1.dm2` | 1285 to 1398 | 816 to 984 |
| `skywater`, `water.dm2` (93 frames) | 894 to 1453 | 750 to 869 |
| `models` | 823 to 1065 | 668 to 709 |
| `dlights`, `demo1.dm2` | 873 to 1102 | 632 to 777 |
| `dlights`, `demo2.dm2` every 7th | 411 to 804 | 385 to 553 |
| `particles` | 767 to 942 | 537 to 581 |
| `particles-pp0` | 770 to 968 | 485 to 629 |
| `particles-sq` | 954 to 1139 | 552 to 679 |
| `cinematic` | 6.0 s each (the cinematic's own clock; no frame rate) | 6.0 s each |

One-minute load 7.8 to 13.4 (the cinematic rows 11.9 to 25.1). `metal` was faster in each of the
33 pairs. A first set at a load of 13 to 63 (an iOS simulator and other work on the host): `metal`
222 to 334 and `gl1-metal` 125 to 309 fps, `metal` slower in 11 of 30 pairs (`hud2d` 2, `world` 1,
`skywater` 1, `skywater` from `water.dm2` 3, `models` 2, `dlights` 1, `particles-sq` 1), by up to
24 percent (`water.dm2`, 0.5 s runs); at that load the two ranges overlap on every scenario.
