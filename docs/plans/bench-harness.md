# Bench harness: quiet, repeatable measurements that carry across hosts and games

Plan anchor for the bench cards. Status: the core (card 1, TASK153) is built; cards 2 to 4 are planned.

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
| Core | `scripts/bench/core/` | nothing game-specific: locking, host identity, build-input identity, case files, the result store, statistics, drift and comparison |
| Adapter | `scripts/bench/adapters/<game>.sh` | how to build, launch and stop one game, its preconditions, and how to read metrics from its logs |
| Cases | `scripts/bench/cases/<name>.case` | one measurement: adapter, build targets, every setting, runs, warm-up, counting window, metrics, tolerance |

`scripts/bench/bench.sh` is the only entry point. A check (`tests/check-bench.sh`) enforces that the core and `bench.sh` name no game (`quake`, `q2`, `yquake`, `ref_`, `gl1`, case-insensitive) and use no `eval`. Later the core can move into the agent tooling shared across repositories, unchanged; another game conversion then adds only an adapter and cases. The `null` adapter and the `null-sleep` case measure nothing: they exist so that the self-test drives every core path without an engine.

Language: POSIX `sh` plus `awk`, safe on bash 3.2, with no new dependency (decision 0008 platforms; CLAUDE.md "no installs"). All arithmetic and sorting run under `LC_ALL=C`. Case files are data: they are parsed line by line against a fixed key whitelist and never evaluated.

Exit status of `bench.sh`: 0 done; 1 a failing verdict, or no baseline where one is needed (or one already there for `baseline put`); 2 a usage, case, commit or setting error; 3 the adapter refused or failed; 75 the lock wait ran out. A command run under the lock exits with its own status.

## Core

### The host lock: one heavy job at a time

`bench.sh lock run <label> -- <cmd...>` runs a command under an exclusive host-wide lock:
- the lock is a `mkdir` of `lock/` under the host state directory, `BENCH_STATE`, by default `$XDG_CACHE_HOME/bench-state` or `$HOME/.cache/bench-state`: per user, shared by every repository and worktree, and refused inside a git work tree;
- a holder file, written to a temporary name and renamed into place, records the label, the pid of the lock shell and of its command, the start time and a token; waiters show it;
- a label is 1 to 64 of `A-Z a-z 0-9 . _ : @ + -`, so it cannot add a line to the holder file;
- waits are bounded and run in the foreground: a poll every `BENCH_LOCK_POLL` seconds (default 2) for at most `BENCH_LOCK_WAIT` seconds (default 3600), then exit 75 naming the holder's label;
- the lock is released on every exit path: traps on EXIT, INT, TERM and HUP; on a signal the command is stopped first (TERM, then KILL after 10 s). The command runs in the background of the lock shell, with its standard input, so a signal reaches the trap at once;
- a lock whose holder pid and command pid are both gone (`kill -0`) is reclaimed, and the reclaim is logged on standard error and in `lock.log` in the state directory. Two waiters that see the same dead holder race for a guard directory (`reclaim.<token>`, a `mkdir` again); the winner reads the holder again and removes the lock only if it still carries that token, then every waiter goes back to the `mkdir`, so exactly one gets the lock. A lock directory left without a holder file for `BENCH_LOCK_GRACE` seconds (default 10) is reclaimed the same way.

The lock is re-entrant: it exports `BENCH_LOCK_TOKEN` to its command, and a nested call passes straight through only when that variable equals the token in the holder file. An empty, stale or forged token waits like any caller.

`bench.sh lock status` prints `lock=free`, or `lock=held` with the holder's label, pids, liveness and start time (never a path).

Every heavy entry point takes the lock itself, so no caller can forget: `scripts/test.sh`, `scripts/pins.sh` (record and compare), `scripts/coverage.sh` (card 3) and the measuring commands of `bench.sh` (`baseline put` and `drift`, built). `make` is not wrapped. Builds started by these entry points are covered, and a bare `make` is a documented rule for agents: run it as `bench.sh lock run build -- make ...`.

### Host identity

`bench.sh host` prints two lines:
- **a token:** `h-` and 12 hex digits, a hash of the CPU model, the GPU name, memory size, OS version and build, and whether the host is a virtual machine; read with `sysctl` and `system_profiler` on macOS, with `uname` and `/proc` elsewhere, and `unknown` for a fact that cannot be read;
- **a class line:** `<cpu>, <gpu> GPU, <os>, physical|vm`, for example `Apple M6, Apple M6 GPU, macOS 27.2.1, physical`.

`bench.sh host --facts` shows the facts and the hash tool. Neither line holds a host name, user name or home path, so both may appear in public files: the values `tests/check-no-local-identity.sh` refuses are scrubbed from the facts, and a token that happens to spell one is hashed again with a salt. Results from different tokens are never compared as before and after.

The hash is `shasum -a 256` when it is present and `cksum` otherwise (two CRC-32s over the text with different prefixes); `BENCH_HASH=shasum|cksum` forces one, and every listing that hashes says which.

### Build-input identity

`bench.sh inputs <commit> [<case>]` hashes exactly what the measured build compiles: the `git rev-parse <commit>:<path>` ids of `Makefile`, `src/`, `third_party/` and every top-level `*.make` file, plus the build flags the case names (`build_flags`). A path the commit lacks hashes as the marker `missing`. The result is `i-` and 12 hex digits; `--list` prints the hashed text and the hash tool. A docs-only commit has the same inputs, so its baseline stays valid. A code change gets new inputs.

### Case files

A case is `key=value` lines; blank lines and `#` comments are ignored. The keys are `adapter`, `runs`, `metric`, `abs_tol` and `rel_tol` (required), `warmup`, `wall`, `better` (`lower` or `higher`), `accept`, `targets`, `build_flags`, `settings`, `requires` and `description`; any other key, or one given twice, refuses the case. The case hash is `c-` and 12 hex digits of the normalised content (kept lines, blanks squeezed, sorted), so comments, spacing and key order do not change it.

### The result store

The store lives outside the repository, per host:

`${BENCH_STORE:-${XDG_CACHE_HOME:-$HOME/.cache}/<repo>-bench}/<host-token>/<inputs>/<case>/<case-hash>/`

where `<repo>` is the name of the repository's main checkout (the same in every worktree). One file per run, `r-<UTC time>-<pid>-<n>.run`, in plain `key=value` lines:
- the case and its hash;
- the commit and inputs;
- the host token, the host class and the hash tool;
- the run kind and index and the start time;
- the load before and after;
- each precondition's value (`pre.<name>`);
- each metric (`metric.<name>`).

A run file is written under a `.tmp.` name in the same directory and renamed into place; readers take only `r-*.run`, so they never see a temporary file. A replaced baseline moves to `superseded/<UTC time>-<pid>/`; the single runs of drift checks are kept in `drift/`.

Operations:
- `bench.sh baseline get|put <case> <commit>`; `put` refuses when a baseline exists, unless given `--replace`;
- `bench.sh drift <case> <commit>`: one run, compared with the stored runs by the case's tolerance rule;
- `bench.sh compare <case> --before <commit> --after <commit>`: per metric and side, the median of run medians, the p90, the range and the number of runs in a fixed-width table, and a `verdict=` line against the case's acceptance, if it states one.

All arithmetic is deterministic in `awk`. Values are sorted; the median is the middle value, or the mean of the two middle values for an even count; the p90 is the nearest rank, the value at rank `ceil(0.9 n)` (1-based), so one value is its own p90. Numbers print with at most 4 decimals.

The drift rule: the run's median must lie within `[min − t, max + t]` of the stored run medians, where `t = max(abs_tol, rel_tol × |median|)` from the case file and the bounds are inclusive. Inside, the stored baseline stands. Outside, the baseline is re-measured in full and replaced, and the old one is kept as `superseded/`.

The verdict: `change` is `(after − before) / |before|` in percent of the before median of the case's `metric`; the improvement is `−change` when `better=lower` and `change` when `better=higher`; it passes when the improvement is at least `accept` (so `accept=0` means "not worse"). A failing verdict exits 1.

### Determinism

A case pins every setting. The adapter refuses a run whose preconditions fail, and records each precondition's value. Every run of a case takes the lock for its whole duration. Runs of before and after builds are only needed when no baseline exists: the first measurement records it, and later ones reuse it.

## Adapter interface

An adapter is a sourced shell file that defines:
- `adapter_preflight <case>`: checks preconditions and prints `pre.<name>=<value>` lines; exits non-zero to refuse. For quake2: the screen is unlocked, no other engine is running, and the case's `requires` capability is present (`physical-gpu`, `raytracing`, `metalfx`, `display-hz>=N`).
- `adapter_build <commit> <dir> <targets...>`: builds a commit in a scratch export.
- `adapter_run <case> <build-dir> <log>`: launches with the case's settings, stops cleanly, and is bounded by a wall time (the core's `bench_bounded` helps).
- `adapter_metrics <case> <log>`: prints `metric.<name>=<value>` lines. For quake2 that covers the timedemo line, `metal_stats` lines (scaler and pass GPU times, presented and rendered counts), `r_rt_stats 2` lines, and the counting window from N s after the first frame.

`<case>` is the normalised case file, read with `bench_case_get <key>`. Each run sees `BENCH_RUN_KIND` (`warmup` or `run`) and `BENCH_RUN_INDEX`. The core refuses preflight or metric lines of another shape, and a run without the case's `metric`.

## Cases

The first cases cover what the open cards measured, so their numbers become reproducible:
- `present-*`: FIX51, both renderers, 640x480, 1920x1080 and 5120x2880, `r_vsync` 0 and 1;
- `rt-shadows-gpu`: TASK131;
- `rt-ao-gpu`: TASK57;
- `metalfx-scaler-*`: TASK111.

## Working on several hosts

Every host runs the same tool. The VM's token differs, so its numbers never mix with bare metal. A case that requires a capability the host lacks is refused with a reason, not faked. Results stay on the host that made them. A runbook cites a result by `<host class>, <case>, <inputs>`, so another host can reproduce it.

## Cards

1. **Core** (TASK153, opus): the lock, host identity, inputs, store, statistics, drift and compare, with a hermetic self-test `tests/check-bench.sh` (no engine) and its mutants, plus this plan as `docs/plans/bench-harness.md` and the commands in `doc/085_unittests.md`.
2. **quake2 adapter and first cases** (TASK, opus; needs 1 and a physical GPU): the adapter, the four case families, a user page, and the runbook method sections pointing at `bench.sh`. Each case is reproduced against one stored result from its card's runbook.
3. **The lock in every heavy entry point** (TASK, sonnet; needs 1): `scripts/test.sh`, `scripts/pins.sh` and `scripts/coverage.sh` take the lock re-entrantly, and a check proves it with a mutant. The agent briefs' timing rules point at `bench.sh lock run`.
4. **The generic core in the shared agent tooling** (plugin candidate): measurement lanes as a first-class swarm concept, so the scheduler holds heavy work while a measurement runs, plus the core of card 1 moved there.
