#!/bin/sh
# The gate's check scheduler (FIX72), run by scripts/test.sh on
# tests/check-*.sh; tests/check-gate-parallel.sh runs it on fake checks.
#
# Usage: scripts/gate-checks.sh <serial> <first> <check>...
#
# Runs every <check> (an executable file), GATE_JOBS of them at a time, and
# exits 0 only when every one of them exited 0. GATE_JOBS defaults to half
# the CPUs (sysctl -n hw.ncpu, else nproc), at least 1; unreadable, it is 1.
# A check whose file name (without its directory) is a word of <serial>
# shares a fixed path with another check: it runs alone, after every other
# check has ended. The checks named in <first> (the long ones) start before
# the others, in that list's order; that changes when a check starts, never
# where its log is printed. With GATE_JOBS=1 both lists are ignored: every
# check runs alone, in the order given, which is how the gate ran them
# before FIX72. A word of either list that names no check is an error.
#
# Each check's standard output and error go to a log of its own, which is
# printed whole, in the order the checks were given, once that check and
# every check before it have ended. After each log one line gives the
# check's status and wall time, and the last line the total:
#     gate: check-a.sh: ok, 4 s
#     gate: check-b.sh: FAILED (status 1), 2 s
#     gate: checks: 1 of 2 FAILED in 5 s, 3 at a time: check-b.sh
#
# Fails closed: a check that exits non-zero or dies from a signal, a log that
# cannot be printed, and an error of the scheduler itself (a bad GATE_JOBS, a
# list word that names no check, no scratch directory) fail the run, and every
# check that failed is named. Nothing here limits how long a check may run.
#
# A SIGHUP, SIGINT or SIGTERM to this script ends the run at once: no other
# check starts, every running one and all it started get SIGTERM, and SIGKILL
# 10 s later (see gate_end), and the exit status is 129, 130 or 143. When
# GATE_PARENT is set (scripts/test.sh sets it to its own pid), the run ends
# the same way, with status 1, within one poll of this script's parent no
# longer being that process: it died, by any signal, SIGKILL included.
# GATE_PARENT is not passed on to the checks. When a check ends, what it
# left running is ended the same way before the check is counted done.
#
# Every check runs under a process supervisor of its own (lib/supervise.c in
# this script's directory, TASK218; TASK221): the supervisor starts it as the
# leader of a new process group, with every signal at its default, and owns
# its reap. When the check exits and left members behind, and when this
# script sends the supervisor SIGTERM, the supervisor sends the group TERM
# and CONT, KILL after gate_grace s, and exits once the group is empty. So a
# stop that has begun ends even when this script is killed during it. A
# group that cannot be emptied makes the supervisor exit 126, and the check
# fails. Without the supervisor, without bench/core/common.sh (its liveness
# test), or with SIGTERM ignored when it starts (no stop could end a check),
# the run fails before any check starts, with status 2.
set -eu
me=gate
# the seconds from SIGTERM to SIGKILL, so that a check's own cleanup trap
# can finish on a loaded host
gate_grace=10
# A standard error that is closed (scripts/ci-checks.sh execs this as it was
# started) becomes /dev/null first, so that a line this script writes there
# cannot fail under set -e: an error before any check starts keeps its
# status 2 (it would be 1, set -e's, from the failed write).
{ :; } 8>&2 2> /dev/null || exec 2> /dev/null
# A SIGTERM ignored when this script starts cannot be trapped (POSIX), and
# every supervisor would inherit it ignored and never begin a stop: a stop
# would wait, with no bound, for each check to end by itself. So the run is
# refused. The test is behavioural, before any TERM trap of this script: a
# child shell, which inherits this script's disposition, sends itself TERM
# and says whether it lived on. (The trap listing is no test: dash lists a
# trap on a signal ignored at entry that it never runs.) Any answer but
# "started" alone, none included (no sh), refuses the run as well.
case $({ sh -c 'echo started; kill -s TERM $$; echo ignored'; } 2> /dev/null) in
started) ;;
*)
	echo "$me: SIGTERM is ignored here (or a child shell could not show that it is not), so no stop could end a check; not starting" >&2
	exit 2
	;;
esac
gate_core=$(dirname "$0")/bench/core/common.sh
if [ ! -r "$gate_core" ]; then
	echo "$me: cannot read the bounded-run primitive, $gate_core" >&2
	exit 2
fi
# shellcheck source=bench/core/common.sh
. "$gate_core"
gate_lib=$(cd "$(dirname "$0")/lib" 2> /dev/null && pwd -P) || gate_lib=
if [ -z "$gate_lib" ] || [ ! -r "$gate_lib/supervise.sh" ]; then
	echo "$me: cannot read the process supervisor, lib/supervise.sh" >&2
	exit 2
fi
# shellcheck source=lib/supervise.sh
. "$gate_lib/supervise.sh"
# the source beside this script, whatever the shell says of the sourced file
SV_SRC_DIR=$gate_lib
gate_sv=$(sv_path) || {
	echo "$me: cannot build the process supervisor" >&2
	exit 2
}

# gate_jobs: GATE_JOBS, else half the CPUs, at least 1
gate_jobs() {
	if [ -n "${GATE_JOBS:-}" ]; then
		printf '%s\n' "$GATE_JOBS"
		return 0
	fi
	_gjn=$(sysctl -n hw.ncpu 2> /dev/null || nproc 2> /dev/null || echo 1)
	# not a number: 1; an empty count is 0 to the arithmetic, so 1 as well
	case $_gjn in *[!0-9]*) _gjn=1 ;; esac
	_gjn=$((_gjn / 2))
	[ "$_gjn" -ge 1 ] || _gjn=1
	printf '%s\n' "$_gjn"
}

# gate_in <list> <word>: <word> is a word of <list>
gate_in() {
	case " $1 " in *" $2 "*) return 0 ;; esac
	return 1
}

# gate_start <index>: starts check <index> under a supervisor of its own,
# the check's output and the supervisor's own lines into the check's log.
# The supervisor is a background job of this script, in its process group,
# with SIGTTOU ignored as here, so no write of the fork can stop it; the
# check gets every signal at its default, SIGINT and SIGTTOU included. Its
# pid is in _grun from the statement after the fork; a signal that comes in
# between finds it as $! (gate_cleanup, by _gss and _gsb).
gate_start() {
	_gsc=$(cat "$_gd/$1.path")
	_gs0=$(date +%s)
	gate_last
	_gsb=$_glast
	_gss=1
	"$gate_sv" --grace "$gate_grace" --name "${_gsc##*/}" -- "$_gsc" < /dev/null > "$_gd/$1.log" 2>&1 &
	_gsp=$!
	_grun="$_grun $1:$_gsp:$_gs0"
	_gss=
	_gnr=$((_gnr + 1))
}

# gate_last: _glast, the pid of the last background job ($!), empty before
# the first (under set -u an unset $! would end the script)
gate_last() {
	set +u
	_glast=$!
	set -u
}

# gate_reap: waits until at least one running check's supervisor has ended
# (so the check has, and its group is empty), records the status and wall
# time of each one that has, and prints what is printable
gate_reap() {
	while :; do
		if [ -n "$_gparent" ] && ! gate_parent "$_gparent"; then
			# its output may be a pipe nobody reads any more: a write
			# there must fail, not kill this script before its cleanup
			trap '' PIPE
			echo "$me: the process that started this run (pid $_gparent) is gone; ending every check" >&2 || :
			exit 1
		fi
		_grk= _grd=0
		for _grp in $_grun; do
			_grk_i=${_grp%%:*}
			_grr=${_grp#*:}
			_grpid=${_grr%%:*}
			_grt0=${_grr#*:}
			if bench_pid_alive "$_grpid"; then
				_grk="$_grk $_grp"
				continue
			fi
			_grst=0
			wait "$_grpid" || _grst=$?
			gate_drop "$_grp"
			printf '%s %s\n' "$_grst" "$(($(date +%s) - _grt0))" > "$_gd/$_grk_i.end"
			_grd=$((_grd + 1))
		done
		_grun=$_grk
		_gnr=$((_gnr - _grd))
		if [ "$_grd" -gt 0 ]; then
			gate_flush
			return 0
		fi
		sleep 0.2 2> /dev/null || sleep 1
	done
}

# gate_drop <entry>: takes that entry (<index>:<pid>:<start>, unique) out of
# the running list _grun; an entry that is not there leaves _grun as it is
gate_drop() {
	case " $_grun " in *" $1 "*) ;; *) return 0 ;; esac
	_gdr=" $_grun "
	_grun="${_gdr%% "$1" *} ${_gdr#* "$1" }"
}

# gate_flush: prints, in the order given, every ended check whose
# predecessors have all been printed: its log whole, then its status line
gate_flush() {
	while [ "$_gnext" -le "$_gn" ] && [ -e "$_gd/$_gnext.end" ]; do
		read -r _gfs _gft < "$_gd/$_gnext.end"
		_gfc=$(cat "$_gd/$_gnext.path")
		_gfname=${_gfc##*/}
		_gfwhy=
		cat "$_gd/$_gnext.log" || _gfwhy="its log cannot be read"
		[ "$_gfs" -eq 0 ] || _gfwhy="status $_gfs${_gfwhy:+, $_gfwhy}"
		if [ -n "$_gfwhy" ]; then
			echo "$me: $_gfname: FAILED ($_gfwhy), $_gft s"
			_gfailed="$_gfailed $_gfname"
			_gnf=$((_gnf + 1))
		else
			echo "$me: $_gfname: ok, $_gft s"
		fi
		_gnext=$((_gnext + 1))
	done
}

# gate_phase <limit> <index>...: runs those checks, at most <limit> at a
# time, and returns once every one of them has ended
gate_phase() {
	_gl=$1
	shift
	for _gpi; do
		while [ "$_gnr" -ge "$_gl" ]; do gate_reap; done
		gate_start "$_gpi"
	done
	while [ "$_gnr" -gt 0 ]; do gate_reap; done
}

# gate_parent <pid>: this script's parent is still <pid> (ps fails: it is not)
gate_parent() {
	case " $(ps -o ppid= -p "$$" 2> /dev/null) " in *" $1 "*) return 0 ;; esac
	return 1
}

# gate_end <supervisor>...: ends those checks and every process in their
# groups: SIGTERM to each supervisor, which stops its group (TERM and CONT,
# KILL after gate_grace s), then waits for each. The shell reaps a
# background job as soon as it exits, before any wait: a supervisor that
# ended after the reap's last poll still has its entry, but its pid is back
# with the system, free for another process. So a supervisor that
# bench_pid_alive says is gone gets no TERM. That leaves the time between
# that test and the kill, not up to a whole poll (0.2 s), for the pid to be
# given to another process; the system hands pids out in turn, so that
# needs the pids to wrap around within that time. The wait stays: it gives
# a reaped job's status, or fails at once for a pid that is no job.
gate_end() {
	for _gep; do
		bench_pid_alive "$_gep" || continue
		kill -s TERM "$_gep" 2> /dev/null || :
	done
	for _gep; do wait "$_gep" 2> /dev/null || :; done
}

# gate_cleanup: on the way out (a signal, an error, the parent gone), ends
# every check still running and everything it started (gate_end on each
# check's supervisor, and on the one a signal kept from _grun); then removes
# the logs
gate_cleanup() {
	_gcg=
	for _gcp in $_grun; do
		_gcr=${_gcp#*:}
		_gcg="$_gcg ${_gcr%%:*}"
	done
	gate_last
	if [ -n "${_gss:-}" ] && [ -n "$_glast" ] && [ "$_glast" != "${_gsb:-}" ]; then
		case " $_gcg " in *" $_glast "*) ;; *) _gcg="$_gcg $_glast" ;; esac
	fi
	# shellcheck disable=SC2086 # a list of process groups
	[ -z "$_gcg" ] || gate_end $_gcg
	rm -rf "${_gd:?}"
}

# gate_run_checks <serial> <first> <check>...: the scheduler (see the top
# of this file)
gate_run_checks() {
	_gserial=$1 _gfirst=$2
	shift 2
	# scripts/test.sh starts this in a process group of its own, which is
	# not the terminal's foreground group: with `stty tostop` a write to the
	# terminal would stop it (SIGTTOU) and hang the gate
	trap '' TTOU
	_gparent=${GATE_PARENT:-}
	unset GATE_PARENT
	_gj=$(gate_jobs)
	case $_gj in *[!0-9]* | 0*)
		echo "$me: GATE_JOBS must be a positive whole number, not '$_gj'" >&2
		return 2
		;;
	esac
	if [ $# -eq 0 ]; then
		echo "$me: no checks to run" >&2
		return 2
	fi
	for _gw in $_gserial $_gfirst; do
		_gfound=0
		for _gc; do [ "${_gc##*/}" != "$_gw" ] || _gfound=1; done
		if [ "$_gfound" = 0 ]; then
			echo "$me: $_gw, named in the serial or the first list, is not one of the checks" >&2
			return 2
		fi
	done
	_grun=
	_gd=$(mktemp -d "${TMPDIR:-/tmp}/gate.XXXXXX") || {
		echo "$me: cannot make a scratch directory for the logs" >&2
		return 2
	}
	trap gate_cleanup EXIT
	trap 'exit 129' HUP
	trap 'exit 130' INT
	trap 'exit 143' TERM
	_gt0=$(date +%s)
	_gn=0 _gpar= _gser= _gsnames=
	for _gc; do
		_gn=$((_gn + 1))
		printf '%s\n' "$_gc" > "$_gd/$_gn.path"
		if [ "$_gj" -gt 1 ] && gate_in "$_gserial" "${_gc##*/}"; then
			_gser="$_gser $_gn"
			_gsnames="$_gsnames ${_gc##*/}"
		elif [ "$_gj" -gt 1 ] && gate_in "$_gfirst" "${_gc##*/}"; then
			: # put in front below, in the first list's order
		else
			_gpar="$_gpar $_gn"
		fi
	done
	_gfront= _gfnames=
	if [ "$_gj" -gt 1 ]; then
		for _gw in $_gfirst; do
			_gi=0
			for _gc; do
				_gi=$((_gi + 1))
				if [ "${_gc##*/}" = "$_gw" ] && ! gate_in "$_gserial" "$_gw" && ! gate_in "$_gfront" "$_gi"; then
					_gfront="$_gfront $_gi"
					_gfnames="$_gfnames $_gw"
				fi
			done
		done
	fi
	_gpar="$_gfront $_gpar"
	echo "$me: running $_gn checks, $_gj at a time; first:${_gfnames:- none}; alone, after the others:${_gsnames:- none}"
	_gnr=0 _gnext=1 _gfailed= _gnf=0
	# shellcheck disable=SC2086 # index lists
	gate_phase "$_gj" $_gpar
	# shellcheck disable=SC2086
	gate_phase 1 $_gser
	# a check is printed only once it has ended, so this also covers one
	# that never started
	if [ "$_gnext" -le "$_gn" ]; then
		echo "$me: scheduler error: $((_gnext - 1)) of $_gn checks ran to their end and were printed" >&2
		_gnf=$((_gnf + 1))
	fi
	_gtt=$(($(date +%s) - _gt0))
	if [ "$_gnf" -ne 0 ]; then
		echo "$me: checks: $_gnf of $_gn FAILED in $_gtt s, $_gj at a time:$_gfailed"
		return 1
	fi
	echo "$me: checks: all $_gn passed in $_gtt s, $_gj at a time"
}

gate_run_checks "$@"
