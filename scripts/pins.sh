#!/bin/bash
# scripts/pins.sh -- the renderer-API pin harness driver (TASK27; INFORM8/9).
# bash 3.2. Records reference frames from a renderer over a scenario, compares
# a candidate renderer against them, and self-tests without game data.
#
#   pins.sh record  <renderer> <scenario>      capture frames into the pin store
#   pins.sh compare <renderer> <scenario> [--ref <renderer>] [--ref-id <dir>] [--threshold T]
#   pins.sh list                               what the store holds
#   pins.sh selftest                           no engine, no game data
#
# Environment:
#   PINS_STORE    the pin store, never inside the repository (INFORM9).
#                 Default: ~/.quake2-pins
#   PINS_BASEDIR  the directory holding baseq2/ (owner game data, never committed)
#   PINS_BIN      engine binary. Default: release/quake2
#   PINS_DEMO     the demo every scenario plays. Default: demo1.dm2
#   PINS_WIDTH, PINS_HEIGHT   capture size. Default: 640 x 480
#   PINS_EVERY    capture every Nth rendered frame. Default: 30
#   PINS_TIMEOUT  seconds before a run is killed. Default: 600
#   PINS_DIFF     comparator. Default: scripts/pins/framediff.c, built on demand.
#                 Contract: "<diff> a.tga b.tga --threshold T" prints score=...,
#                 exit 0 at or under the threshold, 2 over it.
#   PINS_DRYRUN   print the engine command line for "record" and stop
#
# compare: the candidate is exactly <store>/<scenario>/<renderer>/<engine
# commit>-<settings hash> for this checkout and these PINS_* settings (record
# it first). The reference is the directory named by --ref-id, else the most
# recently modified directory of <store>/<scenario>/<ref>/; its path is printed.
#
# A scenario is a list of console commands (scripts/pins/scenarios/*.cfg).
# The driver passes each line to the engine as a "+command" argument, then
# starts the demo with +demomap. Nothing is written into the game data, and
# the engine runs with -portable so the owner's saved config is not touched.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
scen_dir="$root/scripts/pins/scenarios"
die() { echo "pins: $*" >&2; exit 1; }

check_name() { case $2 in ''|*[!a-z0-9_]*) die "$1 must match [a-z0-9_]+, got '$2'";; esac; }

store() {
    if [ -n "${PINS_STORE:-}" ]; then echo "$PINS_STORE"; return; fi
    echo "$HOME/.quake2-pins"
}
engine_sha() { git -C "$root" rev-parse --short=12 HEAD 2>/dev/null || echo nogit; }
scenario_hash() {
    { cat "$scen_dir/$1.cfg"; echo "${PINS_DEMO:-demo1.dm2} ${PINS_WIDTH:-640}x${PINS_HEIGHT:-480} every=${PINS_EVERY:-30}"; } | cksum | awk '{print $1}'
}
difftool() {
    if [ -n "${PINS_DIFF:-}" ]; then echo "$PINS_DIFF"; return; fi
    local bin="$(store)/bin/framediff"
    if [ ! -x "$bin" ] || [ "$root/scripts/pins/framediff.c" -nt "$bin" ]; then
        mkdir -p "$(dirname "$bin")"; cc -O2 -o "$bin" "$root/scripts/pins/framediff.c" -lm || die "cannot build framediff"
    fi
    echo "$bin"
}

# Fills the global array engine_args for <renderer> <scenario> <outdir>.
build_args() {
    local renderer=$1 scenario=$2 out=$3 line w first
    engine_args=(-datadir "${PINS_BASEDIR:-}" -portable
        +set vid_renderer "$renderer" +set vid_fullscreen 0 +set r_mode -1
        +set r_customwidth "${PINS_WIDTH:-640}" +set r_customheight "${PINS_HEIGHT:-480}"
        +set s_initsound 0
        +set pins_dir "$out" +set pins_scenario "$scenario" +set pins_capture "${PINS_EVERY:-30}")
    set -f
    while IFS= read -r line || [ -n "$line" ]; do
        case $line in ''|//*) continue ;; esac
        first=1
        for w in $line; do
            if [ "$first" -eq 1 ]; then engine_args+=("+$w"); first=0; else engine_args+=("$w"); fi
        done
    done < "$scen_dir/$scenario.cfg"
    set +f
    engine_args+=(+demomap "${PINS_DEMO:-demo1.dm2}")
}

cmd_record() {
    local renderer=$1 scenario=$2 bin="${PINS_BIN:-$root/release/quake2}" limit="${PINS_TIMEOUT:-600}"
    check_name renderer "$renderer"; check_name scenario "$scenario"
    case $limit in ''|*[!0-9]*) die "PINS_TIMEOUT must be a number of seconds, got '$limit'";; esac
    [ -f "$scen_dir/$scenario.cfg" ] || die "no scenario $scenario (see $scen_dir)"
    local out; out="$(store)/$scenario/$renderer/$(engine_sha)-$(scenario_hash "$scenario")"
    build_args "$renderer" "$scenario" "$out"
    if [ -n "${PINS_DRYRUN:-}" ]; then echo "$bin ${engine_args[*]}"; return 0; fi
    [ -x "$bin" ] || die "no engine binary at $bin (build the client, or set PINS_BIN)"
    [ -n "${PINS_BASEDIR:-}" ] && [ -d "$PINS_BASEDIR/baseq2" ] || die "PINS_BASEDIR must hold baseq2/ (owner game data, never committed)"
    rm -rf "$out"; mkdir -p "$out"
    "$bin" "${engine_args[@]}" > "$out/engine.log" 2>&1 &
    local pid=$! t=0 w status=0 timedout=0
    while kill -0 "$pid" 2>/dev/null; do
        if [ "$t" -ge "$limit" ]; then
            timedout=1
            kill "$pid" 2>/dev/null || true
            w=0; while kill -0 "$pid" 2>/dev/null && [ "$w" -lt 5 ]; do sleep 1; w=$((w+1)); done
            kill -0 "$pid" 2>/dev/null && kill -9 "$pid" 2>/dev/null || true
            echo "pins: engine killed after ${limit}s (PINS_TIMEOUT)" >> "$out/engine.log"
            break
        fi
        sleep 1; t=$((t+1))
    done
    wait "$pid" 2>/dev/null || status=$?
    grep -E 'frames,.*fps' "$out/engine.log" > "$out/timedemo.txt" || true
    [ "$timedout" -eq 0 ] || die "engine timed out after ${limit}s; the pin in $out is incomplete (see $out/engine.log)"
    [ "$status" -eq 0 ] || die "engine exited with status $status; the pin in $out is incomplete (see $out/engine.log)"
    local n; n=$(ls "$out"/*.tga 2>/dev/null | wc -l | tr -d ' ')
    [ "$n" -gt 0 ] || die "no frames captured; see $out/engine.log"
    echo "recorded $n frames: $out"
}

cmd_compare() {
    local renderer=$1 scenario=$2 ref=gl1 threshold=0.02 ref_id=""; shift 2
    check_name renderer "$renderer"; check_name scenario "$scenario"
    while [ $# -gt 0 ]; do case $1 in --ref) ref=$2; check_name reference "$ref"; shift 2;; --threshold) threshold=$2; shift 2;; --ref-id) ref_id=$2; shift 2;; *) die "unknown option $1";; esac; done
    local s; s=$(store)
    local cand ref_dir
    cand="$s/$scenario/$renderer/$(engine_sha)-$(scenario_hash "$scenario")"
    [ -d "$cand" ] || die "no candidate frames at $cand (run: pins.sh record $renderer $scenario, with this engine and these PINS_* settings)"
    if [ -n "$ref_id" ]; then
        case $ref_id in */*|..*) die "--ref-id must be a directory name";; esac
        ref_dir="$s/$scenario/$ref/$ref_id"
        [ -d "$ref_dir" ] || die "no reference $ref_dir"
    else
        ref_dir=$(ls -dt "$s/$scenario/$ref"/*/ 2>/dev/null | head -1); ref_dir=${ref_dir%/}
        [ -n "$ref_dir" ] || die "no reference frames for $ref/$scenario"
    fi
    local diff; diff=$(difftool)
    local worst=0 fails=0 total=0 f base r out verdict
    printf '%-24s %-10s %s\n' frame score verdict
    local score
    for f in "$ref_dir"/*.tga; do
        base=$(basename "$f"); r="$cand/$base"; total=$((total+1))
        if [ ! -f "$r" ]; then printf '%-24s %-10s %s\n' "$base" - MISSING; fails=$((fails+1)); continue; fi
        if out=$("$diff" "$f" "$r" --threshold "$threshold"); then verdict=ok; else verdict=OVER; fails=$((fails+1)); fi
        score=${out#score=}; score=${score%% *}; [ -n "$score" ] || score=-
        printf '%-24s %-10s %s\n' "$base" "$score" "$verdict"
        worst=$(printf '%s\n%s\n' "$worst" "$score" | awk '{v=$1+0; if (v>m) m=v} END {print m+0}')
    done
    echo "ref=$ref_dir"; echo "candidate=$cand"; echo "frames=$total failed=$fails worst=$worst threshold=$threshold"
    [ "$fails" -eq 0 ]
}

cmd_list() { local s; s=$(store); echo "store: $s"; find "$s" -name '*.tga' 2>/dev/null | sed "s|$s/||" | awk -F/ '{print $1"/"$2"/"$3}' | sort | uniq -c; }

# fake_engine <path> <final command>: writes one frame into the pins_dir it is given, then runs the command
fake_engine() {
    printf '%s\n' '#!/bin/bash' 'while [ $# -gt 0 ]; do [ "$1" = pins_dir ] && d=$2; shift; done' 'printf x > "$d/world-000000.tga"' "$2" > "$1"
    chmod +x "$1"
}

cmd_selftest() {
    # the selftest must not depend on, or act on, the caller's environment
    unset PINS_BIN PINS_BASEDIR PINS_DEMO PINS_WIDTH PINS_HEIGHT PINS_EVERY PINS_TIMEOUT PINS_DRYRUN PINS_STORE PINS_DIFF
    tmp=$(mktemp -d "${TMPDIR:-/tmp}/pins.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
    export PINS_STORE="$tmp/store"
    local diff; diff=$(difftool)
    "$diff" --gen "$tmp/a.tga" 64 48 1 && "$diff" --perturb "$tmp/a.tga" "$tmp/b.tga" 3 && "$diff" --perturb "$tmp/a.tga" "$tmp/c.tga" 60
    "$diff" "$tmp/a.tga" "$tmp/a.tga" | grep -q '^score=0.000000' || die "identical frames must score 0"
    "$diff" "$tmp/a.tga" "$tmp/b.tga" --threshold 0.02 >/dev/null || die "a 3/255 shift must pass at 0.02"
    if "$diff" "$tmp/a.tga" "$tmp/c.tga" --threshold 0.02 >/dev/null; then die "a 60/255 shift must fail at 0.02"; fi
    "$diff" --gen "$tmp/d.tga" 32 32 1; if "$diff" "$tmp/a.tga" "$tmp/d.tga" >/dev/null 2>&1; then die "size mismatch must fail"; fi
    # compare over a fake store: reference = a, candidate = b (pass) then c (fail)
    local cid; cid="$(engine_sha)-$(scenario_hash world)"
    mkdir -p "$PINS_STORE/world/gl1/ref" "$PINS_STORE/world/metal/$cid"
    cp "$tmp/a.tga" "$PINS_STORE/world/gl1/ref/world-000000.tga"; cp "$tmp/b.tga" "$PINS_STORE/world/metal/$cid/world-000000.tga"
    cmd_compare metal world --ref gl1 >/dev/null || die "compare must pass on a near-identical frame"
    local rep; rep=$(cmd_compare metal world --ref gl1); case $rep in *"ref=$PINS_STORE/world/gl1/ref"*) ;; *) die "compare must print the chosen reference path";; esac
    # an unrelated directory that sorts last must not be taken for the candidate
    mkdir -p "$PINS_STORE/world/metal/zzzz-1"; cp "$tmp/c.tga" "$PINS_STORE/world/metal/zzzz-1/world-000000.tga"
    cmd_compare metal world --ref gl1 >/dev/null || die "compare must use the candidate of this engine and scenario, not the lexically last directory"
    cp "$tmp/c.tga" "$PINS_STORE/world/metal/$cid/world-000000.tga"
    if cmd_compare metal world --ref gl1 >/dev/null; then die "compare must fail on a far frame"; fi
    rm -rf "$PINS_STORE/world/metal/$cid"
    if (cmd_compare metal world --ref gl1) >/dev/null 2>&1; then die "compare must fail when this engine has no recorded candidate"; fi
    mkdir -p "$PINS_STORE/world/metal/$cid"; cp "$tmp/b.tga" "$PINS_STORE/world/metal/$cid/world-000000.tga"
    # reference choice: newest by modification time by default, --ref-id picks one
    mkdir -p "$PINS_STORE/world/gl1/older" "$PINS_STORE/world/gl1/newer"
    cp "$tmp/a.tga" "$PINS_STORE/world/gl1/older/world-000000.tga"; cp "$tmp/c.tga" "$PINS_STORE/world/gl1/newer/world-000000.tga"
    touch -t 202001010000 "$PINS_STORE/world/gl1/ref" "$PINS_STORE/world/gl1/older"; touch -t 202101010000 "$PINS_STORE/world/gl1/newer"
    if cmd_compare metal world --ref gl1 >/dev/null; then die "compare must default to the most recently modified reference"; fi
    cmd_compare metal world --ref gl1 --ref-id older >/dev/null || die "--ref-id must select that reference"
    if (cmd_compare metal world --ref gl1 --ref-id nosuch) >/dev/null 2>&1; then die "an unknown --ref-id must fail"; fi
    # all-zero scores: worst is 0, and the score column holds the number only
    cp "$tmp/a.tga" "$PINS_STORE/world/metal/$cid/world-000000.tga"
    rep=$(cmd_compare metal world --ref gl1 --ref-id older)
    case $rep in *"worst=0 "*) ;; *) die "compare must print worst=0 when every score is 0: $rep";; esac
    case $rep in *"world-000000.tga"*"0.000000 "*" ok"*) ;; *) die "score column must hold the number: $rep";; esac
    case $rep in *"max="*frame*|*"score=0.000000 max"*) die "score column carries the comparator line";; esac
    # every scenario: timedemo on, no demo line of its own (the driver adds
    # +demomap), and every cvar it sets exists in this source tree
    local s name
    for s in "$scen_dir"/*.cfg; do
        grep -q '^set timedemo 1$' "$s" || die "$(basename "$s") lacks 'set timedemo 1'"
        if grep -q -E '^(demo|demomap) ' "$s"; then die "$(basename "$s") names a demo; the driver adds +demomap"; fi
        if grep -v -E '^(//|$)' "$s" | grep -q -v -E '^set [a-z0-9_]+ [^ ]+$'; then die "$(basename "$s") has a line that is not 'set <cvar> <value>'"; fi
        for name in $(sed -n 's/^set \([a-z0-9_]*\) .*/\1/p' "$s"); do
            grep -rqF "\"$name\"" "$root/src" || die "$(basename "$s") sets $name, which is not a cvar in src/"
        done
    done
    # the commands and cvars the driver itself passes exist in this tree
    grep -qF 'Cmd_AddCommand("demomap"' "$root/src/server/sv_cmd.c" || die "demomap is not a command in src/server/sv_cmd.c"
    for name in vid_renderer vid_fullscreen r_mode r_customwidth r_customheight s_initsound pins_dir pins_scenario pins_capture; do
        grep -rqF "\"$name\"" "$root/src" || die "the driver sets $name, which is not a cvar in src/"
    done
    local line; line=$(PINS_DRYRUN=1 cmd_record gl1 world)
    for name in '-portable' '+set r_customwidth 640' '+set pins_capture 30' '+set cl_entities 0' '+set timedemo 1' '+demomap demo1.dm2'; do
        case $line in *"$name"*) ;; *) die "record command line lacks: $name" ;; esac
    done
    if (PINS_BIN="$tmp/no-such-engine" cmd_record gl1 world) >/dev/null 2>&1; then die "record must refuse without an engine binary"; fi
    # record against a fake engine: crash and hang must not leave a "recorded" pin
    mkdir -p "$tmp/gd/baseq2"
    fake_engine "$tmp/fake-ok" 'exit 0'
    fake_engine "$tmp/fake-crash" 'exit 139'
    fake_engine "$tmp/fake-hang" 'exec sleep 300'
    PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-ok" cmd_record gl1 world >/dev/null || die "record must pass with an engine that exits 0"
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-crash" cmd_record gl1 world) >/dev/null 2>&1; then die "record must fail when the engine crashes after a frame"; fi
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-hang" PINS_TIMEOUT=2 cmd_record gl1 world) >/dev/null 2>&1; then die "record must fail when the engine hangs until PINS_TIMEOUT"; fi
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-ok" PINS_TIMEOUT=abc cmd_record gl1 world) >/dev/null 2>&1; then die "a non-numeric PINS_TIMEOUT must be refused"; fi
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-ok" cmd_record ../evil world) >/dev/null 2>&1; then die "a renderer with .. must be refused"; fi
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-ok" cmd_record gl1 ../world) >/dev/null 2>&1; then die "a scenario with .. must be refused"; fi
    if (cmd_compare 'a/b' world) >/dev/null 2>&1; then die "compare must refuse a renderer with a slash"; fi
    [ "$(HOME=/h/x PINS_STORE= store)" = "/h/x/.quake2-pins" ] || die "store default must be \$HOME/.quake2-pins, whatever .claude/dev-loop.json holds"
    echo "pins selftest: ok"
}

case "${1:-}" in
    record)   [ $# -ge 3 ] || die "usage: pins.sh record <renderer> <scenario>"; cmd_record "$2" "$3";;
    compare)  [ $# -ge 3 ] || die "usage: pins.sh compare <renderer> <scenario> [--ref r] [--threshold t]"; shift; cmd_compare "$@";;
    list)     cmd_list;;
    selftest) cmd_selftest;;
    *) sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 1;;
esac
