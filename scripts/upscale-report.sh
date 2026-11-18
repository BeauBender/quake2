#!/bin/sh
# upscale-report.sh -- compare the upscale_textures backends (TASK167).
#
#   scripts/upscale-report.sh run --out DIR --basedir DATA [--engine-dir release]
#       [--factor 2|4] [--runs 3] [--backends scale2x,metalfx,vt] [--names a,b]
#   scripts/upscale-report.sh check-out DIR
#
# Runs each backend's `upscale_textures <factor> <backend> force` in a copy
# of the engine under DIR, times each over --runs runs, and writes one
# self-contained HTML report into DIR: the time per backend as a range, the
# lines each run said (a fallback to scale2x among them) and matched crops of
# a fixed list of textures. DIR must be outside every git work tree (refused
# otherwise, as is a path with ".."). The report holds images of the
# player's own data: it stays on this machine and is never committed,
# uploaded or attached. Run it under the host lock. The offline self-test is
# tests/check-upscale-report.sh; the runbook is docs/runbook/TASK167.md.

set -eu
LC_ALL=C; export LC_ALL
here=$(cd "$(dirname "$0")" && pwd -P)
command -v python3 >/dev/null 2>&1 || { echo "upscale-report: python3 is not on PATH" >&2; exit 1; }
[ $# -ge 1 ] || { sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
exec python3 -I "$here/upscale-report/report.py" "$@"
