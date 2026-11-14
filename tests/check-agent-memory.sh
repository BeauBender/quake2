#!/bin/sh
# Invariant: scripts/agent-memory.sh syncs memory by content and never loses
# an edit silently, never reports a push that did not land, never writes
# through a symbolic link, ignores an inherited git environment, restricts
# its remote and transports, and never prints the store's name, a path or the
# content of a memory file.
#
# No network: every store is a local bare repository (AGENT_MEMORY_REMOTE),
# and the location file, HOME, the git configuration and every cache are
# throwaway paths under a private mktemp -d. Failures are injected with shims
# first on PATH (git, cp, a remote helper) and a refusing pre-receive hook.
# The script runs under /bin/sh, or under AGENT_MEMORY_TEST_SHELL when set
# (for example /bin/dash).
#
# Usage: tests/check-agent-memory.sh
set -eu
cd "$(dirname "$0")/.."
me=check-agent-memory
script=$PWD/scripts/agent-memory.sh
sh=${AGENT_MEMORY_TEST_SHELL:-/bin/sh}
realgit=$(command -v git)
realcp=$(command -v cp)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/agmem.XXXXXX")
chmod 700 "${tmp:?}"
trap 'rm -rf "${tmp:?}"' EXIT HUP INT TERM

unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_OBJECT_DIRECTORY \
	GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE GIT_CONFIG GIT_CONFIG_PARAMETERS \
	GIT_CONFIG_COUNT AGENT_MEMORY_REMOTE
# For this test's own git calls; the script unsets GIT_CONFIG_GLOBAL and reads
# $HOME/.gitconfig (absent unless a case writes one).
GIT_CONFIG_GLOBAL=/dev/null
GIT_CONFIG_NOSYSTEM=1
HOME=$tmp/home
XDG_CONFIG_HOME=$tmp/xdg
XDG_CACHE_HOME=$tmp/cache
AGENT_MEMORY_REPO_FILE=$tmp/loc
export GIT_CONFIG_GLOBAL GIT_CONFIG_NOSYSTEM HOME XDG_CONFIG_HOME XDG_CACHE_HOME AGENT_MEMORY_REPO_FILE
mkdir -p "${tmp:?}/home" "${tmp:?}/xdg" "${tmp:?}/A" "${tmp:?}/B"

fail()
{
	echo "$me: FAIL: $*" >&2
	exit 1
}

# runv [VAR=value ...] <command ...>: output in $tmp/out (and $tmp/all),
# status in $rc.
runv()
{
	rc=0
	env "$@" > "${tmp:?}/out" 2>&1 || rc=$?
	cat "${tmp:?}/out" >> "${tmp:?}/all"
}

run()
{
	runv "$sh" "$script" "$@"
}

expect_ok()
{
	what=$1
	shift
	run "$@"
	[ "$rc" -eq 0 ] || fail "$what: exit status $rc, expected 0"
}

expect_fail()
{
	what=$1
	shift
	run "$@"
	[ "$rc" -ne 0 ] || fail "$what: exit status 0, expected failure"
}

said()
{
	grep -q -- "$1" "${tmp:?}/out"
}

mkstore()
{
	git init -q --bare "${tmp:?}/$1"
	git -C "${tmp:?}/$1" symbolic-ref HEAD refs/heads/main
}

# fxcommit <work-dir>: commit everything in a fixture work tree.
fxcommit()
{
	git -C "$1" add -A
	git -C "$1" -c user.name=t -c user.email=t@localhost commit -q -m fixture
}

# fxstore <store> <work-dir>: a work tree whose commits go to <store>.
fxstore()
{
	mkstore "$1"
	git init -q "${tmp:?}/$2"
	git -C "${tmp:?}/$2" checkout -q -b main
	mkdir -p "${tmp:?}/$2/quake2/memory"
}

has()
{
	git -C "${tmp:?}/$1" cat-file -e "main:$2" 2>/dev/null
}

show()
{
	git -C "${tmp:?}/$1" cat-file blob "main:$2"
}

slug=selftest/store

# clone_name <memory-dir>: the name the script gives that directory's clone.
clone_name()
{
	d=$(cd "$1" && pwd -P)
	printf '%s\n%s' "$slug" "$d" | shasum -a 256 2>/dev/null | cut -c1-16 ||
		printf '%s\n%s' "$slug" "$d" | sha256sum | cut -c1-16
}
nameA=$(clone_name "$tmp/A")

# --- refusals before anything is touched -----------------------------------

expect_fail "missing location file" pull "$tmp/A"
said 'location file is missing' || fail "missing location file: no clear message"

: > "${tmp:?}/loc"
expect_fail "empty location file" pull "$tmp/A"
said 'empty' || fail "empty location file: no clear message"

mkstore store.git
AGENT_MEMORY_REMOTE=$tmp/store.git
export AGENT_MEMORY_REMOTE
for bad in 'noslash' 'a b/c' 'a/b/c' '../..' './x' 'a/..' 'a/b;c' '$(x)/y'; do
	printf '%s\n' "$bad" > "${tmp:?}/loc"
	expect_fail "malformed slug" pull "$tmp/A"
	said 'single owner/name' || fail "malformed slug: refused for another reason"
done
printf '%s\n' "$slug" > "${tmp:?}/loc"

expect_fail "missing memory directory" pull "$tmp/nodir"
expect_fail "unknown command" frobnicate "$tmp/A"
expect_fail "extra argument" pull "$tmp/A" "$tmp/B"
expect_fail "dash memory directory without --" pull -x

for bad in 'relative/store.git' 'ext::sh -c true' 'http://github.com/a/b.git' \
	'https://example.com/a/b.git' 'file:///a/b.git' 'ssh://github.com/a/b.git' "$tmp/x
y"; do
	runv AGENT_MEMORY_REMOTE="$bad" "$sh" "$script" pull "$tmp/A"
	[ "$rc" -ne 0 ] || fail "remote restriction: accepted a disallowed remote"
	said 'AGENT_MEMORY_REMOTE must be' || fail "remote restriction: refused for another reason"
done

runv XDG_CACHE_HOME=relative/cache "$sh" "$script" pull "$tmp/A"
[ "$rc" -ne 0 ] && said 'not an absolute path' || fail "a relative XDG_CACHE_HOME was accepted"

mkdir -p "${tmp:?}/elsewhere" "${tmp:?}/cacheL" "${tmp:?}/cacheM/agents" "${tmp:?}/cacheS/agents/memory-store"
ln -s "${tmp:?}/elsewhere" "${tmp:?}/cacheL/agents"
ln -s "${tmp:?}/elsewhere" "${tmp:?}/cacheM/agents/memory-store"
ln -s "${tmp:?}/elsewhere" "${tmp:?}/cacheS/agents/memory-store/$nameA"
for c in cacheL cacheM cacheS; do
	runv XDG_CACHE_HOME="$tmp/$c" "$sh" "$script" pull "$tmp/A"
	[ "$rc" -ne 0 ] && said 'symbolic link' || fail "$c: a symlinked cache path was accepted"
done
[ -z "$(ls -A "${tmp:?}/elsewhere")" ] || fail "a symlinked cache path was written through"

runv AGENT_MEMORY_REMOTE="$tmp/does-not-exist.git" XDG_CACHE_HOME="$tmp/cacheU" "$sh" "$script" push "$tmp/A"
[ "$rc" -ne 0 ] || fail "unreachable store: exit status 0"

# --- round trip --------------------------------------------------------------

printf 'alpha-content-marker\n' > "${tmp:?}/A/a.md"
printf 'not a memory file\n' > "${tmp:?}/A/skip.txt"
expect_ok "push to an empty store" push "$tmp/A"
[ "$(git -C "${tmp:?}/store.git" ls-tree -r --name-only main)" = "quake2/memory/a.md" ] ||
	fail "the store holds something other than quake2/memory/a.md"
[ "$(ls -A "${tmp:?}/cache/agents/memory-store")" = "$nameA" ] ||
	fail "the clone directory is not named by the hash of the store name and memory directory"
[ "$(ls -ld "${tmp:?}/cache/agents/memory-store" | cut -c1-10)" = "drwx------" ] ||
	fail "clone directory is not mode 700"

expect_ok "pull" pull "$tmp/B"
cmp -s "${tmp:?}/A/a.md" "${tmp:?}/B/a.md" || fail "pulled file differs"
[ ! -e "${tmp:?}/B/skip.txt" ] || fail "a non-markdown file was copied"

printf 'beta-content-marker\n' > "${tmp:?}/B/b.md"
expect_ok "second push" push "$tmp/B"
expect_ok "second pull" pull "$tmp/A"
cmp -s "${tmp:?}/A/b.md" "${tmp:?}/B/b.md" || fail "second file did not round-trip"
[ -f "${tmp:?}/A/skip.txt" ] || fail "a local file was deleted"

# deletion: a file removed locally comes back, nothing leaves the store
rm -f "${tmp:?}/A/b.md"
expect_ok "pull after a local delete" pull "$tmp/A"
[ -f "${tmp:?}/A/b.md" ] || fail "a locally deleted memory did not come back"

before=$(git -C "${tmp:?}/store.git" rev-parse main)
expect_ok "unchanged push" push "$tmp/A"
[ "$(git -C "${tmp:?}/store.git" rev-parse main)" = "$before" ] || fail "an unchanged push made a commit"

# an edit in the same second as the last push is kept (no file times)
printf 'one-marker\n' > "${tmp:?}/A/x.md"
expect_ok "push x" push "$tmp/A"
printf 'two-marker\n' > "${tmp:?}/A/x.md"
expect_ok "pull right after an edit" pull "$tmp/A"
[ "$(cat "${tmp:?}/A/x.md")" = two-marker ] || fail "a pull reverted a fresh local edit"
expect_ok "push right after an edit" push "$tmp/A"
[ "$(show store.git quake2/memory/x.md)" = two-marker ] || fail "a fresh edit did not reach the store"

# a symlinked memory file is never followed
printf 'secret-link-marker\n' > "${tmp:?}/secret.txt"
ln -s "${tmp:?}/secret.txt" "${tmp:?}/A/link.md"
expect_ok "push with a symlinked memory file" push "$tmp/A"
! has store.git quake2/memory/link.md || fail "a symlinked memory file was pushed"
rm -f "${tmp:?}/A/link.md"
expect_ok "pull B" pull "$tmp/B"

# --- two machines edit the same file -----------------------------------------

printf '# index\nv1-line\n' > "${tmp:?}/A/MEMORY.md"
expect_ok "push MEMORY v1" push "$tmp/A"
expect_ok "pull MEMORY v1" pull "$tmp/B"
printf '# index\nv1-line\nA-line-marker\n' > "${tmp:?}/A/MEMORY.md"
printf '# index\nv1-line\nB-line-marker\n' > "${tmp:?}/B/MEMORY.md"
expect_ok "B pushes its edit" push "$tmp/B"
expect_fail "A pushes a conflicting edit" push "$tmp/A"
said 'MEMORY.md' || fail "conflict: the file is not named"
grep -q A-line-marker "${tmp:?}/A/MEMORY.md" || fail "conflict: the local edit was lost"
grep -q B-line-marker "${tmp:?}/A/MEMORY.md.store" || fail "conflict: the store's version is not beside it"
show store.git quake2/memory/MEMORY.md | grep -q B-line-marker || fail "conflict: the store lost B's edit"
! show store.git quake2/memory/MEMORY.md | grep -q A-line-marker || fail "conflict: A's edit was pushed over B's"
expect_fail "a run with an unresolved conflict" push "$tmp/A"
said 'unresolved' || fail "unresolved conflict: no clear message"
printf '# index\nv1-line\nA-line-marker\nB-line-marker\n' > "${tmp:?}/A/MEMORY.md"
rm -f "${tmp:?}/A/MEMORY.md.store"
expect_ok "push the resolution" push "$tmp/A"
show store.git quake2/memory/MEMORY.md | grep -q A-line-marker || fail "resolution: A's edit missing"
show store.git quake2/memory/MEMORY.md | grep -q B-line-marker || fail "resolution: B's edit missing"
expect_ok "B pulls the resolution" pull "$tmp/B"
cmp -s "${tmp:?}/A/MEMORY.md" "${tmp:?}/B/MEMORY.md" || fail "resolution did not reach B"

# the reverse order: B edits and pushes first, A edits later and pushes
printf 'r1\n' > "${tmp:?}/B/r.md"
expect_ok "push r" push "$tmp/B"
expect_ok "pull r" pull "$tmp/A"
printf 'r-B-marker\n' > "${tmp:?}/B/r.md"
expect_ok "B pushes r" push "$tmp/B"
printf 'r-A-marker\n' > "${tmp:?}/A/r.md"
expect_fail "A pushes a later conflicting edit" push "$tmp/A"
[ "$(cat "${tmp:?}/A/r.md")" = r-A-marker ] || fail "reverse conflict: A's edit was lost"
[ "$(cat "${tmp:?}/A/r.md.store")" = r-B-marker ] || fail "reverse conflict: B's edit is not beside it"
[ "$(show store.git quake2/memory/r.md)" = r-B-marker ] || fail "reverse conflict: B's edit left the store"
rm -f "${tmp:?}/A/r.md.store"
expect_ok "A keeps its version" push "$tmp/A"
expect_ok "B pulls r" pull "$tmp/B"
[ "$(cat "${tmp:?}/B/r.md")" = r-A-marker ] || fail "the chosen version did not reach B"

# edits to different lines of one file merge cleanly
printf 'l1\nl2\nl3\nl4\nl5\n' > "${tmp:?}/A/c.md"
expect_ok "push c" push "$tmp/A"
expect_ok "pull c" pull "$tmp/B"
printf 'l1-A\nl2\nl3\nl4\nl5\n' > "${tmp:?}/A/c.md"
printf 'l1\nl2\nl3\nl4\nl5-B\n' > "${tmp:?}/B/c.md"
expect_ok "B pushes c" push "$tmp/B"
expect_ok "A merges c" push "$tmp/A"
[ "$(show store.git quake2/memory/c.md | tr '\n' ' ')" = "l1-A l2 l3 l4 l5-B " ] || fail "a clean merge lost an edit"
[ "$(tr '\n' ' ' < "${tmp:?}/A/c.md")" = "l1-A l2 l3 l4 l5-B " ] || fail "a clean merge did not reach the memory directory"
expect_ok "B pulls c" pull "$tmp/B"

# --- pushes that do not land ------------------------------------------------

# the store refuses a write; the next run retries the commit it left
printf '#!/bin/sh\nexit 1\n' > "${tmp:?}/store.git/hooks/pre-receive"
chmod 755 "${tmp:?}/store.git/hooks/pre-receive"
printf 'n-marker\n' > "${tmp:?}/A/n.md"
expect_fail "push to a store that refuses writes" push "$tmp/A"
! said 'up to date' || fail "a refused push said up to date"
! has store.git quake2/memory/n.md || fail "the refusing store took the push"
expect_ok "pull with a pending push" pull "$tmp/A"
said 'not in the private store yet' || fail "pull does not report the pending push"
rm -f "${tmp:?}/store.git/hooks/pre-receive"
expect_ok "push after the store accepts writes again" push "$tmp/A"
has store.git quake2/memory/n.md || fail "the pending commit was not pushed"

# another machine lands a commit between this run's fetch and its push
git clone -q "${tmp:?}/store.git" "${tmp:?}/C"
printf 'other-machine-marker\n' > "${tmp:?}/C/quake2/memory/t.md"
fxcommit "${tmp:?}/C"
mkdir -p "${tmp:?}/racebin"
printf '#!/bin/sh\ncase " $* " in\n*" push "*)\n\tif [ -f "$RACE" ]; then\n\t\trm -f "$RACE"\n\t\t"$REALGIT" -C "$THIRD" push -q origin HEAD:refs/heads/main >/dev/null 2>&1 || exit 99\n\tfi\n\t;;\nesac\nexec "$REALGIT" "$@"\n' > "${tmp:?}/racebin/git"
chmod 755 "${tmp:?}/racebin/git"
: > "${tmp:?}/race"
printf 'b2-marker\n' > "${tmp:?}/A/b2.md"
runv PATH="$tmp/racebin:$PATH" REALGIT="$realgit" THIRD="$tmp/C" RACE="$tmp/race" "$sh" "$script" push "$tmp/A"
[ ! -e "${tmp:?}/race" ] || fail "race: the shim did not run"
has store.git quake2/memory/t.md || fail "race: a push replaced the other machine's commit"
[ "$rc" -ne 0 ] || has store.git quake2/memory/b2.md || fail "race: exit 0 without the local change in the store"
expect_ok "push after a lost race" push "$tmp/A"
has store.git quake2/memory/t.md || fail "race recovery: the other machine's commit is gone"
has store.git quake2/memory/b2.md || fail "race recovery: the local change was not pushed"
[ -f "${tmp:?}/A/t.md" ] || fail "race recovery: the other machine's file was not pulled"

# --- failures inside the run exit non-zero -----------------------------------

mkdir -p "${tmp:?}/cpbin" "${tmp:?}/gitfail"
printf '#!/bin/sh\nfor a do last=$a; done\ncase "$last" in $FAILCP) exit 1 ;; esac\nexec "$REALCP" "$@"\n' > "${tmp:?}/cpbin/cp"
printf '#!/bin/sh\ncase " $* " in *" commit "*) exit 1 ;; esac\nexec "$REALGIT" "$@"\n' > "${tmp:?}/gitfail/git"
chmod 755 "${tmp:?}/cpbin/cp" "${tmp:?}/gitfail/git"

printf 'cp-in-marker\n' > "${tmp:?}/A/a.md"
runv PATH="$tmp/cpbin:$PATH" REALCP="$realcp" FAILCP="$tmp/cache/*" "$sh" "$script" push "$tmp/A"
[ "$rc" -ne 0 ] || fail "a failed copy into the store clone exited 0"
runv PATH="$tmp/gitfail:$PATH" REALGIT="$realgit" "$sh" "$script" push "$tmp/A"
[ "$rc" -ne 0 ] || fail "a failed commit exited 0"
! show store.git quake2/memory/a.md | grep -q cp-in-marker || fail "a failed run pushed"
expect_ok "push after the failures" push "$tmp/A"
show store.git quake2/memory/a.md | grep -q cp-in-marker || fail "the edit did not reach the store"
runv PATH="$tmp/cpbin:$PATH" REALCP="$realcp" FAILCP="$tmp/B/*" "$sh" "$script" pull "$tmp/B"
[ "$rc" -ne 0 ] || fail "a failed copy into the memory directory exited 0"
expect_ok "pull after the failure" pull "$tmp/B"
grep -q cp-in-marker "${tmp:?}/B/a.md" || fail "the edit did not reach B after a failed pull"

# --- an inherited git environment is ignored ---------------------------------

git init -q "${tmp:?}/decoy"
printf 'decoy\n' > "${tmp:?}/decoy/d.txt"
fxcommit "${tmp:?}/decoy"
mkstore decoy-store.git
snap()
{
	find "${tmp:?}/decoy" "${tmp:?}/decoy-store.git" -type f | sort | while IFS= read -r f; do
		cksum < "$f"
	done
}
snap > "${tmp:?}/decoy.before"
printf 'env-marker\n' > "${tmp:?}/A/e.md"
runv GIT_DIR="$tmp/decoy/.git" GIT_WORK_TREE="$tmp/decoy" GIT_INDEX_FILE="$tmp/decoy/.git/index" \
	GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=remote.origin.pushurl GIT_CONFIG_VALUE_0="$tmp/decoy-store.git" \
	"$sh" "$script" push "$tmp/A"
[ "$rc" -eq 0 ] || fail "push with an inherited git environment: exit status $rc"
snap > "${tmp:?}/decoy.after"
cmp -s "${tmp:?}/decoy.before" "${tmp:?}/decoy.after" || fail "an inherited GIT_DIR or GIT_CONFIG_* changed another repository"
has store.git quake2/memory/e.md || fail "push with an inherited git environment did not reach the store"

# --- transports other than https and file are refused ------------------------

mkdir -p "${tmp:?}/home2" "${tmp:?}/helper"
printf '[url "testproto::fixture"]\n\tinsteadOf = %s\n' "$tmp/redir.git" > "${tmp:?}/home2/.gitconfig"
printf '#!/bin/sh\n: > "$PROTO_MARK"\nexit 1\n' > "${tmp:?}/helper/git-remote-testproto"
chmod 755 "${tmp:?}/helper/git-remote-testproto"
runv HOME="$tmp/home2" PATH="$tmp/helper:$PATH" PROTO_MARK="$tmp/proto-ran" \
	AGENT_MEMORY_REMOTE="$tmp/redir.git" XDG_CACHE_HOME="$tmp/cacheP" "$sh" "$script" push "$tmp/A"
[ "$rc" -ne 0 ] || fail "a rewritten remote with another transport was used"
[ ! -e "${tmp:?}/proto-ran" ] || fail "a transport other than https and file ran"

# --- a store tree that could redirect a write or run a program ---------------

fxstore store3.git fx3
mkdir -p "${tmp:?}/victim" "${tmp:?}/D"
printf 'victim-original\n' > "${tmp:?}/victim/target.txt"
ln -s "${tmp:?}/victim/target.txt" "${tmp:?}/fx3/quake2/memory/evil.md"
fxcommit "${tmp:?}/fx3"
git -C "${tmp:?}/fx3" push -q "${tmp:?}/store3.git" main
printf 'd-private-marker\n' > "${tmp:?}/D/evil.md"
runv AGENT_MEMORY_REMOTE="$tmp/store3.git" XDG_CACHE_HOME="$tmp/cache3" "$sh" "$script" push "$tmp/D"
[ "$rc" -ne 0 ] || fail "a store holding a symlinked memory file was accepted"
[ "$(cat "${tmp:?}/victim/target.txt")" = victim-original ] || fail "a push wrote through a store symlink"
[ "$(cat "${tmp:?}/D/evil.md")" = d-private-marker ] || fail "a store symlink changed the memory directory"

fxstore store4.git fx4
mkdir -p "${tmp:?}/outside" "${tmp:?}/E"
printf 'outside-secret-marker\n' > "${tmp:?}/outside/secret.md"
rmdir "${tmp:?}/fx4/quake2/memory"
ln -s "${tmp:?}/outside" "${tmp:?}/fx4/quake2/memory"
fxcommit "${tmp:?}/fx4"
git -C "${tmp:?}/fx4" push -q "${tmp:?}/store4.git" main
printf 'e-marker\n' > "${tmp:?}/E/c.md"
runv AGENT_MEMORY_REMOTE="$tmp/store4.git" XDG_CACHE_HOME="$tmp/cache4" "$sh" "$script" push "$tmp/E"
[ "$rc" -ne 0 ] || fail "a store whose quake2/memory is a symlink was accepted"
[ ! -e "${tmp:?}/outside/c.md" ] || fail "a push wrote into a symlinked store directory"
[ ! -e "${tmp:?}/E/secret.md" ] || fail "a pull read through a symlinked store directory"

fxstore store5.git fx5
rmdir "${tmp:?}/fx5/quake2/memory"
printf 'not a directory\n' > "${tmp:?}/fx5/quake2/memory"
fxcommit "${tmp:?}/fx5"
git -C "${tmp:?}/fx5" push -q "${tmp:?}/store5.git" main
mkdir -p "${tmp:?}/F"
printf 'f-marker\n' > "${tmp:?}/F/f.md"
runv AGENT_MEMORY_REMOTE="$tmp/store5.git" XDG_CACHE_HOME="$tmp/cache5" "$sh" "$script" push "$tmp/F"
[ "$rc" -ne 0 ] || fail "a store whose quake2/memory is a regular file was accepted"

fxstore store6.git fx6
mkdir -p "${tmp:?}/home3" "${tmp:?}/G"
printf '*.md filter=evil\n' > "${tmp:?}/fx6/.gitattributes"
printf 'z\n' > "${tmp:?}/fx6/quake2/memory/z.md"
fxcommit "${tmp:?}/fx6"
git -C "${tmp:?}/fx6" push -q "${tmp:?}/store6.git" main
printf '[filter "evil"]\n\tsmudge = "sh -c '"'"': > %s; cat'"'"'"\n\tclean = "sh -c '"'"': > %s; cat'"'"'"\n' \
	"$tmp/filter-ran" "$tmp/filter-ran" > "${tmp:?}/home3/.gitconfig"
printf 'g-marker\n' > "${tmp:?}/G/z.md"
runv HOME="$tmp/home3" AGENT_MEMORY_REMOTE="$tmp/store6.git" XDG_CACHE_HOME="$tmp/cache6" "$sh" "$script" push "$tmp/G"
[ "$rc" -ne 0 ] || fail "a store with a .gitattributes file was accepted"
[ ! -e "${tmp:?}/filter-ran" ] || fail "a filter driver from the store's attributes ran"

# --- a store rewritten to an unrelated history is not merged or forced -------

git init -q "${tmp:?}/other"
git -C "${tmp:?}/other" checkout -q -b main
printf 'x\n' > "${tmp:?}/other/x.md"
fxcommit "${tmp:?}/other"
git -C "${tmp:?}/other" push -q --force "${tmp:?}/store.git" main
printf 'gamma-marker\n' > "${tmp:?}/A/g.md"
expect_fail "rewritten store" push "$tmp/A"
[ "$(git -C "${tmp:?}/store.git" rev-parse main)" = "$(git -C "${tmp:?}/other" rev-parse main)" ] ||
	fail "a rewritten store was changed"

# --- nothing printed names the store, a path, or a file's content ------------

for marker in "$slug" 'selftest_store' "$tmp" 'alpha-content-marker' 'beta-content-marker' \
	'one-marker' 'two-marker' 'secret-link-marker' 'A-line-marker' 'B-line-marker' 'r-A-marker' \
	'r-B-marker' 'n-marker' 'other-machine-marker' 'b2-marker' 'cp-in-marker' 'env-marker' \
	'victim-original' 'd-private-marker' 'outside-secret-marker' 'e-marker' 'f-marker' \
	'g-marker' 'gamma-marker'; do
	! grep -qF -- "$marker" "${tmp:?}/all" || fail "output leaks a marker"
done

echo "$me: ok"
