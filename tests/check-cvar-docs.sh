#!/bin/sh
# Invariant: no new undocumented cvar or console command.
#
# CLAUDE.md requires every new cvar to be documented in doc/040_cvarlist.md
# and every new command in doc/050_commands.md. The upstream tree predates
# that rule, so the names undocumented today are frozen in
# tests/baselines/. A name that is neither documented nor baselined fails;
# so does a baselined name that is now documented or gone (delete that line).
#
# Usage: tests/check-cvar-docs.sh
#        tests/check-cvar-docs.sh --print-baseline cvars|commands
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C

# The video menu creates cvars by name through the VidRules_Cvar table, not
# through Cvar_Get: every string literal in that function body is a cvar name.
# Fails closed: a missing function or an empty list stops the check.
vidrules_cvars() {
	names=$(awk '/^VidRules_Cvar\(/ { on = 1 } on { print } on && /^}/ { exit }' \
		src/client/menu/vidrules.c | grep -oE '"[A-Za-z0-9_]+"' | tr -d '"' | sort -u)
	if [ -z "$names" ]; then
		echo "check-cvar-docs: no cvar names found in VidRules_Cvar (src/client/menu/vidrules.c)" >&2
		return 1
	fi
	echo "$names"
}
cvars() {
	{
		grep -rhoE '(Cvar_Get|[.>]cvar)[[:space:]]*\([[:space:]]*"[A-Za-z0-9_]+"' src --include='*.c' |
			sed -E 's/.*"([^"]+)"/\1/'
		vidrules_cvars
	} | sort -u
}
commands() {
	grep -rhoE 'Cmd_AddCommand[[:space:]]*\([[:space:]]*"[A-Za-z0-9_+-]+"' src --include='*.c' |
		sed -E 's/.*"([^"]+)"/\1/' | sort -u
}
undocumented() { # $1 = names fn, $2 = doc file
	"$1" | while IFS= read -r name; do
		grep -qwF -- "$name" "$2" || echo "$name"
	done
}

# The table is read in a pipeline below, where an exit would not stop the
# script: prove it is readable here, at the top level.
vidrules_cvars > /dev/null || exit 1

if [ "${1:-}" = "--print-baseline" ]; then
	case "${2:-}" in
	cvars) undocumented cvars doc/040_cvarlist.md ;;
	commands) undocumented commands doc/050_commands.md ;;
	*) echo "usage: $0 --print-baseline cvars|commands" >&2; exit 2 ;;
	esac
	exit 0
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cvardocs.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
fail=0
for kind in cvars commands; do
	case $kind in
	cvars) doc=doc/040_cvarlist.md ;;
	commands) doc=doc/050_commands.md ;;
	esac
	base=tests/baselines/undocumented-$kind.txt
	undocumented "$kind" "$doc" > "$tmp/now"
	sort -u "$base" > "$tmp/base"
	new=$(comm -23 "$tmp/now" "$tmp/base")
	stale=$(comm -13 "$tmp/now" "$tmp/base")
	if [ -n "$new" ]; then
		echo "check-cvar-docs: new $kind not documented in $doc:"
		echo "$new" | sed 's/^/  /'
		fail=1
	fi
	if [ -n "$stale" ]; then
		echo "check-cvar-docs: $base lists $kind that are now documented or removed; delete these lines:"
		echo "$stale" | sed 's/^/  /'
		fail=1
	fi
done
[ "$fail" -eq 0 ] && echo "check-cvar-docs: ok"
exit "$fail"
