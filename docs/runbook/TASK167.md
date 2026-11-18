# Upscaler backends for `upscale_textures`, slice 2: VideoToolbox super-resolution and the local report (TASK167)

Card: TASK167 (#437). Plan anchor: [`docs/plans/high-resolution.md`](../plans/high-resolution.md),
section 4, P6, and section 6. Slice 1 is [TASK124](TASK124.md) (the backend argument, scale2x and
MetalFX spatial). Owner's approval: the card's comment of 2026-11-17, recorded as
[decision 0013](../decisions/0013-videotoolbox-in-the-client.md).

## What the slice adds

- `upscale_textures <2|4> vt [force]`: VideoToolbox's super-resolution scaler
  (`VTSuperResolutionScalerConfiguration`, image input type, in a `VTFrameProcessor` session), in
  `src/client/cl_upscale_vt.m`, built into the macOS client only, with `VideoToolbox` weak-linked
  and every call behind `@available(macOS 26.0, *)`. iOS and other platforms get the stubs in
  `cl_upscale_cmd.c` ("not built for this platform").
- The same tile path as metalfx, shared, not copied: `CL_UpscaleRGBA` (the RGBA conversion, the
  wrap border of `UPSCALE_METALFX_BORDER` texels, one pass at the factor, the crop, the alpha from
  the transparent index), and the first card's name, path, link and git work-tree checks
  (`CL_UpscaleWriteOut`). `CL_UpscaleBackendRGBA` names the backends that take that path.
- **Support checks**, judged by `CL_UpscaleVTState` (`cl_upscale.c`, unit-tested) from the facts
  `CL_UpscaleVTFacts` reads, in this order: built here; the OS has the class (macOS 26); its
  `isSupported`; `supportedScaleFactors` has the run's factor; a configuration at the factor is
  made; the model's status. Unsupported: one line, `upscale_textures: vt is unsupported here
  (<reason>); using scale2x`, and the run goes on with scale2x. A missing model: the run prints
  `UPSCALE_SAY_VT_FETCH` ("the operating system will now download VideoToolbox's
  super-resolution model (once, from Apple)") before asking for it, waits at most 900 s, and says
  how long it took. Nothing else fetches the model: not the menu, not the checks, not the tests.
- **The menu:** Options, "upscale textures" gains "VideoToolbox ML", greyed with "greyed:
  <reason>" where it is unsupported (`CL_UpscaleVTAvailable`: the class and `isSupported`, and the
  reason a run of this process found), never hidden. Opening the page makes no VideoToolbox
  object.
- **The report generator:** `scripts/upscale-report.sh` (`scripts/upscale-report/report.py`). It
  copies the engine into its output directory, runs `upscale_textures <f> <backend> force` for
  each backend, alternated, `--runs` times, with a start-up-only run in each round, and writes one
  self-contained HTML file: the time per backend as a range (process, and command = process less
  the median start-up), the lines each backend's first run printed, and matched crops (the centre
  96 x 96 output texels of the same names from every backend, at 2x zoom). It refuses an output
  inside a git work tree or with `..`. The report holds images of the player's data: local only,
  never committed, uploaded or attached.

## Tests

Red first (commit `e1eb8503`, stubs in place): `make test TEST_ARGS=upscale` failed in
`upscale_backend_name`, `upscale_parse_args_accepts`, `upscale_summary_names_metalfx`,
`upscale_vt_state` and `upscale_vt_fallback_line`, each on the missing behaviour; green with the
implementation: `yq2test: 58 tests, 0 failed`.

- **Unit (`tests/unit/test_cl_upscale.c`):** `vt` parsed, refused in a wrong order or spelling;
  the usage line; `CL_UpscaleVTState` for each failing check (the first one gives the reason), a
  missing or downloading model (fetch), an unknown status, a factor that is not 2 or 4, no facts,
  a short buffer; **the forced fallback**: the facts with `isSupported` NO, through
  `CL_UpscaleChooseBackend`, give `upscale_textures: vt is unsupported here (VideoToolbox's
  super-resolution scaler is not supported on this device); using scale2x`; the summary names vt.
- **Host (`tests/metal/upscale_vt_test.m`, `tests/check-upscale-vt.sh`, macOS, ASAN and UBSAN):**
  nothing scaled outside a run; the test switch `UPSCALE_VT_TEST` makes `isSupported` read NO and
  the menu's check then gives the reason; with the model on the machine (never fetched by the
  test), a 24 x 24 and a 72 x 72 tile at each factor VideoToolbox has, the same bytes twice with
  another size between, alpha 255; a wrong output size refused; nothing scaled after the run.
  `UPSCALE_VT_REQUIRE=1` makes a skipped case fail.
- **Generator (`tests/check-upscale-report.sh`, offline):** a fake engine, no data, no network.

## Evidence on this host

M6, macOS 27.2.1, 2026-11-17. Output is in `~/Developer/no-git/quake2-artifacts/TASK167/` (local only).

- **Support checks:** OS has the class 1; `isSupported` 1; `supportedScaleFactors` has 4x only;
  a configuration at 4x is made; model status 2 (ready) when the runs below started. At the first
  run the status was 0 (download required, measured by the parent); the model was on the machine
  by the time of the logged runs, so no run of this lane logged the fetch line, its size or its
  time. The runs that would show it are the first `vt` run on a host without the model.
- **2x:** `upscale_textures 2 vt` falls back, as decided: "vt is unsupported here (VideoToolbox's
  super-resolution scaler has no 2x); using scale2x", 2432 written, 0 failed. A 4x-then-halve path
  is a possible later option; it is not built.
- **What the first real run showed, and the fix:** at 4x every tile failed (2432 failed, 0 written),
  because the scaler lists one pixel format only, `64RGBAHalf` ('RGhA'), and the code took only
  32BGRA or 32RGBA. The session and the buffers now take half-float RGBA when neither 8-bit
  format is listed (the bytes are converted to and from half floats in the copies). Nothing else
  needed a change: the smallest tile (24 x 24) through 288 x 195 were accepted, and the 4-texel
  border was enough.
- **ASAN (debug build, 4x, one run):** 2432 written, 0 skipped, 2 refused names, 0 failed, 184337862
  bytes, 129 s; no AddressSanitizer or runtime-error lines. At 2x (the fallback to scale2x):
  2432 written, 0 failed, 3 s.
- **Timed report (release, 4x, 3 runs per backend in one hold, alternated; load average 2.85 at
  the start, 2.09 at the end; start-up alone 0.15 to 0.39 s, median 0.16):**

  | backend | time range | median | written | bytes |
  | --- | --- | --- | --- | --- |
  | scale2x | 3.88 to 4.22 s | 4.21 s | 2432 | 99604894 |
  | metalfx | 10.31 to 10.70 s | 10.69 s | 2432 | 547438118 |
  | vt | 88.86 to 91.11 s | 89.01 s | 2432 | 184337862 |

  The three outputs have the same names. The report is
  `~/Developer/no-git/quake2-artifacts/TASK167/report-4x/upscale-report-4x.html` (8.1 MB, local
  only: it holds images of the player's data).
- **Host test:** `tests/check-upscale-vt.sh` with `UPSCALE_VT_REQUIRE=1`: 13 passed, 0 failed (the
  2x case reports "skipped: VideoToolbox has no 2x here").
