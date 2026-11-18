#!/bin/sh
# Invariant: tests/check-cvar-docs.sh refuses what it must (FIX117).
#
# Runs its self-test (fixture trees, each case with its own verdict), then
# --verify-upstream: the committed tests/baselines/upstream-names.txt equals
# the names regenerated from the upstream commit. The plain check runs in
# the Stop hook, which gives its checks 40 s together, so the self-test
# lives here, in the gate only. --verify-upstream needs the upstream
# commit, which a shallow clone (CI) does not have: without it that step
# is skipped here, with the command that fetches it, and runs on every
# host that has the history. The plain check itself needs no history.
set -eu
cd "$(dirname "$0")/.."
sh tests/check-cvar-docs.sh --selftest
id=$(sed -n 's/^# id: //p' tests/baselines/upstream-names.txt)
case $id in
*[!0-9a-f]* | "") echo "check-cvar-docs-selftest: tests/baselines/upstream-names.txt names no upstream id" >&2; exit 1 ;;
esac
if git cat-file -e "$id^{commit}" 2> /dev/null; then
	sh tests/check-cvar-docs.sh --verify-upstream
else
	echo "check-cvar-docs-selftest: the upstream commit $id is not here; --verify-upstream skipped (git fetch --depth=1 origin $id)"
fi
