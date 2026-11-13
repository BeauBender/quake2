#!/bin/bash
# tests/check-evidence.sh -- gate check for the pull-request evidence tool
# (TASK73): the encoder self-test (PNG, animated PNG and GIF read back by
# their own readers) and every refusal of "pins.sh evidence-publish", with a
# stand-in uploader, so the gate never touches the network. Needs python3
# and a C compiler; skipped with one line when python3 is absent. Needs no
# engine and no game data.
set -eu
cd "$(dirname "$0")/.."
if ! command -v python3 >/dev/null 2>&1; then echo "check-evidence: python3 not found, skipped"; exit 0; fi
exec /bin/bash scripts/pins.sh evidence-selftest
