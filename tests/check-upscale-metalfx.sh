#!/bin/sh
# Invariant (TASK124, decision 0012): the metalfx backend of
# upscale_textures (src/client/cl_upscale_metalfx.m) scales only inside
# a run, refuses with its reason when unsupported (and the menu's check
# then gives that reason), and on a Metal device with MetalFX scales a
# synthetic tile at 2x and 4x to the same bytes every time, with
# MetalFX's alpha 255 everywhere, which is why CL_UpscaleRGBA sets the
# alpha from the indices.
#
# It builds tests/metal/upscale_metalfx_test.m with the backend file
# alone and the test-only switch UPSCALE_METALFX_TEST (with ASAN and
# UBSAN where the compiler has them), and runs it under a time bound.
# The padding, crop and alpha of the tile are unit tests
# (tests/unit/test_cl_upscale.c). Only on macOS: elsewhere the backend
# is not built and the check says it skipped.

set -u

cd "$(dirname "$0")/.." || exit 1

if [ "$(uname -s)" != Darwin ]; then
	echo "check-upscale-metalfx: skipped: not macOS"
	exit 0
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/upscale-metalfx.XXXXXX") || exit 1
trap 'rm -rf "${tmp:?}"' EXIT

cc=${CC:-cc}
bound=${UPSCALE_METALFX_CHECK_SECONDS:-120}

case $bound in
	''|*[!0-9]*)
		echo "check-upscale-metalfx: UPSCALE_METALFX_CHECK_SECONDS is not a whole number of seconds"
		exit 1
		;;
esac

san="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
printf 'int main(void){return 0;}\n' > "${tmp:?}/probe.c"

if ! { $cc $san -o "${tmp:?}/probe" "${tmp:?}/probe.c" >/dev/null 2>&1 && "${tmp:?}/probe" >/dev/null 2>&1; }; then
	echo "check-upscale-metalfx: $cc cannot build with ASAN and UBSAN; running without them"
	san=
fi

# shellcheck disable=SC2086 # san is a word list
if ! $cc -fobjc-arc -std=gnu11 -Wall -Wextra -Werror -g -O1 $san \
	-DUPSCALE_METALFX_TEST -Isrc/client -o "${tmp:?}/upscale_metalfx_test" \
	tests/metal/upscale_metalfx_test.m src/client/cl_upscale_metalfx.m \
	-framework Foundation -framework Metal -weak_framework MetalFX \
	> "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	echo "check-upscale-metalfx: the host test does not build"
	exit 1
fi

# the test runs in the background and is killed once the bound has passed
ASAN_OPTIONS=abort_on_error=0 "${tmp:?}/upscale_metalfx_test" > "${tmp:?}/run.out" 2>&1 &
pid=$!
(
	n=0
	while [ "$n" -lt "$bound" ] && kill -0 "$pid" 2>/dev/null; do
		perl -e 'select(undef, undef, undef, 1)'
		n=$((n + 1))
	done
	if kill -0 "$pid" 2>/dev/null; then
		: > "${tmp:?}/expired"
		kill "$pid" 2>/dev/null
	fi
) &
watchdog=$!
status=0
wait "$pid" || status=$?
kill "$watchdog" 2>/dev/null
wait "$watchdog" 2>/dev/null

cat "${tmp:?}/run.out"

if [ -e "${tmp:?}/expired" ]; then
	echo "check-upscale-metalfx: the host test outlived its $bound s bound and was killed"
	exit 1
fi

if [ "$status" != 0 ]; then
	echo "check-upscale-metalfx: the host test failed (exit $status)"
	exit 1
fi

if grep -q 'skipped: no Metal device' "${tmp:?}/run.out"; then
	echo "check-upscale-metalfx: skipped: no Metal device"
	exit 0
fi

last=$(tail -n 1 "${tmp:?}/run.out")

case $last in
	"upscale_metalfx_test: "*" passed, 0 failed") ;;
	*)
		echo "check-upscale-metalfx: the host test printed no result line"
		exit 1
		;;
esac

echo "check-upscale-metalfx: OK (${last#upscale_metalfx_test: })"
