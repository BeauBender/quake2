#!/bin/sh
# dev-loop gate entry point (gate.test in .claude/dev-loop.json).
#
# The gate first runs the repo invariants in tests/check-*.sh (each runs
# in seconds on an idle host, longer on a loaded one, and can be run on its
# own; the three identity checks build throwaway git repositories), then
# builds the parts that need nothing beyond the system toolchain: the
# dedicated server (q2ded) and the game module. This compiles src/common, src/server,
# src/game and the unix backend. The client and renderers need SDL and
# are not built here. Last it builds and runs the unit-test suite
# (make test: tests/unit against the real q2ded objects, pins under
# tests/pins/unit; see doc/085_unittests.md). Coverage is a separate,
# slower command: scripts/coverage.sh. UPDATE_PINS= TEST_ARGS= on the make
# command line override anything the environment holds: the gate never
# rewrites a pin and never runs a filtered subset of the suite.
#
# Usage: scripts/test.sh            (from anywhere; exits non-zero on failure)
#
# -B forces a full rebuild: the Makefile does not emit header dependencies
# on Darwin (no -MMD), so an incremental build can leave stale objects and
# report success for a commit that does not compile. The full build of these
# two targets takes a few seconds; the suite builds and runs in a few more.
set -eu

# MAKEFLAGS=-i or -n from the environment would turn a failing or empty run
# into success; the command-line overrides below are kept.
unset MAKEFLAGS GNUMAKEFLAGS MFLAGS

cd "$(dirname "$0")/.."

# The host lock (docs/plans/bench-harness.md, "The host lock"): everything
# below runs as one hold of "scripts/bench/bench.sh lock run gate", so no
# timing run on this host shares the machine with the gate. Under a caller
# that holds the lock already (BENCH_LOCK_TOKEN), bench.sh passes straight
# through; so do the entry points the gate runs in turn (the pin selftest).
# The lock is off, with one line on stderr that names why, with
# BENCH_LOCK=0, on GitHub Actions (CI=true and GITHUB_ACTIONS=true: a runner
# has no other lanes), in a tree without scripts/bench/bench.sh, and with no
# directory for its state (BENCH_STATE, XDG_CACHE_HOME and HOME all unset).
# The run under the lock comes back here with BENCH_LOCK_ENTRY=gate (cleared
# at once, so nothing below inherits it) and with the caller's LC_ALL, which
# bench.sh sets to C.
#
# On a TERM, INT or HUP the lock shell gives this run its grace (twice
# BENCH_STOP_GRACE and at least BENCH_STOP_GRACE + 5 s) before it KILLs it
# and frees the lock. Stopping the checks takes up to the scheduler's own
# 10 s (gate_grace in scripts/gate-checks.sh), so the lock shell gets a
# BENCH_STOP_GRACE of at least 10 (a grace of 20 s); the run under it gets
# the caller's value back, so the checks see what the caller set.
lock_entry=${BENCH_LOCK_ENTRY:-}
unset BENCH_LOCK_ENTRY
lock_off=
if [ "$lock_entry" != gate ] || [ -z "${BENCH_LOCK_TOKEN:-}" ]; then
	if [ "${BENCH_LOCK:-}" = 0 ]; then
		lock_off="BENCH_LOCK=0"
	elif [ "${CI:-}" = true ] && [ "${GITHUB_ACTIONS:-}" = true ]; then
		lock_off="CI (GITHUB_ACTIONS=true)"
	elif [ ! -f scripts/bench/bench.sh ]; then
		lock_off="no bench core (scripts/bench/bench.sh)"
	elif [ -z "${BENCH_STATE:-}${XDG_CACHE_HOME:-}${HOME:-}" ]; then
		lock_off="no state directory (BENCH_STATE, XDG_CACHE_HOME and HOME unset)"
	else
		set -- BENCH_LOCK_ENTRY=gate sh "$PWD/scripts/test.sh" "$@"
		# (not whole seconds: bench.sh refuses it, as before)
		case ${BENCH_STOP_GRACE:-10} in
		*[!0-9]*) ;;
		*)
			if [ "${BENCH_STOP_GRACE:-10}" -lt 10 ]; then
				set -- BENCH_STOP_GRACE="$BENCH_STOP_GRACE" "$@"
				BENCH_STOP_GRACE=10
				export BENCH_STOP_GRACE
			fi
			;;
		esac
		if [ -n "${LC_ALL+x}" ]; then set -- LC_ALL="$LC_ALL" "$@"; else set -- -u LC_ALL "$@"; fi
		exec sh scripts/bench/bench.sh lock run gate -- env "$@"
	fi
	echo "test.sh: the host lock is off: $lock_off" >&2
fi

# Under the lock this run leads a process group of its own (bench.sh starts
# it so), and its parent is the lock shell: the caller's own pid, which ran
# this script first, or a host's own holder. A KILL to the caller's process
# group reaches the lock shell alone, so a watcher (gate_watch_sh, in a
# process group of its own) polls this run's parent every 0.5 s. Once it is
# no longer the lock shell, it sends TERM to this run's process group (to
# this run alone if it leads none): before the makes the trap below runs
# gate_stop, which ends the scheduler and every check; during a make that
# make ends with this run. The watcher signals only after ps has shown that
# this run is still its parent and that this run's parent is another
# process; then it waits to be stopped. This run stops it on every way out
# (gate_unwatch, from gate_stop and on EXIT) and waits for it; should this
# run die by KILL, it sees its own parent go and exits within one poll.
# A run whose parent at start is pid 1 lost its lock shell before its own
# shell started (a KILL to the caller's group after bench_spawn's fork):
# the watcher would take launchd for the lock shell and never signal, and
# the lock may be free, so that run says so and exits before it starts
# anything.
gate_watch=
gate_lock_shell=
gate_watch_sh='trap "exit 0" TERM
pp() { ps -o "$1=" -p "$2" 2> /dev/null | tr -d " "; }
on=1
while kill -0 "$PPID" 2> /dev/null; do
	sleep 0.5 2> /dev/null || sleep 1
	o=$(pp ppid $$)
	[ -z "$o" ] || [ "$o" = "$PPID" ] || exit 0
	[ -n "$on" ] && [ "$o" = "$PPID" ] || continue
	l=$(pp ppid "$PPID")
	[ -n "$l" ] && [ "$l" != "$1" ] || continue
	on=
	if [ "$(pp pgid "$PPID")" = "$PPID" ]; then
		kill -s TERM -- "-$PPID" 2> /dev/null
	else
		kill -s TERM "$PPID" 2> /dev/null
	fi
done'
gate_unwatch() {
	if [ -n "$gate_watch" ]; then
		kill -s TERM -- "-$gate_watch" 2> /dev/null || kill -s TERM "$gate_watch" 2> /dev/null || :
		wait "$gate_watch" 2> /dev/null || :
		gate_watch=
	fi
}
if [ "$lock_entry" = gate ] && [ -n "${BENCH_LOCK_TOKEN:-}" ]; then
	gate_lock_shell=$PPID
	if [ "$gate_lock_shell" = 1 ]; then
		echo "test.sh: the lock shell is gone before the gate started" >&2
		exit 143
	fi
	trap gate_unwatch EXIT
	# job control, in its long spelling: the scheduler's short one below
	# must stay the one line that tests/check-gate-parallel.sh mutates
	set -o monitor
	sh -c "$gate_watch_sh" gate-watch "$gate_lock_shell" < /dev/null > /dev/null 2>&1 &
	gate_watch=$!
	set +o monitor
fi

# A warning, not a failure: without the hooks the commit and push guards
# (.githooks/) do not run in this checkout.
if [ "$(git config --get core.hooksPath || true)" != ".githooks" ]; then
	echo "test.sh: warning: core.hooksPath is not .githooks in this checkout, so the commit and push hooks are not wired; run scripts/setup.sh (if it says it skipped, wire them by hand: git config core.hooksPath .githooks)"
fi

gate_t0=$(date +%s)

# The checks run through scripts/gate-checks.sh (FIX72): GATE_JOBS at a
# time (default half the CPUs, at least 1; GATE_JOBS=1 runs them one after
# another in the glob's order, as before), each into a log of its own that
# is printed whole and in the glob's order with its wall time. The run fails
# if any check fails, and names every one that did; then nothing below runs.
#
# gate_serial lists the checks that write a fixed path another check also
# uses (a build directory, release/, debug/, a scratch path made without
# mktemp, a tracked file of the checkout): they run alone, after the others.
# None does today: each check writes only below its own mktemp -d, and the
# builds among them (autorelease-pool, metal-shim, ref-metal) copy the tree
# into it; static-modules builds in the checkout with STATIC_ROOT in it.
gate_serial=""
# gate_first: the long checks, longest first, started before the others so
# that a long one does not start late and set the gate's wall time
# (GATE_JOBS=1 ignores it). Wall times one at a time on the 6-core build VM
# (2026-11-15, load 3 to 5): metalfx-report 269 s, pins 148 s, bench 107 s,
# gate-parallel about 180 s (its own self-test, 2026-11-16), identity-env
# 46 s, ref-metal 35 s; every other check 23 s or less.
gate_first="check-metalfx-report.sh check-gate-parallel.sh check-pins.sh check-bench.sh check-identity-env.sh check-ref-metal.sh"
# The scheduler runs in the background so that a SIGHUP, SIGINT or SIGTERM
# to this script is acted on at once (a trap waits for a command in the
# foreground to end): gate_stop passes it on as SIGTERM, which ends the
# scheduler and every check it runs, waits for that, and fails the gate.
# Job control is on for that one command, as gate-checks.sh does for each
# check: it leads a process group of its own, and keeps SIGINT and SIGQUIT
# as this script got them (a command started in the background without job
# control ignores both, and so would every check). GATE_PARENT=$$ names
# this script to it: when this script dies, by any signal (SIGKILL too,
# which no trap sees), the scheduler sees it within one poll and ends every
# check, as on SIGTERM. Under the lock a SIGKILL to the caller's process
# group kills the lock shell, not this script: the watcher above sees that
# and sends this script the SIGTERM, so the same holds there.
gate_sched=
gate_stop() {
	gate_unwatch
	if [ -n "$gate_lock_shell" ]; then
		_gp=$(ps -o ppid= -p "$$" 2> /dev/null | tr -d ' ')
		if [ -n "$_gp" ] && [ "$_gp" != "$gate_lock_shell" ]; then
			# the caller's output may be a pipe nobody reads any more
			trap '' PIPE
			echo "test.sh: the lock shell (pid $gate_lock_shell) is gone; ending every check" >&2 || :
		fi
	fi
	if [ -n "$gate_sched" ]; then
		kill -s TERM "$gate_sched" 2> /dev/null || :
		wait
	fi
	exit "$1"
}
trap 'gate_stop 129' HUP
trap 'gate_stop 130' INT
trap 'gate_stop 143' TERM
set -m
GATE_PARENT=$$ scripts/gate-checks.sh "$gate_serial" "$gate_first" tests/check-*.sh &
gate_sched=$!
set +m
wait "$gate_sched"
trap - HUP INT TERM

make -B -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)" server game
make -B -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)" UPDATE_PINS= TEST_ARGS= test
echo "test.sh: the gate passed in $(($(date +%s) - gate_t0)) s"
