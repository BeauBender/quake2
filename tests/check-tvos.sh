#!/bin/sh
# Invariant: the iOS mode of the Makefile builds the engine for the tvOS
# simulator too (TASK23: "make ios_objects IOS_SDK=appletvsimulator"), from
# the same objects as the iOS simulator build, and leaves the iOS build as
# it was.
#
# Every decision is taken from the build's artifacts (LC_BUILD_VERSION in
# otool -l, plutil, the commands of make -n), never from a line make prints.
# In a scratch IOS_ROOT, with METAL_SHADERS=no (no SDL, no link):
#   (a) every object of the tree has LC_BUILD_VERSION platform 8
#       (TVOSSIMULATOR) and minos 17.0, and there is at least one;
#   (b) the tvOS build compiles the same object list as the iOS simulator
#       build (make -n ios_objects, the "-o <path>" of each compile, the
#       build root removed): the tvOS target adds and drops no file;
#   (c) the Info.plist passes plutil -lint, names AppleTVSimulator and
#       17.0, UIDeviceFamily 3 (Apple TV), no template key, and none of the
#       iPhone's UIFileSharingEnabled, orientation or status bar keys.
# Each assertion first runs on a mutant it must refuse (an object built for
# the iOS simulator, a different object list, an iPhone plist), so an
# assertion that passes everything fails here.
#
# Without the tvOS simulator SDK (another OS, or Xcode without its tvOS
# platform) the check prints one "skipped" line and exits 0. With the SDK a
# missing tool or a failed build is a failure, never a skip.
#
# Usage: tests/check-tvos.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C
unset MAKEFLAGS GNUMAKEFLAGS MFLAGS

if [ "$(uname -s)" != Darwin ]; then
	echo "check-tvos: skipped: the tvOS build is macOS only (docs/decisions/0008)"
	exit 0
fi
sdk=
if command -v xcrun >/dev/null 2>&1; then
	sdk=$(xcrun --sdk appletvsimulator --show-sdk-path 2>/dev/null) || sdk=
fi
if [ -z "$sdk" ] || [ ! -f "$sdk/SDKSettings.plist" ]; then
	echo "check-tvos: skipped: no tvOS simulator SDK (xcrun --sdk appletvsimulator --show-sdk-path)"
	exit 0
fi
if ! xcrun --sdk iphonesimulator --show-sdk-path >/dev/null 2>&1; then
	echo "check-tvos: skipped: no iOS simulator SDK for the object-list comparison"
	exit 0
fi

for tool in make otool plutil awk sed sort grep clang; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "check-tvos: $tool not found: a missing tool is a failure"
		exit 1
	fi
done

tmp=$(mktemp -d "${TMPDIR:-/tmp}/check-tvos.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT
jobs=${CHECK_JOBS:-4}
fail=0
bad() { echo "check-tvos: $*"; fail=1; }

platform_of()
{
	otool -l "$1" 2>/dev/null | awk '$1 == "cmd" { c = $2 } c == "LC_BUILD_VERSION" && $1 == "platform" { p = $2 }
		c == "LC_BUILD_VERSION" && $1 == "minos" { print p, $2; exit }'
}

# every file named in <list> is platform 8 at 17.0; the list is not empty
assert_platform() # <list file>
{
	rc=0
	n=0
	while read -r f; do
		n=$((n + 1))
		got=$(platform_of "$f")
		if [ "$got" != "8 17.0" ]; then
			echo "$f is built for platform/minos '${got:-none}', not 8 (TVOSSIMULATOR) 17.0"
			rc=1
		fi
	done < "$1"
	if [ "$n" = 0 ]; then
		echo "no object was built"
		rc=1
	fi
	return $rc
}

# two object lists (files of "-o" paths relative to their root) are equal
assert_same_list() # <a> <b>
{
	if ! cmp -s "$1" "$2"; then
		echo "the object lists differ: $(diff "$1" "$2" | grep -c '^[<>]') lines, first: $(diff "$1" "$2" | grep '^[<>]' | head -n 2 | tr '\n' ' ')"
		return 1
	fi
}

assert_plist() # <Info.plist>
{
	if ! plutil -lint -s "$1" >/dev/null 2>&1; then
		echo "$1 fails plutil -lint"
		return 1
	fi
	rc=0
	[ "$(plutil -extract CFBundleExecutable raw -o - "$1" 2>/dev/null)" = quake2 ] || { echo "$1: CFBundleExecutable is not quake2"; rc=1; }
	[ "$(plutil -extract CFBundleSupportedPlatforms.0 raw -o - "$1" 2>/dev/null)" = AppleTVSimulator ] || { echo "$1: the platform is not AppleTVSimulator"; rc=1; }
	[ "$(plutil -extract MinimumOSVersion raw -o - "$1" 2>/dev/null)" = 17.0 ] || { echo "$1: MinimumOSVersion is not 17.0"; rc=1; }
	[ "$(plutil -extract UIDeviceFamily.0 raw -o - "$1" 2>/dev/null)" = 3 ] || { echo "$1: UIDeviceFamily is not 3"; rc=1; }
	[ "$(plutil -extract UIDeviceFamily raw -o - "$1" 2>/dev/null)" = 1 ] || { echo "$1: UIDeviceFamily holds more than the Apple TV"; rc=1; }
	if grep -q '@[A-Z_]*@' "$1"; then
		echo "$1 holds a template key"
		rc=1
	fi
	for k in UIFileSharingEnabled UISupportedInterfaceOrientations UIStatusBarHidden; do
		if grep -q "<key>$k</key>" "$1"; then
			echo "$1 holds the iPhone's $k"
			rc=1
		fi
	done
	return $rc
}

# the "-o <path>" of every compile of make -n <args>, the build root removed, sorted
object_list() # <root> <args...>
{
	root=$1
	shift
	make -n -B "$@" ios_objects IOS_ROOT="$root" METAL_SHADERS=no 2>/dev/null |
		awk '{ for (i = 1; i < NF; i++) if ($i == "-o") print $(i + 1) }' | sed "s,^$root/,," | sort
}

# ---- mutants: each must be refused, naming its cause --------------------

printf 'int tvos_mutant;\n' > "${tmp:?}/m.c"
xcrun --sdk iphonesimulator clang -target arm64-apple-ios17.0-simulator -c "${tmp:?}/m.c" -o "${tmp:?}/m_ios.o"
xcrun --sdk appletvsimulator clang -target arm64-apple-tvos17.0-simulator -c "${tmp:?}/m.c" -o "${tmp:?}/m_tv.o"
printf '%s\n' "${tmp:?}/m_ios.o" > "${tmp:?}/l_bad"
printf '%s\n' "${tmp:?}/m_tv.o" > "${tmp:?}/l_good"
: > "${tmp:?}/l_empty"
if assert_platform "${tmp:?}/l_bad" > "${tmp:?}/o" 2>&1; then
	bad "mutant: an object built for the iOS simulator passed the platform check"
elif ! grep -q 'not 8 (TVOSSIMULATOR)' "${tmp:?}/o"; then
	bad "mutant: the iOS object was refused without naming the platform"
fi
assert_platform "${tmp:?}/l_good" > "${tmp:?}/o" 2>&1 || bad "a tvOS object was refused: $(cat "${tmp:?}/o")"
if assert_platform "${tmp:?}/l_empty" > "${tmp:?}/o" 2>&1; then
	bad "mutant: an empty object list passed"
fi

printf 'a.o\nb.o\n' > "${tmp:?}/la"
printf 'a.o\n' > "${tmp:?}/lb"
if assert_same_list "${tmp:?}/la" "${tmp:?}/lb" > "${tmp:?}/o" 2>&1; then
	bad "mutant: a dropped object passed the list comparison"
fi

sed -e 's/@BUNDLE_ID@/x.y/' -e 's/@VERSION@/1/' -e 's/@PLATFORM@/AppleTVSimulator/' -e 's/@MIN_OS@/17.0/' stuff/ios/Info-tvos.plist.in > "${tmp:?}/good.plist"
assert_plist "${tmp:?}/good.plist" > "${tmp:?}/o" 2>&1 || bad "the tvOS template was refused: $(cat "${tmp:?}/o")"
sed -e 's/@BUNDLE_ID@/x.y/' -e 's/@VERSION@/1/' -e 's/@PLATFORM@/iPhoneSimulator/' -e 's/@MIN_OS@/17.0/' stuff/ios/Info.plist.in > "${tmp:?}/iphone.plist"
if assert_plist "${tmp:?}/iphone.plist" > "${tmp:?}/o" 2>&1; then
	bad "mutant: the iPhone's Info.plist passed the tvOS plist check"
elif ! grep -q 'AppleTVSimulator' "${tmp:?}/o"; then
	bad "mutant: the iPhone plist was refused without naming the platform"
fi

# ---- the build ----------------------------------------------------------

root="${tmp:?}/tv"
if ! make -j"$jobs" ios_objects IOS_SDK=appletvsimulator IOS_ROOT="$root" METAL_SHADERS=no > "${tmp:?}/build.log" 2>&1; then
	tail -n 20 "${tmp:?}/build.log"
	bad "make ios_objects IOS_SDK=appletvsimulator failed"
	exit 1
fi
find "$root" -name '*.o' | sort > "${tmp:?}/objs"
assert_platform "${tmp:?}/objs" > "${tmp:?}/o" 2>&1 || { head -n 5 "${tmp:?}/o"; bad "(a) the tvOS objects have the wrong platform"; }
[ -f "$root/Info.plist" ] || bad "(c) no Info.plist was written"
assert_plist "$root/Info.plist" > "${tmp:?}/o" 2>&1 || { cat "${tmp:?}/o"; bad "(c) the tvOS Info.plist is wrong"; }

object_list /r IOS_SDK=appletvsimulator > "${tmp:?}/list_tv"
object_list /r IOS_SDK=iphonesimulator > "${tmp:?}/list_ios"
assert_same_list "${tmp:?}/list_ios" "${tmp:?}/list_tv" > "${tmp:?}/o" 2>&1 || { cat "${tmp:?}/o"; bad "(b) the tvOS object list is not the iOS simulator's"; }
[ -s "${tmp:?}/list_tv" ] || bad "(b) make -n listed no object"

if [ "$fail" != 0 ]; then
	exit 1
fi
echo "check-tvos: ok ($(grep -c . "${tmp:?}/objs") objects for the tvOS simulator, platform 8, the iOS simulator's list)"
