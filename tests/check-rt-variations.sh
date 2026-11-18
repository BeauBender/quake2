#!/bin/bash
# tests/check-rt-variations.sh -- gate check for the RT variation capture (TASK173). Runs its
# selftest: the presets file parses and names every preset the card asks for, the parser refuses
# bad entries, a run through the real scripts/pins.sh record with the harness's stub engine (in a
# copy of the scripts) gives the expected records, captures and file layout, and an output path
# inside a git work tree is refused before anything runs. Needs cc and python3; no engine, no game data.
set -eu
cd "$(dirname "$0")/.."
exec /bin/bash scripts/rt-variations.sh selftest
