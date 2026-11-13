# Renderer Frame Pins

The renderer frame pins compare what a renderer draws with what another
renderer, or an earlier build of the same one, drew from the same demo.
A pin is a set of screenshots taken while a demo plays, one every Nth
frame, kept in a local store outside the source tree. `scripts/pins.sh`
records pins and compares them frame by frame. This is a tool for
renderer work: players never need it.

The frames come from the retail game data, so a pin can only be made on
a machine that has that data. Neither the data nor anything made from it
(frames, manifests, logs) is ever committed or uploaded; the store stays
on the machine that made it. These pins are not the unit-test pins of
[Unit Tests, Pins And Coverage](085_unittests.md), which are committed
text files and need no game data.

## What you need

* The client and the renderer to test, built from this tree (`make
  client ref_soft game`, or `make` for all renderers). `record` refuses a
  build that `make -q` reports out of date.
* The game data: a directory that holds `baseq2/` with the `.pak` files
  of the full game.
* A directory for the store, outside this repository and outside any
  other git work tree.

## Environment variables

* `PINS_BASEDIR`: the directory holding `baseq2/` (the game data). It is
  only read; nothing is written into it.
* `PINS_STORE`: the pin store. Default: `~/.quake2-pins`. `record` and
  `compare` refuse a store inside this repository or any git work tree.
* `PINS_DEMO`: what the scenario plays, a file name without a path: a
  demo under `demos/` ending in `.dm2`, or a cinematic under `video/`
  ending in `.cin`. Default: `demo1.dm2`.
* `PINS_TIMEOUT`: seconds after which a run that has not ended is
  killed and marked incomplete. Default: 600.
* `PINS_EVERY`: capture every Nth frame. Default: 30.
* `PINS_WIDTH`, `PINS_HEIGHT`: the window size. Default: 640 x 480.
* `PINS_BIN`: an engine binary other than `release/quake2` of this tree;
  its checksum becomes part of the pin's name, so `compare` needs the
  same value.
* `PINS_TRACE`: `0` records no renderer trace beside the frames (see
  *Recording*). Default: a trace is recorded. It does not change the
  frames and is not part of the pin's name.

The store path, `PINS_BASEDIR`, `PINS_DEMO` and scenario values may hold
only `A-Z a-z 0-9 _ . / -`, except that `PINS_DEMO` is a bare file name
and holds no `/`. The engine joins its command line back together
without quoting.

## Scenarios

A scenario is a file in `scripts/pins/scenarios/` that lists `set <cvar>
<value>` lines. It switches drawing features on and off, so that a
failing pin names the feature. A scenario never names a demo; every
scenario plays `PINS_DEMO`, so all of them follow the same camera path.

| Scenario | What is drawn |
|---|---|
| `world` | world surfaces, lightmaps, sky, water |
| `models` | the world plus entities: models, sprites, beams, the view weapon |
| `particles` | everything, including particles, dynamic lights and the screen blend |
| `hud2d` | 2D only: status bar, crosshair, console text |
| `skywater` | world surfaces and sky, with the sky and water-warp settings fixed |
| `cinematic` | a cinematic stretched over the screen; `record` needs `PINS_DEMO` naming a `.cin`, and refuses a `.cin` for any other scenario |

`models` and `particles` are not identical from run to run, because
explosions and particles draw on random numbers; their files say so in a
`// nondeterministic:` line, with the thresholds they were measured at.
The others were identical from run to run, observed on one host with the
software renderer.

With the stock demos `skywater` shows the sky but no water surface. The
water-warp settings act on the full-view warp while the camera is under
water, not on a water surface seen from above, so pinning water needs a
demo whose camera passes water. `make-demo water` (see *Making a demo*)
makes one; record `skywater` from it with `PINS_DEMO=water.dm2`.

For `cinematic`, set `PINS_DEMO` to a cinematic, for example
`PINS_DEMO=idlog.cin`. A cinematic changes its picture 14 times a
second, whatever the frame rate, so while one plays every Nth cinematic
frame is captured instead of every Nth rendered frame;
`PINS_EVERY=7` gives two frames per second of the cinematic.

## Recording

```sh
scripts/pins.sh record <renderer> <scenario> [--run N] [--replace]
```

`record` starts the engine with the scenario's settings, the renderer
(`soft`, `gl1`, `gl3`), a fixed window size and sound off, plays
`PINS_DEMO` and quits by itself when it ends. The frames land in
`<store>/<scenario>/<renderer>/<id>/`, where `<id>` names the engine
commit and a hash of the scenario and the `PINS_*` settings. With
`--run N` (2 or more) the record is kept next to the first one instead
of replacing it. A complete record is never overwritten unless
`--replace` is given. A record that did not finish (a crash, a timeout,
a demo or cinematic that could not be opened, frames from another
renderer than the one asked for) is marked incomplete and `record`
exits non-zero.

Every `record` also writes `trace.q2r` beside the frames, a trace of
the drawing calls the engine made into the renderer (about 4.4 MB for the
default demo); it holds data derived from the game data, stays in the
store and is never committed or uploaded. `PINS_TRACE=0` records none.

## Comparing

```sh
scripts/pins.sh compare <renderer> <scenario> [--ref <renderer>] [--ref-id <dir>] [--run N | --cand-id <dir>] [--threshold T] [--bad-threshold B]
```

The candidate is the record of this build and these `PINS_*` settings
(run N with `--run`). The reference is the directory `--ref-id` names,
or else the most recently modified complete record of the `--ref`
renderer (default `gl1`) other than the candidate. `compare` prints one line per frame with its mean difference,
the fraction of pixels that differ by more than 8/255, and `ok` or
`OVER`, then a summary with both thresholds (defaults 0.02 and 0.005),
and exits non-zero when any frame is over, missing, or the frame counts
differ.

When a scenario has been recorded with more than one `PINS_DEMO` or
`PINS_EVERY`, name the reference with `--ref-id`: the most recent record
may have been made with other settings and then holds other frames.

To check that a renderer draws the same thing twice:

```sh
scripts/pins.sh record soft world
scripts/pins.sh record soft world --run 2
scripts/pins.sh compare soft world --run 2 --ref soft
```

## Making a demo

```sh
scripts/pins.sh make-demo <name> [--replace]
```

`make-demo` plays the console script `scripts/pins/demos/<name>.cfg` in
a local single-player game, with no network game joined and no frames
captured, and leaves the demo it records as `demos/<name>.dm2` in the
engine's portable write directory, `baseq2/` beside the binary
(`release/baseq2/` for this tree's build). That is where `PINS_DEMO`
finds it: record from it with `PINS_DEMO=<name>.dm2`. The demo is made
from the game data, so like the frames it is never committed; `release/`
is ignored by git, and `make-demo` refuses a binary whose write
directory is inside `PINS_BASEDIR` or a part of this repository git does
not ignore.

The one script so far, `water`, loads `base1`, puts the camera over its
large pool and then under the surface, and records `water.dm2`; it runs
for about half a minute. Run it once:

```sh
scripts/pins.sh make-demo water
PINS_DEMO=water.dm2 scripts/pins.sh record soft skywater
```

A script may use only console commands that exist in this tree, must
record its own name once, and must end with `stop` and `quit`; `make-demo`
checks the script before it starts the engine. The run is bounded by
`PINS_TIMEOUT`. It fails at once when the script's map is not in the game
data, and fails when the engine exits non-zero or the demo is missing, cut
short, or nearly empty. An existing demo is replaced only with
`--replace`. Because a demo made again is not the same file, a demo
beside the binary adds its checksum to the id of every record made from
it, so records of the old and the new demo never share an id. The run
also reads and rewrites `config.cfg` in the write directory, as any
portable start without the pin arguments does.

## Other commands

* `scripts/pins.sh list`: how many frames each record in the store holds.
* `scripts/pins.sh selftest`: tests the driver and the comparator with
  generated frames and a stand-in engine. It needs no game data and runs
  as part of `scripts/test.sh`.

## The pin arguments

`record` passes four arguments to the engine, `pins_dir`,
`pins_scenario`, `pins_capture` and `pins_record` (the last one left out
with `PINS_TRACE=0`). They look like cvars but are read
only from the command line; see the *Pin harness* section of the
[List Of All New Console Variables](040_cvarlist.md). Without them the
engine behaves exactly as it does otherwise.
