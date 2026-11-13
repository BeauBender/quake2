#!/bin/bash
# tests/check-pins.sh -- gate check for the pin harness (TASK27). Runs the
# harness selftest: builds the comparator, exercises score/threshold/size
# behaviour on synthetic frames, the compare report over a fake store, and
# the scenario files. Needs a C compiler; needs no engine and no game data.
set -eu
cd "$(dirname "$0")/.."
exec /bin/bash scripts/pins.sh selftest
