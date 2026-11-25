# 0016. Harness process control lives in one native supervisor

Status: decided by the owner, 2026-11-25. The owner asked for the root-cause
work behind this record, and for the cards it produced to be carried through
to merge; that request is the sign-off, and it covers the change to the
harness's language rule below.
Plans: [the bench harness plan](../plans/bench-harness.md) ("Language",
superseded in part by this record). Card: #752.

## Context

The process-control and flake class has taken a large share of the loop's
work:

- 49 cards (43 of them FIX) and 51 pull requests, about 27k changed lines
  with the foundations (TASK153, TASK154, TASK155, TASK166, FIX72 and FIX60,
  about 11.6k lines of those);
- 60 of the 347 review rounds;
- 8 of the 33 failed workflow runs since 11-15, all red main runs of the
  harness self-tests: 44998168941, 45010707695, 45337201551, 45547293056,
  45415672331, 45582643870, 45621239929 and 44704990185. The rest of the 33
  were runner-queue cancels, network failures, `check-runbook-index`
  conflicts and other runs.

Three facts point at one cause.

1. [The bench harness plan](../plans/bench-harness.md) chose "POSIX `sh` plus
   `awk`, safe on bash 3.2", so the core could later move to other
   repositories. The core is also exercised under dash. The goal has not been
   needed, and no Apple host runs the core under dash.
2. Shell cannot own the reap: bash reaps a child as soon as it exits, and
   dash reaps it in the next `wait`. So the core identifies processes with
   `ps`, and every sampling window became a card. The card chain is FIX171,
   FIX172, FIX177, FIX179, FIX181, FIX184 and FIX192, then FIX196, #741 and
   #726, which FIX192's background-job polling in `bench_poll` produced.
   The PRs of the identity chain (FIX106, FIX171 twice, FIX172, FIX177,
   FIX179, FIX181 and FIX184) are about 6.1k changed lines; the shell
   signal-semantics PRs (12 of them) are about 3.1k; the primitives written
   several times over, in shell and in `scripts/lib/bounded.py`, about 4.9k.
3. The self-tests assert those windows with real processes, `sleep` and `ps`
   polls, and about 330 mutants re-run them on every gate. A timing case can
   fail both ways: the real code fails, or a mutant survives by luck (#470).

Two decisions changed the premises. [Decision 0014](0014-no-performance-gating.md)
makes performance information, not a gate. TASK166 (#424) makes each timing
run prove its own quiet. So a rare missed exclusion by the bench lock now
costs a measurement that refuses itself. It does not cost a wrong result.

Three open cards harden code that the supervisor stack deletes: #726, #741
and #723.

## Decision

1. **The supervisor.** The target is that all spawning, bounding, signalling and reaping of
   child processes in the harness goes through one native supervisor,
   `scripts/lib/supervise.c` (#755), carried out by the migration cards below,
   built with the `cc` the harness already
   uses for its other helpers. It starts the command as the leader of its own
   process group, owns the `waitpid`, keeps the leader unreaped until the
   group is empty, waits on kqueue events instead of polling `ps`, sends
   TERM, a grace period, then KILL to the group, forwards INT, TERM and HUP,
   and exits as `timeout(1)` does. Harness shell does not `trap` for a child,
   `kill` a child, or `wait` on a child pid outside the supervisor's wrapper.
2. **The shells.** The bench core supports `/bin/bash` 3.2 (also as `sh`) as
   its caller shell. Dash and the paths for a shell without job control are
   dropped. The dash cases and paths stay until the migration card that
   removes them lands. The goal of moving the core to other repositories is withdrawn,
   and can come back later with the supervisor.
3. **The lock.** The lock's identity and stale-reclaim code goes when #758
   lands, and the bench lock's guarantee rests on the per-run quiet evidence
   of TASK166. This record does not change the lock's acquire or queue
   contract. Any such change, a kernel lock on an inherited file descriptor
   included, is decided later on #760, with its own amendment to this record.
4. **Self-tests.** Harness self-tests synchronise on the supervisor's events
   or on the shared test helpers (#758). They do not use wall time, `sleep`
   or `ps` state.
5. **The freeze.** Until the migration cards below land, no new FIX card is
   filed to harden `bench_poll`, `bench_spawn`, `bench_watch`,
   `bench_stop_groups`, `scripts/lib/bounded.py`, the lock's identity code or
   the `reap` and watchdog of `tests/check-image-decoder.sh`.
   Review notes and flakes on that code are added to the replacement card. A
   red main run on the frozen code is re-run once. A repeat is recorded on the
   replacement card, with the run id. The gate is never skipped or weakened
   for it, and a repeat that blocks the gate falls under the exception below.
   The exception: a security defect, a data-loss or lock-safety defect, or a
   defect that leaves the gate or the bench lock unusable is fixed in place as
   usual. Its card names the replacement card that will retire the code.

## The migration cards

| Card | What it does |
|---|---|
| #755 | the supervisor itself, with an event stream and a test clock |
| #758 | the bench core and the gate's three check-runner functions go through it; `bench_poll` and the shell clock and identity reads go; it absorbs the self-test helpers (#756) and the lock's run path (#759), in two PRs, and closes #726 and #741 |
| #762 | `scripts/lib/bounded.py` becomes a wrapper; it needs no pty mode and closes #723 |
| #770 | `check-image-decoder` bounds its runs through it |
| #764 | the local gate selects harness checks the way CI does |

## Cards closed

| Closed | Why |
|---|---|
| #756 | merged into #758 |
| #759 | merged into #758 |
| #757 | the pty mode is not needed |
| #754 | the host contract already exists in a lighter form |

## Cards parked

Hardening cards that a migration card deletes stay open and unworked; their
findings move to the card named.

| Parked | Closed by | Why |
|---|---|---|
| #726 (the end-group and stop-group clock reads) | #758 | the clock reads and `bench_poll` are deleted |
| #741 (a `bench-poll` scratch file left by `sign-int-hang`) | #758 | the poll directory goes with `bench_poll` |
| #723 (signal holds in `run_engine` and `bounded.py`) | #762 | `bounded.py` becomes a wrapper |

Three migration cards carry `gate:manual:replan`. They are re-planned with
measured evidence after #758 and #764.

| Parked | Why |
|---|---|
| #760 (the kernel lock) | the lock's contract is decided there, after #758 |
| #761 (the gate's parent watcher and tty cases) | measured need first |
| #763 (the other scripts) | measured need first |

## What it costs

- The harness needs a built binary. It is built with plain `cc` before the
  first use, and a failed build fails the gate closed; there is no fallback
  to the shell path.
- C adds a memory-safety review class to the harness. The program has no
  untrusted input beyond its arguments and environment.
- The shell and dash portability of the bench core is gone.
- The lock no longer reclaims a stale holder by identity once #758 lands. A
  missed exclusion is possible but rare, and the run that suffers it refuses
  itself.

## How to reverse it

The supervisor lands alone and each caller moves in its own card, so a
migration card is reverted on its own. Withdrawing the record means a new
record that names this one.
