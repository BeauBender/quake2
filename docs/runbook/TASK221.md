# TASK221: the gate's check runner and the bounded path go through the supervisor

TASK221 (#758) moves the bench core and the gate's check runner onto the native
process supervisor (TASK218, `scripts/lib/supervise.c`), and then deletes the
shell supervision and the ps identity layer. Slice 1 moved the gate's check
runner. Slice 2 (below, "Slice 2: the bounded path") moves `bench_bounded`. The
lock run and the deletions are left for slice 3 (see "What remains").

`scripts/test.sh` had a comment-only edit in slice 1: its note on job control now says
that the scheduler's group holds the supervisors too, and that each check gets every signal
at its default from its supervisor. No code in it changed.

## The check runner

`scripts/gate-checks.sh` starts each check under a supervisor of its own:

```sh
"$gate_sv" --grace "$gate_grace" --name "${_gsc##*/}" -- "$_gsc" < /dev/null > "$_gd/$1.log" 2>&1 &
```

- **The start.** The supervisor puts the check into a new process group, with every signal at
  its default, and owns its reap. The `set -m` job, its `trap - TTOU` and `trap '' TTOU` dance,
  and the `bench_spawn_id` record are gone. The supervisor is a background job in the
  scheduler's own process group, with SIGTTOU ignored as in the scheduler, so no write of the
  fork can stop it on a `tostop` terminal.
- **The reap.** `gate_reap` waits for a check's supervisor. The supervisor returns only once the
  check's group is empty: what a check leaves behind gets TERM and CONT, and KILL after
  `gate_grace` (10 s). So the reap no longer calls `gate_end`, and waits no grace of its own.
  A group that cannot be emptied makes the supervisor exit 126, and the check is reported as
  failed (it was ignored before).
- **The stop.** `gate_end` sends SIGTERM to each running supervisor and waits for each. The
  shell reaps a background job as soon as it exits, before any `wait`, so the pid of a
  supervisor that ended after the reap's last poll is already back with the system while its
  entry is still in `_grun`. `gate_end` sends no TERM to a supervisor that `bench_pid_alive`
  says is gone, which spares a `kill` that would fail. The test does not narrow the window in
  which the pid could be another process's (slice 2 corrected this sentence): a pid given out
  again answers `kill -0` too, so the window still runs from the supervisor's reap to the
  `kill`. What keeps it small is that the system hands pids out in turn. The `wait` stays: it gives a
  reaped job's status, or fails at once for a pid that is no job. A signal that arrives between
  the fork and the pid's entry in `_grun` finds the pid as `$!`: `gate_start` marks the window
  (`_gss`, `_gsb`), and `gate_cleanup` adds that pid.
- **The supervisor's build.** `sv_path` builds the supervisor into the tool cache on first use.
  If the build fails, the run fails with status 2 before any check starts.
- **SIGTERM ignored.** A signal ignored at entry cannot be trapped, and each supervisor would
  inherit the ignore and never begin a stop, so a stop would wait with no bound. The scheduler
  therefore refuses to start, with status 2 and one `gate:` line, when SIGTERM is ignored. The
  test runs before any TERM trap: a child shell sends itself TERM and says whether it lived on.
  The trap listing is no test. bash 3.2 as `sh` lists no trap that did not take, but dash lists
  a trap on a signal ignored at entry that it never runs. The child-shell probe gives the right
  answer under `/bin/sh` (bash 3.2), bash and dash, with TERM ignored, INT ignored or neither.

The scheduler still sources `bench/core/common.sh`, for `bench_pid_alive` only.

### Behaviour that changed

- A stop that has begun now ends even when the scheduler is killed during it. The supervisor,
  not the scheduler, runs the grace and the KILL. This is the new `killgrace` case.
- A check now gets SIGINT at its default even when the scheduler has SIGINT ignored, as the
  merge gate starts it. The supervisor's contract is that the command gets every signal at its
  default. The `sigs` case now compares a check run by `test.sh` with a direct start that has
  every signal at its default.
- A supervisor's 126 (a group it cannot empty) is shown as `FAILED (status 126)`. This is the
  new `sv126` case.
- A scheduler started with SIGTERM ignored refuses to start (status 2). This is the new
  `termign` case.

### Limits

- **The gate is now Darwin-only.** Every gate run needs the supervisor, which uses kqueue and
  libproc and is built with `cc`. The repository builds and tests on Apple platforms only.
- **A SIGKILL to the scheduler alone** (no TERM first, `test.sh` alive) leaves the supervisors
  and their checks running. Nothing tells them the scheduler is gone: `supervise.c` does not
  watch its parent. This is unchanged from main, where the checks also ran on in process groups
  of their own. A supervisor that treated its parent's exit as a stop would close it.
- **The `sv_safe` gap is now reachable from every gate run.** The gap is the ACL and parent-path
  hole in the cache directory check, noted in the TASK218 review. Each run calls `sv_path`
  against the default tool cache and runs the cached binary. That binary was a test-time tool
  before. The gap stayed inside the local, same-user threat model, and slice 2 closed it (see
  "`sv_run`" under "Slice 2").

## The check

`tests/check-gate-parallel.sh` copies `scripts/lib/supervise.sh` and `supervise.c` into each
staged tree. It builds the supervisor once up front and exports that tool cache as
`SUPERVISE_CACHE`, so a case that sets `HOME` elsewhere (`ttymsg`) still finds it.

`tests/check-ci-map.sh` and `tests/check-bench-entry.sh` also stage `gate-checks.sh` in trees of
their own, so they copy `scripts/lib/supervise.{sh,c}` there too. Each one builds the supervisor
once, with the real `cc`, into a `SUPERVISE_CACHE` of its own. `check-bench-entry` does this
before any of its stubs is on `PATH`. Its `t_nested` fake check walks one parent further,
because its parent is now its supervisor, whose parent is the scheduler.

- **`killgrace` (new).** The scheduler gets SIGTERM while its one check ignores it. Once the
  check has said it got the TERM, the scheduler gets SIGKILL, and the check must still end
  within the failure bound. The check writes its lines to a FIFO and holds its write end, so
  end of file there means it has exited. The case waits only on those lines and on that end of
  file (`tests/lib/proc.sh`), with no sleep and no ps. On main it fails: the check outlived the
  60 s bound. After the change it passes in about 11 s (the 10 s grace). It passes under
  `sh -c "trap '' INT; exec ..."` too.
- **`drop`.** The case now asserts that the scheduler asks `ps` nothing about a check, before
  the SIGTERM or after it. Its `drop-emptied` mutant is gone: the reap waits no grace any more,
  so no signal can arrive in one.
- **`ttyfork`.** The case looks for the trace line `+ gate_start 1` in place of `+ set -m`.
- **`intskip`.** `sigs` no longer skips its assertion that SIGINT is delivered when the
  self-test runs with SIGINT ignored. That skip is now impossible by design: the supervisor
  spawns each check with every signal at its default (`POSIX_SPAWN_SETSIGDEF`), so a check gets
  SIGINT however the scheduler was started. `intskip` now asserts the new contract: `interrupt`
  and `testsh` each still say what they skip, `sigs` says it skips nothing, and all three pass.
  With SIGINT ignored, `sigs` passing means its unconditional `sig a INT delivered` check held.

### Mutants

| Mutant | Case | What it shows |
|---|---|---|
| `no-supervisor` | `interrupt` | the check run without its supervisor: the deleted `set -m` job and the stop are needed |
| `no-supervisor-leave` | `leave` | the same: the deleted `gate_end` at the reap is now the supervisor's |
| `no-supervisor-killgrace` | `killgrace` | the same: a scheduler that stops its checks itself leaves one running when it is killed |
| `no-supervisor-sigs` | `sigs` | the same: the deleted `trap - TTOU` is now the supervisor's default dispositions |
| `grace-unpassed` | `interrupt` | the supervisor gets `--grace 3600` in place of `gate_grace`: check `b` ignores SIGTERM and its sleep is now 60 s, longer than the case's 30 s bound (it was 30 s, so it ended by itself inside the bound and the mutant survived). Leaving the flag out would be an equivalent mutant, since the supervisor's own default is also 10 s |
| `term-ignored-starts` | `termign` | the refusal of an ignored SIGTERM is dropped: the scheduler runs its check, which outlives the case's bound |
| `sv126-ok` | `sv126` | a supervisor's 126 is taken for success |
| `stderr-closed-unguarded-sched` | `stopnoerr` | the scheduler leaves a closed standard error closed. The stops no longer copy fd 2 (the supervisor opens a closed descriptor on `/dev/null` itself), so the case now also starts the scheduler with its standard error closed and a bad `GATE_JOBS`: status 2 and no check run. Without the guard, the write of the error line fails under `set -e` and the status is 1 |
| `no-own-group` | `kgrp` (was `sigs`) | `test.sh` starts the scheduler without job control. The SIGINT and SIGQUIT a check gets no longer depend on that: its supervisor resets every signal, so `sigs` passes with this mutant (`no-supervisor-sigs` covers the dispositions). What the scheduler's own group still does is keep it and the supervisors, its background jobs, out of `test.sh`'s group: without it, `kgrp`'s SIGKILL to that group kills them all, and the check, in a group of its own, outlives the run. It is no longer SIGINT-only, so it also runs when SIGINT is ignored |
| `no-stop` | `stop` | `gate_end` sends no TERM |
| `no-stop-wait` | `interrupt` | `gate_end` does not wait for the supervisors |
| `group-kill-dropped`, `no-kill-after-grace`, `no-kill-after-grace-stop`, `no-cont` | `interrupt`, `stop` | `cmutant` builds a supervisor from a changed `supervise.c` into a cache of its own: the signal goes to the leader alone, the KILL is signal 0, the CONT is dropped |

The mutants `no-process-group`, `no-end-at-exit`, `ttou-to-checks`, `ttou-after-start` and
`start-stderr-on-tty` named lines that are gone. `drop-emptied` went for the reason in the
`drop` bullet above: its line is still there, but the reap waits no grace any more, so no
signal can arrive in one. `ttou-scheduler` now names the one `trap '' TTOU` left, with no
prefix to tell it from a second one.

## Review notes carried from TASK218

- `sv_safe`'s ACL and parent-path gap in the cache directory check: closed in slice 2 (see
  "`sv_run`" under "Slice 2").
- `sv_count`'s relisting is unbounded and ignores the deadline: this is in `supervise.c`, which
  is outside this card's files.
- The invariants job has no `timeout-minutes`: this is in `.github/workflows/`, which is outside
  this card's files.

## Slice 2: the bounded path

### `sv_run` (scripts/lib/supervise.sh)

- **`sv_run <options> -- cmd...`** starts the supervisor in the background (`sv_start`) and
  waits for it (`sv_wait`). `sv_wait` waits again when a trap ended the wait while the
  supervisor is still there, and collects its status once it is not. `sv_term` sends the
  supervisor TERM, and `sv_stop` is `sv_term` and then `sv_wait`. These are the only shell
  lines that signal the supervisor.
- **Every stop is a TERM.** The supervisor is a background job without job control, so it
  starts with SIGINT and SIGQUIT ignored, and it keeps them ignored. So an INT is never
  forwarded as an INT.
- **`sv_run` sets no trap.** The card asked for its own INT, TERM and HUP traps. A trap set
  there replaces the caller's, and the core uses no `eval`, so the caller's trap could not
  be put back. Instead, the caller's own trap runs at once in `sv_wait`'s `wait`, and it
  calls `sv_stop` (through `bench_stop_group`).
- **The fork window.** A trap that runs between the fork and the store of `SV_PID` finds
  the supervisor as `$!` (`sv_pid`, by `SV_FORK` and `SV_LAST`).
- **`sv_safe`.** It now also refuses a cache directory or a binary that has an ACL (an
  entry line in `ls -lden`), and every parent of the cache directory that is writable by
  group or others and not sticky, or that is owned by neither this user nor root. This
  closes the TASK218 review's ACL and parent-path gap. A deny ACL on a parent (macOS home
  directories have one) is allowed, because parents are judged by mode and owner only.
- **The physical path.** `sv_path` resolves the cache to its physical path (`pwd -P`)
  first. It checks that path, makes its temporary files and builds there, and prints it.
  So no symbolic link on the way can be swapped once it was checked, and `BENCH_SV` holds
  that physical path (review round 1, security I1).

### `bench_bounded` (scripts/bench/core/common.sh)

- **The command runs under the supervisor:** `--timeout <wall>` (a wall time of 0 is
  `0.001`, since `--timeout 0` means no deadline), `--grace BENCH_STOP_GRACE` and
  `--events 9`.
  - A first word `bench_to <out> <err>` becomes the redirections of the supervisor's start,
    because the supervisor cannot run a shell function.
  - `BENCH_BOUNDED_PID` is the supervisor's pid. It is set to `starting` from the events
    file's `mktemp` onwards, so a caller's trap in the fork window still calls
    `bench_stop_group`.
- **The library** is found beside `common.sh` (bash names a sourced file), else beside
  `BENCH_ROOT`, else in `BENCH_SV_LIB`. Some tests run a copy of `scripts/bench` alone:
  `check-bench`'s mutants, `check-bench-quake2`'s mutants and the `ios-device.sh` self-test.
  Those tests export `BENCH_SV_LIB` (and a `SUPERVISE_CACHE` of their own).
- **Status contracts kept:**
  - the command's own status, `HOW=exit`. This holds for its own 124, 125 and 126 too:
    the supervisor passes the command's status through, so `bench_bounded_ours` tells the
    supervisor's own by its events. 124 is the wall time only with a `deadline` event, 125
    is "cannot run" only with no `spawned` event, and 126 is "cannot empty the group" only
    with no `reaped` event, because the group may then still have a member;
  - 124 past the wall time, `HOW=wall`, with the supervisor's events appended to standard
    error;
  - 124 with `HOW=until`;
  - 2 for an until test that is missing or gone;
  - 127 for a command that cannot be run (the supervisor's 125).
- **126 keeps the lock and says so.** The supervisor's 126 (a group it cannot empty) becomes
  125 with `HOW=untold`. The command's group (its `spawned` event) is recorded in the lock
  state as a group record: `untold.<pgid>` with `group=<pgid>`. `bench_lock_untold_live`
  takes such a record as live while `kill -0 -- -<pgid>` succeeds, because no process
  takes a group's id while the group has a member. A record whose id a later group has
  taken holds nothing: that is when ps shows a process with the pid `<pgid>` that started
  after `written=` (`bench_lock_untold_reused`, the pid records' rule; review round 1,
  security I2). A ps that gives no answer keeps the lock. The lock shell's release then
  keeps the lock and says so.
  - **A 126 with no group named** (no `spawned` event: a failed events write, or the
    supervisor's "cannot watch the command") writes a hand record instead:
    `untold.<id>` with `hand=<id>`, under the supervisor's pid (the bounding shell's when
    there is none). It holds the lock until a person removes it, with
    `rm -f '<state>/lock/untold.<id>'` (review round 2, security I1). Every line that shows
    the lock names a hand record as one, with that `rm -f`, because the record's own line
    goes to the bounded caller's standard error, which `snap.sh` and `host.sh` send to
    `/dev/null`: the line `bench_lock_record` prints, the lock shell's keep note, the
    waiters' line, and `lock status` (`hand=<ids>` on the lock line, then
    `hand=<id> rm -f '<path>'` for each). The path is in single quotes, a `'` in it written
    `'\''` (`bench_lock_hand_rm`), so a copied line cannot break (review round 3).
  - **A trap after the 126 is collected.** `bench_bounded` empties `SV_STATUS` before the
    supervisor starts. Once `sv_wait` has collected the status, `bench_bounded_stop` acts on
    `SV_STATUS` rather than on `sv_stop`'s 0, so a trap that comes before `bench_bounded`
    has recorded the group records it itself (review round 2, correctness I1).
- **The shims.**
  - `bench_stop_group` on `BENCH_BOUNDED_PID` stops the supervisor (`bench_bounded_stop`:
    `sv_stop`, then the 126 record) and returns 0. Other pids still go to the shell stop,
    for the lock run and `ios-sim.sh`'s `bench_spawn`.
  - `bench_deferred` is unchanged. `bench_bounded` sets no `BENCH_DEFER`.
  - Both become two-line shims in slice 3, once nothing else uses the shell stop.
- **`BENCH_BOUNDED_UNTIL` and `BENCH_BOUNDED_QUIT`** are read at a 1 s poll (`bench_nap 1`)
  while the supervisor runs, and the poll sends it TERM.
- **The events file.** It is `bench-sv.<pid>.XXXXXX` under TMPDIR. Its name comes from
  `mktemp -u` and is set before the file is made (noclobber, mode 0600). It is removed
  before the bound returns and by `bench_bounded_stop`. `bench_poll_end`, which every trap
  path calls first, removes that one file by its name while no bounded pid is set, never
  `bench-sv.<pid>.*`, which a sibling subshell shares (review round 1, N3).
- **`BENCH_BOUNDED_TAP`** (a test's FIFO) gets the events as well as the file. The
  supervisor writes to a FIFO of the run's own (`<events file>.tap`), and `tee` copies it
  into both. `tee` opens that FIFO before the supervisor's start does. `bench_bounded_tee`
  waits for `tee` once the supervisor is gone. A read-write open of the FIFO first ends a
  `tee` that no supervisor opened (review round 1, N2).
- **`bench.sh`'s signal path** stops the bounded command through the shim. Its FIX181 hold
  for a bounded command it "could not tell" about is gone, because nothing returns that
  status any more.

### Behaviour that changed

- **A group record and a reuse of its id.** Once the recorded group has emptied, a new
  process could take its id as pid and lead a new group with it. The record holds nothing
  then, by the `written=` rule (round 1, security I2). It still keeps the lock when ps
  gives no answer for that pid, or when the new group's leader has gone and its members
  remain.
- **A stopped command** (a stop signal, or the terminal) is no longer told apart: 74 and
  `HOW=stopped` are gone. The supervisor's CONT ends the stop at the wall time (124).
- **What a command leaves in its group** gets TERM at once, not after 1 to 2 s.
- **After a quit time,** the command has `BENCH_STOP_GRACE` seconds to end, not the rest of
  the wall time. Its status is the `reaped` event's.
- **A trap's stop of the bounded command** sends TERM to the supervisor and waits. The
  supervisor runs the grace and the KILL, so the stop ends even when the caller is killed
  during it.

### The cases (tests/check-bounded-run.sh, 2c)

Each case waits on the supervisor's events (`BENCH_BOUNDED_TAP`) and on end of file
(`tests/lib/proc.sh`), with no sleep and no ps, and a 10 s failure bound:

| Case | What it shows | On main (28ebd7aa) | On this branch |
|---|---|---|---|
| `spawned` | an INT at `spawned`: exit 130, the command's group gone, TMPDIR empty | FAIL: no spawned event within 10 s | ok, 0 s |
| `endgroup` (#726) | an INT once the command exited and left a member that ignores TERM: the caller's `trap 130` line and then the supervisor's `signal name=TERM` both come before the supervisor's own stop ends, with a grace of the bound plus 2 s (12 s); then the member is killed and nothing is left | FAIL: no spawned event within 10 s | ok, 12 s |
| `pollint` (#741) | an INT while the until poll waits: exit 130, nothing running, no scratch file in TMPDIR | FAIL: no spawned event within 10 s | ok, 1 s |
| `126` | a stand-in supervisor that exits 126: `caller bounded 125 untold` (round 2: the caller's lines start with `caller`), lock status shows `untold=<pgid>`, and the lock shell says it keeps the lock, then frees it only after the command has said it ended | (added with the change) | ok |
| `126 term` | the same stand-in. An `awk` stand-in first on the caller's PATH sends its parent TERM when it is asked for the `reaped` event, the first fork after `sv_wait` has collected 126. The caller's trap stops the run as `bench.sh`'s does, and the lock is kept for the group until the command has ended | (round 2) FAIL on 0e03a682: lock status shows no `untold=` | ok |
| `126 nospawn` | a stand-in that writes no `spawned` event: a line names the hand release, lock status shows `untold=<stand-in pid>`, the lock is still held after the command has ended, and it is freed once the case removes the record | (round 2) FAIL on 0e03a682: no line names a release by hand (nothing recorded) | ok |
| `lock note` | a lock run with ps silent: the watch says a stop goes to the command's group, and a TERM to the lock shell stops it and frees the lock | (round 1) | ok |
| `group reuse` | a group record written before its group started holds nothing; one written after holds the lock | (round 1) FAIL: the first held the lock | ok |
| `own 124`, `own 125`, `own 126` | `sh -c 'exit N'`: status N, `HOW=exit`, and nothing said of a wall time or of an untold stop | (round 1) FAIL: `124 how wall`, `127 how exit`, `125 how untold` | ok |
| `tap` | with `BENCH_BOUNDED_TAP`, a wall time is still `124 how wall`, the tap has the `deadline` line, and TMPDIR is empty | (round 1) FAIL: `124 how exit` | ok |
| `poll-end` | `bench_poll_end` leaves a sibling's `bench-sv.<pid>.*` file | (round 1) FAIL: removed | ok |
| `sv window` | under `/bin/sh`, the state a trap sees just after `sv_wait`'s `wait` collected a 126 (`wait`: `SV_STATUS` empty; `status`: just after `SV_STATUS=`; `done`: just after `SV_PID=`), with `bench_bounded_stop` called there and the record printed: a group record after `spawned`, a hand record with no events, none after `reaped`, and a TERM to the collected pid only at `wait` | (round 3) FAIL on adeee98e: `wait` and `status` gave no record | ok |
| `hand quote` | the printed `rm -f` of a hand record reads back, through `sh`, as its path, a `'` in the lock directory included | (round 3) | ok |
| `sv endgroup order` | on the four line streams a `sv-no-term` run can give (`trap 130` before and after `group-empty`), the mutant's verdict is the one refusal | (round 3) the in-order read gave "no line trap 130" on two | ok |

The 126 stand-in is planted in a tool cache of its own, under the name `sv_path` gives the
real binary, as slice 1's `sv126` case does.

| Mutant | Case | Refused because |
|---|---|---|
| `sv-no-term` | `spawned`, `endgroup` | `sv_stop` sends no TERM: the caller outlives the INT by the bound, and in `endgroup` no `signal name=TERM` comes before end of file, in any order (round 3; it was "before the stop's end", which an early `group-empty` could cut) |
| `lock-freed-on-126` | `126` | no group record on a 126: lock status shows no `untold=` |
| `record-not-written` | `126` | the record's `mv` fails (moved from `check-bench`) |
| `release-no-wait` | `126` | the release does not wait: "freeing the lock" comes before `command-ended` (moved from `check-bench`) |
| `watch-leader-note` | `lock note` | the watch's leader note is never said (round 1: restored) |
| `group-reuse-unchecked` | `group reuse` | a group record is live by `kill -0` alone (round 1) |
| `status-only` | `own 124` to `own 126` | the status alone decides again: each is taken for the supervisor's (round 1) |
| `trap-status-dropped` | `sv window` (round 3; was `126 term`) | the trap's stop takes `sv_stop`'s status again: at `done` with no events nothing is recorded (round 2). The `126 term` case no longer refuses it, since its `spawned` event now records the group without the collected status |
| `hand-not-recorded` | `126 nospawn` | a 126 with no group named records nothing again: no line names a release by hand (round 2) |
| `events-unchecked` | `sv window` | the trap's status decided by the wait alone: at `wait`, no record (round 3) |
| `status-after-pid` | `sv window` | `SV_STATUS` used only once `SV_PID` is clear (round 2's order): at `status`, the collected pid is sent TERM and waited for again (round 3) |
| `status-hand-unmarked` | `126 nospawn` | lock status does not mark the hand record: no `hand=` and no `rm -f` there (round 3) |

In `tests/check-supervise.sh` (round 1), `sv_refusals` runs five cases. Each has a cache of
its own under the check's TMPDIR, seeded with the built binary, and ACLs are set with
`chmod +a` there only:

| Case | Verdict |
|---|---|
| `parent-0777` | refused |
| `parent-1777` | accepted, in the physical directory |
| `acl-cache` (an allow ACL on the cache) | refused |
| `acl-binary` (an allow ACL on the binary) | refused |
| `symlink-parent` (the cache through a link in a 0777, not sticky directory) | accepted, in the physical directory, never through the link |

The two helper mutants are one-line `sed` changes to a copy of `supervise.sh`.
`parents-ok` (`sv_parents` returns 0) fails `parent-0777`. `acl-unread` (the ACL line count
dropped) fails `acl-cache`. On the round's first head, `parent-1777` and `symlink-parent`
failed: the printed path was the logical one.

### What went with the shell path

These cases drove the shell watch and stop through `bench_bounded`, which no longer runs them:

- `check-bench`'s `t_core_id`: the ps-silent bounded case, the FIX184 window, the no-group
  125 case, the two FIX181 holds, the FIX184 lock shell TERM, the 125-under-the-lock case,
  and the identity table's 2000 bounded runs.
- Their mutants: `watch-leader-note`, `bounded-says-stopped`, `bounded-no-hold`,
  `bounded-hold-lock-grace` and `forget-before-clear`. `record-not-written` and
  `release-no-wait` moved to the 126 case.
  - `watch-leader-note`'s line is still live, in `bench_lock_run`'s watch. Round 1
    restored it: `check-bounded-run`'s `lock note` case asserts it, and the mutant is
    back. Both go with `bench_watch` in slice 3.
- `t_bounded`'s stopped command now wants 124 within a wall time of 2 s.
- `check-bounded-run`'s `int-window` and `int-spawn` probes and their mutants
  (`watch-foreground-ps`, `spawn-foreground-ps`).
- The `ios-device.sh` self-test's staged copy, and its `sign-int-window`, `install-int-window`
  and `sign-hup-spawn` cases. The self-test now runs 65 cases.

### Checks run

| Check | Result |
|---|---|
| `tests/check-bounded-run.sh` | ok, on the branch; on main, only the three new cases fail. After round 1: "ok (guard, 65 mutants refused, probe, the supervisor cases and their 7 mutants, pyprobe and its 10 mutants)", with 13 supervisor cases (`spawned`, `endgroup`, `pollint`, `126`, `group reuse`, `lock note`, `own 124` to `own 126`, `tap`, `poll-end`) |
| `tests/check-supervise.sh` | round 1: ok, with the 5 refusal cases, the 7 supervise.c mutants and the 2 helper mutants |
| `tests/check-bench-entry.sh` | ok (also after round 1) |
| `tests/check-gate-parallel.sh` | round 1: "ok (37 cases, 104 mutants caught)", in 10 min 38 s (a run capped at 590 s passed all 37 cases and ran out in the mutants) |
| `scripts/ios-device.sh --selftest` | ok, 65 cases (also after round 1) |
| `tests/check-doc-links.sh`, `tests/check-runbook-index.sh` | round 1: ok |
| `tests/check-bench.sh` | not run here: the host lock had a queue, so it goes to the hold script (see below) |

**The one-time measurement.** `check-bench`'s wall time, on main and on this branch, is
taken once each by the hold run (`hold-T221s2.sh`), and is recorded here from its output.

### Files outside the card's list

- **`tests/check-bench-quake2.sh`** exports `BENCH_SV_LIB` (and a `SUPERVISE_CACHE` of its
  own) for its mutants, which run a copy of `scripts/bench` alone. That copy has no
  `scripts/lib`, so `bench_bounded` would find no supervisor. No behaviour changes.
- **`scripts/ios-device.sh`** is in the card's list "only if its self-test names a deleted
  function", which it does not. Its self-test needs the supervisor cache
  (`BENCH_SV_LIB`), for the same reason as above, and its dead `stopped)` branch went in
  round 1.
- **`scripts/pins.sh` and `scripts/ios-sim.sh`** (round 1): their dead `stopped` branches
  and comment went, because nothing sets `HOW=stopped` any more.
- **`tests/check-supervise.sh`** (round 1): the refusal cases and helper mutants of
  security I1 and I3, which test `scripts/lib/supervise.sh`.
- **`scripts/gate-checks.sh`** (in the slice's first commit, named in round 2): a comment only. The
  `gate_end` comment claimed that the `bench_pid_alive` test before the TERM narrows the
  window in which the reaped supervisor's pid could be another process's. It does not
  (a pid given out again answers `kill -0` too); the test only spares a kill that would
  fail. The comment now says so. It is kept, not reverted, because the old text was wrong
  about the code this slice's stop relies on. No behaviour changes.

### Review round 1

Each finding was reproduced first, and its case was red before the fix:

- **Correctness B1** (a command's own 124, 125 and 126). Reproduced: `own 124 -> 124
  wall`, `own 125 -> 127 exit`, `own 126 -> 125 untold`. Fixed by `bench_bounded_ours`,
  which decides by the events. Cases `own 124` to `own 126`; mutant `status-only`.
- **Correctness I1** (`endgroup` passed with no TERM). Fixed: the caller's trap writes
  `trap <status>`, and the case waits for it and then for `signal name=TERM`, each before
  `grace-end` or `group-empty`, with a grace above the bound. `sv-no-term` now runs
  against `endgroup` too and is refused ("no line signal name=TERM").
- **Correctness I2** (`watch-leader-note` untested). The branch is reachable: a lock run
  whose ps gives no answer, with a command that leads its group. Restored as the `lock
  note` case with its mutant; the branch is kept.
- **Security I1** (the symbolic link swap). Reproduced by `symlink-parent` and
  `parent-1777`: the printed path was the logical one. Fixed: `sv_path` checks, builds in
  and prints the physical path, and `BENCH_SV` keeps it.
- **Security I2** (a group record's identity). Reproduced by `group reuse`: a record
  written before its group started held the lock. Fixed by `bench_lock_untold_reused` on
  group records; mutant `group-reuse-unchecked`. The lock.sh header now describes group
  records.
- **Security I3** (refusals with no test). Added the five `sv_refusals` cases and the
  `parents-ok` and `acl-unread` mutants. Each mutant fails its own case.
- **Spec I1.** `docs/plans/bench-harness.md`'s adapter bullet no longer says status 74 or 1
  to 2 s, and describes the supervisor's bounded run.
- **Spec I2.** The files outside the card's list are named above, each with its reason.
- **Notes.**
  - N1: the dead `stopped` branches are gone. `git grep -e 'HOW=stopped' -e 'stopped)'
    -e 'exit|stopped' -e 'wall, stopped' -- scripts` finds one line only, a lock.sh
    comment ("command stopped)") about the lock run's own 74.
  - N2: the tap tees into the events file; case `tap`.
  - N3: `bench_poll_end` removes only its own file; case `poll-end`.

**The disposition probe** (for the `tee` background job, which is the only new `&` in
the core; no `trap`, `set -m` or process-group change was made). A background job
started as `tee` is, with no job control, under `/bin/bash` 3.2 (3.2.57) and `/bin/sh`,
directly and under `sh -c "trap '' INT; exec ..."`, prints its own dispositions:

```
bash 3.2: INT ignored, TERM default, HUP default
/bin/sh: INT ignored, TERM default, HUP default
bash 3.2 under INT ignored: INT ignored, TERM default, HUP default
/bin/sh under INT ignored: INT ignored, TERM default, HUP default
```

So `tee` ends only at the end of its FIFO's input, which comes once the supervisor has
exited, or at the read-write open in `bench_bounded_tee`.

### Review round 2

The property: after a supervisor's 126 the lock is freed only on positive evidence. That
evidence is the supervisor's `reaped` event (the 126 was the command's own), or a recorded
group seen to be gone. Each finding's case was red on 0e03a682 first:

- **Security I1, and the correctness and spec notes on a 126 before `spawned`** (a 126
  with no group named freed the lock). Fixed: a hand record (`hand=<id>`, a new kind,
  described in the lock.sh header) holds the lock until it is removed by hand, and
  `bench_lock_record` prints one line with the `rm -f` that does it. Case `126 nospawn`;
  mutant `hand-not-recorded`.
- **Correctness I1** (a TERM after `sv_wait` collected 126, before the record, freed the
  lock). Fixed by the trap path using the collected status (`SV_STATUS`), so the tee wait
  and the `awk` forks between the collect and the record no longer matter. Case
  `126 term`; mutant `trap-status-dropped`.
  - What was left (corrected in round 3): inside `sv_wait`, the window runs from its
    `wait` returning up to `SV_PID=`, so it takes in `SV_STATUS=` too, not only the three
    builtins before it. A trap there found `SV_PID` still set, so `sv_stop` sent the
    collected supervisor's pid a TERM and waited for it again; a trap just after
    `SV_STATUS=` overwrote the collected 126 with that wait's status. Under `/bin/bash`
    3.2 the second wait gives the remembered 126. Under `/bin/sh` (bench.sh's shell, so
    its main path) it gives 127 ("not a child of this shell"), which was not taken as the
    supervisor's, so nothing was recorded and the lock was freed while the group might
    still run. Round 3 closes it (see below).
- **Spec I1.** `scripts/gate-checks.sh` is kept and now named, with its reason, under
  "Files outside the card's list".
- **Spec N1.** `tests/check-bench.sh`'s header says 124 with `HOW=wall` for a stopped
  command at its wall time, and so does the same stale comment over its `t_bounded`
  case. Both are comments only.
- **Correctness note 4 (the two graces).** Nothing changed: the outer grace is always at
  least the supervisor's grace plus its after-KILL time.
  - The lock shell does not use `BENCH_STOP_GRACE` itself. It uses `bench_lock_grace`,
    which is `max(2g, g + 5)` for `BENCH_STOP_GRACE` g.
  - The supervisor takes g, then its KILL, then up to its after-KILL time.
    `bench_bounded` passes no `--after-kill`, so that is the 2 s default (`sv_ms` in
    `supervise.c`).
  - So the outer grace is at least g + 5, and the supervisor's stop is at most g + 2:
    at least 3 s of margin for every g, 0 included (5 against 2).
  - The other outer stops (the traps of `pins.sh`, `ios-sim.sh` and `ios-device.sh`, and
    `bench.sh`'s) go through `bench_bounded_stop`, which waits for the supervisor with no
    grace of its own.
- **Not addressed.** Security N1 to N5 and correctness notes 2 and 3 were notes, and
  nothing changed for them.

The counts: 15 supervisor cases (two new, `126 term` and `126 nospawn`) and 9 supervisor
mutants (two new, `trap-status-dropped` and `hand-not-recorded`). The summary line is
"ok (guard, 65 mutants refused, probe, the supervisor cases and their 9 mutants, pyprobe
and its 10 mutants)".

**The disposition probe** (round 2; no `trap`, `set -m` or process-group change in
`scripts/`, but the new `126 term` case relies on a caller's TERM trap under `lock run`).
A Python child under `bench.sh lock run`, as the case's caller runs, prints the
dispositions it inherited. The outer shell is `/bin/bash` 3.2 (3.2.57) or `/bin/sh`,
directly and under `sh -c "trap '' INT; exec ..."`. "handler" is Python's own INT
handler, which it installs only for an INT inherited at its default:

```
/bin/bash plain: INT=handler TERM=default HUP=default
/bin/bash int-ignored: INT=ignored TERM=default HUP=default
/bin/sh plain: INT=handler TERM=default HUP=default
/bin/sh int-ignored: INT=ignored TERM=default HUP=default
```

So TERM reaches the caller's trap in every shape, the merge gate's SIGINT-ignored one
included. The case sends TERM, not INT.

### Review round 3

The last round. Each finding's case or demonstration was red on adeee98e first:

- **Correctness I1 and security I2** (the re-wait under `/bin/sh`). A trap that lands
  after `sv_wait`'s `wait` has reaped the supervisor and before `SV_PID=` waited again,
  and under `/bin/sh` that wait gives 127, so the 126 was lost and the lock freed. The
  window ran up to `SV_PID=`, `SV_STATUS=` included (round 2's note, corrected above).
  Fixed in `bench_bounded_stop`:
  - `SV_STATUS` is tested first. `sv_wait` sets it before it clears `SV_PID`, and
    `bench_bounded` empties it before each run, so from `SV_STATUS=` on the collected
    status is used, and the collected supervisor is not sent a TERM or waited for again.
    A trap anywhere in `sv_wait` sees a consistent pair.
  - Before `SV_STATUS=`, a status the shell cannot attribute is decided by the
    supervisor's events, never by the wait alone. With no `reaped` event, a 127, or any
    status after a `spawned` event, is taken as the supervisor's 126: a group record when
    `spawned` names one, else a hand record. The supervisor never returns 127 itself, and
    a command's own status always comes with `reaped`.
  - Case `sv window` (red: `wait` and `status` with `spawned`, and `wait` with no events,
    gave no record). Mutants `events-unchecked`, `status-after-pid`, and
    `trap-status-dropped` (moved from `126 term`).
  - What is left: a trap before `SV_STATUS=` still sends the collected supervisor's pid a
    TERM (security N3), since nothing tells the shell it was collected there. The window
    is a few builtins with no fork, and the status no longer depends on it.
- **Spec I1 and security I1** (a hand record's `rm -f` printed once). Fixed:
  `bench_lock_untold_live` sets `BENCH_LOCK_HAND`, and the keep note, the waiters' line
  and `lock status` name each hand record with its `rm -f` (see `bench_bounded` above).
  `bench_lock_hand_rm` writes a `'` in the path as `'\''` (security N2 and correctness
  N3). Case `126 nospawn`, extended (red: neither `lock status` nor a waiter's line named
  it); mutant `status-hand-unmarked`. Case `hand quote`.
- **The `endgroup` flake** (security N4 and correctness N4). By trace: the member set its
  TERM ignore itself, `(trap "" TERM; exec sleep 30) &`, racing the supervisor's TERM to
  the group at `exited`. A member that lost the race ended at once, so `group-empty` came
  before the caller's `trap 130`, and the in-order read stopped there with the line the
  reviewers saw. Fixed:
  - The member ignores TERM from its fork on: `trap "" TERM; sleep 30 & exit 0`, the ignore
    set before the `&` and inherited.
  - The mutant is judged only by what it changes: whether the supervisor got a TERM. With
    `sv_any`, `sv_case` reads every line to end of file (`sv_seen`, bound plus grace) and
    wants `signal name=TERM` among them, in any order; no line ends the read early.
  - By construction: in the mutant `sv_term` sends nothing, the case's INT goes to the
    caller's pid alone and its KILLs come after the verdict, so no `signal name=TERM`
    line can be written, and `sv_seen`'s verdict is a function of the set of lines read,
    not their order. Case `sv endgroup order` runs `sv_seen` on the four streams a mutant
    run can give, `trap 130` before and after `group-empty`: one refusal on each. Round
    1's in-order read gave "no line trap 130 (the last line read: group-empty)" on the
    two with `group-empty` first, which is the reviewers' line.
  - The real case keeps its in-order check (FIX195: the trap runs at once, before the
    supervisor's stop ends). With the ignore inherited, `group-empty` can come only after
    the 12 s grace.
  - Runs, with no added load: the case and its mutant 10 times in a row, alternating.
    `endgroup` passed 10 of 10 ("exit 130 in 12 s, nothing left" each time), and
    `sv-no-term` was refused by it 10 of 10, each for "no line signal name=TERM after the
    INT, in any order, before end of file".
- **Not addressed.** Correctness N2 and spec N4 (a reused id replacing a hand record
  through `mv -f`), security N1 (lost events taking a command's own 126 for the
  supervisor's; fails closed) and the confirmations were notes.

**The disposition probe** (round 3; the `endgroup` command's `trap "" TERM` moved from the
member's subshell to the command's shell, and no `trap`, `set -m` or process-group change
in `scripts/`). A member started as each form starts it is sent TERM as soon as its pid is
read, as the supervisor sends its group TERM at `exited`, and is alive 0.3 s later only if
it ignored TERM by then; 20 runs each, under `/bin/bash` 3.2 and `/bin/sh`, directly and
under `sh -c "trap '' INT; exec ..."`:

```
bash-plain old: member alive after an immediate TERM in 20 of 20 runs
bash-plain new: member alive after an immediate TERM in 20 of 20 runs
sh-plain old: member alive after an immediate TERM in 20 of 20 runs
sh-plain new: member alive after an immediate TERM in 20 of 20 runs
bash-int-ignored old: member alive after an immediate TERM in 20 of 20 runs
bash-int-ignored new: member alive after an immediate TERM in 20 of 20 runs
sh-int-ignored old: member alive after an immediate TERM in 20 of 20 runs
sh-int-ignored new: member alive after an immediate TERM in 20 of 20 runs
```

The new form ignores TERM in every shape. The old form did too on this quiet host,
because the probe reads the pid only after the command's shell has exited, which gives the
subshell time to set its ignore; under the supervisor, `exited` comes at once, so the old
form's ignore depended on the subshell being scheduled first. The new form's does not.

The counts: 18 supervisor cases (three new: `sv window`, `hand quote`, `sv endgroup order`)
and 12 supervisor mutants (three new: `events-unchecked`, `status-after-pid`,
`status-hand-unmarked`). The summary line is "ok (guard, 65 mutants refused, probe, the
supervisor cases and their 12 mutants, pyprobe and its 10 mutants)".

Checks run in round 3, standalone from the worktree: `sh -n` and `bash -n` on
`common.sh`, `lock.sh`, `supervise.sh` and `check-bounded-run.sh` (clean);
`tests/check-bounded-run.sh` (the summary line above); `tests/check-supervise.sh` ("ok
(5 s)"); `tests/check-bench-entry.sh` (ok); `scripts/ios-device.sh --selftest` ("ok (65
cases)"); `tests/check-doc-links.sh` and `tests/check-runbook-index.sh` (ok).
`tests/check-bench.sh` and the gate go to the orchestrator's hold.

## What remains

- **Slice 3: the lock run and the deletions.** `bench_lock_run` through `sv_run`, and the
  supervisor's parent-death watch. Today's callers of what slice 3 deletes, outside
  `common.sh` and `lock.sh`, as `git grep` shows them:
  - **`bench_spawn`, `bench_spawn_running`, `bench_await`:** `scripts/ios-sim.sh` (the
    simulator launch and its log stream, lines 1638 to 1809). `ios-sim.sh` moves in slice 3.
  - **`bench_poll_end`, `bench_lock_stop_child`:** `scripts/bench/bench.sh`'s
    `bench_on_signal`.
  - **`bench_stop_groups`:** named in comments in `scripts/lib/bounded.py`.
  - **The rest** (`bench_poll*`, `bench_spawn_id`, `bench_spawn_read`,
    `bench_spawn_forget`, `bench_spawned`, `bench_watch*`, `bench_group(s)_alive`,
    `bench_signal_group`, `bench_end_group`, `bench_proc_read`, `bench_proc_same`,
    `bench_untold`, `BENCH_SPAWNED`, and in `lock.sh` `bench_lock_hold_untold` and the
    start-time half of `bench_lock_untold_*`): only `common.sh`, `lock.sh` and the tests that
    exercise them (`check-bench`'s `t_core_id`, `t_cant_tell`, `t_poll_end_reaped`, and
    `check-bounded-run`'s nap case).
  - **Kept:** `bench_nap` and `bench_now`, for `snap.sh`.
