#!/bin/sh
# Print the savegame table entries missing for functions and mmove_t tables
# that the game code can store in a savegame. Exit 1 when any are missing.
#
# Usage: .claude/skills/savegame-tables/scripts/missing-entries.sh
set -eu
cd "$(git rev-parse --show-toplevel)"
export LC_ALL=C
tables=src/game/savegame/tables

# field -> list name in gamefunc_list.h
list_for() {
	case $1 in
	think|touch|use|pain|die|blocked|prethink) echo "fnentries_$1" ;;
	endfunc) echo "fnentries_mv_end" ;;
	*) echo "fnentries_mi_$1" ;;
	esac
}

status=0
out="${TMPDIR:-/tmp}/sg-missing.$$"
pairs="${TMPDIR:-/tmp}/sg-pairs.$$"
trap 'rm -f "$out" "$pairs"' EXIT

# (list, name) pairs present in gamefunc_list.h; the loader searches only
# the list that belongs to the field being saved.
awk '/fnentries_[a-z_]+\[\][[:space:]]*=/ { match($0, /fnentries_[a-z_]+/); l = substr($0, RSTART, RLENGTH) }
	/^};/ { l = "" }
	l != "" && /\{"/ { match($0, /"[^"]+"/); print l, substr($0, RSTART + 1, RLENGTH - 2) }' \
	"$tables/gamefunc_list.h" > "$pairs"

grep -rnoE '(->|\.)(think|touch|use|pain|die|blocked|prethink|endfunc|stand|idle|search|walk|run|dodge|attack|melee|sight|checkattack)[[:space:]]*=[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*;' \
	src/game --include='*.c' |
	sed -E 's/^([^:]+:[0-9]+):(->|\.)([a-z_]+)[[:space:]]*=[[:space:]]*([A-Za-z_][A-Za-z0-9_]*).*/\1 \3 \4/' |
	sort -u -k2,3 |
	while read -r where field name; do
		case $name in NULL|func|afterwaitfunc) continue ;; esac
		grep -qxF "$(list_for "$field") $name" "$pairs" && continue
		echo "$name (assigned to $field at $where)"
		echo "  $tables/gamefunc_decs.h:  extern <return type> $name (<parameters>);"
		echo "  $tables/gamefunc_list.h, list $(list_for "$field"):  {\"$name\", (byte *)$name},"
		echo missing
	done > "$out"

grep -rhoE '^[[:space:]]*(static[[:space:]]+)?mmove_t[[:space:]]+[A-Za-z_][A-Za-z0-9_]*' src/game --include='*.c' |
	awk '{print $NF}' | sort -u |
	while read -r name; do
		grep -qF "\"$name\"" "$tables/gamemmove_list.h" && continue
		echo "$name (mmove_t)"
		echo "  $tables/gamemmove_decs.h:  extern mmove_t $name ;"
		echo "  $tables/gamemmove_list.h:  {\"$name\", &$name},"
		echo missing
	done >> "$out"

if grep -qx missing "$out"; then
	grep -vx missing "$out"
	status=1
fi
exit "$status"
