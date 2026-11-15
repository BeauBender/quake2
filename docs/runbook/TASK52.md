# Particles against the software renderer (TASK52)

Which path `particles` exercises: `R_DrawParticles` with `GL_POINTS`
(`gl1_pointparameters` defaults to 1 and the engine log says `Point
parameters: Okay`), smooth points (`gl1_particle_square 0`). Two
scratch copies of the scenario (not committed) cover the rest:
`gl1_pointparameters 0` (`R_DrawParticles2`, textured triangles; log
`Point parameters: Disabled`) and `gl1_particle_square 1` (square
points). A scratch build that logged each draw by kind (not committed)
found points on captured frames 2 to 21 (6 to 852 a frame; 13 and 15
draw 13 and 6), the screen blend on frames 3, 5, 8, 11 and 16, and no
beam, sprite or line on any captured frame: `demo1.dm2` has none in
view, so beams and sprites are judged by the host test and the earlier
cards' code, not on frames.

Records of commit `0af48dd7` (the shim of this card; its gl1 is the
parent's): three `gl1`, three `soft` and three `gl1-metal` records of
each of the three scenarios, every log without a `not implemented`
line (`mgl: 707 frames ... 0 skipped`). 1-minute load 5.6 to 11.7 at
the records, 6.7 to 8.1 at the compares. All compares at the
`particles` values (`--threshold 0.02 --bad-threshold 0.25`), counts
recomputed from the per-frame lines:

| Scenario | `gl1-metal` run to run (3 pairs) | `gl1` run to run | Against `gl1` (9 pairs) |
|---|---|---|---|
| `particles` | 21 exact, 48 passing, 0 over; worst score 0.011813, bad 0.155348 | 14, 55, 0; worst 0.013428, 0.174443 | 0 exact, 162 passing, 45 over |
| `gl1_pointparameters 0` | 21, 48, 0; worst 0.009918, 0.184743 | 15, 54, 0; worst 0.009547, 0.168740 | 0, 162, 45 |
| `gl1_particle_square 1` | 21, 48, 0; worst 0.011276, 0.189528 | 15, 54, 0; worst 0.011487, 0.145524 | 0, 162, 45 |

`soft` run to run: worst 0.012270 and 0.180264 (`particles`). So the
shim's noise is the scenario's, inside the 0.25 the scenario carries.
Against `gl1` every one of the 27 pairs has the same 5 frames over,
5, 8, 11, 13 and 16, with nearly the same numbers in each (`particles`:
frame 5 score 0.131861 to 0.133294, bad 0.918815 to 0.918942; 8: 0.070971
to 0.079575, 0.929056 to 0.932077; 11: 0.038291 to 0.038393, 0.937790
to 0.938587; 13: 0.024436, 0.325430; 16: 0.069967 to 0.072065,
0.927445 to 0.930960). All five are faults of the reference, looked at
three ways (`gl1`, `soft`, `gl1-metal`, the same capture, downscaled in
scratch): on 5, 8, 11 and 16 the screen blend darkens and tints the
whole screen in `soft` and `gl1-metal` and a diagonal wedge in `gl1`
("The screen flash and `hud2d`"); frame 3 has the faintest blend
(alpha 0.02) and passes (0.006546, 0.012507). On 13 the upper left of
`gl1` shows a wrong wall over a triangle reaching the screen edge (the
clipping fault of TASK49), where `soft` and `gl1-metal` agree. At the
defaults (0.02, 0.005) 9 of 207 frames pass, frame 9 in every pair;
the others carry the world faults of TASK49 or particles, and `gl1`
against itself passes only 38 of 69 there. The inventory asked for `R_DrawParticles2` at the defaults:
it cannot be judged so here, because `gl1` against `gl1` fails them
too (bad up to 0.168740 with `gl1_pointparameters 0`).

The particles themselves, judged by blobs, not positions (the
positions are random per run): the region is the pixels more than
8/255 from the same frame of a record with `cl_particles 0` of the same
renderer. On the particle frames without a blend that show particles
(7, 10, 12, 14, 17 to 21), summed per record, `gl1-metal` against `gl1`
over the 9 pairs: coverage ratio 0.706 to 1.941, mean luminance ratio
0.795 to 1.230; `gl1` against itself 0.614 to 1.720 and 0.911 to 1.319,
so the spread is the scenario's. Pooled over the three records each,
coverage 0.7923 against 0.7629 (ratio 1.039) and luminance 34.22
against 34.71 (0.986). Frame 12, the densest (781 points), as means of
three records: coverage 0.1043 (`gl1`), 0.1031 (`gl1-metal`), 0.0794
(`soft`); mean RGB of the region 78.0, 9.1, 0.4 against 77.3, 8.8, 0.4
(`soft` 83.8, 7.7, 0.3); 71, 65 and 79 blobs. Zoomed, `gl1` and
`gl1-metal` both draw round soft-edged discs of the same sizes; `soft`
draws square blocks. No property where `gl1-metal` stands alone was
found.

Lines: a scratch copy with `gl_showtris 1`, one record each: the
outlines of `gl1-metal` lie on `gl1`'s; 18 of 23 frames pass at the
`particles` values, and the 5 over are the same 5 reference faults. An `ASAN=1 UBSAN=1` build recorded the three
scenarios with no sanitizer line.

No threshold or scenario was changed. On this host `particles` cannot
pass against `gl1` for any renderer that draws the blend over the whole
screen; TASK83 (#187) judges it against an accelerated reference, and
there it passes on every frame, the flash frames included.
