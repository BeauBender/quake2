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
no game data): see [the probe's results](#the-probe) below.

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

Results to follow.

## Left for slice 2

The VideoToolbox backend (with its own framework approval, section 6) and the comparison-report
generator, and everything of slice 1 once the owner has chosen the place.
