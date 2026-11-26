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
#     tests/metal/rm_rttemporal_test.c, the temporal accumulation's rule,
#     blend and resets (rm_rttemporal.c and common/mtl_rtt_math.h, RT7,
#     TASK59), with mutants of both files it must fail and a pin of the
#     AO kernel's call to the rule with two mutants (the reset dropped, a
#     material compared with itself),
#     tests/metal/rm_rtdenoise_test.c, the denoiser's weight rule
#     (common/mtl_rtd_math.h, RT7, TASK59 slice 2), with mutants of the
#     header it must fail and pins of rm_rtdenoise.metal's calls with
#     mutants (the depth guide and the count scale dropped, a history
#     written), compiled with the Metal compiler where there is one, and
#     the water reflections' 3 x 3 blur (slice 2b) with its rule's mutants
#     (the depth guide dropped, a neighbour without a reflection blurred
#     in) and rm_rtwdenoise.metal's pins with mutants (the depth guide
#     dropped, a history read, a history written, a tap of another layer
#     or a miss taken),
#     tests/metal/rm_rttab_test.c, r_rt_temporal through the A/B layer
#     (slice 2b): the real text of RM_Effect, RM_RTT_On and RM_RTT_ViewFor's
#     alpha over the real rm_ab_value and rm_rtt_alpha, with mutants that
#     read r_rt_temporal->value directly, and RM_PresBelow's view size
#     (TASK190 slice 3) with a mutant that drops its early return for the
#     menu's player model,
#     tests/metal/rm_rtwater_test.c, the pure part of the water reflections
#     (rm_rtwater.c and common/mtl_rtw_math.h, RT6, TASK58),
#     tests/metal/rm_rtsun_test.c, the pure part of the sun and sky shadow
#     (rm_rtsun.c and common/mtl_rtsun_math.h, RT4, TASK56),
#     tests/metal/rm_rtdir_test.c, the pure part of RT-primary's direct light
#     (rm_rtdir.c and common/mtl_rtdir_math.h, TASK188; eight scratch mutants
#     of the exposure fit in rm_rtdir.c must fail it; TASK189's sky light and
#     sun, common/mtl_rtsky_math.h, and the kernel's mutants of them),
#     tests/metal/rm_rtptemporal_test.c, RT-primary's temporal accumulation
#     (common/mtl_rtp_math.h, TASK190 slice 1), with five mutants of the
#     header it must fail and four pins of rm_rtptemporal.metal with seven
#     mutants, compiled with xcrun metal where it is there,
#     tests/metal/rm_rtpdenoise_test.c, RT-primary's 5 x 5 filter of that
#     light (common/mtl_rtpd_math.h, TASK190 slice 2), with five mutants of
#     the header and five pins of rm_rtpdenoise.metal with six mutants,
#     tests/metal/rm_rtmodel_test.c, the rules of RT-primary's light on
#     alias models (rm_rtmodel.c and common/mtl_rtm_math.h, TASK191; eighteen
#     scratch mutants of the two must fail it, and all eighteen must),
#     tests/metal/rm_rtlights_test.c, the ray-traced presentation's data
#     (rm_rtlights.c alone: the entity-lump parser, an asset loader, on
#     generated lumps, TASK187), and
#     tests/metal/rm_fx_test.c, the pure part of the MetalFX scalers
#     (rm_metalfx.c, TASK111; three scratch mutants of TASK109's effect
#     reset and reset line it must fail), and tests/metal/rm_rtgate_test.c, the
#     hardware gate of ray tracing (rm_rtgate.c, RT8, TASK60), and
#     tests/metal/rm_retro_tex_test.c, Retro's texture upload and 480-line
#     view (common/mtl_retro_tex.h, TASK185). The gate's
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
#     must fail it. It includes rmb_metalfx.m too (TASK196 HD4): the
#     MetalFX scalers' formats and modes under r_hdr 0 and 1, the refusal
#     in float16, the toggle; scratch mutants of it must fail it as well.
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
# (h) Darwin and elsewhere: FIX119, one internal header for the backend files; a
#     hand-written prototype in an rmb_*.m file is refused, with scratch mutants and
#     false-positive fixtures for the check.
# (i) Always: the effect table (TASK179). tests/metal/rm_rtfx_test.c over
#     rm_rtfx.c (RM_RTFX_HOST: every entry point a recording stub of
#     tests/metal/rm_rtfx_host.h) and rm_rtgate.c: the order, the switches
#     against the gate, the pipeline rule, what each walk calls; ten scratch
#     mutants of the two files must fail it. Then, comments and literals
#     stripped, rm_main.c and rmb_gbuf.m name no effect's symbol (a prefix
#     taken from the table's FX(...) rows); four mutants check the lint.
# (j) Always: the HDR tone map's single-map order (TASK199), by source inspection of
#     rm_main.c: R_ClearNoWorld(); is the first statement of R_RenderViewDraw, it draws
#     over &r_view.vp, and R_SetupGL and R_HdrToneMap build their rectangle with
#     rm_view_rect from the same refdef fields (x y width height); four scratch mutants
#     (the clear removed, another rectangle, each rectangle's fields changed) must fail it.
set -eu
cd "$(dirname "$0")/.."

fail=0
bad() { echo "check-ref-metal: $*"; fail=1; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/ref-metal.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT

# The summary line is built from segments: every sub-check block ends with one line
#   seg <n> "<joiner><text>"
# (the text may use variables the block has just set). n is the segment's place in the printed
# line, which is not the order the blocks run in; the joiner ("; " or ", ") is part of the text,
# so the final echo names no sub-check. A new sub-check picks an unused n and adds its seg line.
seg() { printf '%s\t%s\n' "$1" "$2" >> "${tmp:?}/summary"; }

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
seg 1 "${host:-host test}"

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
seg 2 "; ray tracing ${rt:-host test}"

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
seg 3 "; G-buffer ${gb:-host test}"

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
seg 4 "; shadows ${rts:-host test}"

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
seg 5 "; AO ${ao:-host test}"

# the temporal accumulation (RT7, TASK59 slices 1 and 1b): rm_rttemporal.c and common/mtl_rtt_math.h,
# the rejection rule, the blend, the resets, the shadows' light set and the histories' one reset
# state; then the same test against mutants of those two files, each of which must compile and fail
# it, while the real files pass; then the kernels' and the back ends' calls, pinned as text.
rtt=""
rtt_tree() { # <dir>: a copy of the test and the two files, laid out as the test includes them
	rm -rf "${1:?}"
	mkdir -p "$1/src/client/refresh/metal/header" "$1/src/client/refresh/metal/common" "$1/tests/metal"
	cp "$rm_dir/header/rm_rttemporal.h" "$1/src/client/refresh/metal/header/"
	cp "$rm_dir/common/mtl_rtt_math.h" "$rm_dir/common/mtl_rts_math.h" "$rm_dir/common/mtl_shader_math.h" \
		"$1/src/client/refresh/metal/common/"
	cp "$rm_dir/rm_rttemporal.c" "$1/src/client/refresh/metal/"
	cp tests/metal/rm_rttemporal_test.c "$1/tests/metal/"
}
rtt_build() { # <tree>: 0 when the tree's test builds
	# shellcheck disable=SC2086 # flags is a word list
	$cc $flags -o "$1/t" "$1/tests/metal/rm_rttemporal_test.c" "$1/src/client/refresh/metal/rm_rttemporal.c" -lm \
		> "${tmp:?}/cc.out" 2>&1
}
rtt_run() { # <tree>: 0 when the tree's test builds and passes
	rtt_build "$1" && UBSAN_OPTIONS=print_stacktrace=1 "$1/t" > "${tmp:?}/out" 2>&1
}
rtt_tree "${tmp:?}/rtt"
if ! rtt_run "${tmp:?}/rtt"; then
	cat "${tmp:?}/cc.out" "${tmp:?}/out"
	bad "tests/metal/rm_rttemporal_test.c fails on the real files"
else
	rtt=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_rttemporal_test: //')
	case $rtt in *", 0 failed") ;; *) bad "tests/metal/rm_rttemporal_test.c printed no result line"; rtt="";; esac
fi
# the mutants: a rejection reason dropped (material, lights, depth, off screen), the view's reset
# ignored, the frame without an accumulated view not resetting, a map load not resetting; slice 1b's:
# the light set's flags not masked, a history that skipped a view still read (the early return), a
# history made again still read, the views' number restarting on a map load. Each must compile (a
# mutant the compiler refuses tests nothing, review TASK59 slice 1, I1) and fail the test.
rtt_mutants=0
for m in mat lights depth off reset frameend mapload lightsmask histgap histreset seqkeep; do
	rtt_tree "${tmp:?}/rttm"
	h=${tmp:?}/rttm/src/client/refresh/metal/common/mtl_rtt_math.h
	c=${tmp:?}/rttm/src/client/refresh/metal/rm_rttemporal.c
	case $m in
		mat) sed 's/if (mat != hmat)/if ((mat != hmat) \&\& 0)/' "$rm_dir/common/mtl_rtt_math.h" > "$h" ;;
		lights) sed 's/if (lights != hlights)/if ((lights != hlights) \&\& 0)/' "$rm_dir/common/mtl_rtt_math.h" > "$h" ;;
		depth) sed 's/if (!mtl_rtt_depth_ok(dist, hdist, move))/if (!mtl_rtt_depth_ok(dist, hdist, move) \&\& 0)/' \
			"$rm_dir/common/mtl_rtt_math.h" > "$h" ;;
		off) sed 's/return r | MTL_RTT_REJ_OFF;/r |= 0;/' "$rm_dir/common/mtl_rtt_math.h" > "$h" ;;
		reset) sed 's/r |= MTL_RTT_REJ_RESET;/r |= 0;/' "$rm_dir/common/mtl_rtt_math.h" > "$h" ;;
		frameend) sed 's/st->valid = 0;/st->valid = 1;/' "$rm_dir/rm_rttemporal.c" > "$c" ;;
		mapload) sed 's/memset(st, 0, sizeof(\*st));/st->drew = 0;/' "$rm_dir/rm_rttemporal.c" > "$c" ;;
		lightsmask) sed 's/return packed & MTL_RTT_LIGHTS_MASK;/return packed | (MTL_RTT_LIGHTS_MASK \& 0);/' \
			"$rm_dir/common/mtl_rtt_math.h" > "$h" ;;
		histgap) sed 's/if (!h->valid || (t->seq != h->seq + 1u))/if (!h->valid || ((t->seq != h->seq + 1u) \&\& 0))/' \
			"$rm_dir/rm_rttemporal.c" > "$c" ;;
		histreset) sed 's/	h->valid = 0;/	h->valid = h->valid;/' "$rm_dir/rm_rttemporal.c" > "$c" ;;
		seqkeep) sed 's/	st->seq = seq;/	st->seq = seq \& 0;/' "$rm_dir/rm_rttemporal.c" > "$c" ;;
	esac
	if cmp -s "$rm_dir/common/mtl_rtt_math.h" "$h" && cmp -s "$rm_dir/rm_rttemporal.c" "$c"; then
		bad "the temporal mutant ($m) changed nothing"
	elif ! rtt_build "${tmp:?}/rttm"; then
		cat "${tmp:?}/cc.out"
		bad "the temporal mutant ($m) does not compile, so it tests nothing"
	elif rtt_run "${tmp:?}/rttm"; then
		bad "tests/metal/rm_rttemporal_test.c passes the temporal mutant ($m)"
	else
		rtt_mutants=$((rtt_mutants + 1))
	fi
done
# the kernels' calls, as pinned (TASK59 slices 1 and 1b): the rule with every reason's inputs and the
# view's reset passed on (the shadows' light set the masked packed indices on both sides, 0 for the AO
# and the sun), and the reprojection with the motion vector as it is (review slice 1, N4). Each pin
# has mutants it must refuse: the reset not passed, the light set dropped or the material compared
# with itself, the motion's sign flipped.
rtt_pin() { # <file> <kernel> <call>: 0 when the kernel's body holds the call exactly as pinned
	awk -v fn="$2" '$0 ~ "^" fn "\\(" { b = 1 } b && /^}/ { b = 0 } b { print }' "$1" | tr -s ' \t' '  ' |
		grep -qF "$3"
}
rtt_rep='mtl_rtt_reproject(px.x, px.y, mv.x, mv.y, w, hh)'
rtt_kmut=0
rtt_kpins=0
for pin in "rm_rtao.metal|rm_rtao_temporal|mtl_rtt_reject(t.reset, r.ok, dist, h.y, t.move, m, hm, 0u, 0u, h.w)|noreset samemat" \
	"rm_rtshadow.metal|rm_rts_temporal|mtl_rtt_reject(t.reset, r.ok, dist, hd, t.move, m, hm, set, hset, hc)|noreset noset" \
	"rm_rtshadow.metal|rm_rts_temporal|const uint set = mtl_rtt_lights(mtl_rts_pack_idx(s.top, s.flags)), hset = hr.x|unmasked" \
	"rm_rtsun.metal|rm_rtsun_temporal|mtl_rtt_reject(t.reset, r.ok, dist, h.y, t.move, m, hm, 0u, 0u, h.w)|noreset samemat" \
	"rm_rtao.metal|rm_rtao_temporal|$rtt_rep|sign" "rm_rtshadow.metal|rm_rts_temporal|$rtt_rep|sign" \
	"rm_rtsun.metal|rm_rtsun_temporal|$rtt_rep|sign"; do
	# four fields: the file, the kernel, the call, its mutants (the call holds no "|")
	kf=${pin%%|*} rest=${pin#*|}
	kn=${rest%%|*} rest=${rest#*|}
	kc=${rest%|*} kms=${rest##*|}
	if ! rtt_pin "$rm_dir/$kf" "$kn" "$kc"; then
		bad "$kf: $kn does not call $kc"
		continue
	fi
	rtt_kpins=$((rtt_kpins + 1))
	# shellcheck disable=SC2086 # kms is a word list
	for m in $kms; do
		case $m in
			noreset) sed 's/mtl_rtt_reject(t\.reset,/mtl_rtt_reject(0u,/' "$rm_dir/$kf" ;;
			samemat) sed 's/t\.move, m, hm,/t.move, m, m,/' "$rm_dir/$kf" ;;
			noset) sed 's/hm, set, hset, hc)/hm, 0u, 0u, hc)/' "$rm_dir/$kf" ;;
			unmasked) sed 's/const uint set = mtl_rtt_lights(mtl_rts_pack_idx(s\.top, s\.flags))/const uint set = mtl_rts_pack_idx(s.top, s.flags)/' \
				"$rm_dir/$kf" ;;
			sign) awk -v fn="$kn" '$0 ~ "^" fn "\\(" { b = 1 } b && /^}/ { b = 0 }
				b { sub(/mtl_rtt_reproject\(px\.x, px\.y, mv\.x, mv\.y,/, "mtl_rtt_reproject(px.x, px.y, -mv.x, mv.y,") } { print }' \
				"$rm_dir/$kf" ;;
		esac > "${tmp:?}/rtt-kernel.metal"
		if cmp -s "$rm_dir/$kf" "${tmp:?}/rtt-kernel.metal"; then
			bad "the temporal kernel mutant ($m of $kn) changed nothing"
		elif rtt_pin "${tmp:?}/rtt-kernel.metal" "$kn" "$kc"; then
			bad "the temporal kernel pin passes the mutant ($m of $kn)"
		else
			rtt_kmut=$((rtt_kmut + 1))
		fi
	done
done
# the back ends (slice 1b): each encodes a view into its history only where its dispatch is encoded,
# through rm_rtt_hist_take, so that every early return before it leaves the history unread in the
# next view; the mutant passes the view's reasons on without the history's own
rtt_take='tv.reset = rm_rtt_hist_take(&thist, &tview);'
rtt_place() { # <file> <function>: 0 when the take sits after every early return of the function and before a dispatch
	awk -v fn="$2" -v take="$rtt_take" '
		index($0, fn "(") == 1 { b = 1; next }
		!b { next }
		/^}/ { ok = (t == 1) && d && (r == 1) && last; exit }
		index($0, take) { t++; next }
		t && !r && /dispatchThreads/ { d = 1 }
		t && /(^|[^A-Za-z0-9_])return([^A-Za-z0-9_]|$)/ { r++; last = ($0 ~ /^\treturn [^;]*;$/) }
		END { exit !ok }' "$1"
}
rtt_bmut=0
for bf in rmb_rtao.m rmb_rtshadow.m rmb_rtsun.m; do
	if [ "$(grep -cF "$rtt_take" "$rm_dir/$bf")" != 1 ]; then
		bad "$bf: its temporal dispatch does not take the view through $rtt_take"
		continue
	fi
	# round 2 (the slice 1b review's N3): where the call sits, after the trace's last early return
	# (its one return after the call is the function's last statement) and before a dispatch
	case $bf in
		rmb_rtao.m) bfn=rmb_rtao_trace ;;
		rmb_rtshadow.m) bfn=rmb_rts_trace ;;
		*) bfn=rmb_rtsun_trace ;;
	esac
	if ! rtt_place "$rm_dir/$bf" "$bfn"; then
		bad "$bf: $rtt_take is not after every early return of $bfn and before its dispatch"
		continue
	fi
	if [ "$bf" = rmb_rtshadow.m ]; then
		# the call moved above the scene's binding, whose failure returns
		awk -v take="$rtt_take" 'NR == FNR { if (index($0, take)) h = $0; next }
			index($0, take) { next } /if \(!rmb_rt_bind_scene\(/ { print h } { print }' \
			"$rm_dir/$bf" "$rm_dir/$bf" > "${tmp:?}/rtt-back.m"
		if cmp -s "$rm_dir/$bf" "${tmp:?}/rtt-back.m"; then
			bad "the temporal back-end placement mutant of $bf changed nothing"
		elif rtt_place "${tmp:?}/rtt-back.m" "$bfn"; then
			bad "the temporal back-end placement pin passes the mutant of $bf (the call above rmb_rt_bind_scene)"
		else
			rtt_bmut=$((rtt_bmut + 1))
		fi
	fi
	sed 's/tv\.reset = rm_rtt_hist_take(&thist, &tview);/tv.reset = tview.reset;/' "$rm_dir/$bf" > "${tmp:?}/rtt-back.m"
	if cmp -s "$rm_dir/$bf" "${tmp:?}/rtt-back.m"; then
		bad "the temporal back-end mutant of $bf changed nothing"
	elif [ "$(grep -cF "$rtt_take" "${tmp:?}/rtt-back.m")" = 1 ]; then
		bad "the temporal back-end pin passes the mutant of $bf"
	else
		rtt_bmut=$((rtt_bmut + 1))
	fi
done
# the stock shadow trace (slice 1b, round 1): rm_rts_trace and its unit stay as they were before RT7.
# Its helpers shared with rm_rts_temporal in one unit, the compiler stopped inlining rm_rts_visibility
# and mtl_rts_surf_light into it and the stock rt-shadows frames changed. So rm_rts_trace's text is
# pinned by its checksum, the temporal kernel and its copy of the trace's body (rm_rts_shade,
# rm_rts_finish) sit only behind RM_RTS_TEMPORAL_TU, compiled apart by rm_rtshadow_temporal.metal, and
# the copy holds the stock kernel's code lines in their order, so that the two cannot drift apart
# unseen. Mutants: the stock kernel edited (the checksum and the copy pin refuse it), the copy edited in
# either helper, a repeated line of the copy dropped or moved (round 2), the guard dropped.
rts_trace_sum='2595407311 7173'  # cksum of rm_rts_trace's text, from its name to its closing brace (TASK188 slice 3a)
rts_text() { # <file>: rm_rts_trace's definition, from its name to its closing brace
	awk '/^rm_rts_trace\(/ { b = 1 } b { print } b && /^}/ { exit }' "$1"
}
rts_code() { # <file> <function>...: the code lines of the bodies in order, braces kept, whitespace squeezed, rm_rts_px's "s." dropped
	awk -v fns="$(shift; echo "$*")" 'BEGIN { n = split(fns, want, " ") }
		{ for (i = 1; i <= n; i++) if (index($0, want[i] "(") == 1) h = 1 }
		h && /^\{/ { h = 0; b = 1; next }
		b && /^\}/ { b = 0; next }
		b { print }' "$1" |
		sed -E 's/^[[:space:]]+//; s/[[:space:]]+/ /g; s/(^|[^A-Za-z0-9_.])s\./\1/g' | grep -vE '^$|^/?\*'
}
# the lines only one side has: the stock kernel's own declarations and its write of the packed words, the
# copy's carrying of what it found in rm_rts_px and its write of the same words, packed once
printf '%s\n' 'const bool inside = (px.x < out.get_width()) && (px.y < out.get_height());' \
	'float adds[MTL_RTS_MAX_LIGHTS];' \
	'out.write(uint4(mtl_rts_pack_vis(vis[0], vis[1], vis[2], vis[3]), mtl_rts_pack_idx(top, flags), 0, 0), px);' \
	> "${tmp:?}/rts-stock-only"
printf '%s\n' 'rm_rts_px s;' 'nl = 0;' 'thread float *adds = adds;' 'nl = nl;' 'top = top;' 'flags = flags;' \
	'rays = rays;' 'primary = primary;' 'vis[k] = vis[k];' 'return s;' 'out.write(uint4(pvis, pidx, 0, 0), px);' \
	> "${tmp:?}/rts-copy-only"
# the two sequences' whole difference once those lines are out (round 2: in order, braces and repeats
# included): the term's accumulator declared in rm_rts_finish, at its use, instead of at the kernel's
# top; the copy closing the trace's two blocks before its visibilities' copy into s.vis, where the stock
# kernel makes the term inside them
printf '%s\n' '4d3' '< float3 t = float3(0.0f);' '118a118,120' '> }' '> }' '> for (int k = 0; k < MTL_RTS_CAP; k++)' \
	'119a122' '> }' '120a124' '> float3 t = float3(0.0f);' '128,130d131' '< }' '< }' '< }' \
	> "${tmp:?}/rts-diff"
rts_once() { # <lines> <code>: 0 when each of the lines occurs exactly once in the code
	while IFS= read -r l; do
		[ "$(grep -cxF -e "$l" "$2")" = 1 ] || return 1
	done < "$1"
}
rts_alike() { # <file>: 0 when rm_rts_shade and rm_rts_finish are rm_rts_trace's code in its order, but for the pinned difference
	rts_code "$1" rm_rts_trace > "${tmp:?}/rts-stock"
	rts_code "$1" rm_rts_shade rm_rts_finish > "${tmp:?}/rts-copy"
	[ -s "${tmp:?}/rts-stock" ] && [ -s "${tmp:?}/rts-copy" ] &&
		rts_once "${tmp:?}/rts-stock-only" "${tmp:?}/rts-stock" &&
		rts_once "${tmp:?}/rts-copy-only" "${tmp:?}/rts-copy" || return 1
	grep -vxF -f "${tmp:?}/rts-stock-only" "${tmp:?}/rts-stock" > "${tmp:?}/rts-stock-seq"
	grep -vxF -f "${tmp:?}/rts-copy-only" "${tmp:?}/rts-copy" > "${tmp:?}/rts-copy-seq"
	diff "${tmp:?}/rts-stock-seq" "${tmp:?}/rts-copy-seq" > "${tmp:?}/rts-got" || :
	cmp -s "${tmp:?}/rts-diff" "${tmp:?}/rts-got"
}
rts_unit() { # <file>: 0 when rm_rts_trace is outside RM_RTS_TEMPORAL_TU and the temporal kernel and its copy inside
	awk '/^#ifndef RM_RTS_TEMPORAL_TU$/ { if (s == 0) s = 1; next }
		/^#else \/\* RM_RTS_TEMPORAL_TU \*\/$/ { if (s == 1) s = 2; next }
		/^#endif \/\* RM_RTS_TEMPORAL_TU \*\/$/ { if (s == 2) s = 3; next }
		/^rm_rts_trace\(/ { if (s != 1) bad = 1; tr++ }
		/^(rm_rts_temporal|rm_rts_shade|rm_rts_finish)\(/ { if (s != 2) bad = 1; tp++ }
		END { exit !(s == 3 && !bad && tr == 1 && tp == 3) }' "$1"
}
rts_pins=0 rts_mut=0
rts_f=$rm_dir/rm_rtshadow.metal
if [ "$(rts_text "$rts_f" | cksum)" != "$rts_trace_sum" ]; then
	bad "rm_rtshadow.metal: rm_rts_trace's text is not the pinned stock kernel's (cksum $rts_trace_sum)"
else
	rts_pins=$((rts_pins + 1))
fi
if ! rts_alike "$rts_f"; then
	[ -f "${tmp:?}/rts-got" ] && diff "${tmp:?}/rts-diff" "${tmp:?}/rts-got" || :
	bad "rm_rtshadow.metal: rm_rts_shade and rm_rts_finish are not rm_rts_trace's code in its order (or a side-only line is not there once)"
else
	rts_pins=$((rts_pins + 1))
fi
if ! rts_unit "$rts_f"; then
	bad "rm_rtshadow.metal: rm_rts_trace not alone outside RM_RTS_TEMPORAL_TU, or the temporal kernel outside it"
elif ! grep -qxF '#define RM_RTS_TEMPORAL_TU 1' "$rm_dir/rm_rtshadow_temporal.metal" ||
	! grep -qxF '#include "rm_rtshadow.metal"' "$rm_dir/rm_rtshadow_temporal.metal" ||
	[ "$(grep -cF 'ref_metal/rm_rtshadow_temporal.air' Makefile)" != 2 ]; then
	bad "rm_rtshadow_temporal.metal does not compile the temporal kernel apart into ref_metal.metallib"
else
	rts_pins=$((rts_pins + 1))
fi
for m in stock shade finish slab-continue-dropped adds-zero-moved guard; do
	case $m in
		# round 2, lines the copy repeats: a pixel without light to add no longer skipped, and its add
		# cleared only after the skip, so that rm_rts_finish reads it unset
		slab-continue-dropped)
			awk '/^rm_rts_shade\(/ { b = 1 } b && /^}/ { b = 0 }
				b && /^[[:space:]]+if \(!slab\)$/ { s = 1 } s && /^[[:space:]]+continue;$/ { s = 0; next } { print }' \
				"$rts_f" ;;
		adds-zero-moved)
			awk '/^rm_rts_shade\(/ { b = 1 } b && /^}/ { b = 0 }
				b && /^[[:space:]]+adds\[i\] = 0\.0f;$/ { h = $0; next }
				b && (h != "") && /^[[:space:]]+if \(!slab\)$/ { s = 1 }
				s && /^[[:space:]]+}$/ { print; print h; h = ""; s = 0; next } { print }' \
				"$rts_f" ;;
		stock|shade|finish)
			kn=rm_rts_trace
			[ "$m" = shade ] && kn=rm_rts_shade
			[ "$m" = finish ] && kn=rm_rts_finish
			# the shadow ray's end in the trace, the term's test in the outputs: one copy only
			awk -v fn="$kn" '$0 ~ "^" fn "\\(" { b = 1 } b && /^}/ { b = 0 }
				b { sub(/len - 0\.5f\)/, "len - 0.25f)"); sub(/adds\[i\] > 0\.0f\)/, "adds[i] >= 0.0f)") } { print }' \
				"$rts_f" ;;
		guard) sed '/^#else \/\* RM_RTS_TEMPORAL_TU \*\/$/d; /^#endif \/\* RM_RTS_TEMPORAL_TU \*\/$/d' "$rts_f" ;;
	esac > "${tmp:?}/rts-mut.metal"
	if cmp -s "$rts_f" "${tmp:?}/rts-mut.metal"; then
		bad "the stock-trace mutant ($m) changed nothing"
		continue
	fi
	case $m in
		stock)
			if [ "$(rts_text "${tmp:?}/rts-mut.metal" | cksum)" = "$rts_trace_sum" ]; then
				bad "the stock-trace checksum pin passes the mutant ($m)"
			elif rts_alike "${tmp:?}/rts-mut.metal"; then
				bad "the copy pin passes the mutant ($m)"
			else
				rts_mut=$((rts_mut + 1))
			fi ;;
		shade|finish|slab-continue-dropped|adds-zero-moved)
			if rts_alike "${tmp:?}/rts-mut.metal"; then
				bad "the copy pin passes the mutant ($m)"
			else
				rts_mut=$((rts_mut + 1))
			fi ;;
		guard)
			if rts_unit "${tmp:?}/rts-mut.metal"; then
				bad "the unit pin passes the mutant ($m)"
			else
				rts_mut=$((rts_mut + 1))
			fi ;;
	esac
done
seg 5.5 "; temporal ${rtt:-host test}, $rtt_mutants rule mutants, $rtt_kpins kernel pins with $rtt_kmut kernel mutants and $rtt_bmut back-end mutants refused, $rts_pins stock-trace pins with $rts_mut mutants refused"

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
seg 6 "; water ${rtw:-host test}"

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
seg 7 "; sun ${rtsun:-host test}"

# the RT-primary presentation's direct light (TASK188, PM6): rm_rtdir.c and common/mtl_rtdir_math.h, with
# rm_rtshadow.c for the view's inverse, rm_rtgate.c for the mode and its overlay (and the effect table's host
# stubs it takes its switches from), rm_rt.c and rm_pure.c
rtdir=""
rtdir_sky=0
rtdir_bounce=0
# shellcheck disable=SC2086 # flags is a word list
if ! $cc $flags '-DYQ2OSTYPE="host"' '-DYQ2ARCH="host"' -DRM_RTFX_HOST -Itests/metal -I"$rm_dir" \
	-o "${tmp:?}/rm_rtdir_test" tests/metal/rm_rtdir_test.c "$rm_dir/rm_rtdir.c" "$rm_dir/rm_rtgate.c" \
	"$rm_dir/rm_rtfx.c" "$rm_dir/rm_rtshadow.c" "$rm_dir/rm_rt.c" "$rm_dir/rm_pure.c" "$rm_dir/common/mtl_matrix.c" \
	"$rm_dir/common/mtl_ring.c" src/common/shared/shared.c -lm > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	bad "tests/metal/rm_rtdir_test.c does not compile"
elif ! UBSAN_OPTIONS=print_stacktrace=1 "${tmp:?}/rm_rtdir_test" > "${tmp:?}/out" 2>&1; then
	cat "${tmp:?}/out"
	bad "tests/metal/rm_rtdir_test.c failed"
else
	rtdir=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_rtdir_test: //')
	case $rtdir in *", 0 failed") ;; *) bad "tests/metal/rm_rtdir_test.c printed no result line"; rtdir="";; esac
	# the sky light's and the sun's mutants (TASK189), refused inside the test, which counts them
	rtdir_sky=$(sed -n 's/^rm_rtdir_test: \([0-9][0-9]*\) sky mutants refused$/\1/p' "${tmp:?}/out" | tail -n 1)
	case $rtdir_sky in [1-9]*) ;; *) bad "tests/metal/rm_rtdir_test.c printed no sky mutant count"; rtdir_sky=0;; esac
	# the bounce's mutants (TASK189 slice 2), likewise
	rtdir_bounce=$(sed -n 's/^rm_rtdir_test: \([0-9][0-9]*\) bounce mutants refused$/\1/p' "${tmp:?}/out" | tail -n 1)
	case $rtdir_bounce in [1-9]*) ;; *) bad "tests/metal/rm_rtdir_test.c printed no bounce mutant count"; rtdir_bounce=0;; esac
fi
seg 7.5 "; direct light ${rtdir:-host test}"
# Scratch mutants of rm_rtdir.c the test must fail (TASK188 slice 3c): the exposure fit's line test with a
# leaf never solid, a split segment's first half sent down the wrong side, a point at 0 put behind, the
# depth cap a level deeper, the visit cap gone; the fixtures sampled; the luxel's s without texturemins;
# the sample stride rounded down
rtdir_mutants=0
if [ -n "$rtdir" ]; then
	mkdir "${tmp:?}/dirm"
	for dm in leaf-never-solid sides-swapped front-zero-behind depth-cap-off visits-uncapped fixtures-sampled \
		luxel-s-unshifted every-floor; do
		case $dm in
			leaf-never-solid) sed 's/return !(nd.contents & RM_RTDIR_CONTENTS_SOLID);/return 1;/' ;;
			sides-swapped) sed 's/rm_rtdir_clear_node(t, nd.child\[side\], a, mid, depth + 1)/rm_rtdir_clear_node(t, nd.child[!side], a, mid, depth + 1)/' ;;
			front-zero-behind) sed 's/side = front < 0;/side = front <= 0;/' ;;
			depth-cap-off) sed 's/(depth > RM_RTDIR_FIT_DEPTH)/(depth > RM_RTDIR_FIT_DEPTH + 1)/' ;;
			visits-uncapped) sed 's/if (t->visits >= t->cap)/if (0)/' ;;
			fixtures-sampled) sed 's/(RM_RTDIR_SURF_LIGHT | RM_RTDIR_SURF_TRANS33/(RM_RTDIR_SURF_TRANS33/' ;;
			luxel-s-unshifted) sed 's/vecs\[a\]\[3\] - texturemins\[a\]/vecs[a][3] - (a ? texturemins[a] : 0)/' ;;
			every-floor) sed 's|return 1 + (total - 1) / cap;|return total / cap;|' ;;
		esac < "$rm_dir/rm_rtdir.c" > "${tmp:?}/dirm/rm_rtdir.c"
		if cmp -s "$rm_dir/rm_rtdir.c" "${tmp:?}/dirm/rm_rtdir.c"; then
			bad "the exposure-fit mutant ($dm) changed nothing"
		# shellcheck disable=SC2086 # flags is a word list
		elif ! $cc $flags '-DYQ2OSTYPE="host"' '-DYQ2ARCH="host"' -DRM_RTFX_HOST -Itests/metal -I"$rm_dir" \
			-o "${tmp:?}/dirm/t" tests/metal/rm_rtdir_test.c "${tmp:?}/dirm/rm_rtdir.c" "$rm_dir/rm_rtgate.c" \
			"$rm_dir/rm_rtfx.c" "$rm_dir/rm_rtshadow.c" "$rm_dir/rm_rt.c" "$rm_dir/rm_pure.c" "$rm_dir/common/mtl_matrix.c" \
			"$rm_dir/common/mtl_ring.c" src/common/shared/shared.c -lm > "${tmp:?}/dirm.out" 2>&1; then
			cat "${tmp:?}/dirm.out"
			bad "the exposure-fit mutant ($dm) does not compile"
		elif "${tmp:?}/dirm/t" > "${tmp:?}/dirm.out" 2>&1; then
			bad "tests/metal/rm_rtdir_test.c passes the exposure-fit mutant ($dm)"
		else
			rtdir_mutants=$((rtdir_mutants + 1))
		fi
	done
fi
seg 7.6 ", $rtdir_mutants fit mutants, $rtdir_sky sky mutants and $rtdir_bounce bounce mutants refused"

# RT-primary's light on alias models (TASK191 slice 1, PM9): rm_rtmodel.c and common/mtl_rtm_math.h, held
# to gl1's model colour (rm_pure.c) and to the G-buffer's split (rm_gbuf.c), with rm_rtshadow.c for the
# weapon's view and rm_rt.c, which it needs
rtm_tree() { # <dir>: the test, rm_rtmodel.c and the headers, laid out as the test includes them
	rm -rf "${1:?}"
	mkdir -p "$1/src/client/refresh/metal" "$1/tests/metal"
	cp -R "$rm_dir/header" "$rm_dir/common" "$1/src/client/refresh/metal/"
	cp "$rm_dir/rm_rtmodel.c" "$1/src/client/refresh/metal/"
	cp tests/metal/rm_rtmodel_test.c "$1/tests/metal/"
}
rtm_run() { # <tree>: 0 when the tree's test builds (1 when not) and passes (2 when not)
	# shellcheck disable=SC2086 # flags is a word list
	$cc $flags '-DYQ2OSTYPE="host"' '-DYQ2ARCH="host"' -o "$1/t" "$1/tests/metal/rm_rtmodel_test.c" \
		"$1/src/client/refresh/metal/rm_rtmodel.c" "$rm_dir/rm_gbuf.c" "$rm_dir/rm_rtshadow.c" "$rm_dir/rm_rt.c" \
		"$rm_dir/rm_pure.c" "$rm_dir/common/mtl_matrix.c" "$rm_dir/common/mtl_ring.c" src/common/shared/shared.c \
		-lm > "${tmp:?}/cc.out" 2>&1 || return 1
	UBSAN_OPTIONS=print_stacktrace=1 "$1/t" > "${tmp:?}/out" 2>&1 || return 2
}
rtm=""
rtm_tree "${tmp:?}/rtm"
r=0
rtm_run "${tmp:?}/rtm" || r=$?
case $r in
	0)
		rtm=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_rtmodel_test: //')
		case $rtm in *", 0 failed") ;; *) bad "tests/metal/rm_rtmodel_test.c printed no result line"; rtm="";; esac ;;
	1) cat "${tmp:?}/cc.out"; bad "tests/metal/rm_rtmodel_test.c does not compile" ;;
	*) cat "${tmp:?}/out"; bad "tests/metal/rm_rtmodel_test.c failed" ;;
esac
# the mutants, each of which must compile and fail the test: the shadedot applied twice, the minlight floor
# skipped, the glow pulse's sign flipped, the split with a fullbright model traced, the tag's decode dropping
# a bit, the weapon unprojected with the view's projection; slice 2's: the weapon's depth hack read as the
# crosshair's, a model tagged without its normals, the colour index's NaN and sign guard on the bit pattern
# dropped; slice 3's: the weapon sent forward again (weapon-forward), a glowing or minlight model sent forward
# again (flagged-forward), a weapon without its depth hack traced (weapon-nohack), a model without a flag
# tagged (tag-unflagged), the weapon kept in the ray-traced scene under the traced models (gunshadow-kept), a
# model read as a wall by the pull-back ray (wall-alias), the weapon view's flag set when there is none
# (weapon-flag); its review's: the models wanted traced with the model pass refused (wants-refused), the
# tag written over the stock colour as a grey (tag-grey)
rtm_mutants=0
if [ -n "$rtm" ]; then
	for m in shadedot-twice minlight-skipped glow-sign fullbright-traced tag-bit weapon-view-inv \
		depthhack-weapon tag-no-normals nan-guard weapon-forward flagged-forward weapon-nohack tag-unflagged \
		gunshadow-kept wall-alias weapon-flag wants-refused tag-grey; do
		rtm_tree "${tmp:?}/rtmm"
		h=${tmp:?}/rtmm/src/client/refresh/metal/common/mtl_rtm_math.h
		c=${tmp:?}/rtmm/src/client/refresh/metal/rm_rtmodel.c
		case $m in
			shadedot-twice) sed 's/const float f = dot \* s \* 255.0f;/const float f = dot * dot * s * 255.0f;/' \
				"$rm_dir/common/mtl_rtm_math.h" > "$h" ;;
			minlight-skipped) sed 's/if (f & MTL_RTM_MINLIGHT)/if ((f \& MTL_RTM_MINLIGHT) \&\& 0)/' \
				"$rm_dir/common/mtl_rtm_math.h" > "$h" ;;
			glow-sign) sed 's/const float g = s + scale;/const float g = s - scale;/' "$rm_dir/common/mtl_rtm_math.h" > "$h" ;;
			fullbright-traced) sed 's/return rm_rtm_stock_light(flags, rdflags) || /return (rm_rtm_stock_light(flags, rdflags) \&\& !(flags \& RM_RF_FULLBRIGHT)) || /' \
				"$rm_dir/rm_rtmodel.c" > "$c" ;;
			depthhack-weapon) sed 's/(((flags & RM_RF_DEPTHHACK) != 0) != ((flags & RM_RF_WEAPONMODEL) != 0))/(flags \& RM_RF_DEPTHHACK)/' \
				"$rm_dir/rm_rtmodel.c" > "$c" ;;
			weapon-forward) sed 's/if ((flags & RM_RF_WEAPONMODEL) && (kind != RM_GB_KIND_ALIAS))$/if (flags \& RM_RF_WEAPONMODEL)/' \
				"$rm_dir/rm_rtmodel.c" > "$c" ;;
			flagged-forward) sed 's/!= ((flags & RM_RF_WEAPONMODEL) != 0));$/!= ((flags \& RM_RF_WEAPONMODEL) != 0)) || (flags \& (RM_RF_GLOW | RM_RF_MINLIGHT));/' \
				"$rm_dir/rm_rtmodel.c" > "$c" ;;
			weapon-nohack) sed 's/(((flags & RM_RF_DEPTHHACK) != 0) != ((flags & RM_RF_WEAPONMODEL) != 0))/((flags \& RM_RF_DEPTHHACK) \&\& !(flags \& RM_RF_WEAPONMODEL))/' \
				"$rm_dir/rm_rtmodel.c" > "$c" ;;
			tag-unflagged) sed 's/ || (rm_rtm_flags(flags) == 0u))$/)/' "$rm_dir/rm_rtmodel.c" > "$c" ;;
			wants-refused) sed 's/return device \&\& !refused \&\& /return device \&\& (refused >= 0) \&\& /' "$rm_dir/rm_rtmodel.c" > "$c" ;;
			tag-grey) sed 's/v\[i\].color\[3\] = mtl_rtm_tag(c, f).a;/v[i].color[0] = v[i].color[1] = v[i].color[2] = 128 \/ 255.0f; v[i].color[3] = mtl_rtm_tag(c, f).a;/' \
				"$rm_dir/rm_rtmodel.c" > "$c" ;;
			gunshadow-kept) sed 's/return gunshadow && !models;/return gunshadow \&\& (models >= 0);/' "$rm_dir/rm_rtmodel.c" > "$c" ;;
			weapon-flag) sed 's/p->weapon = (unsigned)rm_rtm_weapon_view(/p->weapon = 1u | (unsigned)rm_rtm_weapon_view(/' \
				"$rm_dir/rm_rtmodel.c" > "$c" ;;
			wall-alias) sed 's/return !alias && (mat != 3u)/return (alias >= 0) \&\& (mat != 3u)/' "$rm_dir/common/mtl_rtm_math.h" > "$h" ;;
			tag-no-normals) sed 's/if (!extra_ok)$/if (!extra_ok \&\& 0)/' "$rm_dir/rm_rtmodel.c" > "$c" ;;
			tag-bit) sed 's/(mtl_rtm_byte(c.a) - MTL_RTM_TAG_BASE) & MTL_RTM_FLAGS;/(mtl_rtm_byte(c.a) - MTL_RTM_TAG_BASE) \& 3u;/' \
				"$rm_dir/common/mtl_rtm_math.h" > "$h" ;;
			weapon-view-inv) sed 's/rm_weapon_projection(p, w->fovy, w->gunfov, w->hand, w->width/rm_projection(p, w->fovy, w->width/' \
				"$rm_dir/rm_rtmodel.c" > "$c" ;;
			nan-guard) sed 's/if (u > MTL_RTM_BITS_INF)$/if (0)/' "$rm_dir/common/mtl_rtm_math.h" > "$h" ;;
		esac
		r=0
		rtm_run "${tmp:?}/rtmm" || r=$?
		if cmp -s "$rm_dir/common/mtl_rtm_math.h" "$h" && cmp -s "$rm_dir/rm_rtmodel.c" "$c"; then
			bad "the model-light mutant ($m) changed nothing"
		elif [ "$r" -eq 1 ]; then
			cat "${tmp:?}/cc.out"
			bad "the model-light mutant ($m) does not compile, so it tests nothing"
		elif [ "$r" -eq 0 ]; then
			bad "tests/metal/rm_rtmodel_test.c passes the model-light mutant ($m)"
		else
			rtm_mutants=$((rtm_mutants + 1))
		fi
	done
	[ "$rtm_mutants" -eq 18 ] || bad "tests/metal/rm_rtmodel_test.c refused $rtm_mutants of the 18 model-light mutants"
fi
# slice 2's review: under Metal the colour index's guard reads the bit pattern by as_type, which fast math
# does not fold (the host test sees only the host's union); the index tests the pattern, not the float
if [ "$(grep -cxF '#define MTL_RTM_BITS(f) as_type<uint>(f)' "$rm_dir/common/mtl_rtm_math.h")" != 1 ] ||
	! awk '/^#ifdef __METAL_VERSION__$/ { m = 1; next } m && /^#define MTL_RTM_BITS\(/ { print; exit }' \
		"$rm_dir/common/mtl_rtm_math.h" | grep -qF 'as_type<uint>(f)' ||
	[ "$(grep -cxF '	const unsigned u = MTL_RTM_BITS(f);' "$rm_dir/common/mtl_rtm_math.h")" != 1 ]; then
	bad "common/mtl_rtm_math.h: the colour index under Metal does not test the bit pattern (as_type<uint>)"
fi
# slice 2: under Metal the minlight floor's test is s >= 0.1f, gl1's double test s > 0.1 (the host test holds
# the two the same for every float near 0.1); the host keeps the double test
if [ "$(grep -cxF '#define MTL_RTM_ABOVE(s) ((s) >= MTL_RTM_FLOOR)' "$rm_dir/common/mtl_rtm_math.h")" != 1 ] ||
	[ "$(grep -cxF '#define MTL_RTM_ABOVE(s) ((s) > MTL_RTM_FLOOR)' "$rm_dir/common/mtl_rtm_math.h")" != 1 ] ||
	! awk '/^#ifdef __METAL_VERSION__$/ { m = 1; next } m && /^#define MTL_RTM_ABOVE/ { print; exit }' \
		"$rm_dir/common/mtl_rtm_math.h" | grep -qF '>= MTL_RTM_FLOOR'; then
	bad "common/mtl_rtm_math.h: the minlight floor's test under Metal is not s >= 0.1f"
fi
seg 7.9 "; model light ${rtm:-host test}, $rtm_mutants of 18 mutants refused"

# the ray-traced presentation's data (TASK187, PM5): rm_rtlights.c alone, the entity-lump parser
# (an asset loader: every malformed case runs under the sanitizers when the compiler has them)
rtl=""
# no -fwrapv here: the lights parser must not overflow a signed integer, and UBSAN reports it only without it
# shellcheck disable=SC2086 # psan is a word list
if ! $cc -std=gnu99 -Wall -Wextra -Werror -g -O1 -fno-strict-aliasing ${psan:-} -o "${tmp:?}/rm_rtlights_test" tests/metal/rm_rtlights_test.c "$rm_dir/rm_rtlights.c" -lm \
	> "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	bad "tests/metal/rm_rtlights_test.c does not compile"
elif ! UBSAN_OPTIONS=print_stacktrace=1 "${tmp:?}/rm_rtlights_test" > "${tmp:?}/out" 2>&1; then
	cat "${tmp:?}/out"
	bad "tests/metal/rm_rtlights_test.c failed"
else
	rtl=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_rtlights_test: //')
	case $rtl in *", 0 failed") ;; *) bad "tests/metal/rm_rtlights_test.c printed no result line"; rtl="";; esac
fi
seg 8 "; RT data ${rtl:-host test}"

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
# Scratch mutants of rm_metalfx.c the test must fail (TASK109 slice 2): the effect reason dropped; its
# comparison inverted (an effect turned off resets, one turned on does not); the reset line's two
# fields swapped (the scaler's reasons named as RT7's and the other way). Each must compile and fail,
# and all three must be refused.
fx_mutants=0
if [ -n "$fx" ]; then
	mkdir "${tmp:?}/fxr"
	for fxr in effectdrop effectinvert lineswap; do
		case $fxr in
			effectdrop) sed 's/return RM_FX_RESET_EFFECT;/return 0;/' "$rm_dir/rm_metalfx.c" > "${tmp:?}/fxr/rm_metalfx.c" ;;
			effectinvert) sed 's/if (last->valid \&\& !last->rt \&\& rt)/if (last->valid \&\& last->rt \&\& !rt)/' \
				"$rm_dir/rm_metalfx.c" > "${tmp:?}/fxr/rm_metalfx.c" ;;
			lineswap) sed -e 's/^			scaler);$/			@SWAP@);/' -e 's/^			temporal);$/			scaler);/' \
				-e 's/^			@SWAP@);$/			temporal);/' "$rm_dir/rm_metalfx.c" > "${tmp:?}/fxr/rm_metalfx.c" ;;
		esac
		if cmp -s "$rm_dir/rm_metalfx.c" "${tmp:?}/fxr/rm_metalfx.c"; then
			bad "the MetalFX mutant ($fxr) changed nothing"
		# shellcheck disable=SC2086 # flags is a word list
		elif ! $cc $flags '-DYQ2OSTYPE="host"' '-DYQ2ARCH="host"' -I"$rm_dir" -o "${tmp:?}/fxr/t" tests/metal/rm_fx_test.c \
			"${tmp:?}/fxr/rm_metalfx.c" -lm > "${tmp:?}/fxr.out" 2>&1; then
			cat "${tmp:?}/fxr.out"
			bad "the MetalFX mutant ($fxr) does not compile, so it tests nothing"
		elif "${tmp:?}/fxr/t" > "${tmp:?}/fxr.out" 2>&1; then
			bad "tests/metal/rm_fx_test.c passes the MetalFX mutant ($fxr)"
		else
			fx_mutants=$((fx_mutants + 1))
		fi
	done
	[ "$fx_mutants" -eq 3 ] || bad "tests/metal/rm_fx_test.c refused $fx_mutants of the 3 MetalFX mutants"
fi
seg 9 "; MetalFX ${fx:-host test}, $fx_mutants reset mutants refused"

# Retro's texture upload and 480-line view (TASK185, PM3): common/mtl_retro_tex.h alone
retex=""
# shellcheck disable=SC2086 # flags is a word list
if ! $cc $flags -o "${tmp:?}/rm_retro_tex_test" tests/metal/rm_retro_tex_test.c -lm > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	bad "tests/metal/rm_retro_tex_test.c does not compile"
elif ! UBSAN_OPTIONS=print_stacktrace=1 "${tmp:?}/rm_retro_tex_test" > "${tmp:?}/out" 2>&1; then
	cat "${tmp:?}/out"
	bad "tests/metal/rm_retro_tex_test.c failed"
else
	retex=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_retro_tex_test: //')
	case $retex in *", 0 failed") ;; *) bad "tests/metal/rm_retro_tex_test.c printed no result line"; retex="";; esac
fi
seg 10.5 ", Retro textures ${retex:-host test}"

# the effect table (TASK179): rm_rtfx.c is built for the host with its entry points the recording stubs
# of tests/metal/rm_rtfx_host.h (RM_RTFX_HOST), so the gate, which takes its effect switches from it, and
# the table's test link no backend
fxhost="-DYQ2OSTYPE=\"host\" -DYQ2ARCH=\"host\" -DRM_RTFX_HOST -Itests/metal"

# the hardware gate of ray tracing (RT8, TASK60): rm_rtgate.c, with the effect table for its switches
rtg=""
# shellcheck disable=SC2086 # flags is a word list
if ! $cc $flags $fxhost -o "${tmp:?}/rm_rtgate_test" tests/metal/rm_rtgate_test.c "$rm_dir/rm_rtgate.c" "$rm_dir/rm_rtfx.c" -lm \
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
seg 10 "; ray-tracing gate ${rtg:-host test}"

# TASK190 slice 3: the menu's copy of RT-primary's scaler row (vidrules.c) against the renderer's (rm_rtgate.c,
# with rm_metalfx.c's view size), one binary over both. pmbuild <rtgate dir> <vidrules.c> <out>. Mutants it must
# refuse: the renderer's row dropped; the renderer's, then the menu's, capability rule dropped; the renderer's,
# then the menu's, size rule dropped; the menu's debug-view rule dropped (round 2, N2)
pmbuild() {
	# shellcheck disable=SC2086 # flags and fxhost are word lists
	# (vidrules.c's control tables leave their trailing fields to zero, which -Wextra names)
	$cc $flags $fxhost -Wno-missing-field-initializers -I"$rm_dir" -Isrc/client/menu -o "$3" tests/metal/rm_pres_menu_test.c "$1/rm_rtgate.c" \
		"$rm_dir/rm_rtfx.c" "$rm_dir/rm_metalfx.c" "$2" -lm
}
pm=""
pm_mutants=0
if ! pmbuild "$rm_dir" src/client/menu/vidrules.c "${tmp:?}/rm_pres_menu_test" > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	bad "tests/metal/rm_pres_menu_test.c does not compile"
elif ! UBSAN_OPTIONS=print_stacktrace=1 "${tmp:?}/rm_pres_menu_test" > "${tmp:?}/out" 2>&1; then
	cat "${tmp:?}/out"
	bad "tests/metal/rm_pres_menu_test.c failed: the menu and the renderer disagree on RT-primary's scaler"
else
	pm=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_pres_menu_test: //')
	case $pm in *", 0 failed") ;; *) bad "tests/metal/rm_pres_menu_test.c printed no result line"; pm="";; esac
fi
if [ -n "$pm" ]; then
	mkdir "${tmp:?}/pmm"
	for pmm in row rcaps mcaps rsize msize mshow; do
		cp "$rm_dir/rm_rtgate.c" src/client/menu/vidrules.c "${tmp:?}/pmm/"
		case $pmm in
			row) sed '/{ "r_metalfx", 2\.0f },/d' "$rm_dir/rm_rtgate.c" > "${tmp:?}/pmm/rm_rtgate.c" ;;
			# each keeps its parameter read, so -Werror's unused-parameter does not refuse it before the test runs
			rcaps) sed 's/(!temporal || show))/((void)temporal, show))/' "$rm_dir/rm_rtgate.c" > "${tmp:?}/pmm/rm_rtgate.c" ;;
			mcaps) sed 's/if (!(modemask \& VIDMFX_TEMPORAL))/if (!(modemask | VIDMFX_TEMPORAL))/' src/client/menu/vidrules.c \
				> "${tmp:?}/pmm/vidrules.c" ;;
			rsize) sed 's/\&\& !below)/\&\& ((void)below, 0))/' "$rm_dir/rm_rtgate.c" > "${tmp:?}/pmm/rm_rtgate.c" ;;
			msize) sed 's/else if (!(isfinite(scale) \&\& (scale >= 1.0f)))/else if (!(isfinite(scale) \&\& (scale >= 1.0f)) || 1)/' \
				src/client/menu/vidrules.c > "${tmp:?}/pmm/vidrules.c" ;;
			mshow) sed 's/else if ((e >= 0) \&\& VidRules_RTEffective/else if ((e >= 0) \&\& 0 \&\& VidRules_RTEffective/' \
				src/client/menu/vidrules.c > "${tmp:?}/pmm/vidrules.c" ;;
		esac
		if cmp -s "$rm_dir/rm_rtgate.c" "${tmp:?}/pmm/rm_rtgate.c" && cmp -s src/client/menu/vidrules.c "${tmp:?}/pmm/vidrules.c"; then
			bad "the menu-parity mutant ($pmm) changed nothing"
		elif ! pmbuild "${tmp:?}/pmm" "${tmp:?}/pmm/vidrules.c" "${tmp:?}/pmm/t" > "${tmp:?}/pmm.out" 2>&1; then
			cat "${tmp:?}/pmm.out"
			bad "the menu-parity mutant ($pmm) does not compile"
		elif "${tmp:?}/pmm/t" > "${tmp:?}/pmm.out" 2>&1; then
			bad "tests/metal/rm_pres_menu_test.c passes the menu-parity mutant ($pmm)"
		else
			pm_mutants=$((pm_mutants + 1))
		fi
	done
	[ "$pm_mutants" = 6 ] || bad "tests/metal/rm_pres_menu_test.c refuses $pm_mutants of its 6 mutants"
fi
seg 10.2 "; menu and renderer scaler rule ${pm:-host test}, $pm_mutants parity mutants refused"

# the effect table (TASK179): tests/metal/rm_rtfx_test.c over rm_rtfx.c and rm_rtgate.c, the order every walk
# takes, each row's switch against the gate's table, and the pipeline rule of rmb_gbuf_pipe3d. fxbuild <dir>
# <out>: the test built over rm_rtfx.c and rm_rtgate.c of <dir> (a scratch copy finds the headers by -I)
fxbuild() {
	# shellcheck disable=SC2086 # flags and fxhost are word lists
	$cc $flags $fxhost -I"$rm_dir" -o "$2" tests/metal/rm_rtfx_test.c "$1/rm_rtfx.c" "$1/rm_rtgate.c" -lm
}
rtfx=""
if ! fxbuild "$rm_dir" "${tmp:?}/rm_rtfx_test" > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	bad "tests/metal/rm_rtfx_test.c does not compile"
elif ! UBSAN_OPTIONS=print_stacktrace=1 "${tmp:?}/rm_rtfx_test" > "${tmp:?}/out" 2>&1; then
	cat "${tmp:?}/out"
	bad "tests/metal/rm_rtfx_test.c failed"
else
	rtfx=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_rtfx_test: //')
	case $rtfx in *", 0 failed") ;; *) bad "tests/metal/rm_rtfx_test.c printed no result line"; rtfx="";; esac
fi
seg 11 ", effect table ${rtfx:-host test}"
# Scratch mutants the test must fail: two effects swapped in the order; the AO's switch changed in the
# table but not in the gate, then in the gate but not in the table; the raster's surface choice inverted;
# the forward pipeline asked for every pipe but the blended world face's; the AO row's show and post
# entries swapped; the water's frame entry NULL; the post walk emptied; the resolve walk going on past
# the first yes (with one resolve row: losing it); the wanted walk going on past the first yes
fxh_mutants=0
if [ -n "$rtfx" ]; then
	mkdir "${tmp:?}/fxm"
	for fxm in order tablesw gatesw surf forward aoswap wframe postempty resolvego wantedgo; do
		cp "$rm_dir/rm_rtfx.c" "$rm_dir/rm_rtgate.c" "${tmp:?}/fxm/"
		case $fxm in
			order) sed 's/RM_RTFX_DIRECT, RM_RTFX_MODELS, RM_RTFX_SHADOWS, RM_RTFX_AO, RM_RTFX_SUN, RM_RTFX_WATER/RM_RTFX_DIRECT, RM_RTFX_MODELS, RM_RTFX_SHADOWS, RM_RTFX_SUN, RM_RTFX_AO, RM_RTFX_WATER/' \
				"$rm_dir/rm_rtfx.c" > "${tmp:?}/fxm/rm_rtfx.c" ;;
			tablesw) sed 's/"ao", "r_rt_ao"/"ao", "r_rt_ambient"/' "$rm_dir/rm_rtfx.c" > "${tmp:?}/fxm/rm_rtfx.c" ;;
			gatesw) sed 's/^	"r_rt_ao",$/	"r_rt_ambient",/' "$rm_dir/rm_rtgate.c" > "${tmp:?}/fxm/rm_rtgate.c" ;;
			surf) sed 's/surf ? RM_RTFX_PIPE_RASTER_SURF : RM_RTFX_PIPE_RASTER;/surf ? RM_RTFX_PIPE_RASTER : RM_RTFX_PIPE_RASTER_SURF;/' \
				"$rm_dir/rm_rtfx.c" > "${tmp:?}/fxm/rm_rtfx.c" ;;
			forward) sed 's/if ((pipe == RM_PIPE_WORLD_TEXC_BLEND) \&\& forward/if ((pipe != RM_PIPE_WORLD_TEXC_BLEND) \&\& forward/' \
				"$rm_dir/rm_rtfx.c" > "${tmp:?}/fxm/rm_rtfx.c" ;;
			aoswap) sed 's/FX(rmb_rtao_fx_trace), NULL, FX(rmb_rtao_fx_show), NULL,/FX(rmb_rtao_fx_trace), NULL, NULL, FX(rmb_rtao_fx_show),/' \
				"$rm_dir/rm_rtfx.c" > "${tmp:?}/fxm/rm_rtfx.c" ;;
			wframe) sed 's/FX(RM_RTW_Frame)/NULL/' "$rm_dir/rm_rtfx.c" > "${tmp:?}/fxm/rm_rtfx.c" ;;
			postempty) awk '/^rm_rtfx_post\(/ { f = 1 } { print } f && /^\tint k;$/ { print "\n\treturn;"; f = 0 }' \
				"$rm_dir/rm_rtfx.c" > "${tmp:?}/fxm/rm_rtfx.c" ;;
			resolvego) awk '/^rm_rtfx_resolve\(/ { f = 1 } f && /return 1;/ { sub(/return 1;/, "continue;"); f = 0 } { print }' \
				"$rm_dir/rm_rtfx.c" > "${tmp:?}/fxm/rm_rtfx.c" ;;
			wantedgo) awk '/^rm_rtfx_wanted\(/ { f = 1 } f && /^\tint k;$/ { $0 = "\tint k, r = 0;" }
				f && /return 1;/ { sub(/return 1;/, "r = 1;") } f && /return 0;/ { sub(/return 0;/, "return r;") }
				/^}/ { f = 0 } { print }' "$rm_dir/rm_rtfx.c" > "${tmp:?}/fxm/rm_rtfx.c" ;;
		esac
		if cmp -s "$rm_dir/rm_rtfx.c" "${tmp:?}/fxm/rm_rtfx.c" && cmp -s "$rm_dir/rm_rtgate.c" "${tmp:?}/fxm/rm_rtgate.c"; then
			bad "the effect-table mutant ($fxm) changed nothing"
		elif ! fxbuild "${tmp:?}/fxm" "${tmp:?}/fxm/t" > "${tmp:?}/fxm.out" 2>&1; then
			cat "${tmp:?}/fxm.out"
			bad "the effect-table mutant ($fxm) does not compile"
		elif "${tmp:?}/fxm/t" > "${tmp:?}/fxm.out" 2>&1; then
			bad "tests/metal/rm_rtfx_test.c passes the effect-table mutant ($fxm)"
		else
			fxh_mutants=$((fxh_mutants + 1))
		fi
	done
fi
seg 12 ", $fxh_mutants table mutants refused, no effect outside it in rm_main.c or rmb_gbuf.m"

# the preset to table script (TASK195, slice 2): the rows it prints for a fixture preset, compiled
# into rm_rtgate.c as the enhanced overlay, must make rm_pres_value return the preset's values; a
# fixture with a line that is no "set" and one with a name outside the tuning cvars must be refused
pt=scripts/rt/preset-to-table.sh
ptfix=tests/fixtures/rt-preset
if ! "$pt" "$ptfix/good.cfg" --as enhanced > "${tmp:?}/pt.h" 2> "${tmp:?}/pt.err"; then
	cat "${tmp:?}/pt.err"
	bad "$pt refused $ptfix/good.cfg"
elif ! "$pt" "$ptfix/good.cfg" > "${tmp:?}/pt.def" 2>&1 || ! grep -q '^r_rt_ao_range: 128 -> 256$' "${tmp:?}/pt.def"; then
	bad "$pt --as defaults did not list r_rt_ao_range: 128 -> 256 for $ptfix/good.cfg"
else
	ptvals=$(sed -n 's/^set \([A-Za-z0-9_]*\) "\([0-9.]*\)"$/\1=\2/p' "$ptfix/good.cfg")
	# shellcheck disable=SC2086 # flags is a word list; ptvals is name=value pairs without blanks
	if ! $cc $flags $fxhost -include "${tmp:?}/pt.h" -o "${tmp:?}/rm_pres_table_test" tests/metal/rm_pres_table_test.c \
		"$rm_dir/rm_rtgate.c" "$rm_dir/rm_rtfx.c" -lm > "${tmp:?}/cc.out" 2>&1; then
		cat "${tmp:?}/cc.out"
		bad "tests/metal/rm_pres_table_test.c does not compile with the rows of $pt"
	elif ! "${tmp:?}/rm_pres_table_test" $ptvals > "${tmp:?}/out" 2>&1; then
		cat "${tmp:?}/out"
		bad "rm_pres_value does not return the preset's values under the rows of $pt"
	fi
fi
# a preset with r_hdr and r_hdr_peak: the overlay must not carry them, the defaults output must
if ! "$pt" "$ptfix/hdr.cfg" --as enhanced > "${tmp:?}/pt.hdr" 2> /dev/null || grep -q 'r_hdr' "${tmp:?}/pt.hdr" ||
	! grep -q 'r_rt_ao_range' "${tmp:?}/pt.hdr"; then
	bad "$pt --as enhanced put r_hdr or r_hdr_peak into the overlay of $ptfix/hdr.cfg"
elif ! "$pt" "$ptfix/hdr.cfg" 2>&1 | grep -q '^r_hdr: 0 -> 1$'; then
	bad "$pt --as defaults did not list r_hdr: 0 -> 1 for $ptfix/hdr.cfg"
fi
for ptbad in badline badname; do
	if "$pt" "$ptfix/$ptbad.cfg" > "${tmp:?}/pt.out" 2>&1; then
		bad "$pt accepted $ptfix/$ptbad.cfg"
	elif ! grep -q "$ptbad.cfg:2: refused" "${tmp:?}/pt.out"; then
		bad "$pt refused $ptfix/$ptbad.cfg without naming its line"
	fi
done

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
seg 15 ", ${rtg_cvars:-no} r_rt_* cvars classified, $rtg_mutants table mutants refused"

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
seg 16 "; $mutants vocabulary mutants refused"

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
seg 17 "; 3 flush points, $flush_mutants flush mutants refused"

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
seg 19 "; 5 gate-seat rules, $seat_mutants seat mutants refused"

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
	"rm_rtwater.metal rm_rtw_ss_at mtl_rtw_choose" "rm_rtwater.metal rm_rtw_trace rm_rtw_ss_at" \
	"rm_rtao.metal rm_rtao_temporal mtl_rtt_reproject" "rm_rtao.metal rm_rtao_temporal mtl_rtt_reject" \
	"rm_rtao.metal rm_rtao_temporal mtl_rtt_blend" "rm_rtao.metal rm_rtao_temporal mtl_rtt_count" \
	"rm_rtshadow.metal rm_rts_temporal mtl_rtt_reproject" "rm_rtshadow.metal rm_rts_temporal mtl_rtt_reject" \
	"rm_rtshadow.metal rm_rts_temporal mtl_rtt_blend" "rm_rtshadow.metal rm_rts_temporal mtl_rtt_count" \
	"rm_rtshadow.metal rm_rts_temporal mtl_rtt_lights" "rm_rtshadow.metal rm_rts_temporal rm_rts_shade" \
	"rm_rtshadow.metal rm_rts_temporal rm_rts_finish" \
	"rm_rtsun.metal rm_rtsun_temporal mtl_rtt_reproject" "rm_rtsun.metal rm_rtsun_temporal mtl_rtt_reject" \
	"rm_rtsun.metal rm_rtsun_temporal mtl_rtt_blend" "rm_rtsun.metal rm_rtsun_temporal mtl_rtt_count" \
	"rm_rtdenoise.metal rm_rtd_filter mtl_rtd_weight" "rm_rtdenoise.metal rm_rtd_filter mtl_rtd_add" \
	"rm_rtdenoise.metal rm_rtd_filter mtl_rtd_result" "rm_rtdenoise.metal rm_rtao_denoise rm_rtd_filter" \
	"rm_rtdenoise.metal rm_rtsun_denoise rm_rtd_filter" \
	"rm_rtwdenoise.metal rm_rtw_denoise mtl_rtd_rweight" "rm_rtwdenoise.metal rm_rtw_denoise mtl_rtd_add" \
	"rm_rtwdenoise.metal rm_rtw_denoise mtl_rtd_rresult"; do
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
seg 20 "; $kcall_pairs kernel calls, $kcall_mutants kernel-call mutants refused"

# RT7's denoiser (TASK59 slice 2): the weight rule of common/mtl_rtd_math.h, run by
# tests/metal/rm_rtdenoise_test.c on the real header and on mutants it must fail (each must compile:
# the depth guide, the normal guide, the material, the count's cut at 16 and its scale dropped one at
# a time, the sums' guard against a weight or value that is not finite dropped); then the kernels'
# calls in rm_rtdenoise.metal pinned as text, each with mutants the pin must refuse: the depth guide
# (the neighbour's distance replaced by the pixel's own), the count scale (the strength's count
# replaced by 1), and the denoised value fed back into a history (a history written by the
# denoiser). With the Metal compiler each kernel mutant must also compile, and the real file with it.
rtd=""
rtd_tree() { # <dir>: a scratch tree of the test and its headers
	rm -rf "${1:?}"
	mkdir -p "$1/src/client/refresh/metal/common" "$1/tests/metal"
	cp "$rm_dir/common/mtl_rtd_math.h" "$rm_dir/common/mtl_rtao_math.h" "$rm_dir/common/mtl_shader_math.h" \
		"$1/src/client/refresh/metal/common/"
	cp tests/metal/rm_rtdenoise_test.c "$1/tests/metal/"
}
rtd_build() { # <tree>: 0 when the tree's test builds
	# shellcheck disable=SC2086 # flags is a word list
	$cc $flags -o "$1/t" "$1/tests/metal/rm_rtdenoise_test.c" -lm > "${tmp:?}/cc.out" 2>&1
}
rtd_run() { # <tree>: 0 when the tree's test builds and passes
	rtd_build "$1" && UBSAN_OPTIONS=print_stacktrace=1 "$1/t" > "${tmp:?}/out" 2>&1
}
rtd_tree "${tmp:?}/rtd"
if ! rtd_run "${tmp:?}/rtd"; then
	cat "${tmp:?}/cc.out" "${tmp:?}/out"
	bad "tests/metal/rm_rtdenoise_test.c fails on the real files"
else
	rtd=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_rtdenoise_test: //')
	case $rtd in *", 0 failed") ;; *) bad "tests/metal/rm_rtdenoise_test.c printed no result line"; rtd="";; esac
fi
rtd_mutants=0
for m in depth normal mat countcut countflat finite rdepth rok; do
	rtd_tree "${tmp:?}/rtdm"
	h=${tmp:?}/rtdm/src/client/refresh/metal/common/mtl_rtd_math.h
	case $m in
		depth) sed 's/return (w > 0.0f) \&\& (w <= 1.0f) ? w : 0.0f;/return 1.0f + 0.0f * w;/' ;;
		normal) sed 's|const float w = (c - MTL_RTD_NORMAL_MIN) / (1.0f - MTL_RTD_NORMAL_MIN);|const float w = 1.0f + 0.0f * c;|' ;;
		mat) sed 's/if ((m != mn) || !(cn >= 1.0f))/if (((m != mn) \&\& 0) || !(cn >= 1.0f))/' ;;
		countcut) sed 's/if (!(n >= MTL_RTD_COUNT_FULL) || !(n < MTL_RTD_COUNT_NONE))/if (!(n >= MTL_RTD_COUNT_FULL) || 0)/' ;;
		countflat) sed 's|return (MTL_RTD_COUNT_NONE - n) / (MTL_RTD_COUNT_NONE - MTL_RTD_COUNT_FULL);|return 1.0f + 0.0f * n;|' ;;
		finite) sed 's/if (!(w > 0.0f) || !isfinite(w) || !isfinite(v))/if (!(w > 0.0f))/' ;;
		# slice 2b: the reflections' depth guide dropped; a neighbour without a reflection blurred in
		rdepth) sed 's/w = mtl_rtd_rtap(dx, dy) \* mtl_rtd_depth_w(d, dn);/w = mtl_rtd_rtap(dx, dy) * (1.0f + 0.0f * (d - dn));/' ;;
		rok) sed 's/if (!ok)$/if (!ok \&\& 0)/' ;;
	esac < "$rm_dir/common/mtl_rtd_math.h" > "$h"
	if cmp -s "$rm_dir/common/mtl_rtd_math.h" "$h"; then
		bad "the denoiser mutant ($m) changed nothing"
	elif ! rtd_build "${tmp:?}/rtdm"; then
		cat "${tmp:?}/cc.out"
		bad "the denoiser mutant ($m) does not compile, so it tests nothing"
	elif rtd_run "${tmp:?}/rtdm"; then
		bad "tests/metal/rm_rtdenoise_test.c passes the denoiser mutant ($m)"
	else
		rtd_mutants=$((rtd_mutants + 1))
	fi
done
rtd_metal=""
if [ "$(uname -s)" = Darwin ] && command -v xcrun >/dev/null 2>&1 && xcrun -f metal >/dev/null 2>&1; then
	rtd_metal=1
fi
rtd_compile() { # <file.metal>: 0 when it compiles as the Makefile compiles rm_rtdenoise.metal
	xcrun -sdk macosx metal -c -I"$rm_dir" "$1" -o "${tmp:?}/rtd.air" > "${tmp:?}/metal.out" 2>&1
}
rtd_body() { # <file> <function>: the function's body, its name at the start of a line up to the next "}" in column 0
	strip "$1" | awk -v fn="$2" '$0 ~ "^" fn "\\(" { b = 1 } b && /^}/ { b = 0 } b { print }' | tr -s ' \t' '  '
}
rtd_pin() { # <file> <pin>: 0 when the file holds the pin
	case $2 in
		depth) rtd_body "$1" rm_rtd_filter | grep -qF 'mtl_rtd_weight(dx, dy, hc.y, hn.y, nc, nn, mc, mn, hn.w)' ;;
		count) rtd_body "$1" rm_rtd_filter | grep -qF 'return mtl_rtd_result(acc, hc.x, hc.w);' ;;
		nohist)
			# the denoiser writes the light alone: no write but light_out's, no texture it can write but light_out
			! strip "$1" | grep -E '\.write\(|access::(read_)?write>' | grep -vqE 'light_out' ;;
	esac
}
rtd_kpins=0 rtd_kmut=0
dn=$rm_dir/rm_rtdenoise.metal
if [ -n "$rtd_metal" ] && ! rtd_compile "$dn"; then
	cat "${tmp:?}/metal.out"
	bad "rm_rtdenoise.metal does not compile"
fi
for p in depth count nohist; do
	if ! rtd_pin "$dn" "$p"; then
		bad "rm_rtdenoise.metal does not hold the denoiser's $p pin"
		continue
	fi
	rtd_kpins=$((rtd_kpins + 1))
	case $p in
		depth) sed 's/hc\.y, hn\.y, nc, nn,/hc.y, hc.y, nc, nn,/' "$dn" ;;
		count) sed 's/return mtl_rtd_result(acc, hc\.x, hc\.w);/return mtl_rtd_result(acc, hc.x, 1.0f);/' "$dn" ;;
		nohist) awk '/^rm_rtao_denoise\(/ { b = 1 } b && /^}/ { b = 0 }
			b && /texture\(8\)/ { print; print "\t\ttexture2d<float, access::write> hist_out [[texture(9)]],"; next }
			b && /light_out\.write\(/ { print; print "\t\t\thist_out.write(float4(ao, hc.y, hc.z, hc.w), px);"; next }
			{ print }' "$dn" ;;
	esac > "${tmp:?}/rtd-kernel.metal"
	if cmp -s "$dn" "${tmp:?}/rtd-kernel.metal"; then
		bad "the denoiser kernel mutant ($p) changed nothing"
	elif [ -n "$rtd_metal" ] && ! rtd_compile "${tmp:?}/rtd-kernel.metal"; then
		cat "${tmp:?}/metal.out"
		bad "the denoiser kernel mutant ($p) does not compile, so it tests nothing"
	elif rtd_pin "${tmp:?}/rtd-kernel.metal" "$p"; then
		bad "the denoiser's $p pin passes its mutant"
	else
		rtd_kmut=$((rtd_kmut + 1))
	fi
done
# slice 2b: the reflections' blur, rm_rtwdenoise.metal. Its call of mtl_rtd_rweight and the line that
# computes dn pinned with the depth guide's two mutants (the neighbour's distance replaced by the
# pixel's own in the call, rdepth, and dn never computed, rdist, round 2), and no history: its
# textures are the four it reads and writes, and refl_out is the one it writes. A mutant that reads a
# history (a fifth texture mixed into the blur) and one that writes one must be refused. Round 3: the
# class test of a tap, rm_rtd_rok, pinned whole (water, not a miss, the same MTL_RTW_CLASS_TRANS layer
# as the pixel), with mutants that drop the same-layer term (rlayer) and the not-a-miss term (rmiss).
rtw_rok='return (cn & MTL_RTW_CLASS_WATER) && ((cn & MTL_RTW_CLASS_FROM) != MTL_RTW_MISS) && ((cn & MTL_RTW_CLASS_TRANS) == (cc & MTL_RTW_CLASS_TRANS));'
rtw_pin() { # <file> <pin>: 0 when the file holds the pin
	case $2 in
		rlayer | rmiss)
			rtd_body "$1" rm_rtd_rok | tr -d '\n' | tr -s ' ' | grep -qF "$rtw_rok" ;;
		# round 2: the pin also holds how dn is computed, so a neighbour's distance never taken fails it
		rdepth | rdist)
			rtd_body "$1" rm_rtw_denoise | grep -qF 'dn = ok ? rm_rtd_rdist(v, depth, wdist, uint2(q), cn) : dc;' &&
				rtd_body "$1" rm_rtw_denoise | grep -qF 'mtl_rtd_rweight(dx, dy, dc, dn, ok)' ;;
		histread | histwrite)
			[ "$(rtd_body "$1" rm_rtw_denoise | grep -c '\[\[texture(')" = 4 ] &&
				! strip "$1" | grep -E '\.write\(|access::(read_)?write>' | grep -vqE 'refl_out' ;;
	esac
}
wdn=$rm_dir/rm_rtwdenoise.metal
if [ -n "$rtd_metal" ] && ! rtd_compile "$wdn"; then
	cat "${tmp:?}/metal.out"
	bad "rm_rtwdenoise.metal does not compile"
fi
for p in rdepth rdist histread histwrite rlayer rmiss; do
	if ! rtw_pin "$wdn" "$p"; then
		bad "rm_rtwdenoise.metal does not hold the reflections' blur's $p pin"
		continue
	fi
	rtd_kpins=$((rtd_kpins + 1))
	case $p in
		rlayer) sed 's/((cn & MTL_RTW_CLASS_TRANS) == (cc & MTL_RTW_CLASS_TRANS))/true/' "$wdn" ;;
		rmiss) sed 's/ && ((cn & MTL_RTW_CLASS_FROM) != MTL_RTW_MISS)//' "$wdn" ;;
		rdepth) sed 's/mtl_rtd_rweight(dx, dy, dc, dn, ok)/mtl_rtd_rweight(dx, dy, dc, dc, ok)/' "$wdn" ;;
		rdist) sed 's/dn = ok ? rm_rtd_rdist(v, depth, wdist, uint2(q), cn) : dc;/dn = dc;/' "$wdn" ;;
		histread) awk '/^rm_rtw_denoise\(/ { b = 1 } b && /^}/ { b = 0 }
			b && /texture\(3\)/ { print; print "\t\ttexture2d<float, access::read> hist [[texture(4)]],"; next }
			b && /n = refl\.read\(uint2\(q\)\);/ { print; print "\t\t\tn = 0.5f * (n + hist.read(uint2(q)));"; next }
			{ print }' "$wdn" ;;
		histwrite) awk '/^rm_rtw_denoise\(/ { b = 1 } b && /^}/ { b = 0 }
			b && /texture\(3\)/ { print; print "\t\ttexture2d<float, access::write> hist_out [[texture(4)]],"; next }
			b && /^\trefl_out\.write\(float4/ { print; print "\thist_out.write(c, px);"; next }
			{ print }' "$wdn" ;;
	esac > "${tmp:?}/rtd-kernel.metal"
	if cmp -s "$wdn" "${tmp:?}/rtd-kernel.metal"; then
		bad "the reflections' blur's kernel mutant ($p) changed nothing"
	elif [ -n "$rtd_metal" ] && ! rtd_compile "${tmp:?}/rtd-kernel.metal"; then
		cat "${tmp:?}/metal.out"
		bad "the reflections' blur's kernel mutant ($p) does not compile, so it tests nothing"
	elif rtw_pin "${tmp:?}/rtd-kernel.metal" "$p"; then
		bad "the reflections' blur's $p pin passes its mutant"
	else
		rtd_kmut=$((rtd_kmut + 1))
	fi
done
# TASK190 slice 2 (PM8, the card's default 10): the water scene turns the blur on with r_rt_temporal
# alone, so it runs under r_presentation 3 too (slice 2b's RT-primary gate, rm_rtt_wblur, is gone).
# Mutants: the RT-primary gate put back, the call commented out.
wsc=$rm_dir/rm_rtwaterscene.c
wsc_call='rmb_rtw_set_blur(RM_RTT_On());'
wsc_pin() { # <file>: 0 when the scene runs the blur with r_rt_temporal alone, under every presentation
	[ "$(strip "$1" | grep -cF "$wsc_call")" = 1 ] && ! strip "$1" | grep -q 'rmb_rtw_set_blur(.*RM_PRES_'
}
wsc_mut=0
if ! wsc_pin "$wsc" || grep -rqw 'rm_rtt_wblur' "$rm_dir"; then
	bad "rm_rtwaterscene.c does not run the reflections' blur under every presentation ($wsc_call)"
else
	for m in gate commented; do
		case $m in
			gate) sed 's/rmb_rtw_set_blur(RM_RTT_On());/rmb_rtw_set_blur(RM_RTT_On() \&\& (RM_PresentationMode() != RM_PRES_RTPRIMARY));/' "$wsc" ;;
			commented) sed 's|^\(	rmb_rtw_set_blur(RM_RTT_On.*\)$|	/* \1 */|' "$wsc" ;;
		esac > "${tmp:?}/rtw-scene.c"
		if cmp -s "$wsc" "${tmp:?}/rtw-scene.c"; then
			bad "the reflections' blur's presentation mutant ($m) changed nothing"
		elif wsc_pin "${tmp:?}/rtw-scene.c"; then
			bad "the reflections' blur's presentation pin passes its mutant ($m)"
		else
			wsc_mut=$((wsc_mut + 1))
		fi
	done
fi
[ "$wsc_mut" = 2 ] || bad "the reflections' blur's presentation pin refused $wsc_mut of its 2 mutants"
seg 5.6 "; denoiser ${rtd:-host test}, $rtd_mutants rule mutants, $rtd_kpins kernel pins with $rtd_kmut kernel mutants refused${rtd_metal:+ and compiled}, the blur under every presentation pinned with $wsc_mut mutants refused"
# slice 2b, round 3: r_rt_temporal through the A/B layer. It is a tuning row, so r_rt_ab's B side carries
# it, and both of its sites in rm_rtaoscene.c, the gate (RM_RTT_On) and the view's step (RM_RTT_ViewFor's
# alpha), must read it through RM_Effect. tests/metal/rm_rttab_test.c runs the real text of RM_Effect
# (rm_main.c), RM_RTT_On and the alpha line, copied into its rttab_body.c, over the real rm_ab_value and
# rm_rtt_alpha: a B side holding 0 turns the accumulation off, one holding 0.9 with A at 0 turns it on.
# Mutants: either site restored to the direct read of r_rt_temporal->value; each must build and fail it.
# TASK190 slice 3 adds RM_PresBelow (rm_main.c) and rm_view_rect (rm_pure.c) to rttab_body.c, over the real
# rm_fx_scale (rm_metalfx.c), and a third mutant: RM_PresBelow without its early return for RDF_NOWORLDMODEL.
rttab_fn() { # <file> <function>: the function's definition, its return type's line to the "}" in column 0
	awk -v fn="$2" 'b { print; if (/^}/) exit } $0 ~ "^" fn "\\(" { print prev; print; b = 1 } { prev = $0 }' "$1"
}
rttab_tree() { # <dir> <rm_rtaoscene.c> [<rm_main.c>]: rttab_body.c in <dir> from rm_main.c and the scene file given
	rm -rf "${1:?}"
	mkdir -p "$1"
	al=$(awk '/^RM_RTT_ViewFor\(/ { b = 1 } b && /^}/ { b = 0 } b && /^\talpha = / { print }' "$2")
	[ -n "$al" ] && [ "$(printf '%s\n' "$al" | wc -l | tr -d ' ')" = 1 ] || return 1
	{
		rttab_fn "${3:-$rm_dir/rm_main.c}" RM_Effect
		rttab_fn "${3:-$rm_dir/rm_main.c}" RM_PresBelow
		rttab_fn "$rm_dir/rm_pure.c" rm_view_rect
		rttab_fn "$2" RM_RTT_On
		printf '%s\n' '' 'static float' 'rttab_view_alpha(void)' '{' '	float alpha;' '' "$al" '	return alpha;' '}'
	} > "$1/rttab_body.c"
	grep -q '^RM_Effect(' "$1/rttab_body.c" && grep -q '^RM_PresBelow(' "$1/rttab_body.c" &&
		grep -q '^rm_view_rect(' "$1/rttab_body.c" && grep -q '^RM_RTT_On(' "$1/rttab_body.c"
}
rttab_build() { # <dir>: 0 when the test builds over the dir's rttab_body.c
	# shellcheck disable=SC2086 # flags and fxhost are word lists
	$cc $flags $fxhost -I"$1" -o "$1/t" tests/metal/rm_rttab_test.c "$rm_dir/rm_rtgate.c" "$rm_dir/rm_rtfx.c" \
		"$rm_dir/rm_rttemporal.c" "$rm_dir/rm_metalfx.c" -lm > "${tmp:?}/cc.out" 2>&1
}
rttab_run() { # <dir>: 0 when the test builds and passes
	rttab_build "$1" && UBSAN_OPTIONS=print_stacktrace=1 "$1/t" > "${tmp:?}/out" 2>&1
}
rttab="" rttab_mut=0
aos=$rm_dir/rm_rtaoscene.c
if ! rttab_tree "${tmp:?}/rttab" "$aos"; then
	bad "RM_Effect, RM_RTT_On or RM_RTT_ViewFor's one alpha line was not found for tests/metal/rm_rttab_test.c"
elif ! rttab_run "${tmp:?}/rttab"; then
	cat "${tmp:?}/cc.out" "${tmp:?}/out"
	bad "tests/metal/rm_rttab_test.c fails on the real files"
else
	rttab=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_rttab_test: //')
	case $rttab in *", 0 failed") ;; *) bad "tests/metal/rm_rttab_test.c printed no result line"; rttab="";; esac
	for m in ondirect viewdirect; do
		case $m in
			ondirect) sed 's/return rm_rtt_alpha(r_rt_temporal ? RM_Effect(r_rt_temporal) : 0\.0f) > 0\.0f;/return rm_rtt_alpha(r_rt_temporal ? r_rt_temporal->value : 0.0f) > 0.0f;/' "$aos" ;;
			viewdirect) sed 's/alpha = rm_rtt_alpha(r_rt_temporal ? RM_Effect(r_rt_temporal) : 0\.0f);/alpha = rm_rtt_alpha(r_rt_temporal ? r_rt_temporal->value : 0.0f);/' "$aos" ;;
		esac > "${tmp:?}/rttab-scene.c"
		if cmp -s "$aos" "${tmp:?}/rttab-scene.c"; then
			bad "the A/B temporal mutant ($m) changed nothing"
		elif ! rttab_tree "${tmp:?}/rttabm" "${tmp:?}/rttab-scene.c" || ! rttab_build "${tmp:?}/rttabm"; then
			cat "${tmp:?}/cc.out"
			bad "the A/B temporal mutant ($m) does not build, so it tests nothing"
		elif rttab_run "${tmp:?}/rttabm"; then
			bad "tests/metal/rm_rttab_test.c passes the A/B temporal mutant ($m)"
		else
			rttab_mut=$((rttab_mut + 1))
		fi
	done
	# TASK190 slice 3, I1: RM_PresBelow without its early return for the menu's player model (RDF_NOWORLDMODEL),
	# which then reads r_metalfx 0 between two world views reading 2; it must build and fail the two-view case
	sed '/^RM_PresBelow(/,/^}/s/if (fd->rdflags & RDF_NOWORLDMODEL)/if (0)/' "$rm_dir/rm_main.c" > "${tmp:?}/rttab-main.c"
	if cmp -s "$rm_dir/rm_main.c" "${tmp:?}/rttab-main.c"; then
		bad "the two-view mutant of RM_PresBelow changed nothing"
	elif ! rttab_tree "${tmp:?}/rttabm" "$aos" "${tmp:?}/rttab-main.c" || ! rttab_build "${tmp:?}/rttabm"; then
		cat "${tmp:?}/cc.out"
		bad "the two-view mutant of RM_PresBelow does not build, so it tests nothing"
	elif rttab_run "${tmp:?}/rttabm"; then
		bad "tests/metal/rm_rttab_test.c passes the two-view mutant of RM_PresBelow"
	elif ! grep -q '^FAIL .*model_view' "${tmp:?}/out"; then
		cat "${tmp:?}/out"
		bad "tests/metal/rm_rttab_test.c refuses the two-view mutant of RM_PresBelow, but not on the two-view case"
	else
		rttab_mut=$((rttab_mut + 1))
	fi
	[ "$rttab_mut" = 3 ] || bad "tests/metal/rm_rttab_test.c refuses $rttab_mut of its 3 mutants"
fi
seg 5.65 "; temporal through A/B ${rttab:-host test}, $rttab_mut mutants refused"

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
		'-DYQ2OSTYPE="host"' '-DYQ2ARCH="host"' -o "$2" tests/metal/rmb_cs_probe.m "$rm_dir/rm_pure.c" "$rm_dir/rm_metalfx.c" \
		"$rm_dir/common/mtl_matrix.c" "$rm_dir/common/mtl_ring.c" src/common/shared/shared.c \
		-framework Foundation -framework Metal -framework QuartzCore -framework CoreGraphics -weak_framework MetalFX -lm > "${tmp:?}/probe.out" 2>&1; then
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
	[ "${CHECK_REF_METAL_DEVICE_REQUIRE:-0}" != 1 ] || bad "CHECK_REF_METAL_DEVICE_REQUIRE=1 and the colour-space probe cannot run: not macOS"
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
		[ "${CHECK_REF_METAL_DEVICE_REQUIRE:-0}" != 1 ] || bad "CHECK_REF_METAL_DEVICE_REQUIRE=1 and the colour-space probe found no Metal device"
		echo "check-ref-metal: colour-space probe skipped: no Metal device"
	else
		probe=$(tail -n 1 "${tmp:?}/probe.out" | sed 's/^rmb_cs_probe: //')
		case $probe in *", 0 failed") ;; *) bad "tests/metal/rmb_cs_probe.m printed no result line"; probe="probe skipped";; esac
		# the mutants of rmb_device.m and rmb_resource.m, each of which must fail the probe: 1 -> 0
		# sets NULL, 1 -> 0 leaves the colour space on the layer, detach leaves it too; FIX133: a
		# float16 variant that was not built hands back the stock pipeline (fmtstock), a drawable
		# nothing wrote is presented (unwritten), the frame of a failed variant is presented
		# (failframe), rmb_attach clears the failure while its caches stay (hdrattach), the close
		# empties the caches and keeps the failure (hdrkept); TASK196 HD4, rmb_metalfx.m: the spatial
		# scaler left linear under r_hdr 1 (fxlinear), the temporal output left BGRA8Unorm under
		# r_hdr 1 (fxtemporal8), a scaler refused in float16 tells no note (fxnonote). Each fx mutant
		# runs only when the unmutated probe ran the half of its MetalFX case it needs: fxlinear and
		# fxnonote the spatial half (the spatial scaler; fxnonote is caught by its refusal case),
		# fxtemporal8 the temporal scaler made in RGBA16Float (the build VM has the spatial scaler alone)
		probe_mutants=0
		probe_skipped=0
		probe_skip_why=
		fx_spatial_ran=0
		fx_temporal16=0
		fx_spatial_why=
		fx_why16=
		fx_caps=$(sed -n 's/^rmb_cs_probe: scalers: //p' "${tmp:?}/probe.out" | head -n 1)
		[ -n "$fx_caps" ] || bad "tests/metal/rmb_cs_probe.m printed no 'scalers:' capability line"
		# the line is read as two flags, each exactly 0 or 1, with nothing around them: anything else
		# (spatial=yes, a trailing space or junk, 'none') is a probe that cannot be trusted
		fx_sp=
		fx_tp=
		case "$fx_caps" in
			"spatial="[01]" temporal="[01]) fx_sp=${fx_caps#spatial=}; fx_sp=${fx_sp%% *}; fx_tp=${fx_caps##*temporal=} ;;
			*) [ -z "$fx_caps" ] || bad "tests/metal/rmb_cs_probe.m printed a 'scalers:' line that is not 'spatial=0|1 temporal=0|1': '$fx_caps'" ;;
		esac
		if grep -q 'the MetalFX spatial half under r_hdr 0 and 1, .*: run$' "${tmp:?}/probe.out"; then
			fx_spatial_ran=1
		else
			fx_spatial_why=$(sed -n 's/^rmb_cs_probe: the MetalFX spatial half skipped: //p' "${tmp:?}/probe.out" | head -n 1)
			fx_spatial_why=${fx_spatial_why% on this GPU}
			[ -n "$fx_spatial_why" ] || bad "tests/metal/rmb_cs_probe.m printed neither a run nor a skipped line for the MetalFX spatial half"
		fi
		if grep -q 'the temporal scaler in RGBA16Float colour and output: made$' "${tmp:?}/probe.out"; then
			fx_temporal16=1
		else
			fx_why16=$(sed -n -e 's/^rmb_cs_probe: the MetalFX temporal half skipped: //p' -e 's/^rmb_cs_probe: the temporal scaler in RGBA16Float skipped: //p' "${tmp:?}/probe.out" | head -n 1)
			fx_why16=${fx_why16% on this GPU}
			[ -n "$fx_why16" ] || bad "tests/metal/rmb_cs_probe.m printed neither a run nor a skipped line for the temporal scaler in RGBA16Float"
		fi
		for m in nulled kept detached unretained fmtstock unwritten failframe hdrattach hdrkept fxlinear fxtemporal8 fxnonote; do
			why=
			case $m in
				fxlinear|fxnonote) [ "$fx_spatial_ran" = 1 ] || why=${fx_spatial_why:-unexplained} ;;
				fxtemporal8) [ "$fx_temporal16" = 1 ] || why=${fx_why16:-unexplained} ;;
			esac
			if [ -n "$why" ]; then
				# a skip for a missing scaler is refused when the probe's own capability line says that
				# scaler is there, one half at a time
				case $why in
					"no spatial scaler"*) [ "$fx_sp" != 1 ] || bad "mutant $m skipped ($why) but the probe says spatial=1" ;;
					"no temporal scaler"*) [ "$fx_tp" != 1 ] || bad "mutant $m skipped ($why) but the probe says temporal=1" ;;
					"refused in float16"*) [ "${CHECK_REF_METAL_DEVICE_REQUIRE:-0}" != 1 ] || bad "CHECK_REF_METAL_DEVICE_REQUIRE=1 and mutant $m skipped ($why)" ;;
				esac
				probe_skipped=$((probe_skipped + 1))
				case ", $probe_skip_why, " in *", $why, "*) ;; *) probe_skip_why=${probe_skip_why:+$probe_skip_why, }$why ;; esac
				continue
			fi
			rm -rf "${tmp:?}/csmut"
			mkdir "${tmp:?}/csmut"
			case $m in
				fmtstock|hdrkept) mf=rmb_resource.m ;;
				fx*) mf=rmb_metalfx.m ;;
				*) mf=rmb_device.m ;;
			esac
			case $m in
				nulled) sed 's|rmb_layer.colorspace = rmb_cs_own;|rmb_layer.colorspace = NULL;|' "$rm_dir/$mf" ;;
				kept) sed 's|rmb_layer.colorspace = rmb_cs_own;|(void)0;|' "$rm_dir/$mf" ;;
				unretained) sed 's|CGColorSpaceRetain(rmb_cs_own);|(void)0;|' "$rm_dir/$mf" ;;
				detached) sed 's|rmb_cs_release();   /\* the view|(void)0;   /* the view|' "$rm_dir/$mf" ;;
				fmtstock) sed 's|return (v == \[NSNull null\]) ? nil : v;|return (v == [NSNull null]) ? ps : v;|' "$rm_dir/$mf" ;;
				unwritten) sed 's|if (!cmd \|\| !drawable \|\| !region)|if (!cmd \|\| !drawable \|\| (region < 0))|' "$rm_dir/$mf" ;;
				failframe) sed 's|(hdr \&\& (!have_pso \|\| rmb_hdr_dead))|(hdr \&\& !have_pso)|' "$rm_dir/$mf" ;;
				hdrattach) sed 's|rmb_detach();   /\* a failed float16 path stays off|rmb_detach(); rmb_hdr_dead = 0;   /* a failed float16 path stays off|' "$rm_dir/$mf" ;;
				hdrkept) sed 's|rmb_hdr_retry();   /\* with the caches|(void)0;   /* with the caches|' "$rm_dir/$mf" ;;
				fxlinear) sed 's|MTLFXSpatialScalerColorProcessingModeHDR;|MTLFXSpatialScalerColorProcessingModeLinear;|' "$rm_dir/$mf" ;;
				fxtemporal8) sed 's|d.outputTextureFormat = f;   /\* the temporal output|d.outputTextureFormat = RMB_FX_COLOR;   /* the temporal output|' "$rm_dir/$mf" ;;
				fxnonote) sed 's|rmb_fx_tell(rm_hdr_fx_note((int)f), mode, s);|(void)0;|' "$rm_dir/$mf" ;;
			esac > "${tmp:?}/csmut/$mf"
			if cmp -s "$rm_dir/$mf" "${tmp:?}/csmut/$mf"; then
				bad "the colour-space mutant ($m) of $mf changed nothing"
				continue
			fi
			rc=0
			probe_run "${tmp:?}/csmut" "${tmp:?}/rmb_cs_mutant" || rc=$?
			if [ "$rc" = 99 ]; then
				cat "${tmp:?}/probe.out"
				bad "the colour-space mutant ($m) does not compile"
			elif [ "$rc" = 0 ]; then
				bad "the colour-space probe passes a mutant of $mf ($m)"
			else
				probe_mutants=$((probe_mutants + 1))
			fi
		done
		probe="$probe; $probe_mutants colour-space mutants refused"
		[ "$probe_skipped" = 0 ] || probe="$probe, $probe_skipped skipped ($probe_skip_why)"
	fi
fi
seg 21 "; colour space ${probe}"

# (f2) FIX133: the probe's cases hold for every draw and present only when each bind of a
# frame pipeline goes through rmb_fmt_bind and its result is tested (a draw is skipped when it
# binds nothing), and the frame's present goes through rmb_present_written
for f in "$rm_dir"/rmb_*.m; do
	if strip "$f" | grep -n 'setRenderPipelineState:rmb_fmt_pipe(' > "${tmp:?}/fmtbind.out"; then
		bad "$f binds rmb_fmt_pipe's pipeline itself, not through rmb_fmt_bind: $(head -n 1 "${tmp:?}/fmtbind.out")"
	fi
	[ "$f" = "$rm_dir/rmb_resource.m" ] && continue
	if strip "$f" | grep 'rmb_fmt_bind(' | grep -vE 'if \(!?rmb_fmt_bind\(' > "${tmp:?}/fmtbind.out"; then
		bad "$f calls rmb_fmt_bind without testing its result: $(head -n 1 "${tmp:?}/fmtbind.out")"
	fi
done
if strip "$rm_dir/rmb_encode.m" | grep -n 'presentDrawable' > "${tmp:?}/fmtbind.out"; then
	bad "rmb_encode.m presents a drawable itself, not through rmb_present_written: $(head -n 1 "${tmp:?}/fmtbind.out")"
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
seg 18 "; blend factors once, $blend_mutants mutants refused"

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
	[ "${CHECK_REF_METAL_REQUIRE:-0}" != 1 ] || bad "CHECK_REF_METAL_REQUIRE=1 and the build cannot run: $skip"
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
seg 24 "; $build"

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
seg 22 "; metal_rate_start read once, $rate_mutants mutants refused"

# (h) FIX119: one internal header for ref_metal's backend files. A function one rmb_*.m file
# defines and another calls is declared once, in header/rmb_internal.h, and the file that
# defines it includes that header; a hand-written prototype in a .m file (copies drift: the
# five rmb_watch ones, and a rmb_frame_ring_take that lost its __strong) is refused. protos
# <file>: prints "file:line: text" for each non-static file-scope function prototype in the
# file's code (comments and literals stripped): a statement ending in ";" at brace depth 0
# whose text, after dropping __attribute__((...)) and any trailing identifier words after the
# last ")" (NS_RETURNS_RETAINED, __dead2, several in a row, a blank or a line break before the
# ";"), has the shape name(...) with no "=" before its first "(". It catches a prototype over
# several lines, one with extern, one with an attribute or a trailing macro, one whose parameter
# list holds an #if 0 (the statement goes on across it), one returning a pointer to a function,
# (*name(args))(args), also with qualifiers before the name ((* _Nullable name(...)),
# (*const name(...))), and a parenthesised name, (name)(...). It skips preprocessor lines,
# Objective-C @ lines and + / - method lines, anything between #if 0 and its #else, #elif or
# #endif (nested #if counted), a statement starting with static, typedef, _Static_assert or
# static_assert, a function pointer or block variable ((*fp)(int), (^blk)(int),
# (* _Nonnull fpvar)(int), (* const fp2)(int)), and a definition (it ends in a brace, not in
# ";"). An #if 0 that never closes is reported as "unterminated #if 0", so the check fails closed.
protos() {
	strip "$1" | awk -v f="$1" '
	function flush(   s, p, q) {
		s = stmt; stmt = ""
		if (s == "") return
		while (match(s, /__attribute__[ \t]*\(\(([^()]|\([^()]*\))*\)\)/)) s = substr(s, 1, RSTART - 1) " " substr(s, RSTART + RLENGTH)
		sub(/^[ \t]+/, "", s); gsub(/[ \t]+/, " ", s)
		if (s ~ /^(static|typedef|_Static_assert|static_assert)[ (]/ || s ~ /^[-+@]/) return
		sub(/ +$/, "", s)
		sub(/\) *[A-Za-z_][A-Za-z0-9_]*( +[A-Za-z_][A-Za-z0-9_]*)*$/, ")", s)
		p = index(s, "(")
		if (p == 0) return
		q = substr(s, 1, p - 1)
		if (q ~ /=/) return
		r = substr(s, p)
		fpr = (r ~ /^\( ?[*^][ *^A-Za-z_0-9]*[A-Za-z_0-9] ?\(/)
		if (!fpr && r ~ /^\( ?[*^]/) return
		par = (r ~ /^\( ?[A-Za-z_][A-Za-z0-9_]* ?\) ?\(/ && q ~ /[A-Za-z_0-9>*] ?$/)
		if (fpr && q ~ /^ *$/) return
		if (!fpr && !par && q !~ /[A-Za-z_0-9] ?$/) return
		if (s !~ /\) ?$/) return
		print f ":" first ": " s
	}
	{
		line = $0
		if (cont) { cont = (line ~ /\\[ \t\r]*$/); next }
		if (line ~ /^[ \t]*#/) {
			cont = (line ~ /\\[ \t\r]*$/)
			if (skip == 0 && line ~ /^[ \t]*#[ \t]*if[ \t]+0[ \t]*$/) { skip = 1; nest = 1; skipline = NR }
			else if (skip) {
				if (line ~ /^[ \t]*#[ \t]*if/) nest++
				else if (line ~ /^[ \t]*#[ \t]*endif/) { if (--nest == 0) skip = 0 }
				else if (nest == 1 && line ~ /^[ \t]*#[ \t]*(else|elif)/) skip = 0
			}
			next
		}
		if (skip) next
		if (depth == 0 && line ~ /^[ \t]*@[A-Za-z]/) { stmt = ""; next }
		n = length(line)
		for (i = 1; i <= n; i++) {
			c = substr(line, i, 1)
			if (c == "{") { depth++; continue }
			if (c == "}") { if (depth > 0) depth--; if (depth == 0) stmt = ""; continue }
			if (depth > 0) continue
			if (c == ";") { flush(); continue }
			if (stmt == "" && c ~ /[ \t]/) continue
			if (stmt == "") first = NR
			stmt = stmt c
		}
		if (stmt != "") stmt = stmt " "
	}
	END { if (skip) print f ":" skipline ": unterminated #if 0 (the rest of the file is not checked)" }'
}
proto_mutants=0
mkdir -p "${tmp:?}/pm"
printf '%s\n' 'static void s(void);' 'static void t(int a,' '	int b);' 'void f(void)' '{' '	int x = g(1);' '	h(2);' '}' 'typedef void (*cb)(int);' 'int v = g(3);' '_Static_assert(sizeof(int) == 4, "x");' 'extern int e;' '#define M(a) \' '	void m(a);' '// void c(void);' > "${tmp:?}/pm/ok.m"
if [ -n "$(protos "${tmp:?}/pm/ok.m")" ]; then
	protos "${tmp:?}/pm/ok.m"
	bad "the prototype check flags a static prototype, a definition, a typedef, a macro or a comment"
fi
for proto_mut in 'void f(void);' 'extern void f(void);' 'id<MTLDevice> f(int a,' '__attribute__((unused)) void f(void);' 'void *f(size_t n, __strong id<MTLBuffer> *buf);' 'void f(void) NS_RETURNS_RETAINED;' 'void *(*getf(void))(int);' 'id<MTLBuffer> (f)(void);' 'void (* _Nullable getf(void))(int);' 'void (^ _Nonnull getb(void))(int);' 'int (*const getf(void))(int);' 'void f(void) __dead2;' 'void f(void) NS_A NS_b;' 'void f(void) NS_A ;'; do
	case $proto_mut in *,) printf '%s\n' "$proto_mut" '	int b);' > "${tmp:?}/pm/mut.m";; *) printf '%s\n' "$proto_mut" > "${tmp:?}/pm/mut.m";; esac
	if [ -z "$(protos "${tmp:?}/pm/mut.m")" ]; then
		bad "the prototype check passes a mutant ($proto_mut)"
	else
		proto_mutants=$((proto_mutants + 1))
	fi
done
proto_multi() { # <name> <line>...: a mutant over several lines, which the check must refuse
	proto_name=$1; shift
	printf '%s\n' "$@" > "${tmp:?}/pm/mut.m"
	if [ -z "$(protos "${tmp:?}/pm/mut.m")" ]; then
		bad "the prototype check passes a mutant ($proto_name)"
	else
		proto_mutants=$((proto_mutants + 1))
	fi
}
proto_multi 'an #if 0 inside a parameter list' 'void f(int a,' '#if 0' 'int b,' '#endif' 'int c);'
proto_multi 'an #if 0 / #else inside a parameter list' 'void f(int a,' '#if 0' 'int b,' '#else' 'int d,' '#endif' 'int c);'
proto_multi 'a macro with the ; on the next line' 'void f(void) NS_x' ';'
proto_multi 'an #if 0 never closed' 'void ok(void) {}' '#if 0' 'void dead(void);'
proto_ok=0
for proto_fix in 'void (*fp)(int);' 'void (^blk)(int);' '(* _Nonnull fpvar)(int);' 'void (* const fp2)(int);' 'static_assert(sizeof(int) == 4, "x");'; do
	printf '%s\n' "$proto_fix" > "${tmp:?}/pm/fix.m"
	if [ -n "$(protos "${tmp:?}/pm/fix.m")" ]; then
		bad "the prototype check flags a function pointer, a block variable or a static_assert ($proto_fix)"
	else
		proto_ok=$((proto_ok + 1))
	fi
done
printf '%s\n' '#if 0' 'void dead(void);' '#if defined(X)' 'void nested(void);' '#endif' 'void dead2(void);' '#endif' > "${tmp:?}/pm/fix.m"
if [ -n "$(protos "${tmp:?}/pm/fix.m")" ]; then
	bad "the prototype check flags a prototype inside #if 0 (a nested #if included)"
else
	proto_ok=$((proto_ok + 1))
fi
printf '%s\n' '#if 0' 'void dead(void);' '#else' 'void live(void);' '#endif' > "${tmp:?}/pm/fix.m"
if [ -z "$(protos "${tmp:?}/pm/fix.m")" ]; then
	bad "the prototype check skips a prototype in the #else of an #if 0"
else
	proto_ok=$((proto_ok + 1))
fi
for pf in "$rm_dir"/*.m; do
	pout=$(protos "$pf")
	if [ -n "$pout" ]; then
		printf '%s\n' "$pout"
		bad "$pf declares a function by hand: put it in $rm_dir/header/rmb_internal.h"
	fi
done
seg 23 "; no hand prototype in a .m file, $proto_mutants mutants refused, $proto_ok shapes passed"

# (i) TASK179: rm_main.c and rmb_gbuf.m reach the ray-traced effects only through the effect table's walks
# (rm_rtfx.c). fx_vocab <table>: the effects' prefixes, from the identifiers the table's rows name in FX(...),
# each cut after its second underscore (RM_RTS_Init: RM_RTS_, rmb_rtw_fx_mark: rmb_rtw_), with the pure
# part's rm_ form of each (RM_RTSun_: rm_rtsun_): a row added for a new effect adds its prefixes. fx_named
# <file> <vocabulary>: "file:line: name" for each identifier of the file's code (comments and literals
# stripped) that starts with one of them.
fx_vocab() {
	strip "$1" | grep -oE 'FX\([A-Za-z_][A-Za-z0-9_]*\)' | sed -E 's/^FX\(//; s/\)$//' |
		sed -nE 's/^([A-Za-z]+_[A-Za-z]+_).*/\1/p' |
		awk '{ print; p = $0; sub(/^[A-Za-z]+_/, "", p); print "rm_" tolower(p) }' | sort -u
}
fx_named() {
	strip "$1" | awk -v f="$1" '
		NR == FNR { pre[++n] = $0; next }
		{
			line = $0
			while (match(line, /[A-Za-z_][A-Za-z0-9_]*/)) {
				id = substr(line, RSTART, RLENGTH); line = substr(line, RSTART + RLENGTH)
				for (i = 1; i <= n; i++) if (index(id, pre[i]) == 1) { print f ":" FNR ": " id; break }
			}
		}' "$2" -
}
fx_mutants=0
fx_vocab "$rm_dir/rm_rtfx.c" > "${tmp:?}/fx.vocab"
if ! grep -qx 'RM_RTS_' "${tmp:?}/fx.vocab" || ! grep -qx 'rmb_rtw_' "${tmp:?}/fx.vocab"; then
	cat "${tmp:?}/fx.vocab"
	bad "the effect vocabulary of $rm_dir/rm_rtfx.c lacks RM_RTS_ or rmb_rtw_: its rows are not FX(...) entries"
else
	for fxf in rm_main.c rmb_gbuf.m; do
		fout=$(fx_named "$rm_dir/$fxf" "${tmp:?}/fx.vocab")
		if [ -n "$fout" ]; then
			printf '%s\n' "$fout"
			bad "$rm_dir/$fxf names a ray-traced effect outside the effect table: walk it with rm_rtfx_* (rm_rtfx.c)"
		fi
	done
	# Mutants: an effect called directly from rm_main.c, and from rmb_gbuf.m, must be named; one named in a
	# comment only must not; a new row's effect (RM_RTGI_) called from rm_main.c must be named through the
	# vocabulary of the table that has the row
	mkdir "${tmp:?}/fxc"
	for fxm in main gbuf comment newrow; do
		cp "$rm_dir/rm_main.c" "$rm_dir/rmb_gbuf.m" "$rm_dir/rm_rtfx.c" "${tmp:?}/fxc/"
		want=""
		case $fxm in
			main) awk '{ print } /^[ \t]*rm_rtfx_frame\(fd, RM_RT_SceneReady\(\)\);/ { print "\tRM_RTW_Frame(fd, RM_RT_SceneReady());" }' \
				"$rm_dir/rm_main.c" > "${tmp:?}/fxc/rm_main.c"; want=RM_RTW_Frame ;;
			gbuf) awk '{ print } /^[ \t]*rm_rtfx_post\(&io\);/ { print "\t\trmb_rtw_blend(io.enc, t_material);" }' \
				"$rm_dir/rmb_gbuf.m" > "${tmp:?}/fxc/rmb_gbuf.m"; want=rmb_rtw_blend ;;
			comment) awk '{ print } /^[ \t]*rm_rtfx_frame\(fd, RM_RT_SceneReady\(\)\);/ { print "\t/* RM_RTW_Frame(fd, 0); */" }' \
				"$rm_dir/rm_main.c" > "${tmp:?}/fxc/rm_main.c" ;;
			newrow) awk '{ print } /^static const rm_rtfx_t rm_rtfx\[/ { print "\t[RM_RTFX_COUNT - 1] = { \"gi\", \"r_rt_gi\", FX(RM_RTGI_Init) }," }' \
				"$rm_dir/rm_rtfx.c" > "${tmp:?}/fxc/rm_rtfx.c"
				awk '{ print } /^[ \t]*rm_rtfx_frame\(fd, RM_RT_SceneReady\(\)\);/ { print "\tRM_RTGI_Frame(fd, RM_RT_SceneReady());" }' \
				"$rm_dir/rm_main.c" > "${tmp:?}/fxc/rm_main.c"; want=RM_RTGI_Frame ;;
		esac
		fx_vocab "${tmp:?}/fxc/rm_rtfx.c" > "${tmp:?}/fxc.vocab"
		got=$( { fx_named "${tmp:?}/fxc/rm_main.c" "${tmp:?}/fxc.vocab"; fx_named "${tmp:?}/fxc/rmb_gbuf.m" "${tmp:?}/fxc.vocab"; } |
			sed 's/.*: //' | tr '\n' ' ' | sed 's/ $//')
		if cmp -s "$rm_dir/rm_main.c" "${tmp:?}/fxc/rm_main.c" && cmp -s "$rm_dir/rmb_gbuf.m" "${tmp:?}/fxc/rmb_gbuf.m"; then
			bad "the effect-call mutant ($fxm) changed nothing"
		elif [ "$got" != "$want" ]; then
			bad "the effect-call check names \"$got\", not \"$want\", for a mutant ($fxm)"
		else
			fx_mutants=$((fx_mutants + 1))
		fi
	done
fi
seg 13 ", $fx_mutants call mutants refused"

# (j) TASK199: the HDR tone map runs once per pixel per frame because the menu's RDF_NOWORLDMODEL view starts
# with R_ClearNoWorld, an opaque quad over r_view.vp, the rectangle R_HdrToneMap maps (R_SetupGL and
# R_HdrToneMap build theirs with rm_view_rect from the same refdef fields). The host test cannot see rm_main.c,
# so the order is held here, by source: hdr_body <file> <function> prints the function's body (comments and
# literals stripped, up to the "}" in column 0); hdr_order <file> prints one line per rule the file breaks.
hdr_body() {
	strip "$1" | awk -v fn="$2" '
		!b && !p && $0 ~ "^" fn "\\(" { p = 1 }
		p && /;/ { p = 0; next }
		p && /^{/ { b = 1; p = 0; next }
		b && /^}/ { exit }
		b { print }'
}
hdr_fields() {
	hdr_body "$1" "$2" | tr '\n' ' ' | sed -n 's/.*rm_view_rect(\([^;]*\));.*/\1/p' |
		grep -oE '(r_newrefdef\.|fd->)[a-z_]+' | sed -E 's/^(r_newrefdef\.|fd->)//' | tr '\n' ' ' | sed 's/ $//'
}
# hdr_args <file> <function>: the whole argument list of the function's rm_view_rect call, blanks and the
# refdef prefix removed, without its last argument (the rectangle it fills); "N calls" when it is not one call
hdr_args() {
	calls=$(hdr_body "$1" "$2" | tr -d ' \t\n' | grep -oE 'rm_view_rect\([^;]*\);')
	nc=$(printf '%s\n' "$calls" | grep -c rm_view_rect)
	if [ "$nc" != 1 ]; then
		echo "$nc calls"
		return 0
	fi
	printf '%s\n' "$calls" | sed -E 's/^rm_view_rect\((.*)\);$/\1/; s/(r_newrefdef\.|fd->)//g; s/,&[A-Za-z_.]*$//'
}
# hdr_flat <file> <function>: the function's body with every blank removed
hdr_flat() {
	hdr_body "$1" "$2" | tr -d ' \t\n'
}
hdr_order() { # <rm_main.c> <rm_sdl.c>
	first=$(hdr_body "$1" R_RenderViewDraw | grep -v '^[[:space:]]*$' | sed -n '1p' | awk '{ gsub(/^[ \t]+|[ \t]+$/, ""); print }')
	[ "$first" = "R_ClearNoWorld();" ] || echo "R_RenderViewDraw does not start with R_ClearNoWorld(); (its first statement is \"$first\")"
	hdr_body "$1" R_ClearNoWorld | tr '\n' ' ' | grep -qE 'rmb_draw_color\([^;]*&r_view\.vp[^;]*;' ||
		echo "R_ClearNoWorld does not draw over &r_view.vp"
	cnw=$(hdr_flat "$1" R_ClearNoWorld)
	printf '%s\n' "$cnw" | grep -qF 'if(!(r_newrefdef.rdflags&RDF_NOWORLDMODEL)){return;}' ||
		echo "R_ClearNoWorld does not begin with the !(r_newrefdef.rdflags & RDF_NOWORLDMODEL) guard that returns"
	[ "$(printf '%s\n' "$cnw" | grep -o 'return' | wc -l | tr -d ' ')" = 1 ] ||
		echo "R_ClearNoWorld returns anywhere but in its RDF_NOWORLDMODEL guard, so its draw may not run"
	for hf in R_SetupGL R_HdrToneMap; do
		got=$(hdr_fields "$1" "$hf")
		[ "$got" = "x y width height" ] || echo "$hf builds its rectangle from \"$got\", not \"x y width height\", of one refdef"
	done
	sga=$(hdr_args "$1" R_SetupGL)
	hga=$(hdr_args "$1" R_HdrToneMap)
	[ "$sga" = "$hga" ] || echo "R_HdrToneMap's rm_view_rect arguments (\"$hga\") differ from R_SetupGL's (\"$sga\")"
	hdr_flat "$1" R_HdrToneMap | grep -qF 'rmb_tonemap_pass((int)vp.x,(int)vp.y,(int)vp.w,(int)vp.h);' ||
		echo "R_HdrToneMap's tone-map pass is not over the (int)vp.x, vp.y, vp.w, vp.h of its rm_view_rect"
	hdr_flat "$2" RI_ShutdownContext | grep -qF 'RM_HdrProbeCancel();rmb_detach();' ||
		echo "RI_ShutdownContext does not call RM_HdrProbeCancel(); before rmb_detach();"
	hdr_flat "$1" RM_HdrProbeCancel | grep -qF 'rm_hdr_probe_cancel(&rm_hdr_probe_wait)' ||
		echo "RM_HdrProbeCancel does not call rm_hdr_probe_cancel(&rm_hdr_probe_wait)"
	return 0
}
hdr_mutants=0
hout=$(hdr_order "$rm_dir/rm_main.c" "$rm_dir/rm_sdl.c")
if [ -n "$hout" ]; then
	printf '%s\n' "$hout"
	bad "$rm_dir/rm_main.c or rm_sdl.c breaks the single tone-map order or the probe cancel (docs/plans/hdr-output.md, HD3)"
fi
for hm in noclear othervp fieldsw fieldss tmfull thtw retclear guardinv nocancel nobody; do
	cp "$rm_dir/rm_main.c" "${tmp:?}/hdr-mutant.c"
	cp "$rm_dir/rm_sdl.c" "${tmp:?}/hdr-mutant-sdl.c"
	case $hm in
		noclear) awk '/^R_RenderViewDraw\(/ { b = 1 } b && !d && /^[ \t]*R_ClearNoWorld\(\);/ { d = 1; next } { print }' \
			"$rm_dir/rm_main.c" > "${tmp:?}/hdr-mutant.c"; want="does not start with R_ClearNoWorld" ;;
		othervp) awk '/^R_ClearNoWorld\(/ { b = 1 } /^}/ { b = 0 } b { sub(/&r_view\.vp/, "\\&r_view.proj") } { print }' \
			"$rm_dir/rm_main.c" > "${tmp:?}/hdr-mutant.c"; want="R_ClearNoWorld does not draw over" ;;
		fieldsw) awk '/^R_HdrToneMap\(/ { b = 1 } /^}/ { b = 0 } b { sub(/fd->height/, "fd->width") } { print }' \
			"$rm_dir/rm_main.c" > "${tmp:?}/hdr-mutant.c"; want="R_HdrToneMap builds its rectangle" ;;
		fieldss) awk '/^R_SetupGL\(/ { b = 1 } /^}/ { b = 0 } b { sub(/r_newrefdef\.y,/, "r_newrefdef.x,") } { print }' \
			"$rm_dir/rm_main.c" > "${tmp:?}/hdr-mutant.c"; want="R_SetupGL builds its rectangle" ;;
		tmfull) awk '/^R_HdrToneMap\(/ { b = 1 } /^}/ { b = 0 } b { sub(/rmb_tonemap_pass\(.*\);/, "rmb_tonemap_pass(0, 0, tw, th);") } { print }' \
			"$rm_dir/rm_main.c" > "${tmp:?}/hdr-mutant.c"; want="tone-map pass is not over" ;;
		thtw) awk '/^R_HdrToneMap\(/ { b = 1 } /^}/ { b = 0 } b { sub(/th, &vp/, "tw, \\&vp") } { print }' \
			"$rm_dir/rm_main.c" > "${tmp:?}/hdr-mutant.c"; want="rm_view_rect arguments" ;;
		retclear) awk '/^R_ClearNoWorld\(/ { b = 1 } /^}/ { b = 0 } b && /rmb_draw_color/ && !d { d = 1; print "\treturn;" } { print }' \
			"$rm_dir/rm_main.c" > "${tmp:?}/hdr-mutant.c"; want="returns anywhere but in its RDF_NOWORLDMODEL guard" ;;
		guardinv) awk '/^R_ClearNoWorld\(/ { b = 1 } /^}/ { b = 0 } b { sub(/!\(r_newrefdef/, "(r_newrefdef") } { print }' \
			"$rm_dir/rm_main.c" > "${tmp:?}/hdr-mutant.c"; want="does not begin with the !(r_newrefdef.rdflags" ;;
		nocancel) awk '/^RI_ShutdownContext\(/ { b = 1 } /^}/ { b = 0 } b && /^[ \t]*RM_HdrProbeCancel\(\);/ { next } { print }' \
			"$rm_dir/rm_sdl.c" > "${tmp:?}/hdr-mutant-sdl.c"; want="RI_ShutdownContext does not call RM_HdrProbeCancel" ;;
		nobody) awk '/^RM_HdrProbeCancel\(/ { b = 1 } /^}/ { b = 0 } b { sub(/rm_hdr_probe_cancel\(&rm_hdr_probe_wait\)/, "0") } { print }' \
			"$rm_dir/rm_main.c" > "${tmp:?}/hdr-mutant.c"; want="RM_HdrProbeCancel does not call rm_hdr_probe_cancel" ;;
	esac
	mout=$(hdr_order "${tmp:?}/hdr-mutant.c" "${tmp:?}/hdr-mutant-sdl.c")
	if cmp -s "$rm_dir/rm_main.c" "${tmp:?}/hdr-mutant.c" && cmp -s "$rm_dir/rm_sdl.c" "${tmp:?}/hdr-mutant-sdl.c"; then
		bad "the tone-map order mutant ($hm) changed nothing"
	elif ! printf '%s\n' "$mout" | grep -qF "$want"; then
		bad "the tone-map order check does not refuse the mutant ($hm): \"$mout\""
	else
		hdr_mutants=$((hdr_mutants + 1))
		[ -z "${HDR_MUTANT_SHOW:-}" ] || printf 'mutant %s: %s\n' "$hm" "$mout"
	fi
done
seg 14 ", tone-map order and probe cancel held, $hdr_mutants order mutants refused"

# RT-primary's temporal accumulation (TASK190 slice 1, PM8): tests/metal/rm_rtptemporal_test.c holds
# common/mtl_rtp_math.h's rule (the blend of each channel with its own history, the reset, off, depth
# and material rejects, a history that is not a light, the count's saturation, NaN in and in the
# history, the pass-through of every pixel that is not a traced world pixel); then the same test
# against mutants of the header, each of which must compile and fail it: the reset ignored, the count
# frozen, green blended with red's history, the bad history kept, a non-world pixel not passed through.
rtp=""
rtp_tree() { # <dir>: a copy of the test and the headers, laid out as the test includes them
	rm -rf "${1:?}"
	mkdir -p "$1/src/client/refresh/metal/common" "$1/tests/metal"
	cp "$rm_dir/common/mtl_rtp_math.h" "$rm_dir/common/mtl_rtt_math.h" "$rm_dir/common/mtl_shader_math.h" \
		"$rm_dir/common/mtl_gbuf_math.h" "$1/src/client/refresh/metal/common/"
	cp tests/metal/rm_rtptemporal_test.c "$1/tests/metal/"
}
rtp_build() { # <tree>: 0 when the tree's test builds
	# shellcheck disable=SC2086 # flags is a word list
	$cc $flags -o "$1/t" "$1/tests/metal/rm_rtptemporal_test.c" -lm > "${tmp:?}/cc.out" 2>&1
}
rtp_run() { # <tree>: 0 when the tree's test builds and passes
	rtp_build "$1" && UBSAN_OPTIONS=print_stacktrace=1 "$1/t" > "${tmp:?}/out" 2>&1
}
rtp_tree "${tmp:?}/rtp"
if ! rtp_run "${tmp:?}/rtp"; then
	cat "${tmp:?}/cc.out" "${tmp:?}/out"
	bad "tests/metal/rm_rtptemporal_test.c fails on the real files"
else
	rtp=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_rtptemporal_test: //')
	case $rtp in *", 0 failed") ;; *) bad "tests/metal/rm_rtptemporal_test.c printed no result line"; rtp="";; esac
fi
rtp_mutants=0
rtp_h=$rm_dir/common/mtl_rtp_math.h
for m in reset count mix bad pass; do
	rtp_tree "${tmp:?}/rtpm"
	h=${tmp:?}/rtpm/src/client/refresh/metal/common/mtl_rtp_math.h
	case $m in
		reset) sed 's/mtl_rtt_reject(reset, ok,/mtl_rtt_reject(reset \& 0u, ok,/' ;;
		count) sed 's/p\.hist\.a = mtl_rtt_count(rej, h\.a);/p.hist.a = h.a;/' ;;
		mix) sed 's/mtl_rtp_clamp(cur\.g), h\.g,/mtl_rtp_clamp(cur.g), h.r,/' ;;
		bad) sed 's/r |= MTL_RTP_REJ_BAD;/r |= 0u;/' ;;
		pass) sed 's/		p\.out = cur;/		p.out = h;/' ;;
	esac < "$rtp_h" > "$h"
	if cmp -s "$rtp_h" "$h"; then
		bad "the RT-primary temporal mutant ($m) changed nothing"
	elif ! rtp_build "${tmp:?}/rtpm"; then
		cat "${tmp:?}/cc.out"
		bad "the RT-primary temporal mutant ($m) does not compile, so it tests nothing"
	elif rtp_run "${tmp:?}/rtpm"; then
		bad "tests/metal/rm_rtptemporal_test.c passes the RT-primary temporal mutant ($m)"
	else
		rtp_mutants=$((rtp_mutants + 1))
	fi
done
[ "$rtp_mutants" = 5 ] || bad "the RT-primary temporal test refused $rtp_mutants of its 5 header mutants"
# The kernel, rm_rtptemporal.metal, pinned as text: the reprojection with the motion vector as the
# G-buffer wrote it (mutants: its sign flipped, no motion read), the view's reset and the material
# reaching the rule (mutants: the reset not passed, a material compared with itself), no lightmap
# read (mutant: lm read into the output), and the kernel writing only the light, the history and its
# key, each once (mutants: a fourth output written, the history not written). Where the Metal
# compiler is there, the kernel and every mutant must compile with it (a mutant it refuses tests
# nothing). The back end takes the view into the history only where it dispatches (rtt_place, as RT7's
# back ends), the effect row reads the accumulated light where there is one, and the scene steps the
# shared reset state with r_rt_temporal as the user set it.
rtp_metal=""
if [ "$(uname -s)" = Darwin ] && command -v xcrun >/dev/null 2>&1 && xcrun -f metal >/dev/null 2>&1; then
	rtp_metal=1
fi
rtp_compile() { # <file.metal>: 0 when it compiles as the Makefile compiles rm_rtptemporal.metal
	xcrun -sdk macosx metal -c -I"$rm_dir" "$1" -o "${tmp:?}/rtp.air" > "${tmp:?}/metal.out" 2>&1
}
rtp_body() { # <file>: rm_rtp_temporal's code, comments and literals blanked, whitespace squeezed
	strip "$1" | awk '/^rm_rtp_temporal\(/ { b = 1 } b && /^}/ { b = 0 } b { print }' | tr -s ' \t' '  '
}
rtp_pin() { # <file> <pin>: 0 when the file holds the pin
	case $2 in
		motion) rtp_body "$1" | grep -qF 'const float2 mv = motion.read(px).xy;' &&
			rtp_body "$1" | grep -qF 'mtl_rtt_reproject(px.x, px.y, mv.x, mv.y, w, hh)' ;;
		reset) rtp_body "$1" | grep -qF 'rej = mtl_rtp_reject(t.reset, r.ok, dist, hd, t.move, m, hm, hv);' ;;
		nolm) ! strip "$1" | grep -qwE 'lm|lightmap' ;;
		writes)
			# exactly three writes, one of each output, and no other texture it can write
			[ "$(strip "$1" | grep -oE '[A-Za-z_][A-Za-z0-9_]*\.write\(' | sort | tr -d '\n')" = \
				'hist_out.write(key_out.write(light_out.write(' ] &&
				! strip "$1" | grep -E 'access::(read_)?write>' | grep -vqE ' (light_out|hist_out|key_out) \[\[texture' ;;
	esac
}
rtp_kpins=0 rtp_kmut=0
rtp_k=$rm_dir/rm_rtptemporal.metal
if [ -n "$rtp_metal" ] && ! rtp_compile "$rtp_k"; then
	cat "${tmp:?}/metal.out"
	bad "rm_rtptemporal.metal does not compile"
fi
for p in motion reset nolm writes; do
	if ! rtp_pin "$rtp_k" "$p"; then
		bad "rm_rtptemporal.metal does not hold the accumulation's $p pin"
		continue
	fi
	rtp_kpins=$((rtp_kpins + 1))
	case $p in
		motion) kms="sign nomotion" ;;
		reset) kms="noreset samemat" ;;
		nolm) kms="lmread" ;;
		*) kms="extra nohist" ;;
	esac
	for m in $kms; do
		case $m in
			sign) sed 's/mtl_rtt_reproject(px\.x, px\.y, mv\.x, mv\.y,/mtl_rtt_reproject(px.x, px.y, -mv.x, mv.y,/' ;;
			nomotion) sed 's/const float2 mv = motion\.read(px)\.xy;/const float2 mv = float2(0.0f);/' ;;
			noreset) sed 's/rej = mtl_rtp_reject(t\.reset,/rej = mtl_rtp_reject(0u,/' ;;
			samemat) sed 's/t\.move, m, hm, hv);/t.move, m, m, hv);/' ;;
			lmread) awk '/texture\(9\)/ { print; print "\t\ttexture2d<float, access::read> lm [[texture(10)]],"; next }
				/light_out\.write\(/ { sub(/p\.out\.a\)/, "p.out.a) + lm.read(px)") } { print }' ;;
			extra) awk '/texture\(9\)/ { print; print "\t\ttexture2d<float, access::write> vis_out [[texture(10)]],"; next }
				/key_out\.write\(/ { print; print "\t\tvis_out.write(float4(1.0f), px);"; next } { print }' ;;
			nohist) sed '/hist_out\.write(/d' ;;
		esac < "$rtp_k" > "${tmp:?}/rtp-kernel.metal"
		if cmp -s "$rtp_k" "${tmp:?}/rtp-kernel.metal"; then
			bad "the RT-primary temporal kernel mutant ($m) changed nothing"
		elif [ -n "$rtp_metal" ] && ! rtp_compile "${tmp:?}/rtp-kernel.metal"; then
			cat "${tmp:?}/metal.out"
			bad "the RT-primary temporal kernel mutant ($m) does not compile, so it tests nothing"
		elif rtp_pin "${tmp:?}/rtp-kernel.metal" "$p"; then
			bad "the accumulation's $p pin passes its mutant ($m)"
		else
			rtp_kmut=$((rtp_kmut + 1))
		fi
	done
done
[ "$rtp_kpins" = 4 ] && [ "$rtp_kmut" = 7 ] ||
	bad "rm_rtptemporal.metal holds $rtp_kpins of its 4 pins and refuses $rtp_kmut of its 7 mutants"
# the back end, the effect row and the scene
if [ "$(grep -cF "$rtt_take" "$rm_dir/rmb_rtdir.m")" != 1 ] || ! rtt_place "$rm_dir/rmb_rtdir.m" rmb_rtdir_accumulate; then
	bad "rmb_rtdir.m: rmb_rtdir_accumulate does not take the view through $rtt_take after every early return and before its dispatch"
fi
if ! awk '/^rmb_rtdir_fx_trace\(/ { b = 1 } b && /^}/ { b = 0 } b { print }' "$rm_dir/rmb_rtdir.m" |
	grep -qF 'id<MTLTexture> acc = rmb_rtdir_accumulate(io->depth, io->normal, io->material, dl);' ||
	! awk '/^rmb_rtdir_fx_trace\(/ { b = 1 } b && /^}/ { b = 0 } b { print }' "$rm_dir/rmb_rtdir.m" |
	grep -qF 'io->light = acc ? acc : dl;'; then
	bad "rmb_rtdir.m: rmb_rtdir_fx_trace does not read the accumulated light after the trace"
fi
if [ "$(grep -cF 'rmb_rtdir_temporal(RM_RTT_ViewFor(view), (unsigned long)w, (unsigned long)h);' "$rm_dir/rm_rtdirscene.c")" != 1 ] ||
	[ "$(grep -cF 'ref_metal/rm_rtptemporal.air' Makefile)" != 2 ]; then
	bad "the RT-primary accumulation is not stepped by rm_rtdirscene.c or not linked into ref_metal.metallib"
fi
seg 7.7 "; RT-primary temporal ${rtp:-host test}, $rtp_mutants rule mutants, $rtp_kpins kernel pins with $rtp_kmut kernel mutants refused${rtp_metal:+ and compiled}"

# RT-primary's light on rastered alias models (TASK191 slice 2): rm_rtmodel.metal pinned as text, each
# pin with its mutants, each of which must compile with the Metal compiler where it is there and be refused.
# The file includes rm_rtdir.metal for its helpers with exactly its two entry points renamed (rename-extra:
# a third #define); the kernel lights material-2 pixels with a normal only (model-writes-world: world
# pixels; no-normal-traced: the normal's test dropped); the light attachment gives the tag alone
# (model-from-base: its colour added to the light); no lightmap (model-reads-lm); no fit sum
# (model-fit-leak); the traced light in place of the shadedot (shadedot-back: a shadedot-like factor); the
# tag decoded at material 2 only (tag-no-material). Slice 3: a weapon pixel's point is its own view's at the
# depth hack's depth, the world's unprojection called once, for the other models (weapon-world-view: the
# view's inverse; weapon-depth-raw: the stored depth read as the clip depth; weapon-world-point: the trace
# shades every model at the world's unprojection), pulled back before the first wall, a model no wall
# (no-pullback: the hit distance not used; wall-models: a model read as a wall), a ray that meets no wall
# past the weapon's distance, finite (miss-len: the distance itself, which fast math may put an ulp short). The back end binds the view with no own-light table
# (model-own-ray: nself left as the trace's) and exactly its seven textures (bind-extra), each mutant
# compiled where the compiler is there.
rtk_metal=""
if [ "$(uname -s)" = Darwin ] && command -v xcrun >/dev/null 2>&1 && xcrun -f metal >/dev/null 2>&1; then
	rtk_metal=1
fi
rtk_compile() { # <file.metal>: 0 when it compiles as the Makefile compiles rm_rtmodel.metal
	xcrun -sdk macosx metal -c -I"$rm_dir" "$1" -o "${tmp:?}/rtk.air" > "${tmp:?}/metal.out" 2>&1
}
rtk_body() { # <file>: rm_rtm_trace's code, comments and literals blanked, whitespace squeezed
	strip "$1" | awk '/^rm_rtm_trace\(/ { b = 1 } b && /^}/ { b = 0 } b { print }' | tr -s ' \t' '  '
}
rtk_pin() { # <file> <pin>: 0 when the file holds the pin
	case $2 in
		rename) [ "$(grep -cE '^[[:space:]]*#[[:space:]]*define' "$1")" = 2 ] &&
			grep -qxF '#define rm_rtdir_trace rm_rtm_unused_trace' "$1" &&
			grep -qxF '#define rm_fragment_rtdir_show rm_rtm_unused_show' "$1" && grep -qxF '#include "rm_rtdir.metal"' "$1" ;;
		gate) rtk_body "$1" | grep -qF 'if (inside && (m == MTL_GB_MAT_ALIAS))' &&
			rtk_body "$1" | grep -qF 'model = ((d < 1.0f) && (nn.a > 0.5f)) ? 1u : 0u;' &&
			[ "$(strip "$1" | grep -oE '[A-Za-z_][A-Za-z0-9_]*\.write\(' | sort | tr -d '\n')" = 'light_out.write(vis_out.write(' ] &&
			[ "$(rtk_body "$1" | awk '/^ if \(model\)$/ { b = 1 } b && /^ }$/ { b = 0 } b' | grep -cE '(light_out|vis_out)\.write\(')" = 2 ] ;;
		base) [ "$(strip "$1" | grep -oF 'gbl.' | wc -l | tr -d ' ')" = 1 ] &&
			rtk_body "$1" | grep -qF 'const uint f = rm_rtm_px_flags(m, gbl.read(px));' ;;
		nolm) ! strip "$1" | grep -qwE 'lm|lightmap|lms' ;;
		fit) ! strip "$1" | grep -qE 'RM_RTDIR_C_|rm_rtdir_fit_|rm_rtdir_add64' ;;
		dot) [ "$(strip "$1" | grep -oF 'mtl_rtm_color(' | wc -l | tr -d ' ')" = 1 ] &&
			strip "$1" | grep -qF 'mtl_rtm_color(mtl_rtm_v3(l.r, l.g, l.b), MTL_RTM_DOT_TRACED, f, p.glow, p.overbright);' ;;
		tagmat) strip "$1" | grep -qF 'return (m == MTL_GB_MAT_ALIAS) ? mtl_rtm_tag_flags(t) : 0u;' ;;
		weapon) rtk_body "$1" | grep -qF 'const float3 pt = rm_rtm_point(v, p, px, d, f, ias, inst, tmat, ntris);' &&
			[ "$(strip "$1" | grep -oF 'rm_rtdir_position(' | wc -l | tr -d ' ')" = 1 ] &&
			strip "$1" | grep -qF 'const float4 h = p.winv * float4(cx, cy, mtl_rtm_weapon_ndc(d), 1.0f);' ;;
		pullback) strip "$1" | grep -qF 'const float t = rm_rtm_wall_t(v.eye.xyz, w, ias, inst, tmat, ntris);' &&
			strip "$1" | grep -qF 'mtl_rtm_v3(w.x, w.y, w.z), t);' &&
			strip "$1" | grep -qF 'if (mtl_rtm_wall(m, (b == 0xffffffffu) ? 1 : 0))' &&
			strip "$1" | grep -qF 'const float none = 2.0f * len + 1.0f;' &&
			[ "$(strip "$1" | grep -cxF '			return none;')" = 2 ] && [ "$(strip "$1" | grep -cxF '	return none;')" = 1 ] &&
			[ "$(strip "$1" | grep -cxF '		return len;')" = 1 ] ;;
	esac
}
rtk_kpins=0 rtk_kmut=0
rtk_k=$rm_dir/rm_rtmodel.metal
if [ -n "$rtk_metal" ] && ! rtk_compile "$rtk_k"; then
	cat "${tmp:?}/metal.out"
	bad "rm_rtmodel.metal does not compile"
fi
for p in rename gate base nolm fit dot tagmat weapon pullback; do
	if ! rtk_pin "$rtk_k" "$p"; then
		bad "rm_rtmodel.metal does not hold the model pass's $p pin"
		continue
	fi
	rtk_kpins=$((rtk_kpins + 1))
	case $p in
		rename) kms="rename-extra" ;;
		gate) kms="model-writes-world no-normal-traced" ;;
		base) kms="model-from-base" ;;
		nolm) kms="model-reads-lm" ;;
		fit) kms="model-fit-leak" ;;
		dot) kms="shadedot-back" ;;
		tagmat) kms="tag-no-material" ;;
		weapon) kms="weapon-world-view weapon-depth-raw weapon-world-point" ;;
		*) kms="no-pullback wall-models miss-len" ;;
	esac
	for m in $kms; do
		case $m in
			rename-extra) awk '{ print } /^#define rm_fragment_rtdir_show / { print "#define rm_rtdir_own rm_rtm_own" }' ;;
			model-writes-world) sed 's/if (inside && (m == MTL_GB_MAT_ALIAS))/if (inside \&\& (m == MTL_GB_MAT_WORLD))/' ;;
			no-normal-traced) sed 's/model = ((d < 1.0f) && (nn.a > 0.5f)) ? 1u : 0u;/model = (d < 1.0f) ? 1u : 0u;/' ;;
			model-from-base) sed 's/light_out.write(rm_rtm_out(l.rgb, f, p, tabs), px);/light_out.write(rm_rtm_out(l.rgb + gbl.read(px).rgb, f, p, tabs), px);/' ;;
			model-reads-lm) awk '/texture\(3\)\]\],$/ { print; print "\t\ttexture2d<float, access::read> lm [[texture(4)]],"; next }
				/light_out\.write\(rm_rtm_out\(/ { sub(/l\.rgb,/, "l.rgb + lm.read(px).rgb,") } { print }' ;;
			model-fit-leak) awk '{ print } /counts\[RM_RTM_C_OPEN\], c_o,/ { print "\t\t\tatomic_fetch_add_explicit(&counts[RM_RTDIR_C_FIT_N], c_m, memory_order_relaxed);" }' ;;
			shadedot-back) sed 's/MTL_RTM_DOT_TRACED, f, p.glow/max(l.r, 0.5f), f, p.glow/' ;;
			tag-no-material) sed 's/return (m == MTL_GB_MAT_ALIAS) ? mtl_rtm_tag_flags(t) : 0u;/return mtl_rtm_tag_flags(t);/' ;;
			weapon-world-view) sed 's/const float4 h = p.winv \* float4(/const float4 h = v.inv * float4(/' ;;
			weapon-depth-raw) sed 's/float4(cx, cy, mtl_rtm_weapon_ndc(d), 1.0f)/float4(cx, cy, d * 2.0f - 1.0f, 1.0f)/' ;;
			weapon-world-point) sed 's/const float3 pt = rm_rtm_point(v, p, px, d, f, ias, inst, tmat, ntris);/const float3 pt = rm_rtdir_position(v, px, d);/' ;;
			no-pullback) sed 's/mtl_rtm_v3(w.x, w.y, w.z), t);/mtl_rtm_v3(w.x, w.y, w.z), INFINITY);/' ;;
			wall-models) sed 's/if (mtl_rtm_wall(m, (b == 0xffffffffu) ? 1 : 0))/if (mtl_rtm_wall(m, 0))/' ;;
			miss-len) sed 's/^			return none;$/			return len;/' ;;
		esac < "$rtk_k" > "${tmp:?}/rtk-kernel.metal"
		if cmp -s "$rtk_k" "${tmp:?}/rtk-kernel.metal"; then
			bad "the model pass's kernel mutant ($m) changed nothing"
		elif [ -n "$rtk_metal" ] && ! rtk_compile "${tmp:?}/rtk-kernel.metal"; then
			cat "${tmp:?}/metal.out"
			bad "the model pass's kernel mutant ($m) does not compile, so it tests nothing"
		elif rtk_pin "${tmp:?}/rtk-kernel.metal" "$p"; then
			bad "the model pass's $p pin passes its mutant ($m)"
		else
			rtk_kmut=$((rtk_kmut + 1))
		fi
	done
done
[ "$rtk_kpins" = 9 ] && [ "$rtk_kmut" = 14 ] ||
	bad "rm_rtmodel.metal holds $rtk_kpins of its 9 pins and refuses $rtk_kmut of its 14 mutants"
# the back end: the view bound with no own-light table (model-own-ray), and exactly the kernel's seven textures
rtk_bpins=0 rtk_bmut=0
rtk_b=$rm_dir/rmb_rtmodel.m
rtk_bind() { # <file> <pin>: 0 when the file (rmb_rtmodel.m) holds the pin
	case $2 in
		own) awk '/^\t\tmv\.nself = 0u;$/ { s = NR } /^\t\t\[ce setBytes:&mv length:sizeof\(mv\) atIndex:0\];$/ { b = NR; c++ }
			END { exit !(s && b && (s < b) && (c == 1)) }' "$1" ;;
		bind) [ "$(grep -c 'setTexture' "$1")" = 7 ] &&
			grep -qxF '		[ce setTexture:depth atIndex:0];' "$1" && grep -qxF '		[ce setTexture:normal atIndex:1];' "$1" &&
			grep -qxF '		[ce setTexture:material atIndex:2];' "$1" && grep -qxF '		[ce setTexture:gbl atIndex:3];' "$1" &&
			grep -qxF '		[ce setTexture:f.vis atIndex:5];' "$1" && grep -qxF '		[ce setTexture:light atIndex:6];' "$1" &&
			grep -qxF '		[ce setTexture:f.atlas atIndex:7];' "$1" ;;
	esac
}
for p in own bind; do
	if ! rtk_bind "$rtk_b" "$p"; then
		bad "rmb_rtmodel.m does not hold the model pass's $p pin"
		continue
	fi
	rtk_bpins=$((rtk_bpins + 1))
	case $p in
		own) sed '/^		mv\.nself = 0u;$/d' ;;
		*) awk '{ print } /^\t\t\[ce setTexture:gbl atIndex:3\];$/ { print "\t\t[ce setTexture:gbl atIndex:4];" }' ;;
	esac < "$rtk_b" > "${tmp:?}/rtk-back.m"
	if cmp -s "$rtk_b" "${tmp:?}/rtk-back.m"; then
		bad "the model pass's back-end mutant ($p) changed nothing"
	elif [ -n "$rtk_metal" ] && ! $cc -fsyntax-only -fobjc-arc -I"$rm_dir" -Ithird_party/SDL3/include '-DYQ2OSTYPE="Darwin"' \
		'-DYQ2ARCH="arm64"' "${tmp:?}/rtk-back.m" > "${tmp:?}/cc.out" 2>&1; then
		cat "${tmp:?}/cc.out"
		bad "the model pass's back-end mutant ($p) does not compile, so it tests nothing"
	elif rtk_bind "${tmp:?}/rtk-back.m" "$p"; then
		bad "the model pass's back-end $p pin passes its mutant"
	else
		rtk_bmut=$((rtk_bmut + 1))
	fi
done
[ "$rtk_bpins" = 2 ] && [ "$rtk_bmut" = 2 ] ||
	bad "rmb_rtmodel.m holds $rtk_bpins of its 2 pins and refuses $rtk_bmut of its 2 mutants"
# the wiring: the kernel linked into ref_metal.metallib, the back end and the scene built
if [ "$(grep -cF 'ref_metal/rm_rtmodel.air' Makefile)" != 2 ] || ! grep -qF 'src/client/refresh/metal/rmb_rtmodel.o' Makefile ||
	! grep -qF 'src/client/refresh/metal/rm_rtmodelscene.o' Makefile || ! grep -qF 'src/client/refresh/metal/rm_rtmodel.o' Makefile; then
	bad "the model pass is not linked into ref_metal.metallib or ref_metal"
fi
# slice 3's review: the scene's wiring, which no recorded frame of the default tells apart (r_rt_gunshadow
# defaults to 0): the view weapon left out of the ray-traced scene while the models are traced
# (rm_rtscene.c's rm_rtm_gunshadow; its mutant is main's form, r_rt_gunshadow alone), and the models wanted
# traced by what this frame's passes will decide (rm_rtmodelscene.c's rm_rtm_wants with the model pass's
# refusal and RM_RTDir_Will; its mutant: the direct light only wanted, RM_RTDir_Wanted)
rtk_wpins=0 rtk_wmut=0
rtk_wire() { # <file> <pin>: 0 when the file holds the pin
	case $2 in
		gun) [ "$(grep -cE '^[[:space:]]*gun = ' "$1")" = 1 ] &&
			grep -qxF '	gun = rm_rtm_gunshadow(RM_Effect(r_rt_gunshadow) != 0, RM_RTModel_Wants(fd));' "$1" ;;
		wants) awk '/^RM_RTModel_Wants\(const refdef_t \*fd\)$/ { b = 1 } b && /^}/ { b = 0 } b' "$1" | tr -d '\n\t' |
			grep -qF 'return rm_rtm_wants(rtm_device, rtm_refused, r_rt_direct_models ? RM_Effect(r_rt_direct_models) : 0.0f,RM_RTDir_Will(fd));' ;;
	esac
}
for p in gun wants; do
	case $p in
		gun) rtk_s=$rm_dir/rm_rtscene.c ;;
		*) rtk_s=$rm_dir/rm_rtmodelscene.c ;;
	esac
	if ! rtk_wire "$rtk_s" "$p"; then
		bad "${rtk_s##*/} does not hold the model pass's $p wiring pin"
		continue
	fi
	rtk_wpins=$((rtk_wpins + 1))
	case $p in
		gun) sed 's/^	gun = rm_rtm_gunshadow(RM_Effect(r_rt_gunshadow) != 0, RM_RTModel_Wants(fd));$/	gun = RM_Effect(r_rt_gunshadow) != 0;/' ;;
		*) sed 's/^		RM_RTDir_Will(fd));$/		RM_RTDir_Wanted());/' ;;
	esac < "$rtk_s" > "${tmp:?}/rtk-scene.c"
	if cmp -s "$rtk_s" "${tmp:?}/rtk-scene.c"; then
		bad "the model pass's wiring mutant ($p) changed nothing"
	elif rtk_wire "${tmp:?}/rtk-scene.c" "$p"; then
		bad "the model pass's $p wiring pin passes its mutant"
	else
		rtk_wmut=$((rtk_wmut + 1))
	fi
done
[ "$rtk_wpins" = 2 ] && [ "$rtk_wmut" = 2 ] ||
	bad "the scene holds $rtk_wpins of the model pass's 2 wiring pins and refuses $rtk_wmut of their 2 mutants"
seg 7.95 "; model pass $rtk_kpins kernel pins with $rtk_kmut kernel mutants${rtk_metal:+ compiled and} refused, $rtk_bpins back-end pins with $rtk_bmut mutants refused, $rtk_wpins wiring pins with $rtk_wmut mutants refused"

# RT-primary's 5 x 5 filter of its accumulated light (TASK190 slice 2, PM8): tests/metal/rm_rtpdenoise_test.c
# holds common/mtl_rtpd_math.h over RT7's weights (mtl_rtd_math.h): the pixels it writes, a neighbour's
# weight, each channel with its own neighbours, the strength by the count, the trace's alpha, the clamp,
# and an image across a material and a depth edge. Header mutants, each of which must compile and fail it:
# green summed from red, the count ignored, a neighbour without a normal weighed, the history's count
# kept as the alpha, the clamp dropped.
rtpd=""
rtpd_tree() { # <dir>: a copy of the test and the headers, laid out as the test includes them
	rm -rf "${1:?}"
	mkdir -p "$1/src/client/refresh/metal/common" "$1/tests/metal"
	cp "$rm_dir/common/mtl_rtpd_math.h" "$rm_dir/common/mtl_rtp_math.h" "$rm_dir/common/mtl_rtd_math.h" \
		"$rm_dir/common/mtl_rtao_math.h" "$rm_dir/common/mtl_rtt_math.h" "$rm_dir/common/mtl_shader_math.h" \
		"$rm_dir/common/mtl_gbuf_math.h" "$1/src/client/refresh/metal/common/"
	cp tests/metal/rm_rtpdenoise_test.c "$1/tests/metal/"
}
rtpd_build() { # <tree>: 0 when the tree's test builds
	# shellcheck disable=SC2086 # flags is a word list
	$cc $flags -o "$1/t" "$1/tests/metal/rm_rtpdenoise_test.c" -lm > "${tmp:?}/cc.out" 2>&1
}
rtpd_run() { # <tree>: 0 when the tree's test builds and passes
	rtpd_build "$1" && UBSAN_OPTIONS=print_stacktrace=1 "$1/t" > "${tmp:?}/out" 2>&1
}
rtpd_tree "${tmp:?}/rtpd"
if ! rtpd_run "${tmp:?}/rtpd"; then
	cat "${tmp:?}/cc.out" "${tmp:?}/out"
	bad "tests/metal/rm_rtpdenoise_test.c fails on the real files"
else
	rtpd=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_rtpdenoise_test: //')
	case $rtpd in *", 0 failed") ;; *) bad "tests/metal/rm_rtpdenoise_test.c printed no result line"; rtpd="";; esac
fi
rtpd_mutants=0
rtpd_h=$rm_dir/common/mtl_rtpd_math.h
for m in mix count nonormal alpha clamp; do
	rtpd_tree "${tmp:?}/rtpdm"
	h=${tmp:?}/rtpdm/src/client/refresh/metal/common/mtl_rtpd_math.h
	case $m in
		mix) sed 's/a\.g = mtl_rtd_add(a\.g, w, h\.g);/a.g = mtl_rtd_add(a.g, w, h.r);/' ;;
		count) sed 's/return world \&\& (mtl_rtd_strength(n) > 0\.0f);/return world \&\& (n == n);/' ;;
		nonormal) sed 's/if (!(nn_a > 0\.5f))/if (!(nn_a > 0.5f) \&\& 0)/' ;;
		alpha) sed 's/r\.a = a;/r.a = h.a + 0.0f * a;/' ;;
		clamp) sed 's/r\.r = mtl_rtp_clamp(mtl_rtd_result(s\.r, h\.r, h\.a));/r.r = mtl_rtd_result(s.r, h.r, h.a);/' ;;
	esac < "$rtpd_h" > "$h"
	if cmp -s "$rtpd_h" "$h"; then
		bad "the RT-primary filter mutant ($m) changed nothing"
	elif ! rtpd_build "${tmp:?}/rtpdm"; then
		cat "${tmp:?}/cc.out"
		bad "the RT-primary filter mutant ($m) does not compile, so it tests nothing"
	elif rtpd_run "${tmp:?}/rtpdm"; then
		bad "tests/metal/rm_rtpdenoise_test.c passes the RT-primary filter mutant ($m)"
	else
		rtpd_mutants=$((rtpd_mutants + 1))
	fi
done
[ "$rtpd_mutants" = 5 ] || bad "the RT-primary filter test refused $rtpd_mutants of its 5 header mutants"
# The kernel, rm_rtpdenoise.metal, pinned as text: the pixel's and each neighbour's distance and material
# from the history's key, the neighbour's count and normal reaching the weight (mutants: the material
# compared with itself, the count not passed), the history read for the values (mutant: the trace's light
# read in its place), the neighbours bounded on all four sides (mutant: the right and bottom bounds
# dropped), the pixel's history filtered and not the raw trace (mutant: the trace's light in its place),
# and only the light written (mutant: the history written). Where the Metal compiler
# is there, the kernel and every mutant must compile. The back end dispatches it in the accumulation's
# encoder after rm_rtp_temporal, behind a texture barrier and before the histories swap; the Makefile
# compiles and links the unit.
rtpd_body() { # <file>: rm_rtp_denoise's code, comments and literals blanked, whitespace squeezed
	strip "$1" | awk '/^rm_rtp_denoise\(/ { b = 1 } b && /^}/ { b = 0 } b { print }' | tr -s ' \t' '  '
}
rtpd_pin() { # <file> <pin>: 0 when the file holds the pin
	case $2 in
		weight) rtpd_body "$1" | grep -qF 'mtl_rtpd_weight(dx, dy, d, as_type<float>(kn.x), nc, nn, m, kn.y, hn.a, nv.a)' &&
			rtpd_body "$1" | grep -qF 'const uint m = kc.y;' ;;
		hist) rtpd_body "$1" | grep -qF 'acc = mtl_rtpd_add(acc, wt, mtl_rtpd_rgba(hn));' &&
			rtpd_body "$1" | grep -qF 'const float4 hn = hist.read(sp);' ;;
		bounds) rtpd_body "$1" | grep -qF 'if ((sx < 0) || (sy < 0) || (sx >= (int)w) || (sy >= (int)hh))' ;;
		result) rtpd_body "$1" | grep -qF 'mtl_rtpd_result(acc, mtl_rtpd_rgba(hc), light_in.read(px).a)' ;;
		writes) [ "$(strip "$1" | grep -oE '[A-Za-z_][A-Za-z0-9_]*\.write\(' | tr -d '\n')" = 'light_out.write(' ] &&
			! strip "$1" | grep -E 'access::(read_)?write>' | grep -vqE ' light_out \[\[texture' ;;
	esac
}
rtpd_kpins=0 rtpd_kmut=0
rtpd_k=$rm_dir/rm_rtpdenoise.metal
if [ ! -f "$rtpd_k" ]; then
	bad "rm_rtpdenoise.metal is missing"
elif [ -n "$rtp_metal" ] && ! rtp_compile "$rtpd_k"; then
	cat "${tmp:?}/metal.out"
	bad "rm_rtpdenoise.metal does not compile"
fi
for p in weight hist bounds result writes; do
	if [ ! -f "$rtpd_k" ] || ! rtpd_pin "$rtpd_k" "$p"; then
		bad "rm_rtpdenoise.metal does not hold the filter's $p pin"
		continue
	fi
	rtpd_kpins=$((rtpd_kpins + 1))
	case $p in
		weight) kms="samemat nocount" ;;
		hist) kms="trace" ;;
		bounds) kms="halfbound" ;;
		result) kms="rawtrace" ;;
		*) kms="histw" ;;
	esac
	for m in $kms; do
		case $m in
			samemat) sed 's/nc, nn, m, kn\.y, hn\.a, nv\.a)/nc, nn, m, m, hn.a, nv.a)/' ;;
			nocount) sed 's/nc, nn, m, kn\.y, hn\.a, nv\.a)/nc, nn, m, kn.y, 1.0f, nv.a)/' ;;
			trace) sed 's/const float4 hn = hist\.read(sp);/const float4 hn = float4(light_in.read(sp).rgb, hist.read(sp).a);/' ;;
			halfbound) sed 's/if ((sx < 0) || (sy < 0) || (sx >= (int)w) || (sy >= (int)hh))/if ((sx < 0) || (sy < 0))/' ;;
			rawtrace) sed 's/mtl_rtpd_result(acc, mtl_rtpd_rgba(hc), /mtl_rtpd_result(acc, mtl_rtpd_rgba(light_in.read(px)), /' ;;
			histw) awk '/texture\(8\)/ { sub(/access::read> hist/, "access::read_write> hist") }
				/light_out\.write\(/ { print; print "\t\thist.write(float4(0.0f), px);"; next } { print }' ;;
		esac < "$rtpd_k" > "${tmp:?}/rtpd-kernel.metal"
		if cmp -s "$rtpd_k" "${tmp:?}/rtpd-kernel.metal"; then
			bad "the RT-primary filter kernel mutant ($m) changed nothing"
		elif [ -n "$rtp_metal" ] && ! rtp_compile "${tmp:?}/rtpd-kernel.metal"; then
			cat "${tmp:?}/metal.out"
			bad "the RT-primary filter kernel mutant ($m) does not compile, so it tests nothing"
		elif rtpd_pin "${tmp:?}/rtpd-kernel.metal" "$p"; then
			bad "the filter's $p pin passes its mutant ($m)"
		else
			rtpd_kmut=$((rtpd_kmut + 1))
		fi
	done
done
[ "$rtpd_kpins" = 5 ] && [ "$rtpd_kmut" = 6 ] ||
	bad "rm_rtpdenoise.metal holds $rtpd_kpins of its 5 pins and refuses $rtpd_kmut of its 6 mutants"
# the back end: in rmb_rtdir_accumulate, the temporal dispatch, then a texture barrier, the filter's
# pipeline and its dispatch, then the swap of the histories, in that order
if ! awk '/^rmb_rtdir_accumulate\(/ { b = 1 } b && /^}/ { b = 0 } b { print }' "$rm_dir/rmb_rtdir.m" |
	grep -oE 'setComputePipelineState:tmp_pipe|memoryBarrierWithScope:MTLBarrierScopeTextures|setComputePipelineState:dn_pipe|dispatchThreads|hist_cur = 1 - hist_cur' |
	tr '\n' ' ' | grep -qF 'setComputePipelineState:tmp_pipe dispatchThreads memoryBarrierWithScope:MTLBarrierScopeTextures setComputePipelineState:dn_pipe dispatchThreads hist_cur = 1 - hist_cur'; then
	bad "rmb_rtdir.m: rmb_rtdir_accumulate does not dispatch the filter after the accumulation, behind a barrier, before the swap"
fi
if [ "$(grep -cF 'ref_metal/rm_rtpdenoise.air' Makefile)" != 2 ] ||
	[ "$(grep -cF '@"rm_rtp_denoise"' "$rm_dir/rmb_rtdir.m")" != 1 ]; then
	bad "the RT-primary filter is not linked into ref_metal.metallib or its pipeline is not made by rmb_rtdir.m"
fi
seg 7.8 "; RT-primary filter ${rtpd:-host test}, $rtpd_mutants rule mutants, $rtpd_kpins kernel pins with $rtpd_kmut kernel mutants refused${rtp_metal:+ and compiled}"

if [ "$fail" = 0 ]; then
	echo "check-ref-metal: ok ($(sort -n "${tmp:?}/summary" | cut -f2- | tr -d '\n'))"
fi
exit $fail
