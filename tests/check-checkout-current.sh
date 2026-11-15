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
# clones of it. The hook must fast-forward a clean main that is behind
# (also with a hostile GIT_DIR, GIT_WORK_TREE and GIT_INDEX_FILE), writing
# nothing outside that clone, also over a tracked file that becomes a
# directory and, on a case-insensitive volume, a case-only rename; merge
# the commit it counted when a fetch lands at the merge (a shim); and
# report a current main. It must refuse, leaving HEAD and the working tree
# as they were, on tracked changes, a diverged main, a main ahead (never
# reported as updated), another branch, a HEAD that tracks
# .claude/memory/x.md, a failed fetch, a held index lock, a failing git
# status, ls-tree, symbolic-ref or rev-list (a shim), and a local file or
# link it does not track in the update's way: ignored at an added path or
# where a directory is needed, or ignored or untracked inside a directory
# that becomes a file. In a linked worktree it prints nothing. Every run
# must exit 0 and print no scratch path, so no remote URL.
#
# The real check then reads the hook and the settings file in this
# checkout: the hook holds no reset, rebase, checkout, switch, restore,
# stash, clean or pull, and each merge holds --ff-only and
# --no-overwrite-ignore in the command, not in a trailing comment;
# .claude/settings.json registers the hook for the startup and resume
# matchers with a timeout of at most 30 seconds, keeps the compact and
# Stop hooks, and its allow and deny lists are exactly the ones main had
# before the hook (this card adds no permission rule); and it parses as
# JSON when python3 is present. It never runs the hook on this checkout,
# so it needs no network.
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
	clone behind hostile dirty diverged other fetchfail wtmain shims inway leading \
		inlink leadlink locked
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

	# ign <repo> <path> [<target>]: an ignored local file at <path>, or an
	# ignored link there to <target>.
	ign()
	{
		mkdir -p "${tmp:?}/$1/.git/info"
		printf '%s\n' "$2" >> "${tmp:?}/$1/.git/info/exclude"
		if [ -n "${3:-}" ]; then
			ln -s "$3" "${tmp:?}/$1/$2"
		else
			printf 'local %s\n' "$2" > "${tmp:?}/$1/$2"
		fi
	}
	# keep <repo> <path> <behind> <sha>: with a local file or link at <path>,
	# the hook refuses, <behind> commit(s) behind, without naming <path>;
	# HEAD stays <sha> and the file stays byte for byte, or the link as it was.
	keep()
	{
		if [ -L "${tmp:?}/$1/$2" ]; then
			readlink "${tmp:?}/$1/$2" > "${tmp:?}/$1.keep"
		else
			cp -p "${tmp:?}/$1/$2" "${tmp:?}/$1.keep"
		fi
		run "$1"
		says "$1" "not updated, $3 commit(s) behind origin/main: local untracked or ignored files would be overwritten; update by hand"
		case $out in
		*"$2"*) fail "selftest: $1: the hook named the path: $out" ;;
		esac
		still "$1" "$4"
		if [ -L "${tmp:?}/$1/$2" ]; then
			readlink "${tmp:?}/$1/$2" | cmp -s "${tmp:?}/$1.keep" -
		else
			cmp -s "${tmp:?}/$1.keep" "${tmp:?}/$1/$2"
		fi || fail "selftest: $1: the local $2 was changed"
	}
	# b.txt, which the update starts tracking, and newdir, where the update
	# needs a directory for newdir/c.txt: a file, a dangling link, and a
	# link to an empty directory that must stay empty.
	ign inway b.txt
	keep inway b.txt 2 "$a"
	ign leading newdir
	keep leading newdir 2 "$a"
	ign inlink b.txt nowhere
	keep inlink b.txt 2 "$a"
	mkdir -p "${tmp:?}/linked"
	ign leadlink newdir "${tmp:?}/linked"
	keep leadlink newdir 2 "$a"
	[ -z "$(ls -A "${tmp:?}/linked")" ] || fail "selftest: leadlink: the update wrote through the link"

	# Another git process holds the index lock: the merge fails for a reason
	# other than a file in the way.
	: > "${tmp:?}/locked/.git/index.lock"
	run locked
	says locked "not updated, 2 commit(s) behind origin/main: git merge --ff-only failed (another git process may be running); update by hand"
	still locked "$a"

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
	for w in status ls-tree symbolic-ref rev-list "$a..$b" "$b..$a"; do
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
	# a.txt/d.txt).
	clone typechange
	g -C "${tmp:?}/seed" rm -q a.txt
	put seed a.txt/d.txt four
	g -C "${tmp:?}/seed" push -q origin main
	t=$(rev seed)
	run typechange
	says typechange "updated main from"
	still typechange "$t"

	# E turns the tracked directory newdir into a file, with an ignored and
	# an untracked file inside it.
	clone dirign dirmix
	ign dirign newdir/keep.o
	printf '%s\n' untracked > "${tmp:?}/dirmix/newdir/u.txt"
	g -C "${tmp:?}/seed" rm -q -r newdir
	put seed newdir five
	g -C "${tmp:?}/seed" push -q origin main
	keep dirign newdir/keep.o 1 "$t"
	keep dirmix newdir/u.txt 1 "$t"

	# G renames readme.txt (F) to README.txt. On a case-insensitive volume
	# the old file answers to the new name, and the update still goes on.
	put seed readme.txt six
	g -C "${tmp:?}/seed" push -q origin main
	clone case
	g -C "${tmp:?}/seed" mv readme.txt README.txt
	g -C "${tmp:?}/seed" commit -q -m rename
	g -C "${tmp:?}/seed" push -q origin main
	rn=$(rev seed)
	: > "${tmp:?}/CaseProbe"
	if [ -e "${tmp:?}/caseprobe" ]; then
		run case
		says case "updated main from"
		still case "$rn"
	else
		echo "$me: selftest: case: skipped, the scratch volume is case-sensitive"
	fi

	# The race: origin/main is at H when the hook counts, and a shim pushes
	# I and fetches it as the merge starts. The hook merges H, the commit
	# it counted, and says so.
	clone race
	put seed h.txt seven
	g -C "${tmp:?}/seed" push -q origin main
	h=$(rev seed)
	put seed i.txt eight
	mkdir -p "${tmp:?}/raceshim"
	printf '#!/bin/sh\ncase " $* " in *" merge "*) "%s" -C "%s" push -q origin main > /dev/null 2>&1; "%s" fetch -q origin main > /dev/null 2>&1 ;; esac\nexec "%s" "$@"\n' \
		"$realgit" "${tmp:?}/seed" "$realgit" "$realgit" > "${tmp:?}/raceshim/git"
	chmod 755 "${tmp:?}/raceshim/git"
	run race PATH="${tmp:?}/raceshim:$PATH"
	says race "updated main from $(printf '%.12s' "$rn") to $(printf '%.12s' "$h") (1 commit(s))"
	still race "$h"
	[ "$(g -C "${tmp:?}/race" rev-parse refs/remotes/origin/main)" = "$(rev seed)" ] ||
		fail "selftest: race: the shim did not fetch the newer commit"
}

# Not on the left of ||, so set -e holds inside: a failed setup step stops it.
( selftest )

# The hook runs no git command that can rewrite history or drop changes,
# and merges only by fast-forward, refusing to touch local files it does
# not track: a git subcommand (after any -c, -C or --option) on a line
# that is not a comment. A grep failure fails.
[ -f "$hook" ] || fail "the hook .claude/hooks/checkout-current.sh is missing"
grep -v '^[[:space:]]*#' "$hook" > "${tmp:?}/code" || fail "cannot read the hook"
pre='(^|[^A-Za-z0-9_-])git( +-c +[^ ]+| +-C +[^ ]+| +--[a-z-]+)* +'
rc=0
grep -Eq "${pre}(reset|rebase|checkout|switch|restore|stash|clean|pull)([^A-Za-z0-9-]|\$)" "${tmp:?}/code" || rc=$?
[ "$rc" -eq 1 ] || fail "the hook runs a git command it must never run (reset, rebase, checkout, switch, restore, stash, clean, pull), or grep failed"
# Every line that names a git merge, outside quoted text that expands
# nothing (a message), with its trailing comment cut: the merge command
# itself must hold both flags. There must be one.
sed -e 's/"[^"$`]*"//g' -e "s/'[^']*'//g" "${tmp:?}/code" > "${tmp:?}/lit" || fail "sed failed on the hook"
rc=0
grep -E "${pre}merge([^A-Za-z0-9-]|\$)" "${tmp:?}/lit" > "${tmp:?}/merges" || rc=$?
[ "$rc" -eq 0 ] || fail "the hook runs no merge, or grep failed"
sed 's/[[:space:]]#.*//' "${tmp:?}/merges" > "${tmp:?}/cut" || fail "sed failed on the hook"
for flag in --ff-only --no-overwrite-ignore; do
	rc=0
	grep -Evq -e "${pre}merge( +[^ ;&|()]+)* +$flag([^A-Za-z0-9-]|\$)" "${tmp:?}/cut" || rc=$?
	[ "$rc" -eq 1 ] || fail "the hook runs a merge without $flag in the command, or grep failed"
done

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
	'Bash(tests/check-removed-deps.sh)' 'Bash(tests/check-doc-links.sh)' 'Bash(make config)' \
	'Bash(git -C ~/.config/agents/memory-store status --porcelain quake2/memory)' \
	'Bash(git -C ~/.config/agents/memory-store status --porcelain=v2 --branch quake2/memory)' \
	'Bash(git -C ~/.config/agents/memory-store add -A quake2/memory)' \
	'Bash(git -C ~/.config/agents/memory-store commit -q -m quake2-memory-sync quake2/memory)' \
	'Bash(git -C ~/.config/agents/memory-store pull --rebase -q)' \
	'Bash(git -C ~/.config/agents/memory-store push -q)' \
	'Bash(git -C ~/.config/agents/memory-store -c core.editor=true rebase --continue)' \
	'Read(~/.config/agents/memory-store/**)' \
	'Edit(~/.config/agents/memory-store/quake2/memory/**)' \
	'Write(~/.config/agents/memory-store/quake2/memory/**)')
# The memory-store rules above are TASK127's; tests/check-memory-rules.sh pins and justifies them.
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
