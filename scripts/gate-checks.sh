#!/bin/sh
# The gate's check scheduler (FIX72), run by scripts/test.sh on
# tests/check-*.sh; tests/check-gate-parallel.sh runs it on fake checks.
#
# Usage: scripts/gate-checks.sh <serial> <check>...
#
# Runs every <check> (an executable file), GATE_JOBS of them at a time, and
# exits 0 only when every one of them exited 0. GATE_JOBS defaults to half
# the CPUs (sysctl -n hw.ncpu, else nproc), at least 1; unreadable, it is 1.
# A check whose file name (without its directory) is a word of <serial>
# shares a fixed path with another check: it runs alone, after every other
# check has ended. With GATE_JOBS=1 every check runs alone, in the order
# given, which is how the gate ran them before FIX72.
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
# serial name that is no check, no scratch directory) fail the run, and every
# check that failed is named. Nothing here limits how long a check may run.
set -eu
me=gate

# gate_jobs: GATE_JOBS, else half the CPUs, at least 1
gate_jobs() {
	if [ -n "${GATE_JOBS:-}" ]; then
		printf '%s\n' "$GATE_JOBS"
		return 0
	fi
	_gjn=$(sysctl -n hw.ncpu 2> /dev/null || nproc 2> /dev/null || echo 1)
	case $_gjn in *[!0-9]* | '') _gjn=1 ;; esac
	_gjn=$((_gjn / 2))
	[ "$_gjn" -ge 1 ] || _gjn=1
	printf '%s\n' "$_gjn"
}

# gate_is_serial <name>: <name> is a word of the serial list
gate_is_serial() {
	case " $_gserial " in *" $1 "*) return 0 ;; esac
	return 1
}

# gate_start <index>: starts check <index>, its output into its own log
gate_start() {
	_gsc=$(cat "$_gd/$1.path")
	_gs0=$(date +%s)
	"$_gsc" < /dev/null > "$_gd/$1.log" 2>&1 &
	_grun="$_grun $1:$!:$_gs0"
	_gnr=$((_gnr + 1))
	_gstarted=$((_gstarted + 1))
}

# gate_reap: waits until at least one running check has ended, records the
# status and wall time of each one that has, and prints what is printable
gate_reap() {
	while :; do
		_grk= _grd=0
		for _grp in $_grun; do
			_grk_i=${_grp%%:*}
			_grr=${_grp#*:}
			_grpid=${_grr%%:*}
			_grt0=${_grr#*:}
			if kill -0 "$_grpid" 2> /dev/null; then
				_grk="$_grk $_grp"
				continue
			fi
			_grst=0
			wait "$_grpid" || _grst=$?
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

gate_cleanup() {
	for _gcp in $_grun; do
		_gcr=${_gcp#*:}
		kill -TERM "${_gcr%%:*}" 2> /dev/null || :
	done
	rm -rf "${_gd:?}"
}

# gate_run_checks <serial> <check>...: the scheduler (see the top of this file)
gate_run_checks() {
	_gserial=$1
	shift
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
	for _gw in $_gserial; do
		_gfound=0
		for _gc; do [ "${_gc##*/}" != "$_gw" ] || _gfound=1; done
		if [ "$_gfound" = 0 ]; then
			echo "$me: the serial check $_gw is not one of the checks" >&2
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
		if [ "$_gj" -gt 1 ] && gate_is_serial "${_gc##*/}"; then
			_gser="$_gser $_gn"
			_gsnames="$_gsnames ${_gc##*/}"
		else
			_gpar="$_gpar $_gn"
		fi
	done
	echo "$me: running $_gn checks, $_gj at a time; alone, after the others:${_gsnames:- none}"
	_gnr=0 _gstarted=0 _gnext=1 _gfailed= _gnf=0
	# shellcheck disable=SC2086 # index lists
	gate_phase "$_gj" $_gpar
	# shellcheck disable=SC2086
	gate_phase 1 $_gser
	if [ "$_gstarted" -ne "$_gn" ] || [ "$_gnext" -le "$_gn" ]; then
		echo "$me: scheduler error: $_gstarted of $_gn checks started, $((_gnext - 1)) printed" >&2
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
