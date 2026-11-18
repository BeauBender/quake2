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

Differences from the iPhone build, all behind the SDK or `TARGET_OS_TV`:

- `Makefile`: the SDK's target and `IOS_PLATFORM` (AppleTVSimulator), the plist template
  `stuff/ios/Info-tvos.plist.in` (`UIDeviceFamily` 3, no orientations, no status bar key, no file sharing key),
  the ad hoc `codesign`. MetalFX is weak-linked only if the SDK has it, so the tvOS simulator build links none.
- `third_party/sdl3.make`: the framework list of the iOS mode minus CoreMotion, which the tvOS SDK does not have.
  The SDL sources are the same; SDL's iOS files guard their own tvOS differences.
- `scripts/ios-sim.sh`: `IOS_SIM_SDK=appletvsimulator` passes that SDK to `make ios`, picks a tvOS runtime and the
  device type "Apple TV 4K (4th generation)" (`IOS_SIM_TYPE` and `IOS_SIM_RUNTIME` still override). The default
  is as it was.
- `tests/check-tvos.sh` (in the gate): the objects build for platform 8 (TVOSSIMULATOR) at minos 17.0, the object
  list equals the iOS simulator's, the plist is the Apple TV's; it first refuses mutants of each. It skips with one
  line without the tvOS SDK.

### iOS-only code guarded

`TARGET_OS_IPHONE` is 1 on tvOS, so the iOS blocks compile; only what is wrong at run time was guarded with
`!TARGET_OS_TV` (an iOS-only API, `TARGET_OS_IOS`, must never sit under `TARGET_OS_IPHONE` alone):

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

## Game data on tvOS: options for slice 2

There is no Files app, and an app has no persistent local storage: only `Library/Caches` (purgeable) and the
bundle. `UIFileSharingEnabled` and `Documents` as a place a person puts paks do not exist. Options:

1. In the bundle (as `IOS_BUNDLE_DATA` does): works for a developer build, never for a shipped one (the paks are
   the owner's licensed files, and the bundle's size is limited).
2. Download once into `Library/Caches` from a server the person runs (the engine's HTTP download path exists), at
   the cost that tvOS may purge it: the game must handle the files vanishing.
3. A shared container or iCloud key-value or CloudKit storage: the paks are too large for key-value; CloudKit
   assets are possible but need an entitlement.
4. Streaming from a Mac or NAS on the network at start: needs a server and a protocol, the largest change.
5. On-demand resources tagged to the app: a store mechanism, not checked here.

The simulator does have a writable `Documents`, which is why this slice can copy the paks there with `simctl`; a
device will not. This is the decision slice 2 must take with the owner.
