# Plan: the settings optimizer (TASK202)

Decision record: [0014, no feature is gated on performance](../decisions/0014-no-performance-gating.md).
Cards: TASK202 (#694, this plan), TASK203 (#695, the search and the
`optimize` command), TASK204 (#696, the menu screen), TASK205 (#697,
starting points per device class, waiting on the owner). Related plans:
[HDR output](hdr-output.md), [the presentation modes](presentation-modes.md)
and [the ray-tracing design](metal-raytracing-design.md) (section 8).

Status: proposed. This document changes no code, no pin and no default.
Every pointer below names a file and a symbol, checked on `main` at
`ad159234`. Line numbers are left out on purpose: they move.

## 1. Goal and non-goals

The player ranks what they care about and sets a target. A command and a
menu item play a fixed benchmark, measure it, and adjust the Metal
renderer's settings over a bounded number of rounds. The optimizer keeps
the best configuration that meets the target, giving up the
lowest-ranked settings before the highest-ranked ones. When nothing meets
the target, it reports the closest result and what limits it.

Non-goals, each binding on the stages:

- **It is a starting point, never a gate.** Decision 0014: no feature is
  gated on what a device is thought to handle, and no performance target
  is an acceptance criterion. The optimizer's result is a suggestion the
  player may take, change or ignore.
- **It removes no choice and binds nothing.** It greys or hides nothing.
  Every setting it turns stays the player's, in the menus and the console.
  It never writes the player's own settings (section 6); the player
  accepts a result with their own `rt_preset load`.
- **It is not `scripts/bench/`.** It runs inside the engine on the
  player's device. It takes no host lock, writes no baseline store, and
  uses none of the agent tooling (`scripts/bench/`, `scripts/pins.sh`, the
  dev-loop scripts).
- **No change** to the network protocol, savegames, the game module or the
  renderer API (`src/client/vid/header/ref.h`, `API_VERSION`). Values cross
  from the renderer to the client only as status cvars (section 3.3).
- **Only the Metal renderer's settings.** Every setting it turns is read by
  `ref_metal`. Under another renderer, `optimize run` refuses with "the
  optimizer turns the Metal renderer's settings" (TASK204 greys the menu
  items with the same text).

## 2. The player's inputs

### 2.1 The ranked priorities

`optimize_rank` holds the priorities, highest first, as words separated by
spaces. The six words and what each one turns:

| Word | Priority | Cvars turned | Capability (where false: not tried, and the report and menu give the reason) |
|---|---|---|---|
| `resolution` | Resolution | `r_metalfx_scale`, the 3D view's internal size under a scaler. The output size (`r_mode`, `r_customwidth`, `r_customheight`) is never changed: it needs a `vid_restart`. | a scaler in `r_metalfx_modes` (bits 2, 4 or 8) |
| `rt` | Ray-traced effects | the switches `r_rt_shadows`, `r_rt_sun`, `r_rt_ao`, `r_rt_water`, `r_rt_gunshadow`; their cost settings `r_rt_ao_rays`, `r_rt_ao_scale`, `r_rt_water_hits` | `r_rt_available 1` |
| `hdr` | HDR | `r_hdr`. `r_hdr_peak` is the player's and is not turned. | `VidRules_Hdr` enables "HDR" (`VIDHDR_ITEM_ON`): extended range, from `r_hdr_headroom` |
| `upscale` | AI upscaling (MetalFX) | `r_metalfx` `1` and `2` | the mode's bit in `r_metalfx_modes` (`2` for `1`, `4` for `2`) |
| `denoise` | Denoising | `r_metalfx 3`, the denoised scaler. `r_rt_denoiser`: to locate (only the menu reads it, `VIDITEM_DENOISER` in `src/client/menu/vidrules.c` and `videomenu.c`; no code creates it until TASK113). `r_rt_temporal`: to locate (no code names it; TASK59, #102). Each is added to the ladder when it lands. | `r_metalfx_modes` bit `8`; for the others, their own capability when they land |
| `presentation` | Presentation | none: `r_presentation` is not turned (section 4.4) | `PresentationRule` (`src/client/menu/videomenu.c`) over `VidRules_PresentationMode` |

Where each name was checked:

- `r_metalfx`, `r_metalfx_scale` and `r_metalfx_modes` are created in
  `RM_FX_Init` (`src/client/refresh/metal/rm_fxscene.c`). `r_metalfx` is
  read through `RM_Effect` in `RM_FX_Read`. `r_metalfx_scale` is read
  directly in `RM_FX_Begin` (`rm_fx_scale(r_metalfx_scale->value, ...)`),
  so the B side cannot carry it today; TASK203 changes that read.
- The ray-tracing switches and `r_rt_ao_scale` are `rt_controls` in
  `src/client/menu/vidrules.c`; `r_rt_ao_rays`, `r_rt_water_hits`,
  `r_metalfx`, `r_hdr` and `r_hdr_peak` are `rt_tune` there. Each is read
  through `RM_Effect` by its scene file (`rm_rtshadowscene.c`,
  `rm_rtsunscene.c`, `rm_rtaoscene.c`, `rm_rtwaterscene.c`, `rm_rtscene.c`
  for the gun shadow, `rm_main.c` for `r_hdr`).
- `r_rt_available` is set by `RM_RTG_Frame` (`src/client/refresh/metal/rm_main.c`)
  from `rm_rtg_traces`. `r_hdr_headroom` is set by `R_HdrHeadroomReport`
  there.

A word left out of `optimize_rank` is not turned: its cvars stay at the
player's values in every trial. A word given twice, or one not in the
table, is refused with the list of words. The default is the order in
the card: `rt resolution hdr upscale denoise presentation`.

### 2.2 The target

`optimize_target` is a frame rate (`60`) or a frame time with an `ms`
suffix (`12ms`). A rate is turned into a frame time (`1000 / rate` ms);
the search compares frame times only.

`optimize_stat` (a name this plan adds) says which statistic of a trial's
frame times is compared with the target: `mean`, or `p` and a percentile
from 50 to 99 (`p95`). The default is `p95`. The reason: the mean hides
the hitches the player sees, and the 95th percentile still leaves out the
slowest one frame in twenty, so one slow frame (a texture upload, a
sound load) does not decide a trial. The default is reversible: it is one
cvar default, and TASK203's runbook records both statistics for every
trial so the choice can be revisited.

### 2.3 The caps

- `optimize_rounds`: the most timed trials in a run. Default `10`.
- `optimize_seconds`: the most wall-clock seconds a run takes, the probe
  (section 3.4) included. Default `300`.
- `optimize_warmup`: the seconds at the start of each trial whose frames
  are left out of its statistic. Default `2`.
- `optimize_demo`: the demo played. Default `demo1.dm2`.

These defaults are proposals that TASK203 may change with a reason in its
runbook. They are the same on every device (section 9).

## 3. The benchmark

### 3.1 `timedemo` on `demo1.dm2`, not a flythrough

Settled: each trial is `timedemo 1` and `demomap demo1.dm2`.

- It exists and draws the same frames every time. With `timedemo` set,
  `Qcommon_Frame` (`src/common/frame.c`) skips the frame throttle, so the
  demo plays as fast as the frames are drawn. The pin harness plays the
  same demo and records the same count every time: `demo1.dm2` renders 689
  frames (`doc/100_renderpins.md`, the `timedemo.txt` line). So every trial
  draws the same frames, whatever its speed.
- It is real play: the world, models, particles, water and the HUD, the
  mix a player's frame has. A fixed in-engine flythrough would need new
  code (a camera path, a scripted map load) and would leave out the models
  and effects a demo carries.
- It is the stock demo that ships with the game data, so it is on every
  device that can play the game. If it is missing, the run fails with the
  reason (section 5.3).

Its costs, said by the command and the menu before a run starts: it ends
the current game (`demomap` is `SV_DemoMap_f` in `src/server/sv_cmd.c`,
which calls `SV_Map`). `timedemo` is one of the cheat cvars that
`CL_FixCvarCheats` (`src/client/cl_main.c`) puts back to `0` when
`CS_MAXCLIENTS` is neither empty nor `1`; the pin harness's `timedemo`
runs of `demo1.dm2` show that it holds there.

### 3.2 Per-frame times for a percentile

Today `timedemo` gives only a mean. `V_RenderView`
(`src/client/cl_view.c`) counts the frames (`cl.timedemo_frames`) and keeps
the first frame's `Sys_Milliseconds` in `cl.timedemo_start`; `CL_Disconnect`
(`src/client/cl_network.c`) prints `<n> frames, <s> seconds: <fps> fps` when
the demo ends. A percentile needs each frame's time.

TASK203 adds two calls, each made only while a run is on (so nothing
differs outside one, and every pin stays the same):

- In `V_RenderView`, inside the `if (cl_timedemo->value)` block that counts
  the frames: the frame's end time from `Sys_Microseconds`
  (`src/backends/unix/system.c`), handed to
  `src/client/menu/optimize.c`. It keeps the differences between
  consecutive frames in a fixed array (no allocation per frame) and drops
  those inside the first `optimize_warmup` seconds.
- In `CL_Disconnect`, beside the timedemo line: the trial's end.

### 3.3 The presented rate and the cost hints, without `ref.h`

The rendered rate is not the whole story: a frame may be drawn and not
presented. TASK161 made `ref_metal` keep the presented rate over a window
(`mtl_rate_t` and `mtl_rate_frame` in
`src/client/refresh/metal/common/mtl_rate.h`), fed in `RI_EndFrame`
(`rm_main.c`) from `rmb_rate_counts`, but only while `metal_stats` is set,
and printed only at the renderer's close (`RM_Close`;
`docs/runbook/TASK161.md`).

The renderer hands values to the client through status cvars it sets
itself, which the client and menu read with `Cvar_VariableValue`. Three
precedents: `r_rt_available` (`RM_RTG_Frame`, put back when written),
`r_hdr_headroom` (`R_HdrHeadroomReport`, written when it changes) and
`r_metalfx_modes` (`RM_FX_Init`, put back when written). The optimizer
uses the same pattern, with three names this plan confirms:

- `optimize_state`, set by the client (`optimize.c`), read only, put back
  when written: `0` no run, `1` a timed trial, `2` the probe. The renderer
  reads it; it changes nothing in the renderer while `0`.
- `metal_presented`, set by `ref_metal` in `RI_EndFrame` while
  `optimize_state` is not `0`: the frames presented since the renderer
  started (`rmb_rate_counts`' frames less those without a drawable), read
  only. `optimize.c` reads it after the warm-up and at the trial's end, and
  divides the difference by its own `Sys_Microseconds` time.
- `metal_rt_cost`, set by `ref_metal` while `optimize_state` is `2`: the
  mean GPU time of each ray-traced pass so far in the probe, as
  `shadows=<ms> sun=<ms> ao=<ms> water=<ms>`. The per-pass GPU times are
  the ones the `r_rt_stats 2` lines print (`rm_rts_stats_t` in
  `header/rm_rtshadow.h`, `rm_rtsun_stats_t` in `header/rm_rtsun.h`,
  `rm_rtao_stats_t` in `header/rm_rtao.h`, `rm_rtw_stats_t` in
  `header/rm_rtwater.h`; read today only when `r_rt_stats` is `2` or more,
  as in `rm_rtaoscene.c`). The probe asks for them through
  `optimize_state 2`; `r_rt_stats` itself is not written and nothing is
  printed.

None of the three starts with `r_rt_`, so none joins `rtg_cvars`, the
table of every `r_rt_*` cvar in `src/client/refresh/metal/rm_rtgate.c`.
All three are documented in `doc/040_cvarlist.md` (TASK203).

### 3.4 The probe

The first play of a run is the probe: the starting candidate (section 4.1)
on the B side with `optimize_state 2`. It is not a timed trial and does
not count against `optimize_rounds`. It gives the cost hints, and it
takes the start-of-process slow level: the ray-tracing design measured
GPU work at the start of a process running about 3.5 times slower for
about 0.1 s of GPU work (section 8, "Levels at the start of a process",
and finding 4 of its verdicts). The warm-up of each trial covers the
hypothesis that the level recurs after a map load. Where `r_rt_available`
is `0` or `rt` is not ranked, the probe still plays (for the warm start),
and the fixed order of section 4.1 (the `rt` bullet) is used.

## 4. The search

The search is pure: `src/client/menu/optsearch.c` (TASK203) takes the
ranked list, each priority's ladder, the capabilities, the target and
statistic, the caps, the cost hints and the trials measured so far, and
gives the next candidate, or the result with its report lines. It reads
no cvar, file, timer or renderer state. The elapsed time is an input (each
trial's measured duration), so the time cap is pure too.

### 4.1 The ladders, dearest step first

Each ranked priority has a ladder from its dearest step to its cheapest.
The first candidate is the top of every ladder whose capability is true;
a priority with a false capability, or one not ranked, keeps the player's
value. The player's current settings are not the starting point, because
they may be stock, with nothing to give up; they stay as A, unchanged.

- **`rt`**: first its cost settings, from the dearest to the cheapest, then
  its switches. Top: every switch `1`, `r_rt_ao_rays 2`,
  `r_rt_ao_scale 1`, `r_rt_water_hits 1`. The cost settings step down
  first (`r_rt_water_hits` `1` to `0`, `r_rt_ao_rays` `2` to `1`,
  `r_rt_ao_scale` `1`, `0.5`, `0.33`, the menu's "full", "1/2" and "1/3",
  `rt_ao_scale_names` in `vidrules.c`), because each keeps its effect. Then the switches turn off one at a time, the dearest first by
  the probe's `metal_rt_cost`. Where there are no hints, the fixed order
  is `r_rt_ao`, `r_rt_water`, `r_rt_sun`, `r_rt_shadows`, `r_rt_gunshadow`.
  That order is this plan's choice, not a measurement. The gun shadow
  goes last because it has no `r_rt_stats 2` line of its own, so no hint
  can place it.
- **`hdr`**: `r_hdr` `1`, then `0`.
- **`resolution`**: `r_metalfx_scale` `1`, `0.75`, `0.5`, `0.34`. The
  renderer keeps any value between the scaler's largest ratio and `1`
  (`rm_fx_scale`, `doc/040_cvarlist.md`), so on a GPU with a smaller
  ratio the last steps may draw the same size; the search does not need to
  know the ratio. The family default (`r_metalfx_scale 0`) is never a step:
  which size it picks is the device-class question of section 9.
- **`upscale`**: `r_metalfx` `2` (temporal), then `1` (spatial), each only
  where `r_metalfx_modes` has it.
- **`denoise`**: `r_metalfx` `3`, then down to the `upscale` ladder's top.
  `r_rt_denoiser` and `r_rt_temporal` join as on-then-off steps when they
  land.

How the resolution and upscaling ladders interact (settled). Drawing at
full size without a scaler costs more than drawing smaller and scaling,
so giving up upscaling must never mean "draw at full size". The rule is
one rule, a coupled move, and not a second ladder:

1. A `resolution` step below `1` raises `r_metalfx` to at least `1`
   (spatial). The coupling is applied when a candidate is built: the
   candidate's `r_metalfx` is `max(wanted, 1)` whenever its
   `r_metalfx_scale` is below `1`. The `upscale` priority itself is not
   changed: it still stands at its own step (say `0`), and the report says
   "r_metalfx 1: forced on by resolution 0.34".
2. `r_metalfx 0` is a step of `upscale` only while `resolution` is at `1`,
   or is not ranked and the player's own `r_metalfx_scale` is `1`. There it
   is the cheapest step, since a scaler at full size is all cost.
3. If `upscale` is not ranked and the player's `r_metalfx` is `0`, the
   `resolution` ladder is just `1`, and the report says that the view is
   drawn at full size because no scaler is chosen.
4. `r_metalfx` is one cvar for both `upscale` and `denoise`. While `denoise`
   holds `3` (ranked above `upscale`, and not yet given up), `upscale`
   cannot step down without taking `denoise` with it, so it has no step of
   its own; the report says so. While `denoise` ranks below `upscale`, its
   `3` to `2` step comes first, as for any lower priority.

Why a coupled move, and not one joint ladder over (`r_metalfx_scale`,
`r_metalfx`): the player ranks priorities, and a joint ladder would merge
two of them into one rank, so the player could no longer say "keep the
upscaler, give up resolution". The coupling keeps both ranks and costs one
`max`.

How the search treats it. A candidate is identified by the cvar values it
sets after the coupling, not by the ladder steps that produced it. So:

- The "already measured" lookup keys on the coupled values. A `resolution`
  step to `0.34` with `upscale` at its `0` step, and the same step with
  `upscale` at `1`, are one candidate (`r_metalfx_scale 0.34`,
  `r_metalfx 1`); the second is looked up, not timed.
- The bisection of a priority walks that priority's own ladder positions;
  each position is turned into a candidate by the coupling. Two positions
  that give the same candidate cost one trial. This keeps the bisection
  monotone: a lower `resolution` position never lowers `r_metalfx` below
  `1`, so its cost never rises.
- The coupling never makes `upscale` give up a step it was not asked to.
  A later `resolution` give-up applies the coupling on top of whatever
  `upscale` settled on, and if that trial passes, the report shows the
  forced `r_metalfx 1`.

### 4.2 Giving up, lowest priority first

The first candidate is the first timed trial. If it meets the target, the
search ends there: nothing is given up, and the report says so. (The probe
of section 3.4 plays the same configuration untimed and is not a trial.)
Otherwise the search takes the lowest-ranked priority that still has a
step and moves it straight to its cheapest step. One trial:

- If that meets the target, the search bisects that priority's ladder for
  the dearest step that still meets it, one trial per halving.
- If not, that priority stays at its cheapest and the search moves to the
  next priority up.

A configuration already measured is never measured again: the search
looks it up, by the coupled values (section 4.1). Bisection assumes each
ladder costs less at each step down. The ladders are built that way, and
a measurement that breaks it under noise still gives the same decisions
for the same inputs.

The round count, worked for the default rank (`rt resolution hdr upscale
denoise presentation`). `presentation` has no step (section 4.4), so five
priorities can be given up. The `rt` ladder has ten positions (0 to 9):
the top, `r_rt_water_hits 0`, `r_rt_ao_rays 1`, `r_rt_ao_scale 0.5`,
`0.33`, then the five switches off; nine steps below the top. A bisection
for the dearest passing position, when the cheapest is measured and passes,
takes at most `ceil(log2(n + 1))` trials for `n` unmeasured positions: 4
for `rt` (9 positions), 2 for `resolution` (3) and `upscale` (2), 1 for
`hdr` and `denoise` (1 each).

- Best case: the first candidate meets the target. 1 round.
- Settling on the lowest priority: `denoise` meets it at once. 1 (first) +
  1 (give up) + 1 (bisect) = 3, leaving 7 for buy-back.
- Worst case: nothing above `rt` meets it, and `rt` does. 1 (first) + 5
  (one give-up each for `denoise`, `upscale`, `hdr`, `resolution`, `rt`)
  + 4 (bisect `rt`) = **10**. That is exactly `optimize_rounds` 10, with no
  round left for buy-back. Buy-back gets rounds only when the run settles
  on a lower priority or the bisection ends early. A longer `rt` ladder
  (the `r_rt_denoiser` and `r_rt_temporal` steps of section 4.1, when they
  land) pushes the worst case over 10.

So the default can be reached and, with a longer ladder, passed. The cap
then ends the run with the best so far (section 5.3). If the cap falls
inside the `rt` bisection, the best so far is the cheapest `rt` position,
which was measured first and met the target, or the closest configuration
when nothing met it (section 4.5). The default stays `10`: the worst case
fits today, and a longer default lengthens the common run without helping
it. TASK203 may raise it with a reason in its runbook.

Worked trace, default rank, target `p95 <= 16.7 ms`, `rt` ladder as above.
Each row is one timed trial. The numbers are the example's, not
measurements.

| # | Candidate | Effective cvars after the coupling | p95 | Result |
|---|---|---|---|---|
| 1 | first: every ladder at its top | `r_metalfx 3`, `r_metalfx_scale 1`, `r_hdr 1`, `rt` position 0 | 30.0 | miss |
| 2 | `denoise` to its cheapest | `r_metalfx 2` | 26.0 | miss, stays |
| 3 | `upscale` to its cheapest; `resolution` is at `1`, so `0` is allowed | `r_metalfx 0` | 24.0 | miss, stays |
| 4 | `hdr` to its cheapest | `r_hdr 0` | 22.0 | miss, stays |
| 5 | `resolution` to its cheapest; `upscale` stands at `0`, so the coupling forces `1` | `r_metalfx_scale 0.34`, `r_metalfx 1` | 18.0 | miss, stays |
| 6 | `rt` to its cheapest (position 9) | the five switches `0`, cost cvars at their cheapest | 12.0 | meets |
| 7 | bisect `rt`, position 4 | `r_rt_ao_scale 0.33` and the cheaper cost steps | 15.0 | meets |
| 8 | bisect `rt`, position 2 | `r_rt_ao_rays 1` | 17.0 | miss |
| 9 | bisect `rt`, position 3 | `r_rt_ao_scale 0.5` | 16.0 | meets |
| 10 | buy back `resolution` one step, `0.5`, under `rt` position 3 | `r_metalfx_scale 0.5`, `r_metalfx 1` | 16.5 | meets |

After trial 9 the `rt` bounds meet (2 misses, 3 meets), so the bisection
used 3 trials and the run is at 9 rounds. Trial 10 is the buy-back of
section 4.3: it restores the highest-ranked given-up priority below `rt`,
`resolution`, one step, and keeps it because it meets the target. The cap
(10) ends the run. The result: `rt` position 3, `resolution` `0.5`,
`r_metalfx 1` (the `upscale` priority stands at `0`, forced to `1` while
`resolution` is below `1`, and the report says so), `r_hdr 0`. TASK203's
tests follow this table: the same inputs give the same ten rows in the
same order, and a candidate whose coupled values equal an earlier one is
looked up, not timed.

### 4.3 Buying back, within the round cap

Once a configuration meets the target, the rounds left are spent buying
back. The given-up steps of the priorities below the one the bisection
settled are each at their cheapest. The search restores them one step at
a time, the highest-ranked first, and keeps a step that still meets the
target. A step that misses is undone, and that priority is not tried
again. Buying back stops at the round cap, the time cap, or when no
priority has a step left to try.

### 4.4 Presentation is left alone (settled)

The search never changes `r_presentation`. It only reports its effect.
The reasons:

- It needs a `vid_restart` (`rt_tune`'s row has `restart` set, and
  `PresetRow` in `vidrules.c` leaves restart rows out of a preset). A
  restart in a run rebuilds the renderer, drops the MetalFX history and
  reloads the textures, so a trial after it would not be like the others.
- It is an overlay that sets other rows. `RM_Effect` (`rm_main.c`) passes
  each value through `rm_pres_value` (`rm_rtgate.c`): Enhanced reads the
  four ray-traced switches as `1`, and Retro reads every switch and
  `r_metalfx` as `0` (`doc/040_cvarlist.md`, `r_presentation`). Under
  either, those rows are not the search's to turn. They are left untried,
  with the menu's reason, "set by the presentation".
- It is a look the player chose, not a cost ladder.

`presentation` in `optimize_rank` therefore changes only the report. When
the presentation in force sets a row that a lower-ranked priority would
have given up, the report says which, and what Stock would let the search
try. A later card may add trials that restart into another presentation;
none is planned.

### 4.5 What is kept, and what is reported

- **Kept:** the best measured configuration that meets the target. "Best"
  is compared priority by priority, highest-ranked first: more of a
  higher-ranked priority (a higher step on its ladder) wins, whatever the
  lower ones are. When two configurations are equal on every priority,
  the faster one wins.
- **Nothing meets the target:** the result is the measured configuration
  closest to it (the smallest statistic), with the line `closest: <stat>
  against <target>` and a line naming what limits it. The candidates for
  that line, in the order they are checked:
  1. every ranked priority is at its cheapest step, so the cost left is
     the stock raster at this output size, which the optimizer does not
     change (a `vid_restart` setting);
  2. the presented rate is below the rendered rate by more than the noise
     band, so the display's present rate limits it;
  3. a priority not tried, with its capability's reason;
  4. a row the presentation sets (section 4.4).
- **The report:** one line for each trial (its candidate as changes from
  the first, its statistic, mean, p95 and presented rate) and one line for
  each change in the result and why ("r_rt_ao 0: ao is the lowest-ranked
  priority with a step, and 14.2 ms missed 16.7 ms"). It is printed when
  the run ends, and `optimize report` prints it again.

### 4.6 Determinism and noise

The search is deterministic: the same inputs (the rank, ladders,
capabilities, target, caps, hints and measurements) give the same
candidates in the same order and the same result. TASK203's unit tests
run it twice on recorded measurements.

The measurements are not deterministic: they vary from run to run. The
search tolerates that in two ways:

- **The pass rule.** A trial passes when `stat <= target`, exactly. A
  statistic above the target misses it, however little: there is no "close
  enough" and the band does not move the line.
- **A noise band.** The band is used in two places only: two
  configurations whose statistics differ by less than the band count as
  equally fast when choosing the best (section 4.5), and the presented
  rate is below the rendered rate "by more than the band" in the limit
  line of section 4.5. The band starts at 3 % of the target.
  TASK203 sets it from a record of its own: the same configuration timed
  three times on one device, written in its runbook. That record is
  information, never an acceptance criterion (decision 0014).
- **The warm-up and the probe.** These two keep the start-of-process level
  out of every timed trial (section 3.4).

## 5. The command and its runs

### 5.1 Names (confirmed)

- The command: `optimize run [name]`, `optimize cancel`, `optimize report`,
  `optimize undo`. `undo` is added to the card's proposal for the undo of
  section 6.
- The cvars: `optimize_rank`, `optimize_target`, `optimize_stat`,
  `optimize_rounds`, `optimize_seconds`, `optimize_warmup` and
  `optimize_demo`, each saved with the configuration (`CVAR_ARCHIVE`), and
  the read-only status cvar `optimize_state`. The renderer's status cvars
  are `metal_presented` and `metal_rt_cost`.
- No name is in use on `main` (no `"optimize` or `"metal_presented` string
  in `src`). `sv_optimize_sp_loadtime` and `sv_optimize_mp_loadtime` are
  unrelated server cvars.

Every one goes into `doc/040_cvarlist.md` or `doc/050_commands.md`
(`tests/check-cvar-docs.sh`).

### 5.2 A run

1. Refuse, with the reason, under another renderer, with a run already on,
   or when `optimize_rank` or `optimize_target` does not parse.
2. Say that the benchmark ends the current game.
3. Save the B side and A/B state as they are (section 6). Save the
   player's values as the preset `before-optimize`.
4. Play the probe (section 3.4), then the trials, each with its candidate
   published as the B side.
5. At the end: save the result as the preset `name` (default `optimized`),
   make it the B side with `rt_ab` on B, print the report, and set
   `optimize_state 0`.

### 5.3 Cancel and failure

`optimize cancel`, or a trial that fails, stops the run. It puts back the
B side and A/B state saved in step 3, sets `optimize_state 0`, and says
why. A trial fails when:

- the demo is missing (`demomap` brings no frame within the time cap);
- the renderer changed (`vid_renderer` is not `metal`, or a `vid_restart`
  happened in the run);
- a disconnect came before the demo's end (fewer frames than the probe
  drew);
- the time cap ended it.

A failure after at least one trial reports the best result so far, and
saves it only when one met the target or a closest result exists. A run
never leaves a candidate on the B side.

## 6. Applying the result without overwriting the player's settings

The precedent is TASK195's `rt_preset` (`src/client/menu/rtpreset.c`,
`doc/050_commands.md`):

- `RTPreset_Save` writes the tuning cvars, the rows of
  `VidRules_PresetCvar` (`vidrules.c`: the ray-tracing screen's rows, then
  the `rt_tune` rows that are values, as `PresetRow` picks them) to
  `<writedir>/presets/rt-<name>.cfg`.
- `RTPreset_LoadAB` makes a preset the B side and writes no tuning cvar,
  only `r_rt_ab`, which `RTP_Publish` fills as `B: preset <name>|<cvar>=<value>;...`.
  The renderer reads every tuning cvar through `RM_Effect`, which takes the
  B value from `r_rt_ab` when it holds one.
- `RTPreset_Load` is the one that writes the cvars: the player's accept.

The optimizer:

- **Publishes each trial on the B side.** TASK203 adds an entry point in
  `rtpreset.c` that publishes a given set of values as B, and one that
  saves a given set as a preset. `rt_preset`'s behaviour is otherwise
  unchanged. A trial's candidate is never written to a tuning cvar.
- **Saves the result as a named preset** in the `rt_preset save` format,
  and makes it B. The player flips A and B with `rt_ab`. That is the A/B.
- **Accepting is the player's own `rt_preset load <name>`.** The optimizer
  never calls it.
- **The undo.** `optimize undo` puts back the B side and A/B state from
  before the last run. The preset `before-optimize` (step 3) holds the
  player's values as they were, so `rt_preset load before-optimize` undoes
  an accept. A preset file is not a setting. `optimize run` replaces only
  the file it names, and the report says when it replaced one.

`r_metalfx_scale` is not a preset row today. TASK203 adds it as a row of `rt_tune`
in `vidrules.c` (the only table besides `rt_controls` that `PresetRow`
reads; `rt_controls` holds the switches, and the scale is a value like
`r_metalfx` and `r_hdr`, which are `rt_tune` rows), and reads it through
`RM_Effect` in `RM_FX_Begin`. A row of `rt_tune` is also an item of the RT
tuning screen, so the scale becomes a new item there, and
`VidRules_TuneNum` grows by one: `test_vidrules.c`'s item count and its
loop over the tuning items change with it (TASK203's tests, section 10). A preset without its
line loads as before: `RTP_Read` keeps the current value for a row the
file does not set.

## 7. Platforms

The optimizer runs on macOS, iOS and iPadOS, tvOS and visionOS. The
search and the menu read capabilities only, never a platform or a GPU
family. What differs comes in through the same capability cvars:

- **Ray tracing:** `r_rt_available` is `0` where the device does not trace
  rays. Then `rt` and `denoise`'s ray-traced steps are not tried, with "ray
  tracing is not available on this device".
- **MetalFX:** `r_metalfx_modes` says which scalers the GPU has (MetalFX's
  `supportsDevice:` for each, in `rmb_metalfx.m`). It is `1` (none) where
  MetalFX is missing, as on the iOS, tvOS and visionOS simulator SDKs,
  which build that file's stubs. The card expects no MetalFX scaler on
  visionOS. This plan does not assume it: if the mask has no scaler there,
  `resolution`, `upscale` and `denoise` are not tried, with "no MetalFX
  scaler on this device".
- **HDR:** `VidRules_Hdr` (`vidrules.c`, `vidhdrplat_t` in
  `header/vidrules.h`) greys "HDR" on Apple TV ("not built for Apple TV
  yet"), on visionOS ("no headroom reading on visionOS") and on a display
  with no extended range. The optimizer takes the same rule: where it does
  not enable the item, `hdr` is not tried and the report gives that rule's
  text.
- **Shown, never hidden.** The house rule (`header/vidrules.h`, the header
  comment: "An item that does not apply is greyed with its reason, never
  hidden"; `docs/runbook/TASK195.md`) holds for every optimizer item in
  TASK204's screen. A setting that cannot run on the device is shown
  greyed with its reason and is not tried. The item count is the same
  under every renderer and capability.
- **Nothing to turn.** On a device where no ranked priority can run, a run
  still plays the probe and one trial, and reports the measured statistic
  and that nothing could be turned, with each reason.

## 8. The "to decide" items

| Item | Answer | Reason |
|---|---|---|
| `timedemo` on `demo1.dm2`, or a fixed flythrough | `timedemo` on `demo1.dm2` | section 3.1: it exists, draws the same 689 frames every time, and is real play |
| Presentation: restart into another on the B side, or leave it and report | Leave it; report its effect | section 4.4: a restart breaks trial comparability, the presentation overrides other rows, and it is a look, not a cost |
| How the resolution and upscaling ladders interact | One rule, a coupled move: a `resolution` step below `1` raises `r_metalfx` to at least `1`; candidates are keyed by the coupled values | section 4.1: drawing at full size without a scaler costs more than drawing smaller and scaling, and a joint ladder would merge two ranks |
| How a higher priority is bought back | Bisect the settled priority, then restore lower priorities one step each, the highest-ranked first, within the caps | sections 4.2 and 4.3 |
| The command and cvar names | Confirmed, plus `optimize undo`, `optimize_stat`, `optimize_state`, `metal_presented`, `metal_rt_cost` | section 5.1 |
| The statistic | `optimize_stat`, default `p95` | section 2.2; reversible, one default |
| The starting point | The top of each ranked ladder the device can run | section 4.1; the player's values stay as A |
| May default settings differ by device class? | **The owner's call**; not decided here | section 9 |

## 9. The owner's open question

Decision 0014 leaves one question open: "May default settings still differ
by device class, as long as nothing is forced or hidden?" This plan does
not answer it. Nothing in sections 1 to 8 depends on the answer:

- The search starts from the top of each ladder (section 4.1), not from a
  default.
- The ladders hold no device-class step (`r_metalfx_scale 0` is not one).
- The caps' defaults are the same on every device (section 2.3).

The question decides TASK205, and only TASK205:

- **If the answer is yes:** TASK205 adds a table of starting candidates by
  device class, built from the measured costs (#539, RT-primary on an
  apple7 Mac and an iPhone; #249, ray-query and MetalFX costs on apple7,
  if they are kept and run), read only when a run starts. The search then
  starts from the table's entry where one exists, which saves rounds, and
  as now where none does. The owner also picks which of the card's two
  readings applies. Under the first, the answer covers the shipped cvar
  defaults, so TASK205 may also set defaults by device class. Under the
  second, it covers only the optimizer's first candidate, and every
  shipped default stays one value for every device. Either way nothing is
  greyed or hidden by device class, and the same inputs still give the
  same result.
- **If the answer is no:** TASK205 is closed as not planned. The search
  starts as section 4.1 says on every device. The one existing
  device-class default, `r_metalfx_scale 0` choosing 1080 or 720 lines by
  GPU family (`rm_fx_default_rows` in `RM_FX_Begin`, `doc/040_cvarlist.md`),
  is a matter for the owner's answer and the cards that follow it, not for
  this plan.

## 10. The stages

| Stage | Card | Files | Tests | Acceptance |
|---|---|---|---|---|
| 1. The search and the command | TASK203 (#695) | `src/client/menu/optsearch.c`, `header/optsearch.h` (new: the pure search); `src/client/menu/optimize.c` (new: the command, the cvars, the trials); `rtpreset.c` and `header/vidrules.h` (publish a given set as B; save a given set); `vidrules.c` (`r_metalfx_scale` as a preset row); `rm_fxscene.c` (`r_metalfx_scale` through `RM_Effect`); `rm_main.c` (`metal_presented`, `metal_rt_cost`); `cl_view.c`, `cl_network.c` (one call each, only while a run is on); `menu.c` (the init beside `RTPreset_Init`); `Makefile` (the client and `TEST_OBJS`, beside `rtpreset.o` and `vidrules.o`, against `tests/unit/stub_client.c`); `doc/040_cvarlist.md`, `doc/050_commands.md`; its runbook | `tests/unit/test_optsearch.c`, `test_optimize.c` (new), `test_rtpreset.c` (the row list), `test_vidrules.c` (the tuning item count grows by one, for the `r_metalfx_scale` row), pins in `tests/pins/unit/`: meeting the target keeps the most of the highest-ranked; missing gives up the lowest first, the dearest step first by the hints; nothing meets gives the closest with its limit line; the first candidate meeting the target ends the run at one round; a `resolution` step below 1 forces `r_metalfx` to 1 and a candidate is looked up by its coupled values; the worked trace of section 4.2 gives its ten rows; a trial passes when `stat <= target`; deterministic run twice; each cap stops with the best so far; warm-up frames are left out; a false capability is never tried and the report says why; cancel and each failure put back B and A/B, and no tuning cvar is written; a preset without the `r_metalfx_scale` line loads as before | the tests first and failing; one manual run on a bare-metal Mac with the report's lines in `docs/runbook/TASK203.md` and A unchanged; every pin unchanged; no frame rate is a criterion |
| 2. The menu | TASK204 (#696) | `src/client/menu/videomenu.c` (the Optimize screen and its entry under Video options); `vidrules.c`, `header/vidrules.h` (its rules) | `tests/unit/test_vidrules.c`: each item's rule; a false capability greys its priority with its reason; another renderer greys every item with "the optimizer turns the Metal renderer's settings"; run greyed during a run, cancel when none; review, save, flip and accept greyed until a result exists; the item count is the same under every renderer and capability | a manual walk on a bare-metal Mac: rank, set a target, run, cancel, run again, review, flip A and B, save under a name, accept; console lines in its runbook, no frames committed; A unchanged before accept |
| 3. Starting points per device class | TASK205 (#697) | waits on section 9; if the answer is yes: the table in `optsearch.c` or a data file beside it, its source rows here | `tests/unit/test_optsearch.c`: with an entry the search starts from it; without one as before; the same inputs give the same result | the owner's answer recorded in the card and this plan first; nothing greyed or hidden by device class |

Every stage keeps the non-goals of section 1. Each passes `scripts/test.sh`.
TASK204 uses TASK203's command and cvars only through their interface.
TASK205 does nothing until the owner answers.
