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

cvars() {
	grep -rhoE '(Cvar_Get|[.>]cvar)[[:space:]]*\([[:space:]]*"[A-Za-z0-9_]+"' src --include='*.c' |
		sed -E 's/.*"([^"]+)"/\1/' | sort -u
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
