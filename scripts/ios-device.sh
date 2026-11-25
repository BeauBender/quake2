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
# never stored in the repository, and are not printed. NAME must not start
# with a hyphen ("-" is codesign's ad hoc identity). In order:
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
#     the bundle; "codesign --verify --strict" then checks it. When either
#     fails, or is interrupted, embedded.mobileprovision and _CodeSignature/
#     are removed again, so install refuses the bundle.
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
# limit is killed (by its PID, which this script started) and fails. An
# INT (Ctrl-C), QUIT (Ctrl-\) or TERM to the script ends the running
# command the same way (TERM, then SIGKILL after 2 s; the command ignores INT
# and QUIT, as every command a script starts in the background does) and
# exits 1; a HUP does too through the exit cleanup, and the script then
# ends by the HUP (status 129).
#
# A usage error exits 2 with one usage line; a refusal or a failed step
# exits 1. Messages name a path relative to the repository, or only its
# last component when it is outside (never an absolute path: a refusal's
# text may be posted to a public issue). A failed tool's first five lines
# are printed with the scratch directory, the app's path, the home
# directory, the identity, the team and the device replaced, in that
# order (fixed strings, not patterns), by "<scratch>", the app as messages
# name it, "<home>", "<identity>", "<team>" and "<device>"; a line that
# names a signer (codesign's name for the certificate it resolved, which
# an identity given as a SHA-1 hash or a part of the name does not match)
# is printed as one fixed line instead.
#
# --selftest: runs sign, install and launch against a fake security,
# codesign and xcrun (no keychain, no device, no real codesign; seconds),
# on scratch bundles and scratch profiles it makes itself, and sends TERM,
# QUIT or HUP to the script, and INT to its process group, during a
# hanging step. The script is started with INT and QUIT at their default
# (through perl), so these cases run where the selftest itself was started
# with them ignored (a background job of a shell without job control).
# tests/check-ios.sh runs it.
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

# The one bounded-run primitive (FIX122): bench_bounded and
# bench_stop_group run a command in its own process group and stop that
# whole group, by the identity recorded when it started. The command bounded
# runs is BENCH_BOUNDED_PID while it runs: the timeout and the EXIT, INT,
# TERM, QUIT and HUP handling end it, so the script never leaves it running.
[ -r "$repo/scripts/bench/core/common.sh" ] || die "no bounded-run primitive at scripts/bench/core/common.sh"
. "$repo/scripts/bench/core/common.sh"
exec 8>&-

# stop_child <grace>: ends the command bounded runs, with its whole process
# group: TERM, then SIGKILL when it outlives <grace> seconds (bench_stop_group).
# Never INT.
stop_child()
{
	[ -n "${BENCH_BOUNDED_PID:-}" ] || return 0
	bench_stop_group "$BENCH_BOUNDED_PID" "$1" "${BENCH_BOUNDED_REAPED:-}" ||
		echo "ios-device: could not tell whether the command (pid $BENCH_BOUNDED_PID) still runs; it may" >&2
	BENCH_BOUNDED_PID=
}

# bounded <seconds> <log> <command>...: runs it, its output and errors to
# <log>, through bench_bounded: past the bound its group gets TERM, 5 s of
# grace, then SIGKILL, and it fails with 124
bounded()
{
	local limit=$1 log=$2 status=0 given=$1
	shift 2
	limit=${limit#"${limit%%[!0]*}"}
	[ -n "$limit" ] || limit=0
	bench_is_uint "$limit" || { echo "ios-device: bounded: not a number of seconds of at most 9 digits: $given" >&2; return 2; }
	BENCH_STOP_GRACE=5
	bench_bounded "$limit" bench_to "$log" "$log" "$@" || status=$?
	case $BENCH_BOUNDED_HOW in
	wall|untold)
		echo "ios-device: timed out after $limit s: $1 $2" >&2
		return 124
		;;
	stopped) echo "ios-device: stopped by a stop signal, then ended: $1 $2" >&2 ;;
	esac
	return $status
}

# on_signal <status>: the INT, TERM, QUIT and HUP trap: the running
# command's group is ended first (grace 2 s), then the script exits <status>
on_signal()
{
	if bench_deferred "$1"; then return 0; fi
	trap '' INT TERM QUIT HUP
	stop_child 2
	exit "$1"
}
# bench_undefer calls it with a signal that came while a command started
bench_on_signal() { on_signal "$1"; }

# shown <path>: the path as messages print it: relative to the repository
# when it is in it, its last component otherwise
shown()
{
	local p=$1
	while [ "$p" != "${p%/}" ]; do p=${p%/}; done
	case $p in
	"$repo"/*) printf '%s\n' "${p#"$repo"/}" ;;
	*) printf '%s\n' "${p##*/}" ;;
	esac
}

# forward <log>: a failed tool's first five lines to stderr, with the
# scratch directory, the app's path and the home directory replaced first
# (so a value below cannot break their match), then the identity, the
# team and the device (fixed strings; the real codesign and devicectl
# repeat the identity and the device in their errors). A line that, its
# paths replaced, names a signer is one fixed line instead: codesign names
# there the
# certificate it resolved, which an identity given as a hash or a part of
# the name does not match.
forward()
{
	local line n=0 ph
	while [ "$n" -lt 5 ] && { IFS= read -r line || [ -n "$line" ]; }; do
		ph='<scratch>'; line=${line//"$tmp"/"$ph"}
		ph=$(shown "$app"); line=${line//"$app"/"$ph"}
		if [ -n "${HOME-}" ] && [ "$HOME" != / ]; then ph='<home>'; line=${line//"$HOME"/"$ph"}; fi
		case $line in
		*[Ss][Ii][Gg][Nn][Ee][Rr]*) line='ios-device: (a line naming the signing certificate, not printed)' ;;
		*)
			if [ -n "$identity" ]; then ph='<identity>'; line=${line//"$identity"/"$ph"}; fi
			if [ -n "$team" ]; then ph='<team>'; line=${line//"$team"/"$ph"}; fi
			if [ -n "$device" ]; then ph='<device>'; line=${line//"$device"/"$ph"}; fi
			;;
		esac
		printf '%s\n' "$line" >&2
		n=$((n + 1))
	done < "$1"
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
# the decoded profile holds the team's certificates: only its owner reads its directory
[ "$(stat -f %Lp "$(dirname "$out")")" = 700 ] || { echo "fake security: $(dirname "$out") is readable by others" >&2; exit 1; }
tail -n +2 "$in" > "$out"
[ "$FAKE_SCENARIO" = cmsfail ] && { echo "security: the signer is not trusted" >&2; exit 1; }
# a staged case: the record of the next command's spawn lingers (FIX192)
[ -z "${FAKE_STAGE_SPAWN:-}" ] || : > "$FAKE_STATE/stage-pgid"
exit 0
EOF
	cat > "${sroot:?}/bin/codesign" <<'EOF'
#!/bin/bash
# the fake codesign of "scripts/ios-device.sh --selftest": never a real
# signature; keeps the entitlements file it was given
{ printf 'codesign|'; prev=; for a in "$@"; do if [ "$prev" = --entitlements ]; then printf 'ENT|'; cp "$a" "$FAKE_STATE/entitlements.given"; else printf '%s|' "$a"; fi; prev=$a; done; echo; } >> "$FAKE_STATE/calls"
# its errors name the identity, the team and the bundle's path, as the real
# codesign's do; a hanging one records its PID (exec keeps it)
if [ "${1:-}" = --verify ]; then
	[ "$FAKE_SCENARIO" = verifyfail ] && { echo "${*: -1}: a sealed resource is missing or invalid" >&2; exit 1; }
	exit 0
fi
prev= sid= ent=
for a in "$@"; do
	[ "$prev" = --sign ] && sid=$a
	[ "$prev" = --entitlements ] && ent=$a
	prev=$a
done
case $FAKE_SCENARIO in
signfail)
	echo "$sid: no identity found" >&2
	echo "${*: -1}: the identity $sid is not of team $(plutil -extract application-identifier raw -o - "$ent" 2>/dev/null | cut -d . -f 1)" >&2
	# and the scratch file and a path in the home directory (HOME is the case's)
	echo "$ent: the entitlements could not be applied" >&2
	echo "the keychain $HOME/Library/Keychains/login.keychain-db is locked" >&2
	exit 1 ;;
# an identity given as a hash or a part of the name: codesign names the
# certificate it resolved, FAKE_SIGNER
signchain)
	echo "Warning: unable to build chain to self-signed root for signer \"$FAKE_SIGNER\"" >&2
	echo "${*: -1}: errSecInternalComponent" >&2
	exit 1 ;;
signhang) echo $$ > "$FAKE_STATE/hang.pid"; exec sleep 30 ;;
signstuck) echo $$ > "$FAKE_STATE/hang.pid"; trap '' TERM; exec sleep 30 ;;
esac
app=${*: -1}
mkdir -p "$app/_CodeSignature" && : > "$app/_CodeSignature/CodeResources"
EOF
	cat > "${sroot:?}/bin/xcrun" <<'EOF'
#!/bin/bash
# the fake xcrun of "scripts/ios-device.sh --selftest": devicectl only
{ printf 'xcrun|'; printf '%s|' "$@"; echo; } >> "$FAKE_STATE/calls"
[ "${1:-}" = devicectl ] || { echo "fake xcrun: devicectl only" >&2; exit 1; }
# its error names the device, as the real devicectl's does
prev= dev=
for a in "$@"; do
	[ "$prev" = --device ] && dev=$a
	prev=$a
done
case $FAKE_SCENARIO in
devhang) echo $$ > "$FAKE_STATE/hang.pid"; exec sleep 30 ;;
devfail) echo "ERROR: The specified device was not found. (DeviceIdentifier = $dev)" >&2; exit 1 ;;
esac
exit 0
EOF
	chmod +x "${sroot:?}/bin/security" "${sroot:?}/bin/codesign" "${sroot:?}/bin/xcrun" || die "cannot make the fakes executable"

	# the bundle make leaves (a device's Info.plist), and the profiles
	local id=org.yquake2.quake2 team=SELFTEAM who="Selftest Identity Name" cert=U0VMRlRFU1QtQ0VSVElGSUNBVEU= dev=00000000-SELFTEST
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
	mkprofile "$p/appidchars" "$team.$id;x" "$team"
	mkprofile "$p/teamchars" "SELF.TEAM.$id" SELF.TEAM
	printf '%s\n' "not a CMS message" > "$p/garbage"
	printf '%s\n' FAKE-CMS "not a property list" > "$p/notplist"
	mkapp "$sroot/device.app" iPhoneOS "$id"
	mkapp "$sroot/sim.app" iPhoneSimulator "$id"
	mkapp "$sroot/nobid.app" iPhoneOS ""
	mkapp "$sroot/noexe.app" iPhoneOS "$id"
	rm -f "${sroot:?}/noexe.app/quake2"
	mkapp "$sroot/noplist.app" iPhoneOS "$id"
	rm -f "${sroot:?}/noplist.app/Info.plist"
	# a bundle signed but with no profile (ad hoc), and one with a profile but no signature
	mkapp "$sroot/adhoc.app" iPhoneOS "$id"
	mkdir -p "$sroot/adhoc.app/_CodeSignature" && : > "$sroot/adhoc.app/_CodeSignature/CodeResources" || die "cannot make adhoc.app"
	mkapp "$sroot/profiled.app" iPhoneOS "$id"
	cp "$p/good" "$sroot/profiled.app/embedded.mobileprovision" || die "cannot make profiled.app"
	cp -R "$sroot/device.app" "$sroot/signed.app" || die "cannot copy the bundle"
	cp "$p/good" "$sroot/signed.app/embedded.mobileprovision" && mkdir -p "$sroot/signed.app/_CodeSignature" \
		&& : > "$sroot/signed.app/_CodeSignature/CodeResources" || die "cannot make the signed bundle"
	mkapp "$sroot/platchars.app" "iPhone;OS" "$id"
	# a copy of this script in a scratch repository, the signed bundle at its default path
	mkdir -p "$sroot/repo/scripts/bench/core" "$sroot/repo/build/ios-iphoneos/release" || die "cannot create the scratch repository"
	cp "$self" "$sroot/repo/scripts/ios-device.sh" && cp "$repo/scripts/bench/core/common.sh" "$sroot/repo/scripts/bench/core/common.sh" && cp -R "$sroot/signed.app" "$sroot/repo/build/ios-iphoneos/release/Quake2.app" \
		|| die "cannot fill the scratch repository"
	# a second copy whose primitive is staged (FIX192): its ps read, once
	# FAKE_STATE/stage-<field> exists (stat: a poll of the watch; pgid: the
	# record of a spawn), closes its output once ps has answered and
	# lingers, INT ignored, until FAKE_STATE/sent exists (10 s at most),
	# once: the window in which bash 3.2 drops an INT (between the end of a
	# command substitution's output and its reap), or a HUP comes before
	# the command's pid is stored, opened at will
	local stage_old stage_new stage_s stage_c
	stage_old='	_pq_o=$(TZ=UTC0 ps -o "${2:-stat}=" -o lstart= -p "$1" 2> /dev/null) || return 1'
	stage_new='	_pq_o=$(TZ=UTC0 ps -o "${2:-stat}=" -o lstart= -p "$1" 2> /dev/null; _st=$?; exec >&-; if rm "${FAKE_STATE:-/nonexistent}/stage-${2:-stat}" 2> /dev/null; then trap "" INT; : > "$FAKE_STATE/at-ps"; _n=0; while [ ! -e "$FAKE_STATE/sent" ] && [ "$_n" -lt 100 ]; do sleep 0.1; _n=$((_n + 1)); done; fi; exit "$_st") || return 1'
	stage_s=$(cat "$repo/scripts/bench/core/common.sh") || die "cannot read the primitive"
	case $stage_s in
	*"$stage_old"*"$stage_old"*) die "the stage anchor is in the primitive twice" ;;
	*"$stage_old"*) ;;
	*) die "the stage anchor is not in the primitive" ;;
	esac
	# the replacement quoted, so that bash 5.2 (patsub_replacement) does not
	# read its & as the match; in an assignment, where bash 3.2 drops the
	# quotes too
	stage_c=${stage_s/"$stage_old"/"$stage_new"}
	mkdir -p "$sroot/stagerepo/scripts/bench/core" || die "cannot create the staged repository"
	cp "$self" "$sroot/stagerepo/scripts/ios-device.sh" && printf '%s\n' "$stage_c" > "$sroot/stagerepo/scripts/bench/core/common.sh" \
		|| die "cannot fill the staged repository"
	# the scratch root as the script sees it (pwd -P); no message names it
	sroot_p=$(cd "$sroot" && pwd -P) || die "cannot resolve the scratch root"

	# dt_case <name> <status> <text the output holds> <scenario> <app> [VAR=value]... -- <arguments>...
	# runs the script on a copy of <app> (@APP@ in an argument is its
	# path, @P@ the profiles), with the fakes first on PATH, and checks:
	# the status; the text; a status 2 prints one line, a usage line; no
	# codesign, devicectl or embedded profile where the case refuses
	# before them; nothing of the identity, the team, the device or the
	# certificate printed, and no path in the scratch root; the scratch
	# directory gone. With dt_default set, the script is the scratch
	# repository's copy and IOS_APP is not set (@REPO@ in dt_expect is
	# that repository).
	dt_case()
	{
		local name=$1 want=$2 text=$3 sc=$4 app=$5 d rc=0 why= a t0 script=$self
		local -a envs=() args=() appenv=()
		shift 5
		while [ $# -gt 0 ] && [ "$1" != -- ]; do a=${1//@P@/"$p"}; envs+=("$a"); shift; done
		[ $# -gt 0 ] && shift
		ncase=$((ncase + 1))
		d=$sroot/case/$name
		mkdir -p "${d:?}/state" "${d:?}/tmp" || die "cannot create $d"
		: > "${d:?}/state/calls"
		if [ "$app" != - ]; then
			cp -R "$sroot/$app" "${d:?}/Quake2.app" || die "cannot copy $app"
		fi
		for a in "$@"; do a=${a//@APP@/"$d/Quake2.app"}; a=${a//@P@/"$p"}; args+=("$a"); done
		if [ -n "${dt_default:-}" ]; then
			script=$sroot/repo/scripts/ios-device.sh
		else
			appenv=(IOS_APP="$d/Quake2.app")
		fi
		t0=$SECONDS
		bounded 60 "${d:?}/out" env -u IOS_SIGN_IDENTITY -u IOS_PROFILE -u IOS_DEVICE -u IOS_BUNDLE_ID -u IOS_APP \
			PATH="$sroot/bin:$PATH" FAKE_STATE="$d/state" FAKE_SCENARIO="$sc" TMPDIR="$d/tmp" HOME="$d" \
			${appenv[@]+"${appenv[@]}"} ${envs[@]+"${envs[@]}"} bash "$script" ${args[@]+"${args[@]}"} || rc=$?
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
		elif grep -F -e "$who" -e "$team" -e "$cert" -e SELFTEST-CERTIFICATE -e "$dev" "${d:?}/out" >/dev/null; then
			why="it printed the identity, the team, the device or the certificate"
		elif grep -F -e "$sroot" -e "$sroot_p" "${d:?}/out" >/dev/null; then
			why="it printed a path in the scratch root: $(grep -F -e "$sroot" -e "$sroot_p" "${d:?}/out" | head -n 1)"
		elif [ -n "$(ls -A "${d:?}/tmp")" ]; then
			why="it left its scratch files: $(ls -A "${d:?}/tmp" | head -n 3 | tr '\n' ' ')"
		elif [ "$want" != 0 ] && [ "$sc" = "${sc#sign}" ] && [ "$sc" = "${sc#verify}" ] && [ "$sc" = "${sc#dev}" ] && grep -E -q '^(codesign|xcrun)\|' "${d:?}/state/calls"; then
			why="it refused, but ran $(grep -E -o '^(codesign|xcrun)\|[^|]*' "${d:?}/state/calls" | head -n 1)"
		elif [ "$want" != 0 ] && [ "$sc" = "${sc#sign}" ] && [ "$sc" = "${sc#verify}" ] && [ "$app" = device.app ] && [ -e "${d:?}/Quake2.app/embedded.mobileprovision" ]; then
			why="it refused, but embedded the profile"
		fi
		if [ -z "$why" ] && [ "$rc" = 0 ] && [ -n "${dt_expect:-}" ]; then
			if ! printf '%s\n' "$dt_expect" | sed -e "s|@APP@|$d/Quake2.app|g" -e "s|@P@|$p|g" -e "s|@REPO@|$sroot_p/repo|g" | cmp -s - "${d:?}/state/calls"; then
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
		dt_default=
	}

	# dt_signal <name> <TERM|INT|QUIT|HUP> <scenario> <app> -- <arguments>...:
	# starts the script on a copy of <app> in its own process group (as a
	# terminal's foreground job is), with INT and QUIT reset to their
	# default first (perl, then exec: a shell that ignored them on entry,
	# as a background job of one without job control does, cannot), waits
	# for the fake's hanging step, sends INT to its process group (Ctrl-C),
	# or the signal to the script alone, and checks: it exits 1 (129 for
	# HUP, which the exit cleanup handles) within 10 s; the fake's recorded
	# PID is gone; no scratch file is left; an interrupted sign leaves no
	# profile and no signature in the bundle, an interrupted install or
	# launch leaves the signed bundle as it was. With dt_stage, the staged
	# copy runs (FIX192), and the signal comes inside the staged window:
	# "stat", once the step hangs, the watch's next ps read lingers; "pgid",
	# the record of the spawn after the fake security's lingers
	command -v perl > /dev/null 2>&1 || die "perl not found: the selftest's interrupt cases need it"
	dt_signal()
	{
		local name=$1 sig=$2 sc=$3 app=$4 d why= a hp rc want=1 left script=$self stage=${dt_stage:-}
		local -a args=() stagenv=()
		dt_stage=
		shift 4
		[ "${1:-}" = -- ] && shift
		ncase=$((ncase + 1))
		[ "$sig" = HUP ] && want=129
		d=$sroot/case/$name
		mkdir -p "${d:?}/state" "${d:?}/tmp" || die "cannot create $d"
		: > "${d:?}/state/calls"
		cp -R "$sroot/$app" "${d:?}/Quake2.app" || die "cannot copy $app"
		for a in "$@"; do a=${a//@APP@/"$d/Quake2.app"}; a=${a//@P@/"$p"}; args+=("$a"); done
		if [ -n "$stage" ]; then
			script=$sroot/stagerepo/scripts/ios-device.sh
			[ "$stage" = pgid ] && stagenv=(FAKE_STAGE_SPAWN=1)
		fi
		# a foreground subshell: the script inherits this shell's INT, and
		# set -m starts it in its own process group
		(
			set -m
			perl -e '$SIG{INT} = $SIG{QUIT} = "DEFAULT"; exec { $ARGV[0] } @ARGV or exit 127' \
				env -u IOS_SIGN_IDENTITY -u IOS_PROFILE -u IOS_DEVICE -u IOS_BUNDLE_ID -u IOS_APP \
				PATH="$sroot/bin:$PATH" FAKE_STATE="$d/state" FAKE_SCENARIO="$sc" TMPDIR="$d/tmp" HOME="$d" \
				IOS_APP="$d/Quake2.app" ${stagenv[@]+"${stagenv[@]}"} bash "$script" "${args[@]}" > "$d/out" 2>&1 &
			spid=$!
			n=0
			while [ ! -s "$d/state/hang.pid" ] && [ "$n" -lt 50 ]; do sleep 0.2; n=$((n + 1)); done
			if [ -n "$stage" ]; then
				[ "$stage" = stat ] && : > "$d/state/stage-stat"
				n=0
				while [ ! -e "$d/state/at-ps" ] && [ "$n" -lt 50 ]; do sleep 0.2; n=$((n + 1)); done
			fi
			if [ "$sig" = INT ]; then
				kill -INT -- "-$spid" # bounded-run: allowed: selftest: sends INT under test to the group of the script
			else
				kill -"$sig" "$spid" # bounded-run: allowed: selftest: sends the signal under test
			fi
			: > "$d/state/sent"
			n=0
			while kill -0 "$spid" 2>/dev/null && [ "$n" -lt 50 ]; do sleep 0.2; n=$((n + 1)); done # bounded-run: allowed: selftest: waits for the script under test
			if kill -0 "$spid" 2>/dev/null; then # bounded-run: allowed: selftest: the script under test outlived the signal
				kill -9 "$spid" 2>/dev/null # bounded-run: allowed: selftest: ends the script under test that hung
				wait "$spid" 2>/dev/null
				echo hung
				exit 0
			fi
			wait "$spid"
			echo "$?"
		) > "${d:?}/rc" 2> "${d:?}/launcher.err"
		rc=$(cat "${d:?}/rc")
		hp=$(cat "${d:?}/state/hang.pid" 2>/dev/null) || hp=
		# a hung script, killed above, leaves the fake running: the verdict
		# names the hang first, so it is not read as a stop that missed (FIX192)
		left=
		if [ -n "$hp" ] && kill -0 "$hp" 2>/dev/null; then # bounded-run: allowed: selftest: the fake hanging step outlived the script
			left=1
			kill -9 "$hp" 2>/dev/null # bounded-run: allowed: selftest: ends the fake hanging step
		fi
		if [ -z "$hp" ]; then
			why="the fake never started its hanging step"
		elif [ -n "$stage" ] && [ ! -e "$d/state/at-ps" ]; then
			why="the staged window did not open"
		elif [ "$rc" = hung ]; then
			why="it outlived 10 s after the $sig${left:+, and so did the fake (PID $hp)}"
		elif [ -n "$left" ]; then
			why="the fake (PID $hp) still runs after the script exited"
		elif [ "$rc" != "$want" ]; then
			why="it exited $rc, not $want"
		elif [ -n "$(ls -A "${d:?}/tmp")" ]; then
			why="it left its scratch files: $(ls -A "${d:?}/tmp" | head -n 3 | tr '\n' ' ')"
		elif [ "$sc" != "${sc#sign}" ] && { [ -e "$d/Quake2.app/embedded.mobileprovision" ] || [ -e "$d/Quake2.app/_CodeSignature" ]; }; then
			why="the interrupted sign left a profile or a signature in the bundle"
		elif [ "$sc" = "${sc#sign}" ] && ! { [ -f "$d/Quake2.app/embedded.mobileprovision" ] && [ -f "$d/Quake2.app/_CodeSignature/CodeResources" ]; }; then
			why="the interrupted $sc step unsigned the bundle"
		fi
		if [ -n "$why" ]; then
			echo "ios-device: selftest: FAIL $name: $why"
			sed 's/^/    /' "${d:?}/out" | head -n 8
			nfail=$((nfail + 1))
		else
			echo "ios-device: selftest: ok $name"
		fi
	}

	# sign: a missing or empty identity or profile is a usage error
	dt_case sign-no-identity 2 "usage: scripts/ios-device.sh sign" "" device.app -- sign --profile @P@/good
	dt_case sign-empty-identity 2 "usage: scripts/ios-device.sh sign" "" device.app -- sign --identity "" --profile @P@/good
	dt_case sign-empty-identity-env 2 "usage: scripts/ios-device.sh sign" "" device.app IOS_SIGN_IDENTITY= IOS_PROFILE=@P@/good -- sign
	dt_case sign-no-profile 2 "usage: scripts/ios-device.sh sign" "" device.app -- sign --identity "$who"
	dt_case sign-empty-profile 2 "usage: scripts/ios-device.sh sign" "" device.app -- sign --identity "$who" --profile ""
	dt_case sign-empty-profile-env 2 "usage: scripts/ios-device.sh sign" "" device.app IOS_SIGN_IDENTITY="$who" IOS_PROFILE= -- sign
	dt_case sign-stray-argument 2 "usage: scripts/ios-device.sh sign" "" device.app -- sign --identity "$who" --profile @P@/good extra
	# sign: an identity starting with a hyphen ("-" is codesign's ad hoc one) is a usage error
	dt_case sign-identity-ad-hoc 2 "usage: scripts/ios-device.sh sign" "" device.app -- sign --identity - --profile @P@/good
	dt_case sign-identity-hyphen-env 2 "usage: scripts/ios-device.sh sign" "" device.app IOS_SIGN_IDENTITY=-x IOS_PROFILE=@P@/good -- sign
	# sign: the profile is refused unless it is decoded and names this app
	dt_case sign-profile-missing 1 "is not a file" "" device.app -- sign --identity "$who" --profile @P@/missing
	dt_case sign-profile-undecodable 1 "cannot be decoded" "" device.app -- sign --identity "$who" --profile @P@/garbage
	dt_case sign-profile-not-plist 1 "cannot be decoded" "" device.app -- sign --identity "$who" --profile @P@/notplist
	dt_case sign-profile-cms-fails 1 "cannot be decoded" cmsfail device.app -- sign --identity "$who" --profile @P@/good
	dt_case sign-profile-appid-chars 1 "has no application identifier" "" device.app -- sign --identity "$who" --profile @P@/appidchars
	dt_case sign-profile-team-chars 1 "has no team identifier" "" device.app -- sign --identity "$who" --profile @P@/teamchars
	dt_case sign-profile-no-appid 1 "has no application identifier" "" device.app -- sign --identity "$who" --profile @P@/noappid
	dt_case sign-profile-no-team 1 "has no team identifier" "" device.app -- sign --identity "$who" --profile @P@/noteam
	dt_case sign-profile-other-team 1 "does not start with its team identifier" "" device.app -- sign --identity "$who" --profile @P@/otherteam
	dt_case sign-profile-other-bundle 1 "is for org.example.other, not the app's org.yquake2.quake2" "" device.app -- sign --identity "$who" --profile @P@/other
	dt_case sign-profile-wildcard 1 "is for *, not the app's org.yquake2.quake2" "" device.app -- sign --identity "$who" --profile @P@/wildcard
	# sign: the app must be the device's bundle, of IOS_BUNDLE_ID when set
	dt_case sign-no-app 1 "is not an app bundle" "" - -- sign --identity "$who" --profile @P@/good
	dt_case sign-app-no-executable 1 "is not an app bundle" "" noexe.app -- sign --identity "$who" --profile @P@/good
	dt_case sign-app-no-info-plist 1 "is not an app bundle" "" noplist.app -- sign --identity "$who" --profile @P@/good
	dt_case sign-app-no-bundle-id 1 "has no CFBundleIdentifier" "" nobid.app -- sign --identity "$who" --profile @P@/good
	dt_case sign-simulator-app 1 "is built for iPhoneSimulator, not iPhoneOS" "" sim.app -- sign --identity "$who" --profile @P@/good
	dt_after='! grep -q -F "iPhone;OS" out || echo "it printed the platform unfiltered"'
	dt_case sign-app-platform-chars 1 "Quake2.app/Info.plist names no CFBundleSupportedPlatforms of letters" "" platchars.app -- sign --identity "$who" --profile @P@/good
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
	# sign: --app names the bundle (IOS_APP, a missing one, is not read)
	dt_expect=$sign_calls dt_after=$sign_after
	dt_case sign-ok-app-option 0 "ios-device: signed Quake2.app for $id" "" device.app IOS_APP=@P@/none.app -- sign --identity "$who" --profile @P@/good --app @APP@
	# sign: codesign failing, hanging, or the signature not verifying; the
	# errors it repeats the identity, the team and the path in are printed
	# with placeholders, and the bundle is left with no profile and no
	# signature (a bundle signed before too)
	unsigned_after='[ ! -e Quake2.app/embedded.mobileprovision ] && [ ! -e Quake2.app/_CodeSignature ] || echo "the bundle is left with a profile or a signature"'
	# (the scratch path and the home path, raw, are caught by dt_case's
	# check for a path in the scratch root)
	paths_after='grep -q -F "<scratch>/entitlements.plist: the entitlements could not be applied" out || echo "the scratch path is not printed as <scratch>"
		grep -q -F "the keychain <home>/Library/Keychains/login.keychain-db is locked" out || echo "the home path is not printed as <home>"'
	dt_after="$unsigned_after
		$paths_after"
	dt_case sign-codesign-fails 1 "Quake2.app: the identity <identity> is not of team <team>" signfail device.app -- sign --identity "$who" --profile @P@/good
	dt_after="$unsigned_after
		$paths_after"
	dt_case sign-resign-fails 1 "<identity>: no identity found" signfail signed.app -- sign --identity "$who" --profile @P@/good
	# an identity that is a part of the scratch and home paths ("case"):
	# the paths are replaced first, so it cannot break their match
	dt_after="$unsigned_after
		$paths_after"
	dt_case sign-identity-in-paths 1 "<identity>: no identity found" signfail device.app -- sign --identity case --profile @P@/good
	# an identity as a SHA-1 hash: codesign names the certificate it
	# resolved; a line that names a signer is not printed, one fixed line is
	dt_after="$unsigned_after"'
		grep -q -F "Quake2.app: errSecInternalComponent" out || echo "the line after the signer'"'"'s is not printed"'
	dt_case sign-identity-hash-signer 1 "ios-device: (a line naming the signing certificate, not printed)" signchain device.app \
		FAKE_SIGNER="Apple Development: $who ($team)" -- sign --identity 0123456789ABCDEF0123456789ABCDEF01234567 --profile @P@/good
	dt_after=$unsigned_after
	dt_case sign-codesign-hangs 1 "timed out after 2 s" signhang device.app IOS_DEVICE_SIGN_SECONDS=2 -- sign --identity "$who" --profile @P@/good
	dt_after=$unsigned_after
	dt_case sign-verify-fails 1 "Quake2.app: a sealed resource is missing or invalid" verifyfail device.app -- sign --identity "$who" --profile @P@/good
	# an interrupt during a step: TERM, QUIT (Ctrl-\) or HUP to the script,
	# INT to its process group; a step that ignores TERM too is killed
	# after the grace
	dt_signal sign-term-hang TERM signhang device.app -- sign --identity "$who" --profile @P@/good
	dt_signal sign-term-stuck TERM signstuck device.app -- sign --identity "$who" --profile @P@/good
	dt_signal sign-int-hang INT signhang device.app -- sign --identity "$who" --profile @P@/good
	dt_signal sign-quit-hang QUIT signhang device.app -- sign --identity "$who" --profile @P@/good
	dt_signal sign-hup-hang HUP signhang device.app -- sign --identity "$who" --profile @P@/good
	dt_signal install-int-hang INT devhang signed.app -- install --device "$dev"
	# the same, the signal inside a window the staged primitive opens
	# (FIX192): an INT between the end of the watch's ps read and its reap,
	# which bash 3.2 drops; a HUP before the command's pid is stored
	dt_stage=stat dt_signal sign-int-window INT signhang device.app -- sign --identity "$who" --profile @P@/good
	dt_stage=stat dt_signal install-int-window INT devhang signed.app -- install --device "$dev"
	dt_stage=pgid dt_signal sign-hup-spawn HUP signhang device.app -- sign --identity "$who" --profile @P@/good
	dt_signal launch-term-hang TERM devhang signed.app -- launch --device "$dev"
	# install and launch
	dt_case install-no-device 2 "usage: scripts/ios-device.sh install" "" signed.app -- install
	dt_case install-empty-device 2 "usage: scripts/ios-device.sh install" "" signed.app -- install --device ""
	dt_case install-device-option 2 "usage: scripts/ios-device.sh install" "" signed.app -- install --device -x
	dt_case install-device-space 2 "usage: scripts/ios-device.sh install" "" signed.app -- install --device "a b"
	dt_case install-unsigned 1 "is not signed" "" device.app -- install --device 00000000-SELFTEST
	dt_case install-signed-ad-hoc 1 "is not signed" "" adhoc.app -- install --device 00000000-SELFTEST
	dt_case install-profile-not-signed 1 "is not signed" "" profiled.app -- install --device 00000000-SELFTEST
	dt_case install-limit-not-digits 1 "IOS_DEVICE_INSTALL_SECONDS must be" "" signed.app IOS_DEVICE_INSTALL_SECONDS=-1 -- install --device 00000000-SELFTEST
	dt_expect="xcrun|devicectl|device|install|app|--device|00000000-SELFTEST|@APP@|"
	dt_case install-ok 0 "ios-device: installed" "" signed.app -- install --device 00000000-SELFTEST
	dt_expect="xcrun|devicectl|device|install|app|--device|00000000-SELFTEST|@APP@|"
	dt_case install-ok-env 0 "ios-device: installed" "" signed.app IOS_DEVICE=00000000-SELFTEST -- install
	# the default app, build/ios-iphoneos/release/Quake2.app in the repository, named relative to it
	dt_default=1 dt_expect="xcrun|devicectl|device|install|app|--device|00000000-SELFTEST|@REPO@/build/ios-iphoneos/release/Quake2.app|"
	dt_case install-ok-default-app 0 "ios-device: installed build/ios-iphoneos/release/Quake2.app ($id)" "" - -- install --device 00000000-SELFTEST
	dt_case install-fails 1 "(DeviceIdentifier = <device>)" devfail signed.app -- install --device 00000000-SELFTEST
	dt_case install-hangs 1 "timed out after 2 s" devhang signed.app IOS_DEVICE_INSTALL_SECONDS=2 -- install --device 00000000-SELFTEST
	dt_case launch-no-device 2 "usage: scripts/ios-device.sh launch" "" signed.app -- launch +map base1
	dt_case launch-no-app 1 "is not an app bundle" "" - -- launch --device 00000000-SELFTEST
	dt_expect="xcrun|devicectl|device|process|launch|--device|00000000-SELFTEST|--terminate-existing|$id|--|+set|vid_renderer|metal|+map|base1|"
	dt_case launch-ok 0 "ios-device: launched" "" signed.app -- launch --device 00000000-SELFTEST +set vid_renderer metal +map base1
	dt_expect="xcrun|devicectl|device|process|launch|--device|00000000-SELFTEST|--terminate-existing|$id|--|+map|base1|"
	dt_case launch-ok-dashdash 0 "ios-device: launched" "" signed.app -- launch --device 00000000-SELFTEST -- +map base1
	dt_case launch-fails 1 "(DeviceIdentifier = <device>)" devfail signed.app -- launch --device 00000000-SELFTEST
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
team=
tmp=
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
sign)
	[ -n "$identity" ] && [ -n "$profile" ] || usage sign
	case $identity in
	-*) usage sign ;;
	esac ;;
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
appn=$(shown "$app")
[ -f "$app/Info.plist" ] && [ -f "$app/quake2" ] || die "$appn is not an app bundle (make ios IOS_SDK=iphoneos)"
bid=$(plutil -extract CFBundleIdentifier raw -o - "$app/Info.plist" 2>/dev/null) || bid=
case $bid in
''|*[!A-Za-z0-9.-]*) die "$appn/Info.plist has no CFBundleIdentifier of letters, digits, dots and hyphens" ;;
esac

tmp=$(mktemp -d "${TMPDIR:-/tmp}/ios-device.XXXXXX") || die "no scratch directory"
chmod 700 "${tmp:?}" || die "cannot make the scratch directory private"
# on any exit: the running command ended first, then a sign that did not
# finish undone, then the scratch directory removed; INT, TERM and QUIT
# exit 1 (bash runs no EXIT trap for an untrapped QUIT), HUP 129
unsign=
cleanup()
{
	stop_child 2
	if [ -n "$unsign" ]; then
		rm -f "${app:?}/embedded.mobileprovision"
		rm -rf "${app:?}/_CodeSignature"
	fi
	rm -rf "${tmp:?}"
}
trap cleanup EXIT
trap 'on_signal 1' INT TERM QUIT
# HUP is trapped too, so one that comes while a command starts is kept for
# bench_undefer, which stops it (FIX192); it exits 129, as an untrapped HUP
# did through the EXIT cleanup
trap 'on_signal 129' HUP

if [ "$mode" = sign ]; then
	plat=$(plutil -extract CFBundleSupportedPlatforms.0 raw -o - "$app/Info.plist" 2>/dev/null) || plat=
	case $plat in
	iPhoneOS) ;;
	''|*[!A-Za-z0-9.-]*) die "$appn/Info.plist names no CFBundleSupportedPlatforms of letters, digits, dots and hyphens, not iPhoneOS: make ios IOS_SDK=iphoneos" ;;
	*) die "$appn is built for $plat, not iPhoneOS: make ios IOS_SDK=iphoneos" ;;
	esac
	if [ -n "${IOS_BUNDLE_ID-}" ] && [ "$bid" != "$IOS_BUNDLE_ID" ]; then
		die "the app's CFBundleIdentifier is $bid, not IOS_BUNDLE_ID $IOS_BUNDLE_ID"
	fi
	[ -f "$profile" ] || die "the profile is not a file"
	dec=$tmp/profile.plist
	if ! bounded 60 "${tmp:?}/cms.out" security cms -D -i "$profile" -o "$dec" || ! plutil -lint -s "$dec" > /dev/null 2>&1; then
		die "the profile cannot be decoded (security cms -D, then plutil)"
	fi
	appid=$(plutil -extract Entitlements.application-identifier raw -o - "$dec" 2>/dev/null) || appid=
	case $appid in
	''|*[!A-Za-z0-9.*-]*) die "the profile has no application identifier (Entitlements.application-identifier)" ;;
	esac
	team=$(plutil -extract TeamIdentifier.0 raw -o - "$dec" 2>/dev/null) || team=
	case $team in
	''|*[!A-Za-z0-9]*) die "the profile has no team identifier (TeamIdentifier)" ;;
	esac
	case $appid in
	"$team".*) ;;
	*) die "the profile's application identifier does not start with its team identifier" ;;
	esac
	if [ "${appid#"$team".}" != "$bid" ]; then
		die "the profile is for ${appid#"$team".}, not the app's $bid"
	fi
	ent=$tmp/entitlements.plist
	plutil -extract Entitlements xml1 -o "$ent" "$dec" > /dev/null 2>&1 || die "the profile's entitlements cannot be written to the scratch directory"
	# from here until "signed", any exit removes the profile and the signature again
	unsign=1
	cp "$profile" "$app/embedded.mobileprovision" || die "cannot copy the profile into $appn"
	if ! bounded "$sign_s" "${tmp:?}/codesign.out" codesign --force --sign "$identity" --entitlements "$ent" --timestamp=none "$app"; then
		forward "${tmp:?}/codesign.out"
		die "codesign failed on $appn"
	fi
	if ! bounded 60 "${tmp:?}/verify.out" codesign --verify --strict "$app"; then
		forward "${tmp:?}/verify.out"
		die "codesign --verify failed on $appn"
	fi
	unsign=
	echo "ios-device: signed $appn for $bid (embedded.mobileprovision, the profile's entitlements)"
	exit 0
fi

if [ "$mode" = install ]; then
	[ -f "$app/embedded.mobileprovision" ] && [ -f "$app/_CodeSignature/CodeResources" ] \
		|| die "$appn is not signed: scripts/ios-device.sh sign first"
	if ! bounded "$install_s" "${tmp:?}/install.out" xcrun devicectl device install app --device "$device" "$app"; then
		forward "${tmp:?}/install.out"
		die "devicectl device install app failed"
	fi
	echo "ios-device: installed $appn ($bid)"
	exit 0
fi

if ! bounded "$launch_s" "${tmp:?}/launch.out" xcrun devicectl device process launch --device "$device" --terminate-existing "$bid" -- ${engine_args[@]+"${engine_args[@]}"}; then
	forward "${tmp:?}/launch.out"
	die "devicectl device process launch failed"
fi
echo "ios-device: launched $bid"
exit 0
