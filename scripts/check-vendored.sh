#!/bin/sh
# Verify the vendored SDL3 tree against its committed manifest.
#
# third_party/SDL3/MANIFEST.sha256 lists the sha256 of every file copied
# from the upstream release tarball (see docs/DEPENDENCIES.md and
# third_party/SDL3/README.md). This fails if a listed file is missing or
# differs by one byte, or if a file appears under third_party/SDL3/ that
# the manifest does not list. Offline: only shasum, find, sort, comm.
#
# Usage: scripts/check-vendored.sh [repo-root]   (default: this checkout)
set -eu
export LC_ALL=C

root=${1:-"$(dirname "$0")/.."}
cd "$root"
dir=third_party/SDL3
manifest=$dir/MANIFEST.sha256

if [ ! -f "$manifest" ]; then
	echo "check-vendored: $manifest is missing"
	exit 1
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/vendored.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT

fail=0
cd "$dir"

# 1. Every listed file exists and matches its hash.
grep -v '^#' MANIFEST.sha256 > "$tmp/sums"
if [ ! -s "$tmp/sums" ]; then
	echo "check-vendored: $manifest lists no files"
	exit 1
fi
if ! shasum -a 256 -c --quiet "$tmp/sums" > "$tmp/out" 2>&1; then
	echo "check-vendored: files in $dir differ from $manifest:"
	sed 's/^/  /' "$tmp/out"
	fail=1
fi

# 2. No file under the tree is unlisted. MANIFEST.sha256 and README.md are
# the fork's own files, not upstream's.
sed 's/^[0-9a-f]*  //' "$tmp/sums" | sort > "$tmp/listed"
find . -type f ! -path ./MANIFEST.sha256 ! -path ./README.md | sed 's|^\./||' | sort > "$tmp/present"
extra=$(comm -13 "$tmp/listed" "$tmp/present")
if [ -n "$extra" ]; then
	echo "check-vendored: files in $dir not listed in $manifest:"
	echo "$extra" | sed 's/^/  /'
	fail=1
fi

[ "$fail" -eq 0 ] && echo "check-vendored: ok ($(wc -l < "$tmp/listed" | tr -d ' ') files in $dir)"
exit "$fail"
