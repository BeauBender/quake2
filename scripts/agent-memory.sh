#!/bin/sh
# Copy agent memory files between the agent's memory directory and the
# owner's private memory store.
#
# Agent memory is never committed to this repository. It lives in a private
# store, a git repository whose "owner/name" is read from a file outside the
# tree (one line): ${AGENT_MEMORY_REPO_FILE:-$HOME/.config/agents/memory_repo}.
# This script and the docs never contain that name, and the script never
# prints it or the content of any memory file (it says "the private store").
#
# Usage:
#   scripts/agent-memory.sh pull [--force] <memory-dir>
#   scripts/agent-memory.sh push [--force] <memory-dir>
#
#   pull  copies quake2/memory/*.md from the store into <memory-dir>. It
#         never deletes a local file, and it does not overwrite a local file
#         whose content differs and that is newer than the store's last
#         commit of it, unless --force.
#   push  first updates the store clone (fast-forward only; it stops if that
#         is not possible) and pulls as above, then copies <memory-dir>/*.md
#         into quake2/memory/, commits with a fixed message and pushes
#         without force. Nothing changed, nothing committed.
#
# The clone lives under ${XDG_CACHE_HOME:-$HOME/.cache}/agents/memory-store
# (mode 700). The clone URL is https://github.com/<owner/name>.git unless
# AGENT_MEMORY_REMOTE is set (the selftest points it at a local bare
# repository: tests/check-agent-memory.sh). Every failure exits non-zero.
#
# POSIX sh; also runs under bash 3.2 (stock macOS).
set -eu

SUBDIR=quake2/memory
COMMIT_MSG="memory: sync"

me=agent-memory

die()
{
	printf '%s: %s\n' "$me" "$*" >&2
	exit 1
}

usage()
{
	printf 'usage: %s pull|push [--force] <memory-dir>\n' "$0" >&2
	exit 2
}

mtime()
{
	# GNU stat first: BSD stat rejects -c, while GNU stat accepts -f
	# (filesystem status) and would print something else.
	stat -c %Y "$1" 2>/dev/null || stat -f %m "$1"
}

cmd=
force=0
memdir=
for arg in "$@"; do
	case "$arg" in
	--force)
		force=1
		;;
	pull | push)
		[ -z "$cmd" ] || usage
		cmd=$arg
		;;
	-*)
		usage
		;;
	*)
		[ -z "$memdir" ] || usage
		memdir=$arg
		;;
	esac
done
[ -n "$cmd" ] && [ -n "$memdir" ] || usage

[ -d "$memdir" ] || die "the memory directory is not an existing directory"

# The store location, from a file outside the repository.
if [ -n "${AGENT_MEMORY_REPO_FILE:-}" ]; then
	locfile=$AGENT_MEMORY_REPO_FILE
else
	[ -n "${HOME:-}" ] || die "HOME is not set"
	locfile=$HOME/.config/agents/memory_repo
fi
[ -f "$locfile" ] && [ -r "$locfile" ] ||
	die "the store location file is missing (expected one line, owner/name, in ~/.config/agents/memory_repo); nothing was done"
slug=
IFS= read -r slug < "$locfile" || true
# Trim surrounding blanks and a carriage return.
slug=$(printf '%s' "$slug" | tr -d '\r' | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')
[ -n "$slug" ] || die "the store location file is empty (expected one line, owner/name); nothing was done"
printf '%s\n' "$slug" | grep -Eq '^[A-Za-z0-9._-]+/[A-Za-z0-9._-]+$' ||
	die "the store location file does not hold a single owner/name"
case "$slug" in
./* | ../* | */. | */..)
	die "the store location file does not hold a single owner/name"
	;;
esac

remote=${AGENT_MEMORY_REMOTE:-https://github.com/$slug.git}

if [ -n "${XDG_CACHE_HOME:-}" ]; then
	cache=$XDG_CACHE_HOME
else
	[ -n "${HOME:-}" ] || die "HOME is not set"
	cache=$HOME/.cache
fi
root=$cache/agents/memory-store
umask 077
mkdir -p "${root:?}" || die "cannot create the store clone directory"
chmod 700 "${root:?}"
clone=$root/$(printf '%s' "$slug" | tr '/' '_')

# Never prompt, and never let a global git configuration change what runs.
GIT_TERMINAL_PROMPT=0
export GIT_TERMINAL_PROMPT

g()
{
	git -C "${clone:?}" -c core.hooksPath=/dev/null -c commit.gpgsign=false \
		-c user.name=agent-memory -c user.email=agent-memory@localhost "$@"
}

# Bring the clone up to date with the store: clone it when absent, then a
# fast-forward-only update. A store that is still empty has nothing to merge.
update_clone()
{
	if [ ! -d "${clone:?}/.git" ]; then
		git clone --quiet -- "$remote" "${clone:?}" >/dev/null 2>&1 ||
			die "cannot clone the private store"
	fi
	chmod 700 "${clone:?}"
	g remote set-url origin "$remote" >/dev/null 2>&1 ||
		die "cannot set the private store remote"
	g fetch --quiet origin >/dev/null 2>&1 ||
		die "cannot fetch the private store"
	branch=$(g symbolic-ref --short HEAD 2>/dev/null) ||
		die "the store clone has no current branch"
	if g rev-parse -q --verify "refs/remotes/origin/$branch" >/dev/null 2>&1; then
		g merge --quiet --ff-only "origin/$branch" >/dev/null 2>&1 ||
			die "the store clone cannot fast-forward; stopping (resolve it by hand)"
	fi
}

# Copy the store's memory files into the memory directory.
copy_in()
{
	src=${clone:?}/$SUBDIR
	[ -d "$src" ] || return 0
	copied=0
	kept=0
	for f in "$src"/*.md; do
		[ -f "$f" ] && [ ! -L "$f" ] || continue
		name=${f##*/}
		dest=${memdir:?}/$name
		if [ -e "$dest" ] || [ -L "$dest" ]; then
			[ -f "$dest" ] && [ ! -L "$dest" ] ||
				die "a memory file name is taken by something that is not a regular file"
			cmp -s "$f" "$dest" && continue
			if [ "$force" -eq 0 ]; then
				ct=$(g log -1 --format=%ct -- "$SUBDIR/$name" 2>/dev/null || true)
				lt=$(mtime "$dest")
				if [ -n "$ct" ] && [ "$lt" -gt "$ct" ]; then
					kept=$((kept + 1))
					continue
				fi
			fi
		fi
		cp -- "$f" "$dest" || die "cannot copy a memory file into the memory directory"
		copied=$((copied + 1))
	done
	printf '%s: pulled %d file(s) from the private store, kept %d newer local file(s)\n' "$me" "$copied" "$kept"
}

update_clone
copy_in

if [ "$cmd" = push ]; then
	mkdir -p "${clone:?}/$SUBDIR"
	n=0
	for f in "${memdir:?}"/*.md; do
		[ -f "$f" ] && [ ! -L "$f" ] || continue
		name=${f##*/}
		cp -- "$f" "${clone:?}/$SUBDIR/$name" || die "cannot copy a memory file into the store clone"
		n=$((n + 1))
	done
	g add -A -- "$SUBDIR" >/dev/null 2>&1 || die "cannot stage the memory files"
	if g diff --cached --quiet; then
		printf '%s: the private store is already up to date (%d file(s) checked)\n' "$me" "$n"
		exit 0
	fi
	g commit --quiet -m "$COMMIT_MSG" >/dev/null 2>&1 || die "cannot commit to the store clone"
	g push --quiet origin "HEAD:refs/heads/$branch" >/dev/null 2>&1 ||
		die "cannot push to the private store (not forced; pull again and retry)"
	printf '%s: pushed to the private store\n' "$me"
fi
