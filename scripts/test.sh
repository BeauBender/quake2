#!/bin/sh
# dev-loop gate entry point (gate.test in .claude/dev-loop.json).
#
# The gate first runs the repo invariants in tests/check-*.sh (each takes
# well under a second and can be run on its own), then builds the parts
# that need nothing beyond the system toolchain: the dedicated server
# (q2ded) and the game module. This compiles src/common, src/server,
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
	echo "test.sh: warning: core.hooksPath is not .githooks in this checkout, so the commit and push hooks are not wired; run scripts/setup.sh"
fi

for check in tests/check-*.sh; do
	"$check"
done

make -B -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)" server game
make -B -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)" UPDATE_PINS= TEST_ARGS= test
