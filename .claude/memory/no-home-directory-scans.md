---
name: no-home-directory-scans
description: "Never search or walk the user's home directory on the bare-metal Mac; stay in the repo, the game data directory and the pin store"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 370d35cf-771a-49ed-b0b9-dc5d44f9f073
  modified: 2026-11-14T16:09:38.441Z
---

Never run `find`, `ls -R`, `mdfind` or any other search that walks the home directory (or `/Users`) on the owner's bare-metal Mac mini. Stay inside the repo and its worktrees, the game data directory (read-only) and the pin store.

**Why:** On 2026-11-14 a search of the home directory for the game data walked the photo, document and cloud-drive folders and set off the operating system's privacy prompts; the owner objected.

**How to apply:** Use known paths; if a path is unknown, list one specific named directory or ask. Put the same limit in every subagent brief (my interpretation: the owner did not ask for that explicitly, but subagents share the risk).
