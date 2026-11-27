#!/bin/sh
# tests/check-bounded-run.sh -- one bounded-run primitive in the harness
# (FIX122, finding AR-14 of docs/reviews/2026-11-17-fork-window.md).
#
# bench_bounded and bench_stop_group, with its list form bench_stop_groups
# (scripts/bench/core/common.sh), are meant to be the only code under
# scripts/ that runs a command with a wall time and stops it with TERM, a
# grace period and KILL; the gate's scheduler (scripts/gate-checks.sh)
# stops its checks through bench_stop_groups (FIX186). This check holds
# that in three parts:
#
#   guard   a lint over the common spellings, which a reviewer keeps
#           honest; it is no trust boundary. Every line of every file
#           under scripts/ is read, here-doc bodies, strings and comments
#           included (a file it cannot read is a failure). A line the rules
#           below refuse passes only with the marker comment
#           "# bounded-run: allowed: <reason>" at its end, and MARKED pins
#           each file's reasons, by their exact text, unique in the file.
#           Each caller (pins.sh, ios-sim.sh, ios-device.sh, the quake2
#           adapter) must run its commands through the primitive: the three
#           scripts source it, none of the four defines a bench_* function
#           of its own (bench_on_signal, the primitive's hook, aside), and
#           none sends a signal itself (any kill, pkill or killall,
#           unmarked).
#   mutants each one literal change to a copy of scripts/ must be refused,
#           in its own file and for its own rule.
#   pyprobe the same for the Python helper, scripts/lib/bounded.py, and ten
#           mutants of it, each failing its own case (part 4 below).
#   probe   the disposition probe, the behavioural proof: each caller runs
#           a stand-in (a child that starts a grandchild; both live while a
#           flag file exists, so no verdict depends on the host's speed),
#           and the caller gets TERM, INT or KILL, or its wall time runs
#           out; and ios-sim gets a second TERM in its cleanup's late
#           bounded call. What is left running is read once the caller has
#           exited, then the flag is removed and the stand-ins end by
#           themselves.
#
# What the guard matches. It reads logical lines: a shell line that ends
# in an odd number of backslashes goes on (a whole-line comment does not),
# and a Python line goes on while a parenthesis is open (50 lines at most; a
# whole-line comment inside it does not end the join, and is read on its
# own). A bench_* definition is matched at the start of a logical line or
# after ; & | ( ) { or }. Each is lowered and its " and ' deleted, so kill -s "KILL" is kill -s
# kill. A number may have leading zeros (-09, -00). A KILL is kill, pkill
# or killall with -9, -KILL, -SIGKILL, -s KILL, -s 9, -n 9 or --signal
# KILL (or 9), and timeout with -s KILL (or 9), any prefix of --signal
# naming KILL or 9 (--sig=KILL), -k, or any prefix of --kill-after
# (--kill=1). In a .py file a KILL is also the word SIGKILL (signal.SIGKILL,
# signal.Signals.SIGKILL, from signal import SIGKILL), .kill() (Popen),
# Signals(9), send_signal(9), and kill or killpg (os.kill, os.killpg) with
# 9 as the last argument (a trailing comma allowed), through one level of
# nested parentheses; and subprocess.run, call, check_call or check_output
# with a timeout= on the logical line, whose KILL on the timeout goes to the
# direct child only (FIX186, slice 2): every Python command with a time
# limit goes through scripts/lib/bounded.py, which stops its process group
# TERM first, and whose one KILL is the one marked Python line. A liveness test is kill -0, kill -s 0, kill -n 0,
# kill -EXIT or kill -s EXIT on a shell line that is not a whole-line
# comment. A variable signal is kill, pkill or killall whose first argument
# is a parameter or a command substitution ($ or a backtick), alone or
# after - or -SIG (kill -"$sig", kill -$sig, kill -SIG"$sig", kill
# -`cmd`, kill $flag "$p", kill "$@", and so kill "$pid" too), or with -s,
# -n or --signal followed by one, a SIG prefix allowed (kill -s "$sig",
# kill -s "${sig}", kill -s SIG"$sig", kill -s `cmd`), anywhere on a shell
# line, comments included: the guard cannot trace what the variable holds,
# and it can hold KILL (card FIX186, #699), so every one outside the
# primitive is refused unless marked. The word is matched wherever it
# stands, so a KILL passed to a function (bench_signal_group KILL "$p") is
# refused too, and the primitive's one such line is marked. A group after --
# (kill -s TERM -- "-$pgid") is no signal; pkill's -s (a session) is
# refused too.
# A marker followed by a quote is in a string, so it is no marker;
# a marked line holds one kill, pkill, killall or timeout word or Python
# KILL form at most.
#
# What the pinned reasons guarantee: every marked line carries a reason
# the check lists for its file, each listed reason is on exactly one line,
# and each marked line refuses something. So a marker with a new reason
# needs an edit to MARKED, and so does one taken away. They do not tell
# which line a reason is on: a KILL removed and another added elsewhere
# with the same reason passes, and it is the reviewer who reads the diff.
#
# What the guard does not see, and so does not claim: a variable signal
# to timeout (timeout -s "$sig"); a liveness test through a
# function (bench_pid_alive); a subprocess function imported under another
# name (from subprocess import run), and Popen.kill() reached through
# subprocess.run's own cleanup on an exception (a run with no timeout);
# other languages (Perl, Ruby); Python inside a shell -c string; eval'd or
# constructed text; a backslash inside the command word (k\ill); gtimeout;
# a parenthesis inside a Python string, which the join counts. The source
# rule below ignores order and dead code: an assignment after the source,
# or inside if false, passes.
#
# A source of the primitive is a line . "$<var>/scripts/bench/core/common.sh",
# or . "$<v>" where the name <v> occurs once in the whole file other than
# as $<v> or ${<v>}, in the line <v>="$<var>/scripts/bench/core/common.sh"
# (export, local or readonly before it allowed). A line in a here-doc body
# is no source and no such assignment. Here-docs are tracked for this rule
# alone: a << whose word cannot be read (an odd quote, nothing after it)
# ends the search for a source in that file (fails closed), and a source
# line inside a multi-line quoted string is not told from code.
#
# Usage: tests/check-bounded-run.sh            the three parts, on this tree
#        tests/check-bounded-run.sh guard <root>   the guard alone on <root>
#        tests/check-bounded-run.sh probe <root>   the probe alone on <root>,
#                                                  printed, not judged
#        tests/check-bounded-run.sh pyprobe <root> the Python helper probe
#                                                  alone, printed, not judged
# POSIX sh (dash, bash 3.2). Needs python3 (SIGINT reset), perl-free.
set -u
LC_ALL=C
export LC_ALL
me=check-bounded-run
here=$(cd "$(dirname "$0")/.." && pwd -P) || exit 1

# The marked lines ("# bounded-run: allowed: <reason>" at the end of the
# line): one per entry, the file and the exact reason text after the
# marker, which is unique within its file. A file not listed holds none.
# A marker whose reason is not listed, a listed reason no line carries, a
# reason on two lines, and a marker on a line no rule refuses each fail.
# There is no whole-file entry.
MARKED='
scripts/bench/core/common.sh the primitive: bench_pid_alive
scripts/bench/core/common.sh the primitive: the group a spawn made
scripts/bench/core/common.sh the primitive: a group still has a member
scripts/bench/core/common.sh the primitive: bench_signal_group, to the group
scripts/bench/core/common.sh the primitive: bench_signal_group, to the pid
scripts/bench/core/common.sh the primitive: bench_stop_groups, the KILL after the grace
scripts/bench/core/lock.sh the lock: its holder group still has a member; it signals none
scripts/bench/core/lock.sh the lock: a recorded group still has a member; it signals none
scripts/lib/supervise.sh sv_wait: its own supervisor is there
scripts/test.sh the gate parent watch: it polls the caller, and sends TERM only
scripts/ios-sim.sh selftest: the fake xcrun terminate, only the app it started
scripts/ios-sim.sh selftest: ends the fake hung call it checked
scripts/ios-sim.sh selftest: a hung call ps cannot name
scripts/ios-sim.sh selftest: waits for the fake hung read
scripts/ios-sim.sh selftest: sends the signal under test
scripts/ios-sim.sh selftest: waits for the script under test
scripts/ios-sim.sh selftest: the script under test outlived the signal
scripts/ios-sim.sh selftest: ends the script under test that hung
scripts/ios-sim.sh selftest: ends the fake hung launcher read it checked
scripts/ios-sim.sh selftest: a hung launcher read ps cannot name
scripts/ios-sim.sh selftest: ends the decoy it checked
scripts/ios-sim.sh selftest: a decoy ps cannot name
scripts/ios-sim.sh selftest: the term mutant signals the decoy the fake started
scripts/ios-sim.sh selftest: waits for a wrongly sent signal to show
scripts/ios-sim.sh selftest: the stale pid must still run
scripts/ios-sim.sh selftest: ends its own sleep
scripts/ios-device.sh selftest: sends INT under test to the group of the script
scripts/ios-device.sh selftest: sends the signal under test
scripts/ios-device.sh selftest: waits for the script under test
scripts/ios-device.sh selftest: the script under test outlived the signal
scripts/ios-device.sh selftest: ends the script under test that hung
scripts/ios-device.sh selftest: the fake hanging step outlived the script
scripts/ios-device.sh selftest: ends the fake hanging step
scripts/lib/bounded.py the helper: stop, the KILL after the grace
'

W=
cleanup() { if [ -n "$W" ]; then chmod -R u+rwx "$W" 2> /dev/null; rm -rf "$W"; fi; }
trap cleanup EXIT
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM
trap 'cleanup; exit 129' HUP
W=$(mktemp -d "${TMPDIR:-/tmp}/check-bounded-run.XXXXXX") || exit 1

# pinned <rel>: the marker reasons MARKED pins for <rel>, one per line
pinned() {
	printf '%s\n' "$MARKED" | awk -v f="$1" '$1 == f { sub(/^[^ ]+ /, ""); print }'
}

# scan_file <root> <rel>: one line per finding, "<rel>:<line>: <rule>"
scan_file() {
	PINNED=$(pinned "$2") awk -v rel="$2" '
	BEGIN {
		py = (rel ~ /\.py$/)
		caller = (rel ~ /^scripts\/((pins|ios-sim|ios-device)\.sh|bench\/adapters\/quake2\.sh)$/)
		src = (rel ~ /^scripts\/(pins|ios-sim|ios-device)\.sh$/)
		S = "[ \t,]"
		K = "(sig)?kill"
		N9 = "0*9"
		KILLRE = "(^|[^a-z0-9_.-])(kill|pkill|killall)" S "+[^;&|]*(-" N9 "|-" K "|-[sn]" S "*(" K "|" N9 ")|--signal" S "*=?" S "*(" K "|" N9 "))([^a-z0-9_]|$)"
		# any prefix of --signal that names KILL, any prefix of --kill-after
		TOSRE = "(^|[^a-z0-9_.-])timeout" S "+[^;&|]*(-s" S "*(" K "|" N9 ")|--s(i(g(n(a(l)?)?)?)?)?" S "*=?" S "*(" K "|" N9 "))([^a-z0-9_]|$)"
		TOKRE = "(^|[^a-z0-9_.-])timeout" S "+[^;&|]*(-k|--k(i(l(l(-(a(f(t(e(r)?)?)?)?)?)?)?)?)?)([^a-z_-]|$)"
		ZERORE = "(^|[^a-z0-9_.-])kill[ \t]+(-0+|-[sn][ \t]*0+|--signal[ \t=]*0+|-(sig)?exit|-s[ \t]*(sig)?exit)([^a-z0-9_]|$)"
		# a signal through a variable or a command substitution ($ or a
		# backtick, a SIG prefix allowed): the first argument -$v, -sig$v
		# or $v itself, or -s, -n or --signal and $v anywhere on the line
		# (after -- it is a group, not this)
		VARSIG1 = "(^|[^a-z0-9_.-])(kill|pkill|killall)" S "+(-(sig)?)?[$`]"
		VARSIG2 = "(^|[^a-z0-9_.-])(kill|pkill|killall)" S "+([^;&|]*" S ")?(-[sn]" S "*|--signal" S "*=?" S "*)(sig)?[$`]"
		ANYRE = "(^|[^a-z0-9_.-])(kill|pkill|killall)([^a-z0-9_-]|$)"
		WORDRE = "(^|[^a-z0-9_.-])(kill|pkill|killall|timeout)([^a-z0-9_-]|$)"
		PY1 = "(^|[^a-z0-9_])sigkill([^a-z0-9_]|$)"
		PY2 = "\\.kill[ \t]*\\([ \t]*\\)"
		PY3 = "signals[ \t]*\\([ \t]*0*9[ \t]*,?[ \t]*\\)"
		PY4 = "send_signal[ \t]*\\([^)0-9]*0*9[ \t]*,?[ \t]*\\)"
		PY5 = "(^|[^a-z0-9_])kill(pg)?[ \t]*\\(([^()]|\\([^()]*\\))*,[ \t]*0*9[ \t]*,?[ \t]*\\)"
		# a subprocess call with a timeout: on the timeout it KILLs the direct child only
		PY6 = "(^|[^a-z0-9_])subprocess[ \t]*\\.[ \t]*(run|call|check_call|check_output)[ \t]*\\(.*(^|[^a-z0-9_])timeout[ \t]*="
		BDEF = "(^|[;&|(){}])[ \t]*bench_[A-Za-z0-9_]*[ \t]*\\(\\)"
		LIT = "\"\\$[A-Za-z_][A-Za-z0-9_]*/scripts/bench/core/common\\.sh\""
		hn = 0; hi = 0; lost = 0; acc = ""; depth = 0
		np = split(ENVIRON["PINNED"], PR, "\n")
		for (i = 1; i <= np; i++) if (PR[i] != "") PIN[PR[i]] = 1
	}
	function say(what, ln) { hit = 1; if (!mk) printf "%s:%d: %s\n", rel, ln, what }
	# count(s, re): how many times re matches in s, a boundary character shared
	function count(s, re,    n) {
		n = 0
		while (match(s, re)) { n++; s = substr(s, RSTART + (RLENGTH > 1 ? RLENGTH - 1 : 1)) }
		return n
	}
	function lower(s) { s = tolower(s); gsub(/["\047]/, "", s); return s }
	# bdefs(s): 1 if s defines a bench_* function other than bench_on_signal,
	# the hook of the primitive; every definition on the line is looked at
	function bdefs(s,    t) {
		while (match(s, BDEF)) {
			t = substr(s, RSTART, RLENGTH)
			sub(/^[^b]*/, "", t)
			sub(/[ \t]*\(\)$/, "", t)
			if (t != "bench_on_signal") return 1
			s = substr(s, RSTART + RLENGTH)
		}
		return 0
	}
	# check(line, ln): the rules on one logical line, which starts at line ln
	function check(line, ln,    lc, t, nw) {
		mk = 0
		hit = 0
		if (match(line, /# bounded-run: allowed: [^ \t#][^#]*$/)) {
			t = substr(line, RSTART + 24)
			sub(/[ \t]+$/, "", t)
			if (t ~ /["\047`]/) printf "%s:%d: a bounded-run marker followed by a quote (in a string, not a comment)\n", rel, ln
			else {
				mk = 1
				R[t]++
				RL[t] = ln
				nw = count(lower(substr(line, 1, RSTART - 1)), WORDRE)
				if (py) nw += count(lower(substr(line, 1, RSTART - 1)), PY1) + count(lower(substr(line, 1, RSTART - 1)), PY2) + count(lower(substr(line, 1, RSTART - 1)), PY3) + count(lower(substr(line, 1, RSTART - 1)), PY4) + count(lower(substr(line, 1, RSTART - 1)), PY5) + count(lower(substr(line, 1, RSTART - 1)), PY6)
				if (nw > 1) printf "%s:%d: a marked line with %d kill, pkill, killall or timeout words or Python KILL forms (a marker covers one)\n", rel, ln, nw
			}
		}
		lc = lower(line)
		if (lc ~ KILLRE || lc ~ TOSRE || lc ~ TOKRE) say("a KILL outside the bounded-run primitive", ln)
		else if (py && (lc ~ PY1 || lc ~ PY2 || lc ~ PY3 || lc ~ PY4 || lc ~ PY5))
			say("a KILL outside the bounded-run primitive", ln)
		if (py && lc ~ PY6) say("a subprocess call with a timeout outside the bounded-run primitive (its KILL goes to the direct child only; use scripts/lib/bounded.py)", ln)
		if (!py && (lc ~ VARSIG1 || lc ~ VARSIG2)) say("a signal named through a variable outside the bounded-run primitive (it can hold KILL)", ln)
		if (!py && line !~ /^[ \t]*#/ && lc ~ ZERORE) say("a kill -0 liveness test outside the bounded-run primitive (the shape of a polling loop)", ln)
		if (caller && lc ~ ANYRE) say("a caller that sends a signal itself, not through the primitive", ln)
		if (mk && !hit) printf "%s:%d: a bounded-run marker on a line no rule refuses\n", rel, ln
		if (caller && line !~ /^[ \t]*#/ && bdefs(line))
			printf "%s:%d: a caller that defines a bench_* function of its own\n", rel, ln
		if (caller && line !~ /^[ \t]*#/ && line ~ /(^|[^A-Za-z0-9_])bench_bounded([ \t]|$)/) uses = 1
	}
	{
		line = $0
		L[NR] = line
		# the logical line: a shell line that ends in an odd number of
		# backslashes goes on (a whole-line comment does not), and so does
		# a Python line inside an open parenthesis (counted with quotes
		# deleted, a whole-line comment not counted), for 50 lines at most
		if (acc == "") an = NR
		bs = line
		cont = (match(bs, /\\+$/) && RLENGTH % 2 == 1 && !(acc == "" && !py && line ~ /^[ \t]*#/))
		if (cont) bs = substr(bs, 1, length(bs) - 1)
		if (py && line ~ /^[ \t]*#/ && depth > 0) {
			# a whole-line comment inside an open parenthesis does not end the
			# join; it is read on its own and not joined
			if (NR - an < 50) { check(line, NR); next }
			depth = 0
		}
		if (py && line !~ /^[ \t]*#/) {
			t = lower(line)
			depth += gsub(/\(/, "", t)
			t = lower(line)
			depth -= gsub(/\)/, "", t)
			if (depth < 0) depth = 0
			if (depth > 0 && NR - an < 50) cont = 1
			else depth = 0
		}
		if (cont) acc = acc bs " "
		else { check(acc line, an); acc = "" }
		if (!src) next
		# the source rule: here-doc bodies tracked, for this rule alone
		IB[NR] = 0
		if (hn > 0) {
			IB[NR] = 1
			t = line
			if (hs[hi]) sub(/^\t+/, "", t)
			if (t == hw[hi]) { hi++; if (hi > hn) { hn = 0; hi = 0 } }
			next
		}
		if (!lost && line ~ ("^[ \t]*\\.[ \t]+" LIT "[ \t]*$")) sourced = 1
		if (!lost && match(line, /^[ \t]*\.[ \t]+"\$[A-Za-z_][A-Za-z0-9_]*"[ \t]*$/)) {
			v = line; sub(/^[ \t]*\.[ \t]+"\$/, "", v); sub(/"[ \t]*$/, "", v); via[v] = NR
		}
		s = line
		while ((p = index(s, "<<")) > 0) {
			s = substr(s, p + 2)
			if (substr(s, 1, 1) == "<") { s = substr(s, 2); continue }
			dash = 0
			if (substr(s, 1, 1) == "-") { dash = 1; s = substr(s, 2) }
			sub(/^[ \t]*/, "", s)
			if (!match(s, /^[^ \t;&|<>()]+/)) { lost = 1; break }
			w = substr(s, 1, RLENGTH); s = substr(s, RLENGTH + 1)
			q1 = w; q2 = w
			if (gsub(/\047/, "", q1) % 2 || gsub(/"/, "", q2) % 2) { lost = 1; break }
			gsub(/["\047\\]/, "", w)
			if (w == "") { lost = 1; break }
			hn++; hw[hn] = w; hs[hn] = dash
			if (hi == 0) hi = 1
		}
	}
	END {
		if (acc != "") check(acc, an)
		for (t in R) {
			if (!(t in PIN)) printf "%s:%d: a bounded-run marker whose reason the check does not pin: %s\n", rel, RL[t], t
			else if (R[t] > 1) printf "%s:%d: the marker reason \"%s\" is on %d lines; a pinned reason marks one\n", rel, RL[t], t, R[t]
		}
		for (t in PIN) if (!(t in R)) printf "%s:0: the check pins the marker reason \"%s\", and no line carries it\n", rel, t
		if (rel == "scripts/bench/adapters/quake2.sh" && !uses) printf "%s:0: the adapter does not run the engine through bench_bounded\n", rel
		if (!src) exit
		for (v in via) {
			n = 0; good = 0
			for (i = 1; i <= NR; i++) {
				r = L[i]; off = 0
				while (match(r, "(^|[^A-Za-z0-9_])" v "([^A-Za-z0-9_]|$)")) {
					b = (RSTART == 1 && substr(r, 1, length(v)) == v) ? RSTART : RSTART + 1
					pre = substr(r, 1, b - 1); post = substr(r, b + length(v))
					if (!(pre ~ /\$$/ || (pre ~ /\$\{$/ && post ~ /^\}/))) {
						n++
						if (!IB[i] && r ~ ("^[ \t]*((export|local|readonly)[ \t]+)?" v "=" LIT "[ \t]*$")) good = 1
					}
					r = substr(r, b + length(v))
				}
			}
			if (n == 1 && good) sourced = 1
			else printf "%s:%d: a source through $%s, a name not assigned exactly once, to the literal path of the primitive (%d uses other than $%s)\n", rel, via[v], v, n, v
		}
		if (!sourced) printf "%s:0: a caller that does not source the bounded-run primitive (scripts/bench/core/common.sh)\n", rel
	}' "$1/$2"
}

# guard <root>: every finding under <root>/scripts; false when there is one.
# find runs on its own: a status other than 0, or anything on its standard
# error (a directory it cannot read), is a failure (fails closed)
guard() {
	_g_bad=0
	_g_raw=$W/files.raw.$$
	_g_err=$W/files.err.$$
	_g_list=$W/files.$$
	_g_fs=0
	(cd "$1" && exec find scripts -type f) > "$_g_raw" 2> "$_g_err" || _g_fs=$?
	if [ "$_g_fs" -ne 0 ] || [ -s "$_g_err" ]; then
		echo "scripts:0: cannot list every file (find exited $_g_fs: $(head -n 1 "$_g_err")) (the guard fails closed)"
		_g_bad=1
	fi
	sort "$_g_raw" > "$_g_list" || return 1
	[ -s "$_g_list" ] || { echo "$me: no files under $1/scripts"; return 1; }
	while IFS= read -r _g_f; do
		if [ ! -r "$1/$_g_f" ]; then
			echo "$_g_f:0: cannot be read (the guard fails closed)"
			_g_bad=1
			continue
		fi
		_g_o=$(scan_file "$1" "$_g_f") || { echo "$_g_f:0: the scan failed"; _g_bad=1; continue; }
		if [ -n "$_g_o" ]; then
			printf '%s\n' "$_g_o"
			_g_bad=1
		fi
	done < "$_g_list"
	return "$_g_bad"
}

# mutant <name> <file> <expected rule> <old> <new> [<old2> <new2>]: a copy
# of scripts/ with one change (two edits at most); the guard must refuse it
# with a finding in <file> that holds <expected rule>, so a refusal for any
# other reason (a standing failure) never counts
nmut=0
fails=
mutant() {
	_m_n=$1 _m_f=$2 _m_x=$3
	shift 3
	_m_d=$W/m-$_m_n
	rm -rf "$_m_d"
	mkdir -p "$_m_d" && cp -R "$here/scripts" "$_m_d/scripts" || { fails="$fails $_m_n"; return; }
	case $_m_n in
	unreadable) chmod 000 "$_m_d/$_m_f" ;;
	unreadable-dir)
		mkdir -p "$_m_d/scripts/zz-hidden" && printf 'kill -9 "$$"\n' > "$_m_d/scripts/zz-hidden/x.sh" && chmod 000 "$_m_d/scripts/zz-hidden"
		;;
	*)
		python3 -I -c 'import sys
p = sys.argv[1]
s = open(p).read()
a = sys.argv[2:]
for old, new in zip(a[0::2], a[1::2]):
    if old == "": s = s + new; continue
    if s.count(old) != 1: sys.exit("the mutant does not apply: %d" % s.count(old))
    s = s.replace(old, new)
open(p, "w").write(s)' "$_m_d/$_m_f" "$@" || { echo "$me: FAIL: mutant $_m_n does not apply"; fails="$fails $_m_n"; return; }
		;;
	esac
	_m_o=$(guard "$_m_d" 2>&1) && { echo "$me: FAIL: mutant $_m_n was not refused"; fails="$fails $_m_n"; }
	chmod -R u+rwx "$_m_d" 2> /dev/null
	_m_hit=$(printf '%s\n' "$_m_o" | grep -F -e "$_m_f:" | grep -F -e "$_m_x" | head -n 1)
	if [ -z "$_m_hit" ]; then
		echo "$me: FAIL: mutant $_m_n was not refused for \"$_m_x\" in $_m_f; the guard said: $(printf '%s\n' "$_m_o" | head -n 2 | tr '\n' ' ')"
		fails="$fails $_m_n"
		return
	fi
	nmut=$((nmut + 1))
	echo "$me: mutant $_m_n: refused ($_m_hit)"
}

# ---- the disposition probe ----------------------------------------------

# standin <dir>: the stand-in command: records its pid, starts a grandchild
# that records its own, and both live while <dir>/flag exists; with
# PROBE_TAG, the pid files are <tag>child.pid and <tag>gc.pid
mk_standin() {
	cat > "$1" << 'EOF'
#!/bin/sh
d=${PROBE_DIR:?}
t=${PROBE_TAG:-}
echo $$ > "$d/${t}child.pid"
sh -c 'echo $$ > "$1/$2gc.pid"; while [ -e "$1/flag" ]; do sleep 0.1; done' gc "$d" "$t" &
while [ -e "$d/flag" ]; do sleep 0.1; done
wait
EOF
	chmod +x "$1"
}

# verdict <pid file>: "runs" or "gone"
verdict() {
	_v=$(cat "$1" 2> /dev/null) || { echo none; return; }
	if kill -0 "$_v" 2> /dev/null; then echo runs; else echo gone; fi
}

# probe_one <root> <caller> <case>: one line: the caller's exit, the child's
# and the grandchild's state once it has exited
probe_one() {
	_p_d=$W/p-$2-$3-$(basename "$1")
	rm -rf "$_p_d"
	mkdir -p "$_p_d/bin" "$_p_d/gd/baseq2" "$_p_d/store" "$_p_d/tmp" || return 1
	: > "$_p_d/flag"
	mk_standin "$_p_d/bin/standin"
	_p_lim=600
	[ "$3" = wall ] && _p_lim=2
	case $2 in
	pins)
		set -- "$1" "$2" "$3" env BENCH_LOCK=0 PINS_BIN="$_p_d/bin/standin" PINS_BASEDIR="$_p_d/gd" PINS_STORE="$_p_d/store" \
			PINS_TIMEOUT="$_p_lim" /bin/bash "$1/scripts/pins.sh" record gl1 world
		;;
	ios-device)
		cp "$_p_d/bin/standin" "$_p_d/bin/xcrun"
		_p_a=$_p_d/Quake2.app
		mkdir -p "$_p_a/_CodeSignature" && : > "$_p_a/quake2" && : > "$_p_a/embedded.mobileprovision" && : > "$_p_a/_CodeSignature/CodeResources"
		printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' '<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>org.example.probe</string></dict></plist>' > "$_p_a/Info.plist"
		set -- "$1" "$2" "$3" env PATH="$_p_d/bin:$PATH" TMPDIR="$_p_d/tmp" IOS_DEVICE_INSTALL_SECONDS="$_p_lim" IOS_APP="$_p_a" \
			FAKE_STATE="$_p_d" /bin/bash "$1/scripts/ios-device.sh" install --device 00000000-PROBE
		;;
	ios-sim)
		cp "$_p_d/bin/standin" "$_p_d/bin/xcrun"
		cat > "$_p_d/bin/make" << 'EOF'
#!/bin/sh
for a; do case $a in IOS_ROOT=*) r=${a#IOS_ROOT=} ;; esac; done
mkdir -p "$r/Quake2.app" && printf '#!/bin/sh\nexit 0\n' > "$r/Quake2.app/quake2" && chmod +x "$r/Quake2.app/quake2" || exit 1
printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' '<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>org.example.probe</string></dict></plist>' > "$r/Quake2.app/Info.plist"
EOF
		chmod +x "$_p_d/bin/make"
		set -- "$1" "$2" "$3" env PATH="$_p_d/bin:$PATH" TMPDIR="$_p_d/tmp" IOS_SIM_LIST_SECONDS="$_p_lim" \
			/bin/bash "$1/scripts/ios-sim.sh" smoke --out "$_p_d/out"
		;;
	esac
	_p_root=$1 _p_who=$2 _p_case=$3
	shift 3
	# SIGINT back to the default first: a background job of this shell starts with it ignored
	PROBE_DIR=$_p_d python3 -I -c 'import os, signal, sys
signal.signal(signal.SIGINT, signal.SIG_DFL)
os.execvp(sys.argv[1], sys.argv[1:])' "$@" < /dev/null > "$_p_d/log" 2>&1 &
	_p_c=$!
	_p_n=0
	while { [ ! -s "$_p_d/child.pid" ] || [ ! -s "$_p_d/gc.pid" ]; } && [ "$_p_n" -lt 600 ]; do
		sleep 0.1
		_p_n=$((_p_n + 1))
	done
	if [ ! -s "$_p_d/gc.pid" ]; then
		echo "$_p_who $_p_case: the stand-in did not start ($(tail -n 1 "$_p_d/log"))"
		rm -f "$_p_d/flag"
		wait "$_p_c" 2> /dev/null
		return 1
	fi
	_p_w=
	case $_p_case in
	term) kill -s TERM "$_p_c" ;;
	int) kill -s INT "$_p_c" ;;
	kill) kill -s KILL "$_p_c" ;;
	esac
	_p_n=0
	_p_h=
	while kill -0 "$_p_c" 2> /dev/null && [ "$_p_n" -lt "${PROBE_HANG:-600}" ]; do
		sleep 0.1
		_p_n=$((_p_n + 1))
	done
	if kill -0 "$_p_c" 2> /dev/null; then
		_p_h=" (hung: killed by the probe)"
		kill -s KILL "$_p_c"
	fi
	_p_rc=0
	wait "$_p_c" 2> /dev/null || _p_rc=$?
	printf '%s %s: caller exit %s%s%s; child %s; grandchild %s\n' "$_p_who" "$_p_case" "$_p_rc" "$_p_h" "$_p_w" \
		"$(verdict "$_p_d/child.pid")" "$(verdict "$_p_d/gc.pid")"
	# the stand-ins end by themselves once the flag is gone
	rm -f "$_p_d/flag"
	_p_n=0
	while { [ "$(verdict "$_p_d/child.pid")" = runs ] || [ "$(verdict "$_p_d/gc.pid")" = runs ]; } && [ "$_p_n" -lt 100 ]; do
		sleep 0.1
		_p_n=$((_p_n + 1))
	done
	_p_root=
}

# probe_late <root>: ios-sim's second signal. A fake xcrun lists, creates a
# device and hangs in "simctl bootstatus" (the stand-in); TERM ends the
# script, and its cleanup's late bounded "simctl shutdown" hangs too (the
# stand-in again, its pid files tagged late-). A second TERM then comes in
# that late call. One line: the caller's exit, the late child's and
# grandchild's state once it has exited, and whether its scratch directory
# is left
probe_late() {
	_p_d=$W/p-ios-sim-late-$(basename "$1")
	rm -rf "$_p_d"
	mkdir -p "$_p_d/bin" "$_p_d/tmp" || return 1
	: > "$_p_d/flag"
	mk_standin "$_p_d/bin/standin"
	cat > "$_p_d/bin/xcrun" << 'EOF'
#!/bin/sh
d=${PROBE_DIR:?}
u=11111111-2222-3333-4444-555555555555
case ${2:-} in
list)
	case ${3:-} in
	devices)
		if [ -e "$d/created" ]; then echo "{\"devices\": {\"r\": [{\"udid\": \"$u\", \"name\": \"probe\"}]}}"
		else echo '{"devices": {"r": [{"udid": "00000000-0000-0000-0000-000000000000", "name": "other"}]}}'; fi ;;
	devicetypes) echo '{"devicetypes": [{"name": "iPhone 17", "identifier": "probe.type"}]}' ;;
	runtimes) echo '{"runtimes": [{"isAvailable": true, "platform": "iOS", "version": "27.2", "name": "iOS 27.2", "identifier": "probe.rt"}]}' ;;
	*) exit 1 ;;
	esac ;;
create) : > "$d/created"; echo "$u" ;;
bootstatus) exec "$d/bin/standin" ;;
shutdown) PROBE_TAG=late- exec "$d/bin/standin" ;;
*) exit 1 ;;
esac
EOF
	cat > "$_p_d/bin/make" << 'EOF'
#!/bin/sh
for a; do case $a in IOS_ROOT=*) r=${a#IOS_ROOT=} ;; esac; done
mkdir -p "$r/Quake2.app" && printf '#!/bin/sh\nexit 0\n' > "$r/Quake2.app/quake2" && chmod +x "$r/Quake2.app/quake2" || exit 1
printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' '<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>org.example.probe</string></dict></plist>' > "$r/Quake2.app/Info.plist"
EOF
	chmod +x "$_p_d/bin/xcrun" "$_p_d/bin/make"
	PROBE_DIR=$_p_d python3 -I -c 'import os, signal, sys
signal.signal(signal.SIGINT, signal.SIG_DFL)
os.execvp(sys.argv[1], sys.argv[1:])' env PATH="$_p_d/bin:$PATH" TMPDIR="$_p_d/tmp" \
		/bin/bash "$1/scripts/ios-sim.sh" smoke --out "$_p_d/out" < /dev/null > "$_p_d/log" 2>&1 &
	_p_c=$!
	_p_n=0
	while [ ! -s "$_p_d/gc.pid" ] && [ "$_p_n" -lt 600 ]; do
		sleep 0.1
		_p_n=$((_p_n + 1))
	done
	[ -s "$_p_d/gc.pid" ] && kill -s TERM "$_p_c"
	_p_n=0
	while [ ! -s "$_p_d/late-gc.pid" ] && [ "$_p_n" -lt 600 ]; do
		sleep 0.1
		_p_n=$((_p_n + 1))
	done
	if [ ! -s "$_p_d/late-gc.pid" ]; then
		echo "ios-sim late: the late call did not start ($(tail -n 1 "$_p_d/log"))"
		rm -f "$_p_d/flag"
		kill -s TERM "$_p_c" 2> /dev/null
		wait "$_p_c" 2> /dev/null
		return 1
	fi
	kill -s TERM "$_p_c"
	_p_n=0
	_p_h=
	while kill -0 "$_p_c" 2> /dev/null && [ "$_p_n" -lt 600 ]; do
		sleep 0.1
		_p_n=$((_p_n + 1))
	done
	if kill -0 "$_p_c" 2> /dev/null; then
		_p_h=" (hung: killed by the probe)"
		kill -s KILL "$_p_c"
	fi
	_p_rc=0
	wait "$_p_c" 2> /dev/null || _p_rc=$?
	_p_t=removed
	[ -z "$(ls -A "$_p_d/tmp")" ] || _p_t=left
	printf 'ios-sim late: caller exit %s%s; child %s; grandchild %s; scratch %s\n' "$_p_rc" "$_p_h" \
		"$(verdict "$_p_d/late-child.pid")" "$(verdict "$_p_d/late-gc.pid")" "$_p_t"
	rm -f "$_p_d/flag"
	_p_n=0
	while { [ "$(verdict "$_p_d/late-child.pid")" = runs ] || [ "$(verdict "$_p_d/late-gc.pid")" = runs ]; } && [ "$_p_n" -lt 100 ]; do
		sleep 0.1
		_p_n=$((_p_n + 1))
	done
}

probe() {
	for _pc in pins ios-sim ios-device; do
		for _pk in term int kill wall; do
			probe_one "$1" "$_pc" "$_pk"
		done
	done
	probe_late "$1"
}

# stage_root <dir> [<old> <new>]: a copy of scripts/ in <dir> in which the
# primitive's ps read (bench_proc_read), once $FAKE_STATE/stage-<field>
# exists (stat: a poll of the watch; pgid: the record of a spawn), closes
# its output once ps has answered and lingers, INT ignored, until
# $FAKE_STATE/sent exists (10 s at most), once: the window in which bash
# 3.2 drops an INT, between the end of a command substitution's output and
# its reap, opened at will (FIX192). With <old> and <new>, one more edit of
# the copy's common.sh (a mutant). False when an edit does not apply.
STAGE_OLD='	_pq_o=$(TZ=UTC0 ps -o "${2:-stat}=" -o lstart= -p "$1" 2> /dev/null) || return 1'
STAGE_NEW='	_pq_o=$(TZ=UTC0 ps -o "${2:-stat}=" -o lstart= -p "$1" 2> /dev/null; _st=$?; exec >&-; if rm "${FAKE_STATE:-/nonexistent}/stage-${2:-stat}" 2> /dev/null; then trap "" INT; : > "$FAKE_STATE/at-ps"; _n=0; while [ ! -e "$FAKE_STATE/sent" ] && [ "$_n" -lt 100 ]; do sleep 0.1; _n=$((_n + 1)); done; fi; exit "$_st") || return 1'
stage_root() {
	rm -rf "$1"
	mkdir -p "$1" && cp -R "$here/scripts" "$1/scripts" || return 1
	python3 -I -c 'import sys
p = sys.argv[1]
s = open(p).read()
a = sys.argv[2:]
for old, new in zip(a[0::2], a[1::2]):
    if s.count(old) != 1: sys.exit("the stage does not apply: %d" % s.count(old))
    s = s.replace(old, new)
open(p, "w").write(s)' "$1/scripts/bench/core/common.sh" "$STAGE_OLD" "$STAGE_NEW" ${2+"$2" "$3"}
}

# ---- the Python helper's disposition probe (FIX186, slice 2) -------------

# mk_pystandin <file>: the stand-in of the Python probe: stand-in <dir>
# <ignore> records its pid once its grandchild has recorded its own; each
# writes every TERM it gets to its own record file (child.rec, gc.rec), and
# both live while <dir>/flag exists. With <ignore> 1 a TERM is recorded and
# ignored, so only a KILL ends them before the flag goes. Neither holds the
# probe's output open (a stand-in left running would hold its reader).
mk_pystandin() {
	cat > "$1" << 'EOF'
#!/bin/sh
exec > /dev/null 2>&1
d=$1 ig=$2
if [ "$ig" = 1 ]; then trap 'echo TERM >> "$d/child.rec"' TERM; else trap 'echo TERM >> "$d/child.rec"; exit 143' TERM; fi
sh -c 'd=$1 ig=$2
if [ "$ig" = 1 ]; then trap "echo TERM >> \"\$d/gc.rec\"" TERM; else trap "echo TERM >> \"\$d/gc.rec\"; exit 143" TERM; fi
echo $$ > "$d/gc.pid"
while [ -e "$d/flag" ]; do sleep 0.1; done' gc "$d" "$ig" &
while [ ! -s "$d/gc.pid" ]; do sleep 0.05; done
echo $$ > "$d/child.pid"
while [ -e "$d/flag" ]; do sleep 0.1; done
wait
EOF
	chmod +x "$1"
}

# mk_pyprobe <file>: the probe's Python side, run as python3 -I <file>
# <root> <dir> <case>, which imports <root>/scripts/lib/bounded.py as the
# callers do. Cases: timeout and ignore (bounded.run past its timeout, the
# stand-in acting on TERM or ignoring it), recycled (the identity check: a
# child whose recorded start time is not its own, as a recycled pid would
# be, gets nothing), reaped (a reaped child whose pid another process now
# holds, with ps failing, gets nothing), spawnwin (a SIGTERM to this process
# between the fork and the record), nostart (no start time read at spawn:
# the unreaped child is still stopped), partial (a timeout's Result holds
# what was written), pshang (a ps that does not answer is no answer, within
# PS_WAIT), kinds (a normal exit and a signal told apart), caller
# (bounded.run with a long timeout, for the shell to signal this process).
mk_pyprobe() {
	cat > "$1" << 'EOF'
import os, sys, time
root, d, case = sys.argv[1:4]
sys.path.insert(0, os.path.join(root, "scripts", "lib"))
import bounded  # noqa: E402
st = os.path.join(d, "standin")


def started():
    n = 0
    while not (os.path.exists(os.path.join(d, "child.pid")) and os.path.getsize(os.path.join(d, "child.pid"))) and n < 600:
        time.sleep(0.1)
        n += 1


if case in ("timeout", "ignore"):
    t = 3
    t0 = time.monotonic()
    r = bounded.run([st, d, "1" if case == "ignore" else "0"], timeout=t, grace=1 if case == "ignore" else 10)
    print("how %s, stop took %d s" % (r.how, int(time.monotonic() - t0 - t)))
elif case == "recycled":
    # its output not this probe's: the stand-ins outlive this process until the shell removes the flag
    c = bounded.spawn([st, d, "0"], stdout=bounded.subprocess.DEVNULL, stderr=bounded.subprocess.DEVNULL)
    started()
    c.start = "Thu Jan 1 00:00:00 1970"  # a start time that is not the child's: the pid is another process's
    print("stop sent %s" % bounded.stop(c, grace=1))
elif case == "reaped":
    # a reaped child whose pid a stand-in (a session leader of this probe's own) now holds, and ps
    # failing: the pid is another process's, whatever ps says, so nothing is sent
    c = bounded.spawn(["true"])
    c.popen.wait()
    other = bounded.subprocess.Popen([st, d, "0"], start_new_session=True, stdin=bounded.subprocess.DEVNULL,
                                     stdout=bounded.subprocess.DEVNULL, stderr=bounded.subprocess.DEVNULL)
    started()
    c.pid = other.pid  # as a recycled pid would be
    real = bounded.subprocess.Popen

    def failing(args, *a, **k):
        if args and args[0] == getattr(bounded, "PS", "/bin/ps"):
            raise BlockingIOError(35, "Resource temporarily unavailable")
        return real(args, *a, **k)
    bounded.subprocess.Popen = failing
    print("stop sent %s" % bounded.stop(c, grace=1))
elif case == "spawnwin":
    # this probe's own SIGTERM, sent once from the window between the fork and the record (once the
    # stand-ins have started): held until the child is recorded, and then it stops the group
    orig = bounded.proc_start
    once = []

    def term_in_window(pid):
        if not once:
            once.append(1)
            started()
            os.kill(os.getpid(), bounded.signal.SIGTERM)
        return orig(pid)
    bounded.proc_start = term_in_window
    try:
        bounded.run([st, d, "0"], timeout=60)
        print("status none")
    except SystemExit as e:
        print("status %s" % e.code)
elif case == "nostart":
    # ps gave no start time at spawn: the unreaped pid is still the child's
    c = bounded.spawn([st, d, "0"], stdout=bounded.subprocess.DEVNULL, stderr=bounded.subprocess.DEVNULL)
    started()
    c.start = None
    print("stop sent %s" % bounded.stop(c, grace=1))
elif case == "partial":
    # what the command wrote before its timeout is in the Result
    r = bounded.run(["sh", "-c", 'echo out; echo err >&2; while [ -e "$1/flag" ]; do sleep 0.1; done', "sh", d],
                    timeout=1, grace=2, capture_output=True, text=True)
    print("how %s, stdout %r, stderr %r" % (r.how, r.stdout, r.stderr))
elif case == "pshang":
    # a ps that does not answer (a stand-in that waits while the flag exists) is no answer, in a
    # bounded time
    fake = os.path.join(d, "fakeps")
    with open(fake, "w") as f:
        f.write('#!/bin/sh\nwhile [ -e "%s/flag" ]; do sleep 0.1; done\n' % d)
    os.chmod(fake, 0o755)
    bounded.PS = fake
    bounded.PS_WAIT = 1
    t0 = time.monotonic()
    s = bounded.proc_start(os.getpid())
    print("start %r in %d s" % (s, int(time.monotonic() - t0)))
elif case == "kinds":
    a = bounded.run(["sh", "-c", "exit 3"])
    b = bounded.run(["sh", "-c", "kill -USR1 $$"])
    print("exit %s %s, signal %s %s" % (a.returncode, a.how, b.signal, b.how))
elif case == "caller":
    bounded.run([st, d, "0"], timeout=600)
EOF
}

# pyverdict <dir>: "child got: <its record>, grandchild got: <its record>, left: <what runs>"
pyverdict() {
	_pv_c=$(tr '\n' ' ' 2> /dev/null < "$1/child.rec")
	_pv_g=$(tr '\n' ' ' 2> /dev/null < "$1/gc.rec")
	_pv_l=
	[ "$(verdict "$1/child.pid")" != runs ] || _pv_l="$_pv_l child"
	[ "$(verdict "$1/gc.pid")" != runs ] || _pv_l="$_pv_l grandchild"
	printf 'child got: %s, grandchild got: %s, left:%s' "${_pv_c:-nothing}" "${_pv_g:-nothing}" "${_pv_l:- none}"
}

# pyend <dir>: the flag goes, and the stand-ins end by themselves
pyend() {
	rm -f "$1/flag"
	_pe_n=0
	while { [ "$(verdict "$1/child.pid")" = runs ] || [ "$(verdict "$1/gc.pid")" = runs ]; } && [ "$_pe_n" -lt 100 ]; do
		sleep 0.1
		_pe_n=$((_pe_n + 1))
	done
}

# pyprobe_one <root> <case> [<signal>]: one line for the case
pyprobe_one() {
	_y_d=$W/y-$2${3:+-$3}-$(basename "$1")
	rm -rf "$_y_d"
	mkdir -p "$_y_d" || return 1
	: > "$_y_d/flag"
	mk_pystandin "$_y_d/standin"
	mk_pyprobe "$_y_d/probe.py"
	if [ "$2" != caller ]; then
		# bounded as the caller branch: a probe that outlives 60 s is reported hung, its flag
		# removed (so the stand-ins, and a wait on them, end), and KILLed if it still runs
		python3 -I "$_y_d/probe.py" "$1" "$_y_d" "$2" < /dev/null > "$_y_d/log" 2>&1 &
		_y_c=$!
		_y_n=0
		while kill -0 "$_y_c" 2> /dev/null && [ "$_y_n" -lt 600 ]; do
			sleep 0.1
			_y_n=$((_y_n + 1))
		done
		if kill -0 "$_y_c" 2> /dev/null; then
			_y_v=$(pyverdict "$_y_d")
			rm -f "$_y_d/flag"
			_y_n=0
			while kill -0 "$_y_c" 2> /dev/null && [ "$_y_n" -lt 100 ]; do
				sleep 0.1
				_y_n=$((_y_n + 1))
			done
			! kill -0 "$_y_c" 2> /dev/null || kill -s KILL "$_y_c"
			wait "$_y_c" 2> /dev/null
			printf 'py %s: hung (the probe outlived 60 s, its flag removed), %s\n' "$2" "$_y_v"
		else
			wait "$_y_c" 2> /dev/null
			printf 'py %s: %s, %s\n' "$2" "$(tail -n 1 "$_y_d/log")" "$(pyverdict "$_y_d")"
		fi
		pyend "$_y_d"
		return 0
	fi
	# SIGINT back to the default first: a background job of this shell starts with it ignored
	python3 -I -c 'import os, signal, sys
signal.signal(signal.SIGINT, signal.SIG_DFL)
os.execv(sys.executable, [sys.executable, "-I"] + sys.argv[1:])' "$_y_d/probe.py" "$1" "$_y_d" caller < /dev/null > "$_y_d/log" 2>&1 &
	_y_c=$!
	_y_n=0
	while [ ! -s "$_y_d/child.pid" ] && [ "$_y_n" -lt 600 ]; do
		sleep 0.1
		_y_n=$((_y_n + 1))
	done
	kill -s "$3" "$_y_c"
	_y_n=0
	while kill -0 "$_y_c" 2> /dev/null && [ "$_y_n" -lt 600 ]; do
		sleep 0.1
		_y_n=$((_y_n + 1))
	done
	_y_h=
	if kill -0 "$_y_c" 2> /dev/null; then
		_y_h=" (hung: killed by the probe)"
		kill -s KILL "$_y_c"
	fi
	_y_rc=0
	wait "$_y_c" 2> /dev/null || _y_rc=$?
	printf 'py caller %s: status %s%s, %s\n' "$3" "$_y_rc" "$_y_h" "$(pyverdict "$_y_d")"
	pyend "$_y_d"
}

PYCASES='timeout ignore recycled reaped spawnwin nostart partial pshang kinds caller-TERM caller-INT'

# pyprobe <root> [<cases>]: the cases (all of PYCASES by default), one line each
pyprobe() {
	for _yc in ${2:-$PYCASES}; do
		case $_yc in
		caller-*) pyprobe_one "$1" caller "${_yc#caller-}" ;;
		*) pyprobe_one "$1" "$_yc" ;;
		esac
	done
}

# pyjudge <probe output>: the names of the cases it fails, each with its reason
pyjudge() {
	printf '%s\n' "$1" | grep -E '^py timeout: how timeout, stop took [0-4] s, child got: TERM , grandchild got: TERM , left: none$' > /dev/null ||
		echo "py-timeout(the child and its grandchild must both get TERM first and be gone once the run times out)"
	printf '%s\n' "$1" | grep -E '^py ignore: how timeout, stop took [1-4] s, child got: TERM , grandchild got: TERM , left: none$' > /dev/null ||
		echo "py-ignore(a child and grandchild that ignore TERM must get KILL after the grace)"
	printf '%s\n' "$1" | grep -E '^py recycled: stop sent False, child got: nothing, grandchild got: nothing, left: child grandchild$' > /dev/null ||
		echo "py-recycled(a pid that is not the child's must get no signal)"
	printf '%s\n' "$1" | grep -E '^py reaped: stop sent False, child got: nothing, grandchild got: nothing, left: child grandchild$' > /dev/null ||
		echo "py-reaped(a reaped child's pid held by another process must get no signal, though ps gives nothing)"
	printf '%s\n' "$1" | grep -E '^py spawnwin: status 143, child got: TERM , grandchild got: TERM , left: none$' > /dev/null ||
		echo "py-spawnwin(a SIGTERM between the fork and the record must be held, then stop the group, TERM first)"
	printf '%s\n' "$1" | grep -E '^py nostart: stop sent True, child got: TERM , grandchild got: TERM , left: none$' > /dev/null ||
		echo "py-nostart(an unreaped child whose start time ps did not give at spawn must still be stopped)"
	printf '%s\n' "$1" | grep -E "^py partial: how timeout, stdout 'out\\\\n', stderr 'err\\\\n', " > /dev/null ||
		echo "py-partial(the output written before a timeout must be in the Result)"
	printf '%s\n' "$1" | grep -E '^py pshang: start None in [0-3] s, ' > /dev/null ||
		echo "py-pshang(a ps that does not answer must be no answer, within its bound)"
	printf '%s\n' "$1" | grep -E '^py kinds: exit 3 exit, signal 30 signal, ' > /dev/null ||
		echo "py-kinds(a normal exit and a signal must be told apart)"
	printf '%s\n' "$1" | grep -E '^py caller TERM: status 143, child got: TERM , grandchild got: TERM , left: none$' > /dev/null ||
		echo "py-caller-TERM(a SIGTERM to the caller must stop the group, TERM first)"
	printf '%s\n' "$1" | grep -E '^py caller INT: status 130, child got: TERM , grandchild got: TERM , left: none$' > /dev/null ||
		echo "py-caller-INT(a SIGINT to the caller must stop the group, TERM first)"
}

# pymutant <name> <expected case> <old> <new> [only | <cases>]: a copy of
# the helper with one change; the probe must fail <expected case> on it.
# With "only", every case runs and <expected case> must be the one it fails;
# with <cases>, only those run (a mutant that hangs costs each case 60 s).
pymutant() {
	_pm_only=
	_pm_cases=
	case ${5:-} in
	'') ;;
	only) _pm_only=1 ;;
	*) _pm_cases=$5 ;;
	esac
	_pm_d=$W/pm-$1
	rm -rf "$_pm_d"
	mkdir -p "$_pm_d/scripts/lib" || { fails="$fails $1"; return; }
	python3 -I -c 'import sys
s = open(sys.argv[1]).read()
if s.count(sys.argv[3]) != 1: sys.exit("the mutant does not apply: %d" % s.count(sys.argv[3]))
open(sys.argv[2], "w").write(s.replace(sys.argv[3], sys.argv[4]))' "$here/scripts/lib/bounded.py" "$_pm_d/scripts/lib/bounded.py" "$3" "$4" ||
		{ echo "$me: FAIL: pymutant $1 does not apply"; fails="$fails $1"; return; }
	_pm_o=$(pyprobe "$_pm_d" "$_pm_cases")
	_pm_j=$(pyjudge "$_pm_o")
	if [ -n "$_pm_only" ] && [ "$(printf '%s\n' "$_pm_j" | grep -c .)" != 1 ]; then
		echo "$me: FAIL: pymutant $1 fails more than $2: $(printf '%s\n' "$_pm_j" | tr '\n' ' ')"
		fails="$fails $1"
		return
	fi
	case $_pm_j in
	*"$2("*) echo "$me: pymutant $1: refused ($(printf '%s\n' "$_pm_j" | grep -F "$2(" | head -n 1); $(printf '%s\n' "$_pm_o" | grep -F "py $(printf '%s' "${2#py-}" | tr '-' ' '):" | head -n 1))" ;;
	*)
		echo "$me: FAIL: pymutant $1 passed $2: $(printf '%s\n' "$_pm_o" | tr '\n' ' ')"
		fails="$fails $1"
		;;
	esac
}

case ${1:-} in
guard)
	guard "${2:?usage: $0 guard <root>}"
	exit
	;;
probe)
	probe "${2:?usage: $0 probe <root>}"
	exit 0
	;;
pyprobe)
	pyprobe "${2:?usage: $0 pyprobe <root>}"
	exit 0
	;;
'') ;;
*)
	echo "usage: $0 [guard <root> | probe <root> | pyprobe <root>]" >&2
	exit 2
	;;
esac

# 1. the guard on this tree
if out=$(guard "$here"); then
	echo "$me: ok   guard"
else
	printf '%s\n' "$out" | sed "s/^/$me: FAIL: /"
	fails="$fails guard"
fi

# 2. the mutants: each must be refused in its file, for its rule
K='a KILL outside'
mutant ios-sim-kill9 scripts/ios-sim.sh "$K" '	bench_spawn_forget "$1"
}' '	kill -9 "$1" 2>/dev/null
	bench_spawn_forget "$1"
}'
mutant pins-kill9 scripts/pins.sh "$K" '    trap '"''"' HUP INT TERM
    if [ -n "${BENCH_BOUNDED_PID:-}" ]; then' '    trap '"''"' HUP INT TERM
    kill -9 "${BENCH_BOUNDED_PID:-0}" 2>/dev/null
    if [ -n "${BENCH_BOUNDED_PID:-}" ]; then'
mutant pins-kill-s-KILL scripts/pins.sh "$K" '    pb_how=$BENCH_BOUNDED_HOW' '    pb_how=$BENCH_BOUNDED_HOW; kill -s KILL "$$" 2>/dev/null'
mutant pins-kill-s-kill scripts/pins.sh "$K" '    pb_how=$BENCH_BOUNDED_HOW' '    pb_how=$BENCH_BOUNDED_HOW; kill -s kill "$$" 2>/dev/null'
mutant pins-kill-n-9 scripts/pins.sh "$K" '    pb_how=$BENCH_BOUNDED_HOW' '    pb_how=$BENCH_BOUNDED_HOW; kill -n 9 "$$" 2>/dev/null'
mutant sim-pkill-9 scripts/ios-sim.sh "$K" '	bench_spawn_forget "$1"
}' '	pkill -9 -g "$1"
	bench_spawn_forget "$1"
}'
mutant mfx-os-kill scripts/metalfx-report/mfx.py "$K" 'def parse_scenario(path):
' 'def parse_scenario(path):
    os.kill(os.getpid(), signal.SIGKILL)
'
mutant device-poll-loop scripts/ios-device.sh 'polling loop' '	[ -n "${BENCH_BOUNDED_PID:-}" ] || return 0' '	[ -n "${BENCH_BOUNDED_PID:-}" ] || return 0
	while kill -0 "$BENCH_BOUNDED_PID" 2>/dev/null; do sleep 0.2; done'
mutant device-own-watchdog scripts/ios-device.sh 'does not source' '. "$repo/scripts/bench/core/common.sh"' 'bench_bounded() { "$@" & p=$!; sleep "$1"; kill -TERM "$p"; sleep 5; kill -KILL "$p"; }'
mutant adapter-term-runner scripts/bench/adapters/quake2.sh 'sends a signal itself' '	bench_bounded "$(bench_case_wall)" "$_qr_bin" "$@" > "${_qr_log:?}" 2>&1 || _qr_st=$?' \
	'	{ "$_qr_bin" "$@" & p=$!; sleep "$BENCH_BOUNDED_QUIT"; kill -TERM "$p"; wait "$p"; } > "${_qr_log:?}" 2>&1 || _qr_st=$?'
mutant kill9-elsewhere scripts/gate-checks.sh "$K" 'gate_end() {' 'gate_end() {
	kill -9 "$@" 2>/dev/null'
mutant heredoc-in-comment scripts/ios-device.sh "$K" 'selftest()
{
' 'selftest()
{
	# a fake reads its input <<END
' 'cleanup()
{
	stop_child 2' 'cleanup()
{
	kill -9 "$$" 2>/dev/null
	stop_child 2'
# round 2: the shapes that leaked past the old region tracking; no region
# is tracked now, so each KILL after them is still refused
mutant stub-gate-reap scripts/gate-checks.sh "$K" 'gate_reap() {
	while :; do' 'gate_reap() { :; } # stub
kill -9 "$$"
gate_reap_body() {
	while :; do'
mutant subshell-gate-reap scripts/gate-checks.sh "$K" 'gate_reap() {
	while :; do' 'gate_reap() (
	:
)
kill -KILL "$$"
gate_reap_body() {
	while :; do'
mutant indented-brace scripts/ios-device.sh "$K" '}

if [ $# -eq 1 ] && [ "$1" = --selftest ]; then' '	}
kill -s KILL "$$"

if [ $# -eq 1 ] && [ "$1" = --selftest ]; then'
mutant heredoc-shapes scripts/gate-checks.sh "$K" '' '
cat <<<"x"
cat <<\EOF
	EOF
echo "\"<<EOF"
kill -9 "$$"
EOF
'
mutant quoted-kill scripts/pins.sh "$K" '    pb_how=$BENCH_BOUNDED_HOW' '    pb_how=$BENCH_BOUNDED_HOW; kill -s "KILL" "$$" 2>/dev/null'
mutant timeout-s-kill scripts/test.sh "$K" '' '
timeout -s KILL 5 true
'
mutant timeout-k scripts/test.sh "$K" '' '
timeout -k 2 5 true
'
mutant py-from-import scripts/metalfx-report/mfx.py "$K" 'def parse_scenario(path):
' 'from signal import SIGKILL
def parse_scenario(path):
' 'def sh_group(cmd, cwd=None, env=None, timeout=None):
' 'def sh_group(cmd, cwd=None, env=None, timeout=None):
    os.killpg(os.getpid(), SIGKILL)
'
mutant py-signals-enum scripts/pins/rtvar.py "$K" '' '
X = signal.Signals.SIGKILL
'
mutant py-signals-9 scripts/pins/rtvar.py "$K" '' '
X = signal.Signals(9)
'
mutant py-popen-kill scripts/upscale-report/report.py "$K" '' '
p.kill()
'
mutant py-killpg-9 scripts/pins/evidence.py "$K" '' '
os.killpg(p.pid, 9)
'
# round 3: leading zeros, a continued line, -EXIT, timeout's long prefixes
mutant kill-09 scripts/gate-checks.sh "$K" '' '
kill -09 "$$"
'
mutant kill-s-09 scripts/test.sh "$K" '' '
kill -s 09 "$$"
'
mutant kill-continued scripts/gate-checks.sh "$K" '' '
kill \
	-9 "$$"
'
mutant py-kill-continued scripts/metalfx-report/mfx.py "$K" '' '
os.kill(os.getpid(),
        9)
'
mutant kill-00 scripts/gate-checks.sh 'polling loop' '' '
while kill -00 "$pid" 2>/dev/null; do sleep 1; done
'
mutant kill-EXIT scripts/gate-checks.sh 'polling loop' '' '
while kill -EXIT "$pid" 2>/dev/null; do sleep 1; done
'
mutant kill-s-EXIT scripts/test.sh 'polling loop' '' '
kill -s EXIT "$pid" && echo alive
'
mutant timeout-kill-1 scripts/test.sh "$K" '' '
timeout --kill=1 5 true
'
mutant timeout-sig-KILL scripts/test.sh "$K" '' '
timeout --sig=KILL 5 true
'
# FIX188: a define after a continuation, a trailing comma, a comment in an open call
mutant define-after-continuation scripts/ios-sim.sh 'defines a bench_*' '' '
true; \
bench_foo() { :; }
'
mutant bench-prefix-on-signal scripts/ios-sim.sh 'defines a bench_*' '' '
bench_bench_on_signal() { :; }
'
mutant py-kill-trailing-comma scripts/metalfx-report/mfx.py "$K" '' '
os.kill(os.getpid(), 9,)
'
mutant py-kill-comment-in-parens scripts/metalfx-report/mfx.py "$K" '' '
os.kill(os.getpid(),
    # why
    9)
'
# FIX186 slice 2: a caller left on a raw subprocess call with a timeout
# (its KILL, inside Python, goes to the direct child only)
P='a subprocess call with a timeout'
mutant py-run-timeout scripts/upscale-report/report.py "$P" '' '
p = subprocess.run(["true"], capture_output=True,
                   timeout=10)
'
mutant py-check-output-timeout scripts/pins/rtvar.py "$P" '' '
out = subprocess.check_output(["true"], timeout=5)
'
mutant py-run-timeout-mfx scripts/metalfx-report/mfx.py "$P" '' '
subprocess.run(["true"], timeout=SCENARIO_READ_SECONDS)
'
mutant py-run-timeout-helper scripts/lib/bounded.py "$P" '' '
subprocess.run(["true"], timeout=1)
'
# FIX186: a TERM-then-KILL through a variable signal, in each spelling
V='a signal named through a variable'
mutant varsig-s-quoted scripts/gate-checks.sh "$V" '' '
for sig in TERM KILL; do kill -s "$sig" -- "-$g"; sleep 1; done
'
mutant varsig-s-braced scripts/gate-checks.sh "$V" '' '
for sig in TERM KILL; do kill -s "${sig}" "$p"; sleep 1; done
'
mutant varsig-dash-quoted scripts/gate-checks.sh "$V" '' '
for sig in TERM KILL; do kill -"$sig" "$p"; sleep 1; done
'
mutant varsig-dash-bare scripts/gate-checks.sh "$V" '' '
for sig in TERM KILL; do kill -$sig "$p"; sleep 1; done
'
mutant varsig-signal-long scripts/test.sh "$V" '' '
kill --signal="$sig" "$p"
'
# (the SIG prefix, a backtick, and a first argument that is the variable)
mutant varsig-sig-quoted scripts/gate-checks.sh "$V" '' '
kill -SIG"$sig" "$p"
'
mutant varsig-sig-bare scripts/gate-checks.sh "$V" '' '
kill -SIG$sig "$p"
'
mutant varsig-s-sig-quoted scripts/bench/core/lock.sh "$V" '' '
kill -s SIG"$sig" "$p"
'
mutant varsig-backtick scripts/gate-checks.sh "$V" '' '
kill -`printf KILL` "$p"
'
mutant varsig-s-backtick scripts/test.sh "$V" '' '
kill -s `printf KILL` "$p"
'
mutant varsig-first-flag scripts/gate-checks.sh "$V" '' '
kill $flag "$p"
'
mutant varsig-first-all scripts/bench/core/lock.sh "$V" '' '
kill "$@"
'
# the pinned reasons: a marker is a line whose reason the check pins, once
mutant marker-in-zero-file scripts/pins.sh 'does not pin: borrowed' '' '
kill -9 "$$" # bounded-run: allowed: borrowed
'
mutant marker-one-more scripts/gate-checks.sh 'does not pin: one more' '' '
kill -0 "$$" # bounded-run: allowed: one more
'
mutant marker-reason-twice scripts/bench/core/lock.sh 'is on 2 lines' '' '
kill -0 "$$" # bounded-run: allowed: the lock: its holder group still has a member; it signals none
'
mutant marker-one-less scripts/bench/core/lock.sh 'and no line carries it' ' # bounded-run: allowed: the lock: its holder group still has a member; it signals none' ''
mutant marker-moved scripts/bench/core/lock.sh 'no rule refuses' ' # bounded-run: allowed: the lock: its holder group still has a member; it signals none' '' '	bench_pid_alive "$_lv_c" && return 0' '	bench_pid_alive "$_lv_c" && return 0 # bounded-run: allowed: the lock: its holder group still has a member; it signals none'
mutant marker-joined scripts/ios-device.sh 'a marker covers one' 'if kill -0 "$spid" 2>/dev/null; then # bounded-run: allowed: selftest: the script under test outlived the signal
				kill -9 "$spid" 2>/dev/null # bounded-run: allowed: selftest: ends the script under test that hung' 'if kill -0 "$spid" 2>/dev/null; then kill -9 "$spid" 2>/dev/null # bounded-run: allowed: selftest: the script under test outlived the signal' '' '
kill -9 "$$" # bounded-run: allowed: selftest: ends the script under test that hung
'
mutant marker-appended scripts/bench/core/lock.sh 'a marker covers one' 'kill -0 -- "-$_lv_c" 2> /dev/null #' 'kill -0 -- "-$_lv_c" 2> /dev/null && kill -9 -- "-$_lv_c" #'
mutant marker-in-echo scripts/gate-checks.sh 'followed by a quote' 'gate_end() {' 'gate_end() {
	kill -9 "$	" 2>/dev/null; echo "# bounded-run: allowed: x"'
# the source rule
mutant pins-lib-reassigned scripts/pins.sh 'not assigned exactly once' 'pins_lib_file="$root/scripts/bench/core/common.sh"' 'pins_lib_file="$root/scripts/bench/core/common.sh"
pins_lib_file=/dev/null'
mutant source-in-heredoc scripts/ios-sim.sh 'does not source' '. "$repo/scripts/bench/core/common.sh"
exec 8>&-' 'cat <<EOF
. "$repo/scripts/bench/core/common.sh"
EOF
exec 8>&-'
mutant unreadable scripts/pins/scenario-cases.txt 'cannot be read'
mutant unreadable-dir scripts 'cannot list every file'

# 2b. a misspelled until test is refused before the command starts
u=$(sh -c '. "$1/scripts/bench/core/common.sh"; BENCH_BOUNDED_UNTIL=no_such_test bench_bounded 5 true' sh "$here" 2>&1)
ust=$?
w=$(sh -c '. "$1/scripts/bench/core/common.sh"; st=0; bench_watch "$$" "" 1 no_such_test || st=$?; echo "status $st"' sh "$here" 2>&1)
if [ "$ust" -eq 2 ] && printf '%s\n' "$u" | grep -F 'BENCH_BOUNDED_UNTIL names no function: no_such_test' > /dev/null &&
	printf '%s\n' "$w" | grep -F 'status 4' > /dev/null && printf '%s\n' "$w" | grep -F 'until test no_such_test is not defined' > /dev/null; then
	echo "$me: ok   until: an undefined test is refused (bench_bounded status 2, bench_watch status 4)"
else
	echo "$me: FAIL: until: an undefined test must be refused; got $ust: $u / $w"
	fails="$fails until"
fi
# an until test gone by a later poll (bench_watch status 4: the test unsets
# itself, the quit time starts a second watch) is refused, never "wall"
g=$(sh -c '. "$1/scripts/bench/core/common.sh"; gone() { unset -f gone; return 1; }; BENCH_STOP_GRACE=1; BENCH_BOUNDED_QUIT=1; BENCH_BOUNDED_UNTIL=gone
st=0; bench_bounded 30 sleep 30 || st=$?; echo "status $st how $BENCH_BOUNDED_HOW"' sh "$here" 2>&1)
gst=$?
if [ "$gst" -eq 2 ] && printf '%s\n' "$g" | grep -F 'BENCH_BOUNDED_UNTIL names no function at a poll: gone; stopped the command' > /dev/null &&
	! printf '%s\n' "$g" | grep -F -e 'how wall' -e 'outlived its wall time' > /dev/null; then
	echo "$me: ok   until: a test gone at a later poll is refused (status 2), not taken as the wall time"
else
	echo "$me: FAIL: until: a test gone at a later poll must be refused with status 2; got $gst: $(printf '%s\n' "$g" | tr '\n' ' ')"
	fails="$fails until-gone"
fi
# the same inside one watch (no quit time): the test unsets itself at its
# 3rd call, and the next poll refuses it, never "wall"
g=$(sh -c '. "$1/scripts/bench/core/common.sh"; n=0; gone() { n=$((n + 1)); [ "$n" -lt 3 ] || unset -f gone; return 1; }; BENCH_STOP_GRACE=1; BENCH_BOUNDED_UNTIL=gone
st=0; bench_bounded 5 sleep 30 || st=$?; echo "status $st how $BENCH_BOUNDED_HOW"' sh "$here" 2>&1)
gst=$?
if [ "$gst" -eq 2 ] && printf '%s\n' "$g" | grep -F 'BENCH_BOUNDED_UNTIL names no function at a poll: gone; stopped the command' > /dev/null &&
	! printf '%s\n' "$g" | grep -F -e 'how wall' -e 'outlived its wall time' > /dev/null; then
	echo "$me: ok   until: a test gone inside a watch is refused (status 2), not taken as the wall time"
else
	echo "$me: FAIL: until: a test gone inside a watch must be refused with status 2; got $gst: $(printf '%s\n' "$g" | tr '\n' ' ')"
	fails="$fails until-gone-in-watch"
fi

# 2c. the supervisor's staged cases (TASK221, slice 2): bench_bounded runs
# its command under the process supervisor (scripts/lib/supervise.c), and
# each case waits on the supervisor's own event lines (BENCH_BOUNDED_TAP, a
# FIFO), then on end of file there (tests/lib/proc.sh): no sleep, no ps.
# The caller is a script as ios-device.sh is one (its trap, bench_deferred,
# bench_stop_group); its standard output is the FIFO too, so end of file
# means the caller and the supervisor have both exited. PROC_BOUND is the
# failure bound: what has not ended by then is killed and failed.
. "$here/tests/lib/proc.sh"
PROC_BOUND=10
SUPERVISE_CACHE=$W/svcache
export SUPERVISE_CACHE
cat > "$W/svcaller" << 'EOF'
# svcaller <root> <wall> <until or -> <out> <command...>
r=$1 w=$2 u=$3 o=$4
shift 4
. "$r/scripts/bench/core/common.sh"
exec 8>&-
on_sig() {
	echo "trap $1"
	if bench_deferred "$1"; then return 0; fi
	trap '' INT TERM HUP
	if [ -n "${BENCH_BOUNDED_PID:-}" ]; then bench_stop_group "$BENCH_BOUNDED_PID" "" "${BENCH_BOUNDED_REAPED:-}" || :; fi
	exit "$1"
}
bench_on_signal() { on_sig "$1"; }
trap 'on_sig 130' INT
trap 'on_sig 143' TERM
polled() { echo polled; return 1; }
BENCH_STOP_GRACE=${SV_GRACE:-1}
[ "$u" = - ] || BENCH_BOUNDED_UNTIL=$u
st=0
bench_bounded "$w" bench_to "$o" "$o" "$@" || st=$?
echo "returned $st"
EOF
# sv_first <pattern>: reads the case's lines up to the first that matches
# <pattern> (true) or ends the supervisor's stop, "grace-end" or
# "group-empty" (false); false at end of file or at the bound too
sv_first() {
	while proc_await '*'; do
		case $PROC_LINE in grace-end | group-empty) return 1 ;; esac
		# shellcheck disable=SC2254 # the pattern is the caller's
		case $PROC_LINE in $1) return 0 ;; esac
	done
	return 1
}
# sv_case <root> <name> <wall> <until or -> <event> <command> [<line>...]:
# runs the caller on <command> (sh -c), waits for the supervisor's
# "spawned" line, then for the line <event> (a case pattern), sends the
# caller INT, waits for each <line> (a case pattern) in turn, each before
# the supervisor's stop has ended (sv_first), and then for end of file.
# The caller's grace is sv_grace (1); the end of file may take that grace
# on top of the bound. Sets sv_rc (the caller's status), sv_pg (the
# command's process group, from "spawned"), sv_s (the seconds from the INT
# to the end of file) and sv_why (empty when every wait held). What
# outlived the bound is killed: the caller, and the command's group.
sv_case() {
	_sd=$W/sv-$2-$(basename "$1")
	rm -rf "$_sd"
	mkdir -p "$_sd/tmp" && mkfifo "$_sd/fifo" || return 1
	proc_open "$_sd/fifo"
	env TMPDIR="$_sd/tmp" BENCH_BOUNDED_TAP="$_sd/fifo" SV_GRACE="${sv_grace:-1}" python3 -I -c 'import os, signal, sys
signal.signal(signal.SIGINT, signal.SIG_DFL)
os.execvp(sys.argv[1], sys.argv[1:])' /bin/bash "$W/svcaller" "$1" "$3" "$4" "$_sd/out" sh -c "$6" < /dev/null > "$_sd/fifo" 2> "$_sd/err" &
	_sp=$!
	sv_pg= sv_why= sv_s=
	if ! proc_await 'spawned *'; then
		sv_why="no spawned event from the supervisor within 10 s"
	else
		sv_pg=${PROC_LINE##*pgid=}
		proc_alone
		if [ "$5" != spawned ] && ! proc_await "$5"; then
			sv_why="no line $5 within 10 s"
		else
			kill -s INT "$_sp"
			_st0=$(date +%s)
			shift 6
			for _sl in "$@"; do
				sv_first "$_sl" || {
					sv_why="no line $_sl after the INT before the supervisor's stop ended (the last line read: ${PROC_LINE:-none})"
					break
				}
			done
			if [ -z "$sv_why" ]; then
				_sb=$PROC_BOUND
				PROC_BOUND=$((PROC_BOUND + ${sv_grace:-1}))
				proc_eof || sv_why="the caller or the supervisor outlived the INT by $PROC_BOUND s"
				PROC_BOUND=$_sb
			fi
			sv_s=$(($(date +%s) - _st0))
		fi
	fi
	if [ -n "$sv_why" ]; then
		kill -s KILL "$_sp" 2> /dev/null
		case $sv_pg in '' | *[!0-9]*) ;; *) kill -s KILL -- "-$sv_pg" 2> /dev/null ;; esac
	fi
	sv_rc=0
	wait "$_sp" 2> /dev/null || sv_rc=$?
	proc_close
	return 0
}
# sv_left: what the case left: the command's group, and the files in its TMPDIR
sv_left() {
	_sl=
	case $sv_pg in '' | *[!0-9]*) ;; *) if kill -0 -- "-$sv_pg" 2> /dev/null; then _sl="the command's group $sv_pg runs"; fi ;; esac
	if [ -n "$(ls -A "$_sd/tmp")" ]; then _sl="${_sl:+$_sl; }scratch files: $(ls -A "$_sd/tmp" | tr '\n' ' ')"; fi
	printf '%s\n' "$_sl"
}
# sv_judge <name>: the verdict of the last sv_case: exit 130 with nothing left
sv_judge() {
	_sj=$sv_why
	[ -n "$_sj" ] || [ "$sv_rc" -eq 130 ] || _sj="the caller exited $sv_rc, not 130"
	[ -n "$_sj" ] || _sj=$(sv_left)
	if [ -n "$_sj" ]; then
		echo "$me: FAIL: sv $1: $_sj ($(tr '\n' '|' < "$_sd/err" | cut -c1-300))"
		fails="$fails sv-$1"
	else
		echo "$me: ok   sv $1: exit 130 in $sv_s s, nothing left"
	fi
}
# an INT at "spawned": the caller exits 130, and its command's group is gone
sv_case "$here" spawned 30 - spawned 'exec sleep 30'
sv_judge spawned
# FIX195 (#726): an INT once the command has exited and left a member that
# ignores TERM (the end-group window of the shell watch) runs the trap at
# once, and the trap's stop sends the supervisor TERM: the caller's "trap"
# line and then the supervisor's "signal name=TERM" both come before the
# supervisor's own stop of the member ends (correctness I1, round 1). The
# grace is above the bound, so a trap that waited for that stop, or a stop
# that sent no TERM, fails at the bound, never by a race; the member is
# killed at the grace's end, and nothing is left
sv_grace=$((PROC_BOUND + 2))
sv_case "$here" endgroup 30 - 'exited *' '(trap "" TERM; exec sleep 30) & exit 0' 'trap 130' 'signal name=TERM'
sv_grace=
sv_judge endgroup
# FIX199 (#741): an INT while the until test's poll waits leaves no scratch
# file in TMPDIR (a bench-poll directory, the events file) and nothing running
sv_case "$here" pollint 30 polled polled 'exec sleep 30'
sv_judge pollint

# 3. the probe: TERM and INT stop the child and the grandchild; the wall
# time does too; nothing can run after a KILL of the caller, so both stay
pout=$(probe "$here")
printf '%s\n' "$pout" | sed "s/^/$me: probe: /"
for c in pins ios-sim ios-device; do
	for k in term int wall; do
		printf '%s\n' "$pout" | grep -E "^$c $k: caller exit [1-9][0-9]*; child gone; grandchild gone$" > /dev/null ||
			{ echo "$me: FAIL: probe $c $k: the caller must fail and leave nothing running"; fails="$fails probe-$c-$k"; }
	done
	printf '%s\n' "$pout" | grep -E "^$c kill: caller exit 137; child runs; grandchild runs$" > /dev/null ||
		{ echo "$me: FAIL: probe $c kill: the stand-ins outlive a KILL of the caller"; fails="$fails probe-$c-kill"; }
done
# a second signal in ios-sim's late cleanup call stops that call too, and
# the scratch directory still goes (FIX131)
printf '%s\n' "$pout" | grep -E "^ios-sim late: caller exit [1-9][0-9]*; child gone; grandchild gone; scratch removed$" > /dev/null ||
	{ echo "$me: FAIL: probe ios-sim late: a second signal in cleanup's late call must leave nothing running and no scratch"; fails="$fails probe-ios-sim-late"; }

# The dropped-INT windows of the shell watch and spawn (FIX192), opened in
# ios-device.sh's bounded runs by a staged ps read, are gone with them: its
# bounded runs no longer read ps (TASK221). The INT cases above stand in.

# sv_stage <dir> <file> <old> <new>: a copy of scripts/ in <dir>, <old>
# replaced by <new> once in <file> (a path under scripts/); false when the
# edit does not apply
sv_stage() {
	rm -rf "$1"
	mkdir -p "$1" && cp -R "$here/scripts" "$1/scripts" || return 1
	python3 -I -c 'import sys
p, old, new = sys.argv[1:4]
s = open(p).read()
if s.count(old) != 1: sys.exit("the edit does not apply: %d" % s.count(old))
open(p, "w").write(s.replace(old, new))' "$1/$2" "$3" "$4"
}

# A supervisor that could not empty its command's group (its 126) keeps the
# lock while that group has a member, and says so (TASK221): bench_bounded
# records the group in the lock state, and the lock shell's release waits
# for it. The supervisor is a stand-in, planted in a tool cache of its own
# under the name sv_path gives the real one: it starts the command in a
# process group of its own, says so on the case's FIFO (FAKE_TAP, "fake
# pid=<its pid> pgid=<the group>"), writes its "spawned" event (also to
# the FIFO) and exits 126 at once. The command reads a FIFO, so it runs
# until the case writes there.
# sv_lock126 <root> [<mode>]: the case; <mode> (review round 2) is one of
#   term     the caller's TERM comes after the supervisor's 126 is
#            collected and before the group is recorded: an awk stand-in
#            first on the caller's PATH sends its parent TERM when it is
#            asked for the "reaped" event (bench_bounded_said reaped, the
#            first fork after the collect), and the caller's trap stops the
#            bounded run as bench.sh's does (correctness I1)
#   nospawn  the stand-in writes no "spawned" event (a failed events write,
#            or the supervisor's "cannot watch the command"), so there is
#            no group to record: the lock is held by a record that names
#            its release by hand, also once the command has ended, until
#            the case removes it (security I1)
# Sets l126_why (empty: it held). What the case started is killed by its
# group id, which the stand-in said.
sv_bin=$(SV_SRC_DIR=$here/scripts/lib && . "$here/scripts/lib/supervise.sh" && SV_SRC_DIR=$here/scripts/lib && sv_path) || sv_bin=
cat > "$W/fake126" << 'EOF'
#!/bin/sh
while [ "$#" -gt 0 ] && [ "$1" != -- ]; do shift; done
shift
python3 -I -c 'import os, sys
os.setpgid(0, 0)
os.execvp(sys.argv[1], sys.argv[1:])' "$@" < /dev/null > /dev/null 2>&1 9>&- &
echo "fake pid=$$ pgid=$!" >> "$FAKE_TAP"
if [ -z "${FAKE_NOSPAWN:-}" ]; then
	echo "spawned pid=$! pgid=$!" >&9
	echo "spawned pid=$! pgid=$!" >> "$FAKE_TAP"
fi
echo "group-left count=1" >&9
exit 126
EOF
mkdir -p "$W/awkshim" || exit 1
printf '#!/bin/sh\nfor a; do if [ "$a" = e=reaped ]; then kill -s TERM "$PPID"; break; fi; done\nexec %s "$@"\n' "$(command -v awk)" > "$W/awkshim/awk" && chmod 700 "$W/awkshim/awk" || exit 1
sv_lock126() {
	_l6m=${2:-plain}
	_ld=$W/sv-126-$_l6m-$(basename "$1")
	l126_why=
	rm -rf "$_ld"
	mkdir -p "$_ld/cache" "$_ld/state" "$_ld/tmp" && chmod 700 "$_ld/cache" && mkfifo "$_ld/fifo" "$_ld/hold" || return 1
	cp "$W/fake126" "$_ld/cache/${sv_bin##*/}" && chmod 700 "$_ld/cache/${sv_bin##*/}" || return 1
	_lpath=$PATH _lns=
	case $_l6m in
	term) _lpath=$W/awkshim:$PATH ;;
	nospawn) _lns=1 ;;
	esac
	proc_open "$_ld/fifo"
	# the lock shell's lines go to the FIFO too; the command says
	# "command-ended" there as it ends, so the lock shell's last line
	# ("... have ended ...; freeing the lock") must come after it
	env TMPDIR="$_ld/tmp" SUPERVISE_CACHE="$_ld/cache" BENCH_STATE="$_ld/state" FAKE_TAP="$_ld/fifo" FAKE_NOSPAWN="$_lns" \
		sh "$1/scripts/bench/bench.sh" lock run t126 -- env PATH="$_lpath" sh -c '. "$1/scripts/bench/core/common.sh"; . "$1/scripts/bench/core/lock.sh"
trap '\''trap "" TERM; bench_stop_group "$BENCH_BOUNDED_PID" || :; echo "caller trapped 143"; exit 143'\'' TERM
st=0; bench_bounded 30 sh -c "read x < \"\$0\"; echo command-ended >> \"\$1\"" "$2" "$3" || st=$?; echo "caller bounded $st $BENCH_BOUNDED_HOW"' x "$1" "$_ld/hold" "$_ld/fifo" < /dev/null > "$_ld/fifo" 2>&1 &
	_lp=$!
	_lg= _lf=
	_lw="caller bounded 125 untold"
	[ "$_l6m" != term ] || _lw="caller trapped 143"
	if ! proc_await 'fake *'; then
		l126_why="the stand-in supervisor did not start"
	else
		_lg=${PROC_LINE##*pgid=}
		_lf=${PROC_LINE#fake pid=}
		_lf=${_lf%% *}
		proc_alone
		_lk=$_lg
		if [ "$_l6m" = nospawn ]; then
			_lk=$_lf
			if ! proc_await '*by hand*'; then
				l126_why="the lock was not kept: no line names the record's release by hand"
			else
				case $PROC_LINE in
				*"rm -f '"*"/lock/untold.$_lf'") ;;
				*) l126_why="the lock was not kept by a record named to be removed by hand: $PROC_LINE" ;;
				esac
			fi
		elif ! proc_await 'spawned *'; then
			l126_why="no spawned event from the stand-in"
		fi
		if [ -n "$l126_why" ]; then
			:
		elif ! proc_await 'caller *'; then
			l126_why="the bounded call did not return"
		elif [ "$PROC_LINE" != "$_lw" ]; then
			l126_why="the caller said \"$PROC_LINE\", not \"$_lw\""
		else
			_ll=$(env BENCH_STATE="$_ld/state" sh "$1/scripts/bench/bench.sh" lock status 2>&1)
			case $_ll in
			lock=held*" untold=$_lk"*) ;;
			*) l126_why="the lock was not kept for the group the supervisor could not empty: $_ll" ;;
			esac
		fi
		if [ -z "$l126_why" ] && ! proc_await 'bench: keeping the lock while recorded pid*'; then
			l126_why="the lock shell did not say it kept the lock"
		fi
	fi
	# the command may end now: the lock shell frees the lock once its group
	# is gone, or, for a record kept by hand, once the case removes it
	exec 7<> "$_ld/hold"
	echo go >&7
	exec 7>&-
	if [ -z "$l126_why" ]; then
		if ! proc_await '*ended*'; then
			l126_why="neither the command nor the lock shell said it ended"
		elif [ "$PROC_LINE" != command-ended ]; then
			l126_why="the lock was not kept until the group was gone: \"$PROC_LINE\" came first"
		fi
	fi
	if [ -z "$l126_why" ] && [ "$_l6m" = nospawn ]; then
		# the record holds by itself, not by the group: still held, then
		# removed by hand as its line says
		_ll=$(env BENCH_STATE="$_ld/state" sh "$1/scripts/bench/bench.sh" lock status 2>&1)
		case $_ll in
		lock=held*" untold=$_lf"*) ;;
		*) l126_why="the lock was not kept by hand once the command ended: $_ll" ;;
		esac
	fi
	# the release by hand (a case that held no record removes nothing)
	if [ "$_l6m" = nospawn ] && [ -n "$_lf" ]; then rm -f "$_ld/state/lock/untold.$_lf"; fi
	if ! proc_eof; then
		l126_why=${l126_why:-"the lock shell did not exit once the group was gone"}
		kill -s KILL "$_lp" 2> /dev/null
	fi
	case $_lg in '' | *[!0-9]*) ;; *) kill -s KILL -- "-$_lg" 2> /dev/null ;; esac
	wait "$_lp"
	proc_close
	if [ -z "$l126_why" ]; then
		_ll=$(env BENCH_STATE="$_ld/state" sh "$1/scripts/bench/bench.sh" lock status 2>&1)
		[ "$_ll" = lock=free ] || l126_why="the lock was not freed once the group was gone: $_ll"
	fi
}
if [ -z "$sv_bin" ]; then
	echo "$me: FAIL: sv 126: no supervisor built"
	fails="$fails sv-126"
else
	sv_lock126 "$here"
	if [ -n "$l126_why" ]; then
		echo "$me: FAIL: sv 126: $l126_why"
		fails="$fails sv-126"
	else
		echo "$me: ok   sv 126: the lock is kept while the group runs, said, and freed after"
	fi
	# the mutants, each with the lock freed while the group runs: the group
	# not recorded on a 126; the record not written; the lock shell's
	# release not waiting for it (FIX184's two, moved here from check-bench)
	for _lm in \
		'lock-freed-on-126|scripts/bench/core/common.sh|	*) bench_record_untold "$_bu_g" group ;;|	*) : ;;' \
		'record-not-written|scripts/bench/core/lock.sh|mv -f "$_lk_t" "${BENCH_LOCK_DIR:?}/untold.$1"|false' \
		'release-no-wait|scripts/bench/core/lock.sh|while bench_lock_untold_live; do bench_nap 1; done|:'; do
		_lmn=${_lm%%|*} _lmr=${_lm#*|}
		_lmf=${_lmr%%|*} _lmr=${_lmr#*|}
		if sv_stage "$W/stage-$_lmn" "$_lmf" "${_lmr%%|*}" "${_lmr#*|}"; then
			sv_lock126 "$W/stage-$_lmn"
			case $l126_why in
			'the lock was not kept'* | 'the lock shell did not say'*) echo "$me: mutant $_lmn: refused ($l126_why)" ;;
			*)
				echo "$me: FAIL: mutant $_lmn passed the 126 case: ${l126_why:-held}"
				fails="$fails $_lmn"
				;;
			esac
		else
			echo "$me: FAIL: mutant $_lmn does not apply"
			fails="$fails $_lmn"
		fi
	done
	# review round 2: a 126 frees the lock only on positive evidence, so a
	# TERM between the collected 126 and its record, and a 126 that named
	# no group, both keep it
	sv_lock126 "$here" term
	if [ -n "$l126_why" ]; then
		echo "$me: FAIL: sv 126 term: $l126_why"
		fails="$fails sv-126-term"
	else
		echo "$me: ok   sv 126 term: a TERM after the 126 is collected keeps the lock while the group runs"
	fi
	sv_lock126 "$here" nospawn
	if [ -n "$l126_why" ]; then
		echo "$me: FAIL: sv 126 nospawn: $l126_why"
		fails="$fails sv-126-nospawn"
	else
		echo "$me: ok   sv 126 nospawn: a 126 with no group named keeps the lock until it is released by hand"
	fi
fi
# A group record whose id a later process group has taken holds nothing
# (security I2, round 1), as a pid record's does: the group is started
# first, in a group of its own (its "up" line on a FIFO says so), then
# two records name it: one written before it started (a fixed past time),
# which must hold nothing, and one written after (a fixed future time),
# which must hold the lock while the group has a member. Sets gr_why.
sv_reuse() {
	_rd=$W/sv-reuse-$(basename "$1")
	gr_why=
	rm -rf "$_rd"
	mkdir -p "$_rd/old" "$_rd/new" && mkfifo "$_rd/fifo" "$_rd/hold" || return 1
	proc_open "$_rd/fifo"
	python3 -I -c 'import os, sys
os.setpgid(0, 0)
os.execvp(sys.argv[1], sys.argv[1:])' sh -c 'echo up; read x < "$0"' "$_rd/hold" < /dev/null > "$_rd/fifo" 2> /dev/null &
	_rg=$!
	if ! proc_await up; then
		gr_why="the group did not start"
	else
		printf 'pid=%s\nstart=-\nwritten=Mon Jan 1 00:00:00 2024\ntoken=t\ngroup=%s\n' "$_rg" "$_rg" > "$_rd/old/untold.$_rg"
		printf 'pid=%s\nstart=-\nwritten=Fri Jan 1 00:00:00 2100\ntoken=t\ngroup=%s\n' "$_rg" "$_rg" > "$_rd/new/untold.$_rg"
		_rq='. "$1/scripts/bench/core/common.sh"; . "$1/scripts/bench/core/lock.sh"; BENCH_LOCK_DIR=$2; r=0; bench_lock_untold_live || r=$?; echo "$r [$BENCH_LOCK_UNTOLD]"'
		_ro=$(sh -c "$_rq" sh "$1" "$_rd/old" 2>&1)
		_rn=$(sh -c "$_rq" sh "$1" "$_rd/new" 2>&1)
		[ "$_ro" = "1 []" ] || gr_why="a record written before its group started holds the lock: $_ro"
		[ "$_rn" = "0 [$_rg]" ] || gr_why="${gr_why:+$gr_why; }a record written after its group started does not hold the lock: $_rn"
	fi
	exec 7<> "$_rd/hold"
	echo go >&7
	exec 7>&-
	wait "$_rg" 2> /dev/null
	proc_close
}
sv_reuse "$here"
if [ -n "$gr_why" ]; then
	echo "$me: FAIL: group reuse: $gr_why"
	fails="$fails group-reuse"
else
	echo "$me: ok   group reuse: a group id taken after the record was written holds nothing"
fi
# the mutant: a group record is live by kill -0 alone again
if sv_stage "$W/stage-reuse" scripts/bench/core/lock.sh '			if bench_lock_untold_reused "$_lu_p" "$(bench_kv "$_lu_h" written)"; then continue; fi
			BENCH_LOCK_UNTOLD="${BENCH_LOCK_UNTOLD:+$BENCH_LOCK_UNTOLD }$_lu_p"' '			BENCH_LOCK_UNTOLD="${BENCH_LOCK_UNTOLD:+$BENCH_LOCK_UNTOLD }$_lu_p"'; then
	sv_reuse "$W/stage-reuse"
	case $gr_why in
	'a record written before its group started holds the lock'*) echo "$me: mutant group-reuse-unchecked: refused ($gr_why)" ;;
	*)
		echo "$me: FAIL: mutant group-reuse-unchecked passed the group reuse case: ${gr_why:-held}"
		fails="$fails group-reuse-unchecked"
		;;
	esac
else
	echo "$me: FAIL: mutant group-reuse-unchecked does not apply"
	fails="$fails group-reuse-unchecked"
fi

# the mutant: sv_stop sends the supervisor no TERM, so a caller's INT
# waits for the command to the wall time (30 s, past the 10 s bound), and
# in the endgroup case no "signal name=TERM" comes before the stop's end
if sv_stage "$W/stage-noterm" scripts/lib/supervise.sh '	[ -z "$SV_PID" ] || kill -s TERM "$SV_PID" 2> /dev/null || :' '	:'; then
	sv_case "$W/stage-noterm" spawned 30 - spawned 'exec sleep 30'
	case $sv_why in
	*'outlived the INT'*) echo "$me: mutant sv-no-term: refused by spawned ($sv_why)" ;;
	*)
		echo "$me: FAIL: mutant sv-no-term (sv_stop sends no TERM) passed the spawned case: ${sv_why:-exit $sv_rc}"
		fails="$fails sv-no-term"
		;;
	esac
	sv_grace=$((PROC_BOUND + 2))
	sv_case "$W/stage-noterm" endgroup 30 - 'exited *' '(trap "" TERM; exec sleep 30) & exit 0' 'trap 130' 'signal name=TERM'
	sv_grace=
	case $sv_why in
	'no line signal name=TERM'*) echo "$me: mutant sv-no-term: refused by endgroup ($sv_why)" ;;
	*)
		echo "$me: FAIL: mutant sv-no-term (sv_stop sends no TERM) passed the endgroup case: ${sv_why:-exit $sv_rc}"
		fails="$fails sv-no-term-endgroup"
		;;
	esac
else
	echo "$me: FAIL: mutant sv-no-term does not apply"
	fails="$fails sv-no-term"
fi

# The lock run's watch, with ps silent (correctness I2, round 1): the
# command leads its own group, so the watch says a stop goes to that group
# while it has a member (bench_watch's leader note), and a TERM to the lock
# shell stops the command before the lock is free. ps is a stand-in that
# exits 1 and prints nothing; the lock state is the case's own. The lock
# shell's lines and the command's "cmd <pid>" go to a FIFO. Sets ln_why.
sv_leadnote() {
	_nd=$W/sv-note-$(basename "$1")
	ln_why=
	rm -rf "$_nd"
	mkdir -p "$_nd/shim" "$_nd/state" && mkfifo "$_nd/fifo" || return 1
	printf '#!/bin/sh\nexit 1\n' > "$_nd/shim/ps" && chmod +x "$_nd/shim/ps" || return 1
	proc_open "$_nd/fifo"
	env PATH="$_nd/shim:$PATH" BENCH_STATE="$_nd/state" sh "$1/scripts/bench/bench.sh" lock run tnote -- sh -c 'echo "cmd $$"; exec sleep 30' < /dev/null > "$_nd/fifo" 2>&1 &
	_np=$!
	_ng=
	if ! proc_await 'cmd *'; then
		ln_why="the command did not start"
	else
		_ng=${PROC_LINE#cmd }
		proc_alone
		if ! proc_await '*cannot tell whether pid*'; then
			ln_why="the watch said nothing of a silent ps"
		else
			case $PROC_LINE in
			*'a stop goes to its process group while that has a member'*) ;;
			*) ln_why="the watch did not say the stop goes to the command's group: $PROC_LINE" ;;
			esac
		fi
	fi
	kill -s TERM "$_np" 2> /dev/null
	proc_eof || {
		ln_why=${ln_why:-"the lock shell or its command outlived the TERM by $PROC_BOUND s"}
		kill -s KILL "$_np" 2> /dev/null
	}
	case $_ng in '' | *[!0-9]*) ;; *)
		if kill -0 -- "-$_ng" 2> /dev/null; then
			ln_why=${ln_why:-"the command's group outlived the lock shell"}
			kill -s KILL -- "-$_ng" 2> /dev/null
		fi
		;;
	esac
	wait "$_np" 2> /dev/null
	proc_close
	if [ -z "$ln_why" ]; then
		_nl=$(env BENCH_STATE="$_nd/state" sh "$1/scripts/bench/bench.sh" lock status 2>&1)
		[ "$_nl" = lock=free ] || ln_why="the lock was not freed: $_nl"
	fi
}
sv_leadnote "$here"
if [ -n "$ln_why" ]; then
	echo "$me: FAIL: lock note: $ln_why"
	fails="$fails lock-note"
else
	echo "$me: ok   lock note: ps silent, the watch says the stop goes to the group, and TERM stops it"
fi
# the mutant: the watch never says the stop goes to the group
if sv_stage "$W/stage-note" scripts/bench/core/common.sh 'if [ "$BENCH_ID_GROUP" = "$_bw_p" ]; then' 'if false; then'; then
	sv_leadnote "$W/stage-note"
	case $ln_why in
	'the watch did not say'*) echo "$me: mutant watch-leader-note: refused ($ln_why)" ;;
	*)
		echo "$me: FAIL: mutant watch-leader-note passed the lock note case: ${ln_why:-held}"
		fails="$fails watch-leader-note"
		;;
	esac
else
	echo "$me: FAIL: mutant watch-leader-note does not apply"
	fails="$fails watch-leader-note"
fi

# A command's own 124, 125 and 126 are its own (correctness B1, round 1):
# the supervisor passes the command's status through, so bench_bounded
# tells its own 124 (the wall time), 125 (cannot run) and 126 (cannot empty
# the group) from the command's by its events, not by the status.
# sv_own <root> <status>: "status <s> how <how>|<what it said>"
sv_own() {
	_so_d=$W/own-$2-$(basename "$1")
	rm -rf "$_so_d"
	mkdir -p "$_so_d" || return 1
	env TMPDIR="$_so_d" sh -c '. "$1/scripts/bench/core/common.sh"; st=0; bench_bounded 20 sh -c "exit $2" 2> "$3/err" || st=$?; echo "status $st how $BENCH_BOUNDED_HOW"' sh "$1" "$2" "$_so_d" 2>&1
	tr '\n' '|' < "$_so_d/err"
}
for _on in 124 125 126; do
	_oo=$(sv_own "$here" "$_on")
	case $_oo in
	"status $_on how exit"*)
		if printf '%s\n' "$_oo" | grep -F -e 'outlived' -e 'could not be stopped' -e 'spawned pid=' > /dev/null; then
			echo "$me: FAIL: own $_on: the command's own $_on was said to be the supervisor's: $_oo"
			fails="$fails own-$_on"
		else
			echo "$me: ok   own $_on: a command's own $_on is its status, HOW=exit"
		fi
		;;
	*)
		echo "$me: FAIL: own $_on: wanted \"status $_on how exit\"; got $(printf '%s\n' "$_oo" | tr '\n' ' ')"
		fails="$fails own-$_on"
		;;
	esac
done
# the mutant: the status alone decides again (every 124, 125 and 126 the
# supervisor's), so each of the three is taken for the supervisor's own
if sv_stage "$W/stage-own" scripts/bench/core/common.sh '	124) bench_bounded_said deadline ;;
	125) ! bench_bounded_said spawned ;;
	126) ! bench_bounded_said reaped ;;' '	124 | 125 | 126) return 0 ;;'; then
	_omiss=
	for _on in 124 125 126; do
		case $(sv_own "$W/stage-own" "$_on") in "status $_on how exit"*) _omiss="$_omiss $_on" ;; esac
	done
	if [ -z "$_omiss" ]; then
		echo "$me: mutant status-only: refused (124, 125 and 126 each taken for the supervisor's)"
	else
		echo "$me: FAIL: mutant status-only (the status alone decides) passed the own case for:$_omiss"
		fails="$fails status-only"
	fi
else
	echo "$me: FAIL: mutant status-only does not apply"
	fails="$fails status-only"
fi
# With BENCH_BOUNDED_TAP the events reach the events file too (round 1,
# N2), so a wall time is still told by its "deadline" event, and the tap
# has them as well; no scratch file is left in TMPDIR
_td=$W/tap
mkdir -p "$_td/tmp" || exit 1
_to=$(env TMPDIR="$_td/tmp" BENCH_BOUNDED_TAP="$_td/tap" BENCH_STOP_GRACE=1 sh -c '. "$1/scripts/bench/core/common.sh"; st=0; bench_bounded 1 sleep 30 2> /dev/null || st=$?; echo "status $st how $BENCH_BOUNDED_HOW"' sh "$here" 2>&1)
if [ "$_to" = "status 124 how wall" ] && grep -qx deadline "$_td/tap" && [ -z "$(ls -A "$_td/tmp")" ]; then
	echo "$me: ok   tap: the events reach the file and the tap"
else
	echo "$me: FAIL: tap: wanted \"status 124 how wall\", a deadline line in the tap and an empty TMPDIR; got \"$_to\", tap [$(tr '\n' '|' < "$_td/tap" 2> /dev/null)], TMPDIR [$(ls -A "$_td/tmp" | tr '\n' ' ')]"
	fails="$fails tap"
fi
# bench_poll_end removes its own bounded run's events file only (round 1,
# N3): a sibling subshell's has the same $$ in its name
mkdir -p "$_td/sib" || exit 1
_tp=$(env TMPDIR="$_td/sib" sh -c '. "$1/scripts/bench/core/common.sh"; : > "$TMPDIR/bench-sv.$$.sibling"; bench_poll_end; ls -A "$TMPDIR"' sh "$here" 2>&1)
case $_tp in
*'bench-sv.'*'.sibling') echo "$me: ok   poll-end: a sibling's events file is left" ;;
*)
	echo "$me: FAIL: poll-end: bench_poll_end removed a sibling's events file: [$_tp]"
	fails="$fails poll-end-sibling"
	;;
esac

# a trap that runs in a bare nap prints its lines (FIX192, round 2): with fd
# 8 closed, a TERM in bench_nap runs the caller's trap and its EXIT trap,
# and both lines reach standard error. The mutant puts the nap's /dev/null
# back on the bench_poll call, which then covers the wait the trap runs in
nap_trap() {
	_nt_d=$W/nap-$(basename "$1")
	rm -rf "$_nt_d"
	mkdir -p "$_nt_d" || return 1
	cat > "$_nt_d/nap.sh" << 'EOF'
. "$1/scripts/bench/core/common.sh"
exec 8>&-
on_signal() { bench_deferred 143 || :; echo "on_signal: stopping" >&2; exit 143; }
trap on_signal TERM
trap 'echo "cleanup ran" >&2' EXIT
: > "$2/started"
bench_nap 5
echo "nap returned" >&2
EOF
	/bin/bash "$_nt_d/nap.sh" "$1" "$_nt_d" 2> "$_nt_d/err" &
	_nt_p=$!
	_nt_n=0
	while [ ! -e "$_nt_d/started" ] && [ "$_nt_n" -lt 100 ]; do
		sleep 0.1
		_nt_n=$((_nt_n + 1))
	done
	sleep 0.5
	kill -s TERM "$_nt_p"
	_nt_rc=0
	wait "$_nt_p" || _nt_rc=$?
	printf 'nap trap: exit %s; stderr [%s]\n' "$_nt_rc" "$(tr '\n' '|' < "$_nt_d/err")"
}
nout=$(nap_trap "$here")
echo "$me: $nout"
[ "$nout" = "nap trap: exit 143; stderr [on_signal: stopping|cleanup ran|]" ] ||
	{ echo "$me: FAIL: nap trap: a trap in a bare nap must print its lines and its EXIT trap's to standard error"; fails="$fails nap-trap"; }
if stage_root "$W/stage-nap" '	bench_poll bench_sleep_quiet "${1:-1}" || _bn_st=$?' '	bench_poll sleep "${1:-1}" 2> /dev/null || _bn_st=$?'; then
	mout=$(nap_trap "$W/stage-nap")
	case $mout in
	"nap trap: exit 143; stderr []") echo "$me: mutant nap-quiet-wait: refused ($mout)" ;;
	*)
		echo "$me: FAIL: mutant nap-quiet-wait (the nap's /dev/null on the bench_poll call) passed the nap trap case: $mout"
		fails="$fails nap-quiet-wait"
		;;
	esac
else
	echo "$me: FAIL: mutant nap-quiet-wait does not apply"
	fails="$fails nap-quiet-wait"
fi

# 4. the Python helper's probe (scripts/lib/bounded.py): TERM first to the
# group on a timeout and on the caller's own TERM or INT, KILL after the
# grace to what ignores TERM, nothing to a pid that is not the child's
# (unreaped with another start time, or reaped and held by another process),
# a signal in the spawn window held, a timeout's output kept and a ps that
# hangs bounded; then its mutants, each failing its own case (the three
# marked only fail no other; the last three run their own case alone, each
# hanging up to the probe's 60 s bound without the fix)
yout=$(pyprobe "$here")
printf '%s\n' "$yout" | sed "s/^/$me: pyprobe: /"
yj=$(pyjudge "$yout")
if [ -n "$yj" ]; then
	printf '%s\n' "$yj" | sed "s/^/$me: FAIL: pyprobe: /"
	fails="$fails pyprobe"
fi
pymutant group-dropped py-timeout '        os.killpg(c.pid, sig)' '        os.kill(c.pid, sig)'
pymutant never-kills py-ignore '            signal_group(c, signal.SIGKILL)  # bounded-run' '            pass  # bounded-run'
pymutant identity-unchecked py-recycled '    return c.start is None or s == c.start' '    return True'
pymutant caller-term-unhandled py-caller-TERM '                old[s] = signal.signal(s, _raise_exit)' '                pass'
pymutant reaped-ps-trusted py-reaped '        return s == ""  # reaped' '        return s in ("", None)  # reaped' only
pymutant spawn-unheld py-spawnwin '    held = signal.pthread_sigmask(signal.SIG_BLOCK, STOP_SIGNALS)
    c = None' '    held = signal.pthread_sigmask(signal.SIG_BLOCK, ())
    c = None' only
pymutant nostart-refused py-nostart '    return c.start is None or s == c.start' '    return s == c.start' only
pymutant partial-unread py-partial '                    c.output = c.popen.communicate(timeout=AFTER_KILL)' '                    pass' partial
pymutant ps-unbounded py-pshang 'out = p.communicate(timeout=PS_WAIT)[0]' 'out = p.communicate()[0]' pshang
pymutant run-unbounded py-timeout '            out = c.popen.communicate(timeout=timeout)' '            out = c.popen.communicate()' timeout

if [ -n "$fails" ]; then
	echo "$me: failed:$fails"
	exit 1
fi
echo "$me: ok (guard, $nmut mutants refused, probe, the supervisor cases and their 7 mutants, pyprobe and its 10 mutants)"
