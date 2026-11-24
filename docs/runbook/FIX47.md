# `make-demo water` gives an accepted demo whatever the host pace (FIX47)

## The finding

`wait` holds a console script for one frame of at least 16 ms (`Cbuf_Execute`), and nothing in
`src/` holds a script for server frames or game time (`wait` is the only hold), so the length of
the demo follows the host's pace. The range `97 124` and the holds of 260 and 320 waits suited one
host: on another every demo held 141 to 145 messages and the move under water fell near rendered
frame 60, on the third capture.

## The shape

The result must not move with the pace, so the cut is demo-relative and the driver measures it:

- **Fractions, not counts.** Both holds are counted in waits, so the move falls at a fixed fraction
  of the rendered frames while a wait lasts about the same everywhere in the run. `water.cfg` holds
  220 waits before the move and 370 after (0.37 of the waits; 0.38 of the frames, with the frame or
  so for the teleport).
- **The driver reads the demo.** `demo_move` in `scripts/pins.sh` walks the message blocks, skips a
  leading configstring, print or stufftext, and records the frame messages whose player state
  carries `PS_M_ORIGIN`. The last is the move (the first is the full first frame). Its message
  index less 5 is its rendered frame, since a demo of N messages renders N-5 frames. A new
  `// pins: move <low> <high>` line of the script is the window; the demo fails (kept as
  `.dm2.failed`) when the move is outside it or there are fewer than two origin frames.
- **The window** is frames 33 to 57: with captures at every 30th frame from frame 0, the move sits
  between the second (30) and the third (60) with 3 frames to spare. The message range, which no
  longer decides the split, widens to 92 to 154 (the move at 0.38 of the frames is on 33 at 87
  frames and on 57 at 150).

No engine command or cvar was added.

## Measured on this host (`ref_soft`, `base1`, 640x480 unless stated)

The move frame is read from the demo by `demo_move`; the captures are computed from the frame count
(N-5 frames, one capture per 30), because no `skywater` record was made and no frame was viewed.

| Holds | Run | Messages | Move frame | Captures at `PINS_EVERY` 30 |
|---|---|---|---|---|
| 260 / 320 (before) | 3 of 3 | 98 | 43 (0.46) | frames 0, 30 above; 60 under (4 captures, 2 and 2) |
| 230 / 360 (trial) | 5 of 5 | 100 | 38 | same split |
| 220 / 370 (final) | 5 of 5 | 100 | 36 (0.38) | frames 0, 30 above; 60 under |
| 220 / 370, 2560x1440 | 3 of 3 | 108 | 41 (0.40) | same split |
| 220 / 370, 3840x2160 | 2 of 2 | 159 | 61 (0.40) | refused: 159 is above 154 and frame 61 above 57 |

The 98-message figure here against 141 to 145 on the other host is the spread that the old range
could not cover: 143 frames at 0.38 puts the move on frame 54, inside the window.

## Why it does not depend on the pace

A slower host lengthens every wait, so the demo has more messages, but the waits before and after
the move stay in the ratio 220 to 370 and so does the time: the move frame is about 0.38 of the
frames at 98 messages (frame 36), 108 (41) and 159 (61, a run the 3840x2160 renderer slowed).
Whatever the pace, the driver then compares the measured frame with the window, so a pace that
breaks the ratio, or a demo too short or too long to give the split, is refused, never accepted
by a count. The pace was changed by a larger `PINS_WIDTH` and `PINS_HEIGHT` (real rendering work),
never by load.

The limit stays: a demo of fewer than 92 or more than 154 messages (a factor of 1.7 in pace) is
refused and made again.

## Tests

`scripts/pins.sh selftest`: `make_demo_script` (the move line: the forms refused, one accepted, two
refused, `demo_move_ok` at the window's edges) and `make_demo_run` (a demo with the move on frame
32, 58 or 60, or with one origin frame, is refused and kept as `.failed`; frames 33 and 57 pass;
`demo_move` reads a frame behind a configstring). Mutant: `demo_move_ok` made always true turns
`make_demo_script` and `make_demo_run` red (4 FAIL lines); restored, the selftest is green.

## Left for the other host

The acceptance on the build host (a demo of 141 to 145 messages made there passes with the move
between the second and third capture, 5 of 5 runs) cannot be run here. The expected frame is 53 to
55 of the window 33 to 57. It moves to a child card that needs that host and the game data.
