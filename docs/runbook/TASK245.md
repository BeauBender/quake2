## TASK245: merge.allowedDrift lists the documentation paths

A lane whose head is behind main merges without a re-gate when every file main changed since the merge-base is on the list in `merge.allowedDrift` (`.claude/dev-loop.json`). Before this card the list was the plugin's default, `.claude/memory/*`, so any merge on main that only adds a card file made every open lane stale.

### How the guard uses it

Quoted from the dev-loop plugin (read only), `hooks/guard-risk-tier.sh`, the merge legs:

```
#   main-current passes when origin/<default> is an ancestor of the head, OR
#   every file main changed since the merge-base matches merge.allowedDrift
#   (default .claude/memory/*; NOT gate.docOnlyGlobs): ...
#   A main commit touching any path outside merge.allowedDrift blocks (the first such path is
#   logged to .claude/.pr-checks/merge-guard.log, never echoed into the notice).
```

and the call:

```
if ! git merge-base --is-ancestor "refs/remotes/$mg_rmt/$mg_def" "$mg_head" 2>/dev/null; then
    mg_mb=$(git merge-base "$mg_head" "refs/remotes/$mg_rmt/$mg_def" ...)
    mg_nondoc=$(devloop_first_non_allowed_path "$mg_mb" "refs/remotes/$mg_rmt/$mg_def")
```

`lib/doc-only.sh`:

```
devloop_first_non_allowed_path() {
    local globs
    globs=$(devloop_committed_config merge.allowedDrift '[".claude/memory/*"]')
    _devloop_first_non_doc_with "$1" "$2" "$globs"
}
```

Semantics read from that code:

- The list is read from the default branch's committed config ("the newer of the local default branch and origin/<default>"). So this change takes effect for lanes once it is on main, not on the lane that carries it.
- The list replaces the default (the default is only the fallback when the key is absent), so `.claude/memory/*` is kept in it.
- Matching is a shell `case` pattern per changed path (`case "$path" in $glob)`), so `*` also matches `/`: `docs/runbook/*` covers the whole directory tree. The diff is `git diff --no-renames --name-only`, so a file moved out of a listed path counts as a change to its source path. An unreadable diff prints `(diff unreadable)` and blocks (fails closed). The post-merge trust check uses the same list.
- It governs only the `main-current` leg. The merge-gate (a green gate run on exactly the head), the pin to the head and the CI check are unchanged.

### The rule and the evidence

A path is listed only if no check the gate runs reads it in a way a change there could turn red on a code head. Searched with `grep -rnE "<path>" tests scripts Makefile .github` (hits that are only a comment citing a document are not reads).

| Path | Read by | Verdict |
| --- | --- | --- |
| `.claude/memory/*` | nothing in the gate | kept (the plugin's default) |
| `docs/reviews/*` | comments only (`tests/check-bounded-run.sh`); every `.md` is read by `tests/check-doc-links.sh` | listed |
| `docs/audits/*` | no reference in tests, scripts, Makefile or workflows; `check-doc-links.sh` as above | listed |
| `docs/analysis/*` | no reference; `check-doc-links.sh` as above | listed |
| `docs/decisions/*` | comments and skip messages only (`check-removed-deps.sh`, `check-card-acceptance.sh`, `check-ios.sh`, ...); `check-doc-links.sh` | listed |
| `docs/runbook/*` | `tests/check-runbook-index.sh`, `tests/check-runbook-dedupe.sh`, `tests/check-doc-links.sh` | listed, argued below |
| `docs/plans/*` | `tests/metal/shim-coverage.sh` (`doc=docs/plans/metal-shim-design.md`, parsed for the entry-point table, run by `tests/check-metal-shim.sh`) and `tests/check-runbook-index.sh` (`method=docs/plans/metal-port-harness.md`, a link target it requires) | left out |
| `doc/*` | `doc/040_cvarlist.md` and `doc/050_commands.md` are read by the cvar and command checks | left out |

`check-doc-links.sh` runs over every tracked Markdown file for each listed path. A drift there could turn it red only if a lane links to a file main removed or renamed in these directories; the lane head does not contain main's change, the merged tree does, and main's own CI runs the check after every merge. The failure is a one-line link fix in a document, caught by CI, and cannot touch code.

`docs/runbook/*` is the case the card is for (the merges #834 and #841 touched only `docs/runbook`) and needs the argument. `check-runbook-index.sh` requires every card file to have its index line and every line a file. Main's tree is consistent by its own gate. The lane adds at most its own `<CARD>.md` and one `INDEX.md` line; `INDEX.md` is merged by union (`.gitattributes`), `check-runbook-index.sh` runs the two-branch merge itself, and `check-runbook-dedupe.sh` removes a line both sides added. A red there needs a lane and main to add the same card id, which the card ids and claims prevent, and it would show in main's CI after the merge and be a doc fix.

`docs/plans/*` stays out because two checks read it: a plan changing on main can break the shim coverage or the index check on a code head. Plans changes therefore still ask for a re-gate.

### The final list

```
"allowedDrift": [".claude/memory/*", "docs/runbook/*", "docs/reviews/*", "docs/audits/*", "docs/analysis/*", "docs/decisions/*"]
```

### Dry run, no merge

`devloop_first_non_allowed_path` reads the default branch's committed config, which does not yet hold this list, so the dry run sources the plugin's `lib/doc-only.sh` and calls the function it wraps, `_devloop_first_non_doc_with <base> <tip> <globs>`, with the list read from this lane's `.claude/dev-loop.json`. Each range is one real merged commit on main, as the drift main would have made against a stale lane. Empty means the guard passes; a path means it blocks and names that path.

```
globs: [".claude/memory/*", "docs/runbook/*", "docs/reviews/*", "docs/audits/*", "docs/analysis/*", "docs/decisions/*"]
2cfcff7d^..2cfcff7d -> []                                              # docs/runbook only (#834)
f96b4487^..f96b4487 -> []                                              # docs/reviews only (#807)
8ae2f406^..8ae2f406 -> []                                              # docs/runbook only (#841)
c8ceeaf5^..c8ceeaf5 -> [src/client/refresh/metal/common/mtl_rtmt_math.h]   # code (#840)
d1cfe64a^..d1cfe64a -> [tests/check-image-decoder-selftest.sh]        # tests (FIX201)
2cfcff7d^..8ae2f406 -> [.github/workflows/prune-branches.yml]         # a mixed range stops at the first non-listed path
```

### Checks run

`tests/check-runbook-index.sh`, `tests/check-doc-links.sh` and the validity of `.claude/dev-loop.json` as JSON (no check in `tests/` reads that file; `scripts/pins.sh` only names it in a message).
