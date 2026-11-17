# Ray-traced sun and sky shadow in `ref_metal` (TASK56, RT4)

RT4 adds the fourth ray-traced effect, `r_rt_sun`
([design](../plans/metal-raytracing-design.md), section 2, row RT4 of section 6). It is built in
the shape of the three that landed before it: RT3's shadows
([TASK55](TASK55.md)) for the view and the ray start, RT5's AO ([TASK57](TASK57.md)) for the
compute pass, its outputs, the light it hands the resolve, the stats line and the GPU time, and
RT6's water ([TASK58](TASK58.md)) for the masked comparison. The shared core is reused as it is:
the frame's instance structure and the map's per-triangle material ids (TASK53), the G-buffer
(TASK54) and `MTL_GPU_TIME`'s encoder timestamps.

**Status.** The pass, its cvars, the table, the host test, the scenario and the documentation
are in the branch. The `rt-sun` pin is not recorded: the owner accepts the look first
(INFORM11, #101); the frames for that are local (*Frames for the owner*).

**Open look decision (INFORM11).** The default `r_rt_sunfloor 0.5` is the owner's to settle,
with the look. At 0.5 the term is rare on `base2` (37 of 687 frames, *Effect on: the masked
comparison*), and the owner found the frames "very subtle" (2026-11-17) and is fine with a
default close to stock. The defaults stay as they are here; visible variations are TASK173's.

## What it does

With `r_rt_sun 1`, on a device with ray tracing, with the world lightmapped (`r_fullbright 0`)
and on a map whose sky is at least 2 % of the world's face area, a frame whose instance
structure was built takes the G-buffer path. Before the raster, `RM_RTSun_View` settles what the
trace needs that can fail: the view's inverse (TASK55's `rm_rts_view`) and the outputs at the
render size (`rmb_rtsun_prepare`). A compute pass after RT5's AO kernel and before the resolve
(`rm_rtsun_trace`, `rm_rtsun.metal`) takes each lightmapped world pixel (material 1) with a
depth and a normal:

- *The floor gate.* The raster's light attachment holds the stylized lightmap sample. A pixel
  whose brightest channel is at or above `r_rt_sunfloor` (default `0.5`) takes nothing and traces
  nothing (`mtl_rtsun_gated`), so bright indoor lightmaps are not blown out.
- *Facing.* `max(0, N.L)` from the stored normal and the sun's direction, 0 below 1/32
  (`mtl_rtsun_ndotl`, TASK57's `MTL_RTAO_MIN_COS`): a ray leaves the stored plane by at least 1.8
  degrees, so a pixel never shadows itself; the term left out there is under 1/32 of its peak.
- *One ray toward the sun* from the surface moved along its normal by TASK55's `0.5 + k d^2`
  (`mtl_rtao_bias`), `MTL_RTSUN_RANGE` (16384) units long, against the frame's instance structure
  with TASK57's mask bit (`MTL_RTAO_MASK`: every instance but the view weapon and the viewer's own
  model). The nearest face decides (`mtl_rtsun_at`, `mtl_rtsun_step`): a sky triangle lights the
  pixel; water and translucent triangles let the ray on, up to four of them
  (`MTL_RTSUN_LAYERS`), and what is behind the fourth still decides (a fifth such face shadows
  it), so the ray asks for at most five intersections; any other face (world,
  brush entity, alias model), or nothing met at all, shadows it.
- *The term.* Where the ray is lit: `sky_color x r_rt_sunscale x visibility x max(0, N.L)` added
  to the light the resolve would have read (`mtl_rtsun_light`), each channel at most 1, the alpha
  kept; every other pixel's light bit for bit. The light it adds to is RT5's `lightmap x AO` when
  the AO is on, the lightmap sample when not, so the resolve computes the design's
  `albedo x (lightmap x AO + dlight x vis + sun x vis)`: the AO does not scale the sun, and RT3
  adds its dynamic-light term after both. Nothing of the stock light is replaced, so nothing is
  counted twice.
- *The visibility* of the one ray is 0 or 1; the kernel keeps it in the pixel's 8-bit class
  (`R8Uint`: not traced, at or above the floor, facing away, shadowed, lit), which the
  development view and the counts read. The light it writes is `RGBA16Unorm`, which holds every
  8-bit value exactly.
- *sky_color* is the mean of the sky box's five upper faces (`dn` left out), each face's mean
  taken as it is uploaded (`image_t.mean`, `rm_rtsun_mean`), scaled so its brightest channel is 1
  (`rm_rtsun_sky_color`): the sky's hue, its strength left to `r_rt_sunscale`. White when the sky
  has no image.

*The direction.* `r_rt_sun_angles` (`pitch yaw` in degrees, default `55 45`: pitch the height
above the horizon, 0 to 90; yaw counter-clockwise from +x, 0 up to 360), unless the committed
table [`scripts/rt/sun-angles.txt`](../../scripts/rt/sun-angles.txt) has a line for the map (its
bare name, `base1` for `maps/base1.bsp`). The table's format is its own header comment; it holds
directions only, no asset data. The renderer is built with it (`.incbin` in `rm_rtsunscene.c`,
the Makefile listing the file as the object's prerequisite), so it needs no file at run time; a
bad line is left out and reported once at start. The direction is `(cos p cos y, cos p sin y,
sin p)` (`rm_rtsun_dir`).

*The sky-area skip.* Once a map, `rm_rtsun_sky_area` sums the area of the world model's
triangles from the retained buffers (the first model's index range, the brush entities left
out), and of those whose material id is the sky's (`rm_rt_material`, TASK53). It is measured
when the sun is first asked about on the map (`RM_RTSun_Wanted`, which the map load's
`RM_RT_MapLoaded` reads), and only while `r_rt_sun` is on. Under 2 % (`rm_rtsun_sky_enough`)
the sun asks for no ray-tracing scene (`rm_rtsun_wanted` is 0), so with no other effect on
nothing is built or kept for the map, a pass opened on an earlier map is closed, and the view
keeps its path: the map draws, and costs, as with the effect off. The renderer prints the map's
line once:

    ref_metal: rt sun: <map>: sky <a> of <b> square units (<p> %), the pass runs; the sun at pitch <p> yaw <y> (<source>)

*Hardware gating.* `r_rt_sun` asks for the structures through `rm_rt_wanted`, as the other
effects do: on a device without ray tracing nothing is built or allocated and the existing
report stands.

## Cvars

`r_rt_sun` (on switch, archived, default `0`), `r_rt_sunscale` (archived, `0.35`, clamped to 0 to
4), `r_rt_sunfloor` (archived, `0.5`, clamped to 0 to 1), `r_rt_sun_angles` (archived, `55 45`)
and `r_rt_sun_show` (development view: `1` the mask, black where the term was added, white
elsewhere; `2` the classes, white lit, red shadowed, blue facing away, grey above the floor, black
not traced). `r_rt_sun_show` is in the multiplayer cheat reset (`cl_main.c`), as the other
effects' views are. All are in `doc/040_cvarlist.md`. With `r_rt_stats 2` each traced frame
prints a line: the pixels, the world pixels, those above the floor, those facing away, the rays,
the lit rays and the pass's GPU time.

## The host test

`tests/metal/rm_rtsun_test.c`, run by `tests/check-ref-metal.sh`:

- *The table's parser.* Comments, blank lines, a CR before the newline, a last line without
  one; bad lines (an extra field, a `/` in the name, a missing yaw, pitch past 90 or below 0, yaw
  at 360, a name of 65 characters, a map named twice in another case) left out, counted, the
  first one's number given; a good line past the capacity counted bad; an unknown map, a part of
  a name, an empty or missing name find nothing; names compare without case.
- *The committed table* parses with no bad line.
- *Pitch and yaw.* `r_rt_sun_angles`' parser takes two decimal numbers in bounds and nothing
  else (24 bad forms refused, the outputs untouched: one number, three, a comma, letters, a
  suffix, out of either bound, `nan`, `inf`, an exponent, hex, a `#`).
- *The direction.* The axes at 0/90 and 0/0, 0/90, 0/180, 0/270, the default against its
  closed form, a unit vector never below the horizon over the bounds.
- *The sky share and the 2 % threshold.* A floor, a sky and a translucent wall, and an inline
  model's sky outside the world's range: the areas exact, the inline model counted only when its
  range is asked for; shares 0, 1.99 %, 2 %, 2.01 % and 50 % against the threshold; an index past
  the vertices, a record past the indices or not of whole triangles, a range past the buffer
  refused.
- *The floor gate.* Below, at and above the floor; the brightest channel decides; floor 0 never
  and 1 always (below 1); a channel that is not a number never; 128 of the 256 lightmap levels
  under the default.
- *The term's arithmetic.* `sky_color x scale x vis x N.L` added per channel, the 8-bit
  visibility scaling it, each channel at most 1, the alpha kept; every material and class but a
  lit world pixel bit for bit; a zero scale or N.L leaves the light; after the AO the term is
  added to `lightmap x AO`, not scaled by it.
- What a face does to a ray, `N.L` and its 1/32 cut, `sky_color` (the mean, the faces without an
  image, `dn` left out, white for none or black), the cvars' readings, the view (the same inverse,
  viewport, eye and bias as RT3's) and the stats and map lines (their exact text).

Against stubs of `rm_rtsun.c` (the WIP commit) it fails 541 of its 685 checks
(`<artifacts>/TASK56/tdd/red-against-stubs.out`); the first green run failed two: the 2 % case's
float corners and a gate that let a not-a-number green channel through, both fixed.

## Checks

On the branch (no engine run): `make test` `yq2test: 278 tests, 0 failed`;
`tests/check-ref-metal.sh` ok (sun 685 checks, 0 failed; `ref_metal` built, its scratch copy
holding `scripts/rt/` for the table); `tests/check-cvar-docs.sh`, `tests/check-doc-links.sh`,
`tests/check-no-local-identity.sh`, `tests/check-runbook-index.sh` ok. `make ref_metal client
game`: no error, no warning in the new files.

## Runs on the bare-metal host

Host: the bare-metal host (Apple M6, `supportsRaytracing`). Engine commit measured: `bf0aa793`
(pin ids `bf0aa793e737-*`); the commits after it change only this file. Every batch held the
shared timing lock (`tools/heavy.sh`), checked that no other `quake2` ran and that the screen was
unlocked before and after. Every record is `PINS_SEED=1` and under `MTL_DEBUG_LAYER=1` except the
timing runs. The views compared within one record are replays of its trace (TASK46) with cvars
rewritten at the same length (TASK58's `retrace.py`): `r_rt_sun 1` to `0` with `r_rt_gbuffer 0`
to `1` (*off*, the same G-buffer path), `r_rt_sun_show` to `1` (the mask) or `2` (the classes),
`r_rt_stats` to `2`, `r_rt_sunfloor 0.5` to `1.0`. The as-is replay gives the live record byte for
byte (23 of 23 frames). Logs, stores and tools are in `<artifacts>/TASK56/`; nothing derived from
game data is in the repository.

The three demos' maps all run the pass: `demo1.dm2` is `base2` (sky 6.31 % of the world's face
area), `demo2.dm2` `waste2` (9.46 %), `water.dm2` `base1` (11.71 %, its direction `60 135` from
the table's line, the others `55 45` from `r_rt_sun_angles`).

### Effect off

Records of `origin/main` (`65eaffb3`, exported and built in scratch) against the head, the same
settings: `world`, `rt-shadows` and `rt-ao` at `demo1.dm2` (23 frames each), `rt-shadows` and
`rt-ao` at `demo2.dm2` (21 each), `rt-water` at `water.dm2` (4). **6 records, 115 frames, every one
byte-identical to main's.** Every log holds `teardown: 0 live objects`; `rt 0/0` for `world`, and
for the effects their usual counts (`rt-shadows` 78/78 at `demo1.dm2`). So with `r_rt_sun 0`
nothing of the pass runs, and the other effects are as they were with the sun's hook between
the AO and the resolve.

### Effect on: the masked comparison

`rt-sun` at `demo1.dm2` (23 frames), at 640 x 480 and 1920 x 1080. The mask is the
`r_rt_sun_show 1` replay: white where the kernel added no term, black where it did (and the
HUD's own black pixels, which the comparison then leaves out, the safe side). TASK58's
`masked-compare.sh` (`framediff OFF ON --mask SHOW1 --threshold 0 --bad-threshold 0`) on every
frame:

| Record | Frames | Score 0 | Pixels counted a frame (no term) | Pixels left out a frame |
|---|---|---|---|---|
| `rt-sun`, 640 x 480 | 23 | 23 | 297,438 to 307,100 of 307,200 | 100 to 9,762 |
| `rt-sun`, 1920 x 1080 | 23 | 23 | 2,012,875 to 2,073,036 of 2,073,600 | 564 to 60,725 |

**Every pixel where the term was not added is byte-identical to the effect off on the
G-buffer path, on every frame.** The `r_rt_stats 2` lines of the 1080p replay (687 frames): of
1,693,683 world pixels in a typical frame, 1,306,187 are at or above the floor and 151,692 face
away; the rays traced over the run are 715,916,943, of them 1,700,160 lit, in 37 of the 687
frames. With the default floor `0.5` the term is rare on `base2`: its outdoor floors are lit
above half by the map's own sky light, and the dim pixels below the floor are mostly indoors,
where the ray meets the ceiling. That is the floor doing what the design asks (no blow-out), and
a question of look for the owner (INFORM11): the frames below include the same views at
`r_rt_sunfloor 1.0`.

### Effect on, under the threshold

**Not shown on an engine run yet.** All three demos' maps are above 2 % (above). The attempt to
make a demo standing at the start of `mine1`, `mine2` and `lab` (`pins.sh make-demo` with a
scratch demo cfg) failed twice on the harness's own rules (the harness copy needs `src/`, and
a demo cfg needs its `// pins: messages <low> <high>` line), and the card's budget ran out
there. What holds without it: under 2 % `RM_RTSun_Frame` never opens the pass and never sets
`RM_RTSun_Active`, so `RM_GB_Path` keeps the view's stock path; the host test holds the
threshold (`rm_rtsun_sky_enough`, 1.99 % refused, 2 % taken). The remaining proof is a demo on
such a map, its `rt-sun` and `world` records compared byte for byte.

### Frames for the owner

Four outdoor views of `demo1.dm2` (`base2`) at 1920 x 1080, captures 1, 8, 11 and 21 (the most
lit capture of each quarter of the run), in `<artifacts>/TASK56/frames/`: `N-off.png` (the effect
off on the G-buffer path), `N-on.png` (`r_rt_sun 1`, the defaults) and `N-on-floor1.png`
(`r_rt_sunfloor 1.0`). At the defaults only capture 21 differs from off; at floor 1 all four do.
Local only, never committed or uploaded, and not viewed by the implementer.

### Timing

One batch (`<artifacts>/TASK56/timing/`), `demo1.dm2` timedemo at 1920 x 1080, `r_vsync 0`, the
head build: a 240 s pre-warm of the effect on (206 runs, no thermal or performance warning), then
three rounds of *off* (`r_rt_sun 0`, the default path), *on* (`r_rt_sun 1`) and *on + stats*
(`r_rt_stats 2`, for the pass's GPU time), with snapshots and `freq` dumps around every run. The
screen was unlocked before and after the batch and at every run; the GPU's device utilization
was 0 to 5 % before each run; the one-minute load 3.2 to 5.4 (other lanes share the host).

| Case | fps (timedemo), median | Range | Pass GPU time (`r_rt_stats 2`, frames past 100), median |
|---|---|---|---|
| Effect off (`r_rt_sun 0`) | 7,655.6 | 7,103.1 to 8,506.2 | - |
| Effect on (`r_rt_sun 1`) | 1,085.0 | 1,073.2 to 1,088.5 | - |
| Effect on, `r_rt_stats 2` | 1,069.9 | 1,060.0 to 1,069.9 | 0.555 ms (0.554 to 0.557 over the runs; 0.28 to 1.74 a frame) |

Frequencies over the nine runs (median MHz, share at the top state): GPU 1,604 MHz, 88.1 %
(range 924 to 1,621 MHz); performance cores 4,733 MHz, 87.2 %; efficiency cores 1,568 MHz,
12.4 %. As frame times the medians are 0.131 ms off and 0.922 ms on, a gap of 0.791 ms; the
sun pass's 0.555 ms GPU time is about 70 % of that gap (and about 60 % of the frame with the sun
on). The rest of the gap, about 0.24 ms, was not timed apart: no case of this batch ran the
G-buffer path and the instance structure without the pass, so how much of it they take is not
measured here (review round 1). The demo's timedemo is short (689
frames, 0.1 to 0.7 s), so the fps are noisy at the top; the GPU time is the steadier number.
