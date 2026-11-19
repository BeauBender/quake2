#!/bin/sh
# Invariant (FIX115, review findings AR-01 and AR-02): CI runs the gate's
# invariants, and a check that cannot run says so as a failure wherever it is
# required to run.
#
# (a) Every tests/check-*.sh is named in exactly one of two lists:
#     tests/ci-checks.txt, the checks the invariants job of
#     .github/workflows/macos.yml runs (through scripts/ci-checks.sh), one
#     name per line; or tests/ci-local-only.txt, the checks that run on the
#     build host's gate only, one "name: reason" per line, the reason not
#     empty. Every name in a list is an existing check, and is listed once.
#     So a new check is refused until it is placed in one of them. Both lists
#     are read alike: a "#" starts a comment, blanks (space, tab) around a
#     name are dropped, blank lines are skipped, and a carriage return
#     anywhere is refused. A line of tests/ci-checks.txt is one name, with no
#     blank and no ":" in it; scripts/ci-checks.sh --list prints the names it
#     reads, and they must be the names read here.
# (b) A job of .github/workflows/macos.yml runs scripts/ci-checks.sh, and
#     every job that does has no job-level if: or continue-on-error:, checks
#     out the full history (fetch-depth: 0 on each of its actions/checkout
#     steps; tests/check-cvar-docs.sh reads the upstream base commit and
#     fails on a shallow clone), and runs it in a step that has neither key
#     either and gets CHECK_IOS_REQUIRE: 1 from its own env: or the job's.
# (c) Every skip line of a tests/check-*.sh is paired with a guard that turns
#     the skip into a failure, or is allowed by tests/ci-skip-allow.txt with
#     a reason. A skip line is a command line, not a comment and not in a
#     here-document, that prints text holding "skip" in any case through
#     echo, printf or a function of the file that prints its arguments,
#     whatever redirection comes first (>&2 echo), to the terminal (not into
#     a file or a pipe), and is not a failure (no exit or return with a
#     non-zero status, and no fail*= assignment, on that line or the next).
#     The guard is a [ or test command that reads a <NAME>_REQUIRE variable
#     as an expansion (${X_REQUIRE or $X_REQUIRE) on that line or above it
#     in the same branch (up to the if, else, case arm or { that opens it,
#     never past a closed one, at most 15 lines); the name inside a string
#     does not count. An allow-list entry that matches no skip line is
#     refused, as is a quote the reader cannot close.
#
# Then a self-test on scratch copies of those files (never the real tree):
# the copy passes, a guarded skip passes, and each mutant is refused (see
# the list below).
#
# Needs only sh, awk, sed, grep and cp. Usage: tests/check-ci-parity.sh
set -eu
me=check-ci-parity
cd "$(dirname "$0")/.."

# list_check <file> <ci|local>: prints every malformed line of a list
list_check() {
	awk -v kind="$2" '
		/\r/ { print "line " NR ": a carriage return"; next }
		{
			s = $0
			sub(/#.*/, "", s)
			sub(/^[ \t]+/, "", s)
			sub(/[ \t]+$/, "", s)
			if (s == "")
				next
			if (kind == "ci") {
				if (s ~ /[ \t:]/)
					print "line " NR ": \"" s "\" is not one name (no blank, no \":\")"
				next
			}
			n = index(s, ":")
			r = n ? substr(s, n + 1) : ""
			gsub(/[ \t]/, "", r)
			nm = n ? substr(s, 1, n - 1) : s
			sub(/[ \t]+$/, "", nm)
			if (r == "")
				print "line " NR ": " nm " has no reason (name: reason)"
			else if (nm ~ /[ \t]/ || nm == "")
				print "line " NR ": \"" nm "\" is not one name"
		}' "$1"
}

# list_names <file> <ci|local>: the names of a list (the part before ":" of
# a tests/ci-local-only.txt line)
list_names() {
	awk -v kind="$2" '
		/\r/ { next }
		{
			sub(/#.*/, "")
			if (kind != "ci")
				sub(/:.*/, "")
			sub(/^[ \t]+/, "")
			sub(/[ \t]+$/, "")
			if ($0 != "")
				print
		}' "$1"
}

# skips <file> <allow-list>: "<line>: <text>" for every skip line with no
# guard and no allow-list entry; "allow: ..." for an entry of this file that
# matches no skip line; "0: ..." when the file cannot be read
skips() {
	awk -v name="${1##*/}" '
		function endseg(t, w, out) {
			t = cur
			cur = ""
			out = !rd && !pipe
			rd = 0
			pipe = 0
			sub(/\$$/, "", t)
			for (;;) {
				sub(/^[ \t\n]+/, "", t)
				if (t ~ /^fail[A-Za-z]*=/ || t ~ /^(exit|return)[ \t]+([1-9]|\$)/)
					F[cl] = 1
				if (t ~ /^(exit|return)[ \t]+0/)
					X[cl] = 1
				if (match(t, /^(then|else|elif|do|if|while|until|!)([ \t\n]|$)/))
					t = substr(t, RLENGTH + 1)
				else if (match(t, /^[0-9]*[<>]+&?[0-9-]*[ \t]+/))
					t = substr(t, RLENGTH + 1)
				else if (match(t, /^[A-Za-z_][A-Za-z0-9_]*=[^ \t\n]*[ \t]+/))
					t = substr(t, RLENGTH + 1)
				else
					break
			}
			w = t
			sub(/[ \t\n].*/, "", w)
			if (w == "[" || w == "[[" || w == "test") {
				if (t ~ /\$\{?[A-Z][A-Z0-9_]*_REQUIRE/)
					G[cl] = 1
				return
			}
			if (out && (w == "echo" || w == "printf" || (w in H))) {
				t = substr(t, length(w) + 1)
				gsub(/\$\{[^}]*\}/, "", t)
				gsub(/\$[A-Za-z_][A-Za-z0-9_]*/, "", t)
				if (tolower(t) ~ /skip/)
					S[cl] = 1
			}
		}
		function isclose(s) {
			return s ~ /^[ \t]*(fi|done|esac|\}|;;)([ \t;)]|$)/ || s ~ /;;[ \t]*$/
		}
		function isopen(s) {
			return s ~ /(^|[ \t;])(then|else|do|\{)[ \t]*$/ || s ~ /^[ \t]*(else|elif)([ \t]|$)/ || s ~ /^[ \t]*[^ \t(#][^()]*\)[ \t]*$/
		}
		FILENAME == ARGV[1] {
			if ($0 ~ /^[ \t]*(#|$)/)
				next
			n = index($0, "|")
			if (!n)
				next
			r = substr($0, n + 1)
			m = index(r, "|")
			if (m && substr($0, 1, n - 1) == name) {
				na++
				ap[na] = substr(r, 1, m - 1)
			}
			next
		}
		{ L[++nl] = $0 }
		END {
			# helpers: the functions of the file that print their arguments
			for (i = 1; i <= nl; i++) {
				if (!match(L[i], /^[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*\(\)/))
					continue
				fn = substr(L[i], RSTART, RLENGTH)
				gsub(/[ \t()]/, "", fn)
				body = L[i]
				if (L[i] !~ /\}[ \t;]*$/)
					for (j = i + 1; j <= nl && j <= i + 40; j++) {
						body = body "\n" L[j]
						if (L[j] ~ /^[ \t]*\}[ \t]*$/)
							break
					}
				if (body ~ /(echo|printf)[^\n]*\$[*@1]/ && body !~ /fail[A-Za-z]*=|exit[ \t]+1/)
					H[fn] = 1
			}
			q = ""
			hd = ""
			cur = ""
			cl = 0
			for (i = 1; i <= nl; i++) {
				s = L[i]
				if (hd != "") {
					t = s
					if (hdt)
						sub(/^\t+/, "", t)
					if (t == hd)
						hd = ""
					continue
				}
				lq[i] = q
				cont = 0
				hp = ""
				if (q == "" && match(s, /<<-?[ \t]*["\047]?[A-Za-z_][A-Za-z0-9_]*/)) {
					hp = substr(s, RSTART, RLENGTH)
					hdt = (hp ~ /^<<-/)
					sub(/^<<-?[ \t]*["\047]?/, "", hp)
				}
				for (k = 1; k <= length(s); k++) {
					c = substr(s, k, 1)
					if (q == "" && cur ~ /^[ \t\n]*$/ && c !~ /[ \t]/)
						cl = i
					if (q == "\047") {
						cur = cur c
						if (c == "\047")
							q = ""
						continue
					}
					if (q == "\"") {
						cur = cur c
						if (c == "\\") {
							k++
							cur = cur substr(s, k, 1)
						} else if (c == "\"")
							q = ""
						continue
					}
					if (c == "\\" && k == length(s)) {
						cont = 1
						continue
					}
					if (c == "\\") {
						cur = cur c substr(s, k + 1, 1)
						k++
						continue
					}
					if (c == "\047" || c == "\"") {
						q = c
						cur = cur c
						continue
					}
					if (c == "#" && (cur == "" || cur ~ /[ \t\n]$/))
						break
					if (c == "$" && substr(s, k + 1, 1) == "{" && (j = index(substr(s, k), "}"))) {
						cur = cur substr(s, k, j)
						k += j - 1
						continue
					}
					if (c == ">" && substr(s, k + 1, 1) != "&")
						rd = 1
					if ((c == "|" || c == "&") && substr(s, k + 1, 1) == c) {
						endseg()
						k++
						continue
					}
					if (c == "|")
						pipe = 1
					if (c == "&" && cur ~ /[<>]$/) {
						cur = cur c
						continue
					}
					if (index(";|&(){}", c)) {
						endseg()
						continue
					}
					cur = cur c
				}
				if (q == "" && !cont)
					endseg()
				else
					cur = cur "\n"
				if (hp != "")
					hd = hp
			}
			if (q != "")
				print "0: a quote opened here is never closed, so the skip lines cannot be read"
			for (i = 1; i <= nl; i++) {
				if ((i in F) || (!(i in X) && ((i + 1) in F) && !isopen(L[i + 1]) && !isclose(L[i + 1])))
					continue
				if (!(i in S))
					continue
				al = 0
				for (k = 1; k <= na; k++)
					if (index(L[i], ap[k])) {
						al = 1
						used[k] = 1
					}
				ok = 0
				for (j = i; j >= 1 && j >= i - 15; j--) {
					if (j < i && lq[j] == "" && isclose(L[j]))
						break
					if (j in G) {
						ok = 1
						break
					}
					if (j < i && lq[j] == "" && isopen(L[j]))
						break
				}
				if (!ok && !al) {
					s = L[i]
					sub(/^[ \t]*/, "", s)
					print i ": " s
				}
			}
			for (k = 1; k <= na; k++)
				if (!(k in used))
					print "allow: the entry \"" ap[k] "\" matches no skip line"
		}
	' "$2" "$1"
}

# jobs_ok <workflow>: the invariants job is there, unconditional, deep and
# requires iOS where scripts/ci-checks.sh runs
jobs_ok() {
	awk '
		function err(m) { print "job " job ": " m; bad = 1 }
		function endstep() {
			if (sl && srun) {
				rs++
				if (sif) err("the step that runs scripts/ci-checks.sh has an if: (a condition can skip it)")
				if (scoe) err("the step that runs scripts/ci-checks.sh has continue-on-error: (its failure would not fail the job)")
				if (sios) ios = 1
			}
			if (sl && sco) {
				nco++
				if (!sdepth) shallow = 1
			}
			sl = 0; srun = 0; sif = 0; scoe = 0; sios = 0; sco = 0; sdepth = 0; smap = ""
		}
		function finish() {
			endstep()
			if (job != "" && run) {
				runs++
				if (jif) err("runs scripts/ci-checks.sh under a job-level if: (a condition can skip it)")
				if (jcoe) err("runs scripts/ci-checks.sh with a job-level continue-on-error: (its failure would not fail the run)")
				if (!rs) err("runs scripts/ci-checks.sh outside a step this check can read")
				if (!nco || shallow) err("runs scripts/ci-checks.sh on a shallow checkout (no fetch-depth: 0 on its actions/checkout step)")
				if (!ios && !jios) err("runs scripts/ci-checks.sh without CHECK_IOS_REQUIRE: 1 in that step or job env:")
			}
			job = ""; run = 0; jif = 0; jcoe = 0; jios = 0; jmap = ""; rs = 0; nco = 0; shallow = 0; ios = 0
		}
		/^[ \t]*#/ { next }
		/^jobs:/ { injobs = 1; next }
		injobs && /^  [A-Za-z0-9_-]+:[ \t]*$/ { finish(); job = $1; sub(/:$/, "", job); next }
		injobs && /^[^ \t]/ { finish(); injobs = 0 }
		job == "" { next }
		/scripts\/ci-checks\.sh/ { run = 1; if (sl) srun = 1 }
		/^    - / {
			endstep()
			if (jmap == "steps") sl = NR
			s = $0
			sub(/^    - /, "      ", s)
		}
		!/^    - / { s = $0 }
		s ~ /^    [A-Za-z0-9_-]+:/ {
			endstep()
			jmap = s; sub(/^ */, "", jmap); sub(/:.*/, "", jmap)
			if (jmap == "if") jif = 1
			if (jmap == "continue-on-error") jcoe = 1
			next
		}
		sl && s ~ /^      [A-Za-z0-9_-]+:/ {
			smap = s; sub(/^ */, "", smap); sub(/:.*/, "", smap)
			if (smap == "if") sif = 1
			if (smap == "continue-on-error") scoe = 1
			if (smap == "uses" && s ~ /:[ \t]*actions\/checkout@/) sco = 1
			next
		}
		sl && smap == "with" && s ~ /^        fetch-depth:[ \t]*["\047]?0["\047]?[ \t]*(#.*)?$/ { sdepth = 1 }
		sl && smap == "env" && s ~ /^        CHECK_IOS_REQUIRE:[ \t]*["\047]?1["\047]?[ \t]*(#.*)?$/ { sios = 1 }
		!sl && jmap == "env" && s ~ /^      CHECK_IOS_REQUIRE:[ \t]*["\047]?1["\047]?[ \t]*(#.*)?$/ { jios = 1 }
		END {
			finish()
			if (!runs) { print "no job runs scripts/ci-checks.sh"; bad = 1 }
			exit bad
		}' "$1"
}

# parity <root>: prints every problem of the tree at <root>; fails on any
parity() {
	_r=$1 _pf=0
	_ci=$_r/tests/ci-checks.txt _lo=$_r/tests/ci-local-only.txt _al=$_r/tests/ci-skip-allow.txt
	for _l in "$_ci" "$_lo" "$_al" "$_r/scripts/ci-checks.sh"; do
		[ -f "$_l" ] || { echo "$me: ${_l#"$_r"/} is missing"; return 1; }
	done
	# both lists are well formed
	_lc=$(list_check "$_ci" ci)
	if [ -n "$_lc" ]; then
		printf '%s\n' "$_lc" | sed "s|^|$me: tests/ci-checks.txt: |"
		_pf=1
	fi
	_lc=$(list_check "$_lo" local)
	if [ -n "$_lc" ]; then
		printf '%s\n' "$_lc" | sed "s|^|$me: tests/ci-local-only.txt: |"
		_pf=1
	fi
	# scripts/ci-checks.sh reads the names read here
	if ! _cl=$(sh "$_r/scripts/ci-checks.sh" --list 2>&1); then
		printf '%s\n' "$_cl" | sed "s|^|$me: scripts/ci-checks.sh --list: |"
		_pf=1
	elif [ "$_cl" != "$(list_names "$_ci" ci)" ]; then
		echo "$me: scripts/ci-checks.sh --list reads other names from tests/ci-checks.txt than this check"
		_pf=1
	fi
	# every listed name is a check, and is listed once
	for _n in $( (list_names "$_ci" ci; list_names "$_lo" local) | sort | uniq -d); do
		echo "$me: $_n is listed more than once in tests/ci-checks.txt and tests/ci-local-only.txt"
		_pf=1
	done
	for _n in $(list_names "$_ci" ci) $(list_names "$_lo" local); do
		case $_n in
			check-*.sh) [ -f "$_r/tests/$_n" ] || { echo "$me: $_n is listed but tests/$_n does not exist"; _pf=1; } ;;
			*) echo "$me: '$_n' in a list is not a tests/check-*.sh name"; _pf=1 ;;
		esac
	done
	# every allow-list entry names a check and gives a reason
	_ab=$(awk -F'|' '/^[ \t]*(#|$)/ { next } NF != 3 || $2 == "" || $3 !~ /[^ \t]/ { print "line " NR ": not <check>|<text>|<reason>"; next } { print "name " $1 }' "$_al")
	printf '%s\n' "$_ab" | while IFS= read -r _a; do
		case $_a in
			"name "*) [ -f "$_r/tests/${_a#name }" ] || echo "$me: tests/ci-skip-allow.txt names ${_a#name }, which is not a check" ;;
			?*) echo "$me: tests/ci-skip-allow.txt: $_a" ;;
		esac
	done | grep . && _pf=1
	# every check is listed, and its skips are guarded or allowed
	for _f in "$_r"/tests/check-*.sh; do
		[ -f "$_f" ] || continue
		_n=${_f##*/}
		if ! (list_names "$_ci" ci; list_names "$_lo" local) | grep -qxF "$_n"; then
			echo "$me: tests/$_n is neither in tests/ci-checks.txt (run in CI) nor in tests/ci-local-only.txt (with a reason)"
			_pf=1
		fi
		[ "$_n" != "$me.sh" ] || continue
		_s=$(skips "$_f" "$_al")
		if [ -n "$_s" ]; then
			printf '%s\n' "$_s" | sed "s|^allow: |tests/ci-skip-allow.txt: tests/$_n: |; s|^\([0-9]\)|tests/$_n:\1|; s|^tests/$_n:\([1-9][0-9]*:.*\)|tests/$_n:\1 (a skip with no <NAME>_REQUIRE guard that fails it, and not in tests/ci-skip-allow.txt)|; s|^|$me: |"
			_pf=1
		fi
	done
	_wf=$_r/.github/workflows/macos.yml
	if [ ! -f "$_wf" ]; then
		echo "$me: .github/workflows/macos.yml is missing"
		_pf=1
	elif ! _jo=$(jobs_ok "$_wf"); then
		printf '%s\n' "$_jo" | sed "s|^|$me: .github/workflows/macos.yml: |"
		_pf=1
	fi
	return $_pf
}

fail=0
if ! parity .; then
	fail=1
fi

# the self-test, on scratch copies
tmp=$(mktemp -d "${TMPDIR:-/tmp}/ci-parity.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT
mutants=0
# copy <dir>: the files parity reads, copied to <dir>
copy() {
	mkdir -p "${1:?}/tests" "$1/.github/workflows" "$1/scripts"
	cp tests/check-*.sh tests/ci-checks.txt tests/ci-local-only.txt tests/ci-skip-allow.txt "$1/tests/"
	cp .github/workflows/macos.yml "$1/.github/workflows/"
	cp scripts/ci-checks.sh "$1/scripts/"
}
# check <dir> <line>...: writes the lines as <dir>/tests/check-y.sh, listed
# in tests/ci-checks.txt
check() {
	_d=$1
	shift
	copy "$tmp/$_d"
	printf '%s\n' '#!/bin/sh' "$@" > "$tmp/$_d/tests/check-y.sh"
	chmod +x "$tmp/$_d/tests/check-y.sh"
	echo check-y.sh >> "$tmp/$_d/tests/ci-checks.txt"
}
# after <file> <line> <new>: writes <new> after the first line equal to <line>
after() {
	AFTER_L=$2 AFTER_N=$3 awk '{ print } !d && $0 == ENVIRON["AFTER_L"] { print ENVIRON["AFTER_N"]; d = 1 } END { exit !d }' "$1" > "$1.new" || { echo "$me: selftest: no line '$2' in $1"; return 1; }
	mv "$1.new" "$1"
}
# refused <name> <expected text>...: the mutant in $tmp/<name> is refused,
# with a line that holds each <expected text>
refused() {
	_m=$1
	shift
	mutants=$((mutants + 1))
	if parity "$tmp/$_m" > "$tmp/$_m.out" 2>&1; then
		echo "$me: selftest: mutant $_m passed"
		fail=1
		return
	fi
	for _x; do
		if ! grep -qF -e "$_x" "$tmp/$_m.out"; then
			echo "$me: selftest: mutant $_m was refused, but no line says '$_x':"
			sed 's/^/  | /' "$tmp/$_m.out"
			fail=1
		fi
	done
}
# passes <name>: the copy in $tmp/<name> is accepted
passes() {
	if ! parity "$tmp/$1" > "$tmp/$1.out" 2>&1; then
		echo "$me: selftest: $1 is refused:"
		sed 's/^/  | /' "$tmp/$1.out"
		fail=1
	fi
}
wf=.github/workflows/macos.yml
if [ "$fail" = 0 ]; then
	copy "$tmp/base"
	passes base

	# a guarded skip, on its own line or after the branch's opening line
	check guarded 'if ! command -v zz >/dev/null 2>&1; then' '	[ "${CHECK_Y_REQUIRE:-0}" != 1 ] || { echo "check-y: CHECK_Y_REQUIRE=1 and no zz"; exit 1; }' '	echo "check-y: skipped: no zz"' '	exit 0' 'fi'
	passes guarded

	# (a) the lists
	copy "$tmp/unlisted"
	printf '#!/bin/sh\necho ok\n' > "$tmp/unlisted/tests/check-x.sh"
	refused unlisted "tests/check-x.sh is neither in"

	copy "$tmp/noreason"
	printf '#!/bin/sh\necho ok\n' > "$tmp/noreason/tests/check-x.sh"
	echo 'check-x.sh:' >> "$tmp/noreason/tests/ci-local-only.txt"
	refused noreason "check-x.sh has no reason"

	copy "$tmp/twice"
	list_names tests/ci-checks.txt ci | head -n 1 | sed 's/$/: listed twice/' >> "$tmp/twice/tests/ci-local-only.txt"
	refused twice "listed more than once"

	# a line the two readers used to read apart: this check took the name
	# before the ":", scripts/ci-checks.sh refused the line
	copy "$tmp/listcolon"
	sed 's/^check-doc-links\.sh$/check-doc-links.sh: run in CI/' tests/ci-checks.txt > "$tmp/listcolon/tests/ci-checks.txt"
	refused listcolon "tests/ci-checks.txt: line" "scripts/ci-checks.sh --list: ci-checks:"

	# a carriage return: dropped as a blank here before, kept as part of the
	# name there
	copy "$tmp/listcr"
	sed "s/^check-doc-links\.sh\$/check-doc-links.sh$(printf '\r')/" tests/ci-checks.txt > "$tmp/listcr/tests/ci-checks.txt"
	refused listcr "a carriage return" "scripts/ci-checks.sh --list: ci-checks:"

	# (c) skips: the first two are the round-1 mutants
	check skip 'if ! command -v zz >/dev/null 2>&1; then' '	echo "check-y: skipped: no zz"' '	exit 0' 'fi'
	refused skip "tests/check-y.sh:3:"

	check skip0 '[ -x /zz ] || { echo "check-y: zz not found, skipped"; exit 0; }'
	refused skip0 "tests/check-y.sh:2:"

	# the review's mutants: no exit 0 after the line
	check noexit 'if ! command -v zz >/dev/null 2>&1; then' '	echo "check-y: the zz test is skipped"' 'fi' 'echo ok'
	refused noexit "tests/check-y.sh:3:"

	# an unrelated _REQUIRE two lines above, as an assignment
	check other 'if ! command -v zz >/dev/null 2>&1; then' '	OTHER_REQUIRE=0' '	:' '	echo "skipped: no zz"; exit 0' 'fi'
	refused other "tests/check-y.sh:5:"

	# to standard error, the redirection first
	check stderr 'if ! command -v zz >/dev/null 2>&1; then' '	>&2 echo "check-z3: skipped: no zz"' '	exit 0' 'fi'
	refused stderr "tests/check-y.sh:3:"

	# through a helper that prints its arguments
	check helper 'say() { echo "check-y: $*"; }' 'if ! command -v zz >/dev/null 2>&1; then' '	say "skipped: no zz"' '	exit 0' 'fi'
	refused helper "tests/check-y.sh:4:"

	# no colon after the word, a : line before the exit
	check colon 'if ! command -v zz >/dev/null 2>&1; then' '	echo "zz test skipped (no zz)"' '	:' '	exit 0' 'fi'
	refused colon "tests/check-y.sh:3:"

	# the variable named only inside the message string
	check instring 'if ! command -v zz >/dev/null 2>&1; then' '	echo "check-y: skipped: no zz (${CHECK_Y_REQUIRE:-0} is not 1)"' '	exit 0' 'fi'
	refused instring "tests/check-y.sh:3:"

	# a guard in a branch that is closed before the skip
	check closed 'if [ "${CHECK_Y_REQUIRE:-0}" = 1 ]; then' '	:' 'fi' 'echo "check-y: skipped: no zz"' 'exit 0'
	refused closed "tests/check-y.sh:5:"

	# a quote the reader cannot close: refused, not read as no skip
	check unclosed 'echo "check-y: ok' 'echo "check-y: skipped: no zz"' 'exit 0'
	refused unclosed "tests/check-y.sh:0:"

	# the allow-list: an entry that matches no line, and one with no reason
	copy "$tmp/stale"
	echo 'check-doc-links.sh|no such skip line|a reason' >> "$tmp/stale/tests/ci-skip-allow.txt"
	refused stale "tests/ci-skip-allow.txt: tests/check-doc-links.sh: the entry"

	copy "$tmp/allownoreason"
	echo 'check-doc-links.sh|no such skip line|' >> "$tmp/allownoreason/tests/ci-skip-allow.txt"
	refused allownoreason "not <check>|<text>|<reason>"

	# (b) the workflow
	copy "$tmp/shallow"
	grep -v 'fetch-depth: 0' "$wf" > "$tmp/shallow/$wf"
	refused shallow "on a shallow checkout"

	# fetch-depth: 0 in the job, but not on its checkout step
	copy "$tmp/depthmoved"
	grep -v 'fetch-depth: 0' "$wf" > "$tmp/depthmoved/$wf"
	after "$tmp/depthmoved/$wf" '    - name: Invariants' '      with:
        fetch-depth: 0'
	refused depthmoved "on a shallow checkout"

	copy "$tmp/noios"
	grep -v 'CHECK_IOS_REQUIRE: ' "$wf" > "$tmp/noios/$wf"
	refused noios "without CHECK_IOS_REQUIRE"

	# CHECK_IOS_REQUIRE in the job, but on another step
	copy "$tmp/iosmoved"
	grep -v 'CHECK_IOS_REQUIRE: ' "$wf" > "$tmp/iosmoved/$wf"
	after "$tmp/iosmoved/$wf" '        fetch-depth: 0' '    - name: Other
      env:
        CHECK_IOS_REQUIRE: 1
      run: true'
	refused iosmoved "without CHECK_IOS_REQUIRE"

	copy "$tmp/norun"
	grep -v 'scripts/ci-checks.sh' "$wf" > "$tmp/norun/$wf"
	refused norun "no job runs scripts/ci-checks.sh"

	copy "$tmp/jobif"
	after "$tmp/jobif/$wf" '  invariants_macos:' '    if: false'
	refused jobif "under a job-level if:"

	copy "$tmp/jobcoe"
	after "$tmp/jobcoe/$wf" '  invariants_macos:' '    continue-on-error: true'
	refused jobcoe "job-level continue-on-error:"

	copy "$tmp/stepif"
	after "$tmp/stepif/$wf" '    - name: Invariants' '      if: false'
	refused stepif "step that runs scripts/ci-checks.sh has an if:"

	copy "$tmp/stepcoe"
	after "$tmp/stepcoe/$wf" '    - name: Invariants' '      continue-on-error: true'
	refused stepcoe "step that runs scripts/ci-checks.sh has continue-on-error:"
fi

if [ "$fail" != 0 ]; then
	exit 1
fi
echo "$me: ok ($(list_names tests/ci-checks.txt ci | wc -l | tr -d ' ') checks in CI, $(list_names tests/ci-local-only.txt local | wc -l | tr -d ' ') local only, $(grep -c '^[^#]' tests/ci-skip-allow.txt | tr -d ' ') allowed skips; $mutants mutants refused)"
