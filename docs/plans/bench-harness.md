# Bench harness: quiet, repeatable measurements that carry across hosts and games

Plan anchor for the bench cards. Status: planned.

## Why

Performance cards (FIX51, TASK55, TASK57, TASK111, TASK131) each measured with hand-made scratch scripts, outside the repository. Each one rebuilt and re-ran the "before" build in every batch. None of them could stop another lane's build from starting halfway through a timing run.

The owner's direction (2026-11-16):
- the machine is quiet while anything is timed;
- an unchanged build is measured once and reused;
- the process is durable and deterministic, the same on every computer and VM;
- it extends to future game conversions.

## Shape

`scripts/bench/` has three layers.

| Layer | Path | Knows about |
|---|---|---|
| Core | `scripts/bench/core/` | nothing game-specific: locking, host identity, build-input identity, the result store, statistics, drift and comparison |
| Adapter | `scripts/bench/adapters/<game>.sh` | how to build, launch and stop one game, its preconditions, and how to read metrics from its logs |
| Cases | `scripts/bench/cases/<name>.case` | one measurement: adapter, build targets, every setting, runs, warm-up, counting window, metrics, tolerance |

`scripts/bench/bench.sh` is the only entry point. A check enforces that the core names no game. Later the core can move into the agent tooling shared across repositories, unchanged; another game conversion then adds only an adapter and cases.

Language: POSIX `sh` plus `awk`, safe on bash 3.2, with no new dependency (decision 0008 platforms; CLAUDE.md "no installs").

## Core

### The host lock: one heavy job at a time

`bench.sh lock run <label> -- <cmd...>` runs a command under an exclusive host-wide lock:
- the lock is a `mkdir` under the host state directory;
- a holder file records the label, the pid and the start time, and is shown to waiters;
- waits are bounded and run in the foreground; the lock is released on every exit path;
- a lock whose holder pid is gone is reclaimed, and the reclaim is logged.

The lock is re-entrant: it exports `BENCH_LOCK_TOKEN`, and a nested call that carries the token runs straight through.

`bench.sh lock status` prints the holder.

Every heavy entry point takes the lock itself, so no caller can forget: `scripts/test.sh`, `scripts/pins.sh` (record and compare), `scripts/coverage.sh` and `bench.sh run`. `make` is not wrapped. Builds started by these entry points are covered, and a bare `make` is a documented rule for agents: run it as `bench.sh lock run build -- make ...`.

### Host identity

`bench.sh host` prints two things:
- **a token:** `h-` and 12 hex digits, a hash of the CPU model, the GPU name, memory size, OS version and build, and whether the host is a virtual machine;
- **a class line:** for example `Apple M6, macOS 27.2, physical`.

Neither holds a host name or user name, so both may appear in public files. Results from different tokens are never compared as before and after.

### Build-input identity

`bench.sh inputs <commit>` hashes exactly what the measured build compiles. That is the git tree ids of `src/`, `Makefile`, `*.make` and `third_party/`, plus the build flags the case names. A docs-only commit has the same inputs, so its baseline stays valid. A code change gets new inputs.

### The result store

The store lives outside the repository, per host:

`${BENCH_STORE:-$HOME/.cache/<repo>-bench}/<host-token>/<inputs>/<case>/<case-hash>/`

One file per run, in plain `key=value` lines:
- the case;
- the commit and inputs;
- the host class;
- the start time;
- the load before and after;
- each precondition's value;
- each metric.

Operations:
- `bench.sh baseline get|put <case> <commit>`;
- `bench.sh drift <case> <commit>`: one run, compared with the stored runs by the case's tolerance rule;
- `bench.sh compare <case> --before <commit> --after <commit>`: the median of run medians, the p90, the range, the number of runs, and a verdict against the case's acceptance, if it states one.

All arithmetic is deterministic in `awk`. Medians and p90 come from sorted values with a fixed interpolation rule.

The drift rule: the run's median must lie within `[min − t, max + t]` of the stored run medians, where `t = max(abs_tol, rel_tol × median)` from the case file. Inside, the stored baseline stands. Outside, the baseline is re-measured in full and replaced, and the old one is kept as `superseded/`.

### Determinism

A case pins every setting. The adapter refuses a run whose preconditions fail, and records each precondition's value. Every run of a case takes the lock for its whole duration. Runs of before and after builds are only needed when no baseline exists: the first measurement records it, and later ones reuse it.

## Adapter interface

An adapter is a sourced shell file that defines:
- `adapter_preflight <case>`: checks preconditions and prints `pre.<name>=<value>` lines; exits non-zero to refuse. For quake2: the screen is unlocked, no other engine is running, and the case's `requires` capability is present (`physical-gpu`, `raytracing`, `metalfx`, `display-hz>=N`).
- `adapter_build <commit> <dir> <targets...>`: builds a commit in a scratch export.
- `adapter_run <case> <build-dir> <log>`: launches with the case's settings, stops cleanly, and is bounded by a wall time.
- `adapter_metrics <case> <log>`: prints `metric.<name>=<value>` lines. For quake2 that covers the timedemo line, `metal_stats` lines (scaler and pass GPU times, presented and rendered counts), `r_rt_stats 2` lines, and the counting window from N s after the first frame.

## Cases

The first cases cover what the open cards measured, so their numbers become reproducible:
- `present-*`: FIX51, both renderers, 640x480, 1920x1080 and 5120x2880, `r_vsync` 0 and 1;
- `rt-shadows-gpu`: TASK131;
- `rt-ao-gpu`: TASK57;
- `metalfx-scaler-*`: TASK111.

## Working on several hosts

Every host runs the same tool. The VM's token differs, so its numbers never mix with bare metal. A case that requires a capability the host lacks is refused with a reason, not faked. Results stay on the host that made them. A runbook cites a result by `<host class>, <case>, <inputs>`, so another host can reproduce it.

## Cards

1. **Core** (TASK, opus): the lock, host identity, inputs, store, statistics, drift and compare, with a hermetic self-test `tests/check-bench.sh` (no engine) and its mutants, plus this plan as `docs/plans/bench-harness.md`.
2. **quake2 adapter and first cases** (TASK, opus; needs 1 and a physical GPU): the adapter, the four case families, a user page, and the runbook method sections pointing at `bench.sh`. Each case is reproduced against one stored result from its card's runbook.
3. **The lock in every heavy entry point** (TASK, sonnet; needs 1): `scripts/test.sh`, `scripts/pins.sh` and `scripts/coverage.sh` take the lock re-entrantly, and a check proves it with a mutant. The agent briefs' timing rules point at `bench.sh lock run`.
4. **The generic core in the shared agent tooling** (plugin candidate): measurement lanes as a first-class swarm concept, so the scheduler holds heavy work while a measurement runs, plus the core of card 1 moved there.
