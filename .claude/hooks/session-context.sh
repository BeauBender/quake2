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
- Agent memory is never committed here; it is persisted to the owner's private store named in ~/.config/agents/memory_repo, cloned once per machine at ~/.config/agents/memory-store (no clone: report it and carry on without sync). At session start and after a memory changes, sync with exactly the pre-approved commands in CLAUDE.md, in order: cp -p the memory *.md into ~/.config/agents/memory-store/quake2/memory/, then git -C ~/.config/agents/memory-store add, commit, pull --rebase, push, then cp -p back. No other command shape; never force-push (see CLAUDE.md).
- Keep the list of modified files, the gate command and any open failures in view.
TXT
