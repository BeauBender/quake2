# The screen flash and `hud2d` (TASK82)

On frames 5, 8, 11 and 16 of `hud2d` the player picks up an item and gl1
draws the screen blend (`R_PolyBlend`, a quad 10 units in front of the eye
through `glFrustum` and `glRotatef`). `cl_blend 0` does not stop it: it
clears the blend's colour, not its alpha, so the screen darkens. Apple's
software renderer draws that quad as a diagonal wedge over 6.2 percent of
the screen; `soft` and a correct GL darken the whole screen. Measured
before the decision, with the TASK82 shim (which implements those calls)
against the `gl1` reference `110a25ae2d26-2096632597` of the old scenario
(load 37.6): frames 5, 8, 11 and 16 over at score 0.054615 to 0.110522 and
bad 0.919017 to 0.928447, frame 3 (a fainter blend) at score 0.009718 and
bad 0; the rest exact. Judging those frames against `soft` instead is no
way out: there the clear colour differs on every pixel (gl1-metal against
`soft`, frames 3, 5, 8, 11, 16: score 0.351816 to 0.453720, bad 0.997259
to 0.998561). No threshold was widened.

Decision: `hud2d` sets `gl_polyblend 0`, which keeps the quad out of the GL
renderers (`soft` has no such cvar and still darkens, and stays exact run
to run), and the `gl1` and `soft` references of `hud2d` were recorded again
(the row in the table above). With it, three `gl1-metal` records of commit
`74205ac9` match all four new `gl1` records exactly (score 0, bad 0, all 23
frames, 12 pairs; record and compare load 22.7 to 64.7; three more of
commit `20014fb9`, the final shim code, gave the same at load 21.5 to
37.6), and `gl1`/`gl1`
and `soft`/`soft` stay exact. The new `gl1` frames differ from the old
reference only on frames 3, 5, 8, 11 and 16, by the wedge (score up to
0.007440, bad 0.061862).

The flash is judged in a 3D scenario from S3 on: `particles` keeps
`cl_blend 1` and the default `gl_polyblend 1`. The software renderer's
wedge will appear in those `gl1` references too, so a correct `gl1-metal`
will differ from them on the flash frames for the same reason; the card
that accepts such a scenario records the frames and their numbers, and an
accelerated `gl1` reference (INFORM9) or a recorded exception decides them,
not a wider threshold.
