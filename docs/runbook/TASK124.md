# Upscaler backends for `upscale_textures`, slice 1: the backend argument and MetalFX spatial (TASK124)

Card: TASK124 (#280). Plan anchor: [`docs/plans/high-resolution.md`](../plans/high-resolution.md),
section 4, P6 (the second of its two cards), and section 6. The first card is TASK123 (#312):
`upscale_textures <2|4> [force]` with scale2x (`src/client/cl_upscale.c`,
`src/client/cl_upscale_cmd.c`). The method sections this runbook leans on are in the shared
[harness runbook](../plans/metal-port-harness.md) ("How a run works", "Hardware tiers").

This slice adds `upscale_textures <2|4> [scale2x|metalfx] [force]`: MetalFX's spatial scaler as a
backend over the same padded tile, its support check and its fallback to scale2x, and a menu page
for the command with the backend choice. The VideoToolbox backend and the comparison report are
slice 2, TASK167.

## Design question 1: where the MetalFX code lives

**Settled by the owner: [decision 0012](../decisions/0012-metalfx-in-the-client.md)** (INFORM13,
#438, option A, 2026-11-17), which extends [decision 0011](../decisions/0011-metalfx-framework.md)
to the macOS client executable.

- One new file, `src/client/cl_upscale_metalfx.m`, makes its own Metal device
  (`MTLCreateSystemDefaultDevice()`), command queue and `MTLFXSpatialScaler`. They are made by
  `CL_UpscaleMetalFXBegin` at the start of a `metalfx` run and let go by `CL_UpscaleMetalFXEnd` at
  its end; no Metal or MetalFX object is made at any other time.
- The `quake2` link rule on Darwin gains `-weak_framework MetalFX` (`UPSCALE_METALFX_LDLIBS`, not
  in the iOS mode). Metal was linked already, through the vendored SDL3 (`SDL3_LDLIBS`). The file is
  built with ARC by a new `$(BUILDDIR)/client/%.o : %.m` rule. Elsewhere (iOS, other platforms)
  `cl_upscale_cmd.c` has the same four functions, which say "not built for this platform".
- The renderer API (`API_VERSION 8`), `ref.h` and every renderer are untouched; the backend works
  with any renderer loaded.

Why here: it is the smallest change, it does not tie the command to one renderer, and it is
reversed by reverting the slice (the record's "Reversal"). The places that were ruled out before
the decision, and why, are in the record's context and in this runbook's history (commit
`3ebe041d`).

## Design question 2: determinism, the cache key and `force`

**MetalFX's spatial output repeats byte for byte on this host.** Measured twice:

- The scratch probe before the decision (commit `2dbb2a03`, kept with its output in the artifacts
  directory, not in the repository): a synthetic 66 x 66 RGBA8 tile (64 x 64 plus one texel of
  wrap-padding, hard-edged blocks like a palette tile, alpha 0 in about one texel in seven; no game
  data), scaled to 132 x 132 and 264 x 264 (4x in one pass), four times per factor in a process
  (twice with a new scaler, twice reusing one), in four processes: the same bytes every time, FNV-1a
  `9ca20c9fff012b71` (2x) and `02b99c758d15c5ab` (4x), alpha 255 everywhere.
- The host test of the shipped file (`tests/metal/upscale_metalfx_test.m`, run by
  `tests/check-upscale-metalfx.sh`), the same tile through `CL_UpscaleMetalFXScale`, twice per
  factor with another size between: the same bytes and the same two hashes, alpha 255 everywhere.

Host: Apple M6, macOS 27.2.1, SDK 27.2. Whether it repeats across devices or OS versions is not
measured (an M1-family Mac is the card's T1 host, slice 2).

The first card keeps no cache: the output path is the only key. A file that is there is skipped
unless `force` is given (`CL_UpscaleAction`), and the PNG does not record which backend made it.
That stays so with a backend argument: changing the backend without `force` skips every file
already written by the other backend, and the summary line says how many were skipped; `force`
writes them all again with the backend asked for. The summary line names the backend when it is
`metalfx` ("4x with metalfx"); a scale2x run's line is the first card's, word for word.

## How the metalfx path treats a tile

`CL_UpscaleStepMake` loads the indices as for scale2x (the name, size and path checks are the
first card's), then for `metalfx` calls `CL_UpscaleRGBA` (`cl_upscale.c`, unit-tested with a
stand-in scaler) with `CL_UpscaleMetalFXScale` as the scaler:

1. a skin is flood-filled first (`CL_UpscaleFloodFillSkin`), as for scale2x;
2. the indices are converted with the 8-bit table (`CL_UpscaleToRGBA`: index 255 gets alpha 0 and a
   neighbour's colour, so no black fringe is scaled in);
3. a world texture is wrap-padded by one texel (`CL_UpscaleWrapPadRGBA`), scaled in one pass to
   `factor` times the padded size, and cropped by `factor` texels on each side
   (`CL_UpscaleCropRGBA`); a sky face or a skin is scaled as it is;
4. the alpha of the output is set from the source's index 255 by nearest neighbour
   (`CL_UpscaleMaskAlpha`), because MetalFX returns alpha 255 everywhere (both measurements
   above); a texel is opaque or transparent, as the renderers expect of an 8-bit texture;
5. the PNG is encoded and written by the first card's steps (`CL_UpscaleWriteOut`: the link, git
   work-tree and temporary-file checks), as for every backend.

The scaler is `RGBA8Unorm` in and out, `MTLFXSpatialScalerColorProcessingModePerceptual` (the
palette's values are sRGB-encoded, as a frame's are), the output texture private and blitted to a
shared buffer; the command waits for each tile. One scaler is kept for consecutive tiles of the
same size.

**Unsupported:** not macOS ("not built for this platform"), MetalFX's class missing ("this macOS
has no MetalFX spatial scaler"), no Metal device ("no Metal device"), `supportsDevice:` NO ("the
Metal device has no MetalFX spatial scaler"), or no command queue. The run prints one line,
`upscale_textures: metalfx is unsupported here (<reason>); using scale2x`, and goes on with
scale2x (`CL_UpscaleChooseBackend`).

**The menu:** Options, "upscale textures": the factor (2x, 4x), "write again" (`force`), and one
item per backend, `scale2x` and `MetalFX spatial`, which run the command in the console. `MetalFX
spatial` is greyed (`QMF_GRAYED`) with "greyed: <reason>" in the status line where it is not
available; it is never hidden. Opening the page makes no Metal or MetalFX object (decision 0012):
it asks `CL_UpscaleMetalFXAvailable`, which checks that the file is built and the class is there,
and gives the reason a run of this process found, if one found the backend unsupported.

## Tests

Red first (commit `23f7754b`, stubs in place): `make test TEST_ARGS=upscale`,
`yq2test: 54 tests, 9 failed`, the nine new ones that check something a stub cannot do
(`upscale_backend_name`, `upscale_parse_args_accepts`, `upscale_choose_backend`,
`upscale_rgba_pad_and_crop`, `upscale_mask_alpha`, `upscale_rgba_wrapped_tile`,
`upscale_rgba_skin_not_padded`, `upscale_rgba_refuses`, `upscale_summary_names_metalfx`); then
green with the implementation, `54 tests, 0 failed`.

- **Unit (`tests/unit/test_cl_upscale.c`):** the argument parsing (the first card's four lines
  unchanged, every backend and `force` combination, a bad backend name such as `vt` or `MetalFX`,
  a wrong order or an extra word refused with nothing set, and the usage line); the fallback and
  its line (with a reason, an empty one, none, and a value that is no backend); RGBA pad and crop;
  the alpha mask; the whole metalfx path over a stand-in scaler that, like MetalFX, sets alpha 255
  everywhere, for a wrapped world tile and an unpadded skin at 2x and 4x (the scaler's input is the
  padded RGBA, one pass at the factor, the output cropped back, alpha 0 exactly at the source's
  index 255); refusals (factor, size, no scaler, a failing scaler); the summary naming metalfx.
- **Host (`tests/metal/upscale_metalfx_test.m`, `tests/check-upscale-metalfx.sh`, macOS only,
  with ASAN and UBSAN):** nothing scaled outside a run; the test-only switch
  `UPSCALE_METALFX_TEST` forces unsupported, the run is refused with its reason and the menu's check
  then gives that reason (a long reason is cut, not overrun); a real run clears it; the tile above
  at 2x and 4x, the same bytes twice with another size between, alpha 255 everywhere; sizes it must
  refuse are refused; nothing scaled after the run. `29 passed, 0 failed`.

## Evidence on this host

Apple M6, macOS 27.2.1, the licensed game data read only (`baseq2`, three paks). Each run used a
copy of the binaries in a scratch directory outside every git work tree, `-portable` and a fresh
`game` directory beside the copy, so `FS_Gamedir()` was outside the repository; the generated
PNGs stay there, were not committed, uploaded or viewed. Every engine run held the host lock
(`heavy.sh`), with no other `quake2` running.

**ASAN** (`make DEBUG=1 ASAN=1 client ref_soft`, `vid_renderer soft`, one run each):

| Run | Summary line | ASAN | Wall time of the process |
|---|---|---|---|
| `upscale_textures 2 metalfx` | `2432 written, 0 skipped, 2 refused, 0 failed, 163043481 bytes, 2x with metalfx` | no report | 6.7 s |
| `upscale_textures 4 metalfx` | `2432 written, 0 skipped, 2 refused, 0 failed, 547496148 bytes, 4x with metalfx` | no report | 13.0 s |

Both lines end "2 directories with a refused name not looked at", as a scale2x run over the same
data does (below); 2 refused is the first card's count over the stock data
([`doc/030_configuration.md`](../../doc/030_configuration.md) names the two skins). Load
(`sysctl -n vm.loadavg`, one-minute figure) 2.9 before and 3.9 after the 2x run, 3.9 before and
3.8 after the 4x run.

**Every file of the first card's run is written by metalfx.** Release build, same data: scale2x
and metalfx each wrote 2432 files with 2 refused and 0 failed at 2x and at 4x, and the names
written at 4x are the same list for both backends (2432 each, no difference). scale2x's 2x run
wrote 50227150 bytes, the first card's figure to the byte; metalfx's PNGs are larger (163043481
bytes at 2x, 547496148 at 4x, against scale2x's 99604894 at 4x), as filtered images compress less.
From the second run in a game directory on, both backends' lines say "3 directories with a refused
name" instead of 2: the first card's walk, not this slice (the same for both backends).

**Time per backend** (release build, `vid_renderer soft`, a window, display sync off), by the
owner's timing protocol: one host-lock hold per factor (batches `b2-2x` and `b3-4x`; a first
batch `b1-2x` is void, its pre-warm stopped at once on a script fault), each with a 4-minute
pre-warm of the case's own run back to back (`metalfx force`: 49 runs in 244 s at 2x, 22 in
247 s at 4x, no thermal or performance warning recorded), then the backends alternated, three
runs each: start-up only (`+quit`), scale2x, metalfx, with `force`, so every run writes all 2432
files. The time is the process's wall time; the command's own is that less the start-up run.

| Factor | Start-up only | scale2x, process | scale2x, command | metalfx, process | metalfx, command |
|---|---|---|---|---|---|
| 2x | 0.783 to 0.785 s | 2.36 to 2.81 s (median 2.74) | 1.58 to 2.02 s (median 1.95) | 4.51 to 4.90 s (median 4.90) | 3.73 to 4.12 s (median 4.12) |
| 4x | 0.780 to 0.782 s | 4.32 to 4.92 s (median 4.73) | 3.54 to 4.14 s (median 3.95) | 10.89 to 11.16 s (median 11.14) | 10.11 to 10.38 s (median 10.36) |

Frequencies (the no-root IOReport probe of TASK166, `freq delta` around each run; median of the
three runs, weighted MHz while active and the share of active time at the top state):

| Factor, backend | P cluster | E cluster | GPU |
|---|---|---|---|
| 2x scale2x | 4731 MHz, 96.1 % | 1341 MHz, 3.1 % | 502 MHz, 0 % |
| 2x metalfx | 4713 MHz, 95.1 % | 1292 MHz, 2.6 % | 546 MHz, 0 % |
| 4x scale2x | 4725 MHz, 96.1 % | 1183 MHz, 1.3 % | 502 MHz, 0 % |
| 4x metalfx | 4753 MHz, 97.9 % | 1110 MHz, 0.6 % | 524 MHz, 0 % |

A run is CPU-bound on one P core either way: the GPU stays near its lowest state, and metalfx's
extra time is mostly the encoding of its larger PNGs and the wait for each tile, not the scaling
(under a millisecond a tile, the probe above). GPU utilization before each run was 0 to 5 %
(read 1 s after the previous one ended); no `quake2` of anyone else ran. Load (`vm.loadavg`, one
minute): 2.1 to 6.7 over `b2-2x`, 3.2 to 6.4 over `b3-4x`, mostly this batch's own pre-warm. The
screen was unlocked, checked before and after each batch. Memory free 83 % or more, no swap, low
power mode off. The snapshots, logs and per-run numbers are in the artifacts directory, not in
the repository.

PINS

## Left for slice 2 (TASK167)

The VideoToolbox backend (its own framework approval, section 6 of the plan; no `vt` argument and
no VideoToolbox call here) and the comparison-report generator, with the runs on an M1-family Mac
(T1), where MetalFX's determinism across devices is also to be measured.
