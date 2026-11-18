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

## Slice 2: controller and Siri Remote input, a focus-safe menu

Not in this slice: the game-data path (the options above stay the owner's decision; how data is found is
unchanged) and visionOS input (TASK175 slice 2).

### The bindings

No new binding was needed. `Key_DefaultPadBinds` (`src/client/cl_keyboard.c`, TASK150) already runs on every
`TARGET_OS_IPHONE` target, tvOS included, with the table of `Touch_DefaultBind` (`src/client/input/touch.c`):
on a fresh start (no `config.cfg`) each gamepad key that has no binding gets one, and nothing is replaced.

| Input | Key | Action |
|---|---|---|
| left stick | (axes) | move (`joy_layout` 0) |
| right stick | (axes) | turn and look |
| right trigger | `TRIG_RIGHT` | `+attack` |
| A / cross | `BTN_SOUTH` | `+moveup` (jump) |
| B / circle | `BTN_EAST` | `+movedown` (crouch) |
| Y / triangle, right shoulder | `BTN_NORTH`, `SHOULDR_RIGHT` | `weapnext` |
| left shoulder | `SHOULDR_LEFT` | `weapprev` |
| X / square | `BTN_WEST` | `invuse` |
| view (back) | `BTN_BACK` | `inven` |
| share (misc) | `BTN_MISC1` | `toggleconsole` |
| menu (start) | `K_ESCAPE` (in `IN_Update`) | the game menu |

In the menus the d-pad and the left stick are the arrows, A selects and B goes back (`Key_GetMenuKey`). macOS
and a player with a config are untouched: the function is empty outside `TARGET_OS_IPHONE`, and a start with a
`config.cfg` binds nothing (`Touch_WantBind`, unit-tested in `tests/unit/test_touch.c`).

### The Siri Remote

SDL 3 offers both (`third_party/SDL3/src/joystick/apple/SDL_mfijoystick.m`, `SDL_uikitview.m`). With
`SDL_HINT_TV_REMOTE_AS_JOYSTICK` on, its default, the remote is a gamepad: the touch surface is two axes and its
buttons are A (the click), X (Play/Pause) and the pause button (Menu), which SDL's generated mapping
(`SDL_gamepad.c`, `a:b0,b:b1,x:b2`) makes south, east and west. Menu would then be the west face button, which
the menu does not read and the game binds to `invuse`, and Play/Pause would be back. While the remote is open as
a gamepad SDL also drops its key presses. So on tvOS `IN_Init` sets the hint to `0` (`TARGET_OS_TV` only, before
any gamepad init): the remote is keys, the presses `scancodeFromPress` maps. A click on the up, down, left or right
edge gives `K_UPARROW`/`K_DOWNARROW`/`K_LEFTARROW`/`K_RIGHTARROW`, the middle click gives `K_ENTER` (select),
Menu gives `K_ESCAPE` (back in a menu, the game menu during play) and Play/Pause gives `K_PAUSE`. A controller is
still a gamepad. `tests/check-tvos.sh` (e) pins the hint in the tvOS `sdl3.o` (the iOS simulator's object lacks
it). Swipes on the touch surface do not move the menu: tvOS sends them as gestures, not presses.

### Text on tvOS

The address book (typing a server address) is greyed on tvOS with its reason, "typing an address needs a
keyboard, which tvOS does not have" (`TARGET_OS_TV`, `src/client/menu/menu.c`); a server found on the local
network is still joined from the list. The player name and the server's host name are fields with a default
that need no typing. The console still opens from `BTN_MISC1` but takes no text on tvOS (slice 1's note).

### Input without hands, and the menu walk

`simctl` cannot press a button, so a test build (`CFLAGS=... -DYQ2_INPUT_PROBE`; a release has neither command)
adds `in_probe` (`src/client/input/sdl3.c`: an SDL virtual gamepad, its buttons and axes, and pushed key events,
the remote as keys) and `menu_probe` (`src/client/menu/menu.c`: `state`, and `walk`, which visits every screen
reachable from the main menu with only the six inputs through `M_Keydown`, requires every selectable item to be
reached with down and with up and back to leave every screen it opened, then drops the commands the selects
queued and quits). The engine takes at most 50 command-line words, so the probe's script was a `probe.cfg` in a
scratch `pak9.pak` next to links to the paks, never committed.

Measured on commit `eba0025f`, Apple TV 4K (4th generation), tvOS 27.2 simulator, `ref_metal`:

- The simulator has no game controller and no remote as a gamepad: "0 joysticks were found." at start.
- The remote as keys works end to end: during play `in_probe key Escape` opened the main menu (key_dest 3,
  depth 1); Down, Down, Up moved the cursor game, multiplayer, options, multiplayer; Return opened the
  multiplayer screen (depth 2); Down moved in it; Escape went back to the main menu (depth 1); Down and Return
  opened the options screen, whose frame the engine wrote (`q2_0000.tga`, kept in the local artifacts folder).
- The virtual gamepad did not reach the engine: "in_probe: pad attached", but no "Trying joystick" line followed,
  so its sticks did not move or turn the player and its start button did not open the menu. It was attached at
  the first frame, while `first_init` still ignores SDL_EVENT_JOYSTICK_ADDED; attaching it after the map loads
  is the next try. The binding of each pressed key was printed from the live table: `BTN_SOUTH` `+moveup`,
  `BTN_EAST` `+movedown`, `BTN_NORTH` `weapnext`, start as `ESCAPE`. The table itself is unit-tested
  (`tests/unit/test_touch.c`).
- The walk does not pass yet: `menu_probe: walk FAIL: 6 screens, 291 key presses, 33 failures`. Main, game, load
  game, credits and multiplayer pass; on the join server screen down and up never reach the items after the greyed
  address book (refresh and the eight server slots) and back does not leave it, so the walk cannot rebuild its path
  and the main menu's options, video and quit entries are not visited. Not found yet: whether the screen
  swallows the keys only inside one command (the server search draws a timed popup and pings at open) or a
  person sees it too.
- The mutant (the credits screen's back removed, `M_Credits_Key`) is caught: `FAIL: '(unnamed)' is not left with
  back` (the credits screen has no banner), 34 failures against 33.

#### Round 2: the two causes

The join server screen did not trap the cursor, and the greyed address book was not the cause: a greyed item is
reached by up and down on purpose (`Menu_AdjustCursor` in `qmenu.c` skips only separators and `QMF_INACTIVE`), so
that its reason shows in the status bar; select does nothing on it (`Menu_SelectItem`). The walk swallowed its own
keys. It runs from the command line, before the first frame, when `cls.realtime` is still the value `CL_InitLocal`
took, the clock's first millisecond: `Sys_Milliseconds` starts at 1. `JoinServer_MenuInit` calls
`SearchLocalGames`, which starts a popup with a 2 second end (`Menu_StartPopup`). The walk closed it before each key
with `Menu_ClosePopup`, `(pup)->endtime = cls.realtime - 1` (`qmenu.h`), which is 0 at that time, and
`Menu_PopupActive` reads an end of 0 or less as a popup without an end. So every key then reached
`Default_MenuKey` with the popup still open, and its first lines only closed it again and returned: down, up and
back were all swallowed on that screen, and the cursor stayed on its first item, the address book. A person never
hits it, on tvOS or macOS: keys arrive in frames, and each frame sets `cls.realtime` to the milliseconds since the
start (`CL_Frame`), far past 1, so a close leaves an end in the past. The walk now takes a popup away by clearing its
text (`Walk_Key`), and prints the time it runs at; `menu.c` and `qmenu.c` are otherwise unchanged.

The virtual gamepad was attached by the first line of the probe's script, which runs inside the engine's start,
before any frame. `IN_Update` counts down the start's first 30 updates with `first_init` set (`sdl3.c`,
`updates_countdown`, `REASON_CONTROLLERINIT`); a device added in that window restarts the count, and when it runs out
with `first_init` set, nothing is opened, since the devices present at the start were already opened by
`IN_Controller_Init` in `IN_Init`. The pad was taken as one of those. A person who plugs a controller in after the
start gets it opened 100 updates later. `in_probe pad` now refuses while `first_init` is set, and the probe attaches
the pad after the map has loaded, as a person plugs one in.

#### Round 2: measured

Commit `0c0c3290`, the same simulator and data (links to the paks and a scratch `pak9.pak` with the probe's
script, never committed):

- The walk passes: `menu_probe: walk at cls.realtime 1`, then `menu_probe: walk ok: 20 screens, 891 key presses, 0
  failures`. The screens, by banner or first item as the walk prints them: `m_main_plaque`, `m_banner_game`,
  `m_banner_load_game`, the credits (`(unnamed)`), `m_banner_multiplayer`, `m_banner_join_server`,
  `m_banner_start_server`, `falling damage`, `name`, `Download Options`, `score`, `m_banner_options`, `look
  sensitivity`, `layout`, `attack`, `next weapon`, `attack`, `factor` and `m_banner_video` twice. On join server down and up
  reach the address book (greyed), refresh and the eight slots, and back leaves it. Quit has no screen on tvOS: the
  entry is greyed on every `TARGET_OS_IPHONE` target since TASK150 ("close Quake II from the app switcher"), so the
  walk reaches it with down and up and select does nothing on it; the main menu is then left with back.
- The walk's mutants fail it. The credits screen's back removed: `FAIL: '(unnamed)' is not left with back`, `walk
  FAIL: 20 screens, 890 key presses, 1 failures`. `Walk_Key` closing the popup with `Menu_ClosePopup` again (round
  1's walk): the 33 failures of round 1, `walk FAIL: 6 screens, 289 key presses, 33 failures`, at `cls.realtime 1`.
- The pad (commit `29e876d5`, `in_probe pads` lists the joysticks): attached before the first frame it is refused
  (`pad not attached: the start's controller countdown is not over`); attached after the map, the engine looks again
  ("- Gamepad init attempt -", "2 joysticks were found."). The second joystick is not the probe's: from about the
  first second the simulator has a joystick of its own, `'Gamepad', gamepad 1, virtual 0`, which appears inside the
  start's countdown and so is not opened then. `IN_Controller_Init` opens the first gamepad from `in_initjoy`, which
  is that one ("Trying joystick 1, 'Gamepad'"), so with `in_initjoy` 1 the probe's sticks and start move nothing.
  With `+set in_initjoy 2` the engine opens the probe's pad ("Trying joystick 2, 'in_probe pad'", "Enabled as
  Gamepad."), and then the left stick moves the player (origin 128 -320 24 to -63 -126 24), the right stick turns it
  (yaw 0 to -155), and start opens the game menu (key_dest 3, depth 1, `m_main_plaque`) and closes it again (key_dest
  0). The right trigger is not shown firing: the gun frame stays 19 (the blaster's idle pause) 3 and 23 waits after
  the trigger is pressed and 10 after it is let go. Not found yet whether the press reaches `+attack`. What the simulator's own
  `'Gamepad'` is, and whether a real Apple TV has one, is not known either.
- The walk prints each greyed item with its reason. On join server the address book has its reason; the items of
  the four key binding screens (`score`, `attack`, `next weapon`, `attack`) are greyed with no reason of their own
  on tvOS (an item's status bar is empty). This is TASK150's state, not changed here.
- The default macOS and iOS simulator objects of both files are byte-identical to main's, on `0c0c3290` and again on
  `cc9de5bd`: `menu.o` (164544 and
  164528 bytes) and `sdl3.o` (56872 and 58824 bytes). Both probes are inside `#ifdef YQ2_INPUT_PROBE`, and no menu code
  outside it changed.

### check-tvos

`tests/check-tvos.sh` (e) now also pins `sdl3.o` (no `SDL_RETURN_KEY_HIDES_IME`, no need of `_Touch_TextInput`,
and `SDL_TV_REMOTE_AS_JOYSTICK` held) and `glimp_sdl3.o` (no `SDL_ORIENTATIONS`, no
`SDL_IOS_HIDE_HOME_INDICATOR`), with the iOS simulator's object of each file as the positive control (it holds,
needs or lacks the opposite), and synthetic objects as the mutants of each assertion. (d) takes only a bare true
test: a chain of `&&` of names, `!names` and `defined()`s with `TARGET_OS_IOS` alone among them; seven more
mutants per site (`== 0`, `!= 1`, `- 1`, `0 == ( )`, `||`, `?:`, `!( )` around the chain) are refused, and the
name in double parentheses still passes. A tree with the remote's hint removed fails with "(e) the Siri Remote is
not set to keys in the tvOS build".

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
