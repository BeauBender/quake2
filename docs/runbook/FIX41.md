# `vid_restart` and the main thread's autorelease pool (FIX41)

"Closing TASK21 on the build host" found every window kept across
`vid_restart`. Two things keep it, one ours and one AppKit's.

**Cause 1, ours: no autorelease pool on the main thread.** The engine's
main loop (`Qcommon_Mainloop`, `src/common/frame.c`) never ran inside a
pool on macOS, and nothing else drains one: the client has no Cocoa run
loop of its own. Run with `OBJC_DEBUG_MISSING_POOLS=YES`, two `soft`
restarts log 977 `autoreleased with no pool in place - just leaking`
lines; the 3 on the main thread are the 3 windows' `SDL3_cocoametalview`.
The other 974 come from 6 or 7 threads the engine does not start
(their objects are Metal, frame-pacing and collection ones) and stay after the fix (976 in the same
run, none on the main thread). `leaks --autoreleasePools`
on the memory graph at exit lists the main thread's pool page with no
pool boundary, holding one `SDL3_cocoametalview` per window. The view is
the one `SDL_Renderer`'s Metal backend makes for `soft`; `gl1` (shim) and
`metal` destroy their own views (`SDL_Metal_DestroyView`), so for them
alone no layer is kept, and in a `gl1`/`soft` mix the kept layers are
the `soft` windows'.

**Fix.** On Darwin the client's C files are compiled as Objective-C
(Makefile, `-x objective-c`), so `frame.c` uses `@autoreleasepool`
itself, under `#if defined(__APPLE__) && defined(__OBJC__)`: one around
each `Qcommon_Frame`, one around start-up (the body of `Qcommon_Init`,
now `Qcommon_Startup`), which is drained before the first frame. That
is the public construct; `objc_autoreleasePoolPush`/`Pop` are not
declared by the SDK's headers, and a separate `.m` file would need its
own Makefile rule. `q2ded`, the unit tests and the other platforms
compile `frame.c` as C and get the code they had. An `ERR_DROP` longjmp
lands inside `Qcommon_Frame` (its `setjmp`), inside the pool.
`tests/check-autorelease-pool.sh` decides from the objects: built with
the Makefile's rules, the client's `frame.o` has two branches to
`objc_autoreleasePoolPush` and two to `Pop`, `q2ded`'s none; each of
three mutants (frame pool removed: 1 and 1; start-up pool removed: 1
and 1; guard never true: 0 and 0) is refused. It skips off macOS.

**Cause 2, AppKit's: the close animation.** The remaining window object
at exit is the `NSCGSWindow` (the window server's side; the
`SDL3Window` itself is freed). `leaks --traceTree` on it shows it held
by `_NSWindowTransformAnimation._frozenWindows`, the order-out
animation `[NSWindow close]` (in SDL's `Cocoa_DestroyWindow`) starts,
which is held by its display-link block. None of these animations ends
in the run, though the first window has seconds of frames after its
close. A pool does not release it. A scratch build that also set
`animationBehavior` to `NSWindowAnimationBehaviorNone` on each new
window (through `SDL_PROP_WINDOW_COCOA_WINDOW_POINTER`, not committed)
kept no window: `soft`, 1 and 8 restarts, 0 `NSCGSWindow`, 1 metal view
(the start-up one, that build had no start-up pool), footprint 66.1 and
66.0 MB.

**Fix for cause 2.** `CreateSDLWindow` (`src/client/vid/glimp_sdl3.c`)
fetches each new window's `NSWindow` through SDL's documented
`SDL_PROP_WINDOW_COCOA_WINDOW_POINTER` and sets its `animationBehavior`
to `NSWindowAnimationBehaviorNone` (a NULL pointer is skipped), under
the same guard as `frame.c`; SDL is not patched, `q2ded` and the other
platforms are unchanged. The only visible change is a window that
closes without animation (an opening animation, where AppKit plays one,
goes too). Why the close animation
never completes on this host (display link, paravirtual display) is not
proven, and the behaviour on a physical Mac is unmeasured.
`tests/check-autorelease-pool.sh` also builds the client's
`glimp_sdl3.o` and requires a branch to the selector stub
`_objc_msgSend$setAnimationBehavior:`; the mutants "call removed" and
"guard never true" are refused.

**Procedure.** Needs the game data and a window server, so it is a
runbook step and not a gate check. A console script in the build's
`baseq2/` (not committed) runs, after 30 `wait`s, N cycles of `set
vid_renderer R; vid_restart` and 30 `wait`s for each listed renderer,
then `quit`. The engine runs from the build directory as `./quake2
-portable +set basedir <data> +set s_initsound 0 +set r_mode -1 +set
r_customwidth 640 +set r_customheight 480 +set vid_fullscreen 0 +set
vid_renderer <first> +exec <script>` under `MallocStackLogging=1 leaks
--outputGraph=<file> --atExit --`; then `heap <file>` gives the class
counts and footprint, and `leaks --autoreleasePools <file>` the pool
entries. Configurations: `soft`, `gl1` (shim), `metal` (one restart a
cycle), mix (`soft` and `gl1`, starting at `gl1`) and mix3 (`soft`,
`gl1`, `metal`, starting at `metal`). Release builds, before (commit
`4175e8cc`) and after the fix, two runs of each, load averages 8.6 to
25.6:

| Configuration | Before, 1 cycle | Before, 8 cycles | After, 1 cycle | After, 8 cycles |
|---|---|---|---|---|
| `soft`: windows, layers | 2, 2 | 9, 9 | 2, 0 | 9, 0 |
| `soft`: footprint (MB) | 65.4 to 66.6 | 100.3 to 100.8 | 61.0 to 61.5 | 59.8 to 63.1 |
| `gl1`: windows, layers | 2, 0 | 9, 0 | 2, 0 | 9, 0 |
| `gl1`: footprint (MB) | 61.8 to 62.0 | 58.6 to 63.4 | 62.0 to 62.2 | 63.0 to 63.2 |
| `metal`: windows, layers | 2, 0 | 9, 0 | 2, 0 | 9, 0 |
| `metal`: footprint (MB) | 58.1 | 59.3 to 59.4 | 58.2 to 58.3 | 59.1 to 59.5 |
| mix: windows, layers | 3, 1 | 17, 8 | 3, 0 | 17, 0 |
| mix: footprint (MB) | 68.1 to 68.4 | 103.6 to 103.9 | 58.4 to 63.2 | 61.5 to 66.2 |
| mix3: windows, layers | 4, 1 | 25, 8 | 4, 0 | 25, 0 |
| mix3: footprint (MB) | 63.3 to 68.1 | 102.3 to 107.0 | 63.0 to 63.5 | 69.4 to 69.5 |

Windows are `NSCGSWindow`, layers `CAMetalLayer` (with each the same
number of `SDL3_cocoametalview`), at exit, after the engine destroyed
its last window. After the fix the layer count is 0 after 1 and after 8
cycles in every configuration, and the main thread's pool holds 0 or 1
entry at exit (8 to 10 before, in the mixes). Footprint growth per cycle
from 1 to 8 cycles, before: `soft` 4.8 to 5.1 MB, mix 5.0 to 5.1, mix3
4.9 to 6.2; after: `soft` -0.2 to 0.2, mix 0.4, mix3 0.9 (two and three restarts a
cycle), `gl1` and `metal` -0.5 to 0.2 either way (runs at this load spread
by up to 5 MB, so these are bounds, not costs). The live heap still grows
by about 340 to 360 nodes a restart (490 before, `soft`); the scratch
build without the close animation grew by 110 a restart, so most of the
rest is cause 2; with the pool alone the window count after 8 cycles
still equals the windows opened.

With the close animation turned off as well (fix for cause 2), same
procedure, release builds, two runs of each, load averages 13 to 15:

| Configuration | 1 cycle | 8 cycles |
|---|---|---|
| `soft`: windows, layers | 0, 0 | 0, 0 |
| `soft`: footprint (MB) | 60.5 | 57.2 to 61.6 |
| `metal`: windows, layers | 0, 0 | 0, 0 |
| `metal`: footprint (MB) | 57.4 to 57.8 | 57.8 to 58.0 |
| mix3: windows, layers | 0, 0 | 0, 0 |
| mix3: footprint (MB) | 62.5 to 62.9 | 65.2 to 65.3 |

No old window, layer or metal view is left at exit, after 1 or after 8
cycles, so the counts after 8 cycles equal those after 1. The live heap
grows by about 110 to 130 nodes a restart (`soft` 127, `metal` 130,
mix3 107); footprint per cycle from 1 to 8 cycles: `soft` -0.5 to 0.2 MB,
`metal` 0.0 to 0.1, mix3 0.3 to 0.4 (three restarts a cycle).

Under the `ASAN=1` build with `MTL_DEBUG_LAYER=1` and
`OBJC_DEBUG_MISSING_POOLS=YES`, four mix3 cycles: status 0, no sanitizer
line, no validation message, five `ref_metal` teardowns each `0 live
objects ... 0 unreleased, 0 unmatched`, and no missing-pool line on the
main thread. The same run with the close animation off: status 0, no
sanitizer line, no validation message beyond `Metal API Validation
Enabled`, five clean `ref_metal` teardowns, and the missing-pool lines
(2907) all from six threads other than the main thread.
