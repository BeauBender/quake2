#!/bin/sh
# Invariant: the gate's check scheduler (scripts/gate-checks.sh, FIX72)
# keeps its contract. It is run here on fake checks in a scratch directory,
# each of which logs when it starts and ends, prints a few lines spaced in
# time, and exits as its case says. The cases:
#   one_fail  one failing check fails the run and is named; the checks
#             after it still run;
#   two_fail  two failing checks are both named;
#   signal    a check that dies from a signal fails the run;
#   log       a log that cannot be printed fails the run;
#   order     each check's output (standard output and error) is printed
#             whole, in the order given, although the checks end in another
#             order, each followed by its status line; the scratch logs are
#             removed afterwards;
#   serial    a check named serial never overlaps another check and starts
#             after every other check has ended, while the others overlap;
#   jobs1     GATE_JOBS=1 runs every check alone, in the order given, the
#             serial ones in their place;
#   bound     GATE_JOBS checks run at once, never more;
#   default   without GATE_JOBS, half the CPUs, at least 1, and 1 when the
#             CPU count cannot be read;
#   times     each check's wall time and the total are printed;
#   errors    a bad GATE_JOBS, a serial name that is no check, no checks and
#             no scratch directory fail before any check runs;
#   guard     a scheduler that skips a check or leaves one unprinted fails;
#   wiring    scripts/test.sh runs the scheduler on tests/check-*.sh before
#             its builds, and stops when it fails;
#   interrupt SIGINT to the scheduler, two checks running (one of which
#             ignores SIGTERM, the other has a SIGTERM trap of 5 s, which
#             finishes) and two waiting: it ends within 30 s with status
#             130, what the running checks started (a sleep, by its
#             recorded pid) is gone, and no other check starts;
#   interrupt1  SIGTERM, then SIGHUP, at GATE_JOBS=1, one check running and
#             one waiting: the same, with status 143 or 129;
#   testsh    SIGTERM, SIGHUP and SIGINT to scripts/test.sh itself, run on a
#             copy of the gate's layout with fake checks: the same, with
#             test.sh's status 143, 129 or 130;
#   kgrp      SIGKILL to test.sh's process group there: test.sh's status
#             137, and what the running check started is gone within 10 s;
#   sigs      a check run by test.sh there gets SIGINT, SIGQUIT, SIGHUP,
#             SIGTERM and SIGTSTP as one started directly does (as
#             origin/main's test.sh ran each check), SIGINT delivered;
#   leave     what a passing check leaves running is gone after the run.
# Then each case is shown failing against at least one mutant of the
# scheduler (or of scripts/test.sh, for wiring and testsh). Every run of the
# scheduler is bounded in time here; one that outlives its bound fails its
# case. The bounds leave room for a loaded host: they only catch a hang.
#
# Usage: tests/check-gate-parallel.sh
set -eu
cd "$(dirname "$0")/.."
me=check-gate-parallel
top=$(pwd -P)
real=$top/scripts/gate-checks.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/gatepar.XXXXXX")
tmp=$(cd "${tmp:?}" && pwd -P)
# _lp: a run started by launch and not yet reaped; on the way out it gets
# SIGTERM, which ends it and what it started (that is what the cases test)
_lp=
gp_exit() {
	if [ -n "$_lp" ]; then
		kill -s TERM "$_lp" 2> /dev/null || :
		wait "$_lp" 2> /dev/null || :
	fi
	rm -rf "${tmp:?}"
}
trap gp_exit EXIT
trap 'exit 1' HUP INT TERM
unset GATE_JOBS GATE_PARENT GP_PATH GP_TD GP_NCPU GP_BOUND GP_FIRST WIRING
export LC_ALL=C

fail=0
quiet=0
SCHED=$real
bad() { echo "$me: $*"; fail=1; }
say() { [ "$quiet" = 1 ] || echo "$me: $*"; }

# The fake check: its name comes from its file name, its behaviour from
# $GP_SPEC/<name>: "<lines> <delay> <status> <mode> <hold>"; mode term or
# kill makes it die from that signal after its lines; mode hold runs, after
# its lines, "sleep <hold>" in the foreground, holdterm the same ignoring
# SIGTERM (and so does that sleep), holdtrap the same with a SIGTERM trap
# that takes 5 s and then logs "cleaned <name>"; mode leave starts that
# sleep in the background and exits. The sleep logs "sleep <name> <its pid>
# <the check's pid> <the scheduler's pid>". Mode probe logs how SIGINT,
# SIGQUIT, SIGHUP, SIGTERM and SIGTSTP reach the check.
mkdir "${tmp:?}/bin" "${tmp:?}/shim" "${tmp:?}/make"
cat > "${tmp:?}/fake" << 'EOF'
#!/bin/sh
n=${0##*/}
n=${n#check-}
n=${n%.sh}
read -r lines delay status mode hold < "$GP_SPEC/$n"
printf 'start %s\n' "$n" >> "$GP_EVENTS"
i=1
while [ "$i" -le "$lines" ]; do
	echo "$n line $i"
	[ "$i" -eq "$lines" ] || sleep "$delay"
	i=$((i + 1))
done
echo "$n to stderr" >&2
# probe <signal>: logs "sig <name> <signal> delivered" when a shell this
# check starts dies from <signal> sent to itself, "ignored" when it goes on
probe() {
	if sh -c 'ulimit -c 0; kill -s "$1" $$; exit 0' sh "$1" 2> /dev/null; then _pr=ignored; else _pr=delivered; fi
	printf 'sig %s %s %s\n' "$n" "$1" "$_pr" >> "$GP_EVENTS"
}
case $mode in
hold | holdterm | holdtrap)
	[ "$mode" != holdterm ] || trap '' TERM
	[ "$mode" != holdtrap ] || trap 'sleep 5; printf "cleaned %s\n" "$n" >> "$GP_EVENTS"; exit 1' TERM
	sh -c 'printf "sleep %s %s %s %s\n" "$1" "$$" "$2" "$3" >> "$GP_EVENTS"; exec sleep "$4"' sh "$n" "$$" "$PPID" "$hold"
	;;
leave)
	sleep "$hold" > /dev/null 2>&1 &
	printf 'sleep %s %s %s %s\n' "$n" "$!" "$$" "$PPID" >> "$GP_EVENTS"
	;;
probe)
	# no loop around these: a shell in a loop may take a child's death from
	# SIGINT as its own
	probe INT
	probe QUIT
	probe HUP
	probe TERM
	# SIGTSTP to this check itself (a shell resets it in what it starts): a
	# helper sees it stopped (delivered; it then sends SIGCONT) or sees the
	# mark this check leaves once the kill has returned (ignored)
	_pm=$GP_EVENTS.cont.$$
	sh -c 'i=0
		while [ "$i" -lt 50 ]; do
			case $(ps -o stat= -p "$1") in *T*) r=delivered; break ;; esac
			[ ! -e "$3" ] || { r=ignored; break; }
			sleep 0.2
			i=$((i + 1))
		done
		printf "sig %s TSTP %s\n" "$2" "${r:-unknown}" >> "$GP_EVENTS"
		kill -s CONT "$1"' sh "$$" "$n" "$_pm" &
	kill -s TSTP $$
	: > "$_pm"
	wait
	;;
esac
printf 'end %s\n' "$n" >> "$GP_EVENTS"
case $mode in
term) kill -TERM $$ ;;
kill) kill -KILL $$ ;;
esac
exit "$status"
EOF
# shims: cat that cannot read a log; sysctl that prints $GP_NCPU (or fails
# when it is "fail"); nproc that fails
printf '%s\n' '#!/bin/sh' 'case $* in *.log) echo "cat: cannot read" >&2; exit 1 ;; esac' 'exec /bin/cat "$@"' > "${tmp:?}/shim/cat"
mkdir "${tmp:?}/cpu"
printf '%s\n' '#!/bin/sh' '[ "$GP_NCPU" != fail ] || exit 1' 'printf "%s\n" "$GP_NCPU"' > "${tmp:?}/cpu/sysctl"
printf '%s\n' '#!/bin/sh' 'exit 1' > "${tmp:?}/cpu/nproc"
# make that only logs it ran, for test.sh on a copy of the gate's layout
printf '%s\n' '#!/bin/sh' 'printf "make %s\n" "$*" >> "$GP_EVENTS"' > "${tmp:?}/make/make"
chmod +x "${tmp:?}/fake" "${tmp:?}/shim/cat" "${tmp:?}/cpu/sysctl" "${tmp:?}/cpu/nproc" "${tmp:?}/make/make"

# newcase: a fresh case directory (called as d=$(newcase), so it keeps no count)
newcase() {
	_nd=$(mktemp -d "${tmp:?}/run.XXXXXX")
	mkdir "${_nd:?}/spec" "${_nd:?}/td"
	: > "${_nd:?}/events"
	printf '%s\n' "$_nd"
}
# spec <dir> <name> <lines> <delay> <status> [<mode> [<hold>]]: a fake check
spec() {
	printf '%s %s %s %s %s\n' "$3" "$4" "$5" "${6:-none}" "${7:-0}" > "${1:?}/spec/$2"
	cp "${tmp:?}/fake" "${1:?}/check-$2.sh"
}
# bounded <seconds> <out> <command>...: its status, or 124 once it outlives <seconds>
bounded() {
	_bl=$(($1 * 5)) _bo=$2
	shift 2
	"$@" > "$_bo" 2>&1 &
	_bp=$!
	_bw=0
	while kill -0 "$_bp" 2> /dev/null; do
		if [ "$_bw" -ge "$_bl" ]; then
			kill -TERM "$_bp" 2> /dev/null || :
			wait "$_bp" 2> /dev/null || :
			echo "(stopped: it outlived its bound)" >> "$_bo"
			return 124
		fi
		sleep 0.2
		_bw=$((_bw + 1))
	done
	_bs=0
	wait "$_bp" || _bs=$?
	return "$_bs"
}
# run <dir> <GATE_JOBS, - for none> <serial> <name>...: the scheduler under
# test ($SCHED) on those fakes; its output in <dir>/out, normalised in
# <dir>/norm (wall times as N). One-shot options, set just before and
# cleared by the run (an assignment in front of a function call would
# persist in a POSIX shell): GP_PATH goes in front of PATH, GP_TD replaces
# TMPDIR, GP_NCPU is what the sysctl shim prints, GP_BOUND the bound in s
# (90 by default: a run takes a few seconds, more on a loaded host).
run() {
	_rd=$1 _rj=$2 _rs=$3
	shift 3
	for _rn; do
		set -- "$@" "$_rd/check-$_rn.sh"
		shift
	done
	_rb=${GP_BOUND:-90} _rt=${GP_TD:-$_rd/td} _rp=${GP_PATH:+$GP_PATH:}$PATH _rc=${GP_NCPU:-} _rf=${GP_FIRST:-}
	GP_BOUND= GP_TD= GP_PATH= GP_NCPU= GP_FIRST=
	_rr=0
	if [ "$_rj" = - ]; then
		bounded "$_rb" "$_rd/out" env GP_EVENTS="$_rd/events" GP_SPEC="$_rd/spec" GP_NCPU="$_rc" \
			TMPDIR="$_rt" PATH="$_rp" sh "$SCHED" "$_rs" "$_rf" "$@" || _rr=$?
	else
		bounded "$_rb" "$_rd/out" env GATE_JOBS="$_rj" GP_EVENTS="$_rd/events" GP_SPEC="$_rd/spec" GP_NCPU="$_rc" \
			TMPDIR="$_rt" PATH="$_rp" sh "$SCHED" "$_rs" "$_rf" "$@" || _rr=$?
	fi
	sed -E -e 's/, [0-9]+ s$/, N s/' -e 's/ in [0-9]+ s, / in N s, /' "$_rd/out" > "$_rd/norm"
	return "$_rr"
}
# has <dir> <line>: the normalised output holds that whole line
has() { grep -qxF -- "$2" "$1/norm"; }
# evstat <dir> <serial names>: "max <most open at once> overlap <starts that
# overlapped a serial check> late <other checks started after a serial one>"
evstat() {
	awk -v ser=" $2 " '
		function isser(x) { return index(ser, " " x " ") > 0 }
		$1 == "start" {
			for (k in o) if (o[k] && (isser(k) || isser($2))) ov++
			if (isser($2)) seen = 1; else if (seen) late++
			o[$2] = 1; open++; if (open > max) max = open
		}
		$1 == "end" { o[$2] = 0; open-- }
		END { printf "max %d overlap %d late %d\n", max, ov, late }' "$1/events"
}
# status <dir> <name>: the status part of that check's line ("ok" or "FAILED (...)")
status() { sed -n "s/^gate: check-$2\\.sh: \\(.*\\), N s\$/\\1/p" "$1/norm"; }

c_one_fail() {
	d=$(newcase)
	spec "$d" a 2 0.1 0
	spec "$d" b 2 0.1 3
	spec "$d" c 2 0.1 0
	rc=0
	run "$d" 1 "" a b c || rc=$?
	[ "$rc" -eq 1 ] || { say "one_fail: status $rc, want 1"; return 1; }
	[ "$(status "$d" b)" = "FAILED (status 3)" ] || { say "one_fail: check-b.sh is not shown as failed"; return 1; }
	[ "$(status "$d" a)" = ok ] && [ "$(status "$d" c)" = ok ] || { say "one_fail: the checks around the failing one did not all run and pass"; return 1; }
	has "$d" "gate: checks: 1 of 3 FAILED in N s, 1 at a time: check-b.sh" || { say "one_fail: the last line does not name check-b.sh alone"; return 1; }
}

c_two_fail() {
	d=$(newcase)
	spec "$d" a 2 0.1 1
	spec "$d" b 2 0.1 0
	spec "$d" c 2 0.1 2
	spec "$d" d 2 0.1 0
	rc=0
	run "$d" 2 "" a b c d || rc=$?
	[ "$rc" -eq 1 ] || { say "two_fail: status $rc, want 1"; return 1; }
	[ "$(status "$d" a)" = "FAILED (status 1)" ] && [ "$(status "$d" c)" = "FAILED (status 2)" ] || { say "two_fail: a failing check is not shown as failed"; return 1; }
	has "$d" "gate: checks: 2 of 4 FAILED in N s, 2 at a time: check-a.sh check-c.sh" || { say "two_fail: the last line does not name both failing checks"; return 1; }
}

c_signal() {
	d=$(newcase)
	spec "$d" a 2 0.1 0
	spec "$d" b 2 0.1 0 term
	spec "$d" c 2 0.1 0 kill
	rc=0
	run "$d" 3 "" a b c || rc=$?
	[ "$rc" -eq 1 ] || { say "signal: status $rc, want 1"; return 1; }
	[ "$(status "$d" b)" = "FAILED (status 143)" ] && [ "$(status "$d" c)" = "FAILED (status 137)" ] || { say "signal: a check that died from a signal is not shown as failed"; return 1; }
	has "$d" "gate: checks: 2 of 3 FAILED in N s, 3 at a time: check-b.sh check-c.sh" || { say "signal: the last line does not name both"; return 1; }
}

c_log() {
	d=$(newcase)
	spec "$d" a 1 0 0
	spec "$d" b 1 0 0
	rc=0
	GP_PATH=$tmp/shim ; run "$d" 2 "" a b || rc=$?
	[ "$rc" -eq 1 ] || { say "log: status $rc with unreadable logs, want 1"; return 1; }
	[ "$(status "$d" a)" = "FAILED (its log cannot be read)" ] && [ "$(status "$d" b)" = "FAILED (its log cannot be read)" ] || { say "log: an unreadable log is not a failure"; return 1; }
}

c_order() {
	d=$(newcase)
	spec "$d" a 6 0.15 0
	spec "$d" b 3 0.05 0
	spec "$d" c 5 0.1 0
	spec "$d" d 2 0.05 0
	rc=0
	run "$d" 4 "" a b c d || rc=$?
	[ "$rc" -eq 0 ] || { say "order: status $rc, want 0"; return 1; }
	{
		echo "gate: running 4 checks, 4 at a time; first: none; alone, after the others: none"
		for n in a:6 b:3 c:5 d:2; do
			i=1
			while [ "$i" -le "${n#*:}" ]; do echo "${n%:*} line $i"; i=$((i + 1)); done
			echo "${n%:*} to stderr"
			echo "gate: check-${n%:*}.sh: ok, N s"
		done
		echo "gate: checks: all 4 passed in N s, 4 at a time"
	} > "$d/want"
	cmp -s "$d/want" "$d/norm" || { say "order: the output is not each check's log whole and in order:"; [ "$quiet" = 1 ] || diff "$d/want" "$d/norm" || :; return 1; }
	# the case means something only when the checks ended in another order
	[ "$(grep '^end ' "$d/events" | head -n 1)" != "end a" ] || { say "order: the fake checks ended in the given order; the case shows nothing"; return 1; }
	[ -z "$(ls -A "$d/td")" ] || { say "order: the scheduler left its logs behind"; return 1; }
}

c_serial() {
	d=$(newcase)
	spec "$d" p1 3 0.2 0
	spec "$d" s1 2 0.1 0
	spec "$d" p2 3 0.2 0
	spec "$d" p3 3 0.2 0
	spec "$d" s2 2 0.1 0
	rc=0
	run "$d" 3 "check-s1.sh check-s2.sh" p1 s1 p2 p3 s2 || rc=$?
	[ "$rc" -eq 0 ] || { say "serial: status $rc, want 0"; return 1; }
	has "$d" "gate: running 5 checks, 3 at a time; first: none; alone, after the others: check-s1.sh check-s2.sh" || { say "serial: the start line does not name the serial checks"; return 1; }
	set -- $(evstat "$d" "s1 s2")
	[ "$4" -eq 0 ] || { say "serial: a serial check overlapped another check ($4 times)"; return 1; }
	[ "$6" -eq 0 ] || { say "serial: a check started after a serial one ($6 times)"; return 1; }
	[ "$2" -ge 2 ] || { say "serial: the other checks did not run in parallel (at most $2 at once)"; return 1; }
	[ "$(sed -n 's/^gate: \(check-[a-z0-9]*\.sh\): ok, N s$/\1/p' "$d/norm" | tr '\n' ' ')" = "check-p1.sh check-s1.sh check-p2.sh check-p3.sh check-s2.sh " ] ||
		{ say "serial: the logs are not printed in the order given"; return 1; }
}

c_jobs1() {
	d=$(newcase)
	spec "$d" a 2 0.1 0
	spec "$d" b 2 0.1 0
	spec "$d" c 2 0.1 0
	spec "$d" d 2 0.1 0
	rc=0
	GP_FIRST="check-d.sh"
	run "$d" 1 "check-b.sh" a b c d || rc=$?
	[ "$rc" -eq 0 ] || { say "jobs1: status $rc, want 0"; return 1; }
	printf '%s\n' "start a" "end a" "start b" "end b" "start c" "end c" "start d" "end d" > "$d/want"
	cmp -s "$d/want" "$d/events" || { say "jobs1: GATE_JOBS=1 did not run each check alone in the order given"; return 1; }
	has "$d" "gate: running 4 checks, 1 at a time; first: none; alone, after the others: none" || { say "jobs1: the start line is wrong"; return 1; }
}

# first: the checks named first start before the others, in that list's
# order (a serial one stays serial), and the logs keep the order given
c_first() {
	d=$(newcase)
	for n in a b c d e; do spec "$d" "$n" 2 0.3 0; done
	rc=0
	GP_FIRST="check-d.sh check-c.sh check-e.sh check-d.sh"
	run "$d" 2 "check-e.sh" a b c d e || rc=$?
	[ "$rc" -eq 0 ] || { say "first: status $rc, want 0"; return 1; }
	[ "$(grep '^start ' "$d/events" | tr '\n' ' ')" = "start d start c start a start b start e " ] ||
		{ say "first: the checks did not start in the order d c a b, then e alone: $(grep '^start ' "$d/events" | tr '\n' ' ')"; return 1; }
	has "$d" "gate: running 5 checks, 2 at a time; first: check-d.sh check-c.sh; alone, after the others: check-e.sh" ||
		{ say "first: the start line does not name the first and the serial checks"; return 1; }
	[ "$(sed -n 's/^gate: \(check-[a-z0-9]*\.sh\): ok, N s$/\1/p' "$d/norm" | tr '\n' ' ')" = "check-a.sh check-b.sh check-c.sh check-d.sh check-e.sh " ] ||
		{ say "first: the logs are not printed in the order given"; return 1; }
}

c_bound() {
	d=$(newcase)
	for n in a b c d e f; do spec "$d" "$n" 2 0.4 0; done
	rc=0
	GP_PATH=$tmp/cpu GP_NCPU=16 ; run "$d" 2 "" a b c d e f || rc=$?
	[ "$rc" -eq 0 ] || { say "bound: status $rc, want 0"; return 1; }
	set -- $(evstat "$d" "")
	[ "$2" -eq 2 ] || { say "bound: GATE_JOBS=2 ran $2 checks at once"; return 1; }
}

c_default() {
	for t in 6:3 7:3 16:8 2:1 1:1 fail:1 "6 x:1" ":1"; do
		d=$(newcase)
		spec "$d" a 1 0 0
		rc=0
		GP_PATH=$tmp/cpu GP_NCPU=${t%:*} ; run "$d" - "" a || rc=$?
		[ "$rc" -eq 0 ] || { say "default: status $rc with the CPU count '${t%:*}', want 0"; return 1; }
		has "$d" "gate: running 1 checks, ${t##*:} at a time; first: none; alone, after the others: none" ||
			{ say "default: the CPU count '${t%:*}' does not give ${t##*:} at a time"; return 1; }
	done
}

c_times() {
	d=$(newcase)
	spec "$d" a 2 1.2 0
	spec "$d" b 1 0 0
	rc=0
	t0=$(date +%s)
	run "$d" 2 "" a b || rc=$?
	tw=$(($(date +%s) - t0))
	[ "$rc" -eq 0 ] || { say "times: status $rc, want 0"; return 1; }
	ta=$(sed -n 's/^gate: check-a\.sh: ok, \([0-9][0-9]*\) s$/\1/p' "$d/out")
	tb=$(sed -n 's/^gate: check-b\.sh: ok, \([0-9][0-9]*\) s$/\1/p' "$d/out")
	tt=$(sed -n 's/^gate: checks: all 2 passed in \([0-9][0-9]*\) s, 2 at a time$/\1/p' "$d/out")
	[ -n "$ta" ] && [ -n "$tb" ] && [ -n "$tt" ] || { say "times: a wall time is missing"; return 1; }
	# whole seconds: the check sleeps 1.2 s, so it takes at least 1 s; the
	# total holds it and is held by the run's wall time measured here (no
	# fixed upper bound: a loaded host only makes the run longer)
	[ "$ta" -ge 1 ] || { say "times: a check of about 1.2 s took $ta s"; return 1; }
	[ "$tt" -ge "$ta" ] && [ "$tt" -le "$tw" ] || { say "times: the total ($tt s) is not the wall time of the run ($tw s here, the check $ta s)"; return 1; }
}

# errors: each run stops before any check starts, in well under a second
# even on a loaded host; its 10 s bound is what ends the jobs-zero mutant
# (it waits forever), so a wider one would only make the self-test longer
c_errors() {
	for j in 0 abc -1 " 2"; do
		d=$(newcase)
		spec "$d" a 1 0 0
		rc=0
		GP_BOUND=10 ; run "$d" "$j" "" a || rc=$?
		[ "$rc" -eq 2 ] && has "$d" "gate: GATE_JOBS must be a positive whole number, not '$j'" ||
			{ say "errors: GATE_JOBS='$j' gave status $rc"; return 1; }
		[ ! -s "$d/events" ] || { say "errors: a check ran with GATE_JOBS='$j'"; return 1; }
	done
	d=$(newcase)
	spec "$d" a 1 0 0
	rc=0
	GP_BOUND=10 ; run "$d" 2 "check-a.sh check-zz.sh" a || rc=$?
	[ "$rc" -eq 2 ] && has "$d" "gate: check-zz.sh, named in the serial or the first list, is not one of the checks" && [ ! -s "$d/events" ] ||
		{ say "errors: a serial name that is no check gave status $rc"; return 1; }
	d=$(newcase)
	spec "$d" a 1 0 0
	rc=0
	GP_BOUND=10 GP_FIRST="check-zy.sh"
	run "$d" 2 "" a || rc=$?
	[ "$rc" -eq 2 ] && has "$d" "gate: check-zy.sh, named in the serial or the first list, is not one of the checks" && [ ! -s "$d/events" ] ||
		{ say "errors: a first name that is no check gave status $rc"; return 1; }
	d=$(newcase)
	rc=0
	GP_BOUND=10 ; run "$d" 2 "" || rc=$?
	[ "$rc" -eq 2 ] && has "$d" "gate: no checks to run" || { say "errors: no checks gave status $rc"; return 1; }
	d=$(newcase)
	spec "$d" a 1 0 0
	rc=0
	GP_BOUND=10 GP_TD=$d/no/such/dir ; run "$d" 2 "" a || rc=$?
	[ "$rc" -eq 2 ] && has "$d" "gate: cannot make a scratch directory for the logs" && [ ! -s "$d/events" ] ||
		{ say "errors: no scratch directory gave status $rc"; return 1; }
}

# guard: the scheduler under test, broken to skip the serial phase, or to
# stop printing before the last check, must fail and say so
c_guard() {
	for b in skip flush; do
		case $b in
		skip) old='	gate_phase 1 $_gser' new='	:' ;;
		flush) old='while [ "$_gnext" -le "$_gn" ] && [ -e' new='while [ "$_gnext" -lt "$_gn" ] && [ -e' ;;
		esac
		lit "$SCHED" "$old" "$new" > "$tmp/guard-$b.sh" || { say "guard: the $b breakage does not apply"; return 1; }
		d=$(newcase)
		spec "$d" a 1 0 0
		spec "$d" s 1 0 0
		rc=0
		_gsav=$SCHED
		SCHED=$tmp/guard-$b.sh
		run "$d" 2 "check-s.sh" a s || rc=$?
		SCHED=$_gsav
		[ "$rc" -eq 1 ] && grep -q '^gate: scheduler error: ' "$d/out" ||
			{ say "guard: a scheduler that does not run or print every check passed ($b, status $rc)"; return 1; }
	done
}

# wiring <test.sh>: the scheduler starts on tests/check-*.sh, told test.sh's
# pid, alone on its line, in the background (so that test.sh's traps act at
# once; testsh shows they do), and the next lines record its pid, turn job
# control off again and wait for it, nothing more ("|| ..." would ignore its
# status); all of that before the first make, and the script stops on a
# failure (set -e). Job control on for the start: sigs shows it.
c_wiring() {
	_wf=${WIRING:-$top/scripts/test.sh}
	_wl=$(grep -n '^GATE_PARENT=\$\$ scripts/gate-checks\.sh "\$gate_serial" "\$gate_first" tests/check-\*\.sh &$' "$_wf" | cut -d: -f1)
	_wm=$(grep -n '^make -B' "$_wf" | head -n 1 | cut -d: -f1)
	[ -n "$_wl" ] && [ "$(printf '%s\n' "$_wl" | wc -l)" -eq 1 ] || { say "wiring: test.sh does not start the scheduler on tests/check-*.sh exactly once"; return 1; }
	[ "$(sed -n "$((_wl + 1))p" "$_wf")" = 'gate_sched=$!' ] && [ "$(sed -n "$((_wl + 2))p" "$_wf")" = 'set +m' ] &&
		[ "$(sed -n "$((_wl + 3))p" "$_wf")" = 'wait "$gate_sched"' ] ||
		{ say "wiring: test.sh does not wait for the scheduler, and for its status, right after starting it"; return 1; }
	[ -n "$_wm" ] && [ "$_wl" -lt "$_wm" ] || { say "wiring: the scheduler does not run before the builds"; return 1; }
	grep -qx 'set -eu' "$_wf" || { say "wiring: test.sh does not stop on a failure"; return 1; }
}

# launch <dir> <command>...: starts <command> in the background, its output
# in <dir>/out, its pid in _lp. Job control is on for that one command, so
# its SIGINT is not ignored (a command started in the background without it
# ignores SIGINT); it also leads a process group of its own.
launch() {
	_ld=$1
	shift
	set -m
	"$@" > "$_ld/out" 2>&1 &
	_lp=$!
	set +m
}
# dead <pid>: no such process, or a zombie (it has ended, not yet reaped)
dead() {
	kill -0 "$1" 2> /dev/null || return 0
	case $(ps -o stat= -p "$1" 2> /dev/null) in *Z*) return 0 ;; esac
	return 1
}
# lwait <seconds>: waits for the run _lp (from launch), at most <seconds>;
# its status, or 124 once it outlives them (it is then killed)
lwait() {
	_wn=0
	while kill -0 "$_lp" 2> /dev/null; do
		if [ "$_wn" -ge $(($1 * 5)) ]; then
			kill -s KILL "$_lp" 2> /dev/null || :
			wait "$_lp" 2> /dev/null || :
			_lp=
			return 124
		fi
		sleep 0.2
		_wn=$((_wn + 1))
	done
	_ws=0
	wait "$_lp" || _ws=$?
	_lp=
	return "$_ws"
}
# signalled <dir> <case> <signal> <status> <starts> <held> [group [<gone>]]:
# once <held> held checks of the run _lp (from launch) have logged their
# sleep (60 s at most), _lp gets <signal> (with "group", its whole process
# group does). Then it must end within 30 s with <status>; right after
# (<gone> s at most, 2 by default, for the kernel), every logged sleep must
# be gone, by its pid; and the checks that started must be exactly <starts>.
# What a broken run left running is killed by the pids it logged.
signalled() {
	_sd=$1 _sc=$2 _sg=$3 _sw=$4 _ss=$5 _sh=$6 _sto=${7:-} _sgb=${8:-2} _sbad=0
	_sn=0
	while [ "$(grep -c '^sleep ' "$_sd/events")" -lt "$_sh" ]; do
		if [ "$_sn" -ge 300 ] || ! kill -0 "$_lp" 2> /dev/null; then
			say "$_sc: the held checks did not start"
			_sbad=1
			break
		fi
		sleep 0.2
		_sn=$((_sn + 1))
	done
	if [ "$_sto" = group ]; then
		kill -s "$_sg" -- "-$_lp" 2> /dev/null || :
	else
		kill -s "$_sg" "$_lp" 2> /dev/null || :
	fi
	_sn=0
	while kill -0 "$_lp" 2> /dev/null && [ "$_sn" -lt 150 ]; do
		sleep 0.2
		_sn=$((_sn + 1))
	done
	if kill -0 "$_lp" 2> /dev/null; then
		say "$_sc: the run did not end within 30 s of SIG$_sg"
		_sbad=1
		kill -s KILL "$_lp" 2> /dev/null || :
	fi
	_sr=0
	wait "$_lp" 2> /dev/null || _sr=$?
	_lp=
	_sn=0
	while :; do
		_sl=
		for _sp in $(awk '$1 == "sleep" { print $3 }' "$_sd/events"); do
			dead "$_sp" || _sl="$_sl $_sp"
		done
		[ -n "$_sl" ] && [ "$_sn" -lt $((_sgb * 5)) ] || break
		sleep 0.2
		_sn=$((_sn + 1))
	done
	if [ -n "$_sl" ]; then
		say "$_sc: a process that a check started outlived the run after SIG$_sg (pid$_sl)"
		_sbad=1
		# the scheduler, the check, then the sleep, each only if still there
		for _sp in $_sl; do
			for _sq in $(awk -v p="$_sp" '$1 == "sleep" && $3 == p { print $5, $4, $3 }' "$_sd/events"); do
				dead "$_sq" || kill -s KILL "$_sq" 2> /dev/null || :
			done
		done
	fi
	[ "$_sr" -eq "$_sw" ] || { say "$_sc: status $_sr after SIG$_sg, want $_sw"; _sbad=1; }
	_sst=$(grep '^start ' "$_sd/events" | tr '\n' ' ')
	[ "$_sst" = "$_ss" ] || { say "$_sc: after SIG$_sg the checks that started were: $_sst"; _sbad=1; }
	return "$_sbad"
}

# interrupt: SIGINT to the scheduler with two checks running, a and b, c
# and d waiting. b ignores SIGTERM: only the SIGKILL after the grace ends
# it. a's SIGTERM trap takes 5 s: the grace (10 s) lets it finish.
c_interrupt() {
	d=$(newcase)
	spec "$d" a 1 0 0 holdtrap 30
	spec "$d" b 1 0 0 holdterm 30
	spec "$d" c 1 0 0
	spec "$d" d 1 0 0
	launch "$d" env GATE_JOBS=2 GP_EVENTS="$d/events" GP_SPEC="$d/spec" TMPDIR="$d/td" \
		sh "$SCHED" "" "" "$d/check-a.sh" "$d/check-b.sh" "$d/check-c.sh" "$d/check-d.sh"
	signalled "$d" interrupt INT 130 "start a start b " 2 || return 1
	grep -qx 'cleaned a' "$d/events" || { say "interrupt: a check's SIGTERM trap of 5 s was cut short"; return 1; }
}

# interrupt1: SIGTERM, then SIGHUP, to the scheduler at GATE_JOBS=1, a
# running and b waiting. The held sleep is 10 s: a scheduler that goes on
# after the signal starts b once it has ended, well inside the 30 s.
c_interrupt1() {
	for _ig in TERM:143 HUP:129; do
		d=$(newcase)
		spec "$d" a 1 0 0 hold 10
		spec "$d" b 1 0 0
		launch "$d" env GATE_JOBS=1 GP_EVENTS="$d/events" GP_SPEC="$d/spec" TMPDIR="$d/td" \
			sh "$SCHED" "" "" "$d/check-a.sh" "$d/check-b.sh"
		signalled "$d" interrupt1 "${_ig%:*}" "${_ig#*:}" "start a " 1 || return 1
	done
}

# gtree <dir> <test.sh> <spec>...: a copy of the gate's layout in <dir>/tree:
# that test.sh, the scheduler under test, and as tests/check-*.sh a
# check-a.sh as <spec> says (see spec), followed by a quick fake for each
# check that test.sh's two lists name, and check-z.sh. No Makefile is
# copied: no build can run in that tree.
gtree() {
	_gtd=$1 _gtf=$2
	shift 2
	mkdir "${_gtd:?}/tree" "${_gtd:?}/tree/scripts" "${_gtd:?}/tree/tests"
	cp "$_gtf" "$_gtd/tree/scripts/test.sh"
	cp "$SCHED" "$_gtd/tree/scripts/gate-checks.sh"
	chmod +x "$_gtd/tree/scripts/test.sh" "$_gtd/tree/scripts/gate-checks.sh"
	spec "$_gtd" a "$@"
	for _tn in $(sed -n -e 's/^gate_first="\(.*\)"$/\1/p' -e 's/^gate_serial="\(.*\)"$/\1/p' "$_gtf") check-z.sh; do
		_tn=${_tn#check-}
		spec "$_gtd" "${_tn%.sh}" 1 0 0
	done
	mv "$_gtd"/check-*.sh "$_gtd/tree/tests/"
}

# testsh: SIGTERM, SIGHUP and SIGINT to scripts/test.sh (or $WIRING) itself,
# run on a copy of the gate's layout (gtree) with a held check-a.sh (10 s,
# as in interrupt1); GATE_JOBS=1, so check-a.sh runs first and alone
c_testsh() {
	_tf=${WIRING:-$top/scripts/test.sh}
	for _tg in TERM:143 HUP:129 INT:130; do
		d=$(newcase)
		gtree "$d" "$_tf" 1 0 0 hold 10
		launch "$d" env GATE_JOBS=1 GP_EVENTS="$d/events" GP_SPEC="$d/spec" TMPDIR="$d/td" sh "$d/tree/scripts/test.sh"
		signalled "$d" testsh "${_tg%:*}" "${_tg#*:}" "start a " 1 || return 1
	done
}

# kgrp: SIGKILL to the process group of scripts/test.sh (or $WIRING), on a
# copy of the gate's layout, while check-a.sh holds a 30 s sleep: test.sh
# dies with status 137 (nothing can trap SIGKILL), and its scheduler, in a
# group of its own, sees within one poll that the test.sh that started it is
# gone and ends every check: the sleep is gone within 10 s, nothing else starts
c_kgrp() {
	_tf=${WIRING:-$top/scripts/test.sh}
	d=$(newcase)
	gtree "$d" "$_tf" 1 0 0 hold 30
	launch "$d" env GATE_JOBS=1 GP_EVENTS="$d/events" GP_SPEC="$d/spec" TMPDIR="$d/td" sh "$d/tree/scripts/test.sh"
	signalled "$d" kgrp KILL 137 "start a " 1 group 10
}

# sigs: a probe check-a.sh run through scripts/test.sh (or $WIRING), on a
# copy of the gate's layout, gets SIGINT, SIGQUIT, SIGHUP, SIGTERM and
# SIGTSTP as it does when it is started directly from here, as
# origin/main's test.sh ran each check (in its own foreground, no trap set),
# and SIGINT is delivered to it
c_sigs() {
	_tf=${WIRING:-$top/scripts/test.sh}
	dc=$(newcase)
	spec "$dc" a 1 0 0 probe
	launch "$dc" env GP_EVENTS="$dc/events" GP_SPEC="$dc/spec" "$dc/check-a.sh"
	rc=0
	lwait 30 || rc=$?
	_sgw=$(grep '^sig ' "$dc/events" || :)
	[ "$rc" -eq 0 ] && [ "$(printf '%s\n' "$_sgw" | grep -cE ' (delivered|ignored)$')" -eq 5 ] ||
		{ say "sigs: the probe run directly gave status $rc and: $_sgw"; return 1; }
	d=$(newcase)
	gtree "$d" "$_tf" 1 0 0 probe
	launch "$d" env GATE_JOBS=1 GP_EVENTS="$d/events" GP_SPEC="$d/spec" TMPDIR="$d/td" PATH="$tmp/make:$PATH" \
		sh "$d/tree/scripts/test.sh"
	rc=0
	lwait 90 || rc=$?
	[ "$rc" -eq 0 ] && grep -q '^make ' "$d/events" || { say "sigs: test.sh on the probe gave status $rc"; return 1; }
	_sgt=$(grep '^sig ' "$d/events" || :)
	[ "$_sgt" = "$_sgw" ] || { say "sigs: a check run by test.sh got: $_sgt; run directly: $_sgw" | tr '\n' ' '; echo; return 1; }
	printf '%s\n' "$_sgt" | grep -qx 'sig a INT delivered' || { say "sigs: SIGINT is not delivered to a check run by test.sh"; return 1; }
}

# leave: a check that leaves a sleep running in the background (its output
# elsewhere) and exits 0 passes, and the sleep is gone once the run has
# ended: the scheduler ends a check's process group when the check ends
c_leave() {
	d=$(newcase)
	spec "$d" a 1 0 0 leave 30
	spec "$d" b 1 0 0
	rc=0
	run "$d" 2 "" a b || rc=$?
	_lv=$(awk '$1 == "sleep" { print $3 }' "$d/events")
	[ -n "$_lv" ] || { say "leave: the check did not log its sleep"; return 1; }
	_ln=0
	while ! dead "$_lv" && [ "$_ln" -lt 10 ]; do
		sleep 0.2
		_ln=$((_ln + 1))
	done
	dead "$_lv" || { say "leave: what a passing check left running outlived the run (pid $_lv)"; kill -s KILL "$_lv" 2> /dev/null || :; return 1; }
	[ "$rc" -eq 0 ] && [ "$(status "$d" a)" = ok ] || { say "leave: status $rc, want 0, and check-a.sh passed"; return 1; }
}

# lit <file> <old> <new>: the file with its one occurrence of <old>
# replaced by <new>, both taken literally (no backslash in either: awk -v
# reads escapes); status 1 unless exactly one
lit() {
	awk -v o="$2" -v n="$3" '
		{ p = index($0, o); if (p) { c++; $0 = substr($0, 1, p - 1) n substr($0, p + length(o)) } print }
		END { exit c != 1 }' "$1"
}

cases="one_fail two_fail signal log order serial jobs1 first bound default times errors guard wiring interrupt interrupt1 testsh kgrp sigs leave"
for c in $cases; do
	if "c_$c"; then echo "  ok   $c"; else echo "  FAIL $c"; fail=1; fi
done

# mutant <name> <case> <old> <new>: the scheduler with <old> replaced must fail <case>
mutants=0
mutant() {
	lit "$real" "$3" "$4" > "$tmp/mut-$1.sh" || { bad "mutant $1 does not apply"; return 0; }
	SCHED=$tmp/mut-$1.sh quiet=1
	if "c_$2"; then bad "mutant $1 was not caught by case $2"; else mutants=$((mutants + 1)); fi
	SCHED=$real quiet=0
}
mutant status-ignored one_fail 'wait "$_grpid" || _grst=$?' 'wait "$_grpid" || :'
mutant stop-at-failure one_fail '		gate_start "$_gpi"' '		[ "$_gnf" -ne 0 ] || gate_start "$_gpi"'
mutant first-only two_fail '_gfailed="$_gfailed $_gfname"' '_gfailed=${_gfailed:-" $_gfname"}'
mutant signal-ok signal '[ "$_gfs" -eq 0 ] || _gfwhy=' '[ "$_gfs" -eq 0 ] || [ "$_gfs" -gt 128 ] || _gfwhy='
mutant log-unchecked log 'cat "$_gd/$_gnext.log" || _gfwhy="its log cannot be read"' 'cat "$_gd/$_gnext.log" || :'
mutant no-log order '"$_gsc" < /dev/null > "$_gd/$1.log" 2>&1 &' '"$_gsc" < /dev/null &'
mutant print-unended order 'while [ "$_gnext" -le "$_gn" ] && [ -e "$_gd/$_gnext.end" ]; do' 'while [ "$_gnext" -le "$_gn" ]; do'
mutant logs-left order '	rm -rf "${_gd:?}"' '	:'
mutant serial-ignored serial 'if [ "$_gj" -gt 1 ] && gate_in "$_gserial" "${_gc##*/}"; then' 'if [ "$_gj" -gt 1 ] && false; then'
mutant no-drain serial '	while [ "$_gnr" -gt 0 ]; do gate_reap; done' '	:'
mutant serial-moved-at-1 jobs1 'if [ "$_gj" -gt 1 ] && gate_in "$_gserial"' 'if gate_in "$_gserial"'
mutant first-moved-at-1 jobs1 'elif [ "$_gj" -gt 1 ] && gate_in "$_gfirst"' 'elif gate_in "$_gfirst"'
mutant first-front-at-1 jobs1 '	if [ "$_gj" -gt 1 ]; then' '	if :; then'
mutant first-twice first 'elif [ "$_gj" -gt 1 ] && gate_in "$_gfirst" "${_gc##*/}"; then' 'elif false; then'
mutant first-dropped first '		for _gw in $_gfirst; do' '		for _gw in; do'
mutant first-duplicate first ' && ! gate_in "$_gfront" "$_gi"; then' '; then'
mutant first-over-serial first ' && ! gate_in "$_gserial" "$_gw" &&' ' &&'
mutant first-unknown errors 'for _gw in $_gserial $_gfirst; do' 'for _gw in $_gserial; do'
mutant limit-plus-one jobs1 'while [ "$_gnr" -ge "$_gl" ]; do gate_reap; done' 'while [ "$_gnr" -gt "$_gl" ]; do gate_reap; done'
mutant limit-plus-one-b bound 'while [ "$_gnr" -ge "$_gl" ]; do gate_reap; done' 'while [ "$_gnr" -gt "$_gl" ]; do gate_reap; done'
mutant env-ignored bound 'if [ -n "${GATE_JOBS:-}" ]; then' 'if false; then'
mutant default-whole default '_gjn=$((_gjn / 2))' '_gjn=$((_gjn / 1))'
mutant default-floor default '[ "$_gjn" -ge 1 ] || _gjn=1' ':'
mutant default-garbage default 'case $_gjn in *[!0-9]*) _gjn=1 ;; esac' ':'
mutant default-fallback default '|| nproc 2> /dev/null || echo 1)' '|| nproc 2> /dev/null || echo 4)'
mutant check-time-zero times '"$(($(date +%s) - _grt0))"' '0'
mutant check-time-epoch times '"$(($(date +%s) - _grt0))"' '"$(date +%s)"'
mutant total-zero times '_gtt=$(($(date +%s) - _gt0))' '_gtt=0'
mutant total-epoch times '_gtt=$(($(date +%s) - _gt0))' '_gtt=$(date +%s)'
mutant jobs-zero errors 'case $_gj in *[!0-9]* | 0*)' 'case $_gj in *[!0-9]*)'
mutant jobs-word errors 'case $_gj in *[!0-9]* | 0*)' 'case $_gj in 0*)'
mutant serial-unknown errors 'if [ "$_gfound" = 0 ]; then' 'if false; then'
mutant no-checks errors 'if [ $# -eq 0 ]; then' 'if false; then'
# no-scratch: mktemp's failure ignored; the path it then uses is in the
# case's own scratch and does not exist, so every write fails there
mutant no-scratch errors '_gd=$(mktemp -d "${TMPDIR:-/tmp}/gate.XXXXXX") ||' '_gd=$(mktemp -d "${TMPDIR:-/tmp}/gate.XXXXXX" || echo "${TMPDIR:?}/gate.none") ||'
mutant guard-off guard 'if [ "$_gnext" -le "$_gn" ]; then' 'if false; then'
mutant group-kill-dropped interrupt 'kill -s "$_gks" -- "-$_gkg"' 'kill -s "$_gks" -- "$_gkg"'
mutant no-process-group interrupt '	set -m' '	:'
mutant no-kill-after-grace interrupt '	gate_kill KILL "$@"' '	:'
mutant grace-3s interrupt 'gate_grace=10' 'gate_grace=3'
mutant no-parent-check kgrp 'if [ -n "$_gparent" ] && ! gate_parent "$_gparent"; then' 'if false; then'
mutant no-end-at-exit leave '			gate_end "$_grpid"' '			:'
mutant int-passes interrupt "trap 'exit 130' INT" "trap 'exit 0' INT"
mutant term-goes-on interrupt1 "trap 'exit 143' TERM" "trap ':' TERM"
mutant term-passes interrupt1 "trap 'exit 143' TERM" "trap 'exit 0' TERM"
mutant hup-passes interrupt1 "trap 'exit 129' HUP" "trap 'exit 0' HUP"

# mutants of scripts/test.sh: wmut <name> <case> <old> <new>
wmut() {
	lit "$top/scripts/test.sh" "$3" "$4" > "$tmp/test-$1.sh" || { bad "mutant $1 does not apply to scripts/test.sh"; return 0; }
	quiet=1 WIRING=$tmp/test-$1.sh
	if "c_$2"; then bad "mutant $1 was not caught by case $2"; else mutants=$((mutants + 1)); fi
	quiet=0 WIRING=
}
wmut wiring-dropped wiring 'scripts/gate-checks.sh "$gate_serial" "$gate_first" tests/check-*.sh &' ': tests/check-*.sh &'
wmut wiring-ignored wiring 'wait "$gate_sched"' 'wait "$gate_sched" || :'
wmut wiring-subset wiring 'scripts/gate-checks.sh "$gate_serial" "$gate_first" tests/check-*.sh &' 'scripts/gate-checks.sh "$gate_serial" "$gate_first" tests/check-a*.sh &'
wmut wiring-no-errexit wiring 'set -eu' 'set -u'
wmut stop-not-passed testsh '		kill -s TERM "$gate_sched" 2> /dev/null || :' '		:'
wmut stop-passes testsh "trap 'gate_stop 143' TERM" "trap 'gate_stop 0' TERM"
wmut no-own-group sigs 'set -m' ':'
wmut no-parent-pid kgrp 'GATE_PARENT=$$ scripts/gate-checks.sh' 'scripts/gate-checks.sh'

if [ "$fail" -ne 0 ]; then
	echo "$me: FAILED"
	exit 1
fi
echo "$me: ok ($(echo $cases | wc -w | tr -d ' ') cases, $mutants mutants caught)"
