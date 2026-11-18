#!/bin/sh
# Invariant: the native Metal renderer ref_metal (src/client/refresh/metal/,
# docs/plans/ref-metal-native.md section 10) keeps its pure-C contracts,
# speaks no OpenGL, and, where the toolchain is there, still builds.
#
# (a) Always, on every OS: tests/metal/rm_host_test.c is compiled with the
#     system compiler against rm_pure.c and common/ (the arithmetic shared
#     with the shim), with ASAN and UBSAN when the
#     compiler has them, and run. No SDL, no Metal, no game data. The
#     world's cases (N4) also link files/surf.c with src/common/shared/
#     shared.c (what ref_metal takes from them unchanged) and the shim's
#     mgl_raster.c behind tests/metal/rm_host_shim.c (its viewport); the
#     model checks (N7) link files/models.c, the shared MD2 and SP2 loader.
#     tests/metal/rm_rt_test.c, the pure part of the ray tracing (rm_rt.c,
#     RT1, TASK53), is compiled and run the same way, with rm_pure.c, and
#     so is tests/metal/rm_gbuf_test.c, the pure part of the G-buffer
#     (rm_gbuf.c and common/mtl_gbuf_math.h, RT2, TASK54),
#     tests/metal/rm_rtshadow_test.c, the pure part of the dynamic-light
#     shadows (rm_rtshadow.c and common/mtl_rts_math.h, RT3, TASK55),
#     tests/metal/rm_rtao_test.c, the pure part of the ambient occlusion
#     (rm_rtao.c and common/mtl_rtao_math.h, RT5, TASK57),
#     tests/metal/rm_rtwater_test.c, the pure part of the water reflections
#     (rm_rtwater.c and common/mtl_rtw_math.h, RT6, TASK58),
#     tests/metal/rm_rtsun_test.c, the pure part of the sun and sky shadow
#     (rm_rtsun.c and common/mtl_rtsun_math.h, RT4, TASK56), and
#     tests/metal/rm_fx_test.c, the pure part of the MetalFX scalers
#     (rm_metalfx.c, TASK111), and tests/metal/rm_rtgate_test.c, the
#     hardware gate of ray tracing (rm_rtgate.c, RT8, TASK60). The gate's
#     table of the r_rt_* cvars is then held to the sources: every
#     Cvar_Get("r_rt_ name of src/client/refresh/metal's .c and .m files
#     (a call over several lines included) must have a row, every row
#     such a name, and an effect switch's default must be "0"; scratch
#     mutants (an unclassified cvar on one line and over two, a row whose
#     cvar is gone, an effect switch defaulting to 1) must fail it.
# (b) Only on Darwin with the Metal compiler (xcrun -f metal) and the
#     vendored SDL 3 headers: "make ref_metal" in a scratch copy of
#     Makefile, third_party/sdl3.make, scripts/rt/ (the sun's table, TASK56)
#     and src/ (third_party/SDL3 is linked,
#     not copied), so release/ and build/ of this checkout are not
#     touched; the build must write ref_metal.metallib next to the dylib,
#     and otool -L of the dylib must list Metal and QuartzCore and no
#     OpenGL, and otool -l must show MetalFX as a weak dylib (decision 0011). In the same copy "make ref_metal YQ2_OSTYPE=Linux" must fail
#     with a line naming macOS. Otherwise one line
#     "check-ref-metal: build skipped: <reason>". When the vendored
#     headers exist, a Makefile that cannot be evaluated, or gives an
#     SDLCFLAGS that is empty or cannot compile <SDL3/SDL.h>, fails the
#     check (the probe runs with the make flags cleared, as the builds).
# (c) Always: the vocabulary check. Under src/client/refresh/metal/ no
#     gl[A-Z] call and no GL_ identifier in code (outside comments and
#     string literals; a literal continued by a backslash at the end of
#     its line, blanks or a CR after it allowed, goes on on the next), no
#     <OpenGL/ include, no include of a gl1/ path or of a .DS_Store, no
#     symbolic link (which could hide either), and no file other than .c,
#     .h, .m and .metal (a .mm, .cpp or .inc file fails the check rather
#     than going unscanned; Finder's .DS_Store is passed over). It runs on
#     scratch mutants first, each text mutant once per scanned kind of
#     file, and must fail each of them naming its cause, then on the tree,
#     whose summary line names the causes found. A grep that errors fails
#     the check.
# (d) Always: the 2D flush points, by source inspection. gl1 draws its
#     buffered 2D quads (R_ApplyGLBuffer) at the start of R_RenderFrame,
#     in RI_EndFrame and before R_ScreenShot reads the frame; ref_metal's
#     RM_Flush2D must be called, as a statement outside comments, in
#     RI_RenderFrame before rmb_pass_3d (rm_main.c), in RI_EndFrame before
#     rmb_frame_end (rm_main.c) and in R_ScreenShot before rmb_read
#     (rm_misc.c). The pins see a missing flush on a few frames only
#     (design section 18) and the host test cannot see this glue. A
#     function that is not found fails the check. Scratch mutants of each
#     point (the flush removed, commented out, moved after the call; the
#     function renamed) must fail it.
# (e) Always: the kernels call the helpers their host test drives, by source
#     inspection (TASK164). tests/metal/rm_rtwater_test.c drives C copies of
#     the reflection loops (rtw_trace_ss, rtw_trace_ws) through the shared
#     helpers of common/mtl_rtw_math.h; a kernel that stopped calling a helper
#     would leave the test green. In rm_rtwater.metal, after comments and
#     string literals are stripped, the body of rm_rtw_trace (its name at the
#     start of a line up to the next "}" in column 0) must call
#     mtl_rtw_ss_next and mtl_rtw_choose, and the body of rm_rtw_trace_ws must
#     call mtl_rtw_ws_next and mtl_rtw_choose_ws; the check fails naming the
#     kernel and the helper. Scratch mutants (the call renamed, commented out,
#     moved out of the kernel into another function; the kernel renamed) must
#     fail it. The pairs are the water's only: the shadow and AO host tests
#     call the helpers one by one and carry no copy of a kernel loop.
# (f) Only on Darwin with a Metal device (TASK164): tests/metal/rmb_cs_probe.m
#     includes rmb_device.m and runs the renderer's own rmb_attach,
#     rmb_colorspace_set (rmb_cs_apply, rmb_cs_release) and rmb_detach on a
#     standalone CAMetalLayer, no window, no game data: after metal_colorspace
#     0 -> 1 -> 0, and after 1 -> detach, the layer's colour space is the one
#     it had at attach, and the ledger holds no state object. With no device
#     the probe prints one skip line and passes. Scratch mutants of
#     rmb_device.m (1 -> 0 sets NULL; 1 -> 0 leaves ours; detach leaves ours)
#     must fail it.
# (g) Always: the call seats of the ray-tracing gate (TASK60), by source
#     inspection, as for the flush points. In rm_main.c, comments and string
#     literals stripped: RI_BeginFrame's first statement after its
#     declarations (and a "(void)<parameter>;") is RM_RTG_Frame(); RI_Init
#     calls RM_RT_Init(&rtcaps); and after it RM_RTG_Init();, both before
#     ri.Vid_MenuInit(); and no *_Init( of the file takes the raw &caps but
#     RM_FX_Init, which reads only the families. RM_RTG_Init calls
#     RM_RTG_Frame too, so a dropped frame call still builds, and &caps for
#     &rtcaps would build as well. Scratch mutants (the frame call deleted,
#     moved below another statement; &caps for &rtcaps; RM_RTG_Init deleted,
#     moved after ri.Vid_MenuInit; another *_Init(&caps); either function
#     renamed) must fail it, each naming the rule it breaks.
set -eu
cd "$(dirname "$0")/.."

fail=0
bad() { echo "check-ref-metal: $*"; fail=1; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/ref-metal.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT

cc=${CC:-cc}
rm_dir=src/client/refresh/metal
flags="-std=gnu99 -Wall -Wextra -Werror -g -O1 -fno-strict-aliasing -fwrapv"
san="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"

# (a) the host test, with sanitizers when this compiler can build and run with them
printf 'int main(void){return 0;}\n' > "${tmp:?}/probe.c"
if $cc $san -o "${tmp:?}/probe" "${tmp:?}/probe.c" >/dev/null 2>&1 && "${tmp:?}/probe" >/dev/null 2>&1; then
	flags="$flags $san"
	psan=$san
else
	echo "check-ref-metal: $cc cannot build with ASAN and UBSAN; running without them"
fi

host=""
# the world's tests (N4) also run what ref_metal takes from files/surf.c (with shared.c, which it
# calls), hold the model checks to files/models.c (N7), and compare the 3D viewport with the shim's
# mgl_compute_raster (gl1/metal/mgl_raster.c, behind tests/metal/rm_host_shim.c) and the point arithmetic
# of the particles (mgl_point_size and mgl_point_quads of mgl_draw.c, with mgl_state.c and mgl_tex.c); the engine headers
# need the two names the Makefile defines. files/models.c is the shared loader, not ref_metal's code:
# its one -Wextra finding (a sign comparison) is not this module's
# shellcheck disable=SC2086 # flags is a word list
if ! $cc $flags -Wno-sign-compare '-DYQ2OSTYPE="host"' '-DYQ2ARCH="host"' -c -o "${tmp:?}/models.o" \
	src/client/refresh/files/models.c > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	bad "src/client/refresh/files/models.c does not compile for the host test"
# shellcheck disable=SC2086 # flags is a word list
elif ! $cc $flags '-DYQ2OSTYPE="host"' '-DYQ2ARCH="host"' -o "${tmp:?}/rm_host_test" \
	tests/metal/rm_host_test.c tests/metal/rm_host_shim.c \
	"$rm_dir/rm_pure.c" "$rm_dir/common/mtl_matrix.c" "$rm_dir/common/mtl_ring.c" \
	src/client/refresh/gl1/metal/mgl_raster.c src/client/refresh/gl1/metal/mgl_state.c \
	src/client/refresh/gl1/metal/mgl_tex.c src/client/refresh/gl1/metal/mgl_draw.c src/client/refresh/files/surf.c "${tmp:?}/models.o" \
	src/common/shared/shared.c -lm > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	bad "tests/metal/rm_host_test.c does not compile"
elif ! UBSAN_OPTIONS=print_stacktrace=1 "${tmp:?}/rm_host_test" > "${tmp:?}/out" 2>&1; then
	cat "${tmp:?}/out"
	bad "tests/metal/rm_host_test.c failed"
else
	host=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_host_test: //')
	case $host in *", 0 failed") ;; *) bad "tests/metal/rm_host_test.c printed no result line"; host="";; esac
fi

# the pure part of the ray tracing (RT1, TASK53): rm_rt.c with rm_pure.c, whose entity matrix it reuses
rt=""
# shellcheck disable=SC2086 # flags is a word list
if ! $cc $flags '-DYQ2OSTYPE="host"' '-DYQ2ARCH="host"' -o "${tmp:?}/rm_rt_test" tests/metal/rm_rt_test.c \
	"$rm_dir/rm_rt.c" "$rm_dir/rm_pure.c" "$rm_dir/common/mtl_matrix.c" "$rm_dir/common/mtl_ring.c" \
	src/common/shared/shared.c -lm > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	bad "tests/metal/rm_rt_test.c does not compile"
elif ! UBSAN_OPTIONS=print_stacktrace=1 "${tmp:?}/rm_rt_test" > "${tmp:?}/out" 2>&1; then
	cat "${tmp:?}/out"
	bad "tests/metal/rm_rt_test.c failed"
else
	rt=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_rt_test: //')
	case $rt in *", 0 failed") ;; *) bad "tests/metal/rm_rt_test.c printed no result line"; rt="";; esac
fi

# the pure part of the G-buffer (RT2, TASK54): rm_gbuf.c and common/mtl_gbuf_math.h, with rm_pure.c for the alias stream
gb=""
# shellcheck disable=SC2086 # flags is a word list
if ! $cc $flags '-DYQ2OSTYPE="host"' '-DYQ2ARCH="host"' -o "${tmp:?}/rm_gbuf_test" tests/metal/rm_gbuf_test.c \
	"$rm_dir/rm_gbuf.c" "$rm_dir/rm_pure.c" "$rm_dir/common/mtl_matrix.c" "$rm_dir/common/mtl_ring.c" \
	src/common/shared/shared.c -lm > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	bad "tests/metal/rm_gbuf_test.c does not compile"
elif ! UBSAN_OPTIONS=print_stacktrace=1 "${tmp:?}/rm_gbuf_test" > "${tmp:?}/out" 2>&1; then
	cat "${tmp:?}/out"
	bad "tests/metal/rm_gbuf_test.c failed"
else
	gb=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_gbuf_test: //')
	case $gb in *", 0 failed") ;; *) bad "tests/metal/rm_gbuf_test.c printed no result line"; gb="";; esac
fi

# the pure part of the dynamic-light shadows (RT3, TASK55): rm_rtshadow.c and common/mtl_rts_math.h,
# held to gl1's falloff and lightmap store (rm_pure.c), with rm_rt.c for the material ids
rts=""
# shellcheck disable=SC2086 # flags is a word list
if ! $cc $flags '-DYQ2OSTYPE="host"' '-DYQ2ARCH="host"' -o "${tmp:?}/rm_rtshadow_test" tests/metal/rm_rtshadow_test.c \
	"$rm_dir/rm_rtshadow.c" "$rm_dir/rm_rt.c" "$rm_dir/rm_pure.c" "$rm_dir/common/mtl_matrix.c" \
	"$rm_dir/common/mtl_ring.c" src/common/shared/shared.c -lm > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	bad "tests/metal/rm_rtshadow_test.c does not compile"
elif ! UBSAN_OPTIONS=print_stacktrace=1 "${tmp:?}/rm_rtshadow_test" > "${tmp:?}/out" 2>&1; then
	cat "${tmp:?}/out"
	bad "tests/metal/rm_rtshadow_test.c failed"
else
	rts=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_rtshadow_test: //')
	case $rts in *", 0 failed") ;; *) bad "tests/metal/rm_rtshadow_test.c printed no result line"; rts="";; esac
fi

# the pure part of the ambient occlusion (RT5, TASK57): rm_rtao.c and common/mtl_rtao_math.h, with
# rm_rtshadow.c for the view's inverse, rm_rt.c for the instances AO rays test and rm_pure.c
ao=""
# shellcheck disable=SC2086 # flags is a word list
if ! $cc $flags '-DYQ2OSTYPE="host"' '-DYQ2ARCH="host"' -o "${tmp:?}/rm_rtao_test" tests/metal/rm_rtao_test.c \
	"$rm_dir/rm_rtao.c" "$rm_dir/rm_rtshadow.c" "$rm_dir/rm_rt.c" "$rm_dir/rm_pure.c" "$rm_dir/common/mtl_matrix.c" \
	"$rm_dir/common/mtl_ring.c" src/common/shared/shared.c -lm > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	bad "tests/metal/rm_rtao_test.c does not compile"
elif ! UBSAN_OPTIONS=print_stacktrace=1 "${tmp:?}/rm_rtao_test" > "${tmp:?}/out" 2>&1; then
	cat "${tmp:?}/out"
	bad "tests/metal/rm_rtao_test.c failed"
else
	ao=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_rtao_test: //')
	case $ao in *", 0 failed") ;; *) bad "tests/metal/rm_rtao_test.c printed no result line"; ao="";; esac
fi

# the pure part of the water reflections (RT6, TASK58): rm_rtwater.c and common/mtl_rtw_math.h, with
# rm_rtshadow.c for the view's inverse, rm_rt.c for the instances the rays test and rm_pure.c for gl1's warp
rtw=""
# shellcheck disable=SC2086 # flags is a word list
if ! $cc $flags '-DYQ2OSTYPE="host"' '-DYQ2ARCH="host"' -o "${tmp:?}/rm_rtwater_test" tests/metal/rm_rtwater_test.c \
	"$rm_dir/rm_rtwater.c" "$rm_dir/rm_rtshadow.c" "$rm_dir/rm_rt.c" "$rm_dir/rm_pure.c" "$rm_dir/common/mtl_matrix.c" \
	"$rm_dir/common/mtl_ring.c" src/common/shared/shared.c -lm > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	bad "tests/metal/rm_rtwater_test.c does not compile"
elif ! UBSAN_OPTIONS=print_stacktrace=1 "${tmp:?}/rm_rtwater_test" > "${tmp:?}/out" 2>&1; then
	cat "${tmp:?}/out"
	bad "tests/metal/rm_rtwater_test.c failed"
else
	rtw=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_rtwater_test: //')
	case $rtw in *", 0 failed") ;; *) bad "tests/metal/rm_rtwater_test.c printed no result line"; rtw="";; esac
fi

# the pure part of the sun and sky shadow (RT4, TASK56): rm_rtsun.c and common/mtl_rtsun_math.h, with
# rm_rtshadow.c for the view's inverse, rm_rt.c for the material ids and rm_pure.c; it reads the
# committed table scripts/rt/sun-angles.txt from this directory
rtsun=""
# shellcheck disable=SC2086 # flags is a word list
if ! $cc $flags '-DYQ2OSTYPE="host"' '-DYQ2ARCH="host"' -o "${tmp:?}/rm_rtsun_test" tests/metal/rm_rtsun_test.c \
	"$rm_dir/rm_rtsun.c" "$rm_dir/rm_rtshadow.c" "$rm_dir/rm_rt.c" "$rm_dir/rm_pure.c" "$rm_dir/common/mtl_matrix.c" \
	"$rm_dir/common/mtl_ring.c" src/common/shared/shared.c -lm > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	bad "tests/metal/rm_rtsun_test.c does not compile"
elif ! UBSAN_OPTIONS=print_stacktrace=1 "${tmp:?}/rm_rtsun_test" > "${tmp:?}/out" 2>&1; then
	cat "${tmp:?}/out"
	bad "tests/metal/rm_rtsun_test.c failed"
else
	rtsun=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_rtsun_test: //')
	case $rtsun in *", 0 failed") ;; *) bad "tests/metal/rm_rtsun_test.c printed no result line"; rtsun="";; esac
fi

# the pure part of the MetalFX scalers (TASK111): rm_metalfx.c alone
fx=""
# shellcheck disable=SC2086 # flags is a word list
if ! $cc $flags '-DYQ2OSTYPE="host"' '-DYQ2ARCH="host"' -o "${tmp:?}/rm_fx_test" tests/metal/rm_fx_test.c \
	"$rm_dir/rm_metalfx.c" -lm > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	bad "tests/metal/rm_fx_test.c does not compile"
elif ! UBSAN_OPTIONS=print_stacktrace=1 "${tmp:?}/rm_fx_test" > "${tmp:?}/out" 2>&1; then
	cat "${tmp:?}/out"
	bad "tests/metal/rm_fx_test.c failed"
else
	fx=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_fx_test: //')
	case $fx in *", 0 failed") ;; *) bad "tests/metal/rm_fx_test.c printed no result line"; fx="";; esac
fi

# the hardware gate of ray tracing (RT8, TASK60): rm_rtgate.c alone
rtg=""
# shellcheck disable=SC2086 # flags is a word list
if ! $cc $flags -o "${tmp:?}/rm_rtgate_test" tests/metal/rm_rtgate_test.c "$rm_dir/rm_rtgate.c" -lm \
	> "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	bad "tests/metal/rm_rtgate_test.c does not compile"
elif ! UBSAN_OPTIONS=print_stacktrace=1 "${tmp:?}/rm_rtgate_test" > "${tmp:?}/out" 2>&1; then
	cat "${tmp:?}/out"
	bad "tests/metal/rm_rtgate_test.c failed"
else
	rtg=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_rtgate_test: //')
	case $rtg in *", 0 failed") ;; *) bad "tests/metal/rm_rtgate_test.c printed no result line"; rtg="";; esac
fi

# rtg_names <dir>: name=default for each Cvar_Get("r_rt_ of the directory's .c and .m files, the
# files joined into one line so a call over several lines is seen; a default that is not a
# literal gives the name alone. Sorted, each pair once.
rtg_names() {
	cat "$1"/*.c "$1"/*.m | tr '\n\r' '  ' |
		grep -oE 'Cvar_Get[[:space:]]*\([[:space:]]*"r_rt_[A-Za-z0-9_]*"([[:space:]]*,[[:space:]]*"[^"]*")?' |
		sed -E 's/^Cvar_Get[[:space:]]*\([[:space:]]*"([^"]*)"([[:space:]]*,[[:space:]]*"([^"]*)")?$/\1=\3/; s/=$//; s/ /_/g' |
		sort -u
}
# rtg_table <dir>: 0 when the gate's table classifies every r_rt_* cvar of the directory
rtg_table() {
	names=$(rtg_names "$1") || return 1
	[ -n "$names" ] || { echo "  no Cvar_Get(\"r_rt_ in $1"; return 1; }
	# shellcheck disable=SC2086 # names is a word list of name=default pairs
	"${tmp:?}/rm_rtgate_test" --names $names
}
rtg_mutants=0
if [ -x "${tmp:?}/rm_rtgate_test" ]; then
	if ! rtg_table "$rm_dir" > "${tmp:?}/rtg.out" 2>&1; then
		cat "${tmp:?}/rtg.out"
		bad "an r_rt_* cvar of $rm_dir is not in rm_rtgate.c's table, or a row is stale (see above)"
	fi
	rtg_cvars=$(tail -n 1 "${tmp:?}/rtg.out" | sed 's/^rm_rtgate_test: //; s/ names,.*//')
	mkdir "${tmp:?}/rtgmut"
	rtg_mut=1
	while [ "$rtg_mut" -le 4 ]; do
		rm -f "${tmp:?}"/rtgmut/*
		cp "$rm_dir"/*.c "$rm_dir"/*.m "${tmp:?}/rtgmut/"
		case $rtg_mut in
			1) printf '%s\n' 'static void f(void) { ri.Cvar_Get("r_rt_mutant", "0", CVAR_ARCHIVE); }' > "${tmp:?}/rtgmut/zz_mutant.c" ;;
			2) printf '%s\n' 'static void f(void) { ri.Cvar_Get(' '	"r_rt_mutant", "0", 0); }' > "${tmp:?}/rtgmut/zz_mutant.c" ;;
			3) sed 's/Cvar_Get("r_rt_water_show"/Cvar_Get("x_rt_water_show"/' "$rm_dir/rm_rtwaterscene.c" \
				> "${tmp:?}/rtgmut/rm_rtwaterscene.c" ;;
			4) sed 's/Cvar_Get("r_rt_ao", "0"/Cvar_Get("r_rt_ao", "1"/' "$rm_dir/rm_rtaoscene.c" > "${tmp:?}/rtgmut/rm_rtaoscene.c" ;;
		esac
		if cmp -s "$rm_dir/rm_rtwaterscene.c" "${tmp:?}/rtgmut/rm_rtwaterscene.c" &&
			cmp -s "$rm_dir/rm_rtaoscene.c" "${tmp:?}/rtgmut/rm_rtaoscene.c" && [ ! -f "${tmp:?}/rtgmut/zz_mutant.c" ]; then
			bad "the cvar-table mutant $rtg_mut changed nothing"
		elif rtg_table "${tmp:?}/rtgmut" > "${tmp:?}/rtgmut.out" 2>&1; then
			cat "${tmp:?}/rtgmut.out"
			bad "the cvar-table check passes mutant $rtg_mut"
		else
			rtg_mutants=$((rtg_mutants + 1))
		fi
		rtg_mut=$((rtg_mut + 1))
	done
fi

# strip <file>: the file with its comments and the contents of its string and
# character literals blanked, its line numbers kept (for the code-only patterns)
strip() {
	awk '
	BEGIN { st = 0; q = sprintf("%c", 39); bs = sprintf("%c", 92) }   # st: 0 code, 1 block comment, 2 string, 3 char
	{
		out = ""; n = length($0); i = 1
		while (i <= n) {
			c = substr($0, i, 1); d = substr($0, i, 2)
			if (st == 1) {
				if (d == "*/") { st = 0; out = out "  "; i += 2 } else { out = out " "; i++ }
				continue
			}
			if (st >= 2) {
				if (c == bs) { out = out "  "; i += 2; continue }
				if ((st == 2 && c == "\"") || (st == 3 && c == q)) { st = 0; out = out c } else { out = out " " }
				i++; continue
			}
			if (d == "/*") { st = 1; out = out "  "; i += 2; continue }
			if (d == "//") { break }
			if (c == "\"") { st = 2 } else if (c == q) { st = 3 }
			out = out c; i++
		}
		# a literal ends with its line, unless the line ends in a backslash (spliced
		# to the next one before the literal is read); clang splices a backslash
		# followed by blanks or a CR before the newline as well
		t = $0; sub(/[ \t\r]+$/, "", t)
		if (st >= 2 && substr(t, length(t), 1) != bs) { st = 0 }
		print out
	}' "$1"
}

# (c) the vocabulary check of one directory: prints what it found, 1 on a finding or a
# tool error. A gl[A-Z] call and a GL_ identifier are looked for in code only, outside
# comments and string literals (N2 copies gl1's gl_texturemode value strings, "GL_NEAREST"
# and the rest, as literals); the two includes are looked for in the raw text.
vgrep() { # <pattern> <file to search> <file to name>
	rc=0
	grep -nE "$1" "$2" > "${tmp:?}/vocab.out" 2>&1 || rc=$?
	case $rc in
		0) sed "s|^|  $3:|" "${tmp:?}/vocab.out"; return 2 ;;
		1) return 0 ;;
		*) echo "  grep failed ($rc) on $3"; return 1 ;;
	esac
}
# The causes found go into $causes (the summary line names them): vocab runs in this
# shell, its output redirected, not in a subshell.
cause() {
	case " $causes " in *" $1 "*) ;; *) causes="${causes:+$causes }$1" ;; esac
	found=1
}
vocab() {
	d=$1 found=0 causes=""
	if [ -L "$d" ] || [ ! -d "$d" ]; then
		echo "  $d is not a directory (or is a symbolic link)"
		return 1
	fi
	links=$(find "$d" -type l -print) || { echo "  find failed on $d"; return 1; }
	if [ -n "$links" ]; then
		printf '  symbolic link: %s\n' $links
		cause symlink
	fi
	files=$(find "$d" -type f -print) || { echo "  find failed on $d"; return 1; }
	for f in $files; do
		# the scanned kinds; any other file (.mm, .cpp, .inc, ...) fails the check, but
		# Finder's .DS_Store, which nothing compiles (an include of it is refused below)
		case $f in
			*.c|*.h|*.m|*.metal) ;;
			*/.DS_Store) continue ;;
			*) echo "  $f: not a scanned kind of file (.c .h .m .metal)"; cause unscanned; continue ;;
		esac
		strip "$f" > "${tmp:?}/vocab.code" || { echo "  awk failed on $f"; return 1; }
		for pat in '(^|[^A-Za-z0-9_])gl[A-Z][A-Za-z0-9_]*[[:space:]]*\(' '(^|[^A-Za-z0-9_])GL_[A-Za-z0-9_]'; do
			r=0; vgrep "$pat" "${tmp:?}/vocab.code" "$f" || r=$?
			case $r in 0) ;; 2) cause opengl ;; *) return 1 ;; esac
		done
		for pat in '#[[:space:]]*(include|import)[[:space:]]*<OpenGL/' \
			'#[[:space:]]*(include|import)[[:space:]]*[<"]([^>"]*/)?gl1/' \
			'#[[:space:]]*(include|import)[[:space:]]*[<"]([^>"]*/)?[.]DS_Store[>"]'; do
			r=0; vgrep "$pat" "$f" "$f" || r=$?
			case $r in 0) ;; 2) cause opengl ;; *) return 1 ;; esac
		done
	done
	return $found
}

# the mutants: each must fail the check; the clean file must pass it. Clean: GL_ and gl
# calls inside comments and string literals, lower-case gl_ in code.
mkdir "${tmp:?}/mut"
printf '%s\n' '/* glClear(GL_COLOR_BUFFER_BIT) in a comment' '   over two lines GL_ONE */' \
	'int rm_glossary, gl_texturemode;' \
	'static const char *modes[] = { "GL_NEAREST", "GL_LINEAR_MIPMAP_LINEAR", "a \" GL_X" };' \
	"char q = '\"'; // GL_RGBA glFinish() after a line comment" 'const char *s = "glClear(";' \
	"char e = '\\\\'; const char *t = \"GL_ZERO\";" > "${tmp:?}/mut/clean.c"
for e in h m metal; do
	cp "${tmp:?}/mut/clean.c" "${tmp:?}/mut/clean.$e"
done
vocab "${tmp:?}/mut" > "${tmp:?}/mut.out" || { cat "${tmp:?}/mut.out"; bad "the vocabulary check fails a clean file"; }
# Each text mutant is written once per scanned kind of file (@NL@ is a line break):
# a literal continued over a line by a backslash, and one left open at the end of its
# line, are followed by code. Then a symbolic link and a file of an unscanned kind.
mutants=0
mutant() { # <label> [<cause>]: the check must fail the directory mut/, naming <cause>
	if vocab "${tmp:?}/mut" > "${tmp:?}/mut.out"; then
		bad "the vocabulary check passes a mutant ($1)"
	elif [ "$causes" != "${2:-opengl}" ]; then
		bad "the vocabulary check names \"$causes\", not \"${2:-opengl}\", for a mutant ($1)"
	else
		mutants=$((mutants + 1))
	fi
}
fresh() {
	rm -rf "${tmp:?}/mut"
	mkdir "${tmp:?}/mut"
	cp "${tmp:?}/probe.c" "${tmp:?}/mut/ok.c"
}
for m in 'glClear(0);' 'x = glGetError ();' 'int y = GL_RGBA;' 'f("GL_NEAREST", GL_LINEAR);' \
	'/* a comment */ int z = GL_ONE; /* ends */' 'char *s = "a\"b"; int w = GL_ZERO;' \
	"char c = '\"'; int v = GL_ONE;" \
	'static const char *rm_s = "abc\@NL@def"; static int rm_x = GL_RGBA;' \
	'static const char *rm_s = "abc\ @NL@def"; static int rm_x = GL_RGBA;' \
	'static const char *rm_s = "abc\@CR@@NL@def"; static int rm_x = GL_RGBA;@CR@' \
	'const char *rm_u = "abc@NL@int rm_v = GL_ONE;' \
	'#include <OpenGL/gl.h>' '#import <OpenGL/gl3.h>' '#include "../gl1/header/local.h"' \
	'#include <gl1/gl1.h>' '#import "gl1/x.h"' '#include ".DS_Store"'; do
	for e in c h m metal; do
		fresh
		printf '%s\n' "$m" | awk '{ gsub(/@CR@/, "\r"); gsub(/@NL@/, "\n"); print }' > "${tmp:?}/mut/m.$e"
		mutant "$m in m.$e"
	done
done
fresh
ln -s "$PWD/src/client/refresh/gl1/gl1_main.c" "${tmp:?}/mut/hidden.c"
mutant "a symbolic link" symlink
for e in mm cpp inc; do
	fresh
	printf '%s\n' '#include <OpenGL/gl.h>' > "${tmp:?}/mut/m.$e"
	mutant "an unscanned m.$e" unscanned
done
# Finder's .DS_Store is not judged: beside a clean file it passes
fresh
printf 'Bud1 GL_RGBA glClear(\n' > "${tmp:?}/mut/.DS_Store"
vocab "${tmp:?}/mut" > "${tmp:?}/mut.out" || { cat "${tmp:?}/mut.out"; bad "the vocabulary check fails a .DS_Store"; }
# a tool error fails it too: a missing directory, and a grep that exits 2
if vocab "${tmp:?}/no-such-dir" > "${tmp:?}/mut.out"; then
	bad "the vocabulary check passes a missing directory"
fi
fresh
mkdir "${tmp:?}/stub"
printf '#!/bin/sh\nexit 2\n' > "${tmp:?}/stub/grep"
chmod +x "${tmp:?}/stub/grep"
if (PATH="${tmp:?}/stub:$PATH"; vocab "${tmp:?}/mut" > "${tmp:?}/mut.out"); then
	bad "the vocabulary check passes when grep fails"
fi

if ! vocab "$rm_dir" > "${tmp:?}/vocab.tree"; then
	cat "${tmp:?}/vocab.tree"
	bad "$rm_dir fails the vocabulary check (${causes:-a tool error}; opengl: OpenGL vocabulary, symlink: a symbolic link, unscanned: a file of another kind)"
fi

# (d) the flush points. flushed <file> <function> <call>: 0 when the function is
# defined in the file (its name at the start of a line, its body up to the next
# "}" in column 0) and calls RM_Flush2D(); as a statement, outside comments and
# string literals, before the first <call> in that body; 1 otherwise.
flushed() {
	strip "$1" > "${tmp:?}/flush.code" || return 1
	awk -v fn="$2" -v call="$3" '
		$0 ~ "^" fn "\\(" { body = 1; found = 1; next }
		body && /^}/ { body = 0 }
		body && !fl && $0 ~ /^[ \t]*RM_Flush2D\(\);/ { fl = NR }
		body && !at && index($0, call) { at = NR }
		END { exit !found ? 2 : (fl && at && fl < at) ? 0 : 1 }' "${tmp:?}/flush.code"
}
# flush_mutant <kind> <file> <function> <call>: the file with that flush point broken
flush_mutant() {
	case $1 in
		removed) awk -v fn="$3" '$0 ~ "^" fn "\\(" { b = 1 } b && /^}/ { b = 0 }
			b && /^[ \t]*RM_Flush2D\(\);/ { next } { print }' "$2" ;;
		commented) awk -v fn="$3" '$0 ~ "^" fn "\\(" { b = 1 } b && /^}/ { b = 0 }
			b && /^[ \t]*RM_Flush2D\(\);/ { sub(/RM_Flush2D\(\);/, "/* RM_Flush2D(); */") } { print }' "$2" ;;
		moved) awk -v fn="$3" -v call="$4" '$0 ~ "^" fn "\\(" { b = 1 } b && /^}/ { b = 0 }
			b && held == "" && !done && /^[ \t]*RM_Flush2D\(\);/ { held = $0; done = 1; next }
			{ print } b && held != "" && index($0, call) { print held; held = "" }' "$2" ;;
		renamed) sed "s/^$3(/$3_old(/" "$2" ;;
	esac
}
flush_mutants=0
for point in "rm_main.c RI_RenderFrame rmb_pass_3d(" "rm_main.c RI_EndFrame rmb_frame_end(" \
	"rm_misc.c R_ScreenShot rmb_read("; do
	# shellcheck disable=SC2086 # three words: file, function, call
	set -- $point
	f=$rm_dir/$1 fn=$2 call=$3
	rc=0
	flushed "$f" "$fn" "$call" || rc=$?
	if [ "$rc" = 2 ]; then
		bad "function $fn not found in $f"
		continue
	elif [ "$rc" != 0 ]; then
		bad "$f: $fn does not call RM_Flush2D() before $call (gl1's R_ApplyGLBuffer)"
		continue
	fi
	for m in removed commented moved renamed; do
		flush_mutant "$m" "$f" "$fn" "$call" > "${tmp:?}/flush-mutant.c"
		# the renamed function is "not found" (2), the others "no flush before the call" (1)
		case $m in renamed) want=2 ;; *) want=1 ;; esac
		rc=0
		flushed "${tmp:?}/flush-mutant.c" "$fn" "$call" || rc=$?
		if cmp -s "$f" "${tmp:?}/flush-mutant.c"; then
			bad "the flush-point mutant ($m) of $fn changed nothing"
		elif [ "$rc" = 0 ]; then
			bad "the flush-point check passes a mutant of $fn (the flush $m)"
		elif [ "$rc" != "$want" ]; then
			bad "the flush-point check gives status $rc, not $want, for a mutant of $fn ($m)"
		else
			flush_mutants=$((flush_mutants + 1))
		fi
	done
done

# (g) the ray-tracing gate's call seats in rm_main.c (TASK60). seated <file>: after comments and
# string literals are stripped, prints the broken rules, one word each, and returns 1 if any; 2 when
# RI_Init or RI_BeginFrame is not defined in the file (its name at the start of a line, its body up
# to the next "}" in column 0); 0 otherwise. The rules:
#   frame    RI_BeginFrame's first statement, after its declarations and a "(void)<name>;", is
#            RM_RTG_Frame(); (RM_RTG_Init calls it too, so a removal still links)
#   rtinit   RI_Init calls RM_RT_Init(&rtcaps); before ri.Vid_MenuInit(
#   rtginit  RI_Init calls RM_RTG_Init(); after its RM_RT_Init( and before ri.Vid_MenuInit(
#   rtflag   RI_Init sets rtcaps.raytracing = rm_rtg_traces(&rm_rtg); after its rm_rtg_decide( and before
#            RM_RT_Init(&rtcaps); (without it rtcaps is a copy of caps and the scenes skip the gate)
#   rawcaps  no *_Init( of the file takes the raw &caps, other than RM_FX_Init: the MetalFX scalers
#            trace nothing and RM_FX_Init reads only caps.families; every scene that traces takes
#            rtcaps, whose raytracing is the gate's answer
seated() {
	strip "$1" > "${tmp:?}/seat.code" || return 1
	awk '
		{
			line = $0
			while (match(line, /[A-Za-z_][A-Za-z0-9_]*_Init[ \t]*\([ \t]*&[ \t]*caps[ \t]*[,)]/)) {
				name = substr(line, RSTART, RLENGTH); sub(/[ \t]*\(.*/, "", name)
				if (name != "RM_FX_Init") raw = 1
				line = substr(line, RSTART + RLENGTH)
			}
		}
		/^RI_BeginFrame\(/ { bf = 1; bfound = 1; next }
		/^RI_Init\(/ { ri = 1; ifound = 1; next }
		(bf || ri) && /^}/ { bf = 0; ri = 0 }
		bf && !first {
			if ($0 ~ /^[ \t]*$/ || $0 ~ /^[ \t]*[{][ \t]*$/) next
			if ($0 ~ /^[ \t]*\(void\)[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*;[ \t]*$/) next
			if ($0 !~ /^[ \t]*(return|goto|else|case|do)[^A-Za-z0-9_]/ &&
				$0 ~ /^[ \t]*([A-Za-z_][A-Za-z0-9_]*[ \t]+)+[*]*[A-Za-z_][A-Za-z0-9_]*[ \t]*(=|;|,|\[)/) next
			first = 1
			if ($0 ~ /^[ \t]*RM_RTG_Frame\(\);[ \t]*$/) frame = 1
		}
		ri && !dec && /rm_rtg_decide[ \t]*\(/ { dec = NR }
		ri && !asg && /^[ \t]*rtcaps[.]raytracing[ \t]*=[ \t]*rm_rtg_traces[ \t]*\([ \t]*&[ \t]*rm_rtg[ \t]*\)[ \t]*;/ { asg = NR }
		ri && !rt && /^[ \t]*RM_RT_Init\(&rtcaps\);/ { rt = NR }
		ri && !rta && /^[ \t]*RM_RT_Init\(/ { rta = NR }
		ri && rta && !rtg && /^[ \t]*RM_RTG_Init\(\);/ { rtg = NR }
		ri && !menu && index($0, "ri.Vid_MenuInit(") { menu = NR }
		END {
			if (!bfound || !ifound) exit 2
			out = ""
			if (!frame) out = out " frame"
			if (!rt || !menu || rt > menu) out = out " rtinit"
			if (!rtg || !menu || rtg > menu) out = out " rtginit"
			if (!asg || !dec || !rt || asg < dec || asg > rt) out = out " rtflag"
			if (raw) out = out " rawcaps"
			if (out != "") { print substr(out, 2); exit 1 }
			exit 0
		}' "${tmp:?}/seat.code"
}
# seat_mutant <kind> <file>: the file with one seat broken
seat_mutant() {
	case $1 in
		noframe) awk '/^RI_BeginFrame\(/ { b = 1 } b && /^}/ { b = 0 }
			b && /^[ \t]*RM_RTG_Frame\(\);/ { next } { print }' "$2" ;;
		framelate) awk '/^RI_BeginFrame\(/ { b = 1 } b && /^}/ { b = 0 }
			b && held == "" && !done && /^[ \t]*RM_RTG_Frame\(\);/ { held = $0; done = 1; next }
			{ print } b && held != "" && /;[ \t]*$/ { print held; held = "" }' "$2" ;;
		rawrt) sed 's/RM_RT_Init(&rtcaps);/RM_RT_Init(\&caps);/' "$2" ;;
		nortg) awk '/^RI_Init\(/ { b = 1 } b && /^}/ { b = 0 }
			b && /^[ \t]*RM_RTG_Init\(\);/ { next } { print }' "$2" ;;
		rtglate) awk '/^RI_Init\(/ { b = 1 } b && /^}/ { b = 0 }
			b && held == "" && !done && /^[ \t]*RM_RTG_Init\(\);/ { held = $0; done = 1; next }
			{ print } b && held != "" && index($0, "ri.Vid_MenuInit(") { print held; held = "" }' "$2" ;;
		noflag) awk '/^RI_Init\(/ { b = 1 } b && /^}/ { b = 0 }
			b && /^[ \t]*rtcaps[.]raytracing[ \t]*=/ { next } { print }' "$2" ;;
		flaglate) awk '/^RI_Init\(/ { b = 1 } b && /^}/ { b = 0 }
			b && held == "" && !done && /^[ \t]*rtcaps[.]raytracing[ \t]*=/ { held = $0; done = 1; next }
			{ print } b && held != "" && /^[ \t]*RM_RT_Init\(&rtcaps\);/ { print held; held = "" }' "$2" ;;
		flagraw) sed 's/^\([[:space:]]*rtcaps[.]raytracing[[:space:]]*=[[:space:]]*\)rm_rtg_traces([^;]*);/\1caps.raytracing;/' "$2" ;;
		rawother) awk '{ print } /^[ \t]*RM_FX_Init\(&caps\);/ { print "\tRM_SUN_Init(&caps);" }' "$2" ;;
		renamedframe) sed 's/^RI_BeginFrame(/RI_BeginFrame_old(/' "$2" ;;
		renamedinit) sed 's/^RI_Init(/RI_Init_old(/' "$2" ;;
	esac
}
seat_main=$rm_dir/rm_main.c
seat_mutants=0
rc=0
seated "$seat_main" > "${tmp:?}/seat.out" || rc=$?
if [ "$rc" = 2 ]; then
	bad "RI_Init or RI_BeginFrame not found in $seat_main"
elif [ "$rc" != 0 ]; then
	bad "$seat_main: the ray-tracing gate's call seats are broken ($(cat "${tmp:?}/seat.out"); frame: RM_RTG_Frame(); first in RI_BeginFrame, rtinit: RM_RT_Init(&rtcaps); in RI_Init, rtginit: RM_RTG_Init(); after it, rtflag: rtcaps.raytracing = rm_rtg_traces(&rm_rtg); between rm_rtg_decide and RM_RT_Init, rawcaps: an *_Init(&caps) other than RM_FX_Init)"
else
	for m in noframe framelate rawrt nortg rtglate noflag flaglate flagraw rawother renamedframe renamedinit; do
		# a renamed function is "not found" (2), the others name the rules they break (1)
		case $m in
			noframe|framelate) want=frame ;;
			rawrt) want="rtinit rtflag rawcaps" ;;
			nortg|rtglate) want=rtginit ;;
			noflag|flaglate|flagraw) want=rtflag ;;
			rawother) want=rawcaps ;;
			*) want="" ;;
		esac
		seat_mutant "$m" "$seat_main" > "${tmp:?}/seat-mutant.c"
		rc=0
		seated "${tmp:?}/seat-mutant.c" > "${tmp:?}/seat.out" || rc=$?
		got=$(cat "${tmp:?}/seat.out")
		if cmp -s "$seat_main" "${tmp:?}/seat-mutant.c"; then
			bad "the seat mutant ($m) of $seat_main changed nothing"
		elif [ "$rc" = 0 ]; then
			bad "the seat check passes a mutant of $seat_main ($m)"
		elif [ -z "$want" ] && [ "$rc" != 2 ]; then
			bad "the seat check gives status $rc, not 2, for a mutant of $seat_main ($m)"
		elif [ -n "$want" ] && { [ "$rc" != 1 ] || [ "$got" != "$want" ]; }; then
			bad "the seat check names \"$got\" (status $rc), not \"$want\", for a mutant of $seat_main ($m)"
		else
			seat_mutants=$((seat_mutants + 1))
		fi
	done
fi

# (e) the kernels call the helpers their host tests drive. kcalls <file> <kernel> <helper>: 0 when
# the kernel is defined in the file (its name at the start of a line, its body up to the next "}" in
# column 0) and its body, outside comments and string literals, calls <helper>( ; 1 otherwise.
kcalls() {
	strip "$1" | deadif > "${tmp:?}/kcalls.code" || return 1
	awk -v fn="$2" -v h="$3" '
		$0 ~ "^" fn "\\(" { body = 1; found = 1 }
		body && $0 ~ "(^|[^A-Za-z0-9_])" h "[ \t]*\\(" { hit = 1 }
		body && /^}/ { body = 0 }
		END { exit !found ? 2 : hit ? 0 : 1 }' "${tmp:?}/kcalls.code"
}
# deadif: stdin to stdout, the lines of dead preprocessor branches blanked (so a call there is no
# call): the part of an "#if 0" before its "#else" (the "#else" part is live), the "#else" part of an
# "#if 1", whatever the nesting; directive lines are blanked too, the line count is kept. The forms
# evaluated are "#if 0", "#if 1", "#if (0)" and "#if (1)" (blanks optional, so "#if(0)" too); every
# other condition ("#if X", "#if defined(X)", "#ifdef", "#ifndef", "#elif", "#if 00", "#if false")
# is taken as live on both branches, so a call kept there counts (the check cannot tell it is dead),
# and a live call is never refused for it. Every "#if" form, "#if(" included, opens a level, so each
# "#endif" closes its own.
deadif() {
	awk '
	function dead(   i) { for (i = 1; i <= depth; i++) if (off[i]) return 1; return 0 }
	{
		line = $0
		if (line ~ /^[ \t]*#[ \t]*if([^A-Za-z0-9_]|$)|^[ \t]*#[ \t]*if(n?def)[ \t]/) {
			depth++; kind[depth] = 2; off[depth] = 0
			if (line ~ /^[ \t]*#[ \t]*if([ \t]+0|[ \t]*\([ \t]*0[ \t]*\))[ \t]*$/) { kind[depth] = 0; off[depth] = 1 }
			else if (line ~ /^[ \t]*#[ \t]*if([ \t]+1|[ \t]*\([ \t]*1[ \t]*\))[ \t]*$/) { kind[depth] = 1 }
			print ""; next
		}
		if (line ~ /^[ \t]*#[ \t]*else/) {
			if (depth > 0) { if (kind[depth] == 0) off[depth] = 0; else if (kind[depth] == 1) off[depth] = 1 }
			print ""; next
		}
		if (line ~ /^[ \t]*#[ \t]*elif/) { if (depth > 0) off[depth] = 0; print ""; next }
		if (line ~ /^[ \t]*#[ \t]*endif/) { if (depth > 0) depth--; print ""; next }
		if (dead()) { print ""; next }
		print line
	}'
}
# kcall_mutant <kind> <file> <kernel> <helper>: the file with that call broken
kcall_mutant() {
	case $1 in
		renamed) awk -v fn="$3" -v h="$4" '$0 ~ "^" fn "\\(" { b = 1 } b && /^}/ { b = 0 }
			b { gsub(h "[ \t]*\\(", h "_x(") } { print }' "$2" ;;
		commented) awk -v fn="$3" -v h="$4" '$0 ~ "^" fn "\\(" { b = 1 } b && /^}/ { b = 0 }
			b { gsub(h "[ \t]*\\(", "/* & */ 0 * (") } { print }' "$2" ;;
		elsewhere) awk -v fn="$3" -v h="$4" '$0 ~ "^" fn "\\(" { b = 1 } b && /^}/ { b = 0 }
			b { gsub(h "[ \t]*\\(", h "_x(") } { print }
			END { print "static int kcall_elsewhere(void) { return " h "(0); }" }' "$2" ;;
		renamedkernel) sed "s/^$3(/$3_old(/" "$2" ;;
		deadcode) awk -v fn="$3" -v h="$4" '$0 ~ "^" fn "\\(" { b = 1 } b && /^}/ { b = 0 }
			b && $0 ~ "(^|[^A-Za-z0-9_])" h "[ \t]*\\(" { print "#if 0"; print; print "#else"; print "#endif"; next } { print }' "$2" ;;
		liveelse) awk -v fn="$3" -v h="$4" '$0 ~ "^" fn "\\(" { b = 1 } b && /^}/ { b = 0 }
			b && $0 ~ "(^|[^A-Za-z0-9_])" h "[ \t]*\\(" { print "#if 0"; print; print "#else"; print; print "#endif"; next } { print }' "$2" ;;
		deadnested) awk -v fn="$3" -v h="$4" '$0 ~ "^" fn "\\(" { b = 1 } b && /^}/ { b = 0 }
			b && $0 ~ "(^|[^A-Za-z0-9_])" h "[ \t]*\\(" { print "#if 0"; print "#ifdef X"; print "#endif"; print; print "#endif"; next } { print }' "$2" ;;
		deadnestedparen) awk -v fn="$3" -v h="$4" '$0 ~ "^" fn "\\(" { b = 1 } b && /^}/ { b = 0 }
			b && $0 ~ "(^|[^A-Za-z0-9_])" h "[ \t]*\\(" { print "#if 0"; print "#if(X)"; print "#endif"; print; print "#endif"; next } { print }' "$2" ;;
		deadparen) awk -v fn="$3" -v h="$4" '$0 ~ "^" fn "\\(" { b = 1 } b && /^}/ { b = 0 }
			b && $0 ~ "(^|[^A-Za-z0-9_])" h "[ \t]*\\(" { print "#if (0)"; print; print "#else"; print "#endif"; next } { print }' "$2" ;;
		deadparen2) awk -v fn="$3" -v h="$4" '$0 ~ "^" fn "\\(" { b = 1 } b && /^}/ { b = 0 }
			b && $0 ~ "(^|[^A-Za-z0-9_])" h "[ \t]*\\(" { print "#if(1)"; print "#else"; print; print "#endif"; next } { print }' "$2" ;;
		joined) awk -v fn="$3" '
			held != "" { if (index($0, fn "(") == 1) { print held " " $0 } else { print held; print } held = ""; next }
			/^kernel void$/ || /^static [a-z0-9_]+$/ { held = $0; next }
			{ print }' "$2" ;;
	esac
}
kcall_mutants=0 kcall_pairs=0
for pair in "rm_rtwater.metal rm_rtw_trace mtl_rtw_ss_next" "rm_rtwater.metal rm_rtw_trace mtl_rtw_choose" \
	"rm_rtwater.metal rm_rtw_trace_ws mtl_rtw_ws_next" "rm_rtwater.metal rm_rtw_trace_ws mtl_rtw_choose_ws" \
	"rm_rtwater.metal rm_rtw_ss_at mtl_rtw_choose" "rm_rtwater.metal rm_rtw_trace rm_rtw_ss_at"; do
	# shellcheck disable=SC2086 # three words: file, kernel, helper
	set -- $pair
	f=$rm_dir/$1 kn=$2 hp=$3
	rc=0
	kcalls "$f" "$kn" "$hp" || rc=$?
	if [ "$rc" = 2 ]; then
		bad "kernel $kn not found in $f"
		continue
	elif [ "$rc" != 0 ]; then
		bad "$f: the kernel $kn does not call $hp (the helper its host test drives)"
		continue
	fi
	kcall_pairs=$((kcall_pairs + 1))
	for m in renamed commented elsewhere renamedkernel deadcode deadnested deadnestedparen deadparen deadparen2 joined; do
		kcall_mutant "$m" "$f" "$kn" "$hp" > "${tmp:?}/kcall-mutant.metal"
		# a mutant that takes the function away is "not found" (2), the others "no call" (1)
		case $m in renamedkernel|joined) want=2 ;; *) want=1 ;; esac
		rc=0
		kcalls "${tmp:?}/kcall-mutant.metal" "$kn" "$hp" || rc=$?
		if cmp -s "$f" "${tmp:?}/kcall-mutant.metal"; then
			bad "the kernel-call mutant ($m) of $kn changed nothing"
		elif [ "$rc" = 0 ]; then
			bad "the kernel-call check passes a mutant of $kn (the call to $hp $m)"
		elif [ "$rc" != "$want" ]; then
			bad "the kernel-call check gives status $rc, not $want, for a mutant of $kn ($m)"
		else
			kcall_mutants=$((kcall_mutants + 1))
		fi
	done
	# the "#else" part of an "#if 0" is live: the call repeated there is still a call
	kcall_mutant liveelse "$f" "$kn" "$hp" > "${tmp:?}/kcall-live.metal"
	if cmp -s "$f" "${tmp:?}/kcall-live.metal" || ! kcalls "${tmp:?}/kcall-live.metal" "$kn" "$hp"; then
		bad "the kernel-call check refuses a call kept in the #else part of an #if 0 ($kn, $hp)"
	fi
done

# (f) the colour-space probe. probe_run <dir with rmb_device.m or empty> <exe>: builds
# tests/metal/rmb_cs_probe.m, which includes rmb_device.m (found in <dir> first, else in
# $rm_dir) and runs the renderer's own rmb_attach, rmb_colorspace_set and rmb_detach on a
# standalone CAMetalLayer; its output is in $tmp/probe.out, its exit status is returned (a build
# failure is 99).
probe_run() {
	# shellcheck disable=SC2086 # flags is a word list
	# the sanitizers when (a) found the compiler has them, and the aliasing and overflow flags of the
	# script's other builds; the warnings are -Wall -Wextra -Werror, stricter than rmb_device.m's real
	# build on purpose (docs/runbook/TASK164.md, FIX99)
	if ! $cc -fobjc-arc -std=gnu11 -Wall -Wextra -Werror -g -O1 -fno-strict-aliasing -fwrapv ${psan:-} -I"${1:-$rm_dir}" -I"$rm_dir" -I"$rm_dir/header" -I. \
		'-DYQ2OSTYPE="host"' '-DYQ2ARCH="host"' -o "$2" tests/metal/rmb_cs_probe.m "$rm_dir/rm_pure.c" \
		"$rm_dir/common/mtl_matrix.c" "$rm_dir/common/mtl_ring.c" src/common/shared/shared.c \
		-framework Foundation -framework Metal -framework QuartzCore -framework CoreGraphics -lm > "${tmp:?}/probe.out" 2>&1; then
		return 99
	fi
	rc=0
	# a mutant must fail by an exit status, not a signal (the shell prints "Abort trap" for a signal and
	# the system may write a crash report): the probe stops at its first wrong retain count, and
	# ASAN exits instead of aborting
	ASAN_OPTIONS=abort_on_error=0 "$2" > "${tmp:?}/probe.out" 2>&1 || rc=$?
	return $rc
}
probe="probe skipped"
if [ "$(uname -s)" != Darwin ]; then
	echo "check-ref-metal: colour-space probe skipped: not macOS"
else
	rc=0
	probe_run "" "${tmp:?}/rmb_cs_probe" || rc=$?
	if [ "$rc" = 99 ]; then
		cat "${tmp:?}/probe.out"
		bad "tests/metal/rmb_cs_probe.m does not compile"
	elif [ "$rc" != 0 ]; then
		cat "${tmp:?}/probe.out"
		bad "tests/metal/rmb_cs_probe.m failed"
	elif grep -q 'skipped: no Metal device' "${tmp:?}/probe.out"; then
		echo "check-ref-metal: colour-space probe skipped: no Metal device"
	else
		probe=$(tail -n 1 "${tmp:?}/probe.out" | sed 's/^rmb_cs_probe: //')
		case $probe in *", 0 failed") ;; *) bad "tests/metal/rmb_cs_probe.m printed no result line"; probe="probe skipped";; esac
		# the mutants of rmb_device.m, each of which must fail the probe: 1 -> 0 sets NULL, 1 -> 0
		# leaves the colour space on the layer, detach leaves it too
		probe_mutants=0
		for m in nulled kept detached unretained; do
			rm -rf "${tmp:?}/csmut"
			mkdir "${tmp:?}/csmut"
			case $m in
				nulled) sed 's|rmb_layer.colorspace = rmb_cs_own;|rmb_layer.colorspace = NULL;|' "$rm_dir/rmb_device.m" ;;
				kept) sed 's|rmb_layer.colorspace = rmb_cs_own;|(void)0;|' "$rm_dir/rmb_device.m" ;;
				unretained) sed 's|CGColorSpaceRetain(rmb_cs_own);|(void)0;|' "$rm_dir/rmb_device.m" ;;
				detached) sed 's|rmb_cs_release();   /\* the view|(void)0;   /* the view|' "$rm_dir/rmb_device.m" ;;
			esac > "${tmp:?}/csmut/rmb_device.m"
			if cmp -s "$rm_dir/rmb_device.m" "${tmp:?}/csmut/rmb_device.m"; then
				bad "the colour-space mutant ($m) of rmb_device.m changed nothing"
				continue
			fi
			rc=0
			probe_run "${tmp:?}/csmut" "${tmp:?}/rmb_cs_mutant" || rc=$?
			if [ "$rc" = 99 ]; then
				cat "${tmp:?}/probe.out"
				bad "the colour-space mutant ($m) does not compile"
			elif [ "$rc" = 0 ]; then
				bad "the colour-space probe passes a mutant of rmb_device.m ($m)"
			else
				probe_mutants=$((probe_mutants + 1))
			fi
		done
		probe="$probe; $probe_mutants colour-space mutants refused"
	fi
fi

# (a2) FIX98: the water mark's blend factors are defined once (common/mtl_rtw_math.h:
# MTL_RTW_MARK_SRC_FACTOR and MTL_RTW_MARK_DST_FACTOR, which the host test's
# mtl_rtw_mark_blend evaluates), and the pipeline sets the normal attachment's alpha factors
# from them: a literal MTLBlendFactor there is a second definition that can drift.
blend_ok() { # <file>: 0 when the mark pipeline's two alpha factors come from the macros, none is a literal, and the factor mapping is pinned
	strip "$1" | awk '
	/^rmb_rtw_mark_pipe\(/ { on = 1 }
	on { print }
	on && /^}/ { on = 0 }' > "${tmp:?}/blend.code"
	strip "$1" | awk '
	/^rmb_rtw_factor\(/ { on = 1 }
	on { print }
	on && /^}/ { on = 0 }' > "${tmp:?}/factor.code"
	[ -s "${tmp:?}/blend.code" ] && [ -s "${tmp:?}/factor.code" ] &&
	[ "$(grep -cE 'colorAttachments\[1\]\.sourceAlphaBlendFactor = rmb_rtw_factor\(MTL_RTW_MARK_SRC_FACTOR\);' "${tmp:?}/blend.code")" = 1 ] &&
	[ "$(grep -cE 'colorAttachments\[1\]\.destinationAlphaBlendFactor = rmb_rtw_factor\(MTL_RTW_MARK_DST_FACTOR\);' "${tmp:?}/blend.code")" = 1 ] &&
	[ "$(grep -cE 'colorAttachments\[1\]\.(source|destination)AlphaBlendFactor =' "${tmp:?}/blend.code")" = 2 ] &&
	[ "$(awk '/case MTL_RTW_BF_ONE:/ { getline; print }' "${tmp:?}/factor.code" | tr -d '[:space:]')" = "returnMTLBlendFactorOne;" ] &&
	[ "$(awk '/case MTL_RTW_BF_SRC_ALPHA:/ { getline; print }' "${tmp:?}/factor.code" | tr -d '[:space:]')" = "returnMTLBlendFactorSourceAlpha;" ]
}
blend_mutants=0
if ! blend_ok "$rm_dir/rmb_rtwater.m"; then
	bad "$rm_dir/rmb_rtwater.m: the water mark's alpha blend factors do not both come from MTL_RTW_MARK_SRC_FACTOR and MTL_RTW_MARK_DST_FACTOR, or rmb_rtw_factor's mapping is not pinned"
else
	sed 's/destinationAlphaBlendFactor = rmb_rtw_factor(MTL_RTW_MARK_DST_FACTOR)/destinationAlphaBlendFactor = MTLBlendFactorSourceAlpha/' \
		"$rm_dir/rmb_rtwater.m" > "${tmp:?}/blend-mutant1.m"
	sed 's/sourceAlphaBlendFactor = rmb_rtw_factor(MTL_RTW_MARK_SRC_FACTOR)/sourceAlphaBlendFactor = rmb_rtw_factor(MTL_RTW_BF_ONE)/' \
		"$rm_dir/rmb_rtwater.m" > "${tmp:?}/blend-mutant2.m"
	sed 's/return MTLBlendFactorSourceAlpha;/return MTLBlendFactorOneMinusSourceAlpha;/' \
		"$rm_dir/rmb_rtwater.m" > "${tmp:?}/blend-mutant3.m"
	sed 's/^\(\td\.colorAttachments\[1\]\.destinationAlphaBlendFactor = rmb_rtw_factor(MTL_RTW_MARK_DST_FACTOR);\)$/\1\n\td.colorAttachments[1].destinationAlphaBlendFactor = rmb_rtw_factor(MTL_RTW_BF_ONE);/' \
		"$rm_dir/rmb_rtwater.m" > "${tmp:?}/blend-mutant4.m"
	for n in 1 2 3 4; do
		if cmp -s "$rm_dir/rmb_rtwater.m" "${tmp:?}/blend-mutant$n.m"; then
			bad "the blend-factor mutant $n changed nothing"
		elif blend_ok "${tmp:?}/blend-mutant$n.m"; then
			bad "the blend-factor check passes a mutant ($n)"
		else
			blend_mutants=$((blend_mutants + 1))
		fi
	done
	# a literal on another pipeline's attachment 1 is not the mark's: it must pass
	sed 's/^\(\td\.colorAttachments\[1\]\.writeMask = MTLColorWriteMaskNone;\)$/\1\n\td.colorAttachments[1].sourceAlphaBlendFactor = MTLBlendFactorOne;/' \
		"$rm_dir/rmb_rtwater.m" > "${tmp:?}/blend-other.m"
	if cmp -s "$rm_dir/rmb_rtwater.m" "${tmp:?}/blend-other.m"; then
		bad "the other-pipeline blend case changed nothing"
	elif ! blend_ok "${tmp:?}/blend-other.m"; then
		bad "the blend-factor check refuses a literal factor on another pipeline"
	fi
fi

# (b) the build, where it can be built
skip=""
if [ "$(uname -s)" != Darwin ]; then
	skip="not macOS"
elif ! command -v xcrun >/dev/null 2>&1 || ! xcrun -f metal >/dev/null 2>&1; then
	skip="no Metal compiler (xcrun -f metal)"
elif ! command -v make >/dev/null 2>&1; then
	skip="no make"
else
	printf 'include Makefile\nrm-print-sdlcflags:\n\t@echo $(SDLCFLAGS)\n' > "${tmp:?}/print.mk"
	printf '#include <SDL3/SDL.h>\nint main(void){return 0;}\n' > "${tmp:?}/sdl.c"
	if [ ! -f third_party/SDL3/include/SDL3/SDL.h ]; then
		skip="the vendored SDL 3 headers are missing (third_party/SDL3/include/SDL3/SDL.h)"
	else
		# the headers are part of the tree: a probe that fails or prints nothing is a
		# broken Makefile, not a reason to skip. The make flags are cleared, as for the builds.
		rc=0
		sdlcflags=$(unset MAKEFLAGS GNUMAKEFLAGS MFLAGS; make -s -f "${tmp:?}/print.mk" rm-print-sdlcflags 2> "${tmp:?}/probe.err") || rc=$?
		if [ "$rc" != 0 ]; then
			tail -n 5 "${tmp:?}/probe.err"
			bad "the Makefile cannot be evaluated (make status $rc printing SDLCFLAGS)"
			skip="the SDLCFLAGS probe failed"
		elif [ -z "$sdlcflags" ]; then
			bad "the Makefile gives an empty SDLCFLAGS although the vendored SDL 3 headers exist"
			skip="the SDLCFLAGS probe failed"
		# shellcheck disable=SC2086 # SDLCFLAGS is a word list
		elif ! $cc $sdlcflags -fsyntax-only "${tmp:?}/sdl.c" > "${tmp:?}/probe.err" 2>&1; then
			tail -n 5 "${tmp:?}/probe.err"
			bad "SDLCFLAGS ($sdlcflags) cannot compile <SDL3/SDL.h> although the vendored headers exist"
			skip="the SDLCFLAGS probe failed"
		fi
	fi
fi

if [ -n "$skip" ]; then
	echo "check-ref-metal: build skipped: $skip"
	build="build skipped"
else
	mkdir "${tmp:?}/tree"
	cp -R Makefile src "${tmp:?}/tree/"
	mkdir "${tmp:?}/tree/third_party"
	cp third_party/sdl3.make "${tmp:?}/tree/third_party/"
	# the sun's per-map table, which rm_rtsunscene.c builds in (TASK56)
	mkdir "${tmp:?}/tree/scripts"
	cp -R scripts/rt "${tmp:?}/tree/scripts/"
	ln -s "$PWD/third_party/SDL3" "${tmp:?}/tree/third_party/SDL3"
	jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
	[ "$jobs" -le 4 ] || jobs=4
	if ! (unset MAKEFLAGS GNUMAKEFLAGS MFLAGS; make -C "${tmp:?}/tree" -j"$jobs" ref_metal) > "${tmp:?}/build.out" 2>&1; then
		tail -n 30 "${tmp:?}/build.out"
		bad "make ref_metal failed"
	elif [ ! -f "${tmp:?}/tree/release/ref_metal.dylib" ]; then
		bad "make ref_metal wrote no release/ref_metal.dylib"
	elif [ ! -s "${tmp:?}/tree/release/ref_metal.metallib" ]; then
		bad "make ref_metal wrote no release/ref_metal.metallib"
	else
		libs=$(otool -L "${tmp:?}/tree/release/ref_metal.dylib" 2>&1) || bad "otool -L failed on ref_metal.dylib"
		case $libs in *Metal.framework*) ;; *) bad "ref_metal.dylib does not link Metal";; esac
		case $libs in *QuartzCore.framework*) ;; *) bad "ref_metal.dylib does not link QuartzCore";; esac
		case $libs in *OpenGL.framework*) bad "ref_metal.dylib links OpenGL";; esac
		case $libs in *libSDL3*) bad "ref_metal.dylib links an SDL of its own";; esac
		# MetalFX (decision 0011): linked, and weak, as a target OS may lack it
		weak=$(otool -l "${tmp:?}/tree/release/ref_metal.dylib" 2>&1 | awk '/cmd LC_LOAD_WEAK_DYLIB/ { w = 1 } w && / name / { print $2; w = 0 }')
		case $weak in *MetalFX.framework*) ;; *) bad "ref_metal.dylib does not weak-link MetalFX";; esac
	fi
	rc=0
	(unset MAKEFLAGS GNUMAKEFLAGS MFLAGS; make -C "${tmp:?}/tree" -s ref_metal YQ2_OSTYPE=Linux) > "${tmp:?}/other.out" 2>&1 || rc=$?
	if [ "$rc" = 0 ] || ! grep -q 'macOS' "${tmp:?}/other.out"; then
		tail -n 5 "${tmp:?}/other.out"
		bad "make ref_metal on another OS does not fail naming macOS"
	fi
	build="ref_metal built"
fi

# metal_rate_start is read once, in RI_Init (TASK161): the cvar's value is used nowhere else in
# rm_main.c, so a per-frame read, which a mid-run set would move, cannot come back. Scratch mutants:
# a read in RI_EndFrame, and one at close.
rate_main=src/client/refresh/metal/rm_main.c
rate_reads() { # <file>: how many lines of <file> read the cvar's value
	grep -c 'metal_rate_start->value' "$1"
}
if [ "$(rate_reads "$rate_main")" != 1 ] || ! grep -q 'rm_rate_start = metal_rate_start->value;' "$rate_main"; then
	bad "$rate_main reads metal_rate_start->value other than once, in RI_Init"
fi
rate_mutants=0
for rate_mut in 's/rm_rate_start);/metal_rate_start->value);/' 's/&rm_rate, rm_rate_start,/\&rm_rate, metal_rate_start->value,/'; do
	sed "$rate_mut" "$rate_main" > "${tmp:?}/rm_main_rate.c"
	if [ "$(rate_reads "${tmp:?}/rm_main_rate.c")" = 1 ]; then
		bad "the rate-start check passes a mutant ($rate_mut)"
	else
		rate_mutants=$((rate_mutants + 1))
	fi
done

if [ "$fail" = 0 ]; then
	echo "check-ref-metal: ok (${host:-host test}; ray tracing ${rt:-host test}; G-buffer ${gb:-host test}; shadows ${rts:-host test}; AO ${ao:-host test}; water ${rtw:-host test}; sun ${rtsun:-host test}; MetalFX ${fx:-host test}; ray-tracing gate ${rtg:-host test}, ${rtg_cvars:-no} r_rt_* cvars classified, $rtg_mutants table mutants refused; $mutants vocabulary mutants refused; 3 flush points, $flush_mutants flush mutants refused; blend factors once, $blend_mutants mutants refused; 5 gate-seat rules, $seat_mutants seat mutants refused; $kcall_pairs kernel calls, $kcall_mutants kernel-call mutants refused; colour space ${probe}; metal_rate_start read once, $rate_mutants mutants refused; $build)"
fi
exit $fail
