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
  (<reason>); using scale2x`, and the run goes on with scale2x. A missing model (or one still
  downloading): the run refuses in one line, `upscale_textures: vt: VideoToolbox's
  super-resolution model is not on this machine: fetch it from the menu (Options, upscale
  textures, VideoToolbox ML); nothing written`, and fetches nothing.
- **Who fetches the model (decision 0013):** only a press of the menu item, a key press that
  stuffed text (`svc_stufftext`) cannot make. The press prints `UPSCALE_SAY_VT_FETCH` ("the
  operating system will now download VideoToolbox's super-resolution model (once, from
  Apple)") and starts the download (`CL_UpscaleVTFetchStart`), which runs on its own: the main
  thread never waits, so window events and quit keep working. Its state lives on a serial
  dispatch queue, with a generation per fetch; the 900 s bound is applied when the menu polls
  (`CL_UpscaleVTFetchState`), and a completion of a fetch given up on is ignored.
  `tests/check-upscale-vt.sh`'s fetch guard fails if the bare name `CL_UpscaleVTFetchStart`
  appears under `src` outside `menu.c` and three exact lines (the header's declaration, the
  definition in `cl_upscale_vt.m`, the stub in `cl_upscale_cmd.c`), if `downloadConfigurationModel`
  is not under `src` exactly once inside that definition's body, or if grep, awk or wc fails. It
  runs itself on eight mutants first and fails unless each is refused: a function pointer, a macro
  alias, a call at column 0, a call split across lines, a second definition line, a raw download
  call in another file, the download moved out of the fetch, and an unreadable file (a grep error,
  which the round 1 guard read as "no call"). It matches names, so it does not prove what `menu.c`
  does with the name, nor anything about code outside `src`.
- **The menu:** Options, "upscale textures" gains "VideoToolbox ML", never hidden. Each frame
  it asks `CL_UpscaleVTAvailable` at the selected factor (the class, `isSupported` and the
  class's `supportedScaleFactors`; no object is made) and the fetch's state, and
  `CL_UpscaleVTItem` (`cl_upscale.c`, unit-tested) gives the item: greyed with "greyed:
  <reason>" at a factor that does not apply, greyed while the model downloads, pressable when
  the download is done, failed or timed out (the reason shown, "press to try again"). Nothing a
  run or a fetch found is remembered, so a refusal at 2x greys nothing at 4x.
- **The report generator:** `scripts/upscale-report.sh` (`scripts/upscale-report/report.py`). It
  copies the engine into its output directory, runs `upscale_textures <f> <backend> force` for
  each backend, alternated, `--runs` times, with a start-up-only run in each round, and writes one
  self-contained HTML file: the time per backend as a range (process, and command = process less
  the median start-up), the lines each backend's first run printed, and matched crops (the centre
  96 x 96 output texels of the same names from every backend, at 2x zoom). It refuses an output
  inside a git work tree or with `..`, a link it finds in the output (the engine copy, a log,
  the report), and a `--names` entry that is not a plain relative path. It never fetches vt's
  model: when the engine's vt run says the model is missing, it stops with a line that says to
  fetch it from the client's menu and writes no report. The report holds images of the
  player's data: local only, never committed, uploaded or attached.

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
  super-resolution scaler is not supported on this device); using scale2x`; the summary names vt;
  the vt item's line in each state (greyed with a reason, idle, downloading, downloaded, failed
  with and without a reason). **`tests/unit/test_upscale_half.c`:** the half-float conversion
  (`src/client/header/upscale_half.h`, shared with the backend) without the model: all 256 bytes
  come back exactly and in order, the bit patterns of 0, 51, 128 and 255, and what a decode does
  with a negative, above 1, infinity and NaN.
- **Host (`tests/metal/upscale_vt_test.m`, `tests/check-upscale-vt.sh`, macOS, ASAN and UBSAN):**
  nothing scaled outside a run; the test switch `UPSCALE_VT_TEST` makes `isSupported` read NO, and
  the menu's check then gives the reason and no download starts; with the model on the machine
  (never fetched by the test), a 24 x 24 and a 72 x 72 tile at each factor VideoToolbox has, the
  same bytes twice with another size between, alpha 255; **flat tiles keep their colour** (black,
  grey 32, 128 and 200, white, and two colours: each channel's mean out within 4 levels of the
  colour in); a wrong output size refused; nothing scaled after the run; a factor it lacks is
  greyed, and after the runs the menu's check at each factor is the class's list (nothing
  remembered); the fetch's state is idle at the end. `UPSCALE_VT_REQUIRE=1` makes a skipped case
  fail. The script also runs the fetch guard and its eight mutants (above).
- **Generator (`tests/check-upscale-report.sh`, offline):** a fake engine, no data, no network;
  also a link planted in an existing output (the engine copy, a log) refused with its target
  untouched, a link at the open itself refused by `O_NOFOLLOW` (the generator's `open_out` and
  `copy_out` called with no check before them), `--names` entries with `..` or a leading separator refused, and a vt run without
  its model stopping the report with a line that says so.

## Evidence on this host

M6, macOS 27.2.1, 2026-11-17. Output is in a local artifacts folder, never committed.

**What was measured:** the colour, border, ASAN and timed runs below ("round 1's runs") built
commit `87434ba2`, an ancestor of the head; `src`, `scripts` and the Makefile are the same from
there through `bf8e3527`, where they were written up. After it, `git diff --stat 87434ba2 HEAD --
src scripts Makefile` shows two kinds of change. From main: FIX19's image size checks
(`src/client/refresh/files/{image.c,imgsize.h,pcx.c,wal.c}`, `src/client/refresh/soft/sw_image.c`),
FIX106's host lock (`scripts/bench/core/lock.sh`) and a Makefile line for FIX19's unit test. From
review round 2: the fetch's check and claim in one block (fetch only, no run reaches it), the
half-float conversion moved unchanged into `src/client/header/upscale_half.h`, the vt item's idle
line in `cl_upscale.c`, the generator's `O_NOFOLLOW` opens, and a Makefile line for the new unit
test. The real-data runs were not repeated: the copy path's code is the same text in a header.
The colour check that reaches it, the host test's flat tiles (now within 4 levels), was re-run at
the head (see "Review round 2").

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
  format is listed (the bytes are converted to and from half floats in the copies, as v / 255:
  see "Review round 1" for the first version, which read colours back near white). The smallest
  tile (24 x 24) through 288 x 195 are accepted.
- **The 4-texel border (round 1's runs):** the step across a world texture's wrap edge, divided by
  the mean step between neighbouring texels inside, over six e1u1 textures at 4x (ceil1_1,
  exitdr01_1, exitdr01_2, flat1_1, floor1_3, ggrat4_2): vt 0.16 to 2.01, metalfx 0.01 to 2.24,
  scale2x 0.00 to 7.89 (scale2x doubles the source's own edge, so its figure is the source's).
  vt's is below scale2x's on every texture except ceil1_1 (0.16 against 0.00, a step a sixth of
  an interior one), and at most 0.30 above metalfx's on any: no seam beyond the source's own edge,
  so the border metalfx needs (TASK124) is enough for vt too.
- **Colours (round 1's runs, mean RGB of the opaque texels at 4x):**

  | texture | scale2x | metalfx | vt |
  | --- | --- | --- | --- |
  | textures/e1u1/floor1_3 | 39.3, 28.6, 19.3 | 39.1, 28.5, 19.4 | 39.5, 28.4, 19.5 |
  | textures/e1u1/metal1_1 | 33.6, 29.7, 26.5 | 33.4, 29.6, 26.4 | 33.5, 29.4, 26.5 |
  | textures/e1u1/wndow1_2 | 63.7, 62.2, 62.3 | 62.8, 61.1, 61.1 | 62.8, 61.1, 61.4 |
  | env/unit1_ft | 96.5, 42.6, 14.6 | 96.4, 42.6, 14.6 | 96.9, 42.6, 14.5 |
  | models/monsters/soldier/skin | 22.2, 18.3, 14.5 | 22.5, 18.6, 14.7 | 22.4, 18.4, 14.3 |

  vt is within 1 level of scale2x on every channel. The host test's flat tiles at 4x: (32, 32,
  32) came back (32, 31, 31), (128, 128, 128) as itself, (200, 64, 32) as (199, 63, 32).
- **ASAN (debug build, round 1, one run each):** at 4x, 2432 written, 0 skipped, 2 refused names,
  0 failed, 516772162 bytes, 131 s; at 2x (the fallback to scale2x), 2432 written, 0 failed,
  50227150 bytes, 3 s. No AddressSanitizer or runtime-error lines in either.
- **Timed report (release, round 1, 4x, 3 runs per backend in one hold, alternated; load average
  4.10 at the start, 2.54 at the end; start-up alone 0.155 to 0.530 s, median 0.165):**

  | backend | time range | median | written | bytes |
  | --- | --- | --- | --- | --- |
  | scale2x | 3.78 to 4.31 s | 4.23 s | 2432 | 99604894 |
  | metalfx | 10.40 to 10.93 s | 10.76 s | 2432 | 547438118 |
  | vt | 96.72 to 97.71 s | 96.91 s | 2432 | 516772162 |

  The three outputs have the same names. The report is in a local artifacts folder, never
  committed (10.7 MB: it holds images of the player's data). The first round's figures (vt 88.86
  to 91.11 s, 184337862 bytes) measured the near-white output and are replaced by these.
- **Host test:** `tests/check-upscale-vt.sh` with `UPSCALE_VT_REQUIRE=1`: 25 passed, 0 failed (the
  2x cases report "skipped: VideoToolbox has no 2x here", and the menu's check greys 2x).

## Known gap, not fixed here

`CL_UpscaleAllowed` lets a command through while the client is disconnected, and a server can
make it so: an `svc_stufftext` of `disconnect` followed by the command runs the command after the
drop, from the command buffer, with the client disconnected. This predates TASK167 and reaches
every backend (scale2x and metalfx write files the player did not ask for). Since round 1 it no
longer reaches vt's model download, which only the menu item starts. Left for its own card.

## Review round 1

The panel (correctness, security, spec) found one Blocker and three Importants; each was red
first, against the code as it was at `b1675181`:

- **B1, colours:** the half-float copy encoded a byte as its own value (128 as 128.0) and decoded
  0..1 times 255, so every vt texture came out near white. Red: the host test's flat tiles at 4x
  came back (254, ...) for grey 32, 128 and 200 and both colours. Fixed by encoding v / 255
  (`_Float16`); the host test now checks flat greys and colours at each factor the scaler has.
- **Correctness I1, greying:** one refused run greyed the item for every factor for the rest of
  the process (a "no 2x" refusal on this M6, whose menu default is 2x, or a fetch that failed).
  Red: after the 2x refusal, the menu's check with 4x selected said greyed. Fixed: nothing a run
  or a fetch finds is remembered; the item is greyed by the selected factor
  (`CL_UpscaleVTAvailable(factor, ...)`, `CL_UpscaleVTItem`, unit- and host-tested).
- **Security I1, who fetches:** a server could start the model's download and a main-thread wait
  of up to 900 s with `disconnect` then `upscale_textures 4 vt` (see the known gap above). Red:
  `tests/check-upscale-vt.sh`'s new guard found the fetch called from `cl_upscale_cmd.c`, and the
  unit test's refusal line was empty. Fixed: only the menu item fetches, on its own thread of
  work, the item greyed while it runs; the command refuses without the model and writes nothing
  (decision 0013, doc/050).
- **Spec I1:** this runbook named a local path; it now says "a local artifacts folder, never
  committed".
- **Notes fixed:** the generator refuses a link in its output (the engine copy, the logs, the
  report) and a `--names` entry with `..` or a leading separator, and stops with a clear line
  when vt's model is missing (red: the self-test's new cases failed); the reason buffers are
  written only through the NULL-checked helper; decision 0013 and the DEPENDENCIES row say the
  weak link allows a lower deployment target, which the Makefile does not set.
