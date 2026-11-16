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
# Off with BENCH_LOCK=0, on CI (CI=true: a runner has no other lanes) and in
# a tree without scripts/bench/bench.sh. The run under the lock comes back
# here with BENCH_LOCK_ENTRY=gate (cleared at once, so nothing below inherits
# it) and with the caller's LC_ALL, which bench.sh sets to C.
lock_entry=${BENCH_LOCK_ENTRY:-}
unset BENCH_LOCK_ENTRY
if { [ "$lock_entry" != gate ] || [ -z "${BENCH_LOCK_TOKEN:-}" ]; } &&
	[ "${BENCH_LOCK:-}" != 0 ] && [ "${CI:-}" != true ] && [ -f scripts/bench/bench.sh ]; then
	set -- BENCH_LOCK_ENTRY=gate sh "$PWD/scripts/test.sh" "$@"
	if [ -n "${LC_ALL+x}" ]; then set -- LC_ALL="$LC_ALL" "$@"; else set -- -u LC_ALL "$@"; fi
	exec sh scripts/bench/bench.sh lock run gate -- env "$@"
fi

# A warning, not a failure: without the hooks the commit and push guards
# (.githooks/) do not run in this checkout.
if [ "$(git config --get core.hooksPath || true)" != ".githooks" ]; then
	echo "test.sh: warning: core.hooksPath is not .githooks in this checkout, so the commit and push hooks are not wired; run scripts/setup.sh (if it says it skipped, wire them by hand: git config core.hooksPath .githooks)"
fi

for check in tests/check-*.sh; do
	"$check"
done

make -B -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)" server game
make -B -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)" UPDATE_PINS= TEST_ARGS= test
