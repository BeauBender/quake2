---
name: feedback-devloop-workflow
description: How the user wants work run in this repo - dev-loop for all orchestration, INFORM cards for decisions needing their input, Issues filed in the fork, no Homebrew or dependency installs without explicit approval, be explicit about where things were filed
metadata:
  type: feedback
---

Use the dev-loop plugin for all orchestration in this repo: cards as GitHub Issues on BeauBender/quake2, plan anchors under docs/plans/, /dev-loop:orchestrate to pull work, the pre-pr ritual before any PR. Decisions the user should weigh in on go in INFORM cards (owner BeauBender) and dependent work cards list the INFORM id in depends_on. Do not install Homebrew packages or any dependency without the user's explicit approval. When filing anything, say exactly which repository it landed in.

**Why:** The user said "use the dev-loop plugin for all of your orchestration, and do all of the work within this fork, including filing github issues", "file an INFORM card for where you need my feedback", "do not install homebrew or any dependencies without my explicit approval", and asked "you're filing issues in my fork right? i don't see any there" when earlier issues had gone to another repo.

**How to apply:** Before acting, check the kanban (claim-card.sh ready) and the open INFORM cards. Prune aggressively only where confidence is high; otherwise INFORM + dependent card. The worktree guard is on: all edits happen in a worktree, main checkout is read-only except .claude/memory. See [[project-yquake2-fork-review]].
