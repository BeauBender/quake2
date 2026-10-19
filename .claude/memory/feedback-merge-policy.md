---
name: feedback-merge-policy
description: The loop merges every PR itself (any risk tier, config and workflow changes included) once the review stamp is clean and the merge gate passes; never hand a PR to the owner to merge
metadata:
  type: feedback
---

The owner wants the loop to work autonomously and be responsible for secure delivery. Two preconditions were set, and both are enforced by the installed dev-loop (>= 0.6.0): the review panel (correctness, security, spec) writes a review stamp that `gh pr create` and `gh pr merge` require, and the merge gate requires a full local gate run on the exact head that contains current main, a pinned merge, and green fork CI where configured.

**Why:** The owner said "You work autonomously and are responsible for secure delivery", and later "why are you asking me to merge that pr? can you not do it? the dev-loop plugin is designed so that you can work autonomously. if there's any guidance that conflicts with that, let's correct it."

**How to apply:** Merge every PR yourself once it passes; there is no exception for `.claude/` config or `.github/workflows` changes. This repo sets `risk.autoMergeCap: "H"` so the plugin's own tier labels agree. Merge flow: rebase onto origin/main, `scripts/merge-gate.sh <sha>`, /dev-loop:review (writes the stamp) or `review-stamp.sh amend --rebased`, push, `gh pr merge <n> --squash --match-head-commit <sha>`. dev-loop hooks only govern the repo the session runs in: a merge against another repo from this session is not gated by them, so verify stamps by hand there. See [[feedback-devloop-workflow]] and [[feedback-plugin-root-env]].
