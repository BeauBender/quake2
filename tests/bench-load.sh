#!/bin/sh
# tests/bench-load.sh: runs the bench self-test (tests/check-bench.sh) under
# a CPU load, to show that its cases pass or fail on the bench's behaviour,
# not on how long a loaded host takes (FIX126). It is not part of the gate
# (scripts/test.sh runs tests/check-*.sh only): it loads every core for as
# long as it runs, so run it through the host's lock.
#
# Usage: tests/bench-load.sh [-n RUNS] [-k PER_CORE] [-p NICE] [-- COMMAND [ARGS...]]
#   -n RUNS      runs of the command (default 3)
#   -k PER_CORE  busy loops per core (default 1)
#   -p NICE      runs the command at that niceness (default 0); a busy loop
#                outlasts the scheduler's boost for short processes, so on
#                macOS busy loops alone barely slow the self-test, and -p 20
#                gives it the share a starved host would
#   COMMAND      what each run runs (default: sh tests/check-bench.sh); for
#                example `-- sh tests/check-bench.sh t_settle`
#   --selftest   shows that no busy loop outlives the harness (below)
# Each run's output goes to $BENCH_LOAD_LOG/run.<n> (a directory, kept) or to a
# scratch directory removed at exit.
#
# Each busy loop is a shell that spins while the harness's pid is alive
# (`while kill -0 <harness>`, with a round of arithmetic between two
# checks), so none outlives the harness
# however it ends, SIGKILL included. Its pid is recorded at start and
# written to $BENCH_LOAD_PIDS when that is set ($BENCH_LOAD_PIDS.start gets
# each pid as it is recorded). The EXIT trap kills those pids and no other,
# never by name or pattern, and waits for them: the fast path. INT, TERM
# and HUP exit through it. Around each fork and the record of its pid the
# signal traps only set a flag, checked once the pid is recorded, so a
# signal in that window cannot leave a process unrecorded. Each run is
# started in the background with INT set to the default (a background job
# of a non-interactive shell starts with it ignored, and the bench's
# Ctrl-C cases need it) and waited for, so a signal is taken at once, not
# after the run, and the trap also kills that run's pid.
#
# It prints one line per run and a summary, and exits 0 only when every run
# passed.
set -u
me=bench-load
case "$0" in */*) here=${0%/*} ;; *) here=. ;; esac
root=$(cd "$here/.." && pwd) || exit 1

if [ "${1:-}" = --selftest ]; then
	# A busy loop left over after a stop signal, a SIGKILL or a failed run
	# cannot outlive the harness. Each case starts the harness with a
	# command it controls, reads the busy loops' pids from BENCH_LOAD_PIDS,
	# ends the harness (the signal, or the command's own failure) and
	# probes every pid with kill -0. A background job of a non-interactive
	# shell starts with INT ignored, and an ignored signal cannot be
	# trapped, so the harness is started through perl with INT and TERM set
	# to the default first. Whether INT then reaches a command is probed as
	# tests/check-bench.sh does: when it does, a harness that ignores INT
	# fails the ctrl-c case, and only when it does not is that case skipped.
	# Two mutants of the harness must fail: a plain `yes` busy loop (the
	# sigkill case) and an INT trap of '' (the ctrl-c case).
	st=$(mktemp -d "${TMPDIR:-/tmp}/bench-load-selftest.XXXXXX") || exit 1
	trap 'rm -rf "${st:?}"' EXIT
	fails=0
	h_sh=$0
	nap() { perl -e 'select undef, undef, undef, $ARGV[0]' "$1"; }
	# The harness is started by perl itself, never through a shell function:
	# a function in the background is a subshell, and $! would name it.
	dfl='$SIG{INT} = $SIG{TERM} = "DEFAULT"; exec @ARGV or exit 127'
	INT_OK=
	perl -e "$dfl" sh -c 'kill -INT $$; exit 0' 2> /dev/null || INT_OK=1
	# gone <seconds> <pids...>: waits until no pid is alive, prints the live ones.
	gone() {
		_g=$(($1 * 20)); shift
		while :; do
			_left=
			for p in "$@"; do kill -0 "$p" 2> /dev/null && _left="$_left $p"; done
			[ -n "$_left" ] && [ "$_g" -gt 0 ] || break
			nap 0.05; _g=$((_g - 1))
		done
		echo $_left
	}
	one() {
		# one <name> <INT|TERM|HUP|KILL|fail|start>
		rm -f "${st:?}/pids" "${st:?}/pids.start" "${st:?}/out"
		pf=$st/pids
		case "$2" in
		fail) BENCH_LOAD_PIDS=$st/pids perl -e "$dfl" sh "$h_sh" -n 2 -k 1 -- sh -c 'sleep 1; exit 3' > "${st:?}/out" 2>&1 & ;;
		start) pf=$st/pids.start
			BENCH_LOAD_PIDS=$st/pids BENCH_LOAD_START_NAP=0.3 perl -e "$dfl" sh "$h_sh" -n 1 -k 3 -- perl -e 'select undef, undef, undef, 120' > "${st:?}/out" 2>&1 & ;;
		*) BENCH_LOAD_PIDS=$st/pids perl -e "$dfl" sh "$h_sh" -n 1 -k 1 -- perl -e 'select undef, undef, undef, 120' > "${st:?}/out" 2>&1 & ;;
		esac
		h=$!
		i=0
		while [ ! -s "${pf:?}" ] && [ "$i" -lt 200 ]; do nap 0.05; i=$((i + 1)); done
		pids=$(cat "${pf:?}" 2> /dev/null)
		if [ -z "$pids" ]; then
			echo "$me: selftest $1: the harness wrote no pids in 10 s"; fails=1
			kill "$h" 2> /dev/null; wait "$h" 2> /dev/null
			return
		fi
		for p in $pids; do
			case "$(ps -o command= -p "$p" 2> /dev/null)" in
			yes|*'kill -0'*" $h") ;;
			*) echo "$me: selftest $1: pid $p is not a busy loop of the harness (pid $h)"; fails=1 ;;
			esac
		done
		case "$2" in
		fail) ;;
		start) kill -s TERM "$h" ;;
		*) kill -s "$2" "$h" ;;
		esac
		i=0
		while kill -0 "$h" 2> /dev/null && [ "$i" -lt 600 ]; do nap 0.05; i=$((i + 1)); done
		if kill -0 "$h" 2> /dev/null; then
			if [ "$2" = INT ] && [ -z "$INT_OK" ]; then
				echo "$me: selftest $1: skipped, INT is ignored here"
			else
				echo "$me: selftest $1: the harness did not exit in 30 s"; fails=1
			fi
			kill -s TERM "$h" 2> /dev/null
		fi
		wait "$h"; hs=$?
		n_st=$(wc -l < "${st:?}/pids.start" 2> /dev/null | tr -d ' ')
		if [ "$2" = start ]; then
			# Every busy loop, recorded or not: one forked before its
			# pid was recorded is found by its command line, which
			# ends in the harness's pid.
			[ ! -s "${st:?}/pids" ] || { echo "$me: selftest $1: the signal came after the start loop"; fails=1; }
			pids="$pids $(ps -o pid=,command= -U "$(id -u)" | awk -v h="$h" '/kill -0/ && $NF == h { print $1 }')"
		fi
		left=$(gone 5 $pids)
		if [ -n "$left" ]; then
			echo "$me: selftest $1: busy loops outlived the harness: $left"; fails=1
			for p in $left; do kill "$p" 2> /dev/null; done
		elif [ "$2" = start ]; then
			echo "$me: selftest $1: ok (harness exit $hs, the signal after $n_st busy loops started, none left)"
		else
			echo "$me: selftest $1: ok (harness exit $hs, $(echo $pids | wc -w | tr -d ' ') busy loops gone)"
		fi
	}
	# mutant <name> <old> <new> <case> <how> <message>: the harness with
	# <old> replaced by <new> must fail that case with that message.
	mutant() {
		awk -v o="$2" -v n="$3" '{ p = index($0, o); if (p) { $0 = substr($0, 1, p - 1) n substr($0, p + length(o)); c++ } print } END { exit !c }' "$0" > "${st:?}/mutant.sh" ||
			{ echo "$me: selftest mutant $1 does not apply"; fails=1; return; }
		_f=$fails; fails=0; h_sh=$st/mutant.sh
		one "$4" "$5" > "${st:?}/mutant.out"
		_mf=$fails; fails=$_f; h_sh=$0
		if [ "$_mf" -ne 0 ] && grep -q "$6" "${st:?}/mutant.out"; then
			echo "$me: selftest mutant $1: caught by $4 ($(grep "$6" "${st:?}/mutant.out" | sed -n 1p | sed "s/^$me: selftest //"))"
		else
			echo "$me: selftest mutant $1: not caught by $4: $(tr '\n' '|' < "${st:?}/mutant.out")"; fails=1
		fi
	}
	one ctrl-c INT
	one term TERM
	one hup HUP
	one sigkill KILL
	one start-loop start
	one failed-run fail
	grep -q 'run 1 of 2: fail (exit 3' "${st:?}/out" || { echo "$me: selftest failed-run: no failing run line: $(tr '\n' '|' < "${st:?}/out")"; fails=1; }
	# The measured command gets INT: check-bench's own probe, run as the
	# command, dies of it, so the run fails with 130.
	perl -e "$dfl" sh "$0" -n 1 -k 0 -- sh -c 'kill -INT $$; exit 0' > "${st:?}/out" 2>&1
	if grep -q 'run 1 of 1: fail (exit 130' "${st:?}/out"; then
		echo "$me: selftest run-int: ok (the measured command got INT)"
	elif [ -z "$INT_OK" ]; then
		echo "$me: selftest run-int: skipped, INT is ignored here"
	else
		echo "$me: selftest run-int: the measured command has INT ignored: $(tr '\n' '|' < "${st:?}/out")"; fails=1
	fi
	mutant plain-yes 'sh -c "$spin" sh "$$"' 'yes' sigkill KILL 'outlived the harness'
	if [ -n "$INT_OK" ]; then
		mutant int-ignored "trap 'exit 130' INT" "trap '' INT" ctrl-c INT 'did not exit in 30 s'
	else
		echo "$me: selftest mutant int-ignored: skipped, INT is ignored here"
	fi
	[ "$fails" -eq 0 ] && echo "$me: selftest ok" || echo "$me: selftest failed"
	exit "$fails"
fi

runs=3
per=1
prio=0
usage() {
	echo "usage: $me [-n RUNS] [-k PER_CORE] [-p NICE] [-- COMMAND [ARGS...]] | --selftest" >&2
	[ -z "${1:-}" ] || echo "$me: $1" >&2
	exit 2
}
digits() { case "$1" in ''|*[!0-9]*) return 1 ;; esac; }
while [ "$#" -gt 0 ]; do
	case "$1" in
	-n|-k|-p) [ "$#" -ge 2 ] || usage "$1 takes a value" ;;
	esac
	case "$1" in
	-n) runs=$2; shift 2 ;;
	-k) per=$2; shift 2 ;;
	-p) prio=$2; shift 2 ;;
	--) shift; break ;;
	*) usage ;;
	esac
done
digits "$runs" && [ "$runs" -ge 1 ] || usage "-n takes a whole number of at least 1"
digits "$per" || usage "-k takes a whole number"
digits "$prio" || usage "-p takes a whole number"
[ "$#" -gt 0 ] || set -- sh "$root/tests/check-bench.sh"

cores=$(sysctl -n hw.ncpu 2> /dev/null || getconf _NPROCESSORS_ONLN 2> /dev/null || echo 1)
burners=
run=
stop=
logs=${BENCH_LOAD_LOG:-}
own=
cleanup() {
	for p in $run $burners; do kill "$p" 2> /dev/null; done
	for p in $run $burners; do wait "$p" 2> /dev/null; done
	[ -z "$own" ] || rm -rf "${own:?}"
}
# exit_traps: a stop signal exits through the EXIT trap. flag_traps: it
# only sets $stop, for the window between a fork and the record of its pid.
exit_traps() {
	trap 'exit 130' INT
	trap 'exit 143' TERM
	trap 'exit 129' HUP
}
flag_traps() {
	trap 'stop=130' INT
	trap 'stop=143' TERM
	trap 'stop=129' HUP
}
trap cleanup EXIT
exit_traps
if [ -z "$logs" ]; then
	own=$(mktemp -d "${TMPDIR:-/tmp}/bench-load.XXXXXX") || exit 1
	logs=$own
fi
mkdir -p "${logs:?}" || exit 1

# A busy loop: spins while the harness's pid ($1) is alive. The kill -0
# comes once per 10000 rounds of arithmetic: 24 loops that called it back
# to back (`do :; done`) left a 12-core host at a load average near 4, not
# 24 (FIX126). The rounds take well under a second, so a loop still ends
# within one once the harness is gone.
spin='while kill -0 "$1" 2> /dev/null; do i=0; while [ "$i" -lt 10000 ]; do i=$((i + 1)); done; done'
[ -z "${BENCH_LOAD_PIDS:-}" ] || : > "$BENCH_LOAD_PIDS.start"
i=0
while [ "$i" -lt $((cores * per)) ]; do
	flag_traps
	sh -c "$spin" sh "$$" < /dev/null > /dev/null 2>&1 &
	burners="$burners $!"
	exit_traps
	[ -z "$stop" ] || exit "$stop"
	[ -z "${BENCH_LOAD_PIDS:-}" ] || echo "${burners##* }" >> "$BENCH_LOAD_PIDS.start"
	# Only for the self-test: a pause between two busy loops, so that a
	# signal can be sent while they start.
	[ -z "${BENCH_LOAD_START_NAP:-}" ] || perl -e 'select undef, undef, undef, $ARGV[0]' "$BENCH_LOAD_START_NAP"
	i=$((i + 1))
done
[ -z "${BENCH_LOAD_PIDS:-}" ] || echo $burners > "$BENCH_LOAD_PIDS"
load() { sysctl -n vm.loadavg 2> /dev/null | awk '{ print $2 }'; }
echo "$me: $((cores * per)) busy loops on $cores cores, $runs runs at nice $prio of: $*"

passed=0
streak=0
best=0
n=1
while [ "$n" -le "$runs" ]; do
	t0=$(date +%s)
	flag_traps
	nice -n "$prio" perl -e '$SIG{INT} = "DEFAULT"; exec @ARGV or exit 127' "$@" > "${logs:?}/run.$n" 2>&1 < /dev/null &
	run=$!
	exit_traps
	[ -z "$stop" ] || exit "$stop"
	s=0
	wait "$run" || s=$?
	run=
	t=$(($(date +%s) - t0))
	if [ "$s" -eq 0 ]; then
		passed=$((passed + 1)); streak=$((streak + 1))
		echo "$me: run $n of $runs: ok ($t s, load $(load))"
	else
		streak=0
		echo "$me: run $n of $runs: fail (exit $s, $t s, load $(load)): $(tail -n 3 "${logs:?}/run.$n" | tr '\n' '|')"
	fi
	[ "$streak" -le "$best" ] || best=$streak
	n=$((n + 1))
done
echo "$me: passed $passed of $runs, $best in a row, $((cores * per)) busy loops on $cores cores"
[ "$passed" -eq "$runs" ]
