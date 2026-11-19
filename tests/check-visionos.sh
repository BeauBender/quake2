#!/bin/sh
# Invariant: the iOS mode of the Makefile builds the engine for the Apple
# Vision Pro simulator too (TASK175: "make ios_objects IOS_SDK=xrsimulator"),
# from the same object list as the iOS simulator build, and the iPhone and
# iPad only code is compiled out of it.
#
# Every decision is taken from the build's artifacts (LC_BUILD_VERSION in
# otool -l, the code of otool -tV, the bytes of an object, plutil, the
# preprocessor's output, the commands of make -n), never from a line make
# prints. In a scratch IOS_ROOT, with METAL_SHADERS=no (no SDL, no link):
#   (a) every object of the tree has LC_BUILD_VERSION platform 12
#       (XROSSIMULATOR) and minos 1.0, and there is at least one;
#   (b) the visionOS build compiles the same object list as the iOS
#       simulator build (make -n ios_objects, the "-o <path>" of each
#       compile, the build root removed): the target adds and drops no file;
#   (c) the Info.plist passes plutil -lint, names XRSimulator and 1.0,
#       UIDeviceFamily 7 (Apple Vision) alone, no template key, and none of
#       the iPhone's UIFileSharingEnabled, orientation or status bar keys;
#   (d) the build's target, preprocessed with the SDK's TargetConditionals.h:
#       TARGET_OS_VISION 1, TARGET_OS_IPHONE 1, TARGET_OS_IOS 0, so the
#       iPhone and iPad only sites, whose #if names TARGET_OS_IOS
#       (tests/check-tvos.sh (d) holds that for each), are compiled out;
#   (e) the compiled objects of those sites: Touch_Supported in touch.o is
#       "mov w0, #0x0; ret"; sdl3.o holds no SDL_RETURN_KEY_HIDES_IME
#       (the in_touch default's keyboard hint) and nm -u lists no
#       _Touch_TextInput (the on-screen keyboard's follow-up); glimp_sdl3.o
#       holds neither hint name, SDL_ORIENTATIONS nor
#       SDL_IOS_HIDE_HOME_INDICATOR (the name, whatever the value); vid.o
#       holds no ios_orientation command (the
#       UIWindowSceneGeometryPreferencesIOS request, which visionOS does
#       not have); and nm -u of every imgdecode.o lists
#       _CGImageSourceGetType, and _kCGImageSourceAllowableTypes where the
#       SDK declares it (ImageIO reads only the file's type, FIX138).
#       Each string or symbol is the positive control's too: the iOS
#       simulator's object of the same file, compiled with the command
#       make -n gives for it, must hold it, so a rename fails the check
#       instead of passing it.
# It does not compare the iOS build with an earlier one; that comparison is
# the review's before and after of make -n (docs/runbook/TASK175.md).
# Each assertion first runs on a mutant it must refuse (an object built for
# the iOS or the tvOS simulator, a different object list, an iPhone or an
# Apple TV plist, a plist with a forbidden key, also spelled with an XML
# character reference, two device families or another minimum OS, the iOS
# simulator's target, a Touch_Supported that returns 1 or a variable, an
# object that holds a pinned string or needs a pinned symbol, one that lacks
# the symbol it must need, an unreadable object), so an assertion that
# passes everything fails here. A tool's error (grep, nm) refuses.
#
# The helpers are copied from tests/check-tvos.sh with the platform, the
# minimum OS and the device family changed, rather than shared with it:
# a shared file would change what check-tvos.sh runs.
#
# Without the visionOS simulator SDK (another OS, or Xcode without its
# visionOS platform) it prints one "skipped" line and exits 0.
# With the SDK a missing tool or a failed build is a failure, never a skip.
#
# Usage: tests/check-visionos.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C
unset MAKEFLAGS GNUMAKEFLAGS MFLAGS

if [ "$(uname -s)" != Darwin ]; then
	[ "${CHECK_VISIONOS_REQUIRE:-0}" != 1 ] || { echo "check-visionos: CHECK_VISIONOS_REQUIRE=1 and this is not macOS"; exit 1; }
	echo "check-visionos: skipped: the visionOS build is macOS only (docs/decisions/0008)"
	exit 0
fi
sdk=
if command -v xcrun >/dev/null 2>&1; then
	sdk=$(xcrun --sdk xrsimulator --show-sdk-path 2>/dev/null) || sdk=
fi
if [ -z "$sdk" ] || [ ! -f "$sdk/SDKSettings.plist" ]; then
	[ "${CHECK_VISIONOS_REQUIRE:-0}" != 1 ] || { echo "check-visionos: CHECK_VISIONOS_REQUIRE=1 and there is no visionOS simulator SDK (xcrun --sdk xrsimulator --show-sdk-path)"; exit 1; }
	echo "check-visionos: skipped: no visionOS simulator SDK (xcrun --sdk xrsimulator --show-sdk-path)"
	exit 0
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/check-visionos.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT
fail=0
bad() { echo "check-visionos: $*"; fail=1; }

for tool in make otool nm plutil awk sed sort grep cmp diff clang; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "check-visionos: $tool not found: a missing tool is a failure"
		exit 1
	fi
done
if ! xcrun --sdk iphonesimulator --show-sdk-path >/dev/null 2>&1; then
	echo "check-visionos: the iOS simulator SDK is missing (the object-list comparison needs it): a failure"
	exit 1
fi

jobs=${CHECK_JOBS:-4}
xr_target=arm64-apple-xros1.0-simulator

platform_of()
{
	otool -l "$1" 2>/dev/null | awk '$1 == "cmd" { c = $2 } c == "LC_BUILD_VERSION" && $1 == "platform" { p = $2 }
		c == "LC_BUILD_VERSION" && $1 == "minos" { print p, $2; exit }'
}

# every file named in <list> is platform 12 at 1.0; the list is not empty
assert_platform() # <list file>
{
	rc=0
	n=0
	while read -r f; do
		n=$((n + 1))
		got=$(platform_of "$f")
		if [ "$got" != "12 1.0" ]; then
			echo "$f is built for platform/minos '${got:-none}', not 12 (XROSSIMULATOR) 1.0"
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
	[ "$(plutil -extract CFBundleSupportedPlatforms.0 raw -o - "$1" 2>/dev/null)" = XRSimulator ] || { echo "$1: the platform is not XRSimulator"; rc=1; }
	[ "$(plutil -extract MinimumOSVersion raw -o - "$1" 2>/dev/null)" = 1.0 ] || { echo "$1: MinimumOSVersion is not 1.0"; rc=1; }
	[ "$(plutil -extract UIDeviceFamily.0 raw -o - "$1" 2>/dev/null)" = 7 ] || { echo "$1: UIDeviceFamily is not 7"; rc=1; }
	[ "$(plutil -extract UIDeviceFamily raw -o - "$1" 2>/dev/null)" = 1 ] || { echo "$1: UIDeviceFamily holds more than the Apple Vision"; rc=1; }
	if grep -q '@[A-Z_]*@' "$1"; then
		echo "$1 holds a template key"
		rc=1
	fi
	# plutil decides, not the text: a key spelled with a character reference
	# (<key>UIFile&#83;haringEnabled</key>) is the same key. The file passed
	# -lint above, so a failed -extract is a missing key; xml1, not raw,
	# extracts a value of any type
	for k in UIFileSharingEnabled UISupportedInterfaceOrientations UIStatusBarHidden; do
		if plutil -extract "$k" xml1 -o - "$1" >/dev/null 2>&1; then
			echo "$1 holds the iPhone's $k"
			rc=1
		fi
	done
	return $rc
}

# the target's TargetConditionals: visionOS, not iPhone and iPad (TARGET_OS_IOS 0)
assert_conditionals() # <clang target> <sdk>
{
	printf '#include <TargetConditionals.h>\nvision=TARGET_OS_VISION iphone=TARGET_OS_IPHONE ios=TARGET_OS_IOS\n' > "${tmp:?}/tc.c"
	got=$(xcrun --sdk "$2" clang -target "$1" -E -P "${tmp:?}/tc.c" 2>/dev/null | grep '^vision=') || got=
	if [ "$got" != "vision=1 iphone=1 ios=0" ]; then
		echo "the target $1 gives '${got:-nothing}', not vision=1 iphone=1 ios=0"
		return 1
	fi
}

# the code of Touch_Supported in <object> is exactly "mov w0, #0x0" and "ret"
assert_no_touch() # <object>
{
	otool -tV "$1" > "${tmp:?}/dis" 2>&1 || { echo "otool -tV $1 failed"; return 1; }
	awk '$0 == "_Touch_Supported:" { on = 1; next }
		on && /^[^0-9a-f]/ { exit }
		on { $1 = ""; sub(/^[ \t]+/, ""); gsub(/[ \t]+/, " "); print }' "${tmp:?}/dis" > "${tmp:?}/fn"
	if [ "$(tr '\n' ';' < "${tmp:?}/fn")" != "mov w0, #0x0;ret;" ]; then
		echo "$1: Touch_Supported is not a constant 0: $(tr '\n' ';' < "${tmp:?}/fn")"
		return 1
	fi
}

# bytes_have <object> <text>: 0 the bytes hold <text>, 1 they do not, 2 the
# object is missing or unreadable (said); grep's own status decides, so a
# read error is never "does not hold"
bytes_have()
{
	[ -f "$1" ] || { echo "$1 is missing"; return 2; }
	brc=0
	grep -a -F -q -e "$2" "$1" 2>/dev/null || brc=$?
	case $brc in
	0|1) return $brc ;;
	*) echo "$1: cannot read (grep status $brc)"; return 2 ;;
	esac
}

# needs <object> <symbol>: 0 nm -u lists <symbol>, 1 it does not, 2 nm failed (said)
needs()
{
	[ -f "$1" ] || { echo "$1 is missing"; return 2; }
	nm -u "$1" > "${tmp:?}/nm" 2>/dev/null || { echo "$1: cannot read (nm -u failed)"; return 2; }
	nrc=0
	grep -x -F -q -e "$2" "${tmp:?}/nm" || nrc=$?
	case $nrc in
	0|1) return $nrc ;;
	*) echo "${tmp:?}/nm: cannot read (grep status $nrc)"; return 2 ;;
	esac
}

# <object> exists and its bytes do not hold <text>
assert_lacks() # <object> <text>
{
	arc=0
	bytes_have "$1" "$2" || arc=$?
	case $arc in
	0) echo "$1 holds \"$2\""; return 1 ;;
	1) return 0 ;;
	*) return 1 ;;
	esac
}

# <object> exists and its bytes hold <text> (a pin's positive control)
assert_holds() # <object> <text>
{
	arc=0
	bytes_have "$1" "$2" || arc=$?
	case $arc in
	0) return 0 ;;
	1) echo "$1 does not hold \"$2\""; return 1 ;;
	*) return 1 ;;
	esac
}

# nm -u <object> does not list <symbol>
assert_no_need() # <object> <symbol>
{
	arc=0
	needs "$1" "$2" || arc=$?
	case $arc in
	0) echo "$1 needs $2 (nm -u)"; return 1 ;;
	1) return 0 ;;
	*) return 1 ;;
	esac
}

# nm -u <object> lists <symbol>
assert_need() # <object> <symbol>
{
	arc=0
	needs "$1" "$2" || arc=$?
	case $arc in
	0) return 0 ;;
	1) echo "$1 does not need $2 (nm -u)"; return 1 ;;
	*) return 1 ;;
	esac
}

# the "-o <path>" of every compile of make -n <args>, the build root removed, sorted
# (list_root, not root: the build below keeps its own root)
object_list() # <root> <args...>
{
	list_root=$1
	shift
	make -n -B "$@" ios_objects IOS_ROOT="$list_root" METAL_SHADERS=no 2>/dev/null |
		awk '{ for (i = 1; i < NF; i++) if ($i == "-o") print $(i + 1) }' | sed "s,^$list_root/,," | sort
}

# ---- mutants: each must be refused, naming its cause --------------------

printf 'int xros_mutant;\n' > "${tmp:?}/m.c"
xcrun --sdk iphonesimulator clang -target arm64-apple-ios17.0-simulator -c "${tmp:?}/m.c" -o "${tmp:?}/m_ios.o"
# no header: the tvOS object needs no tvOS SDK
xcrun --sdk iphonesimulator clang -Wno-incompatible-sysroot -target arm64-apple-tvos17.0-simulator -c "${tmp:?}/m.c" -o "${tmp:?}/m_tv.o"
xcrun --sdk xrsimulator clang -target "$xr_target" -c "${tmp:?}/m.c" -o "${tmp:?}/m_xr.o"
for m in ios tv; do
	printf '%s\n' "${tmp:?}/m_$m.o" > "${tmp:?}/l_bad"
	if assert_platform "${tmp:?}/l_bad" > "${tmp:?}/o" 2>&1; then
		bad "mutant: an object built for the $m simulator passed the platform check"
	elif ! grep -q 'not 12 (XROSSIMULATOR)' "${tmp:?}/o"; then
		bad "mutant: the $m object was refused without naming the platform"
	fi
done
printf '%s\n' "${tmp:?}/m_xr.o" > "${tmp:?}/l_good"
: > "${tmp:?}/l_empty"
assert_platform "${tmp:?}/l_good" > "${tmp:?}/o" 2>&1 || bad "a visionOS object was refused: $(cat "${tmp:?}/o")"
if assert_platform "${tmp:?}/l_empty" > "${tmp:?}/o" 2>&1; then
	bad "mutant: an empty object list passed"
fi

printf 'a.o\nb.o\n' > "${tmp:?}/la"
printf 'a.o\n' > "${tmp:?}/lb"
if assert_same_list "${tmp:?}/la" "${tmp:?}/lb" > "${tmp:?}/o" 2>&1; then
	bad "mutant: a dropped object passed the list comparison"
fi

sed -e 's/@BUNDLE_ID@/x.y/' -e 's/@VERSION@/1/' -e 's/@PLATFORM@/XRSimulator/' -e 's/@MIN_OS@/1.0/' stuff/ios/Info-visionos.plist.in > "${tmp:?}/good.plist"
assert_plist "${tmp:?}/good.plist" > "${tmp:?}/o" 2>&1 || bad "the visionOS template was refused: $(cat "${tmp:?}/o")"
sed -e 's/@BUNDLE_ID@/x.y/' -e 's/@VERSION@/1/' -e 's/@PLATFORM@/iPhoneSimulator/' -e 's/@MIN_OS@/17.0/' stuff/ios/Info.plist.in > "${tmp:?}/iphone.plist"
if assert_plist "${tmp:?}/iphone.plist" > "${tmp:?}/o" 2>&1; then
	bad "mutant: the iPhone's Info.plist passed the visionOS plist check"
elif ! grep -q 'XRSimulator' "${tmp:?}/o"; then
	bad "mutant: the iPhone plist was refused without naming the platform"
fi
# the Apple TV's template with the visionOS platform and version: only its device family is wrong
sed -e 's/@BUNDLE_ID@/x.y/' -e 's/@VERSION@/1/' -e 's/@PLATFORM@/XRSimulator/' -e 's/@MIN_OS@/1.0/' stuff/ios/Info-tvos.plist.in > "${tmp:?}/tv.plist"
if assert_plist "${tmp:?}/tv.plist" > "${tmp:?}/o" 2>&1; then
	bad "mutant: the Apple TV's Info.plist passed the visionOS plist check"
elif ! grep -q 'UIDeviceFamily is not 7' "${tmp:?}/o"; then
	bad "mutant: the Apple TV plist was refused without naming the device family"
fi
# one change each to the good plist: an iPhone key, a second device family, another minimum OS
# (mk, not k: assert_plist's own loop sets k, sh has no local)
for mk in UIFileSharingEnabled UISupportedInterfaceOrientations UIStatusBarHidden; do
	sed "s,<key>CFBundleExecutable</key>,<key>$mk</key><true/>&," "${tmp:?}/good.plist" > "${tmp:?}/m.plist"
	if assert_plist "${tmp:?}/m.plist" > "${tmp:?}/o" 2>&1; then
		bad "mutant: a plist with $mk passed"
	elif ! grep -q "the iPhone's $mk" "${tmp:?}/o"; then
		bad "mutant: the plist with $mk was refused without naming it"
	fi
done
# the same key with an XML character reference (&#83; is S): plutil reads UIFileSharingEnabled
sed 's,<key>CFBundleExecutable</key>,<key>UIFile\&#83;haringEnabled</key><true/>&,' "${tmp:?}/good.plist" > "${tmp:?}/m.plist"
if ! grep -q 'UIFile&#83;haringEnabled' "${tmp:?}/m.plist" || ! plutil -lint -s "${tmp:?}/m.plist" >/dev/null 2>&1; then
	bad "mutant: the character-reference plist was not made or does not lint"
elif assert_plist "${tmp:?}/m.plist" > "${tmp:?}/o" 2>&1; then
	bad "mutant: a plist with UIFile&#83;haringEnabled passed"
elif ! grep -q "the iPhone's UIFileSharingEnabled" "${tmp:?}/o"; then
	bad "mutant: the character-reference plist was refused without naming the key"
fi
sed 's,<integer>7</integer>,&<integer>1</integer>,' "${tmp:?}/good.plist" > "${tmp:?}/m.plist"
if assert_plist "${tmp:?}/m.plist" > "${tmp:?}/o" 2>&1; then
	bad "mutant: a plist with the Apple Vision and the iPhone families passed"
elif ! grep -q 'holds more than the Apple Vision' "${tmp:?}/o"; then
	bad "mutant: the two-family plist was refused without naming the count"
fi
sed 's,<string>1\.0</string>,<string>17.0</string>,' "${tmp:?}/good.plist" > "${tmp:?}/m.plist"
if assert_plist "${tmp:?}/m.plist" > "${tmp:?}/o" 2>&1; then
	bad "mutant: a plist with MinimumOSVersion 17.0 passed"
elif ! grep -q 'MinimumOSVersion is not 1.0' "${tmp:?}/o"; then
	bad "mutant: the 17.0 plist was refused without naming the minimum OS"
fi

# the conditionals: the iOS simulator's target (TARGET_OS_IOS 1) is refused
if assert_conditionals arm64-apple-ios17.0-simulator iphonesimulator > "${tmp:?}/o" 2>&1; then
	bad "mutant: the iOS simulator's target passed the visionOS conditionals"
elif ! grep -q 'not vision=1 iphone=1 ios=0' "${tmp:?}/o"; then
	bad "mutant: the iOS target was refused without naming the conditionals"
fi

# Touch_Supported: a constant 0 passes, a 1 or a read of a variable does not
printf 'int Touch_Supported(void) { return 0; }\n' > "${tmp:?}/t0.c"
printf 'int Touch_Supported(void) { return 1; }\n' > "${tmp:?}/t1.c"
printf 'extern int xros_g;\nint Touch_Supported(void) { return xros_g; }\n' > "${tmp:?}/tv.c"
for t in t0 t1 tv; do
	xcrun --sdk xrsimulator clang -O2 -target "$xr_target" -c "${tmp:?}/$t.c" -o "${tmp:?}/$t.o"
done
assert_no_touch "${tmp:?}/t0.o" > "${tmp:?}/o" 2>&1 || bad "a constant-0 Touch_Supported was refused: $(cat "${tmp:?}/o")"
for t in t1 tv; do
	if assert_no_touch "${tmp:?}/$t.o" > "${tmp:?}/o" 2>&1; then
		bad "mutant: a Touch_Supported that is not 0 ($t.c) passed"
	fi
done
if assert_no_touch "${tmp:?}/m_xr.o" > "${tmp:?}/o" 2>&1; then
	bad "mutant: an object without Touch_Supported passed"
fi

# the pinned strings and symbols of (e)
pin_texts='SDL_RETURN_KEY_HIDES_IME
SDL_ORIENTATIONS
SDL_IOS_HIDE_HOME_INDICATOR
ios_orientation'
# an object that holds a pinned string is refused, and one without it fails the positive control
printf 'const char *a = "SDL_RETURN_KEY_HIDES_IME";\nconst char *b = "SDL_ORIENTATIONS";\nconst char *c = "SDL_IOS_HIDE_HOME_INDICATOR";\nconst char *d = "ios_orientation";\n' > "${tmp:?}/s.c"
xcrun --sdk xrsimulator clang -target "$xr_target" -c "${tmp:?}/s.c" -o "${tmp:?}/s.o"
while read -r text; do
	if assert_lacks "${tmp:?}/s.o" "$text" > "${tmp:?}/o" 2>&1; then
		bad "mutant: an object holding \"$text\" passed"
	elif ! grep -q "holds \"$text\"" "${tmp:?}/o"; then
		bad "mutant: the object holding \"$text\" was refused without naming it"
	fi
	assert_lacks "${tmp:?}/m_xr.o" "$text" > "${tmp:?}/o" 2>&1 || bad "an object without \"$text\" was refused: $(cat "${tmp:?}/o")"
	assert_holds "${tmp:?}/s.o" "$text" > "${tmp:?}/o" 2>&1 || bad "an object holding \"$text\" failed the positive control: $(cat "${tmp:?}/o")"
	if assert_holds "${tmp:?}/m_xr.o" "$text" > "${tmp:?}/o" 2>&1; then
		bad "mutant: an object without \"$text\" (a renamed string) passed the positive control"
	fi
done <<EOF
$pin_texts
EOF
# an object that needs _Touch_TextInput is refused, one that does not need
# _kCGImageSourceAllowableTypes too (ImageIO not restricted)
printf 'extern int Touch_TextInput(int, int, int, int *);\nextern const void *kCGImageSourceAllowableTypes;\nint xros_u(void) { return Touch_TextInput(0, 0, 0, 0); }\nconst void *xros_v(void) { return kCGImageSourceAllowableTypes; }\n' > "${tmp:?}/u.c"
xcrun --sdk xrsimulator clang -target "$xr_target" -c "${tmp:?}/u.c" -o "${tmp:?}/u.o"
if assert_no_need "${tmp:?}/u.o" _Touch_TextInput > "${tmp:?}/o" 2>&1; then
	bad "mutant: an object that needs _Touch_TextInput passed"
elif ! grep -q 'needs _Touch_TextInput' "${tmp:?}/o"; then
	bad "mutant: the object that needs _Touch_TextInput was refused without naming it"
fi
assert_no_need "${tmp:?}/m_xr.o" _Touch_TextInput > "${tmp:?}/o" 2>&1 || bad "an object without _Touch_TextInput was refused: $(cat "${tmp:?}/o")"
for sym in _Touch_TextInput _kCGImageSourceAllowableTypes; do
	assert_need "${tmp:?}/u.o" "$sym" > "${tmp:?}/o" 2>&1 || bad "an object that needs $sym was refused: $(cat "${tmp:?}/o")"
	if assert_need "${tmp:?}/m_xr.o" "$sym" > "${tmp:?}/o" 2>&1; then
		bad "mutant: an object that does not need $sym passed"
	elif ! grep -q "does not need $sym" "${tmp:?}/o"; then
		bad "mutant: the object without $sym was refused without naming it"
	fi
done
# a missing or an unreadable object is refused by every assertion, never taken as "lacks"
cp "${tmp:?}/m_xr.o" "${tmp:?}/unreadable.o"
chmod 000 "${tmp:?}/unreadable.o"
for obj in absent unreadable; do
	if [ "$obj" = unreadable ] && [ -r "${tmp:?}/unreadable.o" ]; then
		echo "check-visionos: note: running as a user who reads a mode 000 file (root): the unreadable-object mutant is not run"
		continue
	fi
	for a in assert_lacks assert_holds assert_no_need assert_need; do
		if "$a" "${tmp:?}/$obj.o" ios_orientation > "${tmp:?}/o" 2>&1; then
			bad "mutant: $a passed an $obj object"
		elif ! grep -E -q 'is missing|cannot read' "${tmp:?}/o"; then
			bad "mutant: $a refused an $obj object without saying it cannot be read: $(cat "${tmp:?}/o")"
		fi
	done
done
chmod 600 "${tmp:?}/unreadable.o"

if [ "$fail" != 0 ]; then
	exit 1
fi

# ---- the build ----------------------------------------------------------

assert_conditionals "$xr_target" xrsimulator > "${tmp:?}/o" 2>&1 || { cat "${tmp:?}/o"; bad "(d) the visionOS target is not outside TARGET_OS_IOS"; }

root="${tmp:?}/xr"
if ! make -j"$jobs" ios_objects IOS_SDK=xrsimulator IOS_ROOT="$root" METAL_SHADERS=no > "${tmp:?}/build.log" 2>&1; then
	tail -n 20 "${tmp:?}/build.log"
	bad "make ios_objects IOS_SDK=xrsimulator failed"
	exit 1
fi
find "$root" -name '*.o' | sort > "${tmp:?}/objs"
assert_platform "${tmp:?}/objs" > "${tmp:?}/o" 2>&1 || { head -n 5 "${tmp:?}/o"; bad "(a) the visionOS objects have the wrong platform"; }
[ -f "$root/Info.plist" ] || bad "(c) no Info.plist was written"
assert_plist "$root/Info.plist" > "${tmp:?}/o" 2>&1 || { cat "${tmp:?}/o"; bad "(c) the visionOS Info.plist is wrong"; }

# one_object <name>: the build has exactly one object called <name>; its path is in $tmp/one
one_object()
{
	awk -v n="/$1" 'substr($0, length($0) - length(n) + 1) == n' "${tmp:?}/objs" > "${tmp:?}/one"
	if [ "$(grep -c . "${tmp:?}/one")" != 1 ]; then
		bad "(e) the build has $(grep -c . "${tmp:?}/one") $1, not 1"
		return 1
	fi
}
if one_object touch.o; then
	assert_no_touch "$(cat "${tmp:?}/one")" > "${tmp:?}/o" 2>&1 || { cat "${tmp:?}/o"; bad "(e) the touch overlay is compiled into the visionOS build"; }
fi

# the positive controls: the iOS simulator's object of each pinned file,
# compiled with the command make -n gives for it (its -o path, in $tmp/ios)
ios_root="${tmp:?}/ios"
make -n -B ios_objects IOS_SDK=iphonesimulator IOS_ROOT="$ios_root" METAL_SHADERS=no > "${tmp:?}/ios.n" 2>/dev/null || bad "(e) make -n ios_objects for the iOS simulator failed"
# ios_object <source>: that object's path is in $tmp/ios_one. The first
# compile (a line with -c) that names <source> as a word, wherever the
# word is: the client's rule puts it before -o, ref_metal's after
ios_object()
{
	awk -v s="$1" '/ -c / { for (i = 1; i <= NF; i++) if ($i == s) { print; exit } }' "${tmp:?}/ios.n" > "${tmp:?}/cmd"
	awk '{ for (i = 1; i < NF; i++) if ($i == "-o") print $(i + 1) }' "${tmp:?}/cmd" > "${tmp:?}/ios_one"
	if [ "$(grep -c . "${tmp:?}/ios_one")" != 1 ]; then
		bad "(e) make -n names no compile of $1 for the iOS simulator"
		return 1
	fi
	# every file it writes (-o, and -MF where the rule has it) is under the scratch root
	awk '{ for (i = 1; i < NF; i++) if ($i == "-o" || $i == "-MF") print $(i + 1) }' "${tmp:?}/cmd" > "${tmp:?}/ios_out"
	while read -r out; do
		case $out in
		"$ios_root"/*) mkdir -p "$(dirname "$out")" ;;
		*) bad "(e) the iOS simulator's compile of $1 writes $out, outside the scratch root"; return 1 ;;
		esac
	done < "${tmp:?}/ios_out"
	if ! sh "${tmp:?}/cmd" > "${tmp:?}/cc.log" 2>&1; then
		tail -n 5 "${tmp:?}/cc.log"
		bad "(e) $1 does not compile for the iOS simulator"
		return 1
	fi
}

# pin <lacks|need|no_need> <object name> <source> <text or symbol> <what>:
# every visionOS object called <name> (one at least) and the iOS
# simulator's object of <source>, the control: lacks (iOS: holds), no_need
# (iOS: needs), need (iOS: needs too)
pin()
{
	awk -v n="/$2" 'substr($0, length($0) - length(n) + 1) == n' "${tmp:?}/objs" > "${tmp:?}/named"
	# (pin is called bare under set -e: every path returns 0, bad records the failure)
	if [ ! -s "${tmp:?}/named" ]; then
		bad "(e) the build has no $2"
		return 0
	fi
	while read -r obj; do
		case $1 in
		lacks) assert_lacks "$obj" "$4" ;;
		no_need) assert_no_need "$obj" "$4" ;;
		need) assert_need "$obj" "$4" ;;
		esac > "${tmp:?}/o" 2>&1 || { cat "${tmp:?}/o"; bad "(e) $5"; }
	done < "${tmp:?}/named"
	ios_object "$3" || return 0
	case $1 in
	lacks) assert_holds "$(cat "${tmp:?}/ios_one")" "$4" ;;
	*) assert_need "$(cat "${tmp:?}/ios_one")" "$4" ;;
	esac > "${tmp:?}/o" 2>&1 || { cat "${tmp:?}/o"; bad "(e) the positive control failed: the pin on \"$4\" would pass whatever the visionOS build holds"; }
}
pin lacks sdl3.o src/client/input/sdl3.c SDL_RETURN_KEY_HIDES_IME "the iPhone's in_touch default and keyboard hint are compiled into the visionOS build"
pin no_need sdl3.o src/client/input/sdl3.c _Touch_TextInput "the on-screen keyboard's follow-up is compiled into the visionOS build"
pin lacks glimp_sdl3.o src/client/vid/glimp_sdl3.c SDL_ORIENTATIONS "the orientation hint is compiled into the visionOS build"
pin lacks glimp_sdl3.o src/client/vid/glimp_sdl3.c SDL_IOS_HIDE_HOME_INDICATOR "the home indicator hint is compiled into the visionOS build"
pin lacks vid.o src/client/vid/vid.c ios_orientation "the ios_orientation command is compiled into the visionOS build"
# The restriction is kCGImageSourceAllowableTypes where the SDK declares
# it (27.0 on); an older SDK (FIX138) builds without it, and the type
# check on CGImageSourceGetType is the restriction then, as it is the
# backstop always. Whether the SDKs declare it is asked of them (the
# visionOS one and the iOS one of the positive control), so a decoder
# that leaves it out on an SDK that has it still fails.
printf '#include <ImageIO/ImageIO.h>\nconst void *xros_a(void) { return kCGImageSourceAllowableTypes; }\n' > "${tmp:?}/a.c"
imgdec_allowable=yes
for probe in "xrsimulator $xr_target" "iphonesimulator arm64-apple-ios17.0-simulator"; do
	psdk=${probe%% *}
	ptarget=${probe#* }
	if xcrun --sdk "$psdk" clang -target "$ptarget" -Wno-unguarded-availability-new \
		-c "${tmp:?}/a.c" -o "${tmp:?}/a.o" > "${tmp:?}/a.out" 2>&1; then
		:
	elif grep -q "undeclared identifier 'kCGImageSourceAllowableTypes'" "${tmp:?}/a.out"; then
		echo "check-visionos: note: the $psdk SDK $(xcrun --sdk "$psdk" --show-sdk-version 2>/dev/null) does not declare kCGImageSourceAllowableTypes: the type check (CGImageSourceGetType) is pinned alone"
		imgdec_allowable=no
	else
		cat "${tmp:?}/a.out"
		bad "(e) the probe for kCGImageSourceAllowableTypes does not build for the $psdk SDK for another reason"
	fi
done
pin need imgdecode.o src/client/refresh/files/imgdecode.c _CGImageSourceGetType "the image decoder does not check the file's type"
if [ "$imgdec_allowable" = yes ]; then
	pin need imgdecode.o src/client/refresh/files/imgdecode.c _kCGImageSourceAllowableTypes "the image decoder does not restrict ImageIO to the file's type"
fi

object_list /r IOS_SDK=xrsimulator > "${tmp:?}/list_xr"
object_list /r IOS_SDK=iphonesimulator > "${tmp:?}/list_ios"
assert_same_list "${tmp:?}/list_ios" "${tmp:?}/list_xr" > "${tmp:?}/o" 2>&1 || { cat "${tmp:?}/o"; bad "(b) the visionOS object list is not the iOS simulator's"; }
[ -s "${tmp:?}/list_xr" ] || bad "(b) make -n listed no object"

if [ "$fail" != 0 ]; then
	exit 1
fi
echo "check-visionos: ok ($(grep -c . "${tmp:?}/objs") objects for the visionOS simulator, platform 12, the iOS simulator's list)"
