# Upscaler backends for `upscale_textures`, slice 1: the backend argument and MetalFX spatial (TASK124)

Card: TASK124 (#280). Plan anchor: [`docs/plans/high-resolution.md`](../plans/high-resolution.md),
section 4, P6 (the second of its two cards), and section 6. The first card is TASK123 (#312):
`upscale_textures <2|4> [force]` with scale2x (`src/client/cl_upscale.c`,
`src/client/cl_upscale_cmd.c`).

This slice was to add `upscale_textures <2|4> [scale2x|metalfx] [force]`, the MetalFX spatial
scaler as a backend over the same padded tile, its support check and its fallback to scale2x, and
a menu item for the backend. It stops at this design note: the one place the brief names for the
MetalFX code is ruled out by an owner decision, and so is every other place found (below). No code
was changed.

## Design question 1: where the MetalFX code lives

[Decision 0011](../decisions/0011-metalfx-framework.md), item 2: "`MetalFX.framework` is linked
into `ref_metal` only, on Apple platforms. The client, the server, the game module and the other
renderers [...] do not link it." [`docs/DEPENDENCIES.md`](../DEPENDENCIES.md) repeats it ("for
`ref_metal` only"), and section 6 of the high-resolution plan says each system framework other than
ImageIO needs the owner's approval when its card is filed. Neither plan names a place for this
backend: P6 lists MetalFX's spatial scaler as a backend of the command and stops there, and
[`docs/plans/metal-renderer.md`](../plans/metal-renderer.md) says nothing about the client using
Metal or MetalFX.

| Place | What rules it out |
|---|---|
| A small Objective-C file in the client that makes its own `MTLDevice` and weak-links `MetalFX` on macOS (the brief's first choice) | Decision 0011, item 2: the client does not link MetalFX. |
| `ref_metal` does the scaling; the client reaches it through a second exported symbol beside `GetRefAPI`, so `refexport_t` and `API_VERSION 8` stay as they are | Inside decision 0011, but the card's not-to-touch list and this slice's brief forbid touching any renderer. It would also work only where `ref_metal` is built and found, not with `vid_renderer gl1` or `soft` loaded. |
| The client opens `MetalFX.framework` with `dlopen` at run time, so nothing is linked at build time | The client then depends on MetalFX all the same, which is what item 2 rules out; it would read as a way around the decision. Not taken. |
| A separate helper executable that links MetalFX | A new binary outside the decision's scope (`ref_metal` only), and not a place either plan names. |
| The static-modules build (`STATIC_MODULES=1`, the iOS app), where the executable already links MetalFX because `ref_metal` is inside it | The macOS client is not built that way, and code in the client that calls MetalFX is still a client use. |

So the choice is the owner's. The options, smallest first:

- **A. Extend decision 0011 to the client executable on macOS** (a new record that names 0011):
  weak-linked, used only by `upscale_textures <2|4> metalfx`, no MetalFX or Metal object made
  otherwise. The slice then goes as the brief's first choice: one new file
  `src/client/cl_upscale_metalfx.m` with its own `MTLCreateSystemDefaultDevice()`, and the
  `quake2` link rule on Darwin gains `-weak_framework MetalFX` (Metal is linked already, through
  the vendored SDL3). The client keeps the backend with any renderer loaded. About 400 lines with
  the tests.
- **B. Keep decision 0011 as it is and run the scaler in `ref_metal`**: the card's not-to-touch
  list widened to `src/client/refresh/metal/`, one exported function beside `GetRefAPI` (no change
  to `refexport_t` or `API_VERSION`), found by the client in the loaded renderer. The backend is
  then unsupported, with its report line, whenever another renderer is loaded.
- **C. Drop the MetalFX backend from TASK124** and keep scale2x and, in slice 2, VideoToolbox.
  Note that section 6 asks the same approval of VideoToolbox when its card is filed, so slice 2
  meets the same question for `VideoToolbox.framework`.

Recommended: A. It is the smallest change, it does not tie the command to one renderer, and it is
reversed by reverting the slice and the new record.

## Design question 2: determinism, the cache key and `force`

Measured on this host with a scratch probe outside the repository (a synthetic 66 x 66 RGBA8 tile,
64 x 64 plus one texel of wrap-padding, made of hard-edged blocks of colour like a palette tile;
no game data): the output is the same byte for byte from run to run and from process to process,
at 2x and at 4x. The details are under [the probe](#the-probe) below.

The first card keeps no cache: the output path is the only key. A file that is there is skipped
unless `force` is given (`CL_UpscaleAction`), and the PNG does not record which backend made it.
With a backend argument that stays so: changing the backend without `force` skips every file
already written by another backend, and the summary line says how many were skipped; `force`
writes them all again with the backend asked for. The summary line names the backend, so a run's
output can be told apart in the console.

## How the metalfx path would treat a tile (for when the place is chosen)

The same steps as scale2x up to the colour conversion, then:

1. the indices are flood-filled for a skin (`CL_UpscaleFloodFillSkin`) and converted to RGBA with
   the 8-bit table (`CL_UpscaleToRGBA`, which gives index 255 alpha 0 and a neighbour's colour);
2. a world texture is wrap-padded by one texel, as scale2x's input is (an RGBA form of
   `CL_UpscaleWrapPad`), and scaled to `factor` times the padded size; the result is cropped by
   `factor` texels on each side (an RGBA form of `CL_UpscaleCrop`);
3. the alpha of the output is not taken from MetalFX: it is the transparent-index mask of the
   source (index 255) scaled by nearest neighbour, so a texel is either opaque or transparent as
   the renderers expect of an 8-bit texture;
4. the name, path, git work-tree and link checks are the first card's (`CL_UpscaleWriteOut`), as
   they are for every backend.

Unsupported (no Metal device, `MTLFXSpatialScalerDescriptor` missing or `supportsDevice:` NO, or
the scaler not made): the run says so in one line and goes on with scale2x. The menu shows the
backend choice with metalfx greyed (`QMF_GRAYED`) and a one-line reason where it is unsupported.

## The probe

A scratch Objective-C program, kept with its output in `<artifacts>/TASK124/` and not in the
repository. It makes the 66 x 66 tile above, scales it with `MTLFXSpatialScaler`
(`RGBA8Unorm` in and out, `MTLFXSpatialScalerColorProcessingModePerceptual`, the output texture
private and blitted to a shared buffer) to 132 x 132 (2x) and 264 x 264 (4x, in one pass), four
times per factor in one process: twice with a new scaler each time and twice reusing one. It prints
whether the outputs match byte for byte and an FNV-1a hash of the output. It ran four times, as two
batches of two processes, each under the host lock (`heavy.sh`), with no `quake2` running.

Host: Apple M6, macOS 27.2.1, SDK 27.2. `MTLFXSpatialScalerDescriptor` is present and
`supportsDevice:` says YES.

| | 2x | 4x |
|---|---|---|
| Outputs within a process (new scaler, new scaler, reused, reused) | the same, all four, in every process | the same, all four, in every process |
| FNV-1a of the output, in every process | `9ca20c9fff012b71` | `02b99c758d15c5ab` |
| Output alpha, with alpha 0 in about one input texel in seven | 255 everywhere | 255 everywhere |
| Time per tile, first call in a process (makes the pipeline) | 4.9 to 9.9 ms | 0.6 to 1.1 ms |
| Time per tile after that (make the textures, encode, wait, read back) | 0.26 to 0.88 ms | 0.29 to 0.89 ms |

Load (`sysctl -n vm.loadavg`, one-minute figure) was 3.7 before and 1.6 after the first batch and
1.7 before and after the second. The screen was locked: the times are a rough guide, not timing
evidence, and this is not a timing card.

What it means for the design:

- **MetalFX's spatial output repeats byte for byte** on this host, between scalers, within a
  process and between processes. A metalfx run over the same data writes the same files, so a run
  with `force` changes nothing unless the backend or the data changed. Whether it repeats across
  devices or OS versions is not measured (an M1-family Mac is the card's T1 host).
- **MetalFX drops the alpha channel** (the output is opaque where the input was transparent), so
  step 3 above (the alpha from the source's index 255, by nearest neighbour) is required, not a
  choice.
- **4x is one pass**: the scaler takes a 4x ratio directly, so the metalfx path need not run 2x
  twice as scale2x does.
- After the first call the scaler takes under a millisecond per 64 x 64 tile at either factor here.
  The time of a whole run over the game data (with the PNG encoding and the writes) is not measured:
  that needs the backend in the client, which waits on the place.

## Left for slice 2

The VideoToolbox backend (with its own framework approval, section 6) and the comparison-report
generator, and everything of slice 1 once the owner has chosen the place.
