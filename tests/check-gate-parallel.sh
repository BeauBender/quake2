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
#             recorded pid) is gone, and no other check starts (SIGTERM in
#             its place, status 143, when SIGINT is ignored here: below);
#   interrupt1  SIGTERM, then SIGHUP, at GATE_JOBS=1, one check running and
#             one waiting: the same, with status 143 or 129;
#   testsh    SIGTERM, SIGHUP and SIGINT to scripts/test.sh itself, run on a
#             copy of the gate's layout with fake checks: the same, with
#             test.sh's status 143, 129 or 130 (no SIGINT when it is
#             ignored here);
#   kgrp      SIGKILL to test.sh's process group there: test.sh's status
#             137, and what the running check started is gone within 10 s;
#   sigs      a check run by test.sh there gets SIGINT, SIGQUIT, SIGHUP,
#             SIGTERM, SIGTSTP, SIGTTIN and SIGTTOU as one started directly does (as
#             origin/main's test.sh ran each check), SIGINT delivered
#             (not asked when SIGINT is ignored here);
#   ttyfork   what the shell writes between fork and exec when test.sh
#             starts the scheduler, or the scheduler a check, reaches no
#             terminal: on a tostop one it would stop that child (forced
#             here with xtrace; under load bash's "child setpgid" line);
#   nostderr  test.sh started with its standard error closed, outside the
#             lock and at its re-entry, runs the scheduler and gives its
#             status;
#   watchfork the same for test.sh's start of the lock watcher, under the
#             lock, and the scheduler's own standard error reaches test.sh's;
#   leave    what a passing check leaves running is gone after the run;
#   stop      the disposition probe of the gate's stop (FIX186, through the
#             bounded-run primitive): a check and its grandchild, each held
#             by a flag file and each writing every signal it gets to a
#             record file of its own; SIGTERM, SIGINT and SIGHUP to the
#             scheduler (status 143, 130, 129), and its own end once the
#             check's flag is gone (status 0, the grandchild stopped): both
#             get SIGTERM and are gone. Both ignoring SIGTERM: SIGKILL ends
#             them, no sooner than the 10 s grace. The scheduler started
#             with SIGINT ignored (as merge-gate.sh starts the gate): SIGINT
#             then SIGTERM give status 143, both stopped (no SIGINT when it
#             is ignored here);
#   intskip   interrupt, testsh and sigs, run by this self-test started
#             with SIGINT ignored, each say what they skip and pass.
# Then each case is shown failing against at least one mutant of the
# scheduler (or of scripts/test.sh, for wiring and testsh). Every run of the
# scheduler is bounded in time here; one that outlives its bound fails its
# case. The bounds leave room for a loaded host: they only catch a hang.
#
# SIGINT: when this self-test starts with SIGINT ignored (a background job
# started without job control has it so, as a merge gate may start the
# gate), so does everything it starts, and no shell can undo that (POSIX:
# a signal ignored on entry cannot be trapped or reset). It asks first, as
# tests/check-bench.sh does, whether SIGINT reaches a command here; when it
# does not, the parts that need it are skipped, each with a line that says
# so: interrupt sends SIGTERM in its place, testsh sends no SIGINT, sigs
# does not ask for SIGINT delivered, and the mutants that only SIGINT can
# catch are counted as skipped, never as caught. Every other assertion runs.
# GP_ONLY="<case>..." runs those cases alone, stopping at the first that
# fails, and no mutant (intskip uses it).
#
# Usage: tests/check-gate-parallel.sh
set -eu
cd "$(dirname "$0")/.."
me=check-gate-parallel
top=$(pwd -P)
real=$top/scripts/gate-checks.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/gatepar.XXXXXX")
tmp=$(cd "${tmp:?}" && pwd -P)
only=${GP_ONLY:-}
# _lp: a run started by launch and not yet reaped; on the way out it gets
# SIGTERM, which ends it and what it started (that is what the cases test)
_lp=
# _xdp, _xdl: the shell and the sleep case tdesc started and has not yet
# reaped; on the way out the shell's own children (its sleep) get SIGKILL,
# the shell gets SIGCONT (it may be stopped) and SIGKILL, and so does the sleep
_xdp=
_xdl=
gp_exit() {
	if [ -n "$_lp" ]; then
		kill -s TERM "$_lp" 2> /dev/null || :
		wait "$_lp" 2> /dev/null || :
	fi
	if [ -n "$_xdp" ]; then
		for _xk in $(ps -ax -o pid= -o ppid= | awk -v p="$_xdp" '$2 == p { print $1 }'); do
			kill -s KILL "$_xk" 2> /dev/null || :
		done
		kill -s CONT "$_xdp" 2> /dev/null || :
		kill -s KILL "$_xdp" 2> /dev/null || :
		wait "$_xdp" 2> /dev/null || :
	fi
	if [ -n "$_xdl" ]; then
		kill -s KILL "$_xdl" 2> /dev/null || :
		wait "$_xdl" 2> /dev/null || :
	fi
	rm -rf "${tmp:?}"
}
trap gp_exit EXIT
trap 'exit 1' HUP INT TERM
unset GATE_JOBS GATE_PARENT GP_PATH GP_TD GP_NCPU GP_BOUND GP_FIRST GP_ONLY WIRING SELF TTY_BOUND TTY_HOLD TTY_MODE TTY_X
export LC_ALL=C
# INT_OK: SIGINT reaches a command started here (see the top of this file)
INT_OK=
sh -c 'kill -INT $$; exit 0' 2> /dev/null || INT_OK=1

fail=0
quiet=0
SCHED=$real
# (printf, not echo: this shell's echo decodes backslashes, and a line can
# hold process text, which is printed as it is)
bad() { printf '%s\n' "$me: $*"; fail=1; }
say() { [ "$quiet" = 1 ] || printf '%s\n' "$me: $*"; }
# skipint <part>: says that <part> is skipped, as SIGINT is ignored here
skipint() { say "SIGINT is ignored where this check runs, so $1 is skipped"; }

# The fake check: its name comes from its file name, its behaviour from
# $GP_SPEC/<name>: "<lines> <delay> <status> <mode> <hold>"; mode term or
# kill makes it die from that signal after its lines; mode hold runs, after
# its lines, "sleep <hold>" in the foreground, holdterm the same ignoring
# SIGTERM (and so does that sleep), holdtrap the same with a SIGTERM trap
# that takes 5 s and then logs "cleaned <name>"; mode leave starts that
# sleep in the background and exits; mode holdtty sleeps <hold> s, then
# writes a line to its terminal (with tostop, from a background group, that
# write stops it). The sleep logs "sleep <name> <its pid>
# <the check's pid> <the scheduler's pid>". Mode probe logs how SIGINT,
# SIGQUIT, SIGHUP, SIGTERM, SIGTSTP, SIGTTIN and SIGTTOU reach the check.
mkdir "${tmp:?}/bin" "${tmp:?}/shim" "${tmp:?}/make"
# the scheduler sources the bounded-run primitive from bench/core/ in its
# own directory (FIX186): its mutants are written to $tmp, so it is there too
mkdir -p "${tmp:?}/bench/core"
cp "$top/scripts/bench/core/common.sh" "${tmp:?}/bench/core/common.sh"
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
# stopprobe <signal>: the signal goes to this check itself (a shell resets
# it in what it starts): a helper sees it stopped (delivered; it then sends
# SIGCONT) or sees the mark this check leaves once the kill has returned
# (ignored)
stopprobe() {
	_pm=$GP_EVENTS.cont.$$.$1
	sh -c 'i=0
		while [ "$i" -lt 50 ]; do
			case $(ps -o stat= -p "$1") in *T*) r=delivered; break ;; esac
			[ ! -e "$3" ] || { r=ignored; break; }
			sleep 0.2
			i=$((i + 1))
		done
		printf "sig %s %s %s\n" "$2" "$4" "${r:-unknown}" >> "$GP_EVENTS"
		kill -s CONT "$1"' sh "$$" "$n" "$_pm" "$1" &
	kill -s "$1" $$
	: > "$_pm"
	wait
}
case $mode in
hold | holdterm | holdtrap)
	[ "$mode" != holdterm ] || trap '' TERM
	[ "$mode" != holdtrap ] || trap 'sleep 5; printf "cleaned %s\n" "$n" >> "$GP_EVENTS"; exit 1' TERM
	sh -c 'printf "sleep %s %s %s %s\n" "$1" "$$" "$2" "$3" >> "$GP_EVENTS"; exec sleep "$4"' sh "$n" "$$" "$PPID" "$hold"
	;;
holdtty)
	sleep "$hold"
	printf '%s late\n' "$n" > /dev/tty
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
	stopprobe TSTP
	stopprobe TTIN
	stopprobe TTOU
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
# The stand-in of the stop case: stand <who> <dir> [ignore] writes its pid
# to <dir>/<who>.pid and lives while <dir>/<who>.flag exists; every SIGTERM,
# SIGINT or SIGHUP it gets is a line in <dir>/<who>.sig, after which it
# exits (with "ignore" it goes on after a SIGTERM); its end by the flag is
# the line "end" there. The check (who=check) starts the grandchild (gc).
cat > "${tmp:?}/stand" << 'EOF'
#!/bin/sh
who=$1 r=$2 ign=${3:-}
for s in TERM INT HUP; do
	if [ "$s" = TERM ] && [ -n "$ign" ]; then
		trap "echo $s >> \"\$r/$who.sig\"" "$s"
	else
		trap "echo $s >> \"\$r/$who.sig\"; exit 1" "$s"
	fi
done
if [ "$who" = check ]; then "$0" gc "$r" "$ign" & fi
echo "$$" > "$r/$who.pid"
while [ -e "$r/$who.flag" ]; do sleep 0.1; done
echo end >> "$r/$who.sig"
EOF
chmod +x "${tmp:?}/stand"
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
	# checks that start together may log in either order: d and c, then a and b, as sets, then e
	_fs=$(grep '^start ' "$d/events")
	[ "$(printf '%s\n' "$_fs" | sed -n 1,2p | sort | tr '\n' ' ')$(printf '%s\n' "$_fs" | sed -n 3,4p | sort | tr '\n' ' ')$(printf '%s\n' "$_fs" | sed -n 5,9p | tr '\n' ' ')" = "start c start d start a start b start e " ] ||
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
# failure (set -e). Job control on for the start: sigs shows it. (The
# start's own standard error is /dev/null for the fork, the scheduler's is
# test.sh's on fd 3: ttyfork shows why.)
c_wiring() {
	_wf=${WIRING:-$top/scripts/test.sh}
	_wl=$(grep -n '^{ 2>&3 3>&- GATE_PARENT=\$\$ scripts/gate-checks\.sh "\$gate_serial" "\$gate_first" tests/check-\*\.sh & } 3>&2 2> /dev/null$' "$_wf" | cut -d: -f1)
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
# ignores SIGINT); it also leads a process group of its own. Both sides of
# the fork make that group (setpgid), at the same time: on a busy host the
# child's call can lose that race, and bash then prints "child setpgid
# (<pid> to <pid>): Operation not permitted" from the child, before its
# redirections, although the parent's call made the group. That line goes
# to <dir>/launch.err, out of this check's output; a run that does not lead
# a group of its own fails this self-test, whatever its case says.
launch() {
	_ld=$1
	shift
	set -m
	{ "$@" > "$_ld/out" 2>&1 & } 2> "$_ld/launch.err"
	_lp=$!
	set +m
	_lg=$(ps -o pgid= -p "$_lp" 2> /dev/null | tr -d ' ')
	[ -z "$_lg" ] || [ "$_lg" = "$_lp" ] ||
		bad "launch: pid $_lp is in process group $_lg, not one of its own: $(cat "$_ld/launch.err")"
}
# dead <pid>: no such process, or a zombie (it has ended, not yet reaped)
dead() {
	kill -0 "$1" 2> /dev/null || return 0
	case $(ps -o stat= -p "$1" 2> /dev/null) in *Z*) return 0 ;; esac
	return 1
}
# lwait <seconds> [keep [<probe> <arg>]]: waits for the run _lp (from
# launch), at most <seconds>; its status, or 124 once it outlives them (it
# is then killed). With a probe, "<probe> <arg>" is run every 5 polls (1 s),
# and once it succeeds the wait ends as at the bound. _wn is left as the
# number of polls waited (5 a second).
lwait() {
	_wn=0
	while kill -0 "$_lp" 2> /dev/null; do
		if [ "$_wn" -ge $(($1 * 5)) ] || { [ -n "${3:-}" ] && [ $((_wn % 5)) -eq 4 ] && "$3" "${4:-}"; }; then
			# with a second argument ("keep") the run is left alone: the
			# caller reads its state first and ends it (and _lp) itself
			[ -z "${2:-}" ] || return 124
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
	_sst=$(grep '^start ' "$_sd/events" | sort | tr '\n' ' ')
	[ "$_sst" = "$_ss" ] || { say "$_sc: after SIG$_sg the checks that started were: $_sst"; _sbad=1; }
	return "$_sbad"
}

# interrupt: SIGINT to the scheduler with two checks running, a and b, c
# and d waiting. b ignores SIGTERM: only the SIGKILL after the grace ends
# it. a's SIGTERM trap takes 5 s: the grace (10 s) lets it finish. SIGTERM
# in its place when SIGINT is ignored here: the rest still holds.
c_interrupt() {
	_ii=INT:130
	[ -n "$INT_OK" ] || { skipint "interrupt's SIGINT (SIGTERM is sent in its place)"; _ii=TERM:143; }
	d=$(newcase)
	spec "$d" a 1 0 0 holdtrap 30
	spec "$d" b 1 0 0 holdterm 30
	spec "$d" c 1 0 0
	spec "$d" d 1 0 0
	launch "$d" env GATE_JOBS=2 GP_EVENTS="$d/events" GP_SPEC="$d/spec" TMPDIR="$d/td" \
		sh "$SCHED" "" "" "$d/check-a.sh" "$d/check-b.sh" "$d/check-c.sh" "$d/check-d.sh"
	signalled "$d" interrupt "${_ii%:*}" "${_ii#*:}" "start a start b " 2 || return 1
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
	mkdir -p "${_gtd:?}/tree/scripts/bench/core"
	cp "$(dirname "$SCHED")/bench/core/common.sh" "$_gtd/tree/scripts/bench/core/common.sh"
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
	_ts="TERM:143 HUP:129 INT:130"
	[ -n "$INT_OK" ] || { skipint "testsh's SIGINT"; _ts="TERM:143 HUP:129"; }
	for _tg in $_ts; do
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
	# (2> /dev/null: the shell's own "Killed: 9" line for test.sh)
	signalled "$d" kgrp KILL 137 "start a " 1 group 10 2> /dev/null
}

# sigs: a probe check-a.sh run through scripts/test.sh (or $WIRING), on a
# copy of the gate's layout, gets SIGINT, SIGQUIT, SIGHUP, SIGTERM, SIGTSTP,
# SIGTTIN and SIGTTOU as it does when it is started directly from here, as
# origin/main's test.sh ran each check (in its own foreground, no trap set),
# and SIGINT is delivered to it (when SIGINT is ignored here, both show it
# ignored, and that last is skipped)
c_sigs() {
	_tf=${WIRING:-$top/scripts/test.sh}
	dc=$(newcase)
	spec "$dc" a 1 0 0 probe
	launch "$dc" env GP_EVENTS="$dc/events" GP_SPEC="$dc/spec" "$dc/check-a.sh"
	rc=0
	lwait 30 || rc=$?
	_sgw=$(grep '^sig ' "$dc/events" || :)
	[ "$rc" -eq 0 ] && [ "$(printf '%s\n' "$_sgw" | grep -cE ' (delivered|ignored)$')" -eq 7 ] ||
		{ say "sigs: the probe run directly gave status $rc and: $_sgw"; return 1; }
	d=$(newcase)
	gtree "$d" "$_tf" 1 0 0 probe
	launch "$d" env GATE_JOBS=1 GP_EVENTS="$d/events" GP_SPEC="$d/spec" TMPDIR="$d/td" PATH="$tmp/make:$PATH" \
		sh "$d/tree/scripts/test.sh"
	rc=0
	lwait 90 || rc=$?
	[ "$rc" -eq 0 ] && grep -q '^make ' "$d/events" || { say "sigs: test.sh on the probe gave status $rc"; return 1; }
	_sgt=$(grep '^sig ' "$d/events" || :)
	[ "$_sgt" = "$_sgw" ] ||
		{ say "sigs: a check run by test.sh got: $(printf '%s\n' "$_sgt" | tr '\n' ' ')run directly: $(printf '%s\n' "$_sgw" | tr '\n' ' ')"; return 1; }
	if [ -n "$INT_OK" ]; then
		printf '%s\n' "$_sgt" | grep -qx 'sig a INT delivered' || { say "sigs: SIGINT is not delivered to a check run by test.sh"; return 1; }
	else
		skipint "sigs' assertion that SIGINT is delivered"
	fi
}

# tscrub: its standard input on one line (lines joined by a space), with the
# home directory printed as ~ wherever it occurs, unless the next character
# continues a name (a letter, a digit or _: another account's directory), cut
# to 120 characters. Nothing is required before the match: a scrubber fails
# toward over-scrubbing, and a home path inside a longer path hides nothing
# worth keeping. HOME is read from the environment (awk -v would decode its
# backslashes) and matched literally (index, not a pattern), its trailing
# slashes dropped: HOME empty or / replaces nothing. LC_ALL=C is pinned for
# its awk, so a byte invalid in the caller's locale never aborts it.
tscrub() {
	LC_ALL=C awk '
		{ s = s (NR > 1 ? " " : "") $0 }
		END {
			h = ENVIRON["HOME"]
			while (substr(h, length(h)) == "/") h = substr(h, 1, length(h) - 1)
			o = ""
			while (h != "" && (i = index(s, h)) > 0) {
				c = substr(s, i + length(h), 1)
				if (c !~ /[A-Za-z0-9_]/) { o = o substr(s, 1, i - 1) "~"; s = substr(s, i + length(h)) }
				else { o = o substr(s, 1, i); s = substr(s, i + 1); }
			}
			print substr(o s, 1, 120)
		}'
}
# tscrub_main: the reference, a fixed copy of origin/main's tscrub, which
# replaced HOME before a /, a space or the end. The scrub case holds tscrub to
# leave no HOME that this one replaced.
tscrub_main() {
	awk '
		{ s = s (NR > 1 ? " " : "") $0 }
		END {
			h = ENVIRON["HOME"]
			sub("/+$", "", h)
			o = ""
			while (h != "" && (i = index(s, h)) > 0) {
				c = substr(s, i + length(h), 1)
				if (c == "/" || c == " " || c == "") { o = o substr(s, 1, i - 1) "~"; s = substr(s, i + length(h)) }
				else { o = o substr(s, 1, i); s = substr(s, i + 1) }
			}
			lim = 120; print substr(o s, 1, lim)
		}'
}
# tdesc <pid>: that process, read now, on one line, as "[pid=.. stat=..
# pgid=.. comm=.. args=..]": stat and pgid from one ps call, comm and args
# through tscrub. One that is gone leaves its fields empty.
tdesc() {
	_tdp=$1
	_tdv=$(ps -o stat= -o pgid= -p "$_tdp" 2> /dev/null || :)
	_tdc=$(ps -o comm= -p "$_tdp" 2> /dev/null | tscrub)
	_tda=$(ps -o args= -p "$_tdp" 2> /dev/null | tscrub)
	set -- $_tdv
	printf '%s\n' "[pid=$_tdp stat=${1:-} pgid=${2:-} comm=$_tdc args=$_tda]"
}
# tcut <width> <word>...: the words, each after a space, as many whole ones
# as fit in <width> characters
tcut() {
	_tcw=$1 _tco=
	shift
	for _tcx; do
		[ $((${#_tco} + 1 + ${#_tcx})) -le "$_tcw" ] || break
		_tco="$_tco $_tcx"
	done
	printf '%s' "$_tco"
}
# tsort <pid>...: reads each one's stat now, leaving out one that is gone; a
# stopped one (a stat with T; a slow run is S or R) sets _tstop and goes to
# _tdesc (tdesc), the others' " pid=stat" to _toth, and all to _tstate
tsort() {
	_tstate= _tstop= _tdesc= _toth=
	for _tk; do
		_tst=$(ps -o stat= -p "$_tk" 2> /dev/null | tr -d ' ' || :)
		[ -n "$_tst" ] || continue
		_tstate="$_tstate $_tk=$_tst"
		case $_tst in
		*T*)
			_tstop=1
			_tdesc="$_tdesc $(tdesc "$_tk")"
			;;
		*) _toth="$_toth $_tk=$_tst" ;;
		esac
	done
}
# ttree <pid>: "<pid> <stat>" for <pid> and each process that descends from
# it, from one ps snapshot
ttree() {
	ps -ax -o pid= -o ppid= -o stat= | awk -v p="$1" '
		{ par[$1] = $2; st[$1] = $3 }
		END { for (q in par) { x = q; while (x != "" && x != 0 && x != p) x = (x in par) ? par[x] : 0; if (x == p) print q, st[q] } }'
}
# tkids <pid>: the pids whose parent is <pid>, from one ps snapshot
tkids() {
	ps -ax -o pid= -o ppid= | awk -v p="$1" '$2 == p { print $1 }'
}
# tstopped <dir>: a process of the run, the test.sh whose pid is in
# <dir>/shpid or one that descends from it, is stopped (T)
tstopped() {
	_tsy=$(cat "$1/shpid" 2> /dev/null || :)
	[ -n "$_tsy" ] || return 1
	ttree "$_tsy" | awk '$2 ~ /T/ { f = 1 } END { exit !f }'
}

# tty: scripts/test.sh (or $WIRING), on a copy of the gate's layout and one
# passing check that prints a line, run on a terminal (script(1)) set to
# `stty tostop`. The scheduler is in a process group of its own, not the
# terminal's foreground group, so it must ignore SIGTTOU or its first write
# to the terminal stops it and the gate hangs. The run must end with status
# 0 within 90 s and show the check's line. The wait ends at once when one of
# its processes is stopped; then, or on timeout, what the run left (test.sh,
# whose pid is recorded before its exec, and what descends from it) is read,
# and ended by pid. A stop is named on one line, each stopped process with
# tdesc; the others' pid=stat go on one state: line, whole entries within
# 200 characters. A slow run prints one timed-out line and no state: line.
# TTY_BOUND, TTY_HOLD and TTY_MODE change the bound and check-a.sh's mode;
# TTY_X=1 runs test.sh with sh -x (ttyfork)
c_tty() {
	_tf=${WIRING:-$top/scripts/test.sh}
	_tb=${TTY_BOUND:-90}
	d=$(newcase)
	if [ -n "${TTY_HOLD:-}" ]; then gtree "$d" "$_tf" 1 0 0 "${TTY_MODE:-hold}" "$TTY_HOLD"; else gtree "$d" "$_tf" 1 0 0; fi
	launch "$d" script -q /dev/null sh -c 'printf "%s\n" "$$" > "$1/shpid"; stty tostop; shift; exec "$@"' sh "$d" \
		env GATE_JOBS=1 GP_EVENTS="$d/events" GP_SPEC="$d/spec" TMPDIR="$d/td" PATH="$tmp/make:$PATH" PS4='+ ' sh ${TTY_X:+-x} "$d/tree/scripts/test.sh"
	rc=0
	# the wait also ends, as at the bound, once a process of the run is
	# stopped: nothing sends it SIGCONT, so a stop never resumes
	lwait "$_tb" keep tstopped "$d" || rc=$?
	if [ "$rc" -eq 124 ]; then
		# what it waited, in whole seconds: the bound, unless a stop ended it
		_twt=$((_wn / 5))
		_ty=$(cat "$d/shpid" 2> /dev/null || :)
		_tstate=
		_tstop=
		_tdesc=
		_toth=
		if [ -n "$_ty" ]; then
			# the run's own processes: test.sh (its pid is recorded) and
			# all that descends from it, read before any is ended
			tsort $(ttree "$_ty" | awk '{ print $1 }')
			for _tk in $(ps -ax -o pid= -o ppid= | awk -v p="$_ty" '$2 == p { print $1 }'); do
				kill -s KILL -- "-$_tk" 2> /dev/null || kill -s KILL "$_tk" 2> /dev/null || :
			done
			kill -s KILL "$_ty" 2> /dev/null || :
		fi
		# only now is script(1) itself ended (its pty closing could
		# hang up the run before its state was read)
		kill -s KILL "$_lp" 2> /dev/null || :
		wait "$_lp" 2> /dev/null || :
		_lp=
		if [ -n "$_tstop" ]; then
			say "tty: the run stopped on a terminal with tostop (test.sh pid ${_ty:-unknown}), and was ended by pid; stopped:$_tdesc"
			say "tty: state:$(tcut 200 $_toth)"
		elif [ -n "$_ty" ] && [ -z "$_tstate" ]; then
			say "tty: timed out after $_twt s; the run's state could not be read (tostop not ruled out)"
		else
			say "tty: timed out after $_twt s (state:$_tstate), not stopped: a slow run, not the tostop bug"
		fi
		return 1
	fi
	[ "$rc" -eq 0 ] || { say "tty: status $rc on a terminal with tostop, want 0"; return 1; }
	grep -q 'a line 1' "$d/out" || { say "tty: the check's line did not reach the terminal"; return 1; }
}

# ttymsg: what c_tty says on a timeout tells a real stop from a slow run.
# The scheduler without its SIGTTOU ignore (a mutant), run with HOME=/bin,
# stops on its first write to the terminal: the case fails with one stop
# line that names it (pid, stat T, pgid, comm and args, the last two ~/sh,
# as it runs as /bin/sh: no path under HOME is left), ending with its
# bracket, and one state: line that holds the others as pid=stat (test.sh
# among them, none stopped) within 200 characters. A check that holds 20 s
# with nothing written to the terminal, against a 3 s bound, fails the case
# with the "timed out after 3 s" line (the wait ran to its bound), never the
# tostop one, and with no state: line.
c_ttymsg() {
	lit "$real" "trap '' TTOU" ':' "	trap '' TTOU #" > "$tmp/mut-ttymsg.sh" || { say "ttymsg: its mutant does not apply"; return 1; }
	_mo=$(HOME=/bin SCHED=$tmp/mut-ttymsg.sh quiet=0 c_tty) && { say "ttymsg: the run without the SIGTTOU ignore passed"; return 1; }
	_ms=$(printf '%s\n' "$_mo" | grep 'stopped on a terminal with tostop' || :)
	[ -n "$_ms" ] || { say "ttymsg: a real stop did not say tostop: $_mo"; return 1; }
	[ "$(printf '%s\n' "$_ms" | wc -l | tr -d ' ')" -eq 1 ] || { say "ttymsg: more than one stop line: $_mo"; return 1; }
	case $_ms in
	*"stopped: [pid="[0-9]*" stat=T"*" pgid="[0-9]*" comm="?*" args="?*) ;;
	*) say "ttymsg: the stop line does not name the stopped process (pid, stat, pgid, comm, args): $_ms"; return 1 ;;
	esac
	case $_ms in
	*"/bin/"* | *"/bin "* | *"/bin]"*) say "ttymsg: the stop line holds a path under HOME (/bin): $_ms"; return 1 ;;
	esac
	case $_ms in
	*" comm=~/sh args=~/sh "*) ;;
	*) say "ttymsg: the stopped process's comm and args are not ~/sh (the scheduler's shell, with HOME=/bin): $_ms"; return 1 ;;
	esac
	# it ends with the bracket of the stopped process (no other process's
	# pid=stat after it), and its args are at most 120 characters
	case $_ms in
	*"]") ;;
	*) say "ttymsg: the stop line holds more than the stopped process: $_ms"; return 1 ;;
	esac
	_ma=${_ms##* args=}
	[ "${#_ma}" -le 121 ] || { say "ttymsg: the args are not cut to 120 characters (${#_ma}): $_ms"; return 1; }
	case $_ms in
	*" stat=S"* | *" stat=R"*) say "ttymsg: a running process's state is in the stop line: $_ms"; return 1 ;;
	esac
	# the state: line: the others as pid=stat, none stopped, test.sh among
	# them, within its cut
	_mt=$(printf '%s\n' "$_mo" | grep '^check-gate-parallel: tty: state:' || :)
	[ "$(printf '%s\n' "$_mt" | grep -c .)" -eq 1 ] || { say "ttymsg: not one state: line below the stop line: $_mo"; return 1; }
	printf '%s\n' "$_mt" | grep -Eqx 'check-gate-parallel: tty: state:( [0-9]+=[^ T]+)+' ||
		{ say "ttymsg: the state: line does not hold the others as pid=stat, none stopped: $_mt"; return 1; }
	_my=$(printf '%s\n' "$_ms" | sed -n 's/.*(test\.sh pid \([0-9][0-9]*\)).*/\1/p')
	case "$_mt " in
	*" $_my="[!T]*) ;;
	*) say "ttymsg: test.sh (pid ${_my:-unknown}) is not on the state: line: $_mt"; return 1 ;;
	esac
	_mx=${_mt#check-gate-parallel: tty: state:}
	[ "${#_mx}" -le 200 ] || { say "ttymsg: the state: line is not cut to 200 characters (${#_mx}): $_mt"; return 1; }
	# a slow run
	_mo=$(TTY_BOUND=3 TTY_HOLD=20 quiet=0 c_tty) && { say "ttymsg: a run that outlived the bound passed"; return 1; }
	case $_mo in
	*"tty: timed out after 3 s (state:"*) ;;
	*) say "ttymsg: a slow run did not say it timed out after its 3 s bound: $_mo"; return 1 ;;
	esac
	case $_mo in
	*tostop*"ended by pid"*) say "ttymsg: a slow run was reported as stopped on tostop: $_mo"; return 1 ;;
	esac
	if printf '%s\n' "$_mo" | grep -q '^check-gate-parallel: tty: state:'; then
		say "ttymsg: a slow run printed a state: line: $_mo"
		return 1
	fi
}

# ttylate: a stop that comes late is caught, and ends the wait: check-a.sh
# holds 3 s, then writes to the terminal and stops (tostop). c_tty, with a
# 30 s bound, reports the stop, after the hold and before the bound.
c_ttylate() {
	_lt0=$(date +%s)
	_lo=$(TTY_BOUND=30 TTY_HOLD=3 TTY_MODE=holdtty quiet=0 c_tty) && { say "ttylate: a run whose check stopped passed"; return 1; }
	_ltw=$(($(date +%s) - _lt0))
	case $_lo in
	*"stopped on a terminal with tostop"*"stopped: [pid="*" stat=T"*) ;;
	*) say "ttylate: a stop after a hold was not reported as a stop: $_lo"; return 1 ;;
	esac
	[ "$_ltw" -ge 3 ] || { say "ttylate: the run ended in $_ltw s, inside check-a.sh's 3 s hold: $_lo"; return 1; }
	[ "$_ltw" -lt 30 ] || { say "ttylate: the stop ended the wait only at its 30 s bound ($_ltw s)"; return 1; }
}

# ttyfork: a line the shell writes in a child it has forked for a job, before
# that child's redirections and exec, goes to the shell's own standard
# error. The child is already in a process group of its own, so on a
# terminal set to tostop such a write stops it (SIGTTOU), and the run hangs.
# bash writes one under load when the child's setpgid loses its race with
# the parent's ("child setpgid (<pid> to <pid>): Operation not permitted",
# FIX176 slice 2); xtrace writes one every time (the child traces the
# command it runs), so it stands in for that race here:
# - test.sh run with sh -x (TTY_X) on the tty case's terminal passes: the
#   child that becomes the scheduler is not stopped by its own trace line;
# - the scheduler run with sh -x on one check writes no trace of the
#   check's command (the child's line) to its own output, while it writes
#   its other traces there (+ set -m)
# Every sh -x run here has PS4='+ ' in its own environment, so a PS4 this
# self-test is started with changes no trace line it looks for.
c_ttyfork() {
	_fo=$(TTY_X=1 quiet=0 c_tty) || { say "ttyfork: test.sh's start of the scheduler stopped on a tostop terminal: $_fo"; return 1; }
	d=$(newcase)
	spec "$d" a 1 0 0
	rc=0
	bounded 90 "$d/out" env GATE_JOBS=1 GP_EVENTS="$d/events" GP_SPEC="$d/spec" TMPDIR="$d/td" PS4='+ ' \
		sh -x "$SCHED" "" "" "$d/check-a.sh" || rc=$?
	[ "$rc" -eq 0 ] && grep -qx 'a line 1' "$d/out" || { say "ttyfork: the scheduler with sh -x gave status $rc"; return 1; }
	grep -qx '+ set -m' "$d/out" || { say "ttyfork: the scheduler with sh -x wrote no trace"; return 1; }
	if grep -qxF "+ $d/check-a.sh" "$d/out"; then
		say "ttyfork: the scheduler's child wrote to its standard error before its exec (+ $d/check-a.sh)"
		return 1
	fi
}

# nostderr: scripts/test.sh (or $WIRING) started with its standard error
# closed runs the scheduler, and the gate's status is the scheduler's, as
# with it open: outside the lock (BENCH_LOCK=0) and at the lock's re-entry
# (BENCH_LOCK_ENTRY=gate and a token; lock.sh's exec keeps a closed one).
# The scheduler is a fake that notes it ran and exits with GP_SCHED_STATUS,
# 0 (the makes follow) or 3 (they do not). Without test.sh's guard a write
# to that standard error ends test.sh (set -e), and the start's 3>&2 fails:
# $! is then unset (outside the lock) or the watcher's (under it, and
# test.sh waits on the watcher until the bound)
c_nostderr() {
	_tf=${WIRING:-$top/scripts/test.sh}
	for _ne in BENCH_LOCK=0 BENCH_LOCK_ENTRY=gate; do
		for _ns in 0 3; do
			d=$(newcase)
			gtree "$d" "$_tf" 1 0 0
			printf '%s\n' '#!/bin/sh' 'printf "sched %s\n" "$GATE_PARENT" >> "$GP_EVENTS"' 'exit "$GP_SCHED_STATUS"' > "$d/tree/scripts/gate-checks.sh"
			rc=0
			bounded 20 "$d/out" env "$_ne" BENCH_LOCK_TOKEN=t GP_SCHED_STATUS="$_ns" GP_EVENTS="$d/events" TMPDIR="$d/td" \
				PATH="$tmp/make:$PATH" sh -c 'exec 2>&-; exec sh "$1"' sh "$d/tree/scripts/test.sh" || rc=$?
			_nm=$(grep -c '^make ' "$d/events" || :)
			grep -q '^sched [0-9][0-9]*$' "$d/events" ||
				{ say "nostderr: $_ne: test.sh with its standard error closed did not run the scheduler (status $rc): $(tr '\n' ' ' < "$d/out")"; return 1; }
			[ "$rc" -eq "$_ns" ] || { say "nostderr: $_ne: the scheduler gave $_ns, test.sh $rc"; return 1; }
			if [ "$_ns" -eq 0 ]; then
				[ "$_nm" -eq 2 ] || { say "nostderr: $_ne: $_nm makes after the scheduler passed, not 2"; return 1; }
			else
				[ "$_nm" -eq 0 ] || { say "nostderr: $_ne: $_nm makes after the scheduler failed"; return 1; }
			fi
		done
	done
}

# watchfork: under the lock (BENCH_LOCK_ENTRY=gate and a token) test.sh, run
# with sh -x (PS4='+ '), writes no trace of the watcher's start: the child
# forked for it traces its command before its redirections and exec, and
# with test.sh's standard error there a tostop terminal would stop it, as
# in ttyfork. GATE_JOBS=x makes the scheduler fail at once, and its own line
# saying so reaches test.sh's standard error (the start's 2>&3)
c_watchfork() {
	_tf=${WIRING:-$top/scripts/test.sh}
	d=$(newcase)
	gtree "$d" "$_tf" 1 0 0
	rc=0
	bounded 60 "$d/out" env BENCH_LOCK_ENTRY=gate BENCH_LOCK_TOKEN=t GATE_JOBS=x GP_EVENTS="$d/events" GP_SPEC="$d/spec" \
		TMPDIR="$d/td" PATH="$tmp/make:$PATH" PS4='+ ' sh -x "$d/tree/scripts/test.sh" || rc=$?
	[ "$rc" -ne 0 ] && [ "$rc" -ne 124 ] || { say "watchfork: test.sh with GATE_JOBS=x gave status $rc"; return 1; }
	grep -qx '+ set -o monitor' "$d/out" || { say "watchfork: test.sh did not start the watcher, or wrote no trace"; return 1; }
	grep -qxF "gate: GATE_JOBS must be a positive whole number, not 'x'" "$d/out" ||
		{ say "watchfork: the scheduler's own standard error did not reach test.sh's"; return 1; }
	if grep -qF 'gate-watch' "$d/out"; then
		say "watchfork: the watcher's child wrote to test.sh's standard error before its exec: $(grep -F 'gate-watch' "$d/out" | head -n 1)"
		return 1
	fi
}

# tdesc: how c_tty prints a process. A /bin/sh started here and stopped,
# with backslashes and a newline in its args, is printed by tdesc through
# say on exactly one line, which ends with "]", its args as ps writes them
# (not decoded). tsort, on that shell, a running sleep and a pid that is
# gone: the shell alone on the stop list, the sleep alone on the others',
# the gone pid on no list. tcut keeps whole entries, as many as fit.
# The STOP is sent only once the shell has exec'd (its args are its own) and
# started its sleep (a child of its pid is there): a STOP sent while a
# just-forked child is still in its exec can be lost, and the shell then
# runs on, never stopped (FIX187). All three
# waits are on state, with a ceiling of 30 s that only a hang reaches; a
# shell that never shows T fails the case with that reason, before any
# line is read about it. The sleeps (120 s) outlast the three ceilings
# together (90 s), so the shell and the sleep are still there to be read,
# and killed, at the end; one the check leaves on a signal is killed by
# gp_exit.
c_tdesc() {
	d=$(newcase)
	# the argument is made before the fork: in "$(...)" among the words,
	# the forked child would run it as a child of its own before its exec,
	# and tkids would see that child (FIX187)
	_dnl=$(printf 'x\ny')
	/bin/sh -c 'sleep 120; :' sh 'a\cb' 'p\nq' "$_dnl" &
	_dp=$!
	_xdp=$_dp
	sleep 120 &
	_dl=$!
	_xdl=$_dl
	sh -c 'exit 0' &
	_dg=$!
	wait "$_dg" || :
	_dw=
	_dn=0
	until ps -o args= -p "$_dp" 2> /dev/null | grep -q '^/bin/sh -c sleep 120'; do
		if [ "$_dn" -ge 150 ]; then
			_dw="the shell never showed its own args (args=$(ps -o args= -p "$_dp" 2> /dev/null | tscrub || :))"
			break
		fi
		sleep 0.2
		_dn=$((_dn + 1))
	done
	_dn=0
	[ -n "$_dw" ] || until [ -n "$(tkids "$_dp")" ]; do
		if [ "$_dn" -ge 150 ]; then
			_dw="the shell never started its sleep (stat=$(ps -o stat= -p "$_dp" 2> /dev/null | tr -d ' ' || :))"
			break
		fi
		sleep 0.2
		_dn=$((_dn + 1))
	done
	if [ -z "$_dw" ]; then
		kill -s STOP "$_dp"
		_dn=0
		while :; do
			_dst=$(ps -o stat= -p "$_dp" 2> /dev/null | tr -d ' ' || :)
			case $_dst in *T*) break ;; esac
			if [ "$_dn" -ge 150 ]; then
				_dw="the shell never showed state T (stat=$_dst)"
				break
			fi
			sleep 0.2
			_dn=$((_dn + 1))
		done
	fi
	[ -n "$_dw" ] || tdesc_lines
	_dws=
	_dn=1
	while [ "$_dn" -le 60 ]; do
		_dws="$_dws $((10000 + _dn))=S"
		_dn=$((_dn + 1))
	done
	_dc=$(tcut 200 $_dws)
	[ "${#_dc}" -le 200 ] && [ "${#_dc}" -gt 192 ] || _dw=${_dw:-"tcut 200 gave ${#_dc} characters"}
	printf '%s\n' "$_dc" | grep -Eqx '( [0-9]+=S)+' || _dw=${_dw:-"tcut 200 did not keep whole entries: $_dc"}
	# what was started here: the sleep the stopped shell runs (by its
	# parent's pid), the shell, the other sleep
	for _dk in $(tkids "$_dp") "$_dp" "$_dl"; do
		kill -s KILL "$_dk" 2> /dev/null || :
	done
	wait "$_dp" 2> /dev/null || :
	wait "$_dl" 2> /dev/null || :
	_xdp=
	_xdl=
	[ -z "$_dw" ] || { say "tdesc: $_dw"; return 1; }
}
# tdesc_lines: c_tdesc's assertions on the stopped shell _dp (with the
# running sleep _dl and the gone pid _dg); the first that fails sets _dw
tdesc_lines() {
	(HOME=$d/nohome; export HOME; quiet=0; say "tty: stopped: $(tdesc "$_dp")") > "$d/line"
	_dr=$(HOME=$d/nohome; export HOME; tsort "$_dg" "$_dl" "$_dp"; printf '%s|%s|%s|%s\n' "$_tstop" "$_toth" "$_tstate" "$_tdesc")
	_dln=$(cat "$d/line")
	if [ "$(wc -l < "$d/line" | tr -d ' ')" != 1 ]; then
		_dw="the stop line is not one line: $_dln"
	else
		case $_dln in
		*"]") ;;
		*) _dw="the stop line does not end with ]: $_dln" ;;
		esac
	fi
	case $_dln in
	*"[pid=$_dp stat=T"*" pgid="[0-9]*" comm=/bin/sh args=/bin/sh -c sleep 120; : sh a"*) ;;
	*) _dw=${_dw:-"the stop line does not name the stopped shell: $_dln"} ;;
	esac
	grep -qF ' sh a\cb p\nq ' "$d/line" || _dw=${_dw:-"the args are not printed as ps writes them: $_dln"}
	case $_dr in
	"1| $_dl="*"| "*"| [pid=$_dp stat=T"*"]") ;;
	*) _dw=${_dw:-"tsort did not put the stopped shell alone in the stop list: $_dr"} ;;
	esac
	_dro=${_dr#*|}
	_dro=${_dro%%|*}
	printf '%s\n' "$_dro" | grep -Eqx " $_dl=[^ T]+" || _dw=${_dw:-"the others are not the running sleep alone: $_dr"}
	case "$_dr|" in
	*" $_dg="*) _dw=${_dw:-"a pid that is gone is on a list: $_dr"} ;;
	esac
}

# scrub: tscrub on fixed text, for each kind of HOME: the home path becomes ~
# wherever it occurs (after any character) unless a letter, a digit or _
# follows it; a HOME with regex characters is matched literally; trailing
# slashes in HOME are dropped; HOME / or empty or unset replaces nothing; a
# backslash in HOME is taken as it is; a match that overlaps a longer name does
# not hide the next one; lines are joined with a space; the result is cut to
# 120 characters; and no HOME survives that origin/main's tscrub replaced
# _scrow <home> <text> <want>: one row, also run through tscrub_main
_scrow() {
	_sg=$(HOME=$1; export HOME; printf '%s\n' "$2" | tscrub)
	_so=$(HOME=$1; export HOME; printf '%s\n' "$2" | tscrub_main)
	_sn=$(printf '%s\n' "$_sg" | _scount "$1")
	_sc=$(printf '%s\n' "$_so" | _scount "$1")
	[ "$_sn" -le "$_sc" ] || { say "scrub: HOME='$1' on '$2': tscrub leaves $_sn, origin/main's $_sc"; return 1; }
	[ "$_sg" = "$3" ] || { say "scrub: HOME='$1' on '$2' gave '$_sg', want '$3'"; return 1; }
}
# _scount <home>: the occurrences of that home on standard input, read as the
# scrubbers read it: from ENVIRON (awk -v would decode backslashes), trailing
# slashes dropped
_scount() {
	HOME=$1 awk '{ s = s $0 } END { h = ENVIRON["HOME"]; sub("/+$", "", h); n = 0; while (h != "" && (i = index(s, h)) > 0) { n++; s = substr(s, i + length(h)) } print n }'
}
c_scrub() {
	_tb=$(printf '\t')
	for _st in \
		'/u/qq|/u/qq/a.sh cd /u/qq x=/u/qq/b /u/qqzz/c /u/qq|~/a.sh cd ~ x=~/b /u/qqzz/c ~' \
		'/u/qq/|/u/qq/a.sh cd /u/qq /u/qqzz|~/a.sh cd ~ /u/qqzz' \
		'/u/qq//|/u/qq/a.sh|~/a.sh' \
		'/|/bin/sh / //x/ /|/bin/sh / //x/ /' \
		'|/u/qq/a cd /u/qq|/u/qq/a cd /u/qq' \
		'/u/a\tb|/u/a\tb/x /u/a	b/y|~/x /u/a	b/y' \
		'/ab/a|/ab/ab/a/x|/ab~/x' \
		'/a /a|x/a /a /a/x|x~ /a/x' \
		'/u/qq|/x/u/qq/a|/x~/a' \
		'/u/qq|//u/qq/a x//u/qq/b|/~/a x/~/b' \
		'/u/qq|a /u/qq/ a=/u/qq/ a:/u/qq/ "/u/qq/ '"'"'/u/qq/ (/u/qq/ ,/u/qq/|a ~/ a=~/ a:~/ "~/ '"'"'~/ (~/ ,~/' \
		'/u/qq|/u/qq/ /u/qq /u/qq: /u/qq; /u/qq" /u/qq'"'"' /u/qq, /u/qq)|~/ ~ ~: ~; ~" ~'"'"' ~, ~)' \
		'/u/qq|PATH=/u/qq:/usr/bin|PATH=~:/usr/bin' \
		'/u/qq|-/u/qq/a x/u/qq/a _/u/qq/a /u/qq-a /u/qq_a /u/qq.a /u/qq=a|-~/a x~/a _~/a ~-a /u/qq_a ~.a ~=a' \
		'/u/qq|/u/qqx /u/qq2 /u/qq_y /u/qqX|/u/qqx /u/qq2 /u/qq_y /u/qqX' \
		'/u/qq|-I/u/qq/inc -L/u/qq|-I~/inc -L~' \
		'/u/qq|a;/u/qq a>/u/qq a</u/qq a&&/u/qq|a;~ a>~ a<~ a&&~' \
		'/u/qq|a`/u/qq a$/u/qq file:///u/qq x@/u/qq [/u/qq {/u/qq|a`~ a$~ file://~ x@~ [~ {~' \
		'/u/qq|a'"$_tb"'/u/qq/b|a'"$_tb"'~/b' \
		'/u/qq|/System/Volumes/Data/u/qq/a|/System/Volumes/Data~/a' \
		'/u/qq|/u/qq] /u/qq} /u/qq> /u/qq& /u/qq. /u/qq- /u/qq= /u/qq? /u/qq'"$_tb"'z|~] ~} ~> ~& ~. ~- ~= ~? ~'"$_tb"'z' \
		'/u/q.q|/u/qxq/a /u/q.q/a|/u/qxq/a ~/a' \
		'/u/q*|/u/qqq/a /u/q*/a|/u/qqq/a ~/a' \
		'/u/q[a]|/u/qa/a /u/q[a]/a|/u/qa/a ~/a' \
		'/u/q+|/u/qq/a /u/q+/a|/u/qq/a ~/a'; do
		_sh=${_st%%|*}
		_si=${_st#*|}
		_si=${_si%|*}
		_sw=${_st##*|}
		_scrow "$_sh" "$_si" "$_sw" || return 1
	done
	# rows with a | in them cannot go through the table's separator
	_scrow /u/qq 'a|/u/qq a&&/u/qq /u/qq|' 'a|~ a&&~ ~|' || return 1
	_scrow /u/qq '/u/qq|x' '~|x' || return 1
	_sg=$(printf '%s\n' '/u/qq/a /u/qq' | _scount /u/qq/)
	[ "$_sg" = 2 ] || { say "scrub: _scount with a trailing-slash HOME gave '$_sg', want 2"; return 1; }
	_sg=$(printf '%s\n' '/u/a\tb' | _scount '/u/a\tb')
	[ "$_sg" = 1 ] || { say "scrub: _scount with a backslash HOME gave '$_sg', want 1"; return 1; }
	_sb=$(printf '\351')
	_sg=$(HOME=/u/qq; export HOME; printf '%s\n' "/u/qq${_sb}x" | LC_ALL=en_US.UTF-8 tscrub)
	[ "$_sg" = "~${_sb}x" ] || { say "scrub: a non-ASCII byte after the home, under LC_ALL=en_US.UTF-8, gave '$_sg'"; return 1; }
	_sg=$(unset HOME; printf '%s\n' /u/qq/a | tscrub)
	[ "$_sg" = /u/qq/a ] || { say "scrub: HOME unset gave '$_sg'"; return 1; }
	_sg=$(HOME=/u/qq; export HOME; printf '%s\n' a /u/qq/b | tscrub)
	[ "$_sg" = "a ~/b" ] || { say "scrub: two lines gave '$_sg', want 'a ~/b'"; return 1; }
	_sg=$(printf '%0200d\n' 0 | tscrub)
	[ "${#_sg}" -eq 120 ] || { say "scrub: 200 characters were cut to ${#_sg}, not 120"; return 1; }
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

# stop_one <signal> <status> [ignore] [intign]: one run of the stop case
# (see the top of this file) on a check-a.sh stand-in, GATE_JOBS=1. Once
# both stand-ins have written their pids, the scheduler gets <signal>
# ("none": the check's flag is removed instead; "INTTERM": SIGINT, then
# SIGTERM); it must end with <status> within 60 s, and both stand-ins must
# be gone within 2 s more. Then both flags go, so a stand-in a broken run
# left ends by itself.
stop_one() {
	_od=$(newcase)
	_or=$_od/rec
	mkdir "${_or:?}"
	: > "$_or/check.flag"
	: > "$_or/gc.flag"
	printf '%s\n' '#!/bin/sh' 'exec "$GP_STAND" check "$GP_REC" $GP_IGN' > "$_od/check-a.sh"
	chmod +x "$_od/check-a.sh"
	_ow=
	[ -z "${4:-}" ] || _ow='trap "" INT;'
	launch "$_od" env GATE_JOBS=1 GP_STAND="$tmp/stand" GP_REC="$_or" GP_IGN="${3:-}" TMPDIR="$_od/td" \
		sh -c "$_ow"' exec sh "$@"' sh "$SCHED" "" "" "$_od/check-a.sh"
	_obad=
	_on=0
	while [ ! -s "$_or/check.pid" ] || [ ! -s "$_or/gc.pid" ]; do
		if [ "$_on" -ge 300 ] || ! kill -0 "$_lp" 2> /dev/null; then
			_obad="the stand-ins did not start"
			break
		fi
		sleep 0.2
		_on=$((_on + 1))
	done
	_ot0=$(date +%s)
	if [ -z "$_obad" ]; then
		case $1 in
		none) rm -f "$_or/check.flag" ;;
		INTTERM)
			kill -s INT "$_lp" 2> /dev/null || :
			kill -s TERM "$_lp" 2> /dev/null || :
			;;
		*) kill -s "$1" "$_lp" 2> /dev/null || : ;;
		esac
	fi
	_ors=0
	lwait 60 || _ors=$?
	_ot=$(($(date +%s) - _ot0))
	_ocp=$(cat "$_or/check.pid" 2> /dev/null || :)
	_ogp=$(cat "$_or/gc.pid" 2> /dev/null || :)
	_on=0
	while [ "$_on" -lt 10 ] && { { [ -n "$_ocp" ] && ! dead "$_ocp"; } || { [ -n "$_ogp" ] && ! dead "$_ogp"; }; }; do
		sleep 0.2
		_on=$((_on + 1))
	done
	_oleft=
	[ -z "$_ocp" ] || dead "$_ocp" || _oleft="$_oleft check"
	[ -z "$_ogp" ] || dead "$_ogp" || _oleft="$_oleft grandchild"
	_ocs=$(tr '\n' ' ' < "$_or/check.sig" 2> /dev/null || :)
	_ogs=$(tr '\n' ' ' < "$_or/gc.sig" 2> /dev/null || :)
	rm -f "$_or/check.flag" "$_or/gc.flag"
	say "stop: ${1}${3:+ (both ignore SIGTERM)}${4:+ (SIGINT ignored at the start)}: status $_ors, check got: ${_ocs:-nothing}, grandchild got: ${_ogs:-nothing}, left:${_oleft:- none}, $_ot s"
	[ -z "$_obad" ] || { say "stop $1: $_obad"; return 1; }
	[ "$_ors" -eq "$2" ] || { say "stop $1: status $_ors, want $2"; return 1; }
	[ -z "$_oleft" ] || { say "stop $1: left running:$_oleft"; return 1; }
	if [ "$1" = none ]; then _owant='end '; else _owant='TERM '; fi
	[ "$_ocs" = "$_owant" ] || { say "stop $1: the check got '$_ocs', want '$_owant'"; return 1; }
	[ "$_ogs" = 'TERM ' ] || { say "stop $1: the grandchild got '$_ogs', want 'TERM '"; return 1; }
	if [ -n "${3:-}" ] && [ "$_ot" -lt 10 ]; then
		say "stop $1: the stand-ins that ignore SIGTERM were killed after $_ot s, before the 10 s grace"
		return 1
	fi
	return 0
}
c_stop() {
	_ol="TERM:143 INT:130 HUP:129 none:0"
	[ -n "$INT_OK" ] || { skipint "stop's SIGINT"; _ol="TERM:143 HUP:129 none:0"; }
	for _oc in $_ol; do
		stop_one "${_oc%:*}" "${_oc#*:}" || return 1
	done
	stop_one TERM 143 ignore || return 1
	stop_one INTTERM 143 "" intign
}

# intskip: interrupt, testsh and sigs, run by this self-test (or $SELF)
# alone (GP_ONLY), started with SIGINT ignored: each says what it skips,
# none fails, and the run passes
c_intskip() {
	_xf=${SELF:-$top/tests/check-gate-parallel.sh}
	d=$(newcase)
	rc=0
	bounded 240 "$d/out" env GP_ONLY="sigs testsh interrupt" TMPDIR="$d/td" \
		sh -c 'trap "" INT; exec sh "$1"' sh "$_xf" || rc=$?
	[ "$rc" -eq 0 ] || { say "intskip: the cases that need SIGINT, run with SIGINT ignored, gave status $rc:"; [ "$quiet" = 1 ] || sed 's/^/  | /' "$d/out"; return 1; }
	for _xp in "interrupt's SIGINT (SIGTERM is sent in its place)" "testsh's SIGINT" "sigs' assertion that SIGINT is delivered"; do
		grep -qxF "$me: SIGINT is ignored where this check runs, so $_xp is skipped" "$d/out" ||
			{ say "intskip: no line says that $_xp is skipped"; return 1; }
	done
	for _xc in sigs testsh interrupt; do
		grep -qx "  ok   $_xc" "$d/out" || { say "intskip: $_xc did not pass"; return 1; }
	done
}

# ttyself [<cases>]: those cases (tdesc scrub ttymsg ttylate by default),
# run alone by this self-test (or $SELF) in a process of its own, so a
# mutant of this self-test's tty lines (smut) is run and caught. With
# <want> (a mutant's own reason), a run that fails without <want> in its
# output passes this case, so that mutant counts as not caught: it is caught
# by that reason alone, never by any failure
c_ttyself() {
	_xf=${SELF:-$top/tests/check-gate-parallel.sh}
	_xo=${1:-tdesc scrub ttymsg ttylate}
	d=$(newcase)
	rc=0
	bounded 240 "$d/out" env GP_ONLY="$_xo" TMPDIR="$d/td" sh "$_xf" || rc=$?
	if [ "$rc" -ne 0 ] && [ -n "${2:-}" ] && ! grep -qF -- "$2" "$d/out"; then
		say "ttyself: $_xo failed (status $rc), but not with: $2"
		return 0
	fi
	[ "$rc" -eq 0 ] || { say "ttyself: $_xo run alone gave status $rc:"; [ "$quiet" = 1 ] || sed 's/^/  | /' "$d/out"; return 1; }
	for _xc in $_xo; do
		grep -qx "  ok   $_xc" "$d/out" || { say "ttyself: $_xc did not pass"; return 1; }
	done
}

# lit <file> <old> <new> [<prefix>]: the file with its one occurrence of
# <old> replaced by <new>, both taken literally (no backslash in either: awk
# -v reads escapes), lines that start with <prefix> left as they are;
# status 1 unless exactly one
lit() {
	awk -v o="$2" -v n="$3" -v k="${4:-}" '
		k != "" && index($0, k) == 1 { print; next }
		{ p = index($0, o); if (p) { c++; $0 = substr($0, 1, p - 1) n substr($0, p + length(o)) } print }
		END { exit c != 1 }' "$1"
}

cases="one_fail two_fail signal log order serial jobs1 first bound default times errors guard wiring interrupt interrupt1 testsh kgrp sigs tdesc scrub tty ttymsg ttylate ttyfork nostderr watchfork leave stop intskip ttyself"
[ -z "$only" ] || cases=$only
for c in $cases; do
	if "c_$c"; then echo "  ok   $c"; else echo "  FAIL $c"; fail=1; [ -z "$only" ] || break; fi
done
if [ -n "$only" ]; then
	[ "$fail" -eq 0 ] || { echo "$me: FAILED"; exit 1; }
	echo "$me: ok (cases $only alone, no mutants)"
	exit 0
fi

# mutant <name> <case> <old> <new> [<prefix>]: the scheduler with <old> replaced (on
# no line that starts with <prefix>) must fail <case>
mutants=0
# intmut <mutant|wmut> <name> <case> <old> <new>: a mutant that only SIGINT
# can catch; when SIGINT is ignored here it is skipped, and counted so
intskipped=0
intmut() {
	if [ -n "$INT_OK" ]; then
		"$@"
	else
		skipint "mutant $2"
		intskipped=$((intskipped + 1))
	fi
}
mutant() {
	lit "$real" "$3" "$4" "${5:-}" > "$tmp/mut-$1.sh" || { bad "mutant $1 does not apply"; return 0; }
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
mutant no-process-group interrupt '	set -m' '	:'
mutant grace-3s interrupt 'gate_grace=10' 'gate_grace=3'
mutant grace-3s-stop stop 'gate_grace=10' 'gate_grace=3'
mutant no-stop stop '	bench_stop_groups "$gate_grace" group "$@" || :' '	:'
mutant no-parent-check kgrp 'if [ -n "$_gparent" ] && ! gate_parent "$_gparent"; then' 'if false; then'
mutant no-end-at-exit leave '			gate_end "$_grpid"' '			:'
intmut mutant int-passes interrupt "trap 'exit 130' INT" "trap 'exit 0' INT"
mutant term-goes-on interrupt1 "trap 'exit 143' TERM" "trap ':' TERM"
mutant term-passes interrupt1 "trap 'exit 143' TERM" "trap 'exit 0' TERM"
mutant hup-passes interrupt1 "trap 'exit 129' HUP" "trap 'exit 0' HUP"

# mutants of the bounded-run primitive the scheduler sources (FIX186):
# cmutant <name> <case> <old> <new>, the scheduler as it is beside it
cmutant() {
	_cm=$tmp/cm-$1
	mkdir -p "${_cm:?}/bench/core"
	cp "$real" "$_cm/gate-checks.sh"
	lit "$top/scripts/bench/core/common.sh" "$3" "$4" > "$_cm/bench/core/common.sh" ||
		{ bad "mutant $1 does not apply to scripts/bench/core/common.sh"; return 0; }
	SCHED=$_cm/gate-checks.sh quiet=1
	if "c_$2"; then bad "mutant $1 was not caught by case $2"; else mutants=$((mutants + 1)); fi
	SCHED=$real quiet=0
}
cmutant group-kill-dropped interrupt 'kill -s "$1" -- "-$2" 2> /dev/null' 'kill -s "$1" -- "$2" 2> /dev/null'
cmutant no-kill-after-grace interrupt 'bench_signal_group KILL "$_sg_p" "$_sg_o" || _sg_r=$?' ':'
cmutant no-kill-after-grace-stop stop 'bench_signal_group KILL "$_sg_p" "$_sg_o" || _sg_r=$?' ':'

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
mutant ttou-to-checks sigs '	trap - TTOU' '	:'
# (the two re-ignores: the one in gate_start is told apart by its comment)
mutant ttou-after-start tty "trap '' TTOU # the scheduler again" ':'
mutant ttou-scheduler tty "trap '' TTOU" ':' "	trap '' TTOU #"
# (the shell's standard error left on the terminal for the fork of a check,
# and of the scheduler; the scheduler's own standard error lost with it)
mutant start-stderr-on-tty ttyfork '{ "$_gsc" < /dev/null > "$_gd/$1.log" 2>&1 & } 2> /dev/null' '"$_gsc" < /dev/null > "$_gd/$1.log" 2>&1 &'
wmut sched-stderr-on-tty ttyfork '{ 2>&3 3>&- GATE_PARENT=$$ scripts/gate-checks.sh "$gate_serial" "$gate_first" tests/check-*.sh & } 3>&2 2> /dev/null' 'GATE_PARENT=$$ scripts/gate-checks.sh "$gate_serial" "$gate_first" tests/check-*.sh &'
wmut sched-stderr-lost wiring '{ 2>&3 3>&- GATE_PARENT' '{ GATE_PARENT'
wmut sched-stderr-lost-run watchfork '{ 2>&3 3>&- GATE_PARENT' '{ GATE_PARENT'
# (the watcher's fork with test.sh's standard error; a closed standard error
# left closed, so the scheduler's start fails and $! is not its pid)
wmut watch-stderr-on-tty watchfork '{ sh -c "$gate_watch_sh" gate-watch "$gate_lock_shell" < /dev/null > /dev/null 2>&1 & } 2> /dev/null' 'sh -c "$gate_watch_sh" gate-watch "$gate_lock_shell" < /dev/null > /dev/null 2>&1 &'
wmut stderr-closed-unguarded nostderr '{ :; } 3>&2 2> /dev/null || exec 2> /dev/null' ':'
# (without its own group the scheduler, and every check, ignore SIGINT and
# SIGQUIT; when this self-test already ignores both, nothing differs)
intmut wmut no-own-group sigs 'set -m' ':'
wmut no-parent-pid kgrp 'GATE_PARENT=$$ scripts/gate-checks.sh' 'scripts/gate-checks.sh'

# mutants of this self-test, run from a copy of the scripts it tests:
# smut <name> <case> <old> <new> [<arg>] (its own smut lines are left as
# they are); <arg>, when given, is passed to the case, and so is <want>
# (smut <name> <case> <old> <new> <arg> <want>: for ttyself, the reason the
# mutant must fail with). The arguments after <new> are passed as they
# are, by position: an empty <arg> stays the case's first argument
smut() {
	_sm=$tmp/self-$1
	_smn=$1
	_smc=$2
	mkdir "${_sm:?}" "${_sm:?}/tests" "${_sm:?}/scripts"
	cp "$top/scripts/test.sh" "$top/scripts/gate-checks.sh" "$_sm/scripts/"
	mkdir -p "${_sm:?}/scripts/bench/core"
	cp "$top/scripts/bench/core/common.sh" "$_sm/scripts/bench/core/common.sh"
	lit "$top/tests/check-gate-parallel.sh" "$3" "$4" "smut " > "$_sm/tests/check-gate-parallel.sh" ||
		{ bad "mutant $1 does not apply to this self-test"; return 0; }
	shift 4
	quiet=1 SELF=$_sm/tests/check-gate-parallel.sh
	if "c_$_smc" "$@"; then bad "mutant $_smn was not caught by case $_smc${1:+ ($1)}"; else mutants=$((mutants + 1)); fi
	quiet=0 SELF=
}
smut no-int-probe intskip "sh -c 'kill -INT \$\$; exit 0' 2> /dev/null || INT_OK=1" 'INT_OK=1'
smut skip-unsaid intskip 'skipint() { say' 'skipint() { :'
# (a fifth argument: the cases ttyself runs)
smut say-echo ttyself 'say() { [ "$quiet" = 1 ] || printf' 'say() { [ "$quiet" = 1 ] || echo "$me: $*" || printf' tdesc
smut tty-no-pgid ttyself ' pgid=${2:-}' '' ttymsg
smut tty-no-comm ttyself ' comm=$_tdc' '' ttymsg
smut tty-no-args ttyself ' args=$_tda]' ']' ttymsg
smut tty-no-stat ttyself ' stat=${1:-} pgid' ' pgid' ttymsg
smut tty-read-after-kill ttyself '_tdv=$(ps -o stat= -o pgid= -p "$_tdp"' '_tdv=$(kill -s KILL "$_tdp" 2> /dev/null; sleep 0.3; ps -o stat= -o pgid= -p "$_tdp"' ttymsg
smut tty-others-in-stop ttyself 'stopped:$_tdesc"' 'stopped:$_tdesc$_toth"' ttymsg
smut tty-args-home ttyself '_tda=$(ps -o args= -p "$_tdp" 2> /dev/null | tscrub)' '_tda=$(ps -o args= -p "$_tdp" 2> /dev/null)' ttymsg
smut tty-comm-home ttyself '_tdc=$(ps -o comm= -p "$_tdp" 2> /dev/null | tscrub)' '_tdc=$(ps -o comm= -p "$_tdp" 2> /dev/null)' ttymsg
smut tty-home-trailing ttyself 'while (substr(h, length(h)) == "/")' 'while (0)' scrub
smut tty-home-boundary ttyself 'if (c !~ /[A-Za-z0-9_]/) {' 'if (1) {' scrub
smut tty-home-before-cond ttyself 'if (c !~ /[A-Za-z0-9_]/) {' 'if (c !~ /[A-Za-z0-9_]/ && (i == 1 || substr(s, i - 1, 1) != "/")) {' scrub
smut tty-home-dot-name ttyself '/[A-Za-z0-9_]/' '/[A-Za-z0-9_.]/' scrub
smut tty-home-dash-name ttyself '/[A-Za-z0-9_]/' '/[A-Za-z0-9_-]/' scrub
smut tty-home-underscore-boundary ttyself '/[A-Za-z0-9_]/' '/[A-Za-z0-9]/' scrub
smut tty-home-overlap ttyself 'o = o substr(s, 1, i); s = substr(s, i + 1); }' 'o = o substr(s, 1, i - 1 + length(h)); s = substr(s, i + length(h)); }' scrub
smut tty-count-awk-v ttyself 'HOME=$1 awk '"'"'{ s = s $0 } END { h = ENVIRON["HOME"];' 'awk -v h="$1" '"'"'{ s = s $0 } END {' scrub
smut tty-count-no-trim ttyself 'sub("/+$", "", h); n = 0;' 'n = 0;' scrub
smut tty-tscrub-locale-unpinned ttyself 'LC_ALL=C awk' 'awk' scrub
smut tty-home-unscrubbed ttyself 'if (c !~ /[A-Za-z0-9_]/) {' 'if (0) {' scrub
smut tty-no-cut ttyself 'print substr(o s, 1, 120)' 'print o s' scrub
smut tty-no-state-line ttyself '			say "tty: state:$(tcut 200 $_toth)"' ':' ttymsg
smut tty-state-empty ttyself 'say "tty: state:$(tcut 200 $_toth)"' 'say "tty: state:"' ttymsg
smut tty-state-dropped ttyself '*) _toth="$_toth $_tk=$_tst" ;;' '*) ;;' ttymsg
smut tty-state-uncut ttyself '-le "$_tcw" ] || break' '-le "$_tcw" ] || :' tdesc
smut tty-state-on-slow ttyself '		if [ -n "$_tstop" ]; then' '		say "tty: state:$(tcut 200 $_toth)"; if [ -n "$_tstop" ]; then' ttymsg
smut tty-gone-kept ttyself '[ -n "$_tst" ] || continue' ':' tdesc
# (a SIGCONT right after the STOP: the shell is never in T, and the case
# fails with that reason, never falls through to the stop line)
smut tdesc-cont ttyself 'kill -s STOP "$_dp"' 'kill -s STOP "$_dp"; kill -s CONT "$_dp"' tdesc 'tdesc: the shell never showed state T (stat='
smut tty-no-early-stop ttyself 'lwait "$_tb" keep tstopped "$d"' 'lwait "$_tb" keep' ttylate
smut tty-probe-always ttyself '$2 ~ /T/ { f = 1 }' '{ f = 1 }' ttymsg
smut launch-no-group intskip '{ "$@" > "$_ld/out"' '{ set +m; "$@" > "$_ld/out"'

if [ "$fail" -ne 0 ]; then
	echo "$me: FAILED"
	exit 1
fi
if [ "$intskipped" -eq 0 ]; then
	echo "$me: ok ($(echo $cases | wc -w | tr -d ' ') cases, $mutants mutants caught)"
else
	echo "$me: ok ($(echo $cases | wc -w | tr -d ' ') cases, $mutants mutants caught, $intskipped skipped as SIGINT is ignored here)"
fi
