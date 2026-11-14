#!/bin/bash
# scripts/pins.sh -- the renderer-API pin harness driver (TASK27; INFORM8/9).
# bash 3.2. Records reference frames from a renderer over a scenario, compares
# a candidate renderer against them, and self-tests without game data.
#
#   pins.sh record  <renderer> <scenario> [--run N] [--replace]
#   pins.sh compare <renderer> <scenario> [--ref <renderer>] [--ref-id <dir>]
#                   [--run N | --cand-id <dir>] [--threshold T] [--bad-threshold B]
#   pins.sh make-demo <name> [--replace]       make demos/<name>.dm2 beside the binary
#   pins.sh list                               what the store holds
#   pins.sh selftest                           no engine, no game data
#   pins.sh evidence <renderer> <scenario> [--ref r] [--ref-id d] [--run N | --cand-id d] [--card ID] | --synthetic [--card ID] [--noise BYTES]
#   pins.sh evidence-publish <manifest> [file...]   upload one evidence set to the pr-evidence pre-release
#   pins.sh evidence-selftest                  the evidence encoder and the publish refusals, offline
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
#   PINS_SEED     1 to 9 digits: the engine's pins_seed, a fixed PRNG seed
#                 applied at each map registration, so particles and
#                 explosions draw the same random numbers on every record.
#                 Needs the trace (not PINS_TRACE=0). Default: unset, the
#                 seed comes from OS entropy as in a normal client
#   PINS_EVIDENCE_DIR     where evidence writes its sets, never inside the
#                 pin store. Default: evidence/ of this checkout (git-ignored)
#   PINS_EVIDENCE_REPO    owner/name evidence-publish uploads to. Default: the
#                 GitHub repository of the origin remote
#   PINS_EVIDENCE_BUDGET  bytes per evidence file. Default: 4000000. A larger
#                 value raises the size cap: evidence writes it into the
#                 manifest's budget, and publish allows the smaller of the two
#   PINS_EVIDENCE_UPLOADER  a program run as "<it> <repo> <tag> -- <file>..."
#                 in place of the built-in gh upload, INCLUDING its check that
#                 the release is an existing pre-release: nothing is checked
#                 on the release then. It exists for the self-test's stand-in
#                 uploader; leave it unset to publish
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
#
# make-demo (TASK70) plays scripts/pins/demos/<name>.cfg, a console script,
# in a local single-player game (no network game is joined, pin capture is
# off, the soft renderer at PINS_WIDTH x PINS_HEIGHT) and leaves the demo it
# records as demos/<name>.dm2 in the engine's portable write directory,
# <binary dir>/baseq2, where PINS_DEMO=<name>.dm2 finds it. The script may
# use only commands that exist in src/, must record <name> once, hold one
# "// pins: messages <low> <high>" line and end with stop and quit. The
# write directory and its demos/, by their physical paths, must not be
# inside PINS_BASEDIR, nor inside this repository unless git ignores them
# (release/ is), and the demo, the script copy and the log must not be
# symbolic links. An existing demo is replaced only with --replace. The run
# fails at once when the script's map is not in the game data, and fails
# when the engine times out (PINS_TIMEOUT) or exits non-zero, or when the
# demo is missing, cut short, or holds a number of messages outside the
# script's range (renamed <name>.dm2.failed). A wait is a lower bound on
# time, so on a busy host each place is held longer, the demo holds more
# messages and the run fails; make it again. The script copy and pins_make_<name>.log
# are written into that directory too; the run also reads and rewrites
# config.cfg there, as any portable run without pin capture does. A demo
# beside the binary adds its checksum to the pin id of every scenario
# recorded from it, so pins of a demo made again never share an id.
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
# phys_dir <dir>: the physical path of an existing directory in the
# filesystem's own spelling. /bin/pwd -P asks the filesystem; the shell's
# pwd -P keeps the letter case it was given, which on a case-insensitive
# volume names one directory in several ways
phys_dir() { (cd "$1" && /bin/pwd -P); }
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
    phys_existing=$(phys_dir "$p")
    phys_result=$(printf '%s%s' "$phys_existing" "$rest" | tr -s /)
}
# check_store_path <path>: refuse a place inside this repository or any git
# work tree, and a dangling symbolic link (it could later be made into a
# directory inside one)
check_store_path() {
    local s r; phys_path "$1"; s=$phys_result; r=$(phys_dir "$root")
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
    { cat "$scen_dir/$1.cfg"; echo "${PINS_DEMO:-demo1.dm2} ${PINS_WIDTH:-640}x${PINS_HEIGHT:-480} every=${PINS_EVERY:-30}"; demo_sum
      if [ -n "${PINS_SEED:-}" ]; then echo "seed=$PINS_SEED"; fi; } | cksum | awk '{print $1}'
}
# demo_sum: the checksum of PINS_DEMO when it lies beside the engine binary,
# where make-demo writes it, so a demo made again never shares a pin id with
# the one it replaced; nothing for a demo found only in the game data
demo_sum() {
    local f; f="$(dirname "${PINS_BIN:-$root/release/quake2}")/baseq2/demos/${PINS_DEMO:-demo1.dm2}"
    if [ -f "$f" ]; then echo "demo $(cksum < "$f" | awk '{print $1, $2}')"; fi
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
    if [ -n "${PINS_SEED:-}" ]; then engine_args+=(+set pins_seed "$PINS_SEED"); fi
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
    if [ -n "${PINS_SEED:-}" ]; then
        [[ $PINS_SEED =~ ^[0-9]{1,9}$ ]] || die "PINS_SEED must be 1 to 9 digits, got '$PINS_SEED'"
        [ "${PINS_TRACE:-1}" != 0 ] || die "PINS_SEED needs the trace: the engine takes pins_seed only beside pins_record"
    fi
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

# ---- evidence (TASK73; docs/decisions/0007) ---------------------------------
# evidence writes, from frames already in the store, a side-by-side still of
# the worst-scoring frame, a short clip as animated PNG and GIF, and a
# manifest, into <evidence>/<card>-<commit>-<renderer>-<scenario>/, where
# <evidence> is PINS_EVIDENCE_DIR (default: evidence/ of this checkout,
# git-ignored) and <commit> the engine part of the candidate pin id. It writes
# nothing into the store. evidence-publish uploads the files of one such set,
# validated and staged by scripts/pins/evidence.py ("stage"), to the
# pre-release pr-evidence of PINS_EVIDENCE_REPO (default: the GitHub
# repository of the origin remote), then prints the Markdown for the
# pull-request body. PINS_EVIDENCE_UPLOADER names a program run as
# "<it> <repo> <tag> -- <file>..." instead of gh (the self-test's stand-in).
# PINS_EVIDENCE_BUDGET is the size budget per file in bytes (default 4000000).

evidence_root() { echo "${PINS_EVIDENCE_DIR:-$root/evidence}"; }
ev_py() {
    command -v python3 >/dev/null 2>&1 || die "evidence needs python3 (standard library only)"
    python3 -I "$root/scripts/pins/evidence.py" "$@"
}
check_card() { case $1 in ''|*[!A-Za-z0-9]*) die "--card must match [A-Za-z0-9]+, got '$1'";; esac; }
check_pin_id() { case $2 in ''|[.-]*|*[!A-Za-z0-9_.-]*) die "$1 must be a pin directory name [A-Za-z0-9_][A-Za-z0-9_.-]*, got '$2'";; esac; }
# ev_outdir <prefix>: a fresh <evidence>/<prefix>, never inside the pin store
ev_outdir() {
    local ev; ev=$(evidence_root)
    mkdir -p "$ev"
    phys_path "$ev"; local e=$phys_result; phys_path "$(store)"
    case "$e/" in "$phys_result/"*) die "the evidence folder $ev is inside the pin store; evidence writes nothing there";; esac
    [ ! -L "$ev/$1" ] || die "$ev/$1 is a symbolic link"
    rm -rf "${ev:?}/${1:?}"; mkdir -p "$ev/$1"
    echo "$ev/$1"
}
ev_commit() { git -C "$root" rev-parse --short=12 HEAD 2>/dev/null || echo nogit; }
# ev_cleanup_trap: on every exit of the subshell that runs it, die included,
# remove the scratch folder named by wd (never local: bash drops the locals
# before an EXIT trap runs), keeping the exit status
ev_cleanup_trap() { wd=""; trap 'ev_rc=$?; [ -z "$wd" ] || rm -rf "${wd:?}"; exit $ev_rc' EXIT; }

# cmd_evidence and cmd_evidence_publish run in a subshell of their own, so
# that their EXIT trap removes the scratch folder however they end
cmd_evidence() (
    local card=adhoc args="$*" out prefix
    ev_cleanup_trap
    if [ "${1:-}" = --synthetic ]; then
        local noise=""; shift
        while [ $# -gt 0 ]; do
            [ $# -ge 2 ] || die "$1 needs a value"
            case $1 in --card) card=$2; check_card "$card";; --noise) noise=$2; check_count --noise "$noise";; *) die "unknown option $1";; esac
            shift 2
        done
        prefix="$card-$(ev_commit)-synthetic-${noise:+noise$noise}"; [ -n "$noise" ] || prefix="${prefix}pattern"
        out=$(ev_outdir "$prefix") || exit 1
        ev_py synth --out "$out" --prefix "$prefix" ${noise:+--noise "$noise"} --meta "card=$card" --meta "commit=$(ev_commit)" \
            --meta "command=pins.sh evidence $args"
        echo "folder: ${out#"$root"/}"
        return
    fi
    [ $# -ge 2 ] || die "usage: pins.sh evidence <renderer> <scenario> [--ref r] [--ref-id d] [--run N | --cand-id d] [--card ID]"
    local renderer=$1 scenario=$2 ref=gl1; shift 2
    check_name renderer "$renderer"; check_name scenario "$scenario"
    local cmp_args=()
    while [ $# -gt 0 ]; do
        [ $# -ge 2 ] || die "$1 needs a value"
        case $1 in
            --card) card=$2; check_card "$card";;
            --ref) ref=$2; check_name reference "$ref"; cmp_args+=("$1" "$2");;
            --ref-id|--cand-id) check_pin_id "$1" "$2"; cmp_args+=("$1" "$2");;
            --run|--threshold|--bad-threshold) cmp_args+=("$1" "$2");;
            *) die "unknown option $1";;
        esac
        shift 2
    done
    # compare's own framediff would be built into the store; evidence writes nothing there
    local cmp rc=0 diffbin=${PINS_DIFF:-}
    wd=$(mktemp -d "${TMPDIR:-/tmp}/pins-evidence.XXXXXX"); cmp="$wd/compare.txt"
    if [ -z "$diffbin" ]; then
        diffbin="$wd/framediff"; cc -O2 -o "$diffbin" "$root/scripts/pins/framediff.c" -lm || { rm -rf "${wd:?}"; die "cannot build framediff"; }
    fi
    # ${a[@]+"${a[@]}"}: bash 3.2 under set -u calls an empty array unbound
    ( PINS_DIFF=$diffbin cmd_compare "$renderer" "$scenario" --ref "$ref" ${cmp_args[@]+"${cmp_args[@]}"} ) > "$cmp" || rc=$?
    local ref_dir cand
    ref_dir=$(sed -n 's/^ref=//p' "$cmp"); cand=$(sed -n 's/^candidate=//p' "$cmp")
    if [ -z "$ref_dir" ] || [ -z "$cand" ]; then rm -rf "${wd:?}"; die "compare found no pair of pins to show (see the message above)"; fi
    local cand_id=${cand##*/} ref_id=${ref_dir##*/}
    prefix="$card-${cand_id%%-*}-$renderer-$scenario"
    out=$(ev_outdir "$prefix") || exit 1
    ev_py build --ref-dir "$ref_dir" --cand-dir "$cand" --compare "$cmp" --out "$out" --prefix "$prefix" \
        --meta "card=$card" --meta "commit=$(ev_commit)" --meta "command=pins.sh evidence $args" \
        --meta "renderer=$renderer" --meta "scenario=$scenario" \
        --meta "reference=$scenario/$ref/$ref_id" --meta "candidate=$scenario/$renderer/$cand_id" \
        --meta "compare-exit=$rc" --meta "ref-caption=REFERENCE $ref" --meta "cand-caption=CANDIDATE $renderer" \
        || { rm -rf "${wd:?}"; die "the evidence step failed"; }
    rm -rf "${wd:?}"
    echo "folder: ${out#"$root"/}"
)

# ev_repo: owner/name of the GitHub repository evidence is published to
ev_repo() {
    local r=${PINS_EVIDENCE_REPO:-}
    if [ -z "$r" ]; then
        r=$(git -C "$root" remote get-url origin 2>/dev/null || true)
        r=${r%.git}; r=${r#*github.com[:/]}
    fi
    [[ $r =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] || die "cannot tell the GitHub repository (set PINS_EVIDENCE_REPO=owner/name)"
    echo "$r"
}
# ev_gh_upload <repo> <tag> -- <file>...: only into an existing pre-release, never over an asset
ev_gh_upload() {
    local repo=$1 tag=$2 pre; shift 2
    [ "${1:-}" = -- ] || die "ev_gh_upload: the file names must follow --"
    command -v gh >/dev/null 2>&1 || die "evidence-publish needs gh"
    pre=$(gh release view "$tag" -R "$repo" --json isPrerelease --jq .isPrerelease) ||
        die "no release $tag on $repo; the owner creates it once, as a pre-release that is not marked latest"
    [ "$pre" = true ] || die "the release $tag on $repo is not a pre-release; nothing was uploaded"
    gh release upload "$tag" -R "$repo" "$@"
}

# ev_stage_list <dir>: sets ev_files to the files in the stage folder dir and
# ev_total to their bytes; fails, with the reason in ev_why, unless each is a
# regular file (no link) with an evidence name, lying in dir by physical
# path, and there are at least an image and the manifest
ev_stage_list() {
    local pdir f n size
    ev_files=(); ev_total=0; ev_why=""
    pdir=$(phys_dir "$1") || { ev_why="the stage folder cannot be resolved"; return 1; }
    for f in "$1"/* "$1"/.*; do
        n=${f##*/}
        case $n in .|..) continue;; esac
        [ -e "$f" ] || [ -L "$f" ] || continue
        case $n in [A-Za-z0-9_]*[a-z0-9]) ;; *) n="";; esac
        case $n in *[!A-Za-z0-9_.-]*) n="";; esac
        case $n in *.png|*.gif|*-evidence.txt) ;; *) n="";; esac
        if [ -n "$n" ] && [ -f "$f" ] && [ ! -L "$f" ]; then phys_path "$f"; else phys_result=""; fi
        [ "$phys_result" = "$pdir/$n" ] || { ev_why="the stage folder holds a file it should not"; return 1; }
        size=$(wc -c < "$f") || { ev_why="cannot size a staged file"; return 1; }
        ev_total=$((ev_total + size)); ev_files+=("$f")
    done
    [ ${#ev_files[@]} -ge 2 ] || { ev_why="the stage folder holds no image and manifest"; return 1; }
}

# evidence-publish: scripts/pins/evidence.py ("stage") reads the manifest and
# each image once, checks those bytes and writes them, with a manifest it
# regenerates from the validated fields, into a fresh stage folder, and
# writes the Markdown beside it. What is uploaded is what this function finds
# in the stage folder, each file checked to lie in it by physical path; no
# text another program printed becomes an argument, and every file name
# follows "--".
cmd_evidence_publish() (
    [ $# -ge 1 ] || die "usage: pins.sh evidence-publish <manifest> [file...]"
    ev_cleanup_trap
    local arg
    for arg in "$@"; do
        case $arg in -*) die "evidence-publish: refused: an argument starts with '-' (it takes file names, no option); nothing was uploaded";; esac
    done
    local manifest=$1 tag=pr-evidence repo stage; shift
    repo=$(ev_repo)
    wd=$(mktemp -d "${TMPDIR:-/tmp}/pins-publish.XXXXXX") || die "evidence-publish: no scratch folder; nothing was uploaded"
    stage="$wd/stage"; mkdir "$stage" || { rm -rf "${wd:?}"; die "evidence-publish: no stage folder; nothing was uploaded"; }
    if ! ev_py stage --root "$(evidence_root)" --store "$(store)" --manifest "$manifest" --stage "$stage" \
        --repo "$repo" --markdown "$wd/body.md" -- ${1+"$@"}; then
        rm -rf "${wd:?}"; die "nothing was uploaded"
    fi
    ev_stage_list "$stage" || { rm -rf "${wd:?}"; die "evidence-publish: refused: $ev_why; nothing was uploaded"; }
    echo "evidence-publish: uploading ${#ev_files[@]} files, $ev_total bytes, to the $tag pre-release of $repo; an uploaded asset is public at once and cannot be recalled" >&2
    if [ -n "${PINS_EVIDENCE_UPLOADER:-}" ]; then "$PINS_EVIDENCE_UPLOADER" "$repo" "$tag" -- "${ev_files[@]}"
    else ev_gh_upload "$repo" "$tag" -- "${ev_files[@]}"; fi ||
        { rm -rf "${wd:?}"; die "the upload failed; any file sent before the failure is public (check the release)"; }
    echo "uploaded ${#ev_files[@]} files to the $tag pre-release of $repo"
    echo
    cat "$wd/body.md"
    rm -rf "${wd:?}"
)

# ---- evidence self-test: no network, no game data --------------------------

ev_fail() { echo "  FAIL: $*" >&2; return 1; }
# ev_list <manifest> <file> <kind>: list a file with its true size and digest
ev_list() {
    python3 -I -c 'import hashlib,os,sys; p=sys.argv[2]; d=open(p,"rb").read()
open(sys.argv[1],"a").write("file %s %d %s %s 1x1 1/1\n" % (os.path.basename(p), len(d), hashlib.sha256(d).hexdigest(), sys.argv[3]))' "$@"
}
# ev_craft <how> <file> [text]: rewrite an image the way an adversary would, and
# give its line in the manifest m its true size and digest
ev_craft() {
    python3 -I -c 'import hashlib, os, struct, sys, zlib
sys.path.insert(0, sys.argv[1]); import evidence as e
m, how, p = sys.argv[2], sys.argv[3], sys.argv[4]; d = open(p, "rb").read(); t = sys.argv[5].encode() if len(sys.argv) > 5 else b""
if how == "after-iend": d += b"PACK" + b"GAMEDATA" * 512
elif how == "sig-data": d = e.PNG_SIG + b"PACK" + b"GAMEDATA" * 512
elif how == "gif-junk": d = b"GIF89a" + b"Q2DEMO.." * 512
elif how == "gif-after-trailer": d += b"PACK" + b"GAMEDATA" * 64
elif how == "ztxt": d = d[:-12] + e._chunk(b"zTXt", b"Comment\0\0" + zlib.compress(t)) + d[-12:]
elif how == "pixels":  # a valid PNG of the same size whose stored (uncompressed) pixels hold t
    w, h = struct.unpack(">II", d[16:24]); rows = b"\0" + t.ljust(3 * w, b"\0") + (b"\0" * (1 + 3 * w)) * (h - 1)
    d = e.PNG_SIG + e._ihdr(w, h) + e._chunk(b"IDAT", zlib.compress(rows, 0)) + e._chunk(b"IEND", b"")
elif how == "content": d = t
elif how == "bad-sig": d = b"\x88" + d[1:]
elif how.startswith("gif-"):
    w = 800  # the end of the header, palette and loop block
    if how == "gif-palette": d = d[:13] + bytes([d[13] ^ 1]) + d[14:]
    elif how == "gif-comment": d = d[:w] + b"\x21\xfe\x05hello\x00" + d[w:]
    elif how == "gif-subblock":  # one more sub-block of data after the LZW stream of the first frame
        q = w + 19
        while d[q]: q += 1 + d[q]
        d = d[:q] + b"\x05PACK!" + d[q:]
    else: raise SystemExit("ev_craft: " + how)
else:  # a change to one chunk of a PNG, its CRC made right again
    ch = e.png_chunks(d); kinds = [k for k, _ in ch]
    def at(kind): return kinds.index(kind)
    if how == "actl": i = at(b"acTL"); ch[i] = (b"acTL", ch[i][1][:4] + struct.pack(">I", 1))
    elif how == "fctl": i = at(b"fcTL"); ch[i] = (b"fcTL", ch[i][1][:20] + struct.pack(">H", 250) + ch[i][1][22:])
    elif how == "fdat-seq": i = at(b"fdAT"); ch[i] = (b"fdAT", struct.pack(">I", 99) + ch[i][1][4:])
    elif how == "idat-junk": i = at(b"IDAT"); ch[i] = (b"IDAT", ch[i][1] + b"PACKjunk")
    elif how == "idat-short": i = at(b"IDAT"); raw = zlib.decompress(ch[i][1]); ch[i] = (b"IDAT", zlib.compress(raw[:len(raw) // 2], 9))
    elif how == "fdat-junk": i = at(b"fdAT"); ch[i] = (b"fdAT", ch[i][1] + b"PACKjunk")
    elif how == "idat-noend":
        i = at(b"IDAT"); c = zlib.compressobj(9); ch[i] = (b"IDAT", c.compress(zlib.decompress(ch[i][1])) + c.flush(zlib.Z_SYNC_FLUSH))
    elif how == "filter3":
        i = at(b"IDAT"); raw = bytearray(zlib.decompress(ch[i][1])); raw[0] = 3; ch[i] = (b"IDAT", zlib.compress(bytes(raw), 9))
    else: raise SystemExit("ev_craft: " + how)
    d = e.PNG_SIG + b"".join(e._chunk(k, b) for k, b in ch)
open(p, "wb").write(d)
out = []
for line in open(m).read().split("\n")[:-1]:
    w = line.split(" ")
    if w[0] == "file" and w[1] == os.path.basename(p):
        w[2], w[3] = str(len(d)), hashlib.sha256(d).hexdigest()
    out.append(" ".join(w))
open(m, "w").write("\n".join(out) + "\n")' "$root/scripts/pins" "$m" "$@"
}
# ev_case [build|synth]: a fresh copy of an evidence set; sets d (its folder),
# p (its prefix), m (its manifest), still, clip and gif
ev_case() {
    local t; t=$(ls -d "$tmp/tpl/${1:-build}"/*)
    rm -rf "${PINS_EVIDENCE_DIR:?}"; mkdir -p "$PINS_EVIDENCE_DIR"; cp -R "$t" "$PINS_EVIDENCE_DIR/"
    p=${t##*/}; d="$PINS_EVIDENCE_DIR/$p"; m="$d/$p-evidence.txt"
    still="$d/$p-still.png"; clip="$d/$p-clip.png"; gif="$d/$p-clip.gif"
    rm -rf "${tmp:?}/got"; mkdir "$tmp/got"; : > "$tmp/uploaded"; : > "$tmp/upload-args"
}
# ev_refused <reason> [publish args]: publish must fail naming the reason,
# print neither the scratch path nor the test identity, and upload nothing
ev_refused() {
    local why=$1 rep; shift
    rep=$( (cmd_evidence_publish "$@") 2>&1 ) && { ev_fail "publish must be refused ($why): $rep"; return 1; }
    case $rep in *"refused: "*"$why"*) ;; *) ev_fail "the refusal must say '$why', got: $rep"; return 1;; esac
    case $rep in *"$tmp"*|*evidencetestuser*) ev_fail "the refusal prints what it hides: $rep"; return 1;; esac
    [ ! -s "$tmp/uploaded" ] && [ ! -s "$tmp/upload-args" ] || ev_fail "a refused publish uploaded: $(cat "$tmp/uploaded")" || return 1
}
ev_count() { [ "$(wc -l < "$tmp/uploaded" | tr -d ' ')" -eq "$1" ] || ev_fail "$2: $(cat "$tmp/uploaded")"; }
# ev_td / ev_td_empty <what>: a fresh, empty TMPDIR for the next refused call,
# and the check that the call left nothing in it (no scratch or stage folder)
ev_td() { rm -rf "${tmp:?}/td"; mkdir "$tmp/td"; }
ev_td_empty() { [ -z "$(ls -A "$tmp/td")" ] || ev_fail "$1 left scratch files behind: $(cd "$tmp/td" && find . | sort | tr '\n' ' ')"; }
ev_st_publish_ok() {
    ev_case build
    local rep; rep=$(cmd_evidence_publish "$m" 2>"$tmp/err") || ev_fail "publish of a built set must pass: $rep $(cat "$tmp/err")" || return 1
    ev_count 4 "publish must upload the 3 images and the manifest" || return 1
    [ "$(cat "$tmp/upload-args")" = "example/repo pr-evidence" ] || ev_fail "the uploader must get the repository, the tag and -- before the files" || return 1
    grep -q "uploading 4 files, [0-9]* bytes, to the pr-evidence pre-release of example/repo; an uploaded asset is public at once and cannot be recalled" "$tmp/err" ||
        ev_fail "publish must say where, how much and that it cannot be recalled: $(cat "$tmp/err")" || return 1
    cmp -s "$tmp/got/$p-evidence.txt" "$m" || ev_fail "the regenerated manifest must equal the one evidence wrote" || return 1
    local u="https://github.com/example/repo/releases/download/pr-evidence/$p" line
    for line in '### Evidence: `world` on `soft`' "Reference \`world/gl1/ref-1\`, candidate \`world/soft/" '; `frames=2 failed=' \
        "](${u}-still.png)" "![world: clip, 2 frames at 500 ms each](${u}-clip.png)" "[$p-clip.gif](${u}-clip.gif)" \
        '| `world-000001.tga` | 0.' "[$p-evidence.txt](${u}-evidence.txt)"; do
        case $rep in *"$line"*) ;; *) ev_fail "the Markdown must hold '$line': $rep"; return 1;; esac
    done
    ev_case build; rep=$(cmd_evidence_publish "$m" "$still" 2>/dev/null) || ev_fail "publishing one listed file must pass" || return 1
    ev_count 2 "one named file and the manifest must be uploaded" || return 1
    case $rep in *"-clip."*) ev_fail "the Markdown must name only what was uploaded: $rep"; return 1;; esac
    ev_case synth; rep=$(cmd_evidence_publish "$m" 2>/dev/null) || ev_fail "publish of the synthetic set must pass: $rep" || return 1
    ev_count 4 "the synthetic set has 3 images and the manifest" || return 1
    case $rep in *'### Evidence: synthetic test pattern (no game content)'*"![synthetic test pattern](https://"*) ;; *) ev_fail "synthetic Markdown: $rep"; return 1;; esac
}
ev_st_card_only() {
    # the build template was made by "evidence soft world --card T1" alone (see cmd_evidence_selftest)
    local o; o=$(ls -d "$tmp/tpl/build"/*); o="$o/${o##*/}"
    [ -f "$o-still.png" ] && [ -f "$o-clip.png" ] && [ -f "$o-clip.gif" ] || ev_fail "evidence with only --card must write the still, the clip and the GIF" || return 1
    grep -q '^worst-frame world-000001.tga$' "$o-evidence.txt" || ev_fail "the still must show the worst frame" || return 1
    grep -q '^reference world/gl1/ref-1$' "$o-evidence.txt" || ev_fail "the default reference must be gl1" || return 1
    grep -q "^candidate world/soft/$(run_id world 1)\$" "$o-evidence.txt" || ev_fail "the default candidate must be this checkout's run" || return 1
}
ev_st_args() {
    # a file argument that is an option (it once overrode the evidence root)
    ev_case; mkdir -p "$tmp/elsewhere/$p"; cp "$m" "$tmp/elsewhere/$p/"
    ev_refused "starts with '-'" "$tmp/elsewhere/$p/$p-evidence.txt" --root "$tmp/elsewhere" || return 1
    ev_refused "starts with '-'" "$m" -- "$still" || return 1
    ev_refused "starts with '-'" --manifest "$m" || return 1
    # the Python entry point: only known options, each once, file names only after --
    local rep arg why
    for arg in "--root $tmp --root $tmp:given twice" "--bogus x:does not take" "stray:does not take" "--manifest:needs a value" \
        "--root $tmp:needs --manifest --markdown --repo --stage --store"; do
        why=${arg##*:}; arg=${arg%:*}
        # shellcheck disable=SC2086
        if rep=$(ev_py stage $arg 2>&1); then ev_fail "evidence.py stage $arg must be refused"; return 1; fi
        case $rep in *"$why"*) ;; *) ev_fail "evidence.py stage $arg must say '$why': $rep"; return 1;; esac
    done
    # a manifest name carrying a newline and uploader arguments
    ev_case; local evil="$d/$p-still.png"$'\n'"--clobber"$'\n'"pak0.pak"$'\n'"b-evidence.txt"
    cp "$m" "$evil"; printf 'PACK' > "$d/pak0.pak"
    ev_refused "manifest name" "$evil" || return 1
    ev_case; cp "$m" "$d/other-evidence.txt"
    ev_refused "not named after the set" "$d/other-evidence.txt" || return 1
    ev_case; mv "$d" "$PINS_EVIDENCE_DIR/other"
    ev_refused "not named after the set" "$PINS_EVIDENCE_DIR/other/$p-evidence.txt" || return 1
    # evidence.py itself refuses a file argument that starts with '-', even one that resolves into the set
    ev_case; mkdir -p "$PINS_EVIDENCE_DIR/-x" "$tmp/st2"
    if rep=$(cd "$PINS_EVIDENCE_DIR" && ev_py stage --root "$PINS_EVIDENCE_DIR" --store "$PINS_STORE" --manifest "$m" \
        --stage "$tmp/st2" --repo example/repo --markdown "$tmp/st2.md" -- "-x/../$p/$p-still.png" 2>&1); then
        ev_fail "evidence.py stage must refuse a file argument starting with '-'"; return 1
    fi
    case $rep in *"not one of the files of this set"*) ;; *) ev_fail "a file argument starting with '-': $rep"; return 1;; esac
}
ev_st_place() {
    ev_case; mkdir -p "$tmp/elsewhere/$p"; cp "$m" "$tmp/elsewhere/$p/"
    ev_refused "outside the evidence folder" "$tmp/elsewhere/$p/$p-evidence.txt" || return 1
    ev_case; mkdir -p "$PINS_EVIDENCE_DIR/deeper"; mv "$d" "$PINS_EVIDENCE_DIR/deeper/"
    ev_refused "outside the evidence folder" "$PINS_EVIDENCE_DIR/deeper/$p/$p-evidence.txt" || return 1
    ev_case; mv "$m" "$tmp/real-manifest"; ln -s "$tmp/real-manifest" "$m"
    ev_refused "the manifest cannot be opened" "$m" || return 1
    ev_case; mv "$still" "$tmp/real-still.png"; ln -s "$tmp/real-still.png" "$still"
    ev_refused "cannot be opened" "$m" || return 1
    if [ "$(id -u)" -ne 0 ]; then ev_case; chmod 000 "$still"; ev_refused "cannot be opened" "$m" || return 1; chmod 644 "$still"; fi
    ev_case; rm "$gif"; mkdir "$gif"
    ev_refused "not a regular file" "$m" || return 1
    ev_case; cp "$still" "$d/extra.png"
    ev_refused "not one of the files of this set" "$m" "$d/extra.png" || return 1
    ev_refused "not one of the files of this set" "$m" "$tmp/tpl/build/$p/$p-still.png" || return 1
    # an evidence folder inside the pin store
    ( PINS_EVIDENCE_DIR="$PINS_STORE/ev"; ev_case; ev_refused "inside the pin store" "$m" ) || { rm -rf "${PINS_STORE:?}/ev"; return 1; }
    rm -rf "${PINS_STORE:?}/ev"
}
ev_st_content() {
    ev_case; ev_craft after-iend "$still"
    ev_refused "not a whole PNG" "$m" || return 1
    ev_case; ev_craft bad-sig "$still"
    ev_refused "not a whole PNG" "$m" || return 1
    ev_case; ev_craft sig-data "$still"
    ev_refused "not a whole PNG" "$m" || return 1
    ev_case; ev_craft after-iend "$clip"
    ev_refused "not a whole PNG" "$m" || return 1
    ev_case; ev_craft gif-junk "$gif"
    ev_refused "GIF header" "$m" || return 1
    ev_case; ev_craft gif-after-trailer "$gif"
    ev_refused "does not end at its trailer" "$m" || return 1
    local how why
    for how in "actl:animation control" "fctl:frame control" "fdat-seq:out of sequence" "idat-junk:decompress to the stated size" "fdat-junk:decompress to the stated size" "idat-short:decompress to the stated size" \
        "idat-noend:decompress to the stated size" "filter3:filter the encoder does not write"; do
        why=${how#*:}; how=${how%%:*}
        ev_case; ev_craft "$how" "$clip"
        ev_refused "$why" "$m" || return 1
    done
    ev_case; ev_craft filter3 "$still"
    ev_refused "filter the encoder does not write" "$m" || return 1
    for how in "gif-palette:GIF header" "gif-comment:frame header" "gif-subblock:decode to the stated size"; do
        why=${how#*:}; how=${how%%:*}
        ev_case; ev_craft "$how" "$gif"
        ev_refused "$why" "$m" || return 1
    done
    # the manifest states a size the image does not have
    ev_case; sed -i.bak 's/^\(file [^ ]*-still.png [0-9]* [0-9a-f]* png \)[0-9]*x/\1999x/' "$m"; rm "$m.bak"
    ev_refused "PNG header" "$m" || return 1
    ev_case; ev_craft ztxt "$still" evidencetestuser
    LOGNAME=evidencetestuser ev_refused "chunks are not exactly" "$m" || return 1
    ev_case; printf 'x' >> "$gif"
    ev_refused "digest differs" "$m" || return 1
    # the same size, one byte changed
    ev_case; python3 -I -c 'import sys; p = sys.argv[1]; d = bytearray(open(p, "rb").read()); d[-2] ^= 1; open(p, "wb").write(d)' "$gif"
    ev_refused "digest differs" "$m" || return 1
    ev_case; PINS_EVIDENCE_BUDGET=1000 ev_refused "over the budget of 1000 bytes" "$m" || return 1
    ev_case; sed -i.bak 's/^budget .*/budget 1000/' "$m"; rm "$m.bak"
    ev_refused "over the budget of 1000 bytes" "$m" || return 1
    # game and store data, by name: named after the manifest, or listed in it
    # (ev_st_manifest lists each kind), and by content, as the still
    local name content
    for name in game.pak trace.q2r manifest.txt world-000000.tga; do
        case $name in
            *.pak) content='PACK';; *.q2r) content='Q2REFREC';;
            manifest.txt) content='world-000000.tga soft 640x480';; *) content='data';;
        esac
        ev_case; printf '%s\n' "$content" > "$d/$name"
        ev_refused "not one of the files of this set" "$m" "$d/$name" || return 1
        ev_list "$m" "$d/$name" png
        ev_refused "not the ones evidence writes" "$m" || return 1
        ev_case; ev_craft content "$still" "$content"
        ev_refused "not a whole PNG" "$m" || return 1
    done
}
ev_st_identity() {
    # in the pixels of a valid PNG: the raw bytes hold the user name, and a home path
    ev_case; ev_craft pixels "$still" evidencetestuser
    LOGNAME=evidencetestuser ev_refused "host name, user name or home path" "$m" || return 1
    ev_case; ev_craft pixels "$still" "$(printf '/%s/' Users)nobody"
    ev_refused "absolute home path" "$m" || return 1
    # in the manifest: a set made for a card that is the user name
    ev_case
    local q=evidencetestuser-c0ffee000000-synthetic-pattern
    ev_py synth --out "$PINS_EVIDENCE_DIR/$q" --prefix "$q" --size 32x24x2 --meta card=evidencetestuser \
        --meta commit=c0ffee000000 --meta 'command=pins.sh evidence --synthetic --card evidencetestuser' >/dev/null
    LOGNAME=evidencetestuser ev_refused "host name, user name or home path" "$PINS_EVIDENCE_DIR/$q/$q-evidence.txt" || return 1
    # in the asset names: an identity string that spans the card and the commit (the file lines carry the names)
    ev_case synth
    LOGNAME=T1-c0ffee000000 ev_refused "the manifest holds a host name, user name or home path" "$m" || return 1
}
# ev_variants <manifest> [<regex> <replacement> <reason>]...: every variant of the
# manifest (the first match of the regex, multi-line, replaced; "@md" puts
# Markdown-active text at the end of each line in turn) must be refused by the
# manifest parser of evidence-publish with the reason, printing none of it
ev_variants() {
    python3 -I -c 'import re, sys
sys.path.insert(0, sys.argv[1]); import evidence as e
base = open(sys.argv[2]).read(); a = sys.argv[3:]; bad = 0
md = "x` @team <img src=x> [l](https://example.invalid/a)"
lines = base[:-1].split("\n"); out = []
for pat, rep, why in zip(a[0::3], a[1::3], a[2::3]):
    if pat == "@md":
        out += [("Markdown at the end of line %d" % (i + 1), "\n".join(l + md * (j == i) for j, l in enumerate(lines)) + "\n", why) for i in range(len(lines))]
    else:
        out.append((pat, re.sub(pat, rep, base, count=1, flags=re.M), why))
for what, text, why in out:
    try:
        if text == base:
            raise ValueError("changed nothing")
        e.parse_manifest(text.encode("utf-8"))
        msg = "accepted"
    except SystemExit as x:
        msg = str(x)
        if "refused: " in msg and why in msg and not any(s in msg for s in ("example.invalid", "@team", "<img")):
            continue
    except ValueError as x:
        msg = str(x)
    print("  FAIL: the manifest variant %r must be refused with %r: %s" % (what, why, msg), file=sys.stderr); bad = 1
sys.exit(bad)' "$root/scripts/pins" "$@"
}
ev_st_manifest() {
    # end to end: a pin-store manifest line, free text, Markdown in a field
    ev_case; printf 'world-000000.tga soft 640x480\n' >> "$m"
    ev_refused "not a line evidence writes" "$m" || return 1
    ev_case; printf 'note free text\n' >> "$m"
    ev_refused "not a line evidence writes" "$m" || return 1
    ev_case; sed -i.bak 's/^renderer soft$/renderer soft[l](https:\/\/example.invalid\/a)/' "$m"; rm "$m.bak"
    ev_refused "not a line evidence writes" "$m" || return 1
    # the parser, one variant at a time: Markdown-active text in every field,
    # values and combinations the builder cannot write, and the form
    ev_case
    local s; s=$(sed -n 's/^file \([^ ]*-still.png\) \([0-9]*\) \([0-9a-f]*\) .*/\1 \2 \3/p' "$m")
    local listed=() n many=""
    for n in $(seq 10 57); do many="${many}score world-0000$n.tga 0.000000 0.000000 ok"$'\n'; done
    for n in game.pak game.pk3 game.zip game.dm2 intro.cin trace.q2r manifest.txt world-000000.tga engine.log timedemo.txt complete incomplete; do
        listed+=('\Z' "file $n 5 ${s##* } png 1x1 1/1"$'\n' "not the ones evidence writes")
    done
    ev_variants "$m" @md '' 'refused' \
        '\Z' $'world-000000.tga soft 640x480\n' 'not a line evidence writes' \
        '\Z' $'note free text\n' 'not a line evidence writes' \
        '^renderer soft$' 'renderer Soft' 'not a line evidence writes' \
        '^summary .*' 'summary frames=2 extra' 'not a line evidence writes' \
        '^score world-000000.tga .*' 'score world-000000.tga 1e-5 - ok' 'not a line evidence writes' \
        '^score world-000000.tga (\S+) (\S+) \S+' 'score world-000000.tga \1 \2 bad' 'not a line evidence writes' \
        '^commit .*' 'commit C0FFEE000000' 'not a line evidence writes' \
        ' 1/1$' ' 1/9' 'not a line evidence writes' \
        '^(file \S+ )\d+' '\g<1>x' 'not a line evidence writes' \
        '^card .*\n' '\g<0>\g<0>' 'not a line evidence writes' \
        '^ref-caption .*' 'ref-caption REFERENCE soft' 'do not agree' \
        '^cand-caption .*' 'cand-caption CANDIDATE gl1' 'do not agree' \
        '^candidate world/soft/' 'candidate world/gl1/' 'do not agree' \
        '^candidate world/' 'candidate skywater/' 'do not agree' \
        '^reference world/' 'reference skywater/' 'do not agree' \
        '^frames 2$' 'frames 3' 'do not agree' \
        '^worst-frame .*' 'worst-frame world-000009.tga' 'do not agree' \
        '^clip-frames 2$' 'clip-frames 3' 'do not agree' \
        '^diff-gain 8$' 'diff-gain 9' 'do not agree' \
        '^clip-delay-ms 500$' 'clip-delay-ms 501' 'do not agree' \
        '^budget .*' 'budget exempt: a size-limit probe' 'do not agree' \
        ' --card T1$' ' --card T2' 'do not agree' \
        '^card T1$' 'card T2' 'do not agree' \
        '^command pins.sh evidence soft world' 'command pins.sh evidence gl1 world' 'do not agree' \
        '^command pins.sh evidence soft world' 'command pins.sh evidence soft skywater' 'do not agree' \
        '^command .*' '\g<0> --ref soft' 'do not agree' \
        '^command .*' '\g<0> --cand-id other' 'do not agree' \
        '^command .*' '\g<0> --ref-id other' 'do not agree' \
        '^command .*' '\g<0> --bogus 1' 'command is not one' \
        '^command .*' '\g<0> --run 0' 'command is not one' \
        '^command .*' '\g<0> --card' 'command is not one' \
        '^summary .*\n' '' 'does not hold exactly the fields' \
        '^(card .*\n)(commit .*\n)' '\2\1' 'does not hold exactly the fields' \
        '^(file \S+-still.png .*\n)(file .*\n)' '\2\1' 'not the ones evidence writes' \
        '^(file \S+-still.png \d+ \S+ )png' '\1apng' 'not the ones evidence writes' \
        '^(file \S+-still.png \d+ \S+ png \d+x\d+ 1/1\n)' '' 'not the ones evidence writes' \
        '^(summary .*\n)(score .*\n)' '\1\2\2' 'do not agree' \
        '^(score .*\n)((?:score .*\n)+)(file .*\n)' '\2\3\1' 'not in the form' \
        '\n' $'\r\n' 'line endings' \
        '\n\Z' '' 'line endings' \
        '\Z' $'\xc3\xa9\n' 'not plain ASCII' \
        '^pins-evidence 1$' 'pins-evidence 2' 'does not start with' \
        '^frames 2\nclip-frames 2\n((?:.*\n){5})' "frames 50"$'\n'"clip-frames 49"$'\n''\1'"$many" 'do not agree' \
        "${listed[@]}" || return 1
    ev_case synth
    ev_variants "$m" @md '' 'refused' \
        '^clip-delay-ms 125$' 'clip-delay-ms 500' 'do not agree' \
        '^card T1$' 'card T2' 'do not agree' \
        '^command .*' '\g<0> --noise 5' 'do not agree' \
        '^(command .*)(\n(?:.*\n)*?)budget .*' '\1 --noise 5\2budget exempt: a size-limit probe' 'do not agree' \
        '^budget .*' 'budget exempt: a size-limit probe' 'do not agree' \
        '^command pins.sh evidence --synthetic' 'command pins.sh evidence soft world' 'does not hold exactly the fields' \
        '^(content .*\n)' '\1score world-000000.tga 0.1 0.1 ok\n' 'does not hold exactly the fields' || return 1
}
ev_st_fail_closed() {
    # an uploader that fails is a failure, and a crash of the stage step is a refusal
    ev_case; printf '#!/bin/sh\nexit 1\n' > "$tmp/fail-upload"; chmod +x "$tmp/fail-upload"
    local rep
    ev_td
    if rep=$( (TMPDIR="$tmp/td" PINS_EVIDENCE_UPLOADER="$tmp/fail-upload" cmd_evidence_publish "$m") 2>&1 ); then ev_fail "a failing uploader must fail the publish"; return 1; fi
    case $rep in *"the upload failed"*) ;; *) ev_fail "a failing upload must say so: $rep"; return 1;; esac
    ev_td_empty "a failing upload" || return 1
    mkdir -p "$tmp/st"
    if rep=$(ev_py stage --root "$PINS_EVIDENCE_DIR" --store "$PINS_STORE" --manifest "$m" --stage "$tmp/no-such-dir" \
        --repo example/repo --markdown "$tmp/st/md" 2>&1); then ev_fail "stage into a missing folder must fail"; return 1; fi
    case $rep in *"refused: unexpected FileNotFoundError; nothing was staged") ;; *) ev_fail "a crash must be a refusal that prints no path: $rep"; return 1;; esac
    : > "$tmp/st/x"
    if rep=$(ev_py stage --root "$PINS_EVIDENCE_DIR" --store "$PINS_STORE" --manifest "$m" --stage "$tmp/st" \
        --repo example/repo --markdown "$tmp/st/md" 2>&1); then ev_fail "stage into a used folder must fail"; return 1; fi
    case $rep in *"stage folder is not empty"*) ;; *) ev_fail "a used stage folder: $rep"; return 1;; esac
    if rep=$(ev_py stage --root "$PINS_EVIDENCE_DIR" --store "$PINS_STORE" --manifest "$m" --stage "$tmp/st" \
        --repo 'example/repo](x' --markdown "$tmp/st/md" 2>&1); then ev_fail "a bad repository must fail"; return 1; fi
    case $rep in *"not owner/name"*) ;; *) ev_fail "a bad repository: $rep"; return 1;; esac
    if rep=$( (PINS_EVIDENCE_REPO='example/repo x' cmd_evidence_publish "$m") 2>&1 ); then ev_fail "a bad PINS_EVIDENCE_REPO must fail"; return 1; fi
    case $rep in *"cannot tell the GitHub repository"*) ;; *) ev_fail "a bad PINS_EVIDENCE_REPO: $rep"; return 1;; esac
    [ ! -s "$tmp/uploaded" ] || ev_fail "a failed publish uploaded" || return 1
}
ev_st_stage_list() {
    local s="$tmp/sl" bad
    ev_sl() { rm -rf "${s:?}"; mkdir "$s"; printf 'a' > "$s/a-still.png"; printf 'bb' > "$s/a-evidence.txt"; }
    ev_sl; ev_stage_list "$s" || ev_fail "a stage folder of an image and a manifest must list: $ev_why" || return 1
    [ ${#ev_files[@]} -eq 2 ] && [ "$ev_total" -eq 3 ] || ev_fail "the listing must count 2 files and 3 bytes: ${ev_files[*]+"${ev_files[*]}"} $ev_total" || return 1
    for bad in link newline txt hidden dash dir one; do
        ev_sl
        case $bad in
            link) ln -s "$s/a-still.png" "$s/b-still.png";; newline) printf x > "$s/b"$'\n'"c.png";;
            txt) printf x > "$s/notes.txt";; hidden) printf x > "$s/.b.png";; dash) printf x > "$s/-b.png";;
            dir) mkdir "$s/b.png";; one) rm "$s/a-still.png";;
        esac
        if ev_stage_list "$s"; then ev_fail "a stage folder with $bad must not list"; return 1; fi
        case $ev_why in *"should not"*|*"no image and manifest"*) ;; *) ev_fail "stage folder with $bad: $ev_why"; return 1;; esac
    done
    # publish acts on that refusal: a file planted in the stage folder after the stage step
    ev_case
    ( ev_py() { python3 -I "$root/scripts/pins/evidence.py" "$@" && printf x > "$stage/.planted.png"; }
      ev_refused "the stage folder holds a file it should not" "$m" ) || return 1
}
# the gh path, through a stub gh first on PATH (never the real one): an
# existing pre-release, -R, and -- before the files; and a release that is
# not a pre-release refuses before any upload
ev_st_gh() {
    mkdir -p "$tmp/bin"
    printf '%s\n' '#!/bin/sh' "echo \"\$*\" >> '$tmp/gh-calls'" \
        'case "$1 $2" in "release view") echo "${GH_STUB_PRE:-true}";; "release upload") ;; *) exit 9;; esac' > "$tmp/bin/gh"
    chmod +x "$tmp/bin/gh"
    ev_case; : > "$tmp/gh-calls"
    ( PATH="$tmp/bin:$PATH"; unset PINS_EVIDENCE_UPLOADER; [ "$(command -v gh)" = "$tmp/bin/gh" ] || exit 1
      cmd_evidence_publish "$m" >/dev/null 2>&1 ) || ev_fail "publish through gh must pass: $(cat "$tmp/gh-calls")" || return 1
    grep -q "^release view pr-evidence -R example/repo --json isPrerelease" "$tmp/gh-calls" || ev_fail "publish must ask gh whether pr-evidence is a pre-release" || return 1
    grep -q "^release upload pr-evidence -R example/repo -- [^-]" "$tmp/gh-calls" || ev_fail "the gh upload must name the repository and put -- before the files: $(cat "$tmp/gh-calls")" || return 1
    ev_case; : > "$tmp/gh-calls"; ev_td
    local rep
    if rep=$( (PATH="$tmp/bin:$PATH"; unset PINS_EVIDENCE_UPLOADER; [ "$(command -v gh)" = "$tmp/bin/gh" ] || exit 0
        TMPDIR="$tmp/td" GH_STUB_PRE=false cmd_evidence_publish "$m") 2>&1 ); then ev_fail "a release that is not a pre-release must be refused"; return 1; fi
    case $rep in *"is not a pre-release"*) ;; *) ev_fail "a release that is not a pre-release: $rep"; return 1;; esac
    # the refusal comes from die inside the gh helper; the staged set must still go
    ev_td_empty "a refusal of the gh helper" || return 1
    if grep -q '^release upload' "$tmp/gh-calls"; then ev_fail "a release that is not a pre-release got an upload"; return 1; fi
}
ev_st_noise() {
    ev_case noise
    local rep; rep=$(cmd_evidence_publish "$m" 2>/dev/null) || ev_fail "publish of the noise probe must pass: $rep" || return 1
    ev_count 2 "the noise probe has one image and the manifest" || return 1
    case $rep in *"![synthetic: clip, 1 frames at 500 ms each](https://"*) ;; *) ev_fail "noise Markdown: $rep"; return 1;; esac
    ev_variants "$m" '^command (.*) --noise 1000$' 'command \1' 'do not agree' \
        '^budget .*' 'budget 4000000' 'do not agree' || return 1
    ev_case noise
    PINS_EVIDENCE_BUDGET=1000 ev_refused "over the budget of 1000 bytes" "$m" || return 1
}
ev_st_evidence() {
    local diff="$tmp/framediff" a="$PINS_STORE/world/soft/aaaa-1" b="$PINS_STORE/world/soft/aaaa-1-r2" i
    mkdir -p "$a" "$b"
    for i in 0 1 2; do "$diff" --gen "$a/world-00000$i.tga" 64 48 "$i"; cp "$a/world-00000$i.tga" "$b/"; done
    "$diff" --perturb "$a/world-000001.tga" "$b/world-000001.tga" 40
    touch "$a/complete" "$b/complete"
    local rep; rep=$(cmd_evidence soft world --ref soft --ref-id aaaa-1 --cand-id aaaa-1-r2 --card T1) || ev_fail "evidence over two fixture pins must pass: $rep" || return 1
    local o="$PINS_EVIDENCE_DIR/T1-aaaa-soft-world"
    [ -f "$o/T1-aaaa-soft-world-still.png" ] && [ -f "$o/T1-aaaa-soft-world-clip.png" ] && [ -f "$o/T1-aaaa-soft-world-clip.gif" ] || ev_fail "evidence must write the still, the clip and the GIF: $(ls "$o")" || return 1
    grep -q '^worst-frame world-000001.tga$' "$o/T1-aaaa-soft-world-evidence.txt" || ev_fail "the still must show the worst frame" || return 1
    grep -q '^reference world/soft/aaaa-1$' "$o/T1-aaaa-soft-world-evidence.txt" || ev_fail "the manifest must name the pins relative to the store" || return 1
    if grep -qF "$tmp" "$o"/*; then ev_fail "an output holds the scratch path"; return 1; fi
    ls "$b" | grep -qv -e '\.tga$' -e '^complete$' && { ev_fail "evidence wrote into the pin store"; return 1; }
    [ "$(ls "$PINS_STORE")" = world ] || ev_fail "evidence wrote into the pin store: $(ls "$PINS_STORE")" || return 1
    ev_td
    rep=$( (TMPDIR="$tmp/td" PINS_EVIDENCE_DIR="$PINS_STORE/ev" cmd_evidence soft world --ref soft --ref-id aaaa-1 --cand-id aaaa-1-r2) 2>&1 ) &&
        { ev_fail "an evidence folder inside the store must be refused"; return 1; }
    case $rep in *"inside the pin store"*) ;; *) ev_fail "an evidence folder inside the store: $rep"; return 1;; esac
    ev_td_empty "a refused evidence folder" || return 1
    rep=$( (cmd_evidence soft world --ref soft --ref-id aaaa-1 --cand-id aaaa-1-r2 --card 'a/b') 2>&1 ) && { ev_fail "a card with a slash must be refused"; return 1; }
    case $rep in *"--card must match"*) ;; *) ev_fail "a card with a slash: $rep"; return 1;; esac
    rep=$( (cmd_evidence soft world --ref soft --ref-id aaaa-1 --cand-id -aaaa) 2>&1 ) && { ev_fail "a pin id starting with '-' must be refused"; return 1; }
    case $rep in *"--cand-id must be a pin directory name"*) ;; *) ev_fail "a pin id starting with '-': $rep"; return 1;; esac
    rm -rf "${a:?}" "${b:?}" "${PINS_EVIDENCE_DIR:?}/T1-aaaa-soft-world"
}

cmd_evidence_selftest() {
    command -v python3 >/dev/null 2>&1 || { echo "evidence selftest: python3 not found, skipped"; return 0; }
    # the self-test must not depend on, or act on, the caller's environment: a
    # git hook's GIT_INDEX_FILE or GIT_DIR, or any PINS_ setting
    unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_COMMON_DIR \
        GIT_NAMESPACE GIT_CEILING_DIRECTORIES GIT_DISCOVERY_ACROSS_FILESYSTEM
    unset PINS_EVIDENCE_BUDGET PINS_EVIDENCE_UPLOADER PINS_EVIDENCE_REPO PINS_EVIDENCE_DIR \
        PINS_BIN PINS_BASEDIR PINS_DEMO PINS_WIDTH PINS_HEIGHT PINS_EVERY PINS_TIMEOUT PINS_DRYRUN PINS_STORE PINS_DIFF PINS_TRACE PINS_SEED
    ev_py selftest
    scratch_outside_git pins-ev
    trap 'rm -rf "$tmp"' EXIT
    export PINS_STORE="$tmp/store" PINS_EVIDENCE_DIR="$tmp/ev" PINS_EVIDENCE_REPO=example/repo PINS_EVIDENCE_UPLOADER="$tmp/fake-upload"
    # the stand-in uploader: "<it> <repo> <tag> -- <file>..."; keeps a copy of what it was given
    printf '%s\n' '#!/bin/sh' '[ "$3" = -- ] || { echo "fake-upload: no -- before the files" >&2; exit 3; }' \
        "echo \"\$1 \$2\" > '$tmp/upload-args'; shift 3" "for f; do echo \"\$f\" >> '$tmp/uploaded'; cp \"\$f\" '$tmp/got/'; done" > "$tmp/fake-upload"
    chmod +x "$tmp/fake-upload"
    # the templates: a synthetic set, and a built set from "evidence" with no option beyond --card,
    # over a fixture store holding the default reference (gl1) and this checkout's candidate
    local diff="$tmp/framediff" id i a b q=T1-c0ffee000000-synthetic-pattern
    cc -O2 -o "$diff" "$root/scripts/pins/framediff.c" -lm
    id=$(run_id world 1); a="$PINS_STORE/world/gl1/ref-1"; b="$PINS_STORE/world/soft/$id"
    mkdir -p "$a" "$b" "$tmp/tpl/build" "$tmp/tpl/synth"
    for i in 0 1; do "$diff" --gen "$a/world-00000$i.tga" 32 24 "$i"; cp "$a/world-00000$i.tga" "$b/"; done
    "$diff" --perturb "$a/world-000001.tga" "$b/world-000001.tga" 40
    touch "$a/complete" "$b/complete"
    cmd_evidence soft world --card T1 >/dev/null || die "evidence selftest failed: card_only (evidence soft world --card T1)"
    mv "$PINS_EVIDENCE_DIR"/* "$tmp/tpl/build/"
    rm -rf "${PINS_STORE:?}/world/gl1" "${b:?}"
    ev_py synth --out "$tmp/tpl/synth/$q" --prefix "$q" --size 96x64x4 --meta card=T1 --meta commit=c0ffee000000 \
        --meta 'command=pins.sh evidence --synthetic --card T1' >/dev/null
    q=T1-c0ffee000000-synthetic-noise1000
    ev_py synth --out "$tmp/tpl/noise/$q" --prefix "$q" --noise 1000 --meta card=T1 --meta commit=c0ffee000000 \
        --meta 'command=pins.sh evidence --synthetic --card T1 --noise 1000' >/dev/null
    export PINS_DIFF="$diff"
    local c failed=""
    for c in card_only publish_ok args place content identity manifest fail_closed stage_list gh noise evidence; do
        if ( set -e; "ev_st_$c" ); then echo "  ok   $c"; else echo "  FAIL $c"; failed="$failed $c"; fi
    done
    [ -z "$failed" ] || die "evidence selftest failed:$failed"
    echo "evidence selftest: ok"
}

# ---- make-demo (TASK70) ---------------------------------------------------

demo_dir="$root/scripts/pins/demos"
# demo_cmds: the console commands this tree registers, Cmd_AddCommand("<name>"
# under src/ and the game module's own, Q_stricmp(cmd, "<name>") in g_cmds.c
demo_cmds() {
    { grep -rhoE 'Cmd_AddCommand\("[^"]+"' "$root/src/" | sed 's/^Cmd_AddCommand("//; s/"$//'
      grep -hoE 'Q_stricmp\(cmd, "[^"]+"\)' "$root/src/game/g_cmds.c" | sed 's/^Q_stricmp(cmd, "//; s/")$//'
    } | sort -u
}
# check_demo_script <file> <name>: every line is a comment, an alias of
# commands, or "<command> [<argument>...]" whose command exists in src/ (or
# is an alias defined above it); one "map <map>" line, exactly one
# "record <name>", and the script ends with stop, then quit. Every line
# beginning "// pins:" must be the one range line; it sets demo_lo and demo_hi
check_demo_script() {
    local file=$1 name=$2 line n=0 cmd piece known aliases=" " records=0 maps=0 last="" prev="" aname body
    local re_alias='^alias ([a-z0-9_]+) "([a-z0-9_]+(;[a-z0-9_]+)*)"$'
    local re_range='^// pins: messages ([1-9][0-9]{0,8}) ([1-9][0-9]{0,8})$' ranges=0
    demo_lo=""; demo_hi=""
    known=$(demo_cmds)
    [ -n "$known" ] || die "cannot read the console commands from $root/src"
    while IFS= read -r line || [ -n "$line" ]; do
        n=$((n+1))
        case $line in
            '') continue;;
            //*) case $line in *\;*|*\"*) die "$name.cfg line $n: a comment must not hold ; or a quote (the engine splits commands on them): $line";; esac
                case $line in '// pins:'*)
                    [[ $line =~ $re_range ]] ||
                        die "$name.cfg line $n: a '// pins:' line must be exactly '// pins: messages <low> <high>', two positive integers without a leading zero, single spaces: $line"
                    [ "${BASH_REMATCH[1]}" -ge 10 ] && [ "${BASH_REMATCH[1]}" -le "${BASH_REMATCH[2]}" ] ||
                        die "$name.cfg line $n: the message range must start at 10 or more and not end below its start: $line"
                    ranges=$((ranges+1)); demo_lo=${BASH_REMATCH[1]}; demo_hi=${BASH_REMATCH[2]};;
                esac
                continue;;
        esac
        case $line in *..*) die "$name.cfg line $n: must not contain '..': $line";; esac
        if [[ $line =~ $re_alias ]]; then
            aname=${BASH_REMATCH[1]}; body=${BASH_REMATCH[2]}
            for piece in ${body//;/ }; do
                case $aliases in *" $piece "*) continue;; esac
                printf '%s\n' "$known" | grep -qxF -e "$piece" || die "$name.cfg line $n: alias $aname runs $piece, which is not a command in src/"
            done
            aliases="$aliases$aname "
            continue
        fi
        [[ $line =~ ^[+-]?[a-z0-9_]+(\ [A-Za-z0-9_.-]+)*$ ]] || die "$name.cfg line $n: not '<command> [<argument>...]' with single spaces, or 'alias <name> \"<command>;...\"': $line"
        cmd=${line%% *}
        case $aliases in *" $cmd "*) ;; *)
            printf '%s\n' "$known" | grep -qxF -e "$cmd" || die "$name.cfg line $n: $cmd is not a command in src/";;
        esac
        case $cmd in
            record) [ "$line" = "record $name" ] || die "$name.cfg line $n: must record $name, the script's own name: $line"; records=$((records+1));;
            map) [[ $line =~ ^map\ [a-z0-9_]+$ ]] || die "$name.cfg line $n: not 'map <map>': $line"; maps=$((maps+1));;
            exec) die "$name.cfg line $n: a demo script runs no other script: $line";;
        esac
        prev=$last; last=$line
    done < "$file"
    [ "$maps" -eq 1 ] || die "$name.cfg: needs exactly one 'map <map>' line, has $maps"
    [ "$records" -eq 1 ] || die "$name.cfg: needs exactly one 'record $name' line, has $records"
    [ "$ranges" -eq 1 ] || die "$name.cfg: needs exactly one '// pins: messages <low> <high>' line, has $ranges"
    [ "$prev" = stop ] && [ "$last" = quit ] || die "$name.cfg: must end with 'stop', then 'quit'"
}
# demo_in_range <messages> <low> <high>: true only when all three are integers
# and low <= messages <= high; a comparison that errors is false, never a pass
demo_in_range() { [ "$1" -ge "$2" ] && [ "$1" -le "$3" ]; }
# demo_blocks <file>: the number of message blocks before the end marker
# (a length of -1), or "cut" when the file ends first
demo_blocks() {
    od -An -v -tu1 "$1" | awk '{for (i = 1; i <= NF; i++) b[n++] = $i}
        END {p = 0; c = 0
            while (p + 4 <= n) {
                l = b[p] + 256 * b[p+1] + 65536 * b[p+2] + 16777216 * b[p+3]
                if (l == 4294967295) {print c; exit}
                p += 4 + l; if (p > n) break; c++
            }
            print "cut"}'
}
# check_demo_place <physical dir> <file>: make-demo writes <file> into <dir>
# (the write directory beside the binary, or its demos/, which a link may
# send elsewhere); that is never inside PINS_BASEDIR, and inside this
# repository only where git ignores it (release/, for the default binary)
check_demo_place() {
    local w=$1 b r
    if [ -n "${PINS_BASEDIR:-}" ]; then
        phys_path "$PINS_BASEDIR"; b=$phys_result
        case "$w/" in "$b/"*) die "make-demo would write $w/$2 inside PINS_BASEDIR; use an engine binary outside the game data";; esac
    fi
    r=$(phys_dir "$root")
    case "$w/" in "$r/"*)
        git -C "$root" check-ignore -q "${w#"$r"/}/$2" 2>/dev/null ||
            die "make-demo would write $w/$2 inside this repository, where git does not ignore it";;
    esac
}
# Fills engine_args for the make-demo run of <script copy name>: a local,
# windowed, silent single-player game that execs the script; pin capture is off
build_demo_args() {
    engine_args=(-datadir "${PINS_BASEDIR:-}" -portable +set vid_renderer soft +set vid_fullscreen 0 +set r_mode -1
        +set r_customwidth "${PINS_WIDTH:-640}" +set r_customheight "${PINS_HEIGHT:-480}" +set s_initsound 0
        +set vid_pauseonfocuslost 0 +set deathmatch 0 +set coop 0 +exec "$1")
}

cmd_make_demo() {
    local name=$1 replace=0; shift
    local bin="${PINS_BIN:-$root/release/quake2}" limit="${PINS_TIMEOUT:-600}"
    check_name demo "$name"
    while [ $# -gt 0 ]; do case $1 in
        --replace) replace=1; shift;;
        *) die "unknown option $1";; esac; done
    case $limit in ''|*[!0-9]*) die "PINS_TIMEOUT must be a number of seconds, got '$limit'";; esac
    check_count PINS_WIDTH "${PINS_WIDTH:-640}"; check_count PINS_HEIGHT "${PINS_HEIGHT:-480}"
    local script="$demo_dir/$name.cfg"
    [ -f "$script" ] || die "no demo script $name (see $demo_dir)"
    check_demo_script "$script" "$name"
    local map lo=$demo_lo hi=$demo_hi; map=$(sed -n 's/^map \([a-z0-9_]*\)$/\1/p' "$script")
    [[ $lo =~ ^[1-9][0-9]{0,8}$ ]] && [[ $hi =~ ^[1-9][0-9]{0,8}$ ]] && [ "$lo" -le "$hi" ] ||
        die "$name.cfg: the message range must be two positive integers, low first, got '$lo' and '$hi'"
    [ -z "${PINS_BIN:-}" ] || [ -f "$PINS_BIN" ] || die "no engine binary at $PINS_BIN (build the client, or set PINS_BIN)"
    if [ -n "${PINS_BASEDIR:-}" ]; then check_arg PINS_BASEDIR "$PINS_BASEDIR" path; fi
    # with -portable the engine writes into <binary dir>/baseq2
    phys_path "$(dirname "$bin")/baseq2"; local wdir=$phys_result
    [ -z "$phys_dangling" ] || die "the write directory $(dirname "$bin")/baseq2 holds a dangling symbolic link ($phys_dangling)"
    check_demo_place "$wdir" "pins_make_$name.log"
    # demos/ may be a link: check where it leads, and write there
    phys_path "$wdir/demos"; local ddir=$phys_result
    [ -z "$phys_dangling" ] || die "the demo directory $wdir/demos holds a dangling symbolic link ($phys_dangling)"
    check_demo_place "$ddir" "$name.dm2"
    local demo="$ddir/$name.dm2" copy="pins_make_$name.cfg" log="$wdir/pins_make_$name.log" f
    build_demo_args "$copy"
    if [ -n "${PINS_DRYRUN:-}" ]; then echo "$bin ${engine_args[*]}"; return 0; fi
    [ -x "$bin" ] || die "no engine binary at $bin (build the client, or set PINS_BIN)"
    [ -n "${PINS_BASEDIR:-}" ] && [ -d "$PINS_BASEDIR/baseq2" ] || die "PINS_BASEDIR must hold baseq2/ (owner game data, never committed)"
    if [ -z "${PINS_BIN:-}" ]; then
        local stale; stale=$(stale_engine "$bin" soft)
        [ -z "$stale" ] || die "the build is out of date ($stale); rebuild before making a demo"
    fi
    if [ -f "$demo" ] && [ "$replace" -ne 1 ]; then
        die "a demo already exists at $demo; give --replace to make it again (pins recorded from the old one then have another id)"
    fi
    for f in "$demo" "$demo.failed" "$wdir/$copy" "$log"; do
        [ ! -L "$f" ] || die "$f is a symbolic link; make-demo writes only plain files there"
    done
    mkdir -p "$wdir"; rm -f "$demo"; cp "$script" "$wdir/$copy"
    "$bin" "${engine_args[@]}" > "$log" 2>&1 &
    local pid=$! t=0 w status=0 timedout=0 missing=0
    while kill -0 "$pid" 2>/dev/null; do
        # a map that is not in the game data: the engine stays up, so stop it now
        if grep -q "^Can't find maps/" "$log" 2>/dev/null; then missing=1; fi
        if [ "$missing" -ne 0 ] || [ "$t" -ge "$limit" ]; then
            [ "$missing" -ne 0 ] || timedout=1
            kill "$pid" 2>/dev/null || true
            w=0; while kill -0 "$pid" 2>/dev/null && [ "$w" -lt 5 ]; do sleep 1; w=$((w+1)); done
            kill -0 "$pid" 2>/dev/null && kill -9 "$pid" 2>/dev/null || true
            break
        fi
        sleep 1; t=$((t+1))
    done
    wait "$pid" 2>/dev/null || status=$?
    rm -f "$wdir/$copy"
    if grep -q "^Can't find maps/" "$log" 2>/dev/null; then missing=1; fi
    local reason="" blocks=""
    [ ! -f "$demo" ] || blocks=$(demo_blocks "$demo")
    if [ "$missing" -ne 0 ]; then reason="the map $map is missing from PINS_BASEDIR ($(grep -m1 "^Can't find maps/" "$log"))"
    elif [ "$timedout" -ne 0 ]; then reason="engine timed out after ${limit}s (PINS_TIMEOUT)"
    elif [ "$status" -ne 0 ]; then reason="engine exited with status $status"
    elif [ ! -f "$demo" ]; then reason="the engine wrote no demo at $demo"
    elif [ "$blocks" = cut ]; then reason="the demo $demo ends before its end marker"
    elif [ "$blocks" -lt 10 ]; then reason="the demo $demo holds only $blocks messages"
    elif ! demo_in_range "$blocks" "$lo" "$hi"; then
        reason="the demo holds $blocks messages, outside $lo to $hi ($name.cfg): its waits lasted longer than the script means, so a record of it would not capture the frames documented; make it again on a quieter host"
    fi
    if [ -n "$reason" ]; then
        if [ -f "$demo" ]; then mv -f "$demo" "$demo.failed"; fi
        die "$reason (see $log)"
    fi
    echo "made $demo ($blocks messages, $lo to $hi wanted); record it with PINS_DEMO=$name.dm2"
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
    for name in pins_dir pins_scenario pins_capture pins_record pins_seed; do
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
    # PINS_SEED passes pins_seed beside pins_record, and is part of the pin id
    case $(PINS_SEED=42 PINS_DRYRUN=1 cmd_record gl1 world) in *"+set pins_record trace.q2r +set pins_seed 42 +demomap "*) ;;
        *) st_fail "PINS_SEED=42 must pass pins_seed 42 after pins_record"; return 1;; esac
    case $line in *pins_seed*) st_fail "pins_seed passed without PINS_SEED"; return 1;; esac
    [ "$(scenario_hash world)" != "$(PINS_SEED=42 scenario_hash world)" ] || st_fail "PINS_SEED must change the pin id" || return 1
    local sd
    for sd in x -1 1234567890 '1 2' '4;quit'; do
        if (PINS_SEED="$sd" PINS_DRYRUN=1 cmd_record gl1 world) >/dev/null 2>&1; then st_fail "PINS_SEED='$sd' must be refused"; return 1; fi
    done
    if (PINS_SEED=1 PINS_TRACE=0 PINS_DRYRUN=1 cmd_record gl1 world) >/dev/null 2>&1; then st_fail "PINS_SEED with PINS_TRACE=0 must be refused"; return 1; fi
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

# fake_demo_engine <path> <body>: stands in for the engine in make-demo. It
# finds the script copy it was told to exec in <its dir>/baseq2 (exit 3 if
# absent), then runs <body>, which may write the demo "$f"
fake_demo_engine() {
    printf '%s\n' '#!/bin/bash' \
        'd=$(dirname "$0")/baseq2; c=""' \
        'while [ $# -gt 0 ]; do [ "$1" = +exec ] && c=$2; shift; done' \
        '[ -n "$c" ] && [ -f "$d/$c" ] || { echo "no script copy $d/$c"; exit 3; }' \
        'mkdir -p "$d/demos"; f="$d/demos/water.dm2"; echo "recording to $f."' \
        "$2" > "$1"
    chmod +x "$1"
}
st_make_demo_args() { # TASK70: what make-demo accepts and passes to the engine
    local n rep
    for n in ../water Water -x 'wa ter' ''; do
        if (PINS_DRYRUN=1 cmd_make_demo "$n") >/dev/null 2>&1; then st_fail "make-demo name '$n' must be refused"; return 1; fi
    done
    mkdir -p "$tmp/dn"; printf '%s\n' '// pins: messages 10 20' 'map base1' 'record Up' 'stop' 'quit' > "$tmp/dn/Up.cfg"
    if (demo_dir="$tmp/dn" PINS_DRYRUN=1 cmd_make_demo Up) >/dev/null 2>&1; then st_fail "an upper-case name must be refused even when its script exists"; return 1; fi
    if (PINS_DRYRUN=1 cmd_make_demo nosuch) >/dev/null 2>&1; then st_fail "a name with no script must be refused"; return 1; fi
    if (PINS_DRYRUN=1 cmd_make_demo water --run 2) >/dev/null 2>&1; then st_fail "an unknown option must be refused"; return 1; fi
    if (PINS_DRYRUN=1 PINS_TIMEOUT=abc cmd_make_demo water) >/dev/null 2>&1; then st_fail "a non-numeric PINS_TIMEOUT must be refused"; return 1; fi
    if (PINS_DRYRUN=1 PINS_WIDTH=x cmd_make_demo water) >/dev/null 2>&1; then st_fail "PINS_WIDTH=x must be refused"; return 1; fi
    if (PINS_DRYRUN=1 PINS_BASEDIR="$tmp/g+quit" cmd_make_demo water) >/dev/null 2>&1; then st_fail "PINS_BASEDIR with + must be refused"; return 1; fi
    rep=$( (PINS_BIN="$tmp/no-such-engine" PINS_DRYRUN=1 cmd_make_demo water) 2>&1 ) && { st_fail "make-demo must refuse a missing PINS_BIN"; return 1; }
    rep=$(PINS_BASEDIR="$tmp/gd" PINS_DRYRUN=1 cmd_make_demo water) || st_fail "a dry run of make-demo water must pass: $rep" || return 1
    for n in "$tmp/fake-ok -datadir $tmp/gd -portable " '+set vid_renderer soft ' '+set s_initsound 0 ' '+set deathmatch 0 +set coop 0 ' ' +exec pins_make_water.cfg'; do
        case $rep in *"$n"*) ;; *) st_fail "the make-demo command line lacks '$n': $rep"; return 1;; esac
    done
    case $rep in *pins_*capture*|*pins_dir*|*+demomap*) st_fail "make-demo must not turn pin capture on or play a demo: $rep"; return 1;; esac
    case $rep in *" +exec pins_make_water.cfg") ;; *) st_fail "+exec must come last: $rep"; return 1;; esac
    # what make-demo passes exists in this tree
    for n in vid_pauseonfocuslost deathmatch coop; do
        grep -rqF "\"$n\"" "$root/src" || st_fail "make-demo sets $n, which is not a cvar in src/" || return 1
    done
    grep -qF 'Cmd_AddCommand("exec"' "$root/src/common/cmdparser.c" || st_fail "exec is not a command in src/common/cmdparser.c" || return 1
    grep -qF '"-portable"' "$root/src/backends/unix/main.c" || st_fail "-portable is not an argument of src/backends/unix/main.c" || return 1
}
st_make_demo_script() { # TASK70: a demo script holds only commands of this tree
    local s
    for s in "$demo_dir"/*.cfg; do
        (check_demo_script "$s" "$(basename "$s" .cfg)") || st_fail "the committed $(basename "$s") must pass the script check" || return 1
    done
    mkdir -p "$tmp/ds"; local demo_dir="$tmp/ds" bad good
    good=$(printf '%s\n' '// ok' '// pins: messages 10 20' 'alias w2 "wait;wait"' 'alias w4 "w2;w2"' 'map base1' 'w4' 'record good' '+lookdown' 'teleport 1 -2 3.5' '-lookdown' 'stop' 'quit')
    printf '%s\n' "$good" > "$tmp/ds/good.cfg"
    (check_demo_script "$tmp/ds/good.cfg" good) || st_fail "a plain demo script must pass" || return 1
    # each case: the good script recording "bad", with the teleport line replaced
    printf '%s\n' "$good" | sed 's/^record good$/record bad/' > "$tmp/ds/bad.cfg"
    (check_demo_script "$tmp/ds/bad.cfg" bad) || st_fail "the unchanged script recording its own name must pass" || return 1
    for bad in 'nosuchcmd' 'record other' 'record bad' 'map base2' 'map ../x' 'exec x.cfg' '// a; quit' '// a "b' 'teleport ../x' ' wait' 'wait  ' 'echo "x"' \
        'alias w8 "w2;nosuchcmd"' 'alias w8 "w2; wait"' 'bind x "quit"' 'wait;quit' '// pins: messages 10 20' \
        '// pins: messages 0 999' '// pins: messages 097 124' '// pins: messages  97 124' '// pins: messages 97  124' '// pins: messages 97 124 ' \
        '// pins: messages 97' '// pins: messages' '// pins: messages  ' '// pins:messages 97 124' '// pins: message 97 124' '// pins: messages 1234567890 1234567891'; do
        printf '%s\n' "$good" | sed 's/^record good$/record bad/' | awk -v b="$bad" '$0 == "teleport 1 -2 3.5" {print b; next} {print}' > "$tmp/ds/bad.cfg"
        if (check_demo_script "$tmp/ds/bad.cfg" bad) >/dev/null 2>&1; then st_fail "demo script line '$bad' must be refused"; return 1; fi
    done
    printf '%s\n' "$good" | grep -v '^map ' | sed 's/^record good$/record bad/' > "$tmp/ds/bad.cfg"
    if (check_demo_script "$tmp/ds/bad.cfg" bad) >/dev/null 2>&1; then st_fail "a script without a map line must be refused"; return 1; fi
    printf '%s\n' "$good" | grep -v '^record ' > "$tmp/ds/bad.cfg"
    if (check_demo_script "$tmp/ds/bad.cfg" bad) >/dev/null 2>&1; then st_fail "a script that records nothing must be refused"; return 1; fi
    printf '%s\n' "$good" | sed 's/^record good$/record bad/' | grep -v '^stop$' > "$tmp/ds/bad.cfg"
    if (check_demo_script "$tmp/ds/bad.cfg" bad) >/dev/null 2>&1; then st_fail "a script without stop before quit must be refused"; return 1; fi
    { printf '%s\n' "$good" | sed 's/^record good$/record bad/'; echo wait; } > "$tmp/ds/bad.cfg"
    if (check_demo_script "$tmp/ds/bad.cfg" bad) >/dev/null 2>&1; then st_fail "a script that does not end with quit must be refused"; return 1; fi
    for s in '/^\/\/ pins: messages/d' 's/^\/\/ pins: messages 10 20$/\/\/ pins: messages 20 10/' 's/^\/\/ pins: messages 10 20$/\/\/ pins: messages 5 20/' \
        's/^\/\/ pins: messages 10 20$/\/\/ pins: messages 10 1234567890/'; do
        printf '%s\n' "$good" | sed 's/^record good$/record bad/' | sed "$s" > "$tmp/ds/bad.cfg"
        if (check_demo_script "$tmp/ds/bad.cfg" bad) >/dev/null 2>&1; then st_fail "a script whose message range is edited by '$s' must be refused"; return 1; fi
    done
    # the range make-demo compares with comes from the check, and is verified again before the engine starts
    (check_demo_script "$tmp/ds/good.cfg" good; [ "$demo_lo $demo_hi" = "10 20" ]) || st_fail "the script check must set the range it accepted" || return 1
    for s in '97|124' 'x|124' '097|124' '124|97' '0|999' "97 124
0|124" '|' '97|'; do
        if (demo_dir="$root/scripts/pins/demos"; check_demo_script() { demo_lo=${s%|*}; demo_hi=${s#*|}; }
            PINS_DRYRUN=1 PINS_BASEDIR="$tmp/gd" cmd_make_demo water) >/dev/null 2>&1; then
            [ "$s" = '97|124' ] && continue
            st_fail "make-demo must refuse the message range '$s' before the engine starts"; return 1
        elif [ "$s" = '97|124' ]; then st_fail "a dry run with the range 97 124 must pass"; return 1
        fi
    done
    # a comparison that errors is never in range
    demo_in_range 100 97 124 && demo_in_range 97 97 124 && demo_in_range 124 97 124 || st_fail "100, 97 and 124 are in 97 to 124" || return 1
    for s in '96|97|124' '125|97|124' "500|97 124
0|124" '500|97|124
0 999' 'cut|97|124' '|97|124' '100|x|124' '100|97|'; do
        local m=${s%%|*} l=${s#*|}; l=${l%|*}
        if demo_in_range "$m" "$l" "${s##*|}" 2>/dev/null; then st_fail "demo_in_range must be false for '$s'"; return 1; fi
    done
}
st_make_demo_run() { # TASK70: make-demo passes only with a whole demo, and fails fast on a missing map
    local ok='for i in $(seq 100); do printf "\001\000\000\000x"; done > "$f"; printf "\377\377\377\377" >> "$f"; exit 0'
    local rep t0 d="$tmp/baseq2/demos/water.dm2"
    rm -rf "$tmp/baseq2"
    fake_demo_engine "$tmp/fake-demo" "$ok"
    rep=$(PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-demo" cmd_make_demo water 2>&1) || st_fail "make-demo must pass with a whole demo: $rep" || return 1
    [ -f "$d" ] && [ ! -f "$tmp/baseq2/pins_make_water.cfg" ] || st_fail "the demo must be left and the script copy removed" || return 1
    case $rep in *"(100 messages, 97 to 124 wanted)"*"PINS_DEMO=water.dm2"*) ;; *) st_fail "make-demo must report the demo and how to record it: $rep"; return 1;; esac
    echo keep >> "$d"
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-demo" cmd_make_demo water) >/dev/null 2>&1; then st_fail "an existing demo must not be replaced without --replace"; return 1; fi
    grep -q keep "$d" || st_fail "the refused make-demo must leave the demo untouched" || return 1
    (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-demo" cmd_make_demo water --replace) >/dev/null 2>&1 || st_fail "--replace must make the demo again" || return 1
    if grep -q keep "$d"; then st_fail "--replace must leave a fresh demo"; return 1; fi
    if (PINS_BIN="$tmp/fake-demo" cmd_make_demo water --replace) >/dev/null 2>&1; then st_fail "make-demo must refuse without PINS_BASEDIR"; return 1; fi
    # a map missing from the game data: the engine stays up, the driver must not wait for PINS_TIMEOUT
    fake_demo_engine "$tmp/fake-demo-nomap" 'echo "Can'"'"'t find maps/base1.bsp"; trap "exit 0" TERM; while :; do sleep 1; done'
    t0=$SECONDS
    rep=$( (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-demo-nomap" PINS_TIMEOUT=60 cmd_make_demo water --replace) 2>&1 ) && { st_fail "make-demo must fail when the map is missing"; return 1; }
    [ $((SECONDS - t0)) -lt 10 ] || st_fail "a missing map must fail at once, not after PINS_TIMEOUT" || return 1
    case $rep in *"map base1 is missing"*) ;; *) st_fail "a missing map must be named: $rep"; return 1;; esac
    local body why
    for body in 'exit 0|wrote no demo' \
        'printf "\001\000\000\000x\001\000\000" > "$f"; exit 0|ends before its end marker' \
        'printf "\001\000\000\000x\377\377\377\377" > "$f"; exit 0|holds only 1 messages' \
        'for i in $(seq 96); do printf "\001\000\000\000x"; done > "$f"; printf "\377\377\377\377" >> "$f"; exit 0|holds 96 messages, outside 97 to 124' \
        'for i in $(seq 125); do printf "\001\000\000\000x"; done > "$f"; printf "\377\377\377\377" >> "$f"; exit 0|holds 125 messages, outside 97 to 124' \
        "${ok% exit 0} exit 139|status 139"; do
        why=${body##*|}; fake_demo_engine "$tmp/fake-demo-bad" "${body%|*}"
        rep=$( (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-demo-bad" cmd_make_demo water --replace) 2>&1 ) && { st_fail "make-demo must fail when it $why"; return 1; }
        case $rep in *"$why"*) ;; *) st_fail "make-demo must say it $why: $rep"; return 1;; esac
        [ ! -f "$d" ] || st_fail "a failed make-demo must not leave $d where PINS_DEMO finds it ($why)" || return 1
        case $why in *outside*) [ -f "$d.failed" ] || st_fail "a demo outside its range must be kept as $d.failed" || return 1;; esac
    done
    fake_demo_engine "$tmp/fake-demo-hang" 'trap "exit 0" TERM; while :; do sleep 1; done'
    rep=$( (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/fake-demo-hang" PINS_TIMEOUT=2 cmd_make_demo water --replace) 2>&1 ) && { st_fail "make-demo must fail when the engine hangs"; return 1; }
    case $rep in *"timed out after 2s"*) ;; *) st_fail "the timeout must be named: $rep"; return 1;; esac
}
st_make_demo_place() { # TASK70: the demo is never written into the game data or a tracked part of the repository
    local rep r="$tmp/rp"
    mkdir -p "$tmp/gd/bin"; cp "$tmp/fake-ok" "$tmp/gd/bin/quake2"
    rep=$( (PINS_BASEDIR="$tmp/gd" PINS_BIN="$tmp/gd/bin/quake2" PINS_DRYRUN=1 cmd_make_demo water) 2>&1 ) && { st_fail "a binary inside PINS_BASEDIR must be refused"; return 1; }
    case $rep in *"inside PINS_BASEDIR"*) ;; *) st_fail "the refusal must name PINS_BASEDIR: $rep"; return 1;; esac
    ln -s "$tmp/gd" "$tmp/gd-link"
    if (PINS_BASEDIR="$tmp/gd-link" PINS_BIN="$tmp/gd/bin/quake2" PINS_DRYRUN=1 cmd_make_demo water) >/dev/null 2>&1; then st_fail "PINS_BASEDIR reached through a link must be refused too"; return 1; fi
    # a demos/ beside the binary that is a link into the game data
    local p="$tmp/pl" f
    mkdir -p "$p/baseq2" "$tmp/gd/baseq2/demos"; cp "$tmp/fake-ok" "$p/quake2"; ln -s "$tmp/gd/baseq2/demos" "$p/baseq2/demos"
    rep=$( (PINS_BASEDIR="$tmp/gd" PINS_BIN="$p/quake2" PINS_DRYRUN=1 cmd_make_demo water) 2>&1 ) && { st_fail "a demos/ linked into PINS_BASEDIR must be refused"; return 1; }
    case $rep in *"demos/water.dm2 inside PINS_BASEDIR"*) ;; *) st_fail "the linked demos/ must be refused for its place: $rep"; return 1;; esac
    rm "$p/baseq2/demos"; ln -s "$tmp/no-such-dir" "$p/baseq2/demos"
    if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$p/quake2" PINS_DRYRUN=1 cmd_make_demo water) >/dev/null 2>&1; then st_fail "a dangling demos/ link must be refused"; return 1; fi
    # the demo, the script copy or the log as a link: nothing is written through it
    rm "$p/baseq2/demos"; mkdir -p "$p/baseq2/demos" "$tmp/elsewhere"
    fake_demo_engine "$p/quake2" 'for i in $(seq 100); do printf "\001\000\000\000x"; done > "$f"; printf "\377\377\377\377" >> "$f"; exit 0'
    (PINS_BASEDIR="$tmp/gd" PINS_BIN="$p/quake2" cmd_make_demo water --replace) >/dev/null 2>&1 || st_fail "make-demo must pass beside a plain demos/" || return 1
    for f in demos/water.dm2 demos/water.dm2.failed pins_make_water.cfg pins_make_water.log; do
        echo x > "$tmp/elsewhere/x"; rm -f "$p/baseq2/$f"; ln -s "$tmp/elsewhere/x" "$p/baseq2/$f"
        if (PINS_BASEDIR="$tmp/gd" PINS_BIN="$p/quake2" cmd_make_demo water --replace) >/dev/null 2>&1; then st_fail "make-demo must refuse when $f is a link"; return 1; fi
        [ "$(cat "$tmp/elsewhere/x")" = x ] || st_fail "make-demo must not write through the link $f" || return 1
        rm "$p/baseq2/$f"
    done
    command -v git >/dev/null 2>&1 || { echo "       make_demo_place: no git, skipped"; return 0; }
    mkdir -p "$r/bin" "$r/release"; git -C "$r" init -q; printf '/release/\n' > "$r/.gitignore"; ln -s "$root/src" "$r/src"
    cp "$tmp/fake-ok" "$r/bin/quake2"; cp "$tmp/fake-ok" "$r/release/quake2"
    rep=$( (root="$r" PINS_BIN="$r/bin/quake2" PINS_DRYRUN=1 cmd_make_demo water) 2>&1 ) && { st_fail "a write directory the repository tracks must be refused"; return 1; }
    case $rep in *"git does not ignore"*) ;; *) st_fail "the refusal must say git does not ignore it: $rep"; return 1;; esac
    (root="$r" PINS_BIN="$r/release/quake2" PINS_DRYRUN=1 cmd_make_demo water) >/dev/null 2>&1 || st_fail "the git-ignored release/ must be accepted" || return 1
    # a demos/ in the ignored release/ linked into a tracked directory of the repository
    mkdir -p "$r/release/baseq2" "$r/tracked"; ln -s "$r/tracked" "$r/release/baseq2/demos"
    rep=$( (root="$r" PINS_BIN="$r/release/quake2" PINS_DRYRUN=1 cmd_make_demo water) 2>&1 ) && { st_fail "a demos/ linked into a tracked directory must be refused"; return 1; }
    case $rep in *"tracked/water.dm2 inside this repository"*) ;; *) st_fail "the linked demos/ must be refused for its place: $rep"; return 1;; esac
    rm "$r/release/baseq2/demos"
}
st_make_demo_case() { # TASK70: a place spelled in another letter case is the same place
    local rep c="$tmp/case" ok='for i in $(seq 100); do printf "\001\000\000\000x"; done > "$f"; printf "\377\377\377\377" >> "$f"; exit 0'
    mkdir -p "$c/GDCASE/baseq2/demos" "$c/GDCASE/bin" "$c/e3/baseq2"
    if [ ! -d "$c/gdcase" ]; then echo "       make_demo_case: $c is on a case-sensitive volume, skipped"; return 0; fi
    # the reviewers' fixtures: demos/ linked into the game data under another spelling, then no link
    fake_demo_engine "$c/e3/quake2" "$ok"; ln -s "$c/gdcase/baseq2/demos" "$c/e3/baseq2/demos"
    rep=$( (PINS_BASEDIR="$c/GDCASE" PINS_BIN="$c/e3/quake2" cmd_make_demo water) 2>&1 ) && { st_fail "a demos/ linked into PINS_BASEDIR in another case must be refused: $rep"; return 1; }
    case $rep in *"inside PINS_BASEDIR"*) ;; *) st_fail "the refusal must name PINS_BASEDIR: $rep"; return 1;; esac
    [ ! -e "$c/GDCASE/baseq2/demos/water.dm2" ] || st_fail "nothing may land in the game data" || return 1
    fake_demo_engine "$c/GDCASE/bin/quake2" "$ok"
    rep=$( (PINS_BASEDIR="$c/gdcase" PINS_BIN="$c/GDCASE/bin/quake2" cmd_make_demo water) 2>&1 ) && { st_fail "a binary inside PINS_BASEDIR in another case must be refused: $rep"; return 1; }
    case $rep in *"inside PINS_BASEDIR"*) ;; *) st_fail "the refusal must name PINS_BASEDIR: $rep"; return 1;; esac
    [ ! -e "$c/GDCASE/bin/baseq2" ] || st_fail "nothing may be written beside a binary in the game data" || return 1
    # the repository root spelled in another case
    command -v git >/dev/null 2>&1 || { echo "       make_demo_case: no git, repository case skipped"; return 0; }
    mkdir -p "$c/RP/bin"; git -C "$c/RP" init -q; printf '/release/\n' > "$c/RP/.gitignore"; ln -s "$root/src" "$c/RP/src"
    cp "$tmp/fake-ok" "$c/RP/bin/quake2"
    rep=$( (root="$c/rp" PINS_BIN="$c/RP/bin/quake2" PINS_DRYRUN=1 cmd_make_demo water) 2>&1 ) && { st_fail "a tracked write directory under another spelling of the repository must be refused: $rep"; return 1; }
    case $rep in *"git does not ignore"*) ;; *) st_fail "the refusal must say git does not ignore it: $rep"; return 1;; esac
}
st_demo_sum() { # TASK70: a demo beside the binary is part of the pin id
    rm -rf "$tmp/baseq2"
    local a b c w
    a=$(PINS_DEMO=water.dm2 run_id skywater 1); w=$(run_id world 1)
    mkdir -p "$tmp/baseq2/demos"; printf 'one' > "$tmp/baseq2/demos/water.dm2"
    b=$(PINS_DEMO=water.dm2 run_id skywater 1)
    printf 'two' > "$tmp/baseq2/demos/water.dm2"
    c=$(PINS_DEMO=water.dm2 run_id skywater 1)
    [ "$a" != "$b" ] && [ "$b" != "$c" ] || st_fail "the pin id must change with the demo beside the binary: $a $b $c" || return 1
    [ "$w" = "$(run_id world 1)" ] && [ "$w" = "$cid" ] || st_fail "a demo found only in the game data must keep its id" || return 1
    rm -rf "$tmp/baseq2"
}

# scratch_outside_git <name>: sets tmp to a fresh directory that check_store
# accepts, not inside a git work tree (TMPDIR may be in one), so it tries
# TMPDIR, then /tmp, then /var/tmp
scratch_outside_git() {
    local base t
    tmp=""
    for base in "${TMPDIR:-/tmp}" /tmp /var/tmp; do
        [ -d "$base" ] || continue
        t=$(mktemp -d "$base/$1.XXXXXX") || continue
        t=$(cd "$t" && pwd -P)
        if ! git -C "$t" rev-parse --is-inside-work-tree >/dev/null 2>&1; then tmp=$t; break; fi
        rm -rf "${t:?}"
    done
    [ -n "$tmp" ] || die "no scratch directory outside a git work tree under TMPDIR, /tmp or /var/tmp"
}

cmd_selftest() {
    # the selftest must not depend on, or act on, the caller's environment
    unset PINS_BIN PINS_BASEDIR PINS_DEMO PINS_WIDTH PINS_HEIGHT PINS_EVERY PINS_TIMEOUT PINS_DRYRUN PINS_STORE PINS_DIFF PINS_TRACE PINS_SEED
    scratch_outside_git pins
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
        scenario_lines store_place fast_fail demo_name replace engine_id metal_shim usage \
        make_demo_args make_demo_script make_demo_run make_demo_place make_demo_case demo_sum; do
        rm -rf "$PINS_STORE/world"
        if ( set -e; "st_$c" ); then echo "  ok   $c"; else echo "  FAIL $c"; failed="$failed $c"; fi
    done
    [ -z "$failed" ] || die "selftest failed:$failed"
    echo "pins selftest: ok"
}

case "${1:-}" in
    record)   [ $# -ge 3 ] || die "usage: pins.sh record <renderer> <scenario> [--run N] [--replace]"; shift; cmd_record "$@";;
    compare)  [ $# -ge 3 ] || die "usage: pins.sh compare <renderer> <scenario> [--ref r] [--ref-id d] [--run N | --cand-id d] [--threshold t] [--bad-threshold b]"; shift; cmd_compare "$@";;
    make-demo) [ $# -ge 2 ] || die "usage: pins.sh make-demo <name> [--replace]"; shift; cmd_make_demo "$@";;
    list)     cmd_list;;
    selftest) cmd_selftest;;
    evidence) shift; cmd_evidence "$@";;
    evidence-publish) shift; cmd_evidence_publish "$@";;
    evidence-selftest) cmd_evidence_selftest;;
    *) usage; exit 1;;
esac
