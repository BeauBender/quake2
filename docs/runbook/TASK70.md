# Observed on the scripted water demo (TASK70)

Host: the build VM, macOS arm64, shared with other work and busy
throughout (load averages from `uptime` given per item), `soft` at
640x480, the default `PINS_EVERY` 30.

1. The demo: `scripts/pins.sh make-demo water` runs
   `scripts/pins/demos/water.cfg`. It loads `base1`, the first map of the
   game, gives `god`, `notarget` and `noclip` before `record water`, then
   `teleport`s the player to fixed coordinates over the large pool of
   that map (the eye about 24 units above the surface, level), holds 260
   `wait`s, moves it under the surface (about 24 units below) and holds
   320. It leaves `water.dm2` in `release/baseq2/demos/`; `git status`
   stayed clean, and nothing under `$PINS_BASEDIR` changed (checked with
   `find -newer` against a marker, TASK70 first review).
2. The length is not fixed. Console `wait` holds the script for one frame
   of at least 16 ms (`Cbuf_Execute`), so a wait count is a lower bound on
   time, and the engine has nothing that holds a script for a number of
   server frames (`fixedtime` and `timedemo` change the clock the server
   and the frame loop see, not the real-time test of `wait`). The first
   version held each place for 500 waits; reviewers measured 168 to 835
   messages and 6 to 21 captured frames from it. So the script states the
   message range it must give (97 to 124) and `make-demo` fails a demo
   outside it, keeping it as `water.dm2.failed`. With the current
   script, 7 runs with nothing else of this task running (load 108 to
   232) gave 100 or 101 messages. 5 runs made while this task's own
   mutant runs of the selftest shared the host (load 88 to 195) gave 103
   and 118 (passed) and 140, 165 and 189 (failed for the range). A run
   took 21 to 47 seconds. Re-measured on a quieter host (TASK70 rework):
   15 runs at load 5.8 to 28.0 gave 99 or 100 messages in 17 to 21
   seconds; 7 runs with the engine stopped (`kill -STOP` and `-CONT` on
   its pid) for 0.1 to 0.5 s of every second from its start, at load 7.5
   to 13.2, gave 110 to 126 messages in 20 to 33 seconds (one of 126
   failed for the range).
3. Why no walking: a walk timed by waits would end in a different place
   on another host. A teleport lands on fixed coordinates. In `noclip` the
   player has no gravity, so it stays there, and the teleport's pause
   flag, which holds the pitch at 0 until its timer runs down, never
   clears in `noclip` (`Pmove` returns before the timer for
   `PM_SPECTATOR`), so the view is level at the yaw of the level start.
   `+lookdown` therefore has no effect after a teleport in `noclip`; tried,
   the pitch stayed at 0.
4. Water, relative to the capture: every `skywater` record made from a
   demo in range (from three demos of 100, 100 and 103 messages, 19
   records) captured 4 frames. The first 2 show the pool from just above:
   a rippled, warped water surface over the lower half of the frame, the
   walls and the ceiling lights above it. The last 2 show the view from
   under the surface: the whole view warped, the pool floor below and the
   underside of the surface above. The move under water falls between
   the second and the third capture, and a longer or shorter demo inside
   the range scales both holds alike, which is what the range is chosen
   for. The rework measured it over the range (the 22 runs of item 2, a
   record at `PINS_EVERY` 30 and one at `PINS_EVERY` 1 of each demo): a
   demo of N messages rendered N-5 frames in all 22, and the move under
   water fell on rendered frame 41 to 53, 0.44 to 0.45 of the frames. The
   21 demos of 99 to 122 messages each gave 4 captures, 2 from above and 2
   from under the surface; the one of 126 messages rendered 121 frames and
   gave 5 (2 and 3), the fifth falling on frame 120. Captures fall on
   rendered frames 0, 30, 60, 90 and 120, so 4 captures need 91 to 120
   rendered frames, 96 to 125 messages; the script's range, 97 to 124,
   keeps one message inside each edge against a change in the 5-message
   lead (none seen in 22 runs), and the move under water stays at least
   11 frames after the second capture and 7 before the third.
5. Run to run: records of one demo file compared at score 0 and bad
   fraction 0 in all 27 pairs measured (6 pairs at load 192 to 197 and
   15 pairs at load 182 to 184 from a 103-message demo, 6 pairs at load
   142 to 144 from a 100-message demo). The first review measured, while
   the host was loaded by several engines, a few frames per pair of
   21-frame records at score 0.0017 and bad fraction 0.018 to 0.020. The
   cause is time-dependent rendering, not the demo or the capture
   cadence: the soft renderer animates the water warp by the client
   time, and in a timedemo `CL_AddEntities` clamps that time to between
   100 ms before and the time of the frame being shown, so it equals the
   lower bound, the same in every run, while frames take less than
   100 ms, and follows the real clock on a frame that takes longer.
   Reproduced: two records with the engine stopped for 150 ms of every
   160 ms (`kill -STOP` and `-CONT` on its pid) each differed from an
   unstalled record on one frame showing the surface from above, at
   score 0.001776 and bad fraction 0.020898; the frames under water and
   the frame indices were the same. The row below records the threshold.
6. Regeneration: a demo made again is a different file of the same
   camera places, so it is not byte-identical. Because a demo beside the
   binary adds its checksum to the pin id, records of two made demos
   never share an id; compare them with `--ref-id` and `--cand-id`.
7. A script whose map is not in the game data (tried with a throwaway
   script naming a map that does not exist) fails in 4 seconds with
   `the map <map> is missing from PINS_BASEDIR`.
8. The run writes `config.cfg`, `qconsole.log`, `pins_make_water.log`,
   `save/current/`, `save/save0/` and `scrnshot/` into `release/baseq2/`
   and `console_history.txt` into `release/`, all git-ignored, as a
   portable single-player start does.

## Thresholds measured for nondeterministic scenarios

| Scenario | Worst score | Worst bad fraction | Accept with |
|---|---|---|---|
| `models` | 0.000446 | 0.000973 | the defaults (`--threshold 0.02 --bad-threshold 0.005`) |
| `particles` | 0.011556 | 0.172061 | `--threshold 0.02 --bad-threshold 0.25` |
| `skywater` with `PINS_DEMO=water.dm2` | 0.001776 | 0.020898 | `--threshold 0.02 --bad-threshold 0.03` |

Measured on `soft` over five pairs of runs (run 1 against runs 2 to 6);
`skywater` from `water.dm2` over 27 pairs at score 0 and two records made
with the engine stalled (TASK70, item 5 above).
A fixed PRNG seed (record and replay group) would make both exact.
