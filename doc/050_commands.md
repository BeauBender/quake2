# Portmanbeau Quake II Commands

This list explains all commands added by Yamagi Quake II and inherited by Portmanbeau Quake II. All of the
original clients (Vanilla Quake II) commands are still in place.


* **cycleweap <weapons>**: Cycles through the given weapons. Can be used
  to bind several weapons on one key. The list is provided as a list of
  weapon classnames separated by whitespaces. A weapon in the list is
  skipped if it is not a valid weapon classname, you do not own it in
  your inventory or you do not have enough ammo to use it.
  By quickly tapping the bound key, you can navigate the list faster.

* **gamemode <mode>**: Provides a convenient way to switch the game mode
  between `coop`, `dm` and `sp` without having to set three cvars the
  correct way. `?` prints the current mode.

* **ios_orientation <name>**: iOS only (iPhone, iPad). Asks the window
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

* **spawnentity classname x y z <angle_x angle_y angle_z> <flags>**:
  Spawn new entity of `classname` at `x y z` coordinates.

* **spawnonstart classname**: Spawn new entity of `classname` at start point.

* **teleport <x y z>**: Teleports the player to the given coordinates.

* **upscale_textures <2|4> [scale2x|metalfx] [force]**: Writes PNG
  versions, twice or four times the size, of the world textures, the
  faces of the skies and the model and player skin images of the game
  data into the write directory, where `r_retexturing 1` loads them.
  The backend is `scale2x` (the default: pixel-art edges, no new
  colour) or, on macOS, `metalfx` (MetalFX's spatial scaler); where
  `metalfx` is unsupported the command says why in one line and uses
  `scale2x`. Any other backend name is refused with the usage line.
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
