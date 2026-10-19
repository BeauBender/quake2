---
name: feedback-plugin-root-env
description: Always export CLAUDE_PLUGIN_ROOT when calling dev-loop scripts from the Bash tool, and pass -R BeauBender/quake2 to gh, or risk tiers silently degrade and gh targets upstream
metadata:
  type: feedback
---

When invoking any dev-loop script (`risk-tier.sh`, `receipt.sh`, `devloop_config`, `ship-finish.sh`, ...) from the Bash tool, export `CLAUDE_PLUGIN_ROOT` to the installed dev-loop version directory under `~/.claude/plugins/cache/`. Also pass `-R BeauBender/quake2` (or run `gh repo set-default BeauBender/quake2` once per checkout) for every `gh` call: this repo is a fork and `gh` otherwise targets yquake2/yquake2.

**Why:** Without the variable, `lib/config.sh` does not read the plugin's defaults, and `risk-tier.sh classify` reported tier M for a change deleting CI workflows that is tier H with the variable set.

**How to apply:** Treat a `reasons=none` or `reasons=large-diff` verdict on a diff touching `.github/`, `.claude/`, hooks or the Makefile as suspect and re-run with the variable.

**Also:** `git push` on this Mac uses a keychain credential separate from gh's token, which can lack the `workflow` scope. Push branches that touch `.github/workflows` as `git -c credential.helper= -c 'credential.helper=!gh auth git-credential' push ...`. The owner sometimes works from the mobile app, where `!`-prefixed commands do not run; run interactive `gh auth` steps in the background and relay the one-time code. See [[feedback-devloop-workflow]].
