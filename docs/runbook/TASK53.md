# Acceleration structures in `ref_metal` (TASK53, RT1)

RT1 builds the acceleration structures of `docs/plans/metal-raytracing-design.md`,
section 1, which nothing reads yet; the counts and times are in that section. Host: the
bare-metal Mac, `Apple M6` (`supportsRaytracing: yes`), macOS 27.2.1 (26B131). The pins,
the validation runs and the ASAN run are of engine commit `0269ca35` (pin ids
`0269ca350d6b-*`), the card's final source on its first base, `1701a052`. The card was then
rebased onto `82f2671f` (TASK122, #301), whose only source change is `metal_particle_cap`, off
by default, so the card's own `src` and Makefile are unchanged by the rebase; the counts and times are
of `eaf80979`, which differs from it only in where `RI_Init` registers the two cvars (a run of
`base2` at `0269ca35` gave the same counts). Screen unlocked before every run (`ioreg -n Root -d1 | grep -c
CGSSessionScreenIsLocked` 0), no other `quake2`; one-minute load 2.0 to 4.0 for the single runs
and 3.5 to 5.3 for the records (another agent was building). Every log and frame is in
`<artifacts>/TASK53/` and `<pin store>`.

```sh
export PINS_STORE=<pin store> PINS_BASEDIR=<game data> MTL_DEBUG_LAYER=1
make client game ref_metal
scripts/pins.sh record metal <scenario>            # PINS_SEED=1 for models, particles, dlights as well
# with the structures built: scenarios/<s>-rtb.cfg (<s>.cfg plus "set r_rt_stats 1"), deleted after;
# the cinematic by the line appended to cinematic.cfg for one record (only that scenario plays a .cin)
scripts/pins.sh record metal <s>-rtb
scripts/pins.sh compare metal <scenario> --ref gl1 --ref-id <TASK83 record> --cand-id <record> [values]
cmp <default frame> <the -rtb record's frame of the same number>
```

**Pins.** Against the `gl1` records of TASK83 (`de1da7fdba66-*`, `f6bb192775b8-*`, the two
accelerated references), at the values of "Accept `gl1-metal` against `gl1` at":

| Scenario | Value | Each of the two `gl1` references |
|---|---|---|
| `world` | the defaults | 23 of 23, worst 0.000010, bad 0.000033 |
| `world-fullbright` | the defaults | 23 of 23, worst 0.000031, bad 0.000072 |
| `hud2d` | the defaults | 23 of 23, 0, 0 |
| `skywater`, `demo1.dm2` | the defaults | 23 of 23, worst 0.000010, bad 0.000033 |
| `cinematic` (`idlog.cin`, every 7th) | the defaults | 12 of 12, 0, 0 |
| `models` | the defaults | 23 of 23, worst 0.000084 and 0.000236, bad 0.000612 |
| `particles` | 0.02, 0.25 | 23 of 23, worst 0.010409 and 0.010403, bad 0.153356 and 0.181045 (the references' scenario file differs from today's by its header comment only) |

The store holds no `gl1` record of `dlights`; seeded `dlights`, `models` and `particles`
(`PINS_SEED=1`) were compared with the seeded `gl1-metal` records of `bdda98128905` at 0.005 and
0.001: 23 of 23 each, `dlights` 0 and 0, the other two worst 0.000039, bad 0.000589. With the
structures built, every frame of `world`, `world-fullbright`, `hud2d`, `skywater`, `cinematic`
and of seeded `models`, `particles` and `dlights` is byte-identical to the frame recorded without
them (`cmp`: 23 of 23, 12 of 12 for `cinematic`), so those records pass as the ones above do.
Unseeded `models` and `particles` were not recorded with the structures (two unseeded records
differ by themselves). Every record's `engine.log` holds `Metal API Validation Enabled` and no
other validation line, no failed command buffer and `teardown: 0 live objects`; without the
structures it has no `rt:` line and `rt 0/0`, with them the map's line and `rt 66/66` (no
alias models in the scenario, `cl_entities 0`) or `rt 309/309` (`models`, `particles`). The
cinematic's record with `r_rt_stats 1` built nothing (`rt 0/0`): a cinematic draws no 3D view.

**Counts and times.** `r_rt_stats 2`, one line per frame, 640 x 480, `r_vsync 0`, `timedemo 1`,
nothing captured (`pins_capture 100000`): `+demomap demo1.dm2` (plays `base2`), `+demomap
demo2.dm2` (`waste2`; the data has no `demo3.dm2`), and `base1` from a scratch script beside the
binary (`map base1`, 400 one-frame waits, `quit`), each without the validation layer; the warm
figures skip each run's first 100 frame lines. The GPU times are counter samples at the encoders'
boundaries (`MTLCommonCounterSetTimestamp`, converted through `sampleTimestamps`); a run of
`base2` with `MTL_GPU_TIME=1` gave 0.142 ms per frame command buffer (median) without the
structures and 0.841 ms with them, against 0.821 ms of lerps, refits and rebuild by the samples,
which places the samples' scale. `r_rt_gunshadow 1` on `base2` adds the view weapon (72
instances on frame 91 instead of 71, `0 weapon` left out).

**Validation.** One run per map with `MTL_DEBUG_LAYER=1` and `r_rt_stats 2`: status 0, only the
validation banner, no failed command buffer, `teardown: 0 live objects` (`rt 309/309` on
`base2`, `463/463` on `waste2`, `74/74` on `base1`).

**ASAN.** `make ASAN=1 client game ref_metal`; `debug/quake2` with `MTL_DEBUG_LAYER=1`,
`r_rt_stats 1` and a scratch script (`map base1`, 200 waits, `map base2`, 200 waits,
`vid_restart`, 300 waits, `quit`): status 0, no sanitizer line, both maps' structures built and
the summary line printed at each map change, two renderer sessions, each teardown `0 live
objects` (`rt 176/176`, then `rt 120/120`).

**No device.** This host cannot show the path without ray tracing (the VM's); the decision is
`rm_rt_wanted` (`rm_rt.c`), host-tested (`r_rt_stats 1` or `2` with no ray tracing on the
device: 0), called by `RM_RT_Wanted` (`rm_rtscene.c`) before anything else in
`RM_RT_MapLoaded` and `RM_RT_Frame`, whose only other effect is one line saying the device has
no ray tracing; `rmb_rt_open` checks `supportsRaytracing` again.
