# TASK132: `soft`'s window, opaque and not opaque

Status: measured. No layer difference is shown at `sw_partialrefresh 0`; at
`sw_partialrefresh 1` the same-state spread is too wide to say either way. Every capture was redone after the first set (which may have
overlapped use of the display) and the three cases below are each three runs per
layer state.

FIX52 (#239) made `soft`'s `CAMetalLayer` not opaque when display sync is off
and did not compare the picture on screen; its method is in
[FIX52's runbook file](FIX52.md), and FIX53's capture method is in
[FIX53's runbook file](FIX53.md). This file records the comparison FIX52 left
open. The per-capture settings are encoded only in the capture file names
(`v0p0` is `r_vsync 0` with `sw_partialrefresh 0`, `o0` is not opaque, `o1` is
opaque, the last number is the run); `settings.txt` in the artifacts folder
maps each file name to its settings.

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
(960x572 pixels with the title bar), `vid_highdpiaware 0`, `pins_seed 1`,
`s_initsound 0`, `-portable`. Screen unlocked, no other `quake2` running and the
timing lock held for each run, one run per lock hold (about 12 seconds); load
average (one minute) 2.8 to 4.6 over the runs (no timing is read from these
runs). Engine commit measured: `522cb7ae`, built with `make client ref_soft
game`, plus the scratch edits above (a scratch `ref_soft` relinked with the
capture file); the scratch edits are not committed. Captures are in the artifacts
folder, were never viewed, and are not committed. The comparison is pixel by
pixel over four channels: differing pixels and the largest channel difference.

## Results

Differing pixels of 549,120 (960x572), largest channel difference in brackets.
Each case is three runs per layer state, run alternating opaque and not opaque.
"Same" is two runs of one layer state (three pairs each), "layer" is opaque
against not opaque (all nine pairings, of which the three same-index ones are
listed first).

| Case | Same, opaque | Same, not opaque | Layer, same index | Layer, all nine |
|---|---|---|---|---|
| `r_vsync 0`, `sw_partialrefresh 0` | 1051, 1083, 1060 (33 to 38) | 962, 1098, 1126 (26 to 30) | 1007, 1016, 1148 | 1007 to 1148 (25 to 38) |
| `r_vsync 1`, `sw_partialrefresh 0` | 949, 806, 1067 (34 to 41) | 1139, 1030, 1040 (30 to 42) | 913, 1113, 893 | 815 to 1147 (31 to 42) |
| `r_vsync 0`, `sw_partialrefresh 1` | 121,371, 107,453, 106,560 (54 to 78) | 98,164, 84,855, 71,801 (52 to 67) | 90,280, 121,500, 106,556 | 28,136 to 121,500 (55 to 74) |

Negative control (`r_vsync 0`, `sw_partialrefresh 0`, texture drawn blended at
alpha 200): 511,875 pixels against the opaque run and 511,873 against the not
opaque one (largest difference 59, rows 29 to 571, the whole window below the
title bar); two control runs differ by 1,059 pixels, the same as the others.

Reading:

- No difference due to the layer is shown at `sw_partialrefresh 0`. With either
  `r_vsync`, opaque against not opaque differs by as many pixels as two runs of
  one state (about 0.2 percent: 1007 to 1148 at `r_vsync 0`, 815 to 1147 at
  `r_vsync 1`), and every one of those pixels lies in a box of about 110 by 100
  pixels at the left edge of the view (x 0 to 112, y 244 to 339) in all 30
  pairings (15 per case: 6 same-state and 9 layer), so outside that box the picture
  is identical across layer states and runs. The box is the same one in the
  control pair of runs, so it is a property of the demo frame, not of the layer.
  It was not traced further: the runs are not deterministic at that spot
  although the demo frame and `pins_seed` are the same.
- The first set (74 to 166 differing pixels per pair, taken while the display
  may have been in use) is superseded. Its counts are an order of magnitude
  below these; the reason is not known (the capture code and the scratch edits
  are the same as far as the notes show), and the conclusion is the same.
- With `sw_partialrefresh 1` the same-state spread is 71,801 to 121,371 pixels
  (the streaming texture is only partly rewritten each frame, so what is on
  screen depends on how many frames came before and on the timing of the
  presents), and the nine layer pairings, 28,136 to 121,500 (the largest
  difference 55 to 74), are spread over a range wider than that. The spread is
  too wide for the capture to separate a layer effect at `sw_partialrefresh 1`,
  so no layer difference is shown there, and none is ruled out; the capture
  also shows that with partial refresh the picture on screen is not the same
  from run to run at frame 300, which is FIX54's (#252) ground and not changed
  here.
- The control writes alpha below 1 into the frame and differs in 511,875
  pixels, so the capture can see a layer-alpha difference when there is one.
- SDL's Metal renderer draws the texture into the drawable with blending off by
  default, so the drawable's alpha is 1 wherever the texture covers it; the
  drawable's alpha was not read back directly.

## Not done

- The cause of the roughly 1,000 differing pixels between identical runs was
  not found.
- `sw_partialrefresh 1` with `r_vsync 1` was not run.
