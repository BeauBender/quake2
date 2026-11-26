# TASK221: the gate's check runner goes through the supervisor (slice 1)

TASK221 (#758) moves the bench core and the gate's check runner onto the native
process supervisor (TASK218, `scripts/lib/supervise.c`), and then deletes the
shell supervision and the ps identity layer. This first slice moves the gate's
check runner. The bench core and the deletions are left for the slices after
it (see "What remains").

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
  entry is still in `_grun`. `gate_end` therefore sends no TERM to a supervisor that
  `bench_pid_alive` says is gone. That narrows the window in which the pid could be given to
  another process from up to one poll (0.2 s) to the time between that test and the `kill`.
  This was chosen over dropping finished entries before the stop because dropping them needs the
  same liveness test, only earlier and further from the `kill`. The `wait` stays: it gives a
  reaped job's status, or fails at once for a pid that is no job. A signal that arrives between
  the fork and the pid's entry in `_grun` finds the pid as `$!`: `gate_start` marks the window
  (`_gss`, `_gsb`), and `gate_cleanup` adds that pid.
- **The supervisor's build.** `sv_path` builds the supervisor into the tool cache on first use.
  If the build fails, the run fails with status 2 before any check starts.

The scheduler still sources `bench/core/common.sh`, for `bench_pid_alive` only.

### Behaviour that changed

- A stop that has begun now ends even when the scheduler is killed during it. The supervisor,
  not the scheduler, runs the grace and the KILL. This is the new `killgrace` case.
- A check now gets SIGINT at its default even when the scheduler has SIGINT ignored, as the
  merge gate starts it. The supervisor's contract is that the command gets every signal at its
  default. The `sigs` case now compares a check run by `test.sh` with a direct start that has
  every signal at its default.

## The check

`tests/check-gate-parallel.sh` copies `scripts/lib/supervise.sh` and `supervise.c` into each
staged tree. It builds the supervisor once up front and exports that tool cache as
`SUPERVISE_CACHE`, so a case that sets `HOME` elsewhere (`ttymsg`) still finds it.

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
| `grace-unpassed` | `interrupt` | the supervisor is not given `gate_grace`: check `b` ignores SIGTERM and its sleep is now 60 s, longer than the case's 30 s bound (it was 30 s, so it ended by itself inside the bound and the mutant survived) |
| `stderr-closed-unguarded-sched` | `stopnoerr` | the scheduler leaves a closed standard error closed. The stops no longer copy fd 2 (the supervisor opens a closed descriptor on `/dev/null` itself), so the case now also starts the scheduler with its standard error closed and a bad `GATE_JOBS`: status 2 and no check run. Without the guard, the write of the error line fails under `set -e` and the status is 1 |
| `no-own-group` | `kgrp` (was `sigs`) | `test.sh` starts the scheduler without job control. The SIGINT and SIGQUIT a check gets no longer depend on that: its supervisor resets every signal, so `sigs` passes with this mutant (`no-supervisor-sigs` covers the dispositions). What the scheduler's own group still does is keep it and the supervisors, its background jobs, out of `test.sh`'s group: without it, `kgrp`'s SIGKILL to that group kills them all, and the check, in a group of its own, outlives the run. It is no longer SIGINT-only, so it also runs when SIGINT is ignored |
| `no-stop` | `stop` | `gate_end` sends no TERM |
| `no-stop-wait` | `interrupt` | `gate_end` does not wait for the supervisors |
| `group-kill-dropped`, `no-kill-after-grace`, `no-kill-after-grace-stop`, `no-cont` | `interrupt`, `stop` | `cmutant` builds a supervisor from a changed `supervise.c` into a cache of its own: the signal goes to the leader alone, the KILL is signal 0, the CONT is dropped |

The mutants `no-process-group`, `no-end-at-exit`, `ttou-to-checks`, `ttou-after-start`,
`start-stderr-on-tty` and `drop-emptied` named lines that are gone.

## Review notes carried from TASK218

- `sv_safe`'s ACL and symbolic link gap in the cache directory check: `scripts/lib/supervise.sh`
  is not changed in this slice. It is left for the slice that adds `sv_run`.
- `sv_count`'s relisting is unbounded and ignores the deadline: this is in `supervise.c`, which
  is outside this card's files.
- The invariants job has no `timeout-minutes`: this is in `.github/workflows/`, which is outside
  this card's files.

## What remains

- **Slice 2: the bounded path.** `sv_run` in `scripts/lib/supervise.sh`; `bench_bounded`
  through it, with `bench_stop_group` and `bench_deferred` as shims; the #726 and #741 cases.
  Two things make it larger than it looks:
  - `bench_bounded`'s callers pass a shell function (`bench_to`), which the supervisor cannot
    run.
  - Its staged trees (the `ios-device.sh` self-test, `check-bench`'s dash runs) copy
    `common.sh` alone.
- **Slice 3: the lock run and the deletions.** `bench_lock_run`, the untold and hold code in
  `lock.sh`, and the deletions in `common.sh`. `scripts/ios-sim.sh` still calls `bench_spawn`,
  `bench_spawn_running` and `bench_await`, and is outside the card's files. So `bench_spawn`
  and the identity layer cannot all go until that file moves too.
- **The one-time measurement** of `check-bench`, before and after, belongs to slice 2.
