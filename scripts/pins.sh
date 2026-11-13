#!/bin/bash
# scripts/pins.sh -- the renderer-API pin harness driver (TASK27; INFORM8/9).
# bash 3.2. Records reference frames from a renderer over a scenario, compares
# a candidate renderer against them, and self-tests without game data.
#
#   pins.sh record  <renderer> <scenario> [--run N] [--replace]
#   pins.sh compare <renderer> <scenario> [--ref <renderer>] [--ref-id <dir>]
#                   [--run N | --cand-id <dir>] [--threshold T] [--bad-threshold B]
#   pins.sh list                               what the store holds
#   pins.sh selftest                           no engine, no game data
#
# Names (renderer, scenario) match [a-z0-9_][a-z0-9_-]*, e.g. gl1-metal, rt-ao.
# The renderer gl1-metal is ref_gl1 built with "make ref_gl1 WITH_METAL_SHIM=yes":
# the engine runs it as vid_renderer gl1, and record checks that the module
# it loaded was the shim build (and, for gl1, that it was not).
#
# Environment:
#   PINS_STORE    the pin store, never inside this repository or any other
#                 git work tree (INFORM9; record and compare refuse it).
#                 Default: ~/.quake2-pins
#   PINS_BASEDIR  the directory holding baseq2/ (owner game data, never committed)
#   PINS_BIN      engine binary. Default: release/quake2 of this checkout, which
#                 record refuses when "make -q" says it or its renderer
#                 module is out of date with the source. A binary named here
#                 adds its checksum to the pin id; compare needs the same value.
#   PINS_DEMO     what every scenario plays: a demo, a file name under demos/
#                 ([A-Za-z0-9_][A-Za-z0-9_.-]*.dm2), or a cinematic, a file
#                 name under video/ (the same pattern ending in .cin, for the
#                 cinematic scenario, which needs a .cin and refuses a .dm2;
#                 every other scenario refuses a .cin). Default: demo1.dm2
#   PINS_WIDTH, PINS_HEIGHT   capture size. Default: 640 x 480
#   PINS_EVERY    capture every Nth rendered frame. Default: 30
#   PINS_TIMEOUT  seconds before a run is killed. Default: 600
#   PINS_DIFF     comparator. Default: scripts/pins/framediff.c, built on demand.
#                 Contract: "<diff> a.tga b.tga --threshold T --bad-threshold B"
#                 prints score=... bad=..., exit 0 when both are at or under
#                 their thresholds, 2 when either is over.
#   PINS_DRYRUN   print the engine command line for "record" and stop
#   PINS_TRACE    0: record no renderer-API trace. Default: record writes
#                 trace.q2r beside the frames (the engine's pins_record)
#
# Every path and value handed to the engine (store, PINS_BASEDIR, PINS_DEMO,
# scenario values) may hold only A-Z a-z 0-9 _ . / - : the engine re-joins
# its command line unquoted, so anything else would be mangled or run as a
# command. record refuses such values.
#
# record writes <store>/<scenario>/<renderer>/<id>, where <id> is
# <engine>-<settings hash> for this checkout and these PINS_* settings, with
# "-r<N>" appended for --run N (N >= 2), so a second run of the same engine
# and scenario does not replace the first. <engine> is the commit, plus
# "-d<hash>" when src/ or the Makefile hold uncommitted changes and
# "-b<hash>" when PINS_BIN is set. A complete pin of the same id is never
# overwritten unless --replace is given; an incomplete one is. A record is
# complete only when the engine exited 0 within PINS_TIMEOUT, captured at
# least one frame, manifest.txt holds no FAILED line (the engine's mark for a
# frame it could not write) and every frame in it was rendered by the
# requested renderer (the engine refuses renderer fallback while capturing);
# the file "complete" is then written last. A failed record leaves
# "incomplete" with the reason instead.
#
# compare: the candidate is the directory of this checkout and settings
# (--run N for a later run, or --cand-id <dir>). The reference is --ref-id
# <dir>, else the most recently modified complete directory of
# <store>/<scenario>/<ref>/ other than the candidate. Both must be complete,
# must not be the same directory and must hold the same number of frames. A
# frame passes when its mean difference is at or under --threshold
# (default 0.02) and its fraction of differing pixels at or under
# --bad-threshold (default 0.005); both values are printed with the result.
# Two runs of one renderer: record soft world; record soft world --run 2;
# compare soft world --run 2 --ref soft.
#
# A scenario is a list of "set <cvar> <value>" lines
# (scripts/pins/scenarios/*.cfg); any other line, and a line setting a cvar
# the driver sets itself (pins_*, vid_renderer, vid_fullscreen, r_mode,
# r_customwidth, r_customheight, s_initsound) or one that chooses a directory
# or the game module (basedir, cddir, game, datadir, cfgdir), is refused, as
# is a value holding ".." or starting with "-" that is not a number. The driver passes
# each line to the engine as a "+set" argument, then its own settings, then
# starts the demo with +demomap. Nothing is written into the game data: with
# -portable the engine writes beside its binary (qconsole.log,
# console_history.txt), and with pin capture on it neither reads nor writes
# config.cfg or autoexec.cfg. A demo that cannot be opened, or whose file
# ends before its end marker, makes the engine exit non-zero at once, as
# does a cinematic that cannot be opened. A cinematic is captured every
# Nth cinematic frame, not every Nth rendered frame.
set -eu
# character ranges such as [a-z] in patterns mean ASCII only in the C locale
export LC_ALL=C
root=$(cd "$(dirname "$0")/.." && pwd)
scen_dir="$root/scripts/pins/scenarios"
die() { echo "pins: $*" >&2; exit 1; }
# usage: the header comment of this file, from line 2 to the first line that is not a comment
usage() { awk 'NR == 1 { next } !/^#/ { exit } { sub(/^# ?/, ""); print }' "$root/scripts/pins.sh"; }

check_name() { case $2 in ''|-*|*[!a-z0-9_-]*) die "$1 must match [a-z0-9_][a-z0-9_-]*, got '$2'";; esac; }
check_run() { case $1 in [1-9]|[1-9][0-9]|[1-9][0-9][0-9]) ;; *) die "--run must be a number from 1 to 999, got '$1'";; esac; }
check_number() { [[ $2 =~ ^([0-9]+(\.[0-9]*)?|\.[0-9]+)$ ]] || die "$1 must be a non-negative decimal number, got '$2'"; }
check_count() { case $2 in ''|0*|*[!0-9]*) die "$1 must be a positive integer, got '$2'";; esac; }
# check_arg <what> <value> <path|value>: refuse what the engine command line cannot carry
check_arg() {
    case $2 in ''|*[!A-Za-z0-9_./-]*) die "$1 must hold only A-Z a-z 0-9 _ . / - (the engine re-joins its command line unquoted), got '$2'";; esac
    if [ "$3" = path ]; then case $2 in *..*) die "$1 must not contain '..', got '$2'";; esac; fi
}

store() {
    if [ -n "${PINS_STORE:-}" ]; then echo "$PINS_STORE"; return; fi
    echo "$HOME/.quake2-pins"
}
# phys_path <path>: sets phys_result to the physical path, resolving symbolic
# links through the nearest ancestor that exists, and phys_existing to that
# ancestor; phys_dangling names a dangling symbolic link met on the way
phys_path() {
    local p=$1 rest=""
    phys_dangling=""
    while [ ! -d "$p" ]; do
        if [ -L "$p" ] && [ -z "$phys_dangling" ]; then phys_dangling=$p; fi
        rest="/$(basename "$p")$rest"; p=$(dirname "$p")
    done
    phys_existing=$(cd "$p" && pwd -P)
    phys_result=$(printf '%s%s' "$phys_existing" "$rest" | tr -s /)
}
# check_store_path <path>: refuse a place inside this repository or any git
# work tree, and a dangling symbolic link (it could later be made into a
# directory inside one)
check_store_path() {
    local s r; phys_path "$1"; s=$phys_result; r=$(cd "$root" && pwd -P)
    [ -z "$phys_dangling" ] || die "the pin store path $1 holds a dangling symbolic link ($phys_dangling)"
    case "$s/" in "$r/"*) die "the pin store $s is inside this repository; set PINS_STORE outside it (INFORM9)";; esac
    if git -C "$phys_existing" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        die "the pin store $s is inside a git work tree; set PINS_STORE outside any repository (INFORM9)"
    fi
}
# check_store [<scenario> <renderer>...]: the pin store is never inside this
# repository or any other git work tree (INFORM9), and neither is each
# <store>/<scenario>/<renderer> it is about to use, which a link in the store
# could send elsewhere
check_store() {
    local scen=${1:-} rend
    check_store_path "$(store)"
    if [ $# -ge 1 ]; then shift; fi
    for rend in "$@"; do check_store_path "$(store)/$scen/$rend"; done
}
# a demo file name under demos/ ([A-Za-z0-9_][A-Za-z0-9_.-]*.dm2) or a
# cinematic under video/ (the same, ending in .cin), no path; +demomap plays both
check_demo() {
    case $1 in
        *[!A-Za-z0-9_.-]*|*..*) ;;
        [A-Za-z0-9_]*.dm2|[A-Za-z0-9_]*.cin) return 0;;
    esac
    die "PINS_DEMO must be a demo file name [A-Za-z0-9_][A-Za-z0-9_.-]*.dm2 or a cinematic name ending in .cin (no path, no leading - or .), got '$1'"
}
# The engine part of a pin id: the commit, then "-d<hash>" when src/ or the
# Makefile differ from it (tracked changes and untracked files), then
# "-b<hash>" of the binary when PINS_BIN names one, so a pin from a modified
# tree or from another tree's binary never shares an id with this commit's.
engine_sha() {
    local sha dirty f
    sha=$(git -C "$root" rev-parse --short=12 HEAD 2>/dev/null) || sha=nogit
    if [ "$sha" != nogit ]; then
        dirty=$(git -C "$root" status --porcelain --untracked-files=all -- src Makefile 2>/dev/null || true)
        if [ -n "$dirty" ]; then
            sha="$sha-d$( { git -C "$root" diff HEAD -- src Makefile
                git -C "$root" ls-files -o --exclude-standard -- src Makefile | while IFS= read -r f; do echo "$f"; cat "$root/$f"; done
            } | cksum | awk '{print $1}')"
        fi
    fi
    if [ -n "${PINS_BIN:-}" ]; then sha="$sha-b$(cksum < "$PINS_BIN" 2>/dev/null | awk '{print $1}')"; fi
    echo "$sha"
}
# A pin renderer name that is a build of another engine renderer (TASK47):
# gl1-metal is ref_gl1 built with "make ref_gl1 WITH_METAL_SHIM=yes", which
# the engine loads as vid_renderer gl1 and which names itself in its
# GL_RENDERER string as "(gl1-metal)".
engine_renderer() { case $1 in gl1-metal) echo gl1;; *) echo "$1";; esac; }
shim_marker='^GL_RENDERER: .*(gl1-metal)'
# shim_build <log>: true when the first GL_RENDERER line of the engine log
# names the shim. Only the first counts: the renderer prints it at init,
# before the demo starts, and a demo's svc_print can put later lines of the
# same shape into the log.
shim_build() { grep -m1 '^GL_RENDERER: ' "$1" | grep -q "$shim_marker"; }
# stale_engine <bin> <renderer>: names the binary or renderer module of this
# checkout that "make -q" says is out of date with its sources, or that is
# older than a header under src/, empty when the build is current (or make is
# missing). For gl1-metal it asks make about the WITH_METAL_SHIM=yes build.
stale_engine() {
    local m r; r=$(engine_renderer "$2")
    local build=(); [ "$2" = gl1-metal ] && build=(WITH_METAL_SHIM=yes)
    command -v make >/dev/null 2>&1 || return 0
    for m in "$1" "$(dirname "$1")/ref_$r".so "$(dirname "$1")/ref_$r".dylib "$(dirname "$1")/ref_$r".dll; do
        [ -f "$m" ] || continue
        case $m in "$root"/*) ;; *) continue;; esac
        if ! make -C "$root" -q ${build[@]+"${build[@]}"} "${m#"$root"/}" >/dev/null 2>&1; then echo "${m#"$root"/} is out of date"; return; fi
        # the Makefile has no header dependencies on macOS, so make -q cannot see a header edit
        if [ -n "$(find "$root/src" -name '*.h' -newer "$m" -print 2>/dev/null | head -n 1)" ]; then
            echo "${m#"$root"/} is older than a header under src/"; return
        fi
    done
}
scenario_hash() {
    { cat "$scen_dir/$1.cfg"; echo "${PINS_DEMO:-demo1.dm2} ${PINS_WIDTH:-640}x${PINS_HEIGHT:-480} every=${PINS_EVERY:-30}"; } | cksum | awk '{print $1}'
}
# run_id <scenario> <run>
run_id() {
    local id; id="$(engine_sha)-$(scenario_hash "$1")"
    if [ "$2" -gt 1 ]; then id="$id-r$2"; fi
    echo "$id"
}
frame_count() { ls "$1"/*.tga 2>/dev/null | wc -l | tr -d ' '; }
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
    local renderer=$1 scenario=$2 out=$3 line verb name value extra n=0
    engine_args=(-datadir "${PINS_BASEDIR:-}" -portable)
    # every line is exactly "set <cvar> <value>". Refused: the cvars the driver
    # sets itself and the ones that choose a data or config directory or the
    # game module, a value holding "..", and a value starting with "-" unless
    # it is a plain number. That is all the filter guarantees; the cvars that
    # remain are the scenario author's to reason about.
    set -f
    while IFS= read -r line || [ -n "$line" ]; do
        n=$((n+1))
        case $line in ''|//*) continue ;; esac
        [[ $line =~ ^set\ [^\ ]+\ [^\ ]+$ ]] || die "$scenario.cfg line $n: not 'set <cvar> <value>' with single spaces: $line"
        read -r verb name value extra <<< "$line"
        [ "$verb" = set ] && [ -n "$value" ] && [ -z "$extra" ] || die "$scenario.cfg line $n: not 'set <cvar> <value>': $line"
        case $name in ''|*[!a-z0-9_]*) die "$scenario.cfg line $n: the cvar must match [a-z0-9_]+, got '$name'";; esac
        case $name in pins_*|vid_renderer|vid_fullscreen|r_mode|r_customwidth|r_customheight|s_initsound)
            die "$scenario.cfg line $n: $name is set by the driver, a scenario may not set it";; esac
        case $name in basedir|cddir|game|datadir|cfgdir)
            die "$scenario.cfg line $n: $name chooses a directory or the game module, a scenario may not set it";; esac
        case $value in *..*) die "$scenario.cfg line $n: the value must not contain '..': $value";; esac
        case $value in -*) [[ $value =~ ^-([0-9]+\.?[0-9]*|\.[0-9]+)$ ]] || die "$scenario.cfg line $n: a value starting with - must be a number: $value";; esac
        check_arg "$scenario.cfg value" "$value" value
        engine_args+=(+set "$name" "$value")
    done < "$scen_dir/$scenario.cfg"
    set +f
    engine_args+=(+set vid_renderer "$(engine_renderer "$renderer")" +set vid_fullscreen 0 +set r_mode -1
        +set r_customwidth "${PINS_WIDTH:-640}" +set r_customheight "${PINS_HEIGHT:-480}"
        +set s_initsound 0
        +set pins_dir "$out" +set pins_scenario "$scenario" +set pins_capture "${PINS_EVERY:-30}")
    if [ "${PINS_TRACE:-1}" != 0 ]; then engine_args+=(+set pins_record trace.q2r); fi
    engine_args+=(+demomap "${PINS_DEMO:-demo1.dm2}")
}

cmd_record() {
    local renderer=$1 scenario=$2 run=1 replace=0; shift 2
    local bin="${PINS_BIN:-$root/release/quake2}" limit="${PINS_TIMEOUT:-600}"
    check_name renderer "$renderer"; check_name scenario "$scenario"
    while [ $# -gt 0 ]; do case $1 in
        --run) [ $# -ge 2 ] || die "--run needs a value"; run=$2; check_run "$run"; shift 2;;
        --replace) replace=1; shift;;
        *) die "unknown option $1";; esac; done
    case $limit in ''|*[!0-9]*) die "PINS_TIMEOUT must be a number of seconds, got '$limit'";; esac
    check_count PINS_WIDTH "${PINS_WIDTH:-640}"; check_count PINS_HEIGHT "${PINS_HEIGHT:-480}"; check_count PINS_EVERY "${PINS_EVERY:-30}"
    check_demo "${PINS_DEMO:-demo1.dm2}"
    case ${PINS_DEMO:-demo1.dm2} in
        *.cin) [ "$scenario" = cinematic ] || die "PINS_DEMO '${PINS_DEMO}' is a cinematic; only the cinematic scenario plays one, not $scenario";;
        *) [ "$scenario" != cinematic ] || die "the cinematic scenario needs PINS_DEMO naming a cinematic ending in .cin, got '${PINS_DEMO:-demo1.dm2}'";;
    esac
    [ -f "$scen_dir/$scenario.cfg" ] || die "no scenario $scenario (see $scen_dir)"
    [ -z "${PINS_BIN:-}" ] || [ -f "$PINS_BIN" ] || die "no engine binary at $PINS_BIN (build the client, or set PINS_BIN)"
    check_store "$scenario" "$renderer"
    local out; out=$(printf '%s' "$(store)/$scenario/$renderer/$(run_id "$scenario" "$run")" | tr -s /)
    check_arg "the pin store path" "$out" path
    if [ -n "${PINS_BASEDIR:-}" ]; then check_arg PINS_BASEDIR "$PINS_BASEDIR" path; fi
    build_args "$renderer" "$scenario" "$out"
    if [ -n "${PINS_DRYRUN:-}" ]; then echo "$bin ${engine_args[*]}"; return 0; fi
    [ -x "$bin" ] || die "no engine binary at $bin (build the client, or set PINS_BIN)"
    [ -n "${PINS_BASEDIR:-}" ] && [ -d "$PINS_BASEDIR/baseq2" ] || die "PINS_BASEDIR must hold baseq2/ (owner game data, never committed)"
    if [ -z "${PINS_BIN:-}" ]; then
        local stale; stale=$(stale_engine "$bin" "$renderer")
        [ -z "$stale" ] || die "the build is out of date ($stale); rebuild before recording"
    fi
    if [ -f "$out/complete" ] && [ "$replace" -ne 1 ]; then
        die "a complete pin already exists at $out; record with --run N for another run, or --replace to overwrite it"
    fi
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
    local reason="" n bad
    n=$(frame_count "$out")
    if [ "$timedout" -ne 0 ]; then reason="engine timed out after ${limit}s"
    elif [ "$status" -ne 0 ]; then reason="engine exited with status $status"
    elif [ "$n" -eq 0 ]; then reason="no frames captured"
    elif [ ! -f "$out/manifest.txt" ]; then reason="no manifest.txt"
    elif grep -q '^FAILED ' "$out/manifest.txt"; then reason="the engine could not write a frame ($(grep -m1 '^FAILED ' "$out/manifest.txt"))"
    elif [ "$(wc -l < "$out/manifest.txt" | tr -d ' ')" -ne "$n" ]; then reason="manifest.txt does not list the $n frames"
    else
        bad=$(awk -v r="$(engine_renderer "$renderer")" '$2 != r {print $2; exit}' "$out/manifest.txt")
        [ -z "$bad" ] || reason="frames were rendered by '$bad', not '$renderer' (renderer fallback)"
    fi
    # gl1 and gl1-metal are one module name: the GL_RENDERER line tells which build ran
    if [ -z "$reason" ] && [ "$renderer" = gl1-metal ]; then
        shim_build "$out/engine.log" ||
            reason="the engine did not load the Metal shim build of ref_gl1 (the first GL_RENDERER line of engine.log does not end in '(gl1-metal)'; build it with make ref_gl1 WITH_METAL_SHIM=yes)"
    elif [ -z "$reason" ] && [ "$renderer" = gl1 ] && shim_build "$out/engine.log"; then
        reason="the frames came from the Metal shim build of ref_gl1; record them as gl1-metal"
    fi
    if [ -n "$reason" ]; then
        echo "$reason" > "$out/incomplete"
        die "$reason; the pin in $out is incomplete (see $out/engine.log)"
    fi
    date -u '+%Y-%m-%dT%H:%M:%SZ' > "$out/complete"
    echo "recorded $n frames: $out"
    if [ -f "$out/trace.q2r" ]; then echo "trace: $out/trace.q2r ($(wc -c < "$out/trace.q2r" | tr -d ' ') bytes)"; fi
}

cmd_compare() {
    local renderer=$1 scenario=$2 ref=gl1 threshold=0.02 bad_threshold=0.005 ref_id="" cand_id="" run=1; shift 2
    check_name renderer "$renderer"; check_name scenario "$scenario"
    while [ $# -gt 0 ]; do
        [ $# -ge 2 ] || die "$1 needs a value"
        case $1 in
            --ref) ref=$2; check_name reference "$ref";;
            --threshold) threshold=$2; check_number --threshold "$threshold";;
            --bad-threshold) bad_threshold=$2; check_number --bad-threshold "$bad_threshold";;
            --ref-id) ref_id=$2; case $ref_id in ''|*/*|.*) die "--ref-id must be a directory name";; esac;;
            --cand-id) cand_id=$2; case $cand_id in ''|*/*|.*) die "--cand-id must be a directory name";; esac;;
            --run) run=$2; check_run "$run";;
            *) die "unknown option $1";;
        esac
        shift 2
    done
    [ -f "$scen_dir/$scenario.cfg" ] || die "no scenario $scenario (see $scen_dir)"
    check_store "$scenario" "$renderer" "$ref"
    local s; s=$(store)
    local cand ref_dir
    [ -n "$cand_id" ] || cand_id=$(run_id "$scenario" "$run")
    cand="$s/$scenario/$renderer/$cand_id"
    [ -d "$cand" ] || die "no candidate frames at $cand (run: pins.sh record $renderer $scenario, with this engine and these PINS_* settings)"
    [ -f "$cand/complete" ] || die "the candidate $cand is incomplete (no 'complete' marker; record it again)"
    if [ -n "$ref_id" ]; then
        ref_dir="$s/$scenario/$ref/$ref_id"
        [ -d "$ref_dir" ] || die "no reference $ref_dir"
    else
        ref_dir=$(ls -dt "$s/$scenario/$ref"/*/ 2>/dev/null | while IFS= read -r d; do
            d=${d%/}; [ "$d" != "$cand" ] && [ -f "$d/complete" ] && { echo "$d"; break; }; done || true)
        [ -n "$ref_dir" ] || die "no complete reference for $ref/$scenario other than the candidate"
    fi
    [ "$ref_dir" != "$cand" ] || die "the reference and the candidate are the same directory ($cand)"
    [ -f "$ref_dir/complete" ] || die "the reference $ref_dir is incomplete (no 'complete' marker)"
    local diff; diff=$(difftool)
    local worst=0 worst_bad=0 fails=0 total=0 f base r out verdict score badf nref ncand
    nref=$(frame_count "$ref_dir"); ncand=$(frame_count "$cand")
    printf '%-24s %-10s %-10s %s\n' frame score bad verdict
    for f in "$ref_dir"/*.tga; do
        [ -f "$f" ] || continue
        base=$(basename "$f"); r="$cand/$base"; total=$((total+1))
        if [ ! -f "$r" ]; then printf '%-24s %-10s %-10s %s\n' "$base" - - MISSING; fails=$((fails+1)); continue; fi
        if out=$("$diff" "$f" "$r" --threshold "$threshold" --bad-threshold "$bad_threshold"); then verdict=ok; else verdict=OVER; fails=$((fails+1)); fi
        score=${out#score=}; score=${score%% *}; [ -n "$score" ] || score=-
        case $out in *" bad="*) badf=${out#* bad=}; badf=${badf%% *};; *) badf=-;; esac
        printf '%-24s %-10s %-10s %s\n' "$base" "$score" "$badf" "$verdict"
        worst=$(printf '%s\n%s\n' "$worst" "$score" | awk '{v=$1+0; if (v>m) m=v} END {print m+0}')
        worst_bad=$(printf '%s\n%s\n' "$worst_bad" "$badf" | awk '{v=$1+0; if (v>m) m=v} END {print m+0}')
    done
    for f in "$cand"/*.tga; do
        [ -f "$f" ] || continue
        base=$(basename "$f")
        if [ ! -f "$ref_dir/$base" ]; then printf '%-24s %-10s %-10s %s\n' "$base" - - EXTRA; fi
    done
    if [ "$nref" -ne "$ncand" ]; then echo "frame count differs: reference $nref, candidate $ncand"; fails=$((fails+1)); fi
    if [ "$total" -eq 0 ]; then echo "the reference holds no frames"; fails=$((fails+1)); fi
    echo "ref=$ref_dir"; echo "candidate=$cand"
    echo "frames=$total failed=$fails worst=$worst worst_bad=$worst_bad threshold=$threshold bad_threshold=$bad_threshold"
    [ "$fails" -eq 0 ]
}

cmd_list() {
    local s line; s=$(store); echo "store: $s"
    # the prefix is stripped by parameter expansion: the store path is never part of a program text
    find "$s" -name '*.tga' 2>/dev/null | while IFS= read -r line; do echo "${line#"$s"/}"; done |
        awk -F/ '{print $1"/"$2"/"$3}' | sort | uniq -c
}

# ---- selftest -------------------------------------------------------------

# fake_engine <path> <renderer to report or "-"> <final command>: copies a.tga
# as one frame with its manifest line into the pins_dir it is given (reporting the
# vid_renderer it was given, or the renderer named), then runs the command
fake_engine() {
    printf '%s\n' '#!/bin/bash' \
        'while [ $# -gt 0 ]; do case $1 in pins_dir) d=$2;; vid_renderer) r=$2;; pins_scenario) s=$2;; esac; shift; done' \
        "[ \"$2\" = - ] || r=$2" \
        "cp '$tmp/a.tga' \"\$d/\$s-000000.tga\"" 'echo "$s-000000.tga $r 64x48" >> "$d/manifest.txt"' \
        "$3" > "$1"
    chmod +x "$1"
}

# st_fail <message>: a selftest case failed
st_fail() { echo "  FAIL: $*" >&2; return 1; }
# fixture <renderer> <dir id> <tga>...: a complete pin directory with the given frames as world-00000N.tga
fixture() {
    local d="$PINS_STORE/world/$1/$2" i=0; shift 2
    rm -rf "$d"; mkdir -p "$d"
    for f in "$@"; do cp "$f" "$d/world-00000$i.tga"; i=$((i+1)); done
    touch "$d/complete"
}

st_framediff() {
    "$diff" "$tmp/a.tga" "$tmp/a.tga" | grep -q '^score=0.000000' || st_fail "identical frames must score 0" || return 1
    "$diff" "$tmp/a.tga" "$tmp/b.tga" --threshold 0.02 >/dev/null || st_fail "a 3/255 shift must pass at 0.02" || return 1
    if "$diff" "$tmp/a.tga" "$tmp/c.tga" --threshold 0.02 >/dev/null; then st_fail "a 60/255 shift must fail at 0.02"; return 1; fi
    if "$diff" "$tmp/a.tga" "$tmp/d.tga" >/dev/null 2>&1; then st_fail "size mismatch must fail"; return 1; fi
}
st_framediff_block() { # C-I3: a 77x77 block of 255 in 640x480 keeps the mean low but must fail
    if "$diff" "$tmp/big.tga" "$tmp/bigblock.tga" >/dev/null; then st_fail "a 77x77 defect in 640x480 must fail at the default thresholds"; return 1; fi
    "$diff" "$tmp/big.tga" "$tmp/bigblock.tga" --bad-threshold 0.05 >/dev/null || st_fail "the 77x77 defect must pass with --bad-threshold 0.05 (the mean is under 0.02)" || return 1
}
st_compare_basic() {
    fixture gl1 ref "$tmp/a.tga"; fixture metal "$cid" "$tmp/b.tga"
    cmd_compare metal world --ref gl1 >/dev/null || st_fail "compare must pass on a near-identical frame" || return 1
    local rep; rep=$(cmd_compare metal world --ref gl1)
    case $rep in *"ref=$PINS_STORE/world/gl1/ref"*) ;; *) st_fail "compare must print the chosen reference path"; return 1;; esac
    case $rep in *"threshold=0.02 bad_threshold=0.005"*) ;; *) st_fail "compare must print both thresholds: $rep"; return 1;; esac
    fixture metal zzzz-1 "$tmp/c.tga"
    cmd_compare metal world --ref gl1 >/dev/null || st_fail "compare must use the candidate of this engine and scenario, not the lexically last directory" || return 1
    fixture metal "$cid" "$tmp/c.tga"
    if cmd_compare metal world --ref gl1 >/dev/null; then st_fail "compare must fail on a far frame"; return 1; fi
    rm -rf "$PINS_STORE/world/metal/$cid"
    if (cmd_compare metal world --ref gl1) >/dev/null 2>&1; then st_fail "compare must fail when this engine has no recorded candidate"; return 1; fi
    fixture metal "$cid" "$tmp/a.tga"
    rep=$(cmd_compare metal world --ref gl1)
    case $rep in *"worst=0 "*) ;; *) st_fail "compare must print worst=0 when every score is 0: $rep"; return 1;; esac
    case $rep in *"world-000000.tga"*"0.000000 "*" ok"*) ;; *) st_fail "score column must hold the number: $rep"; return 1;; esac
    case $rep in *"max="*frame*|*"score=0.000000 max"*) st_fail "score column carries the comparator line"; return 1;; esac
}
st_compare_bad_threshold() { # C-I3: compare gates on the bad fraction and passes --bad-threshold through
    fixture gl1 ref "$tmp/big.tga"; fixture metal "$cid" "$tmp/bigblock.tga"
    if cmd_compare metal world --ref gl1 >/dev/null; then st_fail "compare must fail on a large localized defect"; return 1; fi
    local rep; rep=$(cmd_compare metal world --ref gl1 --bad-threshold 0.05) || st_fail "--bad-threshold 0.05 must reach the comparator" || return 1
    case $rep in *"bad_threshold=0.05"*) ;; *) st_fail "compare must print the bad threshold it used: $rep"; return 1;; esac
}
st_ref_mtime() { # C-I2a: the mtime-newest reference sorts neither first nor last
    fixture metal "$cid" "$tmp/a.tga"
    fixture gl1 aaa-old "$tmp/a.tga"; fixture gl1 mmm-new "$tmp/c.tga"; fixture gl1 zzz-old "$tmp/a.tga"
    touch -t 202001010000 "$PINS_STORE/world/gl1/aaa-old" "$PINS_STORE/world/gl1/zzz-old"; touch -t 202101010000 "$PINS_STORE/world/gl1/mmm-new"
    local rep; rep=$(cmd_compare metal world --ref gl1) && { st_fail "compare must default to the most recently modified reference (mmm-new, far)"; return 1; }
    case $rep in *"ref=$PINS_STORE/world/gl1/mmm-new"*) ;; *) st_fail "default reference must be mmm-new: $rep"; return 1;; esac
    cmd_compare metal world --ref gl1 --ref-id aaa-old >/dev/null || st_fail "--ref-id must select that reference" || return 1
    if (cmd_compare metal world --ref gl1 --ref-id nosuch) >/dev/null 2>&1; then st_fail "an unknown --ref-id must fail"; return 1; fi
}
st_missing_frame() { # C-I2b
    fixture gl1 ref "$tmp/a.tga" "$tmp/a.tga"; fixture metal "$cid" "$tmp/a.tga" "$tmp/a.tga"
    cmd_compare metal world --ref gl1 --ref-id ref >/dev/null || st_fail "equal two-frame pins must pass" || return 1
    mv "$PINS_STORE/world/metal/$cid/world-000001.tga" "$PINS_STORE/world/metal/$cid/world-000009.tga"
    local rep; rep=$(cmd_compare metal world --ref gl1 --ref-id ref) && { st_fail "a reference frame missing from the candidate must fail"; return 1; }
    case $rep in *"world-000001.tga"*MISSING*) ;; *) st_fail "the missing frame must be reported: $rep"; return 1;; esac
}
st_self_compare() { # C-B1: the candidate is never its own reference
    rm -rf "$PINS_STORE/world/soft"; fixture soft "$cid" "$tmp/a.tga"
    if (cmd_compare soft world --ref soft) >/dev/null 2>&1; then st_fail "compare with only the candidate recorded must not pass against itself"; return 1; fi
    if (cmd_compare soft world --ref soft --ref-id "$cid") >/dev/null 2>&1; then st_fail "--ref-id naming the candidate must fail"; return 1; fi
    fixture soft "$cid-r2" "$tmp/c.tga"; touch -t 203001010000 "$PINS_STORE/world/soft/$cid-r2"
    local rep; rep=$(cmd_compare soft world --ref soft) && { st_fail "the newest other run (far) must be the default reference"; return 1; }
    case $rep in *"ref=$PINS_STORE/world/soft/$cid-r2"*) ;; *) st_fail "default reference must skip the candidate: $rep"; return 1;; esac
}
st_two_runs() { # C-B1: two consecutive records coexist and compare
    rm -rf "$PINS_STORE/world/gl1"
    PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-ok" cmd_record gl1 world >/dev/null || st_fail "first record must pass" || return 1
    PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-ok" cmd_record gl1 world --run 2 >/dev/null || st_fail "record --run 2 must pass" || return 1
    [ -f "$PINS_STORE/world/gl1/$cid/complete" ] && [ -f "$PINS_STORE/world/gl1/$cid-r2/complete" ] || st_fail "both runs must be kept, complete" || return 1
    local rep; rep=$(cmd_compare gl1 world --run 2 --ref gl1) || st_fail "run 2 against run 1 must pass: $rep" || return 1
    case $rep in *"ref=$PINS_STORE/world/gl1/$cid"$'\n'*"candidate=$PINS_STORE/world/gl1/$cid-r2"*) ;; *) st_fail "compare --run 2 must take run 1 as reference: $rep"; return 1;; esac
    if (cmd_record gl1 world --run 0) >/dev/null 2>&1; then st_fail "--run 0 must be refused"; return 1; fi
}
st_incomplete() { # C-I1
    fixture gl1 ref "$tmp/a.tga"; fixture metal "$cid" "$tmp/a.tga"
    rm "$PINS_STORE/world/gl1/ref/complete"
    if (cmd_compare metal world --ref gl1 --ref-id ref) >/dev/null 2>&1; then st_fail "an incomplete reference must be refused"; return 1; fi
    rm -rf "$PINS_STORE/world/gl1"/*; fixture gl1 ref "$tmp/a.tga"
    rm "$PINS_STORE/world/metal/$cid/complete"
    if (cmd_compare metal world --ref gl1) >/dev/null 2>&1; then st_fail "an incomplete candidate must be refused"; return 1; fi
    fixture metal "$cid" "$tmp/a.tga" "$tmp/a.tga"
    local rep; rep=$(cmd_compare metal world --ref gl1) && { st_fail "a candidate with more frames than the reference must fail"; return 1; }
    case $rep in *"frame count differs"*) ;; *) st_fail "the frame count mismatch must be printed: $rep"; return 1;; esac
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-crash" cmd_record gl1 world) >/dev/null 2>&1; then st_fail "record must fail when the engine crashes after a frame"; return 1; fi
    local crash; crash=$(PINS_BIN="$tmp/fake-crash" run_id world 1)
    [ ! -f "$PINS_STORE/world/gl1/$crash/complete" ] && [ -f "$PINS_STORE/world/gl1/$crash/incomplete" ] || st_fail "a failed record must leave 'incomplete' and no 'complete'" || return 1
}
st_record_engine() {
    PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-ok" cmd_record gl1 world >/dev/null || st_fail "record must pass with an engine that exits 0" || return 1
    local rep; rep=$( (PINS_BIN="$tmp/no-such-engine" cmd_record gl1 world) 2>&1 ) && { st_fail "record must refuse without an engine binary"; return 1; }
    case $rep in "pins: no engine binary at $tmp/no-such-engine"*) ;; *) st_fail "a missing PINS_BIN must give the clean error, got: $rep"; return 1;; esac
    rep=$( (PINS_BIN="$tmp/no-such-engine" PINS_DRYRUN=1 cmd_record gl1 world) 2>&1 ) && { st_fail "a dry run must refuse a missing PINS_BIN too"; return 1; }
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-ok" PINS_TIMEOUT=abc cmd_record gl1 world) >/dev/null 2>&1; then st_fail "a non-numeric PINS_TIMEOUT must be refused"; return 1; fi
}
st_timeout() { # C-I2c: fake-hang exits 0 on TERM, like the engine; only the timeout flag can fail the record
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-hang" PINS_TIMEOUT=2 cmd_record gl1 world) >/dev/null 2>&1; then st_fail "record must fail when the engine hangs until PINS_TIMEOUT"; return 1; fi
}
st_frame_failed() { # R3-N3: a failed frame write is a FAILED manifest line; record must not complete
    fake_engine "$tmp/fake-failed" - 'echo "FAILED $s-000001.tga" >> "$d/manifest.txt"; exit 0'
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-failed" cmd_record gl1 world) >/dev/null 2>&1; then st_fail "record must fail when manifest.txt holds a FAILED line"; return 1; fi
    local fid; fid=$(PINS_BIN="$tmp/fake-failed" run_id world 1)
    [ ! -f "$PINS_STORE/world/gl1/$fid/complete" ] && grep -q 'FAILED' "$PINS_STORE/world/gl1/$fid/incomplete" || st_fail "the pin must be marked incomplete, naming the failed frame" || return 1
    # the manifest itself unwritable: the frame exists but no line was appended
    fake_engine "$tmp/fake-nomanifest" - 'rm -f "$d/manifest.txt"; exit 0'
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-nomanifest" cmd_record gl1 world) >/dev/null 2>&1; then st_fail "record must fail when the manifest is missing"; return 1; fi
    # a frame of two where the manifest line of the first is missing: the counts disagree
    fake_engine "$tmp/fake-shifted" - 'cp "$d/$s-000000.tga" "$d/$s-000001.tga"; exit 0'
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-shifted" cmd_record gl1 world) >/dev/null 2>&1; then st_fail "record must fail when a frame has no manifest line"; return 1; fi
}
st_list() { # R3-N2: the store path is data, whatever characters it holds
    local d="$tmp/s|a&b"; mkdir -p "$d/world/gl1/x"; touch "$d/world/gl1/x/a.tga" "$d/world/gl1/x/b.tga"
    local rep; rep=$(PINS_STORE="$d" cmd_list 2>&1) || st_fail "list must work with a store path holding | and &: $rep" || return 1
    case $rep in *"2 world/gl1/x"*) ;; *) st_fail "list must count the frames under the store: $rep"; return 1;; esac
}
st_fallback() { # C-B2
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-other" cmd_record gl1 world) >/dev/null 2>&1; then st_fail "record must fail when the frames were rendered by another renderer"; return 1; fi
}
st_names() { # C-I4
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-ok" cmd_record ../evil world) >/dev/null 2>&1; then st_fail "a renderer with .. must be refused"; return 1; fi
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-ok" cmd_record gl1 ../world) >/dev/null 2>&1; then st_fail "a scenario with .. must be refused"; return 1; fi
    if (cmd_compare 'a/b' world) >/dev/null 2>&1; then st_fail "compare must refuse a renderer with a slash"; return 1; fi
    if (cmd_compare -x world) >/dev/null 2>&1; then st_fail "a leading - must be refused"; return 1; fi
    (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-shim" cmd_record gl1-metal world) >/dev/null 2>&1 || st_fail "renderer gl1-metal must be accepted" || return 1
    fixture gl1 ref "$tmp/a.tga"; fixture gl1-metal "$cid" "$tmp/a.tga"
    (cmd_compare gl1-metal world --ref gl1) >/dev/null 2>&1 || st_fail "compare gl1-metal world --ref gl1 must parse and pass" || return 1
    (check_name scenario rt-ao) || st_fail "scenario rt-ao must be accepted" || return 1
    if (check_name scenario World) >/dev/null 2>&1; then st_fail "an upper-case name must be refused"; return 1; fi
}
st_cmdline() { # C-I5: what the engine's command line cannot carry is refused
    local p
    for p in "$tmp/a b" "$tmp/a;quit" "$tmp/a\"b" "$tmp/a+quit" "$tmp/a\$b"; do
        if (PINS_STORE="$p" PINS_DRYRUN=1 cmd_record gl1 world) >/dev/null 2>&1; then st_fail "store path '$p' must be refused"; return 1; fi
    done
    if (PINS_STORE="$tmp/x/../y" PINS_DRYRUN=1 cmd_record gl1 world) >/dev/null 2>&1; then st_fail "store path with .. must be refused"; return 1; fi
    if (PINS_BASEDIR="$tmp/g+quit" PINS_DRYRUN=1 cmd_record gl1 world) >/dev/null 2>&1; then st_fail "PINS_BASEDIR with + must be refused"; return 1; fi
    if (PINS_DEMO='demo1.dm2;quit' PINS_DRYRUN=1 cmd_record gl1 world) >/dev/null 2>&1; then st_fail "PINS_DEMO with ; must be refused"; return 1; fi
    if (PINS_EVERY=x PINS_DRYRUN=1 cmd_record gl1 world) >/dev/null 2>&1; then st_fail "PINS_EVERY=x must be refused"; return 1; fi
    (PINS_STORE="$tmp/ok.dir/s-1_2" PINS_DRYRUN=1 cmd_record gl1 world) >/dev/null 2>&1 || st_fail "a plain store path must be accepted" || return 1
}
st_thresholds() { # C-N1
    fixture gl1 ref "$tmp/a.tga"; fixture metal "$cid" "$tmp/a.tga"
    local t
    for t in abc -1 1e3 '' 0.1.2 0x1; do
        if (cmd_compare metal world --ref gl1 --threshold "$t") >/dev/null 2>&1; then st_fail "--threshold '$t' must be refused"; return 1; fi
        if (cmd_compare metal world --ref gl1 --bad-threshold "$t") >/dev/null 2>&1; then st_fail "--bad-threshold '$t' must be refused"; return 1; fi
    done
    for t in 0 0.02 1 .5 2.; do
        (cmd_compare metal world --ref gl1 --threshold "$t" --bad-threshold "$t") >/dev/null 2>&1 || st_fail "threshold '$t' must be accepted" || return 1
    done
}
st_scenarios() {
    # every scenario: timedemo on, no demo line of its own (the driver adds
    # +demomap), and every cvar it sets exists in this source tree
    local s name
    for s in "$scen_dir"/*.cfg; do
        grep -q '^set timedemo 1$' "$s" || st_fail "$(basename "$s") lacks 'set timedemo 1'" || return 1
        if grep -q -E '^(demo|demomap) ' "$s"; then st_fail "$(basename "$s") names a demo; the driver adds +demomap"; return 1; fi
        if grep -v -E '^(//|$)' "$s" | grep -q -v -E '^set [a-z0-9_]+ [A-Za-z0-9_.-]+$'; then st_fail "$(basename "$s") has a line that is not 'set <cvar> <value>'"; return 1; fi
        for name in $(sed -n 's/^set \([a-z0-9_]*\) .*/\1/p' "$s"); do
            grep -rqF "\"$name\"" "$root/src" || st_fail "$(basename "$s") sets $name, which is not a cvar in src/" || return 1
        done
    done
    # the commands, cvars and pin arguments the driver itself passes exist in this tree
    grep -qF 'Cmd_AddCommand("demomap"' "$root/src/server/sv_cmd.c" || st_fail "demomap is not a command in src/server/sv_cmd.c" || return 1
    for name in vid_renderer vid_fullscreen r_mode r_customwidth r_customheight s_initsound; do
        grep -rqF "\"$name\"" "$root/src" || st_fail "the driver sets $name, which is not a cvar in src/" || return 1
    done
    for name in pins_dir pins_scenario pins_capture pins_record; do
        grep -qF "\"$name\"" "$root/src/client/cl_pins.c" || st_fail "the driver passes $name, which CL_PinsLatch in src/client/cl_pins.c does not read" || return 1
    done
    local line; line=$(PINS_DRYRUN=1 cmd_record gl1 world)
    for name in '-portable' '+set r_customwidth 640' '+set pins_capture 30' '+set pins_record trace.q2r' '+set cl_entities 0' '+set timedemo 1' '+demomap demo1.dm2'; do
        case $line in *"$name"*) ;; *) st_fail "record command line lacks: $name"; return 1;; esac
    done
    # the trace lands in the pin directory beside the frames, and PINS_TRACE=0 records none
    case $line in *"+set pins_dir $PINS_STORE/world/gl1/"*" +set pins_record trace.q2r +demomap "*) ;; *) st_fail "the trace must be pins_record inside pins_dir: $line"; return 1;; esac
    line=$(PINS_TRACE=0 PINS_DRYRUN=1 cmd_record gl1 world)
    case $line in *pins_record*) st_fail "PINS_TRACE=0 must not pass pins_record"; return 1;; esac
    [ "$(HOME=/h/x PINS_STORE= store)" = "/h/x/.quake2-pins" ] || st_fail "store default must be \$HOME/.quake2-pins, whatever .claude/dev-loop.json holds" || return 1
}

st_argcount() { # R3-B1: the engine refuses an argv longer than MAX_NUM_ARGVS (src/common/argproc.c)
    local limit s line n worst=0 d
    limit=$(sed -n 's/^#define MAX_NUM_ARGVS \([0-9]*\).*/\1/p' "$root/src/common/argproc.c")
    case $limit in ''|*[!0-9]*) st_fail "cannot read MAX_NUM_ARGVS from src/common/argproc.c"; return 1;; esac
    for s in "$scen_dir"/*.cfg; do
        d=demo1.dm2; [ "$(basename "$s" .cfg)" != cinematic ] || d=idlog.cin
        line=$(PINS_DEMO=$d PINS_BASEDIR="$tmp/gd" PINS_DRYRUN=1 cmd_record gl1 "$(basename "$s" .cfg)") || st_fail "dry run failed for $(basename "$s")" || return 1
        set -f; set -- $line; set +f
        n=$#   # the binary is argv[0], the rest are the arguments
        [ "$n" -gt "$worst" ] && worst=$n
        [ "$n" -le "$limit" ] || st_fail "$(basename "$s"): the engine command line has $n words, MAX_NUM_ARGVS is $limit" || return 1
    done
    echo "       argc: worst scenario $worst of $limit, headroom $((limit - worst))"
}

st_scenario_lines() { # TASK39 (b, c): only "set <cvar> <value>", never a driver-owned cvar
    mkdir -p "$tmp/scen"; local scen_dir="$tmp/scen" bad
    printf '%s\n' '// ok' 'set timedemo 1' 'set cl_gun 0' > "$tmp/scen/good.cfg"
    local line; line=$(PINS_DRYRUN=1 cmd_record gl1 good) || st_fail "a plain scenario must pass" || return 1
    case $line in *"+set cl_gun 0 "*"+set pins_dir "*"+set pins_capture 30 +set pins_record trace.q2r +demomap demo1.dm2") ;; *) st_fail "the driver's own arguments must follow the scenario's: $line"; return 1;; esac
    printf '%s\n' 'set timedemo 1' 'set cl_gun -1' 'set cl_gun -0.5' 'set cl_gun -.5' > "$tmp/scen/neg.cfg"
    PINS_DRYRUN=1 cmd_record gl1 neg >/dev/null || st_fail "negative numbers must be accepted" || return 1
    for bad in 'set pins_capture 1' 'set pins_dir /tmp/x' 'set vid_renderer gl1' 'set r_customwidth 9' 'set s_initsound 1' \
        'set basedir /opt/x' 'set cddir /opt/x' 'set game mymod' 'set datadir /opt/x' 'set cfgdir /opt/x' \
        'set timedemo ../x' 'set timedemo a..b' 'set timedemo -datadir' 'set timedemo -cfgdir' 'set timedemo -b' 'set timedemo -1x' 'set timedemo -' \
        'exec evil.cfg' 'quit' '+set timedemo 1' 'set timedemo' 'set timedemo 1 2' 'set TimeDemo 1' 'set time-demo 1' ' set timedemo 1'; do
        printf '%s\n' 'set timedemo 1' "$bad" > "$tmp/scen/bad.cfg"
        if (PINS_DRYRUN=1 cmd_record gl1 bad) >/dev/null 2>&1; then st_fail "scenario line '$bad' must be refused"; return 1; fi
    done
}
st_store_place() { # TASK39 (d): the store is never inside this repository or another work tree
    if (PINS_STORE="$root/pins-store" PINS_DRYRUN=1 cmd_record gl1 world) >/dev/null 2>&1; then st_fail "a store inside the repository must be refused"; return 1; fi
    if (PINS_STORE="$root" PINS_DRYRUN=1 cmd_record gl1 world) >/dev/null 2>&1; then st_fail "the repository itself must be refused as the store"; return 1; fi
    ln -s "$root/scripts" "$tmp/into-repo"
    if (PINS_STORE="$tmp/into-repo/x" PINS_DRYRUN=1 cmd_record gl1 world) >/dev/null 2>&1; then st_fail "a store reached through a link into the repository must be refused"; return 1; fi
    local rep; rep=$( (PINS_STORE="$root/pins-store" cmd_compare metal world --ref gl1) 2>&1 ) && { st_fail "compare must refuse a store inside the repository"; return 1; }
    case $rep in *"inside this repository"*|*"inside a git work tree"*) ;; *) st_fail "compare must refuse the store for its place, got: $rep"; return 1;; esac
    mkdir -p "$tmp/lnkstore"; ln -s "$root/newstore" "$tmp/lnkstore/dangling"
    rep=$( (PINS_STORE="$tmp/lnkstore/dangling" PINS_DRYRUN=1 cmd_record gl1 world) 2>&1 ) && { st_fail "a store that is a dangling link must be refused"; return 1; }
    case $rep in *dangling*) ;; *) st_fail "the dangling link must be named, got: $rep"; return 1;; esac
    mkdir -p "$tmp/subst"; ln -s "$root/scripts" "$tmp/subst/world"
    rep=$( (PINS_STORE="$tmp/subst" PINS_DRYRUN=1 cmd_record soft world) 2>&1 ) && { st_fail "a scenario directory linked into the repository must be refused"; return 1; }
    case $rep in *"inside this repository"*) ;; *) st_fail "the linked scenario directory must be refused for its place, got: $rep"; return 1;; esac
    mkdir -p "$tmp/subst2/world"; ln -s "$root/scripts" "$tmp/subst2/world/soft"
    (PINS_STORE="$tmp/subst2" PINS_DRYRUN=1 cmd_record soft world) >/dev/null 2>&1 && { st_fail "a renderer directory linked into the repository must be refused"; return 1; }
    if command -v git >/dev/null 2>&1; then
        git init -q "$tmp/other-repo"
        if (PINS_STORE="$tmp/other-repo/a/b" PINS_DRYRUN=1 cmd_record gl1 world) >/dev/null 2>&1; then st_fail "a store inside another git work tree must be refused"; return 1; fi
    fi
    (PINS_STORE="$tmp/not-yet/made" PINS_DRYRUN=1 cmd_record gl1 world) >/dev/null 2>&1 || st_fail "a store outside any repository must be accepted" || return 1
}
st_fast_fail() { # TASK39 (f): an engine that exits non-zero at once (a demo it cannot open) fails the record at once
    local t0=$SECONDS
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-drop" PINS_TIMEOUT=60 cmd_record gl1 world) >/dev/null 2>&1; then st_fail "record must fail when the engine exits 1"; return 1; fi
    [ $((SECONDS - t0)) -lt 10 ] || st_fail "record must not wait for PINS_TIMEOUT when the engine exits at once" || return 1
    grep -q 'status 1' "$PINS_STORE/world/gl1/$(PINS_BIN="$tmp/fake-drop" run_id world 1)/incomplete" || st_fail "the reason must name the exit status" || return 1
}
st_demo_name() { # TASK39 (g): PINS_DEMO is a demo file name the engine keeps
    local d
    for d in -x.dm2 -demo1.dm2 .hidden.dm2 a/demo1.dm2 ../demo1.dm2 demo1 demo1.dm2x 'demo 1.dm2' 'x..dm2' .dm2 \
        .cin -idlog.cin video/idlog.cin ../idlog.cin idlog.cinx 'x..cin' idlog.pcx 'idlog.cin;quit'; do
        if (PINS_DEMO="$d" PINS_DRYRUN=1 cmd_record gl1 world) >/dev/null 2>&1; then st_fail "PINS_DEMO '$d' must be refused"; return 1; fi
    done
    for d in demo2.dm2 my_demo-1.dm2 X.dm2; do
        (PINS_DEMO="$d" PINS_DRYRUN=1 cmd_record gl1 world) >/dev/null 2>&1 || st_fail "PINS_DEMO '$d' must be accepted" || return 1
    done
    for d in idlog.cin eou1_.cin; do
        (PINS_DEMO="$d" PINS_DRYRUN=1 cmd_record gl1 cinematic) >/dev/null 2>&1 || st_fail "PINS_DEMO '$d' must be accepted for cinematic" || return 1
        if (PINS_DEMO="$d" PINS_DRYRUN=1 cmd_record gl1 world) >/dev/null 2>&1; then st_fail "a cinematic '$d' must be refused for world"; return 1; fi
    done
    # the cinematic scenario needs a .cin: the default demo and a named demo are refused
    if (PINS_DRYRUN=1 cmd_record gl1 cinematic) >/dev/null 2>&1; then st_fail "cinematic with the default demo must be refused"; return 1; fi
    if (PINS_DEMO=demo2.dm2 PINS_DRYRUN=1 cmd_record gl1 cinematic) >/dev/null 2>&1; then st_fail "cinematic with a .dm2 must be refused"; return 1; fi
    # a cinematic is played the same way as a demo: +demomap, last on the line
    local line; line=$(PINS_DEMO=idlog.cin PINS_DRYRUN=1 cmd_record gl1 cinematic) || st_fail "a dry run of the cinematic scenario must pass" || return 1
    case $line in *" +demomap idlog.cin") ;; *) st_fail "PINS_DEMO=idlog.cin must reach +demomap: $line"; return 1;; esac
}
st_replace() { # TASK39 (h): a complete pin is never replaced silently
    PINS_BASEDIR="$tmp/gd" cmd_record gl1 world >/dev/null || st_fail "first record must pass" || return 1
    echo keep > "$PINS_STORE/world/gl1/$cid/marker"
    if (PINS_BASEDIR="$tmp/gd" cmd_record gl1 world) >/dev/null 2>&1; then st_fail "recording the same id over a complete pin must be refused"; return 1; fi
    [ -f "$PINS_STORE/world/gl1/$cid/marker" ] || st_fail "the refused record must leave the pin untouched" || return 1
    PINS_BASEDIR="$tmp/gd" cmd_record gl1 world --replace >/dev/null || st_fail "--replace must overwrite the pin" || return 1
    [ ! -f "$PINS_STORE/world/gl1/$cid/marker" ] && [ -f "$PINS_STORE/world/gl1/$cid/complete" ] || st_fail "--replace must leave a fresh complete pin" || return 1
    rm "$PINS_STORE/world/gl1/$cid/complete"; echo failed > "$PINS_STORE/world/gl1/$cid/incomplete"
    PINS_BASEDIR="$tmp/gd" cmd_record gl1 world >/dev/null || st_fail "an incomplete pin must be re-recorded without --replace" || return 1
}
st_engine_id() { # TASK39 (h): the id sees uncommitted source, untracked source and another binary
    command -v git >/dev/null 2>&1 || { echo "       engine_id: no git, skipped"; return 0; }
    local r="$tmp/tree" a b c d
    mkdir -p "$r/src"; printf 'int a;\n' > "$r/src/a.c"; printf 'all:\n' > "$r/Makefile"
    git -C "$r" init -q; git -C "$r" add src/a.c Makefile
    git -C "$r" -c user.name=pins -c user.email=pins@example.invalid commit -q -m t
    a=$(root="$r" PINS_BIN= engine_sha)
    case $a in *-*) st_fail "a clean tree's id must be the bare commit: $a"; return 1;; esac
    printf 'int b;\n' > "$r/src/a.c"; b=$(root="$r" PINS_BIN= engine_sha)
    case $b in "$a"-d*) ;; *) st_fail "a modified source file must add -d<hash>: $a -> $b"; return 1;; esac
    printf 'int a;\n' > "$r/src/a.c"; printf 'int n;\n' > "$r/src/new.c"; c=$(root="$r" PINS_BIN= engine_sha)
    case $c in "$a"-d*) [ "$c" != "$b" ] || st_fail "different changes must give different ids" || return 1;; *) st_fail "an untracked source file must add -d<hash>: $c"; return 1;; esac
    rm "$r/src/new.c"; [ "$(root="$r" PINS_BIN= engine_sha)" = "$a" ] || st_fail "a reverted tree must give the commit's id again" || return 1
    d=$(root="$r" PINS_BIN="$tmp/fake-ok" engine_sha)
    case $d in "$a"-b*) ;; *) st_fail "PINS_BIN must add -b<hash>: $d"; return 1;; esac
    [ "$d" != "$(root="$r" PINS_BIN="$tmp/fake-crash" engine_sha)" ] || st_fail "two binaries must give two ids" || return 1
    command -v make >/dev/null 2>&1 || { echo "       stale build: no make, skipped"; return 0; }
    local m="$tmp/mk" rep
    mkdir -p "$m/src"; printf 'int a;\n' > "$m/src/a.c"; printf 'int r;\n' > "$m/src/r.c"
    printf 'quake2: src/a.c\n\ttouch quake2\nref_gl1.so: src/r.c\n\ttouch ref_gl1.so\n' > "$m/Makefile"
    printf 'x' > "$m/quake2"; printf 'x' > "$m/ref_gl1.so"
    touch -t 202001010000 "$m/quake2" "$m/ref_gl1.so"
    [ -n "$(root="$m" stale_engine "$m/quake2" gl1)" ] || st_fail "a binary older than its sources must be stale" || return 1
    touch -t 203001010000 "$m/quake2"
    [ -n "$(root="$m" stale_engine "$m/quake2" gl1)" ] || st_fail "a renderer module older than its sources must be stale" || return 1
    touch -t 203001010000 "$m/ref_gl1.so"
    [ -z "$(root="$m" stale_engine "$m/quake2" gl1)" ] || st_fail "a current build must not be stale" || return 1
    touch -t 202501010000 "$m/src/a.c"; touch -t 202601010000 "$m/ref_gl1.so"; touch -t 202801010000 "$m/src/r.c" "$m/quake2"
    [ -n "$(root="$m" stale_engine "$m/quake2" gl1)" ] || st_fail "the module's own source newer than the module must be stale" || return 1
    # a header the Makefile does not list: make -q calls the build current, the header test must not
    touch -t 202501010000 "$m/src/a.c" "$m/src/r.c"; touch -t 203001010000 "$m/quake2" "$m/ref_gl1.so"
    printf 'int h;\n' > "$m/src/h.h"; touch -t 202501010000 "$m/src/h.h"
    [ -z "$(root="$m" stale_engine "$m/quake2" gl1)" ] || st_fail "a build newer than every source and header must not be stale" || return 1
    touch -t 203501010000 "$m/src/h.h"
    [ -n "$(root="$m" stale_engine "$m/quake2" gl1)" ] || st_fail "a binary older than a header under src/ must be stale" || return 1
    # record reaches that check when PINS_BIN is not set (root points at the toy tree)
    mkdir -p "$m/release"; printf 'release/quake2: src/a.c\n\ttouch release/quake2\n' > "$m/Makefile"
    fake_engine "$m/release/quake2" - 'exit 0'; touch -t 203001010000 "$m/release/quake2"
    rep=$( (root="$m" PINS_BIN= PINS_BASEDIR="$tmp/gd" cmd_record gl1 world) 2>&1 ) && { st_fail "record must refuse a binary older than a header"; return 1; }
    case $rep in *"the build is out of date"*"older than a header"*) ;; *) st_fail "record must report the stale build, got: $rep"; return 1;; esac
    touch -t 203601010000 "$m/release/quake2"
    (root="$m" PINS_BIN= PINS_BASEDIR="$tmp/gd" cmd_record gl1 world) >/dev/null 2>&1 || st_fail "record must pass with a build newer than every header" || return 1
}
st_metal_shim() { # TASK47: gl1-metal is ref_gl1 built with WITH_METAL_SHIM=yes
    local rep id m="$tmp/mkm"
    rep=$(PINS_DRYRUN=1 cmd_record gl1-metal world) || st_fail "a dry run of gl1-metal must work" || return 1
    case $rep in *"+set vid_renderer gl1 "*) ;; *) st_fail "gl1-metal must start the engine with vid_renderer gl1: $rep"; return 1;; esac
    (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-shim" cmd_record gl1-metal world) >/dev/null 2>&1 || st_fail "gl1-metal must record from an engine whose ref_gl1 is the shim build" || return 1
    id=$(PINS_BIN="$tmp/fake-shim" run_id world 1)
    [ -f "$PINS_STORE/world/gl1-metal/$id/complete" ] || st_fail "the gl1-metal pin must land under world/gl1-metal" || return 1
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-ok" cmd_record gl1-metal world --run 2) >/dev/null 2>&1; then st_fail "gl1-metal must refuse frames from a ref_gl1 without the shim"; return 1; fi
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-shim" cmd_record gl1 world) >/dev/null 2>&1; then st_fail "gl1 must refuse frames from the shim build"; return 1; fi
    # only the first GL_RENDERER line counts: a demo can print later lines of the same shape
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-spoof-shim" cmd_record gl1-metal world --run 3) >/dev/null 2>&1; then st_fail "gl1-metal must judge the first GL_RENDERER line, not a later one naming the shim"; return 1; fi
    (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-spoof-shim" cmd_record gl1 world --run 4) >/dev/null 2>&1 || st_fail "gl1 must record when only a later GL_RENDERER line names the shim" || return 1
    (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-spoof-gl" cmd_record gl1-metal world --run 5) >/dev/null 2>&1 || st_fail "gl1-metal must record when the first GL_RENDERER line names the shim and a later one does not" || return 1
    # the staleness check asks make about the shim build of the module
    command -v make >/dev/null 2>&1 || { echo "       metal_shim: no make, stale build skipped"; return 0; }
    mkdir -p "$m/src"; printf 'int a;\n' > "$m/src/old.c"; printf 'int b;\n' > "$m/src/new.c"
    printf 'ifeq ($(WITH_METAL_SHIM),yes)\nref_gl1.dylib: src/old.c\n\ttouch ref_gl1.dylib\nelse\nref_gl1.dylib: src/new.c\n\ttouch ref_gl1.dylib\nendif\n' > "$m/Makefile"
    printf 'x' > "$m/quake2"; printf 'x' > "$m/ref_gl1.dylib"
    touch -t 203001010000 "$m/quake2" "$m/ref_gl1.dylib"
    touch -t 203501010000 "$m/src/old.c"; touch -t 202001010000 "$m/src/new.c"
    [ -n "$(root="$m" stale_engine "$m/quake2" gl1-metal)" ] || st_fail "ref_gl1 older than a source of the shim build must be stale for gl1-metal" || return 1
    [ -z "$(root="$m" stale_engine "$m/quake2" gl1)" ] || st_fail "the same module must not be stale for gl1, whose sources are older" || return 1
    touch -t 202001010000 "$m/src/old.c"; touch -t 203501010000 "$m/src/new.c"
    [ -z "$(root="$m" stale_engine "$m/quake2" gl1-metal)" ] || st_fail "a shim build newer than the shim's sources must not be stale" || return 1
    [ -n "$(root="$m" stale_engine "$m/quake2" gl1)" ] || st_fail "the same module must be stale for gl1, whose build differs" || return 1
}
st_usage() { # the usage text is the whole header comment, its last line included
    local out last
    out=$(bash "$root/scripts/pins.sh" 2>/dev/null) && { st_fail "pins.sh without a command must exit non-zero"; return 1; }
    last=$(awk 'NR > 1 && !/^#/ { print prev; exit } { prev = $0 }' "$root/scripts/pins.sh" | sed 's/^# \{0,1\}//')
    [ -n "$last" ] || st_fail "the header comment has no last line" || return 1
    [ "$(printf '%s\n' "$out" | tail -n 1)" = "$last" ] || st_fail "the usage text must end with the header's last line: $last" || return 1
    [ "$(printf '%s\n' "$out" | head -n 1)" = "$(sed -n 2p "$root/scripts/pins.sh" | sed 's/^# \{0,1\}//')" ] || st_fail "the usage text must start with line 2 of the file" || return 1
}

cmd_selftest() {
    # the selftest must not depend on, or act on, the caller's environment
    unset PINS_BIN PINS_BASEDIR PINS_DEMO PINS_WIDTH PINS_HEIGHT PINS_EVERY PINS_TIMEOUT PINS_DRYRUN PINS_STORE PINS_DIFF PINS_TRACE
    # the scratch store must be somewhere check_store accepts: not inside a git
    # work tree (TMPDIR may be in one), so try TMPDIR, then /tmp, then /var/tmp
    local base t
    tmp=""
    for base in "${TMPDIR:-/tmp}" /tmp /var/tmp; do
        [ -d "$base" ] || continue
        t=$(mktemp -d "$base/pins.XXXXXX") || continue
        t=$(cd "$t" && pwd -P)
        if ! git -C "$t" rev-parse --is-inside-work-tree >/dev/null 2>&1; then tmp=$t; break; fi
        rm -rf "${t:?}"
    done
    [ -n "$tmp" ] || die "no scratch directory outside a git work tree under TMPDIR, /tmp or /var/tmp"
    trap 'rm -rf "$tmp"' EXIT
    export PINS_STORE="$tmp/store"
    diff=$(difftool)
    "$diff" --gen "$tmp/a.tga" 64 48 1 && "$diff" --perturb "$tmp/a.tga" "$tmp/b.tga" 3 && "$diff" --perturb "$tmp/a.tga" "$tmp/c.tga" 60
    "$diff" --gen "$tmp/d.tga" 32 32 1
    "$diff" --gen "$tmp/big.tga" 640 480 1
    "$diff" --block "$tmp/big.tga" "$tmp/bigblock.tga" 100 100 77 77 255 2>/dev/null || cp "$tmp/big.tga" "$tmp/bigblock.tga"
    mkdir -p "$tmp/gd/baseq2"
    fake_engine "$tmp/fake-ok" - 'exit 0'
    fake_engine "$tmp/fake-crash" - 'exit 139'
    fake_engine "$tmp/fake-other" soft 'exit 0'
    fake_engine "$tmp/fake-hang" - 'trap "exit 0" TERM; while :; do sleep 1; done'
    fake_engine "$tmp/fake-drop" - 'echo "Error: Couldn'"'"'t open demos/x.dm2"; exit 1'
    fake_engine "$tmp/fake-shim" - 'echo "GL_RENDERER: Metal shim on a test device (gl1-metal)"; exit 0'
    fake_engine "$tmp/fake-spoof-shim" - 'echo "GL_RENDERER: Test OpenGL Engine"; echo "GL_RENDERER: hello (gl1-metal)"; exit 0'
    fake_engine "$tmp/fake-spoof-gl" - 'echo "GL_RENDERER: Metal shim on a test device (gl1-metal)"; echo "GL_RENDERER: Test OpenGL Engine"; exit 0'
    export PINS_BIN="$tmp/fake-ok"
    cid=$(run_id world 1)
    local c failed=""
    for c in framediff framediff_block compare_basic compare_bad_threshold ref_mtime missing_frame self_compare \
        two_runs incomplete record_engine timeout fallback names cmdline thresholds scenarios argcount frame_failed list \
        scenario_lines store_place fast_fail demo_name replace engine_id metal_shim usage; do
        rm -rf "$PINS_STORE/world"
        if ( set -e; "st_$c" ); then echo "  ok   $c"; else echo "  FAIL $c"; failed="$failed $c"; fi
    done
    [ -z "$failed" ] || die "selftest failed:$failed"
    echo "pins selftest: ok"
}

case "${1:-}" in
    record)   [ $# -ge 3 ] || die "usage: pins.sh record <renderer> <scenario> [--run N] [--replace]"; shift; cmd_record "$@";;
    compare)  [ $# -ge 3 ] || die "usage: pins.sh compare <renderer> <scenario> [--ref r] [--ref-id d] [--run N | --cand-id d] [--threshold t] [--bad-threshold b]"; shift; cmd_compare "$@";;
    list)     cmd_list;;
    selftest) cmd_selftest;;
    *) usage; exit 1;;
esac
