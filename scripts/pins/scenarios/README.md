# Pin scenarios (TASK27)

One file per rendering feature, so a failing pin names the feature. A
scenario is a list of `set <cvar> <value>` lines. `scripts/pins.sh record`
passes each line to the engine as a `+set` argument and then starts the demo
with `+demomap` (`PINS_DEMO`, default `demo1.dm2`), so all scenarios follow
the same camera path and differ only in what is drawn. The exception is
`cinematic`: `PINS_DEMO` names a cinematic under `video/` instead (for
example `PINS_DEMO=idlog.cin PINS_EVERY=7`), which `+demomap` plays the same
way, and the engine then captures every Nth cinematic frame rather than
every Nth rendered frame. `record` holds you to the pairing: `cinematic`
needs a `.cin` and any other scenario refuses one. Nothing here is
derived from game data, so the files are committed (INFORM9).

| Scenario | Drawn |
|---|---|
| `world` | world surfaces, lightmaps, sky, water |
| `world-fullbright` | `world` with `r_fullbright 1`: world surfaces without lightmaps, sky, water (TASK87, `ref_metal` N4) |
| `models` | world plus entities: alias models, sprites, beams, view weapon |
| `particles` | everything, including particles, dynamic lights, screen blend |
| `hud2d` | 2D only: status bar, crosshair, console text; the GL renderers' screen blend off (`gl_polyblend 0`, TASK82) |
| `skywater` | world surfaces, sky and warped water, with the sky and water-warp settings fixed; no entities, particles, dynamic lights or blend; water needs `PINS_DEMO=water.dm2` from `make-demo water` |
| `cinematic` | a cinematic (`.cin`) stretched over the screen, no world; needs `PINS_DEMO` naming a cinematic |

Rules:

- `set timedemo 1` in every file: playback is locked to the demo stream.
- A scenario never names a demo and sets only cvars that exist in `src/`.
  `scripts/pins.sh selftest` (run by the gate) checks both.
- A scenario name matches `[a-z0-9_][a-z0-9_-]*` (for example `rt-ao`), and a
  value holds only `A-Z a-z 0-9 _ . -`: the engine re-joins its command line
  unquoted. The selftest checks the values and `record` refuses others.
- Every file sets every toggle, so no state leaks from one run to the next.
- This tree has no cvar that hides the status bar, so the status bar is in
  every scenario. Isolation is by the client-side toggles `cl_entities`,
  `cl_particles`, `cl_lights`, `cl_blend` and by `r_drawworld`. `cl_blend 0`
  clears the colour of the screen blend but not its alpha, so a pickup still
  darkens the screen; `gl_polyblend 0` (in `hud2d` only) keeps the blend out
  of `gl1` and `gl3`, not out of `soft`. A pin run neither reads nor writes
  `config.cfg` (TASK39), so leaving it out of the other files leaks nothing
  into their runs, even from `gl3`, which archives it.
- A scenario that cannot be deterministic says so in a `// nondeterministic:`
  line and is accepted at a looser threshold, recorded with the pin.

First run on game data (TASK39, `soft`): `world` and `hud2d` are
identical run to run; `models` and `particles` are not and carry their
measured thresholds in their `// nondeterministic:` lines. The owner's data
holds `demo1.dm2` and `demo2.dm2`. Behind the 2D elements of `hud2d`,
`soft` shows a uniform dark grey; other renderers are not measured yet
(see `docs/plans/metal-port-harness.md`).

Added by TASK40 (`soft`, two runs each and a third): `skywater` and
`cinematic` are identical run to run (score 0 on every frame). On
`demo1.dm2` the sky is in frame 21 at the default `PINS_EVERY` 30; neither
demo showed warped water on any frame looked at, so `skywater` exercises
the sky and the water-warp settings but not a water surface on this data.
`cinematic` was recorded with `idlog.cin`. See the runbook for the frame
lists.

Added by TASK70: a demo that shows water, made from the game data by a
committed console script and never committed itself.
`scripts/pins.sh make-demo water` runs `scripts/pins/demos/water.cfg` and
leaves `water.dm2` beside the binary (`release/baseq2/demos/`); record
`skywater` from it with `PINS_DEMO=water.dm2`. A console `wait` is a
lower bound on time, so the demo is longer on a busy host; the script
states the message range it must give and `make-demo` fails a demo
outside it. From a demo in range (`soft`, the default `PINS_EVERY` 30)
the first half of the frames show the pool of `base1` from just above
its surface and the second half from under it: 4 frames, 2 and 2, in
every record made so far. Records of one demo file compared at score 0
in 27 pairs, but differ slightly when the engine stalls, so `skywater`
from `water.dm2` is accepted at `--bad-threshold 0.03` (its
`// nondeterministic:` line and the runbook). A script under
`scripts/pins/demos/` may use only commands that exist in `src/`; the
driver checks that before it runs one, and the selftest checks every
committed script.
