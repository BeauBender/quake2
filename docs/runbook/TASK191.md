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
    `mtl_rtm_is_tag` and `mtl_rtm_tag_flags` read it back.
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
  - The player's own model (`RF_VIEWERMODEL`) never reaches the refdef in the first
    person, because `CL_AddPacketEntities` adds its lights and skips it. So no instance
    is made of it.
  - With `r_rt_direct_models 0`, `r_rt_gunshadow` is as before.

## Choices and where the plan differs

- **Only flagged models are tagged.** The plan tags weapon and flagged pixels. Tagging
  every traced model would put the grey tag where a later row reads the raster light
  attachment (see slice 2's review notes). The untagged models read as flags 0, which is
  what the tag would say.
- **`RM_RTModel_Wants` is decided as the scene is built,** before the direct light
  decides the frame. On a frame where the direct light is wanted but does not trace (the
  scene not ready), the weapon is also left out of the scene. That matters only with
  `r_rt_gunshadow 1` under RT-primary.
- **The weapon's own rays.** Its shadow, sky and bounce rays start at its point, which is
  0.25 units before the wall when pulled back. They use the world's bias, as a model
  pixel's do.
- **No GPU test.** The kernel is held by text pins and `xcrun metal` compiles, as in
  slice 2. The arithmetic of the weapon point is held by the host test against the
  weapon's round trip.

## The tests, red then green

- **Host.** `rm_rtmodel_test.c`: `model light 3981 checks, 0 failed, 16 of 16 mutants
  refused` (check-ref-metal segment 7.9, run here by the segment's own lines).
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

- **Kernel pins.** check-ref-metal segment 7.95: `model pass 9 kernel pins with 13 kernel
  mutants compiled and refused, 2 back-end pins with 2 mutants refused`.

  | Pin | Mutants |
  | --- | --- |
  | `weapon`: the trace's point is `rm_rtm_point`'s, the world's unprojection is called once, the weapon's inverse at `mtl_rtm_weapon_ndc` | `weapon-world-view`, `weapon-depth-raw`, `weapon-world-point` |
  | `pullback`: the wall's distance feeds `mtl_rtm_pullback`, a model passes | `no-pullback`, `wall-models` |

  Slice 2's seven pins hold unchanged. The tag is read before the point now, and there is
  still one `gbl.` read in the file.
- **The back end.** Its two pins hold, and it binds nothing new. The parameters' static
  assert is now 80 bytes.

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
- the `r_rt_stats 2` model line of `rtprimary-weapon`.

The results go here when the hold has run.

## Left to the next slices

- Slice 4: model pixels, the weapon's among them, join TASK190's accumulation.
- Slice 5: timing, in the owner's quiet window.
