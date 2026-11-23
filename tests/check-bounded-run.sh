#!/bin/sh
# tests/check-bounded-run.sh -- one bounded-run primitive in the harness
# (FIX122, finding AR-14 of docs/reviews/2026-11-17-fork-window.md).
#
# bench_bounded and bench_stop_group (scripts/bench/core/common.sh) are the
# only code under scripts/ that runs a command with a wall time and stops
# it with TERM, a grace period and KILL. This check holds that in three
# parts:
#
#   guard   every line of every file under scripts/ is read, here-doc
#           bodies, strings and comments included (a file it cannot read
#           is a failure). No line may send a KILL, and no shell line may
#           test a pid with kill -0, unless it carries the marker comment
#           "# bounded-run: allowed: <reason>" at its end; each file may
#           hold exactly the number of marked lines MARKED pins below, so a
#           new one needs a visible edit to this check. Each caller
#           (pins.sh, ios-sim.sh, ios-device.sh, the quake2 adapter) must
#           run its commands through the primitive: the three scripts
#           source it, none of the four defines a bench_* function of its
#           own (bench_on_signal, the primitive's hook, aside), and none
#           sends a signal itself (any kill, pkill or killall, unmarked).
#   mutants each one literal change to a copy of scripts/ must be refused,
#           in its own file and for its own rule.
#   probe   the disposition probe: each caller runs a stand-in (a child
#           that starts a grandchild; both live while a flag file exists, so
#           no verdict depends on the host's speed), and the caller gets
#           TERM, INT or KILL, or its wall time runs out. What is left
#           running is read once the caller has exited, then the flag is
#           removed and the stand-ins end by themselves.
#
# What the guard matches. Each line is lowered and its " and ' deleted, so
# kill -s "KILL" is kill -s kill. A KILL is kill, pkill or killall with
# -9, -KILL, -SIGKILL, -s KILL, -s 9, -n 9 or --signal KILL (or 9), and
# timeout with -s KILL (or 9), --signal KILL, -k or --kill-after. In a .py
# file a KILL is also the word SIGKILL (signal.SIGKILL,
# signal.Signals.SIGKILL, from signal import SIGKILL), .kill() (Popen),
# Signals(9), send_signal(9), and kill or killpg (os.kill, os.killpg) with
# 9 as the last argument. A liveness test is kill -0, kill -s 0 or kill
# -n 0 on a shell line that is not a whole-line comment.
#
# What it does not match, and so does not claim: a signal named through a
# variable (kill -s "$sig", the shape of gate_end in scripts/gate-checks.sh,
# card FIX186), a liveness test through a function (bench_pid_alive), and
# subprocess.run(..., timeout=), whose KILL is inside Python (rtvar.py,
# upscale-report/report.py and mfx.py use it). The probe is the
# behavioural proof for the callers; the guard only lints their shape.
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
# POSIX sh (dash, bash 3.2). Needs python3 (SIGINT reset), perl-free.
set -u
LC_ALL=C
export LC_ALL
me=check-bounded-run
here=$(cd "$(dirname "$0")/.." && pwd -P) || exit 1

# The marked lines each file may hold ("# bounded-run: allowed: <reason>"
# at the end of the line), file and count; a file not listed holds none.
# A file with more or fewer fails, and so does a marker on a line no rule
# refuses. There is no whole-file entry.
MARKED='
scripts/bench/core/common.sh 3
scripts/bench/core/lock.sh 1
scripts/gate-checks.sh 2
scripts/test.sh 1
scripts/ios-sim.sh 15
scripts/ios-device.sh 6
scripts/metalfx-report/mfx.py 3
'

W=
cleanup() { if [ -n "$W" ]; then chmod -R u+rwx "$W" 2> /dev/null; rm -rf "$W"; fi; }
trap cleanup EXIT
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM
trap 'cleanup; exit 129' HUP
W=$(mktemp -d "${TMPDIR:-/tmp}/check-bounded-run.XXXXXX") || exit 1

# pinned <rel>: the marked lines MARKED allows <rel>
pinned() {
	printf '%s\n' "$MARKED" | awk -v f="$1" '$1 == f { n = $2 } END { print n + 0 }'
}

# scan_file <root> <rel>: one line per finding, "<rel>:<line>: <rule>"
scan_file() {
	awk -v rel="$2" -v want="$(pinned "$2")" '
	BEGIN {
		py = (rel ~ /\.py$/)
		caller = (rel ~ /^scripts\/((pins|ios-sim|ios-device)\.sh|bench\/adapters\/quake2\.sh)$/)
		src = (rel ~ /^scripts\/(pins|ios-sim|ios-device)\.sh$/)
		S = "[ \t,]"
		K = "(sig)?kill"
		KILLRE = "(^|[^a-z0-9_.-])(kill|pkill|killall)" S "+[^;&|]*(-9|-" K "|-[sn]" S "*(" K "|9)|--signal" S "*=?" S "*(" K "|9))([^a-z0-9_]|$)"
		TOSRE = "(^|[^a-z0-9_.-])timeout" S "+[^;&|]*(-s" S "*(" K "|9)|--signal" S "*=?" S "*(" K "|9))([^a-z0-9_]|$)"
		TOKRE = "(^|[^a-z0-9_.-])timeout" S "+[^;&|]*(-k|--kill-after)([^a-z_-]|$)"
		ZERORE = "(^|[^a-z0-9_.-])kill[ \t]+(-0|-[sn][ \t]*0|--signal[ \t=]*0)([^0-9]|$)"
		ANYRE = "(^|[^a-z0-9_.-])(kill|pkill|killall)([^a-z0-9_-]|$)"
		LIT = "\"\\$[A-Za-z_][A-Za-z0-9_]*/scripts/bench/core/common\\.sh\""
		hn = 0; hi = 0; lost = 0; nmk = 0
	}
	function say(what) { hit = 1; if (!mk) printf "%s:%d: %s\n", rel, NR, what }
	{
		line = $0
		L[NR] = line
		mk = (line ~ /# bounded-run: allowed: [^ \t#][^#]*$/)
		nmk += mk
		hit = 0
		lc = tolower(line)
		gsub(/["\047]/, "", lc)
		if (lc ~ KILLRE || lc ~ TOSRE || lc ~ TOKRE) say("a KILL outside the bounded-run primitive")
		else if (py && (lc ~ /(^|[^a-z0-9_])sigkill([^a-z0-9_]|$)/ || lc ~ /\.kill[ \t]*\([ \t]*\)/ || lc ~ /signals[ \t]*\([ \t]*9[ \t]*\)/ ||
			lc ~ /send_signal[ \t]*\([^)0-9]*9[ \t]*\)/ || lc ~ /(^|[^a-z0-9_])kill(pg)?[ \t]*\([^)]*,[ \t]*9[ \t]*\)/))
			say("a KILL outside the bounded-run primitive")
		if (!py && line !~ /^[ \t]*#/ && lc ~ ZERORE) say("a kill -0 liveness test outside the bounded-run primitive (the shape of a polling loop)")
		if (caller && lc ~ ANYRE) say("a caller that sends a signal itself, not through the primitive")
		if (mk && !hit) printf "%s:%d: a bounded-run marker on a line no rule refuses\n", rel, NR
		if (caller && line ~ /^[ \t]*bench_[A-Za-z0-9_]*[ \t]*\(\)/ && line !~ /^[ \t]*bench_on_signal[ \t]*\(\)/)
			printf "%s:%d: a caller that defines a bench_* function of its own\n", rel, NR
		if (caller && line !~ /^[ \t]*#/ && line ~ /(^|[^A-Za-z0-9_])bench_bounded([ \t]|$)/) uses = 1
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
		if (nmk != want) printf "%s:0: %d marked lines (# bounded-run: allowed:), and the check pins %d\n", rel, nmk, want
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
# that records its own, and both live while <dir>/flag exists
mk_standin() {
	cat > "$1" << 'EOF'
#!/bin/sh
d=${PROBE_DIR:?}
echo $$ > "$d/child.pid"
sh -c 'echo $$ > "$1/gc.pid"; while [ -e "$1/flag" ]; do sleep 0.1; done' gc "$d" &
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
			/bin/bash "$1/scripts/ios-device.sh" install --device 00000000-PROBE
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
	case $_p_case in
	term) kill -s TERM "$_p_c" ;;
	int) kill -s INT "$_p_c" ;;
	kill) kill -s KILL "$_p_c" ;;
	esac
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
	printf '%s %s: caller exit %s%s; child %s; grandchild %s\n' "$_p_who" "$_p_case" "$_p_rc" "$_p_h" \
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

probe() {
	for _pc in pins ios-sim ios-device; do
		for _pk in term int kill wall; do
			probe_one "$1" "$_pc" "$_pk"
		done
	done
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
'') ;;
*)
	echo "usage: $0 [guard <root> | probe <root>]" >&2
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
' 'def stop_group(p, wait):
' 'def stop_group(p, wait):
    os.killpg(p.pid, SIGKILL)
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
# the pinned table: a marker is a line, and each file holds its pinned count
mutant marker-in-zero-file scripts/pins.sh 'and the check pins 0' '' '
kill -9 "$$" # bounded-run: allowed: borrowed
'
mutant marker-one-more scripts/gate-checks.sh 'and the check pins 2' '' '
kill -0 "$$" # bounded-run: allowed: one more
'
mutant marker-one-less scripts/gate-checks.sh 'and the check pins 2' ' # bounded-run: allowed: gate_alive: the group test of gate_end, card FIX186' ''
mutant marker-moved scripts/gate-checks.sh 'no rule refuses' ' # bounded-run: allowed: gate_alive: the group test of gate_end, card FIX186' '' 'gate_alive() {' 'gate_alive() { # bounded-run: allowed: moved'
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

if [ -n "$fails" ]; then
	echo "$me: failed:$fails"
	exit 1
fi
echo "$me: ok (guard, $nmut mutants refused, probe)"
