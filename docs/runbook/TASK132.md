# TASK132: `soft`'s window, opaque and not opaque

Status: partly measured. The `r_vsync 0` comparison and its negative control are
done; the `sw_partialrefresh 1` pair and the `r_vsync 1` pair are not yet run
(see "Remaining").

FIX52 (#239) made `soft`'s `CAMetalLayer` not opaque when display sync is off
and did not compare the picture on screen; its method is in the section "The
60 fps cap at 1920x1080 windowed (FIX52)" of
[the shared runbook](../plans/metal-port-harness.md), and FIX53's capture
method is in its section "The opaque layer of `ref_metal` and the shim (FIX53)".
This file records the comparison FIX52 left open.

## Method

The capture is FIX53's: inside the engine process,
`+[SCShareableContent getCurrentProcessShareableContentWithCompletionHandler:]`
and `SCScreenshotManager` return the engine's own window, composited, with no
Screen Recording permission. The capture code was an Objective-C file of its
own, linked only into a scratch `ref_soft` (never committed). Scratch edits to
`src/client/refresh/soft/sw_main.c`, reverted before this commit: an
environment override of the layer's `opaque` after the existing
`RE_SetMetalLayerOpaque` call, a capture hook after `SDL_RenderPresent` in
`RE_FlushFrame`, and, for the control, `SDL_BLENDMODE_BLEND` with texture alpha
mod 200 on the streaming texture. The capture is taken after frame 300 of
`timedemo demo1.dm2`, half a second after the present, in a 960x540 window
(960x572 pixels with the title bar), `r_vsync 0`, `vid_highdpiaware 0`,
`pins_seed 1`, `s_initsound 0`, `-portable`. Screen unlocked, no other
`quake2` running and the timing lock held for each run; load average (one
minute) 6.07 before the first run and 6.23 after it, so the host was busy with
builds (no timing is read from these runs). Captures are in the artifacts
folder, were never viewed, and are not committed. The comparison is pixel by
pixel over four channels: differing pixels and the largest channel difference.

## Result, `r_vsync 0`, `sw_partialrefresh 0`

| Comparison | Differing pixels of 549,120 | Largest channel difference |
|---|---|---|
| opaque against opaque (two runs) | 166 | 122 |
| not opaque against not opaque (two runs) | 74 | 96 |
| opaque against not opaque | 188 | 126 |
| opaque (second run) against not opaque (second run) | 73 | 108 |
| negative control: not opaque, texture drawn blended at alpha 200, against opaque | 518,931 | 131 |
| the same control against not opaque | 518,931 | 126 |

Two runs of the same configuration already differ by 74 to 166 pixels (about
0.03 percent, with large channel differences: the runs are not deterministic at
the pixel, not traced here), and opaque against not opaque differs by 73 to 188
pixels, the same count. So no difference due to the layer is seen: the
difference between layer states is within the run-to-run difference, and the
control, which writes alpha below 1 into the frame, differs in 518,931 pixels,
so the capture can see a layer-alpha difference. This was one pair of each
kind; the 73 to 188 pixels were not shown to be a fixed region, so a
difference of a few pixels cannot be excluded.

SDL's Metal renderer draws the texture into the drawable with blending off by
default, so the drawable's alpha is 1 wherever the texture covers it; the
control shows the capture does react when that is not so. The drawable's alpha
was not read back directly.

## Remaining (engine runs, not yet run)

- `sw_partialrefresh 1`, opaque and not opaque, two runs each (FIX54, #252, is
  that case; no result bears on it yet).
- `r_vsync 1`, opaque against not opaque.
- A repeat of the `r_vsync 0` pairs to bound the run-to-run difference.
