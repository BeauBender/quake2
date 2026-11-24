#!/bin/bash
# preset-to-table.sh <preset.cfg> [--as defaults|enhanced|retro|raytraced]   (TASK195, slice 2)
#
# Turns a ray-tracing preset saved by `rt_preset save <name>` (the file
# <writedir>/presets/rt-<name>.cfg) into what a pull request changes:
#
#   --as defaults (the default)   each tuning cvar whose value differs from today's default, as
#                                 "<cvar>: <old> -> <new>", each with the source line (file:line)
#                                 holding the default, from the rt_controls and rt_tune tables of
#                                 src/client/menu/vidrules.c (read, never hard-coded).
#   --as enhanced|retro|raytraced the C rows of that presentation's overlay, in the format of
#                                 pres_row_t in src/client/refresh/metal/rm_rtgate.c, as the macro
#                                 body RM_PRES_<MODE>_ROWS. The rows go to stdout, the notes to stderr.
#
# r_hdr and r_hdr_peak are a player's display setting: the --as overlays leave them out (the defaults
# output lists them).
#
# It reads only `set <name> "<value>"` lines (blank lines and // comments pass), refuses any other line
# with its number, and refuses a name that is not one of the 20 tuning cvars. It never edits a file.
# Exit 0 on success, 1 for a refusal, 2 for a usage or source error.

root=$(cd "$(dirname "$0")/../.." && pwd) || exit 2
src=${RTP_VIDRULES:-$root/src/client/menu/vidrules.c}
rel=${src#"$root"/}

usage() {
	echo "usage: $0 <preset.cfg> [--as defaults|enhanced|retro|raytraced]" >&2
	exit 2
}

preset=""
mode=defaults
while [ $# -gt 0 ]; do
	case $1 in
		--as) [ $# -ge 2 ] || usage; mode=$2; shift 2 ;;
		-*) usage ;;
		*) [ -z "$preset" ] || usage; preset=$1; shift ;;
	esac
done
[ -n "$preset" ] || usage
case $mode in defaults | enhanced | retro | raytraced) ;; *) usage ;; esac
[ -r "$preset" ] || { echo "preset-to-table: cannot read $preset" >&2; exit 1; }
[ -r "$src" ] || { echo "preset-to-table: cannot read $src" >&2; exit 2; }

# the 20 tuning cvars with their default and source line, as cvar<TAB>default<TAB>line, in the order
# of VidRules_PresetCvar: every row of rt_controls, then the rt_tune rows that are values
table=$(awk '
	/^static const vidrt_control_t rt_controls\[\]/ { in_t = 1; next }
	/^static const vidrt_control_t rt_tune\[\]/ { in_t = 2; next }
	in_t && /^};/ { in_t = 0; next }
	in_t && match($0, /^\t\{ "[A-Za-z0-9_]+", "[^"]*", "[^"]*"/) {
		line = substr($0, RSTART, RLENGTH)
		split(line, f, "\"")
		name = f[2]; def = f[6]
		if (in_t == 2 && (name ~ /_show$/ || name == "r_presentation")) next
		if (seen[name]++) next
		printf "%s\t%s\t%d\n", name, def, NR
	}' "$src") || exit 2
[ "$(printf '%s\n' "$table" | wc -l | tr -d ' ')" = 20 ] || {
	echo "preset-to-table: expected 20 tuning cvars in $rel, found something else" >&2
	exit 2
}

tab=$(printf '\t')
cr=$(printf '\r')
isnum() { printf '%s' "$1" | grep -Eq '^-?[0-9]+(\.[0-9]+)?$'; }
numeq() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a + 0 == b + 0) }'; }

# parse the preset into name<TAB>value lines, refusing as we go
parsed=""
ln=0
while IFS= read -r line || [ -n "$line" ]; do
	ln=$((ln + 1))
	line=${line%"$cr"}
	case $line in '' | //*) continue ;; esac
	if ! printf '%s\n' "$line" | grep -Eq '^set [A-Za-z0-9_]+ "[A-Za-z0-9._ -]*"$'; then
		echo "preset-to-table: $preset:$ln: refused: not a 'set <name> \"<value>\"' line: $line" >&2
		exit 1
	fi
	rest=${line#set }
	name=${rest%% *}
	value=${rest#* \"}
	value=${value%\"}
	if ! printf '%s\n' "$table" | cut -f1 | grep -Fxq "$name"; then
		echo "preset-to-table: $preset:$ln: refused: $name is not a tuning cvar" >&2
		exit 1
	fi
	parsed="$parsed$name$tab$value
"
done < "$preset"
[ -n "$parsed" ] || { echo "preset-to-table: $preset: refused: no set lines" >&2; exit 1; }

if [ "$mode" = defaults ]; then
	out=""
	while IFS="$tab" read -r name value; do
		[ -n "$name" ] || continue
		row=$(printf '%s\n' "$table" | grep "^$name$tab")
		def=$(printf '%s' "$row" | cut -f2)
		at=$(printf '%s' "$row" | cut -f3)
		if [ "$def" = "$value" ] || { isnum "$def" && isnum "$value" && numeq "$def" "$value"; }; then
			continue
		fi
		out="$out$name: $def -> $value
  change $rel:$at, the default \"$def\" to \"$value\"
"
	done <<< "$parsed"
	if [ -n "$out" ]; then
		printf '%s' "$out"
	else
		echo "no tuning cvar differs from today's default"
	fi
	exit 0
fi

# a presentation: rows of numbers (rm_pres_value returns a float, so r_rt_sun_angles is no row)
macro=RM_PRES_$(printf '%s' "$mode" | tr '[:lower:]' '[:upper:]')_ROWS
rows=""
while IFS="$tab" read -r name value; do
	[ -n "$name" ] || continue
	case $name in
		r_hdr | r_hdr_peak)
			echo "preset-to-table: note: $name is left out of the $mode overlay (it would force HDR for every user of the mode); the defaults output lists it" >&2
			continue ;;
	esac
	if ! isnum "$value"; then
		echo "preset-to-table: note: $name \"$value\" is not a single number, left out of the $mode overlay" >&2
		continue
	fi
	case $value in *.*) ;; *) value=$value.0 ;; esac
	rows="$rows	{ \"$name\", ${value}f },
"
done <<< "$parsed"
[ -n "$rows" ] || { echo "preset-to-table: no numeric row for $mode" >&2; exit 1; }

echo "preset-to-table: replace the rows of $macro in src/client/refresh/metal/rm_rtgate.c with these" >&2
[ "$mode" = enhanced ] || echo "preset-to-table: note: rm_rtgate.c has no overlay table for $mode yet; add one beside pres_enhanced" >&2
printf '#define %s \\\n' "$macro"
printf '%s' "$rows" | sed '$!s/$/ \\/'
