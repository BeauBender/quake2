# Closing TASK22: the simulator on the build host, the device by the owner

The umbrella card's acceptance list (TASK22, #29, the iPhone and iPad
port), checked after its children C1 to C7: TASK146 (the simulator build),
TASK147 (safe area and frame pacing), TASK148 (lifecycle), TASK149 (game
data from the Files app), TASK150 (touch input core), TASK151 (the touch
overlay) and TASK152 (the device build and the signing seam). Each line is
a command and the result to record.

The simulator lines ran on the build VM (macOS arm64, Xcode with the iOS
27.2 SDK and simulator runtime) at the head of TASK152's branch, through
`scripts/ios-sim.sh run --data GAMEDIR`, which copies the paks of the
owner's Quake II into the app's *Documents/baseq2/* on a throwaway
simulator device and deletes the device at the end. The game data went
nowhere else. The engine's own logs (`engine.<n>.log`), the listings of
*Documents/baseq2/* and the simulator's screenshots (`screen.<n>.png`)
are frames and logs of id Software's data: they stay in the owner's local
artifacts folder and are not attached anywhere. What is recorded below is
read from them.

## Simulator, on the build host

**1. The main menu with `ref_metal` (C1, C4).** Done.

```sh
scripts/ios-sim.sh run --out DIR --data GAMEDIR --launch "+menu_main" --launch "+map base1" --shot 60
```

Record from `engine.1.log`: the renderer, the Metal device and the touch
layer; from `screen.1.png`: the menu. Result on an iPhone 17 (iOS 27.2):
`Using the linked-in renderer: ref_metal (linked in)`, `Metal device:
Apple iOS simulator GPU`, `ref_metal: the device floor (apple5) is not
checked in the simulator`, `Successfully loaded ref_metal (linked in) as
rendering backend`, `Touch controls attached.`, `Gamepad in use: 'Touch
controls'`. The screenshot shows the main menu (Game, Multiplayer,
Options, Video, Quit) over the console, with the menu-mode touch pad: the
d-pad at the lower left, OK and Back at the lower right, Con at the top.
No game data in the bundle: the paks were found in *Documents/baseq2/*
(`documents.1.list`: `pak0.pak`, `pak1.pak`, `pak2.pak` and the
`players/`, `video/` and `music/` files), with no option given.

**2. `base1` with `ref_metal` (C1, C4).** Done, the second start of the
same run. Record from `engine.2.log` and `screen.2.png`. Result: `Game
module: linked into the executable`, the server's initialization for
`base1` and its title, `Outer Base`; the screenshot is the first room of
`base1` with the blaster, the status bar (health 100) and the in-game
touch overlay: the two stick rings, Use, Next, Jump, Duck and Fire at the
lower right, Menu and Con at the top.

**3. Safe area on an iPhone and an iPad type (C2).** Done on both. Record the `ref_metal: safe area:` and `Drawable
size:` lines and look at the screenshot's margins. iPhone 17, landscape:
`window 2622x1206 pixels, insets left 186 top 0 right 186 bottom 60`,
`Drawable size: 2250x1146`, the window less the insets; the screenshots
are black outside that rectangle (the sensor housing's side and the home
indicator's strip) and the picture and the touch controls inside it.

iPad Pro 13-inch (M5), the same run with one start:

```sh
IOS_SIM_TYPE="iPad Pro 13-inch (M5)" scripts/ios-sim.sh run --out DIR --data GAMEDIR --launch "+map base1" --shot 60
```

Result: `window 2752x2064 pixels, insets left 0 top 0 right 0 bottom
40`, `Drawable size: 2752x2024`, and `base1` (`Outer Base`) with the
touch overlay. The simulator's screenshot of an iPad is taken in the
screen's portrait orientation, so the landscape game is turned in it; its
40-pixel black strip is the bottom inset (the home indicator), and the
picture, the status bar and the controls are inside the rest.

**4. Background and return (C3).** Done.

```sh
IOS_SIM_BACKGROUND_AFTER=45 scripts/ios-sim.sh run --out DIR --data GAMEDIR --launch "+map base1" --background 20 --shot 10
```

45 s after the start, in `base1`, the script opens another app (Settings),
waits 20 s and launches the app again, and fails if its process did not
live through it (a new process would be a restart, not a return). Record
the `App:` lines of `engine.1.log`, the screenshot 10 s after the return
and errors in `oslog.1.txt`. Result on an iPhone 17: the run passed with
the same process; in order `App: entering the background, no frames are
drawn.`, `App: the configuration is written.` and `App: back in the
foreground.` (each of the first and the last twice, as UIKit's app and
scene callbacks both report them). The screenshot is the same view of
`base1` as before the background, the game shown `PAUSED`, with the touch
overlay. The system's log of the app holds 22 error lines, all from
UIKit and the audio frameworks (this simulator has no audio output; the
engine logs `SDL_OpenAudio() failed: Device not found` at every start),
none from Metal or a command buffer.

**5. Touch moves and fires (C5, C6).** Left for the owner. `simctl` has
no command that sends a touch, so nothing on this host can drive the
overlay unattended, and building a touch injector is outside TASK22. What
the build host shows: the touch layer attaches as the gamepad in use
(items 1 and 2), the overlay is drawn in both modes (the screenshots), and
the unit suite checks the rules under it (`tests/unit/`: the zones, the
finger tracker and the virtual gamepad of TASK150, the menu mode and the
overlay's layout of TASK151). The owner's line is below.

## The owner's lines

**6. Touch in the Simulator.** Run line 1's command with `--launch "+map
base1"` and no `--shot`; the app runs until it is quit. In the Simulator
window a click is a touch and Option with a click is two touches. Drag on
the left half: the player moves; drag on the right half: the view turns;
click Fire: the blaster fires; both at once (Option): moving while firing.
Record each as works or not.

**7. A controller in the Simulator.** Connect a game controller to the
Mac, then run line 6. Record: the controller moves the player and fires,
the touch overlay is no longer drawn while it is connected, and it comes
back when the controller is disconnected.

**8. On a device.** What a device needs, and what each command does, is
in `doc/020_installation.md`, "Running on an iPhone or iPad". With a
development profile for `org.yquake2.quake2` (or the `IOS_BUNDLE_ID` the
build is given) that lists the device:

```sh
make ios IOS_SDK=iphoneos
scripts/ios-device.sh sign --identity "Apple Development: NAME (ID)" --profile PROFILE.mobileprovision
scripts/ios-device.sh install --device DEVICE
scripts/ios-device.sh launch --device DEVICE
```

`DEVICE` is the identifier `xcrun devicectl list devices` shows. Record
for each step: it passed, or its last line.

- The first start shows the missing-data alert naming
  *On My iPhone > Quake II > baseq2* and *pak0.pak*. Copy the paks into
  that folder with the Files app (or Finder's file sharing), start the app
  again: record that it reaches the main menu with no option.
- `scripts/ios-device.sh launch --device DEVICE +map base1`: record that
  `base1` loads and the console names the device's Metal GPU families
  (on a device `ref_metal` checks its apple5 floor, which every iOS 17
  device meets).
- Touch with two fingers: move with one, turn or fire with the other.
- A controller (Bluetooth): it drives the player and the overlay hides.
- Background and return: leave the app with the Home gesture during
  `base1`, come back after a few seconds; record that the game continues
  where it was (no restart, no black screen).

The owner's results go on TASK22 (#29). TASK22 closes on them, or with
the limit the owner states.
