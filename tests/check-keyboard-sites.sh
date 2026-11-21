#!/bin/sh
# Invariant (FIX162): the call sites in src/client/cl_keyboard.c that FIX156
# and the key-index fix made safe stay safe. The unit suite cannot link
# cl_keyboard.c (it needs the input, menu and video layers), so the sites are
# held here, as source rules over the real file. Each rule reads a
# comment-stripped, whitespace-collapsed copy, so a reformat passes and a
# revert fails:
#
#  1. no Cbuf_AddText( keybindings[ ... ) anywhere in the file, and Key_Event
#     queues bound text only through Key_AddBindingText (the four calls the
#     file has today are counted, four are required), with no
#     Cbuf_AddText on kb, cmd or keybindings;
#  2. Key_WriteBindings calls Key_WriteBindingLines(f) and writes no bind
#     line itself (no keybindings, no "bind literal, no Key_KeynumToString);
#  3. Key_Unbindall_f calls Key_UnbindAll();
#  4. Key_StringToKeynum's single-character return casts to unsigned char;
#  5. Key_Bind_f and Key_Unbind_f each refuse a key number below 0 or at
#     K_LAST and above.
#
# It then copies the file, reverts each site in the copy, and requires the
# rules to refuse every copy; the number of mutants is printed.
#
# Usage: tests/check-keyboard-sites.sh [file]
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C

src=${1:-src/client/cl_keyboard.c}

if [ ! -f "$src" ]; then
	echo "check-keyboard-sites: $src is missing"
	exit 1
fi

tmpd=$(mktemp -d "${TMPDIR:-/tmp}/kbsites.XXXXXX")
trap 'rm -rf "${tmpd:?}"' EXIT
trap 'exit 1' INT TERM

# flat FILE: the file with comments removed and every run of white space a
# single space, as one line
flat() {
	awk '
	{
		line = $0
		n = length(line)
		out = ""
		i = 1
		while (i <= n) {
			c = substr(line, i, 1)
			if (incom) {
				if (substr(line, i, 2) == "*/") { incom = 0; i += 2 } else { i++ }
			} else if (c == "\"" || c == "\047") {
				# a string or character literal: kept, and no comment opens in it
				q = c; out = out c; i++
				while (i <= n) {
					d = substr(line, i, 1)
					out = out d
					if (d == "\\") { out = out substr(line, i + 1, 1); i += 2; continue }
					i++
					if (d == q) break
				}
			} else if (substr(line, i, 2) == "//") {
				# a line comment: the rest of the line goes, before any /* in it
				i = n + 1
			} else if (substr(line, i, 2) == "/*") {
				out = out " "; incom = 1; i += 2
			} else {
				out = out c; i++
			}
		}
		print out
	}' "$1" | tr '\n\t' '  ' | sed 's/  */ /g'
}

# body FILE NAME: the flattened body of the function NAME, which starts at a
# line "NAME(" in column 0 and ends at the next line "}" in column 0
body() {
	awk -v n="$2" '
	index($0, n "(") == 1 { on = 1 }
	on { print }
	on && /^}/ { exit }' "$1" > "${tmpd:?}/body"
	flat "${tmpd:?}/body"
}

check() { # file: prints a line per broken rule, returns 1 if any
	f=$1
	bad=0
	whole=$(flat "$f")

	if printf '%s' "$whole" | grep -qE 'Cbuf_AddText ?\( ?keybindings ?\['; then
		echo "check-keyboard-sites: $f queues keybindings[] with Cbuf_AddText"
		bad=1
	fi

	b=$(body "$f" Key_Event)
	if [ -z "$b" ]; then
		echo "check-keyboard-sites: Key_Event is not found in $f"
		bad=1
	else
		n=$(printf '%s' "$b" | grep -oE 'Key_AddBindingText ?\(' | wc -l | tr -d ' ')
		if [ "$n" -lt 4 ]; then
			echo "check-keyboard-sites: Key_Event has $n Key_AddBindingText calls, wants 4 or more"
			bad=1
		fi
		k=$(printf '%s' "$b" | grep -oE 'Key_AddBindingText ?\( ?key ?,' | wc -l | tr -d ' ')
		if [ "$k" != "$n" ]; then
			echo "check-keyboard-sites: a Key_AddBindingText call in Key_Event does not take key first ($k of $n do)"
			bad=1
		fi
		if printf '%s' "$b" | grep -qE 'Cbuf_AddText ?\( ?(kb|cmd|keybindings)'; then
			echo "check-keyboard-sites: Key_Event queues bound text with Cbuf_AddText"
			bad=1
		fi
	fi

	b=$(body "$f" Key_WriteBindings)
	if ! printf '%s' "$b" | grep -qE 'Key_WriteBindingLines ?\( ?f ?\)'; then
		echo "check-keyboard-sites: Key_WriteBindings does not call Key_WriteBindingLines(f)"
		bad=1
	fi
	if printf '%s' "$b" | grep -qE 'keybindings|"bind|Key_KeynumToString'; then
		echo "check-keyboard-sites: Key_WriteBindings writes a bind line itself"
		bad=1
	fi

	b=$(body "$f" Key_Unbindall_f)
	if ! printf '%s' "$b" | grep -qE 'Key_UnbindAll ?\( ?\) ?;'; then
		echo "check-keyboard-sites: Key_Unbindall_f does not call Key_UnbindAll()"
		bad=1
	fi

	b=$(body "$f" Key_StringToKeynum)
	if ! printf '%s' "$b" | grep -qE 'if ?\( ?! ?str ?\[ ?1 ?\] ?\) ?\{ ?return ?\( ?unsigned char ?\) ?str ?\[ ?0 ?\] ?; ?\}'; then
		echo "check-keyboard-sites: Key_StringToKeynum's single-character return is not cast to unsigned char"
		bad=1
	fi

	for fn in Key_Bind_f Key_Unbind_f; do
		b=$(body "$f" "$fn")
		# the block is one Com_Printf statement, then an unconditional return;
		if ! printf '%s' "$b" | grep -qE 'if ?\( ?\(? ?b ?< ?0 ?\)? ?\|\| ?\(? ?b ?>= ?K_LAST ?\)? ?\) ?\{ ?Com_Printf ?\([^;{}]*\) ?; ?return ?; ?\}'; then
			echo "check-keyboard-sites: $fn does not refuse a key number below 0 or at K_LAST and above with one Com_Printf and an unconditional return"
			bad=1
		fi
	done

	return $bad
}

fail=0
check "$src" || fail=1

m=0
mutant() { # name, sed script: the copy must be refused
	m=$((m + 1))
	cp "$src" "${tmpd:?}/mut.c"
	sed -i.bak -e "$2" "${tmpd:?}/mut.c"
	if cmp -s "$src" "${tmpd:?}/mut.c"; then
		echo "check-keyboard-sites: mutant $1 did not change the file"
		fail=1
	elif check "${tmpd:?}/mut.c" >/dev/null; then
		echo "check-keyboard-sites: mutant $1 was NOT refused"
		fail=1
	fi
}

accept() { # name, sed script: an equivalent form, the copy must pass
	cp "$src" "${tmpd:?}/acc.c"
	sed -i.bak -e "$2" "${tmpd:?}/acc.c"
	if cmp -s "$src" "${tmpd:?}/acc.c"; then
		echo "check-keyboard-sites: form $1 did not change the file"
		fail=1
	elif ! check "${tmpd:?}/acc.c" >/dev/null; then
		echo "check-keyboard-sites: equivalent form $1 was refused"
		fail=1
	fi
}

# The self-check mutates the real file only: a file argument (a reflowed
# input, say) is checked against the rules alone.
if [ -z "${1:-}" ]; then
mutant press-plain-cbuf 's/Key_AddBindingText(key, kb);/Cbuf_AddText(keybindings[key]);/'
mutant press-button-cbuf '/"%s %i %i/{n;s/Key_AddBindingText(key, cmd);/Cbuf_AddText(cmd);/;}'
mutant release-cbuf '/"-%s %i %i/{n;s/Key_AddBindingText(key, cmd);/Cbuf_AddText(cmd);/;}'
mutant newline-cbuf 's/Key_AddBindingText(key, "\\n");/Cbuf_AddText("\\n");/'
mutant stray-keybindings-cbuf 's/^Key_WriteBindings(FILE \*f)$/Key_WriteBindingsX(void) { Cbuf_AddText(keybindings[0]); }\nvoid\nKey_WriteBindings(FILE *f)/'
mutant writebindings-no-helper 's/Key_WriteBindingLines(f);/fprintf(f, "x\\n");/'
mutant writebindings-inline 's/Key_WriteBindingLines(f);/fprintf(f, "bind %s %s\\n", Key_KeynumToString(0), keybindings[0]);/'
mutant unbindall-no-helper 's/^\tKey_UnbindAll();/\t;/'
mutant keynum-signed 's/return (unsigned char)str\[0\];/return str[0];/'
bounds='if *( *( *b *< *0 *) *|| *( *b *>= *K_LAST *) *)'
mutant bind-no-low-bound "/^Key_Bind_f/,/^}/s/$bounds/if (b >= K_LAST)/"
mutant bind-no-high-bound "/^Key_Bind_f/,/^}/s/$bounds/if (b < 0)/"
mutant unbind-no-low-bound "/^Key_Unbind_f/,/^}/s/$bounds/if (b >= K_LAST)/"
mutant unbind-no-high-bound "/^Key_Unbind_f/,/^}/s/$bounds/if (b < 0)/"
mutant bind-dead-return-0 "/^Key_Bind_f/,/^}/{/isn't a valid key/{n;s/return *;/if (0) return;/;};}"
mutant unbind-dead-return-0 "/^Key_Unbind_f/,/^}/{/isn't a valid key/{n;s/return *;/if (0) return;/;};}"
mutant bind-dead-return-b "/^Key_Bind_f/,/^}/{/isn't a valid key/{n;s/return *;/if (b == -5) return;/;};}"
mutant unbind-dead-return-b "/^Key_Unbind_f/,/^}/{/isn't a valid key/{n;s/return *;/if (b == -5) return;/;};}"
mutant hidden-by-line-comment 's/Key_AddBindingText(key, kb);/Key_AddBindingText(key, kb); \/\/ see \/*\
\tCbuf_AddText(keybindings[key]);/'
mutant bind-no-return "/^Key_Bind_f/,/^}/{/isn't a valid key/{n;s/return *;/;/;};}"
mutant unbind-no-return "/^Key_Unbind_f/,/^}/{/isn't a valid key/{n;s/return *;/;/;};}"
mutant press-key-zero 's/Key_AddBindingText(key, kb);/Key_AddBindingText(0, kb);/'
mutant release-key-zero '/"-%s %i %i/{n;s/Key_AddBindingText(key, cmd);/Key_AddBindingText(0, cmd);/;}'
mutant newline-key-zero 's/Key_AddBindingText(key, "\\n");/Key_AddBindingText(0, "\\n");/'
mutant hidden-by-string-literal 's/Key_AddBindingText(key, kb);/Key_AddBindingText(key, kb); Cbuf_AddText("\/*"); Cbuf_AddText(kb); Cbuf_AddText("*\/");/'
mutant hidden-by-char-literal "s/Key_AddBindingText(key, kb);/Key_AddBindingText(key, kb); if (key == '\"') {} Cbuf_AddText(\"\/*\"); Cbuf_AddText(kb); Cbuf_AddText(\"*\/\");/"

# equivalent forms must pass
accept bounds-unparenthesised "s/$bounds/if (b < 0 || b >= K_LAST)/"
fi

if [ "$fail" != 0 ]; then
	exit 1
fi
if [ -z "${1:-}" ]; then
	echo "check-keyboard-sites: ok, $m mutants refused"
else
	echo "check-keyboard-sites: ok, rules only"
fi
