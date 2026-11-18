# Ray-tracing variations side by side (TASK173)

The owner, 2026-11-17, after the TASK56 sun frames: the effects are "very subtle"; extend the
harness "to capture a few variations of RT that show more differentiated output"
([design](../plans/metal-raytracing-design.md), section 2; INFORM11, #101). Every effect so far
layers conservatively on the stock lightmap (the sun below `r_rt_sunfloor`, AO on the lightmap
term only with a floor of 0.4, shadows on the dynamic lights only, reflections on the water
only), so the default frames barely differ from ray tracing off. The card's scope addition (the
owner's question, "are they competing with/washing out the RT implementations?") adds a
lightmaps-off preview and a sweep of the sun's floor.

**Status.** The presets, the command, its selftest (in the gate), the preview cvar and the
documentation are in the branch; the run on the bare-metal host is below.

## What is built

- [`scripts/pins/rt-variations.txt`](../../scripts/pins/rt-variations.txt): the presets and the
  views, one line each (format in the file's header).
- [`scripts/rt-variations.sh`](../../scripts/rt-variations.sh) and
  [`scripts/pins/rtvar.py`](../../scripts/pins/rtvar.py): `run`, `plan`, `check`, `selftest`.
  `run` records with `scripts/pins.sh record metal` (unchanged: FIX48 changes `pins.sh` and the
  scenario files in parallel, so this card edits neither) from git-ignored scratch scenarios
  `scripts/pins/scenarios/rtvar-*.cfg`, the precedent of TASK114's `mfxr-*.cfg`; the images use
  `scripts/pins/evidence.py`'s encoders and font. Usage and outputs:
  [doc/100_renderpins.md](../../doc/100_renderpins.md), *Ray-tracing variations for a look review*.
- [`tests/check-rt-variations.sh`](../../tests/check-rt-variations.sh): the selftest in the gate.
- `r_rt_flatlight` ([doc/040_cvarlist.md](../../doc/040_cvarlist.md)), the preview's stand-in
  for the lightmap. Since TASK56 `rm_rtsun_on` and `rm_rtao_on` return 0 under `r_fullbright`,
  so `r_fullbright 1` turns the traced terms off and cannot be the preview. With a level above 0
  the G-buffer raster writes that grey as the light of every lightmapped world draw (the static
  draw and the batched one: `rmb_gbuf_flatlight`, the `flat` field of the lightmap uniforms) and
  every traced term runs on it; only the raster reads it, so no forward or motion draw changes.
  The level is `rm_gb_flatlight` (host test in `tests/metal/rm_gbuf_test.c`). Cheat-protected
  (`cl_main.c`), not saved.

## Presets

All on top of the view's base scenario; every AO preset fixes `r_rt_ao_seed 1`, every record is
`PINS_SEED=1`.

| Preset | Cvars |
| --- | --- |
| `off` | none |
| `shadows` | `r_rt_shadows 1` |
| `shadows-x` | `r_rt_shadows 1`, `r_rt_flatlight 0.15` (the baked light dimmed flat, so the dynamic lights and their shadows dominate) |
| `sun` | `r_rt_sun 1`, `r_rt_sunfloor 0.5`, `r_rt_sunscale 0.35` |
| `sun-x` | `r_rt_sun 1`, `r_rt_sunfloor 1`, `r_rt_sunscale 1` |
| `ao` | `r_rt_ao 1`, `r_rt_ao_range 128`, `r_rt_ao_min 0.4` |
| `ao-x` | `r_rt_ao 1`, `r_rt_ao_range 512`, `r_rt_ao_min 0`, `r_rt_ao_scale 1` |
| `water` | `r_rt_water 1`, `r_rt_water_reflect 0.5`, `r_rt_water_normal 0.08` |
| `water-x` | `r_rt_water 1`, `r_rt_water_reflect 1`, `r_rt_water_normal 0.3`, `r_rt_water_hits 1` |
| `all` | the four at their defaults |
| `all-x` | the four exaggerated, without the flat light |
| `shadows-show1`, `-show2` | `r_rt_shadows 1`, `r_rt_shadows_show 1` or `2` |
| `sun-show1`, `-show2` | `r_rt_sun 1`, `r_rt_sun_show 1` or `2` |
| `ao-show1`, `-show2` | `r_rt_ao 1`, `r_rt_ao_show 1` or `2` |
| `water-show1`, `-show2` | `r_rt_water 1`, `r_rt_water_show 1` or `2` |
| `sunfloor-025` to `-100` | `r_rt_sun 1`, `r_rt_sunscale 0.35`, `r_rt_sunfloor` 0.25, 0.5, 0.75, 1 |
| `flat` | `r_rt_gbuffer 1`, `r_rt_flatlight 0.4`: the flat light alone, no traced term |
| `preview` | `r_rt_flatlight 0.4` and the four at their defaults. Caption: "preview: no traced bounce light, looks harsh; the real lightmap-free mode is TASK174's RT-primary" |

The preview's level is 0.4, under the sun's default floor of 0.5, so the sun adds its light
wherever it reaches.

Views: `outdoor` and `dlights` (base `dlights`, `demo1.dm2` on `base2`, every 30th frame; the
capture where `sun-x`, respectively `shadows`, differs most from `off`), `indoor` (base `dlights`,
`lowsky.dm2` on `fact2`, sky 0.72 %, so the sun is skipped; the capture where `ao-x` differs
most), `water` (base `skywater`, `water.dm2` on `base1`; where `water-x` differs most).

## Runs on the bare-metal host

Host: the bare-metal host (Apple M6, ray tracing). Engine commit measured: `4a6227a7` (pin ids
`4a6227a7e22e-*`; the commits after it change no source). Every build and run held the shared
timing lock (`tools/heavy.sh`); every record is `PINS_SEED=1`, 640 x 480, under
`MTL_DEBUG_LAYER=1`, and its log holds no validation message (the tool refuses one). A record
takes about 10 s; the whole run, 75 records and the images of four views, about 15 minutes.

*Demos.* `scripts/pins.sh make-demo lowsky` made `lowsky.dm2` at 224 messages (100 to 600).
`make-demo water` missed its range four times (143 to 145 messages of 97 to 124: the waits held
each place longer than the script means), so the run used the last try's demo, the same camera
path over `base1`'s pool held longer, as `water.dm2` beside the binary (local; its captures are
not `skywater`'s documented ones, which only the look review uses here).

*The default changes no frame.* The TASK56 head (`e1bb3b6f`'s source, built from a scratch
export) and this branch, the same views and presets: `outdoor` `off`, `all`, `sun-x` and
`water-x` **23 of 23 frames byte-identical** each; `water` the same four **5 of 5** each. So
`r_rt_flatlight 0` draws every frame as before, on the forward path and on the G-buffer path
with every effect on. The tool itself changes no frame: `off` of `outdoor` against a record of
the stock `dlights` scenario, 23 of 23 byte-identical. Sanity: `off` against `all`, and `all`
against `preview`, 0 of 23.

*The views and their difference from `off`* (framediff's mean difference at the view's
capture; `index-outdoor-dlights-indoor-water.txt` holds every preset):

| Preset | `outdoor` (capture 21) | `indoor` (3) | `water` (0) |
| --- | --- | --- | --- |
| `shadows` | 0.0012 | 0.0003 | 0.0002 |
| `shadows-x` | 0.0567 | 0.0261 | 0.0172 |
| `sun` / `sunfloor-050` | 0.0010 | 0 | 0.0002 |
| `sunfloor-025`, `-075`, `-100` | 0.0004, 0.0021, 0.0040 | 0 | 0.0002 |
| `sun-x` | 0.0079 | 0 | 0.0002 |
| `ao-x` | 0.0246 | 0.0304 | 0.0111 |
| `water-x` | 0.0063 | 0.0003 | 0.0180 |
| `all` | 0.0076 | 0.0110 | 0.0160 |
| `all-x` | 0.0259 | 0.0348 | 0.0367 |
| `flat` | 0.0513 | 0.0403 | 0.0449 |
| `preview` | 0.0330 | 0.0309 | 0.0432 |

`dlights` is `outdoor`'s record set: its `auto:shadows` capture is 21 too, so its sheet equals
`outdoor`'s. On `indoor` (`fact2`, sky 0.72 %) the sun is skipped, so every sun preset is `off`.
The sweep shows the floor's hold: at the default 0.5 the sun moves 0.1 % of the picture's level
on `outdoor`, at 1.0 four times as much.

*Outputs*, in a local artifacts folder, never committed or uploaded, and not viewed by the
implementer: per view `sheet.png`, `clip.png` and `full/`, as doc/100 describes.

## Checks

Red first: `tests/metal/rm_gbuf_test.c` named `rm_gb_flatlight` before it existed (does not
compile), and the selftest failed on `r_rt_flatlight` missing from `doc/040_cvarlist.md`. Then,
each under the lock: `make test` `yq2test: 327 tests, 0 failed`; `scripts/pins.sh selftest`
`pins selftest: ok`; `tests/check-rt-variations.sh` `rt-variations selftest: 0 failed`;
`tests/check-ref-metal.sh` ok (13001 checks, 0 failed; G-buffer 177 checks, 0 failed);
`tests/check-cvar-docs.sh`, `tests/check-doc-links.sh`, `tests/check-no-local-identity.sh` and
`tests/check-runbook-index.sh` ok. The full gate (`scripts/test.sh`) is the parent's.
