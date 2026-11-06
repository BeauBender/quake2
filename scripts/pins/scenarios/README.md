# Pin scenarios (TASK27)

One cfg per rendering feature so a failing pin names the feature. Each
scenario is a Quake II config executed with `+exec` after the harness has
set the capture cvars (`pins_dir`, `pins_scenario`, `pins_capture`). The
demo named in each file must exist in the owner's game data
(`demos/<name>.dm2`); the retail `pak0.pak` ships `demo1`, `demo2`, `demo3`.
Nothing here is derived from game data, so the files are committed (INFORM9).

Common rules (every scenario): `timedemo 1` so playback is frame-locked to
the demo stream, fixed resolution from the harness, HUD and gun off where
the feature under test does not need them, and no cvar that depends on wall
clock or a random seed. A scenario that cannot avoid randomized effects
says so in a `// nondeterministic:` line and is accepted at a looser
threshold, recorded with the pin.
