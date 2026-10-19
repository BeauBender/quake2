---
name: feedback-one-agent-at-a-time
description: Until Friday 2026-10-23 9:00 AM Pacific, run at most one subagent at a time (implementers and review passes included) and do the work sequentially
metadata:
  type: feedback
---

The owner (2026-10-19): "we're running out of anthropic quota so do all this work sequentially with never more than 1 subagent at a time working, including pr review agents", then: "FRIDAY at 9am pacific. you have several days to stay at 1 agent".

**Why:** the owner's Anthropic subscription quota resets on Friday 2026-10-23 at 9:00 AM Pacific.

**How to apply:** until then, never have two subagents live at once: run the review panel's passes one after another, wait for an implementer to finish before spawning a reviewer, and prefer doing small mechanical work inline. After that time the limit lifts. See [[feedback-devloop-workflow]].
