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
# (b) .github/workflows/macos.yml, parsed by ruby's yaml as
#     tests/check-workflow-pins.sh parses it (so every quoting of a key
#     counts as YAML reads it), has exactly one step whose run, stripped, is
#     scripts/ci-checks.sh and nothing else (no "|| true", no env prefix, no
#     comment); a run that names it with anything else is refused. Neither
#     that step nor its job has an if or a continue-on-error key. Every
#     actions/checkout step of the job has fetch-depth 0 (tests/check-cvar-
#     docs.sh reads the upstream base commit and fails on a shallow clone)
#     and persist-credentials false. CHECK_IOS_REQUIRE is 1 in the step's
#     env, or, when the step does not set it, in the job's. No ruby on PATH,
#     a ruby that cannot load yaml, or a file it cannot parse is a failure,
#     here and in CI alike.
# (c) A lint of the shape of the skips: every skip line of a
#     tests/check-*.sh has a guard of the right shape before it, or is
#     allowed by tests/ci-skip-allow.txt with a reason. It reads the shell
#     with a small reader of its own (quotes, comments, here-documents,
#     separators), not a shell parser, so it checks how a guard is written,
#     not that it works. That a skip fails under its <NAME>_REQUIRE=1 is
#     shown only by a forced-skip run: the check run with the tool it needs
#     missing and the variable set (docs/runbook/FIX115.md).
#     A skip line is a command line, not a comment and not in a here-document
#     (a <<WORD outside quotes and comments; one never closed is refused, as
#     is a quote never closed), that prints text holding "skip" in any case
#     through echo, printf or a function of the file that prints its
#     arguments, whatever redirection comes first (>&2 echo), to the terminal
#     (not into a file or a pipe; >/dev/stderr, >/dev/tty and >&N are the
#     terminal), and is not a failure. A failure, on that line or the next,
#     is an exit or return with a literal status 1 to 9, or a fail*=
#     assignment of a literal non-zero number, not behind a && or ||; so not
#     exit $var, fail=0, fails=0 or [ ... ] || exit 1.
#     The guard is a [ or test command that reads a <NAME>_REQUIRE variable
#     as an expansion (${X_REQUIRE or $X_REQUIRE; the name inside a string
#     does not count), on that line or above it in the same branch (up to the
#     if, else, case arm or { that opens it, never past a closed one, at most
#     15 lines), and that gates a branch that fails: the command after its
#     || or &&, the { } group there, or the then branch of its if, holds on
#     its own level a failure as above or a call to a function of the file
#     whose body holds one. So a guard that only prints (|| echo required), a
#     bare exit, an inverted guard (= 1 ] && <skip>, a skip in the then
#     branch) and a helper that only counts (check-bench's bad, whose test
#     fails by its status) are refused. An allow-list entry must match
#     exactly one line of its check: none (stale) or two or more is refused.
#     What the lint does not see: a message held in a variable (msg=skipped;
#     echo "$msg"), text printed by cat from a here-document, a skip piped to
#     another command, a helper that prints through another helper, and a
#     guard that names a variable CI never sets.
#
# Then a self-test on scratch copies of those files (never the real tree):
# the copy passes, a guarded skip and a single-line allow-list entry pass,
# and each mutant is refused (see the list below).
#
# Needs sh, awk, sed, grep, cp and ruby (its yaml).
# Usage: tests/check-ci-parity.sh
set -eu
unset RUBYOPT RUBYLIB
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
# matches no skip line or more than one line; "0: ..." when the file cannot
# be read
skips() {
	awk -v name="${1##*/}" '
		# ev <type> <text> <op>: one event of the file: "c" a command, "k" a
		# keyword (if then else elif fi do done case esac), "{" or "}"; <op>
		# is the separator before it
		function ev(type, txt, op) {
			ne++
			ET[ne] = type
			EX[ne] = txt
			EO[ne] = op
			EL[ne] = cl
		}
		# a literal failure: exit or return 1 to 9, or fail*= a non-zero number
		function isfail(t) {
			return t ~ /^(exit|return)[ \t]+[1-9]([ \t\n;)]|$)/ || t ~ /^fail[A-Za-z0-9_]*=["\047]?[1-9][0-9]*["\047]?([ \t\n]|$)/
		}
		function word(t) {
			sub(/[ \t\n].*/, "", t)
			return t
		}
		function endseg(term, t, w, out, op, sf, kw) {
			t = cur
			cur = ""
			out = !rd && !pipe
			rd = 0
			pipe = 0
			op = pop
			pop = term
			sf = 0
			sub(/\$$/, "", t)
			for (;;) {
				sub(/^[ \t\n]+/, "", t)
				if (isfail(t))
					sf = 1
				if (t ~ /^(exit|return)[ \t]+0/)
					X[cl] = 1
				if (match(t, /^(then|else|elif|do|if|while|until|!)([ \t\n]|$)/)) {
					kw = word(t)
					if (kw != "!" && kw != "while" && kw != "until")
						ev("k", kw, op)
					t = substr(t, RLENGTH + 1)
				} else if (match(t, /^[0-9]*[<>]+&?[0-9-]*[ \t]+/))
					t = substr(t, RLENGTH + 1)
				else if (match(t, /^[A-Za-z_][A-Za-z0-9_]*=[^ \t\n]*[ \t]+/))
					t = substr(t, RLENGTH + 1)
				else
					break
			}
			# a failure counts unless a && or || makes it conditional
			if (sf && op != "&&" && op != "||")
				F[cl] = 1
			if (t ~ /^(fi|done|esac)([ \t\n;]|$)/) {
				ev("k", word(t), op)
				return
			}
			if (t ~ /^case[ \t]/)
				ev("k", "case", op)
			if (t == "" && !sf)
				return
			ev("c", t == "" ? "fail" : t, op)
			EF[ne] = sf
			w = word(t)
			if (w == "[" || w == "[[" || w == "test") {
				if (t ~ /\$\{?[A-Z][A-Z0-9_]*_REQUIRE/)
					EG[ne] = 1
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
		# fails <event> <with-functions>: the branch that starts at <event>
		# (up to the else, elif, fi or } that ends it) fails unconditionally:
		# a literal failure, or with <with-functions> a call to a function of
		# FF, on its own level and not behind a && or ||
		function fails(s, usefn, e, d) {
			d = 0
			for (e = s; e <= ne; e++) {
				if (ET[e] == "{" || (ET[e] == "k" && (EX[e] == "if" || EX[e] == "case" || EX[e] == "do"))) {
					d++
					continue
				}
				if (ET[e] == "}" || (ET[e] == "k" && (EX[e] == "fi" || EX[e] == "esac" || EX[e] == "done"))) {
					if (!d)
						return 0
					d--
					continue
				}
				if (ET[e] == "k" && !d && (EX[e] == "else" || EX[e] == "elif"))
					return 0
				if (ET[e] != "c" || d || ((EO[e] == "&&" || EO[e] == "||") && e != s))
					continue
				if (EF[e] || (usefn && (word(EX[e]) in FF)))
					return 1
			}
			return 0
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
			pop = "n"
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
					# a here-document, outside quotes and comments: its body
					# starts on the next line
					if (c == "<" && substr(s, k + 1, 1) == "<" && substr(s, k + 2, 1) != "<") {
						r = substr(s, k + 2)
						if (hp == "" && match(r, /^-?[ \t]*["\047\\]?[A-Za-z_][A-Za-z0-9_]*/)) {
							hp = substr(r, 1, RLENGTH)
							hdt = (hp ~ /^-/)
							sub(/^-?[ \t]*["\047\\]?/, "", hp)
						}
						cur = cur "<<"
						k++
						continue
					}
					if (c == ">" && substr(s, k + 1, 1) != "&") {
						r = substr(s, k + 1)
						sub(/^>?[ \t]*/, "", r)
						if (r !~ /^\/dev\/(stdout|stderr|tty)([ \t;|&)]|$)/)
							rd = 1
					}
					if ((c == "|" || c == "&") && substr(s, k + 1, 1) == c) {
						endseg(c c)
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
						o = pop
						endseg(c)
						if (c == "{" || c == "}")
							ev(c, "", o)
						continue
					}
					cur = cur c
				}
				if (q == "" && !cont)
					endseg("n")
				else
					cur = cur "\n"
				if (hp != "")
					hd = hp
			}
			if (q != "")
				print "0: a quote opened here is never closed, so the skip lines cannot be read"
			if (hd != "")
				print "0: a here-document (<<" hd ") is never closed, so the skip lines cannot be read"
			# the functions that fail: name() { ... } with a literal failure
			# on the level of its body
			for (e = 2; e <= ne; e++)
				if (ET[e] == "{" && EO[e] == ")" && ET[e - 1] == "c" && EX[e - 1] ~ /^[A-Za-z_][A-Za-z0-9_]*$/ && fails(e + 1, 0))
					FF[EX[e - 1]] = 1
			# the guards: a [ or test that reads a <NAME>_REQUIRE variable and
			# gates a branch that fails (after || or &&, or the then of an if)
			for (e = 1; e <= ne; e++) {
				if (!(e in EG))
					continue
				ok = 0
				if (e < ne && (EO[e + 1] == "||" || EO[e + 1] == "&&")) {
					if (ET[e + 1] == "c")
						ok = EF[e + 1] || (word(EX[e + 1]) in FF)
					else if (ET[e + 1] == "{")
						ok = fails(e + 2, 1)
				} else if (e > 1 && ET[e - 1] == "k" && (EX[e - 1] == "if" || EX[e - 1] == "elif")) {
					for (t = e + 1; t <= ne && !(ET[t] == "k" && EX[t] == "then"); t++)
						;
					if (t <= ne)
						ok = fails(t + 1, 1)
				}
				if (ok)
					G[EL[e]] = 1
			}
			for (k = 1; k <= na; k++) {
				nm = 0
				for (i = 1; i <= nl; i++)
					if (index(L[i], ap[k]))
						nm++
				if (nm > 1)
					print "allow: the entry \"" ap[k] "\" matches " nm " lines (an entry matches exactly one)"
			}
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


# jobs_ok <workflow>: the workflow, parsed by ruby's yaml as
# tests/check-workflow-pins.sh parses it, has exactly one step whose run is
# scripts/ci-checks.sh and nothing else; neither that step nor its job has an
# if or a continue-on-error key; every actions/checkout step of the job has
# fetch-depth 0 and persist-credentials false; CHECK_IOS_REQUIRE is 1 in the
# step's env (or, when the step does not set it, the job's). No ruby, or a
# ruby that cannot load yaml or parse the file, is a failure.
jobs_prog='
begin
  require "yaml"
rescue LoadError
  puts "ruby cannot load its yaml library; nothing was checked"
  exit 3
end
RUN = "scripts/ci-checks.sh"
$bad = false
def err(m)
  puts m
  $bad = true
end
def map(v)
  v.is_a?(Hash) ? v : {}
end
def has(h, k)
  h.keys.any? { |x| x.to_s == k }
end
begin
  tree = YAML.safe_load(File.read(ARGV[0]), permitted_classes: [], aliases: true)
rescue StandardError => e
  puts "cannot be read as YAML (" + e.class.to_s + "); nothing was checked"
  exit 1
end
hits = []
map(map(tree)["jobs"]).each do |jn, job|
  steps = map(job)["steps"]
  next unless steps.is_a?(Array)
  steps.each do |st|
    r = map(st)["run"]
    next unless r.is_a?(String)
    if r.strip == RUN
      hits << [jn.to_s, job, st]
    elsif r.include?(RUN)
      err("job " + jn.to_s + ": a step names " + RUN + " but its run is not that line alone: " + r.strip.inspect[0, 120])
    end
  end
end
if hits.empty?
  err("no job runs " + RUN + " (a step whose run is that line alone)")
elsif hits.length > 1
  err(hits.length.to_s + " steps run " + RUN + "; exactly one must")
else
  jn, job, st = hits[0]
  p = "job " + jn + ": "
  err(p + "runs " + RUN + " under a job-level if: (a condition can skip it)") if has(job, "if")
  err(p + "runs " + RUN + " with a job-level continue-on-error: (its failure would not fail the run)") if has(job, "continue-on-error")
  err(p + "the step that runs " + RUN + " has an if: (a condition can skip it)") if has(st, "if")
  err(p + "the step that runs " + RUN + " has continue-on-error: (its failure would not fail the job)") if has(st, "continue-on-error")
  cos = job["steps"].select { |s| map(s)["uses"].is_a?(String) && s["uses"].downcase.start_with?("actions/checkout@") }
  err(p + "runs " + RUN + " on a shallow checkout (no actions/checkout step)") if cos.empty?
  cos.each do |s|
    w = map(s["with"])
    err(p + "runs " + RUN + " on a shallow checkout (an actions/checkout step without fetch-depth: 0)") unless [0, "0"].include?(w["fetch-depth"])
    err(p + "an actions/checkout step keeps its token (no persist-credentials: false)") unless [false, "false"].include?(w["persist-credentials"])
  end
  se = map(st["env"])
  v = se.key?("CHECK_IOS_REQUIRE") ? se["CHECK_IOS_REQUIRE"] : map(job["env"])["CHECK_IOS_REQUIRE"]
  err(p + "runs " + RUN + " without CHECK_IOS_REQUIRE: 1 in that step or job env:") unless [1, "1"].include?(v)
end
exit($bad ? 1 : 0)
'
jobs_ok() {
	if ! command -v ruby > /dev/null 2>&1; then
		echo "no ruby on PATH (its yaml parses the workflow); nothing was checked"
		return 1
	fi
	_s=0
	ruby --disable-gems -e "$jobs_prog" -- "$1" || _s=$?
	if [ "$_s" -gt 1 ]; then
		echo "ruby failed (status $_s); the workflow was not checked"
	fi
	[ "$_s" -eq 0 ]
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
# swap <file> <line> <new>: writes <new> in place of the first line equal to
# <line>
swap() {
	SWAP_L=$2 SWAP_N=$3 awk '!d && $0 == ENVIRON["SWAP_L"] { print ENVIRON["SWAP_N"]; d = 1; next } { print } END { exit !d }' "$1" > "$1.new" || { echo "$me: selftest: no line '$2' in $1"; return 1; }
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
	after "$tmp/iosmoved/$wf" '        persist-credentials: false' '    - name: Other
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

	# round 3: the keys in any quoting, as YAML reads them
	copy "$tmp/jobifq"
	after "$tmp/jobifq/$wf" '  invariants_macos:' '    "if": false'
	refused jobifq "under a job-level if:"

	copy "$tmp/stepifq"
	after "$tmp/stepifq/$wf" '    - name: Invariants' '      "if": false'
	refused stepifq "step that runs scripts/ci-checks.sh has an if:"

	copy "$tmp/stepcoeq"
	after "$tmp/stepcoeq/$wf" '    - name: Invariants' "      'continue-on-error': true"
	refused stepcoeq "step that runs scripts/ci-checks.sh has continue-on-error:"

	# the run line is scripts/ci-checks.sh alone
	copy "$tmp/ortrue"
	swap "$tmp/ortrue/$wf" '        scripts/ci-checks.sh' '        scripts/ci-checks.sh || true'
	refused ortrue "its run is not that line alone" "no job runs scripts/ci-checks.sh"

	copy "$tmp/commented"
	swap "$tmp/commented/$wf" '        scripts/ci-checks.sh' '        true # scripts/ci-checks.sh'
	refused commented "its run is not that line alone" "no job runs scripts/ci-checks.sh"

	copy "$tmp/envprefix"
	swap "$tmp/envprefix/$wf" '        scripts/ci-checks.sh' '        CHECK_IOS_REQUIRE=0 scripts/ci-checks.sh'
	refused envprefix "its run is not that line alone" "no job runs scripts/ci-checks.sh"

	copy "$tmp/nopersist"
	grep -v 'persist-credentials: false' "$wf" > "$tmp/nopersist/$wf"
	refused nopersist "no persist-credentials: false"

	# (c) round 3: a <<EOF in a string or a comment is no here-document, and
	# one never closed is refused
	check hdstring 'echo "a <<EOF b"' 'if ! command -v zz >/dev/null 2>&1; then' '	echo "check-y: skipped: no zz"' '	exit 0' 'fi'
	refused hdstring "tests/check-y.sh:4:"

	check hdcomment '# like cat <<EOF' 'if ! command -v zz >/dev/null 2>&1; then' '	echo "check-y: skipped: no zz"' '	exit 0' 'fi'
	refused hdcomment "tests/check-y.sh:4:"

	check hdopen 'cat <<EOF' 'echo "check-y: skipped: no zz"'
	refused hdopen "tests/check-y.sh:0:"

	# a guard must fail: round 1's || bad then return 0 in check-bench,
	# where bad only counts and a test fails by its status
	check benchbad 'fails=0' 'bad() {' '	echo "check-y: $*"' '	fails=$((fails + 1))' '	if [ -n "$MUTANT" ]; then exit 1; fi' '}' 't_y() {' '	if ! command -v zz >/dev/null 2>&1; then' '		[ "${CHECK_Y_REQUIRE:-0}" != 1 ] || bad "t_y: CHECK_Y_REQUIRE=1 and no zz"' '		echo "check-y: t_y skipped: no zz"' '		return 0' '	fi' '	[ "$fails" -eq 0 ]' '}'
	refused benchbad "tests/check-y.sh:11:"

	check echoreq 'if ! command -v zz >/dev/null 2>&1; then' '	[ "${CHECK_Y_REQUIRE:-0}" != 1 ] || echo required' '	echo "check-y: skipped: no zz"' '	exit 0' 'fi'
	refused echoreq "tests/check-y.sh:4:"

	check bareexit 'if ! command -v zz >/dev/null 2>&1; then' '	[ "${CHECK_Y_REQUIRE:-0}" != 1 ] || { echo "check-y: required"; exit; }' '	echo "check-y: skipped: no zz"' '	exit 0' 'fi'
	refused bareexit "tests/check-y.sh:4:"

	check inverted 'if [ "${CHECK_Y_REQUIRE:-0}" = 1 ]; then' '	echo "check-y: skipped: no zz"' 'fi'
	refused inverted "tests/check-y.sh:3:"

	check invertedand '[ "${CHECK_Y_REQUIRE:-0}" = 1 ] && echo "check-y: skipped: no zz"'
	refused invertedand "tests/check-y.sh:2:"

	# a following line exempts a skip only with a literal failure
	check exitvar 'echo "check-y: skipped: no zz"' 'exit $rc'
	refused exitvar "tests/check-y.sh:2:"

	check fail0 'echo "check-y: skipped: no zz"' 'fail=0'
	refused fail0 "tests/check-y.sh:2:"

	check fails0 'echo "check-y: skipped: no zz"' 'fails=0'
	refused fails0 "tests/check-y.sh:2:"

	check condexit 'echo "check-y: skipped: no zz"' '[ -n "$x" ] || exit 1'
	refused condexit "tests/check-y.sh:2:"

	# the terminal, by another name
	check devstderr 'echo "check-y: skipped: no zz" >/dev/stderr'
	refused devstderr "tests/check-y.sh:2:"

	check devtty 'echo "check-y: skipped: no zz" > /dev/tty'
	refused devtty "tests/check-y.sh:2:"

	check fdn 'echo "check-y: skipped: no zz" >&3'
	refused fdn "tests/check-y.sh:2:"

	# an allow-list entry matches exactly one line
	check allowone 'echo "check-y: zz skipped here"'
	echo 'check-y.sh|zz skipped here|a reason' >> "$tmp/allowone/tests/ci-skip-allow.txt"
	passes allowone

	check allowtwo 'echo "check-y: zz skipped here"' 'echo "check-y: zz skipped here, again"'
	echo 'check-y.sh|zz skipped here|a reason' >> "$tmp/allowtwo/tests/ci-skip-allow.txt"
	refused allowtwo "matches 2 lines"
fi

if [ "$fail" != 0 ]; then
	exit 1
fi
echo "$me: ok ($(list_names tests/ci-checks.txt ci | wc -l | tr -d ' ') checks in CI, $(list_names tests/ci-local-only.txt local | wc -l | tr -d ' ') local only, $(grep -c '^[^#]' tests/ci-skip-allow.txt | tr -d ' ') allowed skips; $mutants mutants refused)"
