# Portmanbeau Quake II Console Variables

This lists explains most console variables (cvars) added by Yamagi
Quake II and inherited by Portmanbeau Quake II. Most of the original clients (Vanilla Quake II) cvars are
still in place, however due to architectural changes some of them
have been renamed. The prefixes are:

* No prefix: General stuff.
* `cl_`: Client.
* `gl_`: Common to all OpenGL renderers.
* `gl1_`: OpenGL 1.4 renderer.
* `gl3_`: OpenGL 3.2 renderer.
* `metal_`: Metal renderer (macOS).
* `ogg_`: Ogg/Vorbis music playback.
* `r_`: Common to all renderers.
* `s_`: Sound system.
* `sv_`: Server 
* `sw_`: Software renderer.
* `vid_`: Video backend.

All cvars may be given at command line through `+set cvar value` or
typed into the console. The console can be opended with *Left Shift +
Esc*.

Keep in mind that some cvars need quotation marks around the arguments.
When giving such cvars at the command line the argument string must be
surrounded by ticks. For example `+set sv_maplist '"q2dm1 q2dm2"'`.


## Command line arguments

These are not console variables, they cannot be entered into the
console, only be given through the command line at startup. While cvars
are prefixed with a `+`, arguments are starting with a `-`. For example
it's `+set busywait 0` (setting the `busywait` cvar) and `-portable`
(setting the `portable` argument).

* **cfgdir**: The name (not the path) of the configuration directory.

* **datadir**: Directory from which the game data is loaded. Can be used
  in startup scripts, to test binaries, etc. If not set, the directory
  containing the binaries is used.

* **portable**: Makes Quake II portable, all runtime data like the the
  config, savegames and so on is stored next to the executable and not
  in the users home directory.


## General

* **aimfix**: Fix aiming. When set to to `0` (the default) aiming is
  slightly inaccurate, bullets and the like have a little drift. When
  set to `1` they hit exactly were the crosshair is.
  
* **busywait**: By default this is set to `1`, causing Quake II to spin
  in a very tight loop until it's time to process the next frame. This
  is a very accurate way to determine the internal timing, but comes with
  a relatively high CPU usage. If set to `0` Quake II lays itself to
  sleep and tells the operating system to send a wakeup signal when it's
  time for the next frame. The latter is more CPU friendly but can be
  rather inaccurate, especially on Windows. Use with care.

* **sv_optimize_sp_loadtime** / **sv_optimize_mp_loadtime**: These cvars
  enable/disable optimizations that speed up level load times (or more
  accurately, client connection).  
  sp stands for singleplayer and mp for multiplayer, respectively.  
  The sp version is fully enabled by default (value 31) while
  multiplayer enables a subset (value 7).  
  The cvar value is a bitmask for 5 optimization features:

  - **1: Message utilization**: When the server sends the client
    configstrings and other data during the connection process, the
    message buffer is only used around 50-60%. When this flag is
    enabled, the message is used much better, dramatically reducing the
    amount of messages needed to deliver all the data to the client.
  - **2: Server send rate**: By default, the server sends messages to
    clients once every 0.1 seconds, roughly. This slows down sending
    data to clients, especially in singleplayer. This is normal for
    active clients, but for connecting/inactive clients, this delay is
    unnecessary. When this flag is set, the server will send messages to
    inactive clients ~8x more frequently.
  - **4: Reconnection**: When the server changes maps, like on level
    transitions, the server first sends a "changing" command to all
    clients, and then a "reconnect" command. The delay between these
    commands can be quite long, ~1 second. This flag will avoid this
    delay when set, by sending the two commands within the same message.
  - **8: HUD code send**: HUD code is stored in several configstring
    slots. When the server sends these slots to clients, it sends the
    full code and then increasingly smaller substrings of the code,
    which has a lot of redundancy. With this optimization enabled,
    only the initial, full HUD code string is sent. This is a
    very minor optimization, will in some cases reduce number of
    configstring packets by 1. The saving would be bigger in
    multiplayer due to the added networking latency, and mods with
    longer HUD code would also benefit more from this.
  - **16: Baselines head-start**: When the server is sending the
    last configstrings data packet, it adds the first baselines
    command at the end and leaves any remaining space
    in the packet unused. When this optimization is enabled,
    the server will try to use that space for the first few
    entity baselines. In some levels this can avoid an extra
    roundtrip of client-server packets.

  Simply add these flag values together to get the cvar value you want.
  For example, sendrate + reconnect = 2 + 4 = 6.
  Set to 31 for all optimizations, or 0 to disable them entirely.

* **cl_maxfps**: The approximate framerate for client/server ("packet")
  frames if *cl_async* is `1`. If set to `-1` (the default), the engine
  will choose a packet framerate appropriate for the render framerate.  
  See `cl_async` for more information.

* **cl_async**: Run render frames independently of client/server frames.  
  If set to `0`, client, server (gamecode) and the renderer run synchronous,
  (like Quake2 originally did) which means that for every rendered frame
  a client- and server-frame is executed, which includes the gamecode and
  physics/movement-simulation etc. At higher framerates (above 95 or so)
  this leads to movement bugs, like being able to jump higher than expected
  (kind of like the infamous Quake 3 125Hz bug).  
  For `cl_async 0`, *vid_maxfps* (or, if vsync is enabled, the display
  refresh rate) is used and *cl_maxfps* is ignored.
  
  If *cl_async* is set to `1` (the default) the client is asynchronous,
  which means that there can be multiple render frames between client-
  and server-frames. This makes it possible to renderer as many frames
  as desired without physics and movement problems.  
  The client framerate is controlled by *cl_maxfps*,  
  the renderer framerate is controlled by *vid_maxfps*.  
  
  As client/server frames ("packet frames") are only run together with
  a render frame, the *real* client/server framerate is always rounded to
  a fraction of the renderframerate that's closest to *cl_maxfps*.  
  So if for example *vid_maxfps* is `60` and *cl_maxfps* is `50`, it will
  be rounded to `60` and every renderframe is also a packet frame.  
  If *vid_maxfps* is `60` and *cl_maxfps* is `40`, it will be rounded to
  `30` and every second render frame is also a packet frame.
  
  It seems like the best working packet framerate is `60` (which means that
  the render framerate should be a multiple of that), otherwise values
  between `45` and `90` seem to work ok, lower and higher values can lead
  to buggy movement, jittering and other issues.  
  Setting *cl_maxfps* to `-1` (the default since 8.02) will automatically
  choose a packet framerate that's *both* a fraction of *vid_maxfps*
  (or display refreshrate if vsync is on) *and* between 45 and 90.
  
* **cl_kickangles**: If set to `0` angle kicks (weapon recoil, damage
  hits and the like) are ignored. Cheat-protected. Defaults to `1`.

* **cl_laseralpha**: Controls how see-through laserbeams are.
  The value ranges from 0.0 to 1.0, from completely invisible to
  completely opaque. So higher value means better visibility.
  Defaults to `0.3`.

* **cl_limitsparksounds**: If set to `1` the number of sound generated
  when shooting into power screen and power shields is limited to 16.
  This works around global volume drops
  if too many sounds are played at the same time. This was the default
  behavior between Yamagi Quake II 7.10 and 7.45. Defaults to `0`.

* **cl_loadpaused**: If set to `1` (the default) the client is put into
  pause mode during single player savegame load. This prevents monsters
  and the environment from hurting the player while the client is still
  connecting. If set to `2` the client stays in pause mode after
  loading. If set to `0` pause mode is never entered, this is the
  Vanilla Quake II behaviour.

* **cl_model_preview_start**: Start frame value in multiplayer model
  preview.  `-1` - don't show animation. Defaults to `84` for show
  salute animation.

* **cl_model_preview_end**: End frame value in multiplayer model
  preview.  `-1` - don't show animation. Defaults to `94` for show
  salute animation.

* **cl_nodownload_list**: Whitespace separated list of substrings, files
  having one these strings in their name are never downloaded. Empty by
  default. Note that some substrings are always forbidden, for security
  reasons these cannot be overridden: '.dll', '.dylib' and '.so' to
  prevent downloading of libraries which could be injected into the
  Portmanbeau Quake II process. '..' or ':' inside filenames and '/' or '.'
  at the beginning of filenames to prevent downloading files into
  arbitrary directories.

* **cl_r1q2_lightstyle**: Since the first release Yamagi Quake II used
  the R1Q2 colors for the dynamic lights of rockets. Set to `0` to get
  the Vanilla Quake II colors. Defaults to `1`.

* **cl_showfps**: Shows the framecounter. Set to `2` for more and to
  `3` for even more informations.
  
* **cl_showspeed**:  Shows the players speed. Set to `1` to display both
  overall speed and (horizontal speed) in Quake Units (QU) respectfully
  at the top right corner of the screen. Set to `2` to show only the
  horizontal speed under the crosshair.
  
* **cl_unpaused_scvis**: If set to `1` (the default) the client unpause
  when the screen becomes visible.

* **cl_centertime**: Time in ms that takes the `centerview` command to
  finish. `0` is immediate (Vanilla Q2 behaviour). Default `180`.

* **in_grab**: Defines how the mouse is grabbed by Portmanbeau Quake IIs
  window. If set to `0` the mouse is never grabbed and if set to `1`
  it's always grabbed. If set to `2` (the default) the mouse is grabbed
  during gameplay and released otherwise (in menu, videos, console or if
  game is paused).

* **singleplayer**: Only available in the dedicated server. Vanilla
  Quake II enforced that either `coop` or `deathmatch` is set to `1`
  when running the dedicated server. That made it impossible to play
  single player campaigns over the dedicated server. When set to `1`,
  both `coop` and `deathmatch` are forced to `0` and `maxclients` is
  forced to `1`. This can be used to run a dedicated server with an old
  single player mod, where the source code isn't available, inside a
  Windows 98 or XP VM and connect over network from a non Windows
  system.

* **coop_pickup_weapons**: In coop a weapon can be picked up only once.
  For example, if the player already has the shotgun they cannot pickup
  a second shotgun found at a later time, thus not getting the ammo that
  comes with it. This breaks the balancing. If set to `1` a weapon can
  be picked up if a) the player doesn't have it or b) it wasn't already
  picked up by another player. Defaults to `1`.

* **coop_elevator_delay**: In coop it's often hard to get on the same
  elevator together, because they're immediately triggered once the
  first player steps on it. This cvar sets a delay for the elevator to
  wait before moving, so other players have some time to get on it.
  Defaults to `1.0` (seconds).

* **coop_baseq2 (Ground Zero only)**: In Ground Zero, entity spawnflags
  (which difficulty modes / game modes level entities spawn in) are
  interpreted a bit differently. In original Quake 2, if an entity is
  set to not spawn on any difficulty, it is treated as deathmatch-only,
  however, in Ground Zero this same condition is treated as coop-only.
  This causes maps made for original Quake 2, including the entire
  Quake 2 campaign, to not work correctly when played in Ground Zero
  in co-op mode. This cvar, when set to 1, restores the original
  interpretation and enables you to play original Quake 2 maps in
  Ground Zero co-op. Though keep in mind that Ground Zero maps will
  not work correctly when this cvar is enabled so remember to
  disable it again before playing Ground Zero maps in co-op. By
  default this cvar is disabled (set to 0).

* **g_commanderbody_nogod**: If set to `1` the tank commanders body
  entity can be destroyed. If the to `0` (the default) it is
  indestructible.

* **g_footsteps**: If set to `1` (the default) footstep sounds are
  generated when the player is on ground and faster than 255. This is
  the behaviour of Vanilla Quake II. If set to `2` footestep sound
  always generated as long as the player is on ground. If set to `3`
  footsteps are always generated. If set to `0` footstep sounds are
  never generated. Cheat protected to `1`. Note that there isn't a
  reliable way to figure out if the player is on ground. Footsteps
  may not be generated in all circumstances, especially when the player
  is moving over stairs and slopes.

* **g_monsterfootsteps**: If set to `1` monster footstep are generated.
  By default this cvar is disabled (set to 0). Additional footstep
  sounds are required. See the installation guide for details.

* **g_fix_triggered**: This cvar, when set to `1`, forces monsters to
  spawn in normally if they are set to a triggered spawn but do not
  have a targetname. There are a few cases of this in Ground Zero and
  The Reckoning. This cvar is disabled by default to maintain the
  original gameplay experience.

* **g_machinegun_norecoil**: Disable machine gun recoil in single player. 
  By default this is set to `0`, this keeps the original machine gun 
  recoil in single player. When set to `1` the recoil is disabled in
  single player, the same way as in multiplayer.  
  This cvar only works if the game.dll implements this behaviour.

* **g_quick_weap**: If set to `1`, both *weapprev* and *weapnext*
  commands will "count" how many times they have been called, making
  possible to skip weapons by quickly tapping one of these keys.
  By default this cvar is set to `1`, and will only work if the
  game.dll implements this behaviour.

* **g_swap_speed**: Sets the speed of the "changing weapon" animation.
  Default is `1`. If set to `2`, it will be double the speed, `3` is
  the triple... up until the max of `8`, since there are at least 2
  frames of animation that will be played compulsorily, on every weapon.
  Cheat-protected, has to be a positive integer. As with the last one,
  will only work if the game.dll implements this behaviour.

* **g_disruptor (Ground Zero only)**: This boolean cvar controls the
  availability of the Disruptor weapon to players. The Disruptor is
  a weapon that was cut from Ground Zero during development but all
  of its code and assets were still present in the source code and
  the released game. This is basically a player-held version of the
  2nd Widow boss' tracker weapon - a black-ish ball of energy.
  When this cvar is set to 1 you can use the "give Disruptor" and
  "give rounds X" commands to give yourself the weapon and its ammo,
  and its items, weapon\_disintegrator and ammo\_disruptor, can be
  spawned in maps (in fact, some official Ground Zero maps contain
  these entities). This cvar is set to 0 by default.

* **nextdemo**: Defines the next command to run after maps from the
  `nextserver` list. By default this is set to the empty string.

* **nextserver**: Used for looping the introduction demos.


## Audio

* **ogg_enable**: Enable Ogg/Vorbis music playback.

* **ogg_ignoretrack0**: Normally Quake II disables the background music
  if a major objective has been archived by setting the music track to
  0.  Setting this cvar to `1` disables this behavior, the music keeps
  playing.

* **ogg_pausewithgame**: When `1` the background is paused when the game
  is paused. Defaults to `0`.

* **ogg_shuffle**: Ogg/Vorbis playback mode. Supported modes are:
  - `0`: Loop the current track (the default).
  - `1`: Play the current track once, then stop.
  - `2`: Play all available tracks in a linear sequence.
  - `3`: Shuffle through the available tracks.

* **s_muteonfocusloss**: If set to `1` (the default) all sounds are
  muted when the game looses focus. Sound are unmuted when the game
  regains focus.

* **s_sdldriver**: Can be set to the name of a SDL audio driver. If set
  to `auto` (the default), SDL chooses the driver. If set to anything
  else the given driver is forced, regardless if supported by SDL or the
  platform or not.

  On iOS the audio session is the ambient category, whatever the
  driver: the game follows the silent switch and mixes with the audio
  of other apps. SDL's own default there is a playback session, which
  does neither.

* **s_underwater**: Dampen sounds if submerged. Enabled by default.

* **s_bsp_soundpos**: Controls the positioning of sounds played by BSP
  entities (doors, elevators, ...). Internally the game treats the
  positioning of BSP entities differently. There are 3 modes to choose
  from. Values 0, 1 and 2.

  - **Mode 0: Original behavior**: Sound events play at the center of
    the bbox, loop sound plays at origin (0, 0, 0). This is how you are
    used to it functioning from before this cvar was added. This is
    considered a bug/code oversight, hence why the default value is 1.
  - **Mode 1: Bbox center**: Both sound events and loop sounds play at
    bbox center. While this works better than mode 0, very long/large
    bboxes, like the elevator at the start of jail4, will play the sound
    too far away for you to hear it.
  - **Mode 2: Bbox edge**: Sound events and loop sounds play at the edge
    of the bbox, closest to the player. Now you hear the bbox itself,
    rather than the point at its center.

  Default mode is 1.

* **cl_audiopaused**: If set to `1` the sounds pause when the game does.


## Graphics (all renderers)

* **cin_force43**: If set to `1` (the default) cinematics are displayed
  with an aspect ratio of 4:3, regardless what the actual windows size
  or resolution is.

* **con_backaspect**: If set to `1` the console background picture keeps
  its 4:3 shape instead of being stretched to the window. It is scaled
  to cover the window, centred across and anchored at the console's
  lower edge, and what lies outside the window is cropped: the top of
  the picture on a wide window, its sides on a narrow one. On a 4:3
  window it draws the same as `0`. The software renderer refuses a
  picture that leaves the window, which this one does on a window narrower
  than 4:3. It therefore always stretches (it could draw the cropped
  rectangle on a window as wide as 4:3 or wider, but is kept the same). The renderer that is loaded decides, not
  `vid_renderer`, which may have been changed without a `vid_restart`. Defaults to `0`. Saved with the
  configuration.

* **cl_gun**: Decides whether the gun is drawn. If set to `0` the gun
  is omitted. If set to `1` the gun is only drawn if the FOV is equal
  or smaller than 90. This was the default with Vanilla Quake II. If set
  to `2` the gun is drawn regardless of the FOV. This is the default
  in Portmanbeau Quake II.

* **fov**: Sets the field of view.

* **r_gunfov**: The weapons are rendered with a custom field of view,
  independently of the global **fov**, so they are not distorted at high
  FOVs. A value of `75` should look identical to the old code at `fov
  90`, it defaults to `80` because that looks a bit better. Set to `-1`
  for the same value as `fov`.

* **hand**: Which hand the view weapon is held in: `0` right (the
  default), `1` left (the weapon is drawn mirrored), `2` centered (the
  weapon is then not drawn). It is also sent to the server.

* **r_drawentities**: If set to `0`, no entity (model, door, platform)
  is drawn. Defaults to `1`.

* **r_lerpmodels**: If set to `1` (the default), the models are drawn
  interpolated between their last two animation frames; at `0` they
  jump from frame to frame.

* **horplus**: If set to 1 (the default) the horplus algorithm is used
  to calculate an optimal horizontal and vertical field of view,
  independent of the window or screen aspect ratio or resolution.

* **r_consolescale** / **r_hudscale** / **r_menuscale** and
  **crosshair_scale**: Scale the console, the HUD, the menu and the
  crosshair. The value given is the scale factor, a factor of `1` means
  no scaling. Values greater `1` make the objects bigger, values lower 1
  smaller. The special value `-1` (default) sets the optimal scaling
  factor for the current resolution. All cvars are set through the
  scaling slider in the video menu.

* **crosshair_color_r** / **crosshair_color_g** / **crosshair_color_b**:
  Set the red, green and blue color components of the crosshair. Each
  value is a float between `0` and `1`. Defaults to `1` for all three
  components (white / no tint). Can also be set through the crosshair
  color option in the options menu.

* **r_customheight** / **r_customwidth**: Specifies a custom resolution,
  the windows will be *r_customheight* pixels high and *r_customwidth*
  pixels wide. Set *r_mode* to `-1` to use the custom resolution.

* **iOS, iPadOS and the screen's own size**: On iPhone and iPad the
  window is always the whole screen, in landscape (either way up), at
  the screen's own pixels, and everything the game draws lies inside
  the safe area, clear of the rounded corners, the sensor housing and
  the home indicator. So `r_mode` (always `-2` there), `vid_fullscreen`
  (always `2`), `vid_displayindex` (one screen), `vid_highdpiaware`
  (always on) and `r_vsync` (a layer is always presented in step with
  the display) have no effect: the engine sets the first two itself
  when it starts the video, and the video menu shows the mode,
  fullscreen, display, vertical sync and renderer items greyed with
  their reasons. A change of the safe area, as in a turn between the
  two landscape positions, restarts the video.

* **r_farsee**: Normally Quake II renders only up to 4096 units. If set
  to `1` the limit is increased to 8192 units. This helps with some
  custom maps and is problematic with other custom maps.

* **r_fixsurfsky**: Some maps misuse sky surfaces for interior
  lighting. The original renderer had a bug that made such surfaces
  mess up the lighting of entities near them. If set to `0` (the
  default) the bug is there and maps look like their developers
  intended. If set to `1` the bug is fixed and the lighting correct.

* **r_vsync**: Enables the vsync: frames are synchronized with
  display refresh rate, should (but doesn't always) prevent tearing.
  Set to `1` for normal vsync and `2` for adaptive vsync. The Metal
  renderer turns the display sync of its layer on for any value but `0`
  and takes a change without `vid_restart`.

* **r_anisotropic**: Anisotropic filtering. Possible values are
  dependent on the GPU driver, most of them support `1`, `2`, `4`, `8`
  and `16`. Anisotropic filtering gives a huge improvement to texture
  quality by a negligible performance impact.

* **r_msaa_samples**: Full scene anti aliasing samples. The number of
  samples depends on the GPU driver, most drivers support at least `2`,
  `4` and `8` samples. If an invalid value is set, the value is reverted
  to the highest number of samples supported. Especially on OpenGL 3.2
  anti aliasing is expensive and can lead to a huge performance hit, so
  try setting it to a lower value if the framerate is too low.

* **r_videos_unfiltered**: If set to `1`, don't use bilinear texture
  filtering on videos (defaults to `0`).

* **r_2D_unfiltered**: If set to `1`, don't filter textures of 2D
  elements like menus and the HUD (defaults to `0`).

* **r_lerp_list**: List separated by spaces of 2D textures that *should*
  be filtered bilinearly, even if `r_2D_unfiltered` is set to `1`.

* **r_nolerp_list**: List separated by spaces of textures omitted from
  bilinear filtering (mostly relevant if `r_2D_unfiltered` is `0`).
  Used by default to exclude the console and HUD font and crosshairs.
  Make sure to include the default values when extending the list.

* **r_retexturing**: If set to `1` (the default) and a retexturing pack
  is installed, the high resolution textures are used.

* **r_scale8bittextures**: If set to `1`, scale up all 8bit textures.
  The Metal renderer takes it as the OpenGL 1.4 renderer does: an 8bit
  image outside the small-picture atlas is scaled by 2, or by 3 when it
  is lerped and the window is at least 720 pixels high, with `scale2x` or
  `scale3x`, and every upload, the atlas of small pictures (the HUD digits
  and icons) included, is smoothed. The pictures in the atlas are not scaled. Applies to images loaded after it
  is set. The smoothing of the atlas follows the cvar at the atlas's next
  upload, so pictures already in the atlas change when a new picture enters
  it.

* **r_shadows**: Enables rendering of shadows. Quake IIs shadows are
  very simple and are prone to render errors.

* **vid_displayrefreshrate**: Sets the displays refresh rate. The
  default `-1` let the game determine the refresh rate automatically.
  Often the default setting is okay, but some graphics drivers report
  wrong refresh rates. For example 59hz are reported while the display
  has 59.95hz.

* **vid_gamma**: The value used for gamma correction. Higher values look
  brighter. The OpenGL 3.2 and Vulkan renderers apply this to
  the window in realtime via shaders (on all platforms). The
  OpenGL 1.4 renderer requires a
  `vid_restart` since gamma values are precomputed at renderer start
  only, and so does the Metal renderer. On MacOS the situation is similar; `vid_restart` is required.
  This cvar is also set by the brightness slider in the video menu.

* **vid_fullscreen**: Sets the fullscreen mode. When set to `0` (the
  default) the game runs in window mode. When set to `1` the games
  switches the display to the requested resolution. That resolution
  must be supported by the display, otherwise the game tries several
  steps to recover. When set to `2` a fullscreen window is created.
  It's recommended to use the displays native resolution with the
  fullscreen window, use `r_mode -2` to switch to it.  
  The cvar always holds the mode the window was actually made with,
  and that value is saved to `config.cfg`: a fraction is cut to its
  whole number (`2.5` is used and saved as `2`), and when the game
  falls back to a window, because the requested `r_mode` is invalid
  or the fullscreen window could not be made, it is set to `0`, so
  the next start opens a window too.

* **vid_highdpiaware**: When set to `1` the client is high DPI aware
  and scales the window (and thus the requested resolution) by the
  scaling factor of the underlying display. Example: The displays
  scaling factor is 1.25 and the user requests 1920x1080. The client
  will render at 1920\*1.25x1080\*1.25=2400x1350.  
  When set to `0` the client leaves the decision if the window should
  be scaled to the underlying compositor. Scaling applied by the
  compositor may introduce blur and sluggishness.  
  High dpi awareness is supported under Wayland and on Apple platforms
  (macOS, iOS, iPadOS, tvOS and visionOS). On a Retina display of
  scaling factor 2 a 640x480 window draws 1280x960 pixels and the
  default 2D scale is 2, so menus, console and HUD keep their size on
  the screen; with `0` it draws 640x480 pixels, which the system
  stretches to the window. Changing it needs a `vid_restart`.  
  Defaults to `1`.

* **vid_maxfps**: The maximum framerate. *Note* that vsync (`r_vsync`) 
  also restricts the framerate to the monitor refresh rate, so if vsync
  is enabled, the game won't render more than frame than the display can
  show. Defaults to `300`.  
  Related to this: *cl_maxfps* and *cl_async*.

* **vid_pauseonfocuslost**: When set to `0` (default) the game is paused
  as soon as its window loses focus. It will only work where the game can
  be paused, e.g. not in multiplayer. If `1`, the game continues to run
  in the background. Setting it as `2` is the same as `0`, but the game
  will unpause right when its window gets back to being on focus.

* **vid_renderer**: Selects the renderer library. Possible options are
  `gl3` (the default) for the OpenGL 3.2 renderer, `gl1` for the
  original OpenGL 1.4 renderer and `soft` for the software renderer;
  on macOS also `metal` for the native Metal renderer, when it is built
  (`make ref_metal`). When the selected renderer fails to load the next
  one is tried, in the order `gl3`, `gl1`, `soft`; on macOS `gl3`,
  `metal`, `gl1`, `soft`.


## Graphics (GL renderers only)

* **gl_zfix**: Sometimes two or even more surfaces overlap and flicker.
  If this cvar is set to `1` the renderer inserts a small gap between
  the overlapping surfaces to mitigate the flickering. This may make
  things better or worse, depending on the map.

* **gl_polyblend**: Toggles the palette blending effect, a.k.a. the
  "flash" you see when getting injured or picking up an item. In GL1 is
  also used for looking underwater. Default is `1` (enabled).

* **gl_znear**: Sets the distance to the *near depth clipping plane* of
  the player view. Default is `4`. Reducing it to `3.2` or similar may
  allow some weapon animations to not get 'clipped' by the player view
  (e.g. railgun recoil), at the risk of heavy glitches with some
  hardware / driver configurations.

* **gl_texturemode**: How textures are filtered.

  - `GL_NEAREST`: No filtering (using value of *nearest* source pixel),
    mipmaps not used
  - `GL_LINEAR`: Bilinear filtering, mipmaps not used
  - `GL_LINEAR_MIPMAP_NEAREST`: The default - Bilinear filtering when
    scaling up, using mipmaps with nearest/no filtering when scaling
    down

  Other supported values: `GL_NEAREST_MIPMAP_NEAREST`,
  `GL_NEAREST_MIPMAP_LINEAR`, `GL_LINEAR_MIPMAP_LINEAR`


## Graphics (OpenGL 1.4 only)

* **gl1_intensity**: Sets the color intensity. Must be a floating point
  value, at least `1.0` - default is `2.0`. Applied when textures are
  loaded, so it needs a `vid_restart` (the Metal renderer takes it too).

* **gl1_minlight**: Sets the minimum light level on screen. Increasing
  this illuminates darker scenes, at the expense of the atmosphere.
  Possible values go from `0` (default, show the full spectrum of light
  & dark) to `255` (equal to `r_fullbright 1`). Requires `vid_restart`.

* **gl1_multitexture**: Enables (`1`, default) the blending of color and
  light textures on a single drawing pass; disabling this (`0`) does one
  pass for color and another for light. Requires a `vid_restart` when
  changed.

* **gl1_overbrightbits**: Enables overbright bits, brightness scaling of
  lightmaps and models. Higher values make shadows less dark. Possible
  values are `0` (no overbright bits), `1` (more correct lighting for
  liquids), `2` (scale lighting by factor 2), and `4` (scale by factor
  4). Applied in realtime, does not need `vid_restart`.

* **gl1_particle_size**: The size of a particle point at a window
  height of 480; it is scaled with the height. Defaults to `40`. The
  Metal renderer takes it too.

* **gl1_particle_min_size**, **gl1_particle_max_size**: The smallest
  and the largest size a particle point is given after the distance
  attenuation. Default to `2` and `40`. Taken when the renderer starts,
  with point parameters only. The Metal renderer takes them too.

* **gl1_particle_att_a**, **gl1_particle_att_b**,
  **gl1_particle_att_c**: The distance attenuation of the point size,
  `size / sqrt(a + b d + c d^2)` for a distance `d`. Default to `0.01`,
  `0` and `0.01`. Taken when the renderer starts, with point parameters
  only. The Metal renderer takes them too.

* **gl1_particle_square**: If set to `1` particles are rendered as
  squares (the Metal renderer takes it too).

* **gl1_pointparameters**: If set to `1` (default) and the driver
  offers point parameters (`glPointParameterf`), particles are drawn as
  points whose size shrinks with distance. If set to `0`, or the driver
  lacks them, or `gl1_stereo` is a split-screen mode (`6` or `7`) with
  a camera separation, particles are drawn as small textured triangles
  instead. Checked when the renderer starts, so it needs a
  `vid_restart` (the Metal renderer takes it too).

* **gl1_stencilshadow**: If `gl_shadows` is set to `1`, this makes them
  look a bit better (no flickering) by using the stencil buffer. Does
  not work when `gl1_stereo` is `3`, `4` or `5`.

* **gl1_flashblend**: With `1`, each dynamic light is drawn as a glowing
  blob instead of being added to the lightmaps of the surfaces it
  reaches (the Metal renderer takes it too). Defaults to `0`.

* **gl1_dynamic**: With `0`, the lightmaps are no longer updated after
  the map is loaded: the animated lightstyles and the dynamic lights
  leave the world as it was built (the Metal renderer takes it too).
  Defaults to `1`.

* **gl1_waterwarp**: Intensity of the "squeeze/stretch" effect on the
  FOV when diving underwater. Can be any floating point number, `0`
  disables it (Vanilla Quake II look). Default `1.0`.

* **gl1_tilerendering**: Available only in GL1, this controls whether
  the optimizations meant for *tile-based* rendering devices
  (mainly SBCs) are also applied in GL1. These optimizations may backfire
  on traditional *immediate mode* GPUs (desktop), so by default this is
  `0`. Enabling this requires `gl1_multitexture 1` & `vid_restart`.


## Graphics (OpenGL 3.2 only)

* **gl3_debugcontext**: Enables the OpenGL 3.2 renderers debug context,
  e.g. prints warnings and errors emitted by the GPU driver.  Not
  supported on macOS. This is a pure debug cvar and slows down
  rendering.

* **gl3_intensity**: Sets the color intensity used for 3D rendering.
  Similar to OpenGL 1.4 `gl1_intensity`, but more flexible: can be any
  value between 0.0 (completely dark) and 256.0 (very bright).  Good
  values are between `1.0` and `2.0`, default is `1.5`.  Applied in
  realtime via shader, so it does not need a `vid_restart`.

* **gl3_intensity_2D**: The same for 2D rendering (HUD, menu, console,
  videos)

* **gl3_overbrightbits**: Enables overbright bits, brightness scaling of
  lightmaps and models. Higher values make shadows less dark. Similar
  to OpenGL 1.4 `gl1_overbrightbits`, but allows any floating point
  number. Default is `1.3`. In the OpenGL 3.2 renderer, no lighting
  fixes for water are needed, so `1.0` has no special meaning.

* **gl3_particle_size**: The size of particles - Default is `40`.

* **gl3_particle_fade_factor**: "softness" of particles: higher values
  look less soft. Defaults to `1.2`. A value of `10` looks similar to
  the OpenGL 1.4 particles.

* **gl3_particle_square**: If set to `1`, particles are rendered as
  squares, like in the old software renderer or Quake 1. Default is `0`.

* **gl3_colorlight**: When set to `0`, the lights and lightmaps are
  colorless (greyscale-only), like in the original soft renderer.
  Default is `1`.

* **gl3_usefbo**: When set to `1` (the default), an OpenGL Framebuffer
  Object is used to implement a warping underwater-effect (like the
  software renderer has). Set to `0` to disable this, in case you don't
  like the effect or it's too slow on your machine.


## Graphics (Software only)

* **sw_gunzposition**: Z offset for the gun. In the original code this
  was always `0`, which will draw the gun too near to the player if a
  custom gun field of view is used. Defaults to `8`, which is more or
  less optimal for the default gun field of view of 80.

* **sw_colorlight**: enable experimental color lighting.


## Graphics (Metal only)

The native Metal renderer, `vid_renderer metal`, is built on macOS
only, with `make ref_metal`, and is listed in the video menu there
when it is built (with the OpenGL 1.4 renderer's settings, which it
takes). Its frames are judged against the OpenGL 1.4 renderer (see
`doc/100_renderpins.md`). It draws the 2D elements, cinematics,
the screen blend and the world's opaque surfaces with their lightmaps,
animated lightstyles, `gl1_overbrightbits` and dynamic lights, the sky
box, the warped water, the translucent and the flowing surfaces, the
brush entities (doors and platforms), the alias models with their
shells, shadows and the view weapon, sprites, beams, the flash
blobs and the particles (`gl1_particle_*`, `gl1_pointparameters`). It takes the OpenGL 1.4 renderer's `r_drawentities`,
`r_lerpmodels`, `hand`, `r_gunfov`, `r_shadows`, `gl1_stencilshadow`
and `gl1_flashblend` as that renderer does, and `gl1_dynamic` (`0`
leaves every lightmap as it was built: no animated lightstyle and no
dynamic light, as in that renderer). `gl_zfix`, `gl1_ztrick` (the OpenGL 1.4 renderer's alternating depth
range) and `gl_showbbox` (its outline of each model's bounding box) are
not taken. A brush entity drawn translucent (which no map of the game
has) is not drawn.

* **metal_stats**: When set to `1`, the Metal renderer prints one line
  per screenshot (so per frame the pin harness captures) and one at
  shutdown: frames ended (those without a drawable included), frames
  without a drawable (not presented,
  for example while the window cannot be shown), frames skipped because
  the GPU still held every frame slot, command buffers that failed,
  readbacks, calls to drawing functions it does not implement yet,
  draws and uploads made and skipped, pipeline states (and those made
  after initialisation), its Metal objects still alive, and the sky,
  warped, translucent and flowing surfaces of the last 3D view whose draw
  it refused (a polygon too large for the sky clip, no room in the frame
  ring, a missing texture), the dynamic lights of the last 3D view it left
  out (a position, colour or intensity that is not a finite number or is
  out of bounds), and the entities of the last 3D view it did not draw:
  sprites, beams, null models and models whose draw it refused (a
  position or angle that is not a finite number, no room in the frame
  ring, a missing texture), the particles of the last 3D view it did not
  draw, and translucent brush models. When a
  map is loaded it also prints the vertex, index and surface-record counts
  and bytes of the map's retained buffers, and the number of its lightmap
  slices. The
  teardown line with its object counts (devices, queues, views,
  textures, buffers and states: the shader library, pipeline,
  depth-stencil and sampler states) is printed at every shutdown
  regardless. With a MetalFX scaler on (`r_metalfx`) it also prints a
  line on every 60th scaled view and on each view whose history is
  dropped: the mode, the input and output sizes, the reasons for the drop
  and the GPU time of the scaler's pass (the pass then runs in a command
  buffer of its own); `2` prints that line on every scaled view. Defaults
  to `0`.

* **metal_lightmap_filter**: How the Metal renderer filters the lightmaps
  of the world. `0` (the default) is the bilinear filter every renderer
  uses, and draws what it drew before this cvar existed. `1` filters each
  lightmap sample with a cubic B-spline (four bilinear taps), which
  smooths light gradients and shadow edges and removes the diamond
  pattern the bilinear filter leaves on them. Every tap stays half a
  luxel inside the lightmap of its own face, so light never bleeds in
  from the neighbouring face stored beside it, and the result is never
  brighter than the brightest or darker than the darkest luxel around the
  sample. A face with no lightmap, and the flowing surfaces, are not
  filtered either way. Only `0` and `1` are values: any other value is
  `0`. Other renderers do not have it. Saved with the
  configuration; applies from the next frame.

* **metal_2d_sharp**: How the Metal renderer samples the 2D pictures (HUD,
  menus, the console background) that are filtered. `0` (the default) is
  the bilinear filter, which blurs a picture drawn at a large scale. `1`
  draws each texel of the picture as a block with no blur at an integer
  scale, where the result equals nearest filtering, and at a scale that
  is not an integer keeps the blocks and blends one screen pixel across
  the edge between two texels. Pictures on `r_nolerp_list` or drawn with
  `r_2D_unfiltered 1` are already unfiltered and are not changed, nor are
  the cinematics. Only `0` and `1` are values: any other value is `0`.
  Other renderers do not have it. Saved with the configuration; applies
  from the next frame.

* **metal_colorspace**: The colour space the Metal renderer tags its
  layer with. `0` (the default) leaves the layer's own colour space, as
  before: the system gives it `kCGColorSpaceSRGB` for this pixel format
  on macOS, and setting `1` and then `0` again restores that value. `1` tags the layer `kCGColorSpaceSRGB`, so the system
  converts the game's sRGB-era colours to the display's profile. Only `0`
  and `1` are values: any other value is `0`. Other renderers do not have
  it. Saved with the configuration; applies from the next frame. The
  readback of the frame (screenshots, the pins) is taken before the
  layer's colour space comes into it and does not change.

* **metal_particle_cap**: How the Metal renderer limits the size of a
  particle point (`gl1_particle_max_size`, 40 pixels by default). `0`
  (the default) is the limit every renderer uses, and draws what it drew
  before this cvar existed. `1` scales the limit with the height of the
  3D view (the window's height when the view fills it) like `gl1_particle_size` is scaled, by `height / 480`, so particles
  close to the camera keep the proportions they have at a height of 480
  (at 1440 lines the limit is 120 pixels, at 2880 lines 240). At a height
  of 480 both values are the same. The size is still never above what
  the point style allows (256 pixels for smooth points, 8191 for
  squares). Without point parameters (`gl1_pointparameters 0`) particles
  are triangles and have no limit. Only `1` and `0` are
  values: any other value is `0`. Other renderers do not have it. Saved with the
  configuration; applies from the next frame.

The `r_metalfx*` cvars choose the MetalFX upscaler of the Metal
renderer's 3D view (decision 0011), a development option for comparing
the scalers. The 3D view is drawn at a smaller size and MetalFX scales it
to the view's rectangle (the whole window at the full screen size); the status
bar, the menus and the console are drawn after it at the window's size.
With every mode off (the default) nothing of MetalFX is made and the
picture is the same as before these cvars existed.

The video menu has an item for each of them (the scaler, its scale, frame
generation and the denoiser), on the "renderer options..." page of the
video menu, with the lighting items of each renderer. No item is hidden: one
that the selected renderer or the GPU does not have is greyed out, cannot be
changed, and shows the reason in the status bar when the cursor is on it.
The items write their cvars with `apply`, which restarts the renderer, and
show what the renderer kept. A scaler mode that `r_metalfx_modes` lacks is
marked `(n/a)` and skipped.

* **r_metalfx**: The scaler. `0` (the default) is none. `1` is
  `MTLFXSpatialScaler`, which scales the colour of each frame on its own.
  `2` is `MTLFXTemporalScaler`, which also takes the depth, how far each
  pixel moved and a sub-pixel jitter of the view, and keeps a history
  across frames. `3` is `MTLFXTemporalDenoisedScaler`, which needs a
  ray-traced effect; until one exists it runs as `2` and says so. A mode
  the GPU lacks is set back to the best mode below it that the GPU has,
  with a line in the console (`r_metalfx_modes` says which it has). A
  fraction is cut to the whole number below it, a value above `3` counts
  as `3`, and a negative value or one that is not a finite number as
  `0`. The history is dropped on a map load, a `vid_restart`, a frame
  drawn without the world, a change of the mode, of `r_metalfx_scale` or
  of the view's rectangle (the screen size, the window's size), and when the
  view moves more than 256 units at once (a teleport or a camera cut,
  the distance the client itself does not smooth); a change of `fov` and
  the player-setup menu's model do not drop it. With a scaled view the
  limit of a smooth particle point (256 pixels) is in the smaller view's
  pixels, so a particle above 256 pixels of the window is drawn smaller
  than without a scaler; this needs `gl1_particle_max_size` above 256.
  Other renderers do not have it. Saved with the configuration; applies
  from the next frame.

* **r_metalfx_interp**: Frame generation with `MTLFXFrameInterpolator`,
  a development option. Frame generation is not built yet: on every GPU
  `1` is set back to `0` with a line in the console (on a GPU without the
  interpolator the line says so). Defaults to `0`. Other renderers do not
  have it. Saved with the configuration.

* **r_metalfx_modes**: Read only, set by the Metal renderer when it
  starts: the modes the GPU supports (MetalFX's `supportsDevice:` for each
  scaler and for the frame interpolator), as the sum of `1` (off, always
  there), `2` (`r_metalfx 1`), `4` (`r_metalfx 2`), `8` (`r_metalfx 3`)
  and `16` (the frame interpolator). The menu uses it to grey out what the
  GPU lacks. A value written to it is put back. `1` where MetalFX is
  missing; under other renderers it is not set.

* **r_metalfx_scale**: The size the 3D view is drawn at with a scaler, as
  a fraction of the view's rectangle, width and height alike. `0` (the
  default) picks the GPU family's default: 1080 lines on Apple GPU family
  9 and later (M3 and later), 720 lines below, as a fraction of the
  view's height. Any value is kept between `1/3` (the largest ratio the
  scalers take, lower where the GPU reports a smaller one) and `1`, and
  each side is rounded up, so the view is never scaled by more than that
  ratio; at `1` the view is drawn at full size and the scaler still runs. A
  negative value or one that is not a finite number counts as `0`. Saved
  with the configuration; applies from the next frame.

The `r_rt_*` cvars belong to the Metal renderer's ray tracing, which is
being built: `r_rt_shadows` is its first effect, `r_rt_ao` its second and `r_rt_water` its third; with `r_rt_stats` set, the renderer
builds acceleration structures for the world, the doors and platforms
and every model the frame lists (not only those in view, since a model
out of view can still cast into it), which nothing reads. On a GPU without ray
tracing they do nothing (the renderer says so once). The development
switches `r_rt_gbuffer` and `r_rt_motion` and the settings beside them
choose how a view is drawn, on any Metal GPU, and leave the picture as it
is.

* **r_rt_ao**: When set to `1`, the Metal renderer darkens the world's
  creases and corners with ray-traced ambient occlusion: from the pixels
  of the world (one in four with the default `r_rt_ao_scale`) two rays
  (`r_rt_ao_rays`) look for nearby surfaces in the half-space in front of
  it, and the nearer what they meet, the less of the lightmap's light the
  pixel keeps, never less than `r_rt_ao_min` of it. Only the
  lightmap's light is darkened; the dynamic lights' added by
  `r_rt_shadows` are not (with `r_rt_shadows 0` the dynamic lights are
  part of the lightmap and are darkened with it), and models, water and
  the sky are drawn as before. The rays meet the world, the doors and
  platforms and the models, but not the view weapon or the player's own
  model; the sky is open, and water and translucent surfaces let them
  through. The rays' directions follow a blue-noise pattern that changes
  every frame, so without the accumulation over frames a later version
  adds the darkening is grainy. Needs a GPU with ray tracing and draws the
  view through the G-buffer (see `r_rt_gbuffer`); with `r_fullbright 1`
  there is no lightmap and it does nothing. With `0` (the default) nothing
  is traced or allocated and the picture is as without ray tracing. Saved
  with the configuration; applies from the next frame.

* **r_rt_ao_min**: The least of its lightmap's light a pixel keeps under
  `r_rt_ao`, from `0` to `1`: `0.4` (the default) keeps at least 40 % of
  it, `1` turns the darkening off. A value outside the range counts as
  the nearest end, one that is not a finite number as `0.4`. Saved with
  the configuration; applies from the next frame.

* **r_rt_ao_range**: How far, in world units, the rays of `r_rt_ao` look
  for surfaces (`128` by default, about the height of two players): a
  surface farther away does not darken the pixel, a nearer one darkens it
  more the nearer it is. A value below `1` counts as `1`, one above `8192`
  as `8192`, one that is not a finite number as `128`. Saved with the
  configuration; applies from the next frame.

* **r_rt_ao_rays**: How many rays each traced pixel of `r_rt_ao` sends:
  `2` (the default) or `1`, which costs less and makes the darkening
  grainier. A value below `1.5` counts as `1`, one above as `2`, one that
  is not a finite number as `2`. In the video menu's renderer options as
  "AO rays", greyed on other renderers and while `r_rt_ao` is `0`. Saved
  with the configuration; applies from the next frame.

* **r_rt_ao_scale**: The share of the 3D view's width and height that
  `r_rt_ao` traces its rays at: `1` traces every pixel, `0.5` (the
  default) one pixel of each 2 x 2 block, a quarter of the rays, and
  `0.33` one of each 3 x 3 block. Below `1` each world pixel takes the
  darkening of the traced pixels next to it that lie on its own surface,
  so an edge between two surfaces stays sharp, and the darkening is a
  little softer than at `1`. It is a share of the size the view is drawn
  at, so with a MetalFX scaler (`r_metalfx`) it applies to the scaler's
  input. A value is taken as the nearest of the three (`1` for `0.67` and
  above, `0.33` below `0.4`); `0`, a negative value or one that is not a
  finite number counts as `0.5`. In the video menu's renderer options as
  "AO trace size", greyed on other renderers and while `r_rt_ao` is `0`.
  Saved with the configuration; applies from the next frame.

* **r_rt_ao_seed**: A development setting of the Metal renderer's
  `r_rt_ao`: with `0` (the default) the rays' blue-noise pattern moves on
  every frame; with a whole number from `1` to `65535` it is fixed at that
  step, the same on every frame, so that two runs draw the same frames
  (the pin scenarios set it). A fraction is cut to the whole number below
  it, a value above `65535` counts as `65535`, and one below `1` or not a
  finite number as `0`. Applies from the next frame.

* **r_rt_ao_show**: A development view of the Metal renderer's
  `r_rt_ao`: with it on, `1` replaces the 3D view by the share of its
  lightmap's light each world pixel keeps, as a grey (white keeps all of
  it, black is drawn where the view shows no world), `2` by the pass's
  classes: white not darkened, blue darkened, red darkened to
  `r_rt_ao_min`, black not a world pixel. The view weapon, models drawn
  over the world, particles and translucent surfaces are left out of
  these views. `0` (the default) shows the picture; a value above `2`
  counts as `2`, one that is not a finite number as `0`, and a fraction is
  cut to the whole number below it. Cheat-protected: reset to `0` in
  multiplayer. Applies from the next frame.

* **r_rt_gbuffer**: A development switch of the Metal renderer. When
  set to anything but `0`, a view with the world is drawn through the
  G-buffer the ray-traced effects will build on: the world and the opaque
  models are drawn into six images (depth, the surface's direction, a
  material number, how far each pixel moved since the last frame, the
  lightmap and the texture colour), a second pass multiplies them back
  into the picture, and the view weapon, sprites, particles, beams and
  translucent surfaces are drawn over it as before. The picture is the
  same within one level of a colour channel (the images keep 8 bits of the
  texture and of the light where the default path multiplies them
  unrounded). With `0` (the default) nothing of it is made: the default
  path is faster when no ray-traced effect is on (see
  `docs/plans/metal-raytracing-design.md`, section 3). The menu's player
  model is always drawn the default way. Applies from the next frame.

* **r_rt_gbuffer_scale**: A development setting of the Metal renderer:
  with `r_rt_gbuffer` or `r_rt_motion` on, the 3D view is drawn at this
  fraction of the window's size (above `0` and below `1`; any other value
  is `1`, the default) and stretched to it, a stand-in for an upscaler
  that will take its place; the status bar, the menus and the console
  stay at the window's size. Applies from the next frame.

* **r_rt_gbuffer_show**: A development view of the Metal renderer: with
  `r_rt_gbuffer` or `r_rt_motion` on, one of the images replaces the 3D
  view: `1` the texture colour, `2` the surface direction, `3` the
  material number, `4` the motion (red and green are half plus the motion
  in pixels over 16, blue is set where something was drawn), `5` the
  light, `6` the depth; `1` to `3` and `5` only with `r_rt_gbuffer`. `0`
  (the default) shows the picture; a value above `6` counts as `6`, a
  value that is not a finite number counts as `0`, and a fraction is cut
  to the whole number below it. Cheat-protected: reset to `0` in
  multiplayer. Applies from the next frame.

* **r_rt_gunshadow**: When set to `1`, the view weapon is part of the
  ray-traced scene, so that later ray-traced effects shade it and let it
  cast shadows; with `0` (the default) it is left out, since from the
  player's eye it would shadow most of the view. It is not part of the
  scene either way when `hand` is `2` (the weapon is not drawn). Saved
  with the configuration.

* **r_rt_jitter_x**, **r_rt_jitter_y**: A development setting of the
  Metal renderer: with `r_rt_gbuffer` or `r_rt_motion` on, the 3D view is
  drawn moved by this many pixels to the right and down (a fraction of a
  pixel, as an upscaler's sub-pixel jitter); the motion images do not
  include the move. `0` (the default) draws it in place. Applies from the
  next frame.

* **r_rt_motion**: A development switch of the Metal renderer. When set
  to anything but `0` (and `r_rt_gbuffer` is `0`), a view with the world
  is drawn as by default and also keeps its depth and how far each pixel
  moved since the last frame, which an upscaler needs; the picture is the
  same. With `0` (the default) neither is kept. Applies from the next
  frame.

* **r_rt_motion_test**: A development check of the Metal renderer's
  motion images: when set to an angle in degrees, the last frame is taken
  to be this one turned by that angle about the camera's vertical axis,
  so that every pixel's motion is that of a known turn (shown with
  `r_rt_gbuffer_show 4`). `0` (the default) uses the real last frame.
  Applies from the next frame.

* **r_rt_shadows**: When set to `1`, the Metal renderer gives the dynamic
  lights (muzzle flashes, explosions, rockets in flight) ray-traced shadows
  on the world: instead of adding each light into the lightmaps, it adds
  the same light (the same fall-off with distance) to each pixel the light
  reaches without anything in the way, so a wall or a pillar between the
  light and a surface leaves that surface unlit by it. At a pixel the four
  lights that add the most are traced; any further one adds its light
  unshadowed. Translucent surfaces and translucent water let a third or
  two thirds of the light through, as they let the view through. Models
  are lit as before, by the light at their position without a shadow, but
  they cast shadows, except on a light they carry: a rocket does not
  shadow its own light, nor a monster its muzzle flash. Needs a GPU with
  ray tracing and draws the view
  through the G-buffer (see `r_rt_gbuffer`); with `gl1_dynamic 0`,
  `r_fullbright 1` or `gl1_flashblend 1` there are no dynamic lights on
  the lightmaps and it does nothing. With `0` (the default) nothing is
  traced or allocated and the picture is as without ray tracing. Saved
  with the configuration; applies from the next frame.

* **r_rt_shadows_show**: A development view of the Metal renderer's
  ray-traced shadows: with `r_rt_shadows` on, `1` replaces the 3D view by
  white where no dynamic light is in range and black where one is (the
  mask of the check that no light is counted twice), `2` by the pass's
  classes: black no light in range, blue in range but adding nothing,
  green traced and lit, red shadowed from at least one light. The view
  weapon, models drawn over the world, particles and translucent surfaces
  are left out of these views; on a frame with no dynamic light both are
  the view with none in range. `0` (the default) shows the picture; a
  value above `2` counts as `2`, one that is not a finite number as `0`,
  and a fraction is cut to the whole number below it. Cheat-protected:
  reset to `0` in multiplayer. Applies from the next frame.

* **r_rt_stats**: When set to `1`, the Metal renderer builds its
  ray-tracing acceleration structures and prints what they hold: when a
  map is loaded, the world's triangles by material (world, sky, water,
  translucent), the doors and platforms with their triangles, the bytes
  of each structure kind and the GPU time of the build; and when the map
  is left, `r_rt_stats` is set back to `0` or the renderer shuts down, a
  summary of the frames since the map was loaded, with the GPU time of the
  per-frame work (the models' updates and the rebuild of the scene's
  instance structure: least, median, 95th percentile and most) and the
  busiest frame's models and triangles. `2` also prints one line per
  frame: the instances, the models' triangles, how many model structures
  were built and updated, what was left out (beams, entities without a
  model, sprites, the view weapon, shells, models it cannot build) and the
  bytes held, and with `r_rt_shadows` on a line for the shadow pass: the
  pixels it ran on, those with a dynamic light in range, those that traced
  a ray and those shadowed, the rays and the pass's GPU time; with
  `r_rt_ao` on a line for the occlusion pass: the pixels it ran on, the
  world pixels that took a darkening, those darkened and those darkened to
  `r_rt_ao_min`, the pixels facing up (floors) and of them those darkened,
  the rays (at the size `r_rt_ao_scale` traces at), those that met a
  surface and the pass's GPU time; with
  `r_rt_water` on a line for the reflections: the pixels of the view, those
  it traced (every pixel, or with `r_rt_water_hits 1` the water and the
  pixels next to it), those of opaque and of translucent water, the rays
  from the eye that looked for translucent water, the water pixels whose
  reflection came from the picture, from a surface drawn on its own, from
  the sky in the picture, from the sky box and from nowhere, and the
  pass's GPU time; with `r_rt_water_hits 1` also each of its two parts
  (the list of its pixels, the trace) and the time from the first's start
  to the last's end, the pass's time being their sum (the water's marks
  are a draw of the G-buffer raster, timed with it, so there are no
  raster figures). Nothing
  is drawn differently. The objects it makes are
  counted as `rt` in the teardown line. With `0` (the default) nothing is
  built and nothing is allocated for ray tracing. Applies from the next
  frame.

* **r_rt_water**: When set to `1`, the Metal renderer gives water a
  ray-traced reflection, one bounce: from each pixel of water (the
  opaque water and the translucent water in front of the rest of the
  view) one ray goes off the surface in the mirror direction, the flat
  surface's direction tilted with the water's own warp (see
  `r_rt_water_normal`), so the reflection ripples as the texture does.
  What it meets is shown as the picture shows it, lightmaps, dynamic
  lights and the other ray-traced effects included; where it meets the
  sky, the sky in that direction. A surface of the world the view does
  not show (off the screen, or hidden behind something nearer) gives no
  reflection, and the water there is drawn as before; `r_rt_water_hits
  1` draws it instead. A model (a player, a monster, an item, a weapon),
  and at `r_rt_water_hits 0` also a door, a platform or a lift, that the
  view does not show is passed over: the reflection shows what lies
  behind it, as without it, so it does not show its outline. This works
  per face, and up to four hidden faces: past four (one hidden player
  with its weapon is four, a closed door two) the ray is traced against
  the map alone, so a model the view shows behind them is passed too,
  and the reflection there shows what lies behind it (at `0`, none),
  while a door or a platform there is met (at `0`, none where the view
  does not show it). The reflection is mixed into
  the water's colour by `r_rt_water_reflect`; translucent water keeps
  its transparency. The rays meet the world, the doors and platforms and
  the models, but not the view weapon or the player's own model. Needs a
  GPU with ray tracing and draws the view through the G-buffer (see
  `r_rt_gbuffer`) on a map with water, so every pixel that is not water is
  drawn as with `r_rt_gbuffer 1`: within one level of a colour channel of
  the picture without the effect. On a map without water it does nothing.
  With `r_rt_shadows_show` or `r_rt_ao_show` on, that view is shown without
  the reflections. With `0`
  (the default) nothing is traced or allocated and the picture is as
  without ray tracing. Saved with the configuration; applies from the
  next frame.

* **r_rt_water_normal**: How far `r_rt_water`'s reflections ripple: the
  water's warp tilts the surface's direction by up to this much
  (`0.08`, the default, is about 6 degrees, `0` a still mirror). The
  water's surface itself stays flat. A value below `0` counts as `0`, one
  above `1` as `1`, one that is not a finite number as `0.08`. Saved with
  the configuration; applies from the next frame.

* **r_rt_water_reflect**: How much of `r_rt_water`'s reflection is mixed
  into the water's colour, from `0` (none) to `1` (the reflection
  alone); `0.5` by default. Translucent water keeps its transparency
  either way. A value outside the range counts as the nearest end, one
  that is not a finite number as `0.5`. Saved with the configuration;
  applies from the next frame.

* **r_rt_water_hits**: How `r_rt_water` shades what a reflection meets.
  `0` (the default): from the picture alone, as described under
  `r_rt_water`; a surface of the world the view does not show gives no
  reflection.
  `1`: a surface of the map the view does not show (off the screen, or
  hidden behind something nearer) is drawn on its own, its texture and
  lightmap, with the dynamic lights but without their shadows or the
  ambient occlusion; the sky the view does not show comes from the sky
  box. At both settings a model (a
  player, a monster, an item, a weapon) the view does not show is passed over, so the
  reflection shows what lies behind it, and neither the model nor its
  outline. This works per face (a shown model's unseen far side is
  passed too) and up to four hidden model faces: past four the ray is
  traced against the map alone, so a model the view shows behind them is
  passed too, and the reflection there shows what lies behind it (at
  `0`, none). A brush
  model the view does not show (a door, a platform, a lift) is part of
  the map: at `1` it is drawn on its own like any other surface, so the
  reflection can show where it stands; at `0` it is passed over as a
  model is, within the same four faces (a closed door is two), so it
  leaves no hole in its outline either. Over the
  pool of the test map that fills the 2 to 9 % of the water that had no
  reflection, and changes no pixel outside the water. It costs more GPU
  time: the water's marks, one more draw of the G-buffer raster, and a list
  of the marked pixels before the rays, which only those pixels trace. The
  marks are written into the alpha of the G-buffer's normal attachment, so
  `1` finds the water pixels without a raster pass, depth reload or target
  of its own. The figures below were measured with the separate raster
  that setting used before, and are to be measured again. Measured on an Apple M6 over the
  test map's pool (about 41 % of the pixels water), the pass takes
  4.66 ms of GPU time at 1920 x 1080 against 3.40 ms at `0` with the
  frame rate held to a 60 Hz display, and 1.20 against 0.99 ms not held;
  12.24 against 9.44 ms at 5120 x 2880 held; with `r_metalfx 2`, 3.82
  against 3.46 ms held. A view without water in it costs 2.06 against
  1.33 ms at 1920 x 1080 held. At `0` the pass and its pictures are
  those before this setting existed but for the hidden models it passes
  over, and `0` needs nothing `1` needs:
  where the renderer cannot build what `1` needs, the console says so
  once and `1` draws as `0`.
  A value of `1` or more counts as `1`, a value below `1` or not a
  finite number as `0`. In the video menu's renderer options as "water
  reflections" (greyed while `r_rt_water` is off). Saved with the
  configuration; applies from the next frame.

* **r_rt_water_show**: A development view of the Metal renderer's
  `r_rt_water`: with it on, `1` replaces the picture by a mask, black
  on every pixel of water the pass found and white elsewhere (the pixels
  that must look as without the reflections), `2` by the pass's classes:
  black not water, green a reflection from the picture, yellow from a
  surface drawn on its own and cyan from the sky box (both with
  `r_rt_water_hits 1`), blue from the sky in the picture, red none. The view weapon, models drawn over the world, particles
  and translucent surfaces are left out of these views. `0` (the
  default) shows the picture; a value above `2` counts as `2`, one that
  is not a finite number as `0`, and a fraction is cut to the whole
  number below it. Cheat-protected: reset to `0` in multiplayer. Applies
  from the next frame.


## Gamepad

* **in_initjoy**: Chooses the preferred gamepad to initialize, starting
  with `1` (default); `0` disables gamepad usage. Can only be set from
  command line.

* **joy_labels**: Defines style of button labels in binding menus. Note
  that binding through console only uses the SDL nomenclature (`0`).
  Default is `-1`, which is autodetected.
  - `-1`: *Autodetect*, sets to `0` if gamepad type isn't detected
  - `0`: *SDL*, face buttons appear as cardinal points
  - `1`: *Xbox*, with One / Series X / S labels
  - `2`: *Playstation*, 4 & 5 format
  - `3`: *Switch*, traditional Nintendo button format

* **joy_confirm**: Style of *confirm* and *cancel* buttons in menus. As
  with the previous one, `-1` autodetects.
  - `-1`: *Autodetect*, sets to `1` if Nintendo gamepad, `0` otherwise
  - `0`: *Standard style* - SOUTH to confirm, EAST to cancel
  - `1`: *Japanese style* - EAST to confirm, SOUTH to cancel

* **joy_sensitivity**: Simple sensitivity adjustment for yaw and pitch.
  Changing this applies a preset that adjusts the advanced cvars listed
  below. Default `3`.

* **joy_advanced**: Show or hide the following advanced sensitivity
  cvars in the menu:
  - **joy_yawspeed**: How quickly the view turns left or right in
    degrees per second. Default `160`.
  - **joy_pitchspeed**: How quickly the view looks up or down in
    degrees per second. Default `120`.
  - **joy_extra_yawspeed**: Additional yaw speed that is applied when
    the stick is fully deflected. Default `220`.
  - **joy_extra_pitchspeed**: Additional pitch speed that is applied
    when the stick is fully deflected. Default `0`.
  - **joy_ramp_time**: Ramp-up time required for any extra yaw or pitch
    speed to be fully applied. Default `0.35` (350 milliseconds).

* **joy_layout**: Allows to select the stick layout of the gamepad.
  - `0`: *Default*, left stick moves, right aims
  - `1`: *Southpaw*, left stick aims, right moves
  - `2`: *Legacy*, left moves forward/backward and turns, right strafes
         and looks up/down
  - `3`: *Legacy Southpaw*, swapped sticks version of previous one
  - `4`: *Flick Stick*, left stick moves, right checks your surroundings
         in 360º, gyro required for looking up/down
  - `5`: *Flick Stick Southpaw*, swapped sticks version of last one

* **joy_forwardsensitivity**: Ramp-up proportion for moving forward and
  backward; affected axis depends on `joy_layout` value. Default `1.0`.

* **joy_sidesensitivity**: Ramp-up proportion for moving side to side;
  affected axis depends on `joy_layout` value. Default `1.0`.

* **joy_left_deadzone** / **joy_right_deadzone**: Inner, circular
  deadzone for each stick, where inputs below this radius will be
  ignored. Default is `0.16` (16% of possible stick travel).

* **joy_left_snapaxis** / **joy_right_snapaxis**: Ratio on the value of
  one axis (X or Y) to snap you to the other. It creates an axial
  deadzone with the shape of a "bowtie", which will help you to do
  perfectly horizontal or vertical movements the more you mark a
  direction with the stick. Increasing this too much will reduce speed
  for the diagonals, but will help you to mark 90º/180º turns with Flick
  Stick. Default `0.15`.

* **joy_left_expo** / **joy_right_expo**: Exponents on the response
  curve on each stick. Increasing this will make small movements to
  represent much smaller inputs, which helps precision with the sticks.
  `1.0` is linear. Default `2.0` (quadratic curve).

* **joy_outer_threshold**: Defines the outer boundary where stick input
  is considered to be at maximum. A small amount may be needed for some
  controllers. Additionally, this cvar defines the boundary where any
  extra yaw or pitch speed is applied. Default `0.02` (outer 2% of
  stick range).

* **joy_trigger**: Trigger pressure required to register it as a
  button press. Default `0.2` (20% of total trigger displacement).

* **joy_flick_threshold**: Used only with Flick Stick, specifies the
  distance from the center of the stick that will make the player flick
  or rotate. Default `0.65` (65%).

* **joy_flick_smoothed**: Flick Stick only, rotations below this angle
  (in degrees per frame) will be smoothed. Reducing this will increase
  responsiveness at the cost of jittery movement. Most gamepads will
  work nicely with a value between `4` and `16`. Default `16`.

* **gyro_mode**: Operation mode for the gyroscope sensor of the game
  controller. Options are `0` = always off, `1` = off with the
  `+gyroaction` bind to enable, `2` = on with `+gyroaction` to
  disable (default), `3` = always on.

* **gyro_space**: How motion input is converted into yaw and pitch
  rotations. `0` = local space, `1` = player space (default),
  `2` = world space.

* **gyro_local_roll**: How gyro roll contributes to turning when using
  local space. `0` = off, `1` = on (default), `2` = inverted.

* **gyro_yawsensitivity**: How quickly the view turns left or right. A
  value of 1.0 means one full rotation of the controller rotates the
  in-game view by 360 degrees. Default `2.5`.

* **gyro_pitchsensitivity**: How quickly the view looks up or down.
  Default `2.5`.

* **gyro_tightening**: Threshold of rotation, in degrees per second,
  below which gyro input is reduced to stabilize aiming. Default `3.5`.

* **gyro_smoothing**: Threshold of rotation, in degrees per second,
  below which gyro input is averaged to reduce jitter. Default `2.5`.

* **gyro_smoothing_window**: The smoothing window size, in seconds.
  Default `0.125`.

* **gyro_acceleration**: Toggles the use of gyro acceleration.
  `0` = off (default), `1` = on.

* **gyro_accel_multiplier**: Multiplier that increases gyro sensitivity
  when rotating the controller quickly. Applied linearly, ramping up
  from the lower to upper threshold. Default `2.0`.

* **gyro_accel_lower_thresh**: The lower input threshold, in degrees per
  second, from which acceleration starts ramping up towards the upper
  threshold. Default `0`.

* **gyro_accel_upper_thresh**: The upper input threshold, in degrees per
  second, at which full acceleration is applied. Default `75`.

* **gyro_calibration_(a/x/y/z)**: Offset values on each axis of the gyro
  which helps it reach true "zero movement", complete stillness. These
  values are wrong if you see your in-game view "drift" when leaving
  the controller alone. As these vary by device, it's better to use
  'calibrate' in the 'gamepad' -> 'gyro' menu to set them.

* **joy_haptic_magnitude**: Haptic magnitude value, By default this cvar
  is `0.0` or disabled. Valid values are positive, e.g. 0..2.0.

* **joy_haptic_filter**: List of sound file names produced haptic
  feedback separated by space. `*` could be used for replace part of
  file name as regular expression. `!` at the beginning of value could
  be used for skip file name equal to value.

* **joy_haptic_distance**: Haptic maximum effect distance value, By
  default this cvar is `100.0`. Any positive value is valid. E.g. effect
  of shoot to a barrel has 58 points when player stay near the barrel.

* **s_feedback_kind**: Select kind of controller feedback to use. By
  default this cvar is `0`. Possible values:
  - `0`: Rumble feedback,
  - `1`: Haptic feedback.

### Touch controls

The touch controls drive a virtual gamepad named "Touch controls" with
the fingers, so the gamepad cvars above (sticks, deadzones, the
bindings of the gamepad keys) apply to them as to a physical
controller. `in_initjoy 0` also turns them off. When a physical gamepad
is connected it is used instead, the touch controls are idle until it
is disconnected.

The layout is made inside the screen's safe area, in landscape. The left
half is the move stick and the right half the look stick, both floating:
the origin is where the finger lands. Buttons are in the lower right
(fire, jump, use item, crouch, next weapon) and at the top (menu, console).

The controls are drawn with outlines inside the safe area, nothing else
is added to the frame: in the game a ring for each stick (where a finger
is down, the ring is under it with a knob) and the buttons; in a menu,
the console or a chat message a d-pad, OK and Back (the d-pad buttons,
`BTN_SOUTH` and `BTN_EAST` of the virtual gamepad) and the console
button. A button that is held is filled. Nothing is drawn when the touch
controls are not attached (`in_touch` was `0` at start, so changing it
in the game changes the overlay and the touch pad only after a restart),
or a physical gamepad is in use.

* **in_touch**: Turns the touch controls on. Default `1` on iOS and `0`
  elsewhere. Read at start. On iOS touches are never mouse clicks while
  it is on. Where there is no touch screen (everywhere but iOS) it does
  nothing: a `1` is said once in the console and the controls stay off.
  The "touch controls" item of the controller page shows it, greyed with
  "no touch screen" there. That page is reached by "customize gamepad" of
  the controls menu, which is always offered, with or without a gamepad,
  so that touch controls turned off can be turned on again.

* **in_touch_scale**: Size of the buttons and the stick radius, from
  `0.5` to `2`; a value outside is held to the range. Default `1`. The
  "touch size" item of the controller page.

* **in_touch_swap**: `1` mirrors the layout, the move stick on the right
  and the buttons on the left. Default `0`. The "left-handed" item of
  the controller page.

On iOS, a first start (no `config.cfg` yet) binds the gamepad keys that
are not bound, once per start (a change of the game directory does not
bind again): `TRIG_RIGHT` to `+attack`, `BTN_SOUTH` to `+moveup`,
`BTN_EAST` to `+movedown`, `BTN_NORTH` to `weapnext`, `BTN_WEST` to
`invuse`, `SHOULDR_LEFT` to `weapprev`, `SHOULDR_RIGHT` to `weapnext`,
`BTN_BACK` to `inven` and `BTN_MISC1` (the console button) to
`toggleconsole`. A binding that is already there is kept, and `START`
is the menu key. Other platforms bind none of them. The on-screen
keyboard of iOS shows only while the console, a chat message or a
menu's text field takes text. It is plain text: no autocorrect, no
prediction and no capital for the first letter. If the system hides it
(the hide key of an iPad) while text is still wanted, it is shown again,
a few times in a row at most, so a keyboard that cannot be shown (a
hardware keyboard) is not asked for on every frame. If the system's
text input cannot be set up as plain text, the keyboard stays off.


## cvar operations

cvar operations are special commands that allow the programmatic
manipulation of cvar values. They can be used for scripting and the
like.

* **dec <cvar> [val]**: Decrements the given cvar by `1` or the optional
  value `val`.

* **inc <cvar> [val]**: Increments the given cvar by `1` or the optional
  value `val`.

* **reset <cvar>**: Reset the given cvar to its default value.

* **resetall**: Reset all known cvar to their default values.

* **toggle <cvar> [val0] [val1]**: Toggle the given cvar between `0` and
  `1`. If the optional arguments `val0` and `val1` are given, the cvar
  is toggled between them.

## Pin harness

These are set by `scripts/pins.sh`, never by a player. They look like
cvars on the command line but are not cvars: the engine reads them once
from its own command line, in exactly the form `+set <name> <value>`,
before any config file runs, and keeps the values where nothing else can
change them. A `set` of the same name in a config file, on the console
or from a server's stufftext (with or without flags such as `a`) only
creates an unrelated cvar that the engine never reads. Their values name
files, which is why. When one is given more than once the last occurrence is used, but any
refused occurrence switches the harness off for the whole run, even if a
later one is valid. If any of them holds a refused value (see below),
the harness is off and no screenshot is written at all. With none of
them given the engine behaves exactly as before.

* **pins_dir**: When given, every screenshot is written into this
  directory as an uncompressed 24-bit TGA named
  `<pins_scenario>-<frame>.tga` plus a line in `manifest.txt` (file
  name, renderer, size), and `scrnshot/` is not touched. Refused when
  empty, when it contains `..`, or when it does not fit the engine's
  path buffer; a frame path that would not fit is never opened. An
  absolute path is accepted. Default: not given.

* **pins_scenario**: The file-name prefix for frames written into
  `pins_dir`. Must match `[a-z0-9_][a-z0-9_-]*`. Default: `pin`.

* **pins_capture**: While a demo plays, capture every Nth rendered frame,
  after the frame is complete and before it is presented (while a
  cinematic started with `demomap <name>.cin` plays, every Nth cinematic
  frame instead, and a cinematic that cannot be opened ends the engine
  with a non-zero exit status); when playback ends, quit so a scenario
  run is unattended; and if the renderer named
  by `vid_renderer` fails to load, stop with an error instead of falling
  back to another renderer. While it is on, `config.cfg` and
  `autoexec.cfg` are neither executed nor written, and an error that
  would drop to the console (a demo that cannot be opened, for example)
  ends the engine with a non-zero exit status, as does a demo file that
  ends before its end marker. Needs `pins_dir`. Must be a non-negative
  integer of digits only (no sign, no spaces). `0` (the default)
  disables all of this.

* **pins_record**: When given, every call the client makes into the
  renderer during the first renderer session of the run is recorded to
  a trace file of this name inside `pins_dir` (`scripts/pins.sh record`
  passes `trace.q2r`): registrations with stable ids in place of the
  renderer's pointers, each `refdef_t` copied in full, every 2D draw,
  the point of each captured frame, a string table naming the models,
  skins and pics registered, a frame index, and the values of the
  renderer cvars a replay may restore (names starting with `gl1_`,
  `gl3_`, `gl_`, `r_`, `sw_` or `vid_` that are not write-protected,
  latched, userinfo or serverinfo; no other cvar, so never the player's
  name or the server address book). A renderer restart is not
  recorded again. The file is streamed to disk; a failed write or a
  recorder limit (1 GiB of records) ends the recording with one message
  and the game carries on. The trace embeds data derived from the game
  data: it stays in the pin store and is never committed or uploaded.
  Must be a plain file name matching `[A-Za-z0-9_][A-Za-z0-9_.-]*` and
  ending in `.q2r` (so it never names a pin frame or the manifest), at
  most 63 characters, without `..` or a directory part. Needs
  `pins_dir`. Default: not given, nothing is recorded and no file is
  opened.

* **pins_replay**: The path of a trace written by `pins_record`. Once the
  renderer named by `vid_renderer` is loaded, the engine feeds the
  trace to it frame by frame instead of running the game: no server,
  no demo, no sound and no input run, and no `+` command after the
  `+set` arguments is executed. Where the live run captured a frame,
  the replay takes a screenshot at the same point of the frame, so the
  frames land in `pins_dir` with the same names and numbers; after the
  last frame of the trace the engine quits. The trace is untrusted:
  the renderer is handed only values the live client could have handed
  it. Every name is shorter than 64 bytes and every registration names
  what the trace's string table holds, or the trace is refused; a 2D
  draw outside the frame is skipped; every other value is bounded. A
  replay is a pin run like a capture: it never reads or writes
  `config.cfg` or `autoexec.cfg`, and an error that would drop to the
  console stops the engine. The engine stops with
  an error (non-zero exit status) when the trace cannot be opened, was
  recorded at another size (`r_customwidth`, `r_customheight`) or for
  another renderer API version, was never finished, is damaged, or
  when the renderer fails to load (there is no fallback). Must be a
  path of `A-Z a-z 0-9 _ . / -` without `..`, whose file name starts
  with a letter, digit or `_` and ends in `.q2r`. Needs `pins_dir`;
  refused together with `pins_record`. Default: not given.
  At the start of the trace the replay restores the renderer cvars the
  live run had (the trace's cvar snapshot), so the replay needs none of
  the scenario's settings on its command line. It sets a cvar only when
  it exists in this engine, its name starts with `gl1_`, `gl3_`, `gl_`,
  `r_`, `sw_` or `vid_`, it is not write-protected, latched, userinfo
  or serverinfo, it does not choose the renderer, the window, the
  display mode or a library (`vid_renderer`, `vid_fullscreen`, `r_mode`
  and the like), and its value is printable text; everything else in
  the snapshot is skipped and counted in the line the replay prints at
  the end. The restored values are never saved.

* **pins_seed**: A fixed seed for the engine's random numbers, for a
  record or a replay: with it, `pins_record` and `pins_replay` reseed the
  engine's generator from this number whenever a map's models are
  registered, so particles and explosions draw the same random numbers
  in every run. Without it (every normal game) the generator is seeded
  from the operating system as before. It never reaches the game
  module's own generator, the dedicated server, or the server's
  connection challenge, which is drawn from the operating system
  directly. Must be 1 to 9 digits without a leading zero (`0` itself
  is accepted). Refused without `pins_record` or
  `pins_replay`. Default: not given.
