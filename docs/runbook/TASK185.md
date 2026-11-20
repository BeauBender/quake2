# Retro upload and sampling (TASK185, PM3)

Card: TASK185 (#532). Plan: `docs/plans/presentation-modes.md`, sections 5.2 and 6, row PM3. Builds on
PM1 (TASK183, the `r_presentation` overlay) and PM2 (TASK184, the Retro post-process).

Under the effective mode `RM_PRES_RETRO` (`r_presentation 2`), `ref_metal` now also uploads and samples
textures as a 16-bit board did and draws the frame's 3D layer as if the window were 480 lines tall. Stock (`r_presentation 0`) takes none
of these paths. No cvar is written; no cvar is new; the renderer API, the protocol, savegames and the game
module are unchanged and no asset is added.

## What it is

- **16-bit texels** (`R_Upload32` in `rm_image.c`, which `R_Upload8` and `R_LoadPic` reach): after the
  gamma and intensity scale, an image that is not a 2D pic is capped to 256 x 256, then quantized in place:
  RGB565 when every texel has alpha 255, ARGB4444 (alpha too, so 0 and 255 stay as they are) when one does
  not, each with the 4 x 4 Bayer rank of the texel's place in the image. The result stays in the existing
  `BGRA8` texture; no new pixel format. The 2D pics, the scrap and the lightmaps are not touched (plan call
  P6). The conversion is `common/mtl_retro_tex.h`, plain C99 on the arithmetic of `mtl_retro_math.h`.
- **The 256 cap**: both sides are halved (rounded up) by 2 x 2 box averages, in place, until each is at
  most 256 (`mtl_retro_cap_apply`). `image->width` and `height` keep the original size, so no texture
  coordinate changes; only `upload_width` and `upload_height` do.
- **Filtering**: `R_TextureMode` uses `GL_LINEAR_MIPMAP_NEAREST` and anisotropy 0 in place of
  `gl_texturemode` and `r_anisotropic` while the mode is Retro (`RM_PresentationMode`), through the same
  sampler classes. Neither cvar is written, not even the clamp of `r_anisotropic` to 16.
- **The 480-line view**: under Retro `RM_GB_Choose` always gives the MOTION path or better, so the world
  view has the render-size target, and `RM_GB_Begin` sizes it with `mtl_retro_view_scale`: 480 lines over
  the target's height, the width by the same fraction (the window's aspect); a target of 480 lines or fewer
  is not scaled. The scale is the target's, not the view's: the frame's 3D layer is drawn as if the window
  were 480 lines tall, and the view's rectangle maps into it in proportion, so at `viewsize 60` in a
  1080-line window the view (about 648 lines) is drawn at about 288. The menu's player model
  (`RDF_NOWORLDMODEL`) takes the FORWARD path, so it is not drawn smaller; `R_RetroFinish` quantizes it at
  the window's size. `RM_FX_Begin` returns 0 under Retro, so no MetalFX scaler runs, and the existing blit at
  the view's end (`rm_fragment_blit`, bilinear) enlarges the view to its rectangle. The 2D layer is drawn on
  the window-size frame after it, as before.
- **Mode change**: the mode is read in `RI_Init`, so it applies at `vid_restart`, which reloads the
  renderer and every image. That is the route PM1 and PM2 use; textures are re-uploaded by it, and nothing
  else is needed. A change of `r_presentation` without a `vid_restart` changes nothing, as the cvar's
  documentation says.

## Order of the post-process and the blit

The post-process runs at 480 lines, then the blit: the order the card asks for, and the one that makes the
look. `rmb_gbuf_view_end` runs `rmb_retro_pass_view` on the render-size colour target (`t_rcolor`, over its
rectangle `src`), before the stretch, so the dither is quantized on the 480-line pixels and the bilinear
blit enlarges those. Running it after the blit, as PM2 did on the window-size frame, would quantize at the
window's pixels and give a dither too fine to see, undoing the low resolution. `R_RetroFinish`, PM2's pass
on the frame, is skipped when the view ended with the pass done (`rmb_gbuf_retro_done`), so a pixel is
never quantized twice; it still runs for a view with no render-size target (a target of 480 lines or
fewer, a menu's model view), as in PM2. `rmb_post_pass` takes the target and its scratch copy as
arguments for this; the tone map's and the frame's pass pass the frame's. The settings read are the same
(`r_retro_filter`, `r_retro_gamma`), set each frame through `rmb_gbuf_set_retro`.

## Tests and mutants

`tests/metal/rm_retro_tex_test.c` (197 checks, run by `tests/check-ref-metal.sh`, line `Retro textures 197
checks, 0 failed`): every byte at every rank for 4, 5 and 6 bits against a floating-point reference; black
and white kept; 128 giving 123 or 132 in 5 bits and 125 or 130 in 6; every channel of a 565 image one of
32 or 64 levels and its alpha untouched; every channel of a 4444 image a multiple of 17 with alpha 0 and
255 kept, a middle alpha going to its two neighbouring levels; a flat image becoming the Bayer pattern with
period 4, each texel its own rank's value, the 16 averaging to the byte; the cap's dimensions (512 to 256,
256 kept, 1024 x 256 to 256 x 64, 2048 to 256, 300 x 200 to 150 x 100, 257 x 1 to 129 x 1); the in-place
box average of a checker (100) and of a gradient against the four sources; the cap in place at odd and
non-square sizes (1 x 1000, 1000 x 1, 257 x 3, 3 x 257, 301 x 299, 1025 x 513, 2049 x 1, 512 x 64,
257 x 257, 4096 x 4096), each in a buffer of exactly w x h x 4 bytes so a read past it is a sanitizer error,
against a reference halving into a second buffer and against `mtl_retro_cap_dims`'s size; the filter name; the view's scale
(1 at 480 lines and below, 480 over the height above, 480 lines after rounding at 481, 1080 and 2880).

Each mutant was applied to a scratch copy of the header and the test built against it; each is red:

| mutant | result |
| --- | --- |
| no dither (rank 0 for every texel) | 79 of 167 failed |
| alpha forced to 255 in the 4444 class | 3 of 167 failed |
| green quantized to 5 bits | 65 of 167 failed |
| opaque images quantized as 4444 | 65 of 167 failed |
| the cap rounds its sides down | 1 of 167 failed |
| the cap is 512 | 10 of 167 failed |
| the cap takes one texel, not the average | 2 of 167 failed |
| 540 lines | 6 of 167 failed |
| the filter is `GL_LINEAR_MIPMAP_LINEAR` | 1 of 167 failed |

Review round 1 found two mutants of `mtl_retro_cap_apply` that the 512 x 512 cases let through. With the
odd-size cases (197 checks) both are red, under ASan and UBSan:

| mutant | result |
| --- | --- |
| the last row's source not clamped (`y1 = 2 * y + 1`) | AddressSanitizer: heap-buffer-overflow (exit 134) |
| the last row of an odd height dropped (`nh = *h / 2 + (*h == 1)`) | 10 of 197 failed |
| none (the header as committed) | 197 checks, 0 failed |

The shader's use of the pass and the wiring in `rm_main.c`, `rm_gbufscene.c` and `rmb_gbuf.m` are not read
back by a host test; they are shown by the run below and by the frames.

## The run (M6, Apple M6, in a `heavy.sh` hold)

`r_presentation 2` on `base1` at 1920 x 1080, `+map base1`, 25 s, the log read from the run's own file:

```
ref_metal: presentation: retro (r_presentation 2): the 3D view in RGB565 with the 4 x 4 dither, r_retro_filter and r_retro_gamma read each frame
ref_metal: retro: 3D view 853x480 of a 1920x1080 target, enlarged by a bilinear blit, no MetalFX; textures 163 RGB565 and 33 ARGB4444, 20 capped to 256x256, largest side 256, filter GL_LINEAR_MIPMAP_NEAREST, no anisotropy
```

The second line is new, printed once for each target and render size at the first Retro 3D view (so after
the map's textures are loaded). It shows the 480-line target (853 x 480 from 1920 x 1080), the cap (20
textures reduced, the largest side kept 256), the two texel classes and the filter. The same run at
`r_presentation 0` prints no `retro:` line.

## Pins

- **Stock unchanged.** Every committed scenario except the Retro ones, recorded seeded (`PINS_SEED=1`, with
  the trace) on Metal at 640 x 480 into scratch stores by a build of main (`bebb1f12`) and by the branch
  build, compared by the hash of the frames' hashes. 24 scenarios, byte-identical, 23 frames each unless
  noted:

  `world`, `world-fullbright`, `world-lmcubic`, `world-scale8`, `world-metalfx1`, `world-metalfx2`,
  `world-metalfx3`, `world-metalfx-interp`, `hud2d`, `hud2d-sharp`, `hud2d-scale8`, `models`, `particles`,
  `particles-cap`, `dlights`, `dlights-lmcubic`, `rt-ao`, `rt-ao-entities`, `rt-shadows`,
  `rt-shadows-entities`, `rt-sun`, `hdr`, `hdr-metalfx2` and `cinematic` (12 frames, `idlog.cin`,
  `PINS_EVERY=7`). The hashes of the 22 scenarios PM2 also compared match PM2's table (`world`
  `b15b40cf1`, `models` `552ee3cb1`, `rt-sun` `374691775`, and the rest).
- **Not compared: the water scenarios** (`skywater`, `rt-water`, `rt-water-entities`). The water demo is
  made by `pins.sh make-demo water`, which failed because the base worktree had no `game.dylib` when it
  ran. The first hold was wasted for the same reason (the game module was missing from both builds, so
  every record failed); the second began with the module built, but its `make-demo` step ran in the same
  script before any fix could apply, so the three water scenarios recorded 0 frames on both builds. The
  two-hold limit left no hold to redo them. Their stock code path is the one the other 24 exercise
  (`R_Upload32`, `R_TextureMode`, `RM_GB_Choose`, `rmb_gbuf_view_end`); this is reported, not claimed as
  compared.
- **Retro pins are not recorded.** PM4 is where the owner accepts the look and the pins are recorded; no
  reference of the pin store was replaced. PM2's records of `retro` and `retro-filter` in the store no
  longer match: with the upload and the sampling, every retro frame differs from PM2's. Frames of the
  current branch went to the artifacts folder and were not looked at: `retro` (`d59665e52ca2`) and
  `retro-filter` (`0306b79f65d8`) at 640 x 480, 23 frames each, none equal to `world`'s (`b15b40cf1604`),
  and both again at 1920 x 1080. At the harness's 640 x 480 the view is 480 lines already, so those
  frames show the upload and the sampling but not the enlarged view; the 1080 set shows it.

## Checks

| check | last line |
| --- | --- |
| `make ref_metal` | `release/ref_metal.dylib' is up to date.` (built before the hold; the hold's `make` found nothing to do) |
| `tests/check-ref-metal.sh` | `check-ref-metal: ok (13159 checks, 0 failed; ...; Retro textures 167 checks, 0 failed, ...; ref_metal built)` |
| `make test` | `yq2test: 444 tests, 0 failed` |
| `check-branch.sh refs/remotes/origin/main HEAD` | run after the last commit; see the card's report |

## Left for PM4 or a follow-up

- The retro overlay of plan call P8 (`r_metalfx` and the `r_rt_*` effects off under Retro) is still not in
  `rm_pres_value`: PM3 only keeps the scaler from running under Retro. With an effect on, Retro takes the
  G-buffer path at 480 lines.
- The cost of the 480-line view and of the post-process at it is not measured here; the view rasterizes
  about 0.41 megapixels at 853 x 480 against 2.07 at 1080p.
- The output filter and the box average work at the 480-line pixels in the enlarged view; whether the
  owner wants them at the window size is PM4's to judge from frames.
- A mode change without a `vid_restart` is as before: nothing happens until one.
