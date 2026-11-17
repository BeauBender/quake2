# Input kept out of a pin run, and the rendered-frame count checked (FIX48)

TASK83 found that a key pressed while `scripts/pins.sh record` plays a demo
opens the menu and pauses the local server; the engine exits 0, logs nothing
unusual, and only the rendered-frame count shows it (9560, 794 and 1064 where
`demo1.dm2` renders 689). This card does both halves: the engine drops the
input of a pin run, and the harness fails a run whose count is not the
scenario's. The user-facing description is in `doc/100_renderpins.md`,
*Input and the rendered-frame count*.

## The guard

- **Where.** `src/client/input/ingate.c`, SDL-free: `IN_KeyEvent` and
  `IN_CharEvent` pass an event to `Key_Event` and `Char_Event` only while
  `IN_InputOpen()` is true. `src/client/input/sdl3.c` has no direct call of
  either any more: every key, mouse button, wheel step, gamepad button,
  virtual stick or trigger key and typed character goes through the two
  wrappers. `Key_Event` and `Char_Event` are the only way into the console,
  the menu and the key bindings, and `sdl3.c` was their only caller, so
  `Esc`, the console key and a key bound to `quit` do nothing. The analog
  input converges on `IN_Move`, which returns at once (and drops the gathered
  mouse movement) while the gate is closed: no mouse, stick or gyro movement
  reaches the view or the move. A focus change, the window being shown and a
  gamepad being removed no longer write `paused` in a pin run.
- **Keyed on.** `IN_InputOpen()` is `!CL_PinsRun()`: `pins_capture` above 0
  or `pins_replay` given, latched from the command line by `CL_PinsLatch`
  before any config, console command or server text runs. It is not a cvar,
  so no server can close a player's input. Without the pin arguments the
  wrappers call `Key_Event` and `Char_Event` exactly as before.
- **Kept.** `SDL_EVENT_QUIT` (the window's close button) still calls
  `Com_Quit`, so a run can be stopped. A replay runs no input at all (it
  never calls `CL_Frame`), as before.

## The count check

- Each scenario that plays `demo1.dm2` ends its header comment with
  `// frames demo1.dm2 689`, beside the `// footprint` bands, the other
  per-scenario facts of a cfg header. The count is the timedemo's (the
  `N frames, ... fps` line `CL_Disconnect` prints, kept in `timedemo.txt`)
  and depends only on the demo.
- `record` checks it after the engine exits: another count, no count or two
  counts leave the pin `incomplete` and exit non-zero with one line, for
  example `pins: the engine rendered 9560 frames, world.cfg expects 689 for
  demo1.dm2 (input reached the run, or the demo paused); the pin in ... is
  incomplete (see .../engine.log)`. A frames line that does not parse stops
  the record before an engine runs. `record` writes `demo <PINS_DEMO>` into
  `settings.txt`.
- `replay` checks the record it is about to replay the same way, from that
  record's `settings.txt` and `timedemo.txt`, before an engine starts.
- **A new scenario's count** (the choice): it copies the line of the scenario
  it is derived from, since every scenario plays the same demo. A new demo
  gets its line from a clean record (its `timedemo.txt`). A demo with no line
  (a `make-demo` demo such as `water.dm2`, whose count changes each time it is
  made; `demo2.dm2`), the cinematic (no timedemo line) and a record made
  before this card (no `demo` line) are not checked, and `record` or `replay`
  says so in one line on stderr. The guard covers them.

## Tests, red first

- `tests/unit/test_ingate.c` drives `IN_KeyEvent` and `IN_CharEvent` with the
  `CL_PinsRun` stub (`stub_pins_run`) off and on, and counts what reaches the
  `Key_Event` and `Char_Event` stubs.
- `scripts/pins.sh selftest frame_count` feeds stand-in engines that print
  9560, 688, no count and two counts, a record of 689, a replay of a record
  whose `timedemo.txt` says 794, a scenario without a frames line and six
  malformed lines.

RESULTS-PLACEHOLDER
