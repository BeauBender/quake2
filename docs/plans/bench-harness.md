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

`scripts/bench/bench.sh` is the only entry point. A check (`tests/check-bench.sh`) enforces that the core and `bench.sh` name no game (`quake`, `q2`, `yquake`, `ref_`, `gl1`, case-insensitive) and no project layout (`third_party`, `Makefile`; the adapter names its input paths), and use no `eval`. Later the core can move into the agent tooling shared across repositories, unchanged; another game conversion then adds only an adapter and cases. The `null` adapter and the `null-sleep` case measure nothing: they exist so that the self-test drives every core path without an engine.

Language: POSIX `sh` plus `awk`, safe on bash 3.2, with no new dependency (decision 0008 platforms; CLAUDE.md "no installs"). All arithmetic and sorting run under `LC_ALL=C`. Case files are data: they are parsed line by line against a fixed key whitelist and never evaluated.

Exit status of `bench.sh`: 0 done; 1 a failing verdict, or no baseline where one is needed (or one already there for `baseline put`); 2 a usage, case, commit or setting error; 3 the adapter refused or failed; 74 a command run under the lock stopped (a stop signal, or the terminal) and was ended; 75 the lock wait ran out. A command run under the lock otherwise exits with its own status.

## Core

### The host lock: one heavy job at a time

`bench.sh lock run <label> -- <cmd...>` runs a command under an exclusive host-wide lock:
- the lock is a `mkdir` of `lock/` under the host state directory, `BENCH_STATE`, by default `$XDG_CACHE_HOME/bench-state` or `$HOME/.cache/bench-state`: per user, shared by every repository and worktree, and refused inside a git repository. The refusal fails closed: a path with a `.` or `..` segment is refused first (past a missing directory the guard could not tell where it ends), a `.git` entry in the directory or any ancestor refuses it whatever git says, git runs without the caller's `GIT_DIR`, `GIT_WORK_TREE`, `GIT_CEILING_DIRECTORIES` and `GIT_DISCOVERY_ACROSS_FILESYSTEM`, and a git that fails for any reason other than "not a git repository" refuses it too. A host with no git at all is judged by the `.git` walk alone. The store has the same guard;
- a holder file records the label, the pid of the lock shell and of its command, the start time and a token; waiters show it. The maker writes it to a temporary name and links it into place (`ln`, which fails when a holder file is there, so it never replaces another's), then checks that the directory is still the one it made (its inode, read right after the `mkdir`) and carries its token; otherwise it takes its holder back out and waits. So a maker that stalls between the `mkdir` and the write for longer than the grace below, while a waiter reclaims the directory and another caller makes it again, backs off instead of holding the lock with that caller;
- a label is 1 to 64 of `A-Z a-z 0-9 . _ : @ + -`, so it cannot add a line to the holder file; a case name is at most 58 characters, so `bench.<case>` is a label;
- waits are bounded and run in the foreground: a poll every `BENCH_LOCK_POLL` seconds (default 2; at least 0.01, and cut to the wait when longer) for at most `BENCH_LOCK_WAIT` seconds (default 3600), then exit 75 naming the holder's label;
- the command runs in the background of the lock shell as the leader of its own process group (`set -m` around the `&`: job control in a non-interactive shell, which bash, 3.2 included and also as `sh`, and zsh have; a shell without it leaves the command in the lock shell's group, says so, and a stop then reaches only the command's pid). So it starts with the caller's signal dispositions (an asynchronous list without job control starts with SIGINT and SIGQUIT ignored), its own traps run, and a signal reaches the lock shell's trap within one poll. It gets the lock shell's standard input, or `/dev/null` when that is a terminal; a terminal's Ctrl-C reaches the lock shell, which stops the command;
- the command never has the terminal: its group is in the background, so a command that reads the terminal, sets it (`stty`) or writes to it under `stty tostop` is stopped by it. The lock shell polls the command (`bench_watch`: every 0.05 s for about 2 s, every 0.2 s up to about 22 s, then every second) instead of a plain `wait`, which with job control off never returns for a stopped child. A stopped command (state `T`: a stop signal, or the terminal) is said, its group stopped as on a signal, and `lock run` exits 74 with the lock freed, so it never waits without end. Only the leader's state is read, so a stop of a single member of the group, or a terminal stop of a member whose leader catches or ignores SIGTTOU, is not detected: `lock run` then keeps waiting on the leader. Heavy jobs do not prompt; one that needs the terminal runs without the lock;
- the lock is released on every exit path: traps on EXIT, INT, TERM and HUP. On a signal the lock shell first stops the command's whole process group: TERM (and CONT) to the group, up to twice `BENCH_STOP_GRACE` seconds (default 10) and at least `BENCH_STOP_GRACE` + 5 s for it to go, then KILL to the group (and up to 2 s for it to go), and only then is the lock removed. The floor lets a nested `bench.sh` that stops a group of its own, as `bench_bounded` does (its grace, a second of clock and its 2 s KILL wait), finish first even with `BENCH_STOP_GRACE=0`. When the command exits, whatever it left running in its process group gets 1 s (by the clock, so 1 to 2 s) to end and is then stopped the same way, by the group only, before the lock is released. So nothing left in the command's process group outlives the lock, a grandchild, a background job or a command that ignores TERM included. The lock reaches the process group and no further: a descendant that moves itself into another group or session (`setsid`, a shell with job control) is out of its reach; a nested `bench.sh` stops the groups it made itself, on the TERM it gets. A signal that arrives between the `&` and the shell storing the pid is held until the pid is stored. The shell's own notices of a job killed by a signal (they name internal paths) are not shown;
- a lock whose holder pid and command pid are both gone (`kill -0`), and whose command's process group has no member left, is reclaimed, and the reclaim is logged on standard error and in `lock.log` in the state directory. Two waiters that see the same dead holder race for a guard directory (`reclaim.<token>`, a `mkdir` again); the winner reads the holder again and removes the lock only if it still carries that token, then every waiter goes back to the `mkdir`, so exactly one gets the lock. A lock directory left without a holder file for `BENCH_LOCK_GRACE` seconds (default 10) is reclaimed the same way.

The lock is re-entrant: it exports `BENCH_LOCK_TOKEN` to its command, and a nested call passes straight through only when that variable equals the token in the holder file. An empty, stale or forged token waits like any caller.

`bench.sh lock status` prints `lock=free`, or `lock=held` with the holder's label, pids, liveness and start time (never a path).

The measuring commands (`baseline put`, `drift`) take the lock by becoming a lock shell (`exec`) that runs the measurement under it, so a signal to them reaches the lock shell's trap with no extra process in between.

Every heavy entry point takes the lock itself, so no caller can forget: `scripts/test.sh`, `scripts/pins.sh` (record and compare), `scripts/coverage.sh` (card 3) and the measuring commands of `bench.sh` (`baseline put` and `drift`, built). `make` is not wrapped. Builds started by these entry points are covered, and a bare `make` is a documented rule for agents: run it as `bench.sh lock run build -- make ...`.

### Host identity

`bench.sh host` prints two lines:
- **a token:** `h-` and 12 hex digits, a hash of the CPU model, the GPU name, memory size, OS version and build, and whether the host is a virtual machine; read with `sysctl` and `system_profiler` on macOS, with `uname` and `/proc` elsewhere, and `unknown` for a fact that cannot be read;
- **a class line:** `<cpu>, <gpu> GPU, <os>, physical|vm`, for example `Apple M6, Apple M6 GPU, macOS 27.2.1, physical`.

`bench.sh host --facts` shows the facts and the hash tool. Neither line holds a host name, user name or home path, so both may appear in public files: the values `tests/check-no-local-identity.sh` refuses are scrubbed from the facts, and a token that happens to spell one is hashed again with a salt. Results from different tokens are never compared as before and after. `system_profiler` runs under `bench_bounded` (30 s); one that does not answer in time is an error, not a token without the GPU.

The hash is `shasum -a 256` when it is present and `cksum` otherwise (two CRC-32s over the text with different prefixes); `BENCH_HASH=shasum|cksum` forces one, and every listing that hashes says which. A tool that fails, is missing or answers with anything but a well-formed digest (64 hex digits, or two CRCs) is an error (exit 2), never an empty id.

### Build-input identity

`bench.sh inputs <commit> [<case>]` hashes exactly what the measured build compiles: the `git rev-parse <commit>:<path>` ids of the paths the case's adapter names (`adapter_inputs_paths <commit>`), plus the build flags the case names (`build_flags`). The core names no project layout: without a case, or with an adapter that names no path, the default is the whole tree (the commit's root tree id), so any change gives new inputs. A path the commit lacks hashes as the marker `missing`. The result is `i-` and 12 hex digits; `--list` prints the hashed text and the hash tool. The quake2 adapter (card 2) names `Makefile`, `src/`, `third_party/` and every top-level `*.make` file, as the `null` adapter already does for the self-test; with that list a docs-only commit has the same inputs, so its baseline stays valid, and a code change gets new inputs.

### Case files

A case is `key=value` lines; blank lines and `#` comments are ignored. The keys are `adapter`, `runs`, `metric`, `abs_tol` and `rel_tol` (required), `warmup`, `wall`, `better` (`lower` or `higher`), `accept`, `targets`, `build_flags`, `settings`, `requires` and `description`; any other key, or one given twice, refuses the case. The case hash is `c-` and 12 hex digits of the normalised content (kept lines, blanks squeezed, sorted), so comments, spacing and key order do not change it.

### The result store

The store lives outside the repository, per host:

`${BENCH_STORE:-${XDG_CACHE_HOME:-$HOME/.cache}/<repo>-bench}/<host-token>/<inputs>/<case>/<case-hash>/set-<n>/`

where `<repo>` is the name of the repository's main checkout (the same in every worktree). Each set is one full measurement; the baseline is the set with the highest number. One file per run, `r-<UTC time>-<pid>-<k>.run` (`k` the run's index), in plain `key=value` lines:
- the case and its hash;
- the commit and inputs;
- the host token, the host class and the hash tool;
- the run kind and index and the start time;
- the load before and after;
- each precondition's value (`pre.<name>`);
- each metric (`metric.<name>`).

A set is stored atomically: every run is written into a staging directory, `.tmp.<pid>/` beside the sets, and the set is made live by one rename to `set-<n+1>`. A reader (`baseline get` and `compare` take no lock) resolves the live set once and reads only in it, so it sees a whole set or none; a store that fails at any step removes its staging directory and leaves the old baseline live. A replaced baseline stays where it is, as the set below the live one; nothing is moved, so a reader is never left with half a set. Readers take only `set-<number>` directories and `r-*.run` files. The single runs of drift checks are kept in `drift/`, each written under a `.tmp.` name and renamed into place.

Operations:
- `bench.sh baseline get|put <case> <commit>`; `put` refuses when a baseline exists, unless given `--replace`;
- `bench.sh drift <case> <commit>`: one run, compared with the stored runs by the case's tolerance rule;
- `bench.sh compare <case> --before <commit> --after <commit>`: per metric and side, the median of run medians, the p90, the range and the number of runs in a fixed-width table, and a `verdict=` line against the case's acceptance, if it states one.

All arithmetic is deterministic in `awk`. Values are sorted; the median is the middle value, or the mean of the two middle values for an even count; the p90 is the nearest rank, the value at rank `ceil(0.9 n)` (1-based), so one value is its own p90. Numbers print with at most 4 decimals.

The drift rule: the run's median must lie within `[min − t, max + t]` of the stored run medians, where `t = max(abs_tol, rel_tol × |median|)` from the case file and the bounds are inclusive. Inside, the stored baseline stands. Outside, the baseline is re-measured in full and stored as the next set; the old one is kept as the set below it.

The verdict: `change` is `(after − before) / |before|` in percent of the before median of the case's `metric`; the improvement is `−change` when `better=lower` and `change` when `better=higher`; it passes when the improvement is at least `accept` (so `accept=0` means "not worse"). A failing verdict exits 1.

### Determinism

A case pins every setting. The adapter refuses a run whose preconditions fail, and records each precondition's value. Every run of a case takes the lock for its whole duration. Runs of before and after builds are only needed when no baseline exists: the first measurement records it, and later ones reuse it.

## Adapter interface

An adapter is a sourced shell file that defines:
- `adapter_preflight <case>`: checks preconditions and prints `pre.<name>=<value>` lines; exits non-zero to refuse. For quake2: the screen is unlocked, no other engine is running, and the case's `requires` capability is present (`physical-gpu`, `raytracing`, `metalfx`, `display-hz>=N`).
- `adapter_build <commit> <dir> <targets...>`: builds a commit in a scratch export.
- `adapter_run <case> <build-dir> <log>`: launches with the case's settings, stops cleanly, and is bounded by the case's wall time (`bench_case_wall`: the case's `wall`, default 600 s). The core's `bench_bounded` helps: it runs the command in its own process group and, past the wall time, sends the group TERM, then KILL after `BENCH_STOP_GRACE` (status 124), so a hung engine that only notes TERM is still stopped; a stopped command is stopped the same way (status 74); and once the command exits, what it left running in its group gets 1 to 2 s to end and is then stopped, before the bound returns. Like the lock, it reaches the process group only, not a descendant that moved itself into another group or session.
- `adapter_metrics <case> <log>`: prints `metric.<name>=<value>` lines. For quake2 that covers the timedemo line, `metal_stats` lines (scaler and pass GPU times, presented and rendered counts), `r_rt_stats 2` lines, and the counting window from N s after the first frame.
- `adapter_inputs_paths <commit>` (optional): prints the paths of the commit the build compiles, one per line, for the inputs id; without it the inputs are the whole tree.

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
