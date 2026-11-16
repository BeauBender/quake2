# Closing TASK21 on the build host

The umbrella card's acceptance list, checked end to end after S1 to S6
on the build VM, with the shim of commit `0c4d6aa6` (this card's; it
adds the capability report and the command-buffer error report to the
merged shim). The Metal device is `Apple Paravirtual device`. On this
host the `gl1` references come from Apple Software Renderer ("`gl1` on
the build host (TASK80)"), so "every pin passes against `gl1`" is
pending TASK83 (#187) for the 3D scenarios; the 2D scenarios, `hud2d`
and `cinematic`, pass. No threshold or scenario was changed. TASK83
closed that on the bare-metal host for every scenario except `skywater`
from `water.dm2`: they pass against accelerated `gl1` references, `models`
also with `gl1_stencilshadow 1`; `skywater` from `water.dm2` passes too,
but on a demo `make-demo water` refused, so that half stays open until
#232 is done ("`gl1` references from an accelerated OpenGL (TASK83)").

**Coverage.** `tests/metal/shim-coverage.sh`, run by
`tests/check-metal-shim.sh`, derives the 59 entry points (54 called
directly, 5 loaded by pointer under 10 names) and checks that the shim's
objects define each one, that the linked `ref_gl1.dylib` leaves no `_gl`
symbol undefined, and that the table of `docs/plans/metal-shim-design.md`
section 14 names exactly those 59: 50 implemented, 5 partial
(`glClear`, `glDrawElements`, `glPolygonMode`, `glTexEnvi`,
`glTexImage2D`), 3 no-op (`glDrawBuffer`, `glHint`, `glShadeModel`: `GL_FLAT` is
stored and not applied), 1 stub
(`glColorTableEXT`, which gl1 never loads because the shim does not
advertise `GL_EXT_shared_texture_palette`). Each of 15 mutants of its
inputs (a row removed, renamed, duplicated or given an unknown status,
the table markers removed, a proc-table entry or alias removed, a new
`gl` call without a row, a call removed from the source while the
objects still import it, a shim object missing, a dylib with undefined
`_gl` symbols, missing objects or dylib, the identifier test dropped
from the source scan) fails it, and each of the three guard lines
removed lets its bad input through. No run below logged a `not
implemented` line, and the `ASAN=1` build, whose gl1 checks
`glGetError` after its calls (`glCheckError_`, `DEBUG` only), logged no
`glError` line in any scenario: no refused or stubbed value is reached
by the traces.

**gl1's C is untouched.** Against the fork point (`9dbf6992`, the last
upstream merge), `git diff --stat 9dbf6992 HEAD --
src/client/refresh/gl1 ':(exclude)src/client/refresh/gl1/metal'` lists
11 files, all changed by TASK5, TASK13, TASK27, FIX25, TASK76 and TASK80
and none by a shim card (`git log --format=%s 9dbf6992..HEAD` on the
same paths has no TASK47 to TASK52 or TASK82 subject). Not even the
context seat changed: `shim_sdl.h`, prepended with `-include` to gl1's
files in a `WITH_METAL_SHIM=yes` build, maps the SDL OpenGL calls of
`gl1_sdl.c` onto the shim, and the include path puts the shim's
`<OpenGL/gl.h>` first. `grep -rniwE 'metal|mgl_[a-z_]*'` over gl1's `.c`
files and `header/` finds nothing.

**Validation.** Every record ran with `MTL_DEBUG_LAYER=1` (the log
says `Metal API Validation Enabled`). A positive control, a scratch
program that blits past the end of a buffer, aborts under it with
`failed assertion` on stderr, which a record keeps in `engine.log`, so
a validation failure would also end the record incomplete.
`MTL_SHADER_VALIDATION=1` does not work on this VM: under it a scratch
program completes a three-vertex draw whose positions come from the
vertex id, but hangs the GPU (`Caused GPU Hang Error`, then `Ignored
(for causing prior/excessive GPU errors)` for every later command
buffer) as soon as the draw reads a device buffer or samples a
texture; every variant completes with the variable unset. Under
it the shim's first records (commit `39a4ceec`) captured frames of
noise without a word in the log; `hud2d` scored 0.47 and bad 1.0
against `gl1` on every frame, and exact with the variable unset. That
silence was a shim defect: a command buffer that ends in error is now
logged (`mgl: a command buffer failed: ...`, the first eight) and
counted at teardown (`mgl: N command buffers failed`); the same run
then logs the hang and 736 failed command buffers. With
`MTL_DEBUG_LAYER=1` alone, at `0c4d6aa6`, three release records of each
of the six scenario runs (`skywater` from `demo1.dm2`; `cinematic` with
`PINS_DEMO=idlog.cin PINS_EVERY=7`), one of each with an `ASAN=1` build
(`PINS_BIN`), and one each of two scratch copies (not committed:
`models` with `r_shadows 1`, `gl1_stencilshadow 1` and `cl_lights 1`;
`world` with `gl1_multitexture 0`): 26 records, no validation message,
no sanitizer line, no failed command buffer, and the shim's counters
`707 frames, 0 without a drawable, 0 skipped, 0 draws past the vertex
ring` (`cinematic` 713 to 718 frames; 2454 in the `ASAN=1` build). The
1-minute load was 3.5 to 11.7 during the records. `skywater` from
`water.dm2` was not run: its pin id carries the checksum of a demo made
by `make-demo water`, and no reference of this store matches a new one.

**`timedemo`.** Every scenario is a `timedemo 1` run of `demo1.dm2`;
`world` is the stock one. The release records' lines read `689 frames,
5.7 to 5.8 seconds: 119.0 to 120.2 fps` (the `world` copy with
`gl1_multitexture 0`: 6.9 seconds, 100.2 fps; the `ASAN=1` build's
`world`: 3.5 seconds, 199.4 fps). Every record completed and quit by
itself with status 0.

**Frames against `gl1`.** Each of the three release records of a
scenario against the newest `gl1` reference of the owner's store with
the same settings hash, copied into a private store (commits
`92302e46596c` for `world` and `skywater`, `74205ac99199` for `hud2d`,
`110a25ae2d26` for the others). The three records gave the same verdicts
on the same frames:

| Scenario | Accept at | Frames passing in each of 3 records | Worst score, bad |
|---|---|---|---|
| `hud2d` | defaults | 23 of 23, all exact | 0, 0 |
| `cinematic` | defaults | 12 of 12 | 0.000191, 0 |
| `world` | defaults | 7 of 23 (over: 0 to 8, 10, 11, 14 to 16, 21, 22) | 0.025886, 0.632692 |
| `skywater`, `demo1.dm2` | defaults | 7 of 23, the same frames | 0.025886, 0.632692 |
| `models` | defaults | 6 of 23 (`world`'s and 13) | 0.031575, 0.676068 |
| `particles` | 0.02, 0.25 | 18 of 23 (over: 5, 8, 11, 13, 16) | 0.131757 to 0.132777, 0.938193 to 0.938249 |

These are the counts TASK49 to TASK52 recorded, whose faults are the
reference's; no frame changed verdict.

**Startup capability report.** Each context creation logs to the console
(and so to `engine.log`): `Metal device: Apple Paravirtual device`,
`Metal supportsFamily (apple5 to apple9): apple5`, `Metal
supportsRaytracing: no, supportsRaytracingFromRender: no`. The device
answers only the apple5 floor.

**`vid_restart` between `gl1` (shim) and `soft`.** A console script in
the build directory (not committed) loads `base1`, then switches `soft`,
`gl1`, `soft`, ... with `vid_restart` and about 1.5 seconds of frames
after each, and quits. Under the `ASAN=1` build with
`MTL_DEBUG_LAYER=1`, four cycles (four restarts each way): status 0, no
sanitizer line, five shim teardowns (`mgl: 283`, 129, 131, 133, 132
frames). Under the release build with `MallocStackLogging=1` and
`leaks --atExit`:

| Cycles | Leaks | Live at exit | Physical footprint at exit |
|---|---|---|---|
| 1 | 0 | 40500 nodes, 7431 KB | 75.4 MB |
| 2 | 0 | 41647, 7943 KB | 84.7 MB |
| 4 | 0 | 44082, 8967 KB | 96.0 MB |
| 8 | 0 | 48625, 10960 KB | 118.2 MB |

No leak, and no crash, but the live heap grows by about 1140 to 1220
nodes and 500 KB per cycle, and the footprint by 5.6 MB per cycle after
the second (9.3 MB from the first to the second); 1-minute load 9.1 to
13.6. The memory graphs after 2 and after 8 cycles (`leaks
--outputGraph`, then `heap`) say what is kept: every window the engine
ever opened (`CGSWindow` 5 and 17: one per restart, for both renderers)
with its display link, `CAContext` and layers, and with each `gl1`
window its `CAMetalLayer` and metal view (2 and 8) and the four
IOSurface-backed textures of the layer's drawables (8 and 32
`MTLDebugTexture`). None of the shim's own objects grows: at exit there
is no `MTLBuffer`, pipeline, sampler or depth-stencil state or library,
no texture beyond the drawables, and one command queue after 2 cycles as
after 8. The shim's teardown
frees what it made; the windows are kept outside it (`@autoreleasepool
content` grows by 295 KB over the six cycles, which suggests window
objects autoreleased into a pool the main loop never drains; not
proven). So "`vid_restart` without leaks" holds for the shim and not
for the process; the window retention is a finding for a card outside
the port queue: FIX41 (#193). FIX41 has since proven the causes and
corrected one point of this paragraph: the kept `CAMetalLayer`s and metal
views belonged to the `soft` windows, not the `gl1` ones (see
[`vid_restart` and the main thread's autorelease pool (FIX41)](FIX41.md)).

**Criterion 1 and the GL-call pins.** The card's first criterion asks
that every GL-call pin of the reference set pass. It is met here by the
coverage report, the host test (`tests/metal/mgl_host_test.c`) and the
frame pins above. No GL-call recorder, trace or GL-call pin was built or
run in this repository, so the GL-call-pin half of the criterion is not
met as worded. The enum values the section 14 table names as refused
come from reading the source and from the runs that logged no `not
implemented` or `glError` line, not from a recorded trace.

**Deviations from the card.** The shim lives in
`src/client/refresh/gl1/metal/`, not in the suggested
`src/client/refresh/glmetal/`, which keeps it next to the one renderer
it serves. It was written in this tree, not seeded from any external
seed. Nothing was promoted elsewhere.
