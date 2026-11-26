# TASK227: the local gate selects the harness checks the way CI does

After [TASK211](TASK211.md), a pull request's CI runs only the harness checks that its changed
paths can affect. `scripts/test.sh` still ran every check on every gate. Its longest checks are
the harness self-tests and their mutants. So a gate on a change under `src/` took about as long
as a gate on a change to the harness.

## What changed

- **`scripts/test.sh` collects the changed paths and selects from them.** The paths are:
  - the commits since `git merge-base HEAD refs/remotes/origin/main`;
  - the staged, unstaged and untracked files (`git ls-files --others --exclude-standard`).

  Each list is read with `-z`, so every path ends with a NUL. A path with a blank, a newline or
  a leading `-` reaches the selector whole. The gate passes the list to `scripts/ci-select.sh`
  with `tests/ci-map.txt`. It is the same selector CI uses, with the same rules: a path in no map
  entry, a harness check's own file, the map or the selector runs every harness check.
- **Only harness checks are left out.** Fast and ios checks, and the checks that the map does not
  name (`tests/ci-local-only.txt`), always run. A harness check that the selector picks runs in
  full, with every mutant.
- **How they are left out.** The scheduler's line in `test.sh` is unchanged, so
  `check-gate-parallel` still reads it. The checks that are left out are removed from its
  `tests/check-*.sh` glob by `GLOBIGNORE`. They are also removed from `gate_first`, because
  `scripts/gate-checks.sh` refuses a name it is not given. `GLOBIGNORE` from the environment is
  unset first. A shell that ignores `GLOBIGNORE` runs every check.
- **It fails closed.** Every check runs, with one line `test.sh: every check runs: <why>`, on:
  - `GATE_ALL=1`;
  - no selector, or an empty or unreadable map;
  - no `refs/remotes/origin/main`, or no merge base;
  - a git command that fails;
  - a selector that fails, prints a name it was not given or prints one twice;
  - a selector that picks no harness check. A selector that prints nothing never means "run
    nothing", so a change to top-level Markdown alone runs every check.
- **The printed line.** When checks are left out, one line names each of them and the reason. It
  names checks only, never a path.
- **No workflow change.** CI runs `scripts/ci-checks.sh`, not `test.sh`.

## Proofs

`tests/check-ci-map.sh` part (e) runs `scripts/test.sh` in scratch repositories with a stub
scheduler that records the checks and the long-check list it gets.

| Case | Change | Want |
|---|---|---|
| `src-only` | a commit under `src/` | every harness check but `check-require-guards` left out |
| `staged` | `.gitignore`, staged | all but `check-metalfx-report` left out |
| `unstaged` | `scripts/coverage.sh`, not staged | `check-metalfx-report`, `check-bench`, `check-gate-parallel` left out |
| `untracked` | a new `Makefile` | all but `check-require-guards` left out |
| `name-blank`, `name-newline`, `name-dash` | untracked `src/a b.c`; `src/x` + newline + `.github`; `src/-n` and `src/--output=.github` | all but `check-require-guards` left out |
| `harness-path` | `tests/check-bench.sh` changed | none left out |
| `gate-all`, `no-origin`, `no-base`, `git-error` | `GATE_ALL=1`; no origin/main; origin/main an orphan; a git whose `ls-files` fails | every check, with the reason |
| `top-md` | `README.md` alone | every check (the selector picks none) |
| `sel-fails-map`, `sel-fails`, `sel-stranger`, `sel-twice`, `sel-none` | a selector that fails, prints `check-nope.sh`, a name twice, or nothing | every check |
| `map-empty`, `map-missing` | the map empty or removed | every check |

Each case also checks that nothing is left in `TMPDIR` and that no scratch path is printed.

`src-only` was red on `main` (commit `c53e00dd`, before the change): the gate gave the scheduler
every check and every long check.

Ten mutants of `test.sh` are each refused by their own case:

- a missing merge base that runs no harness check (`no-base`);
- `GATE_ALL` ignored (`gate-all`);
- a git error ignored (`git-error`);
- an empty selection trusted (`top-md`);
- a selector's unknown name trusted (`sel-stranger`);
- the untracked, staged or unstaged files not read (`untracked`, `staged`, `unstaged`);
- the untracked list read newline-delimited (`name-newline`);
- `gate_first` not filtered (`src-only`: the scheduler refuses a name it is not given).

`check-gate-parallel` reads the scheduler's line, `gate_first` and the lines after it. It stays
green.

## Measured

Measured once each on 2026-11-26, under one hold of the host lock, on the 12-CPU build host (6
checks at a time; load average 2 to 5). The tree was a scratch clone of this branch whose
`origin/main` was the branch tip, with one commit on top that adds a comment to
`src/server/sv_main.c`.

**The gate on a `src/`-only change.** "Before" is `GATE_ALL=1`, which runs every check, as the gate
did before this card.

| Run | Checks | Checks' wall time |
|---|---|---|
| before (`GATE_ALL=1`) | 58 | 1,694 s |
| after | 53 (`check-metalfx-report`, `check-bench`, `check-gate-parallel`, `check-bounded-run`, `check-bench-entry` left out) | 501 s |

After the selection, the longest checks are `check-ios` (417 s) and `check-require-guards`
(303 s), which a change under `src/` still runs. In the "before" run, `check-metalfx-report` took
1,694 s and set the wall time, and `check-bench` took 956 s.

In both runs, `check-no-local-identity` and `check-identity-env` failed in the clone only. A
clone's `origin` is a local path rather than the GitHub address, so the user-name word test is
not skipped. So the gate stopped before its builds in both runs, and the times are of the checks
alone. The builds and `make test` add the same time to both.

**The share of the mutants**, each check alone:

| Check | Whole | Without its mutants | Mutants' share |
|---|---|---|---|
| `check-metalfx-report` | 1,700 s | 98 s (`MFX_REAL_ONLY=1`) | 94% |
| `check-bench` | 928 s | 210 s (every test named) | 77% |

So a change that selects one of these two checks still pays for its mutants. That is right while
its targets changed. A mutant switch would only pay off for a change that selects one of them
without touching what its mutants cover. That is a question for a later card, and these numbers
are its input.

## Limits

- The gate trusts `refs/remotes/origin/main` as it is. A stale one makes the change look larger,
  which runs more checks, not fewer. Run `git fetch` first to get the selection CI would make.
- The globs are only as wide as the reading of the checks, as in CI. `GATE_ALL=1` and CI's
  nightly run are the backstops.
