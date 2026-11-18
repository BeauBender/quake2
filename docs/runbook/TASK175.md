# TASK175: the visionOS simulator build boots to the main menu and loads base1 in a window (slice 1)

Slice 1 of TASK175 (issue #477): the visionOS target builds for the Apple Vision Pro simulator from the iOS code,
and the game reaches the main menu and loads `base1` in the simulator, in a window, on `ref_metal` (visionOS has no
OpenGL, so it is the only renderer). It starts from the tvOS slice (TASK23, *docs/runbook/TASK23.md*) and does
the same things for a fourth SDK. Not in this slice, and left to slice 2: controller input, look-and-pinch menu
navigation, the game-data path, resizing beyond the window visionOS gives by default, and immersive or volumetric
modes.

## The build

`make ios IOS_SDK=xrsimulator` is the iOS mode of the Makefile with a fourth SDK:
`-target arm64-apple-xros1.0-simulator` against `xcrun --sdk xrsimulator`, the same object list as the iOS
simulator build (STATIC_MODULES=1, `ref_metal` linked in, the vendored SDL3 built for the SDK into
`IOS_ROOT/sdl3`). `make ios_objects IOS_SDK=xrsimulator` builds the objects and the plist only.

- `Makefile`: the SDK's target, `IOS_PLATFORM` (XRSimulator, the SDK's platform directory), the plist template
  `stuff/ios/Info-visionos.plist.in` and the ad hoc `codesign`. `IOS_MIN` defaults to 1.0 for this SDK only, the
  visionOS of the iOS 17 year (the SDK's `MinimumDeploymentTarget`); the other SDKs keep 17.0. MetalFX is
  weak-linked only if the SDK has it, and the visionOS simulator SDK has none, so the build links none.
- `stuff/ios/Info-visionos.plist.in`: the Apple TV template with `UIDeviceFamily` 7 (Apple Vision): no
  orientations, no status bar key, no file sharing key (the data path is slice 2).
- `third_party/sdl3.make`: unchanged. The visionOS simulator SDK has every framework of the iOS mode's list
  (AVFoundation, AudioToolbox, CoreBluetooth, CoreGraphics, CoreHaptics, CoreMedia, CoreMotion, CoreVideo,
  Foundation, GameController, Metal, OpenGLES, QuartzCore, UIKit, checked in the SDK's `System/Library/Frameworks`),
  and the app links with it. SDL's own `SDL_build_config_ios.h` turns OpenGL ES off for `TARGET_OS_VISION`, so no
  symbol of the linked executable comes from OpenGLES (`nm -um`: 0) and one from CoreMotion; the SDL sources are
  the same, and SDL's UIKit files guard their own visionOS differences (`SDL_PLATFORM_VISIONOS`: the window is
  made at `SDL_XR_SCREENWIDTH` by `SDL_XR_SCREENHEIGHT`, no `UIScreen`, no orientation, no camera).
- `scripts/ios-sim.sh`: `IOS_SIM_SDK=xrsimulator` passes that SDK to `make ios`, picks a visionOS runtime (simctl's
  platform `xrOS`) and the device type "Apple Vision Pro" (`com.apple.CoreSimulator.SimDeviceType.Apple-Vision-Pro-4K`,
  matched by its whole name, so not "Apple Vision Pro (at 2732x2048)", which the runtime does not list).
  `IOS_SIM_TYPE` and `IOS_SIM_RUNTIME` still override. Any other `IOS_SIM_SDK` than empty, `iphonesimulator`,
  `appletvsimulator` or `xrsimulator` stops the run before `make` with one line naming those. The selftest's fake
  `xcrun` has a visionOS runtime and both Apple Vision Pro types next to the iOS ones.
- `tests/check-visionos.sh` (in the gate): the objects build for platform 12 (XROSSIMULATOR) at minos 1.0; the
  object list equals the iOS simulator's; the plist is the Apple Vision's; the build's target preprocesses to
  `TARGET_OS_VISION` 1, `TARGET_OS_IPHONE` 1, `TARGET_OS_IOS` 0; and the compiled objects of three of the iPhone
  and iPad only sites show them out (`Touch_Supported` in `touch.o` is `mov w0, #0x0; ret`, `glimp_sdl3.o` holds
  no orientation hint, `vid.o` no `ios_orientation` command). It first refuses a mutant of each. Without the
  visionOS SDK it prints one skip line. Its helpers are copied from `tests/check-tvos.sh` with the platform, the
  minimum OS and the device family changed, not shared with it: a shared file would change what `check-tvos.sh`
  runs.

### The iOS-only code on visionOS

No guard is added or changed. `TARGET_OS_IPHONE` is 1 on visionOS and `TARGET_OS_IOS` 0, so the four iPhone and
iPad only sites, whose `#if` names `TARGET_OS_IOS` since TASK23 (`tests/check-tvos.sh` (d) holds that for each),
are compiled out, which is right on visionOS too:

- `Touch_Supported` (src/client/input/touch.c): visionOS has no touch screen; the overlay stays off.
- `in_touch` defaults to 0 and the on-screen keyboard follow-up in `IN_Update` (src/client/input/sdl3.c) is off:
  there is no touch, and the keyboard is slice 2's input work.
- `SDL_HINT_ORIENTATIONS` and `SDL_HINT_IOS_HIDE_HOME_INDICATOR` (src/client/vid/glimp_sdl3.c): a window has no
  orientation and there is no home indicator.

TASK172's `ios_orientation` command and its `UIWindowSceneGeometryPreferencesIOS` request (src/client/vid/vid.c,
src/client/vid/glimp_sdl3.c) are `TARGET_OS_IOS` too; visionOS does not have that class.

The other iOS APIs the card names, looked for in the shared code: `UIScreen` and the device model are not used by
the engine (the screen is measured through SDL, `MeasureScreen`); the orientation is the sites above; the status
bar is a plist key the visionOS plist does not have; the keyboard is the follow-up above; haptics go through SDL's
gamepad rumble, which needs a controller (slice 2); there is no document picker. The `TARGET_OS_IPHONE` blocks
that do run on visionOS are right there, and the run below shows them: the window's pixel size and safe area
(the window SDL gives, 1280x720 points at density 2, no insets), the drawable at that size, high pixel density,
the sandboxed home (`sys_sandbox_home`), the app event watch (background and resume), the SDL main wrapper, the
`ambient` audio category, the quit entry greyed, and the vertical FOV minimum.

## The run

```sh
IOS_SIM_SDK=xrsimulator scripts/ios-sim.sh run --data <data dir> \
    --launch "+set vid_renderer metal +set logfile 2 +set metal_stats 1 +alias w10 \"wait;...;wait\" +alias w100 \"w10;...;w10\" +menu_main +w100 +w100 +screenshot tga +w100 +w100 +w100 +quit" \
    --launch "+set vid_renderer metal +set logfile 2 +set metal_stats 1 +alias w10 \"wait;...;wait\" +alias w100 \"w10;...;w10\" +map base1 +w100 +w100 +w100 +w100 +w100 +w100 +screenshot tga +w100 +w100 +w100 +quit" \
    --out <a local artifacts folder, never committed>/run3
```

`w10` is ten `wait`s and `w100` ten `w10`s, written out in full: the engine takes at most 128 command-line
arguments, and 900 `+wait` words are more (round 0's first run stopped on "Error: argc > MAX_NUM_ARGVS"). The
script builds into `--out`/build, creates a throwaway Apple Vision Pro device, copies `baseq2/*.pak` into the app's
`Documents/baseq2` (`simctl get_app_container <udid> <bundle> data`), launches the app with Metal's validation layer
(`SIMCTL_CHILD_MTL_DEBUG_LAYER=1`) and deletes the device. The data is never committed or bundled.

Host: Xcode 27.2 (27B52), SDK xrsimulator 27.2, visionOS 27.2 simulator runtime (24N120,
`com.apple.CoreSimulator.SimRuntime.xrOS-27-2`), device type Apple Vision Pro
(`com.apple.CoreSimulator.SimDeviceType.Apple-Vision-Pro-4K`). Measured on the tree of commit `a58aa978`; what came
after it is this runbook and its index line (`git diff --stat a58aa978 HEAD` names only `docs/`).

What the logs showed (`engine.<n>.log` is the engine's own log, `console.<n>.log` the app's output):

- both starts: `Metal device: Apple xrOS simulator GPU`, `Using the linked-in renderer: ref_metal (linked in)`,
  `Real display mode: 1280x720@90.00`, `Screen: window 1280x720 points, 2560x1440 pixels, density 2.00, insets left
  0 top 0 right 0 bottom 0 points, orientation 1, display 1`, `Drawable size: 2560x1440`; the console's only
  validation line is `Metal API Validation Enabled`, so no validation message;
- start 1 (the menu): `Wrote q2_0000.tga` 200 frames after `menu_main`, then `ref_metal: metal_stats: 920 frames, 0
  without a drawable, 0 skipped, 0 failed command buffers` after `shutting down`;
- start 2 (`base1`): ` Outer Base`, the world and lightmap lines of `maps/base1.bsp`, `Wrote q2_0001.tga`, then
  `ref_metal: metal_stats: 1681 frames, 0 without a drawable, 0 skipped, 0 failed command buffers` after
  `shutting down`;
- each start ended through its own quit path, and the teardown line shows 0 live objects;
- the two engine frames are 2560x1440 and not one colour; they are in the local artifacts folder, never committed.

The first start makes the window with `Setting mode 4: 640x480 (vid_fullscreen 1)` and then takes the window's own
size, as on the iPhone; the second reads back the saved `r_mode -2` and `vid_fullscreen 2` (`Setting mode -2:
1280x720`). The run printed nothing about the simulated device's own processes. Two lines of the app's output
are for slice 2: SDL's `You need UIApplicationSupportsIndirectInputEvents in your Info.plist for mouse support`,
and UIKit's `Unbalanced calls to begin/end appearance transitions for <SDL_uikitviewcontroller>` (twice, as the map
starts).

## Notes for slice 2

- Input: controller input (SDL's GameController path, and haptics through its rumble), and look-and-pinch onto the
  existing menu navigation. A pinch reaches a UIKit app as an indirect touch or a pointer; SDL asks for
  `UIApplicationSupportsIndirectInputEvents` in the Info.plist for pointer input. The touch overlay is off
  (`Touch_Supported` 0), but `Touch_DefaultBind` (src/client/cl_keyboard.c) still runs, and a hardware keyboard
  cannot type into the console, because text input never starts (the on-screen keyboard follow-up is
  `TARGET_OS_IOS`).
- The data path: visionOS has the Files app, so the iPhone's `UIFileSharingEnabled` and
  `LSSupportsOpeningDocumentsInPlace` with `Documents/baseq2` may work as they do there; this slice copies the paks
  with `simctl` and the plist has neither key (`tests/check-visionos.sh` refuses `UIFileSharingEnabled` until then).
  The missing-data text, `Sys_MissingDataMessage` in src/backends/unix/system.c, names "On My iPhone (or iPad)".
- Resizing: the person can resize a visionOS window. TASK172's live resize (`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`
  and `SDL_EVENT_WINDOW_SAFE_AREA_CHANGED`, src/client/input/sdl3.c, and the frame following the window in
  ref_metal) is `TARGET_OS_IPHONE` and so compiled in, but this slice does not exercise it. The default window is
  SDL's 1280x720 points. `VID_PLATFORM` (src/client/menu/videomenu.c) is the iOS one, with the iOS display list.
- `scripts/ios-sim.sh`: `run --background` launches `com.apple.Preferences`, which may not exist on visionOS; its
  SDK-not-found and runtime-pick errors still say "iOS".
