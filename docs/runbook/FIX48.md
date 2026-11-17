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
  reaches the view or the move. A focus change and the window being shown no
  longer write `paused` in a pin run, and a gamepad removed or a joystick
  added, a battery report and on iOS a safe-area or pixel-size change leave
  at once (round 1, below).
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
  made), the cinematic (no timedemo line) and a record made
  before this card (no `demo` line) are not checked: `record` says so in one
  line on stderr, `replay` in the `frames-check` line of `replay.txt`. The guard covers them.

## Tests, red first

- `tests/unit/test_ingate.c` drives `IN_KeyEvent` and `IN_CharEvent` with the
  `CL_PinsRun` stub (`stub_pins_run`) off and on, and counts what reaches the
  `Key_Event` and `Char_Event` stubs.
- `scripts/pins.sh selftest frame_count` feeds stand-in engines that print
  9560, 688, no count and two counts, a record of 689, a replay of a record
  whose `timedemo.txt` says 794, a scenario without a frames line and six
  malformed lines.

Red against the scaffolding commit (the wrappers passing everything
through, no count check): `yq2test: 2 tests, 1 failed` (`!IN_InputOpen()`,
`stub_keys.n == 0: got 6, want 0`, `stub_keys.chars == 0: got 1, want 0`)
and `FAIL: a record of 9560 rendered frames where world expects 689 must
fail`. Green after it: `yq2test: 280 tests, 0 failed`, `pins selftest: ok`.

## On the bare-metal host

Owner game data, a pin store of the card's own outside the repository,
`soft` at 640x480, `PINS_SEED=1`, `world`, `demo1.dm2`. The input came from
inside the engine: a scratch hook, never committed, at the top of
`IN_Update` pushed an `Esc` press and release (`SDL_PushEvent`) every 60th
call and a mouse movement on every call, and counted its presses on stderr.

| Build | Injected presses | Rendered (`timedemo.txt`) | Captured | Result |
|---|---|---|---|---|
| this card with the hook | 14 | 689 | 23 | complete |
| the gate opened (`IN_InputOpen` true, the old behaviour) with the hook | 26 | 1409 | 47 | incomplete: `the engine rendered 1409 frames, world.cfg expects 689 for demo1.dm2` |
| this card, no hook | 0 | 689 | 23 | complete |

`pins.sh compare soft world` of the first against the third at
`--threshold 0 --bad-threshold 0`: `frames=23 failed=0 worst=0
worst_bad=0`; both traces are 4382172 bytes. A first run of the hook
printed its count with `Com_Printf`, whose console notify lines are drawn
on the screen: that record rendered 689 frames but differed in 22 of 23
frames (worst 0.001911) and was set aside; the hook's count went to stderr
afterwards.

## Review round 1

The panel's findings and what each became, every one with its test red
first.

- **`replay.txt` said "checked" for a record that was not** (correctness
  I1). The `frames-check` line was written before the scenario's count was
  looked up. `replay` now looks it up first and writes `frames-check
  demo1.dm2 against world.cfg (689)` when the count is checked, or
  `frames-check unchecked: world.cfg names no count for water.dm2`. Red:
  `FAIL: a checked replay must say so with the count, got: frames-check
  demo1.dm2 against world.cfg`.
- **A gamepad pulled out still printed into the frames** (correctness I2,
  N3). `SDL_EVENT_GAMEPAD_REMOVED` shut the controller down and started it
  again, and both print; the host record above shows that a print spoils
  the frames while the count stays right. In a pin run that case, and
  `SDL_EVENT_JOYSTICK_ADDED`, `SDL_EVENT_JOYSTICK_BATTERY_UPDATED` and on
  iOS `SDL_EVENT_WINDOW_SAFE_AREA_CHANGED` and
  `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` (a `vid_restart`), now start with
  `if (!IN_InputOpen()) break;`: no pause, no shutdown or init of the
  gamepad, nothing printed, no restart. `sdl3.c` cannot be unit-tested, so
  `tests/check-input-gate.sh` checks it from the source: each of those five
  cases must begin with that gate. Red against the round-0 source: five
  lines, for example `case SDL_EVENT_GAMEPAD_REMOVED: its first statement
  is not the gate: if (controller && ...`. The comment of `IN_InputOpen`
  now claims only this. What stays open in a pin run, and why no frame
  changes: `SDL_EVENT_QUIT` quits (wanted); a focus change still releases
  the held keys (`Key_MarkAllUp`, which calls no key handler) and mutes the
  sound (`s_initsound 0` in a pin run); its `ogg toggle` needs music
  playing outside a demo (`cl.attractloop` is true in a demo); a window
  move resets the refresh rate, which a timedemo does not use.
- **A direct key call would get past the gate** (correctness N1). The same
  check fails on any `Key_Event(` or `Char_Event(` call in
  `src/client/input/` outside `ingate.c`, comments and strings aside. Its
  mutants: a direct `Key_Event(`, `Key_Event (` and `Char_Event(` added to
  `sdl3.c`, and for each of the five cases its gate inverted, removed and
  its label renamed; all 18 must fail it, and do.
- **A count `[` cannot compare passed** (security N1). `rendered_frames`
  now takes a count of 1 to 9 digits without a leading zero (what the
  engine's `%i` prints); `99999999999999999999`, `0689` and `1234567890`
  are no count. Red: `FAIL: a count of '99999999999999999999' frames must
  fail, not pass`.
- **A record with no `demo` line said nothing on stderr** (security N2).
  Its replay now prints `pins: the record world/gl1/rec1 names no demo
  (made before FIX48); its 794 rendered frames are not checked`. Written
  with I1's test and fixed in the same edit, so its red was not seen on
  its own.
- **Loose frames lines were skipped** (security N3, correctness N2). A
  line that looks like a frames line in any case (`// Frames`, `DEMO1.dm2`,
  `.DM2`) and is not the lower-case form is refused; so is any frames line
  below the first line that is not a comment. Red: `FAIL: the frames line
  '// Frames demo1.dm2 689' must be refused`.
- **`dlights` was never count-checked on `demo2.dm2`** (security N4).
  `dlights.cfg` now has `// frames demo2.dm2 618`, from a seeded
  record on the bare-metal host (`soft`, `PINS_SEED=1`,
  `PINS_DEMO=demo2.dm2`, `PINS_EVERY=7`, the card's own pin store): `618 frames, 10.3 seconds: 60.0 fps`, 89 frames captured; a second record with the line in place completes.
- **Files outside the card's list** (spec I1): accepted. The gate file,
  its header and Makefile entries exist because `sdl3.c` cannot be
  unit-tested; the cfg lines are the scenario counts the card asks for.

## Review round 2

- **The check guarded five of the nine gates** (correctness I1). The
  `IN_InputOpen` comment named the focus-lost, focus-gained and
  window-shown pause gates and the `IN_Move` gate, and a reviewer removed
  each with `tests/check-input-gate.sh` still green. It now requires
  `if (IN_InputOpen() && <cvar>->value <op> <digit>)` before every write of
  `paused` in the three window cases, and `if (!IN_InputOpen())` returning
  as the first statement of `IN_Move` after its declarations. Red (the
  round-1 check against a scratch `sdl3.c`): each of the four gates removed
  printed `check-input-gate: ok (... 18 mutants refused)`.
- **The parse could be fooled** (N1). A preprocessor line inside a gated
  case (`#if 0` around the gate), an always-false tail
  (`if (!IN_InputOpen() || 0 ? 0 : 0)`) and a macro alias
  (`#define KE Key_Event`, then `KE(...)`) all passed. Now a `#` line
  inside a case fails, a `?`, a comma or an `&&` in a leaving gate fails,
  and any use of `Key_Event` or `Char_Event` that is not a call (or any
  call) in `src/client/input/` but `ingate.c` fails, headers included.
- **Mutants: 46, all refused** (was 18): 3 direct calls, 2 macro aliases;
  for each of the five leaving cases its gate inverted, removed, its label
  renamed, a `#if 0` put in the case, and an always-false tail (25); for
  each of the three pause cases the same four (12); for `IN_Move` the same
  four (4).
- **What stays open** (N5). The comment of `IN_InputOpen` now lists what
  the runbook says stays open and the check does not guard: QUIT, the focus
  `Key_MarkAllUp`, the sound mute, the `ogg toggle` condition, a window
  move, and on iOS the prints of `IN_AppEventWatch`.
