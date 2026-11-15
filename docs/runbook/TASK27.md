# Closing TASK27 on the build host

The umbrella card's acceptance list, checked end to end after P1 to P3
(TASK39 to TASK41) and R1 to R5 (TASK42 to TASK46), at engine commit
`4175e8cc` on the build VM (macOS arm64). `gl1` there is Apple Software
Renderer (`GL_RENDERER` in every `gl1` `engine.log`); `gl1-metal` is the
shim on `Apple Paravirtual device`. Every record, replay and compare below
ran in a private store; the owner's store was only read (`pins.sh list` and
reading its files), nothing was recorded, replayed or written there. The
1-minute load average was 6.5 to 59 over the builds and runs (other work
shared the machine). No threshold, scenario or source file was changed.

**Criterion 1: a renderer replaying its own records reproduces its own
frames bit-identically.** Met with a limit: replay is exact on all three
renderers; the `soft` references of `models`, `particles` and `skywater`
from `demo1.dm2` in the owner's store are at an older settings hash, and
recording new ones is the owner's step (commands given below). For each renderer and scenario run: one live
record (`record <r> <scenario>`), one replay of that record's own trace
(`replay <r> <scenario> --ref-id <record>`), and `compare <r> <scenario>
--cand-id <replay> --ref <r> --ref-id <record> --threshold 0
--bad-threshold 0`. No `PINS_SEED`: the replay draws what the trace holds,
so `models` and `particles` are exact without it. `skywater` from
`water.dm2` uses a demo made with `make-demo water` in this checkout (98
messages), so its pin id is not the owner's.

| Scenario | Frames | `soft` | `gl1-metal` | `gl1` (Apple Software Renderer) |
|---|---|---|---|---|
| `world` | 23 | 0 differ, worst 0 | 0 differ, worst 0 | 0 differ, worst 0 |
| `hud2d` | 23 | 0, 0 | 0, 0 | 0, 0 |
| `models` | 23 | 0, 0 | 0, 0 | 0, 0 |
| `particles` | 23 | 0, 0 | 0, 0 | 0, 0 |
| `skywater`, `demo1.dm2` | 23 | 0, 0 | 0, 0 | 0, 0 |
| `skywater`, `water.dm2` | 4 | 0, 0 | 0, 0 | 0, 0 |
| `cinematic`, `idlog.cin`, every 7th | 12 | 0, 0 | 0, 0 | 0, 0 |

21 replays, all complete with exactly the live record's frame count, and
every compare exited 0 with `failed=0 worst=0 worst_bad=0`. A record took 2
to 9 s on `soft` and `gl1-metal` and 2 to 18 s on `gl1`, a replay 2 to 10 s
and 3 to 15 s (load 9.9 to 31.1 during these runs). The build was the
default one, so every replay manifest says `game-module present`; the
absent case is TASK46's measurement (`doc/100_renderpins.md`, *Replaying a
trace*), not repeated here.

The head's `soft` frames against the owner's `soft` references, copied into
the private store (`compare --ref-id`, both thresholds 0): `world`
(reference `790c2212f654`), `hud2d` (`74205ac99199`), `cinematic`
(`f40e8ae864e4`) and `skywater` from `demo1.dm2` (`f40e8ae864e4`, recorded
before TASK70 changed the scenario file) exact on every frame; `models`
(`e30ea0e1e161`) 1 of 23 frames differs, score 0.000036, and `particles`
(`e30ea0e1e161`) 16 of 23, worst score 0.011378 and bad fraction 0.156693,
the unseeded run-to-run noise these two scenarios are recorded with, inside
their `// nondeterministic:` thresholds.

**The `ref_soft` references in the owner's store.** Complete live records,
per scenario, and how many carry the settings hash the head computes (the
hash covers the whole scenario file, comment lines included, and the
`PINS_*` settings; a record of another hash is still comparable with
`--ref-id`, but `compare` without it may pick a record of other settings):

| Scenario | `soft`: complete, at the head's hash (commits) | `gl1`: complete, at the head's hash (commits) | `gl1-metal` |
|---|---|---|---|
| `world` | 19; 19 (11 commits, newest `92302e46596c` with uncommitted changes, `790c2212f654` the newest clean) | 2; 2 (`110a25ae2d26`, `92302e46596c`) | 10 |
| `hud2d` | 12; 3 (`74205ac99199`) | 5; 4 (`74205ac99199`) | 14 |
| `models` | 8; 0 (newest `e30ea0e1e161`, before TASK45) | 1; 0 (`110a25ae2d26`, before TASK45) | 0 |
| `particles` | 8; 0 (newest `e30ea0e1e161`, before TASK45) | 1; 0 (`110a25ae2d26`, before TASK45) | 0 |
| `skywater`, `demo1.dm2` | 4 (`f40e8ae864e4`, `63fff16ad3ac`, both before TASK70); 0 at the head's hash | 2; 2 (`110a25ae2d26`, `92302e46596c`) | 5 |
| `skywater`, `water.dm2` | 2 (`92302e46596c`, the same demo as the `gl1` records of that commit) | 3 (2 of `92302e46596c`, 1 of `110a25ae2d26`, two makes of the demo) | 5 |
| `cinematic` | 8; 2 (`f40e8ae864e4`) | 1; 1 (`110a25ae2d26`) | 8 |

The other 14 complete `skywater` records of `soft` are of other demos,
other `PINS_EVERY` values or scenario files before TASK70 (TASK40 and
TASK70 measurements). No record there is of the head commit, as expected: references are made
once per engine commit. `models` and `particles` changed only in comment
lines in TASK45, and `skywater` in TASK70, so those references are of an
older settings hash; their frames still match the head's as shown above.
`skywater` from `water.dm2` depends on the demo file: the pin id carries its
checksum, and a demo made again (here: 98 messages) is another pin. So
"`ref_soft` references exist for every scenario" holds for the stored
records, with `models`, `particles` and `skywater` from `demo1.dm2` short of
one at the head's hash. That is the owner's step, in the owner's store with
the owner's game data and a default (not `WITH_METAL_SHIM`) build:

```sh
make client game ref_soft ref_gl1
for s in models particles skywater; do scripts/pins.sh record soft $s; done
for s in models particles; do scripts/pins.sh record gl1 $s; done
PINS_SEED=1 scripts/pins.sh record soft models    # optional: exact run to run
PINS_SEED=1 scripts/pins.sh record soft particles
```

with `PINS_STORE` and `PINS_BASEDIR` set to the owner's store and game
data.

**Criterion 2: `ref_gl1` references for every scenario, with the threshold
at which `soft` and `gl1` agree documented per scenario.** Met with a
limit. The owner's store holds a complete `gl1` record of every scenario
(table above), each with `GL_RENDERER: Apple Software Renderer`; `models`
and `particles` only at the settings hash before TASK45. Every scenario has
its row in the table of "`gl1` references and the `soft` against `gl1`
thresholds (TASK41, card P3)": a threshold for `cinematic` (`--threshold
0.02 --bad-threshold 0.35`) and for `skywater` from `water.dm2`
(`--threshold 0.034 --bad-threshold 0.68`; on the accelerated references
re-measured as 0.031 / 0.66, see the TASK83 section), and for `world`, `hud2d`,
`models`, `particles` and `skywater` from `demo1.dm2` the documented
finding that no agreement threshold is meaningful, with the reasons. None
was missing, so none was measured or added. Re-measured at the head, live
`soft` against live `gl1` of the same commit, one record each: `world` and
`skywater` from `demo1.dm2` worst 0.081482 and 0.922676, `hud2d` 0.471755
and 0.998561, the documented worst values exactly; `models` 0.090756 and
0.925937 and `particles` 0.141069 and 0.997926, inside the documented
ranges; `cinematic` worst 0.018680 and 0.322956, 12 of 12 frames passing at
its threshold, the documented values exactly. `skywater` from this
checkout's `water.dm2` (98 messages) is over on 1 of 4 frames at its
threshold (frame 2, score 0.035741, bad fraction 0.812705; the other three
0.030220 to 0.031865, 0.537233 to 0.674004): that threshold was measured on
another make of the demo and holds for that demo, not for any water demo.
It was not changed; a new make of the demo needs its own measurement, and
re-measuring it on the accelerated host is noted on TASK83 (#187).

The limit: these are references for the shim's arithmetic, not for a
graphics card (INFORM9 asks for `gl1` where it renders). Accelerated `gl1`
references are TASK83 (#187).

**Criterion 3: nothing derived from game data is committed.** Met. `git
ls-files` lists 1874 files, 1434 under `third_party/` (vendored SDL 3,
byte-identical to its release, checked by `tests/check-vendored.sh`; no
image, sound or binary data file among them by name) and 440 outside it. Of
those 440, `file` calls five binary or image files, all from upstream yquake2 and
unchanged since the fork point (`git diff 9dbf6992 HEAD` on them is empty);
`file` also labels four JSON configuration files as data, which are text:

| File | Bytes | Type |
|---|---|---|
| `stuff/icon/Quake2.ico` | 162731 | Windows icon |
| `stuff/icon/Quake2.png` | 14043 | PNG, 512x512 |
| `stuff/icon/Quake2.svg` | 5167 | SVG |
| `stuff/models/crosshair/skin.pcx` | 4279 | PCX, 128x128 |
| `stuff/models/crosshair/tris.md2` | 1152 | MD2, 12 triangles |

No path added since the fork point (147 added paths outside `third_party/`,
counted by `git diff --name-only --diff-filter=A --no-renames 9dbf6992 HEAD`) ends in a frame, image, demo, cinematic, trace, pak, map, model,
sound or log extension, or is a `manifest` or `complete` file. The committed
test fixtures are text: the 12 unit-test pins in `tests/pins/unit/` (output
over paks and maps the suite generates itself) and the two lists in
`tests/baselines/`. No trace is committed at all: the synthetic frames,
fake engines and traces of the gate are generated at run time in a
`mktemp -d` directory (`pins.sh selftest`, `evidence-selftest`,
`tests/check-refrecord.sh`). `.gitignore` covers `/release/` (the binary,
`make-demo`'s `demos/`, the engine's `qconsole.log`), `/debug/`, `/build*/`
and `/evidence/`. The store has no ignore pattern because it cannot be in
the tree: `record`, `compare` and `replay` refuse a store inside this
repository or any git work tree, and frames, traces, manifests, engine logs
and the built comparator are written into the store only.

**Criterion 4: `scripts/test.sh` green with and without game data.** Met by
reading and by running the checks; the full gate is the orchestrator's run,
not repeated here. No `tests/check-*.sh` and not `scripts/test.sh` names
the game-data path, `PINS_BASEDIR` or a pin store, except:
`tests/check-pins.sh` and `tests/check-evidence.sh` run `pins.sh selftest`
and `evidence-selftest`, which unset every `PINS_*` variable first and set
`PINS_STORE` to their own scratch directory; `tests/check-pins-latch.sh`
unsets the `PINS_*` settings and passes `PINS_STORE=/s PINS_BASEDIR=/b`
with `PINS_DRYRUN=1` (no engine starts, nothing is read there). All 16
checks were run twice, with `PINS_BASEDIR` and `PINS_STORE` unset and with
both pointing at an empty scratch directory: all 16 exited 0 both times
(2 min 53 s and about 3 min, load 5 to 15), their outputs were identical
once scratch paths were removed, and both scratch directories were still
empty afterwards. So no check's behaviour depends on those variables. The
unit suite (`make test`) takes no `PINS_*` variable and generates its own
data.

**Verdicts.** 1 met with a limit (replay is exact on all three renderers;
the `soft` references of `models`, `particles` and `skywater` from
`demo1.dm2` are at an older settings hash, and recording new ones is the
owner's step, commands given above). 2 met with a limit: software OpenGL
only, accelerated references are TASK83 (#187); the `water.dm2` threshold
is tied to the demo it was measured on; re-measuring it on the accelerated
host is noted on TASK83 (#187). 3 met. 4 met as far as the checks
go; the full gate is the orchestrator's run. Still open from the card's
scope (now held by TASK94, #209), not its acceptance list: the pin store path from configuration
(the driver reads `PINS_STORE` only), a content hash tying a pin to its
scenario beyond the settings hash in its id, and a fixed `cl_maxfps` for
capture runs (neither the driver nor a scenario sets it; `timedemo 1` and
capture every Nth frame carry the determinism measured above).
