# Renderer Frame Pins

The renderer frame pins compare what a renderer draws with what another
renderer, or an earlier build of the same one, drew from the same demo.
A pin is a set of screenshots taken while a demo plays, one every Nth
frame, kept in a local store outside the source tree. `scripts/pins.sh`
records pins and compares them frame by frame. This is a tool for
renderer work: players never need it.

The frames come from the retail game data, so a pin can only be made on
a machine that has that data. Neither the data nor the store (frames,
manifests, logs, traces) is ever committed or uploaded; the store stays
on the machine that made it. The one exception is evidence for a pull
request: a scaled still and a short clip rendered from two pins, which
`scripts/pins.sh evidence-publish` may upload (see *Evidence for a pull
request* below and decision record
[0005](../docs/decisions/0005-pr-evidence-frames.md)). These pins are not the unit-test pins of
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
demo whose camera passes water.

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

## Other commands

* `scripts/pins.sh list`: how many frames each record in the store holds.
* `scripts/pins.sh selftest`: tests the driver and the comparator with
  generated frames and a stand-in engine. It needs no game data and runs
  as part of `scripts/test.sh`.

## Evidence for a pull request

A pull request for a renderer change can show what it does: a
side-by-side still and a short clip per scenario, with the score table.
Both commands need `python3` (standard library only); nothing else is
installed.

```sh
scripts/pins.sh evidence <renderer> <scenario> [--ref <renderer>] [--ref-id <dir>] [--run N | --cand-id <dir>] [--card <ID>]
scripts/pins.sh evidence-publish evidence/<folder>/<folder>-evidence.txt
```

`evidence` runs `compare` with the same options, then writes into
`evidence/<card>-<commit>-<renderer>-<scenario>/` of the checkout (git
ignores it; `PINS_EVIDENCE_DIR` names another place, never inside the
store), where `<commit>` is the engine commit of the candidate pin:

* `...-still.png`: the frame with the worst score, as reference,
  candidate and the absolute difference times 8, side by side, with
  captions.
* `...-clip.png`: an animated PNG of the same three panels over every
  captured frame (at most 48, taken evenly), at half size, each frame
  shown for half a second. The frames are the ones the pin captured,
  every `PINS_EVERY`th rendered frame (30 by default), so the clip is a
  slide show of the demo, not smooth motion.
* `...-clip.gif`: the same clip as a GIF with a fixed palette of 256
  colours, for viewers that do not animate PNG.
* `...-evidence.txt`: the manifest: the command, the commit, the pin ids
  (relative to the store), every frame's score and the size and SHA-256
  of each file. No host name, user name or local path.

Each file must fit `PINS_EVIDENCE_BUDGET` bytes (default 4000000);
a file that does not is scaled down by 2, 3 and so on until it does.
`evidence` writes nothing into the store.

`evidence-publish` uploads the files the manifest lists, and the
manifest, as assets of the pre-release `pr-evidence` of the repository
(`PINS_EVIDENCE_REPO=owner/name`, default the GitHub repository of the
`origin` remote), then prints a Markdown section to paste into the pull
request. It uses `gh`, uploads only into an existing pre-release and
never replaces an asset. It refuses, and uploads nothing, when a file is
not listed in the manifest, its size or digest differs from the
manifest, it is not a PNG or GIF written by `evidence`, it is a symbolic
link or lies outside the manifest's folder, the manifest is outside the
evidence folder, any file holds the host name, the user name or a home
path, or a file is game or store data: `.pak`, `.pk3`, `.dm2`, `.cin`, a
trace (`.q2r`), a `.tga` frame, the store's `manifest.txt` or other store
files. Name single files after the manifest to publish only those.

The section it prints looks like this:

```markdown
### Evidence: `world` on `soft`

Reference `world/soft/<id>`, candidate `world/soft/<id>-r2`; frames=23 failed=0 worst=0 ...

![world: reference, candidate, difference x8, frame world-000000.tga (the worst score)](https://github.com/<owner>/<repo>/releases/download/pr-evidence/<card>-<commit>-soft-world-still.png)

![world: clip, 23 frames at 500 ms each](https://github.com/<owner>/<repo>/releases/download/pr-evidence/<card>-<commit>-soft-world-clip.png)

GIF of the same clip (256 fixed colours): [<card>-<commit>-soft-world-clip.gif](https://github.com/<owner>/<repo>/releases/download/pr-evidence/<card>-<commit>-soft-world-clip.gif)

<details><summary>Scores per frame</summary> ... one row per frame ... </details>

Manifest with the command, the commit, the pin ids and the SHA-256 of every file: [<card>-<commit>-soft-world-evidence.txt](https://github.com/<owner>/<repo>/releases/download/pr-evidence/<card>-<commit>-soft-world-evidence.txt)
```

`scripts/pins.sh evidence --synthetic [--card <ID>]` writes the same
kind of set from a generated test pattern (colour bars, a moving square,
frame numbers) with no game content, to try the publishing path.
`scripts/pins.sh evidence-selftest` checks the encoders and every
refusal with a stand-in uploader; it runs in `scripts/test.sh` as
`tests/check-evidence.sh` and is skipped when `python3` is missing.

## The pin arguments

`record` passes three arguments to the engine, `pins_dir`,
`pins_scenario` and `pins_capture`. They look like cvars but are read
only from the command line; see the *Pin harness* section of the
[List Of All New Console Variables](040_cvarlist.md). Without them the
engine behaves exactly as it does otherwise.
