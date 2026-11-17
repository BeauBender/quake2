# scripts/bench/adapters/quake2.sh: the quake2 adapter of the bench harness.
#
# Builds a commit of this repository from an export, runs the engine over a
# demo with a case's settings, and reads the timings the engine prints: the
# timedemo line, the metal_stats lines (the renderer's totals at its close,
# each MetalFX scaler pass's GPU time) and the r_rt_stats 2 lines (each
# ray-traced pass's GPU time). The interface is in
# docs/plans/bench-harness.md ("Adapter interface"); the cases and the
# commands are in doc/100_renderpins.md ("Timings with bench.sh").
#
# Environment:
#   BENCH_Q2_BASEDIR  the directory that holds baseq2/: the owner's game
#                     data, read only, never committed. Default: PINS_BASEDIR
#   BENCH_Q2_BUILDS   the build cache: one export and build per inputs id,
#                     outside any repository, owned by this user and
#                     writable by no one else; never pruned (see
#                     doc/100_renderpins.md). make runs with the caller's
#                     build variables cleared (Q2_BUILD_ENV, every WITH_*).
#                     Default: <cache home>/<repository>-bench-builds
#   BENCH_Q2_JOBS     make -j for a build. Default: hw.ncpu, else 4
#   BENCH_Q2_LOGS     a directory, outside any repository, that keeps a copy
#                     of every run's engine log (text only: no frame, no
#                     trace). Default: unset, the logs go with the run
#   BENCH_Q2_CAPS     a file of capability lines (physical-gpu=yes,
#                     raytracing=no, metalfx=yes, metalfx-temporal=yes,
#                     display-hz=60) read in place of the system probe, for
#                     the self-test and for a host the probe cannot read.
#                     What is still checked whatever the file says:
#                     physical-gpu against kern.hv_vmm_present at preflight
#                     (a VM, or a sysctl that fails, is not a physical GPU);
#                     raytracing, metalfx and metalfx-temporal against the
#                     engine's own support lines after each run, so one
#                     claimed here that the engine lacks fails the run. A
#                     display-hz requirement is refused with a file, since
#                     nothing else reads the display
#
# Its pre-warm workload (adapter_prewarm, for the core's prewarm=) is the
# case's own run with vsync=0 and seconds= cut to the time left.
#
# The case keys it reads:
#   targets      make targets, for example "client game ref_metal"
#   build_flags  make variables, NAME=value words, for example
#                WITH_METAL_SHIM=yes (part of the inputs id)
#   requires     capabilities, words: physical-gpu, raytracing, metalfx,
#                display-hz>=<n>. Read from the system (quake2-caps.m, which
#                asks Metal and the display what ref_metal asks) or
#                BENCH_Q2_CAPS; one the host lacks refuses the case with its
#                reason. metalfx in a case that runs the temporal scaler
#                (cvar.r_metalfx=2 or more) also requires the temporal
#                scaler's support. A run then checks the engine's own lines:
#                "Metal supportsRaytracing: yes" for raytracing, "MetalFX
#                supportsDevice: spatial yes" for metalfx, "temporal yes" on
#                that line for a temporal case
#   settings     words name=value, each at most once:
#     renderer=metal|gl1-metal   required; gl1-metal is ref_gl1 built with
#                                WITH_METAL_SHIM=yes, checked in the log
#     size=<w>x<h>               required; r_mode -1 and the custom size
#     vsync=0|1                  required; r_vsync
#     seconds=<n>                required; the engine gets TERM n seconds
#                                after launch, and quits at its next frame
#     fullscreen=0|1             vid_fullscreen (0)
#     hidpi=0|1                  vid_highdpiaware (0)
#     pace=timedemo|own          timedemo 1 or 0 (timedemo)
#     maxfps=<n>                 vid_maxfps
#     demo=<name>.dm2            what +demomap plays (demo1.dm2)
#     scenario=<name>            the set lines of a pin scenario,
#                                scripts/pins/scenarios/<name>.cfg of the
#                                measured commit (part of the inputs id),
#                                before the adapter's own settings, so pace
#                                and the rest override it
#     seed=<n>                   pins_seed, a fixed PRNG seed from each map
#                                load; the engine takes it only beside a
#                                trace (pins_record), which is written into
#                                the run's scratch directory and removed
#                                after the run; no frame is captured
#     skip=<n>                   per-frame stat lines skipped at the start of
#                                a run, for each kind of line (0)
#     after_frame=<n>            only the r_rt_stats 2 lines of frames
#                                numbered above n (their "frame <f>:"), for a
#                                pass that does not run on every frame (0)
#     cvar.<name>=<value>        one more +set; not one the adapter sets, nor
#                                one that picks a directory or the game
#
# Metrics (metric.<name>), each when its lines are in the log:
#   fps, td_frames, td_seconds   the timedemo line ("<n> frames, <s>
#                                seconds: <f> fps")
#   frames, no_drawable, presented, skipped, failed
#                                the totals at the renderer's close (metal_stats
#                                in ref_metal, the shim's "mgl:" line);
#                                presented is frames less no_drawable
#   scaler_ms, scaler_ms_p90, scaler_ms_n
#                                the MetalFX scaler's GPU time per timed view
#                                (metal_stats 2), past skip lines
#   rts_ms, rts_ms_p90, rts_ms_n, rts_traced_ms, rts_traced_ms_n
#                                the shadow pass's GPU time per frame
#                                (r_rt_stats 2) in the window; _traced only
#                                the frames that traced a ray
#   rtao_ms, rtao_ms_p90, rtao_ms_n
#                                the AO pass's GPU time per frame
# The engine prints no time per frame, so the counting window is in frames
# (skip, after_frame), and a rate over a window of seconds, as FIX51's probe
# counted, is not available.
#
# Every value handed to the engine holds only A-Z a-z 0-9 _ . / - (the
# engine re-joins its command line unquoted), as scripts/pins.sh requires.

# q2_say <message...>: one line on standard error.
q2_say() {
	printf 'bench: quake2: %s\n' "$*" >&2
}

# q2_word <name>: the value of <name>= in the case's settings, empty if unset.
q2_word() {
	bench_case_get settings | tr ' ' '\n' | awk -v k="$1=" 'index($0, k) == 1 { print substr($0, length(k) + 1); exit }'
}

# q2_safe <text>: only A-Z a-z 0-9 _ . / -, not empty, no "..".
q2_safe() {
	case $1 in
	'' | *[!A-Za-z0-9_./-]* | *..*) return 1 ;;
	esac
	return 0
}

# q2_value_ok <text>: a cvar value: A-Z a-z 0-9 _ . -, no "..", and one that
# starts with "-" is a number.
q2_value_ok() {
	case $1 in
	'' | *[!A-Za-z0-9_.-]* | *..*) return 1 ;;
	-*) printf '%s\n' "$1" | grep -E -q '^-([0-9]+\.?[0-9]*|\.[0-9]+)$' ;;
	esac
}

# q2_cvar_ok <name>: a cvar name the case may set: [a-z0-9_]+, not one the
# adapter sets itself, and not one that picks a directory or the game.
q2_cvar_ok() {
	case $1 in
	'' | *[!a-z0-9_]*) return 1 ;;
	pins_* | vid_renderer | vid_fullscreen | r_mode | r_customwidth | r_customheight | s_initsound) return 1 ;;
	vid_highdpiaware | r_vsync | timedemo | vid_maxfps) return 1 ;;
	basedir | cddir | game | datadir | cfgdir) return 1 ;;
	esac
	return 0
}

# q2_uint_in <text> <lo> <hi>: digits, from <lo> to <hi>.
q2_uint_in() {
	bench_is_uint "$1" || return 1
	[ "${#1}" -le 9 ] && [ "$1" -ge "$2" ] && [ "$1" -le "$3" ]
}

# q2_check_settings: every word of settings is known and valid, none twice,
# and the required ones are there. Says each fault; 1 when there is one.
q2_check_settings() {
	_qs_bad=0
	_qs_seen=' '
	set -f
	# shellcheck disable=SC2046 # the settings are words; globbing is off
	set -- $(bench_case_get settings)
	set +f
	for _qs_w in "$@"; do
		case $_qs_w in
		?*=*) ;;
		*)
			q2_say "a setting is name=value: $_qs_w"
			_qs_bad=1
			continue
			;;
		esac
		_qs_n=${_qs_w%%=*}
		_qs_v=${_qs_w#*=}
		case $_qs_seen in
		*" $_qs_n "*)
			q2_say "the setting $_qs_n is given twice"
			_qs_bad=1
			continue
			;;
		esac
		_qs_seen="$_qs_seen$_qs_n "
		_qs_ok=1
		case $_qs_n in
		renderer) case $_qs_v in metal | gl1-metal) ;; *) _qs_ok=0 ;; esac ;;
		size) printf '%s\n' "$_qs_v" | grep -E -q '^[1-9][0-9]{1,4}x[1-9][0-9]{1,4}$' || _qs_ok=0 ;;
		vsync | fullscreen | hidpi) case $_qs_v in 0 | 1) ;; *) _qs_ok=0 ;; esac ;;
		pace) case $_qs_v in timedemo | own) ;; *) _qs_ok=0 ;; esac ;;
		seconds) q2_uint_in "$_qs_v" 1 3600 || _qs_ok=0 ;;
		maxfps) q2_uint_in "$_qs_v" 1 100000 || _qs_ok=0 ;;
		skip | after_frame) q2_uint_in "$_qs_v" 0 1000000 || _qs_ok=0 ;;
		seed) printf '%s\n' "$_qs_v" | grep -E -q '^(0|[1-9][0-9]{0,8})$' || _qs_ok=0 ;;
		demo) printf '%s\n' "$_qs_v" | grep -E -q '^[A-Za-z0-9_][A-Za-z0-9_.-]*\.dm2$' && q2_safe "$_qs_v" || _qs_ok=0 ;;
		scenario) printf '%s\n' "$_qs_v" | grep -E -q '^[a-z0-9_][a-z0-9_-]*$' || _qs_ok=0 ;;
		cvar.*) q2_cvar_ok "${_qs_n#cvar.}" && q2_value_ok "$_qs_v" || _qs_ok=0 ;;
		*)
			q2_say "unknown setting $_qs_n"
			_qs_bad=1
			continue
			;;
		esac
		if [ "$_qs_ok" -eq 0 ]; then
			q2_say "the setting $_qs_n has a value it does not take: $_qs_v"
			_qs_bad=1
		fi
	done
	for _qs_n in renderer size vsync seconds; do
		case $_qs_seen in
		*" $_qs_n "*) ;;
		*)
			q2_say "the settings have no $_qs_n"
			_qs_bad=1
			;;
		esac
	done
	_qs_s=$(q2_word seconds)
	if [ "$_qs_bad" -eq 0 ] && [ "$(bench_case_wall)" -lt $((_qs_s + 20)) ]; then
		q2_say "the case's wall ($(bench_case_wall) s) leaves less than 20 s past seconds=$_qs_s for the engine to quit"
		_qs_bad=1
	fi
	return "$_qs_bad"
}

# q2_physical <kern.hv_vmm_present> <gpu>: physical-gpu=yes only for 0 (no
# hypervisor) and a GPU that answered yes; no for a VM or no GPU; unknown
# for anything else (a sysctl that failed, an answer that is not 0 or 1).
q2_physical() {
	case $1.$2 in
	0.yes) echo physical-gpu=yes ;;
	1.* | *.no) echo physical-gpu=no ;;
	*) echo physical-gpu=unknown ;;
	esac
}

# q2_caps: the capability lines: BENCH_Q2_CAPS, or the system probe
# (quake2-caps.m, built with the host's cc into the temporary directory).
# Prints physical-gpu, raytracing, metalfx, metalfx-temporal and display-hz
# lines (yes, no, a number or unknown) and a caps= line saying which source
# answered. physical-gpu is always checked against kern.hv_vmm_present,
# whatever the file says, so a file cannot make a VM pass. Run it in the
# main shell: the build and the probe run under bench_bounded.
q2_caps() {
	_qc_vm=$(sysctl -n kern.hv_vmm_present 2> /dev/null) || _qc_vm=
	if [ -n "${BENCH_Q2_CAPS:-}" ]; then
		[ -f "$BENCH_Q2_CAPS" ] || {
			q2_say "BENCH_Q2_CAPS names no file"
			return 1
		}
		q2_physical "$_qc_vm" "$(bench_file_kv "$BENCH_Q2_CAPS" physical-gpu)"
		for _qc_k in raytracing metalfx metalfx-temporal display-hz; do
			_qc_v=$(bench_file_kv "$BENCH_Q2_CAPS" "$_qc_k")
			printf '%s=%s\n' "$_qc_k" "${_qc_v:-unknown}"
		done
		echo caps=file
		return 0
	fi
	_qc_p=${BENCH_TMP:?}/quake2-caps
	_qc_st=0
	bench_bounded 120 cc -fobjc-arc -O1 -o "$_qc_p" "$BENCH_ROOT/adapters/quake2-caps.m" \
		-framework Foundation -framework Metal -framework MetalFX -framework CoreGraphics \
		> "${BENCH_TMP:?}/quake2-caps.cc" 2>&1 || _qc_st=$?
	: > "${BENCH_TMP:?}/quake2-caps.out"
	if [ "$_qc_st" -eq 0 ]; then
		bench_bounded 30 "$_qc_p" > "${BENCH_TMP:?}/quake2-caps.out" 2> /dev/null || _qc_st=$?
	fi
	_qc_o=${BENCH_TMP:?}/quake2-caps.out
	q2_physical "$_qc_vm" "$(bench_file_kv "$_qc_o" gpu)"
	for _qc_k in raytracing metalfx metalfx-temporal display-hz; do
		_qc_v=$(bench_file_kv "$_qc_o" "$_qc_k")
		printf '%s=%s\n' "$_qc_k" "${_qc_v:-unknown}"
	done
	if [ "$_qc_st" -eq 0 ]; then echo caps=probe; else echo caps=sysctl; fi
}

# q2_temporal: the case's settings run a temporal MetalFX scaler
# (cvar.r_metalfx of 2 or more: 2 is the temporal scaler, 3 the denoised
# one, which runs as 2 until a ray-traced effect exists).
q2_temporal() {
	_qt_v=$(q2_word cvar.r_metalfx)
	[ -n "$_qt_v" ] && awk -v v="$_qt_v" 'BEGIN { exit !(v + 0 >= 2) }'
}

# q2_check_requires <caps file>: every word of requires is met by the
# capability lines in <caps file>; prints pre.<capability>=<value> for
# each, and says each one the host lacks, with what it has. metalfx in a
# case that runs the temporal scaler (q2_temporal) also requires
# metalfx-temporal, since the engine would quietly fall back to the spatial
# scaler. display-hz is refused when the lines come from a capabilities
# file (caps=file): only the system probe reads the display.
q2_check_requires() {
	_qq_bad=0
	_qq_caps=$1
	set -f
	# shellcheck disable=SC2046 # the requirements are words; globbing is off
	set -- $(bench_case_get requires)
	set +f
	for _qq_r in "$@"; do
		case $_qq_r in
		physical-gpu | raytracing | metalfx)
			_qq_v=$(bench_file_kv "${_qq_caps:?}" "$_qq_r")
			printf 'pre.%s=%s\n' "$_qq_r" "${_qq_v:-unknown}"
			if [ "$_qq_v" != yes ]; then
				q2_say "the case requires $_qq_r, and this host has ${_qq_v:-unknown}"
				_qq_bad=1
			fi
			if [ "$_qq_r" = metalfx ] && q2_temporal; then
				_qq_v=$(bench_file_kv "${_qq_caps:?}" metalfx-temporal)
				printf 'pre.metalfx-temporal=%s\n' "${_qq_v:-unknown}"
				if [ "$_qq_v" != yes ]; then
					q2_say "the case runs the temporal scaler (r_metalfx $(q2_word cvar.r_metalfx)), so it requires metalfx-temporal, and this host has ${_qq_v:-unknown}"
					_qq_bad=1
				fi
			fi
			;;
		display-hz\>=*)
			_qq_n=${_qq_r#display-hz>=}
			_qq_v=$(bench_file_kv "${_qq_caps:?}" display-hz)
			printf 'pre.display-hz=%s\n' "${_qq_v:-unknown}"
			if ! q2_uint_in "$_qq_n" 1 1000; then
				q2_say "display-hz>= takes a whole number of hertz: $_qq_r"
				_qq_bad=1
			elif [ "$(bench_file_kv "${_qq_caps:?}" caps)" = file ]; then
				q2_say "the case requires a display of at least $_qq_n Hz, which a capabilities file cannot vouch for; unset BENCH_Q2_CAPS so the system probe reads the display"
				_qq_bad=1
			elif ! q2_uint_in "${_qq_v:-x}" 0 100000; then
				q2_say "the case requires a display of at least $_qq_n Hz, and this host's refresh rate is ${_qq_v:-unknown}"
				_qq_bad=1
			elif [ "$_qq_v" -lt "$_qq_n" ]; then
				q2_say "the case requires a display of at least $_qq_n Hz, and this host's runs at $_qq_v Hz"
				_qq_bad=1
			fi
			;;
		*)
			q2_say "unknown requirement $_qq_r (physical-gpu, raytracing, metalfx, display-hz>=<n>)"
			_qq_bad=1
			;;
		esac
	done
	return "$_qq_bad"
}

# q2_basedir: the game data directory, checked: absolute, safe for the
# engine's command line, holding baseq2/.
q2_basedir() {
	_qd=${BENCH_Q2_BASEDIR:-${PINS_BASEDIR:-}}
	if [ -z "$_qd" ]; then
		q2_say "BENCH_Q2_BASEDIR (or PINS_BASEDIR) names no game data directory"
		return 1
	fi
	case $_qd in
	/*) ;;
	*)
		q2_say "BENCH_Q2_BASEDIR must be an absolute path"
		return 1
		;;
	esac
	if ! q2_safe "$_qd"; then
		q2_say "BENCH_Q2_BASEDIR may hold only A-Z a-z 0-9 _ . / - (the engine re-joins its command line)"
		return 1
	fi
	if [ ! -d "$_qd/baseq2" ]; then
		q2_say "BENCH_Q2_BASEDIR holds no baseq2/"
		return 1
	fi
	printf '%s\n' "$_qd"
}

# q2_quiet: the screen is unlocked and no quake2 is running; says which is
# not so. Prints pre.screen and pre.engines lines. It fails closed: the
# screen reads as unlocked only when ioreg exits 0 and lists the registry's
# root node; no other quake2 only when pgrep exits 1 (no match). A tool
# that is missing, fails or answers anything else is pre.<line>=unknown and
# a refusal, never a pass.
q2_quiet() {
	_qz_bad=0
	if ! command -v ioreg > /dev/null 2>&1; then
		q2_say "cannot tell whether the screen is locked (no ioreg)"
		echo pre.screen=unknown
		_qz_bad=1
	else
		_qz_st=0
		_qz_o=$(ioreg -n Root -d1 2> /dev/null) || _qz_st=$?
		if [ "$_qz_st" -ne 0 ] || ! printf '%s\n' "$_qz_o" | grep -q -F -e '+-o Root'; then
			q2_say "cannot tell whether the screen is locked (ioreg exited $_qz_st without listing the registry root)"
			echo pre.screen=unknown
			_qz_bad=1
		elif printf '%s\n' "$_qz_o" | grep -q CGSSessionScreenIsLocked; then
			q2_say "the screen is locked"
			echo pre.screen=locked
			_qz_bad=1
		else
			echo pre.screen=unlocked
		fi
	fi
	if ! command -v pgrep > /dev/null 2>&1; then
		q2_say "cannot tell whether another quake2 runs (no pgrep)"
		echo pre.engines=unknown
		_qz_bad=1
	else
		_qz_st=0
		_qz_p=$(pgrep -x quake2 2> /dev/null) || _qz_st=$?
		if [ "$_qz_st" -eq 1 ]; then
			echo pre.engines=0
		elif [ "$_qz_st" -eq 0 ] && [ -n "$_qz_p" ]; then
			q2_say "another quake2 is running (pid $(printf '%s\n' "$_qz_p" | paste -s -d , -)); wait for it, never stop it"
			printf 'pre.engines=%s\n' "$(printf '%s\n' "$_qz_p" | awk 'END { print NR }')"
			_qz_bad=1
		else
			q2_say "cannot tell whether another quake2 runs (pgrep exited $_qz_st)"
			echo pre.engines=unknown
			_qz_bad=1
		fi
	fi
	return "$_qz_bad"
}

adapter_inputs_paths() {
	printf '%s\n' Makefile src third_party
	git ls-tree --name-only "$1" 2> /dev/null | grep -E '^[A-Za-z0-9._-]+\.make$' || true
	_qi_s=$(q2_word scenario)
	case $_qi_s in
	'' | *[!a-z0-9_-]* | -*) ;;
	*) printf 'scripts/pins/scenarios/%s.cfg\n' "$_qi_s" ;;
	esac
}

adapter_preflight() {
	_qp_bad=0
	q2_check_settings || _qp_bad=1
	_qp_caps=${BENCH_TMP:?}/quake2-caps.lines
	q2_caps > "$_qp_caps" || return 1
	printf 'pre.caps=%s\n' "$(bench_file_kv "$_qp_caps" caps)"
	q2_check_requires "$_qp_caps" || _qp_bad=1
	if [ -n "${MTL_DEBUG_LAYER:-}" ] && [ "$MTL_DEBUG_LAYER" != 0 ]; then
		q2_say "MTL_DEBUG_LAYER is set: a timing is made without the Metal validation layer"
		_qp_bad=1
	fi
	echo pre.metal_debug_layer=off
	if q2_basedir > /dev/null; then echo pre.basedir=ok; else _qp_bad=1; fi
	[ "$_qp_bad" -eq 0 ] || return 1
	q2_quiet || return 1
	printf 'pre.renderer=%s\n' "$(q2_word renderer)"
}

# q2_private_dir <dir> <what>: <dir> (symbolic links followed) is a
# directory owned by this user and writable by no group and no other user,
# so no one else can plant an export or a Makefile in it. The message names
# <what>, never the path.
q2_private_dir() {
	_qo_w=$2
	_qo_l=$(ls -ldLn "$1" 2> /dev/null) || _qo_l=
	_qo_u=$(id -u 2> /dev/null) || _qo_u=
	set -f
	# shellcheck disable=SC2086 # the fields of one ls line; globbing is off
	set -- $_qo_l
	set +f
	case ${1:-}.${3:-} in
	d*.?*) ;;
	*)
		q2_say "cannot read the owner and mode of $_qo_w"
		return 1
		;;
	esac
	if [ -z "$_qo_u" ] || [ "$3" != "$_qo_u" ]; then
		q2_say "$_qo_w is not owned by this user; point it at a directory of your own"
		return 1
	fi
	case $1 in
	?????w* | ????????w*)
		q2_say "$_qo_w is writable by its group or by other users; make it private (chmod go-w) or point it elsewhere"
		return 1
		;;
	esac
}

# q2_builds: the build cache directory, made, outside any repository, owned
# by this user and writable by no one else (q2_private_dir). Run it in
# $(...): the outside-repository guard exits on a refusal, and the caller
# returns on any failure.
q2_builds() {
	if [ -n "${BENCH_Q2_BUILDS:-}" ]; then
		_qu=$BENCH_Q2_BUILDS
	else
		_qu=$(bench_cache_home) || {
			q2_say "no cache home for the build cache (set BENCH_Q2_BUILDS)"
			return 1
		}
		_qu=$_qu/$(bench_repo_name)-bench-builds
	fi
	bench_outside_repo "$_qu" "the build cache (BENCH_Q2_BUILDS)"
	(umask 077 && mkdir -p "$_qu") || {
		q2_say "cannot make the build cache"
		return 1
	}
	q2_private_dir "$_qu" "the build cache (BENCH_Q2_BUILDS)" || return 1
	printf '%s\n' "$_qu"
}

# The caller's variables that change what make builds, cleared for every
# build (with every WITH_*), so a cached build depends on the commit, the
# case's targets and build_flags (passed on make's command line) and the
# host, never on who ran it: the compiler and its flags, the Makefile's own
# switches, and make's flags from a parent make.
Q2_BUILD_ENV='CC CFLAGS CPPFLAGS LDFLAGS LDLIBS INCLUDE DEBUG ASAN UBSAN COVERAGE WERR
METAL_SHADERS NO_SDL_GYRO PKG_CONFIG STATIC_MODULES STATIC_RENDERER STATIC_ROOT
YQ2_ARCH YQ2_OSTYPE MINGW_CHOST SOURCE_DATE_EPOCH
MAKEFLAGS MFLAGS GNUMAKEFLAGS MAKEFILES MAKELEVEL MAKEOVERRIDES'

adapter_build() {
	_qb_c=$1
	_qb_d=$2
	shift 2
	[ "$#" -gt 0 ] || {
		q2_say "the case names no make target"
		return 1
	}
	for _qb_t in "$@"; do
		case $_qb_t in
		'' | -* | *[!A-Za-z0-9_.-]*)
			q2_say "a make target is A-Z a-z 0-9 _ . -: $_qb_t"
			return 1
			;;
		esac
	done
	_qb_f=$(bench_case_get build_flags)
	set -f
	for _qb_w in $_qb_f; do
		printf '%s\n' "$_qb_w" | grep -E -q '^[A-Z][A-Z0-9_]*=[A-Za-z0-9_.-]*$' || {
			set +f
			q2_say "a build flag is NAME=value: $_qb_w"
			return 1
		}
	done
	set +f
	printf '%s\n' "$BENCH_INPUTS" | grep -E -q '^i-[0-9a-f]{12}$' || {
		q2_say "no inputs id for the build"
		return 1
	}
	_qb_cache=$(q2_builds) || return 1
	[ -n "$_qb_cache" ] || return 1
	_qb_e=$_qb_cache/$BENCH_INPUTS
	if [ ! -f "$_qb_e/exported" ]; then
		rm -rf "${_qb_e:?}"
		mkdir -p "$_qb_e/tree" || return 1
		git archive --format=tar "$_qb_c" > "$_qb_e/tree.tar" || {
			q2_say "git archive failed"
			return 1
		}
		(cd "$_qb_e/tree" && tar -xf ../tree.tar) || {
			q2_say "the export could not be unpacked"
			return 1
		}
		rm -f "$_qb_e/tree.tar"
		printf 'commit=%s\n' "$_qb_c" > "$_qb_e/exported"
	fi
	_qb_j=${BENCH_Q2_JOBS:-$(sysctl -n hw.ncpu 2> /dev/null || echo 4)}
	bench_is_uint "$_qb_j" || _qb_j=4
	q2_say "make -j$_qb_j $_qb_f $* (build cache $BENCH_INPUTS)"
	# env -u for each of Q2_BUILD_ENV and every WITH_* of the environment
	# (names only, [A-Z0-9_]; a stray line of a multi-line value at worst
	# names a variable that is not set)
	_qb_u=
	set -f
	for _qb_v in $Q2_BUILD_ENV $(env | sed -n 's/^\(WITH_[A-Za-z0-9_]*\)=.*/\1/p'); do
		_qb_u="$_qb_u -u $_qb_v"
	done
	_qb_st=0
	# shellcheck disable=SC2086 # the names and flags are checked words; globbing is off
	bench_bounded 3600 env $_qb_u make -C "$_qb_e/tree" -j"$_qb_j" $_qb_f "$@" > "$_qb_e/build.log" 2>&1 || _qb_st=$?
	set +f
	if [ "$_qb_st" -ne 0 ]; then
		tail -n 20 "$_qb_e/build.log" >&2
		q2_say "the build failed (status $_qb_st)"
		return 1
	fi
	rm -rf "${_qb_d:?}/release"
	cp -R "$_qb_e/tree/release" "$_qb_d/release" || return 1
	_qb_s=$(q2_word scenario)
	if [ -n "$_qb_s" ]; then
		git show "$_qb_c:scripts/pins/scenarios/$_qb_s.cfg" > "$_qb_d/scenario.cfg" 2> /dev/null || {
			q2_say "the commit has no pin scenario $_qb_s"
			return 1
		}
	fi
}

# The run's wrapper: starts the engine, sends it TERM after $1 seconds (it
# quits at its next frame, and its teardown prints the totals), and exits
# with its status. bench_bounded bounds the whole by the case's wall time.
Q2_RUNNER='s=$1
shift
"$@" &
p=$!
n=0
while kill -0 "$p" 2> /dev/null; do
	if [ "$n" -ge "$((s * 5))" ]; then
		echo "bench: quake2: TERM after $s s"
		kill -TERM "$p" 2> /dev/null
		break
	fi
	sleep 0.2
	n=$((n + 1))
done
st=0
wait "$p" || st=$?
exit "$st"'

# q2_scenario_args <file>: appends the "set <cvar> <value>" lines of a pin
# scenario to the global Q2_ARGS list file, one argument a line; refuses
# any other line, as scripts/pins.sh does.
q2_scenario_args() {
	_qa_n=0
	while IFS= read -r _qa_l || [ -n "$_qa_l" ]; do
		_qa_n=$((_qa_n + 1))
		case $_qa_l in '' | //*) continue ;; esac
		printf '%s\n' "$_qa_l" | grep -E -q '^set [^ ]+ [^ ]+$' || {
			q2_say "scenario line $_qa_n is not 'set <cvar> <value>'"
			return 1
		}
		_qa_k=${_qa_l#set }
		_qa_v=${_qa_k#* }
		_qa_k=${_qa_k%% *}
		if ! q2_cvar_ok "$_qa_k" && [ "$_qa_k" != timedemo ] && [ "$_qa_k" != r_vsync ] && [ "$_qa_k" != vid_maxfps ]; then
			q2_say "scenario line $_qa_n sets $_qa_k, which a scenario may not set"
			return 1
		fi
		q2_value_ok "$_qa_v" || {
			q2_say "scenario line $_qa_n has a value the engine cannot take: $_qa_v"
			return 1
		}
		printf '+set\n%s\n%s\n' "$_qa_k" "$_qa_v" >> "${Q2_ARGS:?}"
	done < "$1"
}

# q2_check_log <log>: the run used the case's renderer, and the engine
# reports each capability the case requires; says what is missing.
q2_check_log() {
	_ql_r=$(q2_word renderer)
	# the last renderer library loaded (the game module is loaded the same way)
	_ql_lib=$(sed -n 's/.*Loading library: \(ref_[A-Za-z0-9_]*\).*/\1/p' "$1" | sed -n '$p')
	case $_ql_r.$_ql_lib in
	metal.ref_metal) ;;
	gl1-metal.ref_gl1)
		grep -m1 '^GL_RENDERER: ' "$1" | grep -q '(gl1-metal)' || {
			q2_say "the engine did not load the Metal shim build of ref_gl1 (build it with WITH_METAL_SHIM=yes)"
			return 1
		}
		;;
	*)
		q2_say "the engine ran the renderer '${_ql_lib:-none}', not $_ql_r"
		return 1
		;;
	esac
	if grep -q 'crashed! This should not happen' "$1"; then
		q2_say "the engine crashed"
		return 1
	fi
	for _ql_q in $(bench_case_get requires); do
		case $_ql_q in
		raytracing)
			grep -q '^Metal supportsRaytracing: yes' "$1" || {
				q2_say "the engine reports no ray tracing on this device"
				return 1
			}
			;;
		metalfx)
			if q2_temporal; then
				# the engine sets a mode the device lacks back to one below it
				grep -q 'MetalFX supportsDevice: spatial [a-z]*, temporal yes' "$1" || {
					q2_say "the engine reports no temporal MetalFX scaler on this device"
					return 1
				}
			else
				grep -q 'MetalFX supportsDevice: spatial yes' "$1" || {
					q2_say "the engine reports no MetalFX scaler on this device"
					return 1
				}
			fi
			;;
		esac
	done
}

adapter_run() {
	_qr_d=$2
	_qr_log=$3
	q2_quiet > /dev/null || return 1
	_qr_base=$(q2_basedir) || return 1
	_qr_bin=$_qr_d/release/quake2
	[ -x "$_qr_bin" ] || {
		q2_say "the build has no release/quake2"
		return 1
	}
	q2_safe "$_qr_d" || {
		q2_say "the temporary directory's path holds a character the engine cannot take; set TMPDIR"
		return 1
	}
	# every run starts without a saved configuration: -portable keeps it
	# beside the binary, and the previous run's would carry its cvars over
	rm -f "${_qr_d:?}/release/baseq2/config.cfg"
	_qr_run=$_qr_d/run-$BENCH_RUN_KIND-$BENCH_RUN_INDEX
	rm -rf "${_qr_run:?}"
	mkdir -p "$_qr_run"
	Q2_ARGS=${BENCH_TMP:?}/quake2-args
	printf '%s\n' -datadir "$_qr_base" -portable > "$Q2_ARGS"
	if [ -n "$(q2_word scenario)" ]; then
		q2_scenario_args "$_qr_d/scenario.cfg" || return 1
	fi
	_qr_size=$(q2_word size)
	_qr_r=$(q2_word renderer)
	case $_qr_r in gl1-metal) _qr_r=gl1 ;; esac
	_qr_td=1
	[ "$(q2_word pace)" != own ] || _qr_td=0
	_qr_fs=$(q2_word fullscreen)
	_qr_hd=$(q2_word hidpi)
	{
		printf '+set\n%s\n%s\n' vid_renderer "$_qr_r" vid_fullscreen "${_qr_fs:-0}" r_mode -1
		printf '+set\n%s\n%s\n' r_customwidth "${_qr_size%x*}" r_customheight "${_qr_size#*x}"
		printf '+set\n%s\n%s\n' vid_highdpiaware "${_qr_hd:-0}" r_vsync "$(q2_word vsync)" s_initsound 0
		printf '+set\n%s\n%s\n' timedemo "$_qr_td"
		_qr_m=$(q2_word maxfps)
		[ -z "$_qr_m" ] || printf '+set\nvid_maxfps\n%s\n' "$_qr_m"
		for _qr_w in $(bench_case_get settings); do
			case $_qr_w in
			cvar.*=*)
				_qr_k=${_qr_w%%=*}
				printf '+set\n%s\n%s\n' "${_qr_k#cvar.}" "${_qr_w#*=}"
				;;
			esac
		done
		_qr_seed=$(q2_word seed)
		if [ -n "$_qr_seed" ]; then
			printf '+set\n%s\n%s\n' pins_dir "$_qr_run" pins_record trace.q2r pins_seed "$_qr_seed"
		fi
		_qr_demo=$(q2_word demo)
		printf '+demomap\n%s\n' "${_qr_demo:-demo1.dm2}"
	} >> "$Q2_ARGS"
	set -f
	_qr_IFS=$IFS
	IFS='
'
	# shellcheck disable=SC2046 # one argument a line; globbing is off
	set -- $(cat "$Q2_ARGS")
	IFS=$_qr_IFS
	set +f
	_qr_st=0
	bench_bounded "$(bench_case_wall)" sh -c "$Q2_RUNNER" quake2-run "$(q2_word seconds)" "$_qr_bin" "$@" \
		> "${_qr_log:?}" 2>&1 || _qr_st=$?
	rm -rf "${_qr_run:?}"
	if [ -n "${BENCH_Q2_LOGS:-}" ]; then
		_qr_keep=$(
			bench_outside_repo "$BENCH_Q2_LOGS" "the log directory (BENCH_Q2_LOGS)"
			mkdir -p "$BENCH_Q2_LOGS" && printf '%s\n' "$BENCH_Q2_LOGS"
		) || return 1
		cp "$_qr_log" "$_qr_keep/$BENCH_CASE_NAME-$(date -u +%Y%m%dT%H%M%SZ)-$BENCH_RUN_KIND-$BENCH_RUN_INDEX.log" || return 1
	fi
	if [ "$_qr_st" -ne 0 ]; then
		tail -n 5 "$_qr_log" >&2
		q2_say "the engine exited with status $_qr_st"
		return 1
	fi
	q2_check_log "$_qr_log"
}

# adapter_prewarm <case> <build dir> <log> <seconds>: one piece of the
# core's pre-warm (TASK166): the case's own run with display sync off
# (vsync=0, so the GPU never waits for the display) and seconds= cut to
# <seconds> when that is shorter, through adapter_run on a copy of the
# case with those two words changed.
adapter_prewarm() {
	_qw_s=$(q2_word seconds)
	bench_is_uint "$4" || {
		q2_say "the pre-warm's length is not whole seconds"
		return 1
	}
	[ "$4" -ge "${_qw_s:-0}" ] || _qw_s=$4
	_qw_c=${BENCH_TMP:?}/quake2-prewarm.case
	awk -v s="$_qw_s" 'index($0, "settings=") == 1 {
		n = split(substr($0, 10), w, " ")
		out = ""
		for (i = 1; i <= n; i++) {
			if (w[i] ~ /^vsync=/) w[i] = "vsync=0"
			else if (w[i] ~ /^seconds=/) w[i] = "seconds=" s
			out = out (i > 1 ? " " : "") w[i]
		}
		print "settings=" out
		next
	} { print }' "$1" > "$_qw_c" || return 1
	_qw_keep=$BENCH_CASE_FILE
	BENCH_CASE_FILE=$_qw_c
	_qw_st=0
	adapter_run "$_qw_c" "$2" "$3" || _qw_st=$?
	BENCH_CASE_FILE=$_qw_keep
	return "$_qw_st"
}

adapter_metrics() {
	_qm_log=$2
	_qm_skip=$(q2_word skip)
	_qm_d=${BENCH_TMP:?}/quake2-metrics
	rm -rf "${_qm_d:?}"
	mkdir -p "$_qm_d"
	# A line is found by its prefix anywhere on a log line: the engine's
	# console text does not always end in a newline before the next print,
	# and the shim writes to standard error, unbuffered, between them.
	_qm_after=$(q2_word after_frame)
	awk -v skip="${_qm_skip:-0}" -v after="${_qm_after:-0}" -v d="$_qm_d" '
		function num(s) { return s ~ /^[0-9]+(\.[0-9]+)?$/ }
		function from(p,  i) { i = index($0, p); return i ? substr($0, i) : "" }
		!td && match($0, /(^|[^0-9.])[0-9]+ frames, [0-9]+(\.[0-9]+)? seconds: [0-9]+(\.[0-9]+)? fps$/) {
			s = substr($0, RSTART, RLENGTH)
			sub(/^[^0-9]/, "", s)
			split(s, f, " ")
			td = 1; tdf = f[1]; tds = f[3]; tdfps = f[5]
		}
		{ s = from("ref_metal: metal_stats: ") }
		s ~ /^ref_metal: metal_stats: [0-9]+ frames, [0-9]+ without a drawable, [0-9]+ skipped, [0-9]+ failed command buffers/ {
			split(s, f, " ")
			st = 1; fr = f[3]; nd = f[5]; sk = f[9]; fl = f[11]
		}
		s ~ /^ref_metal: metal_stats: metalfx / {
			if (++fx > skip + 0 && match(s, /, scaler [0-9]+(\.[0-9]+)? ms$/))
				print substr(s, RSTART + 9, RLENGTH - 12) > (d "/scaler_ms")
		}
		{ s = from("mgl: ") }
		s ~ /^mgl: [0-9]+ frames, [0-9]+ without a drawable, [0-9]+ skipped/ {
			split(s, f, " ")
			st = 1; fr = f[2]; nd = f[4]; sk = f[8]
		}
		s ~ /^mgl: [0-9]+ command buffers failed/ { split(s, f, " "); fl = f[2] }
		{ s = from("ref_metal: rt shadows: frame ") }
		s ~ /^ref_metal: rt shadows: frame [0-9]+: / {
			n = split(s, f, " ")
			if (++rs > skip + 0 && f[5] + 0 > after + 0 && n == 18 && num(f[17])) {
				print f[17] > (d "/rts_ms")
				if (f[11] + 0 > 0) print f[17] > (d "/rts_traced_ms")
			}
		}
		{ s = from("ref_metal: rt ao: frame ") }
		s ~ /^ref_metal: rt ao: frame [0-9]+: / {
			n = split(s, f, " ")
			if (++ra > skip + 0 && f[5] + 0 > after + 0 && n == 25 && num(f[24])) print f[24] > (d "/rtao_ms")
		}
		END {
			if (td && num(tdf) && num(tds) && num(tdfps))
				printf "metric.fps=%s\nmetric.td_frames=%s\nmetric.td_seconds=%s\n", tdfps, tdf, tds
			if (st && num(fr) && num(nd) && num(sk)) {
				printf "metric.frames=%s\nmetric.no_drawable=%s\nmetric.presented=%d\n", fr, nd, fr - nd
				printf "metric.skipped=%s\nmetric.failed=%d\n", sk, fl + 0
			}
		}' "${_qm_log:?}" || return 1
	for _qm_n in scaler_ms rts_ms rts_traced_ms rtao_ms; do
		[ -s "$_qm_d/$_qm_n" ] || continue
		_qm_s=$(bench_stats < "$_qm_d/$_qm_n") || return 1
		printf '%s\n' "$_qm_s" | awk -v m="$_qm_n" '{
			for (i = 1; i <= NF; i++) { e = index($i, "="); f[substr($i, 1, e - 1)] = substr($i, e + 1) }
			printf "metric.%s=%s\nmetric.%s_p90=%s\nmetric.%s_n=%s\n", m, f["median"], m, f["p90"], m, f["n"]
		}'
	done
}
