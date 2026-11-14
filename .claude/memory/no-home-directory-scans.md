---
name: no-home-directory-scans
description: "Never search or walk the user's home directory on the bare-metal Mac; stay in the repo, the game data dir and the pin store"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 370d35cf-771a-49ed-b0b9-dc5d44f9f073
  modified: 2026-11-14T16:09:38.441Z
---

Never run `find ~`, `ls -R ~`, `mdfind` or any search that walks the home directory (or `/Users`) on the owner's bare-metal Mac mini. Stay inside the repo/worktrees, `/Users/beaubender/Developer/no-git/q2data` (game data, read-only) and `/Users/beaubender/Developer/no-git/pins-store`.

**Why:** On 2026-11-14 a `find ~ -maxdepth 4 -name pak0.pak` to locate game data walked Pictures, Documents and iCloud Drive and set off macOS privacy prompts; the owner objected ("you just tried to access my photos, documents, icloud library, etc").

**How to apply:** Use known paths; if a path is unknown, list one specific directory or ask. Put the same limit in every subagent brief (my interpretation: the owner did not ask for that explicitly, but subagents share the risk).
