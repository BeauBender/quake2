#!/bin/sh
# Invariant (FIX41): on macOS the client runs its start-up and every frame
# inside an autorelease pool, so objects AppKit, QuartzCore and SDL
# autorelease on the main thread (an SDL metal view and its CAMetalLayer
# per window, among others) are released, and the dedicated server and
# the other platforms are compiled as before.
#
# The client's C files are compiled as Objective-C on Darwin (Makefile,
# "-x objective-c"), and src/common/frame.c opens an @autoreleasepool
# around Qcommon_Startup and around each Qcommon_Frame under
# "#if defined(__APPLE__) && defined(__OBJC__)". The check decides from
# the objects, not from the source: in a scratch copy of Makefile,
# third_party/sdl3.make and src/ (third_party/SDL3 is linked, not
# copied; release/ and build/ of this checkout are not touched) it builds
# src/common/frame.c twice with the Makefile's own rules, as the client
# object and as the q2ded object (DEBUG, ASAN and UBSAN cleared), and
# lists the relocations of both with otool -rv: the client object must
# branch to objc_autoreleasePoolPush exactly twice and to
# objc_autoreleasePoolPop at least twice, the q2ded object to neither. Three
# scratch mutants of frame.c (the frame pool removed, the start-up pool
# removed, the guard never true) are built first and each must be refused;
# a mutant that no longer applies to the source fails the check too.
#
# Off macOS one line "check-autorelease-pool: skipped: not macOS". On
# macOS a missing make or otool, a build that fails, or an object that is
# not written fails the check. No window server, SDL library or game data
# is needed. The behaviour itself (window and Metal layer counts after N
# vid_restart cycles) needs both and is a runbook procedure:
# docs/plans/metal-port-harness.md, "vid_restart and the main thread's
# autorelease pool (FIX41)".
set -eu
cd "$(dirname "$0")/.."

fail=0
bad() { echo "check-autorelease-pool: $*"; fail=1; }

if [ "$(uname -s)" != Darwin ]; then
	echo "check-autorelease-pool: skipped: not macOS"
	exit 0
fi
for t in make otool; do
	if ! command -v "$t" >/dev/null 2>&1; then
		echo "check-autorelease-pool: $t not found (the Xcode command line tools are required on macOS)"
		exit 1
	fi
done

tmp=$(mktemp -d "${TMPDIR:-/tmp}/autorelease-pool.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT

mkdir "${tmp:?}/tree" "${tmp:?}/tree/third_party"
cp -R Makefile src "${tmp:?}/tree/"
cp third_party/sdl3.make "${tmp:?}/tree/third_party/"
ln -s "$PWD/third_party/SDL3" "${tmp:?}/tree/third_party/SDL3"
frame="${tmp:?}/tree/src/common/frame.c"
cp "$frame" "${tmp:?}/frame.c.orig"
client_o="${tmp:?}/tree/build/release/client/src/common/frame.o"
server_o="${tmp:?}/tree/build/release/server/src/common/frame.o"

# calls <object> <symbol>: the number of branch relocations (call sites) to <symbol>
calls() {
	otool -rv "$1" > "${tmp:?}/dis" 2> "${tmp:?}/dis.err" || { cat "${tmp:?}/dis.err"; return 1; }
	n=0
	n=$(grep -cE "[[:space:]](BR26|BRANCH)[[:space:]].*[[:space:]]_$2\$" "${tmp:?}/dis") || [ $? = 1 ] || return 1
	echo "$n"
}

# judge: 0 when the objects built from the current frame.c hold the pools
judge() {
	rm -f "$client_o" "$server_o"
	if ! (unset MAKEFLAGS GNUMAKEFLAGS MFLAGS; make -C "${tmp:?}/tree" -s DEBUG= ASAN= UBSAN= \
		build/release/client/src/common/frame.o build/release/server/src/common/frame.o) > "${tmp:?}/build.out" 2>&1; then
		tail -n 20 "${tmp:?}/build.out"
		echo "build of frame.o failed"
		return 2
	fi
	[ -f "$client_o" ] && [ -f "$server_o" ] || { echo "frame.o not written"; return 2; }
	push=$(calls "$client_o" objc_autoreleasePoolPush) || return 2
	pop=$(calls "$client_o" objc_autoreleasePoolPop) || return 2
	spush=$(calls "$server_o" objc_autoreleasePoolPush) || return 2
	spop=$(calls "$server_o" objc_autoreleasePoolPop) || return 2
	echo "client: $push push, $pop pop; q2ded: $spush push, $spop pop"
	[ "$push" -eq 2 ] && [ "$pop" -ge 2 ] && [ "$spush" -eq 0 ] && [ "$spop" -eq 0 ]
}

# mutant <name> <perl substitution>: frame.c with it applied must be refused
mutants=0
mutant() {
	cp "${tmp:?}/frame.c.orig" "$frame"
	perl -0pi -e "$2" "$frame"
	if cmp -s "${tmp:?}/frame.c.orig" "$frame"; then
		bad "mutant '$1' no longer applies to src/common/frame.c"
		return
	fi
	rc=0
	judge > "${tmp:?}/mut.out" 2>&1 || rc=$?
	if [ "$rc" = 0 ]; then
		cat "${tmp:?}/mut.out"
		bad "mutant '$1' was accepted"
	elif [ "$rc" != 1 ]; then
		cat "${tmp:?}/mut.out"
		bad "mutant '$1' did not build"
	else
		mutants=$((mutants + 1))
	fi
}
mutant "frame pool removed" 's/\@autoreleasepool(\s*\{\s*Qcommon_Frame\()/$1/'
mutant "start-up pool removed" 's/\@autoreleasepool(\s*\{\s*Qcommon_Startup\()/$1/'
mutant "guard never true" 's/defined\(__OBJC__\)/defined(__OBJC_NEVER__)/g'

cp "${tmp:?}/frame.c.orig" "$frame"
rc=0
judge > "${tmp:?}/tree.out" 2>&1 || rc=$?
if [ "$rc" != 0 ]; then
	cat "${tmp:?}/tree.out"
	bad "src/common/frame.c does not run start-up and each frame in an autorelease pool on macOS (or q2ded uses one)"
fi

if [ "$fail" = 0 ]; then
	echo "check-autorelease-pool: ok ($(cat "${tmp:?}/tree.out"); $mutants mutants refused)"
fi
exit $fail
