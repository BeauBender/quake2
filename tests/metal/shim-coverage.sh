#!/bin/sh
# The coverage report of the Metal shim (TASK21): every GL entry point
# ref_gl1 uses is exported by the shim, and the coverage table in
# docs/plans/metal-shim-design.md lists exactly those entry points.
#
# Usage: tests/metal/shim-coverage.sh [<objdir> <dylib>]
#
# Always (text only, sh, grep, sed, sort, comm):
#   D  the direct entry points: every gl<Upper>...( token in
#      src/client/refresh/gl1/*.c (54 on TASK21's tree);
#   P  the names gl1 asks RI_GetProcAddress for, and through the
#      shim's proc table (mgl_procs in metal/mgl_frame.c) the entry
#      points they resolve to (5);
#   every name of P is in the proc table, and the rows of the table
#   between the coverage-table markers of the design document name
#   exactly D and the entry points of P, each with a known status.
# With <objdir> (the build's ref_gl1_metal object directory) and
# <dylib> (the ref_gl1.dylib linked from it), decided from the objects:
#   every _gl symbol gl1's objects import is in D (the grep misses
#   nothing), every entry point of D and P is defined by the shim's
#   objects, and the dylib has no undefined _gl symbol.
# Any tool error, a missing tool or an empty set fails. Prints the counts on success.
set -eu
cd "$(dirname "$0")/../.."
export LC_ALL=C

gl1=src/client/refresh/gl1
doc=docs/plans/metal-shim-design.md
procsrc=$gl1/metal/mgl_frame.c

tmp=$(mktemp -d "${TMPDIR:-/tmp}/shim-coverage.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT

fail=0
bad() { echo "shim-coverage: $*"; fail=1; }
die() { echo "shim-coverage: $*"; exit 1; }

[ $# -eq 0 ] || [ $# -eq 2 ] || die "usage: $0 [<objdir> <dylib>]"
# kept before "set --" below reuses the positional parameters
objdir=${1:-}
dylib=${2:-}
[ $# -eq 0 ] || [ -n "$objdir" ] || die "empty <objdir>"

# D: direct calls in gl1's own C files (not the shim's)
set -- "$gl1"/*.c
[ -f "$1" ] || die "no C files under $gl1"
# a token is a call when no identifier character precedes it (not qglFoo)
awk '{
	s = $0; pre = ""
	while (match(s, /gl[A-Z][A-Za-z0-9]*[ \t]*\(/)) {
		before = pre substr(s, 1, RSTART - 1)
		name = substr(s, RSTART, RLENGTH)
		if (before !~ /[A-Za-z0-9_]$/) {
			sub(/[ \t]*\($/, "", name)
			print name
		}
		pre = before substr(s, RSTART, RLENGTH)
		s = substr(s, RSTART + RLENGTH)
	}
}' "$@" | sort -u > "$tmp/direct"
[ -s "$tmp/direct" ] || die "found no direct GL call under $gl1"

# The proc table: "name target" per entry
[ -f "$procsrc" ] || die "missing $procsrc"
sed -n '/^} mgl_procs\[\] = {$/,/^};$/p' "$procsrc" > "$tmp/table.src"
[ -s "$tmp/table.src" ] || die "no mgl_procs table in $procsrc"
{
	grep -oE 'MGL_PROC\(gl[A-Za-z0-9]+\)' "$tmp/table.src" | sed -E 's/^MGL_PROC\((.*)\)$/\1 \1/' || true
	grep -oE '\{ "gl[A-Za-z0-9]+", \(void \*\)gl[A-Za-z0-9]+ \}' "$tmp/table.src" |
		sed -E 's/^\{ "([^"]+)", \(void \*\)([^ ]+) \}$/\1 \2/' || true
} | sort -u > "$tmp/table"
[ -s "$tmp/table" ] || die "could not read the mgl_procs table in $procsrc"

# P: the names gl1 asks for by pointer, resolved through the table
for f in "$gl1"/*.c; do
	grep -ohE 'RI_GetProcAddress[[:space:]]*\([[:space:]]*"gl[A-Za-z0-9]+"' "$f" || true
done | sed -E 's/.*"([^"]+)"$/\1/' | sort -u > "$tmp/ptrnames"
[ -s "$tmp/ptrnames" ] || die "found no RI_GetProcAddress name under $gl1"
: > "$tmp/pointer"
while read -r name; do
	target=$(awk -v n="$name" '$1 == n { print $2 }' "$tmp/table")
	if [ -z "$target" ]; then
		bad "gl1 loads $name by pointer; the shim's proc table has no entry for it"
	else
		echo "$target" >> "$tmp/pointer"
	fi
done < "$tmp/ptrnames"
sort -u -o "$tmp/pointer" "$tmp/pointer"
comm -12 "$tmp/direct" "$tmp/pointer" > "$tmp/both"
[ ! -s "$tmp/both" ] || bad "called directly and by pointer: $(tr '\n' ' ' < "$tmp/both")"
sort -u "$tmp/direct" "$tmp/pointer" > "$tmp/used"

# The coverage table of the design document
[ -f "$doc" ] || die "missing $doc"
sed -n '/^<!-- coverage-table:start -->$/,/^<!-- coverage-table:end -->$/p' "$doc" |
	grep -E '^\| `gl' > "$tmp/rows" || true
[ -s "$tmp/rows" ] || die "no coverage table between the coverage-table markers in $doc"
sed -E 's/^\| `([^`]+)`.*/\1/' "$tmp/rows" | sort > "$tmp/docnames"
uniq -d "$tmp/docnames" > "$tmp/dups" || die "uniq failed on the coverage table names"
while read -r n; do echo "shim-coverage: $doc lists $n twice"; done < "$tmp/dups"
[ ! -s "$tmp/dups" ] || fail=1
sort -u -o "$tmp/docnames" "$tmp/docnames"
comm -23 "$tmp/used" "$tmp/docnames" > "$tmp/nodoc" || die "comm failed on the used names"
while read -r n; do
	echo "shim-coverage: gl1 uses $n; the coverage table in $doc has no row for it"
done < "$tmp/nodoc"
comm -13 "$tmp/used" "$tmp/docnames" > "$tmp/unused" || die "comm failed on the table names"
while read -r n; do
	echo "shim-coverage: the coverage table in $doc lists $n, which gl1 does not use"
done < "$tmp/unused"
cmp -s "$tmp/used" "$tmp/docnames" || fail=1
# The second column is the status
while IFS= read -r row; do
	status=$(printf '%s\n' "$row" | awk -F'|' '{ gsub(/^ +| +$/, "", $3); print $3 }')
	case $status in
	implemented|partial|no-op|stub) ;;
	*) bad "row of $(printf '%s\n' "$row" | sed -E 's/^\| `([^`]+)`.*/\1/') has status '$status' (implemented, partial, no-op or stub)" ;;
	esac
done < "$tmp/rows"

ndirect=$(wc -l < "$tmp/direct" | tr -d ' ') || die "wc failed"
npointer=$(wc -l < "$tmp/pointer" | tr -d ' ') || die "wc failed"
[ -n "$ndirect" ] && [ -n "$npointer" ] || die "could not count the entry points"
summary="$ndirect direct, $npointer by pointer, table matches"

# The objects, when given
if [ -n "$objdir" ]; then
	command -v nm >/dev/null 2>&1 || die "no nm"
	[ -f "$dylib" ] || die "no $dylib"
	set -- "$objdir/$gl1"/*.o
	[ -f "$1" ] || die "no gl1 objects under $objdir/$gl1"
	nm -u "$@" > "$tmp/nm.u" || die "nm -u failed on the gl1 objects"
	grep -E '^_gl[A-Z]' "$tmp/nm.u" | sed 's/^_//' | sort -u > "$tmp/imports" || true
	[ -s "$tmp/imports" ] || die "the gl1 objects import no GL symbol"
	comm -23 "$tmp/imports" "$tmp/direct" > "$tmp/missed" || die "comm failed on the imports"
	while read -r n; do
		echo "shim-coverage: the gl1 objects import $n, which the source scan missed"
	done < "$tmp/missed"
	[ ! -s "$tmp/missed" ] || fail=1

	set -- "$objdir/$gl1/metal"/*.o
	[ -f "$1" ] || die "no shim objects under $objdir/$gl1/metal"
	nm -g -U "$@" > "$tmp/nm.def" || die "nm -g -U failed on the shim objects"
	awk '$2 == "T" && $3 ~ /^_gl[A-Z]/ { sub(/^_/, "", $3); print $3 }' "$tmp/nm.def" | sort -u > "$tmp/exports"
	[ -s "$tmp/exports" ] || die "the shim objects define no GL symbol"
	comm -23 "$tmp/used" "$tmp/exports" > "$tmp/undefd" || die "comm failed on the exports"
	while read -r n; do
		echo "shim-coverage: gl1 uses $n; the shim does not define it"
	done < "$tmp/undefd"
	[ ! -s "$tmp/undefd" ] || fail=1

	nm -u "$dylib" > "$tmp/nm.dylib" || die "nm -u failed on $dylib"
	if grep -E '^_gl[A-Z]' "$tmp/nm.dylib" > "$tmp/undef"; then
		nundef=$(wc -l < "$tmp/undef" | tr -d ' ') || nundef="some"
		bad "$dylib leaves $nundef GL symbol(s) undefined"
	fi
	nimports=$(wc -l < "$tmp/imports" | tr -d ' ') || die "wc failed"
	nexports=$(wc -l < "$tmp/exports" | tr -d ' ') || die "wc failed"
	[ -n "$nimports" ] && [ -n "$nexports" ] || die "could not count the symbols"
	summary="$summary; objects import $nimports, shim defines $nexports, dylib has none undefined"
fi

[ "$fail" = 0 ] || exit 1
echo "shim-coverage: ok ($summary)"
