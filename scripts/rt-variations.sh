#!/bin/bash
# scripts/rt-variations.sh -- ray-tracing variations side by side, for the owner's look review (TASK173).
# bash 3.2; needs cc and python3 (standard library only).
#
#   rt-variations.sh run --out <dir outside every work tree> [--views a,b] [--presets a,b]
#                        [--seed N] [--size WxH] [--bin PATH] [--replace] [--clip-ms N] [--file F]
#   rt-variations.sh plan [--views a,b] [--presets a,b] [--file F]   the records a run makes
#   rt-variations.sh check [--file F]                                parse the presets file
#   rt-variations.sh selftest                                        no engine, no game data
#
# run records every view of scripts/pins/rt-variations.txt (or --views) under every preset (or
# --presets, plus off and the preset an auto capture names) with scripts/pins.sh record metal, from
# the view's base scenario and demo, with PINS_SEED (--seed, default 1) and the Metal validation
# layer on, into the pin store <out>/store; then writes per view, into <out>/<view>/: sheet.png
# (every preset's frame of the view's capture, captioned, four to a row), clip.png (an animated PNG:
# off, then each preset) and full/<NN>-<preset>.png (the full-size frames). PINS_BASEDIR names the
# game data, as for pins.sh. Build first: make client game ref_metal (or name a binary with --bin).
# The output holds frames made from game data: it goes only outside every repository (the path is
# refused inside a git work tree before anything runs) and is never committed or uploaded.
# Procedure: docs/runbook/TASK173.md.
set -eu
LC_ALL=C; export LC_ALL
here=$(cd "$(dirname "$0")" && pwd -P)
rtvar="$here/pins/rtvar.py"
[ $# -ge 1 ] || { sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "rt-variations: python3 is not on PATH" >&2; exit 1; }
cmd=$1; shift
if [ "$cmd" != run ]; then exec python3 -I "$rtvar" "$cmd" "$@"; fi

command -v cc >/dev/null 2>&1 || { echo "rt-variations: cc is not on PATH" >&2; exit 1; }
# the output path is checked before anything is built or run
out=""; prev=""
for a in "$@"; do
    [ "$prev" != --out ] || out=$a
    prev=$a
done
[ -n "$out" ] || { echo "rt-variations: --out is required" >&2; exit 1; }
python3 -I "$here/metalfx-report/mfx.py" check-out "$out" >/dev/null || exit 1
tools=$(mktemp -d "${TMPDIR:-/tmp}/rtvartools.XXXXXX")
trap 'rm -rf "${tools:?}"' EXIT
cc -O2 -o "${tools:?}/framediff" "$here/pins/framediff.c" -lm
python3 -I "$rtvar" run "$@" --framediff "${tools:?}/framediff"
