#!/bin/sh
# SessionStart hook (matchers: startup, resume). Keeps the main checkout of
# this repository current, so the checked-in .claude/ settings and rules
# reach every machine. On the main checkout ($CLAUDE_PROJECT_DIR with a .git
# directory), it fetches origin main, on any branch, then fast-forwards to
# it when the checkout is on branch main, has no tracked changes, has no
# local commits that origin/main lacks, its HEAD tracks nothing under
# .claude/memory/ (an update from such a commit deletes those files; see
# CLAUDE.md), and no path the update adds is taken by an untracked or
# ignored local file (git overwrites an ignored file, and one where the
# update needs a directory, without asking). The fetch comes first so that
# every line can say how far behind origin/main the checkout is; it writes
# only in .git.
#
# It prints one line, which Claude Code adds to the context: updated from a
# to b, already current, or not updated with the count behind and the
# reason (a failed fetch, another branch, tracked changes, local commits on
# main, a diverged main, tracked memory, paths in the way, counted but not
# named, a failing git command). In a linked worktree (its .git is a file)
# it prints nothing and does nothing.
#
# It never runs reset, rebase, checkout, stash or clean, merges only with
# --ff-only, runs no git hooks, and writes nothing outside the checkout's
# .git and working tree. The caller's GIT_DIR, GIT_WORK_TREE, GIT_INDEX_FILE
# and command-line configuration are dropped, so it acts on the project
# directory only. Git's output, which can carry the remote URL, is
# discarded. The fetch has no terminal prompt and is bounded by the hook's
# timeout in .claude/settings.json. Every failure refuses the update and
# says so; the hook always exits 0, so it never blocks a session.
#
# Usage: registered in .claude/settings.json; tested by
# tests/check-checkout-current.sh.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_OBJECT_DIRECTORY \
	GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE GIT_PREFIX GIT_CONFIG \
	GIT_CONFIG_PARAMETERS GIT_CONFIG_COUNT
GIT_TERMINAL_PROMPT=0
export GIT_TERMINAL_PROMPT
origin=refs/remotes/origin/main

# say <text>: print the one line and stop.
say()
{
	echo "checkout-current: $*"
	exit 0
}

dir=${CLAUDE_PROJECT_DIR:-}
[ -n "$dir" ] || say "not updated: CLAUDE_PROJECT_DIR is not set"
cd "$dir" 2> /dev/null || say "not updated: cannot enter the project directory"
# A linked worktree (or a submodule) has a .git file: not this hook's business.
if [ -f .git ]; then
	exit 0
fi
[ -d .git ] || say "not updated: the project directory is not a main checkout"

if ! git -c core.hooksPath=/dev/null fetch -q origin main < /dev/null > /dev/null 2>&1; then
	n=$(git rev-list --count "HEAD..$origin" 2> /dev/null) || n=unknown
	say "not updated, $n commit(s) behind origin/main as of the last fetch: git fetch origin main failed"
fi
to=$(git rev-parse -q --verify "$origin^{commit}" 2> /dev/null) ||
	say "not updated: there is no origin/main after the fetch"
from=$(git rev-parse -q --verify 'HEAD^{commit}' 2> /dev/null) ||
	say "not updated: cannot read HEAD"
n=$(git rev-list --count "$from..$to" 2> /dev/null) ||
	say "not updated: cannot count the commits behind origin/main"
lag="not updated, $n commit(s) behind origin/main"

branch=$(git symbolic-ref -q HEAD 2> /dev/null) || branch=
[ "$branch" = refs/heads/main ] || say "$lag: the checkout is not on branch main"
st=$(git --no-optional-locks status --porcelain --untracked-files=no 2> /dev/null) ||
	say "$lag: git status failed"
[ -z "$st" ] || say "$lag: tracked changes in the working tree or index"
mem=$(git ls-tree -r --name-only "$from" -- .claude/memory 2> /dev/null) ||
	say "$lag: git ls-tree failed"
[ -z "$mem" ] || say "$lag: HEAD tracks files under .claude/memory/, which the update would delete; save them first as CLAUDE.md says for updating a checkout from such a commit, then update by hand"

a=$(printf '%.12s' "$from")
[ "$from" != "$to" ] || say "main is already current with origin/main ($a)"
ahead=$(git rev-list --count "$to..$from" 2> /dev/null) ||
	say "$lag: cannot count the local commits on main"
if [ "$ahead" -gt 0 ]; then
	[ "$n" -gt 0 ] || say "$lag: main has $ahead local commit(s) not on origin/main"
	say "$lag: main has diverged ($ahead local commit(s) not on origin/main); nothing was merged"
fi

# The paths the update adds, one per line. diff-tree does no rename
# detection unless asked, so a renamed file's new path is listed too. With
# core.quotePath=false only a path holding a control character, a double
# quote or a backslash comes quoted; such a line is not checked and counts
# as in the way.
added=$(git -c core.quotePath=false diff-tree -r --name-only --diff-filter=A "$from" "$to" 2> /dev/null) ||
	say "$lag: git diff-tree failed"
# untracked <path>: true unless HEAD tracks <path>; a git error counts as true.
untracked()
{
	! git cat-file -e "$from:$1" 2> /dev/null
}
# inway: read paths, one per line, and print how many are in the way. A
# path is in the way when something HEAD does not track is at it, or is a
# link or a non-directory at one of the directories above it. (A function,
# not inline: bash 3.2 misparses a case pattern inside $(...).)
inway()
{
	k=0
	while IFS= read -r p; do
		[ -n "$p" ] || continue
		hit=
		case $p in
		\"*) hit=1 ;;
		*) if { [ -e "$p" ] || [ -L "$p" ]; } && untracked "$p"; then hit=1; fi ;;
		esac
		q=$p
		while [ -z "$hit" ]; do
			case $q in
			*/*) q=${q%/*} ;;
			*) break ;;
			esac
			if { [ -L "$q" ] || { [ -e "$q" ] && [ ! -d "$q" ]; }; } && untracked "$q"; then hit=1; fi
		done
		[ -z "$hit" ] || k=$((k + 1))
	done
	echo "$k"
}
# Anything but 0, an empty count included, refuses.
k=$(printf '%s\n' "$added" | inway)
[ "$k" = 0 ] ||
	say "$lag: $k path(s) the update adds are taken by untracked or ignored local files, which the update would overwrite; move them, then update by hand"

if git -c core.hooksPath=/dev/null merge --ff-only --no-autostash -q "$origin" < /dev/null > /dev/null 2>&1; then
	now=$(git rev-parse -q --verify HEAD 2> /dev/null) || now=unknown
	say "updated main from $a to $(printf '%.12s' "$now") ($n commit(s))"
fi
say "$lag: git merge --ff-only failed"
