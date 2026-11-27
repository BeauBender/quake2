RT-primary's light on alias models and the view weapon (TASK191, PM9), slice 1

PM9 of the [presentation plan](../plans/presentation-modes.md) (section 4.1, "Models", and
row PM9 of section 7) lights alias models and the view weapon by traced light under the
RT-primary presentation (`r_presentation 3`). The reading the card works from: a model
pixel gets the traced terms a world pixel gets (direct light with its shadows, the dynamic
lights, the sky, the sun and the bounce) at its own point and normal, under the world's
exposure. Slice 1 holds the rules alone, as plain C and host tests. Nothing in the renderer
calls them, so no frame changes (the hold, below).

## What slice 1 adds

- `src/client/refresh/metal/common/mtl_rtm_math.h`, plain C that the Metal shading language
  compiles too (after `mtl_shader_math.h`), for the kernel of a later slice and the host test:
  - **The model colour.** `mtl_rtm_color` takes a light, a shadedot factor and the model's
    tag flags. It applies gl1's model rule in `rm_alias_shade`'s order: the `RF_MINLIGHT`
    floor of 0.1, the `RF_GLOW` pulse (0.1 sin(7 t), floored at 0.8 of the light),
    overbright (`gl1_overbrightbits`). Then comes the colour index of `rm_alias_color`, the
    shadedot factor times the light times 255, truncated and clamped to 0 .. 255. A traced
    light passes `MTL_RTM_DOT_TRACED`, 1, because its N.L at the pixel's normal takes the
    shadedot's place.
  - **The tag.** A traced model's vertex colour is replaced by a tag, which the G-buffer
    raster stores in its light attachment. The tag is a mid grey (128/255) whose alpha is
    `MTL_RTM_TAG_BASE` (160) plus the flags glow, minlight and weapon, in steps of 1/255.
    `mtl_rtm_is_tag` and `mtl_rtm_tag_flags` read it back. (Slice 3's review drops the
    grey: the tag is now the alpha alone, over the stock colour.)
  - **The weapon's depth.** `mtl_rtm_weapon_depth` turns the stored depth `d` (0 .. 0.3,
    `rm_alias_depth`'s `zfar`) into the 0 .. 1 clip depth that `rm_rts_unproject` takes,
    `d / 0.3`. `mtl_rtm_weapon_ndc` is the same as GL's -1 .. 1.
  - **The pull-back point.** The eye ray toward a weapon pixel can hit a face before it
    reaches the weapon (the weapon inside a wall). Then the pixel is shaded at
    `MTL_RTM_PULLBACK` (0.25 units) before that face, or at the eye if the face is nearer
    than that. No hit before the point (also a NaN or an infinity) keeps the point.
- `src/client/refresh/metal/rm_rtmodel.c` and `header/rm_rtmodel.h`, plain host C:
  - `rm_rtm_entity_split`: with the mode off it is `rm_gb_entity_split`. Under the mode, an
    alias model that keeps gl1's light goes forward: a shell, an `RF_FULLBRIGHT` model, an
    `RF_IR_VISIBLE` model under the goggles, or a model with `RF_DEPTHHACK` that is not the
    weapon (the crosshair model). The view weapon also goes forward in this slice; slice 3
    rasters it. Translucent models, shells and beams go forward, as now.
  - `rm_rtm_traced(flags, rdflags, extra_ok)`: a model the mode rasters, and only when its
    normals were written (`rm_gb_alias_extra`).
  - `rm_rtm_color`: the colour from a light (`mtl_rtm_color` through the minlight and gamma
    tables). A model that keeps gl1's light gets gl1's own two functions.
  - `rm_rtm_tag_verts`: writes the tag over a vertex array's colours.
  - `rm_rtm_weapon_view` and `rm_rtm_weapon_unproject`: the view weapon's inverse, built
    from `rm_weapon_projection`'s inputs (`r_gunfov`, the left hand, the water warp),
    jittered as the view's projection is (`rm_gb_jitter`) and multiplied by the world matrix
    (`rm_rts_view`). Then a weapon pixel unprojected at its depth.

## Where gl1's rule and the plan differ from the card

- **Parity holds at a shadedot of 1 only.** gl1 floors the light (minlight) and pulses it
  (glow) before it multiplies by the shadedot. A traced light already carries its N.L, so a
  traced light of point x shadedot cannot match gl1 on a minlight or glow model unless the
  shadedot is 1. The test holds three things to the bit:
  - the rule with gl1's own light and shadedot equals gl1's result, for every combination of
    the flags;
  - a traced light of point x shadedot equals gl1's result for the models without minlight
    or glow, at overbright 0, 1, 2 and 4, where a power of two keeps the product exact;
  - a traced light at a shadedot of 1 equals gl1's result for every traced model.
- **The tables are inputs.** The minlight table is a closed form, but in float with a
  truncation. The gamma table is a `pow`. The GPU cannot repeat either to the bit, so the
  kernel takes both as tables, as the host does.
- **The light attachment is 8 bits, not 16.** The G-buffer's light attachment is
  `RGBA8Unorm` (`rmb_gbuf.m`). Only RT-primary's output light is `RGBA16Unorm`. The tag is
  in steps of 1/255, so it survives both stores (k/255 is k x 257/65535 exactly) and a
  saturate. A model's own interpolated colour can be any rgb, so the tag is in the alpha. A
  rastered model is opaque, so its own alpha is 1.
- **gl1's constants are doubles.** `rm_alias_shade` compares a float against 0.1 and
  multiplies by 0.8, both in double. The header uses doubles on the host and floats under
  Metal, so the host is gl1's to the bit. Under Metal the test `s > 0.1f` can floor a
  channel that gl1's double test `s > 0.1` keeps, so one input value can change a whole
  channel, not one float ulp. Slice 2 changes the Metal test to `s >= 0.1f`, which equals
  gl1's double test exactly, since no float lies between 0.1 and 0.1f.

## The test, red then green

`tests/metal/rm_rtmodel_test.c`, built by `tests/check-ref-metal.sh` (segment 7.9) with
`rm_gbuf.c`, `rm_rtshadow.c`, `rm_rt.c` and `rm_pure.c`, under ASAN and UBSAN:

- colour parity over 1024 flag combinations (minlight, glow, fullbright, weapon, IR and the
  five shells), each with and without the goggles, over 12 lights, 9 shadedots, 4 times,
  4 overbrights and 3 tables;
- the split with the mode off, against `rm_gb_entity_split`, for all 2^19 flag combinations
  from `RF_MINLIGHT` to `RF_USE_DISGUISE`, each kind, both `r_shadows` and both goggles
  states; under the mode, the named cases and the same sweep against the rule;
- `rm_rtm_traced` for every flag combination, without and with the normals;
- the tag through 8- and 16-bit stores, opaque colours and near-tags never read as tags;
- the weapon: points projected through `rm_weapon_projection` (jittered) and the 0 .. 0.3
  depth range, then unprojected, within 1e-3, for both hands, `r_gunfov` -1 and 90, with and
  without jitter, at two view angles;
- the pull-back point.

Green: `model light 3446 checks, 0 failed, 7 of 7 mutants refused` (the hold's
check-ref-metal line: model light 3446 checks, 0 failed, 7 of 7 mutants refused).

Red: seven scratch mutants, each of which compiles and is refused. `check-ref-metal.sh`
requires all seven:

| Mutant | Change | Refused by |
| --- | --- | --- |
| `shadedot-twice` | the index `dot * dot * s * 255` | the parity with gl1's light and shadedot |
| `minlight-skipped` | the minlight floor never applied | parity, traced parity |
| `glow-sign` | `s - scale` for the pulse | parity, traced parity |
| `weapon-traced` | the weapon not sent forward under the mode | the split's weapon case and sweep |
| `fullbright-traced` | the split rasters a fullbright model | the split's fullbright cases and sweep |
| `tag-bit` | the tag's decode masks with 3 | the tag round-trip of flags 4 to 7 |
| `weapon-view-inv` | the weapon's view built from `rm_projection` | the weapon round-trip |

## The hold

No check here needs the lock: the host test with its mutants, `make test`
(`yq2test: 591 tests, 0 failed`), `check-cvar-docs`, `check-doc-links`, `check-runbook-index`, `check-pin-chars`,
and `xcrun metal -c` of a translation unit that includes `mtl_rtm_math.h` and calls each
function. The hold builds main and the branch in scratch, runs `make test` and
`check-ref-metal`, and records:

- the stock scenarios at `r_presentation 0`: 29 of 29 stock scenarios byte-identical to main;
- every committed `rtprimary-*` record, with `rtprimary-world` on `demo2.dm2` and on the
  pool demo and `rtprimary-dlights` on `demo2.dm2`: 10 of 10 RT-primary records byte-identical to main (the 7 rtprimary-* scenarios and the styles, pool and dlights-d2 demo variants).

Those results were taken against main e76c846e. After main was merged in at 227f87ea (main d7817081), `make ref_metal` built, and `tests/check-ref-metal.sh` passed with 13162 checks and 0 failed. The model-light segment ran 3446 checks with 0 failed, and 7 of 7 mutants were refused.

## Left to the next slices

- Slice 2: a kernel lights the rastered models by traced light (`r_rt_direct_models`).
- Slice 3: the view weapon is rastered under the mode, with the tag, the unprojection and
  the pull-back ray, and the glow and minlight flags are applied from the tag.
- Slice 4: model pixels join TASK190's accumulation.

## Notes for slice 2

- The kernel's tag decode also gates on material 2 (alias).
- Tag only when the normals were written.
- The depth-hack forward rule excludes `RF_WEAPONMODEL`.

# Slice 2: rastered alias models lit by traced light

Slice 2 lights the rastered alias models under RT-primary with the terms a world pixel
takes, at the model pixel's own point and normal. `r_rt_direct_models 0` keeps every
model stock-lit.

## What slice 2 adds

- **Slice 1's review notes.**
  - Under Metal the minlight floor's test is `s >= 0.1f` (`MTL_RTM_ABOVE`). The host test
    shows it equals `s > 0.1` for the 8193 floats around 0.1f, and check-ref-metal pins
    the Metal branch.
  - `rm_rtm_stock_model` is the depth-hack forward rule. It keeps the crosshair model
    stock-lit and leaves out `RF_WEAPONMODEL`.
  - `rm_rtm_tag_verts` tags only when `extra_ok` holds.
  - The kernel decodes a tag at material 2 only (`rm_rtm_px_flags`).
- **The kernel**, `rm_rtm_trace` in the new `rm_rtmodel.metal`:
  - The file includes `rm_rtdir.metal` for its static helpers and renames its two entry
    points with exactly two `#define`s. `rm_rtdir.metal` is unchanged.
  - It runs on material-2 pixels with a normal. It calls `rm_rtdir_direct`, `rm_rtdir_open`
    and `rm_rtdir_bounce` at the pixel's point and normal, and then `rm_rtdir_out_open`
    under the world's exposure and view.
  - It converts that light to the model colour with `mtl_rtm_color` at
    `MTL_RTM_DOT_TRACED`, through the host's minlight and gamma tables (one 512-byte
    buffer, from `R_Minlight` and `gammatable`).
  - It writes the light and the visibility of model pixels only. Its counts are its own,
    so nothing reaches the exposure fit.
  - The light attachment is read for the tag alone. Nothing tags a model before slice 3,
    so every pixel decodes to no flags.
- **The back end**, `rmb_rtmodel.m`:
  - It holds the pipeline, the dispatch, the counts and the GPU time.
  - It reads the frame's trace through `rmb_rtdir_frame`, a read-only getter in
    `rmb_rtdir.m` that binds nothing. TASK188's and TASK190's binding pins in
    `rm_rtdir_test.c` hold unchanged.
  - It binds the trace's view with `nself` 0, so `rm_rtdir_own` returns at once.
- **The scene**, `rm_rtmodelscene.c`:
  - `r_rt_direct_models`: default 1, archived. It is a setting of the direct light in
    `rm_rtgate.c`, because the gate's effect switches must default to 0.
  - The frame's glow pulse, overbright and tables.
  - The `r_rt_stats 2` line, `ref_metal: rt models: frame N: P model pixels, R shadow rays,
    O sky, sun and bounce rays, T ms`.
- **Wiring.**
  - The effect row `RM_RTFX_MODELS` comes after `RM_RTFX_DIRECT` and shares its switch,
    `r_rt_direct`. `rm_rtfx_test.c` now counts the gate's effect switches as the table's
    distinct switches.
  - `R_EntitySplit` uses `rm_rtm_entity_split` while `RM_TracedModels()` holds.
  - The Makefile builds the new `.metal`, `.m` and the two `.c` objects.
- **Scenarios**, all seeded like `models`:
  - `rtprimary-models`;
  - `rtprimary-models-off` (`r_rt_direct_models 0`);
  - `rtprimary-models-vis` (`r_rt_direct_show 1`).

## Choices and where the plan differs

- **The dispatch order.** The model row runs after the direct light's row, and so after
  TASK190's accumulation inside it. It writes the model pixels of the light the resolve
  reads (the trace's, or the accumulated light when `r_rt_temporal` is above 0), never the
  history. The accumulation copies every pixel that is not a traced world pixel, so it
  still accumulates world pixels only. Slice 4 moves the dispatch before the accumulation,
  as the plan says.
- **Glow and minlight models go forward** in this slice, through `rm_rtm_entity_split`,
  until slice 3 tags them. So the real gun (`RF_MINLIGHT`) still goes forward even under
  the `weapon-traced` mutant. That mutant is refused by the bare `RF_WEAPONMODEL` case,
  and the new `depthhack-weapon` mutant holds the weapon's exclusion.
- **The light's range.** The light is the world's after its clamp to 1, so a model's
  light above 1 times overbright saturates as a world pixel's does.

## The tests, red then green

- **Host.** `rm_rtmodel_test.c` (`model light 3466 checks, 0 failed, 10 of 10
  mutants refused`) adds three mutants, `depthhack-weapon`, `untagged-traced` and
  `tag-no-normals`, plus the floor test and the stats line. `rm_rtfx_test.c` covers the
  order, the walks, the shared switch, and `r_rt_direct_models` as a setting.
- **Kernel pins.** check-ref-metal segment 7.95 holds seven pins on `rm_rtmodel.metal`, and
  each mutant compiles under `xcrun metal` and is refused:

  | Pin | Mutants |
  | --- | --- |
  | `rename` | `rename-extra` |
  | `gate` | `model-writes-world`, `no-normal-traced` |
  | `base` | `model-from-base` |
  | `nolm` | `model-reads-lm` |
  | `fit` | `model-fit-leak` |
  | `dot` | `shadedot-back` |
  | `tagmat` | `tag-no-material` |

- **Back-end pins.** Two pins on `rmb_rtmodel.m`: `own`, with the `model-own-ray` mutant
  (`nself` left as the trace's), and `bind`, with an extra texture binding.
- Green: `model pass 7 kernel pins with 8 kernel mutants compiled and refused, 2 back-end pins with 2 mutants refused`.

## The hold

The checks that need no lock:

- `make test`: `yq2test: 591 tests, 0 failed`;
- check-ref-metal, check-cvar-docs, check-doc-links, check-runbook-index, check-pin-chars
  and check-scenario-parsers;
- `xcrun metal -c` of `rm_rtmodel.metal`.

The hold script builds the before (`task190s1` with `task191s1` merged) in scratch and
records, at commit 25e2ce98:

- the stock scenarios: 29 of 29 byte-identical to the before;
- every existing RT-primary record: 12 of 12 byte-identical to the before;
- `rtprimary-models-off` on the branch: identical (c5ec8f241450) against `rtprimary-models`
  with `r_rt_direct_models 0`, and identical (c5ec8f241450) against the before's binary on
  the same scenario (which ignores the cvar);
- each new scenario three times: each identical across its 3 runs, `rtprimary-models`
  6054a4952adc, `rtprimary-models-off` c5ec8f241450, `rtprimary-models-vis` fbdc8cbb6ad4;
  `rtprimary-models` against `rtprimary-models-off` differs in 22 of 23 frames (the pass
  changes the models);
- the `r_rt_stats 2` model line: 686 model lines, the first
  `ref_metal: rt models: frame 18: 4464 model pixels, 24747 shadow rays, 11593 sky, sun and bounce rays, 0.318 ms`,
  and no refusal line. The times on these lines (0.318 ms on the first, a mean of 0.385 ms
  over the 686) were measured with the screen locked during a hold. They are not a timing
  measurement, and no conclusion is drawn from them (decision 0014);
- the frames for the owner: `rtprimary-models` and `rtprimary-models-off` at captures 12 and
  21, and the stock `models` scenario's. They went to the owner directly and are not
  committed.

## The merge of slice 1 and main

Slice 1's final tip 08b6276f (with main d7817081, TASK190 slices 2 and 3) was merged into
slice 2 at b49fb42. The results above were taken before it, at 25e2ce98.

- **Segments.** Main's TASK190 segments are 7.7 (RT-primary temporal) and 7.8 (RT-primary
  filter). The model light is 7.9, as slice 1 numbered it; slice 2 had named it 7.8. The
  model pass, slice 2's own, was 7.9 and is now 7.95.
- **The effect row.** TASK190 slice 3 gave every row a `show_asked` entry. The models row
  takes `RM_RTModel_ShowAsked`, which returns 0: its view is the direct light's, which
  `RM_RTDir_ShowAsked` already reports.
- **The order.** The model pass still runs after RT-primary's accumulation and its 5 x 5
  filter, which share the direct light's encoder. Only the filter writes world pixels
  only (`mtl_rtp_world`). The accumulation passes every other pixel through with a zero
  history. The model pass writes the light the resolve reads and never the history, so
  model pixels still pass through TASK190 untouched. (Slice 3 fixes this wording: it said
  that both write world pixels only, and named the merge as ba5e4dff.)
- **The hold.** The merge's hold compares the stock scenarios and every RT-primary record
  with main d7817081, and records the three `rtprimary-models` scenarios three times each.

## Review notes

- **The pass runs after TASK190's accumulation**, as a row after `RM_RTFX_DIRECT`. Model
  pixels are not accumulated; slice 4 does that.
- **Later rows read the raster lightmap.** Under RT-primary, the rows after the direct
  light read the raster lightmap, not the traced light. That predates this slice.
- **The kernel mutants are text pins.** No GPU test runs the kernel; the pins hold its
  text and each mutant compiles under `xcrun metal`.
- **The model colour index under fast math.** The Metal shaders build with fast math,
  where the compare `!(f > 0.0f)` that sent a NaN to index 0 may be folded as if no NaN
  came. `mtl_rtm_index1` now tests the float's bit pattern with integer compares
  (`MTL_RTM_BITS`, `as_type<uint>` under Metal): a pattern above +inf's (a NaN, or the
  sign set) gives 0, one at or above 255.0f's gives 255, and only a finite value in
  0 .. 255 is truncated. The results equal the float compares' for every input, so the
  host stays gl1-equal. The host test adds NaN of either sign and several payloads, +inf,
  -inf, -0 and a sweep of 65536 bit patterns (`rm_rtmodel_test: 3479 checks, 0 failed`),
  and the `nan-guard` mutant, the guard dropped, is refused (10 failed checks), so
  check-ref-metal now requires 11 model-light mutants. check-ref-metal also pins the
  Metal branch's `as_type<uint>`.
- **`r_rt_stats 2`'s model line** is documented in `doc/040_cvarlist.md`, under
  `r_rt_stats` and `r_rt_direct_models`.
- **`rtprimary-models-vis`** shows model pixels in gl1's model colour (overbright, the
  minlight and gamma tables) and world pixels in the linear term, so the two are not
  directly comparable; `doc/100_renderpins.md` and the scenarios README say so.

# Slice 3: the view weapon, and the glow and minlight models

Slice 3 rasters the view weapon under RT-primary and lights it per pixel by traced light,
and traces the glowing and minlight models with their glow pulse and minlight floor.
`r_rt_direct_models 0` keeps every model and the weapon stock-lit, as before, and no other
presentation changes.

## What slice 3 adds

- **The split** (`rm_rtm_entity_split`). Under the traced models:
  - The view weapon is rastered when it is an alias model with its depth hack. It has no
    shadow, since `rm_shadow_drawn` leaves the weapon out, as gl1 does. A weapon brush
    model goes forward.
  - A weapon without `RF_DEPTHHACK` stays stock-lit (`rm_rtm_stock_model`). Its depth is
    not the 0 .. 0.3 that the kernel unprojects. The client's weapon always has the flag
    (`CL_AddViewWeapon`).
  - Glowing and minlight models are rastered. Fullbright, IR-goggle, translucent and shell
    models, and the crosshair model, stay stock-lit, as before.
- **The tag** (`rm_rtm_tag_wanted`, called in `R_DrawAliasModel`). The raster writes the
  tag only for a traced model with its normals written that carries a flag (glow,
  minlight, the weapon). A model without a flag keeps its own colour in the light
  attachment, as in slice 2, and that reads as no flags. So the untagged models' raster
  is unchanged. `R_AliasMotion` now returns `rm_gb_alias_extra`'s result for this.
  - Since the review, the tag is the alpha alone (`MTL_RTM_TAG_BASE` plus the flags). The
    rgb stays the model's stock colour. So on a frame where the model pass does not write
    a tagged pixel, the resolve shows it stock-lit, not at slice 1's mid grey.
  - Those frames are the early returns of `rmb_rtmodel_trace` after the raster: no direct
    view, a size mismatch, no command buffer or encoder, or the scene not bound.
  - An opaque model's own alpha is 1, so the alpha alone still tells a tag apart.
- **The kernel** (`rm_rtm_trace`):
  - It reads the tag first. Then `rm_rtm_point` gives the point to shade at:
    - A model drawn with the view's projection takes the world's unprojection, as in
      slice 2.
    - A weapon pixel takes the weapon's own inverse (`p.winv`) at
      `mtl_rtm_weapon_ndc(d)`, the arithmetic of `rm_rtm_weapon_unproject`.
  - The pull-back ray goes from the eye toward the weapon point, up to its distance
    (`rm_rtm_wall_t`). It stops at the first wall (`mtl_rtm_wall`): a map face that is not
    water or translucent. Water and translucent faces pass, as they do for a shadow ray,
    and a model is no wall. With a hit, the pixel is shaded at `mtl_rtm_pullback`'s point,
    0.25 units before the wall. With none it is shaded at the weapon point.
  - Since the review, a ray that meets no wall returns `2 * len + 1`, a finite distance
    past the weapon. Under fast math, `len` itself may come out an ulp below the length
    `mtl_rtm_pullback` computes. The pull-back would then fire on an unoccluded pixel.
    An infinity is not used, because fast math may fold the compare against it.
  - `mtl_rtm_wall` names the materials: `MTL_GB_MAT_WATER` and `MTL_GB_MAT_TRANS`.
  - A frame with no weapon view (`p.weapon` 0, when glFrustum refuses the projection)
    shades a weapon pixel at the eye.
  - The glow pulse and the minlight floor come from the tag's flags through
    `mtl_rtm_color`, as slice 2 already wrote it.
- **The parameters.** `rm_rtm_params_t` is now 80 bytes: a `float4x4` first, then the
  glow, overbright and the weapon flag. `rm_rtm_weapon_params` fills it each frame in
  `RM_RTModel_View` from `R_DrawAliasModel`'s inputs, jittered by the new
  `RM_GB_Jitter`, which is `RM_GB_JitterProj`'s jitter.
- **No shadow from the weapon or the player's own model.**
  - `rm_rtm_gunshadow` keeps the weapon out of the ray-traced scene while the models are
    traced (`RM_RTModel_Wants`, read in `RM_RT_Frame`), whatever `r_rt_gunshadow` says.
    So no ray meets it, not even its own pixels' shadow, sky and bounce rays.
  - Since the review, `RM_RTModel_Wants` asks `rm_rtm_wants`. It needs a device with ray
    tracing, the model pass not refused, `r_rt_direct_models` on, and `RM_RTDir_Will`.
  - `RM_RTDir_Will` is `RM_RTDir_Frame`'s own test without the scene. It says no under
    `r_fullbright`, for a view without the world, and when the direct pass was refused.
    `RM_RTDir_Frame` now traces by the same test.
  - The player's own model (`RF_VIEWERMODEL`) never reaches the refdef in the first
    person, because `CL_AddPacketEntities` adds its lights and skips it. So no instance
    is made of it.
  - With `r_rt_direct_models 0`, `r_rt_gunshadow` is as before.

## Choices and where the plan differs

- **Only flagged models are tagged.** The plan tags weapon and flagged pixels. Tagging
  every traced model would change the alpha where a later row reads the raster light
  attachment (see slice 2's review notes). The untagged models read as flags 0, which is
  what the tag would say.
- **`RM_RTModel_Wants` is decided as the scene is built,** before the passes decide the
  frame. Since the review it asks what this frame's passes will decide (item 2 of the
  review was cheap, so the rule is exact, and the cvar docs were not cut back). Two
  frames still differ:
  - When the scene is not ready, no ray is traced at all, so leaving the weapon out
    changes nothing.
  - On the one frame where the direct pass or the model pass first fails to open, the
    weapon is left out and stock-lit. From the next frame on, the refusal says no.
  - Both matter only with `r_rt_gunshadow 1` under RT-primary.
- **The weapon's own rays.** Its shadow, sky and bounce rays start at its point, which is
  0.25 units before the wall when pulled back. They use the world's bias, as a model
  pixel's do.
- **No GPU test.** The kernel is held by text pins and `xcrun metal` compiles, as in
  slice 2. The arithmetic of the weapon point is held by the host test against the
  weapon's round trip.

## The tests, red then green

- **Host.** `rm_rtmodel_test.c`: `model light 4182 checks, 0 failed, 18 of 18 mutants
  refused` (check-ref-metal segment 7.9, run here by the segment's own lines; before the
  review, 3981 checks and 16 mutants).
  - New and changed cases:
    - the split's named weapon, glow and minlight cases, and the sweep against the new
      rule;
    - `rm_rtm_traced`;
    - `rm_rtm_tag_wanted` over every flag combination;
    - `rm_rtm_gunshadow`, through `rm_rt_instances`;
    - `mtl_rtm_wall`;
    - the parameters' layout;
    - `rm_rtm_weapon_params` against the weapon view, and the kernel's arithmetic
      against the weapon round trip.
  - Two of slice 1's and slice 2's mutants lost their lines, so they were rewritten as
    `weapon-forward` and `flagged-forward`, and `depthhack-weapon` now edits the new
    stock-model line. Five mutants are new (`weapon-nohack` to `weapon-flag`):

  | Mutant | Change | Refused by |
  | --- | --- | --- |
  | `weapon-forward` | the weapon forward again for any kind | the split's weapon cases and sweep |
  | `flagged-forward` | a glowing or minlight model stock-lit again | the split's glow and minlight cases, `rm_rtm_traced` |
  | `depthhack-weapon` | the weapon's depth hack read as the crosshair's | the split's weapon cases |
  | `weapon-nohack` | a weapon without its depth hack traced | the split's case and sweep |
  | `tag-unflagged` | a model without a flag tagged | `rm_rtm_tag_wanted` |
  | `gunshadow-kept` | the weapon kept in the scene under the traced models | `rm_rtm_gunshadow` and the instances |
  | `wall-alias` | a model read as a wall | `mtl_rtm_wall` |
  | `weapon-flag` | the weapon flag set without a view | `rm_rtm_weapon_params`' refused case |
  | `wants-refused` | the models wanted with the model pass refused (review) | `rm_rtm_wants` |
  | `tag-grey` | the tag written over the stock colour as a grey (review) | the tag writer's kept rgb |

- **Kernel pins.** check-ref-metal segment 7.95: `model pass 9 kernel pins with 14 kernel
  mutants compiled and refused, 2 back-end pins with 2 mutants refused, 2 wiring pins with
  2 mutants refused` (before the review, 13 kernel mutants and no wiring pin).

  | Pin | Mutants |
  | --- | --- |
  | `weapon`: the trace's point is `rm_rtm_point`'s, the world's unprojection is called once, the weapon's inverse at `mtl_rtm_weapon_ndc` | `weapon-world-view`, `weapon-depth-raw`, `weapon-world-point` |
  | `pullback`: the wall's distance feeds `mtl_rtm_pullback`, a model passes, a miss returns `2 * len + 1` | `no-pullback`, `wall-models`, `miss-len` (a miss back to `len`) |

  Slice 2's seven pins hold unchanged. The tag is read before the point now, and there is
  still one `gbl.` read in the file.
- **The back end.** Its two pins hold, and it binds nothing new. The parameters' static
  assert is now 80 bytes.
- **The wiring** (since the review). The default leaves it inert (`r_rt_gunshadow` is 0),
  so no recorded frame tells it apart, and two text pins hold it:

  | Pin | Mutant |
  | --- | --- |
  | `gun`: `RM_RT_Frame` asks `rm_rtm_gunshadow(RM_Effect(r_rt_gunshadow) != 0, RM_RTModel_Wants(fd))` | main's form, `r_rt_gunshadow` alone |
  | `wants`: `RM_RTModel_Wants` asks `rm_rtm_wants` with the refusal and `RM_RTDir_Will(fd)` | `RM_RTDir_Wanted()` in its place |

  The scenario `rtprimary-weapon-gunshadow` (`rtprimary-weapon` with `r_rt_gunshadow 1`)
  holds the same rule on the GPU: its frames must equal `rtprimary-weapon`'s.

## The hold

The checks that need no lock:

- the host test with its mutants (segment 7.9) and the kernel and back-end pins
  (segment 7.95), each run by the segment's own lines;
- `xcrun metal -c` of `rm_rtmodel.metal`;
- `cc -fsyntax-only` of the changed C and Objective-C files;
- check-cvar-docs, check-doc-links, check-runbook-index, check-scenario-parsers and
  check-pin-chars.

No new cvar is added. `doc/040_cvarlist.md` says what `r_rt_direct_models` and
`r_rt_gunshadow` now do. The hold script (run by the orchestrator, under the lock)
builds main a9414f84 in scratch and records:

- `make ref_metal`, `make` and `make test`;
- check-ref-metal, check-video-menu, check-pin-chars and check-scenario-parsers;
- the 30 stock scenarios against main, byte for byte;
- every existing RT-primary record against main, `rtprimary-models`, `-off` and `-vis`
  among them. They set `cl_gun 0`. `rtprimary-models` and `-vis` can change only where a
  glowing or minlight model is in view, since those are now traced; the hold lists the
  frames that change;
- `rtprimary-weapon` three times (determinism), and against an `r_rt_direct_models 0`
  copy, where the weapon is stock-lit (want differ). That copy is also run against
  main's binary (want identical);
- the `r_rt_stats 2` model line of `rtprimary-weapon`;
- since the review, `rtprimary-weapon-gunshadow` against `rtprimary-weapon`, frame by
  frame (want byte-identical).

The hold ran at 389df5e3 against main a9414f84.

- `make ref_metal`, `make` and `make test` passed, with 591 tests and 0 failed.
- These checks passed: `check-ref-metal` (13162 checks, 0 failed), `check-video-menu`,
  `check-pin-chars`, and `check-scenario-parsers` (5 tests, 11 mutants caught).
- Model light: 4182 checks, 0 failed, 18 of 18 mutants refused.
- Model pass: 9 kernel pins with 14 kernel mutants compiled and refused, 2 back-end
  pins with 2 mutants refused, and 2 wiring pins with 2 mutants refused.
- 30 of 30 stock scenarios were byte-identical to main.
- 16 of 18 existing RT-primary records were byte-identical to main, `rtprimary-models-off`
  (c5ec8f241450) among them.
  - `rtprimary-models` changed in 14 of 23 frames: it was 6054a4952adc and is now
    93c5ef890067.
  - `rtprimary-models-vis` changed in 14 of 23 frames: it was fbdc8cbb6ad4 and is now
    a5cd3e3316b9.
  - Both change as intended, because glow and minlight models are now traced.
- `rtprimary-weapon` (7a061e1228e1) was deterministic: across 3 runs, 0 of 23 frames
  differed.
- `rtprimary-weapon-gunshadow` was byte-identical to `rtprimary-weapon` (0 of 23 frames
  differ). That shows the weapon stays out of the traced scene with `r_rt_gunshadow 1`.
- The `r_rt_direct_models 0` copy was identical to main (37c6cdddfb31). All 23 of its
  frames differ from `rtprimary-weapon`.
- `r_rt_stats 2`, frame 18: 26576 model pixels, 264687 shadow rays, and 70786 sky, sun
  and bounce rays, in 1.167 ms.

## Left to the next slices

- Slice 4: model pixels, the weapon's among them, join TASK190's accumulation.
- Slice 5: timing, in the owner's quiet window.

# Slice 4: model pixels join RT-primary's accumulation

Slice 4 accumulates the model pixels, the view weapon's among them, over the views under
`r_rt_temporal` above 0, as TASK190 accumulates the world's. `r_rt_temporal 0` and
`r_rt_direct_models 0` stay exactly as main, and no other presentation changes.

## The design

What existed (traced at main 9a81ccee): the direct row traces the world into `t_light`, then
`rmb_rtdir_accumulate` blends the world pixels with the history (`t_hist`, RGBA16Float, the
count in alpha) and its key (`t_key`, the distance's bits and the material id) into `t_acc`
(RGBA16Unorm) and filters them 5 x 5. Every other pixel is passed through with count 0, and
the filter gives it weight 0. The model row runs after that and writes the model pixels of
`t_acc` with one traced sample, already in gl1's model colour (`tabs[..] / 255`).

The design chosen: **the model pass accumulates its own pixels into the same history, after
the world's accumulation, in a kernel of its own.**

- **A second kernel, in a unit of its own.** `rm_rtm_temporal` (`rm_rtmtemporal.metal`)
  includes `rm_rtmodel.metal` for its helpers, with its three entry points renamed, as
  `rm_rtmodel.metal` includes `rm_rtdir.metal`. So `rm_rtmodel.metal` is unchanged, and so is
  `rm_rtm_trace`'s compiled code (TASK59 1b's lesson: a kernel whose helpers gained a second
  caller in its own unit moved its frames). The back end runs `rm_rtm_temporal` only on a view
  that accumulated. Otherwise, at `r_rt_temporal 0`, it runs `rm_rtm_trace` with main's
  bindings.
- **The history holds the light, and the colour comes after.** The kernel traces the light `l`
  as `rm_rtm_trace` does. It blends `l` with the history by TASK190's rule (`mtl_rtp_reject`,
  `mtl_rtp_pixel`), writes the blend and its count to the history and the key, and writes gl1's
  model colour of the blend (`mtl_rtm_color` through the tables) to the light the resolve reads.
  So the glow pulse, the minlight floor, overbright and gamma apply to each frame's blend, and
  are not averaged. Neither the tag nor the colour ever enters a float history:
  - the tag is read from the G-buffer's light attachment (RGBA8Unorm), as in slice 3;
  - the history's alpha is the count;
  - the colour is written only to the RGBA16Unorm output.
- **The key.** The key's distance is the shading point's distance from the eye: the world's
  unprojection for a model, and the weapon's own point, pulled back where it is in a wall. Its
  material word is `MTL_GB_MAT_ALIAS + 256 x` the tag's flags (`mtl_rtmt_key`). Every
  G-buffer material id is below 256 (R8Uint), so a model key never equals a world id. A world
  pixel that reprojects onto model history is rejected by the material rule, and so is a model
  pixel that reprojects onto world history. The weapon's key differs from every other model's.
- **The reprojection** is the G-buffer's motion vector, as for the world. The raster writes a
  model's motion from its own previous pose, and the weapon's from its own unjittered
  projection (`R_AliasMotion`'s `wproj`). So the weapon is reprojected by its own motion in the
  depth-hacked view.
- **The resets** are TASK190's. The view's reset applies, including a change of
  `r_rt_direct_show`. So do the point off the screen, the depth tolerance, the key and an empty
  or bad history. A model that is not traced this frame has count 0 in the history, which the
  world's accumulation writes when it passes the pixel through, so it starts again when it is
  traced. That covers `r_rt_direct_models 0`, a model without normals, and a frame where the
  model pass failed.
- **The world is unchanged.** `rm_rtp_temporal` and `rm_rtp_denoise` are not edited, and they
  run before the model pass. A world pixel that reprojects onto a former model texel was
  rejected before (count 0, material 2) and still is (the key), so its blend and count are the
  same. The filter reads this frame's history before the model pass writes it, so it still
  sees model texels at count 0.

The trade-offs:

- **Model pixels are not filtered in this slice.** The 5 x 5 filter stays world-only. A model
  pixel whose history was just rejected (a disocclusion, a fast move past the depth
  tolerance) shows its one sample until the history builds. Filtering would need the model
  colour in a second filter kernel. The rule is ready for it (`mtl_rtpd_weight` already
  separates by the key), but it is a follow-up if the frames show the need.
- **No entity id.** The G-buffer has none, so two models with the same flags that meet on the
  screen at a similar distance can hand history to each other within the depth tolerance, as
  two world faces can.
- **The option of running the model pass before the accumulation** was rejected for three
  reasons:
  - It would blend the final model colour, so the glow pulse would be averaged. At 0.95 and
    60 frames a second, a 7 rad/s pulse keeps about 40 % of its swing.
  - It would edit `rm_rtp_temporal`, whose world frames are pinned.
  - The model colour, a gamma-mapped index, is not a light to average.
- **A failed dispatch** of `rm_rtm_temporal` (no encoder) leaves the model pixels as the world's
  accumulation passed them through, stock-lit, for that frame, as a failed `rm_rtm_trace` does
  on main.
- **Precision.** The history is half floats, so a settled light can be one colour index off
  the float mean, as the world's is. A history's first frame (count 1) is the traced light
  itself, so its colour equals `rm_rtm_trace`'s.
- **The cost** is one more kernel variant, with five more textures read and two more written
  per model pixel. Nothing new is allocated, since the histories are the world's.

## What slice 4 adds

- **The rule**, `common/mtl_rtmt_math.h`. It includes `mtl_rtp_math.h` and `mtl_rtm_math.h`
  unchanged and has three parts:
  - `mtl_rtmt_model` is `rm_rtm_trace`'s pixel test. It never holds where `mtl_rtp_world` does.
  - `mtl_rtmt_key` is `MTL_GB_MAT_ALIAS + (flags << 8)`.
  - `mtl_rtmt_pixel` is TASK190's `mtl_rtp_pixel` on the traced light, with an opaque alpha.
- **The kernel**, `rm_rtm_temporal` in `rm_rtmtemporal.metal`:
  - It includes `rm_rtmodel.metal` with exactly three `#define`s: `rm_rtm_unused_trace`,
    `rm_rtm_unused_show` and `rm_rtm_trace` get new names, so the metallib holds no symbol
    twice.
  - Its light is `rm_rtm_trace`'s, line for line. Then come the key, the reprojection
    through the motion vector, `mtl_rtp_reject` under the accumulation's view, and
    `mtl_rtmt_pixel`.
  - It writes four outputs at model pixels only: `rm_rtm_out` of the blend to the
    resolve's light, the visibility, the blended light with its count to the history, and
    the distance and key to the key texture.
  - A fourth count, the pixels whose history was kept, goes to the stats.
- **The back ends.**
  - `rmb_rtdir_frame` now also hands over this view's accumulation:
    - the history and key it read (`1 - hist_cur` after the swap);
    - the history and key it wrote (`hist_cur`);
    - `t_acc`;
    - the temporal view as it was bound (`tbound`, with its reset). On a view that
      accumulated nothing, all of these are nil.
  - `rmb_rtmodel.m` makes the `rm_rtm_temporal` pipeline beside `rm_rtm_trace`'s. If it
    cannot be made, it prints `ref_metal: rt models: no temporal pipeline; model pixels are
    not accumulated` and runs `rm_rtm_trace` as before.
  - It runs `rm_rtm_temporal` only when the light it writes is the accumulation's (`light
    == f.acc`), with the motion vectors and the history at the light's size. Only then does
    it bind textures 8 to 12 and the view at buffer 19. Otherwise it runs `rm_rtm_trace`
    with main's bindings.
- **The stats.** On a frame that accumulated, the `r_rt_stats 2` model line adds
  `, accumulated: K kept their history`. A frame that did not accumulate prints main's line.
- **The scenario** `rtprimary-models-temporal` is `rtprimary-weapon` at `r_rt_temporal
  0.95`, seeded.
- **No new cvar.** `doc/040_cvarlist.md`'s entries for `r_rt_temporal`, `r_rt_direct_models`
  and `r_rt_stats` say what changed.

## The tests, red then green

- **Host.** `tests/metal/rm_rtmtemporal_test.c` was committed before the header, and it did
  not build (red). Its checks:
  - the pixels against `mtl_rtp_world`, for every material id;
  - every key against every G-buffer id and every other key;
  - the rejections across the key;
  - a pixel's blend, count, alpha and clamp;
  - 60 frames of a settled light with the glow pulse, whose colour equals the unaccumulated
    colour on every frame;
  - the minlight floor of a dark blend.

  Green: `rm_rtmtemporal_test: 2707 checks, 0 failed`. check-ref-metal segment 7.96 runs it
  and requires five header mutants, each refused by FAIL lines:

  | Mutant | Change | Refused by |
  | --- | --- | --- |
  | `key-flags` | the key without the flags | 66 checks: the weapon's and the flags' own keys |
  | `key-low` | the flags not above the G-buffer's ids | 7 checks: the keys against the ids |
  | `model-world` | any material taken as a model pixel | 255 checks: the pixels |
  | `alpha-kept` | the history's count kept as the alpha | 3 checks: the alpha |
  | `pass` | the pixel passed through unblended | 10 checks: the blend and the count |

  `rm_rtmodel_test.c` adds the stats line's suffix: 4185 checks, 0 failed, 18 of 18 mutants
  refused.
- **Kernel pins.** Segment 7.96 has seven pins on `rm_rtmtemporal.metal`. Each mutant
  compiles under `xcrun metal` and is refused:

  | Pin | Mutants |
  | --- | --- |
  | `rename`: exactly three `#define`s, then `rm_rtmodel.metal` | `rename-extra` |
  | `same`: the light's seven lines, each also in `rm_rtm_trace` | `own-point` (the world's unprojection for the weapon) |
  | `gate`: model pixels only, the four writes once each, inside `if (model)` | `writes-world`, `hist-everywhere` |
  | `key`: `mtl_rtmt_key(f)`, the shading point's distance, both to the rule and the key texture | `key-material`, `dist-world` |
  | `motion`: the motion vector and the view's reset reach the rule | `no-motion`, `no-reset` |
  | `blend`: the model colour of the blend, the history the blended light, one `rm_rtm_out` | `color-unblended`, `hist-raw` |
  | `nolm`: no lightmap | `reads-lm` |

- **Back-end pins** (segment 7.96), each with one mutant that compiles and is refused:

  | Pin | Mutant |
  | --- | --- |
  | `accpipe`: `rm_rtm_temporal` only on the accumulated light | `acc-any-light` |
  | `accbind`: its five textures and its view, bound only behind `if (acc)` | `bind-always` |
  | `swap`: `rmb_rtdir_frame` hands over the history it read and the one it wrote, after the swap | `hist-swapped` |

  Segment 7.95's `bind` pin now counts twelve textures, of which five are behind
  `if (acc)`. Its mutant is still refused. The Makefile compiles and links the new unit,
  which check-ref-metal checks.
- Green, from the segments' own lines: `model accumulation 2707 checks, 0 failed, 5 rule
  mutants, 7 kernel pins with 11 kernel mutants compiled and refused, 3 back-end pins with
  3 mutants refused`. Segment 7.95 is unchanged at 9 kernel pins, 14 mutants, 2 back-end
  pins and 2 wiring pins. Segments 7.5 and 7.6 (the direct light's binding rules) and 20
  (the kernel calls) pass.

## The hold

The checks that need no lock:

- the host tests with their mutants (segments 7.9 and 7.96) and the kernel and back-end pins
  (segments 7.95 and 7.96), each run by the segment's own lines;
- `xcrun metal -c` of `rm_rtmtemporal.metal`, and a metallib of it with `rm_rtmodel.metal`
  (no symbol twice);
- `cc -fsyntax-only` of the changed C and Objective-C files;
- check-cvar-docs, check-doc-links, check-runbook-index and check-pin-chars.

The hold script (run by the orchestrator, under the lock) builds main 9a81ccee in scratch.
Before `make` it removes `release/quake2`, so the pins' stale-build check does not refuse a
binary that only the renderer's headers changed. It records:

- `make ref_metal`, `make` and `make test`;
- check-ref-metal, check-video-menu, check-pin-chars and check-scenario-parsers;
- the 30 stock scenarios against main, byte for byte;
- every existing RT-primary record against main. Every record either has `r_rt_temporal 0`
  or has no model (`rtprimary-denoise` at 0.9 has no entities and no gun), so all of them
  must stay byte-identical;
- `rtprimary-models-temporal` three times (determinism), and against main's binary (want
  differ, since main does not accumulate model pixels);
- an `r_rt_direct_models 0` copy of it against main's binary (want identical: the world's
  accumulation is main's, and every model is stock-lit);
- the `r_rt_stats 2` model and temporal lines, and no refusal line.

Results: (the hold's numbers go here.)

## Left to the next slices

- Slice 5: timing, in the owner's quiet window.
- A 5 x 5 filter on model pixels (the trade-off above), if the owner's frames show the need.
