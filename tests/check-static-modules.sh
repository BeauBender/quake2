#!/bin/sh
# Invariant: the static-link build mode (Makefile STATIC_MODULES=1,
# TASK20) links the game and one renderer into the client executable
# with nothing shared between the three, and loads no library.
#
# Every decision here is taken from the build's artifacts (nm, otool,
# the executable's strings), never from a line the engine prints.
#
# Default (the gate): "make STATIC_MODULES=1 static_modules" in a scratch
# STATIC_ROOT builds the game module and the soft renderer (the objects
# only, no SDL link) and checks each combined object:
#   (a) its only global definition is its entry point (_GetGameAPI,
#       _GetRefAPI), so no helper of one module can be named by another
#       module or by the client, and none interposes;
#   (b) a helper every module carries (_Q_strlcpy, from shared.c) is a
#       local definition in it, and is not left undefined, so it binds
#       to the module's own copy.
# --full (CI, macOS job): additionally builds the whole client
# executable ("make STATIC_MODULES=1 client", which links SDL and takes
# minutes cold) and checks it:
#   (c) otool -L lists no game or renderer dylib, and the output
#       directory holds the executable alone;
#   (d) _GetGameAPI and _GetRefAPI are defined in it;
#   (e) no global symbol is defined twice in it;
#   (f) none of the names the dynamic loaders look up (GetGameAPI,
#       GetRefAPI, game.dylib) is a string in it: the loaders were
#       compiled out, not merely not reached;
#   (g) nothing a module leaves undefined is defined by the client's
#       objects: the modules bind to libc, SDL and frameworks only.
#
# Build time, measured here (6 cores, -j4): the default about 5 s, every
# object compiled fresh into the scratch directory. --full about 15 s when
# build/<release|debug>/sdl3/libSDL3.a already exists and about 95 s when
# it has to be built too, which a fresh checkout has not done (the gate
# builds no SDL). So the gate runs the default, and CI's macOS job, which
# builds everything first, runs --full.
#
# Only macOS builds this mode; on another OS the check says so and exits
# 0. On macOS a missing tool or a failed build is a failure, never a skip.
#
# STATIC_CHECK_MAKE_ARGS adds make arguments, to run the check against a
# deliberately broken build.
#
# Usage: tests/check-static-modules.sh [--full]
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C
unset MAKEFLAGS GNUMAKEFLAGS MFLAGS

full=0
case "${1:-}" in
"") ;;
--full) full=1 ;;
*) echo "usage: tests/check-static-modules.sh [--full]" >&2; exit 2 ;;
esac

if [ "$(uname -s)" != Darwin ]; then
	echo "check-static-modules: skipped: STATIC_MODULES=1 is for macOS only (docs/decisions/0008)"
	exit 0
fi

fail=0
bad() { echo "check-static-modules: $*"; fail=1; }

for tool in make nm otool strings ld awk sort comm uniq; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "check-static-modules: $tool not found: a missing tool is a failure"
		exit 1
	fi
done

tmp=$(mktemp -d "${TMPDIR:-/tmp}/static-modules.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT
root=$tmp/static
jobs=${CHECK_JOBS:-4}
renderer=soft
extra=${STATIC_CHECK_MAKE_ARGS:-}

start=$(date +%s)
target=static_modules
[ "$full" = 1 ] && target=client
# shellcheck disable=SC2086 # extra is a word list
if ! make -j"$jobs" QUIET=1 STATIC_MODULES=1 STATIC_RENDERER=$renderer STATIC_ROOT="$root" $extra $target >"$tmp/build.log" 2>&1; then
	tail -n 30 "$tmp/build.log"
	echo "check-static-modules: the build failed (make STATIC_MODULES=1 $target)"
	exit 1
fi
elapsed=$(( $(date +%s) - start ))

game=$root/obj/static/game.o
ref=$root/obj/static/ref_$renderer.o
for f in "$game" "$ref"; do
	if [ ! -s "$f" ]; then
		echo "check-static-modules: $f was not produced"
		exit 1
	fi
done

# the global definitions of an object file or executable, one name per line
defined() { nm -g -U "$1" | awk 'NF == 3 { print $3 }' | sort; }
# the names it leaves undefined
undefined() { nm -u "$1" | awk 'NF == 2 { print $2 } NF == 1 { print $1 }' | sort -u; }

check_module() # <object> <entry point>
{
	obj=$1
	entry=$2
	got=$(defined "$obj")
	if [ "$got" != "$entry" ]; then
		bad "$obj defines $(printf '%s\n' "$got" | grep -c .) globals, not only $entry: $(printf '%s\n' "$got" | head -n 6 | tr '\n' ' ')"
	fi
	# the module's own copy of shared.c: local (lower case t), not
	# global, and not left for another definition to satisfy
	if ! nm "$obj" | awk '$3 == "_Q_strlcpy" && $2 == "t" { f = 1 } END { exit !f }'; then
		bad "$obj has no local definition of _Q_strlcpy"
	fi
	if undefined "$obj" | awk '$1 == "_Q_strlcpy" { f = 1 } END { exit !f }'; then
		bad "$obj leaves _Q_strlcpy undefined, so it would bind outside the module"
	fi
}

check_module "$game" _GetGameAPI
check_module "$ref" _GetRefAPI

if [ "$full" = 1 ]; then
	exe=$root/bin/quake2
	if [ ! -x "$exe" ]; then
		echo "check-static-modules: $exe was not produced"
		exit 1
	fi

	libs=$(otool -L "$exe")
	if printf '%s\n' "$libs" | grep -E '(game|ref_[a-z0-9_]+)\.(dylib|so)' >/dev/null; then
		bad "the executable links a game or renderer library:"
		printf '%s\n' "$libs" | grep -E '(game|ref_[a-z0-9_]+)\.(dylib|so)'
	fi
	left=$(ls "$root/bin")
	if [ "$left" != quake2 ]; then
		bad "the output directory holds more than the executable: $left"
	fi

	exports=$(defined "$exe")
	for sym in _GetGameAPI _GetRefAPI; do
		if ! printf '%s\n' "$exports" | grep -x "$sym" >/dev/null; then
			bad "$sym is not defined in the executable"
		fi
	done

	dups=$(printf '%s\n' "$exports" | uniq -d | tr '\n' ' ')
	if [ -n "$dups" ]; then
		bad "global symbols defined twice in the executable: $dups"
	fi

	# the names the dynamic loaders look for, as strings
	if strings -a "$exe" | grep -E '^(GetGameAPI|GetRefAPI|game\.dylib|game\.so)$' >/dev/null; then
		bad "the executable still carries a loader's module name (GetGameAPI, GetRefAPI or game.dylib)"
	fi

	# every global the client's own objects define, against what the modules leave undefined
	find "$root/obj/client" -name '*.o' | sort > "$tmp/client.list"
	: > "$tmp/clientdefs"
	while read -r o; do
		defined "$o" >> "$tmp/clientdefs"
	done < "$tmp/client.list"
	sort -u "$tmp/clientdefs" -o "$tmp/clientdefs"
	for m in "$game" "$ref"; do
		undefined "$m" > "$tmp/undef"
		both=$(comm -12 "$tmp/clientdefs" "$tmp/undef" | tr '\n' ' ')
		if [ -n "$both" ]; then
			bad "$m leaves undefined what the client defines: $both"
		fi
	done
fi

echo "check-static-modules: build $elapsed s ($target)"
if [ "$fail" -ne 0 ]; then
	exit 1
fi
echo "check-static-modules: ok"
