#!/bin/sh
# Invariant: every card file in docs/runbook/ has its index line, and every
# index line names a file that exists.
#
# The index is the "## Card results" section at the end of
# docs/plans/metal-port-harness.md, one "- [CARD](../runbook/CARD.md): title"
# line per card. docs/runbook/README.md is not a card file. Fails on a
# docs/runbook/<CARD>.md with no index line, on an index line whose file does
# not exist, and on a card indexed twice.
#
# Also fails on a bare "the runbook" pointer in the card files and the
# harness document: since the split there is no single runbook, so a pointer
# names the card's file, or docs/plans/metal-port-harness.md for the method
# ("the shared runbook" and a linked "[the runbook](...)" are allowed). The
# text is joined across line breaks first, so a wrapped pointer is found too.
#
# Usage: tests/check-runbook-index.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C

index=docs/plans/metal-port-harness.md
dir=docs/runbook

tmp=$(mktemp -d "${TMPDIR:-/tmp}/runbookindex.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT

# The card ids the index names, one per line, from its section only.
awk '/^## Card results/ { on = 1; next } /^## / { on = 0 } on' "$index" |
	sed -n -E 's/^- \[([A-Za-z0-9_-]+)\]\(\.\.\/runbook\/([A-Za-z0-9_-]+)\.md\).*/\1 \2/p' > "${tmp:?}/pairs"

fail=0
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
	if ! grep -qx "$card" "${tmp:?}/names"; then
		echo "check-runbook-index: $f has no index line in $index"
		fail=1
	fi
done

for f in "$dir"/*.md "$index"; do
	n=$(tr '\n' ' ' < "$f" | sed -E 's/  +/ /g' |
		grep -oiE '(^|[^[])the runbook' | wc -l | tr -d ' ')
	if [ "$n" -ne 0 ]; then
		echo "check-runbook-index: $f has $n bare \"the runbook\" pointer(s): name the card file or $index"
		fail=1
	fi
done

[ "$fail" -eq 0 ] && echo "check-runbook-index: ok"
exit "$fail"
