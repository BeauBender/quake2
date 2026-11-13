#!/bin/sh
# Invariant: the vendored SDL3 tree is byte-identical to its manifest
# (third_party/SDL3/MANIFEST.sha256). The check itself lives in
# scripts/check-vendored.sh so it can be run on its own.
#
# Usage: tests/check-vendored.sh
set -eu
exec "$(dirname "$0")/../scripts/check-vendored.sh"
