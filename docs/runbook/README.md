# Card results

This directory holds what each card measured and observed, one file per
card: `docs/runbook/<CARD>.md` (for example `TASK55.md`, `FIX53.md`). The
file's first heading is the card's section heading.

The method lives in the shared runbook, [`docs/plans/metal-port-harness.md`](../plans/metal-port-harness.md):
what the pin harness is, how a run works and how a renderer is judged.

The index is [`INDEX.md`](INDEX.md) in this directory, one line per card:
a link to the card's file, a colon and the card's short title. The order is
free (by card id is tidy, `FIX` before `TASK`, numbers ascending, but no
check requires it).

The rule: a card writes its results to its own file here and adds only its
one index line to `INDEX.md`. It does not add a section to
`docs/plans/metal-port-harness.md`. Two cards then touch different files,
and where they both add a line to `INDEX.md`, the merge is by union
(`.gitattributes` marks it `merge=union`, and nothing else), keeping both
lines, so it needs no hand resolution.

The review stamp carries across a merge of the default branch for such a
file because it compares a union-merged doc-only file as a multiset of
lines. `INDEX.md` counts as doc-only: the repository's gate configuration
sets no doc-only globs of its own, so the defaults apply, and
`docs/runbook/INDEX.md` matches the glob `docs/*`; no exclude pattern (the
excludes name agent, command and skill prompt files, `CLAUDE.md` and
`AGENTS.md`) matches it. Keep it that way: do not move the index outside
`docs/`, and keep each line unique, since a union merge keeps a line both
sides added. `tests/check-runbook-index.sh` runs the two-branch merge in a
scratch repository, with the attribute (clean, both lines kept) and without
it (conflict).
