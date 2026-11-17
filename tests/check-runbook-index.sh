#!/bin/sh
# Invariant: every card file in docs/runbook/ has its index line, and every
# index line names a file that exists.
#
# The index is docs/runbook/INDEX.md: a short header and one
# "- [CARD](../runbook/CARD.md): title" line per card, in no checked order (a
# union merge may interleave lines). docs/runbook/README.md and INDEX.md are
# not card files. Fails on a docs/runbook/<CARD>.md with no index line, on an
# index line whose file does not exist, on a card indexed twice, and on a
# list line of the index that is not an index line.
#
# Also builds a scratch git repository and merges two branches that each add
# one line at the same place of the index, with the repository's own
# .gitattributes: the merge must be clean and keep both lines, and the same
# merge without the attribute must stop on a conflict in the index, so the
# test cannot pass without the attribute doing the work. The index is the
# only union-merged path: the top-level .gitattributes holds that one rule,
# no other tracked .gitattributes names merge=union, and git check-attr over
# every tracked file reports union for the index alone.
#
# Every GIT_* repository and configuration variable is unset first, so a git
# hook's GIT_DIR, GIT_WORK_TREE or GIT_INDEX_FILE points neither the scratch
# repositories nor the tree's own reads at another repository. The scratch
# git calls (sg) also read no global, system or user-level configuration,
# attributes or ignore file: HOME and XDG_CONFIG_HOME are inside the check's
# own scratch directory, GIT_CONFIG_GLOBAL is /dev/null and
# GIT_CONFIG_NOSYSTEM and GIT_ATTR_NOSYSTEM are set, and git init takes no
# template.
#
# Also fails on a bare "the runbook" pointer in any Markdown file under
# docs/: since the split there is no single runbook, so a pointer
# names the card's file, or docs/plans/metal-port-harness.md for the method
# ("the shared runbook" and a linked "[the runbook](...)" are allowed). The
# text is joined across line breaks first, so a wrapped pointer is found too.
#
# Self-cases: the check runs itself again (with --no-selftest) with GIT_DIR,
# GIT_WORK_TREE and GIT_INDEX_FILE naming a decoy repository, and with a
# user-level attributes file that union-merges every path; it must pass both
# times and leave the decoy as it was.
#
# Usage: tests/check-runbook-index.sh [--no-selftest]
set -eu
CDPATH='' cd "$(dirname "$0")/.."
export LC_ALL=C
me=check-runbook-index

unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_OBJECT_DIRECTORY \
	GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE GIT_PREFIX GIT_CONFIG \
	GIT_CONFIG_PARAMETERS GIT_CONFIG_COUNT GIT_DEFAULT_HASH GIT_TEMPLATE_DIR \
	GIT_AUTHOR_DATE GIT_COMMITTER_DATE GIT_EXTERNAL_DIFF GIT_DIFF_OPTS \
	GIT_SSH GIT_SSH_COMMAND GIT_ASKPASS GIT_EDITOR GIT_SEQUENCE_EDITOR

method=docs/plans/metal-port-harness.md
dir=docs/runbook
index=$dir/INDEX.md
index_re=$(printf '%s\n' "$index" | sed 's/\./\\./g')
line1='- [FIX1](../runbook/FIX1.md): a'
line2='- [FIX2](../runbook/FIX2.md): b'

tmp=$(mktemp -d "${TMPDIR:-/tmp}/runbookindex.XXXXXX")
tmp=$(CDPATH='' cd "${tmp:?}" && pwd -P)
trap 'rm -rf "${tmp:?}"' EXIT
mkdir -p "${tmp:?}/home/.config"

# git for the scratch repositories only.
sg()
{
	HOME=$tmp/home XDG_CONFIG_HOME=$tmp/home/.config \
		GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 GIT_ATTR_NOSYSTEM=1 \
		GIT_CEILING_DIRECTORIES=$tmp GIT_MERGE_AUTOEDIT=no \
		GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@t \
		GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@t \
		git "$@"
}

fail=0

if [ ! -f "$index" ]; then
	echo "$me: $index is missing"
	exit 1
fi

# The card ids the index names, one pair per line.
sed -n -E 's/^- \[([A-Za-z0-9_-]+)\]\(\.\.\/runbook\/([A-Za-z0-9_-]+)\.md\): .*/\1 \2/p' "$index" > "${tmp:?}/pairs"

# Every list line must be an index line; a malformed one is not skipped.
grep '^- ' "$index" |
	grep -v -E '^- \[[A-Za-z0-9_-]+\]\(\.\./runbook/[A-Za-z0-9_-]+\.md\): .' > "${tmp:?}/bad" || true
if [ -s "${tmp:?}/bad" ]; then
	echo "$me: malformed index line(s) in $index:"
	cat "${tmp:?}/bad"
	fail=1
fi

while read -r name target; do
	if [ "$name" != "$target" ]; then
		echo "$me: index line [$name] links to $target.md"
		fail=1
	fi
	if [ ! -f "$dir/$target.md" ]; then
		echo "$me: index line for $target has no file $dir/$target.md"
		fail=1
	fi
done < "${tmp:?}/pairs"

sed 's/ .*//' "${tmp:?}/pairs" | sort > "${tmp:?}/names"
if [ "$(uniq -d < "${tmp:?}/names" | wc -l)" -ne 0 ]; then
	echo "$me: indexed twice: $(uniq -d < "${tmp:?}/names" | tr '\n' ' ')"
	fail=1
fi

for f in "$dir"/*.md; do
	card=$(basename "$f" .md)
	[ "$card" = README ] && continue
	[ "$card" = INDEX ] && continue
	if ! grep -Fqx -- "$card" "${tmp:?}/names"; then
		echo "$me: $f has no index line in $index"
		fail=1
	fi
done

# The union merge, in a scratch repository. $1 is the directory, $2 is "attr"
# to carry the repository's .gitattributes into it. Returns the status of the
# merge of branch a into branch b (0 clean, 1 conflict), or 2 when a setup
# step fails.
#
# set -e does not cover this body: the callers run merge_try on the left of
# ||, where POSIX turns set -e off for the whole call. So every setup step
# returns 2 itself, and each leg also checks the merge's result in the work
# tree or the index instead of trusting the status alone.
merge_try()
{
	d=$1
	sg init -q --template= "$d" || return 2
	mkdir -p "$d/$dir" || return 2
	if [ "$2" = attr ]; then
		cp .gitattributes "$d/.gitattributes" || return 2
	fi
	sed '/^- /,$d' "$index" > "$d/$index" || return 2
	sg -C "$d" add -A || return 2
	sg -C "$d" commit -q -m base || return 2
	sg -C "$d" checkout -q -b a || return 2
	printf '%s\n' "$line1" >> "$d/$index" || return 2
	sg -C "$d" commit -q -a -m a || return 2
	sg -C "$d" checkout -q - || return 2
	sg -C "$d" checkout -q -b b || return 2
	printf '%s\n' "$line2" >> "$d/$index" || return 2
	sg -C "$d" commit -q -a -m b || return 2
	sg -C "$d" merge -q -m merge a > /dev/null 2>&1
}

if [ ! -f .gitattributes ]; then
	echo "$me: .gitattributes is missing"
	fail=1
else
	rc=0
	merge_try "${tmp:?}/with" attr || rc=$?
	if [ "$rc" -ne 0 ] ||
		! grep -Fqx -- "$line1" "${tmp:?}/with/$index" ||
		! grep -Fqx -- "$line2" "${tmp:?}/with/$index"; then
		echo "$me: two branches adding a line to $index at the same place do not merge cleanly with both lines kept (merge status $rc)"
		fail=1
	fi
	rc=0
	merge_try "${tmp:?}/without" noattr || rc=$?
	unmerged=$(sg -C "${tmp:?}/without" ls-files -u -- "$index" 2>/dev/null) || unmerged=
	if [ "$rc" -ne 1 ] || [ -z "$unmerged" ]; then
		echo "$me: the scratch merge without the attribute did not stop on a conflict in $index (merge status $rc), so the test proves nothing"
		fail=1
	fi
	if [ "$(grep -v -c -E '^[[:space:]]*(#|$)' .gitattributes)" -ne 1 ] ||
		! grep -q -E "^$index_re[[:space:]]+merge=union\$" .gitattributes; then
		echo "$me: .gitattributes must mark $index merge=union and nothing else"
		fail=1
	fi
fi

# Only the index is union-merged, nested attribute files included. The
# merge attribute of every tracked file is read from this work tree through
# a template-less scratch git directory, so neither this repository's
# .git/info/attributes nor a user-level or system attributes file decides.
if ! git -c core.quotePath=false ls-files > "${tmp:?}/files"; then
	echo "$me: git ls-files failed; cannot tell which files are union-merged"
	fail=1
else
	grep -E '(^|/)\.gitattributes$' "${tmp:?}/files" |
		grep -v -F -x .gitattributes > "${tmp:?}/nested" || true
	while read -r f; do
		[ -f "$f" ] || continue
		if grep -v -E '^[[:space:]]*#' "$f" | grep -q -F 'merge=union'; then
			echo "$me: $f names merge=union; only $index is union-merged"
			fail=1
		fi
	done < "${tmp:?}/nested"
	sg init -q --template= "${tmp:?}/probe"
	if ! sg --git-dir="${tmp:?}/probe/.git" --work-tree=. check-attr --stdin merge \
		< "${tmp:?}/files" > "${tmp:?}/attrs"; then
		echo "$me: git check-attr failed; cannot tell which files are union-merged"
		fail=1
	else
		grep -F ': merge: union' "${tmp:?}/attrs" > "${tmp:?}/union" || true
		if [ "$(cat "${tmp:?}/union")" != "$index: merge: union" ]; then
			echo "$me: git merges by union only $index, but check-attr reports:"
			cat "${tmp:?}/union"
			fail=1
		fi
	fi
fi

find docs -name '*.md' | sort > "${tmp:?}/docs"
while read -r f; do
	n=$(tr '\n' ' ' < "$f" | sed -E 's/  +/ /g' |
		grep -oiE '(^|[^[])the runbook' | wc -l | tr -d ' ')
	if [ "$n" -ne 0 ]; then
		echo "$me: $f has $n bare \"the runbook\" pointer(s): name the card file or $method"
		fail=1
	fi
done < "${tmp:?}/docs"

# Self-cases: this script run again with --no-selftest, in a child, under an
# environment that must not change what it does or where it writes.
snap()
{
	(CDPATH='' cd "$1" && find . -print | sort && find . -type f -exec cksum {} + | sort)
}
if [ "${1:-}" != --no-selftest ]; then
	self=tests/$(basename "$0")
	# A decoy repository with a commit and an index, named by GIT_DIR,
	# GIT_WORK_TREE and GIT_INDEX_FILE as a git hook exports them: the check
	# must pass and leave the decoy's refs, objects and index as they were.
	decoy=${tmp:?}/decoy
	sg init -q --template= "$decoy"
	printf '%s\n' decoy > "$decoy/a.txt"
	sg -C "$decoy" add a.txt
	sg -C "$decoy" commit -q -m decoy
	snap "$decoy" > "${tmp:?}/decoy.before"
	rc=0
	GIT_DIR=$decoy/.git GIT_WORK_TREE=$decoy GIT_INDEX_FILE=$decoy/.git/index \
		sh "$self" --no-selftest > "${tmp:?}/hostile.out" 2>&1 || rc=$?
	snap "$decoy" > "${tmp:?}/decoy.after"
	if [ "$rc" -ne 0 ] || ! cmp -s "${tmp:?}/decoy.before" "${tmp:?}/decoy.after"; then
		echo "$me: self-case: with GIT_DIR, GIT_WORK_TREE and GIT_INDEX_FILE naming a decoy repository the check exited $rc, or changed the decoy:"
		cat "${tmp:?}/hostile.out"
		diff "${tmp:?}/decoy.before" "${tmp:?}/decoy.after" | head -20 || true
		fail=1
	fi
	# A user-level attributes file that union-merges every path must decide
	# neither the scratch merge nor the attribute sweep.
	mkdir -p "${tmp:?}/xdg/git"
	printf '%s\n' '* merge=union' > "${tmp:?}/xdg/git/attributes"
	rc=0
	XDG_CONFIG_HOME=${tmp:?}/xdg sh "$self" --no-selftest > "${tmp:?}/xdg.out" 2>&1 || rc=$?
	if [ "$rc" -ne 0 ]; then
		echo "$me: self-case: with a user-level attributes file holding \"* merge=union\" the check exited $rc:"
		cat "${tmp:?}/xdg.out"
		fail=1
	fi
fi

[ "$fail" -eq 0 ] && echo "$me: ok"
exit "$fail"
