# TASK23: the tvOS simulator build boots to the main menu and loads base1 (slice 1)

Slice 1 of TASK23 (issue #30): the tvOS target builds for the Apple TV simulator from the iOS code, and the
game reaches the main menu and loads `base1` in the simulator on `ref_metal`. Not in this slice, and left to
slice 2: controller and Siri Remote input, a focus-safe menu, and the game-data path decision. visionOS is its
own card (TASK175).

## The build

`make ios IOS_SDK=appletvsimulator` is the iOS mode of the Makefile with a third SDK:
`-target arm64-apple-tvos17.0-simulator` against `xcrun --sdk appletvsimulator`, the same object list as the
iOS simulator build (STATIC_MODULES=1, `ref_metal` linked in, the vendored SDL3 built for the SDK into
`IOS_ROOT/sdl3`). `make ios_objects IOS_SDK=appletvsimulator` builds the objects and the plist only.

Differences from the iPhone build, all behind the SDK or `TARGET_OS_IOS`:

- `Makefile`: the SDK's target and `IOS_PLATFORM` (AppleTVSimulator), the plist template
  `stuff/ios/Info-tvos.plist.in` (`UIDeviceFamily` 3, no orientations, no status bar key, no file sharing key),
  the ad hoc `codesign`. MetalFX is weak-linked only if the SDK has it, so the tvOS simulator build links none.
- `third_party/sdl3.make`: the framework list of the iOS mode minus CoreMotion, which the tvOS SDK does not have.
  The SDL sources are the same; SDL's iOS files guard their own tvOS differences.
- `scripts/ios-sim.sh`: `IOS_SIM_SDK=appletvsimulator` passes that SDK to `make ios`, picks a tvOS runtime and the
  device type "Apple TV 4K (4th generation)" (`IOS_SIM_TYPE` and `IOS_SIM_RUNTIME` still override). The default
  is as it was. Any other `IOS_SIM_SDK` than empty, `iphonesimulator` or `appletvsimulator` stops the run before
  `make` with one line naming those. Its selftest has a tvOS runtime and the Apple TV type in the fake lists.
- `tests/check-tvos.sh` (in the gate): every iPhone and iPad only site below is inside an `#if` that names
  `TARGET_OS_IOS` (from the source, one reverted-guard mutant per site, run on any host); the objects build for
  platform 8 (TVOSSIMULATOR) at minos 17.0; the object list equals the iOS simulator's; the plist is the Apple TV's;
  the compiled `touch.o` has `Touch_Supported` as `mov w0, #0x0; ret` (`otool -tV`). It first refuses mutants of
  each. The build checks skip with one line without the tvOS SDK.

### iOS-only code guarded

`TARGET_OS_IPHONE` is 1 on tvOS and visionOS, so the iOS blocks compile; only what is wrong at run time is
guarded, by `TARGET_OS_IOS` (iPhone and iPad, and Mac Catalyst, which `Touch_Supported` still excludes by name).
`TARGET_OS_IPHONE && !TARGET_OS_TV`, round 0's form, is still true on visionOS (TASK175):

- `Touch_Supported` (src/client/input/touch.c): an Apple TV has no touch screen, so no overlay.
- `in_touch` defaults to 0 and the on-screen keyboard follow-up in `IN_Update` (src/client/input/sdl3.c) is off:
  there is no touch, and tvOS has no keyboard for the console in this slice.
- `SDL_HINT_ORIENTATIONS` and `SDL_HINT_IOS_HIDE_HOME_INDICATOR` (src/client/vid/glimp_sdl3.c): an Apple TV has one
  orientation and no home indicator.

Kept as they are, because they are right on tvOS: the safe area (the TV's overscan inset, 160 by 120 on the
simulator at 3840x2160), the sandboxed home (`sys_sandbox_home`), the app event watch (background and resume),
the SDL main wrapper, the audio category and the quit entry greyed (tvOS has no quit either). Not reached in
this slice and still to guard when slice 2 touches them: haptics through SDL's gamepad rumble, and anything
UIKit-only TASK172 adds (`ios_orientation`'s `UIWindowSceneGeometryPreferencesIOS` is `TARGET_OS_IOS`).

Also left to slice 2, found in review round 1:

- the missing-data text, `Sys_MissingDataMessage` in `src/backends/unix/system.c`, still names "Files > On My
  iPhone (or iPad)", which an Apple TV does not have;
- `scripts/ios-sim.sh run --background` launches `com.apple.Preferences` to send the app to the background, and
  that app may not exist on tvOS;
- `Touch_DefaultBind` (`src/client/cl_keyboard.c`) and `VID_PLATFORM` (`src/client/menu/videomenu.c`) still run on
  tvOS: the touch default binds and the iOS display list;
- `ios-sim.sh`'s SDK-not-found and runtime-pick errors still say "iOS" for the tvOS run;
- a hardware keyboard cannot type into the console on tvOS, because text input never starts (the on-screen
  keyboard follow-up that starts it is iPhone and iPad only).

## The run

```sh
IOS_SIM_SDK=appletvsimulator scripts/ios-sim.sh run --data <data dir> \
    --launch "+set vid_renderer metal +set logfile 2 +menu_main <+wait x 200> +screenshot tga <+wait x 300> +quit" \
    --launch "+set vid_renderer metal +set logfile 2 +map base1 <+wait x 600> +screenshot tga <+wait x 300> +quit" \
    --out <a local artifacts folder, never committed>/run2
```

The script builds into `--out`/build, creates a throwaway Apple TV 4K (4th generation) device, copies `baseq2/*.pak`
into the app's `Documents/baseq2` (`simctl get_app_container <udid> <bundle> data`), launches the app with Metal's
validation layer (`SIMCTL_CHILD_MTL_DEBUG_LAYER=1`) and deletes the device. The data is never committed or bundled.
Host: Xcode 27.2 (27B52), tvOS 27.2 simulator runtime (24K150), SDK appletvsimulator27.2.

The evidence of the last run is in the report that came with the card (log lines of `engine.<n>.log` and
`console.<n>.log`, the engine's own `q2_*.tga` frames in `frames.<n>/`).

The measured facts here (the 160 by 120 safe area, the host and runtime line, the frame counts of the two starts)
are from round 0's runs, on the tree of commit `fb729727`. Round 1 did not run the simulator again. What changed
in `src` and `Makefile` after it (`git diff --stat fb729727 HEAD -- src Makefile`): the four guards below went
from `TARGET_OS_IPHONE && !TARGET_OS_TV` to `TARGET_OS_IOS`, which is the same on tvOS (each block is compiled
out either way, and `check-tvos.sh` (e) shows it for `Touch_Supported`); the Makefile's plist template variable
(`:=`, quoted); and main's merge: FIX19's 64-bit image sizes and PCX, WAL, M8 and M32 header checks in the
loaders (`src/client/refresh/files/`, built for tvOS too, with no platform branch; `refresh/soft/sw_image.c`,
not in the tvOS build) and its unit-test dependency line in the Makefile.

## Game data on tvOS: options for slice 2

There is no Files app, and an app has no persistent local storage: only `Library/Caches` (purgeable) and the
bundle. `UIFileSharingEnabled` and `Documents` as a place a person puts paks do not exist. Options:

1. In the bundle (as `IOS_BUNDLE_DATA` does): works for a developer build, never for a shipped one (the paks are
   the owner's licensed files, and the bundle's size is limited).
2. Download once into `Library/Caches` from a server the person runs. The engine has no HTTP client: HTTP
   downloads and libcurl were removed to shrink what has to be trusted (README, *docs/plans/trust-boundary.md*),
   so this means adding one back, a new trust surface the repo removed on purpose. The only download left is the
   UDP one from a game server (`cl_download.c`), which writes to `FS_Gamedir()`, not `Library/Caches`. And tvOS
   may purge the cache: the game must handle the files vanishing.
3. A shared container or iCloud key-value or CloudKit storage: the paks are too large for key-value; CloudKit
   assets are possible but need an entitlement.
4. Streaming from a Mac or NAS on the network at start: needs a server and a protocol, the largest change.
5. On-demand resources tagged to the app: a store mechanism, not checked here.

The simulator does have a writable `Documents`, which is why this slice can copy the paks there with `simctl`; a
device will not. This is the decision slice 2 must take with the owner.

## Review round 1

The panel found no Blocker. Four Importants and the Notes, each fixed:

- Correctness I1: no test failed when a tvOS guard was reverted, and `TARGET_OS_IPHONE && !TARGET_OS_TV` is still
  true on visionOS. The four sites are now `TARGET_OS_IOS` (`Touch_Supported` keeps `!TARGET_OS_MACCATALYST`).
  `tests/check-tvos.sh` (d) lists each site and requires `TARGET_OS_IOS` on its `#if`, with one reverted-guard
  mutant per site; (e) reads the compiled tvOS `touch.o` with `otool -tV` and requires `Touch_Supported` to be
  `mov w0, #0x0; ret`, refusing a 1, a variable and a missing function.
- Correctness I2: the `ios-sim.sh` selftest's fake `xcrun` has a tvOS runtime and the Apple TV 4K (4th
  generation) type next to the iOS ones; cases cover the tvOS pick and `IOS_SDK=appletvsimulator` reaching make,
  the default staying iPhone on the same lists, no tvOS runtime, and two refused values (`iphoneos`, shell text),
  which now stop the run before make with one line.
- Security I1: option 2 of the game-data options no longer says the engine has an HTTP download path.
- Spec I1: the Apple TV section of `doc/020_installation.md` follows the iPhone smoke and `check-ios.sh`
  paragraphs, and its `run` line has the required `--out DIR`.
- Notes: `IOS_PLIST_IN` is set with `:=` in each SDK branch and quoted in the recipe; `check-tvos.sh`'s header
  says what it checks (the comparison with the base is the review's `make -n` before and after), and its
  forbidden-key loop, device-family count and minimum OS line each have a mutant; the measured facts name their
  commit; the slice 2 list has the five items above.
