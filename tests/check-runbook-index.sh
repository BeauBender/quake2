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
# merge without the attribute must conflict, so the test cannot pass without
# the attribute doing the work. .gitattributes must mark the index and nothing
# else as merge=union.
#
# Also fails on a bare "the runbook" pointer in any Markdown file under
# docs/: since the split there is no single runbook, so a pointer
# names the card's file, or docs/plans/metal-port-harness.md for the method
# ("the shared runbook" and a linked "[the runbook](...)" are allowed). The
# text is joined across line breaks first, so a wrapped pointer is found too.
#
# Usage: tests/check-runbook-index.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C

method=docs/plans/metal-port-harness.md
dir=docs/runbook
index=$dir/INDEX.md

tmp=$(mktemp -d "${TMPDIR:-/tmp}/runbookindex.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT

fail=0

if [ ! -f "$index" ]; then
	echo "check-runbook-index: $index is missing"
	exit 1
fi

# The card ids the index names, one pair per line.
sed -n -E 's/^- \[([A-Za-z0-9_-]+)\]\(\.\.\/runbook\/([A-Za-z0-9_-]+)\.md\): .*/\1 \2/p' "$index" > "${tmp:?}/pairs"

# Every list line must be an index line; a malformed one is not skipped.
grep '^- ' "$index" |
	grep -v -E '^- \[[A-Za-z0-9_-]+\]\(\.\./runbook/[A-Za-z0-9_-]+\.md\): .' > "${tmp:?}/bad" || true
if [ -s "${tmp:?}/bad" ]; then
	echo "check-runbook-index: malformed index line(s) in $index:"
	cat "${tmp:?}/bad"
	fail=1
fi

while read -r name target; do
	if [ "$name" != "$target" ]; then
		echo "check-runbook-index: index line [$name] links to $target.md"
		fail=1
	fi
	if [ ! -f "$dir/$target.md" ]; then
		echo "check-runbook-index: index line for $target has no file $dir/$target.md"
		fail=1
	fi
done < "${tmp:?}/pairs"

sed 's/ .*//' "${tmp:?}/pairs" | sort > "${tmp:?}/names"
if [ "$(uniq -d < "${tmp:?}/names" | wc -l)" -ne 0 ]; then
	echo "check-runbook-index: indexed twice: $(uniq -d < "${tmp:?}/names" | tr '\n' ' ')"
	fail=1
fi

for f in "$dir"/*.md; do
	card=$(basename "$f" .md)
	[ "$card" = README ] && continue
	[ "$card" = INDEX ] && continue
	if ! grep -qx "$card" "${tmp:?}/names"; then
		echo "check-runbook-index: $f has no index line in $index"
		fail=1
	fi
done

# The union merge, in a scratch repository. $1 is the directory, $2 is "attr"
# to carry the repository's .gitattributes into it. Succeeds when the merge of
# branch a into branch b is clean.
merge_try() {
	d=$1
	git init -q "$d"
	mkdir -p "$d/docs/runbook"
	if [ "$2" = attr ]; then
		cp .gitattributes "$d/.gitattributes"
	fi
	sed '/^- /,$d' "$index" > "$d/$index"
	git -C "$d" add -A
	git -C "$d" commit -q -m base
	git -C "$d" checkout -q -b a
	printf '%s\n' '- [FIX1](../runbook/FIX1.md): a' >> "$d/$index"
	git -C "$d" commit -q -a -m a
	git -C "$d" checkout -q -
	git -C "$d" checkout -q -b b
	printf '%s\n' '- [FIX2](../runbook/FIX2.md): b' >> "$d/$index"
	git -C "$d" commit -q -a -m b
	git -C "$d" merge -q -m merge a > /dev/null 2>&1
}

GIT_CONFIG_GLOBAL=/dev/null
GIT_CONFIG_NOSYSTEM=1
GIT_AUTHOR_NAME=t
GIT_AUTHOR_EMAIL=t@t
GIT_COMMITTER_NAME=t
GIT_COMMITTER_EMAIL=t@t
export GIT_CONFIG_GLOBAL GIT_CONFIG_NOSYSTEM GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL

if [ ! -f .gitattributes ]; then
	echo "check-runbook-index: .gitattributes is missing"
	fail=1
else
	if merge_try "${tmp:?}/with" attr &&
		grep -q 'FIX1\]' "${tmp:?}/with/$index" &&
		grep -q 'FIX2\]' "${tmp:?}/with/$index"; then
		:
	else
		echo "check-runbook-index: two branches adding a line to $index at the same place do not merge cleanly with both lines kept"
		fail=1
	fi
	if merge_try "${tmp:?}/without" noattr; then
		echo "check-runbook-index: the scratch merge without the attribute did not conflict, so the test proves nothing"
		fail=1
	fi
	if [ "$(grep -v -c -E '^[[:space:]]*(#|$)' .gitattributes)" -ne 1 ] ||
		! grep -q -E "^$index[[:space:]]+merge=union$" .gitattributes; then
		echo "check-runbook-index: .gitattributes must mark $index merge=union and nothing else"
		fail=1
	fi
fi

find docs -name '*.md' | sort > "${tmp:?}/docs"
while read -r f; do
	n=$(tr '\n' ' ' < "$f" | sed -E 's/  +/ /g' |
		grep -oiE '(^|[^[])the runbook' | wc -l | tr -d ' ')
	if [ "$n" -ne 0 ]; then
		echo "check-runbook-index: $f has $n bare \"the runbook\" pointer(s): name the card file or $method"
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
	git init -q --template= "$decoy"
	printf '%s\n' decoy > "$decoy/a.txt"
	git -C "$decoy" add a.txt
	git -C "$decoy" commit -q -m decoy
	snap "$decoy" > "${tmp:?}/decoy.before"
	rc=0
	GIT_DIR=$decoy/.git GIT_WORK_TREE=$decoy GIT_INDEX_FILE=$decoy/.git/index \
		sh "$self" --no-selftest > "${tmp:?}/hostile.out" 2>&1 || rc=$?
	snap "$decoy" > "${tmp:?}/decoy.after"
	if [ "$rc" -ne 0 ] || ! cmp -s "${tmp:?}/decoy.before" "${tmp:?}/decoy.after"; then
		echo "check-runbook-index: self-case: with GIT_DIR, GIT_WORK_TREE and GIT_INDEX_FILE naming a decoy repository the check exited $rc, or changed the decoy:"
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
		echo "check-runbook-index: self-case: with a user-level attributes file holding \"* merge=union\" the check exited $rc:"
		cat "${tmp:?}/xdg.out"
		fail=1
	fi
fi

[ "$fail" -eq 0 ] && echo "check-runbook-index: ok"
exit "$fail"
