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

fail=0
for doc in $(git ls-files '*.md'); do
	dir=$(dirname "$doc")
	for target in $(grep -oE '\]\([^) ]+\)' "$doc" | sed -E 's/^\]\(//; s/\)$//'); do
		case $target in
		*://*|mailto:*|\#*) continue ;;
		esac
		file=${target%%#*}
		[ -n "$file" ] || continue
		if [ ! -e "$dir/$file" ]; then
			echo "check-doc-links: $doc links to missing $target"
			fail=1
		fi
	done
done
[ "$fail" -eq 0 ] && echo "check-doc-links: ok"
exit "$fail"
