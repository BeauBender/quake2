#!/bin/sh
# Invariant: in a pin run nothing from the window system changes what is
# rendered by way of the input code (FIX48). The input gate
# (src/client/input/ingate.c) holds guards of several kinds, and this
# check keeps them all in place by source inspection, outside comments
# and string literals:
#
# (a) No file in src/client/input/ but ingate.c names Key_Event or
#     Char_Event, called or not (a macro or pointer alias would get past
#     the gate): every key and character goes through IN_KeyEvent and
#     IN_CharEvent, which drop it in a pin run.
# (b) Each window-system event of sdl3.c that would print or restart the
#     video (a gamepad removed, a joystick added, a battery report, and
#     on iOS a safe-area or pixel-size change) starts with the gate that
#     leaves it: the first statement of its case is "if (!IN_InputOpen()"
#     (alone, or followed by "|| ..."), and the next one is "break;".
# (c) Each write of "paused" in the cases SDL_EVENT_WINDOW_FOCUS_LOST,
#     SDL_EVENT_WINDOW_FOCUS_GAINED and SDL_EVENT_WINDOW_SHOWN of sdl3.c
#     is the body of "if (IN_InputOpen() && <cvar>->value <op> <digit>)".
# (d) IN_Move starts (after its declarations) with "if (!IN_InputOpen())"
#     whose body returns.
# A case that is not found fails the check, and so does a gate that is
# not exactly so: a preprocessor line inside a case (before its break;),
# a "?", a comma or an "&&" in a leaving gate (an always-false tail).
#
# Scratch mutants must fail: a direct Key_Event( or Char_Event( call
# added to sdl3.c, a macro alias of each, and for each case of (b), (c)
# and (d) its gate inverted, its gate removed, its label renamed and a
# preprocessor line put in the case; for (b) also an always-false tail.
# Needs only sh, awk and sed.
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

# direct <file>: the lines of <file> that name Key_Event or Char_Event,
# called or not (an alias by macro or pointer gets past the gate too)
direct() {
	strip "$1" | grep -nE '(^|[^A-Za-z0-9_])(Key_Event|Char_Event)([^A-Za-z0-9_]|$)' || true
}

# gated <file> <label>: "ok" when the case <label> of <file> starts with
# the gate that leaves it, as (b) says; otherwise why not
gated() {
	strip "$1" | awk -v l="$2" '
		function t(s) { gsub(/^[ \t]+|[ \t]+$/, "", s); gsub(/[ \t]+/, " ", s); return s }
		!st && t($0) ~ "^case " l " ?:$" { st = 1; next }
		!st { next }
		{ s = t($0) }
		s ~ /^#/ { print "a preprocessor line is inside the case: " s; done = 1; exit }
		s == "" || s == "{" || s == "}" || s ~ /^case [A-Z0-9_]+ ?:$/ { next }
		st == 1 {
			if (s !~ /^if \(!IN_InputOpen\(\)( \|\| .*)?\)$/ || s ~ /[?,]|&&/) { print "its first statement is not the gate: " s; done = 1; exit }
			st = 2; next
		}
		st == 2 {
			if (s != "break;") { print "its gate does not leave the case: " s; done = 1; exit }
			print "ok"; done = 1; exit
		}
		END { if (!done) print (st ? "its gate is not followed by break;" : "the case is not found") }'
}

# pausegated <file> <label>: "ok" when every write of "paused" in the case
# <label> is the body of "if (IN_InputOpen() && <cvar>->value <op> <digit>)",
# as (c) says; otherwise why not
pausegated() {
	strip "$1" > "${tmp:?}/stripped.c"
	awk -v l="$2" -v orig="$1" '
		function t(s) { gsub(/^[ \t]+|[ \t]+$/, "", s); gsub(/[ \t]+/, " ", s); return s }
		BEGIN { while ((getline line < orig) > 0) o[++n] = line }
		!st && t($0) ~ "^case " l " ?:$" { st = 1; next }
		!st { next }
		{ s = t($0) }
		s == "break;" { done = 1; print (found ? "ok" : "the case never writes paused"); exit }
		s ~ /^#/ { print "a preprocessor line is inside the case: " s; done = 1; exit }
		s == "" || s == "{" || s == "}" { next }
		s ~ /Cvar_(Set|SetValue|ForceSet) ?\(/ && o[NR] ~ /"paused"/ {
			if (prev !~ /^if \(IN_InputOpen\(\) && [A-Za-z_][A-Za-z0-9_]*->value (==|!=|<=|>=|<|>) [0-9]\)$/) { print "a write of paused is not gated by IN_InputOpen() &&: " prev; done = 1; exit }
			found = 1
		}
		{ prev = s }
		END { if (!done) print (st ? "the case has no break;" : "the case is not found") }' "${tmp:?}/stripped.c"
}

# movegated <file>: "ok" when IN_Move starts with the gate that returns,
# as (d) says; otherwise why not
movegated() {
	strip "$1" | awk '
		function t(s) { gsub(/^[ \t]+|[ \t]+$/, "", s); gsub(/[ \t]+/, " ", s); return s }
		!st && t($0) ~ /^IN_Move\(usercmd_t \*cmd\)$/ { st = 1; next }
		!st { next }
		{ s = t($0) }
		s ~ /^#/ { print "a preprocessor line is inside IN_Move: " s; done = 1; exit }
		s == "" || s == "{" { next }
		st == 1 && s ~ /^(static )?[A-Za-z_][A-Za-z0-9_]* [A-Za-z_][A-Za-z0-9_]*( = [^;]*)?;$/ { next }
		st == 1 {
			if (s != "if (!IN_InputOpen())") { print "its first statement is not the gate: " s; done = 1; exit }
			st = 2; next
		}
		st == 2 {
			if (s == "return;") { print "ok"; done = 1; exit }
			if (s == "}") { print "its gate does not return"; done = 1; exit }
		}
		END { if (!done) print (st ? "its gate does not return" : "IN_Move is not found") }'
}

# mutant <kind> <label> <file>: <file> with the first IN_InputOpen() after
# the case <label> (or the function <label>) inverted, removed or given an
# always-false tail, the label renamed, or a preprocessor line put after it
mutant() {
	awk -v k="$1" -v l="$2" '
		!in_case && ($0 ~ "case " l "[ \t]*:" || $0 ~ "^" l "\\(") {
			in_case = 1
			if (k == "renamed") sub(l, l "_OLD")
			print
			if (k == "pp") print "#if 0"
			next
		}
		in_case && !m && index($0, "IN_InputOpen()") {
			m = 1
			if (k == "inverted") { if (index($0, "!IN_InputOpen()")) sub(/!IN_InputOpen\(\)/, "IN_InputOpen()"); else sub(/IN_InputOpen\(\)/, "!IN_InputOpen()") }
			else if (k == "removed") { if (index($0, "!IN_InputOpen()")) sub(/if \(!IN_InputOpen\(\)/, "if (0"); else sub(/IN_InputOpen\(\) && /, "") }
			else if (k == "tail") sub(/!IN_InputOpen\(\)/, "!IN_InputOpen() || 0 ? 0 : 0")
		}
		{ print }' "$3"
}

sdl=src/client/input/sdl3.c
labels="SDL_EVENT_GAMEPAD_REMOVED SDL_EVENT_JOYSTICK_ADDED SDL_EVENT_JOYSTICK_BATTERY_UPDATED SDL_EVENT_WINDOW_SAFE_AREA_CHANGED SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED"
plabels="SDL_EVENT_WINDOW_FOCUS_LOST SDL_EVENT_WINDOW_FOCUS_GAINED SDL_EVENT_WINDOW_SHOWN"

# (a)
for f in src/client/input/*.c src/client/input/header/*.h; do
	[ "$f" != src/client/input/ingate.c ] || continue
	d=$(direct "$f")
	[ -z "$d" ] || bad "$f names Key_Event or Char_Event, not through IN_KeyEvent or IN_CharEvent (ingate.c): $(printf '%s\n' "$d" | tr -s ' \t' ' ' | head -n 3 | tr '\n' ';')"
done

# (b), (c), (d)
for l in $labels; do
	why=$(gated "$sdl" "$l")
	[ "$why" = ok ] || bad "$sdl: case $l: $why (in a pin run it must leave before it prints or restarts the video)"
done
for l in $plabels; do
	why=$(pausegated "$sdl" "$l")
	[ "$why" = ok ] || bad "$sdl: case $l: $why (in a pin run a focus or visibility change must not pause or unpause)"
done
why=$(movegated "$sdl")
[ "$why" = ok ] || bad "$sdl: IN_Move: $why (in a pin run no movement may reach the view)"

# the mutants
mutants=0
for call in 'Key_Event(K_ENTER, true, false);' 'Key_Event (K_ENTER, true, false);' "Char_Event('a');"; do
	{ cat "$sdl"; printf '\nstatic void\nmutant(void)\n{\n\t%s\n}\n' "$call"; } > "${tmp:?}/direct.c"
	if [ -z "$(direct "${tmp:?}/direct.c")" ]; then bad "the check passes a direct call '$call' added to $sdl"; else mutants=$((mutants + 1)); fi
done
for name in Key_Event Char_Event; do
	{ cat "$sdl"; printf '\n#define KE %s\nstatic void\nmutant(void)\n{\n\tKE(0, 0, 0);\n}\n' "$name"; } > "${tmp:?}/direct.c"
	if [ -z "$(direct "${tmp:?}/direct.c")" ]; then bad "the check passes a macro alias of $name added to $sdl"; else mutants=$((mutants + 1)); fi
done
# check <fn> <label> <kinds...>: each mutant of the gate must be refused
check() {
	fn=$1; l=$2; shift 2
	for k in "$@"; do
		mutant "$k" "$l" "$sdl" > "${tmp:?}/mutant.c"
		if cmp -s "$sdl" "${tmp:?}/mutant.c"; then
			bad "the $k mutant of $l changed nothing"
		elif [ "$($fn "${tmp:?}/mutant.c" "$l")" = ok ]; then
			bad "the check passes the $k mutant of $l"
		else
			mutants=$((mutants + 1))
		fi
	done
}
for l in $labels; do check gated "$l" inverted removed renamed pp tail; done
for l in $plabels; do check pausegated "$l" inverted removed renamed pp; done
check movegated IN_Move inverted removed renamed pp

if [ "$fail" = 0 ]; then
	echo "check-input-gate: ok (no use of Key_Event or Char_Event in src/client/input but ingate.c; $(echo $labels | wc -w | tr -d ' ') leaving cases, $(echo $plabels | wc -w | tr -d ' ') pause cases and IN_Move of $sdl gated; $mutants mutants refused)"
fi
exit $fail
