# The `world` scenario against the software renderer (TASK49)

Three `gl1-metal` records of `world` (commit `6b6a2910`, the S3 shim;
identical to each other, and to a record of the ASAN and UBSAN build of
`0d68aad9`) against the `gl1` reference `110a25ae2d26-2420249005` at the
defaults: in each record 0 frames exact, 7 passing (9, 12, 13, 17 to 20)
and 16 over, worst score 0.025886 and bad 0.632692 (frame 8). Load
average (1 minute) 5.5 to 43.0 over the records and compares. No
threshold or scenario was changed; the decision is the owner's.

`gl1_multitexture` was set explicitly in scratch copies of `world` (not
committed): with 1, the frames of both renderers are byte-identical to
`world`'s (the default is 1); with 0 the classic two-pass path runs.
Each copy was also recorded with `gl_polyblend 0`. `gl1` was recorded
twice per copy and `gl1-metal` three times; `gl1-metal` is exact run to
run in every copy, `gl1` too except frame 0 of the `gl1_multitexture 0`
copy with the flash (score 0.004627, bad 0.027529 between its two
records). Three faults of the reference make the difference, none of
them in the world path:

- **The screen flash** (`R_PolyBlend`), on frames 3, 5, 8, 11 and 16:
  against the same scenario with `gl_polyblend 0` it changes 1.6 to 6.2
  percent of the pixels of `gl1` (the wedge) and 39 to 98 percent of
  `gl1-metal`'s (the whole screen; frame 3 is a faint blend). With
  `gl_polyblend 0`, frames 11 and 16 pass; 3, 5 and 8 stay over for the
  next reason.
- **Polygons that cross the screen edge**: the software renderer drops a
  sliver of them (the clear colour shows: a black triangle at the bottom
  of frame 22, a black band across the floor of frame 0) or draws it
  with wrong texture coordinates (a seam and a brighter wedge of floor
  at the left of frame 3, a triangle of garbled wall in frame 10).
  `soft` draws those places as `gl1-metal` does. Splitting each frame's
  bad pixels (`gl_polyblend 0`) into 8-connected regions of 150 pixels
  or more and scattered ones: the regions are 0.005498 to 0.094684 of
  the frame on frames 0, 3 to 8, 10, 21 and 22, whose scattered rest is
  0.000309 to 0.004268 (under the default); on frames 1, 2, 14 and 15
  the scattered part alone is 0.008066 to 0.035690, on the floor right
  in front of the camera (a smooth difference field of both signs,
  mean 2 to 3 of 255 per channel on frame 1, with no period along x
  and not reduced by a one-pixel shift; those floors cross the bottom
  edge too, so the same clipping is the likely cause, not proven).
- **The classic path stripes**: with `gl1_multitexture 0` the software
  renderer's lightmap pass fails its `GL_LEQUAL` depth test along
  streaks, so the wall shows unlit; its own two paths differ by a bad
  fraction of 0.067484 to 0.294687 on every frame. `gl1-metal`'s two
  paths agree within rounding (bad 0 on 21 frames, at most 0.000029).
  `gl1-metal` against the `gl1` record of that copy: all 23 frames over
  in every pair, score up to 0.071019, bad 0.066068 to 0.696745 (0.066068
  to 0.370527 without the flash).

So `compare gl1-metal world --ref gl1` cannot pass on every frame at the
defaults against these references for any renderer without those
faults. `gl_polyblend 0` in `world`, as in `hud2d`, removes only the
first (two frames). Judging the rest needs a reference from an
accelerated OpenGL (INFORM9, T1), or a recorded exception per frame.

TASK83 (#187) is the card that judges `world` again, against references
from an accelerated OpenGL; it did, and `world` passes on every frame at
the defaults with `gl1_multitexture` 1 and 0 ("`gl1` references from an
accelerated OpenGL (TASK83)"). Polygon offset and dynamic-light sub-uploads
are host-tested only, not judged on frames (`world` has `gl_zfix 0` and
`cl_lights 0`).

The review checked this independently. A three-way comparison with
`soft` on the 15 frames still over without the flash found 56 differing
regions of 150 pixels or more (108,719 pixels), with `gl1` the odd one
out in all 56 and `gl1-metal` alone in none. On passing frames the
luminance ratio of `gl1-metal` to `gl1` is 0.9996 to 1.0015; with
overbright forced to 2 and 4 both renderers brighten alike.
