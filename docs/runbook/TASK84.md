# The `ref_metal` skeleton (TASK84, N1)

`ref_metal` (design: `docs/plans/ref-metal-native.md`, as built in its
section 16) is recorded under its own name, with no change to
`scripts/pins.sh`:

```sh
make client game ref_metal
MTL_DEBUG_LAYER=1 scripts/pins.sh record metal hud2d
```

At this stage it clears and presents and draws nothing, so a `hud2d`
record is 23 frames of the clear colour (255, 0, 128 with the
scenario's `r_clear 1`); nothing is compared against `gl1-metal` yet. A
record is accepted when it completes, every frame is that colour (a
scratch script over the TGAs, not committed), `engine.log` holds the
three capability lines (`Metal device:`, `Metal supportsFamily (apple5
to apple9):`, `Metal supportsRaytracing:`) and no `ref_metal: a command
buffer failed` line, and its last `ref_metal: teardown:` line reports
`0 live objects` and `0 unreleased`.

On the build VM, commit `0da03fd3`, load averages 7.9 to 12.2: three
release records and one `ASAN=1` record (`PINS_BIN=debug/quake2`), all
with `MTL_DEBUG_LAYER=1`, each 23 frames, every pixel 255, 0, 128,
status 0, no validation message beyond `Metal API Validation Enabled`,
no failed command buffer, no sanitizer line; timedemo 2590 to 2883 fps
(release), 1837 fps (`ASAN=1`).

The restart check is a config file in the build's `baseq2/` (not
committed) that sets `metal_stats 1`, takes a `screenshot`, and runs
`set vid_renderer soft; vid_restart` and `set vid_renderer metal;
vid_restart` four times with ten `wait`s between steps, then `quit`;
the engine is started with `vid_renderer metal`, `r_mode -1` at
640x480 and `s_initsound 0`. Two runs with the `ASAN=1` build and the
validation layer: status 0, no sanitizer line, five `ref_metal`
sessions each, every teardown `0 live objects ... 0 unreleased, 0
unmatched`. Window retention across `vid_restart` outside the module is
FIX41's (#193) and is not measured here.

With `ref_metal.dylib` missing, `vid_renderer metal` falls back through
the existing order (`gl3`, `gl1`, `soft`); while capturing, the engine
refuses the fallback as for every renderer.
