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
# status is that of scripts/gate-checks.sh.
#
# Usage: scripts/ci-checks.sh
set -eu
me=ci-checks

# MAKEFLAGS=-i or -n from the environment would turn a failing or empty
# build in a check into success
unset MAKEFLAGS GNUMAKEFLAGS MFLAGS

cd "$(dirname "$0")/.."

set --
while IFS= read -r line || [ -n "$line" ]; do
	line=${line%%#*}
	n=$(printf '%s' "$line" | tr -d ' \t')
	[ -n "$n" ] || continue
	case $n in
		*/* | .*) echo "$me: '$n' in tests/ci-checks.txt is not a tests/check-*.sh name" >&2; exit 2 ;;
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
exec scripts/gate-checks.sh "" "" "$@"
