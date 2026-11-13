# 0004: No change waits for a person

Status: decided by the owner, 2026-11-13.

## Context

The repository is worked by an agent loop configured in
`.claude/dev-loop.json`. The owner does not want to be a step in that loop:
no merge, review sample or decision card may wait for a person.

Two things still did. A change that adds a line shaped like a credential
was held for a person to merge, whatever its review said, and the vendored
SDL 3 source trips that pattern with ordinary C (variables whose names end
in `token`). And the loop's review sampling picks a share of merged changes
for a person to look at afterwards.

## Decision

- The loop merges every change itself once the review panel's stamp is
  clean, CI is green and the merge gate has passed on the exact head.
  This holds for every risk tier, including this configuration, the
  workflows and vendored code.
- Review sampling is off (`risk.sampleRate` is 0 for every tier).
- `third_party/` is added to `risk.secretAllowPaths`. Vendored trees are
  byte-identical to their upstream release and checked against a manifest
  by the gate (`tests/check-vendored.sh`), so a credential cannot enter
  there unnoticed without the manifest and its pin changing too.
- The credential pattern stays on everywhere else. A change that trips it
  outside the allowed paths is not merged and is not handed to a person:
  the loop removes the line from the branch, or, when it is a false
  positive, changes the code so it no longer matches. A real credential
  that was pushed is treated as leaked and reported.
- Decision cards (INFORM) record a decision the loop has already taken and
  acts on. They are information for the owner, who may redirect at any
  time; nothing waits for them to be closed.

## Consequences

- Nobody but the review panel reads a change before it lands. The panel
  (correctness, security and specification passes, each with proof for
  its findings), the gate and CI are the whole control.
- The credential scan no longer looks inside `third_party/`. The manifest
  check is what guards that tree.
- A wrong judgement by the loop lands on `main` and is corrected by a
  later change, not prevented by a second pair of eyes.

## Reversal

Remove `third_party/*` from `risk.secretAllowPaths`, restore the sampling
rates, and state in a new record which changes wait for a person.
