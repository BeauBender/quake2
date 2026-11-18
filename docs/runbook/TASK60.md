# Hardware gating of ray tracing, the forced-off half on the VM (TASK60)

Card: TASK60 (#103), RT8 of [`docs/plans/metal-raytracing-design.md`](../plans/metal-raytracing-design.md)
(section 4 and row RT8 of section 6). Plan anchor: [`docs/plans/metal-renderer.md`](../plans/metal-renderer.md),
Phase E and "What the build machine can do". This lane is the forced-off half: on a device without
ray tracing `ref_metal` says so in one line, forces every ray-tracing effect cvar to 0 and draws the
frame it draws with every effect off. The other half, a ray-tracing Mac showing the compute path
selected (and, per the TASK99 finding in the card's comment, a family cap to exercise the apple6 to
apple8 selection on a device that supports everything), stays with the bare-metal host.

## What it does

The decision is a pure function, `rm_rtg_decide` in `src/client/refresh/metal/rm_rtgate.c`
(header `header/rm_rtgate.h`, host test `tests/metal/rm_rtgate_test.c`), of what `rmb_open` collects:
the Apple family bits (apple1 to apple9), `supportsRaytracing` and `supportsRaytracingFromRender`. The
family is the highest one reported. In `RI_Init` (start-up and every `vid_restart`) `rm_main.c` prints
its line after the device's three, and hands the scenes the gate's word (`rm_rtg_traces`) as their
device flag, so a device the gate refuses builds nothing even with `r_rt_stats` on.

| Device reports | Path | Line |
|---|---|---|
| apple1 to apple5, no ray tracing (this VM) | none | `ref_metal: ray tracing: none (apple5: no supportsRaytracing); the ray-tracing effects read 0` |
| apple5 with `supportsRaytracing` (edge) | none | `ref_metal: ray tracing: none (apple5: supportsRaytracing below apple6); the ray-tracing effects read 0` |
| apple6 without `supportsRaytracing` (edge) | none | `... none (apple6: no supportsRaytracing) ...` |
| apple6, apple7 (M1), apple8 with `supportsRaytracing` | compute | `ref_metal: ray tracing: compute path (apple7, supportsRaytracing)` |
| apple8 with both flags (edge: render-stage is apple9's) | compute | as above, `apple8` |
| apple9 with `supportsRaytracing` only (edge) | compute | as above, `apple9` |
| apple9 with both (M3 and later) | render-stage (runs compute) | `ref_metal: ray tracing: compute path (apple9, supportsRaytracing; render-stage available, not yet built)` |
| apple9 with `supportsRaytracingFromRender` only (edge) | none | `... none (apple9: no supportsRaytracing) ...`: no structures to trace |
| no Apple family (the simulators; edge) | none | `ref_metal: ray tracing: none (no Apple GPU family reported); the ray-tracing effects read 0` |

The reason is checked in this order: no family, no `supportsRaytracing`, a family below apple6.
Render-stage names what the device can take; the render-stage code is TASK25's, so until it lands the
compute passes are what runs there, and the line names the path in use, compute, adding that
render-stage is available and not yet built. `rm_rtg_decide` keeps its three-way answer for TASK25;
only `rm_rtg_format`'s wording of it changes then.

## The r_rt_* cvars: effect switches and settings

One table in `rm_rtgate.c` names every `r_rt_*` cvar of `ref_metal`. `tests/check-ref-metal.sh` scans
every `Cvar_Get("r_rt_` of `src/client/refresh/metal/*.c` and `*.m` (a call over several lines too)
and fails on a name without a row, a row without a name, and an effect switch whose default is not
`"0"`; that is what makes TASK56's `r_rt_sun` (and any later effect) take a row when it merges.

| Cvar | Class | Why |
|---|---|---|
| `r_rt_ao` | effect | RT5's ambient occlusion pass: its rays are what it draws |
| `r_rt_shadows` | effect | RT3's dynamic-light shadows: a ray each light and pixel |
| `r_rt_sun` | effect | RT4's sun and sky shadow (TASK56, merged after this card was measured): a ray each pixel toward the sun |
| `r_rt_water` | effect | RT6's water reflections: a ray each water pixel |
| `r_rt_ao_min`, `_range`, `_rays`, `_scale`, `_seed` | setting | read only while `r_rt_ao` runs |
| `r_rt_ao_show`, `r_rt_shadows_show`, `r_rt_sun_show`, `r_rt_water_show` | setting | development views drawn only while their pass runs |
| `r_rt_sun_angles`, `r_rt_sunfloor`, `r_rt_sunscale` | setting | read only while `r_rt_sun` runs |
| `r_rt_water_hits`, `_normal`, `_reflect` | setting | read only while `r_rt_water` runs |
| `r_rt_gbuffer`, `r_rt_motion` | setting | RT2's raster paths, which every Metal GPU runs (TASK54 built and pinned them on this VM); no ray traced, the picture the same within one level |
| `r_rt_gbuffer_scale`, `_show`, `r_rt_jitter_x`, `_y`, `r_rt_motion_test` | setting | the raster path's size, views, jitter and motion check |
| `r_rt_gunshadow` | setting | whether the view weapon enters the ray-traced scene, which no device without ray tracing builds |
| `r_rt_stats` | setting | draws nothing; without ray tracing it builds nothing and says so once |

`r_rt_scale` (RT11) does not exist yet; it is a setting, not an effect (it reads 1 there, RT11's).

## Forcing

`rm_rtg_step` is the per-frame decision, host-tested over a sequence of calls with one assertion a
case: with the path none, every effect switch whose value is not 0 (a value that is not a finite
number included) is listed to be set to 0, and also to be named in a line when its value is not the
one seen at the last step, so a line comes once per change, not per frame, even if a reset did not
take. With a path, nothing is listed. `rm_main.c` runs it once at the end of `RI_Init` and at the top
of `RI_BeginFrame`, and holds no branch of its own: it prints each line listed, then sets each switch
listed to 0. The line is `ref_metal: <cvar> reads 0: no ray tracing on this device (<reason>)`.

Neither seat is seen by the build: `RM_RTG_Init` calls `RM_RTG_Frame` too, and `&caps` for `&rtcaps`
compiles. Part (g) of `tests/check-ref-metal.sh` holds them in `rm_main.c` by source inspection,
comments and strings stripped, as part (d) holds the flush points: `RI_BeginFrame`'s first statement
after its declarations (and `(void)camera_separation;`) is `RM_RTG_Frame();`; `RI_Init` sets
`rtcaps.raytracing = rm_rtg_traces(&rm_rtg);` after `rm_rtg_decide(` and before `RM_RT_Init(&rtcaps);`, calls
`RM_RT_Init(&rtcaps);` and after it `RM_RTG_Init();`, both before `ri.Vid_MenuInit()`; and no `*_Init(`
of the file takes the raw `&caps` but `RM_FX_Init`, which reads only the families.

## The menu

Unchanged. The video menu has no on/off item for an effect: its ray-tracing items are the AO trace
size and rays, the water reflections' hit shading and the denoiser (`src/client/menu/vidrules.c`).
The first three are greyed with their reason (`needs r_rt_ao 1`, `needs r_rt_water 1`) while the
effect reads 0, which it always does on a device without ray tracing; the denoiser is always greyed
(`needs ray tracing`). No item stays enabled on such a device, so no new signal was published.

## On the VM

Measured on this VM (`Apple Paravirtual device`), 640 x 480, every 30th frame, `MTL_DEBUG_LAYER=1`,
`PINS_SEED=1` for every scenario but `cinematic` (`idlog.cin`, every 7th), `water.dm2` (from
`make-demo water`) for the three water scenarios, a scratch `PINS_STORE`. Head is this branch at
`a3d4ff98`; base is `origin/main` at `c02c14c9` (its `src/` and `Makefile` are those of `75d97f9b`,
the branch point), exported and built in scratch and recorded through `PINS_BIN`. Every engine log
holds the validation layer's banner (`Metal API Validation Enabled`, which shows its messages reach the
log grepped) and no other validation line, and `teardown: 0 live objects` with `rt 0/0`: no
ray-tracing object was made, so no ray-tracing pass ran.

These runs predate the merge of main at `40aa1e90`, which brought TASK56's sun and sky shadow
(`git diff --stat a3d4ff98 40aa1e90 -- src Makefile`: 34 files, among them `rm_rtsunscene.c` and its
back end). The merge classes `r_rt_sun` as an effect, so the gate forces it to 0 here as it does the
other three; the host test and the table check cover that row. The `rt-sun` scenario was not
recorded on this VM by this card. The start-up lines of every head run:

```
Metal device: Apple Paravirtual device
Metal supportsFamily (apple5 to apple9): apple5
Metal supportsRaytracing: no, supportsRaytracingFromRender: no
ref_metal: ray tracing: none (apple5: no supportsRaytracing); the ray-tracing effects read 0
```

Effects on against off: each `rt-*` scenario recorded on head as committed (its effect cvar 1) and
as a scratch copy with only that cvar set to 0, never committed.

| Scenario | Frames | On against off | `reads 0` lines (on, off) |
|---|---|---|---|
| `rt-ao` | 23 | 23 byte-identical | 1, 0 |
| `rt-ao-entities` | 23 | 23 byte-identical | 1, 0 |
| `rt-shadows` | 23 | 23 byte-identical | 1, 0 |
| `rt-shadows-entities` | 23 | 23 byte-identical | 1, 0 |
| `rt-water` | 4 | 4 byte-identical | 1, 0 |
| `rt-water-entities` | 4 | 4 byte-identical | 1, 0 |

Head against base, every Metal scenario: byte-identical frame for frame. The full-content scenario
`particles` (everything: entities, alias models, particles, dynamic lights, blend) is among them.

| Scenarios | Frames each | Head against base |
|---|---|---|
| `world`, `world-fullbright`, `world-lmcubic`, `world-scale8`, `world-metalfx1`, `world-metalfx2`, `world-metalfx3`, `world-metalfx-interp` | 23 | all byte-identical |
| `dlights`, `dlights-lmcubic`, `models`, `particles`, `particles-cap` | 23 | all byte-identical |
| `hud2d`, `hud2d-sharp`, `hud2d-scale8` | 23 | all byte-identical |
| `cinematic` | 12 | all byte-identical |
| `rt-ao`, `rt-ao-entities`, `rt-shadows`, `rt-shadows-entities` | 23 | all byte-identical |
| `skywater`, `rt-water`, `rt-water-entities` | 4 | all byte-identical, base's live trace replayed into head and into base (each side's `water.dm2` is its own) |

The replay of base's `rt-water` trace restores `r_rt_water 1` after the renderer has started, as a
console set would: head printed `ref_metal: r_rt_water reads 0: ...` once in 109 frames and drew
base's frames.

A console session (`+set r_rt_water 1`, then `r_rt_ao 1` at the console, then `r_rt_ao 1`,
`r_rt_shadows 1` and `vid_restart`): `r_rt_water` reads `"0"` with one line at start-up, `r_rt_ao`
reads `"0"` one frame after it was set with one line, `vid_restart` prints the report line again and
one line for each of the two switches, and the settings `r_rt_ao_scale` (`0.5`) and `r_rt_gbuffer`
(set to `1`) keep their values.

The mutants: 41 of `rm_rtgate.c` (every operand of the decision's, the step's and the bounds'
guards, the table's two kinds of row, the line format), 38 killed by the host test under ASAN and
UBSAN and 3 equivalent (a negative `n` that the `size_t` cast already refuses in the line check, a
negative `k` that never counts down to 0 in `rm_rtg_effect`, a negative `n` for which the step's loop
runs no turn); and 4 of the table check (an unclassified cvar on one line and over two lines, a row
whose cvar is gone, an effect switch defaulting to 1), each refused. The seat check refuses 11, each
naming the rule it breaks: the frame call deleted, and moved below the next statement (`frame`);
`&caps` for `&rtcaps` (`rtinit rtflag rawcaps`, the unseated `RM_RT_Init(&rtcaps);` also unseats the flag's bound); `RM_RTG_Init();` deleted, and moved after
`ri.Vid_MenuInit()` (`rtginit`); the `rtcaps.raytracing = rm_rtg_traces(&rm_rtg);` line deleted, moved
after `RM_RT_Init(&rtcaps);`, and its right side replaced with `caps.raytracing` (`rtflag`); an added `RM_SUN_Init(&caps);` (`rawcaps`); and `RI_BeginFrame` or
`RI_Init` renamed (not found). The report line's render-stage wording was mutated 4 ways (the branch
dropped, its suffix dropped, the line naming the render-stage path again, the branch taken by every
traced path), each killed by the host test. The logs, lists and tools are in
`<artifacts>/TASK60/`; nothing derived from game data is in the repository.

## Ray-tracing Mac (TASK181)

Card: TASK181 (#517), the ray-tracing half of RT8. Built from `main` at `8e7ce3f6` (release build of
`client`, `ref_metal` and `game`), run with `+set vid_renderer metal +set r_mode -1 +set r_customwidth 1280
+set r_customheight 720 +set vid_fullscreen 0 -datadir <datadir> -portable`, `+map base1`, three
`+wait 120` and `+quit`. No code changed: the line and the path are right on the device.

### (a) Defaults

The device's three start-up lines and the report line, as the engine printed them:

```
Metal device: Apple M6
Metal supportsFamily (apple5 to apple9): apple5 apple6 apple7 apple8 apple9
Metal supportsRaytracing: yes, supportsRaytracingFromRender: yes
ref_metal: ray tracing: compute path (apple9, supportsRaytracing; render-stage available, not yet built)
```

The teardown line of this run shows that no ray-tracing pass ran with the effects at their default 0:
`ref_metal: teardown: 0 live objects (created/destroyed: device 1/1, queue 2/2, view 1/1, texture 203/203, buffer 7/7, state 47/47, rt 0/0, metalfx 0/0), 0 unreleased, 0 unmatched`.

### (b) The three effects on, and `vid_restart`

`+set r_rt_ao 1 +set r_rt_shadows 1 +set r_rt_water 1`, the map loaded, then each cvar typed at the
console, then `vid_restart`, three more waits, each cvar typed again, `+quit`. Before the restart:

```
"r_rt_ao" is "1"
"r_rt_shadows" is "1"
"r_rt_water" is "1"
```

After the restart the same three lines read `"1"` again. The report line printed again after the
restart, identical to (a), with the same three device lines before it. No `reads 0` line appears in
the log of the run.

Pass evidence, from the same log: the start-up allocations of the passes (printed again after the
restart), the scene build for the map, the ray-tracing timing line, and a teardown line with ray-tracing
objects created and destroyed, where (a) had `rt 0/0`:

```
ref_metal: rt ao traced at 1280x720 (1/2): 1048576 bytes allocated
ref_metal: rt ao 2560x1440: 33947648 bytes allocated
ref_metal: rt water 2560x1440: 30670848 bytes allocated
ref_metal: rt: maps/base1.bsp: world 20495 triangles (world 19561, sky 608, water 326, alpha 0), 1921280 bytes; 34 inline models, 1272 triangles (world 1228, sky 0, water 0, alpha 44), 131456 bytes; materials 21767 b...
ref_metal: rt: maps/base1.bsp: 5 frames; gpu ms over 4 frames: alias lerp and refit min 0.283, median 0.546, p95 0.741, max 0.741; instance rebuild min 0.240, median 0.346, p95 0.431, max 0.431; busiest frame 7 alias instances, 1586 alias triangles
ref_metal: teardown: 0 live objects (created/destroyed: device 1/1, queue 2/2, view 1/1, texture 211/211, buffer 8/8, state 71/71, rt 100/100, metalfx 0/0), 0 unreleased, 0 unmatched
```

The teardown of the pass before the restart reads `rt 100/100` too (its line is interleaved with the
next start-up's allocation lines in the captured stream).

### (c) Forced off

TASK60 provides no cvar or environment variable that forces the gate off on a device that supports ray
tracing, and the card's family cap is not built, so no such run was made. The none path stays covered by
the host test and by the VM section above.
