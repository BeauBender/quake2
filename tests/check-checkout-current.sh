#!/bin/sh
# Invariant: the SessionStart hook .claude/hooks/checkout-current.sh keeps
# the main checkout current with origin/main by fast-forward only, refuses
# every other case without moving HEAD or the working tree, and is
# registered in .claude/settings.json beside permission rules that are
# exactly the expected ones.
#
# The self-test runs in a subshell with the GIT_* repository and
# configuration variables unset, no global or system configuration, no git
# hooks, no signing, a synthetic author and committer, and HOME inside a
# private mktemp -d, on throwaway repositories there: a bare origin and
# clones of it. It shows that the hook fast-forwards a clean main that is
# behind (also with a hostile GIT_DIR, GIT_WORK_TREE and GIT_INDEX_FILE in
# its environment) and writes nothing outside that clone, reports a current
# one, and refuses, leaving HEAD and the working tree as they were (the
# fetch moves only origin/main), on tracked changes, a diverged main, a
# main ahead of origin/main (reported with its local commits, never as
# updated), another branch, a HEAD that tracks .claude/memory/x.md (the
# file stays), an ignored local file at a path the update starts tracking
# or where it needs a directory (the file stays, byte for byte), a path
# git prints quoted, a failed fetch, and a failing git status, ls-tree,
# symbolic-ref, rev-list (either count) or diff-tree (a shim first on
# PATH). It fast-forwards over a tracked file the update turns into a
# directory. In a linked worktree it prints nothing and changes nothing.
# Every run must exit 0 and print no path under the scratch directory, so
# no remote URL.
#
# The real check then reads the hook and the settings file in this
# checkout: the hook holds no reset,
# rebase, checkout, switch, restore, stash, clean or pull and no merge
# without --ff-only; .claude/settings.json registers the hook for the
# startup and resume matchers with a timeout of at most 30 seconds, keeps
# the compact and Stop hooks, and its allow and deny lists are exactly the
# ones main had before the hook (this card adds no permission rule); and
# it parses as JSON when python3 is present. It never runs the hook on
# this checkout, so it needs no network.
#
# Usage: tests/check-checkout-current.sh
set -eu
CDPATH='' cd "$(dirname "$0")/.."
hook=$(pwd)/.claude/hooks/checkout-current.sh
settings=.claude/settings.json
me=check-checkout-current
tmp=$(mktemp -d "${TMPDIR:-/tmp}/ckcur.XXXXXX")
chmod 700 "${tmp:?}"
tmp=$(CDPATH='' cd "${tmp:?}" && pwd -P)
trap 'rm -rf "${tmp:?}"' EXIT
trap 'exit 1' HUP INT TERM

fail()
{
	echo "$me: $*" >&2
	exit 1
}

selftest()
{
	unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_OBJECT_DIRECTORY \
		GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE GIT_PREFIX GIT_CONFIG \
		GIT_CONFIG_PARAMETERS GIT_CONFIG_COUNT GIT_DEFAULT_HASH GIT_TEMPLATE_DIR \
		GIT_AUTHOR_DATE GIT_COMMITTER_DATE GIT_SSH GIT_SSH_COMMAND GIT_ASKPASS \
		GIT_EDITOR GIT_SEQUENCE_EDITOR
	HOME=$tmp/home
	XDG_CONFIG_HOME=$tmp/home/.config
	GIT_CONFIG_GLOBAL=/dev/null
	GIT_CONFIG_NOSYSTEM=1
	GIT_CEILING_DIRECTORIES=$tmp
	GIT_AUTHOR_NAME=selftest
	GIT_AUTHOR_EMAIL=selftest@example.invalid
	GIT_COMMITTER_NAME=selftest
	GIT_COMMITTER_EMAIL=selftest@example.invalid
	GIT_MERGE_AUTOEDIT=no
	export HOME XDG_CONFIG_HOME GIT_CONFIG_GLOBAL GIT_CONFIG_NOSYSTEM \
		GIT_CEILING_DIRECTORIES GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL \
		GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL GIT_MERGE_AUTOEDIT
	mkdir -p "${tmp:?}/home"

	g()
	{
		git -c core.hooksPath=/dev/null -c commit.gpgSign=false "$@"
	}
	# put <repo> <file> <text>: commit <file> holding <text>.
	put()
	{
		mkdir -p "$(dirname "${tmp:?}/$1/$2")"
		printf '%s\n' "$3" > "${tmp:?}/$1/$2"
		g -C "${tmp:?}/$1" add -- "$2"
		g -C "${tmp:?}/$1" commit -q -m "$3"
	}
	rev()
	{
		g -C "${tmp:?}/$1" rev-parse HEAD
	}
	clone()
	{
		for c in "$@"; do
			g clone -q --template= "${tmp:?}/origin.git" "${tmp:?}/$c"
		done
	}
	# run <repo> [NAME=value...]: run the hook on <repo>; it must exit 0
	# and print no scratch path, so no remote URL.
	run()
	{
		r=$1
		shift
		rc=0
		out=$(env "$@" CLAUDE_PROJECT_DIR="${tmp:?}/$r" "$hook" 2>&1 < /dev/null) || rc=$?
		[ "$rc" -eq 0 ] || fail "selftest: $r: the hook exited $rc"
		case $out in
		*ckcur.* | *origin.git* | *nowhere*) fail "selftest: $r: the hook printed a path or a remote URL" ;;
		esac
	}
	says()
	{
		case $out in
		*"$2"*) ;;
		*) fail "selftest: $1: expected \"$2\" in the hook's line, got: $out" ;;
		esac
	}
	still()
	{
		[ "$(rev "$1")" = "$2" ] || fail "selftest: $1: HEAD moved"
	}

	# origin: A (a.txt), M (adds .claude/memory/x.md), B (removes it, adds
	# b.txt and newdir/c.txt).
	g init -q --bare --template= "${tmp:?}/origin.git"
	g -C "${tmp:?}/origin.git" symbolic-ref HEAD refs/heads/main
	g init -q --template= "${tmp:?}/seed"
	g -C "${tmp:?}/seed" symbolic-ref HEAD refs/heads/main
	g -C "${tmp:?}/seed" remote add origin "${tmp:?}/origin.git"
	put seed a.txt one
	g -C "${tmp:?}/seed" push -q origin main
	a=$(rev seed)
	clone behind hostile dirty diverged other fetchfail wtmain shims inway leading
	put seed .claude/memory/x.md note
	g -C "${tmp:?}/seed" push -q origin main
	clone mem
	m=$(rev mem)
	g -C "${tmp:?}/seed" rm -q .claude/memory/x.md
	mkdir -p "${tmp:?}/seed/newdir"
	printf '%s\n' three > "${tmp:?}/seed/newdir/c.txt"
	g -C "${tmp:?}/seed" add -- newdir/c.txt
	put seed b.txt two
	g -C "${tmp:?}/seed" push -q origin main
	b=$(rev seed)
	clone current ahead

	# Making the marker touches the scratch directory itself, so that one
	# entry is left out; any file written under it still shows.
	touch "${tmp:?}/marker"
	run behind
	says behind "updated main from"
	still behind "$b"
	[ -z "$(find "${tmp:?}" -newer "${tmp:?}/marker" ! -path "${tmp:?}" ! -path "${tmp:?}/behind" ! -path "${tmp:?}/behind/*")" ] ||
		fail "selftest: behind: the hook wrote outside the checkout"

	g init -q --template= "${tmp:?}/decoy"
	g -C "${tmp:?}/decoy" symbolic-ref HEAD refs/heads/main
	put decoy a.txt decoy
	d=$(rev decoy)
	run hostile GIT_DIR="${tmp:?}/decoy/.git" GIT_WORK_TREE="${tmp:?}/decoy" \
		GIT_INDEX_FILE="${tmp:?}/decoy/.git/index"
	says hostile "updated main from"
	still hostile "$b"
	still decoy "$d"

	run current
	says current "already current"
	still current "$b"

	printf '%s\n' changed > "${tmp:?}/dirty/a.txt"
	run dirty
	says dirty "not updated, 2 commit(s) behind origin/main: tracked changes"
	still dirty "$a"
	[ "$(cat "${tmp:?}/dirty/a.txt")" = changed ] || fail "selftest: dirty: the change was lost"

	put diverged c.txt local
	c=$(rev diverged)
	run diverged
	says diverged "not updated, 2 commit(s) behind origin/main: main has diverged"
	still diverged "$c"

	put ahead c.txt ahead
	h=$(rev ahead)
	run ahead
	says ahead "not updated, 0 commit(s) behind origin/main: main has 1 local commit(s) not on origin/main"
	still ahead "$h"

	g -C "${tmp:?}/other" checkout -q -b feature
	run other
	says other "not updated, 2 commit(s) behind origin/main: the checkout is not on branch main"
	still other "$a"
	[ "$(g -C "${tmp:?}/other" symbolic-ref HEAD)" = refs/heads/feature ] ||
		fail "selftest: other: the branch changed"

	run mem
	says mem "not updated, 1 commit(s) behind origin/main: HEAD tracks files under .claude/memory/"
	still mem "$m"
	[ -f "${tmp:?}/mem/.claude/memory/x.md" ] || fail "selftest: mem: the memory file was deleted"

	# keep <repo> <path>: an ignored local file at <path>, with a copy to
	# compare against. The refusal names the count, never the path.
	keep()
	{
		mkdir -p "${tmp:?}/$1/.git/info"
		printf '%s\n' "$2" >> "${tmp:?}/$1/.git/info/exclude"
		printf 'local %s\n' "$2" > "${tmp:?}/$1/$2"
		cp -p "${tmp:?}/$1/$2" "${tmp:?}/$1.keep"
		run "$1"
		says "$1" "not updated, 2 commit(s) behind origin/main: 1 path(s) the update adds"
		case $out in
		*"$2"*) fail "selftest: $1: the hook named the path: $out" ;;
		esac
		still "$1" "$a"
		cmp -s "${tmp:?}/$1.keep" "${tmp:?}/$1/$2" ||
			fail "selftest: $1: the ignored file $2 was overwritten"
	}
	# b.txt, which the update starts tracking, and newdir, where the update
	# needs a directory for newdir/c.txt.
	keep inway b.txt
	keep leading newdir

	g -C "${tmp:?}/fetchfail" fetch -q origin
	g -C "${tmp:?}/fetchfail" remote set-url origin "${tmp:?}/nowhere.git"
	run fetchfail
	says fetchfail "not updated, 2 commit(s) behind origin/main as of the last fetch: git fetch origin main failed"
	still fetchfail "$a"
	[ "$(g -C "${tmp:?}/fetchfail" rev-parse refs/remotes/origin/main)" = "$b" ] ||
		fail "selftest: fetchfail: origin/main moved"

	g -C "${tmp:?}/wtmain" checkout -q -b side
	g -C "${tmp:?}/wtmain" worktree add -q "${tmp:?}/wt" main
	run wt
	[ -z "$out" ] || fail "selftest: wt: the hook printed in a linked worktree: $out"
	still wt "$a"

	# A git subcommand that fails while the rest work: a shim first on PATH
	# exits 128 when its arguments hold <word> and runs git otherwise. The
	# range $a..$b fails only the count behind, and $b..$a only the count
	# of local commits after it.
	realgit=$(command -v git)
	i=0
	for w in status ls-tree symbolic-ref rev-list "$a..$b" "$b..$a" diff-tree; do
		i=$((i + 1))
		mkdir -p "${tmp:?}/shim-$i"
		printf '#!/bin/sh\ncase " $* " in *" %s "*) exit 128 ;; esac\nexec "%s" "$@"\n' \
			"$w" "$realgit" > "${tmp:?}/shim-$i/git"
		chmod 755 "${tmp:?}/shim-$i/git"
		run shims PATH="${tmp:?}/shim-$i:$PATH"
		says "shims ($w)" "not updated"
		still shims "$a"
	done

	# Last, as they move origin/main past B. A tracked file the update
	# turns into a directory is not in the way (C: a.txt becomes
	# a.txt/d.txt), and a path git prints quoted (D: q"x.txt) is not
	# checked and refuses the update even with nothing there.
	clone typechange
	g -C "${tmp:?}/seed" rm -q a.txt
	put seed a.txt/d.txt four
	g -C "${tmp:?}/seed" push -q origin main
	t=$(rev seed)
	run typechange
	says typechange "updated main from"
	still typechange "$t"
	clone quoted
	put seed 'q"x.txt' five
	g -C "${tmp:?}/seed" push -q origin main
	run quoted
	says quoted "not updated, 1 commit(s) behind origin/main: 1 path(s) the update adds"
	still quoted "$t"
	[ ! -e "${tmp:?}/quoted/q\"x.txt" ] || fail "selftest: quoted: the path was written"
}

# Not on the left of ||, so set -e holds inside: a failed setup step stops it.
( selftest )

# The hook runs no git command that can rewrite history or drop changes,
# and merges only by fast-forward: a git subcommand (after any -c, -C or
# --option) on a line that is not a comment. A grep failure fails.
[ -f "$hook" ] || fail "the hook .claude/hooks/checkout-current.sh is missing"
grep -v '^[[:space:]]*#' "$hook" > "${tmp:?}/code" || fail "cannot read the hook"
pre='(^|[^A-Za-z0-9_-])git( +-c +[^ ]+| +-C +[^ ]+| +--[a-z-]+)* +'
rc=0
grep -Eq "${pre}(reset|rebase|checkout|switch|restore|stash|clean|pull)([^A-Za-z0-9-]|\$)" "${tmp:?}/code" || rc=$?
[ "$rc" -eq 1 ] || fail "the hook runs a git command it must never run (reset, rebase, checkout, switch, restore, stash, clean, pull), or grep failed"
rc=0
grep -E "${pre}merge([^A-Za-z0-9-]|\$)" "${tmp:?}/code" > "${tmp:?}/merges" || rc=$?
[ "$rc" -le 1 ] || fail "grep failed on the hook"
rc=0
grep -vq -e '--ff-only' "${tmp:?}/merges" || rc=$?
[ "$rc" -eq 1 ] || fail "the hook runs a merge without --ff-only, or grep failed"

# Every hook command in the settings, as "<event> <matcher> <script> <timeout>".
hooks=$(awk '
	/^[[:space:]]*"[A-Za-z]+": \[[[:space:]]*$/ {
		k = $0; sub(/^[[:space:]]*"/, "", k); sub(/".*/, "", k)
		if (k != "hooks" && k != "allow" && k != "deny") { ev = k; m = "-" }
	}
	/"matcher":/ { m = $0; sub(/.*"matcher":[[:space:]]*"/, "", m); sub(/".*/, "", m) }
	/"command":/ {
		c = $0; sub(/.*\.claude\/hooks\//, "", c); sub(/[^A-Za-z0-9._-].*/, "", c)
		t = "-"
		if (match($0, /"timeout":[[:space:]]*[0-9]+/)) { t = substr($0, RSTART, RLENGTH); sub(/.*[^0-9]/, "", t) }
		print ev, m, c, t
	}' "$settings")
want=$(printf '%s\n' 'SessionStart compact session-context.sh -' \
	'SessionStart startup checkout-current.sh 30' \
	'SessionStart resume checkout-current.sh 30' \
	'Stop - stop-invariants.sh 60')
[ "$hooks" = "$want" ] || fail "the hooks in $settings are not the expected ones (checkout-current.sh for startup and resume, timeout 30; compact and Stop unchanged); got: $hooks"

# list <key>: the entries of the "<key>": [ ... ] array, one per line.
list()
{
	awk -v key="\"$1\":" '
		index($0, key) && /\[[[:space:]]*$/ { on = 1; next }
		on && /^[[:space:]]*\]/ { exit }
		on { sub(/^[[:space:]]+/, ""); sub(/,[[:space:]]*$/, ""); print }' "$settings"
}
want=$(printf '"%s"\n' 'Bash(git status:*)' 'Bash(git diff:*)' 'Bash(git log:*)' 'Bash(git show:*)' \
	'Bash(scripts/test.sh)' 'Bash(tests/check-savegame-tables.sh)' 'Bash(tests/check-cvar-docs.sh)' \
	'Bash(tests/check-removed-deps.sh)' 'Bash(tests/check-doc-links.sh)' 'Bash(make config)')
[ "$(list allow)" = "$want" ] || fail "permissions.allow in $settings is not the expected list"
want=$(printf '"%s"\n' 'Bash(git push --no-verify:*)' 'Bash(git commit --no-verify:*)' \
	'Bash(git push --force origin main:*)' 'Bash(git push -f origin main:*)' \
	'Bash(git push origin --delete main:*)' 'Read(./**/*.pak)')
[ "$(list deny)" = "$want" ] || fail "permissions.deny in $settings is not the expected list"
if command -v python3 > /dev/null 2>&1; then
	python3 -I -c 'import json, sys; json.load(open(sys.argv[1]))' "$settings" ||
		fail "$settings is not valid JSON"
fi
echo "$me: ok"
