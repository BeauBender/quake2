# FIX66: CodeQL analysis only when code or the build changes

Card FIX66 (issue 324). Workflow: `.github/workflows/codeql.yml`.

## Required checks

Read-only API queries: `main` has no branch protection (404), and its one
ruleset (`main-protect`) holds only `deletion` and `non_fast_forward`. No status
check is currently required. The change is still written so it stays safe if the
CodeQL check becomes required.

## Change

- A `changes` job (ubuntu, no third-party action) lists the files of the pull
  request with `git diff --name-only HEAD^1 HEAD` on the merge checkout
  (`fetch-depth: 2`; `HEAD^1` is the base). It sets `analyze=true` for `src/**`,
  `third_party/**`, `.github/**` (the workflow itself), `Makefile`, `*.make`
  and `tests/**/*.c` / `*.h`. Pushes to `main` always set it true.
- `analyze` has `needs: changes` and `if: needs.changes.outputs.analyze == 'true'`.
  No workflow-level `paths:` filter is used, since a workflow skipped by a path
  filter leaves a required check pending forever. A job skipped by a job-level
  `if:` reports "skipped" and counts as passing. Documentation: GitHub Docs,
  "Troubleshooting required status checks", section "Handling skipped but
  required checks" (docs.github.com, Repositories > Branches > Protected
  branches). The exact wording was not fetched by this lane; the orchestrator
  should confirm it against the page.
- Concurrency: group `${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}`,
  `cancel-in-progress` only when the event is `pull_request`, never for `main`.
- Cppcheck is untouched (30 seconds, not in the filter).
- There is no `schedule:` trigger today; one added later takes the
  non-pull-request branch and always analyses.

## Before numbers

Median c-cpp analysis job duration over the last ten pull request runs (run
ids 43768244403 down to 43715785219): 469, 702, 747, 808, 845, 953, 960,
1009, 1015, 1024 seconds; median 899 s (15 min 0 s).

Created-to-merged of the last ten merged PRs (#336, #335, #332, #323, #322,
#320, #318, #315, #312, #311): 8.5, 12.3, 12.9, 15.8, 16.5, 16.8, 17.6, 19.9,
23.1, 53.7 minutes; median 16.7 min.

After numbers need ten PRs merged under the change; the orchestrator files that
follow-up.

## Validation

`ruby -ryaml` loads the workflow. The path-filter regex was checked on sample
paths (docs and shell tests: no; `src/`, `tests/unit/*.c`, `Makefile`,
`third_party/*.make`: yes). The expressions were read against the Actions
contexts documentation: `github.event.pull_request.number` is empty on push, so
the group falls back to `github.ref`.
