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
