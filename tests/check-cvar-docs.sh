#!/bin/sh
# Invariant: no new undocumented cvar, console command or environment switch.
#
# CLAUDE.md requires every new cvar to be documented in doc/040_cvarlist.md
# and every new command in doc/050_commands.md. The upstream tree predates
# that rule, so the names undocumented there are frozen in tests/baselines/.
# A name that is neither documented nor baselined fails; so does a baselined
# name that is now documented or gone (delete that line), and so does a
# baselined name that upstream does not create (the baseline freezes
# upstream's gaps, never the fork's).
#
# What is scanned: every *.c, *.m, *.mm and *.h under src/, joined into one
# text per file, so a call whose name sits on a continuation line is read
# like any other. All calls of the whole tree are classified in one awk
# pass, no process per call. The calls:
#   Cvar_Get, gi.cvar                      create a cvar
#   Cvar_Set, Cvar_SetValue, Cvar_ForceSet, Cvar_FullSet, gi.cvar_set,
#   gi.cvar_setvalue, gi.cvar_forceset     create the cvar when the name is
#                                          unknown (Cvar_Set2 calls
#                                          Cvar_GetNew), so a literal name
#                                          counts like Cvar_Get's
#   Cmd_AddCommand                         creates a command
#   getenv                                 reads an environment switch
# (with or without the ri. and gi. prefix). A commented-out call is read
# like a live one. A Cvar_Get or Cmd_AddCommand whose first argument is not
# a string literal (the literal, then a comma or a parenthesis: "a" SUFFIX is
# not one) fails unless tests/baselines/nonliteral-calls.txt lists
# the file, the call and the argument. A Set call with a name taken from a
# variable is not checked: the engine itself makes about fifty of them, on
# cvars that exist, and no reading of the source can tell the ones that
# create from the ones that do not.
#
# The allow list: path|callee|first argument|table|reason. The table names
# where the names the call walks come from, and the check reads that
# table's string literals as cvar names, so a walk cannot add an
# undocumented name: path:Symbol is an array (the first literal of each
# {row} is a name; every literal of a flat array of strings is) or a function
# (every literal in its body is; a lone struct initialiser gives its first
# literal) defined at column 0 of path;
# fmt:pattern:low:high is pattern with %d from low to high, a bound being an
# integer or MACRO or MACRO-n, the macro one plain-integer #define under src
# (else the check fails). An entry whose call is not in src fails (a stale entry would
# pre-approve a future walk); a table that gives no name fails.
#
# Environment switches: every getenv("NAME") literal under src/ must be one
# that upstream already has, or be named in doc/040_cvarlist.md.
#
# The upstream names are the committed file tests/baselines/upstream-names.txt:
# the literal first arguments of the calls above in src/ at the upstream
# commit UPSTREAM_ID, which the file's header names. The plain run reads
# only that file and needs no history (the Stop hook runs it in a copy of
# HEAD with none). --verify-upstream regenerates the list from that commit
# and diffs it against the file; it needs the commit and fails closed
# without it (git fetch --depth=1 origin <id>). tests/check-cvar-docs-selftest.sh
# runs it in the gate when the commit is present.
#
# Fails closed: an unreadable doc, baseline, allow list or upstream file, a
# git that fails, and a missing upstream commit in --verify-upstream stop
# the check and say so.
#
# Usage: tests/check-cvar-docs.sh
#        tests/check-cvar-docs.sh --selftest
#        tests/check-cvar-docs.sh --verify-upstream
#        tests/check-cvar-docs.sh --print-upstream-names   (needs the commit)
#        tests/check-cvar-docs.sh --print-baseline cvars|commands
# Test hooks, honoured only when the self-test runs the check (it sets
# CVARDOCS_SELFTEST to its own scratch directory); a plain run that
# inherits one refuses: CVARDOCS_ROOT is the tree to check, CVARDOCS_BASE
# the upstream commit.
set -eu
export LC_ALL=C
here=$(cd "$(dirname "$0")" && pwd)
UPSTREAM_ID=9dbf699273e2546f3ad14cff3dee6a51a518cd2a
TAB=$(printf '\t')

die() {
	echo "check-cvar-docs: $*" >&2
	exit 1
}

# This script checks the tree at its root: no repository variable of a
# caller (a hook's GIT_DIR, GIT_INDEX_FILE) may point git elsewhere.
for v in $(git rev-parse --local-env-vars 2> /dev/null || :); do
	unset "$v"
done

hooks=
if [ -n "${CVARDOCS_SELFTEST:-}" ] && [ -f "$CVARDOCS_SELFTEST/.cvardocs-selftest" ]; then
	hooks=1
fi
if [ -z "$hooks" ]; then
	for v in CVARDOCS_ROOT CVARDOCS_BASE CVARDOCS_SELFTEST; do
		eval "set_v=\${$v+set}"
		[ -z "$set_v" ] || die "$v is set in the environment: it is a test hook of the self-test; unset it"
	done
fi
if [ -n "$hooks" ]; then
	UPSTREAM_ID=${CVARDOCS_BASE:-$UPSTREAM_ID}
fi

allow=tests/baselines/nonliteral-calls.txt
upfile=tests/baselines/upstream-names.txt

# The scanner: one record per call, "kind<TAB>path<TAB>callee<TAB>L|N<TAB>first
# argument" (L: a string literal, unquoted; N: anything else). kind is get,
# set, cmd or env. The text of each file is one record with its whitespace
# runs collapsed to one blank.
SCAN_AWK='
BEGIN { RS = "\001" }
{
	s = $0
	gsub(/[ \t\r\n]+/, " ", s)
	while (match(s, /(Cvar_Get|Cmd_AddCommand|Cvar_SetValue|Cvar_ForceSet|Cvar_FullSet|Cvar_Set|[.>]cvar_forceset|[.>]cvar_setvalue|[.>]cvar_set|[.>]cvar|getenv) ?\(/)) {
		callee = substr(s, RSTART, RLENGTH)
		s = substr(s, RSTART + RLENGTH)
		sub(/ ?\($/, "", callee)
		sub(/^[.>]/, "", callee)
		kind = "set"
		if (callee == "Cmd_AddCommand") kind = "cmd"
		else if (callee == "getenv") kind = "env"
		else if (callee == "Cvar_Get" || callee == "cvar") kind = "get"
		t = s
		sub(/^ /, "", t)
		if (substr(t, 1, 1) == "\"") {
			q = index(substr(t, 2), "\"")
			if (q > 0 && substr(t, q + 2) ~ /^ *[,)]/) {
				print kind "\t" FILENAME "\t" callee "\tL\t" substr(t, 2, q - 1)
				continue
			}
		}
		if (kind == "env") continue
		i = match(t, /[,)]/)
		arg = (i > 0) ? substr(t, 1, i - 1) : t
		sub(/ +$/, "", arg)
		print kind "\t" FILENAME "\t" callee "\tN\t" arg
	}
}
'

# The classifier: reads the calls, the docs, the baselines, the allow list
# and the upstream names, and prints what fails. Exit 0 when nothing does.
CLASSIFY_AWK='
function die(m) {
	print "check-cvar-docs: " m > "/dev/stderr"
	exit 1
}
function slurp(path,   old, x, r) {
	x = ""
	old = RS
	RS = "\001"
	r = (getline x < path)
	close(path)
	RS = old
	if (r < 0) die("cannot read " path)
	return x
}
# The names a doc opens an entry with: a line that starts "* **" (in doc/040
# also an indented "  - **" or "  * **" one, nested under another), up to the
# first "**:" (the header may wrap onto indented lines), and in the header
# the first word of every **bold** group. "* **a** / **b**: text" and
# "* **a**, **b**:" give a and b; a name in prose, in a code span or after
# the colon gives nothing. A name ending in _(x/y) stands for each choice.
function entries(D, E, nested,   n, L, i, h, on, k, g, A, m, j, base, C, st)
{
	n = split(D, L, "\n")
	on = 0; h = ""
	for (i = 1; i <= n; i++) {
		st = (L[i] ~ /^\* \*\*/) || (nested && L[i] ~ /^  [-*] \*\*/)
		# a header with no "**:" ends where the next entry or a blank line
		# starts; what it held is prose and counts for nothing
		if (on && (st || L[i] == "")) on = 0
		if (!on && !st) continue
		if (!on) { on = 1; h = ""; k = 0 }
		h = h " " L[i]
		k++
		if ((j = index(h, "**:")) > 0) { h = substr(h, 1, j + 1); on = 0 }
		else if (k >= 4) { on = 0; continue }
		else continue
		while (match(h, /\*\*[^*]+\*\*/)) {
			g = substr(h, RSTART + 2, RLENGTH - 4)
			h = substr(h, RSTART + RLENGTH)
			sub(/^ +/, "", g)
			split(g, A, " ")
			if (match(A[1], /_\([A-Za-z0-9\/]+\)$/)) {
				base = substr(A[1], 1, RSTART)
				m = split(substr(A[1], RSTART + 2, RLENGTH - 3), C, "/")
				for (j = 1; j <= m; j++) E[base C[j]] = 1
			} else E[A[1]] = 1
		}
	}
}
function documented(name, D, W)
{
	return (name in W)
}
function section(hdr, A,   k, n) {
	n = 0
	for (k in A) n++
	if (!n) return
	print hdr
	fflush()
	for (k in A) print "  " k | "sort"
	close("sort")
	fail = 1
}
# A fmt: range bound: an integer, or MACRO or MACRO-n, MACRO being a plain
# integer in the one #define of it under src/. Fails closed (returns "" and
# files a problem) when the macro is not found, is defined twice, or is not a
# plain integer.
function bound(b, where,   m, off, cmd, line, v, nd) {
	if (b ~ /^[0-9]+$/) return b + 0
	if (b !~ /^[A-Za-z_][A-Za-z0-9_]*(-[0-9]+)?$/) {
		badallow[where ": " b " is not a range bound"] = 1
		return ""
	}
	m = b; off = 0
	if (match(b, /-[0-9]+$/)) { off = substr(b, RSTART + 1) + 0; m = substr(b, 1, RSTART - 1) }
	cmd = "grep -rhE \"^[ \\t]*#[ \\t]*define[ \\t]+" m "([ \\t]|$)\" src"
	nd = 0; v = ""
	while ((cmd | getline line) > 0) {
		nd++
		sub(/^[ \t]*#[ \t]*define[ \t]+[A-Za-z0-9_]+[ \t]*/, "", line)
		sub(/[ \t]*(\/\*.*)?(\/\/.*)?$/, "", line)
		v = line
	}
	close(cmd)
	if (nd == 0) { tblprob[where ": no #define of " m " under src"] = 1; return "" }
	if (nd > 1) { tblprob[where ": " m " is defined more than once under src"] = 1; return "" }
	if (v !~ /^[0-9]+$/) { tblprob[where ": " m " is not a plain integer (the #define says \"" v "\")"] = 1; return "" }
	if (off > v + 0) { tblprob[where ": " b " is below zero"] = 1; return "" }
	return v - off
}
# the names of the table sym defined at column 0 of path, into out
function tablenames(path, sym, out,   line, on, isarr, body, r, n, lit, flat, single) {
	on = 0; isarr = 0; body = ""; n = 0
	while ((r = getline line < path) > 0) {
		if (!on) {
			if (line ~ /^[A-Za-z_]/ && (line !~ /;[ \t]*$/ || line ~ /=.*\{.*\}[ \t]*;[ \t]*$/) && line ~ ("(^|[ *])" sym "(\\[[^]]*\\])* *(=|\\()")) {
				on = 1
				isarr = (line ~ ("[ *]" sym "\\["))
				body = line
				if (line ~ /\}[ \t]*;[ \t]*$/) break
			}
			continue
		}
		body = body " " line
		if (line ~ /^}/) break
	}
	if (r < 0) return -1
	close(path)
	if (!on) return 0
	gsub(/[ \t\r\n]+/, " ", body)
	if (isarr) {
		# the initialiser'"'"'s own { is dropped; with no { left the array is
		# flat and every literal is a name, else the first literal of each row
		if (match(body, /= ?\{/)) body = substr(body, RSTART + RLENGTH)
		flat = (body !~ /\{/)
		while (match(body, flat ? "\"[^\"]*\"" : "\\{ ?\"[^\"]*\"")) {
			lit = substr(body, RSTART, RLENGTH)
			body = substr(body, RSTART + RLENGTH)
			sub(/^\{ ?"/, "", lit); sub(/^"/, "", lit); sub(/"$/, "", lit)
			out[++n] = lit
		}
	} else {
		# a lone struct initialiser (sym = { "name", "label", ... }): the
		# first literal is the name, the others are its label and default
		single = (body ~ ("(^|[ *])" sym " *= *\\{"))
		while (match(body, /"[^"]*"/)) {
			lit = substr(body, RSTART + 1, RLENGTH - 2)
			body = substr(body, RSTART + RLENGTH)
			out[++n] = lit
			if (single) break
		}
	}
	return n
}
BEGIN {
	RS = "\n"
	pat_cvar = "^[A-Za-z0-9_]+$"
	pat_cmd = "^[A-Za-z0-9_+-]+$"
	d40 = slurp(f_d40); d50 = slurp(f_d50)
	entries(d40, W40, 1); entries(d50, W50, 0)

	# the upstream names
	nup = 0; upid = ""
	while ((r = getline line < f_up) > 0) {
		if (line ~ /^# id: /) { upid = substr(line, 7); continue }
		if (line ~ /^#/ || line == "") continue
		split(line, P, "\t")
		up[P[1] SUBSEP P[2]] = 1
		nup++
	}
	if (r < 0) die("cannot read " f_up)
	close(f_up)
	if (nup == 0) die("no names in " f_up)
	if (upid != want_id) other[f_up ": its header names the upstream id \"" upid "\", the check wants " want_id] = 1

	# the allow list
	while ((r = getline line < f_allow) > 0) {
		if (line ~ /^#/ || line == "") continue
		m = split(line, P, "|")
		reason = P[5]
		for (i = 6; i <= m; i++) reason = reason "|" P[i]
		if (m < 5 || P[1] == "" || P[2] == "" || P[3] == "" || P[4] == "" || reason == "") {
			badallow[line] = 1
			continue
		}
		key = P[1] SUBSEP P[2] SUBSEP P[3]
		aused[key] = 0
		aline[key] = P[1] "|" P[2] "|" P[3]
		tbl = P[4]
		n = 0
		if (tbl ~ /^fmt:[^:]*:[^:]+:[^:]+$/) {
			split(tbl, T, ":")
			lo = bound(T[3], aline[key]); hi = bound(T[4], aline[key])
			if (lo == "" || hi == "") continue
			for (i = lo; i <= hi; i++) {
				if (P[2] == "Cmd_AddCommand") cm[sprintf(T[2], i)] = 1
				else cv[sprintf(T[2], i)] = 1
			}
			n = 1
		} else if (tbl ~ /^[^:]+:[A-Za-z_][A-Za-z0-9_]*$/) {
			split(tbl, T, ":")
			n = tablenames(T[1], T[2], N)
			if (n < 0) die("cannot read " T[1])
			for (i = 1; i <= n; i++) {
				if (N[i] !~ ((P[2] == "Cmd_AddCommand") ? pat_cmd : pat_cvar)) tblprob[P[1] ": " tbl ": \"" N[i] "\" is not a name the check can read"] = 1
				else if (P[2] == "Cmd_AddCommand") cm[N[i]] = 1
				else cv[N[i]] = 1
			}
			delete N
		} else {
			badallow[line] = 1
			continue
		}
		if (n == 0) tblprob[aline[key] ": the table " tbl " gives no name"] = 1
	}
	if (r < 0) die("cannot read " f_allow)
	close(f_allow)

	# the calls
	while ((r = getline line < f_calls) > 0) {
		split(line, F, "\t")
		kind = F[1]; path = F[2]; callee = F[3]; lit = F[4]; arg = F[5]
		if (lit == "L") {
			if (kind == "env") env[arg] = 1
			else if (kind == "cmd") {
				if (arg ~ pat_cmd) cm[arg] = 1
				else callprob[path ": " callee "(\"" arg "\"...) is not a name the check can read"] = 1
			} else {
				if (arg ~ pat_cvar) cv[arg] = 1
				else callprob[path ": " callee "(\"" arg "\"...) is not a name the check can read"] = 1
			}
			continue
		}
		if (kind == "set") continue
		if (arg ~ /^(const |char )/) continue
		key = path SUBSEP callee SUBSEP arg
		if (key in aused) aused[key] = 1
		else callprob[path ": " callee "(" arg " ...) takes its name from a variable; list it with its table and a reason in " f_allow " or pass a string literal"] = 1
	}
	if (r < 0) die("cannot read " f_calls)
	for (key in aused) if (!aused[key]) stale[aline[key]] = 1

	for (nm in cv) if (!documented(nm, d40, W40)) ucv[nm] = 1
	for (nm in cm) if (!documented(nm, d50, W50)) ucm[nm] = 1

	if (mode == "print-cvars") { for (nm in ucv) print nm | "sort"; close("sort"); exit 0 }
	if (mode == "print-commands") { for (nm in ucm) print nm | "sort"; close("sort"); exit 0 }

	fail = 0
	section("check-cvar-docs: calls the check cannot read:", callprob)
	section("check-cvar-docs: " f_allow ": each line is path|callee|first argument|table|reason, the table being path:Symbol or fmt:pattern:low:high; these are not:", badallow)
	section("check-cvar-docs: a table named in " f_allow " is not readable:", tblprob)
	section("check-cvar-docs: " f_allow " lists calls that are not in src (a stale entry would pre-approve a future walk; add it with the walk); delete these lines:", stale)
	section("check-cvar-docs: the upstream names:", other)

	for (k = 1; k <= 2; k++) {
		kindn = (k == 1) ? "cvars" : "commands"
		upk = (k == 1) ? "cvar" : "cmd"
		doc = (k == 1) ? "doc/040_cvarlist.md" : "doc/050_commands.md"
		bf = (k == 1) ? f_bc : f_bm
		delete newn; delete stn; delete forkn; delete base
		nbl = 0
		while ((r = getline line < bf) > 0) {
			nbl++
			if (line == "") continue
			base[line] = 1
			if (!((upk SUBSEP line) in up)) forkn[bf ":" nbl ": " line] = 1
		}
		if (r < 0) die("cannot read " bf)
		close(bf)
		if (k == 1) { for (nm in ucv) if (!(nm in base)) newn[nm] = 1; for (nm in base) if (!(nm in ucv)) stn[nm] = 1 }
		else { for (nm in ucm) if (!(nm in base)) newn[nm] = 1; for (nm in base) if (!(nm in ucm)) stn[nm] = 1 }
		section("check-cvar-docs: new " kindn " not documented in " doc ":", newn)
		section("check-cvar-docs: " bf " lists " kindn " that are now documented or removed; delete these lines:", stn)
		section("check-cvar-docs: " bf " lists " kindn " that upstream does not create (a baseline freezes upstream gaps only; document the name instead):", forkn)
	}
	for (nm in env) if (!((("env") SUBSEP nm) in up) && !documented(nm, d40, W40)) envn[nm] = 1
	section("check-cvar-docs: environment variables read with getenv that upstream does not and doc/040_cvarlist.md does not name:", envn)
	if (!fail) print "check-cvar-docs: ok"
	exit fail
}
'

# scan_tree DIR: the calls of DIR/src, one record per call.
scan_tree() {
	(
		cd "$1" || exit 1
		find src -type f \( -name '*.c' -o -name '*.m' -o -name '*.mm' -o -name '*.h' \) -print0 > "$tmp/files"
		[ -s "$tmp/files" ] || die "no source files under $1/src"
		xargs -0 awk "$SCAN_AWK" < "$tmp/files"
	) || die "the scan of $1/src failed"
}

# upstream_names CALLS: the sorted upstream-names lines for a scan.
upstream_names() {
	awk -F "$TAB" '$4 == "L" { k = "cvar"; if ($1 == "cmd") k = "cmd"; else if ($1 == "env") k = "env"; print k "\t" $5 }' "$1" | sort -u
}

g() {
	git -c color.ui=never -c color.grep=never -C "$root" "$@"
}

# generate_upstream: the upstream-names lines at UPSTREAM_ID, into $tmp/gen.
generate_upstream() {
	g cat-file -e "$UPSTREAM_ID^{commit}" 2> /dev/null ||
		die "the upstream commit $UPSTREAM_ID is not in this repository; fetch it: git fetch --depth=1 origin $UPSTREAM_ID"
	mkdir -p "${tmp:?}/up"
	g archive --format=tar -o "${tmp:?}/up.tar" "$UPSTREAM_ID" src || die "git archive of src at $UPSTREAM_ID failed"
	tar -x -f "${tmp:?}/up.tar" -C "${tmp:?}/up" || die "cannot unpack src at $UPSTREAM_ID"
	scan_tree "${tmp:?}/up" > "${tmp:?}/up.calls"
	upstream_names "${tmp:?}/up.calls" > "${tmp:?}/gen"
	[ -s "${tmp:?}/gen" ] || die "no names found in src at $UPSTREAM_ID"
}

upstream_header() {
	cat <<EOF
# Names the upstream source creates or reads: the literal first arguments of
# Cvar_Get, gi.cvar, the Cvar_Set family and Cmd_AddCommand (cvar, cmd) and of
# getenv (env), in src/ at the upstream base commit, one "kind<TAB>name" a
# line, sorted. tests/check-cvar-docs.sh reads this file and needs no history;
# --verify-upstream regenerates it from the commit and diffs.
# Generate: tests/check-cvar-docs.sh --print-upstream-names > this file
# (git fetch --depth=1 origin $UPSTREAM_ID if the commit is missing).
# id: $UPSTREAM_ID
EOF
}

run_check() {
	mode=$1
	for f in doc/040_cvarlist.md doc/050_commands.md tests/baselines/undocumented-cvars.txt \
		tests/baselines/undocumented-commands.txt "$allow" "$upfile"; do
		[ -r "$f" ] || die "cannot read $f"
	done
	scan_tree . > "$tmp/calls"
	rc=0
	awk -v mode="$mode" -v f_calls="$tmp/calls" -v f_d40=doc/040_cvarlist.md -v f_d50=doc/050_commands.md \
		-v f_bc=tests/baselines/undocumented-cvars.txt -v f_bm=tests/baselines/undocumented-commands.txt \
		-v f_allow="$allow" -v f_up="$upfile" -v want_id="$UPSTREAM_ID" "$CLASSIFY_AWK" || rc=$?
	exit "$rc"
}

verify_upstream() {
	[ -r "$upfile" ] || die "cannot read $upfile"
	generate_upstream
	grep -vE '^#|^$' "$upfile" | sort -u > "${tmp:?}/have"
	if [ "$(sed -n 's/^# id: //p' "$upfile")" != "$UPSTREAM_ID" ]; then
		die "$upfile: its header does not name the upstream id $UPSTREAM_ID"
	fi
	if ! diff "${tmp:?}/have" "${tmp:?}/gen" > "${tmp:?}/diff"; then
		echo "check-cvar-docs: $upfile differs from src at $UPSTREAM_ID (< the file, > the commit); regenerate it with --print-upstream-names:"
		sed 's/^/  /' "${tmp:?}/diff"
		exit 1
	fi
	echo "check-cvar-docs: upstream names ok"
}

# ---- self-test -----------------------------------------------------------
# A fixture tree under a scratch directory, with its own throwaway git
# repository (no configuration of the user's or the system's is read, no
# hooks run, no template is copied) whose first commit is the "upstream
# base". Each case copies it, changes it and runs this script on the copy,
# asserting the verdict and the text the output must carry.

# isolate_git: no configuration, template, attribute file or repository
# variable of the caller (a hook runs a check with GIT_DIR set) reaches git.
isolate_git() {
	export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null GIT_TERMINAL_PROMPT=0 GIT_ATTR_NOSYSTEM=1
	export HOME=$st/home XDG_CONFIG_HOME=$st/xdg
	for v in $(git rev-parse --local-env-vars); do
		unset "$v"
	done
}

selftest() {
	st=$(mktemp -d "${TMPDIR:-/tmp}/cvardocs-self.XXXXXX")
	trap 'rm -rf "${st:?}"' EXIT
	: > "$st/.cvardocs-selftest"
	mkdir -p "$st/home" "$st/xdg"
	# The probe: a caller whose environment is hostile (a decoy repository
	# in GIT_DIR, a global, system and XDG configuration that sign every
	# commit and run a hook, a template directory with a hook). After
	# isolate_git a fresh repository with no template commits cleanly and
	# runs nothing. Then the fixture is built in the isolated environment.
	h=$st/hostile
	mkdir -p "$h/hooks" "$h/tmpl/hooks" "$h/xdg/git" "$h/decoy"
	printf '#!/bin/sh\n: > "%s/ran"\nexit 1\n' "$h" > "$h/hooks/pre-commit"
	cp "$h/hooks/pre-commit" "$h/hooks/post-commit"
	cp "$h/hooks/pre-commit" "$h/tmpl/hooks/pre-commit"
	chmod +x "$h/hooks/pre-commit" "$h/hooks/post-commit" "$h/tmpl/hooks/pre-commit"
	printf '[commit]\n\tgpgsign = true\n[gpg]\n\tprogram = /usr/bin/false\n[core]\n\thooksPath = %s/hooks\n[init]\n\ttemplateDir = %s/tmpl\n' "$h" "$h" > "$h/gitconfig"
	cp "$h/gitconfig" "$h/xdg/git/config"
	cp "$h/gitconfig" "$h/.gitconfig"
	git -C "$h/decoy" init -q --template= . || die "self-test: cannot make the decoy repository"
	probe_ok=1
	(
		export HOME=$h XDG_CONFIG_HOME=$h/xdg GIT_CONFIG_GLOBAL=$h/gitconfig GIT_CONFIG_SYSTEM=$h/gitconfig
		export GIT_DIR=$h/decoy/.git GIT_WORK_TREE=$h/decoy GIT_INDEX_FILE=$h/decoy/.git/index
		isolate_git
		mkdir "$st/probe" && cd "$st/probe" || exit 1
		git init -q . || exit 1
		[ "$(git rev-parse --git-dir)" = .git ] || exit 1
		: > f
		git add f || exit 1
		git -c user.name=st -c user.email=st@invalid commit -q -m probe || exit 1
		[ ! -e "$h/ran" ] || exit 1
	) || probe_ok=0
	if [ "$probe_ok" -eq 1 ]; then
		echo "  ok   git-isolation-under-a-hostile-environment: accepted"
	else
		die "self-test case git-isolation-under-a-hostile-environment: git read the caller's environment (a hook ran, a signature was asked for, or the repository was the decoy)"
	fi
	isolate_git
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
	printf 'void f(void) { cv = VidRules_Cvar(1); x = Cvar_Get(cv, "1", 0); }\n' > "$fx/src/client/menu/videomenu.c"
	cat > "$fx/src/common/up.c" <<'EOF'
static const char *items[] = {"a_bullets", "a_shells"};
void Up(void)
{
	Cvar_Get("up_cvar", "0", 0);
	Cmd_AddCommand("up_cmd", Up);
	getenv("UP_ENV");
}
EOF
	cat > "$fx/src/client/cl_main.c" <<'EOF'
typedef struct
{
	char *name;
	char *value;
} cheatvar_t;

cheatvar_t cheatvars[] = {
	{"ch_one", "1"},
	{NULL, NULL}
};

void F(void)
{
	v = Cvar_Get(cheatvars[n].name,
			cheatvars[n].value, 0);
}
EOF
	printf '* **vr_cvar**: x\n* **ch_one**: y\n' > "$fx/doc/040_cvarlist.md"
	printf 'nothing yet\n' > "$fx/doc/050_commands.md"
	printf 'up_cvar\n' > "$fx/tests/baselines/undocumented-cvars.txt"
	printf 'up_cmd\n' > "$fx/tests/baselines/undocumented-commands.txt"
	cat > "$fx/$allow" <<'EOF'
# path|callee|first argument|table|reason
src/client/cl_main.c|Cvar_Get|cheatvars[n].name|src/client/cl_main.c:cheatvars|the cheat table
src/client/menu/videomenu.c|Cvar_Get|cv|src/client/menu/vidrules.c:VidRules_Cvar|the video menu
EOF
	cp "$here/check-cvar-docs.sh" "$fx/tests/check-cvar-docs.sh"
	(
		cd "$fx" || exit 1
		git init -q --template= . &&
			git add -A &&
			git -c user.name=st -c user.email=st@invalid -c commit.gpgsign=false -c core.hooksPath=/dev/null \
				commit -q -m base
	) || die "self-test: cannot build the fixture repository"
	sha=$(git -C "$fx" rev-parse HEAD)
	CVARDOCS_SELFTEST=$st CVARDOCS_ROOT=$fx CVARDOCS_BASE=$sha sh "$here/check-cvar-docs.sh" --print-upstream-names > "$fx/$upfile" ||
		die "self-test: cannot generate the fixture's upstream names"
	(
		cd "$fx" || exit 1
		git add -A &&
			git -c user.name=st -c user.email=st@invalid -c commit.gpgsign=false -c core.hooksPath=/dev/null \
				commit -q -m names
	) || die "self-test: cannot commit the fixture's upstream names"
	n=0
	bad=0

	# case ID VERDICT EXPECT-TEXT SETUP: SETUP is a shell function run in the
	# copy. CASE_ARGS are the script's arguments, CASE_ENV the environment
	# the run gets (the self-test hooks by default). An accepted case's
	# output must be exactly EXPECT-TEXT; a refused one's must carry it.
	case_run() {
		cid=$1; want=$2; text=$3; setup=$4
		n=$((n + 1))
		d=$st/c$n
		cp -R "$fx" "$d"
		(cd "$d" && "$setup")
		rc=0
		cenv=$(printf '%s' "${CASE_ENV-CVARDOCS_SELFTEST=$st CVARDOCS_ROOT=@D@ CVARDOCS_BASE=$sha}" | sed "s|@D@|$d|g")
		out=$(env $cenv sh "$here/check-cvar-docs.sh" ${CASE_ARGS-} 2>&1) || rc=$?
		got=refused
		[ "$rc" -eq 0 ] && got=accepted
		ok=1
		[ "$got" = "$want" ] || ok=0
		if [ "$want" = accepted ]; then
			[ "$out" = "$text" ] || ok=0
		else
			printf '%s\n' "$out" | grep -qF -- "$text" || ok=0
		fi
		if [ "$ok" -eq 1 ]; then
			echo "  ok   $cid: $want"
		else
			echo "check-cvar-docs: self-test case $cid: expected $want with \"$text\", got $got (exit $rc):"
			printf '%s\n' "$out" | sed 's/^/    /'
			bad=1
		fi
		unset CASE_ENV CASE_ARGS
	}
	OK="check-cvar-docs: ok"

	s_none() { :; }
	s_m() { mkdir -p src/r; printf 'void f(void) { x = ri.Cvar_Get("zz_m", "0", 0); }\n' > src/r/a.m; }
	s_mm() { mkdir -p src/r; printf 'void f(void) { x = ri.Cvar_Get("zz_m", "0", 0); }\n' > src/r/a.mm; }
	s_h() { mkdir -p src/r; printf 'static inline void f(void) { x = ri.Cvar_Get("zz_m", "0", 0); }\n' > src/r/a.h; }
	s_nl() { printf 'void f(void)\n{\n\tx = Cvar_Get(\n\t\t"zz_nl", "0", 0);\n}\n' > src/common/nl.c; }
	s_nl_gi() { printf 'void f(void)\n{\n\tx = gi.cvar(\n\t\t"zz_gi", "0", 0);\n}\n' > src/common/nl.c; }
	s_dot_cvar() { printf 'void f(void) { x = gi.cvar("zz_gi", "0", 0); }\n' > src/common/nl.c; }
	s_var() { printf 'void f(void) { x = Cvar_Get(name, "0", 0); }\n' > src/common/v.c; }
	s_var_allow() {
		mkdir -p src/client/refresh
		printf 'void f(void) { x = ri.Cvar_Get(name, "0", 0); }\n' > src/client/refresh/rt.c
		printf 'src/client/refresh/rt.c|Cvar_Get|name|src/client/cl_main.c:cheatvars|the gate walks the cheat table\n' >> tests/baselines/nonliteral-calls.txt
	}
	s_var_allow_other() { s_var_allow; printf 'void f(void) { x = ri.Cvar_Get(other, "0", 0); }\n' > src/client/refresh/rt.c; }
	s_var_noreason() { s_var_allow; printf 'src/client/refresh/rt.c|Cvar_Get|name|src/client/cl_main.c:cheatvars|\n' > tests/baselines/nonliteral-calls.txt; }
	s_var_notable() { s_var_allow; printf 'src/client/refresh/rt.c|Cvar_Get|name|the gate walks its table\n' > tests/baselines/nonliteral-calls.txt; }
	s_table_empty() { s_var_allow; printf 'src/client/refresh/rt.c|Cvar_Get|name|src/client/cl_main.c:nosuchtable|a table that is not there\n' > tests/baselines/nonliteral-calls.txt; }
	s_cmd_m() { mkdir -p src/r; printf 'void f(void) { ri.Cmd_AddCommand("zz_cmd", f); }\n' > src/r/a.m; }
	s_cmd_nl() { printf 'void f(void)\n{\n\tCmd_AddCommand(\n\t\t"zz_cmd", f);\n}\n' > src/common/nl.c; }
	s_cmd_var() { printf 'void f(void) { Cmd_AddCommand(cmdname, f); }\n' > src/common/v.c; }
	s_cmd_var_allow() { mkdir -p src/client/refresh; printf 'void f(void) { ri.Cmd_AddCommand(name, f); }\n' > src/client/refresh/rt.c; }
	s_cmd_var_allow_listed() { s_cmd_var_allow; printf 'src/client/refresh/rt.c|Cmd_AddCommand|name|fmt:zz_cmd%%d:0:1|the same walk\n' >> tests/baselines/nonliteral-calls.txt; printf '* **zz_cmd0** / **zz_cmd1**: x.\n' >> doc/050_commands.md; }
	s_cmd_var_listed_for_cvar_only() { s_cmd_var_allow; printf 'src/client/refresh/rt.c|Cvar_Get|name|src/client/cl_main.c:cheatvars|the cvar walk\n' >> tests/baselines/nonliteral-calls.txt; }
	s_fork_base() { printf 'zz_fork\nup_cvar\n' > tests/baselines/undocumented-cvars.txt; printf 'void f(void) { Cvar_Get("zz_fork", "0", 0); }\n' > src/common/fork.c; }
	s_fork_base_cmd() { printf 'zz_forkcmd\nup_cmd\n' > tests/baselines/undocumented-commands.txt; printf 'void f(void) { Cmd_AddCommand("zz_forkcmd", f); }\n' > src/common/fork.c; }
	s_bullets() { printf 'up_cvar\na_bullets\n' > tests/baselines/undocumented-cvars.txt; printf 'void f(void) { Cvar_Get("a_bullets", "0", 0); }\n' > src/common/fork.c; }
	s_env_new() { printf 'void f(void) { getenv("ZZ_X"); }\n' > src/common/e.c; }
	s_env_doc() { s_env_new; printf '* **ZZ_X**: read by f.\n' >> doc/040_cvarlist.md; }
	s_prose_doc() { printf 'void f(void) { Cvar_Get("zz_doc", "0", 0); }\n' > src/common/d.c; printf 'The zz_doc cvar is mentioned in prose.\n' >> doc/040_cvarlist.md; }
	s_span_doc() { printf 'void f(void) { Cvar_Get("zz_doc", "0", 0); }\n' > src/common/d.c; printf '* **other**: see `zz_doc` and **zz_doc** here.\n\nSet `zz_doc` to 1.\n' >> doc/040_cvarlist.md; }
	s_prose_cmd() { printf 'void f(void) { Cmd_AddCommand("zz_cmd", f); }\n' > src/common/d.c; printf 'Use zz_cmd to do it.\n' >> doc/050_commands.md; }
	s_wrap_doc() { printf 'void f(void) { Cvar_Get("zz_doc", "0", 0); }\n' > src/common/d.c; printf '* **other**, **x**,\n  **zz_doc <val>**: wrapped.\n' >> doc/040_cvarlist.md; }
	s_nested_doc() { printf 'void f(void) { Cvar_Get("zz_doc", "0", 0); }\n' > src/common/d.c; printf '* **zz_parent**: x.\n  - **zz_doc**: nested.\n' >> doc/040_cvarlist.md; }
	s_nested_star_doc() { printf 'void f(void) { Cvar_Get("zz_doc", "0", 0); }\n' > src/common/d.c; printf '* **zz_parent**: x.\n  * **zz_doc**: nested.\n' >> doc/040_cvarlist.md; }
	s_nested_cmd() { printf 'void f(void) { Cmd_AddCommand("zz_cmd", f); }\n' > src/common/d.c; printf '* **ogg**: x.\n  - **zz_cmd**: a subcommand.\n' >> doc/050_commands.md; }
	s_nocolon_next() { printf 'void f(void) { Cvar_Get("zz_a", "0", 0); }\n' > src/common/d.c; printf '* **zz_a** is said here\n* **other**: y.\n' >> doc/040_cvarlist.md; }
	s_giveup_prose() { printf 'void f(void) { Cvar_Get("zz_b", "0", 0); }\n' > src/common/d.c; printf '* **other** one\n  two\n  three\n  and **zz_b** more\n\n' >> doc/040_cvarlist.md; }
	s_lone_struct() {
		printf 'void f(void) { x = Cvar_Get(name, "0", 0); }\n' > src/common/v.c
		printf 'static zzinfo_t zz_one = { "zz_one", "zz label", "zz def" };\n' > src/common/t.c
		printf 'src/common/v.c|Cvar_Get|name|src/common/t.c:zz_one|one struct\n' >> tests/baselines/nonliteral-calls.txt
	}
	s_lone_struct_doc() { s_lone_struct; printf '* **zz_one**: x.\n' >> doc/040_cvarlist.md; }
	s_fn_table() {
		printf 'void f(void) { x = Cvar_Get(name, "0", 0); }\n' > src/common/v.c
		printf 'const char *zz_fn(int i)\n{\n\tif (i) return "zz_f2";\n\treturn "zz_f1";\n}\n' > src/common/t.c
		printf 'src/common/v.c|Cvar_Get|name|src/common/t.c:zz_fn|a function\n' >> tests/baselines/nonliteral-calls.txt
	}
	s_fn_table_first() { s_fn_table; printf '* **zz_f1**: x.\n' >> doc/040_cvarlist.md; }
	s_literal_doc() { printf 'void f(void) { Cvar_Get("zz_doc", "0", 0); }\n' > src/common/d.c; printf '* **zz_doc**: documented.\n' >> doc/040_cvarlist.md; }
	s_cmt_block() { printf '/* Cvar_Get("zz_c", "0", 0); */\n' > src/common/c.c; }
	s_cmt_line() { printf '// Cvar_Get("zz_c", "0", 0);\n' > src/common/c.c; }
	s_odd() { printf 'void f(void) { Cvar_Get("zz odd", "0", 0); }\n' > src/common/o.c; }
	s_proto() { printf 'cvar_t *Cvar_Get(const char *var_name, const char *value, int flags);\nvoid Cmd_AddCommand(const char *cmd_name, xcommand_t function);\n' > src/common/p.h; }
	s_no_baseline() { rm -f tests/baselines/undocumented-cvars.txt; }
	s_no_allow() { rm -f tests/baselines/nonliteral-calls.txt; }
	s_no_upfile() { rm -f "$upfile"; }
	s_up_wrong_id() { sed 's/^# id: .*/# id: 0000000000000000000000000000000000000000/' "$upfile" > "$upfile.n"; mv "$upfile.n" "$upfile"; }
	s_up_empty() { grep '^#' "$upfile" > "$upfile.n"; mv "$upfile.n" "$upfile"; }
	s_cheat_row() { sed 's/{NULL, NULL}/{"zz_cheat_undoc", "0"},\n\t{NULL, NULL}/' src/client/cl_main.c > src/client/cl_main.n; mv src/client/cl_main.n src/client/cl_main.c; }
	s_cheat_row_doc() { s_cheat_row; printf '* **zz_cheat_undoc**: documented.\n' >> doc/040_cvarlist.md; }
	s_cheat_row_split() { sed 's/{NULL, NULL}/{\n\t\t"zz_cheat_undoc", "0"},\n\t{NULL, NULL}/' src/client/cl_main.c > src/client/cl_main.n; mv src/client/cl_main.n src/client/cl_main.c; }
	s_allow_stale() { printf 'src/client/refresh/rm_main.c|Cvar_Get|name|src/client/cl_main.c:cheatvars|the gate walks its table\n' >> tests/baselines/nonliteral-calls.txt; }
	s_rm_walk_nolist() {
		mkdir -p src/client/refresh
		printf 'static const char *tab[] = {\n\t"zz_rm_undoc",\n};\nvoid f(void)\n{\n\tfor (i = 0; i < 1; i++) ri.Cvar_Get(name, "0", 0);\n}\n' > src/client/refresh/rm_main.c
	}
	s_rm_walk_listed() {
		s_rm_walk_nolist
		printf 'src/client/refresh/rm_main.c|Cvar_Get|name|src/client/refresh/rm_main.c:tab|the gate walks its table\n' >> tests/baselines/nonliteral-calls.txt
	}
	s_rm_walk_listed_doc() { s_rm_walk_listed; printf '* **zz_rm_undoc**: documented.\n' >> doc/040_cvarlist.md; }
	s_set() { printf 'void f(void) { ri.Cvar_Set("zz_set", "1"); }\n' > src/common/s.c; }
	s_set_doc() { s_set; printf '* **zz_set**: documented.\n' >> doc/040_cvarlist.md; }
	s_set_upstream() { printf 'void f(void) { Cvar_Set("up_cvar", "1"); }\n' > src/common/s.c; }
	s_set_var() { printf 'void f(void) { Cvar_Set(name, "1"); gi.cvar_set(var->name, "1"); }\n' > src/common/s.c; }
	s_set_value() { printf 'void f(void) { Cvar_SetValue("zz_set", 1); }\n' > src/common/s.c; }
	s_set_force() { printf 'void f(void) { Cvar_ForceSet("zz_set", "1"); }\n' > src/common/s.c; }
	s_set_full() { printf 'void f(void) { Cvar_FullSet("zz_set", "1", 0); }\n' > src/common/s.c; }
	s_set_gi() { printf 'void f(void) { gi.cvar_set("zz_gset", "1"); }\n' > src/common/s.c; }
	s_set_giforce() { printf 'void f(void) { gi.cvar_forceset("zz_gforce", "1"); }\n' > src/common/s.c; }
	s_set_gisetvalue() { printf 'void f(void) { gi.cvar_setvalue("zz_gval", 1); }\n' > src/common/s.c; }
	s_set_nl() { printf 'void f(void)\n{\n\tri.Cvar_Set(\n\t\t"zz_set", "1");\n}\n' > src/common/s.c; }

	s_flat_arr() {
		mkdir -p src/client/refresh
		printf 'static const char *rt_gate_cvars[] = {"zz_rt_a", "zz_rt_b", "zz_rt_c"};\nvoid f(void) { for (i = 0; i < 3; i++) ri.Cvar_Get(rt_gate_cvars[i], "0", 0); }\n' > src/client/refresh/rt.c
		printf 'src/client/refresh/rt.c|Cvar_Get|rt_gate_cvars[i]|src/client/refresh/rt.c:rt_gate_cvars|the gate walks its table\n' >> tests/baselines/nonliteral-calls.txt
	}
	s_flat_arr_lines() {
		mkdir -p src/client/refresh
		printf 'static const char *rt_gate_cvars[] = {\n\t"zz_rt_a",\n\t"zz_rt_b",\n\t"zz_rt_c"\n};\nvoid f(void) { for (i = 0; i < 3; i++) ri.Cvar_Get(rt_gate_cvars[i], "0", 0); }\n' > src/client/refresh/rt.c
		printf 'src/client/refresh/rt.c|Cvar_Get|rt_gate_cvars[i]|src/client/refresh/rt.c:rt_gate_cvars|the gate walks its table\n' >> tests/baselines/nonliteral-calls.txt
	}
	s_flat_arr_late() { s_flat_arr; printf '* **zz_rt_a** / **zz_rt_b**: x.\n' >> doc/040_cvarlist.md; }
	s_flat_arr_lines_first() { s_flat_arr_lines; printf '* **zz_rt_a**: x.\n' >> doc/040_cvarlist.md; }
	s_flat_arr_doc() { s_flat_arr; printf '* **zz_rt_a**, **zz_rt_b**, **zz_rt_c**: x.\n' >> doc/040_cvarlist.md; }
	s_flat_arr_lines_doc() { s_flat_arr_lines; printf '* **zz_rt_a**, **zz_rt_b**, **zz_rt_c**: x.\n' >> doc/040_cvarlist.md; }
	s_prefix_macro() { printf 'void f(void) { x = Cvar_Get("zz_" SUFFIX, "0", 0); }\n' > src/common/v.c; printf '* **zz_**: x.\n' >> doc/040_cvarlist.md; }
	s_prefix_two() { printf 'void f(void) { x = Cvar_Get("zz_a" "zz_b", "0", 0); }\n' > src/common/v.c; printf '* **zz_a** / **zz_b**: x.\n' >> doc/040_cvarlist.md; }
	s_prefix_cmd() { printf 'void f(void) { Cmd_AddCommand("zz_" "c", f); }\n' > src/common/v.c; printf '* **zz_** / **zz_c**: x.\n' >> doc/050_commands.md; }
	s_blank_comma() { printf 'void f(void) { Cvar_Get("zz_doc" , "0", 0); }\n' > src/common/d.c; printf '* **zz_doc**: documented.\n' >> doc/040_cvarlist.md; }
	s_plus_cmd() { printf 'void f(void) { Cmd_AddCommand("+zz_cmd", f); }\n' > src/common/v.c; }
	s_plus_cmd_doc() { s_plus_cmd; printf '* **+zz_cmd**: x.\n' >> doc/050_commands.md; }
	s_adr_walk() {
		mkdir -p src/client/header src/client/menu
		printf '#define NUM_ADDRESSBOOK_ENTRIES 9\n' > src/client/header/client.h
		printf 'void f(void) { Com_sprintf(buffer, sizeof(buffer), "adr%%d", i); Cvar_Get(buffer, "", 0); }\n' > src/client/menu/ab.c
		printf 'src/client/menu/ab.c|Cvar_Get|buffer|fmt:adr%%d:0:NUM_ADDRESSBOOK_ENTRIES-1|the address book\n' >> tests/baselines/nonliteral-calls.txt
	}
	s_adr_doc() { s_adr_walk; printf '* **adr0**, **adr1**, **adr2**, **adr3**, **adr4**, **adr5**, **adr6**,\n  **adr7**, **adr8**: x.\n' >> doc/040_cvarlist.md; }
	s_adr_undoc() { s_adr_walk; printf '* **adr0**, **adr1**, **adr2**, **adr3**, **adr4**, **adr5**, **adr6**,\n  **adr7**: x.\n' >> doc/040_cvarlist.md; }
	s_adr_nomacro() { s_adr_doc; rm src/client/header/client.h; }
	s_adr_notint() { s_adr_doc; printf '#define NUM_ADDRESSBOOK_ENTRIES (4 + 5)\n' > src/client/header/client.h; }

	echo "check-cvar-docs: self-test"
	case_run control accepted "$OK" s_none
	case_run m-file refused zz_m s_m
	case_run mm-file refused zz_m s_mm
	case_run h-file refused zz_m s_h
	case_run cvar-name-on-next-line refused zz_nl s_nl
	case_run gi.cvar-name-on-next-line refused zz_gi s_nl_gi
	case_run gi.cvar-one-line refused zz_gi s_dot_cvar
	case_run cvar-variable-arg refused "src/common/v.c: Cvar_Get(name" s_var
	case_run cvar-variable-arg-allow-listed accepted "$OK" s_var_allow
	case_run cvar-variable-arg-other-arg-not-listed refused "rt.c: Cvar_Get(other" s_var_allow_other
	case_run allow-list-line-without-reason refused "path|callee|first argument|table|reason" s_var_noreason
	case_run allow-list-line-without-table refused "path|callee|first argument|table|reason" s_var_notable
	case_run allow-list-table-gives-no-name refused "gives no name" s_table_empty
	case_run allow-entry-matches-no-call refused "rm_main.c|Cvar_Get|name" s_allow_stale
	case_run cheat-row-undocumented refused zz_cheat_undoc s_cheat_row
	case_run cheat-row-on-the-next-line-undocumented refused zz_cheat_undoc s_cheat_row_split
	case_run cheat-row-documented accepted "$OK" s_cheat_row_doc
	case_run rm_main-walk-without-an-entry refused "rm_main.c: Cvar_Get(name" s_rm_walk_nolist
	case_run rm_main-walk-with-an-entry-undocumented-name refused zz_rm_undoc s_rm_walk_listed
	case_run rm_main-walk-with-an-entry-documented-name accepted "$OK" s_rm_walk_listed_doc
	case_run flat-array-later-element-undocumented-one-line refused zz_rt_c s_flat_arr_late
	case_run flat-array-later-element-undocumented-per-line refused zz_rt_b s_flat_arr_lines_first
	case_run flat-array-all-documented-one-line accepted "$OK" s_flat_arr_doc
	case_run flat-array-all-documented-per-line accepted "$OK" s_flat_arr_lines_doc
	case_run literal-prefix-then-macro-is-not-a-literal refused 'v.c: Cvar_Get("zz_" SUFFIX' s_prefix_macro
	case_run two-adjacent-literals-are-not-a-literal refused 'v.c: Cvar_Get("zz_a" "zz_b"' s_prefix_two
	case_run command-literal-prefix-is-not-a-literal refused 'v.c: Cmd_AddCommand("zz_" "c"' s_prefix_cmd
	case_run literal-then-blank-then-comma-documented accepted "$OK" s_blank_comma
	case_run command-with-plus-undocumented refused +zz_cmd s_plus_cmd
	case_run command-with-plus-documented accepted "$OK" s_plus_cmd_doc
	case_run fmt-macro-bound-documented-adr8 accepted "$OK" s_adr_doc
	case_run fmt-macro-bound-undocumented-adr8 refused adr8 s_adr_undoc
	case_run fmt-macro-missing refused "no #define of NUM_ADDRESSBOOK_ENTRIES" s_adr_nomacro
	case_run fmt-macro-not-an-integer refused "NUM_ADDRESSBOOK_ENTRIES is not a plain integer" s_adr_notint
	case_run cmd-m-file refused zz_cmd s_cmd_m
	case_run cmd-name-on-next-line refused zz_cmd s_cmd_nl
	case_run cmd-variable-arg refused "src/common/v.c: Cmd_AddCommand(cmdname" s_cmd_var
	case_run cmd-variable-arg-allow-listed accepted "$OK" s_cmd_var_allow_listed
	case_run cmd-variable-arg-listed-for-cvar-only refused "rt.c: Cmd_AddCommand(name" s_cmd_var_listed_for_cvar_only
	case_run set-undocumented refused zz_set s_set
	case_run set-name-on-next-line refused zz_set s_set_nl
	case_run set-documented accepted "$OK" s_set_doc
	case_run set-upstream-name accepted "$OK" s_set_upstream
	case_run set-variable-name-not-read accepted "$OK" s_set_var
	case_run set-value-undocumented refused zz_set s_set_value
	case_run forceset-undocumented refused zz_set s_set_force
	case_run fullset-undocumented refused zz_set s_set_full
	case_run gi.cvar_set-undocumented refused zz_gset s_set_gi
	case_run gi.cvar_forceset-undocumented refused zz_gforce s_set_giforce
	case_run gi.cvar_setvalue-undocumented refused zz_gval s_set_gisetvalue
	case_run baselined-cvar-not-upstream refused "undocumented-cvars.txt:1: zz_fork" s_fork_base
	case_run baselined-command-not-upstream refused "undocumented-commands.txt:1: zz_forkcmd" s_fork_base_cmd
	case_run baselined-fork-cvar-named-like-an-upstream-string refused "undocumented-cvars.txt:2: a_bullets" s_bullets
	case_run baselined-upstream-name accepted "$OK" s_none
	case_run getenv-new-undocumented refused ZZ_X s_env_new
	case_run getenv-new-documented accepted "$OK" s_env_doc
	case_run getenv-upstream accepted "$OK" s_none
	case_run prose-mention-is-not-documentation refused zz_doc s_prose_doc
	case_run prose-mention-of-a-command-is-not-documentation refused zz_cmd s_prose_cmd
	case_run code-span-in-prose-is-not-documentation refused zz_doc s_span_doc
	case_run wrapped-entry-header-is-documentation accepted "$OK" s_wrap_doc
	case_run nested-entry-is-documentation accepted "$OK" s_nested_doc
	case_run nested-star-entry-is-documentation accepted "$OK" s_nested_star_doc
	case_run nested-item-in-the-commands-doc-is-not-a-command refused zz_cmd s_nested_cmd
	case_run bullet-with-no-colon-is-not-documentation refused zz_a s_nocolon_next
	case_run header-that-gives-up-then-bold-prose refused zz_b s_giveup_prose
	case_run lone-struct-counts-only-its-first-literal accepted "$OK" s_lone_struct_doc
	case_run function-table-counts-every-literal refused zz_f2 s_fn_table_first
	case_run literal-documented-one-line accepted "$OK" s_literal_doc
	case_run commented-out-block-counts refused zz_c s_cmt_block
	case_run commented-out-line-counts refused zz_c s_cmt_line
	case_run name-with-a-space refused "is not a name the check can read" s_odd
	case_run prototypes-are-not-calls accepted "$OK" s_proto
	case_run baseline-unreadable refused "cannot read tests/baselines/undocumented-cvars.txt" s_no_baseline
	case_run allow-list-unreadable refused "cannot read tests/baselines/nonliteral-calls.txt" s_no_allow
	case_run upstream-file-unreadable refused "cannot read tests/baselines/upstream-names.txt" s_no_upfile
	case_run upstream-file-names-another-id refused "its header names the upstream id" s_up_wrong_id
	case_run upstream-file-without-names refused "no names in tests/baselines/upstream-names.txt" s_up_empty
	CASE_ENV="CVARDOCS_BASE=HEAD" case_run plain-run-with-CVARDOCS_BASE refused "CVARDOCS_BASE is set in the environment" s_none
	CASE_ENV="CVARDOCS_ROOT=/nonexistent" case_run plain-run-with-CVARDOCS_ROOT refused "CVARDOCS_ROOT is set in the environment" s_none
	CASE_ENV="CVARDOCS_SELFTEST=/nonexistent" case_run plain-run-with-a-forged-CVARDOCS_SELFTEST refused "CVARDOCS_SELFTEST is set in the environment" s_none
	# the Stop hook's shape: an archive of HEAD with no history, plain run
	n=$((n + 1))
	d=$st/c$n
	mkdir "$d"
	git -C "$fx" archive HEAD | tar -x -C "$d"
	rc=0
	out=$(cd "$st" && env -i PATH="$PATH" HOME="$st/home" CVARDOCS_SELFTEST="$st" CVARDOCS_ROOT="$d" CVARDOCS_BASE="$sha" sh "$d/tests/check-cvar-docs.sh" 2>&1) || rc=$?
	if [ "$rc" -eq 0 ] && [ "$out" = "$OK" ] && [ ! -e "$d/.git" ]; then
		echo "  ok   archive-of-head-with-no-history: accepted"
	else
		echo "check-cvar-docs: self-test case archive-of-head-with-no-history: expected accepted, got exit $rc: $out"
		bad=1
	fi
	CASE_ARGS=--verify-upstream case_run verify-upstream-matches accepted "check-cvar-docs: upstream names ok" s_none
	CASE_ARGS=--verify-upstream case_run verify-upstream-name-removed-from-file refused "< " s_up_drop
	CASE_ARGS=--verify-upstream case_run verify-upstream-name-added-to-file refused "< " s_up_add
	CASE_ARGS=--verify-upstream case_run verify-upstream-file-names-another-id refused "its header does not name the upstream id" s_up_wrong_id
	CASE_ARGS=--verify-upstream CASE_ENV="CVARDOCS_SELFTEST=$st CVARDOCS_ROOT=@D@ CVARDOCS_BASE=0000000000000000000000000000000000000000" case_run verify-upstream-commit-missing refused "git fetch --depth=1 origin 0000000000000000000000000000000000000000" s_none
	# the repository variables of a caller do not reach git: the commit is
	# in the fixture, not in the decoy
	mkdir "$st/decoy"
	git -C "$st/decoy" init -q --template= .
	CASE_ARGS=--verify-upstream CASE_ENV="GIT_DIR=$st/decoy/.git GIT_WORK_TREE=$st/decoy GIT_INDEX_FILE=$st/decoy/.git/idx CVARDOCS_SELFTEST=$st CVARDOCS_ROOT=@D@ CVARDOCS_BASE=$sha" case_run verify-upstream-with-a-decoy-GIT_DIR accepted "check-cvar-docs: upstream names ok" s_none
	# git failing is not "no match": a git that finds the commit and then
	# exits 3 on the archive stops the check
	mkdir -p "$st/fakebin"
	printf '#!/bin/sh\nfor a in "$@"; do case "$a" in cat-file) exit 0 ;; esac; done\nexit 3\n' > "$st/fakebin/git"
	chmod +x "$st/fakebin/git"
	n=$((n + 1))
	cp -R "$fx" "$st/c$n"
	rc=0
	out=$(PATH="$st/fakebin:$PATH" CVARDOCS_SELFTEST=$st CVARDOCS_ROOT=$st/c$n CVARDOCS_BASE=$sha sh "$here/check-cvar-docs.sh" --verify-upstream 2>&1) || rc=$?
	if [ "$rc" -ne 0 ] && printf '%s\n' "$out" | grep -qF "git archive of src"; then
		echo "  ok   git-fails: refused"
	else
		echo "check-cvar-docs: self-test case git-fails: expected refused, got exit $rc: $out"
		bad=1
	fi
	[ "$bad" -eq 0 ] || die "self-test failed"
	echo "check-cvar-docs: self-test ok ($n cases)"
}

s_up_drop() { grep -v 'up_cmd' "$upfile" > "$upfile.n"; mv "$upfile.n" "$upfile"; }
s_up_add() { printf 'cvar\tzz_fork_upstream\n' >> "$upfile"; }

case "${1:-}" in
--selftest) selftest; exit 0 ;;
esac

root=${CVARDOCS_ROOT:-$here/..}
cd "$root" || die "cannot enter $root"
root=$(pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cvardocs.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT
case "${1:-}" in
"") run_check check ;;
--verify-upstream) verify_upstream ;;
--print-upstream-names)
	generate_upstream
	upstream_header
	cat "${tmp:?}/gen"
	;;
--print-baseline)
	case "${2:-}" in
	cvars) run_check print-cvars ;;
	commands) run_check print-commands ;;
	*) echo "usage: $0 --print-baseline cvars|commands" >&2; exit 2 ;;
	esac
	;;
*) echo "usage: $0 [--selftest | --verify-upstream | --print-upstream-names | --print-baseline cvars|commands]" >&2; exit 2 ;;
esac
