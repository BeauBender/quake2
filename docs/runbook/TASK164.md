# TASK164: the ref_metal host tests check the kernels' own calls, and the colour-space path has a probe

Two gaps found by reviews, both closed in `tests/check-ref-metal.sh`:

- FIX81: `tests/metal/rm_rtwater_test.c` drives C copies of the reflection loops (`rtw_trace_ss`,
  `rtw_trace_ws`) through the shared helpers of `common/mtl_rtw_math.h`. A revert of
  `rm_rtwater.metal` alone left the host test green; only the engine's masked comparison saw it.
- TASK121: `rmb_cs_apply` and `rmb_cs_release` in `rmb_device.m` (the `metal_colorspace` path) had no
  host harness, and the 1 -> 0 bug of that review lived there.

## Check (e): each water kernel calls the helpers its host test drives

Part (e) of the script, on every OS, no Metal needed. The comments and string literals of the
kernel file are stripped (the script's `strip`), the kernel's body is the text from its name at the
start of a line up to the next `}` in column 0, and the check finds a call `helper(` inside that body
and nowhere else. The pairs, which are the real names on main (the card's `mtl_rtw_ss_next` and
`mtl_rtw_ws_next` exist as named):

| kernel | helpers it must call |
| --- | --- |
| `rm_rtw_trace` | `mtl_rtw_ss_next`, `mtl_rtw_choose` |
| `rm_rtw_trace_ws` | `mtl_rtw_ws_next`, `mtl_rtw_choose_ws` |

These are the four helpers `rtw_trace_ss` and `rtw_trace_ws` call in the host test. The shadow and AO
host tests (`rm_rtshadow_test.c`, `rm_rtao_test.c`) call their helpers one at a time and carry no
copy of a kernel loop, so they have no pair; none was added.

When a call is gone the check fails, naming both:

```
check-ref-metal: src/client/refresh/metal/rm_rtwater.metal: the kernel rm_rtw_trace does not call mtl_rtw_ss_next (the helper its host test drives)
```

(That line is the real output with the two `mtl_rtw_ss_next(` calls of `rm_rtw_trace` renamed in the
tree, run before the check was wired to anything else: red first.)

Mutants, run by the script on every pair, four each (16 in all), all refused: the call inside the
kernel renamed; the call commented out; the call moved out of the kernel into another function
(the file's end); the kernel renamed. A mutant that changes nothing also fails the check.

## Check (f): the colour-space probe

`tests/metal/rmb_cs_probe.m`, built and run by the script on Darwin only. It `#include`s
`rmb_device.m` whole (`rmb_cs_apply` and `rmb_cs_release` are static, so the unit is the smallest
seam that needs no change to `src/`), and defines the three symbols that file calls elsewhere as empty
stubs (`rmb_encode_abandon`, `rmb_encode_release`, `rmb_resources_close`) plus `Com_Printf` for
`shared.c`; `rm_pure.c` supplies the ledger. On a standalone `CAMetalLayer` (no window, no game data)
it runs the renderer's own `rmb_attach`, `rmb_colorspace_set` and `rmb_detach`:

- 0, attach: the layer is as attach leaves it; a tagged layer (Display P3) is left alone; no state
  object in the ledger;
- 1: the layer's colour space is sRGB; one state object; asked again, still one;
- 1 -> 0: the colour space is the one at attach (equal, or none in both), no state object;
- 1, then detach: the layer is as found, no state object;
- 1 before attach: attach applies it; back to 0 and detach leave the layer as found.

It runs on a layer with its default colour space and on one tagged Display P3. With no Metal device it
prints `rmb_cs_probe: skipped: no Metal device` and passes; the script then says
`colour-space probe skipped: no Metal device`, and on another OS `colour-space probe skipped: not macOS`.

Output with a device: `rmb_cs_probe: 30 checks, 0 failed`; in the script's line,
`colour space 30 checks, 0 failed; 3 colour-space mutants refused`.

Mutants of `rmb_device.m`, each built into the probe from a scratch copy and each refused:

| mutant | result |
| --- | --- |
| 1 -> 0 sets the layer's colour space to NULL | `rmb_cs_probe.m:88`, `:94`, `:101`, `:103` FAILED `same_as(layer, at_attach)`, in both layers; `30 checks, 8 failed` |
| 1 -> 0 does not put the colour space back | refused |
| detach does not call `rmb_cs_release` | refused |

## The check against TASK162

Run on the kernel file of TASK162's head (`8d17698f`, `git show` of its `rm_rtwater.metal`, put in place
of this tree's copy for one run, restored after; nothing of that lane was changed). Check (e) passes
there: both kernels still call all four helpers. The script as a whole failed on that run in the
`make ref_metal` part, because that kernel needs `common/mtl_rtw_math.h`, `header/rm_rtwater.h` and
`rmb_rtwater.m` of the same head (19 files differ from main), which were not merged in; that failure
is that of an unmerged half of a lane and says nothing about (e).

## What is still not covered

- The check sees a call, not that the call is right: an argument changed, or the call made on a path
  that never runs, passes. The engine's masked comparison and the pins remain the guard of the kernel's
  values.
- A call inside `#if 0` or in dead code of the kernel body is counted.
- Only the water pairs exist; the other kernels (shadows, AO, G-buffer resolve) are not tied to their
  helpers by this check.
- The probe covers the layer's colour-space state and the ledger, not what the display shows with
  sRGB on (the pins and the screen do), and not the other state `rmb_attach` sets on the layer.
- The probe includes the whole `rmb_device.m`, so a change to the unresolved symbols of that file
  (a new call into `rmb_encode.m` or `rmb_resource.m`) makes it fail to link and needs a stub added.
- Not run: the full `scripts/test.sh` (the brief), and no engine run was needed.
- `tests/check-runbook-index.sh` fails until the orchestrator adds this runbook's index line.

## FIX99: review notes on the kernel-call check and the probe

All in `tests/check-ref-metal.sh` and `tests/metal/rmb_cs_probe.m`; nothing under `src/` changed.

1. **Dead code does not count.** `kcalls` now reads the stripped text through `deadif`, which blanks the
   dead branches of the preprocessor, nested ones included: the part of an `#if 0` before its `#else`,
   and the `#else` part of an `#if 1`. The `#else` part of an `#if 0` stays live (a positive check per
   pair), and an `#elif` is taken as live, as its condition is not evaluated. Red first: with the two
   new mutants added and the filter not yet in, the check printed, for every pair,
   `the kernel-call check passes a mutant of rm_rtw_trace (the call to mtl_rtw_ss_next deadcode)`.
   Mutants: each call of the body wrapped in `#if 0 ... #else #endif` (`deadcode`), and in
   `#if 0 / #ifdef X ... #endif / #endif` (`deadnested`); both refused now, with the 16 earlier ones.
2. **A missing kernel has its own message.** `kcalls` and `flushed` return 2 when the function is not
   found (1 when found without the call), and the check prints
   `kernel rm_rtw_nosuch not found in src/client/refresh/metal/rm_rtwater.metal` (the real output, with
   a pair name that does not exist). Two mutants must give status 2, not 1: `renamedkernel` and
   `joined` (`kernel void` and the kernel's name on one line). Check (d) reads
   `function <name> not found in <file>` the same way.
3. **The later-hit choice is paired.** The pair `rm_rtw_ss_at` / `mtl_rtw_choose` is added (5 pairs,
   7 mutants each and one live-`#else` check, 35 mutants refused). The renamed-call mutant of
   `rm_rtw_ss_at` is refused like the others.
4. **The probe's build flags and an over-release.** The probe is built with `-fno-strict-aliasing
   -fwrapv` and the script's ASAN and UBSAN when the compiler has them. A third run uses a colour space
   the probe creates itself (`CGColorSpaceCreateCalibratedRGB`), and checks `CFGetRetainCount` equals
   the count at attach after each cycle: set and unset, set and detach, attach with it on and unset,
   detach. A named colour space (Display P3, sRGB) is cached by the system, so its count says nothing
   and the probe does not count it; the layer's getter autoreleases, so each read sits in its own
   autorelease pool. The 4th mutant drops `CGColorSpaceRetain(rmb_cs_own)`: it fails
   `rmb_cs_probe.m:116` and `:123` (`!counted || count_of(initial) == base`), and then crashes
   in ASAN at the second release, so it is refused either way. The probe is now 58 checks, 4 colour-space
   mutants refused.
5. **Warnings.** The probe keeps `-Wall -Wextra -Werror`, which is stricter than `rmb_device.m`'s real
   build (`-Wall`, `-Werror` only under `WERR`), on purpose: the probe includes that file whole, and
   the stricter flags find an unused parameter or a sign comparison there before the real build does. A
   finding is the probe's "does not compile". The probe header now says it runs on any Darwin.

### FIX99, rework round 1: the `#if` forms, the nested mutant, the probe's crash

1. **`#if(` opens a level.** `deadif` pushed a level only for `#if` followed by a blank, so the `#endif`
   of an `#if(X)` closed the level around it. It now pushes for any `#if` followed by a character that
   cannot continue an identifier, and keeps the `ifdef` and `ifndef` alternatives. The forms it
   evaluates are `#if 0`, `#if 1`, `#if (0)` and `#if (1)`, with optional blanks (`#if(0)` too). Every
   other condition (`#if X`, `#if defined(X)`, `#ifdef`, `#ifndef`, `#elif`, `#if 00`, `#if false`, a
   directive continued by a backslash) is taken as live on both branches: a dead call kept there is
   counted, and a live call is never refused. Red first, with the old `deadif` and the new mutants
   `deadnestedparen` (`#if 0` / `#if(X)` / `#endif` / call / `#endif`), `deadparen` (`#if (0)`) and
   `deadparen2` (`#if(1)` / `#else` / call / `#endif`), for every pair:
   `the kernel-call check passes a mutant of rm_rtw_trace (the call to mtl_rtw_ss_next deadparen)`
   (the same for `deadparen2` and `deadnestedparen`; 15 lines, 5 pairs at the time). Now 60 kernel-call
   mutants are refused (6 pairs, 10 each).
2. **The nested mutant tests nesting.** `deadnested` now puts the call after the inner block:
   `#if 0` / `#ifdef X` / `#endif` / call / `#endif`. Against a scratch copy of `deadif` that pushes a
   level only for `#if 0` and `#if 1` (so the `#endif` of `#ifdef X` closes the `#if 0`), the check
   prints, for every pair, `the kernel-call check passes a mutant of rm_rtw_trace (the call to
   mtl_rtw_ss_next deadnested)` (and `deadnestedparen`, `deadparen`, `deadparen2`): 20 lines, and
   `pairs=5 mutants=30 fail=1`. Against the real `deadif`, `fail=0`.
3. **The probe no longer crashes in the `unretained` mutant.** The retain-count checks stop the probe
   at the first failure (`_exit(1)` after one message), before any further release or retain, and the
   check runs it with `ASAN_OPTIONS=abort_on_error=0`; the subshell and its comment are gone. The mutant
   now ends with `rmb_cs_probe: stopped at the first wrong retain count, 48 checks` and status 1; with
   the previous probe the shell printed `Abort trap: 6` (SIGABRT in ASAN's SEGV report at
   `rmb_cs_release`). "Abort trap" no longer appears in the check's output.
4. **Check (d)'s status 2 is tested** (the `renamed` flush mutant must give 2, the others 1): with
   `exit !found ? 2` reverted to 1 the check prints `the flush-point check gives status 1, not 2, for a
   mutant of RI_RenderFrame (renamed)` (and `RI_EndFrame`, `R_ScreenShot`).
5. **The pair `rm_rtw_trace` / `rm_rtw_ss_at`** is added: the loop must still call the function that
   finds the later hit. Six pairs now.
