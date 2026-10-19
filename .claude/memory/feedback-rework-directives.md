---
name: feedback-rework-directives
description: Rework directives that hand-specify security or concurrency mechanisms keep introducing new Blockers; prescribe the property and a proven primitive, not a hand-built mechanism
metadata:
  type: feedback
---

On a recent dev-loop change (2026-10-18/19), three of five review rounds were blocked by mechanisms the orchestrator itself dictated in the previous rework: reading a command from a freshly fetched remote config (remote code execution at session start), letting a guard exit 0 when its two detectors disagreed (fail-open via an apostrophe in a shell comment), and a dead-pid lock "steal" by rename plus a process-group time box (two concurrent holders; children surviving a kill). Inferring the tested tree from command text took three rounds before being replaced by a script that tests the exact sha in its own worktree.

**Why:** A rework brief written under time pressure tends to name a quick mechanism; the implementer builds exactly that, and the next panel finds the hole in the mechanism, not in the implementation.

**How to apply:** In a rework directive, state the required property (fail closed on disagreement, one holder at a time, the tested tree is the merged tree) and prefer kernel or git primitives (flock, git merge-tree, a dedicated script that runs on the exact sha) over hand-built protocols. When two rounds find variants of one bug class, change the design rather than patch the next variant. See [[feedback-merge-policy]].
