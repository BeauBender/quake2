#!/bin/sh
# Invariant: the iOS mode of the Makefile builds the engine for the tvOS
# simulator too (TASK23: "make ios_objects IOS_SDK=appletvsimulator"), from
# the same object list as the iOS simulator build, and the iPhone and iPad
# only code is compiled out of it.
#
# Every decision is taken from the build's artifacts (LC_BUILD_VERSION in
# otool -l, the code of otool -tV, plutil, the commands of make -n) or from
# the source text of the guards, never from a line make prints.
#   (d) the source: each iPhone and iPad only site (the touch overlay's
#       Touch_Supported, the in_touch default, the on-screen keyboard's
#       follow-up, the window's orientation and home-indicator hints) is
#       inside an #if that uses TARGET_OS_IOS's value (TARGET_OS_IPHONE is
#       true on tvOS and visionOS too): not an #ifdef, not defined() alone
#       and not negated, since the macro is defined, to 0, there too. It
#       runs everywhere, before the skips.
# In a scratch IOS_ROOT, with METAL_SHADERS=no (no SDL, no link):
#   (a) every object of the tree has LC_BUILD_VERSION platform 8
#       (TVOSSIMULATOR) and minos 17.0, and there is at least one;
#   (b) the tvOS build compiles the same object list as the iOS simulator
#       build (make -n ios_objects, the "-o <path>" of each compile, the
#       build root removed): the tvOS target adds and drops no file;
#   (c) the Info.plist passes plutil -lint, names AppleTVSimulator and
#       17.0, UIDeviceFamily 3 (Apple TV) alone, no template key, and none
#       of the iPhone's UIFileSharingEnabled, orientation or status bar keys;
#   (e) the compiled touch.o: Touch_Supported is "mov w0, #0x0; ret", 0
#       whatever the device; and nm -u of every imgdecode.o lists
#       _kCGImageSourceAllowableTypes (ImageIO reads only the file's type).
# It does not compare the iOS build with an earlier one; that comparison is
# the review's before and after of make -n (docs/runbook/TASK23.md).
# Each assertion first runs on a mutant it must refuse (an object built for
# the iOS simulator, a different object list, an iPhone plist, a plist with
# a forbidden key, also spelled with an XML character reference, two device
# families or another minimum OS, each site's guard reverted, made an
# #ifdef, a defined() or negated, a Touch_Supported that returns 1 or a
# variable, an object that does not need the symbol, an unreadable one), so
# an assertion that passes everything fails here. A tool's error refuses.
#
# Without the tvOS simulator SDK (another OS, or Xcode without its tvOS
# platform) the build checks print one "skipped" line and exit 0 after (d).
# With the SDK a missing tool or a failed build is a failure, never a skip.
#
# Usage: tests/check-tvos.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C
unset MAKEFLAGS GNUMAKEFLAGS MFLAGS

tmp=$(mktemp -d "${TMPDIR:-/tmp}/check-tvos.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT
fail=0
bad() { echo "check-tvos: $*"; fail=1; }

# ---- (d) the iPhone and iPad only sites ----------------------------------

# <file>|<the definition the site is in>|<a line inside the guarded block>
# (awk EREs; awk -v takes a backslash as an escape, so brackets, not \( )
sites='src/client/input/touch.c|^Touch_Supported[(]void[)]|return 1;
src/client/input/sdl3.c|^IN_Update[(]void[)]|The on-screen keyboard only while text is typed
src/client/input/sdl3.c|^IN_Init[(]void[)]|in_touch = Cvar_Get[(]"in_touch", "1"
src/client/vid/glimp_sdl3.c|^CreateSDLWindow[(]|SDL_HINT_ORIENTATIONS'

# guard_of <file> <start> <anchor>: "<line>:<text>" of the innermost #if
# around the first <anchor> after <start>; "else:" when the anchor is in its
# #else or #elif branch; nothing when the anchor or the #if is missing
guard_of()
{
	awk -v s="$2" -v a="$3" '
		!on { if ($0 ~ s) on = 1; next }
		/^[ \t]*#[ \t]*if/ { d++; ln[d] = NR; tx[d] = $0; el[d] = 0; next }
		/^[ \t]*#[ \t]*(else|elif)/ { if (d) el[d] = 1; next }
		/^[ \t]*#[ \t]*endif/ { if (d) d--; next }
		$0 ~ a { if (d && el[d]) print "else:"; else if (d) print ln[d] ":" tx[d]; exit }
	' "$1"
}

# assert_guard <file> <start> <anchor>: that guard is an #if that takes
# TARGET_OS_IOS's value, not negated. Refused: an #ifdef or #ifndef, and
# defined(TARGET_OS_IOS) alone (both are true on tvOS and visionOS too,
# where the macro is defined to 0), a ! before the name.
assert_guard()
{
	g=$(guard_of "$1" "$2" "$3")
	case $g in
	'') echo "$1: no #if around \"$3\" after $2"; return 1 ;;
	else:) echo "$1: \"$3\" after $2 is in an #else branch"; return 1 ;;
	esac
	# a comment on the #if line does not count
	gx=$(printf '%s\n' "${g#*:}" | sed -e 's,/\*.*\*/,,g' -e 's,//.*,,')
	if printf '%s\n' "$gx" | grep -E -q '^[ \t]*#[ \t]*ifn?def'; then
		echo "$1:${g%%:*}: the guard of \"$3\" is an #ifdef or #ifndef, which does not read the value: ${g#*:}"
		return 1
	fi
	if printf '%s\n' "$gx" | grep -E -q '![ \t(]*TARGET_OS_IOS([^_[:alnum:]]|$)'; then
		echo "$1:${g%%:*}: the guard of \"$3\" negates TARGET_OS_IOS: ${g#*:}"
		return 1
	fi
	# defined(TARGET_OS_IOS) and defined TARGET_OS_IOS do not count: only a use of the value does
	if ! printf '%s\n' "$gx" | sed -E -e 's/defined[ \t]*[(][ \t]*[_[:alnum:]]+[ \t]*[)]//g' -e 's/defined[ \t]+[_[:alnum:]]+//g' |
		grep -E -q '(^|[^_[:alnum:]])TARGET_OS_IOS([^_[:alnum:]]|$)'; then
		echo "$1:${g%%:*}: the guard of \"$3\" does not name TARGET_OS_IOS: ${g#*:}"
		return 1
	fi
}

n=0
while IFS='|' read -r f s a; do
	n=$((n + 1))
	if ! assert_guard "$f" "$s" "$a" > "${tmp:?}/o" 2>&1; then
		cat "${tmp:?}/o"
		bad "(d) site $n is not iPhone and iPad only"
		continue
	fi
	# the mutant: this site's guard back to TARGET_OS_IPHONE && !TARGET_OS_TV (true on visionOS)
	l=$(guard_of "$f" "$s" "$a")
	sed "${l%%:*}s/TARGET_OS_IOS/TARGET_OS_IPHONE \&\& !TARGET_OS_TV/" "$f" > "${tmp:?}/mut$n.c"
	if assert_guard "${tmp:?}/mut$n.c" "$s" "$a" > "${tmp:?}/o" 2>&1; then
		bad "mutant: site $n ($f, $a) with its guard reverted passed"
	elif ! grep -q 'does not name TARGET_OS_IOS' "${tmp:?}/o"; then
		bad "mutant: site $n was refused without naming the guard: $(cat "${tmp:?}/o")"
	fi
	# the guards that are true on visionOS or tvOS too: defined(), #ifdef, a ! before the name
	# (<sed expression on the guard's line>|<the refusal's words>)
	while IFS='|' read -r ex why; do
		sed "${l%%:*}$ex" "$f" > "${tmp:?}/mut$n.c"
		if cmp -s "$f" "${tmp:?}/mut$n.c"; then
			bad "mutant: the guard of site $n is not changed by $ex"
		elif assert_guard "${tmp:?}/mut$n.c" "$s" "$a" > "${tmp:?}/o" 2>&1; then
			bad "mutant: site $n ($f, $a) with its guard changed by $ex passed"
		elif ! grep -q "$why" "${tmp:?}/o"; then
			bad "mutant: site $n with $ex was refused without saying it $why: $(cat "${tmp:?}/o")"
		fi
	done <<MUT
s/TARGET_OS_IOS/defined(TARGET_OS_IOS)/|does not name TARGET_OS_IOS
s/TARGET_OS_IOS/defined TARGET_OS_IOS/|does not name TARGET_OS_IOS
s/.*/#ifdef TARGET_OS_IOS/|is an #ifdef or #ifndef
s/.*/#  ifdef TARGET_OS_IOS \/* iPhone *\//|is an #ifdef or #ifndef
s/TARGET_OS_IOS/!TARGET_OS_IOS/|negates TARGET_OS_IOS
s/TARGET_OS_IOS/! (TARGET_OS_IOS)/|negates TARGET_OS_IOS
MUT
done <<EOF
$sites
EOF
[ "$n" = 4 ] || bad "(d) $n sites, not 4"
if [ "$fail" != 0 ]; then
	exit 1
fi

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

for tool in make otool nm plutil awk sed sort grep cmp clang; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "check-tvos: $tool not found: a missing tool is a failure"
		exit 1
	fi
done

jobs=${CHECK_JOBS:-4}

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

# nm -u <object> lists <symbol>; a missing object or an nm or grep error refuses
assert_need() # <object> <symbol>
{
	[ -f "$1" ] || { echo "$1 is missing"; return 1; }
	nm -u "$1" > "${tmp:?}/nm" 2>/dev/null || { echo "$1: cannot read (nm -u failed)"; return 1; }
	nrc=0
	grep -x -F -q -e "$2" "${tmp:?}/nm" || nrc=$?
	case $nrc in
	0) return 0 ;;
	1) echo "$1 does not need $2 (nm -u)"; return 1 ;;
	*) echo "${tmp:?}/nm: cannot read (grep status $nrc)"; return 1 ;;
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
sed 's,<integer>3</integer>,&<integer>1</integer>,' "${tmp:?}/good.plist" > "${tmp:?}/m.plist"
if assert_plist "${tmp:?}/m.plist" > "${tmp:?}/o" 2>&1; then
	bad "mutant: a plist with the Apple TV and the iPhone families passed"
elif ! grep -q 'holds more than the Apple TV' "${tmp:?}/o"; then
	bad "mutant: the two-family plist was refused without naming the count"
fi
sed 's,<string>17\.0</string>,<string>16.0</string>,' "${tmp:?}/good.plist" > "${tmp:?}/m.plist"
if assert_plist "${tmp:?}/m.plist" > "${tmp:?}/o" 2>&1; then
	bad "mutant: a plist with MinimumOSVersion 16.0 passed"
elif ! grep -q 'MinimumOSVersion is not 17.0' "${tmp:?}/o"; then
	bad "mutant: the 16.0 plist was refused without naming the minimum OS"
fi

# Touch_Supported: a constant 0 passes, a 1 or a read of a variable does not
printf 'int Touch_Supported(void) { return 0; }\n' > "${tmp:?}/t0.c"
printf 'int Touch_Supported(void) { return 1; }\n' > "${tmp:?}/t1.c"
printf 'extern int tvos_g;\nint Touch_Supported(void) { return tvos_g; }\n' > "${tmp:?}/tv.c"
for t in t0 t1 tv; do
	xcrun --sdk appletvsimulator clang -O2 -target arm64-apple-tvos17.0-simulator -c "${tmp:?}/$t.c" -o "${tmp:?}/$t.o"
done
assert_no_touch "${tmp:?}/t0.o" > "${tmp:?}/o" 2>&1 || bad "a constant-0 Touch_Supported was refused: $(cat "${tmp:?}/o")"
for t in t1 tv; do
	if assert_no_touch "${tmp:?}/$t.o" > "${tmp:?}/o" 2>&1; then
		bad "mutant: a Touch_Supported that is not 0 ($t.c) passed"
	fi
done
if assert_no_touch "${tmp:?}/m_tv.o" > "${tmp:?}/o" 2>&1; then
	bad "mutant: an object without Touch_Supported passed"
fi

# _kCGImageSourceAllowableTypes: an object that needs it passes, one that
# does not (ImageIO not restricted), a missing or an unreadable one do not
printf 'extern const void *kCGImageSourceAllowableTypes;\nconst void *tvos_v(void) { return kCGImageSourceAllowableTypes; }\n' > "${tmp:?}/u.c"
xcrun --sdk appletvsimulator clang -target arm64-apple-tvos17.0-simulator -c "${tmp:?}/u.c" -o "${tmp:?}/u.o"
assert_need "${tmp:?}/u.o" _kCGImageSourceAllowableTypes > "${tmp:?}/o" 2>&1 || bad "an object that needs _kCGImageSourceAllowableTypes was refused: $(cat "${tmp:?}/o")"
if assert_need "${tmp:?}/m_tv.o" _kCGImageSourceAllowableTypes > "${tmp:?}/o" 2>&1; then
	bad "mutant: an object that does not need _kCGImageSourceAllowableTypes passed"
elif ! grep -q 'does not need _kCGImageSourceAllowableTypes' "${tmp:?}/o"; then
	bad "mutant: the object without the symbol was refused without naming it"
fi
cp "${tmp:?}/u.o" "${tmp:?}/unreadable.o"
chmod 000 "${tmp:?}/unreadable.o"
for obj in absent unreadable; do
	if [ "$obj" = unreadable ] && [ -r "${tmp:?}/unreadable.o" ]; then
		echo "check-tvos: note: running as a user who reads a mode 000 file (root): the unreadable-object mutant is not run"
		continue
	fi
	if assert_need "${tmp:?}/$obj.o" _kCGImageSourceAllowableTypes > "${tmp:?}/o" 2>&1; then
		bad "mutant: an $obj object passed the symbol check"
	elif ! grep -E -q 'is missing|cannot read' "${tmp:?}/o"; then
		bad "mutant: an $obj object was refused without saying it cannot be read: $(cat "${tmp:?}/o")"
	fi
done
chmod 600 "${tmp:?}/unreadable.o"
if [ "$fail" != 0 ]; then
	exit 1
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
grep '/touch\.o$' "${tmp:?}/objs" > "${tmp:?}/touch" || :
if [ "$(grep -c . "${tmp:?}/touch")" != 1 ]; then
	bad "(e) the build has $(grep -c . "${tmp:?}/touch") touch.o, not 1"
else
	assert_no_touch "$(cat "${tmp:?}/touch")" > "${tmp:?}/o" 2>&1 || { cat "${tmp:?}/o"; bad "(e) the touch overlay is compiled into the tvOS build"; }
fi
grep '/imgdecode\.o$' "${tmp:?}/objs" > "${tmp:?}/imgdecode" || :
if [ ! -s "${tmp:?}/imgdecode" ]; then
	bad "(e) the build has no imgdecode.o"
fi
while read -r obj; do
	assert_need "$obj" _kCGImageSourceAllowableTypes > "${tmp:?}/o" 2>&1 || { cat "${tmp:?}/o"; bad "(e) the image decoder does not restrict ImageIO to the file's type"; }
done < "${tmp:?}/imgdecode"

object_list /r IOS_SDK=appletvsimulator > "${tmp:?}/list_tv"
object_list /r IOS_SDK=iphonesimulator > "${tmp:?}/list_ios"
assert_same_list "${tmp:?}/list_ios" "${tmp:?}/list_tv" > "${tmp:?}/o" 2>&1 || { cat "${tmp:?}/o"; bad "(b) the tvOS object list is not the iOS simulator's"; }
[ -s "${tmp:?}/list_tv" ] || bad "(b) make -n listed no object"

if [ "$fail" != 0 ]; then
	exit 1
fi
echo "check-tvos: ok ($(grep -c . "${tmp:?}/objs") objects for the tvOS simulator, platform 8, the iOS simulator's list)"
