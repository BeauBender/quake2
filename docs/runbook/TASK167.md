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

Pending: the M6 runs (ASAN at 2x and 4x, the timed report at 4x) have not run yet; see the lane report.
