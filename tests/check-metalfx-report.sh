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
#   - a failing or silent engine run is an error.
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
mkdir "$tmp/repo" "$tmp/plain" "$tmp/fakegit" "$tmp/real" "$tmp/ro" "$tmp/stubfail" "$tmp/stubtrue" "$tmp/scen" "$tmp/out"
git -C "$tmp/repo" init -q || { bad "git init failed"; exit 1; }
mkdir "$tmp/fakegit/.git"
ln -s "$tmp/real" "$tmp/link"
mkdir "$tmp/plain/dest"; ln -s "$tmp/plain/dest" "$tmp/plain/lnk"
chmod 555 "$tmp/ro"
printf '#!/bin/sh\necho boom >&2\nexit 1\n' > "$tmp/stubfail/git"
printf '#!/bin/sh\necho true\nexit 0\n' > "$tmp/stubtrue/git"
chmod +x "$tmp/stubfail/git" "$tmp/stubtrue/git"
printf 'set timedemo 1\nset r_drawworld 1\nset sw_waterwarp 1\n' > "$tmp/scen/world.cfg"
printf 'set timedemo 1\nset r_rt_shadows 1\n' > "$tmp/scen/rt-shadows.cfg"
# the engine's cvarlist: flags, name, "value". Mentions in other lines are not cvars.
cvl() { printf '%s\n' '*    r_metalfx "0"' "     r_metalfx_modes \"$1\"" '*    r_metalfx_scale "0"' '*    r_metalfx_interp "0"' \
    '     timedemo "0"' '     r_drawworld "1"'; }
{ cvl 3; echo 'ref_metal: warning r_rt_denoiser "1"'; echo 'Unknown command "r_rt_denoiser"'; echo '5 cvars'; } > "$tmp/cv-absent"
{ cvl 31; echo '*    r_validation "0"'; echo '     r_rt_denoiser "0"'; echo '     r_rt_shadows "0"'; echo '12 cvars'; } > "$tmp/cv-present"
{ cvl 1099511627779; echo '12 cvars'; } > "$tmp/cv-big"
printf 'bound\tbound\tstd\tr_metalfx=40\n' > "$tmp/opts-bound"
"$tmp/framediff" --gen "$tmp/a.tga" 64 48 1 || exit 1
"$tmp/framediff" --gen "$tmp/other.tga" 64 48 7 || exit 1
"$tmp/framediff" --perturb "$tmp/a.tga" "$tmp/shift.tga" 10 || exit 1
"$tmp/framediff" --block "$tmp/a.tga" "$tmp/block.tga" 0 0 32 24 255 || exit 1
"$tmp/framediff" --gen "$tmp/small.tga" 32 24 1 || exit 1
# a fake engine: prints what a real one does for the probe and the timing run
mkfake() { # name validation(yes|no) status fps(yes|no)
    { echo '#!/bin/sh'
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

# assertions <dir holding mfx.py> <ssim.c> <scratch dir> [groups]: appends the ids of the failed ones to FAILED
has() { case " ${GROUPS_RUN:-path plan engine report ssim} " in *" $1 "*) return 0;; esac; return 1; }
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
mkdir "$tmp/real-run"; assertions "$SRC" "$SRC/ssim.c" "$tmp/real-run"
if [ "$FAILED" != " " ]; then bad "assertions failed on the real code:$FAILED"; fi

# mutants: one change of the line each assertion covers; the covered assertion must fail
nm=0
mutant() { # name covered-id file(py|c) old new [old new]...: runs in the background, result in $tmp/res-<name>
    local name=$1 id=$2 kind=$3; shift 3
    nm=$((nm+1))
    (
        mdir="$tmp/mut-$name"; ssrc="$SRC/ssim.c"
        mkdir "$mdir" "$mdir/w"; cp "$SRC/options.tsv" "$mdir/"
        if [ "$kind" = py ]; then
            python3 -I "$HELP" mutate "$SRC/mfx.py" "$mdir/mfx.py" "$@" > "$mdir/log" || { echo "bad	mutant $name: $(cat "$mdir/log")" > "$tmp/res-$name"; exit 0; }
        else
            cp "$SRC/mfx.py" "$mdir/mfx.py"
            python3 -I "$HELP" mutate "$SRC/ssim.c" "$mdir/ssim.c" "$@" > "$mdir/log" || { echo "bad	mutant $name: $(cat "$mdir/log")" > "$tmp/res-$name"; exit 0; }
            ssrc="$mdir/ssim.c"
        fi
        FAILED=" "
        case $id in git-*|link-*|unwritable|relative|write-link) g=path;; plan-*|accepted-*) g=plan;; probe-*|timed-*) g=engine;; ssim-*) g=ssim;; *) g=report;; esac
        assertions "$mdir" "$ssrc" "$mdir/w" "$g" > "$mdir/out" 2>&1
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
mutant other-host-images host-images py 'png = os.path.join(rdir, "crops", "%s__%s.png" % (sc, oid))' 'png = os.path.join(others[-1] if others else rdir, "crops", "%s__%s.png" % (sc, oid))'
mutant external-allowed ext-refuse py 'raise Refuse("the report would refer outside itself: %r" % ext[:3])' 'pass'
mutant external-emitted ext-none py 'raise Refuse("the report would refer outside itself: %r" % ext[:3])' 'pass' 'p.append("</main></body></html>\n")' 'p.append("<link rel=stylesheet href=x.css></main></body></html>\n")'
mutant report-in-tree report-path py 'outp = check_out_path(os.path.abspath(out_html), file=True)' 'outp = os.path.abspath(out_html)'
mutant ssim-no-covariance ssim-one c 's = ((2 * ma * mb + c1) * (2 * cov + c2))' 's = ((2 * ma * mb + c1) * (2 * 0 + c2))'
mutant ssim-always-one ssim-less c 'sum += s;' 'sum += 1.0;'
mutant ssim-size-ignored ssim-mismatch c 'size mismatch %dx%d vs %dx%d\n", a.w, a.h, b.w, b.h); return 3;' 'size mismatch %dx%d vs %dx%d\n", a.w, a.h, b.w, b.h); return 0;'
mutant ssim-missing-ok ssim-missing c 'if (!read_luma(argv[1], &a) || !read_luma(argv[2], &b)) return 3;' 'if (!read_luma(argv[1], &a) || !read_luma(argv[2], &b)) return 0;'

wait
for r in "$tmp"/res-*; do
    line=$(cat "$r")
    case $line in ok*) [ "${VERBOSE:-0}" = 0 ] || echo "${line#ok	}";; *) bad "${line#bad	}";; esac
done
[ "$(ls "$tmp"/res-* | wc -l | tr -d ' ')" -eq "$nm" ] || bad "a mutant run left no result"
if [ "$fail" -eq 0 ]; then echo "check-metalfx-report: ok ($nm mutants killed)"; fi
exit $fail
