#!/bin/sh
# Sync agent memory files between the agent's memory directory and the
# owner's private memory store.
#
# Agent memory is never committed to this repository. It lives in a private
# store, a git repository whose "owner/name" is read from a file outside the
# tree (one line): ${AGENT_MEMORY_REPO_FILE:-$HOME/.config/agents/memory_repo}.
# This script and the docs never contain that name. The script never prints
# it, a path that holds it, or the content of a memory file; it says "the
# private store".
#
# Usage:
#   scripts/agent-memory.sh pull [--] <memory-dir>
#   scripts/agent-memory.sh push [--] <memory-dir>
#
# Both keep a clone of the store under
# ${XDG_CACHE_HOME:-$HOME/.cache}/agents/memory-store/<hash> (mode 700), one
# per memory directory: <hash> is the first 16 hex digits of the SHA-256 of
# owner/name and the memory directory's absolute path, never the name. The
# clone's own history is the merge base: its HEAD is the last state this
# memory directory synced. Every run
#   1. copies <memory-dir>/*.md (regular files only) into quake2/memory/ of
#      the clone, stages only those files and commits them on top of the last
#      synced state;
#   2. fetches the store and merges it in with git's three-way merge;
#   3. copies the merged quake2/memory/*.md back into <memory-dir>.
# pull stops there (local commits wait for the next push). push then pushes,
# never forced, and reports success only when the store's branch contains
# every local commit. A push that failed is retried by the next run; a store
# that moved in the meantime is merged first.
#
# Nothing is decided by file time. A file changed on both sides since the
# last sync is a conflict: the run exits non-zero and names the files. Your
# version stays in <memory-dir>/<name>, the store's is written beside it as
# <memory-dir>/<name>.store. Merge the two into <name>, delete <name>.store
# and run push again; every run refuses while a .store file is there. A file
# that changes in <memory-dir> while a run is merging is treated the same
# way (the merged version goes to <name>.store).
#
# Deletion: nothing is ever removed from the store. A memory file deleted
# from <memory-dir> comes back on the next pull or push.
#
# Git runs with the inherited repository environment (GIT_DIR, GIT_WORK_TREE,
# GIT_INDEX_FILE, ...) and every GIT_CONFIG* override unset, only the https
# and file transports allowed, no hooks, no fsmonitor and no attributes, and
# with core.symlinks=false. AGENT_MEMORY_REMOTE (the selftest points it at a
# local bare repository: tests/check-agent-memory.sh) must be an
# https://github.com/ URL or an absolute path. A store whose tree holds a
# symbolic link, a submodule, a .gitattributes or .gitmodules file, or a
# quake2/memory that is not a directory is refused, and so is a cache
# directory that is a symbolic link or not yours. Every failure exits
# non-zero with a fixed message.
#
# POSIX sh; also runs under bash 3.2 (stock macOS) and dash.
set -eu

# A caller such as a git hook, `git rebase --exec` or `git submodule foreach`
# can export these; any of them would send the git calls below to another
# repository or inject configuration.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_OBJECT_DIRECTORY \
	GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE GIT_PREFIX GIT_EXEC_PATH \
	GIT_TEMPLATE_DIR GIT_REPLACE_REF_BASE GIT_SHALLOW_FILE GIT_GRAFT_FILE \
	GIT_CONFIG GIT_CONFIG_PARAMETERS GIT_CONFIG_COUNT GIT_CONFIG_GLOBAL \
	GIT_CONFIG_SYSTEM
GIT_ALLOW_PROTOCOL=https:file
GIT_TERMINAL_PROMPT=0
GIT_ATTR_NOSYSTEM=1
LC_ALL=C
export GIT_ALLOW_PROTOCOL GIT_TERMINAL_PROMPT GIT_ATTR_NOSYSTEM LC_ALL
umask 077

SUBDIR=quake2/memory
COMMIT_MSG="memory: sync"
MERGE_MSG="memory: merge"
me=agent-memory
nl='
'
tab=$(printf '\t')

clone=
tmpd=
undo=

die()
{
	printf '%s: %s\n' "$me" "$*" >&2
	exit 1
}

say()
{
	printf '%s: %s\n' "$me" "$*"
}

usage()
{
	printf 'usage: scripts/agent-memory.sh pull|push [--] <memory-dir>\n' >&2
	exit 2
}

# git with nothing from the clone or the user's configuration that runs a
# program or rewrites content: no hooks, no fsmonitor, no attributes (so no
# filter, diff or merge driver), no symlinks checked out.
gitc()
{
	git --literal-pathspecs \
		-c core.hooksPath=/dev/null -c core.fsmonitor=false \
		-c core.attributesFile=/dev/null -c core.symlinks=false \
		-c core.autocrlf=false -c core.quotePath=false \
		-c commit.gpgSign=false -c submodule.recurse=false \
		-c fetch.fsckObjects=true -c merge.autoStash=false -c merge.ff=true \
		-c merge.verifySignatures=false -c rerere.enabled=false \
		-c gc.auto=0 -c maintenance.auto=false -c init.defaultBranch=main \
		-c user.name=agent-memory -c user.email=agent-memory@localhost "$@"
}

g()
{
	gitc -C "${clone:?}" "$@"
}

# On any exit while a merge is being applied, put the clone back where this
# run found it (HEAD = the commit of the local files) and remove the .store
# files this run wrote, so the next run merges again from the same base.
cleanup()
{
	if [ -n "$undo" ]; then
		u=$undo
		undo=
		if [ -e "$clone/.git/MERGE_HEAD" ]; then
			g merge --abort >/dev/null 2>&1 || :
		fi
		if [ "$u" = unborn ]; then
			g update-ref -d "refs/heads/$branch" >/dev/null 2>&1 || :
			g read-tree --empty >/dev/null 2>&1 || :
			g clean -fdxq >/dev/null 2>&1 || :
		else
			g reset -q --hard "$u" >/dev/null 2>&1 || :
		fi
		if [ -f "$tmpd/written" ]; then
			while IFS= read -r name; do
				rm -f -- "$memdir/$name.store" 2>/dev/null || :
			done < "$tmpd/written"
		fi
	fi
	[ -z "$tmpd" ] || rm -rf -- "$tmpd" 2>/dev/null || :
}

# --- arguments -------------------------------------------------------------

[ $# -ge 1 ] || usage
cmd=$1
shift
case "$cmd" in
pull | push) ;;
*) usage ;;
esac
dashdash=0
if [ $# -ge 1 ] && [ "$1" = -- ]; then
	dashdash=1
	shift
fi
[ $# -eq 1 ] || usage
memdir=$1
case "$memdir" in
'') usage ;;
-*) [ "$dashdash" -eq 1 ] || usage ;;
esac
[ -d "$memdir" ] || die "the memory directory is not an existing directory"
# Absolute, because git runs with -C <clone>.
memdir=$(cd -- "$memdir" 2>/dev/null && pwd -P) || die "cannot enter the memory directory"

pending=
for f in "$memdir"/*.md.store; do
	[ -e "$f" ] || [ -L "$f" ] || continue
	n=${f##*/}
	pending="$pending ${n%.store}"
done
[ -z "$pending" ] ||
	die "an earlier conflict is unresolved:$pending. Merge each <name>.store into <name>, delete <name>.store, then run push again"

# --- the store's location --------------------------------------------------

if [ -n "${AGENT_MEMORY_REPO_FILE:-}" ]; then
	locfile=$AGENT_MEMORY_REPO_FILE
else
	[ -n "${HOME:-}" ] || die "HOME is not set"
	locfile=$HOME/.config/agents/memory_repo
fi
[ -f "$locfile" ] && [ -r "$locfile" ] ||
	die "the store location file is missing (expected one line, owner/name, in ~/.config/agents/memory_repo); nothing was done"
slug=
IFS= read -r slug < "$locfile" || :
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
case "$remote" in
*[[:cntrl:]]*)
	die "AGENT_MEMORY_REMOTE must be an https://github.com/ URL or an absolute path"
	;;
https://github.com/?* | /?*) ;;
*)
	die "AGENT_MEMORY_REMOTE must be an https://github.com/ URL or an absolute path"
	;;
esac

# --- the clone, in a private cache directory -------------------------------

if [ -n "${XDG_CACHE_HOME:-}" ]; then
	cache=$XDG_CACHE_HOME
else
	[ -n "${HOME:-}" ] || die "HOME is not set"
	cache=$HOME/.cache
fi
case "$cache" in
/*) ;;
*) die "the cache directory (XDG_CACHE_HOME, or HOME/.cache) is not an absolute path" ;;
esac

# private_dir <path>: create it when absent; refuse a symbolic link, a
# non-directory, or a directory owned by someone else.
private_dir()
{
	if [ -L "$1" ]; then
		die "a store cache directory is a symbolic link; stopping"
	fi
	if [ ! -e "$1" ]; then
		mkdir -p -- "$1" 2>/dev/null || die "cannot create the store cache directory"
	fi
	[ -d "$1" ] && [ ! -L "$1" ] && [ -O "$1" ] ||
		die "a store cache directory is not a directory you own; stopping"
}

private_dir "$cache/agents"
root=$cache/agents/memory-store
private_dir "$root"
chmod 700 "$root" 2>/dev/null || die "cannot make the store cache directory private"

# One clone per store and memory directory: the clone's HEAD is the merge
# base, so two memory directories must never share one.
h=$(printf '%s\n%s' "$slug" "$memdir" | shasum -a 256 2>/dev/null) ||
	h=$(printf '%s\n%s' "$slug" "$memdir" | sha256sum 2>/dev/null) ||
	die "cannot hash the store name (shasum or sha256sum is needed)"
h=$(printf '%s' "$h" | cut -c1-16)
case "$h" in
'' | *[!0-9a-f]*) die "cannot hash the store name" ;;
esac
[ "${#h}" -eq 16 ] || die "cannot hash the store name"
clone=$root/$h

tmpd=$(mktemp -d "${TMPDIR:-/tmp}/agent-memory.XXXXXX" 2>/dev/null) ||
	die "cannot create a temporary directory"
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
: > "$tmpd/conflicts"

if [ -L "$clone" ]; then
	die "the store clone is a symbolic link; stopping"
fi
if [ ! -e "$clone" ]; then
	gitc clone --quiet --no-checkout --template= -c core.symlinks=false \
		-- "$remote" "$clone" >/dev/null 2>&1 || die "cannot clone the private store"
	# This machine has never synced with the store: start from an unborn
	# branch, so the first merge has an empty base and a local file that the
	# store also holds, with other content, is a conflict, not an overwrite.
	b=$(g symbolic-ref --short HEAD 2>/dev/null) || die "the store clone has no current branch"
	if g rev-parse -q --verify "refs/heads/$b" >/dev/null 2>&1; then
		g update-ref -d "refs/heads/$b" >/dev/null 2>&1 || die "cannot prepare the store clone"
	fi
fi
[ -d "$clone" ] && [ ! -L "$clone" ] && [ -O "$clone" ] ||
	die "the store clone is not a directory you own; stopping"
[ -d "$clone/.git" ] && [ ! -L "$clone/.git" ] && [ -O "$clone/.git" ] ||
	die "the store clone is damaged; delete the agents/memory-store directory in your cache and run again"
chmod 700 "$clone" 2>/dev/null || die "cannot make the store clone private"
if [ -e "$clone/.git/info/attributes" ] || [ -L "$clone/.git/info/attributes" ]; then
	die "the store clone has an attributes file; stopping"
fi
g remote set-url -- origin "$remote" >/dev/null 2>&1 || die "cannot set the private store remote"
branch=$(g symbolic-ref --short HEAD 2>/dev/null) || die "the store clone has no current branch"
case "$branch" in
'' | -* | *[!A-Za-z0-9._/-]*) die "the private store's branch has an unusual name; stopping" ;;
esac
origin=refs/remotes/origin/$branch

# Finish what an interrupted run left: no merge in progress, a clean tree.
if [ -e "$clone/.git/MERGE_HEAD" ]; then
	g merge --abort >/dev/null 2>&1 || die "cannot undo an interrupted merge in the store clone"
fi
st=$(g status --porcelain --untracked-files=all 2>/dev/null) || die "cannot read the store clone"
if [ -n "$st" ]; then
	if g rev-parse -q --verify HEAD >/dev/null 2>&1; then
		g reset -q --hard HEAD >/dev/null 2>&1 || die "cannot clean the store clone"
	else
		g read-tree --empty >/dev/null 2>&1 || die "cannot clean the store clone"
	fi
	g clean -fdxq >/dev/null 2>&1 || die "cannot clean the store clone"
fi

# clone_dirs: quake2 and quake2/memory in the clone are real directories.
clone_dirs()
{
	for d in "$clone/quake2" "$clone/$SUBDIR"; do
		if [ -L "$d" ] || { [ -e "$d" ] && [ ! -d "$d" ]; }; then
			die "quake2/memory in the store clone is not a real directory; stopping"
		fi
		if [ ! -d "$d" ]; then
			mkdir -- "$d" 2>/dev/null || die "cannot create quake2/memory in the store clone"
		fi
	done
}

# check_tree <rev>: refuse a store tree that could redirect a write or run a
# program: a symbolic link or submodule anywhere, a .gitattributes or
# .gitmodules file at any depth (in any letter case), a quake2 or
# quake2/memory that is not a directory, a file name with a line break.
check_tree()
{
	g ls-tree -r -z --full-tree "$1" > "$tmpd/tree" 2>/dev/null ||
		die "cannot read the private store's tree"
	c=$(tr -dc '\n' < "$tmpd/tree" | wc -c | tr -d ' ')
	[ "$c" = 0 ] || die "the private store holds a file name with a line break; stopping"
	tr '\0' '\n' < "$tmpd/tree" > "$tmpd/tree.txt"
	if grep -Ev "^100(644|755) blob [0-9a-f]+$tab" "$tmpd/tree.txt" | grep -q .; then
		die "the private store holds a symbolic link or a submodule; stopping"
	fi
	if grep -Eiq "($tab|/)\\.git(attributes|modules)\$" "$tmpd/tree.txt"; then
		die "the private store holds a .gitattributes or .gitmodules file; stopping"
	fi
	for p in quake2 "$SUBDIR"; do
		t=$(g cat-file -t "$1:$p" 2>/dev/null) || continue
		[ "$t" = tree ] || die "quake2/memory in the private store is not a directory; stopping"
	done
}

# --- 1. commit the local files on top of the last synced state -------------

clone_dirs
n=0
for f in "$memdir"/*.md; do
	[ -f "$f" ] && [ ! -L "$f" ] || continue
	name=${f##*/}
	case "$name" in
	*"$nl"*) die "a memory file name holds a line break; rename it" ;;
	esac
	dest=$clone/$SUBDIR/$name
	if [ -e "$dest" ] || [ -L "$dest" ]; then
		[ -f "$dest" ] && [ ! -L "$dest" ] ||
			die "a file in the store clone is not a regular file; stopping"
	fi
	cp -- "$f" "$dest" 2>/dev/null || die "cannot copy a memory file into the store clone"
	g add -f -- "$SUBDIR/$name" >/dev/null 2>&1 || die "cannot stage a memory file"
	n=$((n + 1))
done
changed=0
if g rev-parse -q --verify HEAD >/dev/null 2>&1; then
	rc=0
	g diff-index --cached --quiet HEAD -- >/dev/null 2>&1 || rc=$?
	case "$rc" in
	0) ;;
	1) changed=1 ;;
	*) die "cannot compare the memory files with the store clone" ;;
	esac
elif [ "$n" -gt 0 ]; then
	changed=1
fi
if [ "$changed" -eq 1 ]; then
	g commit --quiet --no-verify -m "$COMMIT_MSG" >/dev/null 2>&1 || die "cannot commit to the store clone"
fi
L=$(g rev-parse -q --verify HEAD 2>/dev/null) || L=

# --- 2. fetch the store and merge it ---------------------------------------

g fetch --quiet --no-tags --no-recurse-submodules -- origin >/dev/null 2>&1 ||
	die "cannot fetch the private store"

if g rev-parse -q --verify "$origin^{commit}" >/dev/null 2>&1; then
	check_tree "$origin"
	unrelated=
	if [ -n "$L" ] && ! g merge-base "$L" "$origin" >/dev/null 2>&1; then
		synced=$(g config --get agent-memory.synced 2>/dev/null) || synced=
		[ "$synced" != true ] ||
			die "the private store's history no longer contains this clone's (rewritten?); stopping, nothing changed"
		unrelated=--allow-unrelated-histories
	fi
	undo=${L:-unborn}
	rc=0
	g merge --quiet --no-edit --ff $unrelated -m "$MERGE_MSG" "$origin" >/dev/null 2>&1 || rc=$?
	if [ "$rc" -ne 0 ]; then
		[ -e "$clone/.git/MERGE_HEAD" ] || die "cannot merge the private store into the store clone"
		# Both sides changed these files: keep the local version in the
		# merge, and hand the store's version to <name>.store below.
		for f in "$clone/$SUBDIR"/*.md; do
			[ -e "$f" ] || [ -L "$f" ] || continue
			name=${f##*/}
			u=$(g ls-files -u -- "$SUBDIR/$name" 2>/dev/null) || die "cannot read the merge state"
			[ -n "$u" ] || continue
			g checkout -q --ours -- "$SUBDIR/$name" >/dev/null 2>&1 || die "cannot resolve a conflicting memory file"
			g add -f -- "$SUBDIR/$name" >/dev/null 2>&1 || die "cannot resolve a conflicting memory file"
			printf 's %s\n' "$name" >> "$tmpd/conflicts"
		done
		u=$(g ls-files -u 2>/dev/null) || die "cannot read the merge state"
		[ -z "$u" ] || die "cannot merge the private store: a conflict outside the memory files; stopping"
		[ -s "$tmpd/conflicts" ] || die "cannot merge the private store into the store clone"
		g commit --quiet --no-verify -m "$MERGE_MSG" >/dev/null 2>&1 || die "cannot commit the merge in the store clone"
	fi
fi

# --- 3. copy the merged files back -----------------------------------------

clone_dirs
pulled=0
for f in "$clone/$SUBDIR"/*.md; do
	[ -e "$f" ] || [ -L "$f" ] || continue
	[ -f "$f" ] && [ ! -L "$f" ] || die "the store clone holds a memory file that is not a regular file; stopping"
	name=${f##*/}
	dest=$memdir/$name
	# A conflicting file keeps the local version, which is already there.
	if grep -Fxq -- "s $name" "$tmpd/conflicts" && { [ -e "$dest" ] || [ -L "$dest" ]; }; then
		continue
	fi
	if [ -e "$dest" ] || [ -L "$dest" ]; then
		[ -f "$dest" ] && [ ! -L "$dest" ] ||
			die "a memory file name is taken by something that is not a regular file"
		cur=$(g hash-object --no-filters -- "$dest" 2>/dev/null) || die "cannot read a memory file"
		new=$(g rev-parse -q --verify "HEAD:$SUBDIR/$name" 2>/dev/null) || die "cannot read the store clone"
		[ "$cur" != "$new" ] || continue
		old=
		if [ -n "$L" ]; then
			old=$(g rev-parse -q --verify "$L:$SUBDIR/$name" 2>/dev/null) || old=
		fi
		if [ "$cur" != "$old" ]; then
			# Changed in the memory directory while this run merged.
			printf 'm %s\n' "$name" >> "$tmpd/conflicts"
			continue
		fi
	fi
	cp -- "$f" "$dest" 2>/dev/null || die "cannot copy a memory file into the memory directory"
	pulled=$((pulled + 1))
done

# The other side of each conflict, beside the local file.
while IFS= read -r line; do
	name=${line#? }
	case "$line" in
	s*) rev=HEAD^2 ;;
	*) rev=HEAD ;;
	esac
	dest=$memdir/$name.store
	if [ -e "$dest" ] || [ -L "$dest" ]; then
		die "a .store file appeared in the memory directory during the run; stopping"
	fi
	g cat-file blob "$rev:$SUBDIR/$name" > "$tmpd/blob" 2>/dev/null ||
		die "cannot read the store's version of a conflicting memory file"
	printf '%s\n' "$name" >> "$tmpd/written"
	cp -- "$tmpd/blob" "$dest" 2>/dev/null ||
		die "cannot write the store's version of a conflicting memory file"
done < "$tmpd/conflicts"

undo=
if g rev-parse -q --verify "$origin^{commit}" >/dev/null 2>&1; then
	g config agent-memory.synced true >/dev/null 2>&1 || die "cannot record the sync in the store clone"
fi

if [ -s "$tmpd/conflicts" ]; then
	names=$(sed 's/^. //' "$tmpd/conflicts" | tr '\n' ' ')
	printf '%s: conflict, changed on both sides: %s\n' "$me" "$names" >&2
	printf '%s: your version is unchanged in <name>, the store'"'"'s is in <name>.store; merge them into <name>, delete <name>.store, then run push again (nothing was pushed)\n' "$me" >&2
	exit 1
fi

if g rev-parse -q --verify HEAD >/dev/null 2>&1; then
	if g rev-parse -q --verify "$origin^{commit}" >/dev/null 2>&1; then
		ahead=$(g rev-list --count "$origin..HEAD" 2>/dev/null) ||
			die "cannot compare the store clone with the private store"
	else
		ahead=1
	fi
else
	ahead=0
fi

if [ "$cmd" = pull ]; then
	say "pulled $pulled file(s) from the private store"
	[ "$ahead" = 0 ] || say "local changes are not in the private store yet; run push"
	exit 0
fi

# --- 4. push ---------------------------------------------------------------

if [ "$ahead" = 0 ]; then
	say "the private store is already up to date ($n file(s) checked, $pulled pulled)"
	exit 0
fi
g push --quiet --no-verify -- origin "HEAD:refs/heads/$branch" >/dev/null 2>&1 ||
	die "cannot push to the private store (nothing was forced); the change stays in the store clone and the next pull or push merges and retries it"
g fetch --quiet --no-tags --no-recurse-submodules -- origin >/dev/null 2>&1 ||
	die "pushed, but cannot fetch the private store to confirm it; run push again"
g merge-base --is-ancestor HEAD "$origin" >/dev/null 2>&1 ||
	die "the private store does not hold this push; run push again"
g config agent-memory.synced true >/dev/null 2>&1 || die "cannot record the sync in the store clone"
say "pushed to the private store ($n file(s) checked, $pulled pulled)"
