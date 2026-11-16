#!/bin/sh
# Invariant: the quake2 adapter of the bench harness
# (scripts/bench/adapters/quake2.sh) and every case it serves
# (scripts/bench/cases/*.case with adapter=quake2) parse and validate, and
# a case whose requires the host lacks is refused with its reason.
#
# Hermetic: no engine, no game data, no GPU. A temporary directory holds the
# lock state, the store, a stand-in game data directory, stand-ins for
# ioreg, pgrep and system_profiler on PATH, a capability file in place of
# the system probe (BENCH_Q2_CAPS) and a stand-in engine, a shell script. It
# covers:
#   - every quake2 case loads (bench.sh case), its settings and requires
#     validate, its targets build its renderer, its scenario exists, and a
#     ray-traced or MetalFX case requires the capability;
#   - the settings refusals: an unknown word, a bad value, a word given
#     twice, a missing required word, a cvar the adapter sets itself or one
#     that picks a directory, a value with "..", a wall too short;
#   - the preflight: with every capability it prints its pre.* lines; it
#     refuses a missing capability (physical-gpu, raytracing, metalfx,
#     display-hz>=N, an unknown one) with its reason, a locked screen,
#     another quake2 running, the validation layer and missing game data;
#     through bench.sh baseline put the refusal exits 3 before any build;
#   - the run against the stand-in engine: the argument list in its order
#     (the scenario's set lines, then the adapter's, then the case's
#     cvars, then the seed's trace in the run's scratch directory, then the
#     demo), TERM after seconds=, the saved configuration removed before
#     the run, the scratch directory removed after it, a wrong renderer, a
#     missing engine support line and a failing exit refused;
#   - the metrics of a synthetic log against hand-computed values, and the
#     engine's own format strings in src/ still being the ones parsed;
#   - the inputs paths name the case's pin scenario;
#   - one mutant: the preflight with its requires check dropped must fail
#     the refusal test.
#
# Usage: tests/check-bench-quake2.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C
me=check-bench-quake2
ROOT=$(pwd -P)
REAL=$ROOT/scripts/bench

for _v in $(env | sed -n 's/^\(BENCH_[A-Za-z0-9_]*\)=.*/\1/p; s/^\(PINS_[A-Za-z0-9_]*\)=.*/\1/p'); do
	unset "$_v"
done
unset MTL_DEBUG_LAYER XDG_CACHE_HOME

T=$(mktemp -d "${TMPDIR:-/tmp}/check-bench-quake2.XXXXXX")
trap 'rm -rf "${T:?}"' EXIT
fails=0
bad() {
	printf '%s: FAIL: %s\n' "$me" "$*" >&2
	fails=$((fails + 1))
}

# Stand-ins on PATH: an unlocked screen, no other quake2, a fast
# system_profiler, and a make that refuses, so nothing here ever builds. A
# test switches them with STUB_LOCKED and STUB_PIDS.
mkdir -p "${T:?}/bin" "${T:?}/data/baseq2"
printf '#!/bin/sh\nif [ -n "${STUB_LOCKED:-}" ]; then echo "  \\"CGSSessionScreenIsLocked\\" = Yes"; fi\necho "+-o Root"\n' > "${T:?}/bin/ioreg"
printf '#!/bin/sh\nif [ -n "${STUB_PIDS:-}" ]; then printf "%%s\\n" $STUB_PIDS; exit 0; fi\nexit 1\n' > "${T:?}/bin/pgrep"
printf '#!/bin/sh\necho "      Chipset Model: Zzq GPU"\n' > "${T:?}/bin/system_profiler"
printf '#!/bin/sh\necho "make is not run by this check" >&2\nexit 99\n' > "${T:?}/bin/make"
chmod +x "${T:?}/bin/ioreg" "${T:?}/bin/pgrep" "${T:?}/bin/system_profiler" "${T:?}/bin/make"
PATH=${T:?}/bin:$PATH
export PATH
printf 'physical-gpu=yes\nraytracing=yes\nmetalfx=yes\ndisplay-hz=60\n' > "${T:?}/caps-all"
printf 'physical-gpu=yes\nraytracing=no\nmetalfx=yes\ndisplay-hz=60\n' > "${T:?}/caps-nort"
printf 'physical-gpu=no\nraytracing=unknown\ndisplay-hz=unknown\n' > "${T:?}/caps-vm"
BENCH_Q2_BASEDIR=${T:?}/data
BENCH_Q2_BUILDS=${T:?}/builds
export BENCH_Q2_BASEDIR BENCH_Q2_BUILDS

# q2h <case> <function> [args...]: runs the function in a subshell with the
# bench core and the quake2 adapter sourced and the case loaded.
q2h() {
	_hc=$1
	shift
	(
		BENCH_ROOT=$REAL
		. "$REAL/core/common.sh"
		. "$REAL/core/case.sh"
		. "$REAL/core/inputs.sh"
		. "$REAL/core/stats.sh"
		. "$REAL/core/measure.sh"
		. "$REAL/adapters/quake2.sh"
		bench_case_load "$_hc"
		_hst=0
		"$@" || _hst=$?
		rm -rf "${BENCH_TMP:?}"
		exit "$_hst"
	)
}

# mkcase <name> <settings> [requires] [wall]: a case file under T.
mkcase() {
	{
		printf 'adapter=quake2\ntargets=client game ref_metal\nruns=1\nmetric=fps\nabs_tol=1\nrel_tol=0\n'
		printf 'settings=%s\n' "$2"
		[ -z "${3:-}" ] || printf 'requires=%s\n' "$3"
		printf 'wall=%s\n' "${4:-40}"
	} > "${T:?}/$1.case"
}

# ---- every quake2 case -----------------------------------------------------

n=0
for f in "$REAL"/cases/*.case; do
	grep -q '^adapter=quake2$' "$f" || continue
	c=$(basename "$f" .case)
	n=$((n + 1))
	sh "$REAL/bench.sh" case "$c" > /dev/null 2> "${T:?}/case.err" || bad "$c does not load: $(cat "${T:?}/case.err")"
	q2h "$f" q2_check_settings 2> "${T:?}/set.err" || bad "$c: its settings are refused: $(cat "${T:?}/set.err")"
	q2h "$f" q2_check_requires "${T:?}/caps-all" > /dev/null 2> "${T:?}/req.err" ||
		bad "$c: its requires are refused on a host with every capability: $(cat "${T:?}/req.err")"
	s=$(sed -n 's/^settings=//p' "$f")
	tg=$(sed -n 's/^targets=//p' "$f")
	fl=$(sed -n 's/^build_flags=//p' "$f")
	rq=$(sed -n 's/^requires=//p' "$f")
	case " $tg " in *" client "*) ;; *) bad "$c: the targets do not build the client" ;; esac
	case " $tg " in *" game "*) ;; *) bad "$c: the targets do not build the game" ;; esac
	case " $s " in
	*" renderer=metal "*) case " $tg " in *" ref_metal "*) ;; *) bad "$c: renderer=metal without the ref_metal target" ;; esac ;;
	*" renderer=gl1-metal "*)
		case " $tg " in *" ref_gl1 "*) ;; *) bad "$c: renderer=gl1-metal without the ref_gl1 target" ;; esac
		case " $fl " in *" WITH_METAL_SHIM=yes "*) ;; *) bad "$c: renderer=gl1-metal without WITH_METAL_SHIM=yes" ;; esac
		;;
	esac
	case " $s " in *" cvar.r_rt_stats="*) case " $rq " in *" raytracing "*) ;; *) bad "$c: r_rt_stats without requires raytracing" ;; esac ;; esac
	case " $s " in *" cvar.r_metalfx="*) case " $rq " in *" metalfx "*) ;; *) bad "$c: r_metalfx without requires metalfx" ;; esac ;; esac
	sc=$(printf '%s\n' "$s" | tr ' ' '\n' | sed -n 's/^scenario=//p')
	[ -z "$sc" ] || [ -f "$ROOT/scripts/pins/scenarios/$sc.cfg" ] || bad "$c: no pin scenario $sc"
done
[ "$n" -ge 32 ] || bad "only $n quake2 cases (the four families have 32)"

# ---- the settings refusals --------------------------------------------------

ok_set='renderer=metal size=640x480 vsync=0 seconds=5'
refuse_set() { # <label> <settings> <reason part> [wall]
	mkcase refuse "$2" '' "${4:-40}"
	if q2h "${T:?}/refuse.case" q2_check_settings 2> "${T:?}/refuse.err"; then
		bad "settings $1: accepted"
	elif ! grep -q -- "$3" "${T:?}/refuse.err"; then
		bad "settings $1: refused without saying '$3': $(cat "${T:?}/refuse.err")"
	fi
}
mkcase good "$ok_set scenario=world seed=1 skip=100 pace=own maxfps=999 hidpi=1 fullscreen=1 demo=demo2.dm2 cvar.r_metalfx=1 cvar.r_metalfx_scale=0.5"
q2h "${T:?}/good.case" q2_check_settings 2> "${T:?}/good.err" || bad "every setting word refused: $(cat "${T:?}/good.err")"
refuse_set unknown "$ok_set colour=red" 'unknown setting colour'
refuse_set renderer 'renderer=soft size=640x480 vsync=0 seconds=5' 'renderer has a value'
refuse_set size "renderer=metal size=640 vsync=0 seconds=5" 'size has a value'
refuse_set twice "$ok_set vsync=1" 'vsync is given twice'
refuse_set missing 'renderer=metal size=640x480 seconds=5' 'have no vsync'
refuse_set reserved "$ok_set cvar.vid_renderer=gl1" 'cvar.vid_renderer has a value'
refuse_set directory "$ok_set cvar.basedir=x" 'cvar.basedir has a value'
refuse_set dotdot "$ok_set cvar.r_x=a..b" 'cvar.r_x has a value'
refuse_set seed "$ok_set seed=01" 'seed has a value'
refuse_set demo "$ok_set demo=../x.dm2" 'demo has a value'
refuse_set word "$ok_set novalue" 'is name=value'
refuse_set wall "$ok_set" 'leaves less than 20 s' 24

# ---- the preflight -----------------------------------------------------------

mkcase all "$ok_set" 'physical-gpu raytracing metalfx display-hz>=60'
if BENCH_Q2_CAPS=${T:?}/caps-all q2h "${T:?}/all.case" adapter_preflight > "${T:?}/pre.out" 2> "${T:?}/pre.err"; then
	for l in pre.caps=file pre.physical-gpu=yes pre.raytracing=yes pre.metalfx=yes pre.display-hz=60 \
		pre.metal_debug_layer=off pre.basedir=ok pre.screen=unlocked pre.engines=0 pre.renderer=metal; do
		grep -qx "$l" "${T:?}/pre.out" || bad "the preflight did not print $l"
	done
	awk '!/^pre\.[A-Za-z0-9_][A-Za-z0-9._-]*=/ { exit 1 }' "${T:?}/pre.out" || bad "the preflight printed a line that is not pre.<name>=<value>"
else
	bad "the preflight refused a host with every capability: $(cat "${T:?}/pre.err")"
fi
# with_env <NAME=value...> -- <command...>: the command in a subshell with
# those variables exported.
with_env() {
	(
		while [ "$1" != -- ]; do
			export "$1"
			shift
		done
		shift
		"$@"
	)
}
refuse_pre() { # <label> <caps file> <requires> <reason part> [NAME=value...]
	_rl=$1 _rc=$2 _rr=$3 _rw=$4
	shift 4
	mkcase rp "$ok_set" "$_rr"
	if with_env BENCH_Q2_CAPS="$_rc" "$@" -- q2h "${T:?}/rp.case" adapter_preflight > /dev/null 2> "${T:?}/rp.err"; then
		bad "preflight $_rl: accepted"
	elif ! grep -q -- "$_rw" "${T:?}/rp.err"; then
		bad "preflight $_rl: refused without saying '$_rw': $(cat "${T:?}/rp.err")"
	fi
}
refuse_pre raytracing "${T:?}/caps-nort" 'raytracing' 'requires raytracing, and this host has no'
refuse_pre vm "${T:?}/caps-vm" 'physical-gpu' 'requires physical-gpu, and this host has no'
refuse_pre metalfx "${T:?}/caps-vm" 'metalfx' 'requires metalfx, and this host has unknown'
refuse_pre hz "${T:?}/caps-all" 'display-hz>=120' 'at least 120 Hz, and this host.s runs at 60 Hz'
refuse_pre hz-unknown "${T:?}/caps-vm" 'display-hz>=60' 'refresh rate is unknown'
refuse_pre unknown-cap "${T:?}/caps-all" 'teleport' 'unknown requirement teleport'
refuse_pre locked "${T:?}/caps-all" '' 'the screen is locked' STUB_LOCKED=1
refuse_pre engine "${T:?}/caps-all" '' 'another quake2 is running (pid 4242)' STUB_PIDS=4242
refuse_pre layer "${T:?}/caps-all" '' 'MTL_DEBUG_LAYER is set' MTL_DEBUG_LAYER=1
refuse_pre data "${T:?}/caps-all" '' 'holds no baseq2' BENCH_Q2_BASEDIR=${T:?}/bin
refuse_pre nocaps "${T:?}/none" '' 'BENCH_Q2_CAPS names no file'

# Through bench.sh: the refusal exits 3, says why, and builds nothing.
e2e_refusal() { # <bench root>
	mkcase rt "$ok_set" 'physical-gpu raytracing'
	rm -rf "${T:?}/state" "${T:?}/store"
	_es=0
	# A build cache the adapter refuses (a relative path), so a preflight
	# that wrongly lets the run through stops before any export or build.
	BENCH_STATE=${T:?}/state BENCH_STORE=${T:?}/store BENCH_Q2_CAPS=${T:?}/caps-nort BENCH_LOCK_WAIT=60 \
		BENCH_Q2_BUILDS=relative/builds \
		sh "$1/bench.sh" baseline put "${T:?}/rt.case" HEAD > "${T:?}/e2e.out" 2> "${T:?}/e2e.err" || _es=$?
	[ "$_es" -eq 3 ] || { echo "status $_es, not 3"; return 1; }
	grep -q 'requires raytracing, and this host has no' "${T:?}/e2e.err" || { echo "no reason given"; return 1; }
	grep -q 'refused the run in its preflight' "${T:?}/e2e.err" || { echo "the core did not say the preflight refused"; return 1; }
	! grep -q 'build cache' "${T:?}/e2e.err" || { echo "the run reached the build"; return 1; }
}
e2e_refusal "$REAL" > "${T:?}/e2e.why" || bad "bench.sh baseline put on a host without ray tracing: $(cat "${T:?}/e2e.why")"

# ---- the run, against a stand-in engine ---------------------------------------

B=${T:?}/build
mkdir -p "$B/release/baseq2"
cat > "$B/release/quake2" << 'EOF'
#!/bin/sh
for a; do printf 'arg %s\n' "$a"; done
[ ! -f "$(dirname "$0")/baseq2/config.cfg" ] || echo "a saved configuration was read"
echo "Loading library: ${FAKE_LIB:-ref_metal.dylib}"; echo "Loading library: game.dylib"
[ -n "${FAKE_NORT:-}" ] || echo "Metal supportsRaytracing: yes, supportsRaytracingFromRender: yes"
[ -z "${FAKE_EXIT:-}" ] || exit "$FAKE_EXIT"
trap 'echo "ref_metal: metal_stats: 10 frames, 2 without a drawable, 0 skipped, 0 failed command buffers, 0 readbacks"; exit 0' TERM
while :; do sleep 0.1; done
EOF
chmod +x "$B/release/quake2"
printf '// a comment\nset timedemo 1\nset cl_lights 1\n' > "$B/scenario.cfg"
run_fake() { # <case> [NAME=value...]: adapter_run into T/run.log
	_fc=$1
	shift
	: > "$B/release/baseq2/config.cfg"
	with_env BENCH_RUN_KIND=run BENCH_RUN_INDEX=1 BENCH_Q2_CAPS="${T:?}/caps-all" "$@" -- \
		q2h "$_fc" adapter_run "$_fc" "$B" "${T:?}/run.log" 2> "${T:?}/run.err"
}
mkcase run 'renderer=metal size=1920x1080 vsync=1 seconds=1 pace=own scenario=rt-shadows seed=7 cvar.r_rt_stats=2' 'raytracing'
t0=$(date +%s)
if run_fake "${T:?}/run.case"; then
	t1=$(date +%s)
	[ $((t1 - t0)) -le 15 ] || bad "the run took $((t1 - t0)) s with seconds=1"
	grep -q '^bench: quake2: TERM after 1 s$' "${T:?}/run.log" || bad "the run did not send TERM after seconds="
	grep -q 'metal_stats: 10 frames' "${T:?}/run.log" || bad "the engine's teardown line is not in the log"
	grep -q 'a saved configuration was read' "${T:?}/run.log" && bad "the run kept the previous saved configuration"
	sed -n 's/^arg //p' "${T:?}/run.log" | tr '\n' ' ' > "${T:?}/args"
	want="-datadir ${T:?}/data -portable +set timedemo 1 +set cl_lights 1 +set vid_renderer metal +set vid_fullscreen 0 +set r_mode -1 +set r_customwidth 1920 +set r_customheight 1080 +set vid_highdpiaware 0 +set r_vsync 1 +set s_initsound 0 +set timedemo 0 +set r_rt_stats 2 +set pins_dir $B/run-run-1 +set pins_record trace.q2r +set pins_seed 7 +demomap demo1.dm2 "
	[ "$(cat "${T:?}/args")" = "$want" ] || bad "the engine's arguments: $(cat "${T:?}/args")"
	[ ! -e "$B/run-run-1" ] || bad "the run's scratch directory (the trace) was left behind"
else
	bad "a run of the stand-in engine failed: $(cat "${T:?}/run.err")"
fi
run_fake "${T:?}/run.case" FAKE_LIB=ref_gl1.dylib && bad "a run on another renderer was accepted"
grep -q "ran the renderer 'ref_gl1', not metal" "${T:?}/run.err" || bad "a wrong renderer is not named: $(cat "${T:?}/run.err")"
run_fake "${T:?}/run.case" FAKE_NORT=1 && bad "a run whose engine reports no ray tracing was accepted"
grep -q 'reports no ray tracing' "${T:?}/run.err" || bad "the missing ray tracing line is not said: $(cat "${T:?}/run.err")"
run_fake "${T:?}/run.case" FAKE_EXIT=3 && bad "an engine that exits 3 was accepted"
grep -q 'exited with status 3' "${T:?}/run.err" || bad "the failing exit is not said: $(cat "${T:?}/run.err")"
run_fake "${T:?}/run.case" STUB_PIDS=99 && bad "a run beside another quake2 was accepted"

# ---- the metrics --------------------------------------------------------------

L=${T:?}/metrics.log
{
	echo 'Loading library: ref_metal.dylib'
	echo 'ref_metal: metal_stats: metalfx spatial: input 960x540, output 1920x1080 at 0,0, reset first, scaler not timed'
	echo 'ref_metal: metal_stats: metalfx spatial: input 960x540, output 1920x1080 at 0,0, reset none, scaler 5.000 ms'
	echo 'ref_metal: metal_stats: metalfx spatial: input 960x540, output 1920x1080 at 0,0, reset none, scaler 0.800 ms'
	echo 'ref_metal: metal_stats: metalfx spatial: input 960x540, output 1920x1080 at 0,0, reset size, scaler 1.000 ms'
	echo 'ref_metal: metal_stats: metalfx spatial: input 960x540, output 1920x1080 at 0,0, reset none, scaler 0.900 ms'
	echo 'ref_metal: rt shadows: frame 1: 2073600 pixels, 0 in range, 0 traced, 0 shadowed, 0 rays, - ms'
	echo 'ref_metal: rt shadows: frame 2: 2073600 pixels, 9 in range, 9 traced, 1 shadowed, 36 rays, 9.000 ms'
	echo 'ref_metal: rt shadows: frame 3: 2073600 pixels, 0 in range, 0 traced, 0 shadowed, 0 rays, 1.300 ms'
	echo 'ref_metal: rt shadows: frame 4: 2073600 pixels, 5 in range, 5 traced, 2 shadowed, 20 rays, 1.700 ms'
	echo 'ref_metal: rt shadows: frame 5: 2073600 pixels, 7 in range, 7 traced, 3 shadowed, 28 rays, 2.100 ms'
	echo 'ref_metal: rt shadows: frame 6: 2073600 pixels, 7 in range, 7 traced, 3 shadowed, 28 rays, - ms'
	echo 'ref_metal: rt ao: frame 1: 2073600 pixels, 9 traced, 1 occluded, 0 at the floor, 2 up (0 occluded), 18 rays, 1 hits, 8.000 ms'
	echo 'ref_metal: rt ao: frame 2: 2073600 pixels, 9 traced, 1 occluded, 0 at the floor, 2 up (0 occluded), 18 rays, 1 hits, 8.000 ms'
	echo 'Map: base2ref_metal: rt ao: frame 3: 2073600 pixels, 9 traced, 1 occluded, 0 at the floor, 2 up (0 occluded), 18 rays, 1 hits, 3.600 ms'
	echo 'ref_metal: rt ao: frame 4: 2073600 pixels, 9 traced, 1 occluded, 0 at the floor, 2 up (0 occluded), 18 rays, 1 hits, 3.700 ms'
	echo '689 frames, 0.1 seconds: 6890.0 fps'
	echo '12 frames, 9.0 seconds: 1.3 fps'
	echo 'ref_metal: metal_stats: 1000 frames, 400 without a drawable, 3 skipped, 1 failed command buffers, 0 readbacks'
} > "$L"
mkcase m "$ok_set skip=2"
q2h "${T:?}/m.case" adapter_metrics "${T:?}/m.case" "$L" > "${T:?}/m.out" 2> "${T:?}/m.err" || bad "the metrics failed: $(cat "${T:?}/m.err")"
cat > "${T:?}/m.want" << 'EOF'
metric.fps=6890.0
metric.td_frames=689
metric.td_seconds=0.1
metric.frames=1000
metric.no_drawable=400
metric.presented=600
metric.skipped=3
metric.failed=1
metric.scaler_ms=0.9
metric.scaler_ms_p90=1
metric.scaler_ms_n=3
metric.rts_ms=1.7
metric.rts_ms_p90=2.1
metric.rts_ms_n=3
metric.rts_traced_ms=1.9
metric.rts_traced_ms_p90=2.1
metric.rts_traced_ms_n=2
metric.rtao_ms=3.65
metric.rtao_ms_p90=3.7
metric.rtao_ms_n=2
EOF
cmp -s "${T:?}/m.want" "${T:?}/m.out" || bad "the metrics of the synthetic log: $(diff "${T:?}/m.want" "${T:?}/m.out" | tr '\n' ' ')"
# after_frame=3: the pass lines of frames 4 to 6 only ("-" not timed); the
# scaler's lines have no frame number and are all counted
mkcase m3 "$ok_set after_frame=3"
q2h "${T:?}/m3.case" adapter_metrics "${T:?}/m3.case" "$L" > "${T:?}/m3.out" 2> "${T:?}/m.err" || bad "the metrics failed: $(cat "${T:?}/m.err")"
for l in metric.rts_ms=1.9 metric.rts_ms_n=2 metric.rts_traced_ms_n=2 metric.rtao_ms=3.7 metric.rtao_ms_n=1 metric.scaler_ms=0.95 metric.scaler_ms_n=4; do
	grep -qx "$l" "${T:?}/m3.out" || bad "after_frame=3: no $l in $(tr '\n' ' ' < "${T:?}/m3.out")"
done
L=${T:?}/mgl.log
printf 'mgl: 500 frames, 20 without a drawable, 0 skipped, 0 draws past the vertex ring\nmgl: 2 command buffers failed\n' > "$L"
q2h "${T:?}/m.case" adapter_metrics "${T:?}/m.case" "$L" > "${T:?}/m.out" 2> /dev/null || true
printf 'metric.frames=500\nmetric.no_drawable=20\nmetric.presented=480\nmetric.skipped=0\nmetric.failed=2\n' | cmp -s - "${T:?}/m.out" ||
	bad "the shim's totals: $(tr '\n' ' ' < "${T:?}/m.out")"

# The engine still prints the lines the adapter reads.
fmt() { # <file> <fixed text>
	grep -qF -- "$2" "$ROOT/$1" || bad "$1 no longer prints: $2"
}
fmt src/client/cl_network.c '"%i frames, %3.1f seconds: %3.1f fps\n\n"'
fmt src/client/vid/vid.c 'Com_Printf("Loading library: %s\n", reflib_name);'
fmt src/client/refresh/metal/rm_main.c '"Metal supportsRaytracing: %s, supportsRaytracingFromRender: %s\n"'
fmt src/client/refresh/metal/rm_pure.c '"ref_metal: metal_stats: %lu frames, %lu without a drawable, "'
fmt src/client/refresh/metal/rm_pure.c '"%lu skipped, %lu failed command buffers, "'
fmt src/client/refresh/gl1/metal/mgl_layer.m '"mgl: %lu frames, %lu without a drawable, %lu skipped, '
fmt src/client/refresh/gl1/metal/mgl_layer.m '"mgl: %lu command buffers failed\n"'
fmt src/client/refresh/metal/rm_metalfx.c '"ref_metal: metal_stats: metalfx %s: input %dx%d, output %dx%d at %d,%d, reset"'
fmt src/client/refresh/metal/rm_metalfx.c '", scaler %.3f ms"'
fmt src/client/refresh/metal/rm_metalfx.c '"ref_metal: MetalFX supportsDevice: spatial %s, '
fmt src/client/refresh/metal/rm_rtshadow.c '"ref_metal: rt shadows: frame %lu: %lu pixels, %lu in range, %lu traced, %lu shadowed, "'
fmt src/client/refresh/metal/rm_rtshadow.c '"%lu rays, %s ms"'
fmt src/client/refresh/metal/rm_rtao.c '"ref_metal: rt ao: frame %lu: %lu pixels, %lu traced, %lu occluded, %lu at the floor, "'
fmt src/client/refresh/metal/rm_rtao.c '"%lu up (%lu occluded), %lu rays, %lu hits, %s ms"'
fmt src/backends/unix/signalhandler.c 'signal(SIGTERM, terminate);'

# ---- the inputs paths ---------------------------------------------------------

q2h "$REAL/cases/rt-shadows-gpu-1920x1080.case" adapter_inputs_paths HEAD > "${T:?}/paths" 2> /dev/null || bad "adapter_inputs_paths failed"
for p in Makefile src third_party scripts/pins/scenarios/rt-shadows.cfg; do
	grep -qx "$p" "${T:?}/paths" || bad "the inputs paths of rt-shadows-gpu-1920x1080 do not name $p"
done

# ---- one mutant ---------------------------------------------------------------

M=${T:?}/mutant
mkdir -p "$M"
cp -R "$REAL/." "$M/"
sed 's/q2_check_requires "$_qp_caps" || _qp_bad=1/q2_check_requires "$_qp_caps" || true/' "$REAL/adapters/quake2.sh" > "$M/adapters/quake2.sh"
if cmp -s "$REAL/adapters/quake2.sh" "$M/adapters/quake2.sh"; then
	bad "the mutant did not apply"
elif e2e_refusal "$M" > /dev/null 2>&1; then
	bad "the mutant (the requires check dropped) passed the refusal test"
fi

if [ "$fails" -ne 0 ]; then
	printf '%s: %d failures\n' "$me" "$fails" >&2
	exit 1
fi
printf '%s: ok (%d cases)\n' "$me" "$n"
