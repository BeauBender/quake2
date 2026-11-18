# Installation

This guide shows how to install Portmanbeau Quake II from scratch, for
several platforms and for both the full and the demo version. Only Apple
platforms are built and tested by this fork.

Only Apple platforms are built and tested by this fork. The instructions for
Windows, Linux and the BSDs below are kept as they were and those platforms
are unmaintained: nothing tests them or runs them. Linux, BSD and Windows are
compiled nowhere in CI; CodeQL analyses the code as built on macOS.


## The Full Version

Over the years Quake II was distributed in a myriad of ways:

* As retail release on CD.
* As part of Quake IV.
* Through Steam.
* Through GOG.com.
* etc.

Portmanbeau Quake II is compatible with all of these versions. While some of
these versions come with all patches applied, it's highly recommended to
follow this guide step by step and to reapply the patch by hand. Not all
distributors patched the game correctly, leading to severe problems like
missing assets or even crashes.


### Game Data Setup

The easiest way to install Portmanbeau Quake II is to start with the patch.
Please note that the patch is **required** for all full versions of the
game. Without the patch the game will not work correctly!

1. Download the patch from our mirror or somewhere else. The MD5
   checksum is `490557d4a90ff346a175d865a2bade87`:
   https://deponie.yamagi.org/quake2/idstuff/q2-3.20-x86-full-ctf.exe
2. Extract the patch into an empty directory. The patch comes as a
   self-extracting ZIP file. On Windows it can be extracted by double
   clicking on it, on other systems an archiver or the *unzip* command
   can be used.

Now remove the following files from the extracted patch. They're the
original executables, documentation and so on. They aren't needed for
Portmanbeau Quake II and just waste space:

* *3.20_Changes.txt*
* *quake2.exe*
* *ref_gl.dll*
* *ref_soft.dll*
* *baseq2/gamex86.dll*
* *ctf/ctf2.ico*
* *ctf/gamex86.dll*
* *ctf/readme.txt*
* *ctf/server.cfg*
* *xatrix/gamex86.dll*
* *rogue/gamex86.dll*

Copy the *pak0.pak* file and the *video/* subdirectory from the Quake
II distribution (CD, Steam or GOG download, etc) into the *baseq2/*
subdirectory of the extracted patch.

If the optional addons are available their gamedata must be copied too:

* For The Reckoning (also know as "xatrix") copy the *pak0.pak* and the
  *video/* subdirectory from the addon distribution into the *xatrix/*
  subdirectory.
* For Ground Zero (also known as "rogue") copy the *pak0.pak* and the
  *video/* subdirectory from the addon distribution into the *rogue/*
  subdirectory.

The MD5 checksums of the pakfiles are:

* *baseq2/pak0.pak*: `1ec55a724dc3109fd50dde71ab581d70`
* *baseq2/pak1.pak*: `42663ea709b7cd3eb9b634b36cfecb1a`
* *baseq2/pak2.pak*: `c8217cc5557b672a87fc210c2347d98d`
* *ctf/pak0.pak*: `1f6bd3d4c08f7ed8c037b12fcffd2eb5`
* *rogue/pak0.pak*: `5e2ecbe9287152a1e6e0d77b3f47dcb2`
* *xatrix/pak0.pak*: `f5e7b04f7d6b9530c59c5e1daa873b51`


### Music Extraction

The retail releases of Quake II and both addons contain up to 11 Audio
CD tracks as soundtrack. Since modern computers lack the ability for
direct CD playback, Portmanbeau Quake II reads the music from OGG/Vorbis
files.

Later Quake II releases, for example the one included with Quake IV and
the one available through Steam, lack the soundtrack. Nevertheless
Portmanbeau Quake II can play it if the files are copied into the directories
mentioned below.

Some digital distributed versions are special, they includes the
soundtrack as OGG/Vorbis files, but in a non standard format. Portmanbeau
Quake II can read this format for the GOG.com release. Other releases
may be supported in the future.


#### Using a Generic CD Extractor

1. Install a CD extractor (for example CDex) and set it to OGG/Vorbis
   files. Quality factor 6 (192 kbit/s) is usually more than enough.
2. Put the Quake II CD into the CD drive and extract the files.
3. The files must be named after the corresponding CD track: CD track 02
   becomes the file *02.ogg*, CD track 03 becomes the file *03.ogg* and
   so on. On both the Quake II and the Addon CDs track 01 is the data
   track and thus can't be ripped.
4. Put these files into the corresponding subdirectory:
	* *baseq2/music* for Quake II.
	* *xatrix/music* for The Reckoning.
	* *rogue/music* for Ground Zero.


#### Using a Shell Script

An easy way to extract the music on unixoid platforms (BSD, Linux and
MacOS) is to use *stuff/cdripper.sh*, a simple shellscript. It needs
*cdparanoia* and *oggenc* (from the *vorbis-tools* package) installed.
Use the package manager (apt, dnf, homebrew, pacman, pkg, ...) to
install them.  Just execute the script and copy the resulting *music/*
directory to:
  * *baseq2/* for Quake II.
  * *xatrix/* for The Reckoning.
  * *rogue/* for Ground Zero.


#### The GOG.com Release

The Quake II distributed by GOG.com contains the soundtrack, it just
needs to be copied into the game data directory. The target directory is
just *music/*, next to *baseq2/*. **Not** inside *baseq2/*.

Unixoid systems are case sensitive. On them the files should be named
*TrackXX.ogg* with a capital T. Otherwise the game won't find them.


### Alternate Startup Configuration

Portmanbeau Quake II ships with an alternative startup config that overrides
some global settings to saner defaults. To use it copy *stuff/yq2.cfg*
into the *baseq2/* directory.


### Fixed Map Data Files

As an optional feature, Portmanbeau Quake II provides fixed map data files
(.ent files). These files provide a replacement entity list in order
to fix some map bugs that have been discovered by players over the
years.

These fixes include fixes for bad monster counts in some maps /
difficulty settings, fixing broken spawn chains that made it impossible
to spawn some monsters, removing DM-only items that spawn in unreachable
areas in single player /co-op, and so on. You can find detailed
changelogs for each map by opening the .ent files with a text editor and
reading the comment section at the top (lines starting with "//").


#### Download And Setup

1. Download the .ent files from the yquake2 repositories on GitHub.
   * **baseq2**: yquake2/yquake2/stuff/mapfixes/baseq2/
   * **juggernaut**: yquake2/yquake2/stuff/mapfixes/juggernaut/
   * **xatrix**: yquake2/xatrix/stuff/mapfixes/
   * **rogue**: yquake2/rogue/stuff/mapfixes/
   * **zaero**: yquake2/zaero/stuff/mapfixes/
2. Once you have the .ent files you want, put them in the respective
   *maps/* sub-folder. So *xatrix* .ent files should go into your local
   *xatrix/maps/* folder (create this folder if it does not exist).
3. You will see a notification message in the console if an .ent file
   was loaded. If you see this message, you know the map fixes are in
   effect.


#### Reporting Map Bugs

If you know of any map bugs that are not addressed here, by all means
note that this fork does not accept bug reports or patches; the
repository at <https://github.com/BeauBender/quake2> is its home only.


### Monster Footstep Sounds

Portmanbeau Quake II has optional support for monster footstep sounds. These
require some custom audio files. Download the footsteps archive from
https://deponie.yamagi.org/quake2/assets/footsteps.pkz and extract it.
Copy the extracted sound directories into the *baseq2/* directory as
loose files, or repack them as a `.pak` archive and copy that instead.
This fork does not load `.pkz`, `.pk3` or other ZIP based archives;
only `.pak` archives and loose files are read.

The footstep sounds are included with the Yamagi Quake II Windows release.


## The Demo Version

A free demo version of Quake II is available and supported by Portmanbeau
Quake II. This demo contains the first few levels, but no videos and
no soundtrack.


### Game Data Setup

1. Download the demo from our mirror or somewhere else. Its MD5
   checksum is `4d1cd4618e80a38db59304132ea0856c`:
   https://deponie.yamagi.org/quake2/idstuff/q2-314-demo-x86.exe
2. Extract the downloaded file. It's a self-extract ZIP archive.
   On Windows it can be extracted by double clicking on it, on
   other system an archiver or the *unzip* command can be used.
3. Create a new directory and a subdirectory *baseq2/* in it.
4. Copy the *pak0.pak* and the *players/* subdirectory from the
   extracted archive into the newly created *baseq2/* subdirectory.

The demo **must not** be patched! Patching the demo will break it!

The MD5 checksums of the pakfiles are:

* *baseq2/pak0.pak*: `27d77240466ec4f3253256832b54db8a`


## Download and Extract the Executables

How the Portmanbeau Quake II executables are installed depends on the
platform:

- This fork publishes no binary packages. Compile it from source, see
  below.
- Most Linux distributions and BSD systems provide Yamagi Quake II
  packages, which are not this fork. Theses packages may be outdated.

**Please note:** The addons needs their own game library. The libraries
are included with the Yamagi Quake II Windows release. Linux distributions
often package them in a distinced package. If you want to compile from
source, have a look at the README files supplied with the addons.


### Windows

1. Get a Windows build of Portmanbeau Quake II. This fork publishes no
   binary releases yet; build it from source (see below).
2. Extract it into the gamedata directory created above. *quake2.exe*
   must be placed next to the *baseq2/* subdirectory.

On Windows the Portmanbeau Quake II installation is fully portable, the
installation directory can be moved the installation directory wherever
and whenever it's necessary. To update Portmanbeau Quake II just overwrite
the old files with the new ones.

There're two executables:

* *yquake2.exe*: This is main executable and should be preferred.
* *quake.exe*: This is just a wrapper to stay compatible with existing
  setups. For technical reasons *quake.exe* may not start in foreground,
  but in background!

If Windows Defender is activated, that's the default on Windows 8 and
Windows 10, it may complain that Portmanbeau Quake II is untrusted and should
not be started. That's because Yamagi Quake II ships unsigned binaries, and so would any
build of this fork. You can
force Windows to start it anyways.


### Binary Package from Linux distributions or BSD systems

Most Linux distributions and BSD systems provide Yamagi Quake II
packages, which are not this fork. Please refer to the documentation of the distribution or
system. The gamedata is searched at:

- A global directory specified by the package.
- The same directory as the quake2 executable.
- A directory given with the *-datadir /path/to/quake2_installation/*
  commandline argument.
- In *$HOME/.yq2*

If you're a package maintainer, please look at our packaging guide at
the [Packaging Guide](070_packaging.md).


## Compiling from source

To compile Portmanbeau Quake II from source the following dependencies
(including development headers) are needed:

* A GCC compatible compiler like *gcc*, *clang* or *MinGW-w64*.
* A LibGL implementation with system headers.
* SDL 3.0. On MacOS it is not needed: SDL 3 is built from the source
  in *third_party/SDL3/* as part of the client build.


### Prerequisites on Unixoid Platforms

The build dependencies can be installed with:

* On Arch Linux based distributions: `pacman -S base-devel mesa
  sdl3`
* On Debian based distributions: `apt install build-essential
  libgl1-mesa-dev libsdl3-dev`
* On FreeBSD: `pkg install gmake libGL sdl3`
* On NetBSD: `pkgin install gmake sdl3`
* On OpenBSD: `pkg_add gmake sdl3`
* On Solaris/Illumos: `pkg install sdl3`
* On Haiku: `pkgman libsdl3_devel`
* On MacOS only the Xcode command line tools are needed (they provide
  *clang* and *make*). SDL 3 is built from the copy in
  *third_party/SDL3/*, so neither Homebrew nor a system SDL is used.

Other distributions or platforms often have package named similar to the
Debian or FreeBSD packages.


### Compiling with GCC, Clang or MinGW

Clone the source from https://github.com/BeauBender/quake2.git, change
into the *quake2/* source directory and type *make* (Linux, MacOS, Haiku and Windows)
or *gmake* (FreeBSD, NetBSD, OpenBSD). Note on Solaris systems, *make*
 or *gmake* can be used, the latter provides in addition parallel build.
After the build finished, copy everything from the *release/* directory
to the Portmanbeau Quake II installation directory.

For the addons download or clone their source, change into the source
directory and type *make* (Linux, MacOS, Windows) or *gmake* (FreeBSD,
NetBSD, OpenBSD). After the compilation finishes the *release/game.so*
is copied to the corresponding directory in the Quake II installation.


### Compiling one executable with the modules linked in (MacOS)

`make STATIC_MODULES=1 STATIC_RENDERER=soft client` builds a single
*quake2* executable that contains the game and one renderer. There is no
*game.dylib* and no *ref_\*.dylib*, and the executable never calls
`dlopen` for either: `GetGameAPI` and `GetRefAPI` are resolved by the
linker. `STATIC_RENDERER` is `soft` (the default), `gl1` or `metal`;
`STATIC_ROOT` sets where the objects (*obj-<renderer>/*) and the executable (*bin/*)
go, by default `build/release/static-<renderer>/`, never *release/*. The
default build, with its libraries, is unchanged. This is the layout the
platforms that forbid loading code at run time (iOS, iPadOS, tvOS,
visionOS) need, and on MacOS it removes the search for a game library in
the game directories.

In this build:

* The game module is the one linked in. The `game` cvar still selects
  data directories (maps, models, sounds), but a code mod, a *game.dylib*
  in a game directory, is not loaded: it is unsupported.
* The renderer is the one linked in. If `vid_renderer` names another
  renderer, the engine prints two lines (the cvar value is not in this
  build, and the linked renderer is used) and loads the linked one. The
  cvar itself is left as it is, so a static run never rewrites the
  archived `vid_renderer` that a dynamic build shares.
* The build compiles out every module loader (`Sys_LoadLibrary`, the
  game library search), so no library is opened as a module. Only the
  client is built: `make STATIC_MODULES=1 client`; any other goal
  (`all`, `server`, `game`) is an error. `STATIC_RENDERER` must be
  exactly one of `soft`, `gl1`, `metal`.
* `STATIC_RENDERER=metal` needs *ref_metal.metallib* beside the
  executable (the build writes it there).

The `tests/check-static-modules.sh` check, part of the gate, builds the
objects of the mode and checks that each module exports only its entry
point; `tests/check-static-modules.sh --full` also links the executable and
checks it with `otool` and `nm` (CI's macOS job runs it).

### iPhone and iPad (simulator)

On a Mac with Xcode and its iOS platform, `make ios` builds an app for
the iOS simulator, *build/ios-iphonesimulator/release/Quake2.app*, from
the Makefile's own source lists: no Xcode project. It is the build of
the section above with `STATIC_RENDERER=metal`, compiled for
`arm64-apple-ios17.0-simulator`: the vendored SDL3, the client, the game
and `ref_metal` are linked into one *quake2*, so, as there, a mod's game
library is not loaded (the `game` cvar still selects data directories).
The bundle holds *quake2*, *Info.plist* (from *stuff/ios/Info.plist.in*),
*ref_metal.metallib* and no game data, and is signed ad hoc. `IOS_SDK`
(`iphonesimulator` or `iphoneos`, the latter unsigned), `IOS_MIN`
(17.0), `IOS_BUNDLE_ID`, `IOS_ROOT` (the output, never *release/* or
*debug/*) and `IOS_BUNDLE_DATA` (a directory whose *baseq2/* is copied
into the bundle, for a local build only) are described at the top of the
Makefile. `make ios_objects` builds the objects alone.

`scripts/ios-sim.sh smoke --out DIR` builds the app with a synthetic pak
(*scripts/ios/smokepak.py*: a palette, a console font whose glyphs are
transparent and a console background, made from formulas, no id
Software data), runs it on a simulator device it creates and deletes
again, and checks from the engine's own log and frame that `ref_metal`
reached the console, that the simulator's screenshot of the screen's
safe area is that frame pixel for pixel (so a stretched, cropped or
shifted picture fails), and that the app quit through its own quit
path. It writes the build and the logs to
DIR, which must be outside the checkout; the app's data and write
directory is *Documents/* in its data container (see below). The simulator's
Metal device reports no Apple GPU family; in the simulator only,
`ref_metal` does not check its apple5 floor and logs a line that says
so, while a device build still refuses a GPU below it.
`scripts/ios-sim.sh selftest` checks the smoke's own rules (bounded
`simctl` calls, the device lists) against a fake `xcrun`.

`tests/check-ios.sh`, part of the gate, compiles the objects for the
simulator and checks their platform, exports and AppKit references;
`--full` also builds and checks the bundle. Both print one skipped line
where there is no iOS simulator SDK. With the device SDK they do the same
for a device (`IOS_SDK=iphoneos`, below), and they run
`scripts/ios-device.sh --selftest`.

### Apple TV (simulator)

`make ios IOS_SDK=appletvsimulator` builds the same app for the Apple TV
simulator (`arm64-apple-tvos17.0-simulator`, Xcode's tvOS platform needed),
with its own *Info.plist* (*stuff/ios/Info-tvos.plist.in*). It is the
iPhone build with the touch overlay, the on-screen keyboard and the
iPhone's window hints left out. Where the game data lives on a device is
not decided yet (*docs/runbook/TASK23.md*).

To play, pair a controller (an MFi, Xbox or PlayStation one) with the
Apple TV. A fresh start binds it: the left stick moves, the right stick
turns, the right trigger fires, A (cross) jumps, B (circle) crouches, Y
(triangle) and the right shoulder pick the next weapon, the left shoulder
the previous one, X (square) uses the selected item and the view button
opens the inventory; the menu button (options, start) opens the game
menu. A binding that is already there, and every binding once a
`config.cfg` exists, is kept. In the menus the d-pad or the left stick
moves, A selects and B goes back.

The Siri Remote drives the menus as keys: a click on the up, down, left
or right edge of its touch surface (the ring on the second generation)
moves, a click in the middle selects, and the Back (Menu) button goes
back, or opens the game menu during play. Every menu screen works with
those six; the address book (typing a server address) is greyed with its
reason, since tvOS has no keyboard to type it with, and a server found
on the local network is still joined from the list.
`IOS_SIM_SDK=appletvsimulator scripts/ios-sim.sh run --out DIR --data DATA`
runs it on an Apple TV simulator device (DIR outside the checkout, as for
the smoke); `tests/check-tvos.sh` builds its objects in the gate.

### Apple Vision Pro (simulator)

`make ios IOS_SDK=xrsimulator` builds the same app for the Apple Vision Pro
simulator (`arm64-apple-xros1.0-simulator`, Xcode's visionOS platform
needed), with its own *Info.plist* (*stuff/ios/Info-visionos.plist.in*,
the Apple Vision device family). It runs in a window, on `ref_metal`
(visionOS has no OpenGL), and is the iPhone build with the touch overlay,
the on-screen keyboard and the iPhone's window hints left out. Controller
and look-and-pinch input, resizing and where the game data lives are not
done yet (*docs/runbook/TASK175.md*).
`IOS_SIM_SDK=xrsimulator scripts/ios-sim.sh run --out DIR --data DATA`
runs it on an Apple Vision Pro simulator device (DIR outside the
checkout, as for the smoke); `tests/check-visionos.sh` builds its objects
in the gate.

### Running on an iPhone or iPad

A device runs only an app signed by the owner's Apple developer team, so
the device build is made unsigned and signed afterwards, on the Mac that
holds the signing identity. What the owner needs:

- an Apple developer team: the paid Apple Developer Program, or a free
  personal team, whose profiles last 7 days and hold fewer entitlements;
- an "Apple Development" signing identity (certificate and key) in the
  login keychain;
- a development provisioning profile for the app's bundle identifier
  (`IOS_BUNDLE_ID`, `org.yquake2.quake2` unless the build sets another)
  that lists the device, as a *.mobileprovision* file. The profile's
  App ID must be that identifier exactly; a wildcard App ID is refused;
- the device paired with the Mac, with Developer Mode on (Settings >
  Privacy & Security > Developer Mode);
- Xcode, whose `xcrun devicectl` installs and starts the app.

The three commands, from the checkout:

```sh
make ios IOS_SDK=iphoneos
scripts/ios-device.sh sign --identity "Apple Development: NAME (ID)" --profile PROFILE.mobileprovision
scripts/ios-device.sh install --device DEVICE
scripts/ios-device.sh launch --device DEVICE
```

`make ios IOS_SDK=iphoneos` builds
*build/ios-iphoneos/release/Quake2.app* for `arm64-apple-ios17.0`, with
MetalFX linked weak (`ref_metal` uses it where the device has it), the
shaders compiled for the device and `CFBundleSupportedPlatforms`
`iPhoneOS`, and leaves it unsigned: a device refuses the ad hoc signature
of the simulator's build. With a bundle identifier of your own, pass
`IOS_BUNDLE_ID=...` to `make` and set it in the environment for the
script too.

`scripts/ios-device.sh sign` takes the identity's name (as Keychain
Access shows the certificate) and the profile on the command line or in
`IOS_SIGN_IDENTITY` and `IOS_PROFILE`; neither is stored anywhere. It
refuses a profile that `security cms -D` cannot decode, that has no
application identifier or team, or that is for another bundle identifier
than the app's (it names both), and an app that is not the device build.
It then writes the profile's entitlements to a private scratch file,
copies the profile into the bundle as *embedded.mobileprovision* and signs
the bundle with `codesign --force --sign IDENTITY --entitlements FILE`;
the signature is checked with `codesign --verify --strict`. If signing or
the check fails, or is interrupted, the profile and the signature are
removed from the bundle again, so `install` refuses it. An identity that
starts with a hyphen is refused (`-` is codesign's ad hoc identity).
Nothing of the decoded profile, which holds the team's certificates, is
printed; a failing tool's own lines are printed with the scratch
directory, the home directory, the identity, the team and the device
replaced by `<scratch>`, `<home>`, `<identity>`, `<team>` and `<device>`,
and a line naming the signing certificate replaced by one fixed line; the
script's messages name paths relative to the repository.

`install` and `launch` take the device's identifier as `xcrun devicectl
list devices` shows it (or `IOS_DEVICE`). `launch` passes any further
arguments to the engine, for example `scripts/ios-device.sh launch
--device DEVICE +map base1`. Each step is bounded (`IOS_DEVICE_SIGN_SECONDS`,
`IOS_DEVICE_INSTALL_SECONDS`, `IOS_DEVICE_LAUNCH_SECONDS`); Ctrl-C ends
the running `codesign` or `devicectl` before the script exits. The
script's header describes the rest. The game data then goes into the
app's folder as described below; the app finds it there with no option.

Distribution through the App Store or TestFlight is not covered: the
build is for the owner's own devices.

### Game data on iPhone and iPad

The app holds no game data. On iOS the only place a person can put files
into an app is its *Documents* folder, which the Files app shows under
"On My iPhone" (or "On My iPad") > "Quake II" (the app sets
`UIFileSharingEnabled` and `LSSupportsOpeningDocumentsInPlace`). The engine
uses *Documents* as its home directory, so it searches *Documents/baseq2/*
first and needs no option:

1. Start the app once. It creates *Documents/baseq2/* and, with no game
   data, shows an alert that names this place and *pak0.pak*, then quits.
2. Copy *pak0.pak* (and *pak1.pak*, *pak2.pak* if you have them) from the
   *baseq2/* folder of your Quake II into *Quake II > baseq2* in the Files
   app, or into the app's folder through Finder's device file sharing. The
   optional *players/*, *video/* and *music/* folders go next to them.
3. Start the app again.

The device's volume is case sensitive and the engine opens numbered paks
as *pak0.pak*, *pak1.pak* and so on, in lower case. A file named
*PAK0.PAK* is not found; the alert says to rename it.

`scripts/ios-sim.sh run --out DIR --data GAMEDIR` does the same on the
simulator: it copies *GAMEDIR/baseq2/\*.pak* (and *players/*, *video/*,
*music/* when present) into the app's *Documents/baseq2/*, starts the app
on a throwaway device and deletes the device at the end. `--no-data` runs
with no data at all, `--launch "ARGS"` (repeatable) is one start of the
app with engine arguments, and `--shot SECONDS` takes a simulator
screenshot and ends the app. Without either data option the synthetic
data of the smoke is used. The words of a `--launch` argument are split at
spaces and never expanded as file patterns. A start that ended on its own
must show the engine's quit line in its own log (*engine.N.log*), or, for
a start with no game data, the missing-data text, which is where that start
ends; without either the app was killed or crashed and the run fails. A
start that `--shot` ended needs none, and an app that ended before the shot
is reported. Without `--shot`, closing the app from the app switcher is
reported as killed.

The script touches only its own device. Every `simctl` call that changes
something (`shutdown`, `delete`, `erase`, `uninstall`, `terminate`, `boot`,
`bootstatus -b`, `install`, `launch`) names
the UDID of the device it created, never another. That device must be
absent from the device list before `simctl create`, present in the list
taken after it and gone from the list at the end; each of the three is a
failure. Other devices come and go on a Mac (Xcode Previews creates and
deletes its own): one that was listed before the run and is missing at the
end is printed as a warning that names its UDID, and does not fail the run.

### Playing on iPhone and iPad

With the touch controls on (the default on iOS) the game is played with
the fingers. In the game the left half of the screen is the move stick
and the right half the look stick, both floating: the stick is where the
finger lands, and a ring is drawn there. The buttons are in the lower
right corner (fire, jump, use item, crouch, next weapon) and at the top
(menu, console). In a menu, the console or a chat message the screen
shows a d-pad at the lower left and OK and Back at the lower right
instead: they press the d-pad and the confirm and back buttons of the
virtual gamepad, which the menus read as they read a controller. The
console button is there too; it raises the on-screen keyboard, which is
shown only while text is typed. The size and the side of the controls are
the "touch size" and "left-handed" items of the controller page of the
controls menu. When a physical controller is connected it is used
instead, the controls are not drawn, and they come back when it is
disconnected. Elsewhere `in_touch` does nothing, and the three items are
greyed.
