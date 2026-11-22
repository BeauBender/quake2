#!/bin/sh
# Drop the duplicate rows a union merge leaves in docs/runbook/INDEX.md.
#
# The index is merged by union (.gitattributes), so when main edits a row that
# a branch also carries, a merge of main keeps both versions of the row and
# tests/check-runbook-index.sh fails with "indexed twice". This script keeps,
# for each [ID] that has more than one row, the row exactly as it is in a
# reference version of the index and drops the others. The kept row takes the
# place of the first occurrence.
#
# The reference is refs/remotes/origin/main by default, or the git revision
# given as the argument. When the reference has no row for a duplicated [ID],
# the duplicate is the author's own: the script says so, exits 1 and changes
# nothing. The file is written through a temporary file in its own directory
# and a rename. The check itself stays strict and fails on any duplicate.
#
# Usage: scripts/dedupe-runbook-index.sh [REVISION]
set -eu
CDPATH='' cd "$(dirname "$0")/.."
export LC_ALL=C
me=dedupe-runbook-index
index=docs/runbook/INDEX.md
ref=${1:-refs/remotes/origin/main}

unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_OBJECT_DIRECTORY \
	GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE GIT_PREFIX GIT_CONFIG \
	GIT_CONFIG_PARAMETERS GIT_CONFIG_COUNT

if [ -L docs/runbook ] || [ -L "$index" ]; then
	echo "$me: docs/runbook or $index is a symbolic link; refusing"
	exit 1
fi
[ -f "$index" ] || { echo "$me: $index is missing"; exit 1; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/dedupeindex.XXXXXX")
tmp=$(CDPATH='' cd "${tmp:?}" && pwd -P)
new=
trap 'rm -rf "${tmp:?}"; [ -z "$new" ] || rm -f "$new"' EXIT

sha=$(git rev-parse --verify -q --end-of-options "$ref^{commit}" 2> /dev/null) || sha=
case $sha in
*[!0-9a-f]* | '') sha= ;;
esac
if [ -z "$sha" ]; then
	echo "$me: $ref is not a revision"
	exit 1
fi
if ! git cat-file blob "$sha:$index" > "${tmp:?}/ref" 2> "${tmp:?}/err"; then
	echo "$me: cannot read $index at $ref:"
	cat "${tmp:?}/err"
	exit 1
fi

# The [ID] of each index row.
ids='s/^- \[([A-Za-z0-9_-]+)\]\(.*/\1/p'
sed -n -E "$ids" "$index" | sort | uniq -d > "${tmp:?}/dups"
if [ ! -s "${tmp:?}/dups" ]; then
	echo "$me: no duplicated row in $index"
	exit 0
fi

# The same parse as the awk below: only a row that starts a line counts.
sed -n -E "$ids" "${tmp:?}/ref" | sort > "${tmp:?}/refids"
uniq -d "${tmp:?}/refids" > "${tmp:?}/refdups"
bad=0
while read -r id; do
	if ! grep -Fqx -- "$id" "${tmp:?}/refids"; then
		echo "$me: [$id] is indexed twice and $ref has no row for it; fix it by hand"
		bad=1
	fi
done < "${tmp:?}/dups"
[ "$bad" -eq 0 ] || exit 1
if [ -s "${tmp:?}/refdups" ]; then
	echo "$me: $ref itself indexes twice: $(tr '\n' ' ' < "${tmp:?}/refdups")"
	exit 1
fi

new=$(mktemp "$(dirname "$index")/.INDEX.XXXXXX")
awk -v dups="${tmp:?}/dups" '
	BEGIN { while ((getline l < dups) > 0) dup[l] = 1 }
	FNR == NR {
		if (match($0, /^- \[[A-Za-z0-9_-]+\]\(/)) {
			id = substr($0, 4, RLENGTH - 5)
			if (id in dup) refrow[id] = $0
		}
		next
	}
	{
		if (match($0, /^- \[[A-Za-z0-9_-]+\]\(/)) {
			id = substr($0, 4, RLENGTH - 5)
			if (id in dup) {
				if (!(id in refrow)) {
					print "'"$me"': [" id "] has no row in the reference" > "/dev/stderr"
					exit 1
				}
				if (id in done) next
				done[id] = 1
				print refrow[id]
				next
			}
		}
		print
	}
' "${tmp:?}/ref" "$index" > "$new"
chmod 644 "$new"
mv "$new" "$index"
new=
echo "$me: kept $ref's row for: $(tr '\n' ' ' < "${tmp:?}/dups")"
