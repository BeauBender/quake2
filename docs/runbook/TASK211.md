# TASK211: the invariants job split into fast, harness and ios

In run 45683223193 (2026-11-25) the one invariants job of `.github/workflows/macos.yml` took
65 minutes: 54 checks, 2 at a time, 7,307 s of check time. Six checks took 5,925 s of it:
`check-metalfx-report` 2,352 s, `check-bench` 1,388 s, `check-require-guards` 584 s,
`check-bounded-run` 555 s, `check-ios` 529 s and `check-gate-parallel` 517 s. Every merge also
started a push run on `main` of the same length, which competed with pull request runs for the
free macOS runner.

## What changed

- **`invariants_macos` is a matrix** of three jobs that run in parallel (`fail-fast: false`).
  Each one runs `scripts/ci-checks.sh` with `CI_CHECKS_GROUP` set to its group:
  - `fast`: every check that is not in the other two groups (48 checks);
  - `harness`: the six slow self-tests of the harness (`check-metalfx-report`, `check-bench`,
    `check-require-guards`, `check-bounded-run`, `check-gate-parallel`, `check-bench-entry`);
  - `ios`: `tests/check-ios.sh`.

  The job names are `invariants_macos (fast)`, `invariants_macos (harness)` and
  `invariants_macos (ios)`. `.github/rulesets/main-protect.json` requires all three in place of
  `invariants_macos`. Applying it stays the owner's action, with the command in
  [FIX115](FIX115.md).
- **`tests/ci-map.txt`** puts every check of `tests/ci-checks.txt` in one group. Each harness
  check has one `harness <check> <glob>` line for each path glob that can change its result,
  found by reading the check. `all <glob>` lines name the paths that run every harness check: the
  six checks' own files, `scripts/bench/`, `scripts/gate-checks.sh` and `scripts/test.sh`.
  `product <glob>` lines name the paths that no harness check reads.
- **`scripts/ci-select.sh`** (bash 3.2) chooses a group's checks. For `harness` on a pull
  request it reads the changed paths NUL-delimited. Each path is one word that is never split,
  expanded or evaluated, and is compared without regard to letter case, because the runner's file
  system folds case. It fails closed. Every harness check runs when:
  - a path matches no `harness`, `all` or `product` glob;
  - a path is the map, `tests/ci-checks.txt`, `tests/ci-local-only.txt`,
    `scripts/ci-checks.sh`, the selector, or a file under `.github/` (these are fixed in the
    script, so a change to the map cannot remove them);
  - the list of paths is empty, or does not end with a NUL.

  It prints counts, never a path.
- **`scripts/ci-checks.sh`** passes the selector the paths in `HEAD^1..HEAD` of the merge commit
  that a pull request checkout is. It passes "every path" on a push, on the schedule, when `HEAD`
  is not a merge commit and when git fails. When the selector fails, or prints a name it was not
  given, or prints none for `fast` or `ios`, the job runs every check of the list. Only `harness`
  may run nothing, and then the job passes. Each group starts its long checks first. Without
  `CI_CHECKS_GROUP` it runs every check, as before. `--select` prints the names and runs nothing.
- **A nightly `schedule`** (09:17 UTC) runs every check on `main`.
- **Concurrency.** `macos.yml` and `codeql.yml` use the group
  `${{ github.workflow }}-${{ github.event_name == 'push' && 'main' || github.event.pull_request.number || github.run_id }}`
  with `cancel-in-progress: true`. All pushes to `main` share one group, so a newer push cancels
  the older `main` run. A pull request's group is its own number, so only a newer push to that
  pull request cancels its run, as before. A scheduled run's group is its own run id. A push and
  a pull request never share a group.
- **`tests/check-ci-map.sh`**, in the gate by the `tests/check-*.sh` glob and in CI in the fast
  group. It checks that:
  - every check is in a group of the map or in `tests/ci-local-only.txt`;
  - every name in the map exists, is in `tests/ci-checks.txt` and is in one group;
  - the six checks are in the harness group, and `check-ios.sh` is alone in the ios group;
  - the matrix, `CI_CHECKS_GROUP`, the schedule, both concurrency groups and the ruleset's three
    names are as above.

  It runs the selector on lists of paths. These include names with a blank, a newline, a leading
  `-` and glob characters, and a map that names `.github/*` as `product`. It also runs
  `ci-checks.sh` in a scratch repository whose `HEAD` is a merge commit, the way a pull request is
  checked out.

## Wall time

These are estimates from run 45683223193's per-check times, 2 at a time, with the long checks
started first:

| Job | Check time | Estimated wall time |
|---|---|---|
| fast | 1,238 s, 48 checks | about 640 s, 11 minutes |
| harness, full (push, nightly, a harness or map change) | 5,565 s | about 2,870 s, 48 minutes |
| harness, a change under `src/` only | 584 s (`check-require-guards`) | about 584 s, 10 minutes |
| harness, top-level Markdown or `.claude/` only | none | under a minute |
| ios | 529 s | about 9 minutes |

The build job took about 2 minutes in that run. Each job also spends about a minute on the
checkout of the full history and the toolchain line. So a pull request that changes only
`src/client/refresh/metal/` should finish in about 12 minutes once its runners start. Only a
real run shows the true time. A pull request now takes four macOS jobs (the build and three
invariants jobs), plus CodeQL's C analysis.

## Mutants

`tests/check-ci-map.sh` refuses 18 mutants:

- a fail-open selector, where a path in no entry runs nothing;
- a check removed from the map, which is then in neither group;
- a map entry that names a check that does not exist;
- a harness check's lines removed;
- no map at all, which is the tree before this card;
- the changed paths split on blanks and newlines;
- `.github/` left to the map;
- an empty list that runs nothing, and an unterminated list that runs nothing;
- letter case not folded;
- `ci-checks.sh` diffing against `HEAD^2`;
- a fallback that runs nothing;
- a long-check list that names checks the group does not run;
- a workflow concurrency group that spans events, and CodeQL's old per-commit group;
- a matrix without `harness`;
- a fixed `CI_CHECKS_GROUP`;
- `fail-fast: true`.

## What only a real run shows

- The wall time of a pull request that touches only `src/client/refresh/metal/` (20 minutes or
  less is the bar).
- A second push to `main` while a `main` run is in progress cancels the older run and no pull
  request run.
- The nightly run starts.

## Limits

- CI runs the pull request's own selector and map. The selector's fixed list protects against a
  mistake, not against a pull request that rewrites the selector. A pull request that makes the
  selector fail open still fails `check-ci-map` in the fast job, which runs on every pull request.
- A push to `main` runs the whole harness group (about 48 minutes) whatever changed. The next
  merge cancels it, and the nightly run covers what a cancelled run did not finish.
- The globs come from reading the checks, so they are only as wide as that reading. The nightly
  run is the backstop for a glob that is too narrow.
