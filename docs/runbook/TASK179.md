# TASK179: one effect table for ref_metal's ray-traced effects (AR-17)

`src/client/refresh/metal/rm_rtfx.c` holds one row per ray-traced effect (shadows, AO, sun, water): its name,
its switch and its entry points, `NULL` where the effect has none. `header/rm_rtfx.h` declares the ids, the
order (`rm_rtfx_order`, a separate array), the walks over the rows and the pipeline rule of a 3D draw
(`rm_rtfx_pipe`). How an effect is added, and which entry is called where, is in
`docs/plans/metal-raytracing-design.md`, section 4, "Adding an effect".

## What changed

| File | Change |
|---|---|
| `rm_rtfx.c`, `header/rm_rtfx.h` (new) | the table, the order, the walks (`rm_rtfx_init`, `_shutdown`, `_wanted`, `_frame`, `_active`, `_show_only`, `_view` for the scene; `rm_rtfx_mark`, `_trace`, `_resolve`, `_show`, `_post`, `_forward`, `_bind`, `_view_end` for the backend), `rm_rtfx_switch`, `rm_rtfx_is_switch` and the pure `rm_rtfx_pipe` |
| `rm_main.c` | `R_RenderViewGB` walks the views (`rm_rtfx_view`) and the show-only views (`rm_rtfx_show_only`); `RI_RenderFrame` walks the frame calls (`rm_rtfx_frame`). No effect header and no effect symbol is left |
| `rm_rtscene.c` | `RM_RT_Init` walks the inits (with the gate's `rtcaps`, so the seat check's `rawcaps` rule still holds), `RM_RT_FreeAll` the shutdowns, `RM_RT_Wanted` and the frame's scene use `rm_rtfx_wanted` |
| `rm_gbufscene.c` | `RM_GB_Choose` takes `rm_rtfx_active` |
| `rmb_gbuf.m` | `rmb_gbuf_resolve` fills one `rmb_rtfx_io_t` and walks mark, trace, resolve (the stock resolve when no effect draws it), show and post; the post pass is opened by `rmb_gbuf_fx_open`. `rmb_gbuf_pipe3d` switches on `rm_rtfx_pipe`; `rmb_gbuf_bind_motion` walks `rm_rtfx_bind`; the view end and the frame reset walk `rm_rtfx_view_end`. No effect header and no effect symbol is left (`mtl_rts_gb_tag`, the surface attachment's names from `common/mtl_rts_math.h`, is the G-buffer's own and not an effect entry) |
| `rmb_rtshadow.m`, `rmb_rtao.m`, `rmb_rtsun.m`, `rmb_rtwater.m` | each effect's backend rows: wrappers over its own functions taking the walk's io, declared in its header outside `__OBJC__` |
| `header/rmb_internal.h` | `struct rmb_rtfx_io_s`, under `__OBJC__` (opaque to C) |
| `rm_rtgate.c` | the gate's sorted table lost its 0/1 column: `rm_rtg_cvar` and `rm_rtg_effect` classify a name as an effect switch by `rm_rtfx_is_switch`, so the switches have one source |
| `Makefile` | `rm_rtfx.o` in ref_metal's objects (the iOS, tvOS and visionOS static builds take the same list) |
| `tests/metal/rm_rtfx_test.c` (new), `tests/check-ref-metal.sh` | the host test, its five table mutants, and section (f): the effect-call lint with its four mutants. The gate's two host tests link `rm_rtfx.c` (built with `RM_RTFX_HOST`, every entry point `NULL`) |

Not changed: `rmb_metalfx.m`, `rm_pure.c`, `rm_metalfx.c`, `header/rmb.h`, `header/rm_metalfx.h` (TASK196's
lane), every kernel, every cvar and its default. The card's "init from two places" was already one place at
the start point (all four effects' inits ran from `RM_RT_Init`); the init and shutdown walks stay there, where
the seat check holds them to `rtcaps`, rather than move into `rm_main.c`.

## Red first

`tests/metal/rm_rtfx_test.c` built before `rm_rtfx.c` existed (with `rm_rtgate.c`, `RM_RTFX_HOST`):

```
Undefined symbols for architecture arm64:
  "_rm_rtfx_active", referenced from:
  ...
  "_rm_rtfx_order", referenced from:
  "_rm_rtfx_pipe", referenced from:
```

Green: `rm_rtfx_test: 391 checks, 0 failed`; the gate's own test over the derived classification,
`rm_rtgate_test: 833 checks, 0 failed`.

## Mutants

Each is a scratch mutant `tests/check-ref-metal.sh` builds on every run; the line is the first the test or
the lint printed for it.

| Mutant | Refused by | First line |
|---|---|---|
| the AO and the sun swapped in `rm_rtfx_order` | `rm_rtfx_test` (2 failed) | `FAIL tests/metal/rm_rtfx_test.c:62: rm_rtfx_order[k] == want[k]` |
| the AO's switch changed in the table only (`r_rt_ambient`) | `rm_rtfx_test` (7 failed) | `FAIL tests/metal/rm_rtfx_test.c:88: rm_rtfx_switch(id) && !strcmp(rm_rtfx_switch(id), sw[id])` |
| the AO's switch changed in the gate only (`r_rt_ambient`) | `rm_rtfx_test` (5 failed) | `FAIL tests/metal/rm_rtfx_test.c:113: i >= 0` |
| the swap rule inverted: the raster's surface choice | `rm_rtfx_test` (48 failed) | `FAIL tests/metal/rm_rtfx_test.c:159: rm_rtfx_pipe(RMB_GB_PASS_RASTER, p, s, fw, NULL) == (s ? RM_RTFX_PIPE_RASTER_SURF : RM_RTFX_PIPE_RASTER)` |
| the swap rule inverted: the forward pipeline asked for every pipe but the blended world face's | `rm_rtfx_test` (72 failed) | `FAIL tests/metal/rm_rtfx_test.c:165: fw_calls == (p == RM_PIPE_WORLD_TEXC_BLEND)` |
| `RM_RTW_Frame(fd, RM_RT_SceneReady());` after `rm_rtfx_frame` in `rm_main.c` | section (f) | `<copy>/rm_main.c:1321: RM_RTW_Frame` (on the tree itself the check adds `... rm_main.c names a ray-traced effect outside the effect table: walk it with rm_rtfx_* (rm_rtfx.c)`) |
| `rmb_rtw_blend(io.enc, t_material);` after `rm_rtfx_post` in `rmb_gbuf.m` | section (f) | `<copy>/rmb_gbuf.m:841: rmb_rtw_blend` |
| the same call in a comment only | section (f) passes it, as it must | none |
| a new row `FX(RM_RTGI_Init)` and `RM_RTGI_Frame(...)` in `rm_main.c` | section (f), the prefix taken from the new row | `RM_RTGI_Frame` |

## No change in what is drawn

Frame pins of ref_metal (`scripts/pins.sh record metal <scenario>`, `PINS_SEED=1`, demo1.dm2 and, for the two
`rt-water` scenarios, water.dm2), the start point (35b4fe81, built from a `git archive` extraction) and this
branch each built with the game module first. One hash per scenario: the sha256 of the sorted sha256 list of
its frames' files.

| Scenario | Frames | start point | head |
|---|---|---|---|
| hdr | 23 | e60d7019064f34f0d7fcc67caee31bafa7bf53aad02f118aaf329ae63cef5dec | e60d7019064f34f0d7fcc67caee31bafa7bf53aad02f118aaf329ae63cef5dec |
| models | 23 | ebbba4dafdbcc0c59486f49f994ff8a9ccf68471faf42d727ba43b53d6363daa | ebbba4dafdbcc0c59486f49f994ff8a9ccf68471faf42d727ba43b53d6363daa |
| rt-ao | 23 | ef2d8ecdc5f64c779f35ee16a5bad94c77a9b31aeb099a5aa55957e459d72fc1 | ef2d8ecdc5f64c779f35ee16a5bad94c77a9b31aeb099a5aa55957e459d72fc1 |
| rt-ao-entities | 23 | 7ba724b5567d4bdf1eef48c42b90c988482fcfd8923b9f0d9c934a9c62ea4d67 | 7ba724b5567d4bdf1eef48c42b90c988482fcfd8923b9f0d9c934a9c62ea4d67 |
| rt-shadows | 23 | faa35e471b50b9d4159fd5227eba8483b7ba0f660dfc87023b072bfbe4950727 | faa35e471b50b9d4159fd5227eba8483b7ba0f660dfc87023b072bfbe4950727 |
| rt-shadows-entities | 23 | cf0993ccba44b3123e7321bc62e7ecaa0b667313589d078339432dccc471dc3c | cf0993ccba44b3123e7321bc62e7ecaa0b667313589d078339432dccc471dc3c |
| rt-sun | 23 | d6a52cbee5b5eee4a5f414b79fa48eacbddd86c095f0087a0c61b21ba2b07306 | d6a52cbee5b5eee4a5f414b79fa48eacbddd86c095f0087a0c61b21ba2b07306 |
| rt-water | 4 | d05e82b82bc5c5db4cda4debef4dce795d51a2c100ed0ec21d021eb507d9ef16 | d05e82b82bc5c5db4cda4debef4dce795d51a2c100ed0ec21d021eb507d9ef16 |
| rt-water-entities | 4 | d05e82b82bc5c5db4cda4debef4dce795d51a2c100ed0ec21d021eb507d9ef16 | d05e82b82bc5c5db4cda4debef4dce795d51a2c100ed0ec21d021eb507d9ef16 |
| world | 23 | da510450be3a852705af100518db8726b469e55a0c9dd411ee45faac9309d58b | da510450be3a852705af100518db8726b469e55a0c9dd411ee45faac9309d58b |
| world-fullbright | 23 | 3d2feda53e4dac33417b2bc43129793d170c468810671c765a108d942e4c63ec | 3d2feda53e4dac33417b2bc43129793d170c468810671c765a108d942e4c63ec |
| world-lmcubic | 23 | b1a36975bb60c80a2487a97ec548bb5ba8047d138abb5d170054eabc936abc8f | b1a36975bb60c80a2487a97ec548bb5ba8047d138abb5d170054eabc936abc8f |
| world-metalfx-interp | 23 | da510450be3a852705af100518db8726b469e55a0c9dd411ee45faac9309d58b | da510450be3a852705af100518db8726b469e55a0c9dd411ee45faac9309d58b |
| world-metalfx1 | 23 | 39e90b899e2fdb2a927fce84d231ac5bc8e7556d6d308aa6c096a8d80e638a12 | 39e90b899e2fdb2a927fce84d231ac5bc8e7556d6d308aa6c096a8d80e638a12 |
| world-metalfx2 | 23 | 0e8074ad234d1f4997e9d54b8d27d99076e58853990c442616f5fea3c84ca6a7 | 0e8074ad234d1f4997e9d54b8d27d99076e58853990c442616f5fea3c84ca6a7 |
| world-metalfx3 | 23 | 0e8074ad234d1f4997e9d54b8d27d99076e58853990c442616f5fea3c84ca6a7 | 0e8074ad234d1f4997e9d54b8d27d99076e58853990c442616f5fea3c84ca6a7 |
| world-scale8 | 23 | dcbdc590767239213848955c06cbdfe5aa9bd2c5564d24479f5a5ff8ecd96373 | dcbdc590767239213848955c06cbdfe5aa9bd2c5564d24479f5a5ff8ecd96373 |


Every hash is the same. The dylib itself differs (`7cee5948...` at the start point, `2a11d989...` at the head),
as it must: the calls go through the table.

## Checks

Each run through the host lock on an apple9 GPU, at the branch's code head (`21a50083`, before this file
was added), last line. `tests/check-doc-links.sh` (`check-doc-links: ok`) ran after this file and its index line were added.

- `make test`: `yq2test: 435 tests, 0 failed`
- `tests/check-ref-metal.sh`: `check-ref-metal: ok (... ray-tracing gate 833 checks, 0 failed, effect table 391 checks, 0 failed, 5 table mutants refused, no effect outside it in rm_main.c or rmb_gbuf.m, 4 call mutants refused, 31 r_rt_* cvars classified, 4 table mutants refused; ... ref_metal built)`
- `tests/check-ios.sh`: `check-ios: ok (54 mutants refused; 188 smoke selftest cases; 65 device selftest cases; 181 objects for the simulator; 181 for a device; ios_objects)`
- `tests/check-tvos.sh`: `check-tvos: ok (181 objects for the tvOS simulator, platform 8, the iOS simulator's list)`
- `tests/check-visionos.sh`: `check-visionos: ok (181 objects for the visionOS simulator, platform 12, the iOS simulator's list)`

