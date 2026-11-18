#!/bin/sh
# Invariant: no new undocumented cvar, console command or environment switch.
#
# CLAUDE.md requires every new cvar to be documented in doc/040_cvarlist.md
# and every new command in doc/050_commands.md. The upstream tree predates
# that rule, so the names undocumented there are frozen in tests/baselines/.
# A name that is neither documented nor baselined fails; so does a baselined
# name that is now documented or gone (delete that line), and so does a
# baselined name that is not in src/ at the upstream base commit (the
# baseline freezes upstream's gaps, never the fork's).
#
# What is scanned: every *.c, *.m, *.mm and *.h under src/. Each file is
# joined into one line first, so a call whose name sits on a continuation
# line is read like any other. The calls are Cvar_Get (also as ri.Cvar_Get),
# gi.cvar, and Cmd_AddCommand (also as ri.Cmd_AddCommand). The first
# argument must be a string literal; one that is not fails unless
# tests/baselines/nonliteral-calls.txt lists the file, the call and the
# argument with a reason (path|callee|first argument|reason). A
# commented-out call is read like a live one (so it must be
# documented or baselined as well).
#
# Environment switches: every getenv("NAME") literal under src/ must be one
# that the upstream base already has, or be named in doc/040_cvarlist.md.
#
# The upstream base is the commit UPSTREAM_BASE below, read with git at run
# time. When it is missing (a shallow clone), when git fails, or when a
# baseline cannot be read, the check fails closed and says so.
#
# Usage: tests/check-cvar-docs.sh
#        tests/check-cvar-docs.sh --selftest
#        tests/check-cvar-docs.sh --print-baseline cvars|commands
# Test hooks (self-test only): CVARDOCS_ROOT is the tree to check,
# CVARDOCS_BASE the upstream commit.
set -eu
export LC_ALL=C
here=$(cd "$(dirname "$0")" && pwd)
UPSTREAM_BASE=${CVARDOCS_BASE:-9dbf6992}
TAB=$(printf '\t')

die() {
	echo "check-cvar-docs: $*" >&2
	exit 1
}

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

# srcfiles: every C, Objective-C and header file under src/.
srcfiles() {
	find src -type f \( -name '*.c' -o -name '*.m' -o -name '*.mm' -o -name '*.h' \) | sort
}

# scan_calls: one line per call, "path<TAB>callee<TAB>first-argument", where
# callee is Cvar_Get, cvar or Cmd_AddCommand.
scan_calls() {
	srcfiles | while IFS= read -r f; do
		tr -s ' \t\r\n' ' ' < "$f" |
			grep -oE '(Cvar_Get|[.>]cvar|Cmd_AddCommand)[[:space:]]*\([[:space:]]*("[^"]*"|[^,)]*)' |
			sed -E -e 's/^[.>]?(Cvar_Get|cvar|Cmd_AddCommand)[[:space:]]*\([[:space:]]*/\1'"$TAB"'/' \
				-e 's/[[:space:]]+$//' |
			awk -v f="$f" -v t="$TAB" '{ print f t $0 }' || true
	done
}

# scan_getenv: every getenv("NAME") literal under src/.
scan_getenv() {
	srcfiles | while IFS= read -r f; do
		tr -s ' \t\r\n' ' ' < "$f" |
			grep -oE 'getenv[[:space:]]*\([[:space:]]*"[^"]*"' | sed -E 's/.*"([^"]*)"/\1/' || true
	done
}

allow=tests/baselines/nonliteral-calls.txt

# collect DIR: fill DIR/cvars, DIR/commands (names), DIR/problems (calls the
# check cannot read) and DIR/getenv.
collect() {
	d=$1
	: > "$d/cvars"; : > "$d/commands"; : > "$d/problems"
	[ -r "$allow" ] || die "cannot read $allow"
	scan_calls > "$d/calls"
	while IFS="$TAB" read -r f callee arg; do
		case $arg in
		\"*\")
			name=${arg#\"}
			name=${name%\"}
			if [ "$callee" = Cmd_AddCommand ]; then
				pat='^[A-Za-z0-9_+-]+$'; out=$d/commands
			else
				pat='^[A-Za-z0-9_]+$'; out=$d/cvars
			fi
			if printf '%s\n' "$name" | grep -qE "$pat"; then
				echo "$name" >> "$out"
			else
				echo "$f: $callee(\"$name\"...) is not a name the check can read" >> "$d/problems"
			fi
			;;
		"const "* | "char "*)
			: # a definition or a prototype, not a call
			;;
		*)
			if ! grep -qxE "$(printf '%s' "$f|$callee|$arg|" | sed 's/[][\.*^$/+?(){}|]/\\&/g').+" "$allow"; then
				echo "$f: $callee($arg ...) takes its name from a variable; list it with a reason in $allow or pass a string literal" >> "$d/problems"
			fi
			;;
		esac
	done < "$d/calls"
	{ cat "$d/cvars"; vidrules_cvars; } | sort -u > "$d/cvars.s"
	mv "$d/cvars.s" "$d/cvars"
	sort -u "$d/commands" > "$d/commands.s"
	mv "$d/commands.s" "$d/commands"
	scan_getenv | sort -u > "$d/getenv"
}

undocumented() { # $1 = names file, $2 = doc file
	while IFS= read -r name; do
		grep -qwF -- "$name" "$2" || echo "$name"
	done < "$1"
}

run_check() {
	root=${CVARDOCS_ROOT:-$here/..}
	cd "$root" || die "cannot enter $root"
	for f in doc/040_cvarlist.md doc/050_commands.md tests/baselines/undocumented-cvars.txt \
		tests/baselines/undocumented-commands.txt "$allow"; do
		[ -r "$f" ] || die "cannot read $f"
	done
	# The table is read in a loop below, where an exit would not stop the
	# script: prove it is readable here, at the top level.
	vidrules_cvars > /dev/null || exit 1

	tmp=$(mktemp -d "${TMPDIR:-/tmp}/cvardocs.XXXXXX")
	trap 'rm -rf "${tmp:?}"' EXIT
	collect "$tmp"

	if [ "${1:-}" = "--print-baseline" ]; then
		case "${2:-}" in
		cvars) undocumented "$tmp/cvars" doc/040_cvarlist.md ;;
		commands) undocumented "$tmp/commands" doc/050_commands.md ;;
		*) echo "usage: $0 --print-baseline cvars|commands" >&2; exit 2 ;;
		esac
		exit 0
	fi

	# The upstream base: every plain name string at the base commit.
	git cat-file -e "$UPSTREAM_BASE^{commit}" 2> /dev/null ||
		die "the upstream base commit $UPSTREAM_BASE is not in this repository (a shallow clone?); fetch full history"
	rc=0
	git grep -h -oE '"[A-Za-z0-9_+-]+"' "$UPSTREAM_BASE" -- src > "$tmp/up.raw" 2> "$tmp/up.err" || rc=$?
	if [ "$rc" -gt 1 ]; then
		cat "$tmp/up.err" >&2
		die "git grep at $UPSTREAM_BASE failed (exit $rc)"
	fi
	tr -d '"' < "$tmp/up.raw" | sort -u > "$tmp/up.names"
	[ -s "$tmp/up.names" ] || die "no names found in src at $UPSTREAM_BASE"
	rc=0
	git grep -h -oE 'getenv[[:space:]]*\([[:space:]]*"[^"]*"' "$UPSTREAM_BASE" -- src > "$tmp/upenv.raw" 2> "$tmp/up.err" || rc=$?
	if [ "$rc" -gt 1 ]; then
		cat "$tmp/up.err" >&2
		die "git grep at $UPSTREAM_BASE failed (exit $rc)"
	fi
	sed -E 's/.*"([^"]*)"/\1/' < "$tmp/upenv.raw" | sort -u > "$tmp/up.env"

	fail=0
	if [ -s "$tmp/problems" ]; then
		echo "check-cvar-docs: calls the check cannot read:"
		sed 's/^/  /' "$tmp/problems"
		fail=1
	fi
	for kind in cvars commands; do
		case $kind in
		cvars) doc=doc/040_cvarlist.md ;;
		commands) doc=doc/050_commands.md ;;
		esac
		base=tests/baselines/undocumented-$kind.txt
		undocumented "$tmp/$kind" "$doc" > "$tmp/now"
		sort -u "$base" > "$tmp/base"
		new=$(comm -23 "$tmp/now" "$tmp/base")
		stale=$(comm -13 "$tmp/now" "$tmp/base")
		fork=$(awk 'NR == FNR { u[$0] = 1; next } !($0 in u) { print FILENAME ":" FNR ": " $0 }' \
			"$tmp/up.names" "$base")
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
		if [ -n "$fork" ]; then
			echo "check-cvar-docs: $base lists $kind that are not in src at $UPSTREAM_BASE (a baseline freezes upstream's gaps only; document the name instead):"
			echo "$fork" | sed 's/^/  /'
			fail=1
		fi
	done
	newenv=$(comm -23 "$tmp/getenv" "$tmp/up.env" | while IFS= read -r name; do
		grep -qwF -- "$name" doc/040_cvarlist.md || echo "$name"
	done)
	if [ -n "$newenv" ]; then
		echo "check-cvar-docs: environment variables read with getenv that upstream does not and doc/040_cvarlist.md does not name:"
		echo "$newenv" | sed 's/^/  /'
		fail=1
	fi
	# An allow-list line without a reason is not an entry.
	bad=$(grep -vE '^[^|#]+\|[^|]+\|[^|]+\|.+$|^#|^$' "$allow" || true)
	if [ -n "$bad" ]; then
		echo "check-cvar-docs: $allow: each line is path|callee|first argument|reason; these are not:"
		echo "$bad" | sed 's/^/  /'
		fail=1
	fi
	[ "$fail" -eq 0 ] && echo "check-cvar-docs: ok"
	exit "$fail"
}

# ---- self-test -----------------------------------------------------------
# A fixture tree under a scratch directory, with its own throwaway git
# repository (no configuration of the user's or the system's is read, no
# hooks run) whose one commit is the "upstream base". Each case copies it,
# changes it and runs this script on the copy, asserting the verdict and the
# name the output must carry.

selftest() {
	st=$(mktemp -d "${TMPDIR:-/tmp}/cvardocs-self.XXXXXX")
	trap 'rm -rf "${st:?}"' EXIT
	export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null GIT_TERMINAL_PROMPT=0
	unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE
	fx=$st/fx
	mkdir -p "$fx/src/client/menu" "$fx/src/common" "$fx/src/client/refresh" "$fx/doc" "$fx/tests/baselines"
	cat > "$fx/src/client/menu/vidrules.c" <<'EOF'
const char *
VidRules_Cvar(int item)
{
	switch (item)
	{
		case 0:
			return "vr_cvar";
	}
	return 0;
}
EOF
	cat > "$fx/src/common/up.c" <<'EOF'
void Up(void)
{
	Cvar_Get("up_cvar", "0", 0);
	Cmd_AddCommand("up_cmd", Up);
	getenv("UP_ENV");
}
EOF
	printf 'vr_cvar documented here\n' > "$fx/doc/040_cvarlist.md"
	printf 'nothing yet\n' > "$fx/doc/050_commands.md"
	printf 'up_cvar\n' > "$fx/tests/baselines/undocumented-cvars.txt"
	printf 'up_cmd\n' > "$fx/tests/baselines/undocumented-commands.txt"
	printf '# path|callee|first argument|reason\nsrc/client/refresh/rt.c|Cvar_Get|name|the gate walks its own table\n' > "$fx/$allow"
	(
		cd "$fx" || exit 1
		git init -q . &&
			git add -A &&
			git -c user.name=st -c user.email=st@invalid -c commit.gpgsign=false -c core.hooksPath=/dev/null \
				commit -q -m base
	) || die "self-test: cannot build the fixture repository"
	sha=$(git -C "$fx" rev-parse HEAD)
	n=0
	bad=0

	# case ID VERDICT EXPECT-TEXT SETUP: SETUP is a shell function run in the copy.
	case_run() {
		cid=$1; want=$2; text=$3; setup=$4
		n=$((n + 1))
		d=$st/c$n
		cp -R "$fx" "$d"
		(cd "$d" && "$setup")
		rc=0
		out=$(CVARDOCS_ROOT=$d CVARDOCS_BASE=${CASE_BASE:-$sha} sh "$here/check-cvar-docs.sh" 2>&1) || rc=$?
		got=refused
		[ "$rc" -eq 0 ] && got=accepted
		ok=1
		[ "$got" = "$want" ] || ok=0
		printf '%s\n' "$out" | grep -qF -- "$text" || ok=0
		if [ "$want" = accepted ] && [ "$out" != "check-cvar-docs: ok" ]; then ok=0; fi
		if [ "$ok" -eq 1 ]; then
			echo "  ok   $cid: $want"
		else
			echo "check-cvar-docs: self-test case $cid: expected $want with \"$text\", got $got (exit $rc):"
			printf '%s\n' "$out" | sed 's/^/    /'
			bad=1
		fi
	}

	s_none() { :; }
	s_m() { mkdir -p src/r; printf 'void f(void) { x = ri.Cvar_Get("zz_m", "0", 0); }\n' > src/r/a.m; }
	s_mm() { mkdir -p src/r; printf 'void f(void) { x = ri.Cvar_Get("zz_m", "0", 0); }\n' > src/r/a.mm; }
	s_h() { mkdir -p src/r; printf 'static inline void f(void) { x = ri.Cvar_Get("zz_m", "0", 0); }\n' > src/r/a.h; }
	s_nl() { printf 'void f(void)\n{\n\tx = Cvar_Get(\n\t\t"zz_nl", "0", 0);\n}\n' > src/common/nl.c; }
	s_nl_gi() { printf 'void f(void)\n{\n\tx = gi.cvar(\n\t\t"zz_gi", "0", 0);\n}\n' > src/common/nl.c; }
	s_dot_cvar() { printf 'void f(void) { x = gi.cvar("zz_gi", "0", 0); }\n' > src/common/nl.c; }
	s_var() { printf 'void f(void) { x = Cvar_Get(name, "0", 0); }\n' > src/common/v.c; }
	s_var_allow() { mkdir -p src/client/refresh; printf 'void f(void) { x = ri.Cvar_Get(name, "0", 0); }\n' > src/client/refresh/rt.c; }
	s_var_allow_other() { mkdir -p src/client/refresh; printf 'void f(void) { x = ri.Cvar_Get(other, "0", 0); }\n' > src/client/refresh/rt.c; }
	s_var_noreason() { s_var_allow; printf 'src/client/refresh/rt.c|Cvar_Get|name|\n' > tests/baselines/nonliteral-calls.txt; }
	s_cmd_m() { mkdir -p src/r; printf 'void f(void) { ri.Cmd_AddCommand("zz_cmd", f); }\n' > src/r/a.m; }
	s_cmd_nl() { printf 'void f(void)\n{\n\tCmd_AddCommand(\n\t\t"zz_cmd", f);\n}\n' > src/common/nl.c; }
	s_cmd_var() { printf 'void f(void) { Cmd_AddCommand(cmdname, f); }\n' > src/common/v.c; }
	s_cmd_var_allow() { mkdir -p src/client/refresh; printf 'void f(void) { ri.Cmd_AddCommand(name, f); }\n' > src/client/refresh/rt.c; }
	s_cmd_var_allow_listed() { s_cmd_var_allow; printf 'src/client/refresh/rt.c|Cmd_AddCommand|name|the same walk\n' >> tests/baselines/nonliteral-calls.txt; }
	s_fork_base() { printf 'zz_fork\nup_cvar\n' > tests/baselines/undocumented-cvars.txt; printf 'void f(void) { Cvar_Get("zz_fork", "0", 0); }\n' > src/common/fork.c; }
	s_fork_base_cmd() { printf 'zz_forkcmd\nup_cmd\n' > tests/baselines/undocumented-commands.txt; printf 'void f(void) { Cmd_AddCommand("zz_forkcmd", f); }\n' > src/common/fork.c; }
	s_env_new() { printf 'void f(void) { getenv("ZZ_X"); }\n' > src/common/e.c; }
	s_env_doc() { s_env_new; printf 'ZZ_X is read by f.\n' >> doc/040_cvarlist.md; }
	s_literal_doc() { printf 'void f(void) { Cvar_Get("zz_doc", "0", 0); }\n' > src/common/d.c; printf 'zz_doc is documented\n' >> doc/040_cvarlist.md; }
	s_cmt_block() { printf '/* Cvar_Get("zz_c", "0", 0); */\n' > src/common/c.c; }
	s_cmt_line() { printf '// Cvar_Get("zz_c", "0", 0);\n' > src/common/c.c; }
	s_no_base() { :; }
	s_no_baseline() { rm -f tests/baselines/undocumented-cvars.txt; }
	s_no_allow() { rm -f tests/baselines/nonliteral-calls.txt; }

	echo "check-cvar-docs: self-test"
	case_run control accepted "check-cvar-docs: ok" s_none
	case_run m-file refused zz_m s_m
	case_run mm-file refused zz_m s_mm
	case_run h-file refused zz_m s_h
	case_run cvar-name-on-next-line refused zz_nl s_nl
	case_run gi.cvar-name-on-next-line refused zz_gi s_nl_gi
	case_run gi.cvar-one-line refused zz_gi s_dot_cvar
	case_run cvar-variable-arg refused "src/common/v.c: Cvar_Get(name" s_var
	case_run cvar-variable-arg-allow-listed accepted "check-cvar-docs: ok" s_var_allow
	case_run cvar-variable-arg-other-arg-not-listed refused "rt.c: Cvar_Get(other" s_var_allow_other
	case_run allow-list-line-without-reason refused "path|callee|first argument|reason" s_var_noreason
	case_run cmd-m-file refused zz_cmd s_cmd_m
	case_run cmd-name-on-next-line refused zz_cmd s_cmd_nl
	case_run cmd-variable-arg refused "src/common/v.c: Cmd_AddCommand(cmdname" s_cmd_var
	case_run cmd-variable-arg-allow-listed accepted "check-cvar-docs: ok" s_cmd_var_allow_listed
	case_run cmd-variable-arg-listed-for-cvar-only refused "rt.c: Cmd_AddCommand(name" s_cmd_var_allow
	case_run baselined-cvar-not-upstream refused "undocumented-cvars.txt:1: zz_fork" s_fork_base
	case_run baselined-command-not-upstream refused "undocumented-commands.txt:1: zz_forkcmd" s_fork_base_cmd
	case_run baselined-upstream-name accepted "check-cvar-docs: ok" s_none
	case_run getenv-new-undocumented refused ZZ_X s_env_new
	case_run getenv-new-documented accepted "check-cvar-docs: ok" s_env_doc
	case_run getenv-upstream accepted "check-cvar-docs: ok" s_none
	case_run literal-documented-one-line accepted "check-cvar-docs: ok" s_literal_doc
	case_run commented-out-block-counts refused zz_c s_cmt_block
	case_run commented-out-line-counts refused zz_c s_cmt_line
	CASE_BASE=0000000000000000000000000000000000000000 case_run base-commit-missing refused "upstream base commit" s_no_base
	case_run baseline-unreadable refused "cannot read tests/baselines/undocumented-cvars.txt" s_no_baseline
	case_run allow-list-unreadable refused "cannot read tests/baselines/nonliteral-calls.txt" s_no_allow
	# git failing is not "no match": a git that exits 3 stops the check.
	mkdir -p "$st/fakebin"
	printf '#!/bin/sh\nexit 3\n' > "$st/fakebin/git"
	chmod +x "$st/fakebin/git"
	n=$((n + 1))
	cp -R "$fx" "$st/c$n"
	rc=0
	out=$(PATH="$st/fakebin:$PATH" CVARDOCS_ROOT=$st/c$n CVARDOCS_BASE=$sha sh "$here/check-cvar-docs.sh" 2>&1) || rc=$?
	if [ "$rc" -ne 0 ] && printf '%s\n' "$out" | grep -qF "upstream base commit"; then
		echo "  ok   git-fails: refused"
	else
		echo "check-cvar-docs: self-test case git-fails: expected refused, got exit $rc: $out"
		bad=1
	fi
	[ "$bad" -eq 0 ] || die "self-test failed"
	echo "check-cvar-docs: self-test ok ($n cases)"
}

case "${1:-}" in
--selftest) selftest; exit 0 ;;
esac
selftest_out=$(mktemp "${TMPDIR:-/tmp}/cvardocs-st.XXXXXX")
if [ -z "${CVARDOCS_ROOT:-}" ] && [ "${1:-}" != "--print-baseline" ]; then
	sh "$here/check-cvar-docs.sh" --selftest > "$selftest_out" 2>&1 || {
		cat "$selftest_out"
		rm -f "$selftest_out"
		exit 1
	}
fi
rm -f "$selftest_out"
run_check "$@"
