# Pin scenarios (TASK27)

One file per rendering feature, so a failing pin names the feature. A
scenario is a list of `set <cvar> <value>` lines. `scripts/pins.sh record`
passes each line to the engine as a `+set` argument and then starts the demo
with `+demomap` (`PINS_DEMO`, default `demo1.dm2`), so all scenarios follow
the same camera path and differ only in what is drawn. The exception is
`cinematic`: `PINS_DEMO` names a cinematic under `video/` instead (for
example `PINS_DEMO=idlog.cin PINS_EVERY=7`), which `+demomap` plays the same
way, and the engine then captures every Nth cinematic frame rather than
every Nth rendered frame. `record` holds you to the pairing: `cinematic`
needs a `.cin` and any other scenario refuses one. Nothing here is
derived from game data, so the files are committed (INFORM9).

The format has one reader, `scenario_sets` in `scripts/pins.sh` (FIX122):
it holds every refusal (any line that is not `set <cvar> <value>`, the
cvars the driver sets itself, the ones that choose a directory or the game
module, `..` in a value, a value starting with `-` that is not a number).
`scripts/pins.sh scenario-args <file>` prints what it accepts, and the bench
harness's quake2 adapter and the MetalFX report generator read every
scenario through it. The cases each of them is held to are in
`scripts/pins/scenario-cases.txt` (`tests/check-scenario-parsers.sh`).

| Scenario | Drawn |
|---|---|
| `world` | world surfaces, lightmaps, sky, water |
| `world-fullbright` | `world` with `r_fullbright 1`: world surfaces without lightmaps, sky, water (TASK87, `ref_metal` N4) |
| `dlights` | `world` with `cl_lights 1`: the dynamic lights on the world's lightmaps; not deterministic unseeded (the engine PRNG gives the lights their radii), so recorded with `PINS_SEED`, also with `PINS_DEMO=demo2.dm2 PINS_EVERY=7` for the animated lightstyles (TASK91, `ref_metal` N8) |
| `world-lmcubic` | `world` with `metal_lightmap_filter 1`: the cubic lightmap filter of `ref_metal`; other renderers ignore the cvar (TASK120) |
| `rt-shadows` | `dlights` with `r_rt_shadows 1`: the dynamic lights added in `ref_metal`'s resolve, each with a ray-traced shadow; other renderers ignore the cvar; temporal accumulation is off (`r_rt_temporal 0`, the design's pins run without it, TASK59) and the pass draws no noise, so the frames are stateless; recorded with `PINS_SEED`, as `dlights`, and judged against `ref_metal` itself once the owner accepts the look (TASK55) |
| `rt-shadows-entities` | `rt-shadows` with `cl_entities 1`: the brush entities, which gl1 lights in their model's space with the light's world origin, and the alias models, which the pass never lights, in the double-count check; recorded with `PINS_SEED`, as `dlights` (TASK55) |
| `rt-ao` | `world` with `r_rt_ao 1`: the world's lightmap term darkened by `ref_metal`'s ray-traced ambient occlusion, two hemisphere rays a pixel; other renderers ignore the cvars; temporal accumulation is off (`r_rt_temporal 0`, the design's pins run without it, TASK59) and the noise is fixed (`r_rt_ao_seed 1`), so the frames are stateless; judged against `ref_metal` itself once the owner accepts the look (TASK57) |
| `rt-ao-entities` | `rt-ao` with `cl_entities 1`: the brush entities and the alias models, which the rays meet and the pass never darkens; recorded with `PINS_SEED`, as `models` (TASK57) |
| `rt-sun` | `world` with `r_rt_sun 1`: dim world surfaces lit by `ref_metal`'s ray-traced sun where a ray toward it reaches the sky (`r_rt_sunscale 0.35`, below `r_rt_sunfloor 0.5`, the direction from `scripts/rt/sun-angles.txt` or `r_rt_sun_angles`); a map under 2 % sky draws `world`'s frames; other renderers ignore the cvars; temporal accumulation is off (`r_rt_temporal 0`, the design's pins run without it, TASK59) and the pass draws no noise, so the frames are stateless; no footprint band yet, so its companion is `models`; judged against `ref_metal` itself once the owner accepts the look (TASK56) |
| `rt-water` | `skywater` with `r_rt_water 1`: `ref_metal`'s ray-traced water reflections, one bounce, blended over the water (`r_rt_water_reflect 0.5`, the warp's tilt at `r_rt_water_normal 0.08`); other renderers ignore the cvars; recorded on `skywater`'s camera path (`PINS_DEMO=water.dm2`); temporal accumulation is off (`r_rt_temporal 0`, the design's pins run without it, TASK59) and the pass draws no noise; judged against `ref_metal` itself once the owner accepts the look (TASK58) |
| `rt-water-entities` | `rt-water` with `cl_entities 1`: the brush entities and the alias models, which the reflection rays meet and the pass never reflects on; recorded with `PINS_SEED` on `skywater`'s camera path (`PINS_DEMO=water.dm2`), as `models` (TASK58) |
| `dlights-lmcubic` | `dlights` with `metal_lightmap_filter 1`; not deterministic unseeded, recorded with `PINS_SEED` (TASK120) |
| `hud2d-sharp` | `hud2d` with `metal_2d_sharp 1`: the sharp-bilinear sample of the 2D pictures of `ref_metal`; other renderers ignore the cvar (TASK119). At the harness's default 640x480 the 2D scale is 1, so it draws exactly `hud2d`; it tests the switch only at another size: `PINS_WIDTH=2560 PINS_HEIGHT=1440 scripts/pins.sh record metal hud2d-sharp`, judged against `hud2d` at that size, from which it must differ |
| `hud2d-scale8` | `hud2d` with `r_scale8bittextures 1`; the HUD pictures are in the scrap, which is smoothed but not scaled (TASK119) |
| `world-scale8` | `world` with `r_scale8bittextures 1`: the 8-bit world textures scaled by `scale2x` or `scale3x` and smoothed (TASK119) |
| `particles-cap` | `particles` with `metal_particle_cap 1`: the largest particle size of `ref_metal` scaled by `height / 480`; other renderers ignore the cvar; recorded with `PINS_SEED` (TASK122). At the harness's default height of 480 the factor is 1 and it draws exactly `particles`, so it tests the switch only at another height: `PINS_WIDTH=2560 PINS_HEIGHT=1440 PINS_SEED=1 scripts/pins.sh record metal particles-cap`, judged against a reference from a build with the switch, or against `particles` at 1440, from which it must differ |
| `world-metalfx1`, `world-metalfx2`, `world-metalfx3` | `world` with `r_metalfx` 1 (`MTLFXSpatialScaler`), 2 (`MTLFXTemporalScaler`) or 3 (`MTLFXTemporalDenoisedScaler`, run as 2 until a ray-traced effect exists) at `r_metalfx_scale 0.5`; other renderers ignore the cvars. A mode the GPU lacks falls back to the best one below with a line in `engine.log`: on the VM (spatial only) all three draw the frames of `world-metalfx1`, pixel for pixel (TASK111) |
| `world-metalfx-interp` | `world` with `r_metalfx_interp 1`: frame generation is not built yet, so the cvar reads 0 with a line and the frames are `world`'s, pixel for pixel (TASK111) |
| `retro` | `world` with `r_presentation 2`: `ref_metal`'s Retro post-process, the 3D view quantized to RGB565 with the 4 x 4 ordered dither, no filter and no curve (`r_retro_filter 0`, `r_retro_gamma 0`); since TASK185 also the 16-bit texels with the 256 cap, the nearest-mip filter and, above 480 lines, the 480-line view (at the harness default of 480 lines the view is unscaled); other renderers ignore the cvars and draw it as `world`; judged against `ref_metal` itself once the owner accepts the look (TASK184, PM4) |
| `retro-filter` | `retro` with `r_retro_filter 1`: the 2 x 2 box output filter over the dithered pixels (TASK184) |
| `rtprimary-world` | `world` with `r_presentation 3`: `ref_metal`'s Ray traced presentation (RT-primary), the lightmap off at the visible world surface and the direct light of the map's light entities in its place, one shadow ray a light, the 16 visible lights a pixel, the exposure fitted once at the map's load from a sample of its luxels (`r_rt_direct_exposure 0`); no noise and no temporal accumulation (each light is traced, none sampled), and the fit depends on the map alone, not on any frame, so the frames are the same run to run: a pin of `ref_metal` itself, judged byte for byte against the run before (the frames' hashes) and provisional until the owner accepts the look (PM11), when it is recorded again and compared at the plan's 0.02 (a scenario cannot carry a threshold: it is `pins.sh compare`'s `--threshold`, default 0.02), also recorded with `PINS_DEMO=demo2.dm2 PINS_EVERY=7` (89 frames, `waste2`, whose captures 61 and 67 show an animated lightstyle; its text in `scripts/pins/text/demo2.dm2-every7.txt`); other renderers ignore the cvars and draw it as `world`; the look is the owner's to accept (TASK188, PM6, PM11) |
| `rtprimary-direct` | `rtprimary-world` with `r_rt_direct_show 1`: the direct term alone, on white albedo; a pin as `rtprimary-world` (TASK188) |
| `rtprimary-vis` | `rtprimary-world` with `r_rt_direct_show 2`: the shadow visibility, grey by the share of the lights in range that their ray reached, dark blue where none is in range; a pin as `rtprimary-world` (TASK188) |
| `rtprimary-noshadow` | `rtprimary-world` with `r_rt_direct_show 3`: the direct term alone with no shadow ray traced, to compare with `rtprimary-direct`; a pin as `rtprimary-world` (TASK188) |
| `rtprimary-dlights` | `rtprimary-world` with `cl_lights 1`: the dynamic lights as a term of RT-primary's light, each in range with one shadow ray, by gl1's radius rule; recorded with `PINS_SEED` (the engine PRNG sets a muzzle flash's radius, as in `dlights`), at the default demo and with `PINS_DEMO=demo2.dm2 PINS_EVERY=7`; seeded, a pin as `rtprimary-world`, provisional until the owner accepts the look (TASK188 slice 3b, PM11) |
| `hdr` | `world` with `r_hdr 1`: `ref_metal`'s float16 frame target, read back as its SDR fallback (each channel clamped to white, in 8 bits); its own reference, not compared with `world`, since float16 blends round once where 8-bit ones round at each layer; other renderers ignore the cvar and draw it as `world` (TASK196, HD2); `r_hdr_peak 1` holds HD3's tone map to that clamp on any display, so the frames do not follow a display's headroom |
| `hdr-metalfx2` | `hdr` with `r_metalfx 2` at `r_metalfx_scale 0.5`: `MTLFXTemporalScaler` on `ref_metal`'s float16 view and frame, before the tone map; its own reference, not compared with `hdr` or `world-metalfx2`; where the device lacks the scaler `r_metalfx` reads the best supported mode below; other renderers ignore the cvars and draw it as `world` (TASK196, HD4) |
| `models` | world plus entities: alias models, sprites, beams, view weapon |
| `particles` | everything, including particles, dynamic lights, screen blend |
| `hud2d` | 2D only: status bar, crosshair, console text; the GL renderers' screen blend off (`gl_polyblend 0`, TASK82) |
| `skywater` | world surfaces, sky and warped water, with the sky and water-warp settings fixed; no entities, particles, dynamic lights or blend; water needs `PINS_DEMO=water.dm2` from `make-demo water` |
| `cinematic` | a cinematic (`.cin`) stretched over the screen, no world; needs `PINS_DEMO` naming a cinematic |

Rules:

- `set timedemo 1` in every file: playback is locked to the demo stream.
- A scenario never names a demo and sets only cvars that exist in `src/`.
  `scripts/pins.sh selftest` (run by the gate) checks both.
- A scenario name matches `[a-z0-9_][a-z0-9_-]*` (for example `rt-ao`), and a
  value holds only `A-Z a-z 0-9 _ . -`: the engine re-joins its command line
  unquoted. The selftest checks the values and `record` refuses others.
- Every file sets every toggle, so no state leaks from one run to the next.
- This tree has no cvar that hides the status bar, so the status bar is in
  every scenario. Isolation is by the client-side toggles `cl_entities`,
  `cl_particles`, `cl_lights`, `cl_blend` and by `r_drawworld`. `cl_blend 0`
  clears the colour of the screen blend but not its alpha, so a pickup still
  darkens the screen; `gl_polyblend 0` (in `hud2d` only) keeps the blend out
  of `gl1` and `gl3`, not out of `soft`. A pin run neither reads nor writes
  `config.cfg` (TASK39), so leaving it out of the other files leaks nothing
  into their runs, even from `gl3`, which archives it.
- A scenario that cannot be deterministic says so in a `// nondeterministic:`
  line and is accepted at a looser threshold, recorded with the pin, or,
  like `dlights`, is recorded with `PINS_SEED` and judged only seeded.
- A scenario's header comment ends with `// frames <demo> <count>` for each
  demo it is recorded with whose count is fixed: the frames the engine
  renders playing it (`timedemo.txt`), 689 for `demo1.dm2` in every
  scenario. `record` and `replay` fail when a run rendered another number
  (FIX48, `doc/100_renderpins.md`, *Input and the rendered-frame count*).
  `dlights` also has one for `demo2.dm2`. A made demo such as `water.dm2`
  and the cinematic have no line and are not checked. The line is in
  lower case and in the header; one in another case or below it is
  refused. A new scenario copies the line of the one it is derived from.

## Content

What each scenario draws, as tags, and the full-content companion of every
scenario that draws no entities (TASK133). A scenario that sets
`cl_entities 0` isolates its effect from the alias models, sprites and beams
on the screen, and the effect is then judged on a picture that is not the
one a player sees: TASK55's ray-traced shadows passed on `cl_entities 0`
frames while a muzzle flash's light, inside its own model, shadowed 37 to 48 %
of the traced pixels with entities on. So a scenario with `cl_entities 0`
names a companion, a scenario that sets `cl_entities 1`, to be recorded and
judged beside it. Tags, from the cvars the scenario sets:

| Tag | The scenario draws it when |
|---|---|
| `world` | `r_drawworld 1` (not the cinematic) |
| `entities` | `cl_entities 1` |
| `alias-models` | `cl_entities 1` (the alias models, sprites and beams are one switch) |
| `particles` | `cl_particles 1` |
| `dynamic-lights` | `cl_lights 1` |
| `2d` | `r_drawworld 0`: the 2D elements are the subject |
| `cinematic` | the `cinematic` scenario only |

The status bar is in every scenario and is not a tag. The scenario, its
tags (comma separated) and its companion (`-` when it sets `cl_entities 1`)
form one row each; `scripts/pins.sh check-content`, which the selftest runs
(case `content_tags`) and so the gate, fails on a scenario without a row, a
row without a scenario, a tag that does not follow the cvars, and a
`cl_entities 0` scenario whose companion is missing, is not a scenario, or does
not set `cl_entities 1`. The last `set` of a cvar in a file is the one that
counts, as it is in the engine's argument list.

A companion carries the effect only for a scenario that declares a footprint
band (see "Footprint band"). For such a scenario that sets `cl_entities 0`,
`check-content` also requires that the companion:

- declares the same bands, in its cfg's leading comment block;
- sets every one of the scenario's cvars, other than the ones below, to the
  same value (a cvar the companion sets and the scenario does not is free).

The cvars that may differ are the content cvars, which decide what is in the
scene, `cl_entities`, `cl_particles`, `cl_lights`, `cl_gun`, `cl_blend` and
`gl_polyblend` (the tags follow them, so a companion with particles is tagged
`particles`), and the demo and frame settings `timedemo`, `scr_conspeed`,
`cl_showfps`, `viewsize` and `crosshair`. Anything else, `r_metalfx`, a
ray-tracing cvar, `r_drawworld`, is an effect or a mode and must match, so the
companion is the same scene with entities on and the effect on.

For a scenario with no band the companion stays nominal: it only has to be a
scenario that sets `cl_entities 1`. That is every scenario in the tree today, so
`models` and `particles` are companions of effect-free neighbours by name, not
because they carry the effect (`world-metalfx1` sets `r_metalfx 1` and `models`
does not). An effect that declares a band names a companion that carries it; the
scenarios in the table whose effect is judged this way get a band and a real
companion when a card judges them. For `hud2d*` and `cinematic`, which draw no
3D view, the companion is nominal. A card that adds a scenario adds its row.

| Scenario | Tags | Companion |
|---|---|---|
| `world` | world | `models` |
| `world-fullbright` | world | `models` |
| `world-lmcubic` | world | `models` |
| `world-scale8` | world | `models` |
| `world-metalfx1` | world | `models` |
| `world-metalfx2` | world | `models` |
| `world-metalfx3` | world | `models` |
| `world-metalfx-interp` | world | `models` |
| `retro` | world | `models` |
| `retro-filter` | world | `models` |
| `rtprimary-world` | world | `models` |
| `rtprimary-direct` | world | `models` |
| `rtprimary-vis` | world | `models` |
| `rtprimary-noshadow` | world | `models` |
| `rtprimary-dlights` | world,dynamic-lights | `particles` |
| `hdr` | world | `models` |
| `hdr-metalfx2` | world | `models` |
| `dlights` | world,dynamic-lights | `particles` |
| `dlights-lmcubic` | world,dynamic-lights | `particles` |
| `models` | world,entities,alias-models | `-` |
| `particles` | world,entities,alias-models,particles,dynamic-lights | `-` |
| `particles-cap` | world,entities,alias-models,particles,dynamic-lights | `-` |
| `hud2d` | 2d | `models` |
| `hud2d-sharp` | 2d | `models` |
| `hud2d-scale8` | 2d | `models` |
| `skywater` | world | `models` |
| `cinematic` | cinematic | `models` |
| `rt-shadows` | world,dynamic-lights | `rt-shadows-entities` |
| `rt-shadows-entities` | world,entities,alias-models,dynamic-lights | `-` |
| `rt-ao` | world | `rt-ao-entities` |
| `rt-ao-entities` | world,entities,alias-models | `-` |
| `rt-water` | world | `rt-water-entities` |
| `rt-water-entities` | world,entities,alias-models | `-` |
| `rt-sun` | world | `models` |

TASK55 adds `rt-shadows` (tags `world`, `dynamic-lights`; companion
`rt-shadows-entities`) and `rt-shadows-entities` (tags `world`, `entities`,
`alias-models`, `dynamic-lights`; companion `-`) here, and the cfg of each
carries `// footprint darker 0..0.12` in its leading comment block (0.05 holds without
entities, whose worst frame is 1.44 % darker, but not with them: see `docs/runbook/TASK55.md`). `rt-shadows`
then has to pass the rule above: `rt-shadows-entities` declares the same band and
sets every cvar of `rt-shadows` (the ray-tracing ones too) to the same value,
and sets `cl_entities 1`.

## Footprint band

What an effect changes in the picture, and how much it may. Record the
scenario with the effect off and on (the cvar that switches it, or a replay of
one trace, which keeps the camera and the entities identical), then
`scripts/pins.sh footprint <scenario> --off <record> --on <record>` prints, per
frame, the share of pixels changed by more than 8 levels (`changed`), split
into `darker` and `brighter`, the largest change in levels (`max`) and the mean
change in levels (`mean`). A scenario declares the band its effect must stay
in with header lines in its cfg:

    // footprint darker 0..0.05
    // footprint max 0..90

A line is `// footprint <metric> <lo>..<hi>`, the metric one of `changed`,
`darker`, `brighter` (shares of the pixels, 0 to 1), `max` or `mean` (levels,
0 to 255), both bounds inclusive; any number of lines, in the cfg's leading
comment block (the lines before its first `set`). A line that starts `//` and
`footprint` and is not of that form, `//footprint ...` for one, is an error, not
a comment. The scenario has to exist: a misspelled name is refused. `max` and
`mean` average the three channels, so the table also has `chmax`, the largest
change of any one channel, which shows a shift of colour alone. The command
names every frame outside a band and exits non-zero. A scenario without such a line has its
footprint reported and never fails. To set a band: record the effect on the
`cl_entities 0` scenario, read the worst frame, and set the upper bound at a
small multiple of it (TASK55: about 1 % darker without entities, band
`darker 0..0.05` held without entities); the entity-bearing companion carries the same band, which is
where a defect that only shows with entities is caught. `--band` replaces the
cfg lines for one run. `scripts/pins.sh where` draws where the effect changed
the picture, for the owner.

First run on game data (TASK39, `soft`): `world` and `hud2d` are
identical run to run; `models` and `particles` are not and carry their
measured thresholds in their `// nondeterministic:` lines. The owner's data
holds `demo1.dm2` and `demo2.dm2`. Behind the 2D elements of `hud2d`,
`soft` shows a uniform dark grey; other renderers are not measured yet
(see `docs/runbook/TASK39.md`).

Added by TASK40 (`soft`, two runs each and a third): `skywater` and
`cinematic` are identical run to run (score 0 on every frame). On
`demo1.dm2` the sky is in frame 21 at the default `PINS_EVERY` 30; neither
demo showed warped water on any frame looked at, so `skywater` exercises
the sky and the water-warp settings but not a water surface on this data.
`cinematic` was recorded with `idlog.cin`. See `docs/runbook/TASK40.md` for the frame
lists.

Added by TASK70: a demo that shows water, made from the game data by a
committed console script and never committed itself.
`scripts/pins.sh make-demo water` runs `scripts/pins/demos/water.cfg` and
leaves `water.dm2` beside the binary (`release/baseq2/demos/`); record
`skywater` from it with `PINS_DEMO=water.dm2`. A console `wait` is a
lower bound on time, so the demo is longer on a busy host; the script
states the message range it must give and `make-demo` fails a demo
outside it. From a demo in range (`soft`, the default `PINS_EVERY` 30)
the first 2 frames show the pool of `base1` from just above its surface
and the rest (2 or 3) from under it: a demo of 96 to 147 messages gives 4
or 5 frames, and the id of a record includes the demo's checksum. Records of one demo file compared at score 0
in 27 pairs, but differ slightly when the engine stalls, so `skywater`
from `water.dm2` is accepted at `--bad-threshold 0.03` (its
`// nondeterministic:` line and the runbook). A script under
`scripts/pins/demos/` may use only commands that exist in `src/`; the
driver checks that before it runs one, and the selftest checks every
committed script.

Added by TASK56 (review round 1): `scripts/pins.sh make-demo lowsky` runs
`scripts/pins/demos/lowsky.cfg` and leaves `lowsky.dm2`, a demo on
`fact2`, whose sky `ref_metal` measures at 0.72 % of the world's face
area, under `r_rt_sun`'s 2 % threshold; the player stands at the level
start and turns in place. `rt-sun` recorded from it with
`PINS_DEMO=lowsky.dm2` draws `world`'s frames: on `ref_metal`, 7 of 7
frames byte-identical with `PINS_SEED=1` (`docs/runbook/TASK56.md`).

Added by TASK91 (`ref_metal` N8): `dlights`. Unseeded, two `gl1-metal`
records differ wherever a muzzle-flash or explosion light reaches (worst
score 0.009399 on `demo1.dm2`, 0.012701 on `demo2.dm2` at `PINS_EVERY=7`,
over five records each), by more than a missing lightstyle update moves a
frame, so no unseeded threshold guards the update. It is recorded with
`PINS_SEED` (any value, the same for the reference and the candidate,
compared with `--ref-id`): two `gl1-metal` records at `PINS_SEED=1` were
identical byte for byte on all 23 frames of `demo1.dm2` and all 89 of
`demo2.dm2` every 7th, and `metal` compared at `--threshold 0.005
--bad-threshold 0.001` on every frame of both (worst score 0.000001, bad
0.000013). On `demo2.dm2` every 7th frames 51, 61, 63, 67 and 69 show an
animated lightstyle; a seeded control with `gl1_dynamic 0` goes over those
values on 61, 67 and 69 (and on 29 frames whose dynamic light it drops),
while on 51 and 63 it moves pixels by one level only. See
`docs/runbook/TASK91.md`.
