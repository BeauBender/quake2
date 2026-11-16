#!/bin/sh
# Invariant: the iOS mode of the Makefile (TASK146: "make ios", "make
# ios_objects") builds the engine for the iOS simulator, with the game
# and ref_metal linked in (STATIC_MODULES=1, TASK20), and leaves the
# macOS build as it was.
#
# Every decision here is taken from the build's artifacts (LC_BUILD_VERSION
# in otool -l, nm, otool -L, codesign --verify, plutil), never from a line
# make or the engine prints.
#
# Default (the gate): in a scratch copy of the tree (Makefile,
# third_party/sdl3.make, stuff/ios/, src/; third_party/SDL3 linked, not
# copied) "make ios_objects IOS_SDK=iphonesimulator METAL_SHADERS=no"
# compiles the client, game and ref_metal objects for the simulator and
# writes the Info.plist (no SDL, no link). Then:
#   (a) every object the build was to write exists, and every object in
#       the iOS tree has LC_BUILD_VERSION platform 7 (IOSSIMULATOR) and
#       minos 17.0;
#   (b) the combined game and renderer objects define only their entry
#       point (_GetGameAPI, _GetRefAPI);
#   (c) no object references an AppKit class (nm -u against the class
#       list of the macOS SDK's AppKit.tbd; Cocoa is AppKit's umbrella);
#   (d) the Info.plist passes plutil -lint, names quake2 as its
#       CFBundleExecutable, iPhoneSimulator and 17.0, and holds no
#       template key, and holds UIFileSharingEnabled and
#       LSSupportsOpeningDocumentsInPlace as true (TASK149: the Files
#       app shows the app's Documents folder only with them);
#   (e) "make -n -B VERBOSE=1 client ref_metal" without the iOS mode
#       prints no -target, iPhone or ios token (assert_macos_cmds);
#   (f) the objects of ios_objects are compiled with the commands ios
#       links them from (make -n of both goals);
#   (k) the client's main.o defines _SDL_main and references
#       _SDL_RunApp: main.c includes SDL_main.h, whose main runs
#       UIApplicationMain (assert_sdl_main);
#   (l) the simulator's rm_main.o holds ref_metal's line that the
#       simulator does not check the apple5 floor; rm_main.o compiled
#       with the device build's own command (make -n IOS_SDK=iphoneos)
#       holds the floor's refusal and not that line (assert_floor);
#   (m) make refuses an IOS_ROOT under release/ or debug/ however it is
#       spelled, and an IOS_ROOT or IOS_BUNDLE_DATA with a make or shell
#       metacharacter, without evaluating it (make -n ios_objects);
#   (n) scripts/ios-sim.sh selftest: the smoke's device, list and limit
#       rules against a fake xcrun and make (no simulator).
# --full (by hand): additionally "make ios" (the
# vendored SDL3 for the simulator, the link, the bundle, the ad hoc
# signature; METAL_SHADERS from the environment, default yes) and:
#   (g) the executable has platform 7, defines _GetGameAPI and
#       _GetRefAPI, and passes (k);
#   (h) otool -L names no game or renderer library and no AppKit, Cocoa,
#       Carbon or IOKit;
#   (i) codesign --verify --strict passes on the bundle;
#   (j) the bundle's Info.plist passes (d), and the bundle holds
#       ref_metal.metallib (METAL_SHADERS=yes) and no .pak and no
#       baseq2/, as no IOS_BUNDLE_DATA was given.
# Each assertion first runs on scratch mutants that it must refuse,
# naming the cause, and on a good sample it must accept, so an assertion
# that passes everything fails here.
#
# Build time, measured here (6 cores, -j3): the default about 8 s; --full
# about 25 s more, most of it the 231 vendored SDL3 sources, which the
# default does not compile. So the gate runs the default, and --full is
# run by hand (a CI step for it is left to a later card).
#
# Without the iOS simulator SDK (another OS, or Xcode without its iOS
# platform) the check prints one "skipped" line and exits 0. With the
# SDK a missing tool or a failed build is a failure, never a skip.
#
# IOS_CHECK_MAKE_ARGS adds make arguments, to run the check against a
# deliberately broken build.
#
# Usage: tests/check-ios.sh [--full]
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C
unset MAKEFLAGS GNUMAKEFLAGS MFLAGS

full=0
case "${1:-}" in
"") ;;
--full) full=1 ;;
*) echo "usage: tests/check-ios.sh [--full]" >&2; exit 2 ;;
esac

if [ "$(uname -s)" != Darwin ]; then
	echo "check-ios: skipped: the iOS mode builds on macOS only (docs/decisions/0008)"
	exit 0
fi
sdk=
if command -v xcrun >/dev/null 2>&1; then
	sdk=$(xcrun --sdk iphonesimulator --show-sdk-path 2>/dev/null) || sdk=
fi
if [ -z "$sdk" ] || [ ! -f "$sdk/SDKSettings.plist" ]; then
	echo "check-ios: skipped: no iOS simulator SDK (xcrun --sdk iphonesimulator --show-sdk-path)"
	exit 0
fi

fail=0
bad() { echo "check-ios: $*"; fail=1; }

tools="make nm otool plutil awk sort comm find grep sed strings bash python3"
[ "$full" = 1 ] && tools="$tools codesign"
for tool in $tools; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "check-ios: $tool not found: a missing tool is a failure"
		exit 1
	fi
done

tmp=$(mktemp -d "${TMPDIR:-/tmp}/check-ios.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT
jobs=${CHECK_JOBS:-4}
extra=${IOS_CHECK_MAKE_ARGS:-}
minos=17.0
target=arm64-apple-ios$minos-simulator

# ---- the assertions; each prints its cause and returns 1 ----------------

# "<platform> <minos>" of LC_BUILD_VERSION, empty without one
platform_of()
{
	otool -l "$1" 2>/dev/null | awk '$1 == "cmd" { c = $2 } c == "LC_BUILD_VERSION" && $1 == "platform" { p = $2 }
		c == "LC_BUILD_VERSION" && $1 == "minos" { print p, $2; exit }'
}

# every file named in <list> is built for the simulator at the minimum version
assert_platform() # <list file>
{
	rc=0
	while read -r f; do
		got=$(platform_of "$f")
		if [ -z "$got" ]; then
			echo "$f has no LC_BUILD_VERSION, so no platform 7 (IOSSIMULATOR)"
			rc=1
		elif [ "$got" != "7 $minos" ]; then
			echo "$f is built for platform ${got%% *} minos ${got#* }, not platform 7 (IOSSIMULATOR) minos $minos"
			rc=1
		fi
	done < "$1"
	return $rc
}

# the object's only global definition is its entry point
assert_exports() # <object> <entry point>
{
	got=$(nm -g -U "$1" | awk 'NF == 3 { print $3 }' | sort)
	if [ "$got" != "$2" ]; then
		echo "$1 defines $(printf '%s\n' "$got" | grep -c .) globals, not only $2: $(printf '%s\n' "$got" | head -n 6 | tr '\n' ' ')"
		return 1
	fi
}

# no object in <list> references a class of <AppKit class list>
assert_no_appkit() # <list file> <class list>
{
	rc=0
	while read -r f; do
		nm -u "$f" | sed -n 's/^_OBJC_CLASS_\$_//p' | sort -u > "${tmp:?}/used"
		hit=$(comm -12 "${tmp:?}/used" "$2" | tr '\n' ' ')
		if [ -n "$hit" ]; then
			echo "$f references AppKit classes: $hit"
			rc=1
		fi
	done < "$1"
	return $rc
}

assert_plist() # <Info.plist>
{
	if ! plutil -lint -s "$1" > "${tmp:?}/lint" 2>&1; then
		echo "$1 fails plutil -lint: $(head -n 1 "${tmp:?}/lint")"
		return 1
	fi
	rc=0
	exe=$(plutil -extract CFBundleExecutable raw -o - "$1" 2>/dev/null) || exe="(none)"
	[ "$exe" = quake2 ] || { echo "$1: CFBundleExecutable is $exe, not quake2"; rc=1; }
	plat=$(plutil -extract CFBundleSupportedPlatforms.0 raw -o - "$1" 2>/dev/null) || plat="(none)"
	[ "$plat" = iPhoneSimulator ] || { echo "$1: CFBundleSupportedPlatforms is $plat, not iPhoneSimulator"; rc=1; }
	min=$(plutil -extract MinimumOSVersion raw -o - "$1" 2>/dev/null) || min="(none)"
	[ "$min" = "$minos" ] || { echo "$1: MinimumOSVersion is $min, not $minos"; rc=1; }
	id=$(plutil -extract CFBundleIdentifier raw -o - "$1" 2>/dev/null) || id=
	[ -n "$id" ] || { echo "$1: no CFBundleIdentifier"; rc=1; }
	for key in UIFileSharingEnabled LSSupportsOpeningDocumentsInPlace; do
		v=$(plutil -extract "$key" raw -o - "$1" 2>/dev/null) || v="(none)"
		[ "$v" = true ] || { echo "$1: $key is $v, not true (the Files app shows Documents only with it)"; rc=1; }
	done
	if grep -E '@[A-Z_]+@' "$1" >/dev/null; then
		echo "$1 holds a template key: $(grep -E -o '@[A-Z_]+@' "$1" | sort -u | tr '\n' ' ')"
		rc=1
	fi
	return $rc
}

# the commands of a macOS build name no iOS target or SDK: no word that
# is -target or ios, or holds iPhone (the SDKs, iPhoneSimulator.sdk, the
# iOS build root) or apple-ios. Not a path through an ios/ directory:
# the vendored SDL3's source list is multi-platform (main/ios/, hidapi/ios/).
assert_macos_cmds() # <make -n output>
{
	hit=$(tr -s ' \t' '\n\n' < "$1" | grep -E -e '^-target$' -e '^[iI][oO][sS]$' -e '[iI][pP][hH][oO][nN][eE]' -e 'apple-ios' \
		| sort -u | head -n 5 | tr '\n' ' ') || true
	if [ -n "$hit" ]; then
		echo "$1: the macOS build's commands carry iOS tokens: $hit"
		return 1
	fi
}

# the compile command of every object of ios_objects is the one of ios
assert_same_compiles() # <make -n ios_objects> <make -n ios>
{
	grep -E ' -c .* -o [^ ]*/obj-metal/[^ ]+\.o( |$)|/obj-metal/[^ ]+\.o$' "$1" | sort -u > "${tmp:?}/c.objects"
	grep -E ' -c .* -o [^ ]*/obj-metal/[^ ]+\.o( |$)|/obj-metal/[^ ]+\.o$' "$2" | sort -u > "${tmp:?}/c.ios"
	n=$(grep -c . "${tmp:?}/c.objects") || n=0
	if [ "$n" -lt 1 ]; then
		echo "$1 holds no compile command of an iOS object"
		return 1
	fi
	only=$(comm -23 "${tmp:?}/c.objects" "${tmp:?}/c.ios" | head -n 2 | cut -c 1-300)
	if [ -n "$only" ]; then
		echo "ios_objects compiles an object with another command than ios: $only"
		return 1
	fi
}

# otool -L output: no module library, no macOS-only framework
assert_libs() # <otool -L output>
{
	hit=$(grep -E -o '(game|ref_[a-z0-9_]+)\.(dylib|so)|/(AppKit|Cocoa|Carbon|IOKit)\.framework/' "$1" | sort -u | tr '\n' ' ') || true
	if [ -n "$hit" ]; then
		echo "the executable links $hit"
		return 1
	fi
}

assert_entry_points() # <executable>
{
	rc=0
	for sym in _GetGameAPI _GetRefAPI; do
		if ! nm -g -U "$1" | awk -v s="$sym" 'NF == 3 && $3 == s { f = 1 } END { exit !f }'; then
			echo "$sym is not defined in $1"
			rc=1
		fi
	done
	return $rc
}

# main.c is SDL's on iOS: SDL_main.h renames its main SDL_main and adds
# SDL's main, which calls SDL_RunApp (UIApplicationMain). In the linked
# executable both mains are local (t), -fvisibility=hidden.
assert_sdl_main() # <object or executable>
{
	if ! nm "$1" > "${tmp:?}/nm" 2>&1; then
		echo "nm failed on $1: $(head -n 1 "${tmp:?}/nm")"
		return 1
	fi
	if ! awk 'NF == 3 && ($2 == "T" || $2 == "t") && $3 == "_SDL_main" { f = 1 } END { exit !f }' "${tmp:?}/nm"; then
		echo "$1 defines no _SDL_main: main.c does not include SDL_main.h, so UIApplicationMain never runs"
		return 1
	fi
	if ! awk '$NF == "_SDL_RunApp" { f = 1 } END { exit !f }' "${tmp:?}/nm"; then
		echo "$1 has no _SDL_RunApp: its main is not SDL's, so UIApplicationMain never runs"
		return 1
	fi
}

# rm_main.o: the simulator's skips the apple5 floor (and says so), the
# device's refuses a device below it
assert_floor() # <object> sim|device
{
	if ! strings -a "$1" > "${tmp:?}/strings" 2>&1; then
		echo "strings failed on $1"
		return 1
	fi
	skip=0
	grep -F 'is not checked in the simulator' "${tmp:?}/strings" >/dev/null && skip=1
	refuse=0
	grep -F 'supports no Apple GPU family from apple' "${tmp:?}/strings" >/dev/null && refuse=1
	if [ "$2" = sim ] && [ "$skip" != 1 ]; then
		echo "$1 has no line that the simulator does not check the apple5 floor: RI_Init refuses the simulator's device"
		return 1
	fi
	if [ "$2" = device ] && [ "$skip" = 1 ]; then
		echo "$1 skips the apple5 floor in the device build: the simulator's guard leaks into it"
		return 1
	fi
	if [ "$2" = device ] && [ "$refuse" != 1 ]; then
		echo "$1 has no apple5 floor refusal in the device build"
		return 1
	fi
}

assert_signed() # <bundle>
{
	if ! codesign --verify --strict "$1" > "${tmp:?}/sig" 2>&1; then
		echo "$1 fails codesign --verify --strict: $(head -n 2 "${tmp:?}/sig" | tr '\n' ' ')"
		return 1
	fi
}

assert_no_data() # <bundle>
{
	hit=$(cd "$1" && find . \( -iname '*.pak' -o -name baseq2 \) | sort | head -n 5 | tr '\n' ' ')
	if [ -n "$hit" ]; then
		echo "$1 holds game data although no IOS_BUNDLE_DATA was given: $hit"
		return 1
	fi
}

# ---- the assertions on scratch mutants -----------------------------------

mutants=0
refused() # <name> <text its cause must hold> <assertion> <args>...
{
	name=$1
	want=$2
	shift 2
	mutants=$((mutants + 1))
	if "$@" > "${tmp:?}/mut.out" 2>&1; then
		bad "mutant $name passed $1"
	elif ! grep -F -e "$want" "${tmp:?}/mut.out" >/dev/null; then
		bad "mutant $name was refused by $1 without naming its cause ($want): $(head -n 1 "${tmp:?}/mut.out")"
	fi
}
accepted() # <name> <assertion> <args>...
{
	name=$1
	shift
	if ! "$@" > "${tmp:?}/mut.out" 2>&1; then
		bad "the good sample $name was refused by $1: $(head -n 1 "${tmp:?}/mut.out")"
	fi
}

m=$tmp/mut
mkdir -p "${m:?}/good.app" "${m:?}/data.app/baseq2" "${m:?}/signed.app"
cc_ios() { xcrun -sdk iphonesimulator clang -target "$target" -isysroot "$sdk" "$@"; }

printf 'int GetRefAPI(void) { return 1; }\nint rm_helper(void) { return 2; }\n' > "${m:?}/two.c"
printf 'extern char OBJC_CLASS_$_NSWindow;\nvoid *w(void) { return &OBJC_CLASS_$_NSWindow; }\n' > "${m:?}/appkit.c"
printf 'extern char OBJC_CLASS_$_NSString;\nvoid *s(void) { return &OBJC_CLASS_$_NSString; }\n' > "${m:?}/foundation.c"
printf 'int main(void) { return 0; }\n' > "${m:?}/main.c"
printf 'int SDL_main(int c, char **v) { (void)c; (void)v; return 0; }\n' > "${m:?}/sdlmain-only.c"
printf '%s\n' 'int SDL_RunApp(int c, char **v, int (*f)(int, char **), void *r);' \
	'int SDL_main(int c, char **v) { (void)c; (void)v; return 0; }' \
	'int main(int c, char **v) { return SDL_RunApp(c, v, SDL_main, 0); }' > "${m:?}/sdlmain.c"
printf '%s\n' 'const char *skip = "ref_metal: the device floor (apple%d) is not checked in the simulator";' \
	'const char *refuse = "ref_metal: the Metal device supports no Apple GPU family from apple%d on";' > "${m:?}/leak.c"
printf '%s\n' 'const char *refuse = "ref_metal: the Metal device supports no Apple GPU family from apple%d on";' > "${m:?}/floor.c"
printf '%s\n' 'const char *skip = "ref_metal: the device floor (apple%d) is not checked in the simulator";' > "${m:?}/skip.c"
if ! cc_ios -c "${m:?}/two.c" -o "${m:?}/two.o" > "${m:?}/cc.out" 2>&1 \
	|| ! cc_ios -c "${m:?}/appkit.c" -o "${m:?}/appkit.o" >> "${m:?}/cc.out" 2>&1 \
	|| ! cc_ios -c "${m:?}/foundation.c" -o "${m:?}/foundation.o" >> "${m:?}/cc.out" 2>&1 \
	|| ! cc_ios "${m:?}/main.c" -o "${m:?}/signed.app/quake2" >> "${m:?}/cc.out" 2>&1 \
	|| ! cc_ios -c "${m:?}/main.c" -o "${m:?}/main.o" >> "${m:?}/cc.out" 2>&1 \
	|| ! cc_ios -c "${m:?}/sdlmain-only.c" -o "${m:?}/sdlmain-only.o" >> "${m:?}/cc.out" 2>&1 \
	|| ! cc_ios -c "${m:?}/sdlmain.c" -o "${m:?}/sdlmain.o" >> "${m:?}/cc.out" 2>&1 \
	|| ! cc_ios -c "${m:?}/skip.c" -o "${m:?}/skip.o" >> "${m:?}/cc.out" 2>&1 \
	|| ! xcrun -sdk iphoneos clang -target arm64-apple-ios$minos -c "${m:?}/leak.c" -o "${m:?}/leak.o" >> "${m:?}/cc.out" 2>&1 \
	|| ! xcrun -sdk iphoneos clang -target arm64-apple-ios$minos -c "${m:?}/floor.c" -o "${m:?}/floor.o" >> "${m:?}/cc.out" 2>&1 \
	|| ! xcrun -sdk macosx clang -target arm64-apple-macos13.0 -c "${m:?}/two.c" -o "${m:?}/macos.o" >> "${m:?}/cc.out" 2>&1 \
	|| ! xcrun -sdk iphonesimulator ld -r -arch arm64 -exported_symbol _GetRefAPI "${m:?}/two.o" -o "${m:?}/one.o" >> "${m:?}/cc.out" 2>&1; then
	tail -n 5 "${m:?}/cc.out"
	echo "check-ios: the mutants could not be compiled for the simulator"
	exit 1
fi

# (a)
printf '%s\n' "${m:?}/two.o" "${m:?}/macos.o" > "${m:?}/mixed.list"
printf '%s\n' "${m:?}/two.o" "${m:?}/one.o" > "${m:?}/ios.list"
refused "macOS object in the iOS tree" "macos.o is built for platform 1 " assert_platform "${m:?}/mixed.list"
accepted "simulator objects" assert_platform "${m:?}/ios.list"
# (b)
refused "renderer exporting a helper" "not only _GetRefAPI" assert_exports "${m:?}/two.o" _GetRefAPI
accepted "renderer exporting its entry point" assert_exports "${m:?}/one.o" _GetRefAPI
# (c)
mac_sdk=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null) || mac_sdk=
tbd=$mac_sdk/System/Library/Frameworks/AppKit.framework/AppKit.tbd
appkit=$tmp/appkit.classes
if [ -n "$mac_sdk" ] && [ -f "$tbd" ]; then
	awk '/objc-classes:/ { f = 1; sub(/.*objc-classes:/, "") }
		f { l = $0; gsub(/[][,'"'"']/, " ", l); n = split(l, w, " "); for (i = 1; i <= n; i++) print w[i]; if ($0 ~ /\]/) f = 0 }' \
		"$tbd" | sort -u > "${appkit:?}"
fi
if [ ! -s "$appkit" ] || ! grep -x NSWindow "$appkit" >/dev/null; then
	echo "check-ios: no AppKit class list (NSWindow) from the macOS SDK's AppKit.tbd ($tbd)"
	exit 1
fi
printf '%s\n' "${m:?}/appkit.o" > "${m:?}/appkit.list"
printf '%s\n' "${m:?}/foundation.o" > "${m:?}/foundation.list"
refused "object referencing NSWindow" "references AppKit classes: NSWindow" assert_no_appkit "${m:?}/appkit.list" "$appkit"
accepted "object referencing NSString" assert_no_appkit "${m:?}/foundation.list" "$appkit"
# (d), on the template filled in here as the Makefile does
sed -e 's/@BUNDLE_ID@/org.example.check/g' -e 's/@VERSION@/1.0/g' -e "s/@PLATFORM@/iPhoneSimulator/g" \
	-e "s/@MIN_OS@/$minos/g" stuff/ios/Info.plist.in > "${m:?}/good.app/Info.plist"
cp "${m:?}/good.app/Info.plist" "${m:?}/noexe.plist"
plutil -remove CFBundleExecutable "${m:?}/noexe.plist"
refused "Info.plist without CFBundleExecutable" "CFBundleExecutable is (none)" assert_plist "${m:?}/noexe.plist"
cp "${m:?}/good.app/Info.plist" "${m:?}/nosharing.plist"
plutil -remove UIFileSharingEnabled "${m:?}/nosharing.plist"
refused "Info.plist without UIFileSharingEnabled" "UIFileSharingEnabled is (none)" assert_plist "${m:?}/nosharing.plist"
cp "${m:?}/good.app/Info.plist" "${m:?}/noinplace.plist"
plutil -remove LSSupportsOpeningDocumentsInPlace "${m:?}/noinplace.plist"
refused "Info.plist without LSSupportsOpeningDocumentsInPlace" "LSSupportsOpeningDocumentsInPlace is (none)" assert_plist "${m:?}/noinplace.plist"
cp "${m:?}/good.app/Info.plist" "${m:?}/sharingoff.plist"
plutil -replace UIFileSharingEnabled -bool NO "${m:?}/sharingoff.plist"
refused "Info.plist with file sharing off" "UIFileSharingEnabled is false, not true" assert_plist "${m:?}/sharingoff.plist"
refused "Info.plist template unfilled" "template key: @BUNDLE_ID@" assert_plist stuff/ios/Info.plist.in
accepted "Info.plist filled in" assert_plist "${m:?}/good.app/Info.plist"
# (e)
printf '%s\n' "cc -c -O2 -target $target -isysroot $sdk src/x.c -o build/x.o" > "${m:?}/ioscmds.txt"
printf '%s\n' "cc -c -O2 -arch arm64 src/x.c -o build/release/x.o" \
	"cc -c third_party/SDL3/src/main/ios/x.m -o build/release/sdl3/main/ios/x.o" > "${m:?}/maccmds.txt"
refused "iOS target in a macOS command" "-target" assert_macos_cmds "${m:?}/ioscmds.txt"
printf '%s\n' "xcrun -sdk iphonesimulator metal -c x.metal -o x.air" > "${m:?}/iosmetal.txt"
refused "iOS SDK in a macOS shader command" "iphonesimulator" assert_macos_cmds "${m:?}/iosmetal.txt"
accepted "macOS command" assert_macos_cmds "${m:?}/maccmds.txt"
# (f)
printf '%s\n' "cc -c -O2 -DUSE_XDG -o build/ios/obj-metal/client/a.o a.c" > "${m:?}/ios.n"
printf '%s\n' "cc -c -O2 -o build/ios/obj-metal/client/a.o a.c" > "${m:?}/objects.n"
refused "ios_objects compiling without a flag of ios" "another command than ios" assert_same_compiles "${m:?}/objects.n" "${m:?}/ios.n"
accepted "ios_objects compiling as ios" assert_same_compiles "${m:?}/ios.n" "${m:?}/ios.n"
# (g)
refused "executable without the entry points" "_GetRefAPI is not defined" assert_entry_points "${m:?}/signed.app/quake2"
# (h)
printf '%s\n' "x:" "	/System/Library/Frameworks/AppKit.framework/AppKit (compatibility version 45.0.0)" > "${m:?}/appkit.L"
printf '%s\n' "x:" "	@rpath/ref_metal.dylib (compatibility version 0.0.0)" > "${m:?}/ref.L"
printf '%s\n' "x:" "	/System/Library/Frameworks/UIKit.framework/UIKit (compatibility version 1.0.0)" > "${m:?}/uikit.L"
refused "executable linking AppKit" "/AppKit.framework/" assert_libs "${m:?}/appkit.L"
refused "executable linking ref_metal.dylib" "ref_metal.dylib" assert_libs "${m:?}/ref.L"
accepted "executable linking UIKit" assert_libs "${m:?}/uikit.L"
# (i): a bundle signed as the Makefile signs it, then changed
cp "${m:?}/good.app/Info.plist" "${m:?}/signed.app/Info.plist"
if ! codesign --force --sign - --timestamp=none "${m:?}/signed.app" > "${m:?}/sign.out" 2>&1; then
	head -n 3 "${m:?}/sign.out"
	echo "check-ios: codesign cannot sign a bundle ad hoc"
	exit 1
fi
accepted "bundle signed ad hoc" assert_signed "${m:?}/signed.app"
plutil -replace CFBundleName -string changed "${m:?}/signed.app/Info.plist"
refused "bundle changed after signing" "fails codesign --verify --strict" assert_signed "${m:?}/signed.app"
# (j)
: > "${m:?}/data.app/baseq2/smoke.pak"
refused "bundle with a .pak but no IOS_BUNDLE_DATA" "smoke.pak" assert_no_data "${m:?}/data.app"
accepted "bundle without data" assert_no_data "${m:?}/good.app"
# (k)
refused "main.o built from int main(void)" "defines no _SDL_main" assert_sdl_main "${m:?}/main.o"
refused "main.o defining SDL_main without SDL's main" "has no _SDL_RunApp" assert_sdl_main "${m:?}/sdlmain-only.o"
accepted "main.o of SDL_main.h's shape" assert_sdl_main "${m:?}/sdlmain.o"
# (l)
refused "simulator rm_main.o without the floor line" "does not check the apple5 floor" assert_floor "${m:?}/two.o" sim
refused "device rm_main.o with the simulator's guard" "the simulator's guard leaks" assert_floor "${m:?}/leak.o" device
refused "device rm_main.o without the floor refusal" "no apple5 floor refusal" assert_floor "${m:?}/two.o" device
accepted "simulator rm_main.o" assert_floor "${m:?}/skip.o" sim
accepted "device rm_main.o" assert_floor "${m:?}/floor.o" device

# ---- the build -----------------------------------------------------------

tree=$tmp/tree
mkdir -p "${tree:?}/third_party" "${tree:?}/stuff"
cp -R Makefile src "${tree:?}/"
cp -R stuff/ios "${tree:?}/stuff/"
cp third_party/sdl3.make "${tree:?}/third_party/"
ln -s "$PWD/third_party/SDL3" "${tree:?}/third_party/SDL3"
root=$tree/build/ios-iphonesimulator/release
mk() { make -C "${tree:?}" "$@"; }

# (m): each refused at parse time, naming its cause, never evaluated
mk_refused() # <name> <text its cause must hold> <make arguments>...
{
	name=$1
	want=$2
	shift 2
	mutants=$((mutants + 1))
	if mk -n ios_objects IOS_SDK=iphonesimulator METAL_SHADERS=no "$@" > "${tmp:?}/mk.out" 2>&1; then
		bad "make accepted $name: $*"
	elif ! grep -F -e "$want" "${tmp:?}/mk.out" >/dev/null; then
		bad "make refused $name without naming its cause ($want): $(tail -n 1 "${tmp:?}/mk.out")"
	fi
	if grep -x EVALUATED "${tmp:?}/mk.out" >/dev/null; then
		bad "make evaluated $name as make text: $*"
	fi
}
under="must not be release/ or debug/"
meta="must be one path of letters"
mk_refused "IOS_ROOT release" "$under" IOS_ROOT=release
mk_refused "IOS_ROOT debug/x" "$under" IOS_ROOT=debug/x
mk_refused "IOS_ROOT \$PWD/release" "$under" IOS_ROOT="$tree/release"
mk_refused "IOS_ROOT ./build/../release" "$under" IOS_ROOT=./build/../release
mk_refused "IOS_ROOT ./././release" "$under" IOS_ROOT=./././release
# letter case: the default APFS volume ignores it, and release/ and debug/
# do not exist in this tree
[ ! -e "${tree:?}/release" ] && [ ! -e "${tree:?}/debug" ] || bad "the scratch tree holds a release/ or debug/"
mk_refused "IOS_ROOT RELEASE" "$under" IOS_ROOT=RELEASE
mk_refused "IOS_ROOT Release/sub" "$under" IOS_ROOT=Release/sub
mk_refused "IOS_ROOT Debug" "$under" IOS_ROOT=Debug
ln -s "$tree" "${tmp:?}/tree-link"
mk_refused "IOS_ROOT through a symlink" "$under" IOS_ROOT="$tmp/tree-link/debug/sub"
mk_refused "IOS_ROOT with make text" "$meta" IOS_ROOT="$tmp/a\$(info EVALUATED)b"
mk_refused "IOS_ROOT with a glob" "$meta" IOS_ROOT="$tmp/*"
mk_refused "IOS_ROOT with [" "$meta" IOS_ROOT="$tmp/a[b]"
mk_refused "IOS_ROOT with ;" "$meta" IOS_ROOT="$tmp/a;b"
mk_refused "IOS_ROOT with a quote" "$meta" IOS_ROOT="$tmp/a'b"
mk_refused "IOS_ROOT with a space" "$meta" IOS_ROOT="$tmp/a b"
mk_refused "IOS_ROOT as an option" "$meta" IOS_ROOT=-rf
mk_refused "IOS_BUNDLE_DATA with make text" "IOS_BUNDLE_DATA $meta" IOS_BUNDLE_DATA="$tmp/a\$(info EVALUATED)b"
mk_refused "IOS_BUNDLE_DATA with a glob" "IOS_BUNDLE_DATA $meta" IOS_BUNDLE_DATA="$tmp/a?b"
mk_refused "IOS_BUNDLE_DATA with a backquote" "IOS_BUNDLE_DATA $meta" IOS_BUNDLE_DATA="$tmp/a\`b"
mk_refused "IOS_BUNDLE_DATA with a quote" "IOS_BUNDLE_DATA $meta" IOS_BUNDLE_DATA="$tmp/a'b"
if ! mk -n ios_objects IOS_SDK=iphonesimulator METAL_SHADERS=no IOS_ROOT="$tmp/elsewhere/ios" IOS_BUNDLE_DATA="$tmp/data-ok" > "${tmp:?}/mk.out" 2>&1; then
	bad "make refused an IOS_ROOT and IOS_BUNDLE_DATA outside the tree: $(tail -n 1 "${tmp:?}/mk.out")"
fi

# (n)
nself=0
if bash scripts/ios-sim.sh selftest > "${tmp:?}/selftest.log" 2>&1; then
	nself=$(grep -c '^ios-sim: selftest: ok [a-z]' "${tmp:?}/selftest.log") || nself=0
else
	grep -E 'FAIL|^    |cases failed' "${tmp:?}/selftest.log" | head -n 30
	bad "scripts/ios-sim.sh selftest failed"
fi

# what the build is to write, and the commands ios would compile it with
# shellcheck disable=SC2086 # extra is a word list
if ! mk -n -B VERBOSE=1 ios_objects IOS_SDK=iphonesimulator METAL_SHADERS=no $extra > "${tmp:?}/objects.n" 2>&1 \
	|| ! mk -n -B VERBOSE=1 ios IOS_SDK=iphonesimulator METAL_SHADERS=no $extra > "${tmp:?}/ios.n" 2>&1; then
	tail -n 5 "${tmp:?}/objects.n" "${tmp:?}/ios.n" 2>/dev/null || true
	echo "check-ios: the iOS mode cannot be evaluated (make -n ios_objects, make -n ios)"
	exit 1
fi
grep -E -o ' -o [^ ]+\.o( |$)' "${tmp:?}/objects.n" | awk '{ print $2 }' | sort -u > "${tmp:?}/expected"

start=$(date +%s)
goal=ios_objects
[ "$full" = 1 ] && goal=ios
ms=no
[ "$full" = 1 ] && ms=${METAL_SHADERS:-yes}
# shellcheck disable=SC2086 # extra is a word list
if ! mk -j"$jobs" QUIET=1 $goal IOS_SDK=iphonesimulator METAL_SHADERS="$ms" $extra > "${tmp:?}/build.log" 2>&1; then
	tail -n 30 "${tmp:?}/build.log"
	echo "check-ios: the build failed (make $goal IOS_SDK=iphonesimulator METAL_SHADERS=$ms $extra)"
	exit 1
fi
elapsed=$(( $(date +%s) - start ))

# (a)
nexp=$(grep -c . "${tmp:?}/expected") || nexp=0
if [ "$nexp" -lt 100 ]; then
	bad "make -n ios_objects names $nexp objects, not the client, game and ref_metal"
fi
while read -r o; do
	[ -s "$tree/$o" ] || bad "the build did not write $o"
done < "${tmp:?}/expected"
# every object in the tree, SDL's too under --full
(cd "${tree:?}" && find build -name '*.o' | sort) > "${tmp:?}/objects.rel"
sed "s|^|$tree/|" "${tmp:?}/objects.rel" > "${tmp:?}/objects"
nobj=$(grep -c . "${tmp:?}/objects") || nobj=0
out=$(assert_platform "${tmp:?}/objects") || bad "$out"
# (b)
out=$(assert_exports "$root/obj-metal/static/game.o" _GetGameAPI) || bad "$out"
out=$(assert_exports "$root/obj-metal/static/ref_metal.o" _GetRefAPI) || bad "$out"
# (c)
out=$(assert_no_appkit "${tmp:?}/objects" "$appkit") || bad "$out"
# (d)
out=$(assert_plist "$root/Info.plist") || bad "$out"
# (e)
if ! mk -n -B VERBOSE=1 client ref_metal > "${tmp:?}/macos.n" 2>&1; then
	tail -n 5 "${tmp:?}/macos.n"
	bad "make -n client ref_metal failed"
fi
out=$(assert_macos_cmds "${tmp:?}/macos.n") || bad "$out"
# (f)
out=$(assert_same_compiles "${tmp:?}/objects.n" "${tmp:?}/ios.n") || bad "$out"
# (k)
out=$(assert_sdl_main "$root/obj-metal/client/src/backends/unix/main.o") || bad "$out"
# (l): the simulator's object, and the device's compiled with its own command
out=$(assert_floor "$root/obj-metal/ref_metal/src/client/refresh/metal/rm_main.o" sim) || bad "$out"
dev=$tree/build/ios-iphoneos-check
# shellcheck disable=SC2086 # extra is a word list
if ! mk -n -B VERBOSE=1 ios_objects IOS_SDK=iphoneos METAL_SHADERS=no IOS_ROOT="$dev" $extra > "${tmp:?}/device.n" 2>&1; then
	tail -n 5 "${tmp:?}/device.n"
	bad "make -n ios_objects IOS_SDK=iphoneos failed"
fi
grep -E ' -c .* -o [^ ]*/rm_main\.o src/client/refresh/metal/rm_main\.c$' "${tmp:?}/device.n" | head -n 1 > "${tmp:?}/device.cc"
if [ ! -s "${tmp:?}/device.cc" ]; then
	bad "make -n ios_objects IOS_SDK=iphoneos has no compile command of rm_main.c"
else
	mkdir -p "${dev:?}/obj-metal/ref_metal/src/client/refresh/metal"
	if (cd "${tree:?}" && sh "${tmp:?}/device.cc") > "${tmp:?}/device.out" 2>&1; then
		out=$(assert_floor "$dev/obj-metal/ref_metal/src/client/refresh/metal/rm_main.o" device) || bad "$out"
	else
		tail -n 5 "${tmp:?}/device.out"
		bad "rm_main.c does not compile with the device build's command"
	fi
fi

if [ "$full" = 1 ]; then
	app=$root/Quake2.app
	exe=$app/quake2
	if [ ! -x "$exe" ]; then
		echo "check-ios: $exe was not produced"
		exit 1
	fi
	# (g)
	printf '%s\n' "$exe" > "${tmp:?}/exe.list"
	out=$(assert_platform "${tmp:?}/exe.list") || bad "$out"
	out=$(assert_entry_points "$exe") || bad "$out"
	out=$(assert_sdl_main "$exe") || bad "$out"
	# (h)
	otool -L "$exe" > "${tmp:?}/libs" || bad "otool -L failed on $exe"
	out=$(assert_libs "${tmp:?}/libs") || bad "$out"
	# (i)
	out=$(assert_signed "$app") || bad "$out"
	# (j)
	out=$(assert_plist "$app/Info.plist") || bad "$out"
	out=$(assert_no_data "$app") || bad "$out"
	if [ "$ms" = yes ] && [ ! -s "$app/ref_metal.metallib" ]; then
		bad "$app holds no ref_metal.metallib although METAL_SHADERS=yes"
	fi
fi

echo "check-ios: build $elapsed s ($goal)"
if [ "$fail" -ne 0 ]; then
	exit 1
fi
echo "check-ios: ok ($mutants mutants refused; $nself smoke selftest cases; $nobj objects for the simulator; $goal)"
