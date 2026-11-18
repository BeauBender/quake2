# Video options > Ray tracing: a live screen, saved across restarts (TASK194)

Card: TASK194 (#541). The video menu gets a "ray tracing..." entry that opens a screen of switches
and sliders for ref_metal's ray-traced effects. Every change is written to its cvar at once, so it
shows on the next frame with no `vid_restart`; every control is saved with the configuration. The
default look is unchanged: every switch defaults to off.

## The screen

Rows, in order, from `rt_controls` in `src/client/menu/vidrules.c`:

| item | cvar | kind | range, step |
| --- | --- | --- | --- |
| shadows | `r_rt_shadows` | switch | off, on |
| sun | `r_rt_sun` | switch | off, on |
| ambient occlusion | `r_rt_ao` | switch | off, on |
| water reflections | `r_rt_water` | switch | off, on |
| gun shadow | `r_rt_gunshadow` | switch | off, on |
| sun floor | `r_rt_sunfloor` | slider, needs sun | 0 to 1, 0.05 |
| sun scale | `r_rt_sunscale` | slider, needs sun | 0 to 4, 0.05 |
| AO range | `r_rt_ao_range` | slider, needs AO | 16 to 1024, 16 (the cvar takes 1 to 8192) |
| AO minimum | `r_rt_ao_min` | slider, needs AO | 0 to 1, 0.05 |
| AO scale | `r_rt_ao_scale` | slider, needs AO | 0.25 to 1, 0.25 (read as 1/3, 1/2, full, full) |
| water reflect | `r_rt_water_reflect` | slider, needs water | 0 to 1, 0.05 |
| water ripple | `r_rt_water_normal` | slider, needs water | 0 to 1, 0.02 |
| reset to defaults | (every row) | action | the renderer's defaults |

Greying (`VidRules_RTEntry`, `VidRules_RTItem`), every item always shown, the reason on the status bar:

- the entry: "needs the Metal renderer" unless the menu was built for `vid_renderer metal`;
- every item, the reset included: "needs the Metal renderer" off metal, "ray tracing is not available
  on this device" where `r_rt_available` is `0`;
- a slider whose switch is off: "turn on sun first", "turn on ambient occlusion first", "turn on water
  reflections first".

Nothing exposed the gate's decision to the client without a renderer API change, so ref_metal now sets
a new read-only cvar, `r_rt_available` (`1` where the gate has a path), at start and puts back a value
written to it; it has its row in `rm_rtgate.c` (a setting) and its `doc/040_cvarlist.md` entry. The
gate's own reason text stays in the start-up line; the menu shows the fixed text above. `ref.h` is
unchanged.

Live preview: the page sets `menuframework_s.keepview`, so `M_Draw` skips `Draw_FadeScreen` for it
alone, and its items sit at 30 % of the width, leaving the view to their right clear. No control needs
a restart; the page's status line says each change shows on the next frame.

## Persistence test

`tests/unit/test_vidrules.c`, `vidrules_rt_controls_persist`: on a fresh cvar table each row is
registered with the menu's `Cvar_Get(cvar, def, flags)` and moved as the menu moves it (a switch to 1,
a slider one step right), written with `Cvar_WriteVariables`, the table emptied, the file run through
the command buffer and each row registered again: every value matches. The mutant that registers
`r_rt_sunfloor` with flags `0` instead of `CVAR_ARCHIVE` fails it (at `f752959f` and again at
`bc95e68d`, where the whole suite passed just before, 384 tests, 0 failed):

```
FAIL vidrules_rt_controls_persist (tests/unit/test_vidrules.c:716): check failed: c->flags & CVAR_ARCHIVE
FAIL vidrules_rt_controls_persist (tests/unit/test_vidrules.c:762): v->string == want[i]: got "0.5", want "0.550000"
```

## On the ray-tracing Mac

Built from `eb769966` (release build of `client`, `ref_metal` and `game`), run with `-datadir <datadir>
-portable +set vid_renderer metal +set r_mode -1 +set r_customwidth 1280 +set r_customheight 720 +set
vid_fullscreen 0`, `+map base1`, then a script of `set` lines with about 100 frames between them, as
the screen writes them. The menu walk (`YQ2_INPUT_PROBE`, TASK23 slice 2a) is not on main yet, so the
screen was driven from the console and the walk was not run.

### Run A: each effect on and off, each slider moved

`r_rt_available` reads `"1"`; `set r_rt_available 0` reads `"1"` again ten frames later. Each switch
was set to 1, read, set to 0, read; with each effect on its sliders were set and read (`"r_rt_sunfloor"
is "0.55"`, `"r_rt_sunscale" is "0.4"`, `"r_rt_ao_range" is "144"`, `"r_rt_ao_min" is "0.45"`,
`"r_rt_ao_scale" is "1"`, `"r_rt_water_reflect" is "0.55"`, `"r_rt_water_normal" is "0.1"`). Last, all
five switches on, then `quit`. One `Refresh:` line in the log: the renderer started once, no restart.
The passes' allocation lines (stderr, so not in order with the console lines):

```
ref_metal: rt sun 2560x1440: 33947648 bytes allocated
ref_metal: rt ao traced at 1280x720 (1/2): 1048576 bytes allocated
ref_metal: rt ao 2560x1440: 33947648 bytes allocated
ref_metal: rt water 2560x1440: 30670848 bytes allocated
ref_metal: rt ao 2560x1440: 33947648 bytes allocated
ref_metal: rt sun 2560x1440: 33947648 bytes allocated
ref_metal: rt water 2560x1440: 30670848 bytes allocated
```

Sun, AO and water each allocate twice, once for their own phase and once for the last, all-on phase:
each pass started when its switch went on, stopped and freed its images when it went off, and started
again, all within the one renderer start. The shadow pass allocates only once a dynamic light is in
view (`rm_rtshadowscene.c`), and no light was in view in either of its phases; it allocated in run B.
The gun shadow has no pass of its own. The teardown at quit:

```
ref_metal: teardown: 0 live objects (created/destroyed: device 1/1, queue 2/2, view 1/1, texture 226/226, buffer 8/8, state 71/71, rt 166/166, metalfx 0/0), 0 unreleased, 0 unmatched
```

`rt 166/166`, where the defaults' run of TASK60 reads `rt 0/0`. No `reads 0` line in either run.

### Run B: the values read back

The config the run wrote holds every control (`set r_rt_ao "1"`, `set r_rt_ao_min "0.45"`, `set
r_rt_ao_range "144"`, `set r_rt_ao_scale "1"`, `set r_rt_gunshadow "1"`, `set r_rt_shadows "1"`, `set
r_rt_sun "1"`, `set r_rt_sunfloor "0.55"`, `set r_rt_sunscale "0.4"`, `set r_rt_water "1"`, `set
r_rt_water_normal "0.1"`, `set r_rt_water_reflect "0.55"`). The second start, with no `set`, read:

```
"r_rt_shadows" is "1"
"r_rt_sun" is "1"
"r_rt_ao" is "1"
"r_rt_water" is "1"
"r_rt_gunshadow" is "1"
"r_rt_sunfloor" is "0.55"
"r_rt_sunscale" is "0.4"
"r_rt_ao_range" is "144"
"r_rt_ao_min" is "0.45"
"r_rt_ao_scale" is "1"
"r_rt_water_reflect" is "0.55"
"r_rt_water_normal" is "0.1"
```

and the four passes allocated at the map's first frames (`rt ao`, `rt sun`, `rt water`, `rt shadows
2560x1440: 59506688 bytes allocated`), teardown `rt 124/124`.

## The menu walk with the screen (TASK23 slice 2a)

The walk (`menu_probe walk`, a build with `-DYQ2_INPUT_PROBE`, see TASK23.md) visits every screen. Measured on
the merge of main (`053a872d`) into this branch, `c95c017e`, in the macOS client built from clean with the
probe (`make DEBUG=1`, so the release objects stay without it), with `+menu_probe walk` on the command line and
the licensed data read-only (`-datadir`, `-portable`):

- `vid_renderer metal`: `menu_probe: screen 22 'm_banner_video' at depth 3, 13 items` is the ray-tracing screen
  (the video menu is screen 20, renderer options screen 21), then `walk ok: 24 screens, 971 key presses, 0
  failures`. The entry is live; the only greyed item of the walk is VideoToolbox ML's 2x.
- `vid_renderer soft`: `menu_probe: item 11 'ray tracing...' of 'm_banner_video' is greyed: needs the Metal
  renderer`, then `walk ok: 23 screens, 929 key presses, 0 failures`. 23 screens is the count without the new one.
- The mutant that drops the entry from the video menu's item list (`Menu_AddItem(&s_opengl_menu, &s_rt_action)`
  deleted) leaves the walk green at 23 screens, so the walk alone does not guard the entry. The check that does:
  `test_ray_tracing_entry_is_always_present` in `tests/menu/videomenu_test.c` (commit `2d92d757`), the entry live
  under Metal, greyed with "needs the Metal renderer" under soft. On the mutant `tests/check-video-menu.sh` prints
  `FAIL: no menu item named "ray tracing..."` and `check-video-menu: the host test videomenu_test failed`; on the
  branch, `videomenu_test: 3035 checks, 0 failed`.
