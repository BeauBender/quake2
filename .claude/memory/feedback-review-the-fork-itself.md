---
name: feedback-review-the-fork-itself
description: "User wants security reviews done on the code in their fork directly, not by comparing to or trusting upstream reputation; no external fetches or builds without asking"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 4bb2b1af-a9c5-43ee-ab2c-fa8fae19d86e
  modified: 2026-10-18T03:44:51.038Z
---

When asked to review this repo for safety/security/privacy, audit the code in the working tree itself. Do not substitute "identical to upstream" or upstream's reputation for a code review, do not fetch upstream sources or external releases for comparison, and do not build or run anything before asking.

**Why:** The user said: "being byte identical to the original repo doesn't mean the original repo was free from vulnerabilities" and "i'm focusing on the fork of the code, which is what i'll be running and iterating on ... changes may continue to only live in my fork." They also objected to a smoke build run mid-review.

**How to apply:** Review trust boundaries in the tree (network parsing, file-format loaders, filesystem paths, console/cvar, savegames, build scripts, binary blobs) with file:line evidence. Treat the fork as the sole source of truth. Ask before executing any repo code, including make. See [[project-yquake2-fork-review]].
