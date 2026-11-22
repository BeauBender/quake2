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
#       Touch_Supported, the in_touch default, the window's orientation and home-indicator hints) is
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
#       whatever the device; nm -u of every imgdecode.o lists
#       _CGImageSourceGetType, and _kCGImageSourceAllowableTypes where the
#       SDK declares it (ImageIO reads only the file's type, FIX138);
#       sdl3.o holds no SDL_RETURN_KEY_HIDES_IME, does not need
#       _Touch_TextInput and holds the name SDL_TV_REMOTE_AS_JOYSTICK, which
#       sdl3.c's one line naming it sets to "0" (the Siri Remote as keys);
#       glimp_sdl3.o holds neither SDL_ORIENTATIONS nor
#       SDL_IOS_HIDE_HOME_INDICATOR; menu.o holds the address book's tvOS
#       reason (greyed, TASK23 slice 2a). The iOS simulator's object of each
#       of those files is the positive control: it holds what tvOS lacks,
#       needs what tvOS does not, and lacks the remote's hint and the
#       address book's reason. A menu.c whose address book guard is widened
#       to "#ifdef TARGET_OS_TV" (true on every Apple target) is compiled
#       for the iOS simulator as the mutant that control must refuse.
#   (d2) the two sites that are iPhone, iPad and visionOS (the keyboard's
#       follow-up in IN_Update, its Enter hint in IN_Init): the guard line is
#       evaluated with cc -E under the macros of iOS and visionOS (true) and
#       of tvOS and macOS (false), so a guard widened to TARGET_OS_IPHONE,
#       or with an "|| TARGET_OS_TV", is refused whatever its spelling. It
#       runs everywhere, before the skips.
#   (d) also takes only a bare true test: a chain of && of names, !names
#       and defined()s with TARGET_OS_IOS alone among them (TASK23 slice 2).
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
# (the on-screen keyboard's follow-up in IN_Update and its Enter hint in
# IN_Init are iPhone, iPad and visionOS, so their guard is an ||: (d2)
# below evaluates them)
# (awk EREs; awk -v takes a backslash as an escape, so brackets, not \( )
sites='src/client/input/touch.c|^Touch_Supported[(]void[)]|return 1;
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
	# a bare true test only (TASK23 slice 2): the condition is a chain of
	# && whose terms are each a name, !name or defined(name), in any
	# parentheses, and one of them is TARGET_OS_IOS alone. Such a chain is 0
	# whenever TARGET_OS_IOS is; "TARGET_OS_IOS == 0", "!= 1", "- 1", an ||,
	# a ?: or a !( ) around the chain is refused
	if ! printf '%s\n' "$gx" | awk '{
		c = $0
		sub(/^[ \t]*#[ \t]*if[ \t]*/, "", c)
		gsub(/defined[ \t]*[(][ \t]*[_[:alnum:]]+[ \t]*[)]/, "D", c)
		gsub(/defined[ \t]+[_[:alnum:]]+/, "D", c)
		n = split(c, t, /&&/)
		bare = 0
		for (i = 1; i <= n; i++) {
			x = t[i]
			gsub(/^[ \t(]+/, "", x)
			gsub(/[ \t)]+$/, "", x)
			if (x !~ /^!?[_[:alpha:]][_[:alnum:]]*$/) exit 1
			if (x == "TARGET_OS_IOS") bare = 1
		}
		exit !bare
	}'; then
		echo "$1:${g%%:*}: the guard of \"$3\" is not a bare true test of TARGET_OS_IOS: ${g#*:}"
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
	while IFS=@ read -r ex why; do
		sed "${l%%:*}$ex" "$f" > "${tmp:?}/mut$n.c"
		if cmp -s "$f" "${tmp:?}/mut$n.c"; then
			bad "mutant: the guard of site $n is not changed by $ex"
		elif assert_guard "${tmp:?}/mut$n.c" "$s" "$a" > "${tmp:?}/o" 2>&1; then
			bad "mutant: site $n ($f, $a) with its guard changed by $ex passed"
		elif ! grep -q "$why" "${tmp:?}/o"; then
			bad "mutant: site $n with $ex was refused without saying it $why: $(cat "${tmp:?}/o")"
		fi
	done <<MUT
s/TARGET_OS_IOS/defined(TARGET_OS_IOS)/@does not name TARGET_OS_IOS
s/TARGET_OS_IOS/defined TARGET_OS_IOS/@does not name TARGET_OS_IOS
s/.*/#ifdef TARGET_OS_IOS/@is an #ifdef or #ifndef
s/.*/#  ifdef TARGET_OS_IOS \/* iPhone *\//@is an #ifdef or #ifndef
s/TARGET_OS_IOS/!TARGET_OS_IOS/@negates TARGET_OS_IOS
s/TARGET_OS_IOS/! (TARGET_OS_IOS)/@negates TARGET_OS_IOS
s/TARGET_OS_IOS/TARGET_OS_IOS == 0/@is not a bare true test
s/TARGET_OS_IOS/TARGET_OS_IOS != 1/@is not a bare true test
s/TARGET_OS_IOS/TARGET_OS_IOS - 1/@is not a bare true test
s/TARGET_OS_IOS/0 == (TARGET_OS_IOS)/@is not a bare true test
s/TARGET_OS_IOS/(TARGET_OS_IOS || 1)/@is not a bare true test
s/TARGET_OS_IOS/(TARGET_OS_IOS ? 0 : 1)/@is not a bare true test
s/^#[ \t]*if[ \t]*\(.*\)$/#if !(\1)/@is not a bare true test
MUT
	# the positive control of the bare-test rule: parentheses around the name still pass
	sed "${l%%:*}s/TARGET_OS_IOS/((TARGET_OS_IOS))/" "$f" > "${tmp:?}/mut$n.c"
	assert_guard "${tmp:?}/mut$n.c" "$s" "$a" > "${tmp:?}/o" 2>&1 ||
		bad "site $n with its guard in parentheses was refused: $(cat "${tmp:?}/o")"
done <<EOF
$sites
EOF
[ "$n" = 3 ] || bad "(d) $n sites, not 3"

# ---- (d2) the iPhone, iPad and visionOS sites: the guard's meaning --------
# The keyboard's follow-up (IN_Update) and its Enter hint (IN_Init) are for
# iOS and visionOS, not tvOS and not macOS. Their guard is an ||, so (d)'s
# text rules do not apply; the guard line is evaluated with cc -E under the
# macro sets of each platform (the macros are defined to 0 where false, as
# the SDK does) and must be 1 on iOS and visionOS, 0 on tvOS and macOS.

if ! command -v cc >/dev/null 2>&1; then
	bad "(d2) cc not found: the guards of the keyboard sites cannot be evaluated"
	exit 1
fi

# eval_guard <file> <start> <anchor> <macro set>: 1, 0 or "error"
eval_guard()
{
	g=$(guard_of "$1" "$2" "$3")
	case $g in '' | else:) echo error; return ;; esac
	gx=$(printf '%s\n' "${g#*:}" | sed -e 's,/\*.*\*/,,g' -e 's,//.*,,')
	printf '%s\n1\n#else\n0\n#endif\n' "$gx" > "${tmp:?}/g.c"
	# shellcheck disable=SC2086
	if ! r=$(cc -E -P -U__APPLE__ -D__APPLE__=1 -UTARGET_OS_IPHONE -UTARGET_OS_IOS -UTARGET_OS_TV -UTARGET_OS_VISION -UTARGET_OS_OSX -UTARGET_OS_SIMULATOR $4 "${tmp:?}/g.c" 2>/dev/null); then
		echo error
		return
	fi
	echo "$r" | tr -d ' \t\n'
}

sets_true='-DTARGET_OS_IPHONE=1 -DTARGET_OS_IOS=1 -DTARGET_OS_TV=0 -DTARGET_OS_VISION=0 -DTARGET_OS_SIMULATOR=0
-DTARGET_OS_IPHONE=1 -DTARGET_OS_IOS=1 -DTARGET_OS_TV=0 -DTARGET_OS_VISION=0 -DTARGET_OS_SIMULATOR=1
-DTARGET_OS_IPHONE=1 -DTARGET_OS_IOS=0 -DTARGET_OS_TV=0 -DTARGET_OS_VISION=1 -DTARGET_OS_SIMULATOR=0
-DTARGET_OS_IPHONE=1 -DTARGET_OS_IOS=0 -DTARGET_OS_TV=0 -DTARGET_OS_VISION=1 -DTARGET_OS_SIMULATOR=1'
sets_false='-DTARGET_OS_IPHONE=1 -DTARGET_OS_IOS=0 -DTARGET_OS_TV=1 -DTARGET_OS_VISION=0 -DTARGET_OS_SIMULATOR=0
-DTARGET_OS_IPHONE=1 -DTARGET_OS_IOS=0 -DTARGET_OS_TV=1 -DTARGET_OS_VISION=0 -DTARGET_OS_SIMULATOR=1
-DTARGET_OS_IPHONE=0 -DTARGET_OS_IOS=0 -DTARGET_OS_TV=0 -DTARGET_OS_VISION=0 -DTARGET_OS_OSX=1 -DTARGET_OS_SIMULATOR=0'

# assert_meaning <file> <start> <anchor>
assert_meaning()
{
	am_rc=0
	while read -r am_set; do
		[ "$(eval_guard "$1" "$2" "$3" "$am_set")" = 1 ] || { echo "$1: the guard of \"$3\" is not true under: $am_set"; am_rc=1; }
	done <<SETS
$sets_true
SETS
	while read -r am_set; do
		[ "$(eval_guard "$1" "$2" "$3" "$am_set")" = 0 ] || { echo "$1: the guard of \"$3\" is not false under: $am_set"; am_rc=1; }
	done <<SETS
$sets_false
SETS
	return $am_rc
}

m=0
while IFS='|' read -r f s a; do
	m=$((m + 1))
	if ! assert_meaning "$f" "$s" "$a" > "${tmp:?}/o" 2>&1; then
		cat "${tmp:?}/o"
		bad "(d2) site $m is not iOS and visionOS only"
		continue
	fi
	l=$(guard_of "$f" "$s" "$a")
	while IFS=@ read -r ex why; do
		sed "${l%%:*}$ex" "$f" > "${tmp:?}/mut$m.c"
		if cmp -s "$f" "${tmp:?}/mut$m.c"; then
			bad "mutant: the guard of (d2) site $m is not changed by $ex ($why)"
		elif assert_meaning "${tmp:?}/mut$m.c" "$s" "$a" > "${tmp:?}/o" 2>&1; then
			bad "mutant: (d2) site $m ($f, $a) with its guard changed by $ex ($why) passed"
		fi
	done <<MUT
s/.*/#if defined(__APPLE__) \&\& TARGET_OS_IPHONE/@TARGET_OS_IPHONE
s/))[ \t]*\$/) || TARGET_OS_TV)/@or TARGET_OS_TV
s/))[ \t]*\$/) || 1)/@or 1
s/(TARGET_OS_IOS/(!TARGET_OS_IOS/@negated iOS
s/TARGET_OS_IOS ||/TARGET_OS_IOS || TARGET_OS_IPHONE ||/@or TARGET_OS_IPHONE
s/TARGET_OS_IOS || //@no iOS term
s/ || (defined(TARGET_OS_VISION) \&\& TARGET_OS_VISION)//@no visionOS term
s/.*/#ifdef TARGET_OS_IOS/@ifdef
s/(defined(TARGET_OS_VISION) \&\& TARGET_OS_VISION)/defined(TARGET_OS_VISION)/@defined only
MUT
done <<EOF
src/client/input/sdl3.c|^IN_Update[(]void[)]|static int typing = -1
src/client/input/sdl3.c|^IN_Init[(]void[)]|SDL_HINT_RETURN_KEY_HIDES_IME
EOF
[ "$m" = 2 ] || bad "(d2) $m sites, not 2"
if [ "$fail" != 0 ]; then
	exit 1
fi

if [ "$(uname -s)" != Darwin ]; then
	[ "${CHECK_TVOS_REQUIRE:-0}" != 1 ] || { echo "check-tvos: CHECK_TVOS_REQUIRE=1 and this is not macOS"; exit 1; }
	echo "check-tvos: skipped: the tvOS build is macOS only (docs/decisions/0008)"
	exit 0
fi
sdk=
if command -v xcrun >/dev/null 2>&1; then
	sdk=$(xcrun --sdk appletvsimulator --show-sdk-path 2>/dev/null) || sdk=
fi
if [ -z "$sdk" ] || [ ! -f "$sdk/SDKSettings.plist" ]; then
	[ "${CHECK_TVOS_REQUIRE:-0}" != 1 ] || { echo "check-tvos: CHECK_TVOS_REQUIRE=1 and there is no tvOS simulator SDK (xcrun --sdk appletvsimulator --show-sdk-path)"; exit 1; }
	echo "check-tvos: skipped: no tvOS simulator SDK (xcrun --sdk appletvsimulator --show-sdk-path)"
	exit 0
fi
if ! xcrun --sdk iphonesimulator --show-sdk-path >/dev/null 2>&1; then
	[ "${CHECK_TVOS_REQUIRE:-0}" != 1 ] || { echo "check-tvos: CHECK_TVOS_REQUIRE=1 and there is no iOS simulator SDK for the object-list comparison"; exit 1; }
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

# bytes_have <object> <text>: 0 the bytes hold <text>, 1 they do not, 2 the
# object is missing or unreadable (said); grep's own status decides
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

# assert_lacks / assert_holds <object> <text>: the bytes do not hold, or hold, <text>
assert_lacks()
{
	arc=0
	bytes_have "$1" "$2" || arc=$?
	case $arc in
	0) echo "$1 holds \"$2\""; return 1 ;;
	1) return 0 ;;
	*) return 1 ;;
	esac
}
assert_holds()
{
	arc=0
	bytes_have "$1" "$2" || arc=$?
	case $arc in
	0) return 0 ;;
	1) echo "$1 does not hold \"$2\""; return 1 ;;
	*) return 1 ;;
	esac
}

# assert_no_need <object> <symbol>: nm -u does not list <symbol>
assert_no_need()
{
	[ -f "$1" ] || { echo "$1 is missing"; return 1; }
	nm -u "$1" > "${tmp:?}/nm" 2>/dev/null || { echo "$1: cannot read (nm -u failed)"; return 1; }
	nrc=0
	grep -x -F -q -e "$2" "${tmp:?}/nm" || nrc=$?
	case $nrc in
	0) echo "$1 needs $2 (nm -u)"; return 1 ;;
	1) return 0 ;;
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

# the pinned strings of (e) (TASK23 slice 2, as check-visionos pins them):
# an object holding one is refused by assert_lacks and passes assert_holds,
# one without it the other way round; an unreadable or missing one is
# refused by both
printf 'const char *a = "SDL_RETURN_KEY_HIDES_IME";\nconst char *b = "SDL_ORIENTATIONS";\nconst char *c = "SDL_IOS_HIDE_HOME_INDICATOR";\nconst char *d = "SDL_TV_REMOTE_AS_JOYSTICK";\n' > "${tmp:?}/s.c"
xcrun --sdk appletvsimulator clang -target arm64-apple-tvos17.0-simulator -c "${tmp:?}/s.c" -o "${tmp:?}/s.o"
for text in SDL_RETURN_KEY_HIDES_IME SDL_ORIENTATIONS SDL_IOS_HIDE_HOME_INDICATOR SDL_TV_REMOTE_AS_JOYSTICK; do
	if assert_lacks "${tmp:?}/s.o" "$text" > "${tmp:?}/o" 2>&1; then
		bad "mutant: an object holding \"$text\" passed"
	elif ! grep -q "holds \"$text\"" "${tmp:?}/o"; then
		bad "mutant: the object holding \"$text\" was refused without naming it"
	fi
	assert_lacks "${tmp:?}/m_tv.o" "$text" > "${tmp:?}/o" 2>&1 || bad "an object without \"$text\" was refused: $(cat "${tmp:?}/o")"
	assert_holds "${tmp:?}/s.o" "$text" > "${tmp:?}/o" 2>&1 || bad "an object holding \"$text\" was refused by assert_holds: $(cat "${tmp:?}/o")"
	if assert_holds "${tmp:?}/m_tv.o" "$text" > "${tmp:?}/o" 2>&1; then
		bad "mutant: an object without \"$text\" passed assert_holds"
	fi
done
# _Touch_TextInput: an object that needs it is refused, one that does not passes
printf 'extern int Touch_TextInput(int, int, int, int *);\nint tvos_t(void) { return Touch_TextInput(0, 0, 0, 0); }\n' > "${tmp:?}/ti.c"
xcrun --sdk appletvsimulator clang -target arm64-apple-tvos17.0-simulator -c "${tmp:?}/ti.c" -o "${tmp:?}/ti.o"
if assert_no_need "${tmp:?}/ti.o" _Touch_TextInput > "${tmp:?}/o" 2>&1; then
	bad "mutant: an object that needs _Touch_TextInput passed"
elif ! grep -q 'needs _Touch_TextInput' "${tmp:?}/o"; then
	bad "mutant: the object that needs _Touch_TextInput was refused without naming it"
fi
assert_no_need "${tmp:?}/m_tv.o" _Touch_TextInput > "${tmp:?}/o" 2>&1 || bad "an object without _Touch_TextInput was refused: $(cat "${tmp:?}/o")"
chmod 000 "${tmp:?}/unreadable.o"
for obj in absent unreadable; do
	if [ "$obj" = unreadable ] && [ -r "${tmp:?}/unreadable.o" ]; then
		continue
	fi
	for a in assert_lacks assert_holds assert_no_need; do
		if "$a" "${tmp:?}/$obj.o" SDL_ORIENTATIONS > "${tmp:?}/o" 2>&1; then
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
# The restriction is kCGImageSourceAllowableTypes where the SDK declares
# it (27.0 on); an older SDK (FIX138) builds without it, and the type
# check on CGImageSourceGetType is the restriction then, as it is the
# backstop always. Whether the SDK declares it is asked of the SDK, so a
# decoder that leaves it out on an SDK that has it still fails.
printf '#include <ImageIO/ImageIO.h>\nconst void *tvos_a(void) { return kCGImageSourceAllowableTypes; }\n' > "${tmp:?}/a.c"
imgdec_syms=_CGImageSourceGetType
if xcrun --sdk appletvsimulator clang -target arm64-apple-tvos17.0-simulator -Wno-unguarded-availability-new \
	-c "${tmp:?}/a.c" -o "${tmp:?}/a.o" > "${tmp:?}/a.out" 2>&1; then
	imgdec_syms="$imgdec_syms _kCGImageSourceAllowableTypes"
elif grep -q "undeclared identifier 'kCGImageSourceAllowableTypes'" "${tmp:?}/a.out"; then
	echo "check-tvos: note: the tvOS simulator SDK $(xcrun --sdk appletvsimulator --show-sdk-version 2>/dev/null) does not declare kCGImageSourceAllowableTypes: the type check (CGImageSourceGetType) is pinned alone"
else
	cat "${tmp:?}/a.out"
	bad "(e) the probe for kCGImageSourceAllowableTypes does not build for another reason"
fi
while read -r obj; do
	for sym in $imgdec_syms; do
		assert_need "$obj" "$sym" > "${tmp:?}/o" 2>&1 || { cat "${tmp:?}/o"; bad "(e) the image decoder does not restrict ImageIO to the file's type"; }
	done
done < "${tmp:?}/imgdecode"

# the positive controls: the iOS simulator's object of each pinned file,
# compiled with the command make -n gives for it, under the scratch root
ios_root="${tmp:?}/ios"
make -n -B ios_objects IOS_SDK=iphonesimulator IOS_ROOT="$ios_root" METAL_SHADERS=no > "${tmp:?}/ios.n" 2>/dev/null || bad "(e) make -n ios_objects for the iOS simulator failed"
# ios_object <source>: the path of that object is in $tmp/ios_one
ios_object()
{
	awk -v s="$1" '/ -c / { for (i = 1; i <= NF; i++) if ($i == s) { print; exit } }' "${tmp:?}/ios.n" > "${tmp:?}/cmd"
	awk '{ for (i = 1; i < NF; i++) if ($i == "-o") print $(i + 1) }' "${tmp:?}/cmd" > "${tmp:?}/ios_one"
	if [ "$(grep -c . "${tmp:?}/ios_one")" != 1 ]; then
		bad "(e) make -n names no compile of $1 for the iOS simulator"
		return 1
	fi
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

# pin <lacks|holds|no_need> <object name> <source> <text or symbol> <what>:
# every tvOS object called <name> (one at least), and the iOS simulator's
# object of <source> as the control: lacks (iOS: holds), holds (iOS:
# lacks), no_need (iOS: needs). Every path returns 0; bad records it
pin()
{
	awk -v n="/$2" 'substr($0, length($0) - length(n) + 1) == n' "${tmp:?}/objs" > "${tmp:?}/named"
	if [ ! -s "${tmp:?}/named" ]; then
		bad "(e) the build has no $2"
		return 0
	fi
	while read -r obj; do
		case $1 in
		lacks) assert_lacks "$obj" "$4" ;;
		holds) assert_holds "$obj" "$4" ;;
		no_need) assert_no_need "$obj" "$4" ;;
		esac > "${tmp:?}/o" 2>&1 || { cat "${tmp:?}/o"; bad "(e) $5"; }
	done < "${tmp:?}/named"
	ios_object "$3" || return 0
	case $1 in
	lacks) assert_holds "$(cat "${tmp:?}/ios_one")" "$4" ;;
	holds) assert_lacks "$(cat "${tmp:?}/ios_one")" "$4" ;;
	no_need) assert_need "$(cat "${tmp:?}/ios_one")" "$4" ;;
	esac > "${tmp:?}/o" 2>&1 || { cat "${tmp:?}/o"; bad "(e) the positive control failed: the pin on \"$4\" would pass whatever the tvOS build holds"; }
}
pin lacks sdl3.o src/client/input/sdl3.c SDL_RETURN_KEY_HIDES_IME "the iPhone's in_touch default and keyboard hint are compiled into the tvOS build"
pin no_need sdl3.o src/client/input/sdl3.c _Touch_TextInput "the on-screen keyboard's follow-up is compiled into the tvOS build"
pin holds sdl3.o src/client/input/sdl3.c SDL_TV_REMOTE_AS_JOYSTICK "the tvOS sdl3.o does not hold the name SDL_TV_REMOTE_AS_JOYSTICK: the Siri Remote's hint is not compiled in"
# the value the object cannot show: the one line of sdl3.c naming the hint sets it to "0" (keys)
n=$(grep -c 'SDL_HINT_TV_REMOTE_AS_JOYSTICK' src/client/input/sdl3.c) || :
if [ "$n" != 1 ] || ! grep -F -q 'SDL_SetHint(SDL_HINT_TV_REMOTE_AS_JOYSTICK, "0");' src/client/input/sdl3.c; then
	bad "(e) sdl3.c does not set SDL_HINT_TV_REMOTE_AS_JOYSTICK to \"0\" on the one line naming it ($n lines name it): the Siri Remote is not set to keys"
fi
pin lacks glimp_sdl3.o src/client/vid/glimp_sdl3.c SDL_ORIENTATIONS "the orientation hint is compiled into the tvOS build"
pin lacks glimp_sdl3.o src/client/vid/glimp_sdl3.c SDL_IOS_HIDE_HOME_INDICATOR "the home indicator hint is compiled into the tvOS build"
reason="typing an address needs a keyboard, which tvOS does not have"
pin holds menu.o src/client/menu/menu.c "$reason" "the address book is not greyed with its reason in the tvOS build"
# the mutant of that pin's control: the address book's guard widened to
# "#ifdef TARGET_OS_TV", true on every Apple target (the name is always
# defined, 0 or 1), greys it for macOS and iOS players; compiled for the iOS
# simulator with the command pin just ran ($tmp/cmd), it must be refused
guard='#if defined(__APPLE__) && TARGET_OS_TV'
n=$(grep -c -x -F -e "$guard" src/client/menu/menu.c) || :
if [ "$n" != 1 ]; then
	bad "(e) menu.c has $n lines '$guard', not the address book's one: the mutant has no site"
else
	mkdir -p "${tmp:?}/mut"
	awk -v g="$guard" '$0 == g { $0 = "#ifdef TARGET_OS_TV" } { print }' src/client/menu/menu.c > "${tmp:?}/mut/menu.c"
	awk -v s=src/client/menu/menu.c -v m="${tmp:?}/mut/menu.c" -v o="${tmp:?}/mut/menu.o" '{
		for (i = 2; i <= NF; i++) {
			if ($i == s) $i = m
			else if ($(i - 1) == "-o") $i = o
			else if ($(i - 1) == "-MF") $i = o ".d"
		}
		print $0 " -Isrc/client/menu"
	}' "${tmp:?}/cmd" > "${tmp:?}/mut/cmd"
	if ! grep -F -q -e "${tmp:?}/mut/menu.c" "${tmp:?}/mut/cmd"; then
		bad "(e) the mutant's compile does not name the mutated menu.c"
	elif ! sh "${tmp:?}/mut/cmd" > "${tmp:?}/mut/cc.log" 2>&1; then
		tail -n 5 "${tmp:?}/mut/cc.log"
		bad "(e) the widened-guard mutant of menu.c does not compile for the iOS simulator"
	elif assert_lacks "${tmp:?}/mut/menu.o" "$reason" > "${tmp:?}/o" 2>&1; then
		bad "mutant: the address book's guard widened to #ifdef TARGET_OS_TV passed (e)'s control"
	fi
fi

# TASK23 slice 3: the missing-data text. An Apple TV has no Files app: the tvOS
# system.o holds its own words and not the iPhone's, and the iOS simulator's
# object (the control) holds the iPhone's and not the tvOS words
pin lacks system.o src/backends/unix/system.c "On My iPhone (or iPad)" "the iPhone's missing-data text is compiled into the tvOS build"
pin holds system.o src/backends/unix/system.c "an Apple TV has no Files app" "the tvOS missing-data text is not compiled into the tvOS build"

object_list /r IOS_SDK=appletvsimulator > "${tmp:?}/list_tv"
object_list /r IOS_SDK=iphonesimulator > "${tmp:?}/list_ios"
assert_same_list "${tmp:?}/list_ios" "${tmp:?}/list_tv" > "${tmp:?}/o" 2>&1 || { cat "${tmp:?}/o"; bad "(b) the tvOS object list is not the iOS simulator's"; }
[ -s "${tmp:?}/list_tv" ] || bad "(b) make -n listed no object"

if [ "$fail" != 0 ]; then
	exit 1
fi
echo "check-tvos: ok ($(grep -c . "${tmp:?}/objs") objects for the tvOS simulator, platform 8, the iOS simulator's list)"
