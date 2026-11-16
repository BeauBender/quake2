#!/bin/bash
# scripts/ios-device.sh -- the device app: signed with the owner's identity,
# installed and launched on the owner's iPhone or iPad (TASK152).
#
#   scripts/ios-device.sh sign --identity NAME --profile FILE [--app DIR]
#   scripts/ios-device.sh install --device ID [--app DIR]
#   scripts/ios-device.sh launch --device ID [--app DIR] [ARG]...
#   scripts/ios-device.sh --selftest
#
# The app is the bundle "make ios IOS_SDK=iphoneos" leaves unsigned,
# build/ios-iphoneos/release/Quake2.app (--app DIR or IOS_APP for another).
#
# sign: NAME is a signing identity the owner's login keychain holds (an
# "Apple Development" certificate and its key), FILE a development
# provisioning profile (.mobileprovision) for the app's bundle identifier
# that lists the device. Both come from the command line (--identity,
# --profile) or the environment (IOS_SIGN_IDENTITY, IOS_PROFILE), are
# never stored in the repository, and are not printed. In order:
#   - the app is a device bundle: its Info.plist names iPhoneOS in
#     CFBundleSupportedPlatforms and a CFBundleIdentifier, which is
#     IOS_BUNDLE_ID when that is set (the Makefile wrote it from there);
#   - the profile is decoded (security cms -D) into a scratch directory
#     only this user can read, and read with plutil: its
#     Entitlements.application-identifier must be its TeamIdentifier, a
#     dot and the app's bundle identifier, exactly. A profile that cannot
#     be decoded, has no application identifier or no team, or names
#     another bundle identifier (a wildcard one too) is refused, naming
#     the profile's identifier without its team and the app's;
#   - the profile's Entitlements go to a scratch file, the profile is
#     copied into the bundle as embedded.mobileprovision, and "codesign
#     --force --sign NAME --entitlements <scratch> --timestamp=none" signs
#     the bundle; "codesign --verify --strict" then checks it.
# Nothing of the decoded profile is printed (it holds the team's
# certificates), and the scratch directory is removed on exit.
#
# install: "xcrun devicectl device install app --device ID <app>"; the app
# must be signed (it holds embedded.mobileprovision and _CodeSignature/).
# launch: "xcrun devicectl device process launch --device ID
# --terminate-existing <bundle id> -- [ARG]..."; each ARG is an engine
# argument ("+map" "base1"), the bundle identifier is the app's. ID is the
# device's identifier as "xcrun devicectl list devices" shows it (its
# UDID or CoreDevice identifier), or IOS_DEVICE: letters, digits and
# hyphens, not starting with a hyphen.
#
# Limits, in seconds (digits only, or the script refuses):
# IOS_DEVICE_SIGN_SECONDS (300: codesign may wait on the keychain's
# prompt), IOS_DEVICE_INSTALL_SECONDS (300), IOS_DEVICE_LAUNCH_SECONDS
# (120); security cms and codesign --verify have 60. A command past its
# limit is killed (by its PID, which this script started) and fails.
#
# A usage error exits 2 with one usage line; a refusal or a failed step
# exits 1.
#
# --selftest: runs sign, install and launch against a fake security,
# codesign and xcrun (no keychain, no device, no real codesign; seconds),
# on scratch bundles and scratch profiles it makes itself. tests/check-ios.sh
# runs it.
set -u
set -o pipefail
export LC_ALL=C

repo=$(cd "$(dirname "$0")/.." && pwd -P) || exit 1
self=$(cd "$(dirname "$0")" && pwd -P)/$(basename "$0") || exit 1
die() { echo "ios-device: $*" >&2; exit 1; }
usage()
{
	case ${1:-} in
	sign) echo "usage: scripts/ios-device.sh sign --identity NAME --profile FILE [--app DIR]" >&2 ;;
	install) echo "usage: scripts/ios-device.sh install --device ID [--app DIR]" >&2 ;;
	launch) echo "usage: scripts/ios-device.sh launch --device ID [--app DIR] [ARG]..." >&2 ;;
	*) echo "usage: scripts/ios-device.sh sign|install|launch ... | --selftest" >&2 ;;
	esac
	exit 2
}

# bounded <seconds> <log> <command>...: runs it, its output and errors to
# <log>, and kills it (our PID) past the bound
bounded()
{
	local limit=$1 log=$2 pid t=0 w status=0
	shift 2
	"$@" > "$log" 2>&1 &
	pid=$!
	while kill -0 "$pid" 2>/dev/null; do
		if [ "$t" -ge $((limit * 5)) ]; then
			kill "$pid" 2>/dev/null
			w=0; while kill -0 "$pid" 2>/dev/null && [ "$w" -lt 5 ]; do sleep 1; w=$((w + 1)); done
			kill -0 "$pid" 2>/dev/null && kill -9 "$pid" 2>/dev/null
			wait "$pid" 2>/dev/null
			echo "ios-device: timed out after $limit s: $1 $2" >&2
			return 124
		fi
		sleep 0.2
		t=$((t + 1))
	done
	wait "$pid" || status=$?
	return $status
}

digits() # <name> <value>
{
	case $2 in
	''|*[!0-9]*) die "$1 must be a number of seconds, digits only, not \"$2\"" ;;
	esac
}

# ---- selftest: sign, install and launch against fakes --------------------

selftest()
{
	local nfail=0 ncase=0
	sroot=$(mktemp -d "${TMPDIR:-/tmp}/ios-device-selftest.XXXXXX") || die "no scratch directory"
	trap 'rm -rf "${sroot:?}"' EXIT
	mkdir -p "${sroot:?}/bin" || die "cannot create $sroot/bin"
	# every fake logs its arguments, "|" after each, to FAKE_STATE/calls
	cat > "${sroot:?}/bin/security" <<'EOF'
#!/bin/bash
# the fake security of "scripts/ios-device.sh --selftest": cms -D only; a
# scratch profile is the line FAKE-CMS and then the plist it carries
{ printf 'security|'; prev=; for a in "$@"; do if [ "$prev" = -o ]; then printf 'OUT|'; else printf '%s|' "$a"; fi; prev=$a; done; echo; } >> "$FAKE_STATE/calls"
[ "${1:-} ${2:-}" = "cms -D" ] || { echo "fake security: cms -D only" >&2; exit 1; }
shift 2
in= out=
while [ $# -gt 0 ]; do
	case $1 in
	-i) in=$2; shift 2 ;;
	-o) out=$2; shift 2 ;;
	*) echo "fake security: unknown option $1" >&2; exit 1 ;;
	esac
done
[ "$(head -n 1 "$in" 2>/dev/null)" = FAKE-CMS ] || { echo "security: failed to decode message" >&2; exit 1; }
tail -n +2 "$in" > "$out"
EOF
	cat > "${sroot:?}/bin/codesign" <<'EOF'
#!/bin/bash
# the fake codesign of "scripts/ios-device.sh --selftest": never a real
# signature; keeps the entitlements file it was given
{ printf 'codesign|'; prev=; for a in "$@"; do if [ "$prev" = --entitlements ]; then printf 'ENT|'; cp "$a" "$FAKE_STATE/entitlements.given"; else printf '%s|' "$a"; fi; prev=$a; done; echo; } >> "$FAKE_STATE/calls"
if [ "${1:-}" = --verify ]; then
	[ "$FAKE_SCENARIO" = verifyfail ] && { echo "fake codesign: a sealed resource is missing or invalid" >&2; exit 1; }
	exit 0
fi
case $FAKE_SCENARIO in
signfail) echo "fake codesign: no identity found" >&2; exit 1 ;;
signhang) exec sleep 30 ;;
esac
app=${*: -1}
mkdir -p "$app/_CodeSignature" && : > "$app/_CodeSignature/CodeResources"
EOF
	cat > "${sroot:?}/bin/xcrun" <<'EOF'
#!/bin/bash
# the fake xcrun of "scripts/ios-device.sh --selftest": devicectl only
{ printf 'xcrun|'; printf '%s|' "$@"; echo; } >> "$FAKE_STATE/calls"
[ "${1:-}" = devicectl ] || { echo "fake xcrun: devicectl only" >&2; exit 1; }
case $FAKE_SCENARIO in
devhang) exec sleep 30 ;;
devfail) echo "ERROR: The device was not found." >&2; exit 1 ;;
esac
exit 0
EOF
	chmod +x "${sroot:?}/bin/security" "${sroot:?}/bin/codesign" "${sroot:?}/bin/xcrun" || die "cannot make the fakes executable"

	# the bundle make leaves (a device's Info.plist), and the profiles
	local id=org.yquake2.quake2 team=SELFTEAM who="Selftest Identity Name" cert=U0VMRlRFU1QtQ0VSVElGSUNBVEU=
	mkapp() # <dir> <platform> <bundle id>
	{
		mkdir -p "${1:?}" || die "cannot create $1"
		printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
			'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
			'<plist version="1.0"><dict>' '<key>CFBundleExecutable</key><string>quake2</string>' \
			"<key>CFBundleIdentifier</key><string>$3</string>" \
			"<key>CFBundleSupportedPlatforms</key><array><string>$2</string></array>" '</dict></plist>' > "$1/Info.plist"
		: > "$1/quake2"
		chmod +x "$1/quake2"
	}
	mkprofile() # <file> <application-identifier or -> <team or ->
	{
		{
			echo FAKE-CMS
			printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
				'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
				'<plist version="1.0"><dict>' "<key>DeveloperCertificates</key><array><data>$cert</data></array>"
			[ "$3" = - ] || printf '%s\n' "<key>TeamIdentifier</key><array><string>$3</string></array>"
			printf '%s\n' '<key>Entitlements</key><dict>' '<key>get-task-allow</key><true/>'
			[ "$2" = - ] || printf '%s\n' "<key>application-identifier</key><string>$2</string>"
			printf '%s\n' '</dict></dict></plist>'
		} > "${1:?}"
	}
	p=$sroot/profiles
	mkdir -p "${p:?}"
	mkprofile "$p/good" "$team.$id" "$team"
	mkprofile "$p/other" "$team.org.example.other" "$team"
	mkprofile "$p/wildcard" "$team.*" "$team"
	mkprofile "$p/otherteam" "OTHERTEAM.$id" "$team"
	mkprofile "$p/noappid" - "$team"
	mkprofile "$p/noteam" "$team.$id" -
	printf '%s\n' "not a CMS message" > "$p/garbage"
	printf '%s\n' FAKE-CMS "not a property list" > "$p/notplist"
	mkapp "$sroot/device.app" iPhoneOS "$id"
	mkapp "$sroot/sim.app" iPhoneSimulator "$id"
	cp -R "$sroot/device.app" "$sroot/signed.app" || die "cannot copy the bundle"
	cp "$p/good" "$sroot/signed.app/embedded.mobileprovision" && mkdir -p "$sroot/signed.app/_CodeSignature" \
		&& : > "$sroot/signed.app/_CodeSignature/CodeResources" || die "cannot make the signed bundle"

	# dt_case <name> <status> <text the output holds> <scenario> <app> [VAR=value]... -- <arguments>...
	# runs the script on a copy of <app> (@APP@ in an argument is its
	# path, @P@ the profiles), with the fakes first on PATH, and checks:
	# the status; the text; a status 2 prints one line, a usage line; no
	# codesign, devicectl or embedded profile where the case refuses
	# before them; nothing of the identity, the team or the certificate
	# printed; the scratch directory gone
	dt_case()
	{
		local name=$1 want=$2 text=$3 sc=$4 app=$5 d rc=0 why= a t0
		local -a envs=() args=()
		shift 5
		while [ $# -gt 0 ] && [ "$1" != -- ]; do envs+=("${1//@P@/$p}"); shift; done
		[ $# -gt 0 ] && shift
		ncase=$((ncase + 1))
		d=$sroot/case/$name
		mkdir -p "${d:?}/state" "${d:?}/tmp" || die "cannot create $d"
		: > "${d:?}/state/calls"
		if [ "$app" != - ]; then
			cp -R "$sroot/$app" "${d:?}/Quake2.app" || die "cannot copy $app"
		fi
		for a in "$@"; do a=${a//@APP@/$d/Quake2.app}; args+=("${a//@P@/$p}"); done
		t0=$SECONDS
		bounded 60 "${d:?}/out" env -u IOS_SIGN_IDENTITY -u IOS_PROFILE -u IOS_DEVICE -u IOS_BUNDLE_ID \
			PATH="$sroot/bin:$PATH" FAKE_STATE="$d/state" FAKE_SCENARIO="$sc" TMPDIR="$d/tmp" \
			IOS_APP="$d/Quake2.app" ${envs[@]+"${envs[@]}"} bash "$self" ${args[@]+"${args[@]}"} || rc=$?
		t0=$((SECONDS - t0))
		if [ "$rc" = 124 ]; then
			why="it outlived 60 s"
		elif [ "$rc" != "$want" ]; then
			why="it exited $rc, not $want"
		elif ! grep -F -e "$text" "${d:?}/out" >/dev/null; then
			why="its output does not hold \"$text\""
		elif [ "$want" = 2 ] && { [ "$(grep -c . "${d:?}/out")" != 1 ] || ! grep -q '^usage: ' "${d:?}/out"; }; then
			why="a usage error that is not one usage line"
		elif [ "$t0" -gt 20 ]; then
			why="it took $t0 s"
		elif grep -F -e "$who" -e "$team" -e "$cert" -e SELFTEST-CERTIFICATE "${d:?}/out" >/dev/null; then
			why="it printed the identity, the team or the certificate"
		elif [ -n "$(ls -A "${d:?}/tmp")" ]; then
			why="it left its scratch files: $(ls -A "${d:?}/tmp" | head -n 3 | tr '\n' ' ')"
		elif [ "$want" != 0 ] && [ -z "$sc" ] && grep -E -q '^(codesign|xcrun)\|' "${d:?}/state/calls"; then
			why="it refused, but ran $(grep -E -o '^(codesign|xcrun)\|[^|]*' "${d:?}/state/calls" | head -n 1)"
		elif [ "$want" != 0 ] && [ -z "$sc" ] && [ "$app" = device.app ] && [ -e "${d:?}/Quake2.app/embedded.mobileprovision" ]; then
			why="it refused, but embedded the profile"
		fi
		if [ -z "$why" ] && [ "$rc" = 0 ] && [ -n "${dt_expect:-}" ]; then
			if ! printf '%s\n' "$dt_expect" | sed -e "s|@APP@|$d/Quake2.app|g" -e "s|@P@|$p|g" | cmp -s - "${d:?}/state/calls"; then
				why="its calls are not the expected ones: $(tr '\n' ' ' < "${d:?}/state/calls" | cut -c 1-300)"
			fi
		fi
		if [ -z "$why" ] && [ -n "${dt_after:-}" ]; then
			why=$(cd "${d:?}" && eval "$dt_after")
		fi
		if [ -n "$why" ]; then
			echo "ios-device: selftest: FAIL $name: $why"
			sed 's/^/    /' "${d:?}/out" | head -n 8
			nfail=$((nfail + 1))
		else
			echo "ios-device: selftest: ok $name"
		fi
		dt_expect=
		dt_after=
	}

	# sign: a missing or empty identity or profile is a usage error
	dt_case sign-no-identity 2 "usage: scripts/ios-device.sh sign" "" device.app -- sign --profile @P@/good
	dt_case sign-empty-identity 2 "usage: scripts/ios-device.sh sign" "" device.app -- sign --identity "" --profile @P@/good
	dt_case sign-empty-identity-env 2 "usage: scripts/ios-device.sh sign" "" device.app IOS_SIGN_IDENTITY= IOS_PROFILE=@P@/good -- sign
	dt_case sign-no-profile 2 "usage: scripts/ios-device.sh sign" "" device.app -- sign --identity "$who"
	dt_case sign-empty-profile 2 "usage: scripts/ios-device.sh sign" "" device.app -- sign --identity "$who" --profile ""
	dt_case sign-empty-profile-env 2 "usage: scripts/ios-device.sh sign" "" device.app IOS_SIGN_IDENTITY="$who" IOS_PROFILE= -- sign
	dt_case sign-stray-argument 2 "usage: scripts/ios-device.sh sign" "" device.app -- sign --identity "$who" --profile @P@/good extra
	# sign: the profile is refused unless it is decoded and names this app
	dt_case sign-profile-missing 1 "is not a file" "" device.app -- sign --identity "$who" --profile @P@/missing
	dt_case sign-profile-undecodable 1 "cannot be decoded" "" device.app -- sign --identity "$who" --profile @P@/garbage
	dt_case sign-profile-not-plist 1 "cannot be decoded" "" device.app -- sign --identity "$who" --profile @P@/notplist
	dt_case sign-profile-no-appid 1 "has no application identifier" "" device.app -- sign --identity "$who" --profile @P@/noappid
	dt_case sign-profile-no-team 1 "has no team identifier" "" device.app -- sign --identity "$who" --profile @P@/noteam
	dt_case sign-profile-other-team 1 "does not start with its team identifier" "" device.app -- sign --identity "$who" --profile @P@/otherteam
	dt_case sign-profile-other-bundle 1 "is for org.example.other, not the app's org.yquake2.quake2" "" device.app -- sign --identity "$who" --profile @P@/other
	dt_case sign-profile-wildcard 1 "is for *, not the app's org.yquake2.quake2" "" device.app -- sign --identity "$who" --profile @P@/wildcard
	# sign: the app must be the device's bundle, of IOS_BUNDLE_ID when set
	dt_case sign-no-app 1 "is not an app bundle" "" - -- sign --identity "$who" --profile @P@/good
	dt_case sign-simulator-app 1 "is built for iPhoneSimulator, not iPhoneOS" "" sim.app -- sign --identity "$who" --profile @P@/good
	dt_case sign-bundle-id-not-ios-bundle-id 1 "is org.yquake2.quake2, not IOS_BUNDLE_ID org.example.other" "" device.app IOS_BUNDLE_ID=org.example.other -- sign --identity "$who" --profile @P@/good
	dt_case sign-limit-not-digits 1 "IOS_DEVICE_SIGN_SECONDS must be" "" device.app IOS_DEVICE_SIGN_SECONDS=5m -- sign --identity "$who" --profile @P@/good
	# sign: the good profile, from the command line and from the environment
	sign_calls="security|cms|-D|-i|@P@/good|-o|OUT|
codesign|--force|--sign|$who|--entitlements|ENT|--timestamp=none|@APP@|
codesign|--verify|--strict|@APP@|"
	sign_after='cmp -s "'"$p"'/good" Quake2.app/embedded.mobileprovision || echo "embedded.mobileprovision is not the profile"
		[ "$(plutil -extract application-identifier raw -o - state/entitlements.given 2>/dev/null)" = '"$team.$id"' ] || echo "codesign was not given the profile'"'"'s entitlements"
		[ "$(plutil -extract get-task-allow raw -o - state/entitlements.given 2>/dev/null)" = true ] || echo "codesign was not given all of the profile'"'"'s entitlements"'
	dt_expect=$sign_calls dt_after=$sign_after
	dt_case sign-ok 0 "ios-device: signed" "" device.app -- sign --identity "$who" --profile @P@/good
	dt_expect=$sign_calls dt_after=$sign_after
	dt_case sign-ok-env 0 "ios-device: signed" "" device.app IOS_SIGN_IDENTITY="$who" IOS_PROFILE=@P@/good -- sign
	dt_expect=$sign_calls dt_after=$sign_after
	dt_case sign-ok-bundle-id-is-ios-bundle-id 0 "ios-device: signed" "" device.app IOS_BUNDLE_ID=$id -- sign --identity "$who" --profile @P@/good
	# sign: codesign failing, hanging, or the signature not verifying
	dt_case sign-codesign-fails 1 "codesign failed" signfail device.app -- sign --identity "$who" --profile @P@/good
	dt_case sign-codesign-hangs 1 "timed out after 2 s" signhang device.app IOS_DEVICE_SIGN_SECONDS=2 -- sign --identity "$who" --profile @P@/good
	dt_case sign-verify-fails 1 "codesign --verify failed" verifyfail device.app -- sign --identity "$who" --profile @P@/good
	# install and launch
	dt_case install-no-device 2 "usage: scripts/ios-device.sh install" "" signed.app -- install
	dt_case install-empty-device 2 "usage: scripts/ios-device.sh install" "" signed.app -- install --device ""
	dt_case install-device-option 2 "usage: scripts/ios-device.sh install" "" signed.app -- install --device -x
	dt_case install-device-space 2 "usage: scripts/ios-device.sh install" "" signed.app -- install --device "a b"
	dt_case install-unsigned 1 "is not signed" "" device.app -- install --device 00000000-SELFTEST
	dt_case install-limit-not-digits 1 "IOS_DEVICE_INSTALL_SECONDS must be" "" signed.app IOS_DEVICE_INSTALL_SECONDS=-1 -- install --device 00000000-SELFTEST
	dt_expect="xcrun|devicectl|device|install|app|--device|00000000-SELFTEST|@APP@|"
	dt_case install-ok 0 "ios-device: installed" "" signed.app -- install --device 00000000-SELFTEST
	dt_expect="xcrun|devicectl|device|install|app|--device|00000000-SELFTEST|@APP@|"
	dt_case install-ok-env 0 "ios-device: installed" "" signed.app IOS_DEVICE=00000000-SELFTEST -- install
	dt_case install-fails 1 "devicectl device install app failed" devfail signed.app -- install --device 00000000-SELFTEST
	dt_case install-hangs 1 "timed out after 2 s" devhang signed.app IOS_DEVICE_INSTALL_SECONDS=2 -- install --device 00000000-SELFTEST
	dt_case launch-no-device 2 "usage: scripts/ios-device.sh launch" "" signed.app -- launch +map base1
	dt_case launch-no-app 1 "is not an app bundle" "" - -- launch --device 00000000-SELFTEST
	dt_expect="xcrun|devicectl|device|process|launch|--device|00000000-SELFTEST|--terminate-existing|$id|--|+set|vid_renderer|metal|+map|base1|"
	dt_case launch-ok 0 "ios-device: launched" "" signed.app -- launch --device 00000000-SELFTEST +set vid_renderer metal +map base1
	dt_case launch-fails 1 "devicectl device process launch failed" devfail signed.app -- launch --device 00000000-SELFTEST
	dt_case launch-hangs 1 "timed out after 2 s" devhang signed.app IOS_DEVICE_LAUNCH_SECONDS=2 -- launch --device 00000000-SELFTEST
	dt_case no-command 2 "usage: scripts/ios-device.sh sign|install|launch" "" device.app --
	if [ "$nfail" -ne 0 ]; then
		echo "ios-device: selftest: $nfail of $ncase cases failed"
		exit 1
	fi
	echo "ios-device: selftest: ok ($ncase cases)"
	exit 0
}

if [ $# -eq 1 ] && [ "$1" = --selftest ]; then
	selftest
fi

# ---- the commands --------------------------------------------------------

mode=${1:-}
[ $# -ge 1 ] && shift
case $mode in
sign|install|launch) ;;
*) usage ;;
esac
identity=${IOS_SIGN_IDENTITY-}
profile=${IOS_PROFILE-}
device=${IOS_DEVICE-}
app=${IOS_APP-$repo/build/ios-iphoneos/release/Quake2.app}
engine_args=()
while [ $# -gt 0 ]; do
	case $mode:$1 in
	sign:--identity) [ $# -ge 2 ] || usage sign; identity=$2; shift 2 ;;
	sign:--profile) [ $# -ge 2 ] || usage sign; profile=$2; shift 2 ;;
	install:--device|launch:--device) [ $# -ge 2 ] || usage "$mode"; device=$2; shift 2 ;;
	*:--app) [ $# -ge 2 ] || usage "$mode"; app=$2; shift 2 ;;
	launch:--) shift; engine_args=("$@"); break ;;
	launch:*) engine_args=("$@"); break ;;
	*) usage "$mode" ;;
	esac
done
case $mode in
sign) [ -n "$identity" ] && [ -n "$profile" ] || usage sign ;;
*)
	case $device in
	''|-*|*[!A-Za-z0-9-]*) usage "$mode" ;;
	esac ;;
esac

sign_s=${IOS_DEVICE_SIGN_SECONDS-300}
install_s=${IOS_DEVICE_INSTALL_SECONDS-300}
launch_s=${IOS_DEVICE_LAUNCH_SECONDS-120}
digits IOS_DEVICE_SIGN_SECONDS "$sign_s"
digits IOS_DEVICE_INSTALL_SECONDS "$install_s"
digits IOS_DEVICE_LAUNCH_SECONDS "$launch_s"

for tool in plutil xcrun; do
	command -v "$tool" >/dev/null 2>&1 || die "$tool not found: a missing tool is a failure"
done
if [ "$mode" = sign ]; then
	for tool in security codesign; do
		command -v "$tool" >/dev/null 2>&1 || die "$tool not found: a missing tool is a failure"
	done
fi

# the app: a bundle with its Info.plist and executable
[ -d "$app" ] && [ -f "$app/Info.plist" ] && [ -f "$app/quake2" ] || die "$app is not an app bundle (make ios IOS_SDK=iphoneos)"
bid=$(plutil -extract CFBundleIdentifier raw -o - "$app/Info.plist" 2>/dev/null) || bid=
case $bid in
''|*[!A-Za-z0-9.-]*) die "$app/Info.plist has no CFBundleIdentifier of letters, digits, dots and hyphens" ;;
esac

tmp=$(mktemp -d "${TMPDIR:-/tmp}/ios-device.XXXXXX") || die "no scratch directory"
chmod 700 "${tmp:?}" || die "cannot make $tmp private"
trap 'rm -rf "${tmp:?}"' EXIT
trap 'exit 1' INT TERM

if [ "$mode" = sign ]; then
	plat=$(plutil -extract CFBundleSupportedPlatforms.0 raw -o - "$app/Info.plist" 2>/dev/null) || plat="(none)"
	[ "$plat" = iPhoneOS ] || die "$app is built for $plat, not iPhoneOS: make ios IOS_SDK=iphoneos"
	if [ -n "${IOS_BUNDLE_ID-}" ] && [ "$bid" != "$IOS_BUNDLE_ID" ]; then
		die "the app's CFBundleIdentifier is $bid, not IOS_BUNDLE_ID $IOS_BUNDLE_ID"
	fi
	[ -f "$profile" ] || die "the profile $profile is not a file"
	dec=$tmp/profile.plist
	if ! bounded 60 "${tmp:?}/cms.out" security cms -D -i "$profile" -o "$dec" || ! plutil -lint -s "$dec" > /dev/null 2>&1; then
		die "the profile $profile cannot be decoded (security cms -D, then plutil)"
	fi
	appid=$(plutil -extract Entitlements.application-identifier raw -o - "$dec" 2>/dev/null) || appid=
	case $appid in
	''|*[!A-Za-z0-9.*-]*) die "the profile $profile has no application identifier (Entitlements.application-identifier)" ;;
	esac
	team=$(plutil -extract TeamIdentifier.0 raw -o - "$dec" 2>/dev/null) || team=
	case $team in
	''|*[!A-Za-z0-9]*) die "the profile $profile has no team identifier (TeamIdentifier)" ;;
	esac
	case $appid in
	"$team".*) ;;
	*) die "the profile's application identifier does not start with its team identifier" ;;
	esac
	if [ "${appid#"$team".}" != "$bid" ]; then
		die "the profile $profile is for ${appid#"$team".}, not the app's $bid"
	fi
	ent=$tmp/entitlements.plist
	if ! plutil -extract Entitlements xml1 -o "$ent" "$dec" > /dev/null 2>&1 || ! plutil -lint -s "$ent" > /dev/null 2>&1; then
		die "the profile's entitlements cannot be read"
	fi
	cp "$profile" "$app/embedded.mobileprovision" || die "cannot copy the profile into $app"
	if ! bounded "$sign_s" "${tmp:?}/codesign.out" codesign --force --sign "$identity" --entitlements "$ent" --timestamp=none "$app"; then
		head -n 5 "${tmp:?}/codesign.out" >&2
		die "codesign failed on $app"
	fi
	if ! bounded 60 "${tmp:?}/verify.out" codesign --verify --strict "$app"; then
		head -n 5 "${tmp:?}/verify.out" >&2
		die "codesign --verify failed on $app"
	fi
	echo "ios-device: signed $app for $bid (embedded.mobileprovision, the profile's entitlements)"
	exit 0
fi

if [ "$mode" = install ]; then
	[ -f "$app/embedded.mobileprovision" ] && [ -f "$app/_CodeSignature/CodeResources" ] \
		|| die "$app is not signed: scripts/ios-device.sh sign first"
	if ! bounded "$install_s" "${tmp:?}/install.out" xcrun devicectl device install app --device "$device" "$app"; then
		head -n 5 "${tmp:?}/install.out" >&2
		die "devicectl device install app failed"
	fi
	echo "ios-device: installed $app ($bid)"
	exit 0
fi

if ! bounded "$launch_s" "${tmp:?}/launch.out" xcrun devicectl device process launch --device "$device" --terminate-existing "$bid" -- ${engine_args[@]+"${engine_args[@]}"}; then
	head -n 5 "${tmp:?}/launch.out" >&2
	die "devicectl device process launch failed"
fi
echo "ios-device: launched $bid"
exit 0
