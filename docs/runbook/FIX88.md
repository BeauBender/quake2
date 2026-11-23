# FIX88: `ios-sim.sh run` judges each start by its own log and whole quit lines, and the test-only `Sys_ForgetHomeDir` no longer ships

FIX80's second review left four notes on how `scripts/ios-sim.sh run` decides
that a start ended through the engine's own quit path. Each weakened the test
oracle; none is a trust issue (the operator controls every input). This card
closes all four.

## 1. A stale log judged a later start

`run` copied `Documents/baseq2/qconsole.log` after each start, and nothing
removed it before the next one. The engine opens the log only at its first
print after `logfile` is registered, so a start that died before that print
was judged by the previous start's log, which holds the shutdown line.

The fix: before each start, once the data container is known
(`data_container`, the same bounded `simctl get_app_container` the run already
uses), `run` removes `Documents/baseq2/qconsole.log` and fails if it is still
there. A start that writes no log then fails with "the engine wrote no
qconsole.log in Documents/baseq2 after start N".

Proof: the selftest case `run-second-start-dies-unprinted` (fake scenario
`seconddies`, two `--launch` starts; the fake's second start exits at once,
before writing a log, decided by the count of starts in its `launches` file,
not by a time). On the old logic it passed ("run ok, 2 start(s)") and left an
`engine.2.log` copied from start 1; now it fails with the line above and
leaves no `engine.2.log`.

## 2. The quit lines were matched loosely

The shutdown line was already matched as a whole line (`grep -F -x`). The
missing-data text was matched anywhere in a line, by its opening sentence
alone (`grep -F 'Quake II needs its game data.'`), and a crash inside
`Sys_Quit`'s shutdown after the alert still passed.

The fix, for a start with no game data:

- The engine log must hold the alert's whole text as a line of its own:
  `^Quake II needs its game data[.,] .*then start the game again\.` with the
  optional rename sentence (`Sys_MissingDataMessage` on every SDK: iOS,
  visionOS, and tvOS, whose text opens with a comma), to the end of the line.
- The app's own output (`console.N.log`, the `--console-pty` output, read with
  any `\r` of a terminal's line end removed) must hold `Sys_Quit`'s last line,
  `------------------------------------` (36 dashes, `printf` to stdout just
  before `exit(0)`), as a whole line. A crash in the shutdown after the alert
  never prints it, and fails with "does not show Sys_Quit's last line".

Proof, each red on the old logic ("the smoke passed") and green now:

| Case | Fake scenario | What the start did |
|---|---|---|
| `run-missing-data-echoed` | `echodata` | printed only the opening sentence, as `+echo` would, then quit through `Sys_Quit` |
| `run-missing-data-inline` | `inlinedata` | printed the whole text inside a longer line |
| `run-missing-data-crash-after-alert` | `alertcrash` | printed the whole alert text, then crashed before `Sys_Quit`'s line |

Two cases stay green on both: `run-missing-data-quits` (`nodata`, the plain
alert) and the new `run-missing-data-rename` (`nodatarename`, the alert with
the rename sentence, its output ending lines in `\r\n`).

An exact whole-line `+echo` of the shutdown line is still indistinguishable
from Com_Quit's in the log; the card asks for the `Sys_Quit` output check on
the missing-data path only, and the shutdown path keeps its whole-line match.

## 3. The usage header was out of date

The header said a start must show the "shutting down" line. It now says what
`doc/020_installation.md` says, and both name the new rules: each start's log
is removed before it starts; the shutdown line as a whole line, or, with no
game data, the alert's whole text and `Sys_Quit`'s last line in the app's
output. The selftest list in the header names the new cases.

## 4. The test-only hook shipped

`Sys_ForgetHomeDir` (`src/backends/unix/system.c`), which only
`tests/unit/test_filesystem.c` calls, was built into every binary of the unix
backend. Its definition and its declaration in `src/common/header/common.h`
are now inside `#ifdef YQ2_UNIT_TESTS`, and the `Makefile` adds
`-DYQ2_UNIT_TESTS` to `TEST_CFLAGS` alone, the flags of the unit-test object
tree (`build/<release|debug>/test`, and `build/coverage`). `Sys_GetHomeDir`,
its cache and the filesystem search order are unchanged.

Proof: `nm` on the shipped `q2ded` and `quake2` built from the parent commit
lists `t _Sys_ForgetHomeDir` next to `t _Sys_GetHomeDir` (a local text
symbol: the release link keeps it out of the export table, so `nm -g` shows
neither, but the code ships); built from this branch it lists
`_Sys_GetHomeDir` alone. `make test` still runs the filesystem tests that
call it. The mutant (the unit build with `YQ2_UNIT_TESTS` left out of
`TEST_CFLAGS`, `DEBUG=1` so it has a tree of its own) fails the unit build
on `Sys_ForgetHomeDir`.
