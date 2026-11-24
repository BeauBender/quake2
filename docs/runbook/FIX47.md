# `make-demo water` gives an accepted demo whatever the host pace (FIX47)

## The finding

`wait` holds a console script for one frame of at least 16 ms (`Cbuf_Execute`), and nothing in
`src/` holds a script for server frames or game time (`wait` is the only hold), so the length of
the demo follows the host's pace. The range `97 124` and the holds of 260 and 320 waits suited one
host: on another every demo held 141 to 145 messages and the move under water fell near rendered
frame 60, on the third capture.

## The shape

The result must not move with the pace, so the cut is demo-relative and the driver measures it:

- **Fractions, not counts.** Both holds are counted in waits, so the move falls at nearly a fixed
  fraction of the rendered frames while a wait lasts about the same everywhere in the run.
  `water.cfg` holds 220 waits before the move and 370 after (0.37 of the waits). The measured
  fraction of the frames is 0.38 to 0.40 (36 of 95, 41 of 103, 61 of 154), not one number.
- **The driver reads the demo.** `demo_move` in `scripts/pins.sh` walks the message blocks, skips a
  leading configstring, print or stufftext, and records the frame messages whose player state
  carries `PS_M_ORIGIN`. The last is the move (the first is the full first frame). Its message
  index less 5 is its rendered frame, since a demo of N messages renders N-5 frames. A new
  `// pins: move <low> <high>` line of the script is the window; the demo fails (kept as
  `.dm2.failed`) when the move is outside it or there are fewer than two origin frames.
- **The window** is frames 33 to 57: with captures at every 30th frame from frame 0, the move sits
  between the second (30) and the third (60) with 3 frames to spare. The message range is 96 to
  147, and both ends are arithmetic on N-5 frames and the fraction 0.38 to 0.40:
  - 96 messages: 91 frames, captures at frames 0, 30, 60 and 90 (4), the move at 91 * 0.38 = 34.6
    to 91 * 0.40 = 36.4, inside 33 to 57. Fewer messages (95: 90 frames) give 3 captures.
  - 147 messages: 142 frames, captures at frames 0, 30, 60, 90 and 120 (5), the move at
    142 * 0.38 = 54.0 to 142 * 0.40 = 56.8, inside 33 to 57. At 148 messages (143 frames) the move
    may be at 143 * 0.40 = 57.2, outside the window, so the range stops at 147.
  - So a demo gives 4 captures (96 to 125 messages) or 5 (126 to 147): the first 2 from above the
    water, the rest (2 or 3) from under it. The earlier "4 captures, 2 and 2" was true of the
    first range only. The count may differ between demos because the id of a record includes the
    demo's checksum (`demo_sum`), so a demo made again records fresh pins.

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
| 220 / 370, 3840x2160 | 2 of 2 | 159 | 61 (0.40) | refused: 159 is above 147 and frame 61 above 57 |

The 98-message figure here against 141 to 145 on the other host is the spread that the old range
could not cover. With the final holds (590 waits against 580) that host is expected at about 143 to
148 messages, 138 to 143 frames, the move at 52.4 to 57.2. Plainly: 148 messages would now be
refused by the range, and 147 is accepted; the child card checks that point on that host.

## Why it does not depend on the pace

A slower host lengthens every wait, so the demo has more messages, but the waits before and after
the move stay in the ratio 220 to 370 and so does the time: the move frame is 0.38 to 0.40 of the
frames at 98 messages (frame 36), 108 (41) and 159 (61, a run the 3840x2160 renderer slowed). The
window tolerates any fraction from 0.38 to 0.40 over the whole range, and 3 frames of error either
way on top (the 3 frames to the captures). Not "0.38 at any pace".
Whatever the pace, the driver then compares the measured frame with the window, so a pace that
breaks the ratio, or a demo too short or too long to give the split, is refused, never accepted
by a count. The pace was changed by a larger `PINS_WIDTH` and `PINS_HEIGHT` (real rendering work),
never by load.

The limit stays: a demo of fewer than 96 or more than 147 messages (a factor of 1.5 in pace) is
refused and made again.

## Tests

`scripts/pins.sh selftest`: `make_demo_script` (the move line: the forms refused, one accepted, two
refused, `demo_move_ok` at the window's edges) and `make_demo_run` (a demo with the move on frame
32, 58 or 60, or with one origin frame, is refused and kept as `.failed`; frames 33 and 57 pass;
`demo_move` reads a frame behind a configstring and behind a print block, svc_print, level, empty
string; a message range of 95 or 148 is refused and 96 to 147 passes; the tracked `water.cfg` carries
exactly one `// pins: move` line). Mutants: `demo_move_ok` made always true turns `make_demo_script`
and `make_demo_run` red (4 FAIL lines); the print skip changed to `? 3 : 1` turns `make_demo_run`
red on the print fixture; the move line deleted from `water.cfg` turns `make_demo_script` red;
restored, the selftest is green.

## Left for the other host

The acceptance on the build host (a demo of 141 to 145 messages made there passes with the move
between the second and third capture, 5 of 5 runs) cannot be run here. The expected frame is 52 to
57 of the window 33 to 57, and the message count 143 to 148, where 148 is refused. It moves to a child card that needs that host and the game data.
