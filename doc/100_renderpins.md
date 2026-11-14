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
[0007](../docs/decisions/0007-pr-evidence-frames.md)). These pins are not the unit-test pins of
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
* `PINS_SEED`: 1 to 9 digits without a leading zero (`0` itself is
  accepted, `01` is refused: it would be seed 1 under a second pin
  name), a fixed seed for the engine's random
  numbers (the engine's `pins_seed`), applied whenever a map is
  registered, so `models` and `particles` draw the same explosions and
  particles on every record. It needs the trace (`PINS_TRACE=0` is
  refused) and is part of the pin's name. Default: unset, the seed comes
  from the operating system as in a normal game.

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
| `hud2d` | 2D only: status bar, crosshair, console text; the OpenGL renderers draw no screen blend there (`gl_polyblend 0`), the software renderer still darkens the screen on a pickup |
| `skywater` | world surfaces and sky, with the sky and water-warp settings fixed |
| `cinematic` | a cinematic stretched over the screen; `record` needs `PINS_DEMO` naming a `.cin`, and refuses a `.cin` for any other scenario |

`models` and `particles` are not identical from run to run, because
explosions and particles draw on random numbers; their files say so in a
`// nondeterministic:` line, with the thresholds they were measured at.
Recorded with `PINS_SEED` set they are identical from run to run (see
*Determinism* below), and compare at `--threshold 0 --bad-threshold 0`.
The others were identical from run to run, observed on one host with the
software renderer. `skywater` played from `water.dm2` (below) is also
marked nondeterministic: the water warp follows the client's clock, and
in a timedemo that clock follows the host whenever a frame takes longer
than 100 ms, so a record made while the host stalls differs a little on
frames that show the water surface.

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
(`soft`, `gl1`, `gl3`, `gl1-metal` or `metal`, see below), a fixed window size and sound off, plays
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

The engine can replay such a trace into a renderer without running the
game: `pins_replay <trace.q2r>` with `pins_dir` (and no `pins_record`)
feeds the recorded calls to the renderer named by `vid_renderer` at the
recorded size, writes a frame wherever the live run captured one, with
the same names, and quits after the last frame. No server, demo, sound
or input runs, so the game module may be absent. A replay is a pin run
even without `pins_capture`: no configuration file is read or written,
and an error stops the engine. The replay first restores the renderer
cvars the live run had (`r_*`, `gl_*`, `gl1_*`, `gl3_*`, `sw_*`,
`vid_*`, from the trace), so it needs none of the scenario's settings on
its command line; the cvars that choose the renderer, the window or the
display mode are never changed, and nothing is saved. The trace stores
only those renderer cvars, never the player's name, the server address
book or any other client or server setting. `scripts/pins.sh`
does not drive this yet.

### Determinism

A renderer replaying its own trace draws exactly the frames it drew
live: everything it is handed is in the trace, `refdef_t.time` included
(bit-exact, `tests/check-refrecord.sh`), and the renderer cvars are
restored from it. The renderers in this tree draw no random numbers
themselves; the randomness of explosions and particles is the client's,
and the trace records its result. So the replay is identical to the
live run with or without `PINS_SEED`; `PINS_SEED` makes two live runs
identical too.

One caveat: a latched renderer cvar (in this tree only `r_farsee`) is
neither stored in the trace nor restored, so a replay uses the value of
the replaying process. Record and replay with the same `r_farsee`.

Measured (TASK45) on Apple silicon, load average 1.8 to 6.0 while the
runs went, 640x480, every 30th frame of `demo1.dm2` (23 frames;
`cinematic` 3 frames of `idlog.cin`), `framediff` with both thresholds
0. Each live record was replayed twice with no scenario setting on the
replay's command line:

| Comparison | `soft` | `gl1-metal` |
|---|---|---|
| live against replay, `world` `models` `particles` `hud2d` `skywater` `cinematic` | 0.000000 on every frame, every scenario, both replays | 0.000000 on every frame, every scenario, both replays |
| live against replay, `particles` and `models` with `PINS_SEED=1`, 3 records each (gl1-metal: `particles`, 2 records) | 0.000000 on every frame | 0.000000 on every frame |
| live against live, `particles`, no seed | 16 of 23 frames differ, worst score 0.011087, bad fraction 0.110687 | not measured |
| live against live, `models`, no seed | 1 of 23 frames differs, worst score 0.000114 | not measured |
| live against live, `particles` and `models`, `PINS_SEED=1`, every pair of 3 records (soft) or 2 (gl1-metal, `particles`) | 0.000000 on every frame | 0.000000 on every frame |
| live against live, `particles`, `PINS_SEED=1` against `PINS_SEED=2` | 16 of 23 frames differ, worst score 0.010905 | not measured |

Replaying the `hud2d` trace with the cvar restore switched off (a
measurement build only) differed on all 23 frames (worst score
0.126499): the replay drew the world `hud2d` switches off. Plain `gl1`
(`GL_RENDERER` Apple Software Renderer) was measured in the TASK45
review on the same host: `world`, `hud2d`, and `particles` with
`PINS_SEED=1`, each recorded and replayed, 23 of 23 frames identical
each. An earlier run
in which `gl1` could not create an OpenGL context was an environment
condition of that host at the time, not a property of the engine.
A live record taken while the host stalls for more than 100 ms a frame
can still differ from another live record (see `skywater` above); its
replay matches it all the same.

A seeded record (`PINS_SEED`, the engine's `pins_seed`) is for making
pins, not for hosting a server: a listen server started in that process
draws its spawn count from the fixed stream. `pins_record` without
`pins_capture` is not a pin run, so it reads the user's `config.cfg` and
`autoexec.cfg` like a normal game.

A trace is a local development artifact: your own engine records it,
you name it on the command line, and like the rest of the store it is
derived from the game data and never leaves your machine. Replaying a
trace obtained from someone else is not supported. The checks the
replay makes on every call are defence in depth, not a sandbox; the
renderers' own missing bounds checks are tracked on FIX40 (issue #185).

### The Metal shim build of gl1

On macOS `ref_gl1` can be built against an OpenGL subset on Metal that
lives in this tree, instead of Apple's OpenGL framework, for Macs where
OpenGL does not work. The switch is a build variable and needs the
Xcode Metal toolchain (`xcrun metal`):

```sh
make client game ref_gl1 WITH_METAL_SHIM=yes
scripts/pins.sh record gl1-metal world
```

The build writes `ref_gl1.metallib` next to `ref_gl1.dylib`. The
engine still loads it as `gl1`; the pin harness records it as
`gl1-metal`. `record gl1-metal` asks `make` whether the shim build is
current and checks that the module the engine loaded is that build (its
`GL_RENDERER` string ends in `(gl1-metal)`); `record gl1` refuses frames
from it. The shim is being built up step by step: until it draws, a
frame from it is the cleared screen.

With the environment variable `MGL_READBACK_DUMP` set to an absolute
directory (characters `A-Z a-z 0-9 _ . / -`, no `..`), the shim also
writes every `glReadPixels` result there, byte for byte as the renderer
received it (bottom row first), as `mgl-readback-NNNNNN.raw`, with one
line per read in `mgl-readback.txt`. That is the way to check a
captured frame against the shim's own readback. Every file is created
new and no symbolic link is followed: use an empty directory per run, as
one that already holds a `mgl-readback.txt` turns the hook off for that
run. At most 1000 reads are written per run; later reads are not, and
the shim says so once on stderr. Like the frames, these files come from
the game data and stay on the machine that made them.

### The native Metal renderer

`ref_metal` is a renderer module of its own that draws with Metal
directly (design: `docs/plans/ref-metal-native.md`). It is built on
macOS only and is not part of `make`:

```sh
make client game ref_metal
scripts/pins.sh record metal hud2d
```

The engine loads it as `vid_renderer metal` and the pin harness records
it as `metal`. It is being built up step by step: for now every frame is
the cleared screen (with `r_clear 1`, the colour 255, 0, 128 that `gl1`
clears to), and nothing is compared against it yet. At start it prints
the Metal device, the Apple GPU families it supports and whether it
supports ray tracing; it refuses a device older than the apple5 family.
`MTL_DEBUG_LAYER=1` in the environment turns on Metal's validation
layer for a record.

### The gl1 frames on a host without accelerated OpenGL

`ref_gl1` first asks for an accelerated OpenGL context. If the system
refuses it, the renderer tries once more without that requirement and
prints `No accelerated OpenGL context available, using an unaccelerated
one.` in the engine log. On such a host (a macOS virtual machine, for
example) the `gl1` frames come from the system's software OpenGL
renderer, not from a graphics card. The `GL_RENDERER` line of
`engine.log` in the pin's directory tells which one drew them; on the
build host it reads `Apple Software Renderer`. In this mode the renderer
does not wait for the display to sync, as that wait does not end on a
host that never shows the window. A machine where the accelerated
request succeeds is not affected.

### Recording the gl1 references

A reference is a `gl1` record of each scenario, made once per engine
commit on a machine where `ref_gl1` renders:

```sh
for s in world hud2d models particles; do scripts/pins.sh record gl1 $s; done
PINS_DEMO=water.dm2 scripts/pins.sh record gl1 skywater   # after make-demo water
PINS_DEMO=idlog.cin PINS_EVERY=7 scripts/pins.sh record gl1 cinematic
```

To know how much two `gl1` records differ by themselves, record a scenario
a few times (`--run 2`, `--run 3`) and compare them with each other as
shown under *Comparing*. `soft` and `gl1` do not draw the same picture
(palette against true colour, filtering, lighting), so a candidate is
judged against `gl1`, not `soft`. The measured numbers, the host and the
renderer string are in `docs/plans/metal-port-harness.md`.

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
is ignored by git. `make-demo` refuses a binary whose write directory,
or the `demos/` in it, is by its physical path inside `PINS_BASEDIR` or a
part of this repository git does not ignore, and refuses when the demo,
the script copy or the log is a symbolic link.

The one script so far, `water`, loads `base1`, puts the camera over its
large pool and then under the surface, and records `water.dm2`. A run
took 17 to 47 seconds on a shared host (load average 6 to 232). Run it
once:

```sh
scripts/pins.sh make-demo water
PINS_DEMO=water.dm2 scripts/pins.sh record soft skywater
```

A script holds each camera place for a number of console `wait`s, and a
wait lasts at least 16 ms but longer when the host is busy, so the length
of the demo is not fixed; nothing in the engine holds a script for a
number of server frames. Each script therefore states the number of demo
messages it must give, in a `// pins: messages <low> <high>` line, and
`make-demo` fails a demo outside that range and keeps it as
`<name>.dm2.failed`. From a `water.dm2` in range, `skywater` at the
default `PINS_EVERY` 30 shows the pool from just above its surface on the
first half of its frames and from under it on the second half (4 frames,
2 and 2, in every record so far). The range follows from the capture: a
demo of N messages renders N-5 frames and a capture falls on every 30th
rendered frame, so 96 to 125 messages give 4 captures; the stated range,
97 to 124, keeps one message inside each edge. When a run fails for the range, make
the demo again when the host is quieter.

A script may use only console commands that exist in this tree, must
record its own name once, must hold its message range, and must end with
`stop` and `quit`; `make-demo` checks the script before it starts the
engine. The run is bounded by `PINS_TIMEOUT`. It fails at once when the
script's map is not in the game data, and fails when the engine exits
non-zero or the demo is missing, cut short, nearly empty or outside its
range. An existing demo is replaced only with
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
Setting `PINS_EVIDENCE_BUDGET` higher raises that size cap: `evidence`
writes the value into the manifest's `budget` line, and
`evidence-publish` accepts files up to it.
`evidence` writes nothing into the store.

`evidence-publish` uploads the files the manifest lists, and the
manifest, as assets of the pre-release `pr-evidence` of the repository
(`PINS_EVIDENCE_REPO=owner/name`, default the GitHub repository of the
`origin` remote), then prints a Markdown section to paste into the pull
request. It uses `gh`, uploads only into an existing pre-release and
never replaces an asset. All of that holds only while
`PINS_EVIDENCE_UPLOADER` is unset: when it names a program,
`evidence-publish` runs that program as
`<program> <repo> <tag> -- <file>...` in place of the built-in `gh`
upload, including its check that the release exists and is a
pre-release, so nothing is checked on the release. The variable exists
for the self-test's stand-in uploader; leave it unset to publish.
Before uploading it says on stderr where to,
how many files and bytes, and that an uploaded asset is public at once
and cannot be recalled: deleting it later stops further downloads, not
copies already fetched.

It never uploads a file as it found it. It reads each image once, without
following a link, checks those bytes and uploads those same bytes; it
uploads a manifest it writes again from the validated fields, and the
asset names come from the card, commit, renderer and scenario, not from
the manifest. It refuses the whole set, and uploads nothing, when:

* the manifest is not named `<folder>-evidence.txt` inside its set
  folder `<card>-<commit>-<renderer>-<scenario>` (or the synthetic
  equivalent) directly in the evidence folder, is a symbolic link, or
  lies in the store;
* any line of the manifest is not one `evidence` writes, any value does
  not match the pattern `evidence` can write (names, pin ids, numbers;
  no Markdown, no free text), or the values disagree with each other or
  with the names;
* an image's size is over the budget (`PINS_EVIDENCE_BUDGET` or the
  manifest's `budget`, whichever is smaller) or its size or SHA-256
  differs from the manifest;
* an image is not exactly what the encoder writes: for PNG the
  signature, every chunk's CRC, only the chunks the encoder writes
  (`IHDR`, `acTL`, `fcTL`, `IDAT`, `fdAT`, `IEND`), nothing after
  `IEND`, every frame decompressing to the stated size; for GIF the
  header, the fixed palette and loop block, only the frame blocks the
  encoder writes, nothing after the trailer;
* the regenerated manifest or an image holds the host name, the user
  name or a home path;
* a file named after the manifest is not one of the set's files in its
  folder, or any argument starts with `-`.

Game and store data (`.pak`, `.pk3`, `.dm2`, `.cin`, a trace `.q2r`, a
`.tga` frame, the store's `manifest.txt` and other store files) are
refused because no set has such a name and no such file parses as an
image the encoder writes. Name single files after the manifest to
publish only those.

The section it prints looks like this:

```markdown
### Evidence: `world` on `soft`

Reference `world/soft/<id>`, candidate `world/soft/<id>-r2`; `frames=23 failed=0 worst=0 ...`.

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

`record` passes four arguments to the engine, `pins_dir`,
`pins_scenario`, `pins_capture` and `pins_record` (the last one left out
with `PINS_TRACE=0`), and a fifth, `pins_seed`, when `PINS_SEED` is
set. They look like cvars but are read only from the command line; see the *Pin harness* section of the
[List Of All New Console Variables](040_cvarlist.md). Without them the
engine behaves exactly as it does otherwise.
