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
- Keep the list of modified files, the gate command and any open failures in view.
TXT
