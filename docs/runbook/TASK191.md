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
  channel. Slice 2 changes the Metal test to `s >= 0.1f`, which equals gl1's double test
  exactly.

## The test, red then green

`tests/metal/rm_rtmodel_test.c`, built by `tests/check-ref-metal.sh` (segment 7.7) with
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

- the stock scenarios at `r_presentation 0`: 29 of 29 stock scenarios byte-identical to the base (main e76c846e) byte-identical to main;
- every committed `rtprimary-*` record, with `rtprimary-world` on `demo2.dm2` and on the
  pool demo and `rtprimary-dlights` on `demo2.dm2`: 10 of 10 RT-primary records byte-identical to main's (the 7 rtprimary-* scenarios and the styles, pool and dlights-d2 demo variants) byte-identical to main.

## Left to the next slices

- Slice 2: a kernel lights the rastered models by traced light (`r_rt_direct_models`).
- Slice 3: the view weapon is rastered under the mode, with the tag, the unprojection and
  the pull-back ray, and the glow and minlight flags are applied from the tag.
- Slice 4: model pixels join TASK190's accumulation.

## Notes for slice 2

- The kernel's tag decode also gates on material 2 (alias).
- Tag only when the normals were written.
- The depth-hack forward rule excludes `RF_WEAPONMODEL`.
