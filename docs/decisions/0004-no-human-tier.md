# 0004. No change waits for a person

Status: decided by the owner, 2026-11-13.

Number 0003 is taken by a record that lands separately.

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
- `third_party/` is added to `risk.secretAllowPaths`. This change lands
  before the change that vendors SDL 3, so `third_party/` does not exist
  yet. The manifest check `tests/check-vendored.sh` arrives with the change
  that adds the tree. It will compare vendored files with their upstream
  release and refuse any file the manifest does not list. Until it lands,
  the path is exempt from the credential scan and only the review panel
  reads the diff.
- The credential pattern stays on everywhere else. A change that trips it
  outside the allowed paths is not merged and is not handed to a person:
  the loop removes the line from the branch, or, when it is a false
  positive, changes the code so it no longer matches. A real credential
  that was pushed is treated as leaked and reported.
- Decision cards (INFORM) record a decision the loop has already taken and
  acts on. They are information for the owner, who may redirect at any
  time; nothing waits for them to be closed.

## Supersedes

Some sentences in the tree say a card or a merge waits for the owner. This
record replaces them with one rule: an owner-gated decision card is decided
by the loop, which records the decision, its reasons and how to reverse it
on the card and proceeds. The owner may reverse it afterwards.

The sentences are in these files:

- `docs/plans/metal-port-agent-prompt.md` and `docs/plans/metal-port-cards.md`
  (the owner-gated card and the owner gates).
- `docs/plans/metal-renderer.md` (the owner-gated decision cards).
- `docs/plans/metal-raytracing-design.md` (the owner checkpoint).
- `docs/plans/trust-boundary.md` (the owner-gated decision cards and the
  removals that wait for them).
- `docs/decisions/0001-remove-optional-dependencies.md` (cards gated on
  owner decisions).
- The committed memory notes under `.claude/memory/` that say decisions
  unblock dependents when the owner closes them.

A follow-up card corrects those sentences. Until it lands, this record
wins where they disagree.

These standing rules remain and this record does not touch them:

- No package is installed and no dependency is added without the owner's
  approval.
- The savegame format, the network protocol and the module API structs do
  not change without a card that asks for it.
- Game data is never uploaded.
- Work blocked on hardware stays blocked.

## Consequences

- Nobody but the review panel reads a change before it lands. The panel
  (correctness, security and specification passes, each with proof for
  its findings), the gate and CI are the whole control.
- The credential scan does not look inside `third_party/`. Once the
  vendored tree exists, the manifest check guards it. A change that edits
  a vendored file, the manifest and the pin together passes the manifest
  check and is not flagged by the scan, so only the panel stands there.
  The manifest check must refuse files it does not list, or any file
  parked under `third_party/` dodges the scan.
- Other paths already let a credential-shaped line through unscanned: the
  existing allow paths (`*.md` at any depth, tests, fixtures, lock files).
  Pinning the list here means a later plugin default will not reach this
  repository.
- A wrong judgement by the loop lands on `main` and is corrected by a
  later change, not prevented by a second pair of eyes.

## Reversal

Set `risk.autoMergeCap` and `risk.unattendedCap` (both "H", which is what
lets every tier merge) to a lower tier, remove `third_party/*` from
`risk.secretAllowPaths`, restore the sampling rates, and state in a new
record which changes wait for a person. A head the classifier cannot read
is still held by the tooling, whatever these settings say.
