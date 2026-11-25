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
  particles, and `dlights` the same lights, on every record. It needs the trace (`PINS_TRACE=0` is
  refused) and is part of the pin's name. Default: unset, the seed comes
  from the operating system as in a normal game.
* `PINS_HIDPI`: `1` runs the engine with `vid_highdpiaware 1`, so on a
  display of pixel density 2 (a Retina display) the `PINS_WIDTH` x
  `PINS_HEIGHT` window draws and captures twice that size in pixels;
  `PINS_HIDPI=1` is part of the pin's name. Default: unset (or `0`), the
  engine runs with `vid_highdpiaware 0` and captures `PINS_WIDTH` x
  `PINS_HEIGHT` pixels on any display, and a pin keeps the name it had
  before this setting existed. Any other value is refused. `record`
  writes the value as `hidpi 0` or `hidpi 1` into `settings.txt` beside
  the frames, `replay` into its `replay.txt`. `replay` refuses
  `PINS_HIDPI=1`: a trace is replayed at `PINS_WIDTH` x `PINS_HEIGHT`.
  A scenario may not set `vid_highdpiaware`. Without `--ref-id`, `compare`
  takes the newest complete pin of the reference renderer whatever its
  `PINS_HIDPI`, as it does whatever its `PINS_WIDTH`; pass `--ref-id` to
  compare a pin with one of the other value.

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
| `world-fullbright` | `world` with `r_fullbright 1`: world surfaces without lightmaps, sky, water |
| `dlights` | `world` with `cl_lights 1`: the dynamic lights on the world's lightmaps (with `gl1_flashblend 1`, the flash blobs instead) |
| `world-lmcubic` | `world` with `metal_lightmap_filter 1`: the cubic lightmap filter of the Metal renderer (the others draw it as `world`) |
| `dlights-lmcubic` | `dlights` with `metal_lightmap_filter 1`; recorded with `PINS_SEED` like `dlights` |
| `hud2d-sharp` | `hud2d` with `metal_2d_sharp 1`: the sharp-bilinear sample of the 2D pictures of the Metal renderer (the others draw it as `hud2d`). At the harness's default 640x480 the 2D scale is 1, so it draws exactly `hud2d`; it tests the switch only at another size: `PINS_WIDTH=2560 PINS_HEIGHT=1440 scripts/pins.sh record metal hud2d-sharp`, judged against `hud2d` at that size, from which it must differ |
| `hud2d-scale8` | `hud2d` with `r_scale8bittextures 1` |
| `world-scale8` | `world` with `r_scale8bittextures 1`: the 8-bit world textures scaled up and smoothed |
| `world-metalfx1`, `world-metalfx2`, `world-metalfx3` | `world` with `r_metalfx` 1, 2 or 3 at `r_metalfx_scale 0.5`: the 3D view drawn at half size and upscaled by a MetalFX scaler (the others draw it as `world`). Where the GPU lacks a mode it falls back to the best one below, so on a GPU with only the spatial scaler all three draw `world-metalfx1`'s frames |
| `world-metalfx-interp` | `world` with `r_metalfx_interp 1`; frame generation is not built yet, so it draws `world`'s frames on every GPU |
| `rt-sun` | `world` with `r_rt_sun 1`: the Metal renderer's ray-traced sun and sky shadow, new light on the world's dim surfaces where a ray toward the sun reaches the sky (the others draw it as `world`, and so does the Metal renderer on a map whose sky is under 2 % of its face area or on a GPU without ray tracing); its pin is recorded once the owner accepts the look (TASK56) |
| `particles-cap` | `particles` with `metal_particle_cap 1`: the particle size limit of the Metal renderer scaled by `height / 480` (the others draw it as `particles`); recorded with `PINS_SEED` like `particles` |
| `retro` | `world` with `r_presentation 2`: the Metal renderer's Retro post-process, the 3D view in RGB565 with the 4 x 4 ordered dither, no filter and no curve, and since TASK185 the 16-bit textures, the 256 cap and, above 480 lines, the 480-line view (the others draw it as `world`); judged against the Metal renderer itself once the owner accepts the look (TASK184) |
| `retro-filter` | `retro` with `r_retro_filter 1`: the 2 x 2 box output filter over the dithered pixels (TASK184) |
| `rtprimary-world` | `world` with `r_presentation 3`: the Metal renderer's Ray traced presentation, the lightmap off at the visible world surface and the direct light of the map's light entities in its place, the 16 visible lights a pixel, the exposure fitted once at the map's load from a sample of its luxels, with no temporal accumulation (`r_rt_temporal 0`, set by every RT-primary scenario, TASK190) (the others draw it as `world`); deterministic, so a pin of the Metal renderer itself, judged byte for byte against the run before, provisional until the owner accepts the look (PM11), when it is recorded again and compared at 0.02 (`pins.sh compare`'s default `--threshold`; a scenario file cannot carry one), also with `PINS_DEMO=demo2.dm2 PINS_EVERY=7` for the lightstyles (TASK188) |
| `rtprimary-direct`, `rtprimary-vis`, `rtprimary-noshadow` | `rtprimary-world` with `r_rt_direct_show` 1, 2 or 3: the direct term alone, the shadow visibility, and the direct term with no shadow ray; none of the three traces or adds the sky light or the sun, which only the picture adds (TASK188, TASK189) |
| `rtprimary-sky`, `rtprimary-sun`, `rtprimary-bounce` | `rtprimary-world` with `r_rt_direct_show` 5, 6 or 7: the sky light's term alone, the sun's term alone and the bounce alone (the stock lightmap read at its hit, TASK189 slice 2), one ray a pixel each; every RT-primary scenario sets `r_rt_direct_seed 1`, so the sky's and the bounce's noise is the same run to run (TASK189) |
| `rtprimary-dlights` | `rtprimary-world` with `cl_lights 1`: the dynamic lights as a term of the Ray traced presentation, one shadow ray each (the others draw it as `dlights`); recorded with `PINS_SEED` like `dlights`, and with `PINS_DEMO=demo2.dm2 PINS_EVERY=7`; provisional as `rtprimary-world` (TASK188) |
| `hdr` | `world` with `r_hdr 1`: the Metal renderer's float16 frame, read back as its SDR fallback (clamped to white, in 8 bits); its own reference, not compared with `world` (the others draw it as `world`) (TASK196) |
| `hdr-metalfx2` | `hdr` with `r_metalfx 2` at `r_metalfx_scale 0.5`: the Metal renderer's float16 view upscaled by `MTLFXTemporalScaler`, before the tone map; its own reference, not compared with `hdr` or `world-metalfx2` (the others draw it as `world`) (TASK196, HD4) |
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
`dlights` is not identical from run to run either unless seeded: the
muzzle-flash and explosion lights take their radii from the same random
numbers, and two unseeded records differ by more than a missing
lightstyle update changes a frame, so no unseeded threshold can judge
it. It is recorded with `PINS_SEED`, the same seed for the reference and
the candidate, the reference named with `--ref-id`, and judged only
seeded; its file gives the values it is judged at and, for information,
the unseeded run-to-run values, at which no record is judged. Record it
also with `PINS_DEMO=demo2.dm2 PINS_EVERY=7`, whose frames show animated
lightstyles. `particles` is judged the same way, seeded only, at
`--threshold 0.005 --bad-threshold 0.001`: at its unseeded run-to-run
values (`--threshold 0.02 --bad-threshold 0.25`, kept in its file as
data) a build that draws no particles at all passes every frame. The others were identical from run to run, observed on one
host with the software renderer (`world-fullbright` with the Metal shim
build of gl1). `skywater` played from `water.dm2` (below) is also
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
book or any other client or server setting. `scripts/pins.sh replay`
drives it (see *Replaying a trace* below).

### Input and the rendered-frame count

A record is made in a normal window, often on a desktop in use. Before
FIX48 a key pressed in it opened the menu, which pauses the local server
playing the demo while the client keeps rendering: the engine still
exited 0 and logged nothing unusual, and the record held other frames
(TASK83 saw 9560, 794 and 1064 rendered frames where `demo1.dm2` renders
689). Two things now keep such a record out of the store:

- **Input is ignored in a pin run.** While the command line holds a pin
  run (`pins_capture` or `pins_replay`, the latch of *The pin arguments*,
  never a cvar), no key, mouse button, wheel step, gamepad button or
  typed character reaches the console, the menu or a key binding, so
  `Esc`, the console key and a key bound to `quit` do nothing, and the
  mouse, the sticks and the gyro do not move the view. A focus change
  does not pause the game either, and a gamepad pulled out or plugged
  in, a low gamepad battery, or on iOS a rotation does nothing: no pause,
  no message on the screen, no `vid_restart` (a message printed during a
  record is drawn into its frames). `tests/check-input-gate.sh` keeps
  these cases gated. The window's close button still quits
  the engine, so a run can be stopped. Without the pin arguments input
  reaches the game exactly as before. A replay runs no input at all.
- **The rendered-frame count is checked.** A scenario's header comment
  holds `// frames <demo> <count>`: the number of frames the engine
  renders when it plays that demo, as printed by the timedemo
  (`timedemo.txt`, the line `689 frames, ... fps`). It does not depend on
  the renderer or the scenario's other settings, so every scenario that
  plays `demo1.dm2` has `// frames demo1.dm2 689`. `record` fails, with
  one line naming both counts, when the engine printed another count, no
  count or more than one; the pin is left incomplete. A count is 1 to 9
  digits without a leading zero, as the engine prints it; any other
  number is no count. `record` writes
  `demo <PINS_DEMO>` into `settings.txt`, and `replay` checks the record
  it replays the same way before it starts an engine. A demo without a
  `// frames` line (a demo made with `make-demo`, whose count changes
  each time it is made), and the cinematic, which prints
  no count, are not checked: `record` says so in one line on stderr and
  records. A record made before FIX48 has no `demo` line and is replayed
  unchecked, with one line on stderr that gives the record's own count.
  The `frames-check` line of `replay.txt` says which it was: `frames-check
  demo1.dm2 against world.cfg (689)` when the count was checked, or
  `frames-check unchecked: ...` with the reason. A frames line is
  written in lower case, in the header comment; one in another case
  (`// Frames`, `DEMO1.dm2`) or below the first line that is not a
  comment is refused before an engine runs, never skipped.

A new scenario gets its count from the scenario it is derived from: all
of them play the same demo, so it copies the `// frames demo1.dm2 689`
line. A new demo gets its line from a clean record of it: record any
scenario with it, read the count from `timedemo.txt` in the pin, and add
`// frames <demo> <count>` to each scenario that plays it.

### Determinism

`record` starts the engine with `+set con_notifytime -1`, so no console
notify line is drawn over a frame whatever the engine prints while the
demo plays (FIX157; a line printed to the console is drawn for
`con_notifytime` seconds of wall time, so a pin's frames would depend on
the clock). The setting is not archived and a pin run writes no
`config.cfg`, so it never reaches a player's configuration. After each
record that writes a trace (not `PINS_TRACE=0`, which has no trace to read, so
no text check), the trace is read to its end (`tests/refrecord/charscan.c`);
a trace that cannot be read, or a scanner that cannot be built, makes the
record incomplete, never a pass. A frame the run
captured that holds a character draw must be listed in
`scripts/pins/text/<demo>-every<PINS_EVERY>.txt`, as `pin <capture
number> <characters>`, the text the demo itself draws; no such file
declares none. Any other text makes the record incomplete and writes the
difference to `text.txt` beside the frames. `pins.sh check-text <trace>
[demo [every]]` runs the same check over one trace.

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
could differ from another live record (see `skywater` above) before
FIX102, which keeps such a stall out of a pin record's clock (below,
except for the frames with no new server frame, which it leaves);
its replay matches it all the same.

A temporal MetalFX scaler (`r_metalfx 2`) makes that caveat reach every
frame. Measured (FIX96) on `ref_metal`, `water.dm2`, `PINS_EVERY=30`, at
5120x2880 (`PINS_HIDPI=1` at 2560x1440): `refdef_t.time` differs by 1 to
5 ms between two live records on the frames after a capture, and
rounding it to 0.1 s in a scratch build makes every record repeat. That
this is the capture's stall pushing the next frame past 100 ms, after
which the timedemo clock follows the host, is read from the capture
cadence and from the records at smaller sizes repeating; it is not
measured (the stall was not timed). Also unexplained: in some runs the
frames two before a capture differ too. The frame after a capture is not
itself captured, so without the scaler, or with the spatial one, the
records repeat; the temporal scaler's history carries it into every
later frame. Five records a case for the four 5K cases (`r_rt_water_hits`
1 and 0 with `r_metalfx 2`, 1 with `r_metalfx` 0 and 1), three for the
others. With `r_metalfx 2`, `compare` of records 2 to 5 against record 1
differs on 4 of 5 frames (up to 3.9 % of the pixels and 11 levels; worst
score 4.6e-05, bad fraction 0), at `r_rt_water_hits` 0 and 1, with
`r_rt_water 0`, and in `world-metalfx2`. At 1920x1080 and 640x480 the
records of the hits-0 case repeat (hits 1 at those sizes is covered by
TASK162's comparisons).

FIX102 measured the stall (the frame after a 5K capture takes 67 to
103 ms, the first capture 170 to 221 ms; at 1920x1080 the frames after
the first capture take under 40 ms, the first capture itself 83.5 to
89 ms) and
keeps it out of the clock: in a pin record's timedemo the client clock's
step is cut below a server frame (`CL_ClockStep`), so every frame that
plays a demo frame gets its time from the demo alone. With it, five 5K
records with `r_metalfx 2` repeat byte for byte at `r_rt_water_hits` 1
and 0, and are compared byte for byte like any other record. Only the
frames before the first capture and after the demo's end, which bring no
new server frame, still follow the host; a scenario that captures the
last frame (`PINS_EVERY=1`) can still differ there. Records made before
FIX102 at 5K with a temporal scaler spread up to a worst score of
4.6e-05 with bad fraction 0 live against live.

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
it as `metal`. It draws everything `gl1` draws in the scenarios above,
and is judged against `gl1-metal` (the shim build of `gl1` on the same
GPU), recorded with `MTL_DEBUG_LAYER=1` (Metal's validation layer) at
`--threshold 0.005 --bad-threshold 0.001`; `models` unseeded and
`skywater` from `water.dm2` at the values in their files, `dlights` and
`particles` seeded only. The values and the last sweep of every scenario
are in `docs/plans/ref-metal-native.md` (section 11) and
the card files in `docs/runbook/`, listed in `docs/plans/metal-port-harness.md`. At start it prints the Metal device,
the Apple GPU families it supports and whether it supports ray tracing,
then the ray-tracing path it takes (`ref_metal: ray tracing: ...`, TASK60).
On a GPU without ray tracing, such as the build VM, the effect cvars the
`rt-*` scenarios set (`r_rt_ao`, `r_rt_shadows`, `r_rt_water`) read 0 with
a line each, so each `rt-*` scenario draws the frames of the same scenario
with its effects off, byte for byte on the VM
([`docs/runbook/TASK60.md`](../docs/runbook/TASK60.md)); its pin there
says nothing about the effect. It refuses a device older than the apple5 family. At each shutdown it
prints its Metal objects created and destroyed, by kind, which must read
`0 live objects`. Its frames have the size the mode asks for, as `gl1`'s
do, also in fullscreen when the window has another size.

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
renderer string are in `docs/runbook/TASK41.md`.

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

`compare` prints `ref-kind=` and `candidate-kind=` after the two paths:
`replay` for a pin `replay` wrote, `live` for a record. A replay is the
reference only when `--ref-id` names it; without it `compare` takes the
newest live record.

## Footprint of an effect

```sh
scripts/pins.sh footprint <scenario> --off <dir|id> --on <dir|id> [--renderer r] [--mask-dir d] [--delta n] [--band 'metric lo..hi']... [--out dir]
scripts/pins.sh where <scenario> --off <dir|id> --on <dir|id> --out <dir> [--renderer r] [--mask-dir d] [--top N] [--delta n]
scripts/pins.sh check-content
```

`footprint` measures what an effect changes. `--off` and `--on` are two
records of one scenario, the effect off and on (a directory path, or an id under
`<store>/<scenario>/<renderer>/`, `--renderer` default `metal`); their frames
are matched by name. For each frame it prints the share of pixels whose change
(the mean over the three channels of on minus off) is more than `--delta` levels
(default 8): `changed`, split into `darker` and `brighter`, then `max`, the
largest change in levels, `chmax`, the largest change of any one channel (`max`
and `mean` average the three, so a shift of colour alone shows only there), and
`mean`, the mean change in levels over all counted pixels. `--mask-dir` counts only the pixels the same-named frame there marks (any
channel above 0). A summary follows, and `--out` also writes it to
`footprint-<scenario>.txt`. The per-frame numbers come from
`framediff --footprint` (`scripts/pins/framediff.c`, whose header documents its
options), which does not change what a plain `framediff A B` prints.

A band bounds a metric: `changed`, `darker` and `brighter` as shares from 0
to 1, `max` and `mean` in levels. A scenario declares its band in its cfg
header, `// footprint darker 0..0.05` (inclusive, any number of lines), and
`--band 'darker 0..0.05'` replaces it for one run. The scenario has to exist (a
misspelled name is refused, not read as one without a band), and a cfg line that
looks like a band and does not parse is an error; bands are read from the
leading comment block only. The command names every frame outside the band and
exits non-zero; a frame missing from the on record, one only it has, an off
record with no frames and a frame of no pixels also fail, and the summary never
says otherwise. With no band it reports and, those apart, never fails.
How to set one: `scripts/pins/scenarios/README.md`, "Footprint band".

`where` writes, for the `--top` frames (default 8) that changed most, a
highlight, `<frame>-highlight.tga` (the off frame in grey, darker pixels red,
brighter pixels green, the more saturated the larger the change: 32 levels is
full colour), and a zoom, `<frame>-zoom.tga` (the 160x120 window, a quarter of
a 640x480 frame each way, with the most change, the off frame on the left and
the on frame on the right, each 3 times larger). The images are TGA, which
Preview opens. `--out` must lie outside this repository and any git work tree:
frames are game data and never enter the repository. Neither command writes
through a symbolic link: an existing link, or anything but a regular file, at the
table or an image refuses the run before the first file is written, and
`framediff` refuses it too. The pin store is checked as for `compare`, so
`framediff` is never compiled into a store inside a repository, and an id is a
directory name that does not start with `.`.

`check-content` (the selftest case `content_tags` runs it, so the gate does)
checks the "Content" table of `scripts/pins/scenarios/README.md`: every scenario
has a row, its tags follow the cvars it sets, and a scenario that sets
`cl_entities 0` names a companion that sets it to 1. When that scenario declares
a footprint band, the companion declares the same band and sets every other cvar
of the scenario to the same value, except the content cvars (`cl_entities`,
`cl_particles`, `cl_lights`, `cl_gun`, `cl_blend`, `gl_polyblend`) and the demo
and frame settings (`timedemo`, `scr_conspeed`, `cl_showfps`, `viewsize`,
`crosshair`); without a band the companion is nominal. The README says why.

## Replaying a trace

```sh
scripts/pins.sh replay <renderer> <scenario> [--trace-of <renderer>] [--ref-id <dir>] [--run N] [--replace]
```

`replay` feeds the trace of a complete record to `<renderer>` (the
engine's `pins_replay`): the frames come from the recorded drawing
calls, with no demo, no server and no game module. The record is the
one of the `--trace-of` renderer (default: `<renderer>` itself) that
`--ref-id` names, or else its most recently modified complete record
holding a `trace.q2r`. Before the engine starts, `replay` refuses:

* a record that is not complete, or that is itself a replay;
* a trace that is not a regular file inside the store by its physical
  path (a symbolic link anywhere on the way is refused), or whose path,
  or the frame paths the replay would write, do not fit the engine's
  smallest path buffer (255 bytes);
* a trace whose header is not finished (the engine died while
  recording), is marked cut short by a recorder limit, is of another
  format version or renderer API than this tree, holds no frame, or was
  recorded at another size or capture cadence than `PINS_WIDTH` x
  `PINS_HEIGHT` and `PINS_EVERY`.

Only numbers are read from the header; nothing from a trace or a
manifest is run. `PINS_SEED`, `PINS_TIMEOUT`, `PINS_BIN` and
`PINS_BASEDIR` mean what they mean for `record`. The frames land in
`<store>/<scenario>/<renderer>/<engine>-replay-<trace-of>-<record>/`
(with `-s<seed>` when `PINS_SEED` is set and `-r<N>` for `--run N`,
so replays with two seeds are two pins and `--replace` of one never
overwrites the other), beside the renderer's records but never under a
record's name. `replay.txt` there, the replay manifest, is
written before the engine starts: the trace it replayed, its size and
checksum, the seed, and `game-module absent` or `present`, whether a
game module lay where the engine would look for one (beside the binary
or in `PINS_BASEDIR`, under `baseq2/`). The replay is complete on the
same terms as a record, and only when it wrote as many frames as the
record holds; otherwise it is left with `incomplete` and the reason.
A complete replay is replaced only with `--replace`, and a directory
without `replay.txt` (a regular file, not a link) never. `list` shows,
per scenario and renderer, how many complete records hold a trace and
how many replays there are.

One trace, two renderers:

```sh
scripts/pins.sh record gl1-metal world
scripts/pins.sh replay soft world --trace-of gl1-metal
scripts/pins.sh compare soft world --cand-id <the id replay printed> --ref soft
```

Measured (TASK46) on Apple silicon, load average 8.1 to 13.0 while the
runs went, 640x480, every 30th frame of `demo1.dm2`, `world`, both
thresholds 0. One live record each of `soft` and `gl1-metal`; each
replay was run three times, with an engine binary copied into a
directory without a game module (`PINS_BIN`), `PINS_BASEDIR` holding
none for macOS either; every replay manifest says `game-module absent`.
The same binary refuses a live record (`failed to load game DLL`), so
the game module was needed by the record and not by the replay.
With a default build the binary is `release/quake2` beside
`release/baseq2/game.dylib`, so the manifest says `game-module present`
although the replay does not load it; the copied binary above is how
the absent case was shown. The cross-renderer rows are two renderers on
one host replaying each other's trace, not a trace carried from another
machine. Plain `gl1` and the native `ref_metal` were not measured in
these runs (plain `gl1` replaying its own trace is under *Determinism*
above).

| Replay | Against | Result, each of 3 runs |
|---|---|---|
| `soft` from its own trace | live `soft` | 23 frames, 0 differ, worst 0 |
| `gl1-metal` from its own trace | live `gl1-metal` | 23 frames, 0 differ, worst 0 |
| `soft` from the `gl1-metal` trace | live `soft` | 23 frames, 0 differ, worst 0 |
| `gl1-metal` from the `soft` trace | live `gl1-metal` | 23 frames, 0 differ, worst 0 |
| `soft` from the `gl1-metal` trace | live `gl1-metal` | 23 of 23 frames differ, worst score 0.077802, worst bad fraction 0.922135 (the same in all 3) |

The client hands the renderer the same calls whichever renderer is
loaded, so a replay from another renderer's trace draws what that
renderer drew live; the two traces differ in size (4,382,172 and
4,383,028 bytes) by their renderer cvar snapshot. The last row is the
difference between the two rasterizers themselves, not a replay error.
A replay took 1.28 to 1.34 s on `gl1-metal` and 2.31 to 2.34 s on
`soft` (6 runs each).

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
`<name>.dm2.failed`. A script may also state where its camera move must
fall, in a `// pins: move <low> <high>` line: `make-demo` then reads the
demo, finds the last frame message that carries the player's origin (the
move) and fails the demo, the same way, when that is not on rendered
frames `<low>` to `<high>` (the message's index less 5; a demo of N
messages renders N-5 frames). That is a property of the demo and not of
the clock. `water` holds 220 waits before the move and 370 after, so the
move falls at 0.38 to 0.40 of the frames (measured, from 36 of 95 to
61 of 154), and states frames 33 to 57 and 96 to 147 messages. A `skywater`
capture falls on every 30th rendered frame from frame 0, so a move on those
frames lies between the second capture (frame 30) and the third (frame 60),
3 frames from each: the first 2 captures look from just above the pool's
surface and the rest (2 or 3) from under it, in a demo of 4 captures (96 to
125 messages) or of 5 (126 to 147). The count differs between demos, which
is safe because the id of a record includes the demo's checksum. At the top
of the range the move is at 142 * 0.40 = 56.8, and a demo of 148 messages
may put it on frame 57.2, so it is refused. A pace that gives a demo outside
96 to 147 messages (the range spans a factor of 1.5) still fails; make the demo again when the host is quieter, or lower
`PINS_WIDTH` and `PINS_HEIGHT`.
How it was checked is in [FIX47](../docs/runbook/FIX47.md).

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

## Timings with bench.sh

Frame rates and GPU times are not judged from pins. They are measured
with the bench harness, `scripts/bench/bench.sh` (its commands, the
host lock and the store are in
[Unit Tests, Pins And Coverage](085_unittests.md#benchmarks-and-the-host-lock),
its design in `docs/plans/bench-harness.md`), through the quake2
adapter, `scripts/bench/adapters/quake2.sh`, and the cases in
`scripts/bench/cases/`. A case exports a commit, builds it, plays a demo
with every setting fixed and reads the numbers the engine prints; a
baseline is measured once per build and reused. It needs the same game
data as a pin, and every quake2 case needs a bare-metal Mac. This page
holds the quake2 part because it shares the pins' game data, scenarios
and rules for what is never committed.

```sh
export BENCH_Q2_BASEDIR=/path/to/quake2     # holds baseq2/, as PINS_BASEDIR
scripts/bench/bench.sh case rt-ao-gpu-1920x1080
scripts/bench/bench.sh baseline put rt-ao-gpu-1920x1080 HEAD
scripts/bench/bench.sh drift rt-ao-gpu-1920x1080 HEAD
scripts/bench/bench.sh compare rt-ao-gpu-1920x1080 --before HEAD~1 --after HEAD
```

| Variable | Meaning |
|---|---|
| `BENCH_Q2_BASEDIR` | the directory that holds `baseq2/`; default `PINS_BASEDIR` |
| `BENCH_Q2_BUILDS` | the build cache, one export and build per inputs id, outside any repository, owned by you and writable by no one else; default `<cache home>/<repository>-bench-builds` (below) |
| `BENCH_Q2_JOBS` | `make -j` of a build; default the number of CPUs |
| `BENCH_Q2_LOGS` | a directory, outside any repository, that keeps each run's engine log (text only); default unset |
| `BENCH_Q2_CAPS` | a file of capability lines (`physical-gpu=yes`, `raytracing=no`, `metalfx=yes`, `metalfx-temporal=yes`, `display-hz=60`) read in place of the system probe; `physical-gpu` is still checked against `kern.hv_vmm_present`, and a `display-hz` requirement is refused with a file |

Before a measurement the adapter checks the case's settings and its
`requires`: `physical-gpu`, `raytracing`, `metalfx` and `display-hz>=N`,
read from the system by a small probe built with the host's `cc`
(`scripts/bench/adapters/quake2-caps.m`, which asks Metal and the
display what `ref_metal` asks). A case that runs the temporal scaler
(`r_metalfx 2`) needs the temporal scaler's support as well as
`metalfx`, since the engine would quietly fall back to the spatial one.
A capability the host lacks refuses the case with its reason (`bench.sh`
exits 3), so the build VM refuses every quake2 case rather than measure
something else. It also refuses a run while `MTL_DEBUG_LAYER` is set,
while the screen is locked or while another `quake2` runs, and when
`ioreg` or `pgrep` fails, so it cannot tell; it never stops a process it
did not start. Each run starts without a saved `config.cfg`, plays with
sound off, is sent TERM `seconds=` after launch and must show the case's
renderer and, for a ray-traced or MetalFX case, the engine's own support
line (for a temporal case, the temporal scaler's).

The build cache is `$XDG_CACHE_HOME/<repository>-bench-builds`, else
`~/.cache/<repository>-bench-builds`, where `<repository>` is the name
of the repository's directory (`quake2` for a clone of that name), or
`BENCH_Q2_BUILDS`. It holds one directory per inputs id (`bench.sh
inputs <commit> <case>` prints it): `tree/`, an export of the commit,
about 40 MB before the build (three quarters of it `third_party/`),
then its objects and `release/`; `exported`, the marker of a finished
export; and `build.log`. Builds run with the caller's build variables
cleared (`CC`, `CFLAGS`, `LDFLAGS`, `DEBUG`, `ASAN`, every `WITH_*` and
the rest of `Q2_BUILD_ENV` in the adapter), so an entry does not depend
on who ran it; a case's `build_flags` are part of its inputs id. Nothing
prunes it. Remove an entry, or the whole directory, with `rm -rf` while
no bench run is in progress (`bench.sh lock status`); the next run of
that inputs id exports and builds it again.

| Cases | From | Metric | Settings |
|---|---|---|---|
| `present-<renderer>-<size>-vsync<0 or 1>`: 12, `metal` and `gl1-metal`, 640x480 and 1920x1080 windowed, 5120x2880 `vid_fullscreen 1` | FIX51 | `fps`, the timedemo's rendered rate; also `presented` and `no_drawable` over the run | stock settings, a timedemo of `demo1.dm2`, `metal_stats 1` |
| `rt-shadows-gpu-<size>[-entities]`: 4 | TASK131 | `rts_ms`, the shadow pass's GPU time, median of the frames numbered past 100 | scenario `rt-shadows` or `rt-shadows-entities`, `pins_seed 1`, timedemo, `r_rt_stats 2` |
| `rt-ao-gpu-<size>[-entities]`: 4 | TASK57 | `rtao_ms`, the AO pass's GPU time, past the first 100 lines | scenario `rt-ao` or `rt-ao-entities`, `pins_seed 1`, timedemo, `r_rt_stats 2` |
| `metalfx-scaler-<size>-<spatial or temporal>-<scale>`: 12, 1920x1080 windowed and 5120x2880 fullscreen, scales 0.75, 0.5 and 0.34 | TASK111 | `scaler_ms`, the scaler pass's GPU time per view, past the first 60 lines | scenario `world`, the engine's own pace, display sync off, `vid_maxfps 300`, `metal_stats 2` |

The ray-traced cases' 5120x2880 is a 2560x1440 window with
`vid_highdpiaware 1`, as in TASK131. A case is one measurement, so each
cell has its own file. The scaler cases run with display sync off: at
60 frames a second on a quiet host the GPU clocks down between frames
and the pass reads about twice its time, varying between runs. The
engine prints no time per frame, so a rate over a window of seconds
(FIX51's presented frames a second) is not measured; the totals over a
run are. The reproductions of each family against its card are in
`docs/runbook/TASK154.md`.

Every measurement first warms the host up: the case's own run with
display sync off, again and again, for the case's `prewarm` seconds
(240 unless the case says otherwise; `prewarm=0` skips it), in the same
hold of the lock as the measured runs. A run started from idle measures
a GPU that has clocked down: the unchanged binary of
`rt-shadows-gpu-1920x1080` read 2.79 ms cold and 1.67 ms after a
4-minute pre-warm (`docs/runbook/TASK166.md`). A thermal or performance
warning stops the pre-warm early, and the set says so. Before each
counted run `bench.sh` waits, at most `gate_wait` seconds (120), until
the GPU is at most `gate_gpu` percent busy (10) and the CPU at most
`gate_cpu` percent (25) for `gate_hold` seconds (3). Each run file
records the host just before and after the run (load, CPU and memory
use, swap, the GPU's use and memory, thermal warnings, low-power mode,
the five busiest processes by the name the kernel gave each at exec,
never what a process wrote into its own arguments) and the CPU
clusters' and the GPU's frequencies over the run, read without root,
with which reader made each (`snap.source` and `freq.source`: `system`,
or `stub` when a stand-in of the self-test did). A run that started when
the wait ran out, or with another program using the GPU, is
`quiet=no`; `baseline put` stores no set with such a run, and `drift`
and `compare` print the flag and the readers. `BENCH_GATE_POLL` (the
seconds between the gate's readings, 1, at least 0.01) and
`BENCH_QUIET_TTL` (the seconds the quiet marker below lasts, 3600) are
checked before the lock is taken. While it measures, `bench.sh` keeps a
quiet marker beside its lock (`bench.sh lock run --quiet`), which the
agent tooling reads to hold other sessions' work. The fields are listed
in `docs/plans/bench-harness.md` ("Determinism").

## Other commands

* `scripts/pins.sh list`: how many frames each record in the store holds,
  and the trace coverage (records with a trace, and replays, per
  scenario and renderer).
* `scripts/pins.sh footprint`, `where` and `check-content`: "Footprint of an
  effect" above.
* `scripts/pins.sh selftest [case...]`: tests the driver and the comparator with
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

## Ray-tracing variations for a look review

The ray-traced effects of `ref_metal` layer conservatively on the stock lightmap, so their
default frames barely differ from ray tracing off. `scripts/rt-variations.sh` (TASK173) records
the same views under a set of named presets and lays them side by side, so the effects can be
judged by eye:

```sh
PINS_BASEDIR=/path/to/quake2 scripts/rt-variations.sh run --out /path/outside/every/repo
scripts/rt-variations.sh plan --views water     # the records a run would make
scripts/rt-variations.sh selftest               # offline, no engine, no game data
```

The presets and views are in `scripts/pins/rt-variations.txt`, one line each. A preset is a list
of `r_rt_*` cvars set on top of a view's base scenario: `off`; each effect alone (dynamic-light
shadows, sun, ambient occlusion, water reflections) at its defaults and exaggerated; `all` and
`all-x`; each `r_rt_*_show` debug view; the sun at `r_rt_sunfloor` 0.25, 0.5, 0.75 and 1.0; and
`preview`, every effect on over a flat light in place of the lightmap (`r_rt_flatlight`, see
`doc/040_cvarlist.md`), with `flat`, the flat light alone, beside it. The preview has no traced
bounce light and looks harsh: it shows what the traced terms add without the baked light that
masks them, and is not a presentation (the lightmap-free mode is TASK174's). A view is a base
scenario, a demo and a capture cadence, and the capture it shows: a number, or `auto:<preset>`,
the capture in which that preset differs most from `off`. Views of one base, demo and cadence
share their records.

`run` records each view's base under each preset with `scripts/pins.sh record metal`, from a
scratch scenario `scripts/pins/scenarios/rtvar-<view>-<preset>.cfg` (the base's lines, then the
preset's; git-ignored and removed after its record), with `PINS_SEED` (`--seed`, default `1`)
and the Metal validation layer on; a record whose log holds a validation message fails the run.
The records go to the pin store `<out>/store`, and a complete record already there is used again
unless `--replace` is given, so a run can be split with `--views`. It then writes per view, in
`<out>/<view>/`:

- `sheet.png`, the contact sheet: every preset's frame of the view's capture, four to a row,
  each under its name and caption;
- `clip.png`, the difference clip, an animated PNG: `off`, then each preset, a second each
  (`--clip-ms`);
- `full/<NN>-<preset>.png`, the full-size frames;

and `<out>/index-<views>.txt`, the capture each view shows, and per preset its cvars, its mean
difference from `off` and its record. The `off` preset sets no cvar, so the tool changes no frame:
its frames are the base scenario's own. The output holds frames made from game data, so `--out`
is refused inside any git work tree (with `scripts/metalfx-report/mfx.py check-out`) before
anything is built or run; keep it in a local artifacts folder, never committed or uploaded. The
selftest runs the real `pins.sh record` with the harness's stub engine in a copy of the scripts;
it runs in `scripts/test.sh` as `tests/check-rt-variations.sh`. The procedure and the first run
are in [`docs/runbook/TASK173.md`](../docs/runbook/TASK173.md).

## The pin arguments

`record` passes four arguments to the engine, `pins_dir`,
`pins_scenario`, `pins_capture` and `pins_record` (the last one left out
with `PINS_TRACE=0`), and a fifth, `pins_seed`, when `PINS_SEED` is
set. They look like cvars but are read only from the command line; see the *Pin harness* section of the
[List Of All New Console Variables](040_cvarlist.md). Without them the
engine behaves exactly as it does otherwise.
