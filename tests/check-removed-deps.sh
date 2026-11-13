#!/bin/sh
# Invariant: dependencies this fork removed stay removed.
#
# The trust-boundary epic (docs/plans/trust-boundary.md, decision record
# docs/decisions/0001-remove-optional-dependencies.md) deleted OpenAL,
# libcurl, minizip/miniz, the GLES renderers, the CMake build and SDL2 (and
# with it the WITH_SDL3 switch). This fails if any of them comes back into the
# build: a source directory, a Makefile switch, an include or a CI install
# line. SDL2 is matched case-insensitively for the name SDL2, SDL_VERSION_ATLEAST(2
# and a bare <SDL.h> include, over src, Makefile, .github and stuff/misc.
#
# Usage: tests/check-removed-deps.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C

fail=0
for path in src/client/curl src/common/unzip src/client/sound/openal.c src/client/sound/qal.c \
	src/client/refresh/gl3/glad-gles3 CMakeLists.txt; do
	if [ -e "$path" ]; then
		echo "check-removed-deps: $path is back"
		fail=1
	fi
done

# Word-bounded on purpose: "angles", "triangles_max" and stb's "unzigzag"
# must not match.
pattern='USE_CURL|WITH_CURL|curl/curl\.h|libcurl|USE_OPENAL|WITH_OPENAL|AL/alc?\.h|libopenal|WITH_SYSTEM_MINIZIP|unzip\.h|miniz\.h|-lminizip|ref_gles[0-9]|YQ2_GL3_GLES|USE_GLES|WITH_SDL3|USE_SDL3'
hits=$(git grep -nE "$pattern" -- src Makefile .github stuff/misc 2>/dev/null || true)
if [ -n "$hits" ]; then
	echo "check-removed-deps: references to removed dependencies:"
	echo "$hits" | sed 's/^/  /'
	fail=1
fi

# SDL2 has to be caught by every route, so this scan ignores case: the name
# in any spelling (sdl2-config, libSDL2, -lSDL2, WITH_SDL2, glimp_sdl2,
# input/sdl2, sdl2-compat), a version test written for 2.x
# (SDL_VERSION_ATLEAST(2, ...)) and SDL2's bare <SDL.h> include (SDL3 headers
# are <SDL3/SDL.h>, which this does not match).
sdl2='SDL2|SDL_VERSION_ATLEAST[[:space:]]*\([[:space:]]*2|[<"]SDL\.h[>"]'
hits=$(git grep -niE "$sdl2" -- src Makefile .github stuff/misc 2>/dev/null || true)
if [ -n "$hits" ]; then
	echo "check-removed-deps: references to SDL2:"
	echo "$hits" | sed 's/^/  /'
	fail=1
fi
[ "$fail" -eq 0 ] && echo "check-removed-deps: ok"
exit "$fail"
