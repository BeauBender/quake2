#!/bin/sh
# Invariant: dependencies this fork removed stay removed.
#
# The trust-boundary epic (docs/plans/trust-boundary.md, decision record
# docs/decisions/0001-remove-optional-dependencies.md) deleted OpenAL,
# libcurl, minizip/miniz, the GLES renderers and the CMake build. This
# fails if any of them comes back into the build: a source directory, a
# Makefile switch, an include or a CI install line.
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
pattern='USE_CURL|WITH_CURL|curl/curl\.h|libcurl|USE_OPENAL|WITH_OPENAL|AL/alc?\.h|libopenal|WITH_SYSTEM_MINIZIP|unzip\.h|miniz\.h|-lminizip|ref_gles[0-9]|YQ2_GL3_GLES|USE_GLES'
hits=$(git grep -nE "$pattern" -- src Makefile .github stuff/misc 2>/dev/null || true)
if [ -n "$hits" ]; then
	echo "check-removed-deps: references to removed dependencies:"
	echo "$hits" | sed 's/^/  /'
	fail=1
fi
[ "$fail" -eq 0 ] && echo "check-removed-deps: ok"
exit "$fail"
