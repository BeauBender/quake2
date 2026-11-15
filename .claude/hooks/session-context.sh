#!/bin/sh
# SessionStart hook (matcher: compact). Re-states the working contract after
# the context is compacted, so the gate and the rules survive a summary.
#
# Usage: registered in .claude/settings.json; prints to stdout, which Claude
# Code adds to the new context.
cat <<'TXT'
quake2 working contract (re-stated after compaction):
- Gate: scripts/test.sh (repo invariants in tests/check-*.sh, then the q2ded and game build). Run it before saying a change is done.
- Never commit on main; work in a worktree on a branch and land through a PR (gh pr create, gh pr checks).
- Savegame format, network protocol and module API structs do not change without a card that asks for it.
- No installs or new dependencies without the owner's approval.
- This repository is public: nothing about other repositories goes into issues, PRs, commits or files.
- Agent memory is never committed here and the store is never named here (see CLAUDE.md). The store's one clone is ~/.config/agents/memory-store, and your memory directory is a symbolic link to its quake2/memory; nothing is copied. No clone or no link: report it and carry on; setup is only done with the owner present. These commands are pre-approved as exact text; type them exactly:
  sync, in order: `git -C ~/.config/agents/memory-store status --porcelain`; only if it printed anything, `git -C ~/.config/agents/memory-store add -A quake2/memory` and `git -C ~/.config/agents/memory-store commit -q -m quake2-memory-sync`; then `git -C ~/.config/agents/memory-store pull --rebase -q` and `git -C ~/.config/agents/memory-store push -q`
  on a rebase conflict, edit the conflicted files by hand, then `git -C ~/.config/agents/memory-store add -A quake2/memory`, `git -C ~/.config/agents/memory-store -c core.editor=true rebase --continue` and `git -C ~/.config/agents/memory-store push -q`; never force-push
  to remove a memory: `git -C ~/.config/agents/memory-store rm -q quake2/memory/<file>`, then sync
- Keep the list of modified files, the gate command and any open failures in view.
TXT
