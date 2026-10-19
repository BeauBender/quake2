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

# func and afterwaitfunc are local function-pointer variables, not game
# functions; the fngen README tells the maintainer to drop them too.
grep -rhoE '(->|\.)(think|touch|use|pain|die|blocked|prethink|endfunc|stand|idle|search|walk|run|dodge|attack|melee|sight|checkattack)[[:space:]]*=[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*;' \
	src/game --include='*.c' |
	sed -E 's/.*=[[:space:]]*([A-Za-z_][A-Za-z0-9_]*).*/\1/' |
	grep -vxE 'NULL|func|afterwaitfunc' | sort -u > "$tmp/assigned"
grep -oE '"[A-Za-z_][A-Za-z0-9_]*"' "$tables/gamefunc_list.h" | tr -d '"' | sort -u > "$tmp/funcs"

grep -rhoE '^[[:space:]]*(static[[:space:]]+)?mmove_t[[:space:]]+[A-Za-z_][A-Za-z0-9_]*' \
	src/game --include='*.c' | awk '{print $NF}' | sort -u > "$tmp/mmoves"
grep -oE '"[A-Za-z_][A-Za-z0-9_]*"' "$tables/gamemmove_list.h" | tr -d '"' | sort -u > "$tmp/mmlisted"

fail=0
missing=$(comm -23 "$tmp/assigned" "$tmp/funcs")
if [ -n "$missing" ]; then
	echo "check-savegame-tables: functions assigned to savable fields but missing from $tables/gamefunc_list.h:"
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
