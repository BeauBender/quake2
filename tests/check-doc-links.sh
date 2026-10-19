#!/bin/sh
# Invariant: relative links in tracked Markdown resolve.
#
# Checks [text](target) links whose target is a relative path (no scheme,
# no leading #). An anchor suffix (#section) is ignored; the file must
# exist relative to the linking document. External URLs are not fetched.
#
# Usage: tests/check-doc-links.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C

tmp=$(mktemp "${TMPDIR:-/tmp}/doclinks.XXXXXX")
trap 'rm -f "$tmp"' EXIT

# One "doc<TAB>target" line per link; read line by line so names with
# spaces or glob characters are taken literally.
git ls-files -z '*.md' | xargs -0 grep -oHE '\]\([^) ]+\)' 2>/dev/null |
	sed -E 's/^([^:]+):\]\((.*)\)$/\1	\2/' > "$tmp" || true

fail=0
while IFS='	' read -r doc target; do
	case $target in
	*://*|mailto:*|\#*) continue ;;
	esac
	file=${target%%#*}
	[ -n "$file" ] || continue
	if [ ! -e "$(dirname "$doc")/$file" ]; then
		echo "check-doc-links: $doc links to missing $target"
		fail=1
	fi
done < "$tmp"
[ "$fail" -eq 0 ] && echo "check-doc-links: ok"
exit "$fail"
