# TASK172: the iPhone Duo, a live fold and unfold

The iPhone Duo is a foldable: an outer display and a larger inner one, and an app moves between them,
resized live, as the device folds and unfolds. This card makes the iOS window follow any change of its
size or safe area with no `vid_restart`, lets the view take any aspect, and adds a check that the iOS
paths size nothing by the main screen, the model or a fixed number.

## Apple's page

<https://developer.apple.com/iphone-duo/prepare/> (read 2026-11-17) says: an app built with the iOS 27
SDK resizes dynamically on the iPhone Duo, avoiding the status bar at the inner display's right edge;
built with the 27.1 SDK it is "optimized" and extends to the full display. A fold can resize an app in
the middle of a session. Replace `UIScreen.main.bounds` with the view's or window's bounds and take the
scale from the trait collection; do not lay out by `UIDevice` orientation, `userInterfaceIdiom`, a size
class taken as iPad, or a device size such as `bounds.height == 844`. The page does not give the
displays' sizes, does not say that `UIScreen.main` is deprecated, and says nothing about orientations,
the safe area's sides, the scene's lifecycle during a fold, or Metal. The card's figures (669x951 and
466x678 points) are checked against the device type below.

## Measurements (iPhone Duo simulator, iOS 27.2, Xcode 27.2, on the M6)

The device type (`com.apple.CoreSimulator.SimDeviceType.iPhone-Duo`, model `iPhone19,4`, idiom
`phone`) lists two integrated displays in its profile: `primary` ("LCD", screen 1) 1398x2034 pixels at
3x, 466x678 points, the outer display, corner radii 8 on the left and 59 on the right (the fold side is
square); and `primary-1` ("LCD-1", screen 3) 2007x2853 pixels at 3x, 669x951 points, the inner display,
native rotation 270. Both card figures hold. It also lists a "Resizable" scene display
(`com.apple.CoreSimulator.display.resizableScene`), powered off.

What the engine logs at start. On main (before this card) the `Real display mode: 678x466@60.00`,
`ref_metal: safe area: window 2034x1398 pixels, insets left 252 top 0 right 0 bottom 102` and
`Drawable size: 1782x1296` lines; with this card the same three and the new `Screen:` line, which
gives the points, each inset and the orientation:

| state | window (points) | window (pixels) | insets left / top / right / bottom (points) | orientation | view (pixels) |
| --- | --- | --- | --- | --- | --- |
| outer display, landscape | 678x466 | 2034x1398 | 84 / 0 / 0 / 34 | 1 (landscape) | 1782x1296 |

The insets are asymmetric: the sensor housing's 84 points (252 pixels) on the left only, none on the
right. The app opens on the outer display, landscape, and stays there. One `refresher initialization`
line on both trees: no `vid_restart` at start.

## How folding is driven

It is not scriptable with `simctl`. `simctl` has no fold, rotate or posture command (`simctl help`,
`simctl ui` lists appearance, contrast and content size only). `simctl io <udid> screenConfig
--display=<primary|primary-1> power on|off` switches a display on or off and is accepted, but the app's
window does not move: in both runs (on main and with this card) the engine logged no window event
through 10 power cycles of the two displays during play, the app stayed on the outer display, and
`screenConfig geometry` accepts only a display's own mode (`No mode found with scale factor 3.0 that
supports size 2007x2853` for the outer display). A screenshot of a powered-off display hangs, and with
two displays `simctl io screenshot` needs `--display`, so `scripts/ios-sim.sh` takes `IOS_SIM_DISPLAY`
(the outer display is `primary`). Folding is done by hand (owner step below).

The resize itself therefore is shown by the path it takes, which a fold shares with a rotation: SDL
reports `SDL_EVENT_WINDOW_SAFE_AREA_CHANGED` and `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`, read from the
window's view (`safeAreaInsets`, the view's bounds), never from `UIScreen.main`.

## The design

- **A live resize.** `GLimp_ScreenEvent` (glimp_sdl3.c) runs on those two events. When the window's
  pixel size or safe rectangle is not the one recorded, it records it, logs the window (the `Screen:`
  line: points, pixels, density, each inset on its own, orientation, display), takes viddef from the
  renderer's drawable (`re.GetDrawableSize`, ref_metal's safe rectangle in pixels) and logs `Drawable
  size: WxH (live)`. ref_metal's `RI_BeginFrame` takes the same size for its frame at the next frame,
  and `rmb_frame_begin` remakes the colour and depth targets and the layer's drawable for it (the path
  any size change takes, `rm_frame_remake`); the old ones are released. The 3D view (`SCR_CalcVrect`),
  the HUD, the console (`Con_CheckResize`) and the menus read viddef every frame, and the touch overlay
  reads the safe area at each touch (`IN_TouchSync`), so all follow on the next frame. There is no
  `vid_restart`. A window with no pixels (hidden in the middle of a change) is left alone and the change
  is taken when it has a size again. Only a renderer that gives no drawable falls back to TASK147's
  `vid_restart`. Textures upscaled for the screen's height at load (`upscale_textures`) keep the
  height they were loaded for until the next map.
- **Any aspect: Hor+ with a vertical minimum.** With `horplus` (the default) the view was Hor+: the
  vertical field of view of 4:3 kept, the horizontal one widened with the aspect. Under 4:3 that
  narrows it (a portrait inner display, 2007x2652 pixels, would see 59 degrees across). On iOS the
  horizontal view is now never narrower than `fov` (`CL_AspectFov`): the whole 4:3 view stays in the
  picture and a taller window shows more above and below it. At 4:3 and wider it is Hor+ as before,
  so the iPhone (2250x1146) and the iPad (2420x1628) are unchanged, and other platforms keep plain
  Hor+. The console, the menus and the HUD are laid out in viddef, the safe rectangle: the default
  scales are the smaller of width / 640 and height / 240, and an asked scale is clamped so that the
  320x240 block fits, so they fit at any aspect; the HUD's corners (`xl`, `xr`, `yt`, `yb`) anchor to
  the rectangle's edges, each inset its own. The look is the owner's call: the frames below are for
  that decision, which is not yet recorded.
- **Safe areas.** Every inset is taken on its own from SDL's safe rectangle (`rm_safe_insets`, the
  four edges separately) on every change; nothing assumes left equals right.
- **No model or fixed size.** `tests/check-ios-screen.sh` refuses, in the iOS-only blocks of `src/`,
  `src/client/input/touch.c` and `stuff/ios/`: `UIScreen.main` and `mainScreen` (anywhere in `src/`),
  `hw.machine`, `hw.model`, `utsname`, `UIDevice`, `userInterfaceIdiom`, a model string or identifier,
  a comparison with a number of three digits or more, and `CGSizeMake` or `CGRectMake` of one. Its
  selftest runs 19 cases first (14 mutants refused, 5 look-alikes passed) and it fails when it finds
  fewer than 200 lines of iOS paths.

## Tests

- `tests/unit/test_duo_layout.c`: the field of view and the HUD's rectangles for the outer and inner
  displays, portrait and landscape, with the measured outer insets and insets unequal on every side.
  Red first against a `CL_AspectFov` that was Hor+ alone: `duo_fov_any_aspect_keeps_the_4_3_view` and
  `duo_fov_portrait_is_cl_fov_across` failed (`outer portrait: view 1398x1755, fov 61.71 x 73.74`,
  `inner portrait: view 2007x2652, fov 59.16 x 73.74`), and so did `duo_hud_inside_the_view`, for
  the test's own reason (it took `vw / 2 - 160 * scale` in float where the layout takes it into an
  int, half a pixel left of 0 at an odd width); the test now takes it as the layout does.
- `tests/metal/rm_host_test.c` `test_duo_viewports`: the insets and the view in pixels for the five
  states, through `rm_safe_insets` and `rm_window_sizes` (`tests/check-ref-metal.sh` runs it).

## Frames

In `<artifacts>/TASK172/frames/` (the owner's data: local only, never committed or uploaded, never
viewed by the agent): `before/` (main), `after/` and `geom/` (this card). `a0-both.primary.png`
(2034x1398) is the outer display in landscape at the start; `a1-unfolded.primary-1.png`
(2853x2007) and `a4-unfolded-end.primary-1.png` (2007x2853) are the inner display after the outer
was switched off; `a2-folded.primary.png` and `a3-folded-end.primary.png` the outer display after
the inner was switched off. Since the app did not move between the displays, the inner display's
frames show what the simulator puts there, not the game in a portrait window.

## The runs

| run | tree | what | result |
| --- | --- | --- | --- |
| `measure-before` | main | `base1`, the displays switched off and on | no window event, one refresher initialization |
| `measure-after` | this card | `base1`, 10 cycles of the two displays during play | no window event, no crash, 0 failed command buffers in 29193 frames, no validation message, `leaks`: 0 leaks for 0 total leaked bytes |
| `measure-geom` | this card | `screenConfig geometry`, 10 cycles | the outer display refuses the inner's size; 0 leaks |

All with Metal's validation layer on (`SIMCTL_CHILD_MTL_DEBUG_LAYER=1`, "Metal API Validation
Enabled" in the app's output). Commands: the scratch driver around `IOS_SIM_TYPE="iPhone Duo"
IOS_SIM_DISPLAY=primary scripts/ios-sim.sh run --data <data dir> --launch "+set metal_stats 1 +map
base1" --shot <s> --out <artifacts>/TASK172/<run>`, each through the host lock.

## Fold by hand (owner step)

1. `IOS_SIM_TYPE="iPhone Duo" IOS_SIM_DISPLAY=primary scripts/ios-sim.sh run --data <data dir>
   --launch "+map base1" --out <artifacts>/TASK172/fold` and open Simulator on that device.
2. Fold and unfold it from Simulator's Device menu at least 10 times during play, once with the
   console down and once in a menu; rotate the outer display between landscape left and right.
3. Quit from the console (`quit`) and read `<out>/engine.1.log`: each change has a `Screen:` line and
   a `Drawable size: WxH (live)` line, and the drawable is the window less the four insets of the
   `ref_metal: safe area:` line before it; there is one `refresher initialization` line in the whole
   run; `metal_stats` has 0 failed command buffers. On the inner display, look whether the window is
   portrait (`orientation 3` or `4`), and whether the HUD's corners clear the status bar on the right.
4. Record the look decision (Hor+ with a vertical minimum, or another rule) here.

## iPhone and iPad unchanged

On this card's tree, each through the host lock:

- `scripts/ios-sim.sh smoke` on the iPhone 17 and on the iPad Pro 11-inch (M5): `smoke ok` on both
  (the safe rectangle pixel for pixel, at most one `vid_restart`, the quit path, no validation
  message). Their `Screen:` lines: iPhone 17 874x402 points, 2622x1206 pixels, insets 62 / 0 / 62 /
  20 points, orientation 1; iPad 1210x834 points, 2420x1668 pixels, insets 0 / 0 / 0 / 20 points,
  orientation 3 (SDL's orientation of the iPad's display, whose own is portrait; the window is
  landscape).
- TASK147 item 2 again (`run --data <data dir> --launch "+map base1" --shot 45`, the owner's data):
  the iPhone 17's safe rectangle 2250x1146 at insets 186 / 0 / 186 / 60 pixels and the iPad's
  2420x1628 at 0 / 0 / 0 / 40, both TASK147's exactly; `Drawable size:` is the window less the
  insets, one refresher initialization each, and no lit pixel outside the rectangle in the
  screenshot (counted at every second pixel by a throwaway script, the iPad's frame turned
  clockwise; the other turn leaves 23874 outside).
- `tests/check-ios.sh`: `check-ios: ok (54 mutants refused; 125 smoke selftest cases; 65 device
  selftest cases; 174 objects for the simulator; 174 for a device; ios_objects)`.

## Round 2: a live resize driven by orientation

Round 1 could not move the window: `simctl` cannot fold the iPhone Duo and `screenConfig` power cycles
never moved it. Round 2 drives a change that can be driven, an interface orientation, on the iPhone 17
and the iPad Pro 11-inch (M5) simulators (iOS 27.2).

### The request

`ios_orientation <portrait|portraitupsidedown|landscapeleft|landscaperight|landscape|all>` (iOS only,
`doc/050_commands.md`) calls `-[UIWindowScene requestGeometryUpdateWithPreferences:errorHandler:]` with
`UIWindowSceneGeometryPreferencesIOS` for that mask (`GLimp_RequestOrientation`, glimp_sdl3.c). It is
harmless from a server's stufftext: the system grants only an orientation the view controller supports
(the two landscapes of `Info.plist`) and answers anything else with an error, which the engine prints as
`Orientation: refused (...)`. `ios_orientation` with no name prints `Orientation now: scene N` (the
scene's `effectiveGeometry.interfaceOrientation`: 3 landscape right, 4 landscape left) and a
`Screen now:` line, a measurement that does not touch the screen.

### What each request does (measured, `rot-iphone17/` and `rot-ipad/`)

The app supports `landscapeLeft` and `landscapeRight` on both devices (the plist has one list for both
families; the error names them). So:

| request | iPhone 17 | iPad Pro 11-inch (M5) |
| --- | --- | --- |
| `portrait`, `portraitupsidedown` | refused, nothing moves | refused, nothing moves |
| `landscapeleft` | scene 4 (SDL orientation 2); window 874x402 points, insets 62 / 0 / 62 / 20 unchanged | scene 4; window 1210x834 points unchanged; insets 0 / 0 / 0 / 10 (was 0 / 10 / 0 / 20) |
| `landscaperight` | scene 3 (SDL orientation 1); size and insets unchanged | scene 3; insets 0 / 10 / 0 / 20 (was 0 / 0 / 0 / 10) |
| `landscape`, `all` | stays where it is (both are allowed) | stays where it is |

The 180 degree flip is real on both (the scene's orientation alternates 3 and 4), but on the iPhone 17 its
insets are symmetric (62 each side), so the window's size and safe area do not change and SDL reports no
event: the live path has nothing to follow, which is the correct behaviour, and there is no `Screen:` line.
On the iPad the flip moves the home indicator's inset between the top and the bottom edge, so the safe
area changes and the drawable follows (2420x1648 and 2420x1608 pixels, the window 2420x1668 less the four
insets). No request changes the size of the window: a portrait window needs a portrait orientation in
`Info.plist`, which this card does not change. The iPhone Duo's inner display, which does not honour the
supported orientations, is still the owner's hand-fold step below.

### The runs (Metal validation layer on, `base1`, `metal_stats 1`)

15 requests per device: `landscapeleft`/`landscaperight` alternately (11), then `landscape`, `all`,
`portrait` and `portraitupsidedown` once each. Driver: a `rot.cfg` of `ios_orientation` lines separated by
`wait` aliases, started with `+exec`, with a scratch script that shoots a frame after each settle and
runs `leaks` at the end (the scratch is not committed).

| device | requests | refused | `Screen:` after the first | `Drawable size ... (live)` | refresher initialization | `vid_restart` | failed command buffers | validation messages | `leaks` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| iPhone 17 | 15 | 2 (portrait kinds) | 0 (no size or inset change) | 0 | 1 | 0 | 0 | 0 | 0 leaks for 0 bytes |
| iPad Pro 11-inch (M5) | 15 | 2 | 11 | 11 | 1 | 0 | 0 | 0 | 0 leaks for 0 bytes |

No crash on either (`shutting down` once). The live path ran 11 times on the iPad, where the safe area
changed with each landscape flip (11 of the 11 flips that changed the scene's orientation). On the iPhone 17
nothing a supported orientation does changes the window, so the proof there is that 15 requests, 11 real
flips included, ran with no restart, no second initialization and no failure.

### Frames

`<artifacts>/TASK172/rotate/` (owner's data: local only, never viewed by the agent): `phone-NN-<request>.png`
and `ipad-NN-<request>.png`, one after each request, and `hud-check.txt`. The check is TASK147's HUD
rectangle method on every frame (every second pixel, the iPad's frame turned as its framebuffer is): no lit
pixel outside the new safe area (0 of 30 frames), and the lit pixels' bounding box is the safe area itself
to within a pixel (iPhone 17 (186, 0)-(2434, 1144) of a (186, 0)-(2435, 1145) rectangle; iPad (0, 0)-(2418,
1646) or (0, 20)-(2418, 1626)), 98 to 99 percent of the rectangle lit.

### N2

`GLimp_ScreenEvent`'s three-way decision is `GLimp_ScreenAction(made, now, dw, dh)` in glimp_hidpi.c,
returning `GLIMP_SCREEN_IGNORE` (an empty window, or a screen already noted), `GLIMP_SCREEN_RESTART` (changed,
the renderer gives no drawable) or `GLIMP_SCREEN_LIVE`. Red first, against a stub that always ignored:
```
FAIL hidpi_screen_action_live (tests/unit/test_hidpi.c:257): check failed: GLimp_ScreenAction(made, flipped, 2436, 1146) == GLIMP_SCREEN_LIVE
FAIL hidpi_screen_action_restart (tests/unit/test_hidpi.c:243): check failed: GLimp_ScreenAction(made, flipped, 0, 0) == GLIMP_SCREEN_RESTART
yq2test: 15 tests, 2 failed
```
and green after it: `yq2test: 16 tests, 0 failed`. The stale comment on `GLimp_ScreenChanged` (an empty drawable
restarts) now says that `GLimp_ScreenAction` is what leaves such a window alone.

### Still the owner's

The hand-fold steps above stand: the inner display, the portrait window and the look decision are not
reachable from `simctl` or from an orientation request.

## Round 3: ios_orientation is iOS only

`ios_orientation` and `GLimp_RequestOrientation` were guarded by `TARGET_OS_IPHONE`, which is 1 on tvOS and
visionOS too. `UIWindowSceneGeometryPreferencesIOS`, `UIInterfaceOrientationMask*` and
`effectiveGeometry.interfaceOrientation` are unavailable on tvOS, so a tvOS build failed (`clang -fsyntax-only` of
`glimp_sdl3.c` for `arm64-apple-tvos17.0-simulator`: `error: 'UIWindowSceneGeometryPreferencesIOS' is unavailable: not available on tvOS`).
Both are now guarded by `TARGET_OS_IOS`, which the SDK's `TargetConditionals.h` sets to 0 on tvOS (line 243) and
leaves at its default 0 on visionOS (line 418). The command is not registered on tvOS or visionOS. The UIKit
import stays on `TARGET_OS_IPHONE`.
