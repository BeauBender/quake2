#!/bin/sh
# SessionStart hook (matchers: startup, resume). Keeps the main checkout of
# this repository current, so the checked-in .claude/ settings and rules
# reach every machine. On the main checkout ($CLAUDE_PROJECT_DIR with a .git
# directory), it fetches origin main, on any branch, then fast-forwards to
# it when the checkout is on branch main, has no tracked changes, has no
# local commits that origin/main lacks, and its HEAD tracks nothing under
# .claude/memory/ (an update from such a commit deletes those files; see
# CLAUDE.md). The merge runs with git's --no-overwrite-ignore, so git
# itself refuses an update that would overwrite or delete a local file it
# does not track, ignored or not, including one inside a directory the
# update turns into a file. The fetch comes first so that every line can
# say how far behind origin/main the checkout is; it writes only in .git.
#
# It prints one line, which Claude Code adds to the context: updated from a
# to b, already current, or not updated with the count behind and the
# reason (a failed fetch, another branch, tracked changes, local commits on
# main, a diverged main, tracked memory, local files in the way, a failing
# git command). In a linked worktree (its .git is a file) it prints nothing
# and does nothing.
#
# It never runs reset, rebase, checkout, stash or clean, merges only with
# --ff-only, runs no git hooks, and writes nothing outside the checkout's
# .git and working tree. The caller's GIT_DIR, GIT_WORK_TREE, GIT_INDEX_FILE
# and command-line configuration are dropped, so it acts on the project
# directory only. Git's output, which can carry the remote URL or a path, is
# discarded or kept in a variable and only matched, never printed. The
# fetch has no terminal prompt. Every failure refuses the update and says
# so; the hook always exits 0, so it never blocks a session.
#
# Limits:
# - The fetch and the merge share the hook's 30-second timeout in
#   .claude/settings.json; a slow fetch leaves the merge less of it.
# - The fetch updates origin/main through the remote's fetch refspec; with
#   a non-standard one, origin/main stays stale and so does the count.
# - On another branch, the count is that branch's lag behind origin/main.
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

# Merge the commit counted above, not the ref, which a concurrent fetch can
# move. Git's text can name paths, so it is only matched for the reason.
if err=$(LC_ALL=C git -c core.hooksPath=/dev/null merge --ff-only --no-overwrite-ignore --no-autostash -q "$to" 2>&1 < /dev/null); then
	now=$(git rev-parse -q --verify HEAD 2> /dev/null) || now=unknown
	say "updated main from $a to $(printf '%.12s' "$now") ($n commit(s))"
fi
case $err in
*"untracked working tree files would be "* | *"would lose untracked files"*)
	why='local untracked or ignored files would be overwritten' ;;
*)
	why='git merge --ff-only failed (another git process may be running)' ;;
esac
say "$lag: $why; update by hand"
