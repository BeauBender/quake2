# The `models` scenario against the software renderer (TASK51)

The shim needed no new code for models, the view weapon, flash blobs or
stencil shadows (design section 12). Measured from one private store on
the build VM: `gl1` and `soft` built from `d1c2ed53` (main), `gl1-metal`
from `260bdf4b` (the TASK51 shim), plus one `gl1-metal` record of main
and one of an `ASAN=1 UBSAN=1` build. Besides `models` itself, three
scratch copies of it were recorded (not committed, like TASK49's):
`r_shadows 1` with `gl1_stencilshadow 0` ("shadow"), the same with
`gl1_stencilshadow 1` ("stencil"), and `cl_lights 1` with
`gl1_flashblend 1` ("flash"). Two `gl1` records of each (three of
"flash"), one `soft`, three `gl1-metal`. 1-minute load: records 3.6 to
21.7 (`gl1` and `soft` 7.7 to 21.7, `gl1-metal` 3.6 to 9.7, ASAN 5.6 to
7.5), compares 7.0 to 8.5. Every count below is recomputed from the
per-frame lines of `compare` at the defaults.

**Logs.** No "not implemented" line in any `gl1-metal` record (main,
TASK51, ASAN; all four scenarios), no `glError` line in the ASAN records
(gl1 checks `glGetError` after each draw in a DEBUG build), no
sanitizer line, every record 707 frames, none skipped, none past the
vertex ring.

**Run to run** (score, bad fraction; frames exact of 23):

| Scenario | `gl1-metal`/`gl1-metal` (3 pairs) | `gl1`/`gl1` | ASAN against release `gl1-metal` |
|---|---|---|---|
| `models` | 22 exact in each; frame 7 only, worst 0.000297, 0.000436 | 1 pair: 21 exact, worst 0.000615, 0.004583 (frame 0) | 22 exact, worst 0.000062, 0.000068 |
| shadow | 22 exact in each; frame 7, worst 0.000664, 0.000859 | 1 pair: 22 exact, 0.000068, 0.000098 | worst 0.000504, 0.000863 |
| stencil | 22, 23 and 22 exact; frame 7, worst 0.000761, 0.000967 | 1 pair: 22 exact, 0.000329, 0.000456 | worst 0.000193, 0.000244 |
| flash | 14 to 15 exact, 1 to 3 over (frames 8, 18, 20), worst 0.004754, 0.083018 | 3 pairs: 1 over in each (frame 8 or 10), 12 exact in the first, worst 0.004986, 0.018848 | 2 over, worst 0.001968, 0.007363 |

Frame 7 of the three model scenarios holds an explosion sprite whose
frame comes from the engine's PRNG (`models.cfg`). In "flash" the blobs
and the dynamic lighting of models follow the lights' timing and differ
on the same frames in both renderers (frame 8: 5,790 pixels between two
`gl1` records, 457 between two `gl1-metal` records; frame 20: 4,124 in
`gl1-metal`); the mean luminance of a lit enemy on frame 10 is 21.3 to
27.0 over the three `gl1` records and 23.3 to 28.6 over the four
`gl1-metal` ones, so the "flash" noise is the scenario's, not a
renderer's.

**Against `gl1`** (each of 3 `gl1-metal` records against each of 2 `gl1`
records, 6 pairs): `models`, shadow and stencil identical in all pairs:
0 exact, 6 passing (frames 9, 12, 17, 18, 19, 20), 17 over, worst score
0.031575 and bad 0.676068 (frame 8; shadow and stencil 0.031572,
0.675843 and 0.675830). Flash: 0 exact, 4 to 6 passing, 17 to 19 over,
worst 0.037705 and 0.740062. For scale: `soft` against `gl1` 0 of 23
passing, worst 0.090731 and 0.925937; `gl1-metal` against `soft` the
same, worst 0.086572 and 0.925459.

**Three-way look** (`gl1`, `soft`, `gl1-metal`, first record of each,
same capture index; sheets downscaled in scratch only). Regions: 8-connected
regions of 150 pixels or more where `gl1` and `gl1-metal` differ by more
than 8. "Entity mask": the pixels where a renderer's frame differs from
its own `world` frame (no entities, no gun), in either renderer: models,
the weapon, sprites, beams, blobs, shadows, and brush entities (doors,
lifts, panels). Odd one out per region by its contrast against a ring
of 6 pixels around it (mean luminance inside minus outside, which takes
out `soft`'s different overall lighting): the renderer whose contrast is
at least twice as far from `soft`'s as the other's; else no witness.
`soft` draws no screen flash and no flash blob, and draws the view
weapon in another place and shape, so it is no witness on the flash
frames (3, 5, 8, 11, 16), for blobs, or along the weapon's edges.

- `models`: 118 regions (471,561 pixels) in 22 frames: `gl1` odd 52,
  `gl1-metal` odd 24, no witness 42. Of the 24, 19 lie on the flash
  frames 5, 8 and 16, where `gl1-metal` darkens the whole screen and
  the reference a wedge (TASK49; `soft` no witness). Two (frame 2,
  299,435 to 333,458; frame 7, 462,0 to 517,22) are outside the entity
  mask and differ identically between the two renderers' `world`
  frames: the world path, judged with `world` by TASK83. Two (frame 13,
  0,1 to 44,7 and 92,331 to 106,358) lie inside the triangle that `gl1`
  draws garbled over the close brush panel of frame 13 (the upper left
  half of the screen; `soft` and `gl1-metal` draw the panel alike). One
  is in the view weapon: frame 19, 589,461 to 638,479, 354 pixels, a
  straight-edged sliver along the bottom edge where `gl1-metal` is
  darker (contrast -14.7 against 9.3 in `gl1` and 7.2 in `soft`); `soft`
  draws its weapon elsewhere there, so its agreement with `gl1` is
  chance, and the shape is that of the other bottom-edge slivers where
  `soft` is a witness and `gl1` is the odd one (frame 0, 488,431 to
  556,479: `gl1` leaves the floor black under the gun; frame 10, 295,416
  to 326,479: `gl1` drops a bright weapon triangle that `soft` and
  `gl1-metal` draw). Review decided it as a fault of the reference, not of the
  shim (see the conclusion below and design section 12, "Frame 19:
  decided").
- shadow and stencil: the same regions as `models`, plus in shadow one
  at frame 7 (203,158 to 220,182), the PRNG sprite of that frame.
  Stencil adds no region: the stencil changes 574 pixels in `gl1-metal`
  and 635 in `gl1` (design section 12).
- flash: 115 regions, `gl1` odd 53, `gl1-metal` odd 20, no witness 42;
  outside the flash frames the `gl1-metal`-odd ones are frame 2 and 7
  (world, as above), frame 13 (the garbled panel, as above), frame 10
  (the weapon triangle that `gl1` drops, as above), and frame 14,
  265,395 to 414,467, 2,165 pixels: the floor behind the weapon, which
  differs in the `world` frames too (3,024 pixels of that box), the
  floor-at-the-bottom-edge difference TASK49 recorded on frame 14.

**Entity regions where the reference is sound** (the entity mask minus
the `gl1`-odd and no-witness regions, the flash frames left out, 18
frames, pooled over their pixels): `models` 592,614 pixels, score
0.000698, bad fraction 0.006972; stencil 599,936, 0.000712, 0.006957;
shadow 602,564, 0.001044, 0.007813; flash 654,635, 0.001611, 0.010667.
Per frame on `models`: score 0.000111 to 0.008249, bad 0.000357 to
0.129759 (the worst is frame 22, whose mask is 1,287 pixels).

**Luminance** of those pixels, `gl1-metal` over `gl1`: pooled 0.9995
(`models`), 0.9994 (stencil), 1.0014 (shadow), 1.0004 (flash); per
frame on `models` 0.9855 to 1.0199. No systematic offset, so no sign of
a shading or colour-array error. On the flash frames the ratio is 0.77
to 0.89, the whole-screen blend against the wedge.

So `compare gl1-metal models --ref gl1` cannot pass at the defaults
against these references, with or without `gl1_stencilshadow 1`; no
threshold or scenario was changed. TASK83 (#187) judges `models` again
against references from an accelerated OpenGL; there it passes on every
frame at the defaults, also with `gl1_stencilshadow 1`.

Frame 19 is decided: the reference is wrong and the shim is right. In
589,461 to 638,479 `gl1` draws 328 pixels that `gl1-metal` does not and
none the other way; three witnesses say no weapon geometry is there:
`gl1` at `r_gunfov 100` (the spot is then away from every screen edge)
covers 16 of the 328, `gl1-metal` at `r_gunfov 100` covers 6, and `soft`
at `r_gunfov 96.379` (the value that matches the weapon projection of
`gl1`; `soft` reads the cvar as a horizontal field of view, `gl1` as a
vertical one) covers 19. Frames 18, 20 and 21 have no differing pixel
there, and on frame 17 the reference drops a piece of the weapon instead
(81 pixels the other way, 78 confirmed by `gl1` at fov 100).
