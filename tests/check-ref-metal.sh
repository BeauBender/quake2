#!/bin/sh
# Invariant: the native Metal renderer ref_metal (src/client/refresh/metal/,
# docs/plans/ref-metal-native.md section 10) keeps its pure-C contracts,
# speaks no OpenGL, and, where the toolchain is there, still builds.
#
# (a) Always, on every OS: tests/metal/rm_host_test.c is compiled with the
#     system compiler against rm_pure.c, with ASAN and UBSAN when the
#     compiler has them, and run. No SDL, no Metal, no game data.
# (b) Only on Darwin with the Metal compiler (xcrun -f metal) and the
#     vendored SDL 3 headers: "make ref_metal" in a scratch copy of
#     Makefile, third_party/sdl3.make and src/ (third_party/SDL3 is linked,
#     not copied), so release/ and build/ of this checkout are not
#     touched; otool -L of the dylib must list Metal and QuartzCore and no
#     OpenGL. In the same copy "make ref_metal YQ2_OSTYPE=Linux" must fail
#     with a line naming macOS. Otherwise one line
#     "check-ref-metal: build skipped: <reason>".
# (c) Always: the vocabulary check. Under src/client/refresh/metal/ no
#     gl[A-Z] call, no GL_ token, no <OpenGL/ include, no include of a
#     gl1/ path, and no symbolic link (which could hide either). It runs
#     on scratch mutants first and must fail each of them, then on the
#     tree. A grep that errors fails the check.
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
else
	echo "check-ref-metal: $cc cannot build with ASAN and UBSAN; running without them"
fi

host=""
# shellcheck disable=SC2086 # flags is a word list
if ! $cc $flags -o "${tmp:?}/rm_host_test" tests/metal/rm_host_test.c "$rm_dir/rm_pure.c" > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	bad "tests/metal/rm_host_test.c does not compile"
elif ! UBSAN_OPTIONS=print_stacktrace=1 "${tmp:?}/rm_host_test" > "${tmp:?}/out" 2>&1; then
	cat "${tmp:?}/out"
	bad "tests/metal/rm_host_test.c failed"
else
	host=$(tail -n 1 "${tmp:?}/out" | sed 's/^rm_host_test: //')
	case $host in *", 0 failed") ;; *) bad "tests/metal/rm_host_test.c printed no result line"; host="";; esac
fi

# (c) the vocabulary check of one directory: prints what it found, 1 on a finding or a tool error
vocab() {
	d=$1 found=0
	if [ -L "$d" ] || [ ! -d "$d" ]; then
		echo "  $d is not a directory (or is a symbolic link)"
		return 1
	fi
	links=$(find "$d" -type l -print) || { echo "  find failed on $d"; return 1; }
	if [ -n "$links" ]; then
		printf '  symbolic link: %s\n' $links
		found=1
	fi
	for pat in '(^|[^A-Za-z0-9_])gl[A-Z][A-Za-z0-9_]*[[:space:]]*\(' \
		'(^|[^A-Za-z0-9_])GL_[A-Za-z0-9_]' \
		'#[[:space:]]*(include|import)[[:space:]]*<OpenGL/' \
		'#[[:space:]]*(include|import)[[:space:]]*[<"]([^>"]*/)?gl1/'; do
		rc=0
		grep -rnE --include='*.c' --include='*.h' --include='*.m' --include='*.metal' "$pat" "$d" > "${tmp:?}/vocab.out" 2>&1 || rc=$?
		case $rc in
			0) sed 's/^/  /' "${tmp:?}/vocab.out"; found=1 ;;
			1) ;;
			*) echo "  grep failed ($rc) on $d"; cat "${tmp:?}/vocab.out"; return 1 ;;
		esac
	done
	return $found
}

# the mutants: each must fail the check; the clean file must pass it
mkdir "${tmp:?}/mut"
printf '/* no GL here: glossary, gl_texturemode */\nint rm_glossary;\n' > "${tmp:?}/mut/clean.c"
vocab "${tmp:?}/mut" > "${tmp:?}/mut.out" || { cat "${tmp:?}/mut.out"; bad "the vocabulary check fails a clean file"; }
mutants=0
for m in 'glClear(GL_COLOR_BUFFER_BIT);' 'x = glGetError ();' 'int y = GL_RGBA;' \
	'#include <OpenGL/gl.h>' '#import <OpenGL/gl3.h>' '#include "../gl1/header/local.h"' \
	'#include <gl1/gl1.h>' 'link'; do
	rm -rf "${tmp:?}/mut"
	mkdir "${tmp:?}/mut"
	cp "${tmp:?}/probe.c" "${tmp:?}/mut/ok.c"
	if [ "$m" = link ]; then
		ln -s "$PWD/src/client/refresh/gl1/gl1_main.c" "${tmp:?}/mut/hidden.c"
	else
		printf '%s\n' "$m" > "${tmp:?}/mut/m.m"
	fi
	if vocab "${tmp:?}/mut" > "${tmp:?}/mut.out"; then
		bad "the vocabulary check passes a mutant ($m)"
	else
		mutants=$((mutants + 1))
	fi
done
# a tool error fails it too
if vocab "${tmp:?}/no-such-dir" > "${tmp:?}/mut.out"; then
	bad "the vocabulary check passes a missing directory"
fi

if ! vocab "$rm_dir" > "${tmp:?}/vocab.tree"; then
	cat "${tmp:?}/vocab.tree"
	bad "$rm_dir speaks OpenGL or holds a symbolic link"
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
	sdlcflags=$(make -s -f "${tmp:?}/print.mk" rm-print-sdlcflags 2>/dev/null || true)
	printf '#include <SDL3/SDL.h>\nint main(void){return 0;}\n' > "${tmp:?}/sdl.c"
	# shellcheck disable=SC2086 # SDLCFLAGS is a word list
	if [ -z "$sdlcflags" ] || ! $cc $sdlcflags -fsyntax-only "${tmp:?}/sdl.c" >/dev/null 2>&1; then
		skip="the vendored SDL 3 headers are missing (SDLCFLAGS cannot compile <SDL3/SDL.h>)"
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
	ln -s "$PWD/third_party/SDL3" "${tmp:?}/tree/third_party/SDL3"
	jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
	[ "$jobs" -le 4 ] || jobs=4
	if ! (unset MAKEFLAGS GNUMAKEFLAGS MFLAGS; make -C "${tmp:?}/tree" -j"$jobs" ref_metal) > "${tmp:?}/build.out" 2>&1; then
		tail -n 30 "${tmp:?}/build.out"
		bad "make ref_metal failed"
	elif [ ! -f "${tmp:?}/tree/release/ref_metal.dylib" ]; then
		bad "make ref_metal wrote no release/ref_metal.dylib"
	else
		libs=$(otool -L "${tmp:?}/tree/release/ref_metal.dylib" 2>&1) || bad "otool -L failed on ref_metal.dylib"
		case $libs in *Metal.framework*) ;; *) bad "ref_metal.dylib does not link Metal";; esac
		case $libs in *QuartzCore.framework*) ;; *) bad "ref_metal.dylib does not link QuartzCore";; esac
		case $libs in *OpenGL.framework*) bad "ref_metal.dylib links OpenGL";; esac
		case $libs in *libSDL3*) bad "ref_metal.dylib links an SDL of its own";; esac
	fi
	rc=0
	(unset MAKEFLAGS GNUMAKEFLAGS MFLAGS; make -C "${tmp:?}/tree" -s ref_metal YQ2_OSTYPE=Linux) > "${tmp:?}/other.out" 2>&1 || rc=$?
	if [ "$rc" = 0 ] || ! grep -q 'macOS' "${tmp:?}/other.out"; then
		tail -n 5 "${tmp:?}/other.out"
		bad "make ref_metal on another OS does not fail naming macOS"
	fi
	build="ref_metal built"
fi

if [ "$fail" = 0 ]; then
	echo "check-ref-metal: ok (${host:-host test}; $mutants vocabulary mutants refused; $build)"
fi
exit $fail
