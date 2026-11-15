# FIX66: CodeQL analysis skipped for documentation-only pull requests

Card FIX66 (issue 324). Workflow: `.github/workflows/codeql.yml`; decision
script: `.github/scripts/codeql-changes.sh`; test:
`tests/check-codeql-changes.sh`.

## Required checks

Read-only API queries: `main` has no branch protection (404), and its one
ruleset (`main-protect`) holds only `deletion` and `non_fast_forward`. No status
check is currently required.

A matrix job skipped by its `if:` is skipped before the matrix expands, so it
may report under its unexpanded name (plain `Analyze`) instead of the names its
matrix jobs report on a run that analyses. So before any CodeQL check is made
required, read its real check names from a run, a skipped one included; a
required name that a skipped run never reports would leave the check pending.

## Change

Round 2 replaces the round-1 filter, which listed the paths that trigger the
analyses and so skipped everything it did not list (a `config.mk`, which the
`Makefile` includes, plus a header under `docs/`; a name git quotes).

- A `changes` job (ubuntu, no third-party action beyond checkout,
  `fetch-depth: 2` on the merge checkout) runs
  `bash .github/scripts/codeql-changes.sh`, which writes `analyze=true` or
  `analyze=false` to `$GITHUB_OUTPUT` (stdout when it is unset).
- The script says `false` only when all of these hold: the event is
  `pull_request`; `git rev-list --parents -n1 HEAD` has three words (HEAD is a
  two-parent merge, `HEAD^1` the base); `git -c core.quotePath=false diff
  --no-renames --name-only HEAD^1 HEAD`, written to a file, succeeds and is
  non-empty; and `grep -Ev` on that file finds no path off the documentation
  list. The list is Markdown (`*.md`) under `doc/` or `docs/` at any depth, or
  at the top level, with names in ASCII letters, digits, `.`, `_` and `-`.
- Everything else says `true`: any other event (push, schedule, a manual run),
  a HEAD with one, none or three or more parents, a failed `git`, `mktemp` or
  `grep`, an empty diff, any other path, any name git quotes (a double quote,
  a backslash, a control character) and any non-ASCII name. A rename is a
  deletion plus an addition (`--no-renames`), so moving a file out of `src/`
  counts as a change to `src/`.
- The list is matched in its file, never through a pipe: with `pipefail`,
  `printf | grep -q` can fail on SIGPIPE and turn that into a decision.
- The log holds counts only (changed paths, paths off the list); no file name
  from the pull request is printed, and git's own stderr is discarded.
- `analyze` has `needs: changes` and
  `if: ${{ !cancelled() && needs.changes.outputs.analyze != 'false' }}`, so a
  failed `changes` job or a missing decision analyses too. No workflow-level
  `paths:` filter is used, since a workflow skipped by a path filter leaves a
  required check pending.
- Concurrency: group
  `${{ github.workflow }}-${{ github.event.pull_request.number || github.sha }}`,
  `cancel-in-progress` only for `pull_request`. Each push to `main` has a
  group of its own commit, so it is never queued behind, replaced by or
  cancelled for another run (round 1 grouped `main` by `github.ref`, where a
  pending run is replaced by a newer one even without `cancel-in-progress`).
- Cppcheck is untouched (30 seconds) and always runs.

Limit: a `pull_request` run uses the workflow and the script from the pull
request's own merge commit, so a pull request that edits either decides its
own CI. That is how `pull_request` workflows work and is not something this
filter can close; such a change is not documentation only, so its review
covers it, and every push to `main` is analysed whatever it changes.

## Before numbers

Median c-cpp analysis job duration over the last ten pull request runs (run
ids 43768244403 down to 43715785219): 469, 702, 747, 808, 845, 953, 960,
1009, 1015, 1024 seconds; median 899 s (14 min 59 s).

Created-to-merged of the last ten merged PRs (#336, #335, #332, #323, #322,
#320, #318, #315, #312, #311): 8.5, 12.3, 12.9, 15.8, 16.5, 16.8, 17.6, 19.9,
23.1, 53.7 minutes; median 16.7 min.

After numbers need ten PRs merged under the change; the orchestrator files that
follow-up.

## Validation

- `tests/check-codeql-changes.sh` (picked up by `scripts/test.sh` through
  `tests/check-*.sh`) builds a throwaway repository per case (base commit,
  pull request commit, `git merge --no-ff`) and runs the script there: 27
  cases, all passing. Skipped: Markdown under `docs/`, under `doc/`, at the top
  level, and all at once with an added and a deleted page. Analysed: `src/*.c`,
  `tests/unit/*.c`, `Makefile`, `third_party/*.make`, `.github/workflows/*`,
  Markdown under `.github/`, `scripts/*.sh`, `tests/check-*.sh`, `config.mk`
  plus `docs/payload.h`, `src/a.c` renamed to `docs/a.c` and to `docs/a.md`, a
  non-ASCII `src/` name, a `src/` and a `docs/` name holding a double quote, a
  deleted `src/` file, an empty diff, a single-parent HEAD, a root commit, a
  three-parent merge, a failing diff, a push event and no event; plus the
  stdout form. Each case also checks exit status 0, a log that names no file
  and no file list left behind.
- Mutants: with the round-1 step body as the script, 25 of the 27 cases fail,
  14 on the decision itself (among them `config.mk` plus `docs/payload.h`, the
  non-ASCII and both quoted names, the renames, `scripts/` and `tests/*.sh`,
  the empty diff and a HEAD that is not a merge), the rest because the log
  prints file names. With `--no-renames` dropped, the rename of `src/a.c` to
  `docs/a.md` fails.
- `ruby -ryaml` loads the workflow. The expressions were read against the
  Actions contexts documentation: `github.event.pull_request.number` is empty
  on push, so the group falls back to `github.sha`.
