# Pin scenarios (TASK27)

One file per rendering feature, so a failing pin names the feature. A
scenario is a list of `set <cvar> <value>` lines. `scripts/pins.sh record`
passes each line to the engine as a `+set` argument and then starts the demo
with `+demomap` (`PINS_DEMO`, default `demo1.dm2`), so all scenarios follow
the same camera path and differ only in what is drawn. Nothing here is
derived from game data, so the files are committed (INFORM9).

| Scenario | Drawn |
|---|---|
| `world` | world surfaces, lightmaps, sky, water |
| `models` | world plus entities: alias models, sprites, beams, view weapon |
| `particles` | everything, including particles, dynamic lights, screen blend |
| `hud2d` | 2D only: status bar, crosshair, console text |

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
  `cl_particles`, `cl_lights`, `cl_blend` and by `r_drawworld`.
- A scenario that cannot be deterministic says so in a `// nondeterministic:`
  line and is accepted at a looser threshold, recorded with the pin.

Not yet verified: no scenario has been run against game data. Which demos
the owner's data contains, and what `hud2d` shows behind the 2D elements on
each renderer, are settled by the first runs (see
`docs/plans/metal-port-harness.md`).
