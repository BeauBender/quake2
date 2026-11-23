#!/bin/sh
# tests/check-bounded-run.sh -- one bounded-run primitive in the harness
# (FIX122, finding AR-14 of docs/reviews/2026-11-17-fork-window.md).
#
# bench_bounded and bench_stop_group (scripts/bench/core/common.sh) are the
# only code under scripts/ that runs a command with a wall time and stops
# it with TERM, a grace period and KILL. This check holds that in three
# parts:
#
#   guard   every file under scripts/ is read (one it cannot read is a
#           failure). Outside the allow-list below it refuses a KILL
#           (kill -9, kill -KILL, kill -s KILL, SIGKILL) and a loop that
#           polls with kill -0 and sleeps. Each caller (pins.sh, ios-sim.sh,
#           ios-device.sh, the quake2 adapter) must run its commands through
#           the primitive: the three scripts source it, none of the four
#           defines a bench_* function of its own (bench_on_signal, the
#           primitive's hook, aside), and none sends a signal itself.
#   mutants each one literal change to a copy of scripts/ must be refused.
#   probe   the disposition probe: each caller runs a stand-in (a child
#           that starts a grandchild; both live while a flag file exists, so
#           no verdict depends on the host's speed), and the caller gets
#           TERM, INT or KILL, or its wall time runs out. What is left
#           running is read once the caller has exited, then the flag is
#           removed and the stand-ins end by themselves.
#
# The allow-list: the primitive's file, and the selftests that test the
# callers from outside (their fakes and harnesses), each by its function;
# plus the gate's own process handling, which is not a harness entry point
# (scripts/gate-checks.sh's reap
# loop, carded as a follow-up), named so that nothing else there is let through.
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

ALLOW='scripts/bench/core/common.sh:* scripts/ios-sim.sh:selftest scripts/ios-device.sh:selftest scripts/gate-checks.sh:gate_reap'

W=
cleanup() { if [ -n "$W" ]; then chmod -R u+rwx "$W" 2> /dev/null; rm -rf "$W"; fi; }
trap cleanup EXIT
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM
trap 'cleanup; exit 129' HUP
W=$(mktemp -d "${TMPDIR:-/tmp}/check-bounded-run.XXXXXX") || exit 1

# scan_file <root> <rel>: one line per finding, "<rel>:<line>: <rule>"
scan_file() {
	awk -v rel="$2" -v allow="$ALLOW" '
	BEGIN {
		n = split(allow, A, " ")
		for (i = 1; i <= n; i++) ok[A[i]] = 1
		region = "-"; depth = 0; pend = 0; hd = ""
	}
	function allowed() { return ((rel ":*") in ok) || ((rel ":" region) in ok) }
	function say(l, what) { if (!allowed()) printf "%s:%d: %s\n", rel, l, what }
	{
		line = $0
		if (hd != "") { if (line ~ ("^[\t]*" hd "$")) hd = ""; else { body = line } }
		else body = line
		# a function or a multi-line string assignment at column 0 opens a region
		if (hd == "" && match(line, /^[A-Za-z_][A-Za-z0-9_]*[ \t]*\(\)/)) { r = substr(line, 1, RLENGTH); sub(/[ \t]*\(\)$/, "", r); region = r }
		else if (hd == "" && match(line, /^[A-Za-z_][A-Za-z0-9_]*='\''/) && line !~ /^[A-Za-z_][A-Za-z0-9_]*='\''[^'\'']*'\''[ \t]*$/) { region = substr(line, 1, RLENGTH - 2); instr = 1 }
		code = body
		if (code ~ /^[ \t]*#/) code = ""
		k9 = (code ~ /(^|[^A-Za-z0-9_-])kill[ \t]+(-s[ \t]+)?-?(9|KILL|SIGKILL)([^A-Za-z0-9_]|$)/)
		k0 = (code ~ /(^|[^A-Za-z0-9_-])kill[ \t]+(-s[ \t]+)?-0([^0-9]|$)/ || code ~ /kill[ \t]+-s[ \t]+0([^0-9]|$)/)
		sl = (code ~ /(^|[^A-Za-z0-9_-])sleep([ \t]|$)/)
		sig = (code ~ /(^|[^A-Za-z0-9_.-])(kill|pkill|killall)([ \t]|$)/)
		if (k9) say(NR, "a KILL outside the bounded-run primitive")
		if (sig) sigs[NR] = region
		if (code ~ /^[ \t]*\.[ \t]+"?\$[A-Za-z_]+\/scripts\/bench\/core\/common\.sh"?/) sourced = 1
		if (match(code, /^[ \t]*bench_[A-Za-z0-9_]*[ \t]*\(\)/) && code !~ /^[ \t]*bench_on_signal[ \t]*\(\)/) owndef[NR] = region
		if (code ~ /(^|[^A-Za-z0-9_])bench_bounded([ \t]|$)/) uses = 1
		# loops: the outermost one gathers what its lines hold
		c = code; gsub(/"[^"]*"|'\''[^'\'']*'\''/, " ", c); gsub(/[;&|()]/, " ", c)
		m = split(c, T, /[ \t]+/)
		if (depth > 0 || pend) { lk0 = lk0 || k0; lsl = lsl || sl }
		for (i = 1; i <= m; i++) {
			t = T[i]
			if (t == "while" || t == "until" || t == "for") {
				if (depth == 0 && !pend) { pend = 1; lk0 = k0; lsl = sl; lstart = NR }
			} else if (t == "do") { depth++; pend = 0 }
			else if (t == "done" && depth > 0) {
				depth--
				if (depth == 0) { if (lk0 && lsl) say(lstart, "a kill -0 and sleep polling loop outside the bounded-run primitive"); lk0 = 0; lsl = 0 }
			}
		}
		if (hd == "" && match(line, /<<-?[ \t]*['\''"]?[A-Za-z_]+['\''"]?/)) { h = substr(line, RSTART, RLENGTH); sub(/^<<-?[ \t]*/, "", h); gsub(/['\''"]/, "", h); hd = h }
		if (line ~ /^}/ && hd == "") region = "-"
		else if (instr && NR > 0 && line ~ /'\''[ \t]*$/ && line !~ /^[A-Za-z_][A-Za-z0-9_]*='\''/) { region = "-"; instr = 0 }
	}
	END {
		if (rel ~ /^scripts\/(pins|ios-sim|ios-device)\.sh$/ && !sourced) printf "%s:0: a caller that does not source the bounded-run primitive (scripts/bench/core/common.sh)\n", rel
		if (rel == "scripts/bench/adapters/quake2.sh" && !uses) printf "%s:0: the adapter does not run the engine through bench_bounded\n", rel
		if (rel ~ /^scripts\/((pins|ios-sim|ios-device)\.sh|bench\/adapters\/quake2\.sh)$/) {
			for (l in owndef) { region = owndef[l]; say(l, "a caller that defines a bench_* function of its own") }
			for (l in sigs) { region = sigs[l]; say(l, "a caller that sends a signal itself, not through the primitive") }
		}
	}' "$1/$2"
}

# guard <root>: every finding under <root>/scripts; false when there is one
guard() {
	_g_bad=0
	_g_list=$W/files.$$
	(cd "$1" && find scripts -type f) | sort > "$_g_list" || return 1
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

# mutant <name> <file> <python replacement: old> <new>: a copy of scripts/
# with one change; the guard must refuse it
nmut=0
fails=
mutant() {
	_m_d=$W/m-$1
	rm -rf "$_m_d"
	mkdir -p "$_m_d" && cp -R "$here/scripts" "$_m_d/scripts" || { fails="$fails $1"; return; }
	if [ "$2" = - ]; then
		chmod 000 "$_m_d/scripts/pins/scenario-cases.txt"
	else
		python3 -I -c 'import sys
p, old, new = sys.argv[1:4]
s = open(p).read()
if s.count(old) != 1: sys.exit("the mutant does not apply: %d" % s.count(old))
open(p, "w").write(s.replace(old, new))' "$_m_d/$2" "$3" "$4" || { echo "$me: FAIL: mutant $1 does not apply"; fails="$fails $1"; return; }
	fi
	_m_o=$(guard "$_m_d" 2>&1) && { echo "$me: FAIL: mutant $1 was not refused"; fails="$fails $1"; return; }
	nmut=$((nmut + 1))
	echo "$me: mutant $1: refused ($(printf '%s\n' "$_m_o" | head -n 1))"
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

# 2. the mutants
mutant ios-sim-kill9 scripts/ios-sim.sh '	bench_spawn_forget "$1"
}' '	kill -9 "$1" 2>/dev/null
	bench_spawn_forget "$1"
}'
mutant pins-kill9 scripts/pins.sh '    trap '"''"' HUP INT TERM
    if [ -n "${BENCH_BOUNDED_PID:-}" ]; then' '    trap '"''"' HUP INT TERM
    kill -9 "${BENCH_BOUNDED_PID:-0}" 2>/dev/null
    if [ -n "${BENCH_BOUNDED_PID:-}" ]; then'
mutant pins-kill-s-KILL scripts/pins.sh '    pb_how=$BENCH_BOUNDED_HOW' '    pb_how=$BENCH_BOUNDED_HOW; kill -s KILL "$$" 2>/dev/null'
mutant device-poll-loop scripts/ios-device.sh '	[ -n "${BENCH_BOUNDED_PID:-}" ] || return 0' '	[ -n "${BENCH_BOUNDED_PID:-}" ] || return 0
	while kill -0 "$BENCH_BOUNDED_PID" 2>/dev/null; do sleep 0.2; done'
mutant device-own-watchdog scripts/ios-device.sh '. "$repo/scripts/bench/core/common.sh"' 'bench_bounded() { "$@" & p=$!; sleep "$1"; kill -TERM "$p"; sleep 5; kill -KILL "$p"; }'
mutant adapter-term-runner scripts/bench/adapters/quake2.sh '	bench_bounded "$(bench_case_wall)" "$_qr_bin" "$@" > "${_qr_log:?}" 2>&1 || _qr_st=$?' \
	'	{ "$_qr_bin" "$@" & p=$!; sleep "$BENCH_BOUNDED_QUIT"; kill -TERM "$p"; wait "$p"; } > "${_qr_log:?}" 2>&1 || _qr_st=$?'
mutant kill9-elsewhere scripts/gate-checks.sh 'gate_end() {' 'gate_end() {
	kill -9 "$@" 2>/dev/null'
mutant unreadable - '' ''

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
