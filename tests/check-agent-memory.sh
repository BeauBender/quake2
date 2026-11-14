#!/bin/sh
# Invariant: scripts/agent-memory.sh stops cleanly without a store location,
# rejects a malformed one, and round-trips memory files between two memory
# directories through a store. No network: the store is a local bare
# repository (AGENT_MEMORY_REMOTE), and the location file, HOME and the clone
# cache are throwaway paths under a private mktemp -d.
#
# Usage: tests/check-agent-memory.sh
set -eu
cd "$(dirname "$0")/.."
me=check-agent-memory
script=$PWD/scripts/agent-memory.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/agmem.XXXXXX")
chmod 700 "${tmp:?}"
trap 'rm -rf "${tmp:?}"' EXIT HUP INT TERM

export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE AGENT_MEMORY_REMOTE
HOME=$tmp/home
XDG_CACHE_HOME=$tmp/cache
AGENT_MEMORY_REPO_FILE=$tmp/loc
export HOME XDG_CACHE_HOME AGENT_MEMORY_REPO_FILE
mkdir -p "${tmp:?}/home" "${tmp:?}/A" "${tmp:?}/B"

fail()
{
	echo "$me: FAIL: $*" >&2
	exit 1
}

# run <args>: runs the script, output in $tmp/out, status in $rc.
run()
{
	rc=0
	"$script" "$@" > "${tmp:?}/out" 2>&1 || rc=$?
	cat "${tmp:?}/out" >> "${tmp:?}/all"
}

expect_fail()
{
	what=$1
	shift
	run "$@"
	[ "$rc" -ne 0 ] || fail "$what: exit status 0, expected failure"
}

# 1. no location file
expect_fail "missing location file" pull "$tmp/A"
grep -q 'location file is missing' "${tmp:?}/out" || fail "missing location file: no clear message"

# 2. empty location file
: > "${tmp:?}/loc"
expect_fail "empty location file" pull "$tmp/A"
grep -q 'empty' "${tmp:?}/out" || fail "empty location file: no clear message"

# 3. malformed slugs, with a reachable store so that only the slug check can refuse
git init -q --bare "${tmp:?}/store.git"
git -C "${tmp:?}/store.git" symbolic-ref HEAD refs/heads/main
AGENT_MEMORY_REMOTE=$tmp/store.git
export AGENT_MEMORY_REMOTE
for bad in 'noslash' 'a b/c' 'a/b/c' '../..' './x' 'a/..' 'a/b;c' '$(x)/y'; do
	printf '%s\n' "$bad" > "${tmp:?}/loc"
	expect_fail "malformed slug" pull "$tmp/A"
	grep -q 'single owner/name' "${tmp:?}/out" || fail "malformed slug: refused for another reason"
done

# 4. memory directory that does not exist
printf 'selftest/store\n' > "${tmp:?}/loc"
expect_fail "missing memory directory" pull "$tmp/nodir"
expect_fail "unknown command" frobnicate "$tmp/A"

# 5. unreachable store
AGENT_MEMORY_REMOTE=$tmp/does-not-exist.git
export AGENT_MEMORY_REMOTE
expect_fail "unreachable store" push "$tmp/A"

# 6. round trip through a local bare repository
AGENT_MEMORY_REMOTE=$tmp/store.git
export AGENT_MEMORY_REMOTE

printf 'alpha-content-marker\n' > "${tmp:?}/A/a.md"
printf 'not a memory file\n' > "${tmp:?}/A/skip.txt"
run push "$tmp/A"
[ "$rc" -eq 0 ] || fail "push to an empty store failed"
[ "$(git -C "${tmp:?}/store.git" ls-tree -r --name-only main)" = "quake2/memory/a.md" ] ||
	fail "the store holds something other than quake2/memory/a.md"

run pull "$tmp/B"
[ "$rc" -eq 0 ] || fail "pull failed"
cmp -s "${tmp:?}/A/a.md" "${tmp:?}/B/a.md" || fail "pulled file differs"
[ ! -e "${tmp:?}/B/skip.txt" ] || fail "a non-markdown file was copied"

# the second memory directory adds a file; it flows back; nothing is deleted
printf 'beta-content-marker\n' > "${tmp:?}/B/b.md"
run push "$tmp/B"
[ "$rc" -eq 0 ] || fail "second push failed"
run pull "$tmp/A"
[ "$rc" -eq 0 ] || fail "second pull failed"
cmp -s "${tmp:?}/A/b.md" "${tmp:?}/B/b.md" || fail "second file did not round-trip"
[ -f "${tmp:?}/A/skip.txt" ] || fail "a local file was deleted"

# a newer local file is kept without --force, replaced with it
printf 'local-edit-marker\n' > "${tmp:?}/A/a.md"
touch -t 203001010000 "${tmp:?}/A/a.md"
run pull "$tmp/A"
[ "$rc" -eq 0 ] || fail "pull with a newer local file failed"
[ "$(cat "${tmp:?}/A/a.md")" = "local-edit-marker" ] || fail "a newer local file was overwritten"
run pull --force "$tmp/A"
[ "$rc" -eq 0 ] || fail "forced pull failed"
[ "$(cat "${tmp:?}/A/a.md")" = "alpha-content-marker" ] || fail "--force did not overwrite"

# an unchanged push makes no commit
before=$(git -C "${tmp:?}/store.git" rev-parse main)
run push "$tmp/A"
[ "$rc" -eq 0 ] || fail "unchanged push failed"
[ "$(git -C "${tmp:?}/store.git" rev-parse main)" = "$before" ] || fail "an unchanged push made a commit"

# a store that moved to an unrelated history is not forced over
other=$tmp/other
git init -q "${tmp:?}/other"
git -C "${tmp:?}/other" checkout -q -b main
printf 'x\n' > "${tmp:?}/other/x.md"
git -C "${tmp:?}/other" add x.md
git -C "${tmp:?}/other" -c user.name=t -c user.email=t@localhost commit -q -m x
git -C "${tmp:?}/other" push -q --force "${tmp:?}/store.git" main
printf 'gamma\n' > "${tmp:?}/A/g.md"
expect_fail "diverged store" push "$tmp/A"
[ "$(git -C "${tmp:?}/store.git" rev-parse main)" = "$(git -C "${tmp:?}/other" rev-parse main)" ] ||
	fail "a diverged store was changed"

# nothing printed, in any run above, names the store or a file's content
for marker in 'selftest/store' 'alpha-content-marker' 'beta-content-marker' 'local-edit-marker' 'gamma'; do
	! grep -q "$marker" "${tmp:?}/all" || fail "output leaks a marker"
done

# the clone directory is private
[ "$(ls -ld "${tmp:?}/cache/agents/memory-store" | cut -c1-10)" = "drwx------" ] || fail "clone directory is not mode 700"

echo "$me: ok"
