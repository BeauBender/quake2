#!/bin/sh
# Invariant: in a pin run nothing from the window system changes what is
# rendered by way of the input code (FIX48). The input gate
# (src/client/input/ingate.c) holds two kinds of guard, and this check
# keeps both in place by source inspection, outside comments and string
# literals:
#
# (a) No file in src/client/input/ but ingate.c calls Key_Event( or
#     Char_Event( directly: every key and character goes through
#     IN_KeyEvent and IN_CharEvent, which drop it in a pin run.
# (b) Each window-system event of sdl3.c that would print, pause or
#     restart the video (a gamepad removed, a joystick added, a battery
#     report, and on iOS a safe-area or pixel-size change) starts with
#     the gate that leaves it: the first statement of its case is
#     "if (!IN_InputOpen()" (alone, or followed by "|| ..."), and the
#     next one is "break;". A case that is not found fails the check.
#
# Scratch mutants must fail: a direct Key_Event( or Char_Event( call
# added to sdl3.c, and for each case of (b) its gate inverted, its gate
# removed, and its label renamed. Needs only sh, awk and sed.
set -eu
cd "$(dirname "$0")/.."

fail=0
bad() { echo "check-input-gate: $*"; fail=1; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/input-gate.XXXXXX")
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

# direct <file>: the lines of <file> that call Key_Event( or Char_Event(
direct() {
	strip "$1" | grep -nE '(^|[^A-Za-z0-9_])(Key_Event|Char_Event)[[:space:]]*\(' || true
}

# gated <file> <label>: "ok" when the case <label> of <file> starts with
# the gate that leaves it, as (b) says; otherwise why not
gated() {
	strip "$1" | awk -v l="$2" '
		function t(s) { gsub(/^[ \t]+|[ \t]+$/, "", s); gsub(/[ \t]+/, " ", s); return s }
		!st && t($0) ~ "^case " l " ?:$" { st = 1; next }
		!st { next }
		{ s = t($0) }
		s == "" || s == "{" || s == "}" || s ~ /^#/ || s ~ /^case [A-Z0-9_]+ ?:$/ { next }
		st == 1 {
			if (s !~ /^if \(!IN_InputOpen\(\)( \|\| .*)?\)$/) { print "its first statement is not the gate: " s; done = 1; exit }
			st = 2; next
		}
		st == 2 {
			if (s != "break;") { print "its gate does not leave the case: " s; done = 1; exit }
			print "ok"; done = 1; exit
		}
		END { if (!done) print (st ? "its gate is not followed by break;" : "the case is not found") }'
}

# mutant <kind> <label> <file>: <file> with the gate of the case <label>
# broken (inverted or removed) or the label renamed
mutant() {
	awk -v k="$1" -v l="$2" '
		!m && $0 ~ "case " l "[ \t]*:" { if (k == "renamed") sub(l, l "_OLD"); in_case = 1 }
		in_case && !m && index($0, "!IN_InputOpen()") {
			m = 1
			if (k == "inverted") sub(/!IN_InputOpen\(\)/, "IN_InputOpen()")
			else if (k == "removed") sub(/if \(!IN_InputOpen\(\)/, "if (0")
		}
		{ print }' "$3"
}

sdl=src/client/input/sdl3.c
labels="SDL_EVENT_GAMEPAD_REMOVED SDL_EVENT_JOYSTICK_ADDED SDL_EVENT_JOYSTICK_BATTERY_UPDATED SDL_EVENT_WINDOW_SAFE_AREA_CHANGED SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED"

# (a)
for f in src/client/input/*.c; do
	[ "$f" != src/client/input/ingate.c ] || continue
	d=$(direct "$f")
	[ -z "$d" ] || bad "$f calls Key_Event or Char_Event directly, not through IN_KeyEvent or IN_CharEvent (ingate.c): $(printf '%s\n' "$d" | tr -s ' \t' ' ' | head -n 3 | tr '\n' ';')"
done

# (b)
for l in $labels; do
	why=$(gated "$sdl" "$l")
	[ "$why" = ok ] || bad "$sdl: case $l: $why (in a pin run it must leave before it prints, pauses or restarts the video)"
done

# the mutants
mutants=0
for call in 'Key_Event(K_ENTER, true, false);' 'Key_Event (K_ENTER, true, false);' "Char_Event('a');"; do
	{ cat "$sdl"; printf '\nstatic void\nmutant(void)\n{\n\t%s\n}\n' "$call"; } > "${tmp:?}/direct.c"
	if [ -z "$(direct "${tmp:?}/direct.c")" ]; then bad "the check passes a direct call '$call' added to $sdl"; else mutants=$((mutants + 1)); fi
done
for l in $labels; do
	for k in inverted removed renamed; do
		mutant "$k" "$l" "$sdl" > "${tmp:?}/mutant.c"
		if cmp -s "$sdl" "${tmp:?}/mutant.c"; then
			bad "the $k mutant of case $l changed nothing"
		elif [ "$(gated "${tmp:?}/mutant.c" "$l")" = ok ]; then
			bad "the check passes the $k mutant of case $l"
		else
			mutants=$((mutants + 1))
		fi
	done
done

if [ "$fail" = 0 ]; then
	echo "check-input-gate: ok (no direct Key_Event or Char_Event call in src/client/input; $(echo $labels | wc -w | tr -d ' ') cases of $sdl gated; $mutants mutants refused)"
fi
exit $fail
