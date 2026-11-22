#!/bin/sh
# Invariant: the commands that write a file named by a console argument
# (condump in src/client/cl_console.c, record in src/client/cl_main.c) call
# FS_SafeRelativeName(Cmd_Argv(1)) before their first Q_fopen. FS_CreatePath
# refuses "..", but Q_fopen does not (CLAUDE.md, security boundaries). Those
# two client files are not in the unit suite, so the commands cannot be
# driven there; this is a source rule. It runs the rule on scratch mutants
# (call deleted, moved after Q_fopen, bare call with its result ignored,
# disabled condition, guard in a comment, return removed, function renamed),
# each of which must be refused. Needs no compiler, no SDL and no game data.
set -eu
cd "$(dirname "$0")/.."

fail=0
bad() { echo "check-safe-names: $*"; fail=1; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/safenames.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT

# rule FILE FUNCTION: prints nothing when, in the body of FUNCTION, every
# Q_fopen is preceded by the line
#   if (!FS_SafeRelativeName(Cmd_Argv(1)))
# (leading whitespace ignored) and a "return;" within the next 4 lines, else
# a reason. Comment lines (// or /* at the start, or inside a /* */ block) are
# not counted, so a bare call whose result is ignored, a disabled condition
# or a commented-out guard does not satisfy it.
rule()
{
	awk -v fn="$2" '
		$0 ~ "^" fn "[ \t]*\\(" { infn = 1; found = 1; next }
		infn && /^}/ { infn = 0 }
		!infn { next }
		{
			line = $0
			sub(/^[ \t]+/, "", line)
			sub(/[ \t]+$/, "", line)
		}
		inblock { if (index(line, "*/")) { inblock = 0 } next }
		line ~ /^\/\// { next }
		line ~ /^\/\*/ { if (!index(line, "*/")) { inblock = 1 } next }
		{
			if (line == "if (!FS_SafeRelativeName(Cmd_Argv(1)))") { guard = NR }
			if (guard && NR <= guard + 4 && line == "return;") { safe = 1 }
			if (index(line, "Q_fopen")) {
				opens = 1
				if (!safe) { unsafe = 1 }
			}
		}
		END {
			if (!found) { print fn " not found"; exit }
			if (!opens) { print fn " has no Q_fopen, the rule is stale"; exit }
			if (unsafe) { print fn " reaches Q_fopen without if (!FS_SafeRelativeName(Cmd_Argv(1))) and a return;" }
		}' "$1"
}

check()
{
	_msg=$(rule "$1" "$2")
	if [ -n "$_msg" ]; then
		bad "$1: $_msg"
	fi
}

check src/client/cl_console.c Con_Dump_f
check src/client/cl_main.c CL_Record_f

# mutant LABEL FILE FUNCTION AWKPROGRAM: the rewritten file must be refused.
mutant()
{
	awk "$4" "$2" > "${tmp:?}/m.c"
	if [ -z "$(rule "${tmp:?}/m.c" "$3")" ]; then
		bad "mutant '$1' of $3 was accepted"
	fi
}

for pair in "src/client/cl_console.c Con_Dump_f" "src/client/cl_main.c CL_Record_f"; do
	set -- $pair
	f=$1
	fn=$2
	mutant "call deleted" "$f" "$fn" \
		'!index($0, "FS_SafeRelativeName(Cmd_Argv(1))")'
	mutant "function renamed" "$f" "$fn" \
		"{ sub(/^$fn[ \\t]*\\(/, \"Renamed_f(\"); print }"
	mutant "call moved after Q_fopen" "$f" "$fn" \
		'index($0, "FS_SafeRelativeName(Cmd_Argv(1))") { next }
		 { print }
		 index($0, "Q_fopen") { print "\tFS_SafeRelativeName(Cmd_Argv(1));" }'
	mutant "result ignored" "$f" "$fn" \
		'index($0, "if (!FS_SafeRelativeName(Cmd_Argv(1)))") { print "\tFS_SafeRelativeName(Cmd_Argv(1));"; next }
		 { print }'
	mutant "disabled condition" "$f" "$fn" \
		'{ sub(/if \(!FS_SafeRelativeName/, "if (0 \\&\\& !FS_SafeRelativeName"); print }'
	mutant "guard in a line comment" "$f" "$fn" \
		'index($0, "if (!FS_SafeRelativeName(Cmd_Argv(1)))") { print "\t// " $0; next }
		 { print }'
	mutant "guard in a block comment" "$f" "$fn" \
		'index($0, "if (!FS_SafeRelativeName(Cmd_Argv(1)))") { print "\t/*"; print; print "\t*/"; next }
		 { print }'
	mutant "return removed" "$f" "$fn" \
		'seen && !gone && /^[ \t]*return;/ { gone = 1; next }
		 index($0, "if (!FS_SafeRelativeName(Cmd_Argv(1)))") { seen = 1 }
		 { print }'
done

if [ "$fail" -ne 0 ]; then
	exit 1
fi
echo "check-safe-names: ok (Con_Dump_f and CL_Record_f check the name before Q_fopen, 16 mutants refused)"
