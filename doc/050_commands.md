# Portmanbeau Quake II Commands

This list explains all commands added by Yamagi Quake II and inherited by Portmanbeau Quake II. All of the
original clients (Vanilla Quake II) commands are still in place.


* **cycleweap <weapons>**: Cycles through the given weapons. Can be used
  to bind several weapons on one key. The list is provided as a list of
  weapon classnames separated by whitespaces. A weapon in the list is
  skipped if it is not a valid weapon classname, you do not own it in
  your inventory or you do not have enough ammo to use it.
  By quickly tapping the bound key, you can navigate the list faster.

* **in_probe <what> [args]**: Only in a test build (compiled with
  `-DYQ2_INPUT_PROBE`, TASK23); a release has no such command. Drives
  input without hands, since a simulator cannot press a controller
  button: `pad` attaches an SDL virtual gamepad, which the engine finds
  as any controller plugged in after the start (it is refused in the
  start's first frames, where a new device is taken as one that was
  already there); `pads` lists the joysticks and which one the engine
  uses; `button <name> <0|1>` presses or releases one of its
  buttons and `axis <name> <value>` moves one of its axes (SDL's names:
  `a`, `b`, `start`, `dpup`, `leftx`, `righttrigger`, ...); `key <name>
  <0|1>` pushes the key event of a scancode (`Up`, `Down`, `Left`,
  `Right`, `Return`, `Escape`: the Siri Remote as keys); `state` prints
  where input goes, the player's origin, view angles and gun frame.

* **menu_probe state|walk**: Only in a test build (compiled with
  `-DYQ2_INPUT_PROBE`, TASK23); a release has no such command. `state`
  prints the menu's depth, screen and the item under the cursor. `walk`
  visits every screen reachable from the main menu with only up, down,
  left, right, select and back, fails a screen whose selectable items are
  not all reached with down and with up or that back does not leave,
  fails a binding item (on the four key and controller binding screens)
  where select does not enter bind mode or back does not leave it binding
  nothing, and any other greyed item that select changes, prints how many
  binding items passed, then `menu_probe: walk ok` or `walk FAIL` with the
  counts, drops the
  commands the selects queued and quits.

* **metal_hdr_info**: The Metal renderer only. Prints the state of its
  layer for `r_hdr`: the pixel format (`BGRA8Unorm`, or `RGBA16Float`
  under `r_hdr 1`), the colour space's name (`none` when the layer has
  none), whether extended dynamic range is on, and the display's headroom
  now and its potential, which read `unavailable` until the renderer
  reads them; then, on a second line, the peak in force (`r_hdr_peak`
  held to the headroom, `1.00` without one). It only reads; it changes
  nothing.

* **metal_hdr_probe <x> <y>**: The Metal renderer only. Reads the pixel
  at `x`, `y` (whole numbers from the top left, held to the drawable's
  size, so a negative one reads the first row or column and a huge one the
  last; anything else prints the usage line) of the next
  frame shown, after the renderer wrote it and before the system displays
  it, and prints its four values (four 16-bit floats under `r_hdr 1`, the
  bytes as fractions of 255 otherwise) and whether a colour component is
  above SDR white (1.0). Under `r_hdr 1` a value above 1.0 needs a peak
  in force above 1.0, so a display with extended range. When no frame is
  shown within 60 frames (the display off or asleep, the window hidden)
  it says so and reads nothing. It changes nothing.

* **gamemode <mode>**: Provides a convenient way to switch the game mode
  between `coop`, `dm` and `sp` without having to set three cvars the
  correct way. `?` prints the current mode.

* **ios_orientation <name>**: iOS (iPhone and iPad) only; not registered on tvOS or visionOS. Asks the window
  scene for an interface orientation, the way turning the device does, so
  a test can change the window's size or insets with no hand on the
  device. `name` is `portrait`, `portraitupsidedown`, `landscapeleft`,
  `landscaperight`, `landscape` (either) or `all`. The system grants only
  an orientation the app supports (the two landscapes), and answers
  `Orientation: refused (...)` in the console for another; it does
  nothing a person turning the device cannot do, so a server sending it
  is harmless. The window then follows as it does for any size change.
  Without a name it only reports the scene's interface orientation and
  the window's size, insets and orientation as `Screen now: ...`.

* **listentities <class>**: Lists the coordinates of all entities of a
  given class.  Possible classes are `ammo`, `items`, `keys`, `monsters`
  and `weapons`. Multiple classes can be given, they're separated by
  whitespaces. The special class `all` lists the coordinates of all
  entities.

* **listmaps**: Lists available maps for the player to load. Maps from
  loaded pak files will be listed first followed by maps placed in 
  the current game's maps folder.

* **load <name>**: Loads the single-player savegame `save/<name>` from
  the write directory.

* **ogg <cmd>**: Controls OGG/Vobis music playback. Commands are:
  * **info**: Print informations about the current track.
  * **mute**: Mute playback.
  * **play <num>**: Play track number <num>.
  * **skip**: Depending on the the value of `ogg_shuffle` skip back to
    the start of the current track, replay the last track or skip to
    another random track.
  * **stop**: Stop playback.
  * **toggle**: Pause or unpause playback.

* **playermodels**: Lists available multiplayer models.

* **prefweap <weapons>**: Similar to the cycleweap command, this will
  select the first weapon available in the priority list given. Useful
  to set a "panic button". E.g. the following will select your best
  shotgun: `prefweap weapon_supershotgun weapon_shotgun`.

* **screenshot <tga>**: Writes the current frame to `scrnshot/q2_NNNN.tga`
  as an uncompressed 24-bit TGA. TGA is the only format; the argument is
  optional and, if given, must be `tga`.

* **rt_ab [stock]**: Flips the Metal renderer between A, the player's
  own ray-tracing settings, and B, a second set: every tuning setting at
  its default ("B: stock", the default), or a preset given with
  `rt_preset ab`. B is an overlay (`r_rt_ab`): no setting is written, and
  flipping back shows the player's values. The console and the video
  menu's tuning screen show which side is on screen. `rt_ab stock` makes B the
  defaults again without flipping. Bindable to a key.

* **rt_preset save|load|ab <name>, rt_preset list**: The ray-tracing
  tuning presets, `<writedir>/presets/rt-<name>.cfg`: plain `set` lines
  for every tuning setting (the ray-tracing screen's and the tuning
  screen's, without the debug views and the presentation) under a
  comment with the engine version and the date; a setting the renderer
  has not registered, or whose value holds a quote or one of `;`, `|`
  and `=`, gets no line. Its first word: save writes the
  current values, load sets them from the file (only those settings;
  any other line, and an empty value, is passed over) and prints each one it changed, ab
  makes the preset the B side of `rt_ab` without writing any setting,
  and list names the presets. The name is refused when it is empty,
  longer than 32 characters, holds `..`, a slash or a backslash, starts
  with a dot, or holds a character other than a letter, a digit, `_` or
  `-`.

  From a preset to defaults or a presentation: `scripts/rt/preset-to-table.sh
  <preset.cfg> [--as defaults|enhanced|retro|raytraced]` reads a saved
  preset and prints, for `defaults`, each setting that differs from
  today's default with the source line to change, or, for a
  presentation, the C rows of that mode's overlay in the format of
  `rm_rtgate.c`. It refuses any line that is not `set <name> "<value>"`
  and any name that is not a tuning setting, and never edits a file; the
  change itself is made in a pull request.

* **save <name>**: Saves the single-player game as `save/<name>` in the
  write directory.

* **spawnentity classname x y z <angle_x angle_y angle_z> <flags>**:
  Spawn new entity of `classname` at `x y z` coordinates.

* **spawnonstart classname**: Spawn new entity of `classname` at start point.

* **teleport <x y z>**: Teleports the player to the given coordinates.

* **upscale_textures <2|4> [scale2x|metalfx|vt] [force]**: Writes PNG
  versions, twice or four times the size, of the world textures, the
  faces of the skies and the model and player skin images of the game
  data into the write directory, where `r_retexturing 1` loads them.
  The backend is `scale2x` (the default: pixel-art edges, no new
  colour) or, on macOS, `metalfx` (MetalFX's spatial scaler) or `vt`
  (VideoToolbox's machine-learning super-resolution scaler, macOS 26 or
  later). `vt` needs a model that macOS downloads once from Apple. Only
  the menu item fetches it (Options, "upscale textures", "VideoToolbox
  ML"; a key press, which a server's stuffed commands cannot make): a
  first press says so in one line and starts the fetch, which runs
  while the game stays responsive, the item greyed until it is done.
  The command with the model missing refuses in one line, says to fetch
  it from the menu, and writes nothing (decision 0013). Where `metalfx` or `vt` is unsupported (on
  iOS, an older macOS, or a Mac VideoToolbox does not support) the
  command says why in one line and uses `scale2x`. Any other backend
  name is refused with the usage line.
  Files that exist are skipped unless `force` is given, and a symlink at
  an output name is never written through; names that are not plain
  paths (letters, digits, `_`, `-`, `+`, `.` and `/`) are refused,
  nothing is written inside a git work tree, and the command is refused
  while a demo plays or while connected to a remote server (a server
  that first ends the connection itself can still start it). The
  Options menu's "upscale textures" page runs it. See "Textures
  upscaled on your own machine" in `030_configuration.md`.

* **viewpos**: Show player position.

* **vstr**: Inserts the current value of a variable as command text.
