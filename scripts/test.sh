#!/bin/sh
# dev-loop gate entry point (gate.test in .claude/dev-loop.json).
#
# The repo has no test suite; the gate is a build of the parts that need
# nothing beyond the system toolchain: the dedicated server (q2ded) and the
# game module. This compiles src/common, src/server, src/game and the unix
# backend. The client and renderers need SDL/OpenAL and are not built here.
#
# Usage: scripts/test.sh            (from anywhere; exits non-zero on failure)
set -eu

cd "$(dirname "$0")/.."

make -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)" server game
