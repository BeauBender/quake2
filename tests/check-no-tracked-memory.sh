#!/bin/sh
# Invariant: agent memory is not part of this public repository.
#
# Fails when any file under .claude/memory/ is tracked (in the index), or
# when a file created there would not be ignored. Memory is kept in the
# owner's private store (scripts/agent-memory.sh), never in a commit.
#
# The self-test runs first, on throwaway repositories under a private
# mktemp -d, and shows the check fails on a tracked memory file (also one
# only staged), fails when the ignore rule is missing, and passes on a clean
# repository.
#
# Usage: tests/check-no-tracked-memory.sh
set -eu
cd "$(dirname "$0")/.."
me=check-no-tracked-memory
tmp=$(mktemp -d "${TMPDIR:-/tmp}/nomem.XXXXXX")
chmod 700 "${tmp:?}"
trap 'rm -rf "${tmp:?}"' EXIT HUP INT TERM

# check <repo-dir>: prints a reason and returns 1 on a violation.
check()
{
	n=$(git -C "$1" ls-files -- .claude/memory | wc -l | tr -d ' ')
	if [ "$n" != 0 ]; then
		echo "$me: $n file(s) under .claude/memory/ are tracked; agent memory belongs in the private store (scripts/agent-memory.sh), not in a commit (git rm -r .claude/memory)" >&2
		return 1
	fi
	if ! git -C "$1" check-ignore -q -- .claude/memory/probe.md; then
		echo "$me: .claude/memory/ is not ignored; add it to .gitignore" >&2
		return 1
	fi
	return 0
}

mkrepo()
{
	mkdir -p "${tmp:?}/$1"
	git -C "${tmp:?}/$1" init -q .
}

selftest()
{
	export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
	unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE

	mkrepo clean
	printf '%s\n' '.claude/memory' > "${tmp:?}/clean/.gitignore"
	check "$tmp/clean" 2>/dev/null ||
		{ echo "$me: selftest: a clean repository was rejected" >&2; exit 1; }

	mkrepo staged
	printf '%s\n' '.claude/memory' > "${tmp:?}/staged/.gitignore"
	mkdir -p "${tmp:?}/staged/.claude/memory"
	printf 'x\n' > "${tmp:?}/staged/.claude/memory/note.md"
	git -C "${tmp:?}/staged" add -f .claude/memory/note.md
	if check "$tmp/staged" 2>/dev/null; then
		echo "$me: selftest: a staged memory file was not detected" >&2
		exit 1
	fi

	mkrepo noignore
	if check "$tmp/noignore" 2>/dev/null; then
		echo "$me: selftest: a missing ignore rule was not detected" >&2
		exit 1
	fi
}

selftest
check .
echo "$me: ok"
