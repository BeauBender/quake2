# 0015. A card closes only when every acceptance criterion has a verdict

Status: decided by the owner, 2026-11-25.
Card: TASK210. Enforced by [`scripts/card-acceptance.sh`](../../scripts/card-acceptance.sh),
[the close workflow](../../.github/workflows/card-acceptance.yml), [the pull-request
check](../../.github/workflows/card-acceptance-pr.yml) and
[`tests/check-card-acceptance.sh`](../../tests/check-card-acceptance.sh).

## Context

A card's acceptance criteria say what "done" means, but nothing checked them
when the card closed. Cards closed with criteria unmet and unticked: FIX51
closed with part of its acceptance unmet, and TASK22 closed with its hands-on
lines open until they were split out into a new card. The leftover work
survived only where someone noticed it later. Closed cards routinely kept
every box unticked, so a ticked box said nothing either.

## Decision

1. A card closes as completed only when every acceptance criterion in its
   body has one of three verdicts, written in the card itself:
   - **met**: its box is ticked (`- [x]`);
   - **moved**: its text says `moved to #N`, the card that now holds it;
   - **dropped**: its text says `dropped: <reason>`, and the owner agreed.
2. The rule is checked by a script, not by a reviewer. Its criteria are the
   list items under an `## Acceptance` or `## Acceptance criteria` heading
   (levels 2 to 4); `scripts/card-acceptance.sh` states the grammar.
3. A card closed as completed with a criterion that has no verdict is
   reopened as `state:review`, with a comment naming the criteria, and the
   workflow run fails. This holds for every way a card closes: a merged pull
   request, the loop's ship path, `gh issue close` or the web UI.
4. A pull request that closes a card is red until that card's criteria all
   have verdicts, so the gap shows before the merge, not after.
5. A card closed as not planned or as a duplicate, a promoted card, and an
   issue with no `state:*` label are not checked.

## Costs

- Closing a card takes one more edit: the verdicts in its body. The loop's
  ship path does not write them yet; a plugin card asks it to, and until then
  the agent that ships a card writes them by hand before the merge.
- A tick is still the closer's word. The rule makes every criterion answered
  and visible; it does not prove a criterion met. "dropped" relies on the
  closer to have the owner's agreement.
- The pull-request check re-runs on a push or an edit of the pull request,
  not on an edit of the card, so after updating a card the closer edits the
  pull request (or re-runs the check). Its copy of the script is the pull
  request's own; the close workflow runs the default branch's.
- Cards closed before this record are not rechecked.

## Reversing it

Delete the two workflows, the script and its check (and its line in
`tests/ci-checks.txt`), and write a record that supersedes this one.
