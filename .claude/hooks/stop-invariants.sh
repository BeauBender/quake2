#!/bin/sh
# Stop hook. When the working tree has uncommitted changes under src/, doc/,
# docs/, tests/ or any Markdown file, run the fast repo invariants
# (tests/check-*.sh, about two seconds) and block the stop once if one fails.
# It never blocks twice in a row: when Claude Code reports stop_hook_active,
# the hook only prints the failure and lets the turn end.
#
# Usage: registered in .claude/settings.json; reads the hook JSON on stdin.
input=$(cat)
cd "$(git rev-parse --show-toplevel 2>/dev/null || echo .)" || exit 0
[ -n "$(git status --porcelain -- src doc docs tests '*.md' 2>/dev/null)" ] || exit 0

out=$(for check in tests/check-*.sh; do "$check" || echo "FAILED: $check"; done 2>&1)
case $out in
*FAILED:*) ;;
*) exit 0 ;;
esac

case $input in
*'"stop_hook_active":true'*|*'"stop_hook_active": true'*)
	echo "repo invariants still failing (not blocking again):" >&2
	echo "$out" | grep -v ': ok' >&2
	exit 0 ;;
esac
echo "repo invariants fail on the working tree; fix them or say why they cannot be fixed:" >&2
echo "$out" | grep -v ': ok' >&2
exit 2
