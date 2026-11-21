#!/bin/bash
# tests/check-metalfx-report.sh -- gate check for the MetalFX report generator (TASK114).
# Offline: no engine, no game data. Feeds synthetic results, synthetic images and a
# fake engine to scripts/metalfx-report/mfx.py and ssim.c, and checks:
#   - the output path is refused inside a git work tree, through a symbolic link at any
#     component, and when unwritable; a failing git is an error, never a pass;
#   - an option row whose cvar the engine does not report produces no row, one it
#     reports does, and the engine's own cvarlist is the only source of that;
#   - a second host's numbers show beside the first, as a range, and its images never do;
#   - the HTML refers to nothing outside itself;
#   - the structural measure is 1 on identical images and less on different ones;
#   - a failing or silent engine run is an error, and an option whose probe fails is skipped;
#   - every engine run starts from the engine's defaults: the binary's config.cfg is set aside,
#     cleared before each run and put back after it (also after an error and on SIGTERM), and a
#     second run is not affected by the first;
#   - --bin: the quality records get the same binary (PINS_BIN), the freshness check and the
#     recorded commit are those of the binary's own tree, a binary outside one is said to be unchecked;
#   - one run per engine binary and per scenarios folder (a directory flock; a held lock is exit 3), a
#     config.cfg newer than a saved one is kept, pins.sh runs in a process group that SIGTERM ends;
#   - the cvars a run sets reach the engine through one console script (+exec of mfx-run.cfg in the
#     binary's baseq2), not as +set pairs: a scenario of 70 cvars runs with an argument count far under
#     the engine's 128, plan prints that count, a value that cannot be written into a console line is
#     refused with its cvar name, and the script is removed with the held config;
#   - a run split into chunks (one --out each) is combined by report --dir A --dir B: the same rows as
#     one run, a row two chunks both hold, a chunk of another engine commit or setting refused;
#   - the host lock (the real scripts/bench/bench.sh, a private BENCH_STATE, a stub pins.sh that takes it
#     as scripts/pins.sh does): the generator holds it once for its whole run, so while another job holds
#     it the generator waits, and no record times out on that wait, and each record and engine run passes
#     straight through; it is off, with its one line, for the entry points' reasons; a timeout, a SIGTERM,
#     a Ctrl-C, a second SIGTERM during the stop and a pins.sh that ignores TERM each end the run under the
#     lock, its grandchild included, and leave the lock free (TERM first, KILL after a wait that follows
#     BENCH_STOP_GRACE and lands before the lock shell's own KILL);
#   - the run end to end in a tree copy with a stub scripts/pins.sh: an option's SSIM is below 1, its crop
#     is not the native one, the crop frame is the middle one by number; nothing touches the checkout;
#   - the run paths: an existing results folder is refused, --replace empties the old crops, frames
#     are paired by number (and ordered numerically), the stats line is read with several reset words;
#   - names read from another host's results are plain names, a crop is read only as a plain file
#     of the local results folder and only for a row that ran, a FIFO is not waited on, a hard link
#     at the report path is replaced and not written through.
# Then every assertion is run against a mutant of the line it covers and must fail.
# Needs bash, cc, python3 (standard library) and git. Isolated from the caller's git
# environment (GIT_*, global and system config) and home directory.
set -u
cd "$(dirname "$0")/.."
root=$(pwd -P)

for v in $(env | sed -n 's/^\(GIT_[A-Za-z_]*\)=.*/\1/p'); do unset "$v"; done
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null GIT_CONFIG_NOSYSTEM=1 GIT_TERMINAL_PROMPT=0
tmp=$(cd "$(mktemp -d "${TMPDIR:-/tmp}/mfxtest.XXXXXX")" && pwd -P) || exit 1
trap 'chmod -R u+w "${tmp:?}" 2>/dev/null; rm -rf "${tmp:?}"' EXIT
export HOME="$tmp/home"; mkdir "$HOME" || exit 1

HELP="$root/tests/metalfx-report-helper.py"
SRC="$root/scripts/metalfx-report"
fail=0
FAILED=" "
bad() { echo "check-metalfx-report: $*"; fail=1; }
ko() { FAILED="$FAILED$1 "; [ "${VERBOSE:-0}" = 0 ] || echo "  failed: $1 ($(printf '%s' "$2" | tr '\n' ' ' | cut -c1-160))"; }
want_rc() { [ "$3" -eq "$2" ] || ko "$1" "rc $3, want $2"; }
want_has() { case $3 in *"$2"*) ;; *) ko "$1" "no '$2' in: $3";; esac; }
want_not() { case $3 in *"$2"*) ko "$1" "'$2' found";; esac; }

cc -O2 -o "$tmp/framediff" "$root/scripts/pins/framediff.c" -lm || { bad "cannot build framediff"; exit 1; }

# fixtures
mkdir "$tmp/repo" "$tmp/plain" "$tmp/fakegit" "$tmp/real" "$tmp/ro" "$tmp/stubfail" "$tmp/stubtrue" "$tmp/scen" "$tmp/out" \
    "$tmp/makefail" "$tmp/maketrue" "$tmp/basedir" "$tmp/basedir/baseq2"
git -C "$tmp/repo" init -q || { bad "git init failed"; exit 1; }
mkdir "$tmp/fakegit/.git"
ln -s "$tmp/real" "$tmp/link"
mkdir "$tmp/plain/dest"; ln -s "$tmp/plain/dest" "$tmp/plain/lnk"
chmod 555 "$tmp/ro"
printf '#!/bin/sh\necho boom >&2\nexit 1\n' > "$tmp/stubfail/git"
printf '#!/bin/sh\necho true\nexit 0\n' > "$tmp/stubtrue/git"
printf '#!/bin/sh\nexit 1\n' > "$tmp/makefail/make"
printf '#!/bin/sh\nexit 0\n' > "$tmp/maketrue/make"
chmod +x "$tmp/stubfail/git" "$tmp/stubtrue/git" "$tmp/makefail/make" "$tmp/maketrue/make"
printf 'set timedemo 1\nset r_drawworld 1\nset sw_waterwarp 1\n' > "$tmp/scen/world.cfg"
printf 'set timedemo 1\nset r_rt_shadows 1\n' > "$tmp/scen/rt-shadows.cfg"
{ echo 'set timedemo 1'; for i in $(seq 1 70); do echo "set r_rt_w$i 1"; done; } > "$tmp/scen/rt-wide.cfg"
# the engine's cvarlist: flags, name, "value". Mentions in other lines are not cvars.
cvl() { printf '%s\n' '*    r_metalfx "0"' "     r_metalfx_modes \"$1\"" '*    r_metalfx_scale "0"' '*    r_metalfx_interp "0"' \
    '     timedemo "0"' '     r_drawworld "1"'; }
{ cvl 3; echo 'ref_metal: warning r_rt_denoiser "1"'; echo 'Unknown command "r_rt_denoiser"'; echo '5 cvars'; } > "$tmp/cv-absent"
{ cvl 31; echo '*    r_validation "0"'; echo '     r_rt_denoiser "0"'; echo '     r_rt_shadows "0"'; echo '12 cvars'; } > "$tmp/cv-present"
{ cvl 1099511627779; echo '12 cvars'; } > "$tmp/cv-big"
{ cvl 31; echo '     r_rt_shadows "0"'; for i in $(seq 1 70); do echo "     r_rt_w$i \"0\""; done; } > "$tmp/cv-wide"
{ cvl 31; echo '     r_rt_other "5"'; echo '*    r_rt_arch "2"'; echo '*    r_metalfx_modes "7"'; echo '*  - r_rt_fixed "1"'; echo '     r_rt_shadows "0"'; echo '     r_rt_denoiser "0"'; } > "$tmp/cv-defaults"
printf 'bound\tbound\tstd\tr_metalfx=40\n' > "$tmp/opts-bound"
"$tmp/framediff" --gen "$tmp/a.tga" 64 48 1 || exit 1
"$tmp/framediff" --gen "$tmp/other.tga" 64 48 7 || exit 1
"$tmp/framediff" --perturb "$tmp/a.tga" "$tmp/shift.tga" 10 || exit 1
"$tmp/framediff" --block "$tmp/a.tga" "$tmp/block.tga" 0 0 32 24 255 || exit 1
"$tmp/framediff" --gen "$tmp/small.tga" 32 24 1 || exit 1
# a fake engine: prints what a real one does for the probe and the timing run
mkfake() { # name validation(yes|no) status fps(yes|no) [noexec]
    { echo '#!/bin/sh'
      [ "${5:-}" = noexec ] || echo 'echo "execing mfx-run.cfg."'
      [ "$2" = yes ] && echo 'echo "Metal API Validation Enabled"'
      echo "cat '$tmp/cv-present'"
      [ "$4" = yes ] && echo 'echo "689 frames, 6.0 seconds: 114.9 fps"'
      echo "exit $3"; } > "$tmp/$1"
    chmod +x "$tmp/$1"
}
mkfake eng-good yes 0 yes
mkfake eng-fail yes 1 yes
mkfake eng-novalid no 0 yes
mkfake eng-nofps yes 0 no
mkfake eng-noexec yes 0 yes noexec
# a probe that fails for one option only
{ echo '#!/bin/sh'; echo 'case "$*" in *r_metalfx_interp*) exit 1;; esac'
  echo 'grep -q r_metalfx_interp "$(dirname "$0")/baseq2/mfx-run.cfg" 2>/dev/null && exit 1'; echo 'echo "execing mfx-run.cfg."'; echo 'echo "Metal API Validation Enabled"'
  echo "cat '$tmp/cv-present'"; echo 'exit 0'; } > "$tmp/eng-failopt"
# an engine that reads and rewrites <its dir>/baseq2/config.cfg as the real one does (it saves the +set
# arguments), and notes in $MFX_TRACE what config it found at each start
{ echo '#!/bin/sh'
  echo 'd=$(dirname "$0")'
  echo '[ -z "${MFX_TRACE:-}" ] || echo START >> "$MFX_TRACE"'
  echo 'if [ -f "$d/baseq2/config.cfg" ]; then cat "$d/baseq2/config.cfg"; [ -z "${MFX_TRACE:-}" ] || sed "s/^/seen: /" "$d/baseq2/config.cfg" >> "$MFX_TRACE"; fi'
  echo '[ -z "${MFX_ARGC:-}" ] || echo "$# $(grep -c "^set " "$d/baseq2/mfx-run.cfg" 2>/dev/null)" >> "$MFX_ARGC"'
  echo '[ ! -f "$d/baseq2/mfx-run.cfg" ] || echo "execing mfx-run.cfg."'
  echo 'echo "Metal API Validation Enabled"'
  echo 'mkdir -p "$d/baseq2"; : > "$d/baseq2/config.cfg.n"'
  echo 'while [ $# -gt 0 ]; do if [ "$1" = +set ]; then echo "set $2 \"$3\"" >> "$d/baseq2/config.cfg.n"; shift 3; elif [ "$1" = +exec ]; then sed -n "s/^set \([^ ]*\) \(.*\)\$/set \1 \"\2\"/p" "$d/baseq2/$2" >> "$d/baseq2/config.cfg.n"; shift 2; else shift; fi; done'
  # the cvarlist reports each cvar at the value the arguments set, as the engine does
  echo "awk 'NR==FNR { v[\$2] = \$3; next } { n = substr(\$0, 6); sub(/ .*/, \"\", n); if (n in v && substr(\$0, 5, 1) == \" \") \$0 = substr(\$0, 1, 5) n \" \" v[n]; print }' \"\$d/baseq2/config.cfg.n\" '$tmp/cv-present'"
  echo 'echo "689 frames, 6.0 seconds: 114.9 fps"'
  echo 'echo "ref_metal: gpu_ms 1.250 frame"'
  echo 'mv "$d/baseq2/config.cfg.n" "$d/baseq2/config.cfg"'
  echo 'exit 0'; } > "$tmp/eng-cfg"
printf '#!/bin/sh\nexec sleep 60\n' > "$tmp/eng-slow"
chmod +x "$tmp/eng-failopt" "$tmp/eng-cfg" "$tmp/eng-slow"
# a stub scripts/pins.sh for a tree copy: what a complete record writes (the status, an engine.log with the
# validation line) and three frames, 60, 120 and 180, that differ between the native record and an option
{ echo '#!/bin/sh'
  echo '[ "$1" = record ] || exit 2'
  echo 'name=$3; d="$PINS_STORE/$name/metal/stub1"; mkdir -p "$d" || exit 1'
  # the real record puts every `set` line of the scratch scenario on the engine command line as three arguments,
  # besides 42 words of its own, and the engine refuses more than 128
  echo 'n=$(grep -c "^set " "$(dirname "$0")/pins/scenarios/$name.cfg"); [ $((42 + 3 * n)) -le 128 ] || { echo "stub: the engine would get $((42 + 3 * n)) arguments" >&2; exit 1; }'
  echo 'if [ -n "${MFX_STUB_CHILD:-}" ]; then sleep 300 & echo $! > "$MFX_STUB_CHILD"; wait; fi'
  echo 'case $name in *-native) src=$MFX_NATIVE_TGA;; *) src=$MFX_OPT_TGA;; esac'
  echo 'for n in 60 120 180; do cp "$src" "$d/$name-$n.tga" || exit 1; done'
  echo 'echo "Metal API Validation Enabled" > "$d/engine.log"'
  echo 'echo complete > "$d/complete"'; } > "$tmp/pins-stub"
# the same stub run the way scripts/pins.sh runs itself: as the command of the real host lock
# (scripts/bench/bench.sh lock run pins-record, the bench harness copied into the tree). Under a generator
# that holds the lock itself, the lock passes straight through and the stub's pid is the generator's child;
# otherwise it is the lock shell, and the run under it is in a process group of the lock's own. Under the
# lock it writes its pid (MFX_STUB_SH), its parent's (MFX_STUB_PARENT), the holder it sees (MFX_STUB_LABEL)
# and, with MFX_STUB_DEAF, ignores TERM, HUP and INT (so does its child); then it records as the stub above
{ echo '#!/bin/sh'
  echo 'if [ "${BENCH_LOCK_ENTRY:-}" != pins-record ] || [ -z "${BENCH_LOCK_TOKEN:-}" ]; then'
  echo '    exec sh "$(dirname "$0")/bench/bench.sh" lock run pins-record -- env BENCH_LOCK_ENTRY=pins-record /bin/sh "$0" "$@"'
  echo 'fi'
  echo '[ -z "${MFX_STUB_SH:-}" ] || echo $$ > "$MFX_STUB_SH"'
  echo '[ -z "${MFX_STUB_PARENT:-}" ] || echo $PPID > "$MFX_STUB_PARENT"'
  echo '[ -z "${MFX_STUB_LABEL:-}" ] || sh "$(dirname "$0")/bench/bench.sh" lock status >> "$MFX_STUB_LABEL"'
  echo '[ -z "${MFX_STUB_DEAF:-}" ] || trap "" TERM HUP INT'
  sed 1d "$tmp/pins-stub"; } > "$tmp/pins-stub-locked"
# an engine like eng-cfg that first notes its LC_ALL and the holder of the host lock it runs under (MFX_ENG_LABEL)
{ echo '#!/bin/sh'
  echo '[ -z "${MFX_ENG_LABEL:-}" ] || { printf "lc=%s " "${LC_ALL-unset}"; sh "$MFX_BENCH" lock status; } >> "$MFX_ENG_LABEL"'
  sed 1d "$tmp/eng-cfg"; } > "$tmp/eng-lockseen"
chmod +x "$tmp/pins-stub" "$tmp/pins-stub-locked" "$tmp/eng-lockseen"
# starts python3 -I with SIGINT at its default, which a background job of this script ignores, so that a
# Ctrl-C (SIGINT) reaches the generator as KeyboardInterrupt
INTDFL='import os, signal, sys; signal.signal(signal.SIGINT, signal.SIG_DFL); os.execv(sys.executable, [sys.executable, "-I"] + sys.argv[1:])'
# a tree copy for the generator: its own scripts/pins/scenarios and a stub scripts/pins.sh, so that a run
# writes and deletes nothing in the caller's checkout
mktree() { # <tree> <mfx.py>
    mkdir -p "$1/scripts/metalfx-report" "$1/scripts/pins" || return 1
    cp "$SRC/options.tsv" "$1/scripts/metalfx-report/"; cp "$2" "$1/scripts/metalfx-report/mfx.py"
    cp -R "$root/scripts/pins/scenarios" "$1/scripts/pins/scenarios"; cp "$tmp/pins-stub" "$1/scripts/pins.sh"
}
# an engine in a place: <dir>/quake2 with the owner's config.cfg beside it
mkeng() { # dir engine-script
    mkdir -p "$1/baseq2"; cp "$2" "$1/quake2"; chmod +x "$1/quake2"
    printf 'set owner_cvar "7"\n' > "$1/baseq2/config.cfg"
}

# assertions <dir holding mfx.py> <ssim.c> <scratch dir> [groups]: appends the ids of the failed ones to FAILED
has() { case " ${GROUPS_RUN:-path plan engine cfg frames report io run lock ssim} " in *" $1 "*) return 0;; esac; return 1; }
# the command line of a generator run on the fake engine (the caller sets PATH, MFX_TRACE)
e2e() { # <engine> <out dir> [more arguments]
    local eng=$1 o=$2; shift 2
    ${EXEC:-} python3 -I "$d/mfx.py" run --out "$o" --basedir "$tmp/basedir" --host-name t1 --runs 1 --gpu-runs 1 --scenarios world \
        --only fx1-s75 --timeout 20 --framediff "$tmp/framediff" --ssim "$w/ssim" --bin "$eng" "$@"
}
assertions() {
    local d=$1 ssrc=$2 w=$3 py out rc n
    GROUPS_RUN=${4:-}
    mkdir -p "$w/lw"
    py="python3 -I $d/mfx.py"
    cc -O2 -o "$w/ssim" "$ssrc" -lm 2>/dev/null || { ko ssim-build "cannot build"; return; }

    # the output path
    if has path; then
    out=$($py check-out "$tmp/repo/out" 2>&1); want_rc git-tree 3 $?
    out=$($py check-out "$tmp/repo/a/b/c" 2>&1); want_rc git-tree 3 $?
    out=$($py check-out "$tmp/fakegit/x" 2>&1); want_rc git-dotgit 3 $?
    out=$(PATH="$tmp/stubtrue:$PATH" $py check-out "$tmp/plain/fresh" 2>&1); want_rc git-true 3 $?
    out=$(PATH="$tmp/stubfail:$PATH" $py check-out "$tmp/plain/fresh" 2>&1); want_rc git-fail 3 $?
    out=$($py check-out "$tmp/link/x" 2>&1); want_rc link-mid 3 $?
    out=$($py check-out "$tmp/plain/lnk" 2>&1); want_rc link-last 3 $?
    if [ "$(id -u)" -ne 0 ]; then out=$($py check-out "$tmp/ro/new/dir" 2>&1); want_rc unwritable 3 $?; fi
    out=$($py check-out "relative/path" 2>&1); want_rc relative 3 $?
    out=$($py check-out "$tmp/plain/fresh/er" 2>&1); want_rc path-ok 0 $?
    want_has path-ok "$tmp/plain/fresh/er" "$out"
    out=$(python3 -I "$HELP" "$d" write-link "$w/lw" 2>&1)
    want_has write-link refused "$out"

    fi

    # which rows run: the engine's own cvar list decides
    if has plan; then
    out=$($py plan --options "$SRC/options.tsv" --cvarlist "$tmp/cv-absent" --scenarios-dir "$tmp/scen" --scenarios world,rt-shadows 2>&1)
    want_not plan-absent "den1" "$(printf '%s\n' "$out" | grep '^run')"
    want_has plan-noise "skip	den1	*	absent: the engine reports no cvar r_rt_denoiser" "$out"
    want_has plan-absent "skip	den1	*	absent" "$out"
    want_has plan-absent "skip	*	rt-shadows	absent cvar in the scenario" "$out"
    want_has plan-present "run	fx1-s50	world" "$out"
    want_not plan-modes "run	fx2-s50	world" "$out"
    want_has plan-modes "r_metalfx 2 is not in r_metalfx_modes 3" "$out"
    out=$($py plan --options "$SRC/options.tsv" --cvarlist "$tmp/cv-present" --scenarios-dir "$tmp/scen" --scenarios world,rt-shadows 2>&1)
    want_has plan-present "run	den1	rt-shadows" "$out"
    want_not plan-present "run	den1	world" "$out"
    want_has plan-present "run	fx3-s50	world" "$out"
    # the largest command line of any run is printed with its kind: 70 cvars in a scenario leave the timed runs
    # and the probes far under the engine's 128, and the record, whose line is pins.sh's, is the largest and over it
    out=$($py plan --options "$SRC/options.tsv" --cvarlist "$tmp/cv-wide" --scenarios-dir "$tmp/scen" --scenarios world,rt-wide 2>&1)
    want_has plan-args "run	fx1-s50	rt-wide" "$out"
    n=$(printf '%s\n' "$out" | sed -n 's/^args	\([0-9]*\)	limit 128	.*/\1/p'); [ -n "$n" ] && [ "$n" -gt 128 ] || ko plan-args "the count is '$n': $out"
    want_has plan-args "limit 128	record of rt-wide with " "$out"
    was=$(printf '%s\n' "$out" | sed -n 's/^args	.*would be \([0-9]*\)$/\1/p'); [ -n "$was" ] && [ "$was" -gt 128 ] || ko plan-args "the +set count is '$was': $out"
    # a scenario of the real size: its largest is a record or a timed run, under the limit
    out=$($py plan --options "$SRC/options.tsv" --cvarlist "$tmp/cv-present" --scenarios-dir "$tmp/scen" --scenarios world 2>&1)
    n=$(printf '%s\n' "$out" | sed -n 's/^args	\([0-9]*\)	limit 128	.*/\1/p'); [ -n "$n" ] && [ "$n" -lt 128 ] && [ "$n" -gt 42 ] || ko plan-args "the count of world is '$n': $out"
    want_has plan-args "record of world with " "$out"
    out=$($py plan --options "$tmp/opts-bound" --cvarlist "$tmp/cv-big" --scenarios-dir "$tmp/scen" --scenarios world 2>&1)
    want_not plan-bound "run	bound" "$out"
    want_has plan-bound "skip	bound" "$out"
    out=$(python3 -I "$HELP" "$d" accepted 1 0 2>&1); want_has accepted-no "no: r_metalfx_interp 1 reads 0" "$out"
    out=$(python3 -I "$HELP" "$d" accepted 0.5 0.50 2>&1); want_has accepted-yes "ok" "$out"; want_not accepted-yes "no:" "$out"
    out=$(python3 -I "$HELP" "$d" accepted-missing 2>&1); want_has accepted-missing "no: the engine does not report r_rt_denoiser" "$out"

    fi

    # engine runs: a failure to run or to say what it did is an error
    if has engine; then
    mkdir -p "$w/logs"
    out=$(python3 -I "$HELP" "$d" probe "$tmp/eng-good" "$w/logs" 2>&1); want_has probe-ok "ok 9 cvars" "$out"
    out=$(python3 -I "$HELP" "$d" probe "$tmp/eng-fail" "$w/logs" 2>&1); want_has probe-status "error: the probe run exited with status 1" "$out"
    out=$(python3 -I "$HELP" "$d" probe "$tmp/eng-novalid" "$w/logs" 2>&1); want_has probe-validation "error: the Metal validation layer was not enabled" "$out"
    out=$(python3 -I "$HELP" "$d" timed "$tmp/eng-good" "$w/logs" 2>&1); want_has timed-ok "ok 114.9" "$out"
    out=$(python3 -I "$HELP" "$d" timed "$tmp/eng-nofps" "$w/logs" 2>&1); want_has timed-nofps "error: no timedemo result" "$out"
    # an engine that never executed the run's console script: its cvars were not set, so the run is an error
    out=$(python3 -I "$HELP" "$d" probe "$tmp/eng-noexec" "$w/logs" 2>&1); want_has probe-noexec "error: the engine did not execute the console script mfx-run.cfg" "$out"
    out=$(python3 -I "$HELP" "$d" timed "$tmp/eng-noexec" "$w/logs" 2>&1); want_has timed-noexec "error: the engine did not execute the console script mfx-run.cfg" "$out"
    out=$(python3 -I "$HELP" "$d" probe-opts "$tmp/eng-failopt" "$w/logs" 2>&1); want_has probe-skip "ok good skipped bad:probe failed:" "$out"

    fi

    # a run starts from the engine's defaults: config.cfg set aside, cleared, put back
    if has cfg; then
    out=$(python3 -I "$HELP" "$d" defaults "$tmp/cv-defaults" 2>&1); want_has cfg-defaults "ok" "$out"; want_not cfg-defaults "leak" "$out"; 
    mkeng "$w/cfgbin" "$tmp/eng-cfg"; mkdir -p "$w/cfglogs"
    out=$(python3 -I "$HELP" "$d" cfg-run "$w/cfgbin/quake2" "$w/cfglogs" 2>&1)
    want_has cfg-clear "second run clean" "$out"
    want_has cfg-restored "restored" "$out"; want_not cfg-restored "not restored" "$out"
    out=$(python3 -I "$HELP" "$d" cfg-recover "$w/cfgrec" 2>&1); want_has cfg-recover "recovered" "$out"
    out=$(python3 -I "$HELP" "$d" cfg-both "$w/cfgboth" 2>&1)
    want_has cfg-both "config.cfg 'owner" "$out"; want_has cfg-both "kept 'played since" "$out"; want_has cfg-both "files 2" "$out"
    # the cvars go through one console script: a scenario of 70 cvars (and defaults) is 32 arguments, however many
    # cvars there are; a command line over the limit is still an error
    out=$(python3 -I "$HELP" "$d" cfg-many "$w/cfgmany" 70 2>&1)
    want_has cfg-many "args 32" "$out"; want_has cfg-many "exec 1" "$out"; want_has cfg-many "script complete" "$out"
    want_has cfg-pairs "pairs fixed only" "$out"; want_has cfg-many "wide 32" "$out"; want_has cfg-limit "
limit" "$out"
    # a value or name that cannot be written into a console line is refused with its cvar name
    out=$(python3 -I "$HELP" "$d" cfg-unsafe "$w/cfgunsafe" 2>&1)
    want_has cfg-unsafe "quote refused: the value 'a\"b' of the cvar r_rt_q " "$out"
    want_has cfg-unsafe "semicolon refused: the value '1;quit' of the cvar r_rt_s " "$out"
    want_has cfg-unsafe "newline refused: the value '1\\nquit' of the cvar r_rt_n " "$out"
    want_has cfg-unsafe "space refused: the value '1 2' of the cvar r_rt_p " "$out"
    want_has cfg-unsafe "empty refused: the value '' of the cvar r_rt_e " "$out"
    want_has cfg-unsafe "name refused: the cvar name 'r_rt_q;quit'" "$out"
    want_has cfg-fixed "fixed refused: the cvar vid_renderer is set by the generator" "$out"
    want_has cfg-fixed "width refused: the cvar r_customwidth is set by the generator" "$out"
    want_has cfg-unsafe "tailname refused: the cvar name 'r_rt_t\\n'" "$out"
    want_has cfg-unsafe "tailvalue refused: the value '1\\n' of the cvar r_rt_t " "$out"
    want_has cfg-unsafe "plain accepted" "$out"
    # a link at the script's path is replaced, never written through; the owner's config is put back even when
    # the script cannot be removed
    out=$(python3 -I "$HELP" "$d" cfg-link "$w/cfglink" 2>&1)
    want_has cfg-link "victim intact" "$out"; want_has cfg-link "script written" "$out"
    out=$(python3 -I "$HELP" "$d" cfg-restore-order "$w/cfgorder" 2>&1)
    want_has cfg-restore-order "owner config back" "$out"; want_has cfg-restore-order "saved gone" "$out"
    # the console script goes when the held config is put back, and one a killed run left goes at the next hold
    out=$(python3 -I "$HELP" "$d" cfg-script-gone "$w/cfggone" 2>&1)
    want_has cfg-script-hold "after hold gone" "$out"; want_has cfg-script-restore "after restore gone" "$out"

    fi

    # frame pairing and order, the stats line
    if has frames; then
    out=$(python3 -I "$HELP" "$d" frames "$w/fa" "$w/fb" 2>&1)
    want_has frames-pair "paired" "$out"; want_not frames-pair "not paired" "$out"
    want_has frames-order "order 60 180 1200" "$out"
    out=$(python3 -I "$HELP" "$d" stats 2>&1); want_has stats-multi "matched 3 timed 2" "$out"

    fi

    # the report: two hosts side by side, ranges, images from the local host only, nothing external
    if has report; then
    mkdir -p "$w/out"
    python3 -I "$HELP" "$d" make-host "$w/ha" hosta 100 140 "$tmp/a.tga" || ko host-both "make-host a"
    python3 -I "$HELP" "$d" make-host "$w/hb" hostbb 300 340 "$tmp/a.tga" || ko host-both "make-host b"
    python3 -I "$HELP" "$d" make-host "$w/hc" "http://evil.test/x" 1 2 "$tmp/a.tga" || ko ext-refuse "make-host c"
    out=$($py report --dir "$w/ha" --other "$w/hb" --html "$w/out/r.html" 2>&1); want_rc host-both 0 $?
    html=$(cat "$w/out/r.html" 2>/dev/null)
    out=$($py report --dir "$w/ha" --other "$w/hb" --html "$w/out/r.html" 2>&1); want_rc report-rewrite 0 $?
    want_has host-both "hosta" "$html"; want_has host-both "hostbb" "$html"
    want_has host-both "420.0 to 476.0 (5 runs)" "$html"      # host B, fx1-s50 (x1.4)
    want_has host-range "100.0 to 140.0 (5 runs)" "$html"
    want_has host-range "300.0 to 340.0 (5 runs)" "$html"
    want_has host-both "not run: unsupported: r_metalfx 3 is not in r_metalfx_modes 3" "$html"
    want_has load-mark "300.0 to 340.0 (5 runs) [measured under load (1-minute load up to 60.0" "$html"
    want_not load-quiet "140.0 (5 runs) [measured" "$html"
    b64a=$(base64 < "$w/ha/crops/world__native.png" | tr -d '\n')
    b64b=$(base64 < "$w/hb/crops/world__native.png" | tr -d '\n')
    want_has host-images "$b64a" "$html"
    want_not host-images "$b64b" "$html"
    if printf '%s' "$html" | grep -Eiq 'https?://|//[a-z0-9]|<link|@import|<script|(src|href)="[^d#]'; then ko ext-none "external reference in the report"; fi
    [ -n "$html" ] || ko ext-none "no report"
    out=$(python3 -I "$HELP" "$d" ext-b64 2>&1); want_has ext-b64 "b64 refs 0, outside refs" "$out"; want_not ext-b64 "outside refs 0" "$out"
    out=$($py report --dir "$w/hc" --html "$w/out/bad.html" 2>&1); rc=$?
    want_rc ext-refuse 3 $rc
    [ ! -e "$w/out/bad.html" ] || ko ext-refuse "a report with an external reference was written"
    out=$($py report --dir "$w/ha" --html "$tmp/repo/r.html" 2>&1); want_rc report-path 3 $?
    # names and statuses read from another host are not trusted
    python3 -I "$HELP" "$d" craft-host "$w/ha" "$w/hx1" "$tmp/secret/holiday" y || ko name-abs "craft 1"
    python3 -I "$HELP" "$d" craft-host "$w/ha" "$w/hx2" "../../secret/holiday" y || ko name-abs "craft 2"
    for h in hx1 hx2; do
        out=$($py report --dir "$w/ha" --other "$w/$h" --html "$w/out/$h.html" 2>&1); want_rc name-abs 1 $?
        want_has name-abs "not a plain name" "$out"
        [ ! -e "$w/out/$h.html" ] || ko name-abs "a report was written from $h"
    done
    cp -R "$w/hb" "$w/hx3"
    awk -F'\t' 'BEGIN { OFS = "\t" } NR > 1 { $3 = "weird" } { print }' "$w/hb/results.tsv" > "$w/hx3/results.tsv"
    out=$($py report --dir "$w/ha" --other "$w/hx3" --html "$w/out/hx3.html" 2>&1); want_rc name-status 1 $?
    want_has name-status "the status 'weird'" "$out"
    out=$(python3 -I "$HELP" "$d" read-crop "$w/rc" 2>&1)
    want_has crop-plain "b'CROP' None None None None None" "$out"
    # a rerun that skipped fx1-s50 does not show the crop an earlier run left for it
    python3 -I "$HELP" "$d" stale-crop "$w/ha" "$w/hs" || ko crop-status "stale-crop"
    out=$($py report --dir "$w/hs" --html "$w/out/hs.html" 2>&1); want_rc crop-status 0 $?
    html2=$(cat "$w/out/hs.html" 2>/dev/null)
    want_has crop-status "not run: unsupported: rerun" "$html2"
    b64s=$(printf 'STALE-CROP-BYTES' | base64 | tr -d '\n')
    want_not crop-status "$b64s" "$html2"
    want_has crop-status "$b64a" "$html2"

    fi

    # files a report reads or writes
    if has io; then
    [ -d "$w/ha" ] || python3 -I "$HELP" "$d" make-host "$w/ha" hosta 100 140 "$tmp/a.tga" || ko io-hardlink "make-host"
    cp -R "$w/ha" "$w/hf"; rm "$w/hf/results.tsv"; mkfifo "$w/hf/results.tsv"
    out=$(python3 -I "$HELP" "$d" read-fifo "$w/hf" 2>&1); want_has io-fifo "not a regular file" "$out"
    printf 'tracked\n' > "$w/tracked.html"; mkdir -p "$w/hl"; ln "$w/tracked.html" "$w/hl/h.html"
    out=$($py report --dir "$w/ha" --html "$w/hl/h.html" 2>&1); want_rc io-hardlink 0 $?
    [ "$(cat "$w/tracked.html")" = tracked ] || ko io-hardlink "the report was written through the hard link"
    case $(cat "$w/hl/h.html") in "<!doctype html>"*) ;; *) ko io-hardlink "no report at the linked path";; esac
    mkdir -p "$w/rs"; printf 'set timedemo 1\n' > "$w/rs/world.cfg"
    out=$(python3 -I "$HELP" "$d" record-env "$w/rs" /opt/other/quake2 2>&1)
    want_has record-bin "PINS_BIN /opt/other/quake2" "$out"
    want_has record-scratch "scratch ['mfxr-world-t.cfg'] left []" "$out"
    mkdir -p "$w/cs"; out=$(python3 -I "$HELP" "$d" clear-scratch "$w/cs" 2>&1); [ "$out" = "left keep.cfg" ] || ko clear-scratch "$out"

    fi

    # the run: --bin, the freshness check, the commit, an existing results folder, --replace, the quality and
    # the crops (through the stub pins.sh of the tree copy), the locks, a signal
    if has run; then
    export MFX_NATIVE_TGA="$tmp/a.tga" MFX_OPT_TGA="$tmp/other.tga"
    mkeng "$w/ft/release" "$tmp/eng-cfg"; mkdir -p "$w/ft/src"; touch "$w/ft/Makefile"
    git -C "$w/ft" init -q && git -C "$w/ft" -c user.name=t -c user.email=t@t commit -q --allow-empty -m x || ko build-commit "fixture"
    want=$(git -C "$w/ft" rev-parse --short=8 HEAD)
    mkeng "$w/loose" "$tmp/eng-cfg"
    out=$(PATH="$tmp/makefail:$PATH" e2e "$w/ft/release/quake2" "$w/oa" 2>&1); want_rc build-stale 3 $?
    want_has build-stale "out of date" "$out"
    mkdir -p "$w/oa" "$w/ob"
    out=$(PATH="$tmp/maketrue:$PATH" MFX_TRACE="$w/trace1" e2e "$w/ft/release/quake2" "$w/oa" 2>&1); want_rc e2e-run 0 $?
    want_has e2e-run "results: " "$out"
    rd=$(ls -d "$w"/oa/metalfx-t1-* 2>/dev/null | head -1); [ -n "$rd" ] || { ko e2e-run "no results folder"; rd="$w/none"; }
    [ "$(cat "$w/ft/release/baseq2/config.cfg")" = 'set owner_cvar "7"' ] && [ ! -e "$w/ft/release/baseq2/config.cfg.mfx-saved" ] || ko e2e-restored "the owner's config.cfg was not put back"
    want_has build-commit "commit	$want" "$(cat "$rd/meta.tsv" 2>/dev/null)"
    want_has build-commit "build	checked" "$(cat "$rd/meta.tsv" 2>/dev/null)"
    [ -s "$w/trace1" ] || ko e2e-leak "the fake engine left no trace"
    want_not e2e-leak "seen:" "$(cat "$w/trace1" 2>/dev/null)"
    # the quality and the crops of an option, from the records the stub wrote (frames 60, 120, 180)
    out=$(python3 -I "$HELP" "$d" check-run "$rd" fx1-s75 world 2>&1)
    want_has e2e-status "status ok" "$out"
    want_has e2e-quality "ssim_less_than_one yes" "$out"
    want_has e2e-crop "crop_differs yes" "$out"
    want_has e2e-index "index frame 120 " "$out"
    # the same day again: refused, then replaced, with the earlier crops gone
    out=$(PATH="$tmp/maketrue:$PATH" e2e "$w/ft/release/quake2" "$w/oa" 2>&1); want_rc rdir-exists 3 $?
    want_has rdir-exists "pass --replace" "$out"
    mkdir -p "$rd/crops"; printf 'old\n' > "$rd/crops/old__x.png"
    out=$(PATH="$tmp/maketrue:$PATH" MFX_TRACE="$w/trace2" e2e "$w/ft/release/quake2" "$w/oa" --replace 2>&1); want_rc crops-stale 0 $?
    [ ! -e "$rd/crops/old__x.png" ] || ko crops-stale "the earlier run's crop is still there"
    want_not e2e-leak "seen:" "$(cat "$w/trace2" 2>/dev/null)"
    [ "$(cat "$w/ft/release/baseq2/config.cfg")" = 'set owner_cvar "7"' ] || ko e2e-restored "config.cfg after the second run"
    # a binary that is not in a build tree is not checked, its commit is unknown, and it says so
    out=$(PATH="$tmp/makefail:$PATH" e2e "$w/loose/quake2" "$w/ob" 2>&1); want_rc build-unknown 0 $?
    want_has build-unknown "not checked for freshness" "$out"
    rl=$(ls -d "$w"/ob/metalfx-t1-* 2>/dev/null | head -1)
    want_has build-unknown "commit	unknown" "$(cat "$rl/meta.tsv" 2>/dev/null)"
    want_has build-unknown "build	not checked" "$(cat "$rl/meta.tsv" 2>/dev/null)"
    # the caller's checkout is not touched: the run's scratch scenarios were in the tree copy, and
    # a file of that name in the checkout (here: the real one) is still there
    [ -f "$root/scripts/pins.sh" ] || ko e2e-tree "no pins.sh in the checkout"
    case $d in "$root"/*) ko e2e-tree "the end-to-end run is not in a tree copy";; esac
    # one run per binary and per scenarios folder: while a run holds the binary, another is refused, and
    # after SIGTERM to the first the owner's config.cfg is back
    mkeng "$w/slow" "$tmp/eng-slow"; mkdir -p "$w/oc" "$w/od" "$w/oe"
    printf 'set owner_cvar "7"\n' > "$w/slow/baseq2/config.cfg"
    # started directly, not through a function, so that $pid is the generator itself
    PATH="$tmp/maketrue:$PATH" python3 -I "$d/mfx.py" run --out "$w/oc" --basedir "$tmp/basedir" --host-name t1 --runs 1 --gpu-runs 1 \
        --scenarios world --only fx1-s75 --timeout 20 --framediff "$tmp/framediff" --ssim "$w/ssim" --bin "$w/slow/quake2" > "$w/slow.log" 2>&1 &
    pid=$!
    n=0; while [ ! -e "$w/slow/baseq2/config.cfg.mfx-saved" ] && [ $n -lt 100 ]; do sleep 0.1; n=$((n+1)); done
    sleep 1
    # a second run on the same binary (own results folder), and one on another binary of the same checkout
    for which in bin scen; do
        if [ $which = bin ]; then eng="$w/slow/quake2"; o="$w/od"; else eng="$w/loose/quake2"; o="$w/oe"; fi
        ( PATH="$tmp/maketrue:$PATH" EXEC=exec e2e "$eng" "$o" --timeout 3 > "$w/lock-$which.log" 2>&1 ) &
        p2=$!
        n=0; while kill -0 $p2 2>/dev/null && [ $n -lt 40 ]; do sleep 0.1; n=$((n+1)); done
        if kill -0 $p2 2>/dev/null; then kill -TERM $p2 2>/dev/null; n=0; while kill -0 $p2 2>/dev/null && [ $n -lt 100 ]; do sleep 0.1; n=$((n+1)); done; kill -KILL $p2 2>/dev/null; fi
        wait $p2 2>/dev/null; rc2=$?
        want_rc "run-lock-$which" 3 $rc2
        want_has "run-lock-$which" "another run holds" "$(cat "$w/lock-$which.log")"
    done
    want_has run-lock-bin "the engine binary directory" "$(cat "$w/lock-bin.log")"
    want_has run-lock-scen "the scenarios folder" "$(cat "$w/lock-scen.log")"
    [ -e "$w/slow/baseq2/config.cfg.mfx-saved" ] || ko run-lock-bin "the first run's saved config is gone while it runs"
    kill -TERM $pid 2>/dev/null
    n=0; while kill -0 $pid 2>/dev/null && [ $n -lt 100 ]; do sleep 0.1; n=$((n+1)); done
    if kill -0 $pid 2>/dev/null; then kill -KILL $pid 2>/dev/null; ko e2e-sigterm "the run did not end on SIGTERM"; fi
    wait $pid 2>/dev/null
    [ "$(cat "$w/slow/baseq2/config.cfg" 2>/dev/null)" = 'set owner_cvar "7"' ] && [ ! -e "$w/slow/baseq2/config.cfg.mfx-saved" ] || ko e2e-sigterm "the owner's config.cfg was not put back"
    # a SIGTERM during record() ends the engine pins.sh started: the stub's long-lived child is gone
    mkeng "$w/nt" "$tmp/eng-cfg"; mkdir -p "$w/of"
    PATH="$tmp/maketrue:$PATH" MFX_STUB_CHILD="$w/childpid" python3 -I "$d/mfx.py" run --out "$w/of" --basedir "$tmp/basedir" --host-name t1 --runs 1 --gpu-runs 1 \
        --scenarios world --only fx1-s75 --timeout 20 --framediff "$tmp/framediff" --ssim "$w/ssim" --bin "$w/nt/quake2" > "$w/nt.log" 2>&1 &
    pid=$!
    n=0; while [ ! -s "$w/childpid" ] && kill -0 $pid 2>/dev/null && [ $n -lt 300 ]; do sleep 0.1; n=$((n+1)); done
    cpid=$(cat "$w/childpid" 2>/dev/null)
    # $pid is signalled only before the wait that collects it: once collected, the system may give the
    # pid to another process (FIX171)
    if [ -z "$cpid" ]; then
        ko e2e-group "the stub pins.sh did not start"
        if kill -0 $pid 2>/dev/null; then kill -KILL $pid 2>/dev/null; fi
        wait $pid 2>/dev/null
    else
        kill -TERM $pid 2>/dev/null
        n=0; while kill -0 $pid 2>/dev/null && [ $n -lt 100 ]; do sleep 0.1; n=$((n+1)); done
        if kill -0 $pid 2>/dev/null; then kill -KILL $pid 2>/dev/null; ko e2e-group "the run did not end on SIGTERM during record"; fi
        wait $pid 2>/dev/null
        sleep 1
        if kill -0 "$cpid" 2>/dev/null; then kill -KILL "$cpid" 2>/dev/null; ko e2e-group "the child of pins.sh was left running after SIGTERM"; fi
    fi

    fi

    # the host lock (a private BENCH_STATE, the bench harness copied into the tree): the generator takes it
    # once for its whole run, pins.sh passes straight through, and every way out leaves the lock free
    if has lock; then
    export MFX_NATIVE_TGA="$tmp/a.tga" MFX_OPT_TGA="$tmp/other.tga"
    # the seconds stop_group gives pins.sh between TERM and KILL: when pins.sh may be the lock shell of a
    # hold of its own, the lock's grace plus 5 s (BENCH_STOP_GRACE=1: 6 s, so at most 11 s); under the
    # generator's own hold, BENCH_STOP_GRACE alone, so that its KILL lands before the lock shell's KILL of
    # the generator (the lock's grace, at least BENCH_STOP_GRACE + 5)
    out=$(python3 -I "$HELP" "$d" stop-wait 2>&1); [ "$out" = "25 11 25 65 25 25 2000000003 25 25 / 10 1 10 30 10 10 999999999 10 10" ] || ko e2e-lock-grace "$out"
    lt="$w/lt"; mktree "$lt" "$d/mfx.py"; cp "$tmp/pins-stub-locked" "$lt/scripts/pins.sh"; cp -R "$root/scripts/bench" "$lt/scripts/bench"
    # the ways out during record(), each of which ends the run under the lock, the grandchild included,
    # and leaves the lock free:
    #   timeout  sh_group alone (no hold of its own), so the stub pins.sh is the lock shell of its own hold:
    #            the stop must be TERM, which the lock shell's trap passes on to its command's group (a KILL
    #            first ends the lock shell alone), and KILL only after the lock's grace;
    #   TERM, INT  to the generator, which is the lock shell of its own hold by then (a Ctrl-C reaches the
    #            lock shell): its trap stops the generator, which stops pins.sh;
    #   TERM2    two SIGTERMs to the generator's python itself, 1 s apart, with a stub that ignores TERM
    #            (BENCH_STOP_GRACE=5): the second must not cut the stop short, so the generator is still
    #            there 1 s later, and its KILL ends the stub;
    #   deaf     SIGTERM to the lock shell, the stub ignoring TERM: the generator's KILL of pins.sh must
    #            land before the lock shell's KILL of the generator, or pins.sh outlives the lock
    for how in timeout TERM INT TERM2 deaf; do
        st="$w/lt-state-$how"; rm -f "$w/lt-sh" "$w/lt-child" "$w/lt-parent"
        g=1; deaf=; case $how in TERM2) g=5; deaf=1;; deaf) deaf=1;; esac
        lenv=(env -u BENCH_LOCK_TOKEN -u BENCH_LOCK_ENTRY -u BENCH_LOCK -u BENCH_LOCK_WAIT -u BENCH_LOCK_POLL -u CI -u GITHUB_ACTIONS BENCH_STATE="$st"
              BENCH_STOP_GRACE=$g MFX_STUB_SH="$w/lt-sh" MFX_STUB_CHILD="$w/lt-child" MFX_STUB_PARENT="$w/lt-parent"
              MFX_STUB_DEAF=$deaf PINS_STORE="$w/lt-store-$how" PATH="$tmp/maketrue:$PATH")
        if [ $how = timeout ]; then
            "${lenv[@]}" python3 -I "$HELP" "$lt/scripts/metalfx-report" group-timeout "$lt" 5 > "$w/lt-$how.log" 2>&1 &
        else
            mkeng "$w/lt-eng-$how" "$tmp/eng-cfg"; mkdir -p "$w/lt-out-$how"
            "${lenv[@]}" python3 -I -c "$INTDFL" "$lt/scripts/metalfx-report/mfx.py" run --out "$w/lt-out-$how" --basedir "$tmp/basedir" \
                --host-name t1 --runs 1 --gpu-runs 1 --scenarios world --only fx1-s75 --timeout 20 --framediff "$tmp/framediff" \
                --ssim "$w/ssim" --bin "$w/lt-eng-$how/quake2" > "$w/lt-$how.log" 2>&1 &
        fi
        pid=$!
        n=0; while [ ! -s "$w/lt-child" ] && kill -0 $pid 2>/dev/null && [ $n -lt 300 ]; do sleep 0.1; n=$((n+1)); done
        spid=$(cat "$w/lt-sh" 2>/dev/null); cpid=$(cat "$w/lt-child" 2>/dev/null); ppid=$(cat "$w/lt-parent" 2>/dev/null)
        [ -n "$cpid" ] || ko e2e-lock "$how: the stub pins.sh did not start under the lock: $(tail -3 "$w/lt-$how.log")"
        case $how in
            TERM|INT) kill -$how $pid 2>/dev/null;;
            deaf) kill -TERM $pid 2>/dev/null;;
            TERM2)
                [ "$ppid" != "$pid" ] || ko e2e-lock "TERM2: the generator holds no lock of its own (the stub's parent is its pid)"
                # the second TERM only while $ppid is still the generator read from the file: the same start
                # time and command line as before the first (FIX171)
                pidn=$(ps -o lstart= -o args= -p "$ppid" 2>/dev/null)
                kill -TERM "$ppid" 2>/dev/null; sleep 1
                if [ -n "$pidn" ] && [ "$(ps -o lstart= -o args= -p "$ppid" 2>/dev/null)" = "$pidn" ]; then kill -TERM "$ppid" 2>/dev/null; fi
                sleep 1
                kill -0 "$ppid" 2>/dev/null || ko e2e-lock "TERM2: a second SIGTERM cut the stop short (the generator was gone 1 s later)";;
        esac
        n=0; while kill -0 $pid 2>/dev/null && [ $n -lt 200 ]; do sleep 0.1; n=$((n+1)); done
        if kill -0 $pid 2>/dev/null; then kill -KILL $pid 2>/dev/null; ko e2e-lock "$how: the generator did not end within 20 s"; fi
        wait $pid 2>/dev/null
        [ $how != timeout ] || want_has e2e-lock "timed out" "$(cat "$w/lt-$how.log")"
        for q in $spid $cpid; do
            if kill -0 "$q" 2>/dev/null; then kill -KILL "$q" 2>/dev/null; ko e2e-lock "$how: the run under the lock was left running"; fi
        done
        ls=$(BENCH_STATE="$st" sh "$lt/scripts/bench/bench.sh" lock status 2>&1)
        [ "$ls" = lock=free ] || ko e2e-lock "$how: the lock is not free afterwards: $ls"
        case $FAILED in *" e2e-lock "*) break;; esac  # one way out failed: the others would wait their bound too
    done
    # one hold for the whole run: while another job holds the lock, the generator waits for it before it
    # runs anything, so a record's time limit (4 s in this copy) counts the record alone and not that wait;
    # under the generator's hold each record and each timed engine run passes straight through (the holder
    # they see is metalfx-report)
    lk="$w/lk"; mktree "$lk" "$d/mfx.py"; cp "$tmp/pins-stub-locked" "$lk/scripts/pins.sh"; cp -R "$root/scripts/bench" "$lk/scripts/bench"
    python3 -I "$HELP" mutate "$d/mfx.py" "$lk/scripts/metalfx-report/mfx.py" 'env=env, timeout=900)' 'env=env, timeout=4)' > "$w/lk-mut.log" ||
        ko e2e-lock-wait "cannot shorten the record's limit: $(cat "$w/lk-mut.log")"
    st="$w/lk-state"; bsh="$lk/scripts/bench/bench.sh"
    lenv=(env -u BENCH_LOCK_TOKEN -u BENCH_LOCK_ENTRY -u BENCH_LOCK -u BENCH_STOP_GRACE -u CI -u GITHUB_ACTIONS BENCH_LOCK_WAIT=60 BENCH_LOCK_POLL=0.2
          BENCH_STATE="$st" PATH="$tmp/maketrue:$PATH")
    "${lenv[@]}" sh "$bsh" lock run gate -- sleep 10 > "$w/lk-holder.log" 2>&1 &
    hp=$!
    n=0; until BENCH_STATE="$st" sh "$bsh" lock status 2>/dev/null | grep -q '^lock=held label=gate '; do
        [ $n -lt 50 ] || { ko e2e-lock-wait "the holder did not take the lock"; break; }; sleep 0.1; n=$((n+1)); done
    mkeng "$w/lk-eng" "$tmp/eng-lockseen"; mkdir -p "$w/lk-out"
    out=$("${lenv[@]}" LC_ALL=POSIX MFX_STUB_LABEL="$w/lk-labels" MFX_ENG_LABEL="$w/lk-eng-labels" MFX_BENCH="$bsh" python3 -I "$lk/scripts/metalfx-report/mfx.py" \
        run --out "$w/lk-out" --basedir "$tmp/basedir" --host-name t1 --runs 1 --gpu-runs 1 --scenarios world --only fx1-s75 --timeout 20 \
        --framediff "$tmp/framediff" --ssim "$w/ssim" --bin "$w/lk-eng/quake2" 2>&1); rc=$?
    want_rc e2e-lock-wait 0 $rc
    want_not e2e-lock-wait "timed out" "$out"
    want_not e2e-lock-wait "the host lock is off" "$out"
    labs=$(cat "$w/lk-labels" 2>/dev/null)
    [ "$(printf '%s\n' "$labs" | grep -c 'label=metalfx-report ')" -eq 2 ] && [ "$(printf '%s\n' "$labs" | wc -l | tr -d ' ')" -eq 2 ] ||
        ko e2e-lock-wait "the two records did not run under the generator's own hold: $labs"
    labs=$(cat "$w/lk-eng-labels" 2>/dev/null)
    [ -n "$labs" ] || ko e2e-lock-wait "the engine noted no holder"
    want_not e2e-lock-wait "lock=free" "$labs"; want_not e2e-lock-wait "label=gate" "$labs"
    # the caller's LC_ALL comes back under the hold (bench.sh sets C for its command)
    want_has e2e-lock-wait "lc=POSIX " "$labs"; want_not e2e-lock-wait "lc=C " "$labs"
    kill -TERM $hp 2>/dev/null; wait $hp 2>/dev/null
    [ "$(BENCH_STATE="$st" sh "$bsh" lock status 2>&1)" = lock=free ] || ko e2e-lock-wait "the lock is not free afterwards"
    # the lock is off, with one line on stderr that names why, as for the entry points: BENCH_LOCK=0, GitHub
    # Actions, no bench core in the tree, no state directory (each run is then refused as out of date)
    mkeng "$w/lo/release" "$tmp/eng-cfg"; mkdir -p "$w/lo/src" "$w/lo-out"; touch "$w/lo/Makefile"
    for why in "BENCH_LOCK=0" "CI (GITHUB_ACTIONS=true)" "no bench core (scripts/bench/bench.sh)" "no state directory (BENCH_STATE, XDG_CACHE_HOME and HOME unset)"; do
        t=$lk
        case $why in
            BENCH*) oenv=(BENCH_LOCK=0);; CI*) oenv=(CI=true GITHUB_ACTIONS=true);;
            no\ bench*) oenv=(BENCH_LOCK=1); t=$(dirname "$(dirname "$d")");; *) oenv=(-u BENCH_STATE -u XDG_CACHE_HOME -u HOME);;
        esac
        out=$("${lenv[@]}" env "${oenv[@]}" PATH="$tmp/makefail:$PATH" python3 -I "$t/scripts/metalfx-report/mfx.py" run --out "$w/lo-out" --basedir "$tmp/basedir" \
            --host-name t1 --scenarios world --only fx1-s75 --framediff "$tmp/framediff" --ssim "$w/ssim" --bin "$w/lo/release/quake2" 2>&1); rc=$?
        want_rc e2e-lock-off 3 $rc
        [ "$(printf '%s\n' "$out" | grep -c "^metalfx-report: the host lock is off: $why\$")" -eq 1 ] || ko e2e-lock-off "$why: $out"
    done
    out=$("${lenv[@]}" PATH="$tmp/makefail:$PATH" python3 -I "$lk/scripts/metalfx-report/mfx.py" run --out "$w/lo-out" --basedir "$tmp/basedir" \
        --host-name t1 --scenarios world --only fx1-s75 --framediff "$tmp/framediff" --ssim "$w/ssim" --bin "$w/lo/release/quake2" 2>&1); rc=$?
    want_rc e2e-lock-off 3 $rc; want_not e2e-lock-off "the host lock is off" "$out"

    fi

    if has run; then
    # a scenario of 70 cvars runs: the engine gets under 128 arguments and the cvars through the console script
    # (the fake engine counts its arguments and the `set` lines of mfx-run.cfg at each start)
    # (the scenario is as large as a record can be: pins.sh puts three arguments on the line for each cvar of it)
    for i in $(seq 1 15); do printf 'set mfxpad%d 1\n' $i; done >> "$d/../pins/scenarios/world.cfg"
    mkeng "$w/lim" "$tmp/eng-cfg"; mkdir -p "$w/og"
    out=$(PATH="$tmp/maketrue:$PATH" MFX_ARGC="$w/argc" e2e "$w/lim/quake2" "$w/og" 2>&1); want_rc e2e-args 0 $?
    rq=$(ls -d "$w"/og/metalfx-t1-* 2>/dev/null | head -1)
    res=$(cut -f1-3 "$rq/results.tsv" 2>/dev/null)
    want_has e2e-args "native	world	ok" "$res"; want_has e2e-args "fx1-s75	world	ok" "$res"
    mx=$(awk 'BEGIN { m = 0 } $1 > m { m = $1 } END { print m }' "$w/argc" 2>/dev/null)
    [ "${mx:-0}" -gt 10 ] && [ "$mx" -lt 128 ] || ko e2e-args "the largest argument count was '$mx'"
    ln=$(awk 'BEGIN { m = 0 } $2 > m { m = $2 } END { print m }' "$w/argc" 2>/dev/null)
    [ "${ln:-0}" -ge 26 ] || ko e2e-script "the console script held at most '$ln' set lines"
    [ ! -e "$w/lim/baseq2/mfx-run.cfg" ] || ko e2e-script "the console script is left after the run"
    out=$(python3 -I "$HELP" "$d" check-run "$rq" fx1-s75 world 2>&1)
    want_has e2e-quality "ssim_less_than_one yes" "$out"
    # a scenario value that cannot be written into a console line refuses the run with the cvar name
    printf 'set timedemo 1\nset r_drawworld 1;quit\n' > "$d/../pins/scenarios/unsafe.cfg"
    mkeng "$w/uns" "$tmp/eng-cfg"; mkdir -p "$w/ou"
    out=$(PATH="$tmp/maketrue:$PATH" e2e "$w/uns/quake2" "$w/ou" --scenarios unsafe 2>&1); want_rc e2e-unsafe 3 $?
    want_has e2e-unsafe "the value '1;quit' of the cvar r_drawworld" "$out"
    [ -f "$w/uns/baseq2/config.cfg" ] && [ ! -e "$w/uns/baseq2/config.cfg.mfx-saved" ] && grep -q owner_cvar "$w/uns/baseq2/config.cfg" \
        || ko e2e-unsafe-clean "the owner's config.cfg is not back: $(ls "$w/uns/baseq2")"
    [ ! -e "$w/uns/baseq2/mfx-run.cfg" ] || ko e2e-script "the console script is left after a refused run"
    rm -f "$d/../pins/scenarios/unsafe.cfg"
    # a record whose command line would be over the engine's limit (pins.sh passes every cvar as a +set pair)
    # refuses the run, naming the scenario and the option, before any engine run
    for i in $(seq 1 40); do printf 'set mfxlong%d 1\n' $i; done > "$d/../pins/scenarios/long.cfg"
    mkeng "$w/rl" "$tmp/eng-cfg"; mkdir -p "$w/orl"
    out=$(PATH="$tmp/maketrue:$PATH" MFX_ARGC="$w/argc-rl" e2e "$w/rl/quake2" "$w/orl" --scenarios long 2>&1); want_rc e2e-record-limit 3 $?
    want_has e2e-record-limit "the record of scenario long with option native would put 165 arguments" "$out"
    [ ! -s "$w/argc-rl" ] || ko e2e-record-limit "an engine ran before the refusal"
    grep -q owner_cvar "$w/rl/baseq2/config.cfg" 2>/dev/null || ko e2e-record-limit "the owner's config.cfg is not back"
    rm -f "$d/../pins/scenarios/long.cfg"
    # an engine that does not execute the run's console script is an error, never a measured row
    mkeng "$w/nx" "$tmp/eng-noexec"; mkdir -p "$w/onx"
    out=$(PATH="$tmp/maketrue:$PATH" e2e "$w/nx/quake2" "$w/onx" 2>&1); want_rc e2e-noexec 1 $?
    want_has e2e-noexec "did not execute the console script mfx-run.cfg" "$out"
    ! grep -qs "	ok	" "$w"/onx/metalfx-t1-*/results.tsv 2>/dev/null || ko e2e-noexec "a row was recorded as measured"
    # a relative --bin, from another directory, is the same binary: the script lands where the engine looks
    mkeng "$w/rel" "$tmp/eng-cfg"; mkdir -p "$w/orel"
    out=$(cd "$w/rel" && PATH="$tmp/maketrue:$PATH" e2e ./quake2 "$w/orel" 2>&1); want_rc e2e-relative 0 $?
    want_has e2e-relative "native	world	ok" "$(cut -f1-3 "$w"/orel/metalfx-t1-*/results.tsv 2>/dev/null)"

    # a run split into chunks, one --out each, is combined by report with one --dir each; the rows are those of
    # the one full run, and a row two chunks both hold, or a chunk of another commit or setting, is refused
    mkeng "$w/ck" "$tmp/eng-cfg"; mkdir -p "$w/c0" "$w/c1" "$w/c2"
    out=$(PATH="$tmp/maketrue:$PATH" e2e "$w/ck/quake2" "$w/c0" --scenarios world,models 2>&1); want_rc chunk-full 0 $?
    out=$(PATH="$tmp/maketrue:$PATH" e2e "$w/ck/quake2" "$w/c1" --scenarios world 2>&1); want_rc chunk-one 0 $?
    out=$(PATH="$tmp/maketrue:$PATH" e2e "$w/ck/quake2" "$w/c2" --scenarios models 2>&1); want_rc chunk-two 0 $?
    r0=$(ls -d "$w"/c0/metalfx-t1-* 2>/dev/null | head -1); r1=$(ls -d "$w"/c1/metalfx-t1-* 2>/dev/null | head -1); r2=$(ls -d "$w"/c2/metalfx-t1-* 2>/dev/null | head -1)
    out=$($py report --dir "$r1" --dir "$r2" --html "$w/out/chunks.html" 2>&1); want_rc chunk-merge 0 $?
    full=$(cut -f1-5,7-17 "$r0/results.tsv" 2>/dev/null | sed 1d | sort)
    parts=$({ cut -f1-5,7-17 "$r1/results.tsv"; cut -f1-5,7-17 "$r2/results.tsv"; } 2>/dev/null | grep -v '^option	' | sort)
    [ -n "$full" ] && [ "$full" = "$parts" ] || ko chunk-merge "the chunks' rows are not the full run's"
    # the report of the chunks shows what the full run's own report shows: the scenario tables and every crop
    sec() { sed -e 's/\[measured under load[^]]*\]//g' "$1" 2>/dev/null | awk 'BEGIN { RS = "<h2>" } /^Scenario / { print }'; }
    [ -n "$(sec "$w/out/chunks.html")" ] && [ "$(sec "$w/out/chunks.html")" = "$(sec "$(ls "$w"/c0/metalfx-t1-*.html | head -1)")" ] || ko chunk-merge "the chunks' scenario tables differ from the full run's"
    nf=$(grep -o '<figure>' "$w/out/chunks.html" | wc -l | tr -d ' '); nf0=$(grep -o '<figure>' "$(ls "$w"/c0/metalfx-t1-*.html | head -1)" | wc -l | tr -d ' ')
    [ "$nf" -gt 0 ] && [ "$nf" = "$nf0" ] || ko chunk-merge "the crops of the chunks ($nf) are not the full run's ($nf0)"
    cp -R "$r1" "$w/c1dup"
    out=$($py report --dir "$r1" --dir "$w/c1dup" --html "$w/out/dup.html" 2>&1); want_rc chunk-dup 3 $?
    want_has chunk-dup "is in both" "$out"
    out=$($py report --dir "$r1" --dir "$r1" --html "$w/out/dup.html" 2>&1); want_rc chunk-dup 3 $?
    [ ! -e "$w/out/dup.html" ] || ko chunk-dup "a report was written from duplicate rows"
    for k in commit size; do
        cp -R "$r2" "$w/c2x-$k"
        awk -F'\t' -v k=$k 'BEGIN { OFS = "\t" } $1 == k { $2 = "changed" } { print }' "$r2/meta.tsv" > "$w/c2x-$k/meta.tsv"
        out=$($py report --dir "$r1" --dir "$w/c2x-$k" --html "$w/out/diff-$k.html" 2>&1); want_rc chunk-$k 3 $?
        want_has chunk-$k "not chunks of one run: $k is " "$out"
        [ ! -e "$w/out/diff-$k.html" ] || ko chunk-$k "a report was written from chunks that differ in $k"
    done

    fi

    # the structural measure
    if has ssim; then
    out=$("$w/ssim" "$tmp/a.tga" "$tmp/a.tga" 2>&1); want_rc ssim-one 0 $?; want_has ssim-one "ssim=1.000000" "$out"
    s_shift=$("$w/ssim" "$tmp/a.tga" "$tmp/shift.tga" | sed 's/^ssim=\([0-9.-]*\).*/\1/')
    s_block=$("$w/ssim" "$tmp/a.tga" "$tmp/block.tga" | sed 's/^ssim=\([0-9.-]*\).*/\1/')
    s_other=$("$w/ssim" "$tmp/a.tga" "$tmp/other.tga" | sed 's/^ssim=\([0-9.-]*\).*/\1/')
    awk -v v="$s_shift" 'BEGIN { exit !(v < 1.0 && v > 0.9) }' || ko ssim-less "shifted image scored '$s_shift'"
    awk -v v="$s_block" 'BEGIN { exit !(v < 0.9) }' || ko ssim-less "a changed block scored '$s_block'"
    awk -v v="$s_other" 'BEGIN { exit !(v < 0.9) }' || ko ssim-lower "a different image scored '$s_other'"
    awk -v a="$s_shift" -v b="$s_block" -v c="$s_other" 'BEGIN { exit !(a > b && a > c) }' || ko ssim-order "a small shift must score above a big change: $s_shift $s_block $s_other"
    out=$("$w/ssim" "$tmp/a.tga" "$tmp/small.tga" 2>&1); want_rc ssim-mismatch 3 $?
    out=$("$w/ssim" "$tmp/a.tga" "$tmp/nonexistent.tga" 2>&1); want_rc ssim-missing 3 $?
    fi
}

# the real code
FAILED=" "
mkdir "$tmp/real-run"; mktree "$tmp/real-tree" "$SRC/mfx.py" || exit 1
assertions "$tmp/real-tree/scripts/metalfx-report" "$SRC/ssim.c" "$tmp/real-run" "${MFX_GROUPS:-}"  # MFX_GROUPS="lock": only these (while iterating)
if [ "$FAILED" != " " ]; then bad "assertions failed on the real code:$FAILED"; fi
# the scratch scenarios the generator writes into scripts/pins/scenarios are ignored by git (and the check fails without the rule)
ign() { grep -qxF '/scripts/pins/scenarios/mfxr-*.cfg' "$1"; }
ign "$root/.gitignore" || bad "no .gitignore rule for the generator's scratch scenarios"
printf '/release/\n' > "$tmp/noign"; if ign "$tmp/noign"; then bad "the ignore check passes without the rule"; fi
[ "${MFX_REAL_ONLY:-0}" = 0 ] || { [ "$fail" -ne 0 ] || echo "check-metalfx-report: real code ok (mutants skipped)"; exit $fail; }

# mutants: one change of the line each assertion covers; the covered assertion must fail
nm=0
mutant() { # name covered-id file(py|c) old new [old new]...: runs in the background, result in $tmp/res-<name>
    local name=$1 id=$2 kind=$3; shift 3
    case " ${MFX_MUTANTS:-} " in "  "|*" $name "*) ;; *) return;; esac  # MFX_MUTANTS="a b": only these (while iterating)
    nm=$((nm+1))
    while [ "$(jobs -r | wc -l | tr -d ' ')" -ge 8 ]; do sleep 0.2; done
    (
        # the mutant lives in a tree of its own (scripts/metalfx-report, scripts/pins/scenarios), so that
        # the generator's root is the mutant's and no two mutants share a scenarios folder
        mdir="$tmp/mut-$name"; sub="$mdir/scripts/metalfx-report"; ssrc="$SRC/ssim.c"
        mkdir -p "$mdir/w"; mktree "$mdir" "$SRC/mfx.py"
        if [ "$kind" = py ]; then
            python3 -I "$HELP" mutate "$SRC/mfx.py" "$sub/mfx.py" "$@" > "$mdir/log" || { echo "bad	mutant $name: $(cat "$mdir/log")" > "$tmp/res-$name"; exit 0; }
        else
            python3 -I "$HELP" mutate "$SRC/ssim.c" "$mdir/ssim.c" "$@" > "$mdir/log" || { echo "bad	mutant $name: $(cat "$mdir/log")" > "$tmp/res-$name"; exit 0; }
            ssrc="$mdir/ssim.c"
        fi
        FAILED=" "
        case $id in
            git-*|link-*|unwritable|relative|write-link) g=path;;
            plan-*|accepted-*) g=plan;;
            probe-*|timed-*) g=engine;;
            cfg-*) g=cfg;;
            frames-*|stats-*) g=frames;;
            name-*|crop-*|host-*|ext-*|report-*|load-*) g=report;;
            io-*|record-*|clear-*) g=io;;
            e2e-lock*) g=lock;;
            build-*|rdir-*|crops-*|e2e-*|run-*|chunk-*) g=run;;
            ssim-*) g=ssim;;
            *) g=report;;
        esac
        assertions "$sub" "$ssrc" "$mdir/w" "$g" > "$mdir/out" 2>&1
        case $FAILED in
            *" $id "*) echo "ok	mutant $name killed ($id)" > "$tmp/res-$name";;
            *) echo "bad	mutant $name survived: $id did not fail (failed:$FAILED)" > "$tmp/res-$name";;
        esac
    ) &
}

mutant git-dotgit-off git-dotgit py 'if os.path.lexists(os.path.join(d, ".git")):' 'if False:'
mutant git-tree-off git-tree py 'if os.path.lexists(os.path.join(d, ".git")):' 'if False:' 'if _git_says_inside(deepest):' 'if False:'
mutant git-answer-ignored git-true py 'return out == "true"' 'return False'
mutant git-fail-open git-fail py 'raise Refuse("git failed (%s): the output path cannot be checked" % p.stderr.strip()[:200])' 'return False'
mutant link-allowed-mid link-mid py 'if stat.S_ISLNK(st.st_mode):' 'if stat.S_ISLNK(st.st_mode) and i == len(parts) - 1:' 'if not stat.S_ISDIR(st.st_mode):' 'if False:'
mutant link-allowed-last link-last py 'if stat.S_ISLNK(st.st_mode):' 'if stat.S_ISLNK(st.st_mode) and i < len(parts) - 1:' 'if not stat.S_ISDIR(st.st_mode):' 'if False:'
mutant unwritable-allowed unwritable py 'if not os.access(deepest, os.W_OK | os.X_OK):' 'if False:'
mutant relative-allowed relative py 'if not path or "\0" in path or not os.path.isabs(path):' 'if not path:' 'if not os.access(deepest, os.W_OK | os.X_OK):' 'if False:'
mutant write-follows-link write-link py 'os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW' 'os.O_WRONLY | os.O_CREAT'
mutant absent-runs plan-absent py 'gone = [k for k, _ in o["set"] if k not in cvars]' 'gone = []'
mutant scenario-absent-runs plan-absent py 'gone = sorted(c for c in scenarios[sc] if c not in cvars and c.startswith(GATING_PREFIXES))' 'gone = []'
mutant present-skipped plan-present py 'gone = [k for k, _ in o["set"] if k not in cvars]' 'gone = [k for k, _ in o["set"]]'
mutant mode-unchecked plan-modes py 'elif not (0 <= m <= MAX_MODE) or not ((modes >> m) & 1):' 'elif not (0 <= m <= MAX_MODE):'
mutant mode-unbounded plan-bound py 'elif not (0 <= m <= MAX_MODE) or not ((modes >> m) & 1):' 'elif not ((modes >> m) & 1):'
mutant cvarlist-by-mention plan-noise py "CVARLIST_RE = re.compile(r'^[ *][ U][ S][ L-] ([A-Za-z0-9_]+) \"(.*)\"\$')" "CVARLIST_RE = re.compile(r'.*?([A-Za-z0-9_]+) \"(.*)\"\$')"
mutant readback-any accepted-no py 'return abs(float(asked) - float(got)) < 1e-6' 'return True'
mutant readback-none accepted-yes py 'return abs(float(asked) - float(got)) < 1e-6' 'return False'
mutant readback-missing accepted-missing py 'if k not in readback:' 'if False:'
mutant probe-status-ignored probe-status py 'if st != 0:
        raise RuntimeError("the probe run exited' 'if False:
        raise RuntimeError("the probe run exited'
mutant validation-unchecked probe-validation py 'if VALIDATION_ON not in text:' 'if False:'
mutant no-fps-passes timed-nofps py 'raise RuntimeError("no timedemo result (%s)" % log)' 'return 0.0, text'
mutant load-limit-high load-mark py 'LOAD_QUIET = 8.0 ' 'LOAD_QUIET = 800.0 '
mutant load-always load-quiet py 'if loads and max(loads) > LOAD_QUIET:' 'if loads:'
mutant rewrite-refused report-rewrite py 'if file and i == len(parts) - 1 and stat.S_ISREG(st.st_mode):' 'if False:'
mutant base64-scanned ext-b64 py 'doc = re.sub(r'"'"'(src="data:image/png;base64,)[A-Za-z0-9+/=]*'"'"', r"\1", doc)' 'pass'
mutant scenario-gated-by-any-cvar plan-present py 'if c not in cvars and c.startswith(GATING_PREFIXES))' 'if c not in cvars)'
mutant second-host-hidden host-both py 'cells, _ = host_cell(h, sc, oid)' 'cells, _ = host_cell(hosts[0], sc, oid)'
mutant range-collapsed host-range py 't = ("%.*f" % (nd, lo)) if lo == hi else' 't = ("%.*f" % (nd, lo)) if True else'
mutant other-host-images host-images py 'png = read_crop(local["where"][(oid, sc)], sc, oid)' 'png = read_crop(others[-1] if others else rdirs[0], sc, oid)'
mutant external-allowed ext-refuse py 'raise Refuse("the report would refer outside itself: %r" % ext[:3])' 'pass'
mutant external-emitted ext-none py 'raise Refuse("the report would refer outside itself: %r" % ext[:3])' 'pass' 'p.append("</main></body></html>\n")' 'p.append("<link rel=stylesheet href=x.css></main></body></html>\n")'
mutant report-in-tree report-path py 'outp = check_out_path(os.path.abspath(out_html), file=True)' 'outp = os.path.abspath(out_html)'
mutant defaults-unemitted cfg-defaults py 'for k, v in sorted(ctx.defaults.items()) + list(scenario_sets) + list(extra_sets):' 'for k, v in list(scenario_sets) + list(extra_sets):'
mutant defaults-after cfg-defaults py 'for k, v in sorted(ctx.defaults.items()) + list(scenario_sets) + list(extra_sets):' 'for k, v in list(scenario_sets) + list(extra_sets) + sorted(ctx.defaults.items()):'
mutant args-set-pairs cfg-many py 'a += ["+exec", RUN_CFG]' 'a += ["+exec", RUN_CFG]
    for l in text.split("\n")[1:-1]:
        a += ["+set"] + l.split()[1:]'
mutant args-set-pairs-run e2e-args py 'a += ["+exec", RUN_CFG]' 'a += ["+exec", RUN_CFG]
    for l in text.split("\n")[1:-1]:
        a += ["+set"] + l.split()[1:]'
mutant script-not-exec e2e-args py 'a += ["+exec", RUN_CFG]' 'a += []'
mutant script-not-written e2e-script py 'write_atomic(run_cfg_path(ctx), text.encode())' 'pass'
mutant script-through-link cfg-link py 'write_atomic(run_cfg_path(ctx), text.encode())' 'write_file(run_cfg_path(ctx), text.encode())'
mutant script-not-checked-probe probe-noexec py 'check_script_ran(text, log)
    if st != 0:' 'if st != 0:'
mutant script-not-checked-timed timed-noexec py 'check_script_ran(text, log)
    m = TIMEDEMO_RE.search(text)' 'm = TIMEDEMO_RE.search(text)'
mutant script-not-checked-run e2e-noexec py 'def check_script_ran(text, log):' 'def check_script_ran(text, log):
    return'
mutant bin-relative e2e-relative py 'ctx.bin = os.path.abspath(a.bin or os.environ.get("PINS_BIN") or os.path.join(ROOT, "release", "quake2"))' 'ctx.bin = a.bin or os.environ.get("PINS_BIN") or os.path.join(ROOT, "release", "quake2")'
mutant record-limit-unchecked e2e-record-limit py 'check_record_lengths(ctx.scen_dir, options, scen)' 'pass'
mutant record-limit-lax e2e-record-limit py 'if n > MAX_ENGINE_ARGS + 1:
                raise Refuse("the record' 'if n > 10 * MAX_ENGINE_ARGS:
                raise Refuse("the record'
mutant plan-record-uncounted plan-args py '(record_argc(scenario_body(scen_dir, sc, by[oid])), "record of %s with %s" % (sc, oid))' '(0, "record of %s with %s" % (sc, oid))'
mutant restore-script-first cfg-restore-order py "try:  # the owner's config goes back first; the run's console script goes after it, whatever happens
            self._remove(self.path)" "self._remove(os.path.join(os.path.dirname(self.path), RUN_CFG))
        try:
            self._remove(self.path)"
mutant name-prefix-match cfg-unsafe py 'if not CVAR_RE.fullmatch(k):' 'if not CVAR_RE.match(k):'
mutant value-prefix-match cfg-unsafe py 'if not VALUE_RE.fullmatch(v):' 'if not VALUE_RE.match(v):'
mutant script-kept-restore cfg-script-restore py 'self._remove(os.path.join(os.path.dirname(self.path), RUN_CFG))  # the console script of the run goes with it' 'pass'
mutant script-kept-hold cfg-script-hold py 'self._remove(os.path.join(os.path.dirname(self.path), RUN_CFG))  # one a killed run left' 'pass'
mutant script-value-unchecked cfg-unsafe py 'if not VALUE_RE.fullmatch(v):' 'if False:'
mutant script-name-unchecked cfg-unsafe py 'if not CVAR_RE.fullmatch(k):' 'if False:'
mutant script-fixed-settable cfg-fixed py 'if k in FIXED_NAMES:' 'if False:'
mutant chunk-dup-allowed chunk-dup py 'raise Refuse("the row %s %s is in both %s and %s" % (key[0], key[1], where[key], h["dir"]))' 'pass'
mutant chunk-commit-unchecked chunk-commit py 'if h["meta"].get(k, "") != first["meta"].get(k, ""):' 'if False:'
mutant chunk-size-unchecked chunk-size py 'CHUNK_SAME = ("host", "commit", "tree", "build", "device", "family", "modes", "size", ' 'CHUNK_SAME = ("host", "commit", "tree", "build", "device", "family", "modes", '
mutant chunk-crops-first chunk-merge py 'png = read_crop(local["where"][(oid, sc)], sc, oid)' 'png = read_crop(rdirs[0], sc, oid)'
mutant chunk-first-only chunk-merge py 'res.append(r)' 'res.append(r) if h is first else None'
mutant defaults-no-gating cfg-defaults py 'names = set(c for c in base_cv if c.startswith(GATING_PREFIXES) and c in archived)' 'names = set()'
mutant defaults-not-only-archived cfg-defaults py 'names = set(c for c in base_cv if c.startswith(GATING_PREFIXES) and c in archived)' 'names = set(c for c in base_cv if c.startswith(GATING_PREFIXES))'
mutant archived-any-flag cfg-defaults py "ARCHIVED_RE = re.compile(r'^\\*[ U]" "ARCHIVED_RE = re.compile(r'^[ *][ U]"
mutant defaults-noset cfg-defaults py 'if c in base_cv and c not in noset and c not in RENDERER_REPORTED' 'if c in base_cv and c not in RENDERER_REPORTED'
mutant defaults-modes cfg-defaults py 'if c in base_cv and c not in noset and c not in RENDERER_REPORTED' 'if c in base_cv and c not in noset'
mutant args-unbounded cfg-limit py 'if len(a) > MAX_ENGINE_ARGS:' 'if False:'
mutant cfg-not-cleared cfg-clear py 'ctx.cfg.clear()  # the run starts' 'pass  # the run starts'
mutant owner-not-set-aside cfg-restored py 'os.rename(self.path, self.saved)' 'pass'
mutant restore-noop cfg-restored py 'if not self.held:
            return' 'return'
mutant recover-off cfg-recover py 'belongs to a run that was killed
        if os.path.lexists(self.saved):' 'belongs to a run that was killed
        if False:'
mutant pair-by-name frames-pair py 'k = f[:-4].rsplit("-", 1)[-1]' 'k = f'
mutant order-by-string frames-order py 'return sorted(frames, key=int)' 'return sorted(frames)'
mutant stats-one-word stats-multi py 'reset [a-z ]+, scaler' 'reset \w+, scaler'
mutant stats-timed-only stats-multi py 'scaler (?:([0-9.]+) ms|not timed)' 'scaler ([0-9.]+) ms'
mutant probe-failure-ends probe-skip py 'except RuntimeError as e:
            skipped.append((oid' 'except ZeroDivisionError as e:
            skipped.append((oid'
mutant record-bin-unset record-bin py ', "PINS_BIN": ctx.bin})' '})'
mutant scratch-left record-scratch py 'os.remove(cfg)' 'pass'
mutant scratch-kept clear-scratch py 'raise Refuse("%s is in the way and is not a regular file" % p)
            os.unlink(p)' 'raise Refuse("%s is in the way and is not a regular file" % p)
            pass'
mutant name-unchecked name-abs py 'if not NAME_RE.match(r["option"]) or not NAME_RE.match(r["scenario"]):' 'if False:'
mutant status-unchecked name-status py 'if r["status"] not in ("ok", "error"):' 'if False:'
mutant crop-name-unchecked crop-plain py 'if not NAME_RE.match(sc) or not NAME_RE.match(oid):
        return None' 'if False:
        return None'
mutant crop-follows-link crop-plain py 'os.path.join(cdir, "%s__%s.png" % (sc, oid)), os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)' 'os.path.join(cdir, "%s__%s.png" % (sc, oid)), os.O_RDONLY | os.O_NONBLOCK)'
mutant crop-dir-link crop-plain py 'if not stat.S_ISDIR(os.lstat(cdir).st_mode):' 'if False:'
mutant crop-fifo crop-plain py 'if not stat.S_ISREG(os.fstat(fd).st_mode):
            return None' 'if False:
            return None'
mutant crop-any-status crop-status py 'if not any(r["option"] == oid and r["scenario"] == sc and r["status"] == "ok" for r in local["res"]):' 'if False:'
mutant fifo-opened io-fifo py 'if not stat.S_ISREG(os.fstat(fd).st_mode):
            raise ValueError("%s: not a regular file" % path)' 'if False:
            raise ValueError("%s: not a regular file" % path)'
mutant write-through-link io-hardlink py 'write_atomic(outp, doc.encode("utf-8"))' 'write_file(outp, doc.encode("utf-8"))'
mutant stale-build-passes build-stale py 'raise Refuse("the build in %s is out of date' 'print("the build in %s is out of date'
mutant build-unchecked build-stale py 'if ctx.tree is not None:
        check_build(ctx.tree)' 'if False:
        check_build(ctx.tree)'
mutant commit-of-checkout build-commit py 'commit, dirty = engine_commit(ctx.tree)' 'commit, dirty = engine_commit(os.path.dirname(os.path.dirname(HERE)))'
mutant unchecked-not-said build-unknown py 'ctx.build_note = "not checked: " + why' 'ctx.build_note = "checked"'
mutant rdir-replaced-silently rdir-exists py 'if os.path.lexists(rdir) and not replace:' 'if False:'
mutant crops-kept crops-stale py '            os.unlink(f)
    return rdir' '            pass
    return rdir'
mutant config-not-restored e2e-restored py 'ctx.cfg.restore()' 'pass'
mutant no-sigterm-handler e2e-sigterm py 'for s in (signal.SIGTERM, signal.SIGHUP):' 'for s in ():'
mutant ssim-no-covariance ssim-one c 's = ((2 * ma * mb + c1) * (2 * cov + c2))' 's = ((2 * ma * mb + c1) * (2 * 0 + c2))'
mutant ssim-always-one ssim-less c 'sum += s;' 'sum += 1.0;'
mutant ssim-size-ignored ssim-mismatch c 'size mismatch %dx%d vs %dx%d\n", a.w, a.h, b.w, b.h); return 3;' 'size mismatch %dx%d vs %dx%d\n", a.w, a.h, b.w, b.h); return 0;'
mutant ssim-missing-ok ssim-missing c 'if (!read_luma(argv[1], &a) || !read_luma(argv[2], &b)) return 3;' 'if (!read_luma(argv[1], &a) || !read_luma(argv[2], &b)) return 0;'

mutant newer-config-deleted cfg-both py 'os.rename(self.path, newer)' 'os.unlink(self.path)'
mutant newer-config-overwritten cfg-both py 'os.rename(self.path, newer)' 'pass'
mutant lock-bin-off run-lock-bin py 'fds.append(take_lock(bindir, "the engine binary directory"))' 'pass'
mutant lock-scen-off run-lock-scen py 'fds.append(take_lock(ctx.pins_scen_dir, "the scenarios folder of this checkout"))' 'pass'
mutant lock-blocking run-lock-bin py 'fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)' 'fcntl.flock(fd, fcntl.LOCK_EX)'
mutant lock-fails-open run-lock-bin py 'raise Refuse("another run holds %s (%s), or it cannot be locked (%s): one run per %s at a time" % (what, path, e, what))' 'return -1'
mutant quality-vs-itself e2e-quality py 'diff_frames(ctx, nat[k], fr[k])' 'diff_frames(ctx, nat[k], nat[k])'
mutant crop-is-native e2e-crop py 'img = nimg if oid == "native" else read_tga(frames_by_index(pins[oid])[mid])' 'img = nimg'
mutant crop-frame-first e2e-index py 'mid = nkeys[len(nkeys) // 2]' 'mid = nkeys[0]'
mutant pins-not-in-group e2e-group py 'os.killpg(p.pid, signal.SIGTERM)' 'p.terminate()' 'os.killpg(p.pid, signal.SIGKILL)' 'p.kill()'
mutant pins-kill-first e2e-lock py 'os.killpg(p.pid, signal.SIGTERM)' 'os.killpg(p.pid, signal.SIGKILL)'
mutant stop-wait-unfloored e2e-lock-grace py 'return max(2 * g, g + 5) + 5' 'return 2 * g + 5'
mutant stop-unheld e2e-lock py 'held = signal.pthread_sigmask(signal.SIG_BLOCK, STOP_SIGNALS)' 'held = signal.pthread_sigmask(signal.SIG_BLOCK, [])'
mutant stop-wait-leading-zero e2e-lock-grace py 're.match(r"(0|[1-9][0-9]{0,8})\Z", g)' 're.match(r"[0-9]{1,9}\Z", g)'
mutant stop-wait-cap9 e2e-lock-grace py 're.match(r"(0|[1-9][0-9]{0,8})\Z", g)' 're.match(r"(0|[1-9][0-9]*)\Z", g)'
mutant stop-wait-outlives-lock e2e-lock py 'if LOCK_HELD:' 'if False:'
mutant lock-self-off e2e-lock-wait py 'os.execv("/bin/sh", cmd)' 'off = "mutant"'
mutant lock-lc-all-lost e2e-lock-wait py 'lc = ["LC_ALL=" + e["LC_ALL"]] if "LC_ALL" in e else ["-u", "LC_ALL"]' 'lc = []'
mutant lock-off-silent e2e-lock-off py 'print("metalfx-report: the host lock is off: " + off, file=sys.stderr)' 'pass'
mutant pins-no-group e2e-group py 'rc, out = sh_group([os.path.join(ROOT, "scripts", "pins.sh"), "record", "metal", name], cwd=ROOT, env=env, timeout=900)' 'rc, out = sh([os.path.join(ROOT, "scripts", "pins.sh"), "record", "metal", name], cwd=ROOT, env=env, timeout=900)'

wait
for r in "$tmp"/res-*; do
    line=$(cat "$r")
    case $line in ok*) [ "${VERBOSE:-0}" = 0 ] || echo "${line#ok	}";; *) bad "${line#bad	}";; esac
done
[ "$(ls "$tmp"/res-* | wc -l | tr -d ' ')" -eq "$nm" ] || bad "a mutant run left no result"
if [ "$fail" -eq 0 ]; then echo "check-metalfx-report: ok ($nm mutants killed)"; fi
exit $fail
