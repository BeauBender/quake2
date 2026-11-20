# TASK199: HDR probe and tone-map follow-ups

Three follow-ups of the HD3 review (TASK196, "HD3, rework round 2" in `TASK196.md`).

## What changed

- `tests/check-ref-metal.sh`, new section (j), listed in the header. By source, on `rm_main.c` with
  comments and literals stripped: `R_ClearNoWorld();` is the first statement of `R_RenderViewDraw`;
  `R_ClearNoWorld` draws over `&r_view.vp`; `R_SetupGL` and `R_HdrToneMap` each build their rectangle
  with `rm_view_rect` from the refdef fields `x y width height`, in that order, and the whole stripped
  argument list of the two `rm_view_rect` calls is equal (one call each); `R_HdrToneMap`'s tone-map pass
  is `rmb_tonemap_pass((int)vp.x, (int)vp.y, (int)vp.w, (int)vp.h)`; `R_ClearNoWorld` begins with the
  `!(r_newrefdef.rdflags & RDF_NOWORLDMODEL)` guard that returns and has no other `return`, so it always
  reaches its draw. The probe cancel is held too: `RI_ShutdownContext` (`rm_sdl.c`) calls
  `RM_HdrProbeCancel();` before `rmb_detach();`, and `RM_HdrProbeCancel` calls
  `rm_hdr_probe_cancel(&rm_hdr_probe_wait)`. A function body starts at its definition, not at a forward
  declaration. Ten scratch mutants, each refused (below). The host test could not hold this: it does not link `rm_main.c`.
- `RI_ShutdownContext` (`rm_sdl.c`) calls `RM_HdrProbeCancel` (`rm_main.c`), which ends a pending
  `metal_hdr_probe` wait (`rm_hdr_probe_cancel`, `rm_pure.c`, so the host test can drive it) and
  prints `ref_metal: hdr: probe cancelled (the renderer was shut down)` when one was pending. A
  `vid_restart` keeps the module's statics, so before this the wait ran on and printed the false
  "no drawable was presented" line 60 frames later.
- The comment of `R_HdrToneMap` names `timerefresh`: up to 128 world views in one frame, so a pixel no
  surface covers is mapped once per view. A benchmark, not a frame to look at.

## The mutants

Each is the check's own scratch copy of `rm_main.c` or `rm_sdl.c` (`HDR_MUTANT_SHOW=1` prints these lines):

```
mutant noclear: R_RenderViewDraw does not start with R_ClearNoWorld(); (its first statement is "if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL))")
mutant othervp: R_ClearNoWorld does not draw over &r_view.vp
mutant fieldsw: R_HdrToneMap builds its rectangle from "x y width width", not "x y width height", of one refdef
mutant fieldss: R_SetupGL builds its rectangle from "x x width height", not "x y width height", of one refdef
mutant tmfull: R_HdrToneMap's tone-map pass is not over the (int)vp.x, vp.y, vp.w, vp.h of its rm_view_rect
mutant thtw: R_HdrToneMap's rm_view_rect arguments ("x,y,width,height,vid.width,vid.height,tw") differ from R_SetupGL's ("x,y,width,height,vid.width,vid.height,th")
mutant retclear: R_ClearNoWorld returns anywhere but in its RDF_NOWORLDMODEL guard, so it can skip its draw
mutant guardinv: R_ClearNoWorld does not begin with the !(r_newrefdef.rdflags & RDF_NOWORLDMODEL) guard that returns
mutant nocancel: RI_ShutdownContext does not call RM_HdrProbeCancel(); before rmb_detach();
mutant nobody: RM_HdrProbeCancel does not call rm_hdr_probe_cancel(&rm_hdr_probe_wait)
```

On the real tree the section prints nothing and the check's last line says
`tone-map order and probe cancel held, 10 order mutants refused`.

## The host test

`rm_hdr_probe_cancel` cases in `tests/metal/rm_host_test.c`. Red first (the helper did not exist):
`error: call to undeclared function 'rm_hdr_probe_cancel'` at `rm_host_test.c:8919`. Green after:
`rm_host_test: 13162 checks, 0 failed`.

## The restart, on the machine (screen locked, console frames)

`metal_hdr_probe 10 10; vid_restart`, 640 x 480, `r_hdr 1`, 25 frames of waits before, 150 after:

`release/quake2 -portable ... +r_hdr 1 +w25 +metal_hdr_probe 10 10 +vid_restart +w150 +metal_hdr_info +quit`
(`w25`, `w150`: aliases of that many `wait`s). The branch's log, grepped for `hdr` and `probe`:

```
ref_metal: hdr: probe cancelled (the renderer was shut down)
ref_metal: hdr: layer RGBA16Float, colour space kCGColorSpaceExtendedLinearSRGB ...
ref_metal: hdr: peak 1.00 in force (r_hdr_peak 4.00, headroom unavailable)
```

No "no drawable was presented" line, 0 matches in 150 frames after the restart. The start point's log
also had none in this run (the restart's own `rmb_encode_release` cancelled the probe's ask, and the
screen was locked), so the machine run does not show the old false line; the host test is the red
for the reset (`rm_hdr_probe_cancel` did not exist), and this run shows the new line and no timeout.

## No change in what is drawn

Frame pins of ref_metal (`scripts/pins.sh record metal <scenario>`, `PINS_SEED=1`, demo1.dm2),
the start point (origin/main, from a `git archive` extraction) and this branch, each built with the
game module first. One hash per scenario: the sha256 of the sorted sha256 list of its frames' files.

| Scenario | Frames | start point | head |
|---|---|---|---|
| world | 23 | fccafdc3d5c42a9fb97b3b5c711a84bedf36000e0a22a216e2928b6a6f0217f1 | fccafdc3d5c42a9fb97b3b5c711a84bedf36000e0a22a216e2928b6a6f0217f1 |
| models | 23 | 6777c323114b7834b6af21bde9c3af440856a0f0395969379680c45afb79ce57 | 6777c323114b7834b6af21bde9c3af440856a0f0395969379680c45afb79ce57 |
| hdr | 23 | 366d393428e5b9aeb99ec4b39dc7852a0ef366921b2c2a8a4eb41a53da0dd051 | 366d393428e5b9aeb99ec4b39dc7852a0ef366921b2c2a8a4eb41a53da0dd051 |

## Checks

Each at the branch's code head, in the holds, last line:

- `tests/check-ref-metal.sh`: `check-ref-metal: ok (13162 checks, 0 failed; ...; tone-map order and probe cancel held, 10 order mutants refused; ...)`
- `make test`: `yq2test: 444 tests, 0 failed`
- `tests/check-doc-links.sh`: `check-doc-links: ok`; `tests/check-runbook-index.sh`: `check-runbook-index: ok`
- `check-branch.sh` against origin/main: `check-branch: ok (refs/remotes/origin/main..HEAD: no private repository, host name or home path)`
