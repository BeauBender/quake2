#!/bin/sh
# Invariant: the video menu's glue (src/client/menu/videomenu.c, qmenu.c and
# the rules of vidrules.c) does what the menu shows. tests/menu/videomenu_test.c
# links the three real files against stubs for the cvar system and the
# drawing, and drives them with key presses and "apply": the MetalFX scale
# item gets its choices back when a scaler is turned on and apply writes only
# a scale the player chose; apply names every cvar under the renderer the menu
# was built for; a greyed item cannot be changed by Left, Right or Enter; the
# scale item's "custom" entry is not a step; built for iOS (TASK147), the
# mode, fullscreen, display, vertical sync and renderer are greyed with their
# reasons, never hidden, and apply writes none of them. No SDL, no engine, no game data.
#
# Built with ASAN and UBSAN when the compiler has them. Each of the two runs
# (desktop, then iOS) has a wall bound of VIDEOMENU_CHECK_SECONDS (default
# 120); a watchdog kills the test it started once the bound has passed and
# the check fails.
set -eu
cd "$(dirname "$0")/.."

tmp=$(mktemp -d "${TMPDIR:-/tmp}/video-menu.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT

bound=${VIDEOMENU_CHECK_SECONDS:-120}
case $bound in
	'' | *[!0-9]*)
		echo "check-video-menu: VIDEOMENU_CHECK_SECONDS is not a whole number of seconds"
		exit 1
		;;
esac
if [ "$bound" -lt 1 ]; then
	echo "check-video-menu: VIDEOMENU_CHECK_SECONDS is 0"
	exit 1
fi

cc=${CC:-cc}
flags="-std=gnu99 -Wall -Wextra -g -O1 -fno-strict-aliasing -fwrapv"
san="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
inc="-I. -Isrc/client -Isrc/common/header -Isrc/client/header -Isrc/client/menu/header -Isrc/client/vid/header -Isrc/client/refresh/header -Isrc/game/header"
defs="-DYQ2OSTYPE=\"test\" -DYQ2ARCH=\"test\""

printf 'int main(void){return 0;}\n' > "${tmp:?}/probe.c"
sflags=$flags
# shellcheck disable=SC2086 # san is a word list
if $cc $san -o "${tmp:?}/probe" "${tmp:?}/probe.c" >/dev/null 2>&1 && "${tmp:?}/probe" >/dev/null 2>&1; then
	sflags="$flags $san"
else
	echo "check-video-menu: $cc cannot build with ASAN and UBSAN; running without them"
fi

# shellcheck disable=SC2086 # sflags, inc and defs are word lists
if ! $cc $sflags $inc $defs -Wno-unused-parameter -Wno-sign-compare -o "${tmp:?}/videomenu_test" \
	tests/menu/videomenu_test.c src/client/menu/videomenu.c src/client/menu/qmenu.c \
	src/client/menu/vidrules.c -lm > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	echo "check-video-menu: the host test does not build"
	exit 1
fi

# TASK147: the same menu built for iOS, where the screen's own items are greyed with their reasons
# shellcheck disable=SC2086 # sflags, inc and defs are word lists
if ! $cc $sflags $inc $defs -DTEST_IOS=1 -DVID_PLATFORM=VIDPLAT_IOS -Wno-unused-parameter -Wno-sign-compare \
	-o "${tmp:?}/videomenu_test_ios" \
	tests/menu/videomenu_test.c src/client/menu/videomenu.c src/client/menu/qmenu.c \
	src/client/menu/vidrules.c -lm > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	echo "check-video-menu: the host test does not build for iOS"
	exit 1
fi

# run_bounded <binary>: runs one host test as a simple background command
# (so the pid the watchdog kills is the test's own, never a subshell that
# would leave the test running) under the bound; the status is the test's
run_bounded()
{
	rm -f "${tmp:?}/expired"
	"$1" > "${tmp:?}/run.out" 2>&1 &
	run=$!
	(
		n=0
		while [ "$n" -lt "$bound" ]; do
			sleep 1
			n=$((n + 1))
		done
		if kill -0 "$run" 2>/dev/null; then
			: > "${tmp:?}/expired"
			kill "$run" 2>/dev/null
		fi
	) < /dev/null > /dev/null 2>&1 &
	dog=$!
	status=0
	wait "$run" || status=$?
	kill "$dog" 2>/dev/null || :
	wait "$dog" 2>/dev/null || :

	cat "${tmp:?}/run.out"
	if [ -e "${tmp:?}/expired" ]; then
		echo "check-video-menu: the host test $(basename "$1") outlived its $bound s bound and was killed"
		exit 1
	fi
	if [ "$status" -ne 0 ]; then
		echo "check-video-menu: the host test $(basename "$1") failed"
		exit 1
	fi
}

# the desktop build first, then iOS, each under the bound
run_bounded "${tmp:?}/videomenu_test"
run_bounded "${tmp:?}/videomenu_test_ios"

echo "check-video-menu: ok"
