# The lock shell's fork said "child setpgid" on the caller's standard error (FIX83)

During TASK155's rework the full gate failed once, under load, with only
`tests/check-bench.sh` red, in `t_lock_stop`: "stop: the lock shell printed
a line that is not its own (a job notice)". The check did not say which
line. The plan anchor is the bench plan's
[host lock section](../plans/bench-harness.md#the-host-lock-one-heavy-job-at-a-time).
No engine ran for this card, so none of the shared runbook's engine method
applies; every run here held the host lock, as
[TASK155](TASK155.md) has every heavy entry point do.

**Verdict.** The line was not a job notice: it was bash's
`<core>/common.sh: child setpgid (<pid> to <pid>): Operation not permitted`,
printed by the lock shell's own fork of the command (`bench_spawn`) on the
caller's standard error. With job control on, bash sets the new process
group in the parent and in the child; under load on macOS the child's
`setpgid` can fail with EPERM while the parent's makes the group, and bash
says so on the standard error the child has before the command's
redirections. A loop of `t_lock_stop` beside a CPU load showed it within
its first 20 runs, about a minute. `bench_spawn` now forks with standard
error on `/dev/null` and gives the command the caller's back: 203 runs of
`t_lock_stop` under the same load then passed, and of about 2,100 lock
runs under load each side, the core before the fix printed the line twice
and the fixed core never. The test's fixture could print a job notice of
its own into the same files (never seen in 2,872 tries); it is silenced,
and a probe shows it is.

## What was run

The host: 12 cores, bash 3.2.57 as `/bin/sh`. The load: one `yes >
/dev/null` per core (`sysctl -n hw.ncpu`), started and stopped by pid by the
runner itself, so it ended with each chunk; on top of it four lanes of
`tests/check-bench.sh t_lock_stop` at once, each run with a wall bound of
120 s, stopping at the first failure or at 200 runs in all. The gate that
failed ran six checks at a time. Each chunk was one `heavy.sh` call (the
host lock) of at most 10 minutes. The 1-minute load average is the
`sysctl -n vm.loadavg` value at the start and the end of each batch.

| Batch | Tree | Runs | Failures | Load (1 min) |
|---|---|---|---|---|
| 1 | `d73e960d` (main's core) | 20 | 1 (`left.err`, below) | 15.65 to 14.66 |
| 2 | `a4a3a37e` (the fix) | 108 | 0 | 19.19 to 16.01 |
| 3 | `a4a3a37e` | 95 | 0 | 2.07 (before the load started) to 15.32 |

The first chunk stopped at its failure; the other two stopped at 203 runs
in all (lanes that were running at 200 finished). A run took 11 s before
the fix and 14 s after it (the probe added at the start of `t_lock_stop`
waits 1 to 2 s for the lock to stop its leftover sleep).

Two smaller probes ran in the same locked chunks, beside the same load:

- the test's fixture `gc.sh` as the lock runs it (the leader of its own
  process group, here by `set -m`), ended by one TERM to its group, over and
  over for 40 s, as it was and with its standard error on `/dev/null`:
  1,400 and 1,472 runs, no line from the fixture in either (load 2.25 to
  11.31, then to 15.65). The probe's own shell, which starts each `gc.sh`
  with `set -m` as the lock does, printed `child setpgid (<pid> to <pid>):
  Operation not permitted` three times in those 2,872 starts;
- `bench.sh lock run sr -- true` over and over in four lanes for 60 s, over
  the core before and after the fix, counting the lines on the lock
  shell's standard error that are not its own: before, 2,085 runs and two
  lines, both `<core>/common.sh: child setpgid (<pid> to <pid>): Operation
  not permitted` (load 2.25 to 15.00); after, 2,121 runs and none (load
  15.00 to 19.19).

## What was seen

The failing run (`d73e960d`, the check that names its lines, over main's
core) printed:

```
check-bench:   left.err: <core>/common.sh: child setpgid (31410 to 31410): Ope
check-bench: stop: the lock shell printed a line that is not its own (a job notice), above
```

(the line is cut at 200 characters by the check; `<core>` stands for the
path of `scripts/bench/core/`). Every other check of that run passed: the
command's group was made, its leftover work was stopped and said, and the
lock was free.

## The cause, by reading

Every place the lock shell can print a job or status line was traced
(bash 3.2's `jobs.c`, `make_child` and `notify_of_job_status`):

- **A notice of its own job.** A non-interactive bash prints `<script>:
  line <n>: <pid> Terminated: 15 <command>` for a job ended by a signal
  other than INT that it does not trap, at the next wait for a foreground
  command. The lock shell traps TERM, INT and HUP (and an ignored signal,
  as in `bench_on_signal`, counts as trapped), so its command ended by TERM
  gives no notice. KILL is sent only in `bench_stop_group`, whose standard
  error is `/dev/null` and which reaps the command itself; `bench_watch`,
  the poll in which an exit is first seen, has its standard error on
  `/dev/null` too, and so has the `wait` after it. `t_lock_tokens`
  (`killed.err`) and `t_bounded` already show no notice for a command ended
  by KILL.
- **Stopped.** With job control off after the `&` (`set +m`), bash waits
  without `WUNTRACED` and never sees the stop, so it prints nothing; the
  lock shell reads the stop with `ps`.
- **`set -m` and `set +m`** print nothing in a non-interactive shell.
- **The fork under `set -m`.** `make_child` calls `setpgid(child, child)`
  in the parent and in the child (both, as the POSIX rationale recommends,
  so the group is there whichever runs first); only the child reports a
  failure, with `sys_error ("child setpgid (%ld to
  %ld)")`, on its standard error. The child of an asynchronous command
  forks before the command's redirections are made, so that standard error
  is the lock shell's own, the caller's. This is the line seen.
- **An early read.** The three files are read after the lock shell has
  exited (waited for) and its command's group is gone (`bench_stop_group`
  and `bench_end_group` return only then, or after KILL), about 10 s after
  the last of the three ends. Nothing writes to them by then.
- **The fixture itself.** `gc.sh` (a script) and its `sh -c` print the
  same kind of notice when their foreground child is ended by TERM while
  they live on: a probe that TERMs the grandchild alone shows
  `<tmp>/gc.sh: line <n>: <pid> Terminated: 15 sh -c '...'`. A TERM to a
  process group reaches its members one after the other, so under load the
  fixture could print one into the lock shell's output. It was never seen
  in 2,872 group TERMs under load, but nothing rules it out.

## The fix

**The fork's standard error is `/dev/null`** (`bench_spawn`,
`scripts/bench/core/common.sh`): both forms of the fork (standard input
open, through fd 3, and closed) run inside a group whose standard error is
`/dev/null`, with the caller's saved in fd 9; the command gets it back
(`2>&9 9>&-`), so it has the caller's standard error, fd 9 closed, and fd
3 closed when standard input is open (as before). When the caller's standard error is closed, the lock shell's own is
made `/dev/null` first (its lines went nowhere anyway), so the command
still starts and has `/dev/null` for it. No trap, signal disposition,
process group or `set -m` changed.

**`t_spawn_stderr`** (`tests/check-bench.sh`) shows it without load: with
xtrace, bash prints the trace of the command in the forked shell, on the
standard error it has before the command's redirections, the same one the
`setpgid` line uses. With standard input open and closed, that line must
not reach the caller (and the lock shell's own call of `bench_spawn` must
be in the trace, so the probe sees something). The command must get the
caller's standard error, fds 3 and 9 closed, and must run with `/dev/null`
for a closed standard error. Over main's core it fails all four; three
mutants fail it: `fork-stderr` and `fork-stderr-nostdin` (either form
forked with the caller's standard error) and `fork-stderr-closed` (no
`/dev/null` for a closed one, so the command never starts).

**The fixture is silent.** `gc.sh` starts with `exec 2> /dev/null`
(`GC_QUIET`), so its shells cannot print into the lock shell's output; a
probe at the start of `t_lock_stop` TERMs its grandchild under the lock and
requires `gc.sh`'s exit status 143 and no line in that output. The
`gc-notice` mutant clears `GC_QUIET` and fails it with the fixture's own
notice.

**The check names what it finds.** `foreign` prints the lines that are not
the lock shell's own, at most five, each cut to 200 characters, after the
name of their file, with the check's temporary directory as `<tmp>`.

## Checks

Each through the host lock, on the final tree, without load:
CHECK_RESULTS

## Commits measured

- `4364987e`, main with TASK155: the core before the fix;
- `d73e960d`: the check names the lines it finds (the core unchanged), the
  tree of the failing run and of the pre-fix probes;
- `a4a3a37e`: the fix, its tests and mutants, the tree of the post-fix runs;
- `78ca72e1`: comments, and the fd probe of `t_spawn_stderr` given
  `/dev/null` for standard input (no change to `t_lock_stop` or to what
  the core does), the tree of the checks.
