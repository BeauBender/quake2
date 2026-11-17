# TASK147: the iOS display is the safe rectangle

TASK147's code merged as #383: the iOS view is the safe rectangle, the screen is its own size, the
layer is the window's whole pixels, and the video menu is greyed on iOS. The smoke
(`scripts/ios-sim.sh smoke`) matched the safe rectangle pixel for pixel on the iPhone 17 and iPad Pro
11-inch simulators with synthetic data.

## Checks on a physical host

Four checks were left for a person at the build host or with a device. Item 2 is done below with the
owner's data; items 1, 3 and 4 are owner steps, written out here. None of them is scripted: the
Simulator's menus and the device's settings are not driven by any script.

### 1. Rotation by hand in Simulator (owner step)

`simctl` has no rotate command, so this is a person at the Simulator window. Portrait is never offered
by the app; only landscape left and right.

1. `scripts/ios-sim.sh run --data <data dir> --launch "+map base1" --out <artifacts>/TASK147/rotate`,
   with the owner's data directory (`<data dir>/baseq2/*.pak`); `<out>` below is that `--out`
   directory. Without `--shot` the script waits for the app to quit (and ends it after
   `IOS_SIM_RUN_SECONDS`, 3600 by default, as a failed run), so leave it running for the steps below,
   and quit from the engine's console (`quit`) when done: the script then copies the log. The
   simulator device is deleted when the app ends.
2. In Simulator, choose Device > Rotate Left or Rotate Right (Cmd+Left, Cmd+Right) so the phone goes
   from landscape left to landscape right, and back, at least twice. Do it once while the console is
   down (the backtick key, or the touch console button) and once in the map.
3. After each rotation, wait a second and look for:
   - the picture fills the area between the sensor housing and the home indicator, and nothing is
     drawn in the black bands beside the housing or under the indicator;
   - the HUD's four corners (health, ammo, armor, the frags/timer row) stay inside the picture, none
     cut off by the housing or the indicator;
   - the picture is not stretched or squashed (the crosshair stays round) and is not cropped;
   - the housing's side swaps with the rotation: the wide inset moves to the other side.
4. Take the engine's own record of it: the app's `qconsole.log` (with `--launch` the script copies it
   to `<out>/engine.<n>.log` when the app ends) holds a `ref_metal: safe area: window WxH pixels,
   insets left L top T right R bottom B` line and a `Drawable size:` line per window change. Each
   `Drawable size:` must be W - L - R by H - T - B of the safe area line above it, with L and R swapped
   after a rotation to the other side. More than one `vid_restart` per rotation is a loop and a fail.
5. A frame while rotated: `xcrun simctl io <udid> screenshot <file>` (the UDID is in the script's
   output). The check is the log arithmetic of step 4 (the drawable is the window less the insets)
   and a look at the frame: the picture sits inside the bands, nothing drawn beside the housing or
   under the indicator. The throwaway script of item 2 is not in the repository, so it is not a step
   here. The frame holds game data with the owner's paks: keep it local, never attach or upload it.

### 2. The HUD's corners in `base1` with the owner's data (done on simulators)

Commands, from the repository root, each through the host lock (`heavy.sh`), with the owner's data
copied into the app container by `run --data` (TASK149):

```
scripts/ios-sim.sh run --data <data dir> --launch "+map base1" \
    --shot 45 --out <artifacts>/TASK147/iphone
IOS_SIM_TYPE="iPad Pro 11-inch (M5)" scripts/ios-sim.sh run --data <data dir> \
    --launch "+map base1" --shot 45 --out <artifacts>/TASK147/ipad
```

`--shot 45` takes `simctl io screenshot` 45 s after the start, then terminates the app. The map is
loaded by then (the log has the `Map: base1` line), and a cheat-free spawn is on screen, where the
engine's own 2D code draws the HUD (the frame cannot tell it from the world, see below). Both runs ended `run ok, 1 start(s)`; the
machine's load average was 2.6 (1 minute) before the iPhone run; the iPad run waited for the host lock
behind another lane, and its load average after it was 68 (other lanes' work, which does not matter
for a placement check; this is not a timing). The simulators were iOS 27.2, and each device was made
and deleted by the script.

From each run's `engine.1.log`: the safe rectangle in pixels (the `safe area` line), the `Drawable
size:` line and the view size in points (the `Real display mode:` line, which is what SDL reports
for the view).

The check is a script (Python standard library only, a hand-written PNG decoder; run with `python3
-I`, and not committed since it reads frames of the owner's data and is a throwaway). It reads
`screen.1.png` and `engine.1.log` and:

- takes the window size and the insets from the `safe area` line, and asserts that `Drawable size:`
  is the window less the insets;
- turns the screenshot to the window's orientation (the simulator's screenshot of an iPad is in the
  device's portrait orientation; the turn that leaves no lit pixel outside the rectangle is the one
  taken, and the other turn's count is printed: 95483 lit pixels outside for the wrong one);
- counts the non-black pixels (any channel above 0) inside and outside the safe rectangle, and takes
  the bounding box of all of them;
- counts the lit pixels in a 240 x 120 pixel patch at each corner of the safe rectangle;
- passes when no lit pixel lies outside the safe rectangle, there is some inside, and the bounding
  box lies inside the rectangle.

Never by looking: no frame was opened or viewed. The frames hold game data and stay in the artifacts
folder (`<artifacts>/TASK147/`), never committed, attached or uploaded.

The script's output, iPhone 17 (`Real display mode: 874x402@60.00`, the view in points):

```
screenshot 2622x1206, window 2622x1206, view 874x402 points
safe rectangle x 186..2435 y 0..1145 (2250x1146); Drawable size 2250x1146
non-black pixels: inside 2540572, outside 0
non-black bounding box x 186..2435 y 0..1145
bounding box inside safe rectangle: True
corner top-left     lit 28310 of 28800
corner top-right    lit 27102 of 28800
corner bottom-left  lit 28800 of 28800
corner bottom-right lit 28800 of 28800
RESULT PASS
```

The engine's line: `ref_metal: safe area: window 2622x1206 pixels, insets left 186 top 0 right 186 bottom 60`.

iPad Pro 11-inch (M5) (`ios-sim.sh` picked the 12 GB variant; the view is 834x1210 points, in the
device's portrait numbers, as `Real display mode:` reports it; the engine's window is 2420x1668
pixels, landscape):

```
turns tried (lit pixels outside the safe rectangle): counter-clockwise 0, clockwise 95483
screenshot turned counter-clockwise
screenshot 2420x1668, window 2420x1668, view 834x1210 points
safe rectangle x 0..2419 y 0..1627 (2420x1628); Drawable size 2420x1628
non-black pixels: inside 3890755, outside 0
non-black bounding box x 0..2419 y 0..1627
bounding box inside safe rectangle: True
corner top-left     lit 28669 of 28800
corner top-right    lit 26700 of 28800
corner bottom-left  lit 28800 of 28800
corner bottom-right lit 27970 of 28800
RESULT PASS
```

| device | safe rectangle (pixels) | non-black bounding box | outside the rectangle | result |
| --- | --- | --- | --- | --- |
| iPhone 17 | x 186..2435, y 0..1145 (2250x1146, insets 186 / 0 / 186 / 60) | x 186..2435, y 0..1145 | 0 lit pixels | pass |
| iPad Pro 11-inch (M5) | x 0..2419, y 0..1627 (2420x1628, insets 0 / 0 / 0 / 40) | x 0..2419, y 0..1627 | 0 lit pixels | pass |

The bounding box is the rectangle exactly: the 3D view fills the rectangle, and the bands (the
housing's sides on the phone, the bottom band for the home indicator on both) are black. What this
does and does not show: the HUD's own pixels are not told apart from the world's (the frame is one
picture), so the check is that the whole picture lies inside the rectangle (the HUD, being part of
that picture, with it) and that the four corner patches hold lit pixels, two of them about 93 to 94
percent (26700 and 27102 of 28800), the others 97 percent or more. It does not show the HUD's elements one by one; item 1 above, by
eye, does. The acceptance's wording ("the HUD's corners in `base1` lie inside the safe area on the
iPhone simulator") holds, and on the iPad too.

### 3. Presenting above 60 Hz on a ProMotion iPhone (owner step)

`stuff/ios/Info.plist.in` sets `CADisableMinimumFrameDurationOnPhone`, so a ProMotion iPhone (iPhone
Pro models, 120 Hz) may present above 60 Hz. The simulator always reports 60 Hz (`Real display
mode: 874x402@60.00` above), so this can only be seen on the device.

1. Build, sign, install (the full text is `doc/020_installation.md`, "Running on an iPhone or iPad";
   the owner needs a development profile for the bundle identifier that lists the device, an "Apple
   Development" identity in the login keychain, and the device paired with Developer Mode on):

   ```
   make ios IOS_SDK=iphoneos
   scripts/ios-device.sh sign --identity "Apple Development: NAME (ID)" --profile PROFILE.mobileprovision
   scripts/ios-device.sh install --device DEVICE
   ```

   `make ios` alone builds for the simulator. `sign` runs `codesign` with the identity and puts the
   profile in the bundle, `install` runs `xcrun devicectl device install app`, and `DEVICE` is the
   identifier `xcrun devicectl list devices` shows. The game data goes into the app's Documents
   folder with the Files app or Finder file sharing (`doc/020_installation.md`, "Game data on
   iPhone and iPad"), not into the build. Do not post the identity, the team or the device
   identifier anywhere public.
2. On the iPhone, turn Low Power Mode off (Settings > Battery); in Settings > Accessibility > Motion,
   Limit Frame Rate must be off. Both cap the display at 60 Hz.
3. Start the app with launch arguments, which `ios-device.sh launch` passes on (it runs `xcrun
   devicectl device process launch`):

   ```
   scripts/ios-device.sh launch --device DEVICE +set logfile 2 +set cl_showfps 1 +map base1
   ```

   `cl_showfps` is the framecounter (see `doc/040_cvarlist.md`); Instruments' Core Animation FPS
   is another gauge. The log comes back as in item 4, step 5.
4. Look for: the display refresh rate in the engine's log line `Real display mode: WxH@HZ`, which on a
   ProMotion panel reads above 60 (120.00); and the frame rate in the map reaching above 60 when the
   scene is light (an empty corridor of `base1`). If the log says 120 and the rate stays at 60, the
   frame is paced to 60 by the engine (`r_vsync` has no effect on iOS: the layer presents in step with
   the display); report that with the log line, the map and the gauge.
5. Record: the iPhone model, the iOS version, the log line, the rate in a corridor and the rate in a
   fight. If the frame is not at the display's rate, it is a bug card, not a pass.

### 4. A run on a device GPU (owner step)

The simulator's GPU is not a device GPU: its log says `Metal supportsFamily (apple5 to apple9):
none` and `the device floor (apple5) is not checked in the simulator`. On a device the family line
names a family and the floor is checked.

1. The same build, signed and installed as in item 3, on an iPhone and, if there is one, an iPad
   (a profile that lists each device).
2. Start it with
   `scripts/ios-device.sh launch --device DEVICE +set logfile 2 +set metal_stats 1 +map base1`
   (`metal_stats` makes the engine print its closing stats line); play for a minute, open the
   console and the menus, rotate the device between landscape left and right, then quit from the
   console (`quit`), so the stats line is written. The command-line launch has no Metal validation
   layer; to run with it, start the same bundle from Xcode with the scheme's Metal API and Shader
   Validation on, and say so in the record.
3. Look for, in the log:
   - `Metal device:` names the real GPU and `Metal supportsFamily (apple5 to apple9):` names a family,
     with no line that the floor is not checked;
   - `ref_metal: safe area:` and `Drawable size:` as in item 2, for the device's real insets;
   - `metal_stats` at the close with `0` failed command buffers, and no validation message in the
     console;
   - the picture inside the safe area, with the HUD's corners clear of the housing and the home
     indicator.
4. Record the device model, the iOS version, the family line, the `safe area` and `Drawable size:`
   lines and the result.
5. Getting `qconsole.log` back (`logfile 2` writes it as the engine runs): it is in the app's
   `baseq2` folder inside Documents, which the Files app shows under "On My iPhone" > "Quake II" >
   `baseq2` (the app sets `UIFileSharingEnabled`), or from the Mac with
   `xcrun devicectl device copy from --device DEVICE --domain-type appDataContainer
   --domain-identifier <bundle id> --source Documents/baseq2/qconsole.log --destination <file>`
   (the bundle identifier is `org.yquake2.quake2` unless the build set another). A frame from the device holds game data: keep it local.
