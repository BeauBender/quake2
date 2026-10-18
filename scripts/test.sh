#!/bin/sh
# dev-loop gate entry point (gate.test in .claude/dev-loop.json).
#
# The repo has no test suite; the gate is a build of the parts that need
# nothing beyond the system toolchain: the dedicated server (q2ded) and the
# game module. This compiles src/common, src/server, src/game and the unix
# backend. The client and renderers need SDL and are not built here.
#
# Usage: scripts/test.sh            (from anywhere; exits non-zero on failure)
#
# -B forces a full rebuild: the Makefile does not emit header dependencies
# on Darwin (no -MMD), so an incremental build can leave stale objects and
# report success for a commit that does not compile. The full build of these
# two targets takes a few seconds.
set -eu

cd "$(dirname "$0")/.."

make -B -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)" server game
