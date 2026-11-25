#!/bin/sh
# The gate's invariants in CI (FIX115): runs every check named in
# tests/ci-checks.txt through scripts/gate-checks.sh, the scheduler that
# scripts/test.sh runs tests/check-*.sh with. The invariants job of
# .github/workflows/macos.yml runs this, with the <NAME>_REQUIRE variables
# the runner satisfies set to 1. tests/ci-local-only.txt names the checks
# that run on the build host's gate only, each with its reason, and
# tests/check-ci-parity.sh keeps the two lists equal to tests/check-*.sh.
#
# Fails closed: a name in the list that is not an executable
# tests/check-*.sh, or an empty list, is an error (status 2); otherwise the
# status is that of scripts/gate-checks.sh. A line of tests/ci-checks.txt is
# read as tests/check-ci-parity.sh reads it: a "#" starts a comment, the
# blanks (space, tab) around the name are dropped, a blank line is skipped,
# and the rest is one name, with no blank and no ":" in it; a carriage return
# anywhere is refused. --list prints the names and runs nothing, for that
# check to compare.
#
# The invariants job is a matrix of three (TASK211): CI_CHECKS_GROUP is
# fast, harness or ios, and the job runs the checks of tests/ci-checks.txt
# that scripts/ci-select.sh chooses for that group from tests/ci-map.txt.
# For harness on a pull request (GITHUB_EVENT_NAME pull_request), it passes
# the paths the change touches, HEAD^1..HEAD of the merge commit the
# checkout is, NUL-delimited; any other event, a HEAD that is not a merge
# commit or a git that fails passes "every path", so every harness check
# runs. Fails closed: a selector that fails, or prints a name it was not
# given or none for fast or ios, makes the job run every check of the list.
# Only the harness group may run none, and then the job passes. Unset, every
# check runs, as before. --select prints the names the group would run and
# runs nothing, for tests/check-ci-map.sh.
#
# Usage: scripts/ci-checks.sh [--list | --select]
set -eu
me=ci-checks

# MAKEFLAGS=-i or -n from the environment would turn a failing or empty
# build in a check into success
unset MAKEFLAGS GNUMAKEFLAGS MFLAGS

list=
select=
case ${1-} in
	--list) list=1 ;;
	--select) select=1 ;;
	"") ;;
	*) echo "$me: usage: scripts/ci-checks.sh [--list | --select]" >&2; exit 2 ;;
esac
cr=$(printf '\r')

cd "$(dirname "$0")/.."

set --
while IFS= read -r line || [ -n "$line" ]; do
	case $line in
		*"$cr"*) echo "$me: a line of tests/ci-checks.txt holds a carriage return" >&2; exit 2 ;;
	esac
	n=${line%%#*}
	while :; do case $n in [" 	"]*) n=${n#?} ;; *) break ;; esac; done
	while :; do case $n in *[" 	"]) n=${n%?} ;; *) break ;; esac; done
	[ -n "$n" ] || continue
	case $n in
		*[" 	:"]* | */* | .*) echo "$me: '$n' in tests/ci-checks.txt is not a tests/check-*.sh name" >&2; exit 2 ;;
		check-*.sh) ;;
		*) echo "$me: '$n' in tests/ci-checks.txt is not a tests/check-*.sh name" >&2; exit 2 ;;
	esac
	if [ ! -f "tests/$n" ] || [ ! -x "tests/$n" ]; then
		echo "$me: tests/$n, named in tests/ci-checks.txt, is not an executable check" >&2
		exit 2
	fi
	set -- "$@" "tests/$n"
done < tests/ci-checks.txt
if [ $# -eq 0 ]; then
	echo "$me: tests/ci-checks.txt names no check" >&2
	exit 2
fi
if [ -n "$list" ]; then
	for t; do printf '%s\n' "${t#tests/}"; done
	exit 0
fi

# The group of the invariants job's matrix (TASK211), or none: every check.
group=${CI_CHECKS_GROUP-}
case $group in
	"" | fast | harness | ios) ;;
	*) echo "$me: CI_CHECKS_GROUP is fast, harness or ios, or unset" >&2; exit 2 ;;
esac
if [ -n "$group" ]; then
	set -f
	# the candidates by name, kept for the fallback
	for t do set -- "$@" "${t#tests/}"; shift; done
	cands=" $* "
	tmp=$(mktemp -d "${TMPDIR:-/tmp}/ci-checks.XXXXXX")
	changed=-
	if [ "$group" = harness ] && [ "${GITHUB_EVENT_NAME-}" = pull_request ]; then
		# the checkout of a pull request is the merge of its head into its
		# base: HEAD^1 is the base, and the change is HEAD^1..HEAD
		if git rev-parse -q --verify 'HEAD^2^{commit}' > /dev/null 2>&1 &&
			git diff --no-renames --no-relative --name-only -z 'HEAD^1' HEAD > "$tmp/changed" 2> /dev/null; then
			changed=$tmp/changed
		else
			echo "$me: harness: the change cannot be read (HEAD is not a merge commit, or git failed); every harness check runs" >&2
		fi
	fi
	ok=1
	if bash scripts/ci-select.sh tests/ci-map.txt "$group" "$changed" "$@" > "$tmp/sel"; then
		set --
		while IFS= read -r n || [ -n "$n" ]; do
			case $cands in *" $n "*) ;; *) ok=0; break ;; esac
			case " $* " in *" tests/$n "*) ok=0; break ;; esac
			set -- "$@" "tests/$n"
		done < "$tmp/sel"
		# only the harness group may be empty (no harness check reads a
		# path the change touches)
		[ $# -gt 0 ] || [ "$group" = harness ] || ok=0
	else
		ok=0
	fi
	rm -rf "${tmp:?}"
	if [ "$ok" != 1 ]; then
		# fail closed: never fewer checks than the list
		echo "$me: $group: the selector failed or printed a name it was not given; every check of tests/ci-checks.txt runs" >&2
		set --
		for n in $cands; do set -- "$@" "tests/$n"; done
	fi
	set +f
fi
if [ -n "$select" ]; then
	for t; do printf '%s\n' "${t#tests/}"; done
	exit 0
fi
if [ $# -eq 0 ]; then
	echo "$me: harness: no harness check reads a path this change touches"
	exit 0
fi
exec scripts/gate-checks.sh "" "" "$@"
