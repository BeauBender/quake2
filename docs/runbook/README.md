# Card results

This directory holds what each card measured and observed, one file per
card: `docs/runbook/<CARD>.md` (for example `TASK55.md`, `FIX53.md`). The
file's first heading is the card's section heading.

The method lives in the shared runbook, [`docs/plans/metal-port-harness.md`](../plans/metal-port-harness.md):
what the pin harness is, how a run works and how a renderer is judged.
That file ends with a `## Card results` section, one line per card,
sorted by card id (`FIX` before `TASK`, numbers ascending), in the form
`- [<CARD>](../runbook/<CARD>.md): <the card's short title>`.

The rule: a card writes its results to its own file here and adds only its
one index line to the shared runbook. It does not add a section to
`docs/plans/metal-port-harness.md`. Two cards then touch different files
and different lines, and do not conflict.
