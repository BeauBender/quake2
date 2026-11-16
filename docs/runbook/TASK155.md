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
and its engine, going with the lock held. Stop an entry point with TERM,
INT or HUP and send KILL only after the lock's grace (twice
`BENCH_STOP_GRACE`, at least `BENCH_STOP_GRACE` + 5 s). The MetalFX
report generator (`scripts/metalfx-report/mfx.py`), the one caller in the
tree that stops a `pins.sh` process group, now does so: on its timeout, a
SIGTERM or SIGHUP, or Ctrl-C it sends TERM to the group, waits up to the
lock's grace plus 5 s (25 s at the defaults, read from `BENCH_STOP_GRACE`
as the lock reads it), then sends KILL, with a second signal held until
the stop is over. The repository's stop hook stops process groups too,
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
Twenty-four mutants, each caught: the lock call removed from the gate,
from coverage and from each of the eight pins commands, the run under the
lock taking the lock again (the gate, `pins.sh`), a `BENCH_LOCK_ENTRY`
without a token taken as a hold, an unset `LC_ALL` not given back (the
gate, coverage), and, for each of the three scripts, no state directory
taking the lock, `CI=true` alone turning it off and a silent lock-off.
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

On the bare-metal host, each run alone under the host's timing lock:
`tests/check-bench-entry.sh`, mutants included, passed in under a
minute, `tests/check-bench.sh` in about 2 min, and
`tests/check-metalfx-report.sh` (89 mutants) in about 6 min; `make test`
passed (163 tests). The full gate passed in 707 s under its own hold (a
temporary `BENCH_STATE`, empty again afterwards; no lock-off line, and the
pin selftest passed straight through), with a load of 2.9 before and 2.2
after.
