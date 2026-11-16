#!/bin/bash
# scripts/metalfx-report.sh -- a local HTML report comparing the MetalFX options on this Mac (TASK114).
# bash 3.2; needs cc and python3 (standard library only), nothing else.
#
#   metalfx-report.sh run --out <dir outside every work tree> --basedir <game data> [options]
#   metalfx-report.sh report --dir <results dir> [--other <results dir of another host>]... --html <file>
#   metalfx-report.sh plan|check-out ...      (see scripts/metalfx-report/mfx.py)
#
# run options: --host-name NAME (default: this host's short name), --size WxH (1280x720),
#   --runs N (5 timedemo rounds), --gpu-runs N (3), --every N (capture one frame in N, 60),
#   --timeout S (180, the bound of one timedemo run), --seed N (1), --scenarios a,b, --only option,..., --other DIR (a second host's results,
#   shown beside this one), --bin PATH, --replace.
# Build first: make client game ref_metal. The report holds frames made from game data: it goes
# only outside every repository (the generator refuses any other path) and is never committed,
# uploaded or attached. Procedure: docs/runbook/TASK114.md.
set -eu
LC_ALL=C; export LC_ALL
here=$(cd "$(dirname "$0")" && pwd -P)
mfx="$here/metalfx-report/mfx.py"
[ $# -ge 1 ] || { sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
cmd=$1; shift
command -v python3 >/dev/null 2>&1 || { echo "metalfx-report: python3 is not on PATH" >&2; exit 1; }
if [ "$cmd" != run ]; then exec python3 -I "$mfx" "$cmd" "$@"; fi

command -v cc >/dev/null 2>&1 || { echo "metalfx-report: cc is not on PATH" >&2; exit 1; }
# the output path is checked before anything is built or run
out=""; host=""; prev=""
for a in "$@"; do
    [ "$prev" = --out ] && out=$a
    [ "$prev" = --host-name ] && host=$a
    prev=$a
done
[ -n "$out" ] || { echo "metalfx-report: --out is required" >&2; exit 1; }
python3 -I "$mfx" check-out "$out" >/dev/null || exit $?
if [ -z "$host" ]; then
    host=$(hostname -s | tr 'A-Z' 'a-z' | tr -c 'a-z0-9\n-' '-' | sed 's/^-*//') || exit 1
    [ -n "$host" ] || { echo "metalfx-report: no host name; pass --host-name" >&2; exit 1; }
    set -- "$@" --host-name "$host"
fi
tools=$(mktemp -d "${TMPDIR:-/tmp}/mfxtools.XXXXXX")
trap 'rm -rf "${tools:?}"' EXIT
cc -O2 -o "$tools/framediff" "$here/pins/framediff.c" -lm
cc -O2 -o "$tools/ssim" "$here/metalfx-report/ssim.c" -lm
python3 -I "$mfx" run "$@" --framediff "$tools/framediff" --ssim "$tools/ssim"
