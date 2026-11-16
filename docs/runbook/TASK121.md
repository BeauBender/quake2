# The Metal layer's colour space: `metal_colorspace` (TASK121)

`rmb_attach` ([`rmb_device.m`](../../src/client/refresh/metal/rmb_device.m))
set the pixel format and no colour space, and SDL's Metal view sets none.
Apple documents a nil `colorspace` as content that is not colour-matched,
so on a wide-gamut display the game's sRGB-era colours were expected to
come out more saturated than authored
([plan P7](../plans/high-resolution.md#p7-tag-the-layer-as-srgb-filed)).
This card adds the switch and measures what the display shows before any
default changes. The shared runbook's method applies:
[how a run works](../plans/metal-port-harness.md#how-a-run-works) and the
[pin arguments](../plans/metal-port-harness.md#pin-arguments).

**Verdict.** `metal_colorspace` 0 (the default, today's output) leaves the
layer as SDL made it; 1 tags it `kCGColorSpaceSRGB`. **On this host's
display the two give the same screen values, to the last bit, in every
run: nothing is more saturated than authored at 0.** A control that tags
the layer Display P3 does change the captured values, so the capture can
see a colour-space difference. The layer reports `kCGColorSpaceSRGB`
already at 0: the untagged layer is treated as sRGB by this system. The
default stays 0 (the card does not change it). Pins are unchanged at both
values in every scenario (the nine random ones recorded seeded), the teardown is clean, and
the host tests hold the menu rule.

## The switch

- `metal_colorspace` (`CVAR_ARCHIVE`, default 0, documented in
  [`doc/040_cvarlist.md`](../../doc/040_cvarlist.md)): `rm_colorspace_mode`
  (`rm_pure.c`) maps the value to 0 or 1 (1 only for exactly 1; any other
  value, a NaN included, is 0). `RI_BeginFrame` applies it between frames
  when the cvar changed; `RI_Init` gives the value to `rmb_colorspace_set`
  before the layer exists, and `rmb_attach` applies it.
- `rmb_device.m`: `rmb_cs_apply` creates `CGColorSpaceCreateWithName(kCGColorSpaceSRGB)`,
  counts it in the ledger as `RM_OBJ_STATE` and sets `layer.colorspace`;
  setting back to 0, or `rmb_detach`, clears the layer's property and
  releases the object (and counts it destroyed). No other function of the
  file changed, so a merge with FIX77's present and drain code is clean.
- The video menu's renderer page has a "colour space" item
  (untagged / sRGB), always shown, greyed with "Metal renderer only" under
  every other renderer (`VIDITEM_COLORSPACE` in `vidrules.c`), written by
  apply when changed.

## The screen measurement

The host is an Apple Silicon Mac with a Studio Display, whose colour
profile is named "Studio Display" (`NSScreen.colorSpace.localizedName`),
at backing scale 2. The screen was unlocked and no other `quake2` ran
before each run, under the timing lock.

**The probe** (a scratch build of `7d4d3f8e`, never committed: the
ScreenCaptureKit and AppKit link and about 120 lines in `rmb_device.m`,
`rm_main.c` and `rmb_encode.m`). With `T121_PROBE` set, every 2D draw of
the game is dropped and each frame is the layer cleared to `r_clear`'s
colour (`rm_clear_color`, `1.0 0.0 0.5`) under eight `RDraw_Fill`
stripes of palette entries 0, 15, 4, 208, 240, 8, 7 and 12 over the lower
two thirds of the window: no world, no 2D and no game asset drawn.
Frame 90 starts the capture from inside the engine process with
`+[SCShareableContent getCurrentProcessShareableContentWithCompletionHandler:]`
(no Screen Recording permission, and none was asked for or changed) and
`SCScreenshotManager` on the engine's own window (640x480 mode, so a
window of 640x512 points, 1280x1024 pixels; `r_vsync 1`; window server
composite of the frames the engine kept presenting). The values were read
numerically from the captured pixel rows (one row in the clear colour,
one through the stripes) and logged; the image was never viewed or saved.
The first capture is the default (the display's native space, the pixel
data of the image); a second asked for Display P3 explicitly and was
identical.

Captured pixel values (B, G, R; alpha 255) at the middle of each region,
four runs at each value, runs of 0 and 1 alternated:

| Region (palette entry) | Authored sRGB (R G B) | Captured at 0, runs 1 to 4 | Captured at 1, runs 1 to 4 |
|---|---|---|---|
| clear colour | 255 0 128 (as 8 bit) | R234 G51 B127, all four runs | R234 G51 B127, all four runs |
| stripe 0 (0) | 0 0 0 | R0 G0 B0, all four | R0 G0 B0, all four |
| stripe 1 (15) | 235 235 235 | R235 G235 B235, all four | R235 G235 B235, all four |
| stripe 2 (4) | 63 63 63 | R63 G63 B63, all four | R63 G63 B63, all four |
| stripe 3 (208) | 0 255 0 | R117 G251 B76, all four | R117 G251 B76, all four |
| stripe 4 (240) | 239 0 0 | R219 G47 B32, all four | R219 G47 B32, all four |
| stripe 5 (8) | 123 123 123 | R123 G123 B123, all four | R123 G123 B123, all four |
| stripe 6 (7) | 107 107 107 | R107 G107 B107, all four | R107 G107 B107, all four |
| stripe 7 (12) | 187 187 187 | R187 G187 B187, all four | R187 G187 B187, all four |

**Difference between 0 and 1: none (0 on every channel of every region in
every run).** Both are what an sRGB to Display P3 conversion of the
authored values gives (pure sRGB green 0 255 0 becomes about 117 251 76,
pure sRGB red 239 0 0 about 219 47 32), so the system already colour-
matches the untagged layer as sRGB. The layer's own `colorspace` property
read `kCGColorSpaceSRGB` at 0 with no colour-space object of ours alive,
and `kCGColorSpaceSRGB` with one alive at 1.

**The control.** With the layer tagged Display P3 (a scratch environment
override of the colour-space name, `metal_colorspace 1`), the same stripes
were captured as the raw values: stripe 3 R0 G255 B0, stripe 4 R239 G0 B0,
the greys unchanged. So the capture shows a colour-space difference when
there is one, and 0 against 1 has none here.

Load (`sysctl -n vm.loadavg`, one minute) before and after the probe
batches: 2.1 to 3.8, other lanes running builds and checks outside the
timing batches; the values are exact and do not depend on timing.

The card asked for an INFORM if the values differ. They do not, so there
is no case for a default of 1 on this display; the finding is that the
sRGB tag is a no-op here, and that the question stays open on a display
profile where an untagged layer is not read as sRGB (not available on
this host).

## Pins at both values

`PINS_STORE` outside the repository. Each scenario of
`scripts/pins/scenarios` was recorded for `metal` twice with the engine of
`7d4d3f8e` (clean tree): at the default (0, `--run 11`) and with
`metal_colorspace 1` (`--run 12`, a wrapper in `release/` passing
`+set metal_colorspace 1` as `PINS_BIN`), and compared at threshold 0 and
bad threshold 0 (`pins.sh compare metal <scenario> --ref metal --ref-id
.. --cand-id .. --threshold 0 --bad-threshold 0`). `cinematic` was
recorded with `PINS_DEMO=idlog.cin PINS_EVERY=7`.

- **Score 0, bad 0 on every frame: 15 of the 24 scenarios** (the 14 listed,
  23 frames each, and `cinematic`, 12 frames): `hud2d`, `hud2d-scale8`, `hud2d-sharp`, `rt-ao`,
  `rt-water`, `skywater`, `world`, `world-fullbright`, `world-lmcubic`,
  `world-metalfx-interp`, `world-metalfx1`, `world-metalfx2`,
  `world-metalfx3`, `world-scale8` and `cinematic`.
- **The other 9 scenarios** (`dlights`, `dlights-lmcubic`, `models`,
  `particles`, `particles-cap`, `rt-ao-entities`, `rt-shadows`,
  `rt-shadows-entities`, `rt-water-entities`) are not identical from run to
  run unless recorded with `PINS_SEED` (see `doc/100_renderpins.md`, the
  scenario table and *Determinism*). A first pass recorded them unseeded
  (runs 11 to 14) and 0 against 0 differed as much as 0 against 1 (worst
  score up to 0.0123, worst bad up to 0.264), so it judged nothing. The
  second pass recorded each with `PINS_SEED=1`, once at the default
  (`--run 21`) and once with `metal_colorspace 1` (`--run 22`, the same
  `PINS_BIN` wrapper), engine `b2777630` (the sources equal those of
  `7d4d3f8e`), same compare arguments at threshold 0 and bad threshold 0.
  The records were made with the screen locked (pin records are
  byte-identical locked or unlocked), no other `quake2` of mine running,
  under the timing lock; load average (1 minute) 2.9 to 4.4 around the
  batches. Every frame, 23 of 23 in each:

| Scenario | 0 vs 1, seeded (runs 21, 22): worst score / worst bad |
|---|---|
| `models` | 0 / 0 |
| `dlights` | 0 / 0 |
| `dlights-lmcubic` | 0 / 0 |
| `particles` | 0 / 0 |
| `particles-cap` | 0 / 0 |
| `rt-ao-entities` | 0 / 0 |
| `rt-shadows` | 0 / 0 |
| `rt-shadows-entities` | 0 / 0 |
| `rt-water-entities` | 0 / 0 |
| control: `models`, 0 against 0 (run 21 with a third record, run 23) | 0 / 0 |

**All 24 scenarios are now score 0, bad 0 between `metal_colorspace` 0 and
1** (the 15 above unseeded, deterministic; the 9 seeded). The unseeded
differences of the first pass were the scenarios' own randomness, not the
colour space.

The change draws nothing and reads back before the layer's colour space
comes into play (the readback precedes composition), so the pins at 0 and
1 are identical in every scenario.

## Teardown

An `ASAN=1` build of the client, the game and `ref_metal` (`7d4d3f8e`),
one session: ten switches of `metal_colorspace` at run time (0, 1, 0, 1 and
so on, six frames apart, from a `+exec`ed config), then ten `vid_restart`
cycles with the value at 1. Result: exit status 0, no AddressSanitizer
report, and every teardown line of the ledger
`0 live objects, 0 unreleased, 0 unmatched`, with the state counter
created equal to destroyed (53/53 in the session that held the switches,
48/48 in each later one), the colour-space object counted in it. The last
line, at quit with the value back at 0, reads the same.

## Host tests

`tests/metal/rm_host_test.c` (`test_colorspace_mode`),
`tests/unit/test_vidrules.c` (`vidrules_colorspace_is_metal_only`) and
`tests/menu/videomenu_test.c` (`test_colorspace_item`, and the item in the
greyed-items-cannot-change check), each written before the code. The checks
of the card ran: see the report of the lane.
