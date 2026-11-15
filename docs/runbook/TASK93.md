# `ref_metal` on the build host (TASK93, N10)

N10 closes group N: the lifecycle (restarts, sizes, vsync, gamma), the selection on macOS and
the sweep again (`docs/plans/ref-metal-native.md`, section 25). FIX45 (#230) is folded in: the
frame is the mode's size, not the window's.

**The sweep.** References: `gl1-metal` at the card's base, `9a38a4e9`, built from a detached
checkout of that commit (the Makefile has no header dependencies on macOS, so `ref_gl1.dylib`
was relinked and every `ref_metal` object rebuilt after each checkout; `stale_engine` refuses a
module older than a header otherwise). Candidates: `metal` at `93be24f9` (pin ids
`93be24f93eea-*`); the commits up to `1e53d562` change no file under `src` or the Makefile
(`git diff 93be24f9 1e53d562 -- src Makefile` is empty); `f1e23000` changes `rmb.h` and
`rmb_encode.m` (the black clear of a drawable larger than the frame), covered by the records
made again below and by the fullscreen-window run under **Sizes and FIX45**. `particles` was recorded again at `fe97ef2e`, whose
scenario file has the new header comment (a comment changes the pin id, not the engine's input).
A scratch `world-gamma` (`world.cfg` plus `set vid_gamma 1.6` and `set gl1_intensity 3`) was put
in `scripts/pins/scenarios/` for the records and deleted after. Every record with
`MTL_DEBUG_LAYER=1`; the seeded ones with `PINS_SEED=1` and the trace; `water.dm2` made once by
`make-demo water` at the base and deleted after. Recorded 19:35 to 19:38 and 20:16 local, load
about 10:

```sh
export PINS_STORE=<private store> PINS_BASEDIR=<game data> MTL_DEBUG_LAYER=1
# at the base: make client game ref_soft && make ref_gl1 WITH_METAL_SHIM=yes
scripts/pins.sh record gl1-metal <scenario> [--run 2]        # PINS_SEED=1 for models (second pair), dlights, particles
# at the card: make client game ref_metal
scripts/pins.sh record metal <scenario>
scripts/pins.sh compare metal <scenario> --ref gl1-metal --ref-id <reference> --cand-id <candidate> <values>
```

| Scenario | Value (design section 11) | `metal` against each reference |
|---|---|---|
| `hud2d` | 0.005, 0.001 | 23 of 23, score 0, bad 0 |
| `cinematic` (`idlog.cin`, every 7th) | 0.005, 0.001 | 12 of 12, 0, 0 |
| `world` | 0.005, 0.001 | 23 of 23, 0, 0 |
| `world-fullbright` | 0.005, 0.001 | 23 of 23, score 0, bad at most 0.000013 |
| `skywater`, `demo1.dm2` | 0.005, 0.001 | 23 of 23, 0, 0 |
| `skywater`, `water.dm2`, two records | 0.02, 0.03 | 4 of 4 each, 0, 0 |
| `models`, two records | 0.02, 0.005 | 23 of 23 each (worst 0.000103, bad 0.000589; 0.000152, 0.000589); also at 0.005, 0.001 |
| `models` seeded, two records | 0.005, 0.001 | 23 of 23 each, worst 0.000039, bad 0.000589 |
| `dlights` seeded, `demo1.dm2`, two records | 0.005, 0.001 | 23 of 23 each, 0, 0 |
| `dlights` seeded, `demo2.dm2` every 7th, two records | 0.005, 0.001 | 89 of 89 each, worst 0.000001, bad 0.000013 |
| `particles` seeded, two records | 0.005, 0.001 | 23 of 23 each, worst 0.000039, bad 0.000589; the `fe97ef2e` record byte-identical to the `93be24f9` one |
| `world-gamma` (scratch) | 0.005, 0.001 | 23 of 23, 0, 0; its frames differ from `world`'s on 99 percent of the pixels |
| `gl1-metal` record 2 against record 1 | 0, 0 | seeded `models`, `dlights` (both demos), `particles` and `skywater` from `water.dm2`: identical; unseeded `models` over on 1 frame (0.000116) |

Every `engine.log` holds only `Metal API Validation Enabled` beside the game's output and, for
`metal`, `teardown: 0 live objects`, `state 45/45`, `0 unreleased`, `0 unmatched`.

At `f1e23000` (the present clears a drawable larger than the frame; nothing else under `src`
changed) every `metal` record but `skywater` from `water.dm2` and the scratch `world-gamma` was
made again: each passes against each reference as above (unseeded `models` worst 0.000233 and
0.000123, bad 0.000589), and `hud2d`, `cinematic`, `world`, `world-fullbright`, `skywater`,
seeded `models`, both `dlights` and `particles` are byte-identical to the `93be24f9` records.
The two left out read the same target and present path as the others (pin frames are windowed,
the frame the drawable's size, so the new clear never runs).

**Restarts under `ASAN=1`.** `make ASAN=1 client game ref_metal ref_soft` and `make ASAN=1
ref_gl1 WITH_METAL_SHIM=yes` (in `debug/`); a console script beside the binary (deleted after):
`set r_shadows 1`, `set gl1_stencilshadow 1`, `map base1`, then eight cycles of `set vid_renderer
R`, `vid_restart`, a wait, `screenshot`, for `gl1`, `soft` and `metal`, with `gl1_flashblend 1`
and `map base2` before cycle 3, the three off and `map base1` before cycle 5, shadows on and `map
base3` before cycle 7, then `quit`; run as `MTL_DEBUG_LAYER=1 ./quake2 -datadir <game data>
-portable +set vid_renderer metal +set r_mode 4 +set s_initsound 0 +exec <script>`. `wait` waits
one frame of at least 16 ms whatever its argument (`Cmd_Wait_f`), so the wait is an alias of 120
`wait`s; with single `wait`s restarts given in quick succession were lost (one `vid_restart`
taking effect for two `set vid_renderer` lines), which is the engine's command timing, not a
renderer's. Result: status 0, 25 loads in the order asked (`metal`, then `gl1`, `soft`, `metal`
eight times), 25 screenshots, no sanitizer line, no validation message beyond `Metal API
Validation Enabled`, nine `ref_metal` teardowns each `0 live objects ... 0 unreleased, 0
unmatched`.

**`leaks --atExit`.** FIX41's procedure ("`vid_restart` and the main thread's autorelease pool"):
release build, 30 `wait`s, N cycles of `set vid_renderer R; vid_restart` and 30 `wait`s, `quit`,
under `MallocStackLogging=1 leaks --outputGraph=<file> --atExit --`; `leaks <file>` for the
count, `heap <file>` for the footprint, the node count and the classes. Two runs of each, load
about 10:

| Configuration | 1 cycle: footprint, nodes | 8 cycles: footprint, nodes | per restart, 1 to 8 cycles |
|---|---|---|---|
| `soft` | 62.3, 60.5 MB; 37,782, 37,669 | 61.4, 61.5 MB; 38,515, 38,515 | 105 to 121 nodes |
| `gl1` (shim) | 62.2, 62.0 MB; 37,598, 37,615 | 61.7, 61.7 MB; 38,528, 38,547 | 133 nodes |
| `metal` | 61.7, 61.6 MB; 37,631, 37,590 | 61.7, 61.9 MB; 38,457, 38,463 | 118 to 125 nodes |
| mix3 (`metal`, then `gl1`, `soft`, `metal` a cycle), one run | 64.8 MB; 38,079 | 67.8 MB; 40,390 | 110 nodes (330 a cycle) |

Every run: status 0, `0 leaks for 0 total leaked bytes`, 0 `NSCGSWindow`, 0 `CAMetalLayer`, 0
`SDL3_cocoametalview` at exit, every `ref_metal` teardown (2 after 1 cycle, 9 after 8) at `0 live
objects`. A `metal` restart grows the heap by less than a `gl1` one and within the spread of
`soft`'s; the footprint moves by -0.13 to 0.14 MB a cycle in every configuration but mix3 (0.4 MB
a cycle of three restarts, FIX41's 0.3 to 0.4), within the run-to-run spread at this load.

**Sizes and FIX45.** A console script beside the binary (deleted after) with `developer 1`, `map
base1`, then for each mode a `vid_restart`, a wait and `screenshot`; the size of each TGA from its
header. The same script for this card's `metal`, the base's (`ref_metal.dylib` built from `git
archive 9a38a4e9` in a scratch directory, next to a copy of the engine) and `gl1-metal`:

| Mode | Client's `Drawable size` | `metal` | `metal` at the base | `gl1-metal` |
|---|---|---|---|---|
| `r_mode 4`, windowed | 640x480 | 640x480 | 640x480 | 640x480 |
| `r_mode 6`, windowed | 800x600 | 800x600 | 800x600 | 800x600 |
| `r_mode -1`, 1024x640, windowed | 1024x640 | 1024x640 | 1024x640 | 1024x640 |
| `r_mode 10`, `vid_fullscreen 1` | 640x480 (reverted) | 640x480 | 640x480 | 640x480 |
| `r_mode -2`, `vid_fullscreen 2` | 1333x888 | 1333x888 | 1333x748 | 1333x888 |
| `r_mode 4`, `vid_fullscreen 2` | 640x480 | 640x480 | 1333x748 | 640x480 |

`vid_fullscreen 1` cannot change the display mode on this host (`SDL SetVideoMode failed:
CGDisplaySwitchToMode(): Unknown Error`, then the client's `Reverting to windowed r_mode 4
(640x480)`), so TASK98's case, a display mode other than the one asked for, does not appear here
and is to be repeated on the bare-metal host (done: **On the bare-metal host (FIX45)**, below). The fullscreen window shows the same defect: its
pixels (1333x748 here) are neither the desktop mode nor the mode asked for, and the base rendered
and read back at them. With the frame at the mode's size `metal` writes what `gl1-metal` writes
and what the client's line says, in every mode. HighDPI windows are not made on macOS (section 25)
until TASK118 (below).

**On the bare-metal host (FIX45).** The case above, run on the bare-metal Apple Silicon Mac
(Apple M6) at engine `c01bd7d2` (the TASK93 merge on `main`): `make client game ref_metal
ref_gl1` in a worktree, `release/` copied out, then `make ref_gl1 WITH_METAL_SHIM=yes` and copied
again, so `ref_gl1` (`GL_RENDERER: Apple M6`) and the shim (`GL_RENDERER: Metal shim on Apple M6
(gl1-metal)`) ran beside the same `quake2` and `ref_metal.dylib`. The display: one 5120x2880
Retina panel, desktop mode 2560x1440 points, 5120x2880 pixels, 60 Hz (a 24-line C probe kept
outside the repository, `CGDisplayCopyDisplayMode` on the main display, printed `points 2560x1440
pixels 5120x2880 refresh 60.00`). A console script beside the binary (deleted after): `developer
1`, `map base1`, 100 `wait`s (an alias), `screenshot tga`, 30 `wait`s, `quit`; one run per mode,
the mode on the command line: `./quake2 -datadir <game data> -portable +set vid_renderer R +set
vid_fullscreen F +set r_mode M [+set r_customwidth W +set r_customheight H] +set s_initsound 0
+exec <script>` under `timeout 120`, `config.cfg` removed before each run, every `metal` run with
`MTL_DEBUG_LAYER=1`. The harness capture is the line `PINS_DRYRUN=1 scripts/pins.sh record metal
world` printed (`r_mode -1`, 640x480, `demo1.dm2`, `pins_capture 30`) with `vid_renderer` and
`vid_fullscreen 1` set by hand and a private `pins_dir`, as TASK98 ran it, under `timeout 300`. Each TGA's size from its header. The client's lines are
the same for the three renderers in every mode:

| Mode | `Setting mode` | `User requested ..., setting closest mode` | `Real display mode` | `Drawable size` | `metal` | `gl1-metal` | `ref_gl1` |
|---|---|---|---|---|---|---|---|
| `r_mode 4`, `vid_fullscreen 1` | `4: 640x480 (vid_fullscreen 1)` | `640x480@-1.000000`, `960x540@60.000000` | `960x540@60.00` | `640x480` | 640x480 | 640x480 | 640x480 |
| harness `world`, `r_mode -1` 640x480, `vid_fullscreen 1` | `-1: 640x480 (vid_fullscreen 1)` | `640x480@-1.000000`, `960x540@60.000000` | `960x540@60.00` | `640x480` | 23 frames, each 640x480 | 23, each 640x480 | 23, each 640x480 |
| `r_mode -1`, 1920x1080, `vid_fullscreen 1` | `-1: 1920x1080 (vid_fullscreen 1)` | `1920x1080@-1.000000`, `1920x1080@60.000000` | `1920x1080@60.00` | `1920x1080` | 1920x1080 | 1920x1080 | 1920x1080 |
| `r_mode -1`, 2560x1440, `vid_fullscreen 1` | `-1: 2560x1440 (vid_fullscreen 1)` | `2560x1440@-1.000000`, `2560x1440@60.000000` | `2560x1440@60.00` | `2560x1440` | 2560x1440 | 2560x1440 | 2560x1440 |
| `r_mode -1`, 5120x2880, `vid_fullscreen 1` | `-1: 5120x2880 (vid_fullscreen 1)` | `5120x2880@-1.000000`, `5120x2880@60.000000` | `5120x2880@60.00` | `5120x2880` | 5120x2880 | 5120x2880 | 5120x2880 |
| `r_mode 4`, windowed | `4: 640x480 (vid_fullscreen 0)` | none | `2560x1440@60.00` | `640x480` | 640x480 | 640x480 | 640x480 |
| `r_mode 4`, `vid_fullscreen 2` | `4: 640x480 (vid_fullscreen 2)` | none | `2560x1440@60.00` | `640x480` | 640x480 | 640x480 | 640x480 |

So the display really switched for every `vid_fullscreen 1` run, and 640x480 fell back to
960x540 as in TASK98 (`User requested 640x480@-1.000000, setting closest mode 960x540@60.000000`,
then `Real display mode: 960x540@60.00` and `Drawable size: 640x480`). No log has a `SDL
SetVideoMode failed`, `Couldn't switch` or `Reverting to windowed` line. The harness manifests
list every frame at 640x480 (`world-000000.tga metal 640x480`). One more `metal` harness capture,
the same line with `vid_fullscreen 0`, has 23 frames byte-identical to the fullscreen ones
(`cmp`; `framediff` score 0, bad 0): the fallback mode changes neither the size nor a byte of the
frame. With `scripts/pins/framediff.c` at 0.005, 0.001, the fullscreen `metal` capture against
the fullscreen `gl1-metal` one passes 23 of 23 (worst score 0.000002, bad at most 0.000016), and
`gl1-metal` against `ref_gl1` 23 of 23 (0.000010, 0.000042). Each of the eight `metal` logs holds
`Metal API Validation Enabled` and no other validation message and no failed command buffer, and
its teardown line reads `ref_metal: teardown: 0 live objects (created/destroyed: device 1/1, queue
1/1, view 1/1, texture 203/203, buffer 8/8, state 45/45), 0 unreleased, 0 unmatched` (`texture
232/232` in the two harness runs). 22 runs, 21:56 to 22:01 local, each status 0 with no kill;
before and after each the screen was unlocked (`ioreg -n Root -d1 | grep -c
CGSSessionScreenIsLocked` printed 0), no other `quake2` was running before each (`pgrep -x
quake2`); one-minute load 2.52 to 4.63.
After every fullscreen run, the last `vid_fullscreen 1` one (`gl1-metal` at 5120x2880) included,
the probe printed the desktop mode again (`points 2560x1440 pixels 5120x2880 refresh 60.00`), and
after the last run `system_profiler SPDisplaysDataType` reported `Resolution: 5120 x 2880 Retina`
as before the first; what the panel showed during the runs was not looked at, the runs are judged
from their files. FIX45's acceptance:

- With `vid_fullscreen 1` and 640x480 asked for, `screenshot` and the harness capture from
  `ref_metal` are 640x480 (TASK98's finding 1 had them at the fallback mode's size): **holds**.
- The `Drawable size` line matches the size actually rendered: **holds**. The line says 640x480,
  the frame read back from `ref_metal`'s colour target is 640x480, and it is the windowed frame
  byte for byte; in the other modes the line and the `metal` picture agree too.
- Windowed behaviour is unchanged: **holds**. `r_mode 4` windowed and `vid_fullscreen 2` write
  640x480 on all three renderers, as in TASK93's table; the client's `Real display mode` is the
  desktop's 2560x1440 here.

At `67e56ab` (after `f1e23000`, whose black clear runs only when the drawable is larger than the
frame) the fullscreen-window case was run once more under the validation layer: `MTL_DEBUG_LAYER=1
./quake2 -datadir <game data> -portable +set vid_renderer metal +set vid_fullscreen 2 +set r_mode
4 +set s_initsound 0 +exec <script>`, the script `developer 1`, `map base1`, 30 `wait`s,
`screenshot`, 10 `wait`s, `quit` (status 0, no kill needed). The client printed `Setting mode 4:
640x480 (vid_fullscreen 2)`, `Real display mode: 1333x888@60.00`, `Drawable size: 640x480`; the
screenshot's TGA header reads 640x480; the log holds `Metal API Validation Enabled` and no other
validation message and no failed command buffer, and ends `ref_metal: teardown: 0 live objects
(... texture 203/203, buffer 8/8, state 45/45), 0 unreleased, 0 unmatched`.

**vsync.** In the same run, `set r_vsync 0`, `1`, `0`, `1` between frames printed
`ref_metal: displaySyncEnabled off`, `on`, `off`, `on`: the property read back from the layer,
which is also what `IsVSyncActive` returns.

**Selection.** `+set vid_renderer gl3` with no `ref_gl3.dylib` beside the binary: `Library
ref_gl3.dylib cannot be found!`, `Retrying with metal...`, `Successfully loaded ref_metal.dylib`.
The preprocessed `vid.c` (the Makefile's compile line with `-E -P`) has `"gl3", "metal", "gl1",
"soft"`; the `r_order` block preprocessed without `__APPLE__` (and with `_WIN32`) has `"gl3",
"gl1", "soft"`. The menu's `fulllist` preprocessed the same way has `[Metal     ]` on macOS and
not without `__APPLE__`.

**The module mapping.** A scratch program that `dlopen`s a module, `dlclose`s it and asks
`dlopen(RTLD_NOLOAD)` again: a plain C dylib is unmapped, `ref_metal.dylib` and the shim's
`ref_gl1.dylib` stay mapped (dyld keeps an image with Objective-C metadata). `RM_KeepMapped`
adds the documented `RTLD_NODELETE` reference.

**Mutants.** Each of the five new functions of `rm_pure.c` (`rm_ledger_kind_live`,
`rm_ledger_clean`, `rm_frame_size`, `rm_frame_remake`, `rm_present_region`), every operand of
each compound guard on its own, each comparison moved by one, each result field: 51 mutants, all
killed by `tests/metal/rm_host_test.c` under ASAN and UBSAN. Three that made a parameter unused
were refused by `-Werror`; each was run again with the parameter cast to `void` and killed. The
one that let `RM_OBJ_KINDS` through `rm_ledger_kind_live` was killed only by UBSAN at first; the
case now reads a 1 through such an index, so it fails without a sanitizer too.
