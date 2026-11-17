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
  model). The nearest face decides (`mtl_rtsun_at`): a sky triangle lights the pixel; water and
  translucent triangles let the ray on, up to four; any other face (world, brush entity, alias
  model), or nothing met at all, shadows it.
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
out), and of those whose material id is the sky's (`rm_rt_material`, TASK53). Under 2 %
(`rm_rtsun_sky_enough`) the pass does not open and the view keeps its path, so the map draws as
with the effect off. The renderer prints the map's line once:

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
