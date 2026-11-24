# The settings optimizer's search and `optimize run` (TASK203)

Card: TASK203 (#695). Plan: [the settings optimizer](../plans/settings-optimizer.md), stage 1. Decision:
[0014, no feature is gated on performance](../decisions/0014-no-performance-gating.md): no frame rate here
is an acceptance criterion.

## What it is

- **The search** (`src/client/menu/optsearch.c`, `header/optsearch.h`): pure, no cvar, file, timer or
  renderer access. `OptSearch_Init` builds each ranked priority's ladder (plan, section 4.1); `OptSearch_Next`
  replays the search from the start over the trials recorded so far and stops at the first candidate not yet
  measured, so the same inputs give the same candidates; `OptSearch_Record` adds a trial's measurement. A
  candidate is keyed by the cvar values it sets after the coupling of resolution and the scaler, and one
  equal to an earlier trial is looked up, not timed. The worked trace of the plan's section 4.2 is pinned in
  `tests/pins/unit/optsearch_worked_trace.pin`.
- **The command** (`src/client/menu/optimize.c`): `optimize run [name] | cancel | report | undo` and the
  `optimize_*` cvars (`doc/050_commands.md`, `doc/040_cvarlist.md`). The probe and each trial play
  `timedemo 1` on `optimize_demo`; the first play starts from the command, the next ones from the constant
  console line `optimize _play` queued at the demo's end, which hands the checked name to `SV_Map` as an
  argument. Each candidate is published as the B side through `RTPreset_PublishSet`; the result is saved
  through `RTPreset_SaveSet`, which goes through `rt_preset save`'s own name and path checks.
- **Hooks**: `cl_view.c` calls `Optimize_Frame` in the timedemo block, `cl_main.c` calls `Optimize_Tick`
  in `CL_Frame` on every client frame in every connection state (after `CL_FixCvarCheats`), and
  `cl_network.c` calls `Optimize_DemoEnd` in `CL_Disconnect`; each returns at once when no run is on, so
  nothing differs outside a run and every pin stays the same.
- **The renderer**: `RM_FX_Begin` reads `r_metalfx_scale` through `RM_Effect`; `RI_EndFrame` writes
  `metal_presented` only while `optimize_state` is not `0`, and `metal_rt_cost` only while it is `2` (the
  probe): each traced pass's stats harvest (shadows, sun, AO, water) hands its completed frames' GPU times
  to `RM_OptCost_Add`, and the means per timed frame go out through `rm_rt_cost_format` in the form
  `optimize.c` parses. With no run on nothing is kept or written.
- **The preset row**: `r_metalfx_scale` is the last row of `rt_tune`, so presets, the A/B and the tuning
  screen hold it; a preset without its line keeps the current value (`RTP_Read`).

## Choices made here

- **The run is bounded in every state** (round 1). The time cap's clock starts at `optimize run` and is
  checked by `Optimize_Tick` on every client frame, whether a frame is drawn or not, so map loads count and
  a demo that never draws a frame is still stopped. The same check fails the run when `timedemo` is not 1
  (`CL_FixCvarCheats` resets it while the configstrings say `maxclients` is above 1, which a multiplayer
  demo does and so does a local multiplayer game the benchmark ends; a demo's own text can set it too).
- **A play that ends with no frame** fails the run ("the demo ended with no frame drawn"). The disconnect
  of the game the benchmark ends is told apart by where it happens: inside the play call (`SV_InitGame`'s
  `CL_Drop`, when a game was on). A local single-player game ends without a disconnect (`changing`, then
  `reconnect` while connected, which sends `new`). An `ERR_DROP` inside the play call jumps out of it; the
  next tick clears the flag. The demo's existence is checked again before every play, so a demo removed
  between trials fails the run by name.
- **A `vid_restart` mid-run** fails it with "the renderer changed": the renderer's presented count
  (`metal_presented`, `rmb_counts`, zeroed when the device starts) goes back. `optimize run` sets the cvar
  to 0 first, so a stale value from an earlier renderer cannot look like a restart. The one restart this
  does not see is one made before the old renderer presented more frames than the new one has at the next
  tick, which a renderer that has drawn the menus and a map load has already passed.
- **A failure's report** (round 1): `OptSearch_Stop` replays the search to the trial it was waiting for and
  ends it there as a cap does, so a failure after a trial prints the best so far (`result:` or `closest:`),
  its `limit:` line and what it gave up, then "stopped before the search ended: <reason>"; `optimize
  report` prints the same lines.
- **`before-optimize` is reserved**: `optimize run before-optimize` is refused, in any letter case, so the
  result cannot overwrite the copy `optimize undo` points to.
- **Cancel saves nothing**; a failed trial saves the best result so far as the named preset when there is
  one (plan, section 5.3). Either way the B side and A/B state from before the run are put back.
- **`denoise`'s ladder** is `r_metalfx 3`, then the top of the scaler ladder the device has (`2`, else `1`);
  it needs ray tracing (`r_rt_available`) as the denoised scaler does.
- **Rule 3 of section 4.1** is read as: the `resolution` ladder is `1` alone when neither `upscale` nor
  `denoise` can be tried and the player's `r_metalfx` is `0`.
- **The noise band** is the plan's starting 3 % of the target; the record of one configuration timed three
  times is not yet made (below).

## Review notes, round 1

- **Stock B now carries `r_metalfx_scale`** (correctness N3). With the new row, `RTP_Publish` writes
  `r_metalfx_scale=0` (the row's default, the device's own scale) into a stock B side, where before stock B
  had no entry and B used A's scale. So a player at 0.75 who flips to stock B sees the family default
  there. This is what a new row means for every preset and the stock side; it is kept, and said here.
- **The frame-count disconnect check** (correctness N4) assumed that a timedemo of the same demo draws
  exactly the probe's frame count every time. It now calls a trial a disconnect only when it drew fewer
  than 90 % of the probe's frames, so a frame or two fewer (a frame of the old game counted before the
  demo, timing at the demo's end) is not a failure; a real disconnect early in the demo still is.
- **The "gave up" lines** (correctness N1) say what the result holds where the scaler's coupling changed
  it: `r_metalfx 0: upscale gave up step 2 of 2 ... (the result holds r_metalfx 1)`.
- **`optimize_target`** as a rate is at least 1 (correctness N2), and `optimize_rounds`,
  `optimize_seconds` and `optimize_warmup` that are not finite numbers refuse the run before the clamp
  (security N4).
- **A local multiplayer game** on at `optimize run` ends with the run failed at once ("timedemo is not
  1"): the cheat check sees the old game's `maxclients` until the demo's own configstrings arrive. A
  remote game, a single-player game and the console are not affected.

## Checks

- `make test TEST_ARGS=optsearch`, `TEST_ARGS=optimize`, `TEST_ARGS=rtpreset`: the acceptance tests
  (`tests/unit/test_optsearch.c`, `test_optimize.c`, `test_rtpreset.c`).
- `tests/check-cvar-docs.sh`: every new cvar and the command documented.
- The stock `metal` pins (`world`, `hud2d`, `rt-water`) unchanged.

## A manual run

A bounded run on real data: `optimize run` with the defaults but `optimize_rounds 4`. Its report lines are
recorded here when the run is made on a bare-metal Mac; A (the tuning cvars) is compared before and after.
