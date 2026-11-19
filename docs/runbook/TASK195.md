# Ray-tracing tuning harness: the tuning screen, A/B and presets (TASK195, slice 1)

Card: TASK195 (#552). The owner judges the ray-traced look over the running game: every setting that
changes it is on a screen that keeps the view undimmed, a key flips between the owner's settings and a
second set, and a set can be saved as a preset Claude can turn into a default or a presentation.

## What it is

- **Tuning screen** (Video, ray tracing..., tuning...), `keepview` as the ray-tracing screen. Its rows are
  the table `rt_tune` in `src/client/menu/vidrules.c` (`VidRules_TuneNum`, `VidRules_TuneControl`), in order:
  sun elevation and sun azimuth (the two words of `r_rt_sun_angles`, 0 to 90 and 0 to 355 by 5), AO rays
  (`r_rt_ao_rays`), water hits (`r_rt_water_hits`), flat light (`r_rt_flatlight`, a slider), scaler
  (`r_metalfx`: none, spatial, temporal, denoised; the denoiser is its mode 3), debug view (none, shadows,
  sun, AO, water, G-buffer: one `*_show` cvar at 1 at a time) and presentation (`r_presentation`). Then the
  actions: "A/B: <side>", "B: stock", the "preset name" field, "save preset", "load preset", "preset as B"
  and "apply (restart)". Every row writes its cvar at once and is never hidden: greyed off Metal ("needs
  the Metal renderer"), without ray tracing, while its effect is off ("turn on sun first", ...); the save and
  load actions are greyed with the name check's reason. The presentation is the one row that waits for
  `vid_restart`: after it changes, the status bar says so and "apply (restart)" turns live and queues
  `vid_restart`. Nothing restarts by itself.
- **A/B** (`rt_ab`, bindable; `rt_ab stock` makes B the defaults again). It writes no tuning cvar. The client
  writes only `r_rt_ab`, not archived, empty for A or `<label>|name=value;...` for B. ref_metal reads every
  tuning cvar through `RM_Effect` / `RM_EffectString` (`src/client/refresh/metal/rm_main.c`), which take the
  B value from `r_rt_ab` (`rm_ab_value` in `rm_rtgate.c`) before the presentation overlay of TASK183. So the
  existing overlay is extended and the renderer API is not changed. A flip prints
  `ref_metal: A/B: B: stock (17 settings overlaid, no cvar written)` or `ref_metal: A/B: A: your settings`.
  The console prints `rt_ab: <side>` and the screen's action shows the side.
- **Presets** (`rt_preset save|load|ab <name>`, `rt_preset list`, in `src/client/menu/rtpreset.c`):
  `<writedir>/presets/rt-<name>.cfg`, a header comment `// rt preset <name>: Yamagi Quake II <version>,
  <date>`, then `set <cvar> "<value>"` for each of the 17 tuning cvars (`VidRules_PresetCvar`: the 12 rows
  of the ray-tracing screen, then `r_rt_sun_angles`, `r_rt_ao_rays`, `r_rt_water_hits`, `r_rt_flatlight`,
  `r_metalfx`; no debug view, no presentation). The name is checked by `VidRules_PresetNameError` before
  any path is built (CLAUDE.md: `Q_fopen` does not check): refused when empty, longer than 32, holding `..`,
  `/` or `\`, starting with `.`, or holding anything outside `[A-Za-z0-9_-]`. `FS_CreatePath` makes the
  directory. `load` sets only those cvars (any other line is passed over, and so is a value holding a quote,
  `;`, `|` or `=`), and prints each change. `ab` reads the preset into the B side and writes no cvar.

## Tests

| test | where | proves |
|---|---|---|
| `rtpreset_name_check` | `tests/unit/test_rtpreset.c` | each refused form and two valid names; save, load and ab refuse and write nothing |
| `rtpreset_save_load_round_trip` | same | on a fresh cvar table every one of the 17 values comes back |
| `rtpreset_load_writes_only_tuning_cvars` | same | `r_mode`, `r_rt_ao_seed` and an `alias` line in a preset are passed over; a value holding `;` is too |
| `rtpreset_ab_writes_no_tuning_cvar` | same | B stock and B preset change no tuning cvar; `r_rt_ab` holds the B values; back to A it is empty |
| `rtpreset_cvars_are_the_look` | same | each preset cvar once, no `*_show`, no `r_presentation` |
| `test_ab_overlay` | `tests/metal/rm_rtgate_test.c` | the overlay gives the B value on B and none on A or for a near name; the report lines |
| `test_tuning_screen` | `tests/menu/videomenu_test.c` | the 15 items, the greying reasons, the pair rows, one debug view at a time, the A/B and preset commands (a bad name queues none), apply (restart) only after the presentation changed, every row greyed off Metal |

The ray-tracing screen's own test now counts 14 items (the "tuning..." entry).

### Mutants

MUTANTS_TABLE

## Proof on this Mac

ENGINE_EVIDENCE

## Pin comparison at defaults

PIN_TABLE

## Checks

CHECKS_TABLE

## Left for slice 2

- `scripts/rt/preset-to-table.sh <preset.cfg>`: print the `rm_rtgate.c` `pres_*` rows (or the default
  changes) a preset implies, with a host test that the printed rows parse.
- The sun angle rows are spin controls of 5-degree steps, not sliders: a slider binds one cvar's whole value,
  and `r_rt_sun_angles` is a pair.
- A map listed in `scripts/rt/sun-angles.txt` (base1 is) takes its sun from that table, so the angle rows
  change nothing there; the rows could say so.
- `r_metalfx_scale` is not on the tuning screen (it is on the video menu, where apply writes it).
