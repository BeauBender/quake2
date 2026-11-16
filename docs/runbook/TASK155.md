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

## CI

The lock is off on CI (`CI=true`), as it is with `BENCH_LOCK=0` and in a
tree without `scripts/bench/bench.sh`. A GitHub runner runs one job at a
time, with no lane beside it to keep quiet, so the lock would add a
process group and a state directory and protect nothing. The scripts read
`CI` themselves; the workflows are unchanged.

## The check

`tests/check-bench-entry.sh` (in the gate) copies each entry point into a
throwaway tree beside a stub `bench.sh` and stub `make`, `cc` and `gcov`
that log the token they ran under, then, with the real core, runs the gate
nested: it holds the lock once while the pin selftest it runs passes
straight through, it passes straight through under an outer holder, and it
gives up with 75 and runs nothing while another job holds the lock.
Thirteen mutants, each caught: the lock call removed from the gate, from
coverage and from each of the eight pins commands, the run under the lock
taking the lock again (the gate, `pins.sh`), and a `BENCH_LOCK_ENTRY`
without a token taken as a hold. Against the entry points before this
card, the gate, coverage and pins tests fail, each with "not one call of
bench.sh".

The whole check, mutants included, ran in 21 s on the bare-metal host;
`tests/check-bench.sh` in 83 s; `make test` passed (163 tests).
