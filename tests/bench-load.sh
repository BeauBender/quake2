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
# Each busy loop is `yes > /dev/null`, started here, its pid recorded at
# start and written to $BENCH_LOAD_PIDS when that is set. The EXIT trap
# kills those pids and no other, never by name or pattern, and waits for
# them; INT, TERM and HUP exit through it. Each run is started in the
# background and waited for, so a signal is taken at once, not after the
# run, and the trap also kills that run's pid.
#
# It prints one line per run and a summary, and exits 0 only when every run
# passed.
set -u
me=bench-load
case "$0" in */*) here=${0%/*} ;; *) here=. ;; esac
root=$(cd "$here/.." && pwd) || exit 1

if [ "${1:-}" = --selftest ]; then
	# A busy loop left over after a stop signal or a failed run cannot
	# outlive the harness. Each case starts the harness with a command it
	# controls, reads the busy loops' pids from BENCH_LOAD_PIDS, ends the
	# harness (the signal, or the command's own failure) and probes every
	# pid with kill -0. A background job of a non-interactive shell starts
	# with INT ignored, and an ignored signal cannot be trapped, so the
	# harness is started through perl with INT and TERM set to the default
	# first; when INT is still ignored there, that case is skipped and says so.
	st=$(mktemp -d "${TMPDIR:-/tmp}/bench-load-selftest.XXXXXX") || exit 1
	trap 'rm -rf "${st:?}"' EXIT
	fails=0
	nap() { perl -e 'select undef, undef, undef, $ARGV[0]' "$1"; }
	one() {
		# one <name> <signal|fail>
		rm -f "${st:?}/pids" "${st:?}/out"
		if [ "$2" = fail ]; then
			BENCH_LOAD_PIDS=$st/pids perl -e '$SIG{INT} = $SIG{TERM} = "DEFAULT"; exec @ARGV' \
				sh "$0" -n 2 -k 1 -- sh -c 'sleep 1; exit 3' > "${st:?}/out" 2>&1 &
		else
			BENCH_LOAD_PIDS=$st/pids perl -e '$SIG{INT} = $SIG{TERM} = "DEFAULT"; exec @ARGV' \
				sh "$0" -n 1 -k 1 -- perl -e 'select undef, undef, undef, 120' > "${st:?}/out" 2>&1 &
		fi
		h=$!
		i=0
		while [ ! -s "${st:?}/pids" ] && [ "$i" -lt 200 ]; do nap 0.05; i=$((i + 1)); done
		pids=$(cat "${st:?}/pids" 2> /dev/null)
		if [ -z "$pids" ]; then
			echo "$me: selftest $1: the harness wrote no pids in 10 s"; fails=1
			kill "$h" 2> /dev/null; wait "$h" 2> /dev/null
			return
		fi
		for p in $pids; do
			[ "$(ps -o comm= -p "$p" 2> /dev/null)" = yes ] || { echo "$me: selftest $1: pid $p is not a yes busy loop"; fails=1; }
		done
		[ "$2" = fail ] || kill -s "$2" "$h"
		i=0
		while kill -0 "$h" 2> /dev/null && [ "$i" -lt 600 ]; do nap 0.05; i=$((i + 1)); done
		if kill -0 "$h" 2> /dev/null; then
			if [ "$2" = INT ]; then
				echo "$me: selftest $1: skipped, INT is ignored here"
			else
				echo "$me: selftest $1: the harness did not exit in 30 s"; fails=1
			fi
			kill -s TERM "$h" 2> /dev/null
		fi
		wait "$h"; hs=$?
		left=
		for p in $pids; do kill -0 "$p" 2> /dev/null && left="$left $p"; done
		if [ -n "$left" ]; then
			echo "$me: selftest $1: busy loops outlived the harness:$left"; fails=1
			for p in $left; do kill "$p" 2> /dev/null; done
		else
			echo "$me: selftest $1: ok (harness exit $hs, $(echo $pids | wc -w | tr -d ' ') busy loops gone)"
		fi
	}
	one ctrl-c INT
	one term TERM
	one hup HUP
	one failed-run fail
	grep -q 'run 1 of 2: fail (exit 3' "${st:?}/out" || { echo "$me: selftest failed-run: no failing run line: $(tr '\n' '|' < "${st:?}/out")"; fails=1; }
	[ "$fails" -eq 0 ] && echo "$me: selftest ok" || echo "$me: selftest failed"
	exit "$fails"
fi

runs=3
per=1
prio=0
while [ "$#" -gt 0 ]; do
	case "$1" in
	-n) runs=${2:-}; shift 2 || exit 2 ;;
	-k) per=${2:-}; shift 2 || exit 2 ;;
	-p) prio=${2:-}; shift 2 || exit 2 ;;
	--) shift; break ;;
	*) echo "usage: $me [-n RUNS] [-k PER_CORE] [-p NICE] [-- COMMAND [ARGS...]] | --selftest" >&2; exit 2 ;;
	esac
done
case "$runs$per$prio" in *[!0-9]*|'') echo "$me: -n, -k and -p take a whole number" >&2; exit 2 ;; esac
[ "$#" -gt 0 ] || set -- sh "$root/tests/check-bench.sh"

cores=$(sysctl -n hw.ncpu 2> /dev/null || getconf _NPROCESSORS_ONLN 2> /dev/null || echo 1)
burners=
run=
logs=${BENCH_LOAD_LOG:-}
own=
cleanup() {
	for p in $run $burners; do kill "$p" 2> /dev/null; done
	for p in $run $burners; do wait "$p" 2> /dev/null; done
	[ -z "$own" ] || rm -rf "${own:?}"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
if [ -z "$logs" ]; then
	own=$(mktemp -d "${TMPDIR:-/tmp}/bench-load.XXXXXX") || exit 1
	logs=$own
fi
mkdir -p "${logs:?}" || exit 1

i=0
while [ "$i" -lt $((cores * per)) ]; do
	yes > /dev/null &
	burners="$burners $!"
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
	nice -n "$prio" "$@" > "${logs:?}/run.$n" 2>&1 < /dev/null &
	run=$!
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
