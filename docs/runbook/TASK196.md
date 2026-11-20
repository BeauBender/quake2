# HDR output in `ref_metal` (TASK196)

Card: TASK196 (#553). Plan: [docs/plans/hdr-output.md](../plans/hdr-output.md). This file holds one
section per slice; HD1 (the headroom readings, display on) is not done yet, so the headroom reads
`unavailable` below.

## HD2: `r_hdr`, the float16 path

Done with the screen locked and the display off: frames render and the pins compare byte for byte;
no timing, no headroom reading. No frame was looked at.

### What it is

- `r_hdr` (archived, default 0; only 1 is on) is read each frame in `RI_BeginFrame` and applied
  between frames. `rm_hdr_active` takes it off under Retro (`r_presentation 2`, the plan's H5). A
  line is printed on a live change only, so neither a stock run nor a start at `r_hdr 1` puts a
  notify line into a recorded frame.
- On the float16 path the frame target (`rmb_frame_begin`), the four present slots
  (`rmb_present_take`, now dropped on a format change as on a size change, `rm_hdr_slot_keep`) and
  the scaled view's `t_rcolor` (`rmb_gbuf.m`) are `RGBA16Float`, remade at the next frame.
- Pipelines: every render pipeline is now built through `rmb_new_render_pipe`, which keeps a copy of
  the descriptor of each one that draws into `BGRA8Unorm`; `rmb_fmt_pipe`, at each
  `setRenderPipelineState:`, returns the pipeline itself on the stock path and, on the float16 path,
  its variant with those attachments `RGBA16Float`, built at its first use. A stock run builds
  nothing more.
- The layer (`rmb_hdr_apply`, `rmb_device.m`): `RGBA16Float`, `kCGColorSpaceExtendedLinearSRGB`,
  `wantsExtendedDynamicRangeContent` YES; back to `BGRA8Unorm` with the colour space
  `metal_colorspace` gives it. Under `metal_colorspace 0` the colour space found on the layer is now
  kept when HDR goes on and put back when it goes off (the "as found" capture of the plan's H4);
  while HDR is on, `metal_colorspace` leaves the layer alone. A drawable acquired ahead in the old
  format is not used, and a drawable of another format than the frame's is not presented.
- The present on the float16 path is always the pass, `rm_fragment_present_hdr`, which decodes the
  stored sRGB-encoded values to linear once (`mtl_hdr_linear`, extended past 1.0, sign kept).
- `rmb_read` on the float16 path draws `rm_fragment_hdr_read` (each channel clamped to [0, 1]) into an
  8-bit copy and reads that, so the harness reads 8-bit frames as before.
- MetalFX: `rmb_fx_prepare` runs no scaler on the float16 path (`rm_hdr_fx_mode`) and says so once
  (`RM_FX_NOTE_HDR`: "the MetalFX ... scaler is off under r_hdr 1 (it takes 8-bit colour); the view
  is stretched"), until HD4.
- `metal_hdr_info` prints the layer's format, colour space name and EDR property; the headroom is
  `unavailable` until HD1 reads it.

### Tests

`tests/metal/rm_host_test.c` (`test_hdr`): the mode, the gate under Retro, the format, the slots
dropped on a format or size change, MetalFX off, `metal_hdr_info`'s line, and the shared arithmetic
(`common/mtl_hdr_math.h`): the decode's fixed points, toe, 2.4 segment, values past 1.0, sign and
monotonicity, and the clamp's bytes (every byte value reads back as itself; above 1, below 0 and
NaN clamp). `tests/metal/rm_fx_test.c`: the note's line. Hold 1, commit `9d1d735e` plus the code of
`45715a03`: `check-ref-metal: ok (13067 checks, 0 failed; ... MetalFX 321 checks, 0 failed; ...)`.

### Pins at `r_hdr 0`

Every committed scenario but `hdr`, recorded on Metal (apple9, this Mac, screen locked) by the base
build (main at `005378bd`) and the branch build (`791ba7d7`), both `PINS_SEED=1` with the trace, 640 x
480, into scratch stores, compared by the hash of the frames' hashes.

| scenario | frames | base | branch |
| --- | --- | --- | --- |
| `cinematic` (`idlog.cin`, `PINS_EVERY=7`) | 12 | `83cda5a3ee9b` | identical |
| `dlights-lmcubic` | 23 | `47b37c13760d` | identical |
| `dlights` | 23 | `ac67e8f5ce7a` | identical |
| `hud2d-scale8` | 23 | `32724f15ca55` | identical |
| `hud2d-sharp` | 23 | `f9ffe0290d69` | identical |
| `hud2d` | 23 | `f9ffe0290d69` | identical |
| `models` | 23 | `552ee3cb16b8` | identical |
| `particles-cap` | 23 | `75b26afbad69` | identical |
| `particles` | 23 | `75b26afbad69` | identical |
| `retro-filter` | 23 | `33dfbc89de5a` | identical |
| `retro` | 23 | `1c33e27aa3ff` | identical |
| `rt-ao-entities` | 23 | `f1a43c24854f` | identical |
| `rt-ao` | 23 | `53c06d8a47ad` | identical |
| `rt-shadows-entities` | 23 | `6cd65dd54373` | identical |
| `rt-shadows` | 23 | `bb85d7a96bf0` | identical |
| `rt-sun` | 23 | `374691775071` | identical |
| `world-fullbright` | 23 | `6e92dc9a2a96` | identical |
| `world-lmcubic` | 23 | `1a8ff9d43e26` | identical |
| `world-metalfx-interp` | 23 | `b15b40cf1604` | identical |
| `world-metalfx1` | 23 | `8bb165628bea` | identical |
| `world-metalfx2` | 23 | `15df2f0d4ca4` | identical |
| `world-metalfx3` | 23 | `15df2f0d4ca4` | identical |
| `world-scale8` | 23 | `b611e93476e1` | identical |
| `world` | 23 | `b15b40cf1604` | identical |

| `rt-water-entities` (`water.dm2`) | 4 | `35b7dd160f74` | identical |
| `rt-water` (`water.dm2`) | 4 | `35b7dd160f74` | identical |
| `skywater` (`water.dm2`) | 4 | `df83a256bcbb` | identical |

27 of 27 byte-identical: at `r_hdr 0` the branch changes no pixel. The 24 rows above the water rows
were recorded in hold 2 (branch `791ba7d7`); the water rows in hold 3 (branch `fb47cb4b`, the same
engine source as `791ba7d7` plus this file), against the same base build, with one `water.dm2`
made by `make-demo water` on the branch and copied beside the base binary (two `make-demo` runs give
two different files, so these hashes are not TASK184's).

### The `hdr` record

`scripts/pins/scenarios/hdr.cfg` (`world` with `r_hdr 1`), branch `791ba7d7`, `PINS_SEED=1`, 640 x
480: 23 frames, hash `11b7dcc7968c`, twice in scratch stores and once into the pin store
(`hdr/metal/791ba7d7c093-3613970603`). Against the branch's `world`: 0 of 23 frames equal; over the
23 frames 5.13% of the bytes differ, every one by 1 and none by more, which is the one 8-bit step of
the plan's section 1 (float16 rounds a blend once where 8 bits round each pass). That is why it is its
own reference.

### The live switch

`r_hdr` changed while frames are drawn, before the demo starts (the frames before `demomap` are
the console's, drawn through the whole frame: target, present, layer), then the demo recorded as
`world` is (the driver's command line, `PINS_TRACE=0`, the scenario's `demomap` replaced by a
script with `wait` between the changes; a switch during the demo itself cannot be scripted: the
waits hold back the server's own commands and the demo stops with `CM_InlineModel: bad number`).
Hold 3, branch `fb47cb4b`:

| run | what changes before the demo | frames | hash |
| --- | --- | --- | --- |
| base | nothing (`r_hdr 0`) | 23 | `b15b40cf1604` (the `world` pin's) |
| hdr | `r_hdr 1` from the start | 23 | `11b7dcc7968c` (the `hdr` pin's) |
| live010 | 20 frames at 0, 20 at 1, 20 at 0, then the demo | 23 | `b15b40cf1604`, equal to base |
| live01 | 20 frames at 0, 20 at 1, then the demo | 23 | `11b7dcc7968c`, equal to hdr |

So after 0, 1, 0 the frames equal those of a run that never switched, and a switch from 0 to 1
draws what a start at 1 draws. `metal_hdr_info` in live010, with the screen locked and the display
off:

```
ref_metal: hdr: layer BGRA8Unorm, colour space kCGColorSpaceSRGB, extended dynamic range off; headroom unavailable now, unavailable potential
ref_metal: hdr: on (r_hdr 1): the frame in float16, shown at SDR until the tone map
ref_metal: hdr: layer RGBA16Float, colour space kCGColorSpaceExtendedLinearSRGB, extended dynamic range on; headroom unavailable now, unavailable potential
ref_metal: hdr: off
ref_metal: hdr: layer BGRA8Unorm, colour space kCGColorSpaceSRGB, extended dynamic range off; headroom unavailable now, unavailable potential
```

The layer was found tagged `kCGColorSpaceSRGB` under `metal_colorspace 0` (as TASK121 found), and
that is what it has again after `r_hdr 0`.

### Mutants

Each applied to a copy of `rm_pure.c`, the host test built against it with `check-ref-metal`'s
flags (hold 3, branch `fb47cb4b`); the control build passes all 13067 checks.

| mutant | result |
| --- | --- |
| a format change keeps the present slots (`rm_hdr_slot_keep` ignores the format) | 2 of 13067 failed |
| `r_hdr 0` takes the float16 path (`rm_hdr_format` always `RGBA16Float`) | 1 of 13067 failed |
| Retro takes the float16 path (`rm_hdr_active` ignores Retro) | 1 of 13067 failed |
| MetalFX stays on under `r_hdr 1` (`rm_hdr_fx_mode` returns the mode) | 2 of 13067 failed |

The engine-level form of the second mutant is the pin comparison above: any float16 frame at
`r_hdr 0` would differ from the base, as the `hdr` record differs from `world`.

### Checks and the gate

Hold 3, branch `fb47cb4b`: `check-ref-metal: ok (13067 checks, 0 failed; ...)`, `pins selftest: ok`,
`check-cvar-docs: ok`, `check-doc-links: ok`, `check-runbook-index: ok`,
`check-no-local-identity: ok`. `scripts/test.sh` on the same tree: 2 of 45 checks failed,
`check-tvos.sh` (`wantsExtendedDynamicRangeContent` is unavailable on tvOS, so the build of
`ref_metal` for tvOS stopped) and `check-runbook-index.sh` (a bare pointer phrase in this file,
written while the gate ran). Both are fixed in the next commit (the property is set only on macOS,
iOS, iPadOS and visionOS); the gate was not run again in this slice.

### Left

- HD1: the headroom readings (`metal_hdr_info` prints `unavailable`), with the display on.
- The layer's state on a display that is on and an EDR pixel read (`metal_hdr_probe`, HD3).
- HD3 to HD7 as the plan's section 7 has them; the tone map, `r_hdr_peak` and the report line are HD3.
- The ray-tracing passes draw into `t_rcolor` through their float16 variants on this path; their
  terms stay clipped in `t_light` until HD6.

## HD2, rework round 2

Review of `fc5ca005` found one Important and several notes; the fixes, each its own commit.

- `0513c1da` (I1): the HDR present clamped nothing, so a display with headroom showed above-white
  pixels (the float16 target does not clamp an additive blend), against what the docs and plan H6
  say. `rm_fragment_present_hdr` now writes `mtl_hdr_present(c)` (`common/mtl_hdr_math.h`: the clamp to
  [0, 1], NaN to 0, then the decode), with a host test; HD3's tone map replaces the clamp.
- `5b369c81`: a pipeline of the float16 path that cannot be built (`rmb_fmt_pipe`, the present's,
  the readback's) calls `rmb_hdr_fail`: `r_hdr` is off for the session through the next
  `rmb_hdr_set` (between frames), as a live `r_hdr 0` does, said once on stderr. `rmb_fmt_pipe`
  returns the stock pipeline, never nil, as `rmb.h` now says; the frame that failed is not presented
  (its drawable and frame differ in format), the next is stock. The Retro line
  ("hdr: off: Retro is a 16-bit SDR look") is said once on a live `r_hdr 1` under Retro (it sat
  behind the early return); a start under Retro stays quiet. doc/040 says `r_hdr` has no effect
  under Retro (the cvar keeps its value). `rmb_hdr_info` checks `CFStringGetCString` ("unknown" on
  failure).
- The colour-space probe (`tests/metal/rmb_cs_probe.m`) runs headless and now drives `r_hdr` 0, 1,
  0 with `metal_colorspace` 0 and 1 (format and colour space back as they were) and the failure
  path (off at the next `rmb_hdr_set`, stays off). The fix to clear the failed state between its
  cases is the last code commit.

### Re-run

Hold 1 (branch at `5b369c81`) and hold 2 (`check-ref-metal` after the probe fix), `PINS_SEED=1`,
640 x 480, scratch stores, base = main at `878e9051`:

| record | branch | base |
| --- | --- | --- |
| `hdr` | `11b7dcc7968c`, equal to round 1's | (no such scenario) |
| `world` | `b15b40cf1604` | `b15b40cf1604` |
| `rt-ao` | `53c06d8a47ad` | `53c06d8a47ad` |
| `retro` | `1c33e27aa3ff` | `1c33e27aa3ff` |

The `hdr` hash did not change: the readback reads the target, not the drawable, so the present's
clamp is not in it. Checks: `check-ref-metal: ok (13072 checks, 0 failed; ...; colour space 118
checks, 0 failed; ...)`, `check-tvos: ok`, `check-ios: ok`, `check-runbook-index: ok`,
`check-cvar-docs: ok`, `check-doc-links: ok`, `check-no-local-identity: ok`, `make test`: 397 tests,
0 failed. The first `check-ref-metal` run in hold 1 failed the new probe cases (a failed session
stays off, and the probe's later cases saw it); fixed and passing in hold 2. The gate
(`scripts/test.sh`) is the parent's.

## HD3: the tone map, `r_hdr_peak`, the report line and `metal_hdr_probe` (screen locked)

Done with the screen locked and the display off, as HD2 was: the code, the host tests, the pins and
the probe's mechanics. No timing, no headroom reading, no frame looked at. The parts that need the
display on are listed at the end, for a child card.

### What it is

- The tone map (`common/mtl_hdr_math.h`, the plan's H3): `mtl_hdr_tone` maps each encoded channel
  of a pixel by the largest of its linear components, `mtl_hdr_max`: as it is at or below white
  (bit for bit), `mtl_hdr_shoulder` above it (`1 + e / (1 + e / (P - 1))`, `e = x - 1`, held to
  `P`), the pixel scaled by `f(m) / m` so its hue is kept, then encoded back (`mtl_hdr_encode`);
  not a number and below 0 to 0; at `P <= 1` it is HD2's clamp, `mtl_hdr_clamp`. It runs in place
  as `rm_fragment_tonemap`, through `rmb_tonemap_pass`, from `R_HdrToneMap` in `RI_RenderFrame`:
  the seat of `R_RetroFinish` (the view's rectangle at the output size, after the view and its
  scaling, before the 2D layer). Retro and `r_hdr 1` never run together (HD2's `rm_hdr_active`), so
  the two share the seat and its copy texture; `rmb_retro_pass` and `rmb_tonemap_pass` now share
  `rmb_post_pass`.
- The present (`rm_fragment_present_hdr`) now holds each linear channel to `[0, P]`
  (`mtl_hdr_show`); at `P = 1` that equals HD2's `mtl_hdr_present` for every input (the host test
  sweeps it bit for bit).
- `r_hdr_peak` (archived, default 4, held to 1 to 16): the peak in force is
  `mtl_hdr_peak(r_hdr_peak, headroom now)`, set each frame in `R_HdrFrame` (`rmb_hdr_peak_set`), 1.0
  when the headroom is unavailable or not above 1.0. The headroom comes from `rmb_hdr_headroom`
  (`rmb_device.m`), which reads `unavailable` until HD1 fills it, so on every host today the peak in
  force is 1.0 and `r_hdr 1` is shown clamped at SDR white, as in HD2.
- The report line (`rm_hdr_report`), on a live change of `r_hdr`, or of `r_hdr_peak` while on, never
  at start (the rule of `791ba7d7`: a value set before the first frame is taken without a line):
  `on (r_hdr 1), peak P (r_hdr_peak V, display N now, M potential)`, `on, shown at SDR: this display
  reports no extended range`, `on, shown at SDR: the display's headroom is unavailable`, or `off`.
- `metal_hdr_info` prints a second line, `rm_hdr_peak_line`: the peak in force, `r_hdr_peak` and the
  headroom now.
- `metal_hdr_probe <x> <y>` (`rmb_hdr_probe_ask`, `rmb_hdr_probe_copy` in `rmb_frame_end`): at the
  next present the pixel is copied out of the drawable after the present wrote it and before
  `presentDrawable:`, and printed when its command buffer completes (`rm_hdr_probe_format`: four
  float16 values for `RGBA16Float`, bytes over 255 for `BGRA8Unorm`, and whether a component is above
  1.0). When no drawable is presented in 60 frames it says so and reads nothing.
- `scripts/pins/scenarios/hdr.cfg` sets `r_hdr_peak 1`, so its frames are the clamp on any display,
  not a function of a display's headroom once HD1 reads one. The pin id changes with the file; the
  frames do not (below).

### Tests and mutants

`tests/metal/rm_host_test.c`, `test_hdr_tonemap`: the peak held to `[1, 16]` and to the headroom
(unavailable, NaN, 1.0 and below give 1.0; a peak above the headroom is the headroom); over a sweep
of 0 to 40 at six peaks from 1.0001 to 16, the shoulder is the identity at and below 1, monotonic and
never above the peak (also at 65504 and infinity), with slope 1 at white; NaN and negative inputs
to 0 through the shoulder, the tone map, the max and the present; a pixel at or below white is
untouched bit for bit; above white the largest component follows the shoulder and the ratios of the
components are kept; and at a headroom of 1.0, -1, NaN or 0.25 and every `r_hdr_peak` from -3 to 40,
the tone map and the present together equal `mtl_hdr_present` bit for bit (so byte for byte) over
-1 to 7 in steps of 1e-4 and at NaN, infinity and 65504, and the in-place tone map gives the
readback the bytes it gave in HD2. Also the report, peak and probe lines and `rm_hdr_half`.

Hold 1, branch `ab01f8d0`, each in a scratch copy (`git archive`) built with `check-ref-metal`'s flags:

| case | result |
| --- | --- |
| control | 13169 checks, 0 failed |
| red: main's `mtl_hdr_math.h`, `rm_pure.c` and `rmb.h` | does not compile: `call to undeclared function 'mtl_hdr_max'` |
| the shoulder overshoots the peak (`e / P` for `e / (P - 1)`, its hold at `P` removed) | 3 failed (the peak bound at 1000, the sweep's bound, the hue check) |
| the headroom clamp removed (`mtl_hdr_peak` returns `r_hdr_peak`) | 11 failed |
| a headroom of 1.0 does not reduce to the clamp (`mtl_hdr_tone`'s `P <= 1` branch removed) | 2 failed (the bit-for-bit and the byte sweeps) |

### Pins

Branch `ab01f8d0` (the engine source of this slice; later commits change only this file), recorded on
Metal (apple9, this Mac, screen locked), `PINS_SEED=1` with the trace, 640 x 480, into scratch
stores, each record's hash the hash of its frames' hashes (`shasum` of the frames, then of that
list, as HD2's table), compared with HD2's table above, whose base column is main before HD2 and
whose branch column is HD2 itself (now main, `88c15591`).

| scenario | frames | HD3 | against HD2's table |
| --- | --- | --- | --- |
| `cinematic` (`idlog.cin`, `PINS_EVERY=7`) | 12 | `83cda5a3ee9b` | identical |
| `dlights-lmcubic` | 23 | `47b37c13760d` | identical |
| `dlights` | 23 | `ac67e8f5ce7a` | identical |
| `hud2d-scale8` | 23 | `32724f15ca55` | identical |
| `hud2d-sharp` | 23 | `f9ffe0290d69` | identical |
| `hud2d` | 23 | `f9ffe0290d69` | identical |
| `models` | 23 | `552ee3cb16b8` | identical |
| `particles-cap` | 23 | `75b26afbad69` | identical |
| `particles` | 23 | `75b26afbad69` | identical |
| `retro-filter` | 23 | `33dfbc89de5a` | identical |
| `retro` | 23 | `1c33e27aa3ff` | identical |
| `rt-ao-entities` | 23 | `f1a43c24854f` | identical |
| `rt-ao` | 23 | `53c06d8a47ad` | identical |
| `rt-shadows-entities` | 23 | `6cd65dd54373` | identical |
| `rt-shadows` | 23 | `bb85d7a96bf0` | identical |
| `rt-sun` | 23 | `374691775071` | identical |
| `world-fullbright` | 23 | `6e92dc9a2a96` | identical |
| `world-lmcubic` | 23 | `1a8ff9d43e26` | identical |
| `world-metalfx-interp` | 23 | `b15b40cf1604` | identical |
| `world-metalfx1` | 23 | `8bb165628bea` | identical |
| `world-metalfx2` | 23 | `15df2f0d4ca4` | identical |
| `world-metalfx3` | 23 | `15df2f0d4ca4` | identical |
| `world-scale8` | 23 | `b611e93476e1` | identical |
| `world` | 23 | `b15b40cf1604` | identical |

The water rows do not match HD2's table: that table's `water.dm2` was a demo made on HD2's branch,
and the one kept on this host gives other frames (two `make-demo` runs give two different files). So
they were recorded on both sides with the same demo: the branch, and a base build of main at
`352b1887` (which holds HD2), in hold 3.

| scenario (`water.dm2`) | frames | base (main `352b1887`) | HD3 |
| --- | --- | --- | --- |
| `rt-water-entities` | 4 | `240c138cd6d8` | identical |
| `rt-water` | 4 | `240c138cd6d8` | identical |
| `skywater` | 4 | `22c21fe372ac` | identical |

27 of 27 byte-identical at `r_hdr 0`: the tone map, the peak uniform and the probe change no stock
pixel, and the refactor of the Retro pass into `rmb_post_pass` leaves `retro` and `retro-filter` as
they were.

### The `hdr` record

`hdr`: 23 frames, `11b7dcc7968c`, the hash of HD2's record. The readback reads the frame target,
which the tone map now writes in place; with the peak in force at 1.0 (the headroom is unavailable,
and the scenario sets `r_hdr_peak 1` besides) the tone map is HD2's clamp, and the readback's clamp
after it gives the same bytes, so the hash is unchanged. The scenario's pin id is new (the file gained
the `r_hdr_peak 1` line); its frames are not.

### `metal_hdr_info` and `metal_hdr_probe`, screen locked, display off

A run of the branch (`release/quake2 -portable +set basedir <datadir>`, `vid_renderer metal`, 640 x
480, `r_hdr 0`, `r_hdr_peak 4`, then a script of waits: `metal_hdr_info`; `r_hdr 1`;
`metal_hdr_info`; `r_hdr_peak 8`; `metal_hdr_probe 320 240`; `metal_hdr_info`; `r_hdr 0`;
`metal_hdr_probe 320 240`; `metal_hdr_info`), the console's frames (no map):

```
ref_metal: hdr: layer BGRA8Unorm, colour space kCGColorSpaceSRGB, extended dynamic range off; headroom unavailable now, unavailable potential
ref_metal: hdr: peak 1.00 in force (r_hdr_peak 4.00, headroom unavailable)
ref_metal: hdr: on, shown at SDR: the display's headroom is unavailable
ref_metal: hdr: layer RGBA16Float, colour space kCGColorSpaceExtendedLinearSRGB, extended dynamic range on; headroom unavailable now, unavailable potential
ref_metal: hdr: peak 1.00 in force (r_hdr_peak 4.00, headroom unavailable)
ref_metal: hdr: on, shown at SDR: the display's headroom is unavailable
ref_metal: hdr: probe (320, 240) of the drawable, RGBA16Float: r 0.0087 g 0.0052 b 0.0032 a 1.0000, at or below SDR white
ref_metal: hdr: layer RGBA16Float, colour space kCGColorSpaceExtendedLinearSRGB, extended dynamic range on; headroom unavailable now, unavailable potential
ref_metal: hdr: peak 1.00 in force (r_hdr_peak 8.00, headroom unavailable)
ref_metal: hdr: off
ref_metal: hdr: probe (320, 240) of the drawable, BGRA8Unorm: r 0.0902 g 0.0627 b 0.0431 a 1.0000, at or below SDR white
ref_metal: hdr: layer BGRA8Unorm, colour space kCGColorSpaceSRGB, extended dynamic range off; headroom unavailable now, unavailable potential
ref_metal: hdr: peak 1.00 in force (r_hdr_peak 8.00, headroom unavailable)
```

The third line is the live `r_hdr 1`, the sixth the live `r_hdr_peak 8`; the start (`r_hdr 0`,
`r_hdr_peak 4` on the command line) printed nothing. With the display off the drawables are still
presented, so the probe reads: the console's background pixel, `RGBA16Float` linear `0.0087` under
`r_hdr 1` and the byte `0.0902` (23 of 255) under `r_hdr 0`, the same colour (the sRGB decode of
0.0902 is 0.0087). No value above 1.0 is expected here: the headroom reads unavailable, the peak in
force is 1.0, and the console draws no highlight. That is the expected locked-screen result; the
reading above 1.0 is left for the display-on card.

### Checks

Hold 1 (branch `ab01f8d0`): `check-ref-metal: ok (13169 checks, 0 failed; ...)`, `check-tvos: ok`,
`check-ios: ok`, `pins selftest: ok`, `check-cvar-docs: ok`, `check-doc-links: ok`,
`check-runbook-index: ok`, `check-no-local-identity: ok`, `check-bench-quake2: ok (32 cases)`,
`check-mode-size: ok`, `make test`: 397 tests, 0 failed. Hold 3, on the tree with this section: the
doc checks again (see the lane's report). The gate (`scripts/test.sh`) is the parent's.

### Left for the display-on card

- HD1's headroom readings: `rmb_hdr_headroom` returns `unavailable` until they land, so the peak in
  force is 1.0 everywhere and the shoulder never runs in the engine yet (it runs, at `P = 1` as the
  clamp, in every `r_hdr 1` frame; the host test covers `P > 1`).
- The owner's frames: HDR on and off pairs on the M6's display, awake and unlocked, the display named.
- The 5K cost of `r_hdr 1` (the plan's section 6 estimate, measured), with the timing protocol.
- `metal_hdr_probe` reading a value above 1.0 from the drawable on an EDR display, on an explosion
  core with `r_hdr_peak` above 1.0, and `metal_hdr_info` showing a headroom above 1.0.

## HD3, rework round 2

Review of `ac990ba8` found one Important and several notes; the fixes, each its own commit.

- `56f5f135` (I1): `metal_hdr_probe -5 10` cancelled the probe (a negative coordinate is the
  internal cancel of `rmb_hdr_probe_ask`) while `R_HdrProbe_f` still armed the wait, so sixty frames
  later it printed a false "no drawable was presented"; an argument past `INT_MAX` wrapped negative
  through `(int)strtol` and did the same. The parse is now `rm_hdr_probe_args` (`rm_pure.c`): each
  coordinate a whole decimal number held to [0, `INT_MAX`], so a console ask never cancels (the probe
  then holds it to the drawable, as doc/050 says); anything else prints the usage line and arms
  nothing. Cancel stays the timeout's. Red: the host test against a stub with the old parse failed
  10 checks (negative, huge, past `long`, non-numeric, empty, trailing junk, a fraction, a missing
  argument).
- `5778227f`: the probe's generation and state are one atomic word (`rm_probe_word` in `rmb.h`:
  state in the low 2 bits, a 30-bit wrapping generation above), and every change is a
  compare-and-swap of the whole word. The completion handler expects the word its own copy wrote,
  which a re-ask always moves on, so the old load-then-swap race (a re-ask and a new copy between a
  stale handler's load and its swap) cannot mark the new probe read. The host test checks the word's
  arithmetic, that the stale handler's expected word differs from the new copy's, and the wrap.
  `rmb_encode_release` now drops `probe_buf` and sets the word idle in the next generation (not to 0,
  so a completion still in flight from before the release matches no later probe).
- `506ee3b8`: doc/040's `r_hdr` and `r_hdr_peak` said a live change prints the peak in force; with
  the headroom unavailable or not above white the line is `on, shown at SDR` with the reason and no
  peak (`rm_hdr_report`). The docs now say so.
- `2eaa1f4a`: the tone map maps each pixel once a frame. `R_HdrToneMap` runs after each view, and the
  player model of the menu (`RDF_NOWORLDMODEL`, `menu.c`) is a second view over the world. The trace:
  that view's draws start with `R_ClearNoWorld` (`R_RenderViewDraw`), a quad of 0.3 grey at alpha 1
  over all of clip space with no depth test, in `r_view.vp`, which `R_SetupGL` takes from
  `rm_view_rect` of the same `fd` rectangle, vid size and target height that `R_HdrToneMap` maps; an
  `RDF_NOWORLDMODEL` view is always the forward path (`rm_gb_path`), and MetalFX is off under
  `r_hdr 1`. So every pixel of that rectangle is repainted, opaque, before the model view's map, and
  what the world's map wrote there is gone: inside the rectangle a pixel is mapped once (by the model
  view), outside it once (by the world's). The code is unchanged; `R_HdrToneMap` says why, and the
  host test walks that order on a strip of pixels at a peak of 4, with a check that the shoulder
  applied twice is not applied once (so a view that did not repaint would show). Retro has the same
  shape (`R_RetroFinish` after each view, at the same seat) and the same answer: the model view's
  rectangle is repainted before its Retro pass, so no pixel is quantized twice; Retro is unchanged.

Left as notes for later: fast-math NaN handling in the shaders, and the report line's `%.2f`.

### Re-run

One hold, branch at `2eaa1f4a`, the game module built first, `PINS_SEED=1`, 640 x 480, a scratch
store, each record's hash the hash of its frames' hashes (`shasum` of the frames, then of that list):

| scenario | frames | branch | main (the tables above) |
| --- | --- | --- | --- |
| `world` | 23 | `b15b40cf1604` | identical |
| `retro` | 23 | `1c33e27aa3ff` | identical |
| `models` | 23 | `552ee3cb16b8` | identical |
| `hud2d` | 23 | `f9ffe0290d69` | identical |
| `hdr` | 23 | `11b7dcc7968c` | identical (HD2's and HD3's) |

The probe, the screen locked (the drawable is 1280 wide at 640 x 480 points), first at `r_hdr 0`
and then at `r_hdr 1`:

```
ref_metal: hdr: probe (0, 10) of the drawable, BGRA8Unorm: r 0.1333 g 0.0941 b 0.0588 a 1.0000, at or below SDR white
ref_metal: hdr: probe (1279, 5) of the drawable, BGRA8Unorm: r 0.1333 g 0.1059 b 0.0824 a 1.0000, at or below SDR white
usage: metal_hdr_probe <x> <y>: one pixel of the drawable, from its top left (whole numbers, held to the drawable's size)
ref_metal: hdr: on, shown at SDR: the display's headroom is unavailable
ref_metal: hdr: probe (0, 10) of the drawable, RGBA16Float: r 0.0156 g 0.0093 b 0.0047 a 1.0000, at or below SDR white
ref_metal: hdr: probe (1279, 5) of the drawable, RGBA16Float: r 0.0157 g 0.0110 b 0.0072 a 1.0000, at or below SDR white
ref_metal: hdr: off
```

`metal_hdr_probe -5 10` read pixel (0, 10), `metal_hdr_probe 99999999999 5` the last column, and
`metal_hdr_probe abc 5` printed the usage line; the timeout line was not printed. Checks:
`check-ref-metal: ok (13197 checks, 0 failed; ...)`, `check-tvos: ok`, `check-ios: ok`,
`check-cvar-docs: ok`, `make test`: 406 tests, 0 failed; the doc checks on the tree with this
section are in the lane's report. The gate (`scripts/test.sh`) is the parent's.

## FIX133: a pipeline failure on the float16 path

HD2's switch-off waits for the frame boundary; on the frame that failed three gaps lasted one frame
each. Fixed:

- A variant that is not built is never bound. `rmb_fmt_pipe` returns nil, not the stock `BGRA8Unorm`
  pipeline, on the failing ask and every later one, and every draw binds through `rmb_fmt_bind`, which
  returns 0 with nothing bound. Its caller then skips the draw. No pipeline is made at failure time.
- A drawable nothing wrote is not presented. `rmb_present_region` returns 0 in three cases: the
  formats differ, the float16 path has no present pipeline, or a variant of the frame was not built.
  `rmb_present_written` presents only a nonzero region. Otherwise the drawable is dropped, as a frame of
  the other format always was.
- The lifetime: a failure keeps `r_hdr 1` drawing as `0` until the next renderer start (`vid_restart`),
  which tries the float16 path again (doc/040, `r_hdr`). `rmb_hdr_dead` is cleared by `rmb_hdr_retry`,
  which only `rmb_resources_close` calls, right after it empties the caches that remember the failure:
  the `NSNull` entries of `fmt_variants` and the `*_tried` flags of the present, readback and tone-map
  pipelines. The flag and the caches therefore always reset together. `rmb_attach` no longer clears
  the flag, so an attach without a close cannot turn the path on against a cached failure. Two paths
  are closed this way. A cached `NSNull` would make `rmb_fmt_pipe` return nil without `rmb_hdr_fail`,
  so every draw would be skipped. A tried, nil present pipeline would give region 0 on every frame, so
  nothing would be presented.
- `rmb_present_pass` returns 0 when its pipeline is not bound, and the frame end then zeroes the
  region: that drawable is not presented.

### Tests and mutants

`tests/metal/rmb_cs_probe.m` now includes `rmb_resource.m` as well. Its new failure case runs under
`metal_colorspace` 0 and 1, for each of the probe's three layers. It refuses variant builds through the
file's test seam `fmt_refuse`, which only an including file can set, and uses mock objects that count
binds and presents. The case asserts that the failing ask and the next one bind nothing, that the frame
and a float16 frame without its present pipeline present nothing. A detach and an attach without
`rmb_resources_close` must keep the path off: `rmb_hdr_failed()` is still 1 after `rmb_hdr_set(1)`, and
`rmb_fmt_pipe(ps) != nil || !rmb_hdr_on()`. After the close, the flag is clear and `ps`'s cache entry is
gone. The next attach turns the path on, and the variant is built again: a refused build fails anew,
not from the cache. The older `r_hdr` loop runs its failure sub-case under both modes now, and it no
longer clears `rmb_hdr_dead` by hand. Result:
`rmb_cs_probe: 346 checks, 0 failed` (round 2; 292 in round 1), and nine colour-space mutants refused.

`check-ref-metal.sh` (f) adds five mutants, each reverting one fix, and the probe must fail each:

- `fmtstock`: `rmb_fmt_pipe` hands back the stock pipeline.
- `unwritten`: `rmb_present_written` presents a zero region.
- `failframe`: `rmb_present_region` presents the failing frame.
- `hdrattach`: `rmb_attach` clears `rmb_hdr_dead` again, while the caches stay.
- `hdrkept`: `rmb_resources_close` empties the caches and keeps `rmb_hdr_dead`.

Its new (f2) is a source guard: no `setRenderPipelineState:rmb_fmt_pipe(` in `rmb_*.m`, every
`rmb_fmt_bind(` call tested in an `if`, and no `presentDrawable` in `rmb_encode.m`.

### Pins at `r_hdr 0`

Branch `de91e822`, Metal, screen locked, `PINS_SEED=1` with the trace, 640 x 480, hashed as in HD3's
table (`shasum` of the frames, then of the list of hashes):

| scenario | frames | branch | against HD3's table |
| --- | --- | --- | --- |
| `world` | 23 | `b15b40cf1604` | identical |
| `models` | 23 | `552ee3cb16b8` | identical |
| `hdr` | 23 | `11b7dcc7968c` | identical |

## HD4: MetalFX and the denoiser under `r_hdr 1`

Plan sections 2.6 and 2.7, slice HD4. HD2 turned MetalFX off under `r_hdr 1` (`RM_FX_NOTE_HDR`);
the scalers now run on the float16 path.

### What it is

- `rmb_metalfx.m`: the scaler's colour format is the path's, `rmb_fx_color()` (that is,
  `rm_hdr_format(rmb_hdr_on())`, the format of `t_rcolor` and of the frame). Under `r_hdr 1` the
  spatial scaler takes `RGBA16Float` in and out with `MTLFXSpatialScalerColorProcessingModeHDR`;
  under `r_hdr 0` it is `BGRA8Unorm` and perceptual, as before. The temporal scaler's colour and
  output formats follow the path too. Auto exposure stays off, and `preExposure` is set to 1.0 (its
  default) at each encode. The output target `fx_out` takes the same format.
- The live switch: `rmb_fx_prepare` remakes the scaler when the format differs from the one it was
  made in (`fx_fmt`), as it does on a change of mode or size, so a toggle drops the history. A change
  of format also forgets the refusal memo, so a scaler refused in one format is tried again in the
  other. `rmb_fx_upscale` runs nothing unless the scaler's format is the path's, the frame's and
  the view's: a scaler made before a toggle runs on neither the old targets nor the new ones.
- The fallback: a scaler the GPU will not make in float16 leaves MetalFX off and tells
  `RM_FX_NOTE_HDR` once for that mode, size and format, and the view is stretched. The note now
  reads `the MetalFX <mode> scaler from WxH to WxH cannot be made in float16 colour; it is off under
  r_hdr 1 and the view is stretched`. In 8 bits a refusal is still `RM_FX_NOTE_UNMADE`.
  `rm_hdr_fx_note` (rm_pure.c) picks the note by format. It replaces HD2's `rm_hdr_fx_mode`, which
  turned every mode off.
- The denoiser: the tree has no `MTLFXTemporalDenoisedScaler` of its own yet (TASK59). Until a
  ray-traced effect exists, `r_metalfx 3` runs as the temporal scaler (`rm_fx_run`), so it follows
  the rule above. When TASK59 builds it, its output takes `rmb_fx_color()` and the same
  verify-or-fallback rule; the GPU's acceptance is recorded below.
- The tone map stays after MetalFX (plan 2.6); the fallback of moving it before was not applied
  (see *Stability*).

### Tests and mutants

`tests/metal/rmb_cs_probe.m` now includes `rmb_metalfx.m` as well and links `rm_metalfx.c` (and
MetalFX, weakly). Its `rmb_frame_compute` stub hands out one real command buffer, which the probe
commits, and counts its calls. The new case, `fx_case`, runs for each of the three layers under
`metal_colorspace` 0 and 1:

- Under `r_hdr 0`: the spatial and the temporal scaler and `fx_out` are `BGRA8Unorm`, the spatial
  one perceptual, and the spatial one runs on the GPU.
- Under `r_hdr 1`, with the 8-bit scaler of `r_hdr 0` still prepared: a float16 view and frame are
  not run (no `rmb_frame_compute`).
- Under `r_hdr 1`: the spatial scaler is `RGBA16Float` in and out, its mode
  `MTLFXSpatialScalerColorProcessingModeHDR`, and `fx_out` is `RGBA16Float`. An 8-bit frame or an
  8-bit view is not run. Both scalers run on the GPU in float16, and the command buffer completes.
- A refusal, forced through the file's test seam `fx_refuse` (static, set only by a file that
  includes `rmb_metalfx.m`; no cvar or environment variable reaches it), for the spatial and the
  temporal mode under `r_hdr 1`: nothing is kept, `rmb_fx_ready()` is 0, the note is
  `RM_FX_NOTE_HDR` for that mode, and nothing is encoded. Asked again: no second note, and still
  refused after the seam is cleared. A toggle to `r_hdr 0` tries again, and a refusal there gives
  `RM_FX_NOTE_UNMADE`.
- The toggle back to `r_hdr 0`: the float16 scaler is not run on any targets, the next prepare
  remakes it in `BGRA8Unorm` and perceptual, and it runs. A scaler made in 8 bits is not run after a
  toggle to `r_hdr 1`.

Result: `rmb_cs_probe: 736 checks, 0 failed` (346 before HD4), with `the temporal scaler in RGBA16Float colour and output: made` printed on each of the six runs.

`check-ref-metal.sh` (f) adds three mutants of `rmb_metalfx.m`, and the probe must fail each one:

- `fxlinear`: the spatial scaler's mode under `r_hdr 1` is linear.
- `fxtemporal8`: the temporal output stays `BGRA8Unorm` under `r_hdr 1`.
- `fxnonote`: a scaler refused in float16 tells no note.

`check-ref-metal: ... colour space 736 checks, 0 failed; 12 colour-space mutants refused` (nine before HD4).

Review round 1: `fx_case` has two halves, each printing a line when it runs or is skipped. The
spatial half (the spatial scaler under `r_hdr 0` and 1, and its refusal case) runs on any GPU with
the spatial scaler; the temporal half (the temporal scaler in both formats, its refusal case) only
when the GPU has the temporal one too. The build VM's paravirtual GPU has the spatial scaler alone
(TASK111), and before the split the case skipped there as a whole, so the three mutants passed the
probe and the check failed a correct tree. The check now runs `fxlinear` and `fxnonote` only when
the spatial half ran (the spatial refusal case catches `fxnonote`), and `fxtemporal8` only when the
probe printed `the temporal scaler in RGBA16Float colour and output: made`, and counts the skipped
ones in its line. Proved on this Mac with a scratch copy of the probe whose temporal-support test
was forced to 0 (a `sed`, not committed), and a copy of the check pointed at it: the probe gave
`640 checks, 0 failed` with `the MetalFX temporal half skipped: no temporal scaler on this GPU` on
each run, and the check `colour space 640 checks, 0 failed; 11 colour-space mutants refused, 1
skipped (no temporal scaler)`, so `fxlinear` and `fxnonote` were still refused. The normal run is
in the checks below.

`rm_fx_test.c` pins the note's new line; `rm_host_test.c` pins `rm_hdr_fx_note`.

### On this GPU (apple9, this Mac, screen locked)

`rmb_cs_probe --measure`, by hand: it builds the probe as `check-ref-metal.sh` does and runs only
the measurements.

- **Acceptance.** `MTLFXTemporalScalerDescriptor` with `RGBA16Float` colour and output: `made`
  (`newTemporalScalerWithDevice:` returns a scaler, its `colorTextureFormat` and
  `outputTextureFormat` read back `RGBA16Float`, and it encodes and completes). The spatial scaler in
  `RGBA16Float` with the hdr mode: made, runs. `MTLFXTemporalDenoisedScalerDescriptor` (960x540 to
  1920x1080, depth `Depth32Float`, motion `RG16Float`, albedos `RGBA8Unorm`, normal `RGBA16Float`,
  roughness `R16Float`): `made` with `BGRA8Unorm` colour and output and `made` with `RGBA16Float`,
  with no exception. The denoiser was made only; it was not encoded.
- **Memory**, 960x540 to 1920x1080 (`r_metalfx_scale 0.5`), as the device's
  `currentAllocatedSize` before and after the scaler and `fx_out` are made:

  | scaler | `BGRA8Unorm` | `RGBA16Float` | extra | of which `fx_out` |
  | --- | --- | --- | --- | --- |
  | spatial | 17.0 MB | 33.7 MB | 16.7 MB | 8.3 MB |
  | temporal | 98.8 MB | 107.2 MB | 8.4 MB | 8.3 MB |

  The spatial scaler's own allocation doubles with the format (8.7 to 17.1 MB). The temporal
  scaler's history stays the same, about 90.5 MB in both formats, so its extra memory is the output
  target alone. The denoiser's figure is not reliable: the first one made added 12.8 MB, and the
  second 0.0 MB (its allocation is deferred or reused), so no denoiser memory figure is given.
- **Stability** (plan 2.6). This is a still view drawn by the probe, not a game frame, so no image
  is viewed. The view is a smooth gradient, a checker of hard edges, and a grid of 576 small cores
  at 7 to 8 times white, each about 3 output pixels across, which stand in for additive cores. It is
  sampled at each frame's jitter (`rm_fx_jitter`) at 960x540, with depth 0.5 and no motion, then
  upscaled to 1920x1080 by the renderer's own `rmb_fx_prepare` and `rmb_fx_upscale`, for 60 frames.
  The view is clamped at white and quantized to 8 bits under `r_hdr 0`, and kept in float16 under
  `r_hdr 1`. The metric is the change of each output channel from one frame to the next, read in
  display units (clamped to [0, 1], as `r_hdr_peak 1` shows them), over frames 10 to 59 (50 frame
  pairs, 103,680,000 pixel samples):

  | run | mean | max | samples changing by more than 0.1 |
  | --- | --- | --- | --- |
  | `r_hdr 0`, jitter as sampled (+1) | 0.000556 | 0.3961 | 15,154 |
  | `r_hdr 0`, jitter negated (-1) | 0.000254 | 0.0863 | 0 |
  | `r_hdr 1`, jitter negated (-1) | 0.000441 | 0.5930 | 35,154 (0.034 %) |

  The negated jitter is the scaler's convention here: it is the steadier one at `r_hdr 0`, and
  `r_hdr 1` was run with it. Under `r_hdr 1` the mean change is 1.7 times that of `r_hdr 0`, and
  about 700 samples a frame change by more than 0.1. That is about 1.2 for each core a frame, so
  the change is inferred to sit on the cores' rims, which the scaler's history blends against values
  far above white. Not measured: where in the frame those samples are.

  Reading: deferred. In this synthetic still view the temporal scaler is unstable at the cores'
  rims under `r_hdr 1`: a max change of 0.593 against 0.0863, and about 700 samples a frame above
  0.1 against none at `r_hdr 0` with the same jitter. The flat and edged parts are as steady as in
  8 bits. That is the symptom plan 2.6 names. How large the flicker is on a display with a peak
  above 1 is unknown, since the clamp at white hides part of the change. So the plan's fallback
  (the tone map before the scaler) is neither applied nor ruled out here: the decision moves to
  HD6, with the display on, on game frames, starting from these figures (plan 2.6 and the HD6 row
  of its slice table).

### Pins

Branch `1a918da2`, Metal, screen locked, `PINS_SEED=1` with the trace, 640 x 480, with the game module
built, MetalFX at its default (off) in the `r_hdr 0` scenarios, and hashed as in HD3's table
(`shasum` of the frames, then of the list of hashes):

| scenario | frames | branch | against FIX133's table |
| --- | --- | --- | --- |
| `world` | 23 | `b15b40cf1604` | identical |
| `models` | 23 | `552ee3cb16b8` | identical |
| `hdr` | 23 | `11b7dcc7968c` | identical |
| `hdr-metalfx2` (new) | 23 | `966095662f89` | its own reference |

Review round 1, head `022c1d74` (the same `src/` as `1a918da2`; the round changed the probe, the
check and the docs), the same settings: the `r_hdr 0` scenarios that run the changed scaler code
with MetalFX on, against HD3's table, and `hdr-metalfx2` again, into a fresh scratch store:

| scenario | frames | branch | against HD3's table |
| --- | --- | --- | --- |
| `world-metalfx1` | 23 | `8bb165628bea` | identical |
| `world-metalfx2` | 23 | `15df2f0d4ca4` | identical |
| `world-metalfx3` | 23 | `15df2f0d4ca4` | identical |
| `world-metalfx-interp` | 23 | `b15b40cf1604` | identical |
| `hdr-metalfx2` (second run) | 23 | `966095662f89` | identical to the first run |

`hdr-metalfx2` is `hdr` with `r_metalfx 2` at `r_metalfx_scale 0.5`: the temporal scaler on the
float16 view, before the tone map. Its engine log has no MetalFX note, and its teardown counts
`metalfx 2/2` (the scaler and `fx_out`), so the scaler ran under `r_hdr 1` for the whole demo.

### Checks

Run on `1a918da2` under the lock:

- `make test`: `yq2test: 435 tests, 0 failed`
- `tests/check-ref-metal.sh`: `check-ref-metal: ok (13194 checks, 0 failed; ...; MetalFX 335 checks, 0 failed; ...; colour space 736 checks, 0 failed; 12 colour-space mutants refused; ...; ref_metal built)`
- `tests/check-ios.sh`: `check-ios: ok (54 mutants refused; ...)`
- `tests/check-tvos.sh`: `check-tvos: ok (180 objects for the tvOS simulator, platform 8, the iOS simulator's list)`
- `tests/check-visionos.sh`: `check-visionos: ok (180 objects for the visionOS simulator, platform 12, the iOS simulator's list)`
- `tests/check-cvar-docs.sh`: `check-cvar-docs: ok`
- `tests/check-doc-links.sh`: `check-doc-links: ok`
- `tests/check-metalfx-report.sh`: `check-metalfx-report: ok (118 mutants killed)`

Review round 1, on `22f155d6` under the lock: each of these checks gave the same last line as
above (`check-ref-metal` with `colour space 736 checks, 0 failed; 12 colour-space mutants refused`,
no mutant skipped on this GPU).

`scripts/test.sh` was not run here; the orchestrator runs the gate.

### Left

- The denoiser's own float16 path is built with it, in TASK59: its output takes `rmb_fx_color()`,
  and it uses the same note when refused.
- The MetalFX fallback of plan 2.6 (the tone map before the scaler): decided by HD6 with the
  display on and a peak above 1, on game frames, from the stability figures above.
