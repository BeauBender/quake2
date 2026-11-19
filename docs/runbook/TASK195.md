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

Each applied to the tree, its test run, the file restored (commit `4a8c1fd3`, then `e0813699`):

| mutant | file | caught by |
|---|---|---|
| the name check skipped (`VidRules_PresetNameError` returns NULL) | `vidrules.c` | `rtpreset_name_check`: `the name "" is accepted`, `the name ".." is accepted`, ... |
| the A/B overlay writes each B value to its cvar too | `rtpreset.c` | `rtpreset_ab_writes_no_tuning_cvar`: `got "0", want "1"` (r_rt_ao), `got "128", want "512"` |
| a tuning item hidden (water hits not added to the screen) | `videomenu.c` | `test_tuning_screen`: `the tuning screen has 14 items`, `no ray tracing item named "water hits"` |

A first run of the second mutant reused the first's stale test object (make's one-second clock: the
restore fell in the same second as the build); it was run again with each change two seconds clear.

## Proof on this Mac

Commit `4a8c1fd3` (runs 1 and 2) and `e0813699` (run 3), `release/quake2 -datadir <datadir> -portable +set
vid_renderer metal`, 1280x720 windowed, `+map base1`, commands from a config in `<writedir>`. Neither tree had
the game module built (`make client ref_metal` only), so `+map base1` stopped at "failed to load game DLL" and
the runs drew the console, not the world: the commands, the cvars and the renderer's A/B line are shown;
a traced pass's counters are not.

Run 1 (`set r_metalfx 3`, `rt_ab`, `r_metalfx`, `rt_ab`, then two tuning values set and `rt_preset save t195`):

```
rt_ab: B: stock
ref_metal: A/B: B: stock (17 settings overlaid, no cvar written)
"r_metalfx" is "3"
rt_ab: A: your settings
ref_metal: A/B: A: your settings
"r_metalfx" is "3"
rt_preset: saved t195 (17 settings)
```

Run 2, a new start (the values set back before run 1 quit): `rt_preset list`, `load`, the values, `ab`,
`rt_ab` twice, and a refused name:

```
"r_rt_ao_range" is "128"
  t195
rt_preset: 1 preset
  r_rt_sunscale: "0.35" -> "1.25"
  r_rt_ao_range: "128" -> "512"
  r_metalfx: "0" -> "3"
rt_preset: loaded t195, 3 changed
"r_rt_ao_range" is "512"
rt_preset: B is preset t195; rt_ab flips; now A: your settings
rt_ab: B: preset t195
ref_metal: A/B: B: preset t195 (17 settings overlaid, no cvar written)
"r_rt_ao_range" is "512"
rt_ab: A: your settings
ref_metal: A/B: A: your settings
rt_preset load: refused: the name holds ".."
```

Run 3 (`r_rt_ao 1`, `r_rt_stats 2`, `r_rt_ao_scale` 1 then 0.5, `rt_ab` twice) printed the A/B lines again,
and no AO counter line, as the world was not drawn.

## Pin comparison at defaults

Base: `git archive` of main `7e29deee`; branch: `task195` after merging that main. Both built with
`make client game ref_metal`. Every committed scenario but `cinematic` recorded on metal with `PINS_SEED=1`, no
preset, A side: 25 scenarios, 23 frames each, the hash of the frames' hashes equal base against branch in all 25
(for example `world` da510450be3a8527, `rt-ao` ef2d8ecdc5f64c77, `rt-water` 9fdbdc338273b39e,
`world-metalfx3` 0e8074ad234d1f49, `hud2d` 82c4e1a9bfdf550d). Byte-identical.

## Engine run with the world drawn

`+map base1`, `r_rt_ao 1`, `r_rt_stats 2`, 1280x720 windowed. The AO pass line (`ref_metal: rt ao: frame N`):

| step | rays per frame | AO pass |
|---|---|---|
| `r_rt_ao_scale 1` | 7313574 | 3.5 to 5.8 ms |
| `set r_rt_ao_scale 0.5` live | 1828488 (traced at 1280x720, 1/2) | 3.2 ms |
| `rt_ab` (`rt_ab: B: stock`, `ref_metal: A/B: B: stock (17 settings overlaid, no cvar written)`) | no AO line: stock has AO off | |
| `rt_ab` again (`A: your settings`) | 1828488, the live value kept | 3.2 ms |

`r_rt_ao_scale` still read "0.5" at the end: the overlay wrote no cvar.

## Checks

Gate at the merge of main into `task195`: `test.sh: the gate passed in 799 s`. `check-video-menu`,
`check-ref-metal`, `check-cvar-docs`, `check-doc-links`, `check-runbook-index` and `check-no-local-identity` ok.
The suite's cvar table is put back by each new test (the earlier 2 failures are gone).

`check-cvar-docs`: the word "save" in `rt_preset save` documents the upstream command of that name too, so
its line left `tests/baselines/undocumented-commands.txt`; restoring it makes the check refuse ("lists commands
that are now documented or removed; delete these lines: save"), which is its documented rule.

## From a preset to defaults or a presentation (slice 2)

`scripts/rt/preset-to-table.sh <preset.cfg> [--as defaults|enhanced|retro|raytraced]` turns the owner's saved
preset (`rt_preset save <name>`) into the change a pull request makes. It reads only `set <name> "<value>"`
lines (blank lines and `//` comments pass), refuses any other line with its number and any name outside the 17
tuning cvars (exit 1), and never edits a file. The 17 and their defaults are read from the `rt_controls` and
`rt_tune` tables of `src/client/menu/vidrules.c`.

- `--as defaults` (the default): `<cvar>: <old> -> <new>` for each value that differs from today's default,
  each followed by the `vidrules.c` line holding the default. Claude changes those string literals in a PR;
  changing a shipped default needs the owner's yes.
- `--as enhanced` (or `retro`, `raytraced`): the rows of that mode's overlay as the macro body
  `RM_PRES_<MODE>_ROWS` (stdout; notes on stderr), `{ "<cvar>", <value>f },`, the format of `pres_row_t` in
  `src/client/refresh/metal/rm_rtgate.c`. Enhanced's table is `RM_PRES_ENHANCED_ROWS`, which the printed block
  replaces; `rm_pres_value` now returns each row's own value (the stock rows are the four switches at 1).
  Only numbers are rows, so `r_rt_sun_angles` (a pair, read as a string) is left out with a note. The other
  modes have no table in `rm_rtgate.c` yet: add one beside `pres_enhanced`.

Test: `tests/check-ref-metal.sh` runs the script on `tests/fixtures/rt-preset/good.cfg`, compiles the printed
enhanced rows into `rm_rtgate.c` (`-include`, `RM_PRES_ENHANCED_ROWS`) with `tests/metal/rm_pres_table_test.c`,
which checks `rm_pres_value` returns each of the preset's values under enhanced and the user's under stock and
retro; the defaults output must list `r_rt_ao_range: 128 -> 256`; `badline.cfg` (a `bind` line) and
`badname.cfg` (`r_mode`) must be refused naming line 2.

## Left for slice 2

- The sun angle rows are spin controls of 5-degree steps, not sliders: a slider binds one cvar's whole value,
  and `r_rt_sun_angles` is a pair.
- A map listed in `scripts/rt/sun-angles.txt` (base1 is) takes its sun from that table, so the angle rows
  change nothing there; the rows could say so.
- `r_metalfx_scale` is not on the tuning screen (it is on the video menu, where apply writes it).
