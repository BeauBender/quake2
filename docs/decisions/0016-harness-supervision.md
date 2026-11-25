# 0016. Harness process control lives in one native supervisor

Status: decided by the owner, 2026-11-25. The owner asked for the root-cause
work behind this record, and for the cards it produced to be carried through
to merge; that request is the sign-off, and it covers the change to the
harness's language rule and to the lock's contract below.
Plans: [the bench harness plan](../plans/bench-harness.md) ("Language",
superseded in part by this record). Card: #752.

## Context

The process-control and flake class has taken a large share of the loop's
work:

- 49 cards (43 of them FIX) and 51 pull requests, about 27k changed lines
  with the foundations (TASK153, TASK154, TASK155, TASK166, FIX72 and FIX60,
  about 11.6k lines of those);
- 60 of the 347 review rounds;
- 11 of the 33 failed workflow runs since 11-15, and every red main run that
  was not infrastructure. The harness self-test runs are 44998168941,
  45010707695, 45337201551, 45547293056, 45415672331, 45582643870,
  45621239929 and 44704990185. The rest of the 33 were runner-queue cancels,
  network failures and `check-runbook-index` conflicts.

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

1. **The supervisor.** All spawning, bounding, signalling and reaping of
   child processes in the harness goes through one native supervisor,
   `scripts/lib/supervise.c` (#755), built with the `cc` the harness already
   uses for its other helpers. It starts the command as the leader of its own
   process group, owns the `waitpid`, keeps the leader unreaped until the
   group is empty, waits on kqueue events instead of polling `ps`, sends
   TERM, a grace period, then KILL to the group, forwards INT, TERM and HUP,
   and exits as `timeout(1)` does. Harness shell does not `trap` for a child,
   `kill` a child, or `wait` on a child pid outside the supervisor's wrapper.
2. **The shells.** The bench core supports `/bin/bash` 3.2 (also as `sh`) as
   its caller shell. Dash and the paths for a shell without job control are
   dropped. The goal of moving the core to other repositories is withdrawn,
   and can come back later with the supervisor.
3. **The lock.** The bench lock's guarantee is a kernel lock held by an
   inherited file descriptor, together with the per-run quiet evidence of
   TASK166. It is not a holder record with identity checks. A process that
   escapes with `setsid` keeps the lock, which is the safe direction.
4. **Self-tests.** Harness self-tests synchronise on the supervisor's events
   or on the shared test helpers (#756). They do not use wall time, `sleep`
   or `ps` state.
5. **The freeze.** Until the migration cards below land, no new FIX card is
   filed to harden `bench_poll`, `bench_spawn`, `bench_watch`,
   `bench_stop_groups`, `scripts/lib/bounded.py` or the lock's identity code.
   Review notes and flakes on that code are added to the replacement card. A
   red main run on it is re-run once and then quarantined, named in the
   replacement card, not fixed forward.

## The migration cards

| Card | What it does |
|---|---|
| #755 | the supervisor itself, with an event stream and a test clock |
| #756 | one self-test helper library over the supervisor's events |
| #758 | the bench core's bounded run goes through it; `bench_poll` and the shell clock and identity reads go |
| #759 | the lock's run path spawns, watches and stops through it; the `ps` identity layer goes |
| #760 | the lock is a kernel lock on an inherited fd, arrival order kept, stale reclaim and holder identity deleted |
| #761 | the gate starts, reaps and stops its checks through it |
| #757 | the supervisor's pty mode with a bounded drain |
| #762 | `scripts/lib/bounded.py` becomes a wrapper and `run_engine` uses the pty mode |
| #763 | the iOS scripts, `pins.sh`, the quake2 adapter and `check-video-menu` use it; the bounded-run lint becomes one rule |

## Cards parked

Each is hardening of code that a migration card deletes. It stays open and
unworked; its findings move to the card named.

| Parked | Closed by | Why |
|---|---|---|
| #726 (the end-group and stop-group clock reads) | #758 | the clock reads and `bench_poll` are deleted |
| #741 (a `bench-poll` scratch file left by `sign-int-hang`) | #758 | the poll directory goes with `bench_poll` |
| #723 (signal holds in `run_engine` and `bounded.py`) | #762, with #757 | `bounded.py` becomes a wrapper and the pty drain is the supervisor's |

## What it costs

- The harness needs a built binary. It is built with plain `cc` before the
  first use, and a failed build fails the gate closed; there is no fallback
  to the shell path.
- C adds a memory-safety review class to the harness. The program has no
  untrusted input beyond its arguments and environment.
- The shell and dash portability of the bench core is gone.
- The lock no longer reclaims a stale holder by identity. A missed exclusion
  is possible but rare, and the run that suffers it refuses itself.

## How to reverse it

The supervisor lands alone and each caller moves in its own card, so a
migration card is reverted on its own. Withdrawing the record means a new
record that names this one.
