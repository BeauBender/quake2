#!/bin/sh
# Invariant: the renderers that can size their frame from the drawable
# decide it with the client's own rule, on the client's own input, so the
# rule cannot drift between them (FIX61).
#
# The client takes viddef from the drawable when GLimp_DrawableInPixels()
# (src/client/vid/glimp_hidpi.c) says so, in GLimp_InitGraphics(). The
# software renderer calls the same function in SWimp_DrawableInPixels()
# (sw_main.c), on the fullscreen value the window was made with (the
# vid_fullscreen cvar, which GLimp_InitGraphics() keeps at that value).
# By source inspection, outside comments and string literals:
#
# (a) soft's SWimp_DrawableInPixels() is found and returns
#     GLimp_DrawableInPixels(<args>); its <args> are the reference.
# (b) SetMode_impl() in src/client/refresh/metal/rm_main.c and in
#     src/client/refresh/gl1/gl1_main.c (both builds of ref_gl1) each
#     calls GLimp_DrawableInPixels() once, with the same <args> as soft,
#     as the whole condition of the one if that takes the drawable size
#     (RI_GetDrawableSize(pwidth, pheight), called nowhere else in the
#     function), and reads vid_fullscreen nowhere but in those <args>:
#     no copy of the rule, and no other input.
#
# Scratch mutants of each body must fail (b): the value passed instead
# of soft's input, the old copy of the rule, an unguarded or a second
# drawable call, another read of the cvar, the function renamed. A
# function that is not found fails the check. Needs only sh, awk and sed.
set -eu
cd "$(dirname "$0")/.."

fail=0
bad() { echo "check-mode-size: $*"; fail=1; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/mode-size.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT

# strip <file>: the file with comments and string and character literals
# blanked (the stripper of tests/check-ref-metal.sh)
strip() {
	awk '
	BEGIN { st = 0; q = sprintf("%c", 39); bs = sprintf("%c", 92) }
	{
		out = ""; n = length($0); i = 1
		while (i <= n) {
			c = substr($0, i, 1); d = substr($0, i, 2)
			if (st == 1) {
				if (d == "*/") { st = 0; out = out "  "; i += 2 } else { out = out " "; i++ }
				continue
			}
			if (st >= 2) {
				if (c == bs) { out = out "  "; i += 2; continue }
				if ((st == 2 && c == "\"") || (st == 3 && c == q)) { st = 0; out = out c } else { out = out " " }
				i++; continue
			}
			if (d == "/*") { st = 1; out = out "  "; i += 2; continue }
			if (d == "//") { break }
			if (c == "\"") { st = 2 } else if (c == q) { st = 3 }
			out = out c; i++
		}
		t = $0; sub(/[ \t\r]+$/, "", t)
		if (st >= 2 && substr(t, length(t), 1) != bs) { st = 0 }
		print out
	}' "$1"
}

# body <file> <function>: the function's body (its name at the start of a
# line, up to the next "}" in column 0), code only, on one line, blanks
# collapsed and none after "(" or before ")". Empty when not found.
body() {
	strip "$1" | awk -v fn="$2" '
		$0 ~ "^" fn "\\(" { b = 1; next }
		b && /^}/ { exit }
		b { printf "%s ", $0 }' |
		tr '\t' ' ' | sed -e 's/  */ /g' -e 's/( /(/g' -e 's/ )/)/g' -e 's/^ //' -e 's/^{ *//' -e 's/ $//'
}

# count <text> <literal>: how often the literal occurs in the text
count() {
	printf '%s\n' "$1" | awk -v s="$2" '{ n = 0; t = $0
		while ((i = index(t, s)) > 0) { n++; t = substr(t, i + length(s)) }
		print n }'
}

# decides <body> <args>: 0 when the body takes the drawable only under
# if (GLimp_DrawableInPixels(<args>)), as (b) says; 1 otherwise
decides() {
	[ -n "$1" ] || return 1
	case $1 in
		*"if (GLimp_DrawableInPixels($2)) { RI_GetDrawableSize(pwidth, pheight); }"*) ;;
		*) return 1 ;;
	esac
	[ "$(count "$1" 'GLimp_DrawableInPixels(')" = 1 ] || return 1
	[ "$(count "$1" 'RI_GetDrawableSize(')" = 1 ] || return 1
	[ "$(count "$1" vid_fullscreen)" = "$(count "$2" vid_fullscreen)" ] || return 1
}

# mutant <kind> <body> <args>: the body with its decision broken
mutant() {
	case $1 in
		passed) printf '%s\n' "$2" | sed 's/GLimp_DrawableInPixels([^)]*)*/GLimp_DrawableInPixels(IsHighDPIaware, fullscreen, r_mode->value)/' ;;
		own-rule) printf '%s\n' "$2" | sed 's/GLimp_DrawableInPixels([^)]*)*)/IsHighDPIaware \&\& ((vid_fullscreen->value != 2) || (r_mode->value == -2)))/' ;;
		unguarded) printf '%s\n' "$2" | sed 's/if (GLimp_DrawableInPixels(/if (1 || GLimp_DrawableInPixels(/' ;;
		second) printf '%s\n' "$2" | sed 's/return \([a-z_]*\); *$/RI_GetDrawableSize(pwidth, pheight); return \1;/' ;;
		cvar) printf '%s\n' "$2" | sed 's/^/fullscreen = (int)vid_fullscreen->value; /' ;;
	esac
}

sw=src/client/refresh/soft/sw_main.c
sw_body=$(body "$sw" SWimp_DrawableInPixels)
args=$(printf '%s\n' "$sw_body" | sed -n 's/^return GLimp_DrawableInPixels(\(.*\));$/\1/p')
if [ -z "$sw_body" ]; then
	bad "$sw: SWimp_DrawableInPixels not found"
elif [ -z "$args" ]; then
	bad "$sw: SWimp_DrawableInPixels does not return GLimp_DrawableInPixels(...) alone: $sw_body"
fi

mutants=0
if [ -n "$args" ]; then
	for f in src/client/refresh/metal/rm_main.c src/client/refresh/gl1/gl1_main.c; do
		b=$(body "$f" SetMode_impl)
		if [ -z "$b" ]; then
			bad "$f: SetMode_impl not found"
			continue
		fi
		if ! decides "$b" "$args"; then
			bad "$f: SetMode_impl does not take the drawable size only under if (GLimp_DrawableInPixels($args)), soft's call, with no other read of vid_fullscreen"
			continue
		fi
		for m in passed own-rule unguarded second cvar; do
			mb=$(mutant "$m" "$b" "$args")
			if [ "$mb" = "$b" ]; then
				bad "the $m mutant of $f changed nothing"
			elif decides "$mb" "$args"; then
				bad "the check passes the $m mutant of $f"
			else
				mutants=$((mutants + 1))
			fi
		done
		sed 's/^SetMode_impl(/SetMode_impl_old(/' "$f" > "${tmp:?}/renamed.c"
		if [ -n "$(body "${tmp:?}/renamed.c" SetMode_impl)" ]; then
			bad "the renamed mutant of $f still finds SetMode_impl"
		else
			mutants=$((mutants + 1))
		fi
	done
fi

if [ "$fail" = 0 ]; then
	echo "check-mode-size: ok (metal and gl1 decide with GLimp_DrawableInPixels($args), as soft; $mutants mutants refused)"
fi
exit $fail
