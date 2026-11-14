#!/bin/sh
# Invariant: the Metal shim of ref_gl1 (src/client/refresh/gl1/metal/,
# docs/plans/metal-shim-design.md) keeps its pure-C contracts and, where
# the toolchain is there, still builds.
#
# (a) Always, on every OS: tests/metal/mgl_host_test.c is compiled with
#     the system compiler against mgl_raster.c (mgl_compute_raster,
#     mgl_read_contract), mgl_state.c (validators, matrix stacks) and
#     mgl_tex.c (glPixelStorei) and mgl_draw.c (draw assembly, blend
#     indices), with ASAN and UBSAN when the compiler has
#     them, and run. No SDL, no Metal, no game data.
# (b) Only on Darwin with the Metal compiler (xcrun -f metal) and the
#     vendored SDL 3 headers (the Makefile's SDLCFLAGS there is
#     -Ithird_party/SDL3/include; they must compile <SDL3/SDL.h>):
#     "make ref_gl1 WITH_METAL_SHIM=yes" in a scratch copy of Makefile,
#     third_party/sdl3.make and src/ (third_party/SDL3 is linked, not
#     copied), so release/ and build/ of this
#     checkout are not touched; the dylib must link Metal and not OpenGL.
#     Otherwise one line "check-metal-shim: build skipped: <reason>".
# (c) The coverage report (tests/metal/shim-coverage.sh): always its text
#     half (every GL entry point gl1 calls or loads by pointer has a row
#     in the table of docs/plans/metal-shim-design.md section 14 and a
#     proc-table entry); after the build of (b) also its object half
#     (the shim defines every one, the dylib leaves no _gl undefined).
set -eu
cd "$(dirname "$0")/.."

fail=0
bad() { echo "check-metal-shim: $*"; fail=1; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/metal-shim.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

cc=${CC:-cc}
mgl=src/client/refresh/gl1/metal
srcs="tests/metal/mgl_host_test.c $mgl/mgl_raster.c $mgl/mgl_state.c $mgl/mgl_tex.c $mgl/mgl_draw.c"
flags="-std=gnu99 -Wall -Werror -g -O1 -fno-strict-aliasing -fwrapv"
san="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"

# (a) the host test, with sanitizers when this compiler can build and run with them
printf 'int main(void){return 0;}\n' > "$tmp/probe.c"
if $cc $san -o "$tmp/probe" "$tmp/probe.c" >/dev/null 2>&1 && "$tmp/probe" >/dev/null 2>&1; then
	flags="$flags $san"
else
	echo "check-metal-shim: $cc cannot build with ASAN and UBSAN; running without them"
fi

# shellcheck disable=SC2086 # flags and srcs are word lists
if ! $cc $flags -o "$tmp/mgl_host_test" $srcs -lm > "$tmp/cc.out" 2>&1; then
	cat "$tmp/cc.out"
	bad "tests/metal/mgl_host_test.c does not compile"
elif ! ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 "$tmp/mgl_host_test" > "$tmp/out" 2>&1; then
	cat "$tmp/out"
	bad "tests/metal/mgl_host_test.c failed"
else
	host=$(tail -n 1 "$tmp/out" | sed 's/^mgl_host_test: //')
fi

# (b) the shim build, where it can be built
skip=""
covobjs=""
if [ "$(uname -s)" != Darwin ]; then
	skip="not macOS"
elif ! command -v xcrun >/dev/null 2>&1 || ! xcrun -f metal >/dev/null 2>&1; then
	skip="no Metal compiler (xcrun -f metal)"
elif ! command -v make >/dev/null 2>&1; then
	skip="no make"
else
	printf 'include Makefile\nmgl-print-sdlcflags:\n\t@echo $(SDLCFLAGS)\n' > "$tmp/print.mk"
	sdlcflags=$(make -s -f "$tmp/print.mk" mgl-print-sdlcflags 2>/dev/null || true)
	printf '#include <SDL3/SDL.h>\nint main(void){return 0;}\n' > "$tmp/sdl.c"
	# shellcheck disable=SC2086 # SDLCFLAGS is a word list
	if [ -z "$sdlcflags" ] || ! $cc $sdlcflags -fsyntax-only "$tmp/sdl.c" >/dev/null 2>&1; then
		skip="the vendored SDL 3 headers are missing (SDLCFLAGS cannot compile <SDL3/SDL.h>)"
	fi
fi

if [ -n "$skip" ]; then
	echo "check-metal-shim: build skipped: $skip"
	build="build skipped"
else
	mkdir "$tmp/tree"
	cp -R Makefile src "$tmp/tree/"
	mkdir "$tmp/tree/third_party"
	cp third_party/sdl3.make "$tmp/tree/third_party/"
	ln -s "$PWD/third_party/SDL3" "$tmp/tree/third_party/SDL3"
	jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
	if ! (unset MAKEFLAGS GNUMAKEFLAGS MFLAGS; make -C "$tmp/tree" -j"$jobs" ref_gl1 WITH_METAL_SHIM=yes) > "$tmp/build.out" 2>&1; then
		tail -n 30 "$tmp/build.out"
		bad "make ref_gl1 WITH_METAL_SHIM=yes failed"
	elif [ ! -f "$tmp/tree/release/ref_gl1.metallib" ]; then
		bad "the shim build wrote no release/ref_gl1.metallib"
	else
		libs=$(otool -L "$tmp/tree/release/ref_gl1.dylib")
		case $libs in *Metal.framework*) ;; *) bad "the shim build of ref_gl1.dylib does not link Metal";; esac
		case $libs in *OpenGL.framework*) bad "the shim build of ref_gl1.dylib links OpenGL";; esac
		covobjs="$tmp/tree/build/release/ref_gl1_metal"
	fi
	build="shim built"
fi

# (c) the coverage report, with the objects of (b) when it built them
covrc=0
if [ -n "$covobjs" ]; then
	covout=$(sh tests/metal/shim-coverage.sh "$covobjs" "$tmp/tree/release/ref_gl1.dylib" 2>&1) || covrc=$?
else
	covout=$(sh tests/metal/shim-coverage.sh 2>&1) || covrc=$?
fi
printf '%s\n' "$covout"
[ "$covrc" = 0 ] || bad "coverage report failed"

if [ "$fail" = 0 ]; then
	echo "check-metal-shim: ok (${host:-host test}; $build)"
fi
exit $fail
