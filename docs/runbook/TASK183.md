# Presentation: Stock, Enhanced, Ray traced, Retro (TASK183, PM1)

Card: TASK183 (#530). `r_presentation` picks one look for the Metal renderer. Stock (0) is the default and
changes nothing. Enhanced (1) makes the four ray-traced effect scenes read `r_rt_shadows`, `r_rt_sun`,
`r_rt_ao` and `r_rt_water` as 1 without writing any cvar, so the user's own values come back when the
mode is switched back. Ray traced (3) and Retro (2) are listed but "awaiting review" and cannot be applied.
A device without ray tracing runs stock.

## What it is

- `r_presentation` (`CVAR_ARCHIVE`, default 0) is read at `RI_Init`, so a change needs `vid_restart`.
- The overlay is code in `src/client/refresh/metal/rm_rtgate.c` (`rm_pres_*`), applied through
  `RM_Effect(cvar)` in the four effect scenes. It sits on the report line after TASK60's line.
- The video menu has a "presentation" spin control: Stock is always available, Enhanced and Ray traced
  need ray tracing, Ray traced and Retro read "awaiting review", off Metal everything is greyed. Apply
  writes only an enabled mode and queues `vid_restart`.

## Runs

Branch run commit `65d1ba67` (the TASK183 code `7fd76947`, main `4ba8a7ed` merged, plus one allow-list line
in `tests/baselines/nonliteral-calls.txt`). Base: `git archive` of main `4ba8a7ed`, built in scratch.
Host: Apple GPU family apple9, `supportsRaytracing`. Game data at `<datadir>`, `-portable`, 1280x720
windowed, map `base1`, the four `r_rt_*` cvars set to 0 on the command line.

### Report line and cvar read-backs

Run A, started at `r_presentation 0`, then `set r_presentation 1` and `vid_restart`, then back to 0 and
`vid_restart`:

| point | report line (after the TASK60 line) | `r_rt_ao`, `r_rt_shadows`, `r_rt_sun`, `r_rt_water` | `r_presentation` |
| --- | --- | --- | --- |
| start | `ref_metal: presentation: stock (r_presentation 0)` | 0, 0, 0, 0 | 0 |
| after switch to 1 | `ref_metal: presentation: enhanced (r_presentation 1): r_rt_shadows, r_rt_sun, r_rt_ao and r_rt_water read 1, the user's values kept` | 0, 0, 0, 0 | 1 |
| back to 0 | `ref_metal: presentation: stock (r_presentation 0)` | 0, 0, 0, 0 | 0 |

Run B, started at `+set r_presentation 1`: the start line is the enhanced line above, the restart to 1
prints it again, the restart to 0 prints the stock line, and the four `r_rt_*` read-backs are `"0"` at
every point. The TASK60 line (`ray tracing: compute path (apple9, supportsRaytracing; ...)`) precedes the
presentation line each time. Every engine run exited with status 0.

## Mutants

Each mutant edits one line, runs the named test, and the file is restored after (the tree was clean
afterwards). Every one turns its test red.

| mutant | file | test | result |
| --- | --- | --- | --- |
| overlay applies to every name | `rm_rtgate.c` | `rm_rtgate_test` | 105 of 720 failed |
| stock mode also overlays | `rm_rtgate.c` | `rm_rtgate_test` | 65 of 720 failed |
| decide ignores the no-ray-tracing path | `rm_rtgate.c` | `rm_rtgate_test` | 1 of 720 failed |
| Retro becomes available | `vidrules.c` | `make test` (vidrules) | `vidrules_presentation_modes` |
| `r_presentation` not archived | `vidrules.c` | `make test` (vidrules) | `vidrules_presentation_modes`, `vidrules_presentation_persists` |
| status line never names the override | `vidrules.c` | `make test` (vidrules) | `vidrules_rt_status_names_the_presentation` |
| Apply allowed on a greyed mode | `videomenu.c` | `tests/check-video-menu.sh` | `videomenu_test` failed |
| greyed item loses its reason | `videomenu.c` | `tests/check-video-menu.sh` | `videomenu_test` failed |

A first pass of the three `vidrules.c` mutants reused a stale object after a restore (all three showed the
Retro failure); the script now touches the file after each edit and restore, and the table is from the
second pass.

## Pin comparison at `r_presentation 0`

Every committed scenario recorded on Metal with the base build and the branch build, same seed, 23 frames
each, compared by the hash of the frames' hashes. All 24 recorded are identical, base against branch:
dlights, dlights-lmcubic, hud2d, hud2d-scale8, hud2d-sharp, models, particles, particles-cap, rt-ao,
rt-ao-entities, rt-shadows, rt-shadows-entities, rt-sun, rt-water, rt-water-entities, skywater,
world, world-fullbright, world-lmcubic, world-metalfx-interp, world-metalfx1, world-metalfx2,
world-metalfx3, world-scale8. The cinematic scenario was not recorded on either side: it needs a `.cin`
file named by `PINS_DEMO`, and none was supplied. No pin was re-recorded.

## Checks

`make test` 387 tests, 0 failed; `check-video-menu` ok (3134 host checks); `check-ref-metal` ok;
`check-cvar-docs` ok; `check-doc-links` ok; `check-no-local-identity` ok.
