#!/bin/sh
# Invariant (TASK167): the vt backend of upscale_textures
# (src/client/cl_upscale_vt.m, VideoToolbox's super-resolution scaler)
# scales only inside a run, reads as unsupported with its reason when
# isSupported says no (and the menu's check then gives that reason),
# and where the model is on the machine scales a synthetic tile at 2x
# and 4x to the same bytes twice, with alpha 255 everywhere, which is
# why CL_UpscaleRGBA sets the alpha from the indices, and keeps the
# colour of flat tiles (TASK167 round 1: a half-float copy that read
# the bytes back near white). The menu's check greys by the factor
# only, remembering nothing a run found. It never fetches the model:
# without it the scaling cases say they skipped. Before building, it
# checks that only the menu item (src/client/menu/menu.c) calls the
# model's fetch, so no command can start a download.
#
# It builds tests/metal/upscale_vt_test.m with the backend file alone
# and the test-only switch UPSCALE_VT_TEST (with ASAN and UBSAN where the
# compiler has them), and runs it under a time bound. What a run does
# with the checks (CL_UpscaleVTState) and the forced fallback line are
# unit tests (tests/unit/test_cl_upscale.c). Only on macOS: elsewhere
# the backend is not built and the check says it skipped.
#
# UPSCALE_VT_REQUIRE=1 (opt in) makes "unsupported here" and "the model
# is not on this machine" fail the check: set it on a host known to have
# the model, so a run that quietly tested nothing is red.
# UPSCALE_VT_CHECK_SECONDS bounds the test's run (default 120).

set -u

cd "$(dirname "$0")/.." || exit 1

# The model's download starts from the menu item only (decision 0013): no
# file but menu.c calls a fetch, so no command, stuffed text among them,
# can start one. Definitions (the name at the start of a line) and the
# header's declarations are not calls.
calls=$(grep -rn -E 'CL_UpscaleVTFetch(Start)?[[:space:]]*\(' src |
	grep -v -E '^[^:]+:[0-9]+:CL_UpscaleVTFetch' |
	grep -v -E '^src/client/header/upscale_vt\.h:' |
	grep -v -E '^src/client/menu/menu\.c:')

if [ -n "$calls" ]; then
	printf '%s\n' "$calls"
	echo "check-upscale-vt: the model's download is started outside the menu item"
	exit 1
fi

if [ "$(uname -s)" != Darwin ]; then
	echo "check-upscale-vt: skipped: not macOS"
	exit 0
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/upscale-vt.XXXXXX") || exit 1
trap 'rm -rf "${tmp:?}"' EXIT

cc=${CC:-cc}
bound=${UPSCALE_VT_CHECK_SECONDS:-120}

case $bound in
	''|*[!0-9]*)
		echo "check-upscale-vt: UPSCALE_VT_CHECK_SECONDS is not a whole number of seconds"
		exit 1
		;;
esac

san="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
printf 'int main(void){return 0;}\n' > "${tmp:?}/probe.c"

if ! { $cc $san -o "${tmp:?}/probe" "${tmp:?}/probe.c" >/dev/null 2>&1 && "${tmp:?}/probe" >/dev/null 2>&1; }; then
	echo "check-upscale-vt: $cc cannot build with ASAN and UBSAN; running without them"
	san=
fi

if ! $cc -fobjc-arc -std=gnu11 -Wall -Wextra -Werror -g -O1 $san \
	-DUPSCALE_VT_TEST -Isrc/client -o "${tmp:?}/upscale_vt_test" \
	tests/metal/upscale_vt_test.m src/client/cl_upscale_vt.m \
	-framework Foundation -framework CoreVideo -framework CoreMedia \
	-weak_framework VideoToolbox > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	echo "check-upscale-vt: the host test does not build"
	exit 1
fi

status=0
ASAN_OPTIONS=abort_on_error=0 perl -e 'alarm shift; exec @ARGV or exit 127' \
	"$bound" "${tmp:?}/upscale_vt_test" > "${tmp:?}/run.out" 2>&1 || status=$?

sed 's/^/upscale_vt_test: /' "${tmp:?}/run.out"

if [ "$status" = 142 ]; then
	echo "check-upscale-vt: the host test outlived its $bound s bound and was stopped"
	exit 1
fi

if [ "$status" != 0 ]; then
	echo "check-upscale-vt: the host test failed (exit $status)"
	exit 1
fi

last=$(tail -n 1 "${tmp:?}/run.out")

case $last in
	*" passed, 0 failed") ;;
	*)
		echo "check-upscale-vt: the host test printed no result line"
		exit 1
		;;
esac

echo "check-upscale-vt: OK ($last)"
