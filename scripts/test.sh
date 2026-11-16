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
# identity-env 46 s, ref-metal 35 s; every other check 23 s or less.
gate_first="check-metalfx-report.sh check-pins.sh check-bench.sh check-identity-env.sh check-ref-metal.sh"
# The scheduler runs in the background so that a SIGHUP, SIGINT or SIGTERM
# to this script is acted on at once (a trap waits for a command in the
# foreground to end): gate_stop passes it on as SIGTERM, which ends the
# scheduler and every check it runs, waits for that, and fails the gate.
gate_sched=
gate_stop() {
	if [ -n "$gate_sched" ]; then
		kill -s TERM "$gate_sched" 2> /dev/null || :
		wait
	fi
	exit "$1"
}
trap 'gate_stop 129' HUP
trap 'gate_stop 130' INT
trap 'gate_stop 143' TERM
scripts/gate-checks.sh "$gate_serial" "$gate_first" tests/check-*.sh &
gate_sched=$!
wait "$gate_sched"
trap - HUP INT TERM

make -B -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)" server game
make -B -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)" UPDATE_PINS= TEST_ARGS= test
echo "test.sh: the gate passed in $(($(date +%s) - gate_t0)) s"
