# `r_rt_water_hits 1` becomes the default (TASK168, RT6)

TASK168 follows [TASK162](TASK162.md), whose *Proposal on the default* said the cost supports the flip
and left the look to the owner. Design anchor: [section 2](../plans/metal-raytracing-design.md#2-the-three-effects-as-compute-passes)
(RT6).

## The decision

The owner approved the look on 2026-11-22, under decision 0014: the look decides, and the cost is
information only. So `r_rt_water_hits` is created with `1`: a reflection that meets a surface the
picture does not show shades it in world space. `0` stays as the setting for TASK58's screen-space pass.

## Where the default lives

- `rm_rtwaterscene.c`: the `Cvar_Get` in the renderer. This is the one that decides.
- `vidrules.c`: the `rt_tune` row of the video menu, its default column, which the menu and the
  presets' "defaults" side read.
- Documents: `doc/040_cvarlist.md` (the entry), the RT6 section of
  [the design](../plans/metal-raytracing-design.md) and the comment of `rm_rtw_hits` in `rm_rtwater.h`.
- Not a table of the default: `rm_rtgate.c` lists the cvar only as one read while `r_rt_water` runs,
  and `VIDITEM_RTW_HITS` (`vidrules.h`) only the rule that greys it; neither holds a value.
  `rtpreset.c` records a preset's values from the cvars; `tests/fixtures/rt-preset/good.cfg` sets `1`
  itself.

## Who sees the new default

- *Archived values persist.* `r_rt_water_hits` is `CVAR_ARCHIVE`: `Cvar_WriteVariables`
  (`src/common/cvar.c`) writes every archived cvar whatever its value, and `Cvar_Get` on an existing
  cvar keeps the stored one. A `config.cfg` that already holds `r_rt_water_hits "0"` keeps 0. That
  covers anyone who ran the Metal RT water or opened the Ray tracing menu, since the menu's
  `Cvar_Get(c->cvar, c->def, c->flags)` (`videomenu.c`) creates the cvar too. Only a fresh
  configuration, or one without the cvar, gets `1`.
- *Presets* (`rtpreset.c`). A preset without the line leaves the current value alone; a preset with an
  explicit `r_rt_water_hits "0"` loads 0. The A/B "stock" fill and reset-to-defaults use the
  `rt_tune` default, now `1`.
- *The one-time refusal line.* Where the world-space kernels cannot be built, the line
  "r_rt_water_hits 1 refused, its hits shaded as 0's" now prints once for every user with
  `r_rt_water 1`, not only for those who chose `1`.

## What each presentation mode reads

`rm_pres_value` has no row for `r_rt_water_hits` in any mode, so every mode reads the user's value,
which is now `1` unless set.

| Mode | `r_rt_water` | Hits | Changes? |
|---|---|---|---|
| Stock (0) | the user's, `0` by default | not read | no, with the effects off nothing runs |
| Enhanced (1) | reads `1` | the user's, now `1` | yes |
| Retro (2) | reads `0` | not read | no |
| RT-primary (3) | reads `1` | the user's, now `1` | yes |

So the flip changes Enhanced and RT-primary, besides the plain RT settings with `r_rt_water 1`. No
scenario of the harness shows RT-primary with water in view (`rtprimary-*` play `demo1.dm2`, which has
none), so no pin of it changes.

## Pins

Pins that rely on the default: `rt-water` and `rt-water-entities`, which leave the cvar unset. Those
that set it themselves are unaffected: none of `scripts/pins/scenarios/` does; the variation preset
`water-x` (`scripts/pins/rt-variations.txt`) sets `r_rt_water_hits 1` and the presets that turn only
`r_rt_water` on (`water`, `all`, `preview`, the `water-show*` ones) now follow the default; their
records are made by `scripts/rt-variations.sh` outside the tree.

Nothing in `tests/pins/` depends on it (`tests/pins/` holds only the unit-test pins, and `make test`
passes unchanged: 562 tests, 0 failed). The harness's references live in the local pin store and were
recorded again there, not committed: `rt-water` and `rt-water-entities` on `water.dm2`, 4 frames each.

## Proofs (`PINS_BASEDIR` the owner's data, screen state as found, one hold each)

Commit `a0957721` (the source; later commits are documents). The base is the newest stored record of
the same scenario, made on an earlier main, since the hold has no second build.

- *Stock pins, `ref_metal`, `r_presentation 0`, effects off.* Compared with the stored base at the
  default thresholds:

| Scenario | Frames | Result |
|---|---|---|
| `world` | 23 | 0 failed, worst 0 (byte-identical) |
| `world-fullbright` | 23 | 0 failed, worst 0 |
| `models` | 23 | 0 failed, worst 6e-06 |
| `hud2d` | 23 | 0 failed, worst 0 |
| `retro` | 23 | 0 failed, worst 0 |
| `hdr` | 23 | 0 failed, worst 0 |
| `skywater` (`water.dm2`) | 4 | 0 failed, worst 0.0019 (the water warp follows the client clock, FIX102 and TASK70) |
| `dlights` | 23 | 6 failed, worst 0.011 bad fraction 0.25: not deterministic unseeded (the scenarios README), and nothing in it runs water |

  `skywater` from `demo1.dm2` does not apply: the stored base is from `water.dm2`.
- *`ref_gl1`.* Not run: `ref_gl1` in this tree hung for the 600 s bound on `world` (the engine log ends
  at a normal shutdown, the frame was captured, the bound killed it), and the card changes no file of
  `ref_gl1`. The change is in `ref_metal` and the menu table only.
- *The new `rt-water` pins* (`water.dm2`, 4 frames): the new default is byte-identical to a scratch
  scenario that sets `r_rt_water_hits 1` itself (4 of 4 frames equal). Against the stored records made
  at `0`, `rt-water` and `rt-water-entities` pass the default thresholds (worst 0.0023, bad 0.0031),
  as TASK158's 0.08 to 0.31 % of pixels changed predicts, and no frame is byte-equal; a record at an
  explicit `0` is no byte-equal to the old record either, because the warp follows the client clock,
  so the byte test is the explicit `1` one.
- *5K with `r_metalfx 2`* was not used (FIX96, FIX102).

## Checks on the final commit

`make test`: 562 tests, 0 failed. `check-ref-metal`: ok, 13162 checks. `check-cvar-docs`,
`check-runbook-index` and `check-doc-links`: ok.
