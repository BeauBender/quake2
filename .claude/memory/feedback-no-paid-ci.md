---
name: feedback-no-paid-ci
description: The owner will not pay for GitHub Actions; keep this repo's CI on free standard runners and never add paid runners or services
metadata:
  type: feedback
---

The owner will not pay for GitHub Actions. This repo is public, so its workflows on GitHub's standard hosted runners (Linux, Linux Arm, macOS, Windows) are free; it stays that way only while the repo is public and no larger runners or paid third-party services are added.

**Why:** The owner said CI that "requires github payments" is not something they will pay for.

**How to apply:** Never propose paid runners, paid scanning services, or making this repo private without flagging the Actions cost. A red CI run with zero steps can mean a billing block, not a code failure: check `gh api repos/<r>/check-runs/<job>/annotations` before debugging. See [[feedback-merge-policy]].
