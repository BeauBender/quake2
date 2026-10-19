#!/bin/sh
# Invariant: every game function stored in a savable entity field, and every
# mmove_t monster move table, is listed in the savegame tables.
#
# Savegames store these pointers by name (src/game/savegame/savegame.c). A
# function assigned to think/touch/use/pain/die/blocked/endfunc or a
# monsterinfo callback that is missing from gamefunc_list.h compiles and
# runs, but saving a game that uses it fails. Regenerate the tables with
# stuff/fngen and stuff/mmgen (see their READMEs, or the
# savegame-tables skill under .claude/skills/).
#
# Usage: tests/check-savegame-tables.sh    (exit 1 lists the missing names)
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C

tables=src/game/savegame/tables
tmp=$(mktemp -d "${TMPDIR:-/tmp}/sgtables.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

# Pairs of (list, name). The loader looks a pointer up only in the list for
# its field (think -> fnentries_think, monsterinfo.stand ->
# fnentries_mi_stand, moveinfo.endfunc -> fnentries_mv_end), so a name
# listed under another field does not count.
# func and afterwaitfunc are local function-pointer variables, not game
# functions; the fngen README tells the maintainer to drop them too.
grep -rhoE '(->|\.)(think|touch|use|pain|die|blocked|prethink|endfunc|stand|idle|search|walk|run|dodge|attack|melee|sight|checkattack)[[:space:]]*=[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*;' \
	src/game --include='*.c' |
	sed -E 's/^(->|\.)([a-z_]+)[[:space:]]*=[[:space:]]*([A-Za-z_][A-Za-z0-9_]*).*/\2 \3/' |
	awk '$2 != "NULL" && $2 != "func" && $2 != "afterwaitfunc" {
		f = $1
		if (f ~ /^(think|touch|use|pain|die|blocked|prethink)$/) l = f
		else if (f == "endfunc") l = "mv_end"
		else l = "mi_" f
		print "fnentries_" l, $2
	}' | sort -u > "$tmp/assigned"
awk '/fnentries_[a-z_]+\[\][[:space:]]*=/ { match($0, /fnentries_[a-z_]+/); l = substr($0, RSTART, RLENGTH) }
	/^};/ { l = "" }
	l != "" && /\{"/ { match($0, /"[^"]+"/); print l, substr($0, RSTART + 1, RLENGTH - 2) }' \
	"$tables/gamefunc_list.h" | sort -u > "$tmp/funcs"

grep -rhoE '^[[:space:]]*(static[[:space:]]+)?mmove_t[[:space:]]+[A-Za-z_][A-Za-z0-9_]*' \
	src/game --include='*.c' | awk '{print $NF}' | sort -u > "$tmp/mmoves"
grep -oE '"[A-Za-z_][A-Za-z0-9_]*"' "$tables/gamemmove_list.h" | tr -d '"' | sort -u > "$tmp/mmlisted"

fail=0
missing=$(comm -23 "$tmp/assigned" "$tmp/funcs")
if [ -n "$missing" ]; then
	echo "check-savegame-tables: (list, function) pairs assigned in src/game but missing from $tables/gamefunc_list.h:"
	echo "$missing" | sed 's/^/  /'
	fail=1
fi
missing=$(comm -23 "$tmp/mmoves" "$tmp/mmlisted")
if [ -n "$missing" ]; then
	echo "check-savegame-tables: mmove_t tables missing from $tables/gamemmove_list.h:"
	echo "$missing" | sed 's/^/  /'
	fail=1
fi
[ "$fail" -eq 0 ] && echo "check-savegame-tables: ok ($(wc -l < "$tmp/assigned" | tr -d ' ') functions, $(wc -l < "$tmp/mmoves" | tr -d ' ') mmoves)"
exit "$fail"
