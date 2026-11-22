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
  orientations, no status bar key, no file sharing key (the data path is slice 2; slice 2a adds the two keys,
  below).
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
  `TARGET_OS_VISION` 1, `TARGET_OS_IPHONE` 1, `TARGET_OS_IOS` 0; and the compiled objects of the iPhone and iPad
  only sites show them out: `Touch_Supported` in `touch.o` is `mov w0, #0x0; ret`; `sdl3.o` holds no
  `SDL_RETURN_KEY_HIDES_IME` (the `in_touch` default's block) and `nm -u` lists no `_Touch_TextInput` (the
  on-screen keyboard follow-up); `glimp_sdl3.o` holds neither hint name, `SDL_ORIENTATIONS` nor
  `SDL_IOS_HIDE_HOME_INDICATOR`, whatever the value; `vid.o` holds no `ios_orientation` command. And `nm -u` of
  every `imgdecode.o` lists `_kCGImageSourceAllowableTypes` (see the version checks below). Every pinned string or
  symbol has a positive control: the iOS simulator's object of the same file, compiled with the command `make -n`
  gives for it, must hold it, so a renamed string fails the check instead of passing it. A `grep` or `nm` error,
  or a missing or unreadable object, refuses; the plist's forbidden keys are decided by `plutil -extract`, not by
  the text. It first refuses a mutant of each. Without the visionOS SDK it prints one skip line. Its helpers are
  copied from `tests/check-tvos.sh` with the platform, the minimum OS and the device family changed, not shared
  with it: a shared file would change what `check-tvos.sh` runs.

### The iOS-only code on visionOS

No `TARGET_OS_*` guard is added or changed (one OS version check is, below). `TARGET_OS_IPHONE` is 1 on visionOS
and `TARGET_OS_IOS` 0, so the four iPhone and iPad only sites, whose `#if` reads `TARGET_OS_IOS`'s value since
TASK23, are compiled out, which is right on visionOS too. `tests/check-tvos.sh` (d) holds that for each, and from
review round 1 it refuses a guard that is true on visionOS although it names the macro: `defined(TARGET_OS_IOS)`
alone, `#ifdef TARGET_OS_IOS` (the macro is defined, to 0, on tvOS and visionOS) and a `!` before the name.

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

### The OS version checks on visionOS

The visionOS SDK has its own version macros: `__VISION_OS_VERSION_MAX_ALLOWED` (`__VISIONOS_27_2` in the 27.2
SDK), while it sets `__IPHONE_OS_VERSION_MAX_ALLOWED` to `__IPHONE_17_0` whatever its release (`clang -E -dM` for
`arm64-apple-xros1.0-simulator`). And `__builtin_available` and `@available` on visionOS read only a `visionOS`
spec: a probe built for `arm64-apple-xros1.0-simulator` at `-O2` folds `__builtin_available(macOS 13.0, *)`,
`(iOS 99.0, *)` and `(macOS 13.0, iOS 16.0, tvOS 27.1, *)` to true (a tail call, no version check), and only
`(visionOS 27.0, *)` calls `___isPlatformVersionAtLeast`. Every check in `src` (review round 1), with the branch
visionOS takes; "in the build" is the visionOS simulator's `make -n -B ios_objects`:

| Site | Check | In the visionOS build | Branch on visionOS | Intended |
|---|---|---|---|---|
| src/client/refresh/files/imgdecode.c, `IMGDEC_HAVE_ALLOWABLE_TYPES` | `#if` of `__MAC_OS_X`/`__IPHONE_OS`/`__TV_OS`/`__WATCH_OS_VERSION_MAX_ALLOWED >= 270000` | yes (2 objects) | was compiled out (`__IPHONE_17_0`) | **no: fixed**, the `#if` also reads `__VISION_OS_VERSION_MAX_ALLOWED >= 270000` |
| src/client/refresh/files/imgdecode.c, the per-source `kCGImageSourceAllowableTypes` | `__builtin_available(macOS 27.0, iOS 27.0, tvOS 27.0, watchOS 27.0, *)` | yes | would be true from visionOS 1.0 (`*`), on a system without the key | **no: fixed**, `visionOS 27.0` added (the visionOS of the 27.0 systems), a run-time check |
| src/client/refresh/metal/rmb_metalfx.m (6 sites) | `@available(macOS 13.0, iOS 16.0, tvOS 27.1, *)` (4), `(macOS 26.0, iOS 26.0, tvOS 27.1, *)` (2) | yes, but none of the six | compiled out: the visionOS simulator SDK has no MetalFX, so `RMB_HAVE_METALFX` is unset and every mode reads as unsupported (0 `@available` after the preprocessor, as for the iOS and tvOS simulators) | yes (decision 0011) |
| src/client/refresh/metal/rmb_device.m:738 | `@available(macOS 14.0, *)` (`supportsRaytracingFromRender`) | yes | true, as on iOS and tvOS | yes; `-Wunguarded-availability` gives no warning |
| src/client/refresh/metal/rmb_rt.m:343, 1007, 1078 | `@available(macOS 13.0, *)` (`vertexFormat`, `MTLAccelerationStructurePassDescriptor`) | yes | true, as on iOS and tvOS | yes; no warning |
| src/client/cl_upscale_metalfx.m:77, 155, 232 | `@available(macOS 13.0, *)` | no (the macOS client only) | not compiled | n/a |
| src/client/refresh/gl1/metal/mgl_layer.m:208 | `@available(macOS 14.0, *)` | no (the macOS `ref_gl1` shim) | not compiled | n/a |

So two checks were wrong on visionOS, both in imgdecode.c, and both are fixed. `clang -fsyntax-only
-Wunguarded-availability` of imgdecode.c, with each build's own command, gives no diagnostic but the driver's
unused `-c` for macOS, the iOS, tvOS and visionOS simulators and the iPhone. `nm -u imgdecode.o` lists
`_kCGImageSourceAllowableTypes` on all five now; before the fix the visionOS object did not. The object is
byte-identical before and after the fix on the four others. `rmb_metalfx.m`'s checks would need a `visionOS` spec
on a visionOS device SDK that has MetalFX; only the simulator is built.

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
after it is this runbook and its index line, and review round 1: the two checks, and imgdecode.c, which changes
the visionOS objects only (the run was not repeated for it; the round needed no simulator run).

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
  with `simctl` and the plist has neither key (`tests/check-visionos.sh` refuses `UIFileSharingEnabled` until slice 2a).
  The missing-data text, `Sys_MissingDataMessage` in src/backends/unix/system.c, names "On My iPhone (or iPad)".
- Resizing: the person can resize a visionOS window. TASK172's live resize (`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`
  and `SDL_EVENT_WINDOW_SAFE_AREA_CHANGED`, src/client/input/sdl3.c, and the frame following the window in
  ref_metal) is `TARGET_OS_IPHONE` and so compiled in, but this slice does not exercise it. The default window is
  SDL's 1280x720 points. `VID_PLATFORM` (src/client/menu/videomenu.c) is the iOS one, with the iOS display list.
- `scripts/ios-sim.sh`: `run --background` launches `com.apple.Preferences`, which may not exist on visionOS; its
  SDK-not-found and runtime-pick errors still say "iOS".
- The run's two app-output lines: SDL's `You need UIApplicationSupportsIndirectInputEvents in your Info.plist for
  mouse support` (the pointer input above) and UIKit's `Unbalanced calls to begin/end appearance transitions for
  <SDL_uikitviewcontroller>` (twice, as the map starts).
- A visionOS device build: its SDK may have MetalFX, and `rmb_metalfx.m`'s `@available` checks name no visionOS
  version (see the version checks above).

## Review round 1

No Blocker; two Importants and five Notes, all fixed on this branch.

- I1, `tests/check-visionos.sh` let three iPhone-only sites back in: the reviewer's mutant (`TARGET_OS_IOS` made
  `defined(TARGET_OS_IOS)` at the `in_touch` default, the keyboard follow-up and the hint site, and the hint value
  reworded) passed both checks. Now (e) pins `sdl3.o` (no `SDL_RETURN_KEY_HIDES_IME`, `nm -u` no
  `_Touch_TextInput`) and `glimp_sdl3.o` (neither hint name), each with a positive control on the iOS simulator's
  object, and `tests/check-tvos.sh` (d) refuses `defined(TARGET_OS_IOS)` alone, `#ifdef` and a `!` before the name
  (TASK23's open note). The reviewer's mutant, applied to a copy of the tree, now fails both: check-visionos names
  the four sites, check-tvos (d) sites 2 to 4.
- I2, the image decoder: see the version checks above. Both checks pin `nm -u imgdecode.o` to
  `_kCGImageSourceAllowableTypes`; with the old imgdecode.c in the copy, check-visionos fails on both objects.
- N1: `grep` and `nm` errors, and a missing or unreadable object, refuse (a mode 000 object is a mutant). N2: the
  plist's forbidden keys are decided by `plutil -extract <key> xml1`, so `<key>UIFile&#83;haringEnabled</key>` is
  refused (a mutant; it passes `plutil -lint`). N3: `object_list` sets `list_root`, not the build's `root`. N4: the
  run's two app-output lines stay listed for slice 2. N5: this section.

The mutants each check now runs first, besides slice 1's: in check-visionos, an object holding each of the four
pinned strings, one needing `_Touch_TextInput`, one not needing `_kCGImageSourceAllowableTypes`, an object without
a pinned string or symbol for each positive control, a missing and an unreadable object for each of the four
assertions, the character-reference plist; in check-tvos, for each of the four sites `defined(TARGET_OS_IOS)`,
`defined TARGET_OS_IOS`, `#ifdef TARGET_OS_IOS` (two spellings), `!TARGET_OS_IOS` and `! (TARGET_OS_IOS)`, an
object not needing the ImageIO key, a missing and an unreadable one, and the character-reference plist.

The `make -n -B` of `ios_objects` for the iOS simulator, the iPhone, the tvOS and the visionOS simulators, and of
the default macOS goal, is identical before (`5bcecc78`) and after this round, and so are their object lists
(175 objects each for the four SDKs, 477 for macOS): the round changes no build.

## Slice 2a: the game-data path, and a controller (the run is blocked)

Slice 2a is a controller on the visionOS simulator, through the tvOS lane's probe (TASK23 slice 2, TASK182), and
the game-data path. Its run did not start the app (below), so the controller is not shown yet.

### The game-data path: the Files app, as on the iPhone

visionOS takes the iPhone's path. `stuff/ios/Info-visionos.plist.in` now has `UIFileSharingEnabled` and
`LSSupportsOpeningDocumentsInPlace`, both true, as `stuff/ios/Info.plist.in` has: with both, the Files app should show the
app's `Documents`, where the engine finds `baseq2` with no option. The evidence:

- Apple's documentation gives both keys from visionOS 1.0 (`UIFileSharingEnabled`: iOS and iPadOS 3.2, tvOS 9.0,
  visionOS 1.0, watchOS 2.0; `LSSupportsOpeningDocumentsInPlace`: iOS and iPadOS 2.0, visionOS 1.0), and neither
  is marked deprecated or unavailable there.
- The visionOS 27.2 simulator runtime has the Files app (`Applications/Files.app`, `com.apple.DocumentsApp`) and
  its DocumentManager and FileProvider frameworks; DocumentManager's English strings hold `On My %@` and `On My
  Apple Vision Pro`, the name of the place a person puts the paks.
- `tests/check-visionos.sh` (c) requires both keys true instead of refusing `UIFileSharingEnabled`, with a
  mutant for each key removed and for each set false (each refused by name); the character-reference mutant
  now spells `UIStatusBarHidden`. The check fails on the template without the keys (`UIFileSharingEnabled is
  (none), not true`) and passes with them.

Not shown in a run yet: that the app reads paks placed in its `Documents/baseq2`. Slice 1's run did that with
`simctl` copies into the same container, before the keys; with the keys no start of the app reached the engine.

### The run: the app does not start

Every simulator start of slice 2a, on the same host and runtime as slice 1 (Xcode 27.2, visionOS 27.2, Apple
Vision Pro), stalled the same way: `scripts/ios-sim.sh run` built, made and booted the device, copied the paks,
and launched the app, and then the launch client relayed nothing at all (an empty console log, not even Metal's
`Metal API Validation Enabled`, which slice 1's first line was), the app did not quit by itself, `simctl
terminate` timed out after 30 s and a `simctl get_app_container` of the device hung for minutes. Each device was
shut down and deleted, and each run printed `launcher: no crash` (SurfBoard did not crash). The four runs:

- the probe build with the keys (the pad's script, then the menu walk): no output in 600 s;
- the plain build with the keys, slice 1's kind of start (`+menu_main`, 300 frames, `+quit`): no output in 240 s;
- the probe build without the keys (the template before this slice): no output in 300 s;
- the plain build with the keys again: the same.

So the keys are not the cause: the build without them stalls too. The host's screen was locked during these runs
(`CGSSessionScreenIsLocked` true in `ioreg`); whether slice 1's run had it unlocked is not recorded. A locked
screen stalling a visionOS launch is a guess, not shown. The next run should check, with the screen unlocked,
whether the process starts at all (`simctl launch` without `--console-pty`, then the device's `launchctl list`).

### The controller: prepared, not run

The probe's script, as the tvOS lane's (TASK182): after 60 frames, `in_probe pads`, attach the probe's pad, wait
150 updates, `in_probe pads` again; `menu_main`, then the pad's d-pad down, down, up, up, A (the game screen), down,
B (back), A, down, up, with `menu_probe state` after each; A on `easy` (a new game, `base1`); `give all`; Y with
`in_probe expect gun weapnext`; the right trigger at 32767 with `in_probe expect ammo attack` (`+attack`), released
at -32768; the sticks; start (the game menu during play), down, B. The same script with `+set in_initjoy 2`, in
case the visionOS simulator has a `Gamepad` of its own as the tvOS simulator does (TASK182: `IN_IsTVSimulatorPad`
is `TARGET_OS_TV` only); then `menu_probe walk`. No source was changed for the controller: no run has shown a
change is needed.

## Slice 2b: the app starts, a controller walks it, the paks come from Documents

Same host, Xcode, runtime and device type as slice 1 and 2a (visionOS 27.2 simulator, Apple Vision Pro). Three
runs, each on devices it made, shut down and deleted itself; each printed `launcher: no crash` or kept the same
SurfBoard PID from start to end, so SurfBoard did not crash. Measured on `a69c1016` (the controller runs) and
`6879f6d1` (the diagnosis; the commit after it changes only the missing-data text and the simulator-pad guard).

### The launch: it starts, and the locked screen was not the cause

The screen read unlocked when the slice began, but the lock key was set (`CGSSessionScreenIsLocked` in `ioreg`)
at the start of every run. The app started all the same:

- the diagnosis, on a device of its own: `simctl launch` without `--console-pty` printed `org.yquake2.quake2:
  80158`, and the device's `launchctl list` showed `80158 0 UIKitApplication:org.yquake2.quake2[825e][rb-legacy]`
  at each of four reads 10 s apart; the engine log (`qconsole.log` in the container) had `Metal device: Apple xrOS
  simulator GPU`, `Real display mode: 1280x720@90.00` and `Drawable size: 2560x1440`; `simctl terminate` ended it;
- every start of `scripts/ios-sim.sh run`, which launches with `--console-pty`, relayed the app's output
  (`Metal API Validation Enabled`) and ended through `quit` (`----------- shutting down ----------`, then
  `metal_stats: ... 0 failed command buffers`).

So a locked screen does not stall a visionOS launch, and neither does `--console-pty`. What stalled slice 2a's
four starts is not found: those devices are gone.

### The data path

The paks went into the container's `Documents/baseq2`, as a person puts them there through the Files app, and
the engine found them with no option. `+path` in the first start printed the search path:

```
Current search path:
<container>/Documents/baseq2/pak9.pak (1 files)
<container>/Documents/baseq2/pak2.pak (2 files)
<container>/Documents/baseq2/pak1.pak (279 files)
<container>/Documents/baseq2/pak0.pak (3307 files)
<container>/Documents/baseq2
```

with `Raw search paths:` the bundle and `<container>/Documents` (`<container>` is the device's
`data/Containers/Data/Application/<id>`; `pak9.pak` holds the probe's script, below).

### The controller

The probe build (`-DYQ2_INPUT_PROBE`) and the tvOS lane's script (TASK182, slice 2a's above), three starts: the
probe's pad with `in_initjoy 1`, the same with `in_initjoy 2`, then `menu_probe walk`.

- **The visionOS simulator has its own `Gamepad`.** About a second after the start it adds the device the tvOS
  simulator adds, with the same facts: `joystick 1, id 3, 'Gamepad', gamepad 1, virtual 0`, `vendor 0x05ac,
  product 0x0004, version 0x25fa, guid 0500a5d0ac05000004000000fa256d04, type 1, gamepad type 'standard', path
  '', buttons 15, axes 19, hats 0`. So the tvOS preference applies: `IN_IsTVSimulatorPad` and its three uses
  (src/client/input/sdl3.c) are now `TARGET_OS_TV || TARGET_OS_VISION`, and the skip line names the OS
  (`IN_SIMULATOR_OS`: "visionOS" there, "tvOS" on tvOS, the same text as before). The Siri Remote hint stays
  `TARGET_OS_TV`. As on tvOS, only the name `Gamepad` marks the simulator's pad.
- **The first run's script attached the pad too early.** `wait 60` was not past the start's controller
  countdown: the simulator's pad restarts it at 100 updates (`SDL_EVENT_JOYSTICK_ADDED` with no controller open),
  and `in_probe pad` refused (`pad not attached: the start's controller countdown is not over`), so nothing
  after it moved. The script now waits 250 updates first.
- **Green** (`in_initjoy 1`): `in_probe: pad attached`, `2 joysticks were found.`, `Skipping joystick 1, the
  visionOS simulator's own gamepad: another gamepad is connected.`, `Trying joystick 2, 'in_probe pad'`, and the
  pad list marks `joystick 2, id 4, 'in_probe pad', gamepad 1, virtual 1, the engine's gamepad, the probe's
  pad`. The pad's d-pad walks the main menu (`cursor 2 'm_main_game'`, down to `3 'm_main_multiplayer'` and `4
  'm_main_options'`, up to `2 'm_main_game'`), A opens the game screen (`depth 2, screen 'm_banner_game', cursor
  0 'easy'`), down moves in it (`1 'medium'`), B goes back (`depth 1`), A on a difficulty starts `base1` (` Outer
  Base`), Y fires `weapnext` (`expect gun weapnext: ok, 36 -> 64`), and start opens the game menu during play
  (`depth 1, screen 'm_main_plaque'`), down moves in it and B closes it (`key_dest 0, depth 0`).
- **`in_initjoy 2`**: the same lines, except that the skip line is not printed (the engine tries the second
  joystick first) and the pad is again the engine's gamepad.
- **The walk**: `menu_probe: walk ok: 22 screens, 1079 key presses, 0 failures`.
- **`+attack`**: in the second run the press reached the engine (`trigger axis 32767, TRIG_RIGHT down`) but
  `expect ammo attack: FAIL, unchanged at 100`, in both starts. The state line at the press read `gunframe 3`
  and `gunframe 2`: the shotgun Y had just selected was still being raised, which ignores the attack, and the
  script held the trigger 20 updates, a fifth of a second at 90 Hz. The third run, start 1 alone, waits 200
  updates after Y (`gunframe 22`, the shotgun up) and holds the trigger 60: `trigger axis 32767, TRIG_RIGHT down`,
  `expect ammo attack: ok, 100 -> 99`, then at the release (-32768) `trigger axis 0, TRIG_RIGHT up`. The
  probe's scripts are in a scratch `pak9.pak`, never committed.

### The missing-data text

`Sys_MissingDataMessage` (src/backends/unix/system.c) says `Files > On My Apple Vision Pro > Quake II >
baseq2` under `TARGET_OS_VISION`, the name the visionOS Files app gives the place (slice 2a's strings); the
iOS text is byte for byte as it was, and `tests/unit/test_filesystem.c` still pins it (the host build cannot
reach the visionOS branch, so no unit check is added). `tests/check-visionos.sh` (e) pins it in the compiled
object: every visionOS `system.o` holds `On My Apple Vision Pro` and not `On My iPhone (or iPad)`, and the iOS
simulator's `system.o` the other way round (`pin holds` is new: its control is that the iOS object lacks the
text). Both strings are in the mutant loop (an object holding each is refused by name, one without it fails the
control). Before the text it failed with `system.o holds "On My iPhone (or iPad)"` and `system.o does not hold
"On My Apple Vision Pro"`.

### The checks

On `a69c1016`, in the second run's hold:

- `check-visionos: ok (181 objects for the visionOS simulator, platform 12, the iOS simulator's list)`;
- `check-ios: ok (54 mutants refused; 188 smoke selftest cases; 65 device selftest cases; 181 objects for the
  simulator; 181 for a device; ios_objects)`;
- `check-tvos: ok (181 objects for the tvOS simulator, platform 8, the iOS simulator's list)`;
- `make test`: `yq2test: 444 tests, 0 failed`.

`tests/check-ios.sh` and `tests/check-tvos.sh` are unchanged. The guards add `TARGET_OS_VISION` beside
`TARGET_OS_TV` and nothing else, so the iOS and macOS objects of sdl3.c and system.c compile the same code.

### Round 2: the pad skip is pinned, and it is simulator-only

- The visionOS skip applies to the simulator only: its half of the guard in src/client/input/sdl3.c is now
  `(defined(TARGET_OS_VISION) && TARGET_OS_VISION && TARGET_OS_SIMULATOR)`, and tvOS keeps its existing behaviour
  on a device (`TARGET_OS_TV`, unchanged).
- `tests/check-visionos.sh` (e) pins it: `sdl3.o` holds `the visionOS simulator` (the skip line), with the iOS
  simulator's `sdl3.o` as the control, and the text is in the mutant loop. With the sdl3.c hunk reverted, the
  check fails on that pin.
- `pin` refuses a kind it does not know (`pin: unknown kind <kind>`) instead of skipping the visionOS half.

## Left for slice 2c

- Look-and-pinch menu navigation (a pinch reaches a UIKit app as an indirect touch or a pointer; SDL asks for
  `UIApplicationSupportsIndirectInputEvents`).
- Resizing beyond the default 1280x720-point window (TASK172's live resize is compiled in, not exercised).
- The on-screen keyboard (text input never starts: the keyboard follow-up is `TARGET_OS_IOS`).
