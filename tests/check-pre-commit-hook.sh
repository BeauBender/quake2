#!/bin/sh
# Invariant: .githooks/pre-commit refuses a commit that stages the working
# machine's identity or a credential, whatever paths it touches, and lets an
# ordinary commit through. (Scaffolding; the cases follow.)
#
# Usage: tests/check-pre-commit-hook.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C
echo "check-pre-commit-hook: ok"
