#!/bin/bash
# scripts/pins.sh -- the renderer-API pin harness driver (TASK27; INFORM8/9).
# bash 3.2. Records reference frames from a renderer over a scenario, compares
# a candidate renderer against them, and self-tests without game data.
#
#   pins.sh record  <renderer> <scenario>      capture frames into the pin store
#   pins.sh compare <renderer> <scenario> [--ref <renderer>] [--threshold T]
#   pins.sh list                               what the store holds
#   pins.sh selftest                           no engine, no game data
#
# Store: $PINS_STORE, else "pins.store" in .claude/dev-loop.json, else
# ~/.quake2-pins. Never inside the repository (asset-derived, INFORM9).
# Game data: $PINS_BASEDIR (the directory holding baseq2/). Binary:
# $PINS_BIN, else release/quake2. Diff tool: $PINS_DIFF (the dev-loop pin
# plugin's), else scripts/pins/framediff.c built on demand; contract:
# "<diff> a.tga b.tga --threshold T" prints score=..., exit 0 ok / 2 over.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
scen_dir="$root/scripts/pins/scenarios"
die() { echo "pins: $*" >&2; exit 1; }

store() {
    if [ -n "${PINS_STORE:-}" ]; then echo "$PINS_STORE"; return; fi
    local s; s=$(sed -n 's/.*"store"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$root/.claude/dev-loop.json" 2>/dev/null | head -1)
    [ -n "$s" ] && echo "$s" || echo "$HOME/.quake2-pins"
}
engine_sha() { git -C "$root" rev-parse --short=12 HEAD 2>/dev/null || echo nogit; }
scenario_hash() { cksum "$scen_dir/$1.cfg" | awk '{print $1}'; }
difftool() {
    if [ -n "${PINS_DIFF:-}" ]; then echo "$PINS_DIFF"; return; fi
    local bin="$(store)/bin/framediff"
    if [ ! -x "$bin" ] || [ "$root/scripts/pins/framediff.c" -nt "$bin" ]; then
        mkdir -p "$(dirname "$bin")"; cc -O2 -o "$bin" "$root/scripts/pins/framediff.c" -lm || die "cannot build framediff"
    fi
    echo "$bin"
}

cmd_record() {
    local renderer=$1 scenario=$2 bin="${PINS_BIN:-$root/release/quake2}"
    [ -f "$scen_dir/$scenario.cfg" ] || die "no scenario $scenario (see $scen_dir)"
    [ -x "$bin" ] || die "no engine binary at $bin (build the client, or set PINS_BIN)"
    [ -n "${PINS_BASEDIR:-}" ] && [ -d "$PINS_BASEDIR/baseq2" ] || die "PINS_BASEDIR must hold baseq2/ (owner game data, never committed)"
    local out="$(store)/$scenario/$renderer/$(engine_sha)-$(scenario_hash "$scenario")"
    rm -rf "$out"; mkdir -p "$out"
    cp "$scen_dir/$scenario.cfg" "$PINS_BASEDIR/baseq2/pins_$scenario.cfg"
    trap "rm -f '$PINS_BASEDIR/baseq2/pins_$scenario.cfg'" EXIT
    "$bin" -datadir "$PINS_BASEDIR" +set vid_renderer "$renderer" +set vid_fullscreen 0 +set r_mode -1 \
        +set vid_width "${PINS_WIDTH:-640}" +set vid_height "${PINS_HEIGHT:-480}" +set s_initsound 0 \
        +set pins_dir "$out" +set pins_scenario "$scenario" +set pins_capture "${PINS_EVERY:-30}" \
        +exec "pins_$scenario.cfg" > "$out/engine.log" 2>&1 || true
    grep -E 'frames,.*fps' "$out/engine.log" > "$out/timedemo.txt" || true
    local n; n=$(ls "$out"/*.tga 2>/dev/null | wc -l | tr -d ' ')
    [ "$n" -gt 0 ] || die "no frames captured; see $out/engine.log"
    echo "recorded $n frames: $out"
}

cmd_compare() {
    local renderer=$1 scenario=$2 ref=gl1 threshold=0.02; shift 2
    while [ $# -gt 0 ]; do case $1 in --ref) ref=$2; shift 2;; --threshold) threshold=$2; shift 2;; *) die "unknown option $1";; esac; done
    local s; s=$(store)
    local cand ref_dir; cand=$(ls -d "$s/$scenario/$renderer"/* 2>/dev/null | tail -1); ref_dir=$(ls -d "$s/$scenario/$ref"/* 2>/dev/null | tail -1)
    [ -n "$cand" ] || die "no candidate frames for $renderer/$scenario"
    [ -n "$ref_dir" ] || die "no reference frames for $ref/$scenario"
    local diff; diff=$(difftool)
    local worst=0 fails=0 total=0 f base r out
    printf '%-24s %-10s %s\n' frame score verdict
    for f in "$ref_dir"/*.tga; do
        base=$(basename "$f"); r="$cand/$base"; total=$((total+1))
        if [ ! -f "$r" ]; then printf '%-24s %-10s %s\n' "$base" - MISSING; fails=$((fails+1)); continue; fi
        if out=$("$diff" "$f" "$r" --threshold "$threshold"); then printf '%-24s %-10s %s\n' "$base" "${out#score=}" ok
        else printf '%-24s %-10s %s\n' "$base" "${out#score=}" OVER; fails=$((fails+1)); fi
        worst=$(printf '%s\n%s\n' "$worst" "${out#score=}" | awk '{v=$1+0; if (v>m) m=v} END {print m}')
    done
    echo "ref=$ref_dir"; echo "candidate=$cand"; echo "frames=$total failed=$fails worst=$worst threshold=$threshold"
    [ "$fails" -eq 0 ]
}

cmd_list() { local s; s=$(store); echo "store: $s"; find "$s" -name '*.tga' 2>/dev/null | sed "s|$s/||" | awk -F/ '{print $1"/"$2"/"$3}' | sort | uniq -c; }

cmd_selftest() {
    local tmp; tmp=$(mktemp -d "${TMPDIR:-/tmp}/pins.XXXXXX"); trap "rm -rf '$tmp'" EXIT
    export PINS_STORE="$tmp/store"; unset PINS_DIFF
    local diff; diff=$(difftool)
    "$diff" --gen "$tmp/a.tga" 64 48 1 && "$diff" --perturb "$tmp/a.tga" "$tmp/b.tga" 3 && "$diff" --perturb "$tmp/a.tga" "$tmp/c.tga" 60
    "$diff" "$tmp/a.tga" "$tmp/a.tga" | grep -q '^score=0.000000' || die "identical frames must score 0"
    "$diff" "$tmp/a.tga" "$tmp/b.tga" --threshold 0.02 >/dev/null || die "a 3/255 shift must pass at 0.02"
    if "$diff" "$tmp/a.tga" "$tmp/c.tga" --threshold 0.02 >/dev/null; then die "a 60/255 shift must fail at 0.02"; fi
    "$diff" --gen "$tmp/d.tga" 32 32 1; if "$diff" "$tmp/a.tga" "$tmp/d.tga" >/dev/null 2>&1; then die "size mismatch must fail"; fi
    # compare over a fake store: reference = a, candidate = b (pass) then c (fail)
    mkdir -p "$PINS_STORE/world/gl1/ref" "$PINS_STORE/world/metal/cand"
    cp "$tmp/a.tga" "$PINS_STORE/world/gl1/ref/world-000000.tga"; cp "$tmp/b.tga" "$PINS_STORE/world/metal/cand/world-000000.tga"
    cmd_compare metal world --ref gl1 >/dev/null || die "compare must pass on a near-identical frame"
    cp "$tmp/c.tga" "$PINS_STORE/world/metal/cand/world-000000.tga"
    if cmd_compare metal world --ref gl1 >/dev/null; then die "compare must fail on a far frame"; fi
    for s in "$scen_dir"/*.cfg; do grep -q '^set timedemo 1' "$s" && grep -q '^demo ' "$s" || die "$(basename "$s") lacks timedemo/demo"; done
    if (cmd_record gl1 world) >/dev/null 2>&1; then die "record must refuse without an engine binary"; fi
    echo "pins selftest: ok"
}

case "${1:-}" in
    record)   [ $# -ge 3 ] || die "usage: pins.sh record <renderer> <scenario>"; cmd_record "$2" "$3";;
    compare)  [ $# -ge 3 ] || die "usage: pins.sh compare <renderer> <scenario> [--ref r] [--threshold t]"; shift; cmd_compare "$@";;
    list)     cmd_list;;
    selftest) cmd_selftest;;
    *) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 1;;
esac
