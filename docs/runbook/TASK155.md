# Every heavy entry point takes the bench lock itself (TASK155)

A lock that callers must remember is a lock someone forgets: on
2026-11-15 three merge-queue gates ran beside two timing lanes. Now the
gate, the pin harness and coverage take the host lock of the bench
harness themselves. The rules are in the plan's
[host lock section](../plans/bench-harness.md#the-host-lock-one-heavy-job-at-a-time);
this card measured nothing on the GPU, so no method section of the shared
runbook applies.

## What takes the lock

| Entry point | Label | Held for |
|---|---|---|
| `scripts/test.sh` | `gate` | the whole gate: every check and both builds, one hold |
| `scripts/coverage.sh` | `coverage` | the instrumented build, the run and the report |
| `scripts/pins.sh record`, `compare`, `replay`, `footprint`, `where`, `make-demo`, `selftest`, `evidence` | `pins-<command>` | the command; a dry run (`PINS_DRYRUN`) takes none |
| `scripts/metalfx-report/mfx.py run` (`scripts/metalfx-report.sh run`) | `metalfx-report` | the whole report run after its argument checks: the build check, every timed engine run and every `pins.sh record`, which passes straight through |

Each one runs itself again under `scripts/bench/bench.sh lock run
<label>`. A caller that holds the lock already passes straight through by
its token, so the gate running the pin selftest, or any entry point under
a host's own wrapper, does not wait for itself. The state directory is
`BENCH_STATE`, else the core's default; no default changed.

## When the lock is off

The lock is off with `BENCH_LOCK=0`, on GitHub Actions (`CI=true`
together with `GITHUB_ACTIONS=true`), in a tree without
`scripts/bench/bench.sh`, and with no directory for its state
(`BENCH_STATE`, `XDG_CACHE_HOME` and `HOME` all unset, where `bench.sh`
would refuse the run with 2). Each time, the script says so in one line
on stderr that names the reason, for example
`test.sh: the host lock is off: no state directory (BENCH_STATE, XDG_CACHE_HOME and HOME unset)`,
and runs as before.

CI does not take the lock: a GitHub runner runs one job at a time, with
no lane beside it to keep quiet, so the lock would add a process group
and a state directory and protect nothing. The scripts read the two
variables themselves; the workflows are unchanged. Both are required
because GitHub Actions, the only CI here, sets both, while many host
tools export the generic `CI=true` alone: on this host that no longer
turns the lock off.

## Stopping an entry point

The run under the lock is in a process group of the lock's own, so a
KILL to the caller's group ends the lock shell alone and leaves the run,
and its engine, going with the lock held. The gate is the exception (see
the merge with FIX72, below): its run under the lock sees its lock shell
go within a second and stops itself. Stop an entry point with TERM,
INT or HUP and send KILL only after the lock's grace (twice
`BENCH_STOP_GRACE`, at least `BENCH_STOP_GRACE` + 5 s). The MetalFX
report generator (`scripts/metalfx-report/mfx.py`), the one caller in the
tree that stops a `pins.sh` process group, now does so: on its timeout, a
SIGTERM or SIGHUP, or Ctrl-C it sends TERM to the group, waits, then
sends KILL, with a second signal held until the stop is over. Under its
own hold (review round 2, below) the wait is `BENCH_STOP_GRACE` (10 s at
the default), so that its KILL lands before the lock shell's KILL of the
generator; called without one, the lock's grace plus 5 s (25 s at the
defaults, read from `BENCH_STOP_GRACE` as the lock reads it). The repository's stop hook stops process groups too,
TERM first, but its checks build nothing and run no entry point.

## The checks

`tests/check-bench-entry.sh` (in the gate) copies each entry point into a
throwaway tree beside a stub `bench.sh` and stub `make`, `cc` and `gcov`
that log the token they ran under, then, with the real core, runs the gate
nested: it holds the lock once while the pin selftest it runs passes
straight through, it passes straight through under an outer holder, and it
gives up with 75 and runs nothing while another job holds the lock. Each
off reason takes no lock and says so in its one line; `CI=true` alone,
`GITHUB_ACTIONS=true` alone, and `BENCH_STATE` or `XDG_CACHE_HOME` alone
leave the lock on. The caller's `LC_ALL`, unset or set, comes back under
the real lock for the gate and for coverage. Every pid the check waits for
leaves its cleanup list, which must be empty after the tests.
Twenty-nine mutants, each caught: the lock call removed from the gate,
from coverage and from each of the eight pins commands, the run under the
lock taking the lock again (the gate, `pins.sh`), a `BENCH_LOCK_ENTRY`
without a token taken as a hold, an unset `LC_ALL` not given back (the
gate, coverage), `GATE_PARENT` naming the lock shell, the four of the
merge with FIX72 (below), and, for each of the three scripts, no state
directory taking the lock, `CI=true` alone turning it off and a silent
lock-off.
Against the entry points before this card, the gate, coverage and pins
tests fail, each with "not one call of bench.sh"; against those of the
first review round, the off-reason tests fail (no line, and with no state
directory a call of bench.sh), and without the cleanup-list fix the list
check fails.

`tests/check-metalfx-report.sh` runs a stub `pins.sh` as the command of
the real `bench.sh lock run`, with a private `BENCH_STATE`, a grandchild
of the lock shell standing in for the engine and `BENCH_STOP_GRACE=1`. A
timeout, a SIGTERM and a Ctrl-C to the generator must each leave nothing
running and the lock free. Against the generator before this round, which
sent KILL first, all three fail: the stub and its grandchild run on and
the lock stays held (`lock=held label=pins-record ... alive=no`). A
KILL-first mutant fails the case.

## Review round 2

Two findings, both in the report generator, `scripts/metalfx-report/mfx.py`:

- Each `pins.sh record` took the lock on its own, inside the record's
  900 s limit, so a wait for another holder could time the record out
  before `pins.sh` had started. The generator now runs itself again as one
  hold of `bench.sh lock run metalfx-report` (table above), the way the
  entry points do, with their off reasons and line, and with the caller's
  `LC_ALL` given back as the gate does. Each record passes
  straight through, its limit counts the record alone, and the timed
  engine runs are under the same hold. Under that hold the generator is
  the lock shell's command and `pins.sh` is its own child, so the
  generator's stop of `pins.sh` waits `BENCH_STOP_GRACE`, which lets its
  KILL land before the lock shell's KILL of the generator.
- Nothing tested the hold of a second signal during the stop.

`tests/check-metalfx-report.sh` now has the lock cases in a group of
their own (`lock`). The new cases: a holder (`gate`) holds the lock for
10 s while the generator runs with a record limit cut to 4 s, and the run
must pass, with both records and every engine run seeing the holder
`metalfx-report`, and the engine the caller's `LC_ALL`. Each off reason must give its one line. A second
SIGTERM sent to the generator's process 1 s after the first, with a stub
that ignores TERM (`TERM2`), must leave the generator running 1 s later
and nothing running at the end. A SIGTERM to the lock shell with that
stub (`deaf`) must leave nothing running and the lock free. Against the
generator of round 2, the waiting case fails (rc 1, the record timed out,
the engine saw `label=gate`), and so do the off lines and the wait under
the hold. The mutants `stop-unheld` (the signal hold gone),
`stop-wait-outlives-lock` (the longer wait under the hold), `lock-self-off`,
`lock-lc-all-lost` and `lock-off-silent` are each caught.

On the bare-metal host, each run alone under the host's timing lock:
`tests/check-bench-entry.sh`, mutants included, passed in under a
minute, `tests/check-bench.sh` in about 2 min, and
`tests/check-metalfx-report.sh` (89 mutants) in about 6 min; `make test`
passed (163 tests). The full gate passed in 707 s under its own hold (a
temporary `BENCH_STATE`, empty again afterwards; no lock-off line, and the
pin selftest passed straight through), with a load of 2.9 before and 2.2
after.

## The merge with FIX72's parallel gate

The delta review of the merge with FIX72 found a Blocker: with the lock
on, a KILL to the gate's process group reached the lock shell alone. The
run of `test.sh` under the lock leads a group of its own, and FIX72's
`GATE_PARENT` names that run, so the scheduler and every check ran on,
the lock stayed held (`alive=yes`), and both `make -B` builds ran after
the caller had seen 137. FIX72's own KILL test passed because its trees
have no bench core, so the lock is off there.

The fix is in `scripts/test.sh` alone; `scripts/gate-checks.sh` and its
`GATE_PARENT` contract are unchanged. The run under the lock (the
`BENCH_LOCK_ENTRY=gate` marker with a token) starts a watcher, a small
`sh -c` in a process group of its own with no terminal and no output,
that polls every 0.5 s. Once `ps` shows that the run's parent is no
longer the lock shell (the run was given to another parent) while the run
is still the watcher's own parent, it sends TERM, once, to the run's
process group. Before the makes the run's trap stops the gate as on any
TERM (`gate_stop`: the scheduler and every check end, the scheduler's
10 s grace included), with the line `test.sh: the lock shell (pid N) is
gone; ending every check`; during a make, the make ends with the run.

The watcher cannot leak. It leads its own group, so it is in no check's
group (the scheduler starts each check as a group leader), not the
scheduler's and not the run's: a stop of any of those neither reaches it
nor waits on it. The run stops it on every way out: `gate_stop`, and an
EXIT trap, which the shell runs on a normal exit, on an error under
`set -e` and on a signal the run does not trap; each sends TERM to the
watcher's group (the watcher and its `sleep`) and waits for it, and the
watcher ends on it with status 0. It never ends on its own while the run
lives, so its pid stays taken until the run has stopped it. Only a KILL
of the run itself skips the EXIT trap; the watcher then sees its own
parent change and exits within one poll. It signals nothing but the run's
group, and only once `ps` has shown both facts; a `ps` that fails answers
nothing, and it asks again at the next poll.

The review's second point: on a TERM or INT to the lock shell, the lock
gives the run twice `BENCH_STOP_GRACE`, at least `BENCH_STOP_GRACE` + 5 s,
before it KILLs the run and frees the lock, and with a grace of 4 or
less that was shorter than the scheduler's 10 s, so a check that ignores
TERM ran on after the lock was free. The gate now gives its lock shell a
`BENCH_STOP_GRACE` of at least 10 (a lock grace of 20 s), and the run
under it the caller's value back, as it does `LC_ALL`.

`tests/check-bench-entry.sh` has two new tests with the real bench core.
`t_kgrp` KILLs the gate's process group while a check sleeps: the check
and the scheduler must be gone within seconds, no make may run, the
lock must be free or stale (`alive=no`), and the gate must say why it
stopped; then it KILLs the group during the first make (a slow stub
`make`), which must end, with the second make never run. `t_grace` sends
TERM to the lock shell with `BENCH_STOP_GRACE=1` and a check that
ignores TERM: once the lock shell has exited, the check and the
scheduler must be gone and the lock free, and the check must have seen
`BENCH_STOP_GRACE=1`. `t_nested` now also finds the watcher among the
run's children: one, the leader of its own group, gone once the gate has
returned. Four new mutants are caught: no watcher (`t_kgrp`, the check
outlives the KILL), a watcher that sends TERM to the run alone and not
its group (`t_kgrp`, the make outlives it), the lock shell's grace not
raised and the caller's not given back (`t_grace`). Against the
`test.sh` of the merge (`58461f7f`), all three tests fail: in `t_kgrp`
the check, the scheduler and the make outlive the KILL and no reason is
given, in `t_grace` the check and the scheduler outlive the lock shell,
and in `t_nested` there is no watcher.

Each run alone under the host's timing lock: `tests/check-bench-entry.sh`,
29 mutants included, passed in under 2 min, and again as a background job
with SIGINT ignored; `tests/check-gate-parallel.sh` passed (21 cases, 57
mutants; with SIGINT ignored, 55 and 2 skipped), as did
`tests/check-bench.sh`, `tests/check-doc-links.sh`,
`tests/check-no-local-identity.sh` and `make test` (173 tests). The full
gate, under its own hold with a temporary `BENCH_STATE` (empty again
afterwards, no lock-off line), passed in 414 s, its 33 checks in 410 s,
6 at a time. A first run before it failed one check, `check-bench.sh`, on
its `t_lock_stop` assertion that the lock shell prints no job notice: a
self-test of the bench core, which this change does not touch. It passed
alone just before and in the second run.
