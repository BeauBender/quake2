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

WATER_ROWS

### The `hdr` record

`scripts/pins/scenarios/hdr.cfg` (`world` with `r_hdr 1`), branch `791ba7d7`, `PINS_SEED=1`, 640 x
480: 23 frames, hash `11b7dcc7968c`, twice in scratch stores and once into the pin store
(`hdr/metal/791ba7d7c093-3613970603`). Against the branch's `world`: 0 of 23 frames equal; over the
23 frames 5.13% of the bytes differ, every one by 1 and none by more, which is the one 8-bit step of
the plan's section 1 (float16 rounds a blend once where 8 bits round each pass). That is why it is its
own reference.

### The live switch

LIVE_SECTION

### Mutants

MUTANT_SECTION

### Left

- HD1: the headroom readings (`metal_hdr_info` prints `unavailable`), with the display on.
- The layer's state on a display that is on and an EDR pixel read (`metal_hdr_probe`, HD3).
- HD3 to HD7 as the plan's section 7 has them; the tone map, `r_hdr_peak` and the report line are HD3.
- The ray-tracing passes draw into `t_rcolor` through their float16 variants on this path; their
  terms stay clipped in `t_light` until HD6.
