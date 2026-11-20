#!/bin/sh
# Invariant: the commands that write a file named by a console argument
# (condump in src/client/cl_console.c, record in src/client/cl_main.c) call
# FS_SafeRelativeName(Cmd_Argv(1)) before their first Q_fopen. FS_CreatePath
# refuses "..", but Q_fopen does not (CLAUDE.md, security boundaries). Those
# two client files are not in the unit suite, so the commands cannot be
# driven there; this is a source rule. It runs the rule on scratch mutants
# (call deleted, call moved after Q_fopen, function renamed), each of which
# must be refused. Needs no compiler, no SDL and no game data.
set -eu
cd "$(dirname "$0")/.."

fail=0
bad() { echo "check-safe-names: $*"; fail=1; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/safenames.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

# rule FILE FUNCTION: prints nothing when the body of FUNCTION has the
# FS_SafeRelativeName(Cmd_Argv(1)) call before every Q_fopen, else a reason.
rule()
{
	awk -v fn="$2" '
		$0 ~ "^" fn "[ \t]*\\(" { infn = 1; found = 1; next }
		infn && /^}/ { infn = 0 }
		infn {
			if (index($0, "FS_SafeRelativeName(Cmd_Argv(1))")) { safe = 1 }
			if (index($0, "Q_fopen") && !safe) { unsafe = 1 }
			if (index($0, "Q_fopen")) { opens = 1 }
		}
		END {
			if (!found) { print fn " not found"; exit }
			if (!opens) { print fn " has no Q_fopen, the rule is stale"; exit }
			if (unsafe) { print fn " reaches Q_fopen before FS_SafeRelativeName(Cmd_Argv(1))" }
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
	awk "$4" "$2" > "$tmp/m.c"
	if [ -z "$(rule "$tmp/m.c" "$3")" ]; then
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
done

if [ "$fail" -ne 0 ]; then
	exit 1
fi
echo "check-safe-names: ok (Con_Dump_f and CL_Record_f check the name before Q_fopen, 6 mutants refused)"
