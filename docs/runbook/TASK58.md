# Ray-traced water reflections in `ref_metal` (TASK58, RT6)

RT6 adds the third ray-traced effect, `r_rt_water`
([design](../plans/metal-raytracing-design.md), section 2, row RT6 of section 6). This file
holds the card's results. The method sections it uses are in the shared runbook: the
[shadows' proofs](../plans/metal-port-harness.md#ray-traced-shadows-from-dynamic-lights-in-ref_metal-task55-rt3)
(the replays of one trace with a cvar rewritten, the GPU time from the encoder's timestamps)
and [judging an effect's footprint](../plans/metal-port-harness.md#judging-an-effects-footprint-task133).
It is stacked on TASK57 (RT5, [its file](TASK57.md)).

**Status.** The pass, its cvars, its host test, the scenario and the documentation are in the
branch. Every check the card names passes (see *Checks*). No engine run has been made: the
host's display was taken for other use while the card was worked. The runs still to make are
listed under *Runs to make*; their results go in the sections named there.

## What it does

With `r_rt_water 1`, on a device with ray tracing and on a map whose structures hold water (a
warped triangle of the world or a brush model, `rmb_rt_map_water`), a frame whose instance
structure was built takes the G-buffer path. On a map without water nothing is traced and the
view stays on the stock path. Before the raster, `RM_RTW_View` settles what can fail: the
view's inverse and projection (`rm_rtw_view`, TASK55's `rm_rts_view` plus the forward matrix)
and the outputs at the render size (`rmb_rtw_prepare`). A compute pass runs after the lighting
resolve, the last of the ray passes, because its hit shading reads their lit result
(`rm_rtw_trace`, `rm_rtwater.metal`). For each pixel of the view:

- *The water.* A pixel of opaque water (G-buffer material 3, `mtl_rtw_is_water`) takes its point
  from the depth and its normal from the G-buffer, as RT3 and RT5 do. On any other pixel a ray
  from the eye looks for translucent water in front of the raster's surface, which the forward
  pass draws later and the G-buffer does not hold: up to that surface less TASK55's tolerance (2
  units, or 0.2 %; `mtl_rtw_eye_max`), the whole map where the raster drew the sky or nothing. At
  each face it meets (`mtl_rtw_eye_step`, on the map's per-triangle ids) water is the pixel's
  water, a translucent face or a model is passed (both are drawn over the lit image later), and a
  world face or the sky brush stops it. The translucent water's normal is its triangle's, from the
  map's vertex and index buffers (`rmb_rt_bind_geometry`), turned by the instance's transform; its
  distance from the eye is kept (`R32Float`).
- *The direction.* The flat normal, turned to face the eye, tilted by `r_rt_water_normal` (0.08)
  along the plane's texture axes (`mtl_rtw_normal`) by the stock warp's offsets at the point:
  gl1's table (`r_turbsin` as `RI_Init` halves it, over its largest value, `rm_rtw_turb_table`)
  at gl1's index (`mtl_rtw_warp_index`, rm_warp_index in floats), `s` moved by the table at `t`
  and `t` by the table at `s`, as `R_EmitWaterPolys`. The axes are qbsp's
  `TextureAxisFromPlane` (`mtl_rtw_axes`), so the coordinates are the face's texture coordinates
  for a texture at scale 1; a texinfo shift moves the phase only. The geometry stays flat. The
  ray is the view direction mirrored about the tilted normal (`mtl_rtw_reflect`), kept at least
  1.8 degrees (`MTL_RTW_MIN_COS`, a cosine of 1/32) above the flat plane.
- *The start.* The point moved along the flat normal by `0.5 + k d^2` units (`mtl_rtao_bias`,
  TASK55's rule, `k = 4 x 2^-24 / gl_znear`): above the true plane wherever the depth is within
  four float steps, and with the direction above the plane the ray's distance to it only grows,
  so a ray from the water never meets that water (the host test holds it for 50,000 starts).
- *Geometry tested.* The frame's instance structure (TASK53) with `MTL_RTW_MASK`, the AO rays'
  bit (`0x04`, which `rmb_rt_frame` sets on every instance but the view weapon and the viewer's
  own model): the world, the brush entities in their moved place and the alias models (monsters,
  players, items). Never the view weapon: it is in the structure only with `r_rt_gunshadow 1`,
  and then without the bit, as in RT5. Never the viewer's own model: the client keeps it out of
  the refdef in the first person and an instance of it would have no bit. Sprites, beams, shells
  and particles are not in the structure. Sharing the bit changes no instance's mask, so the
  shadow and AO rays meet what they met before. Every face stops a reflection ray, water and
  translucent ones too (design section 1, "opaque for reflection rays"). One bounce, no recursion.
- *The hit's colour* (`mtl_rtw_choose`, the sky lookup's choice). The hit projected into the view
  with the forward matrix: where it lands inside the view at a pixel whose depth gives a point
  within 4 units, or 1 %, of the hit along the eye's ray, the colour is that pixel of the
  resolved image, so the hit is shaded as the picture shades it: albedo times the lightmap, RT5's
  AO and RT3's dynamic-light term (with `r_rt_shadows 0` gl1's dynamic lights are in the
  lightmap). A ray that meets the sky brush or nothing takes the pixel in its direction from the
  eye (the box is drawn around the eye, so a sky pixel in that direction is the sky's texel there)
  where the screen shows the sky there. Anywhere else, a hit off the screen or hidden behind
  something nearer, there is no reflection and the water is drawn as with the effect off.
- *The blend* (`mtl_rtw_blend`). The water's RGB mixed toward the reflection's by
  `r_rt_water_reflect` (0.5), its alpha kept. Opaque water: a full-screen draw over the resolved
  target right after the kernel, blended by source alpha with the target's alpha not written,
  every other pixel discarded (`rm_fragment_rtw_blend`). Translucent water: while the view
  traced, the forward pass draws its translucent faces (`RM_PIPE_WORLD_TEXC_BLEND`) with
  `rm_fragment_rtw_trans`, the stock `rm_fragment_world_color` colour, mixed toward the reflection
  only where the fragment is the water the kernel found at its pixel (its distance within
  TASK55's tolerance, `mtl_rtw_is_trans_water`), with the stock blend, so `SURF_TRANS33`/`66`'s
  alpha composes it over what lies behind as before. A pane in front of the water, or any other
  translucent face, keeps its stock colour.

Left out, by this design: a hit the screen does not show gives no reflection (shading it would
need every map texture and the lightmaps bound to the kernel); the warp's phase follows qbsp's
axes, not the face's texinfo (a rotated or scaled water texture ripples in its own phase); a
moved translucent brush entity's water, which gl1 draws with the world's matrix
(`R_DrawAlphaSurfaces`), is not where the ray finds it and keeps its stock colour; the eye ray is
cast on every pixel of the view that is not opaque water, which is the pass's main cost.

## Cvars

`r_rt_water` (on switch, archived, default `0`), `r_rt_water_normal` (archived, `0.08`,
clamped to 0 to 1), `r_rt_water_reflect` (archived, `0.5`, clamped to 0 to 1) and
`r_rt_water_show` (development view: `1` the mask, white where no water was found and black on
the water; `2` the classes, green a reflection from the picture, blue from the sky, red none,
black not water). `r_rt_water_show` is in the multiplayer cheat reset (`cl_main.c`), as RT3's
and RT5's views are. The others cannot make anything easier to see: a reflection shows only
pixels the view already shows (its colour is read from the resolved image), so it reveals
nothing hidden. All are in `doc/040_cvarlist.md`. With `r_rt_stats 2` each traced frame prints a
line: the pixels, the opaque and the translucent water pixels, the eye rays, the water pixels
whose reflection came from the picture, from the sky and from nowhere, and the pass's GPU time.

## The mask

The masked comparison counts the pixels that must not change. Its mask is the
`r_rt_water_show 1` frame of the same trace: white (counted by `framediff --mask`, any channel
above 0) on every pixel where the kernel found no water, black on the water it found, opaque or
translucent. It is the kernel's own class byte, so it marks every pixel the blend could touch:
the opaque blend draws only where the class says opaque water with a reflection, and the
translucent draw only where it says translucent water at the fragment's distance. Where the
water is hidden behind something nearer that the eye ray does not test (the view weapon), the
mask is black but the pixel keeps its colour, so the mask is a superset of the changed pixels,
which is the safe side.

## The host test

`tests/metal/rm_rtwater_test.c`, run by `tests/check-ref-metal.sh` (152 checks):

- *The material mask.* Of the 256 ids only 3 is a water pixel; the eye ray finds water at 3,
  passes 5 and alias models, stops at every other id; the map's ids are the G-buffer's numbers;
  the eye ray's bound; the class byte; a translucent fragment matches only within the tolerance
  (a pane 8 units in front, or a face behind, does not).
- *The warp's phase.* The table is gl1's over its largest value; over 200,000 coordinates and
  times of a map and a session the float index is gl1's double one but within a float's error of
  a table step, where it is one step off; truncation toward 0 then the two's complement mask.
- *The direction.* qbsp's axes for each face; the tilted normal a unit vector within `atan(2
  amp)` of the flat one, the flat one at amplitude 0; the reflection a unit vector, at least
  `MTL_RTW_MIN_COS` above the plane, the mirror direction with the flat normal, a grazing one
  lifted in its own vertical plane, the warp moving it.
- *No self-hit.* 50,000 starts from a depth four float steps off and an `rgb10a2` normal: none
  below the true plane, no direction into it.
- *The sky lookup's choice.* Off the screen nothing; the sky's pixel only where the screen shows
  sky; a face's lit pixel only within the tolerance and never on the sky box or an empty pixel.
- *The blend.* No reflection, a weight of 0, below 0 or not a number: the colour bit for bit; the
  alpha (0.33, 0.66, 1) always kept; the mix exact; the weight clamped to 1.
- The cvars' readings, the view (RT3's inverse, viewport, eye and bias; the forward matrix times
  the inverse is the identity), the instances (the gun with `r_rt_gunshadow 1` and an
  `RF_VIEWERMODEL` entity without the bit, the rest with it) and the stats line.

Red first: the same test against a copy of `common/mtl_rtw_math.h` and `rm_rtwater.c` whose 17
math functions and 7 pure functions return 0, the input or the flat normal fails 77 of its then
147 checks (`<artifacts>/TASK58/tdd/red-against-stubs.out`).

## Checks

On the branch head (no engine run):

- `make test`: `yq2test: 163 tests, 0 failed`.
- `tests/check-ref-metal.sh`: ok (water 152 checks, 0 failed; `ref_metal` built).
- `tests/check-pins.sh`: `pins selftest: ok` (with `content_tags`: `rt-water`, companion `models`).
- `tests/check-cvar-docs.sh`, `tests/check-doc-links.sh`, `tests/check-no-local-identity.sh`: ok.
- Builds: `make client`, `make -B ref_metal` with and without `METAL_SHADERS=no`, `make
  ref_gl1 WITH_METAL_SHIM=yes`, and `make ASAN=1 client game ref_metal`: no error, no warning in
  the new files.

## Runs to make

Each under the brief's rules (screen unlocked, no other `quake2`, the timing lock, the load
before and after), artifacts in `<artifacts>/TASK58/`. Water needs `PINS_DEMO=water.dm2` from
`scripts/pins.sh make-demo water` (TASK70), the camera path of `skywater`: its first half above
the pool of base1, its second half under it.

1. **Effect off.** Every scenario recorded on the head with `MTL_DEBUG_LAYER=1`, against
   records of TASK57's head built in scratch with the same settings: the scenarios of TASK57's
   run 1 (TASK55's Proof 1, `dlights` at `PINS_DEMO=demo2.dm2 PINS_EVERY=7`, `rt-shadows`,
   `rt-shadows-entities`, `rt-ao`, `rt-ao-entities`); byte-identical expected (with `r_rt_water
   0` nothing of the pass is made, no pipeline is swapped and no instance's mask changes). The
   logs: the validation banner only, `teardown: 0 live objects`, `rt 0/0` where no effect is on.
   Also `skywater` at `PINS_DEMO=water.dm2` with `r_rt_water 0`, compared as `skywater` is
   (`--bad-threshold 0.03`, the warp follows the client clock).
2. **Effect on.** `rt-water` at `PINS_DEMO=water.dm2` (`PINS_EVERY=30`, and every 7th for more
   frames), each record's trace replayed with `r_rt_water` rewritten to `0` and `r_rt_gbuffer` to
   `1` (the off view on the same path) and with `r_rt_water_show 1` (the mask) and `2` (the
   classes). From these:
   - the masked comparison on every frame: `framediff OFF.tga ON.tga --mask SHOW1.tga --threshold
     0 --bad-threshold 0`, score 0 expected (every white pixel byte-identical). The warp follows
     the client clock, so a replay frame may be at another warp time than its record; the water
     pixels are outside the mask, so only a stall that moves the camera would show, and the run
     states the frames it compared and any it did not;
   - the share of water pixels per class from the `r_rt_stats 2` line (lit, sky, none), above
     the pool and under it;
   - `scripts/pins.sh footprint rt-water` (`--off` the off replay, `--on` the record, over the
     whole frame), the worst frame, and the band `// footprint changed <lo>..<hi>` set from it in
     `rt-water.cfg`, replacing the placeholder line; a scenario with a band and `cl_entities 0`
     needs a companion that carries the effect (`check-content`), so the card that sets it adds
     `rt-water-entities`, as TASK55 and TASK57 did;
   - the pins are not recorded: the owner accepts the look first (design section 5).
3. **Frames for the owner.** Four off/on pairs at 1920 x 1080 (`PINS_WIDTH=1920
   PINS_HEIGHT=1080`), two above the pool and two under it, with the most reflected pixels,
   converted to PNG (`sips -s format png`) as `<artifacts>/TASK58/frames/<n>-off.png` and
   `<n>-on.png`, with the classes view of each, never committed.
4. **Cost.** The pass's GPU time per frame (the `r_rt_stats 2` line's timestamps), warm (frames
   past the first 100 of each run), three runs of `rt-water` over `water.dm2` whole, at 1920 x
   1080 and at 5120 x 2880 (`PINS_HIDPI=1` at 2560 x 1440): median, p10, p90 and max, against
   design section 2's budget (no budget of its own; section 8 bounds the reflection rays at 0.88
   ms at 1080p on every pixel, and the eye ray on every pixel that is not opaque water adds about
   as much); the fps of `timedemo.txt` at 1920 x 1080, `skywater` and `rt-water` alternated,
   three rounds, on the bare-metal host (Apple M6). The measured cost goes into design section 2.
5. **Validation and ASAN.** The effect-on records above with `MTL_DEBUG_LAYER=1` (no validation
   line, a matched teardown, `teardown: 0 live objects`), and one `rt-water` record from a `make
   ASAN=1` build (no report).
