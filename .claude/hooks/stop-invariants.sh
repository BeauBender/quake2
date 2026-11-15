#!/bin/bash
# Stop hook. When the working tree has uncommitted changes under src/, doc/,
# docs/, tests/ or any Markdown file, run the repo invariants named in
# $checks below and block the stop once if one fails, is missing or does not
# finish by the deadline. It never blocks twice in a row: when Claude Code
# reports stop_hook_active, the hook only prints the failure and lets the
# turn end. Apart from the two failures below that keep it from running the
# checks at all, it never blocks on its own error: only the one line that
# sets status=2 makes it exit 2, whatever else goes wrong; bash runs it in
# POSIX mode, as /bin/sh does, so an expansion error (a date command that
# prints nothing, say) ends it, and the EXIT trap makes that exit 0.
#
# The list holds only the checks that build nothing. The others build
# ref_metal, the Metal shim or the pin harness, or run throwaway git
# repositories, and take minutes together; scripts/test.sh runs all of them.
# Measured on a copy of HEAD with a change under src/, the slowest of
# three runs each (seconds):
#   check-compat-names.sh   0.3   check-removed-deps.sh     0.3
#   check-cvar-docs.sh      1.7   check-savegame-tables.sh  0.2
#   check-doc-links.sh      0.4   check-title-fit.sh        0.2
#   check-mode-size.sh      0.3   check-vendored.sh         0.3
# 3.7 s in all; the whole hook took 3.7 to 4.1 s in three runs. The Stop
# entry in .claude/settings.json gives it 60 s. tests/check-stop-invariants.sh
# fails when the hook takes more than half of that, when the list names a
# check that does not exist, or when the deadline and the grace do not fit
# well inside the timeout.
#
# The deadline: the checks run one at a time, each in its own process group
# (set -m), and $deadline seconds after the first one starts the one still
# running gets TERM, its whole group KILL $grace seconds later; the checks
# not started by then are not run. Both are reported, and block like a
# failure; the hook then ends about $deadline + $grace s after it started.
# It signals only the process groups it started.
#
# It needs bash: the deadline holds only with job control, and dash, or zsh
# run as sh, give none without a terminal. When set -m does not take
# effect, or no temporary directory can be made under $TMPDIR or /tmp, the
# hook runs no check, says why, and blocks once like a failure.
#
# Usage: registered in .claude/settings.json; reads the hook JSON on stdin.
checks='
tests/check-compat-names.sh
tests/check-cvar-docs.sh
tests/check-doc-links.sh
tests/check-mode-size.sh
tests/check-removed-deps.sh
tests/check-savegame-tables.sh
tests/check-title-fit.sh
tests/check-vendored.sh
'
deadline=40
grace=2

[ -z "${BASH_VERSION:-}" ] || set -o posix
status=0
input=$(cat)

# report <text>: print the failures and exit as the block-once rule says.
report()
{
	case $input in
	*'"stop_hook_active":true'*|*'"stop_hook_active": true'*)
		echo "repo invariants still failing (not blocking again):" >&2
		printf '%s\n' "$1" >&2
		exit 0 ;;
	esac
	echo "repo invariants fail on the working tree; fix them or say why they cannot be fixed:" >&2
	printf '%s\n' "$1" >&2
	status=2
	exit 2
}

cd "$(git rev-parse --show-toplevel 2>/dev/null || echo .)" || exit 0
[ -n "$(git status --porcelain -- src doc docs tests '*.md' 2>/dev/null)" ] || exit 0

tmp=$(mktemp -d "${TMPDIR:-/tmp}/stopinv.XXXXXX" 2> /dev/null) ||
	tmp=$(mktemp -d /tmp/stopinv.XXXXXX 2> /dev/null) ||
	report "NOT RUN: any listed check, no temporary directory could be made under ${TMPDIR:-/tmp} or /tmp"
c=
w=
trap 'rm -rf "${tmp:?}"; exit "$status"' EXIT
trap '[ -z "$c" ] || kill -KILL -- "-$c"; [ -z "$w" ] || kill -KILL -- "-$w"; exit' HUP INT TERM
: > "${tmp:?}/log"

# Job control puts each background job in a process group of its own,
# led by $!. Its notices about killed jobs go to /dev/null with the rest
# of the loop's stderr; the checks write to $tmp/out. No check or watcher
# holds the hook's stderr (fd 3 here), so nothing left behind by a check
# keeps Claude Code reading it after the hook exits.
exec 3>&2 2> /dev/null
(set -m) && set -m
case $- in
*m*) ;;
*)
	echo "NOT RUN: any listed check, this shell has no job control (set -m) to keep the ${deadline} s deadline" >> "${tmp:?}/log"
	checks= ;;
esac
end=$(($(date +%s) + deadline))
for check in $checks; do
	if [ ! -f "$check" ]; then
		echo "MISSING: $check, listed in .claude/hooks/stop-invariants.sh" >> "${tmp:?}/log"
		continue
	fi
	left=$((end - $(date +%s)))
	if [ "$left" -le 0 ]; then
		echo "NOT RUN: $check, the ${deadline} s deadline had passed" >> "${tmp:?}/log"
		continue
	fi
	rm -f "${tmp:?}/late"
	"$check" < /dev/null > "${tmp:?}/out" 2>&1 3>&- &
	c=$!
	(
		sleep "$left"
		: > "${tmp:?}/late"
		kill -TERM -- "-$c"
		sleep "$grace"
		kill -KILL -- "-$c"
	) < /dev/null > /dev/null 2>&1 3>&- &
	w=$!
	rc=0
	wait "$c" || rc=$?
	kill -KILL -- "-$w"
	wait "$w"
	if [ -f "${tmp:?}/late" ]; then
		echo "DID NOT FINISH: $check, stopped at the ${deadline} s deadline" >> "${tmp:?}/log"
	elif [ "$rc" -ne 0 ]; then
		grep -v ': ok' "${tmp:?}/out" >> "${tmp:?}/log"
		echo "FAILED: $check" >> "${tmp:?}/log"
	fi
	c=
	w=
done
set +m
exec 2>&3 3>&-

[ -s "${tmp:?}/log" ] || exit 0
report "$(cat "${tmp:?}/log")"
