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
- **Hooks**: `cl_view.c` calls `Optimize_Frame` in the timedemo block, `cl_network.c` calls
  `Optimize_DemoEnd` in `CL_Disconnect`; both return at once when no run is on, so nothing differs outside a
  run and every pin stays the same.
- **The renderer**: `RM_FX_Begin` reads `r_metalfx_scale` through `RM_Effect`; `RI_EndFrame` writes
  `metal_presented` only while `optimize_state` is not `0`.
- **The preset row**: `r_metalfx_scale` is the last row of `rt_tune`, so presets, the A/B and the tuning
  screen hold it; a preset without its line keeps the current value (`RTP_Read`).

## Choices made here

- **The time cap's clock** starts at the run's first frame drawn, not at the command: the frame hook is the
  only clock the run has, and it keeps the unit tests on their own clock. The first map load is therefore
  outside `optimize_seconds`.
- **Cancel saves nothing**; a failed trial saves the best result so far as the named preset when there is
  one (plan, section 5.3). Either way the B side and A/B state from before the run are put back.
- **`denoise`'s ladder** is `r_metalfx 3`, then the top of the scaler ladder the device has (`2`, else `1`);
  it needs ray tracing (`r_rt_available`) as the denoised scaler does.
- **Rule 3 of section 4.1** is read as: the `resolution` ladder is `1` alone when neither `upscale` nor
  `denoise` can be tried and the player's `r_metalfx` is `0`.
- **The noise band** is the plan's starting 3 % of the target; the record of one configuration timed three
  times is not yet made (below).

## Not done in this slice

- `metal_rt_cost`, the probe's per-pass GPU times: not published by `ref_metal` yet. The search reads it
  when present and uses the plan's fixed order (`r_rt_ao`, `r_rt_water`, `r_rt_sun`, `r_rt_shadows`, then
  `r_rt_gunshadow`) without it.
- A `vid_restart` during a run is not detected by itself; a change of `vid_renderer` is.
- A demo that never draws a frame leaves the run waiting; `optimize cancel` stops it.

## Checks

- `make test TEST_ARGS=optsearch`, `TEST_ARGS=optimize`, `TEST_ARGS=rtpreset`: the acceptance tests
  (`tests/unit/test_optsearch.c`, `test_optimize.c`, `test_rtpreset.c`).
- `tests/check-cvar-docs.sh`: every new cvar and the command documented.
- The stock `metal` pins (`world`, `hud2d`, `rt-water`) unchanged.

## A manual run

A bounded run on real data: `optimize run` with the defaults but `optimize_rounds 4`. Its report lines are
recorded here when the run is made on a bare-metal Mac; A (the tuning cvars) is compared before and after.
