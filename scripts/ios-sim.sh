#!/bin/bash
# scripts/ios-sim.sh -- the iOS simulator app on a throwaway device (TASK146).
#
#   scripts/ios-sim.sh smoke --out DIR
#   scripts/ios-sim.sh run --out DIR [--data DIR | --no-data] [--launch "ARGS"]... [--shot SECONDS]
#   scripts/ios-sim.sh selftest
#
# smoke: writes the synthetic data (scripts/ios/smokepak.py, no id Software
# content) to a scratch directory, builds "make ios IOS_BUNDLE_DATA=<it>"
# with IOS_ROOT=DIR/build, creates a device of its own with "simctl create"
# (IOS_SIM_TYPE, default "iPhone 17", on IOS_SIM_RUNTIME, default the newest
# available iOS runtime), boots it, installs the app and launches it with
# --console-pty and Metal's validation layer (SIMCTL_CHILD_MTL_DEBUG_LAYER=1)
# on "+set vid_renderer metal", about 30 "+wait", "+screenshot tga" and
# "+quit". It then copies the engine's own log (qconsole.log, logfile 2)
# and frame from the app's data container to DIR/engine.log and
# DIR/q2_0000.tga and decides from those, from the simulator's own
# screenshot (its screen size) and from the device lists:
#   - the linked-in ref_metal, the Metal device and family lines (with
#     no family from apple5 on, ref_metal's line that the simulator does
#     not check that floor), a Drawable size of the screen in pixels
#     (landscape) and "Wrote q2_0000.tga" are in engine.log;
#   - the frame is that size and not one colour;
#   - Metal's validation layer was on (its line in the app's own output,
#     DIR/console.log), no validation message, and 0 failed command
#     buffers in ref_metal's metal_stats line;
#   - the device lists (DIR/devices.{before,created,after}.json, each
#     "simctl list devices -j"): every device listed before the run is
#     listed after it, and this run's device is in the list taken after
#     "simctl create", not in the one before and not in the one after.
#     Devices that others create or delete meanwhile (Xcode Previews) are
#     not this run's. A list that fails, cannot be read or names no
#     device is a failure.
#
# run (TASK149): what smoke does up to the launch, without the scripted
# quit and the decisions: the app on the throwaway device for a person to
# look at (the device is shut down and deleted when the app ends, or past
# IOS_SIM_RUN_SECONDS). The paks go where a person puts them on a device,
# the app's data container, Documents/baseq2/ (simctl get_app_container
# <udid> <id> data); the engine finds them there with no option:
#   without --data: the synthetic data in the bundle, as smoke does;
#   --data DIR: DIR/baseq2/*.pak, and players/, video/ and music/ of
#     DIR/baseq2 when present, are copied into Documents/baseq2/; the
#     bundle holds no data; nothing is copied anywhere else. DIR must hold
#     baseq2/ with at least one .pak, or the run does not start;
#   --no-data: no data at all (the missing-data alert).
# Each --launch "ARGS" is one start of the app, in order, on the same
# device and container (a setting or a save is read back by the next
# one); ARGS are engine arguments, split at spaces. With none, one start
# with no argument, the app until the person quits it. --shot SECONDS:
# a simctl screenshot that long after each start (DIR/screen.<n>.png),
# then simctl terminate. After each start DIR/engine.<n>.log is the
# engine's own log (qconsole.log), DIR/documents.<n>.list the files of
# Documents/baseq2/ with their sizes (names, never contents) and
# DIR/frames.<n>/ its scrnshot/ files. With --data these hold the
# owner's data: DIR is the artifacts folder, never published.
#
# selftest: runs "smoke" against a fake xcrun and a fake make (no
# simulator, no build, seconds) and checks its rules: a hung "simctl
# list" is timed out, a failed or empty device list fails, another device
# appearing meanwhile passes, a device from before that disappears fails,
# this run's device left behind fails, the device of a create that timed
# out is deleted, a create whose UDID is not listed after it, or was
# listed before it, fails (and leaves that device alone), ref_metal's
# floor refusal fails, and a limit that is not digits is refused; for run:
# the synthetic data without --data, the pak and the three folders of
# --data copied into Documents/baseq2 and nothing else (not a stray file,
# not another folder, not the bundle), a --data without baseq2 or without
# a pak refused before a device is made, a container that cannot be found
# fails and still deletes the device, an app that outlives
# IOS_SIM_RUN_SECONDS is terminated and fails, each --launch is a start
# with its own log and listing, --shot ends the app and keeps the frame.
# tests/check-ios.sh runs it.
#
# Limits, in seconds (digits only, or the script refuses):
# IOS_SIM_BOOT_SECONDS (300), IOS_SIM_SMOKE_SECONDS (180, the app's run),
# IOS_SIM_LIST_SECONDS (60, each "simctl list"), IOS_SIM_CREATE_SECONDS
# (120), IOS_SIM_RUN_SECONDS (3600, each start of "run").
#
# Rules: every wait is bounded; the app is watched by its PID; nothing is
# killed but a PID this script started, and the app only through "simctl
# terminate" on this script's device; no other device is touched (the
# trap shuts down and deletes this one by its UDID); DIR must be outside
# the checkout, and the build goes to DIR/build, never to release/ or
# debug/. A failed build, list, create, install or launch is a failure,
# not a skip.
set -u
set -o pipefail
export LC_ALL=C

repo=$(cd "$(dirname "$0")/.." && pwd -P) || exit 1
self=$(cd "$(dirname "$0")" && pwd -P)/$(basename "$0") || exit 1
die() { echo "ios-sim: $*" >&2; exit 1; }
usage()
{
	echo "usage: scripts/ios-sim.sh smoke --out DIR" >&2
	echo "       scripts/ios-sim.sh run --out DIR [--data DIR | --no-data] [--launch \"ARGS\"]... [--shot SECONDS]" >&2
	echo "       scripts/ios-sim.sh selftest" >&2
	exit 2
}
uuid_re='[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}'

# bounded <seconds> <log> [-e <err>] <command>...: runs it, kills it (our
# PID) past the bound; its output goes to <log>, its errors there too or
# to <err>
bounded()
{
	local limit=$1 log=$2 err=$2 pid t=0 w status=0
	shift 2
	if [ "${1:-}" = -e ]; then
		err=$2
		shift 2
	fi
	case $limit in
	''|*[!0-9]*) echo "ios-sim: bounded: not a number of seconds: $limit" >&2; return 2 ;;
	esac
	if [ "$err" = "$log" ]; then
		"$@" > "$log" 2>&1 &
	else
		"$@" > "$log" 2> "$err" &
	fi
	pid=$!
	while kill -0 "$pid" 2>/dev/null; do
		if [ "$t" -ge $((limit * 5)) ]; then
			kill "$pid" 2>/dev/null
			w=0; while kill -0 "$pid" 2>/dev/null && [ "$w" -lt 5 ]; do sleep 1; w=$((w + 1)); done
			kill -0 "$pid" 2>/dev/null && kill -9 "$pid" 2>/dev/null
			wait "$pid" 2>/dev/null
			echo "ios-sim: timed out after $limit s: $*" >&2
			return 124
		fi
		sleep 0.2
		t=$((t + 1))
	done
	wait "$pid" || status=$?
	return $status
}

# stop_pid <pid>: TERM, a few seconds of grace, then KILL and reap (our
# PID only); returns 0 if it had to be killed hard, 1 if TERM was enough
stop_pid()
{
	local w=0
	kill "$1" 2>/dev/null
	while kill -0 "$1" 2>/dev/null && [ "$w" -lt 5 ]; do sleep 1; w=$((w + 1)); done
	if kill -0 "$1" 2>/dev/null; then
		kill -9 "$1" 2>/dev/null
		wait "$1" 2>/dev/null
		return 0
	fi
	wait "$1" 2>/dev/null
	return 1
}

# ---- selftest: the smoke against a fake xcrun and make -------------------

selftest()
{
	sroot=$(mktemp -d "${TMPDIR:-/tmp}/ios-sim-selftest.XXXXXX") || die "no scratch directory"
	trap 'rm -rf "${sroot:?}"' EXIT
	mkdir -p "${sroot:?}/bin" || die "cannot create $sroot/bin"
	cat > "${sroot:?}/bin/xcrun" <<'EOF'
#!/bin/bash
# the fake xcrun of "scripts/ios-sim.sh selftest": simctl on the device
# list in $FAKE_STATE/devices ("<udid> <name>" lines), FAKE_SCENARIO
st=${FAKE_STATE:?}
sc=${FAKE_SCENARIO:-extra}
printf '%s\n' "$*" >> "$st/calls"
[ "${1:-}" = simctl ] || { echo "fake xcrun: simctl only" >&2; exit 1; }
shift
drop() { awk -v u="$1" '$1 != u' "$st/devices" > "$st/devices.new" && mv "$st/devices.new" "$st/devices"; }
case ${1:-} in
list)
	case ${2:-} in
	devices)
		case $sc in
		hang) echo $$ > "$st/hang.pid"; exec sleep 30 ;;
		samename) grep -q '^55555555-' "$st/devices" || printf '%s\n' "55555555-5555-5555-5555-555555555555 yq2-ios-sim-$PPID" >> "$st/devices" ;;
		empty) exit 0 ;;
		listfail) echo "fake xcrun: CoreSimulatorService is not running" >&2; exit 1 ;;
		esac
		python3 -I -c 'import json, sys
devs = []
for l in open(sys.argv[1]):
    u, n = l.rstrip("\n").split(" ", 1)
    devs.append({"udid": u, "name": n, "state": "Shutdown"})
print(json.dumps({"devices": {"com.apple.CoreSimulator.SimRuntime.iOS-27-2": devs}}))' "$st/devices" ;;
	devicetypes) echo '{"devicetypes": [{"name": "iPhone 17", "identifier": "com.apple.CoreSimulator.SimDeviceType.iPhone-17"}]}' ;;
	runtimes) echo '{"runtimes": [{"isAvailable": true, "platform": "iOS", "version": "27.2", "name": "iOS 27.2", "identifier": "com.apple.CoreSimulator.SimRuntime.iOS-27-2", "supportedDeviceTypes": [{"identifier": "com.apple.CoreSimulator.SimDeviceType.iPhone-17"}]}]}' ;;
	*) exit 1 ;;
	esac ;;
create)
	u=$(printf 'AAAAAAAA-0000-0000-0000-%012d' $$)
	case $sc in
	ghost) echo "BBBBBBBB-0000-0000-0000-000000000000"; exit 0 ;;
	reuse) echo "11111111-1111-1111-1111-111111111111"; exit 0 ;;
	esac
	printf '%s %s\n' "$u" "$2" >> "$st/devices"
	if [ "$sc" = extra ]; then
		printf '%s\n' "33333333-3333-3333-3333-333333333333 Previews kept" \
			"44444444-4444-4444-4444-444444444444 Previews gone" >> "$st/devices"
	fi
	if [ "$sc" = createhang ] || [ "$sc" = samename ]; then
		echo $$ > "$st/hang.pid"
		exec sleep 30
	fi
	echo "$u" ;;
terminate) [ -f "$st/app.pid" ] && kill "$(cat "$st/app.pid")" 2>/dev/null; echo "$*" >> "$st/terminates"; exit 0 ;;
bootstatus|install|shutdown) ;;
io)
	# io <udid> screenshot <png>: the head of a 16x8 PNG
	printf '\211PNG\r\n\032\n\000\000\000\015IHDR\000\000\000\020\000\000\000\010' > "$4" ;;
launch)
	# launch --console-pty <udid> <bundle id> <args>...
	printf '%s\n' "$*" >> "$st/launches"
	c=$st/container/Documents/baseq2
	mkdir -p "$c/scrnshot"
	skip="ref_metal: the device floor (apple5) is not checked in the simulator"
	[ "$sc" = floorrefused ] && skip="ref_metal: the Metal device supports no Apple GPU family from apple5 on; ref_metal needs one"
	printf '%s\n' "Using the linked-in renderer: ref_metal (linked in)" "Metal device: Fake GPU" \
		"Metal supportsFamily (apple5 to apple9): none" "$skip" \
		"Drawable size: 16x8" "Wrote q2_0000.tga" \
		"ref_metal: metal_stats: 30 frames, 0 without a drawable, 0 skipped, 0 failed command buffers, " > "$c/qconsole.log"
	python3 -I -c 'import sys
open(sys.argv[1], "wb").write(bytes([0, 0, 2] + [0] * 9 + [16, 0, 8, 0, 24, 0]) + bytes(range(256)) + bytes(range(128)))' "$c/scrnshot/q2_0000.tga"
	[ "$sc" = launchterm ] && trap "" TERM
	if [ "$sc" = appstays ]; then sleep 30 & else sleep 1 & fi
	p=$!
	echo $p > "$st/app.pid"
	echo "$4: $p"
	echo "Metal API Validation Enabled"
	wait "$p"
	# a launch client that ignores TERM and outlives the app
	[ "$sc" = launchterm ] && { echo $$ > "$st/hang.pid"; exec sleep 60; }
	exit 0 ;;
get_app_container)
	[ "$sc" = nocontainer ] && { echo "fake xcrun: no such container" >&2; exit 1; }
	mkdir -p "$st/container"; echo "$st/container" ;;
delete)
	[ "$sc" = leftover ] || drop "$2"
	[ "$sc" = vanish ] && drop 22222222-2222-2222-2222-222222222222
	[ "$sc" = extra ] && drop 44444444-4444-4444-4444-444444444444
	exit 0 ;;
*) exit 1 ;;
esac
EOF
	cat > "${sroot:?}/bin/make" <<'EOF'
#!/bin/bash
# the fake make of "scripts/ios-sim.sh selftest": an app with an Info.plist
r=
for a; do case $a in IOS_ROOT=*) r=${a#IOS_ROOT=} ;; esac; done
[ -n "$r" ] || { echo "fake make: no IOS_ROOT" >&2; exit 2; }
mkdir -p "$r/Quake2.app" || exit 1
printf '%s\n' "$*" > "$r/make.args"
printf '#!/bin/sh\nexit 0\n' > "$r/Quake2.app/quake2" && chmod +x "$r/Quake2.app/quake2" || exit 1
printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
	'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	'<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>org.example.selftest</string></dict></plist>' \
	> "$r/Quake2.app/Info.plist"
EOF
	chmod +x "${sroot:?}/bin/xcrun" "${sroot:?}/bin/make" || die "cannot make the fakes executable"

	nfail=0
	ncase=0
	# <name> <scenario> <ok|fail> <text the output must hold> <max seconds> [VAR=value]...
	st_case()
	{
		local name=$1 sc=$2 want=$3 text=$4 max=$5 d rc=0 t0 why=
		shift 5
		ncase=$((ncase + 1))
		d=$sroot/$name
		mkdir -p "${d:?}/state" || die "cannot create $d"
		printf '%s\n' "11111111-1111-1111-1111-111111111111 Owner iPhone" \
			"22222222-2222-2222-2222-222222222222 Owner iPad" > "${d:?}/state/devices"
		t0=$(date +%s)
		cmd=${ST_CMD:-smoke --out @D@/out}; cmd=${cmd//@D@/$d}
		bounded 60 "${d:?}/run.log" env PATH="$sroot/bin:$PATH" FAKE_STATE="$d/state" FAKE_SCENARIO="$sc" \
			IOS_SIM_LIST_SECONDS=2 IOS_SIM_CREATE_SECONDS=2 IOS_SIM_BOOT_SECONDS=10 IOS_SIM_SMOKE_SECONDS=10 "$@" \
			bash "$self" $cmd || rc=$?
		t0=$(( $(date +%s) - t0 ))
		if [ "$rc" = 124 ]; then
			why="the smoke outlived 60 s"
		elif [ "$want" = ok ] && [ "$rc" != 0 ]; then
			why="the smoke failed ($rc)"
		elif [ "$want" = fail ] && [ "$rc" = 0 ]; then
			why="the smoke passed"
		elif ! grep -F -e "$text" "${d:?}/run.log" >/dev/null; then
			why="its output does not hold \"$text\""
		elif [ "$t0" -gt "$max" ]; then
			why="it took $t0 s, more than $max s"
		elif [ "$sc" != leftover ] && grep '^AAAAAAAA-' "${d:?}/state/devices" >/dev/null; then
			why="this run's device is left in the fake's device list"
		elif ! grep '^11111111-' "${d:?}/state/devices" >/dev/null \
			|| { [ "$sc" != vanish ] && ! grep '^22222222-' "${d:?}/state/devices" >/dev/null; }; then
			why="an owner's device was deleted"
		fi
		if [ "$sc" = samename ] && ! grep '^55555555-' "${d:?}/state/devices" >/dev/null; then
			why="${why:+$why; }a device named like this run's, listed before it, was deleted"
		fi
		if [ -f "${d:?}/state/hang.pid" ] && kill -0 "$(cat "${d:?}/state/hang.pid")" 2>/dev/null; then
			kill -9 "$(cat "${d:?}/state/hang.pid")" 2>/dev/null
			why="${why:+$why; }the hung simctl call was left running"
		fi
		if [ -n "$why" ]; then
			echo "ios-sim: selftest: FAIL $name: $why"
			tail -n 4 "${d:?}/run.log" | sed 's/^/    /'
			nfail=$((nfail + 1))
		else
			echo "ios-sim: selftest: ok $name"
		fi
	}
	# st_has <case dir name> <file|nofile|grep|nogrep> <path under the case dir> <text> <why>:
	# a check on what a case left behind
	st_has()
	{
		local name=$1 kind=$2 f=$sroot/$1/$3 text=${4:-} why=
		ncase=$((ncase + 1))
		case $kind in
		file) [ -f "$f" ] || why="$3 is missing" ;;
		nofile) [ ! -e "$f" ] || why="$3 exists" ;;
		grep) grep -F -e "$text" "$f" >/dev/null 2>&1 || why="$3 does not hold \"$text\"" ;;
		nogrep) ! grep -F -e "$text" "$f" >/dev/null 2>&1 || why="$3 holds \"$text\"" ;;
		esac
		if [ -n "$why" ]; then
			echo "ios-sim: selftest: FAIL $name: $why"
			nfail=$((nfail + 1))
		else
			echo "ios-sim: selftest: ok $name $kind $3"
		fi
	}
	mkdata()
	{
		mkdir -p "$sroot/$1/baseq2/players/male" "$sroot/$1/baseq2/video" "$sroot/$1/baseq2/music" "$sroot/$1/baseq2/maps" "$sroot/$1/baseq2/other"
		printf 'pak' > "$sroot/$1/baseq2/pak0.pak"
		printf 'pak' > "$sroot/$1/baseq2/pak1.pak"
		printf 'x' > "$sroot/$1/baseq2/players/male/tris.md2"
		printf 'x' > "$sroot/$1/baseq2/video/idlog.cin"
		printf 'x' > "$sroot/$1/baseq2/music/02.ogg"
		printf 'x' > "$sroot/$1/baseq2/maps/stray.bsp"
		printf 'x' > "$sroot/$1/baseq2/other/stray.txt"
		printf 'x' > "$sroot/$1/baseq2/stray.cfg"
		printf 'x' > "$sroot/$1/stray.pak"
	}
	st_case extra-device-meanwhile extra ok "smoke ok" 30
	# the smoke gives no write-directory option: the engine finds Documents
	st_has extra-device-meanwhile nogrep state/launches -cfgdir
	st_has extra-device-meanwhile nogrep state/launches -portable
	st_has extra-device-meanwhile grep out/build/make.args IOS_BUNDLE_DATA=
	# run without --data: the synthetic data in the bundle, one start
	ST_CMD="run --out @D@/out" st_case run-synthetic extra ok "run ok, 1 start(s)" 30
	st_has run-synthetic grep out/build/make.args IOS_BUNDLE_DATA=
	st_has run-synthetic file out/engine.1.log
	st_has run-synthetic file out/documents.1.list
	st_has run-synthetic nogrep state/launches -cfgdir
	# run --data: the paks and the three folders land in Documents/baseq2
	mkdir -p "$sroot/run-data" && mkdata run-data-src
	ST_CMD="run --out @D@/out --data $sroot/run-data-src" st_case run-data extra ok "run ok, 1 start(s)" 30
	st_has run-data nogrep out/build/make.args IOS_BUNDLE_DATA=
	st_has run-data file state/container/Documents/baseq2/pak0.pak
	st_has run-data file state/container/Documents/baseq2/pak1.pak
	st_has run-data file state/container/Documents/baseq2/players/male/tris.md2
	st_has run-data file state/container/Documents/baseq2/video/idlog.cin
	st_has run-data file state/container/Documents/baseq2/music/02.ogg
	st_has run-data nofile state/container/Documents/baseq2/stray.cfg
	st_has run-data nofile state/container/Documents/baseq2/maps
	st_has run-data nofile state/container/Documents/baseq2/other
	st_has run-data nofile state/container/Documents/stray.pak
	st_has run-data nofile state/container/Documents/baseq2/baseq2
	st_has run-data grep out/documents.1.list "pak0.pak"
	# the data is in no other place of the case: not in the build, the bundle
	if [ -n "$(find "$sroot/run-data/out/build" -name '*.pak' 2>/dev/null)" ]; then
		ncase=$((ncase + 1)); nfail=$((nfail + 1)); echo "ios-sim: selftest: FAIL run-data: a pak in the build tree"
	fi
	# run --no-data: no data in the bundle, the engine alone
	ST_CMD="run --out @D@/out --no-data --shot 0" st_case run-no-data extra ok "run ok, 1 start(s)" 30
	st_has run-no-data nogrep out/build/make.args IOS_BUNDLE_DATA=
	st_has run-no-data file out/screen.1.png
	st_has run-no-data file out/documents.1.list
	st_has run-no-data grep state/terminates terminate
	# --data refused before a device is made
	mkdir -p "$sroot/run-data-no-baseq2/src"
	ST_CMD="run --out @D@/out --data @D@/src" st_case run-data-no-baseq2 extra fail "holds no baseq2/ directory" 10
	st_has run-data-no-baseq2 nofile state/calls
	mkdir -p "$sroot/run-data-no-pak/src/baseq2" && printf x > "$sroot/run-data-no-pak/src/baseq2/readme.txt"
	ST_CMD="run --out @D@/out --data @D@/src" st_case run-data-no-pak extra fail "holds no .pak file" 10
	st_has run-data-no-pak nofile state/calls
	# a container that cannot be found: a failure, and the device is deleted
	mkdata run-no-container/src
	ST_CMD="run --out @D@/out --data $sroot/run-no-container/src" st_case run-no-container nocontainer fail "simctl get_app_container failed" 30
	# an app that outlives the bound is terminated and fails
	ST_CMD="run --out @D@/out" st_case run-app-stays appstays fail "did not quit within 2 s (IOS_SIM_RUN_SECONDS); terminated it" 30 IOS_SIM_RUN_SECONDS=2
	st_has run-app-stays grep state/terminates terminate
	# each --launch is a start, in order, with its own log and listing
	ST_CMD="run --out @D@/out --launch +map_base1 --launch +set_a_1" st_case run-two-launches extra ok "run ok, 2 start(s)" 30
	st_has run-two-launches file out/engine.1.log
	st_has run-two-launches file out/engine.2.log
	st_has run-two-launches file out/documents.2.list
	st_has run-two-launches grep state/launches "+map_base1"
	st_has run-two-launches grep state/launches "+set_a_1"
	# the usage: no --out, both data options, a limit that is not digits
	ST_CMD="run" st_case run-no-out extra fail "usage: scripts/ios-sim.sh" 5
	ST_CMD="run --out @D@/out --data /x --no-data" st_case run-data-and-no-data extra fail "usage: scripts/ios-sim.sh" 5
	ST_CMD="run --out @D@/out --shot 3s" st_case run-shot-not-digits extra fail "--shot must be a number of seconds" 5
	ST_CMD="run --out @D@/out" st_case run-limit-not-digits extra fail "IOS_SIM_RUN_SECONDS must be" 5 IOS_SIM_RUN_SECONDS=1h
	st_case list-empty empty fail "simctl list devices: no device list" 30
	st_case list-fails listfail fail "simctl list devices failed" 30
	st_case list-hangs hang fail "timed out after 2 s: xcrun simctl list devices" 10
	st_case device-from-before-gone vanish fail "gone after the run: 22222222-2222-2222-2222-222222222222" 30
	st_case own-device-left leftover fail "is still listed after its delete" 30
	st_case create-times-out createhang fail "deleting AAAAAAAA-0000-0000-0000-" 30
	st_case created-device-not-listed ghost fail "is not in the device list taken after simctl create" 30
	st_case created-device-listed-before reuse fail "was already listed before simctl create" 30
	st_case lost-device-listed-before samename fail "not deleting 55555555-5555-5555-5555-555555555555" 30
	st_case launch-ignores-term launchterm ok "the launch client did not exit after the app" 25
	st_case family-refused floorrefused fail "ref_metal refused it (its floor)" 30
	st_case smoke-limit-not-digits extra fail "IOS_SIM_SMOKE_SECONDS must be" 5 IOS_SIM_SMOKE_SECONDS=3m
	st_case boot-limit-not-digits extra fail "IOS_SIM_BOOT_SECONDS must be" 5 IOS_SIM_BOOT_SECONDS=1e3
	st_case list-limit-not-digits extra fail "IOS_SIM_LIST_SECONDS must be" 5 IOS_SIM_LIST_SECONDS=-1
	st_case create-limit-not-digits extra fail "IOS_SIM_CREATE_SECONDS must be" 5 "IOS_SIM_CREATE_SECONDS= 9"
	if [ "$nfail" -ne 0 ]; then
		echo "ios-sim: selftest: $nfail of $ncase cases failed"
		exit 1
	fi
	echo "ios-sim: selftest: ok ($ncase cases)"
	exit 0
}

if [ $# -eq 1 ] && [ "$1" = selftest ]; then
	selftest
fi
mode=${1:-}
[ $# -ge 1 ] && shift
out=
data=
nodata=0
shot=
launches=()
case $mode in
smoke)
	[ $# -eq 2 ] && [ "$1" = --out ] || usage
	out=$2 ;;
run)
	while [ $# -gt 0 ]; do
		case $1 in
		--out) [ $# -ge 2 ] && [ -z "$out" ] || usage; out=$2; shift 2 ;;
		--data) [ $# -ge 2 ] && [ -z "$data" ] && [ "$nodata" = 0 ] || usage; data=$2; shift 2 ;;
		--no-data) [ -z "$data" ] && [ "$nodata" = 0 ] || usage; nodata=1; shift ;;
		--launch) [ $# -ge 2 ] || usage; launches+=("$2"); shift 2 ;;
		--shot) [ $# -ge 2 ] && [ -z "$shot" ] || usage; shot=$2; shift 2 ;;
		*) usage ;;
		esac
	done ;;
*) usage ;;
esac
[ -n "$out" ] || usage

digits() # <name> <value>
{
	case $2 in
	''|*[!0-9]*) die "$1 must be a number of seconds, digits only, not \"$2\"" ;;
	esac
}
boot_s=${IOS_SIM_BOOT_SECONDS-300}
smoke_s=${IOS_SIM_SMOKE_SECONDS-180}
list_s=${IOS_SIM_LIST_SECONDS-60}
create_s=${IOS_SIM_CREATE_SECONDS-120}
run_s=${IOS_SIM_RUN_SECONDS-3600}
digits IOS_SIM_BOOT_SECONDS "$boot_s"
digits IOS_SIM_SMOKE_SECONDS "$smoke_s"
digits IOS_SIM_LIST_SECONDS "$list_s"
digits IOS_SIM_CREATE_SECONDS "$create_s"
digits IOS_SIM_RUN_SECONDS "$run_s"
[ -z "$shot" ] || digits "--shot" "$shot"

# the owner's data: checked before anything is built or created
if [ -n "$data" ]; then
	[ -d "$data/baseq2" ] || die "--data $data holds no baseq2/ directory"
	data=$(cd "$data" && pwd -P) || die "cannot enter --data"
	npak=0
	for f in "$data"/baseq2/*.pak; do
		[ -f "$f" ] && npak=$((npak + 1))
	done
	[ "$npak" -gt 0 ] || die "--data $data/baseq2 holds no .pak file"
fi

for tool in xcrun python3 make plutil; do
	command -v "$tool" >/dev/null 2>&1 || die "$tool not found: a missing tool is a failure"
done

# outside the checkout before anything is made, and again once resolved
case "$out" in /*) ;; *) out=$PWD/$out ;; esac
case "$out/" in "$repo/"*|"$(cd "$(dirname "$0")/.." && pwd -L)/"*) die "--out must be outside the checkout ($repo), not $out" ;; esac
mkdir -p "${out:?}" || die "cannot create $out"
out=$(cd "${out:?}" && pwd -P) || die "cannot enter $out"
case "$out/" in "$repo/"*) die "--out must be outside the checkout ($repo), not $out" ;; esac

tmp=$(mktemp -d "${TMPDIR:-/tmp}/ios-sim.XXXXXX") || die "no scratch directory"
udid=
lpid=
cleanup()
{
	if [ -n "$lpid" ] && kill -0 "$lpid" 2>/dev/null; then
		stop_pid "$lpid" || true
	fi
	if [ -n "$udid" ]; then
		bounded 60 "${tmp:?}/shutdown.out" xcrun simctl shutdown "$udid" || true
		bounded 60 "${tmp:?}/delete.out" xcrun simctl delete "$udid" || echo "ios-sim: could not delete the device $udid" >&2
		udid=
	fi
	rm -rf "${tmp:?}"
}
trap cleanup EXIT
trap 'exit 1' INT TERM

# list_devices <file>: "simctl list devices -j" into <file>.json (bounded,
# its status checked), then the default device set's devices, one
# "<udid> <name>" per line, sorted, into <file>; a list that cannot be
# read or names no device is an error
list_devices()
{
	local f=$1 rc=0
	bounded "$list_s" "$f.json" -e "$f.err" xcrun simctl list devices -j || rc=$?
	if [ "$rc" -ne 0 ]; then
		echo "ios-sim: simctl list devices failed ($rc): $(head -n 2 "$f.err" 2>/dev/null | tr '\n' ' ')" >&2
		return 1
	fi
	if ! python3 -I - "$f.json" > "$f" 2> "$f.err" <<'EOF'
import json, sys
try:
    lists = json.load(open(sys.argv[1]))["devices"]
    lines = sorted(d["udid"] + " " + d["name"] for devs in lists.values() for d in devs)
except (ValueError, KeyError, TypeError, AttributeError) as e:
    sys.exit("unreadable JSON: %s" % e)
if not lines:
    sys.exit("it names no device")
print("\n".join(lines))
EOF
	then
		echo "ios-sim: simctl list devices: no device list ($(tail -n 1 "$f.err" 2>/dev/null))" >&2
		return 1
	fi
}
udids() { cut -d ' ' -f 1 "$1" | sort -u; }

# lost_device <name>: a create that failed (or timed out) may still have
# made the device: delete every listed device of that name by its UDID,
# except one that was already listed before the run (not ours)
lost_device()
{
	local u
	if ! list_devices "${tmp:?}/devices.lost"; then
		echo "ios-sim: cannot list the devices to find $1; delete it by hand if it exists" >&2
		return 1
	fi
	awk -v n="$1" '{ u = $1; sub(/^[^ ]+ /, ""); if ($0 == n) print u }' "${tmp:?}/devices.lost" > "${tmp:?}/lost"
	while read -r u; do
		printf '%s\n' "$u" | grep -E -x "$uuid_re" >/dev/null || continue
		if udids "$out/devices.before" | grep -F -x -e "$u" >/dev/null; then
			echo "ios-sim: not deleting $u ($1): it was listed before the run" >&2
			continue
		fi
		echo "ios-sim: deleting $u ($1), made by the create that failed" >&2
		bounded 60 "${tmp:?}/lost-shutdown.out" xcrun simctl shutdown "$u" || true
		bounded 60 "${tmp:?}/lost-delete.out" xcrun simctl delete "$u" || echo "ios-sim: could not delete the device $u ($1)" >&2
	done < "${tmp:?}/lost"
}

# ---- the data and the build --------------------------------------------

build=$out/build
makeargs=(-C "$repo" -j"${IOS_SIM_JOBS:-3}" QUIET=1 ios IOS_SDK=iphonesimulator IOS_ROOT="$build")
# the bundle's data: the synthetic paks (smoke, run without --data); with
# --data or --no-data the bundle holds none
if [ -z "$data" ] && [ "$nodata" = 0 ]; then
	python3 -I "$repo/scripts/ios/smokepak.py" --out "${tmp:?}/data" > "${tmp:?}/smokepak.out" 2>&1 \
		|| { cat "${tmp:?}/smokepak.out" >&2; die "smokepak.py failed"; }
	makeargs+=(IOS_BUNDLE_DATA="$tmp/data")
fi
echo "ios-sim: make ios (into $build)"
if ! (unset MAKEFLAGS GNUMAKEFLAGS MFLAGS; make "${makeargs[@]}") > "$out/build.log" 2>&1; then
	tail -n 20 "$out/build.log" >&2
	die "make ios failed (log: $out/build.log)"
fi
app=$build/Quake2.app
[ -x "$app/quake2" ] || die "make ios wrote no $app/quake2"
bid=$(plutil -extract CFBundleIdentifier raw -o - "$app/Info.plist" 2>/dev/null) || die "no CFBundleIdentifier in $app/Info.plist"

# ---- the device ----------------------------------------------------------

list_devices "$out/devices.before" || die "no device list before the run"
bounded "$list_s" "${tmp:?}/types.json" -e "${tmp:?}/types.err" xcrun simctl list devicetypes -j || die "simctl list devicetypes failed"
bounded "$list_s" "${tmp:?}/runtimes.json" -e "${tmp:?}/runtimes.err" xcrun simctl list runtimes -j || die "simctl list runtimes failed"
pick=$(python3 -I - "${tmp:?}/types.json" "${tmp:?}/runtimes.json" "${IOS_SIM_TYPE:-iPhone 17}" "${IOS_SIM_RUNTIME:-}" <<'EOF'
import json, sys
types = json.load(open(sys.argv[1]))["devicetypes"]
runtimes = json.load(open(sys.argv[2]))["runtimes"]
want_type, want_rt = sys.argv[3], sys.argv[4]
t = [d for d in types if want_type in (d["name"], d["identifier"])]
if not t:
    sys.exit("no device type " + want_type)
ios = [r for r in runtimes if r.get("isAvailable") and r.get("platform", "") == "iOS"]
if want_rt:
    ios = [r for r in ios if want_rt in (r["identifier"], r["version"], r["name"])]
if not ios:
    sys.exit("no available iOS runtime" + (" " + want_rt if want_rt else ""))
rt = max(ios, key=lambda r: [int(x) for x in r["version"].split(".")])
ok = [s["identifier"] for s in rt.get("supportedDeviceTypes", [])]
if ok and t[0]["identifier"] not in ok:
    sys.exit(t[0]["name"] + " is not supported by " + rt["name"])
print(t[0]["identifier"], rt["identifier"], rt["name"])
EOF
) || die "no device for the run"
read -r type_id rt_id rt_name <<< "$pick"
name="yq2-ios-sim-$$"
if ! bounded "$create_s" "${tmp:?}/create.out" xcrun simctl create "$name" "$type_id" "$rt_id"; then
	cat "${tmp:?}/create.out" >&2
	lost_device "$name"
	die "simctl create failed"
fi
udid=$(tail -n 1 "${tmp:?}/create.out")
printf '%s\n' "$udid" | grep -E -x "$uuid_re" >/dev/null || { udid=; lost_device "$name"; die "simctl create gave no UDID"; }
run_udid=$udid
echo "ios-sim: device $udid ($type_id, $rt_name)"
udids "$out/devices.before" > "${tmp:?}/udids.before"
if grep -F -x -e "$run_udid" "${tmp:?}/udids.before" >/dev/null; then
	udid=
	die "the device $run_udid that simctl create gave was already listed before simctl create: not this run's, left alone"
fi
list_devices "$out/devices.created" || die "no device list after simctl create"
udids "$out/devices.created" > "${tmp:?}/udids.created"
grep -F -x -e "$run_udid" "${tmp:?}/udids.created" >/dev/null || die "this run's device $run_udid is not in the device list taken after simctl create"
bounded "$boot_s" "$out/boot.log" xcrun simctl bootstatus "$udid" -b || die "the device did not boot (log: $out/boot.log)"
bounded 60 "${tmp:?}/shot.out" xcrun simctl io "$udid" screenshot "$out/screen.png" || die "simctl io screenshot failed"
bounded 180 "${tmp:?}/install.out" xcrun simctl install "$udid" "$app" || { cat "${tmp:?}/install.out" >&2; die "simctl install failed"; }

# ---- the run ---------------------------------------------------------------

# data_container: the app's data container into $container (fail closed)
data_container()
{
	bounded 60 "${tmp:?}/container.out" xcrun simctl get_app_container "$udid" "$bid" data || die "simctl get_app_container failed"
	container=$(tail -n 1 "${tmp:?}/container.out")
	[ -d "$container" ] || die "no data container: $container"
}

# launch_app <limit seconds> <what the limit is> <shot seconds or empty>
# <n> <engine argument>...: one start of the app, the engine's output in
# $out/console.log (console.<n>.log for run); waits for the app's PID to end
launch_app()
{
	local limit=$1 what=$2 shotat=$3 n=$4 log=$out/console.log t w=0
	shift 4
	[ "$mode" = run ] && log=$out/console.$n.log
	SIMCTL_CHILD_MTL_DEBUG_LAYER=1 xcrun simctl launch --console-pty "$udid" "$bid" "$@" > "$log" 2>&1 &
	lpid=$!
	apid=
	t=0
	while [ "$t" -lt 60 ]; do
		apid=$(sed -n "s/^$bid: \([0-9][0-9]*\).*/\1/p" "$log" | head -n 1)
		[ -n "$apid" ] && break
		kill -0 "$lpid" 2>/dev/null || break
		sleep 1
		t=$((t + 1))
	done
	[ -n "$apid" ] || { tail -n 5 "$log" >&2; die "the app did not start (log: $log)"; }
	echo "ios-sim: app $bid pid $apid"
	t=0
	while kill -0 "$apid" 2>/dev/null; do
		if [ -n "$shotat" ] && [ "$t" -ge "$shotat" ]; then
			bounded 60 "${tmp:?}/shot.out" xcrun simctl io "$udid" screenshot "$out/screen.$n.png" || die "simctl io screenshot failed"
			bounded 30 "${tmp:?}/term.out" xcrun simctl terminate "$udid" "$bid" || true
			break
		fi
		if [ "$t" -ge "$limit" ]; then
			bounded 30 "${tmp:?}/term.out" xcrun simctl terminate "$udid" "$bid" || true
			die "the app did not quit within $limit s ($what); terminated it"
		fi
		sleep 1
		t=$((t + 1))
	done
	while kill -0 "$lpid" 2>/dev/null && [ "$w" -lt 10 ]; do sleep 1; w=$((w + 1)); done
	if kill -0 "$lpid" 2>/dev/null; then
		echo "ios-sim: the launch client did not exit after the app; stopping it" >&2
		stop_pid "$lpid" || true
	else
		wait "$lpid" 2>/dev/null
	fi
	lpid=
}

# the owner's data goes into Documents/baseq2 of the app's data container,
# where a person puts it on a device, and nowhere else
if [ -n "$data" ]; then
	data_container
	docs=$container/Documents/baseq2
	mkdir -p "$docs" || die "cannot make $docs"
	for f in "$data"/baseq2/*.pak; do
		[ -f "$f" ] || continue
		cp "$f" "$docs/" || die "cannot copy $(basename "$f") into $docs"
	done
	for d in players video music; do
		[ -d "$data/baseq2/$d" ] || continue
		cp -R "$data/baseq2/$d" "$docs/" || die "cannot copy $d/ into $docs"
	done
fi

if [ "$mode" = run ]; then
	[ "${#launches[@]}" -gt 0 ] || launches=("")
	n=0
	for l in "${launches[@]}"; do
		n=$((n + 1))
		# no write-directory option: the engine finds Documents by itself
		# shellcheck disable=SC2086
		launch_app "$run_s" "IOS_SIM_RUN_SECONDS" "$shot" "$n" +set vid_renderer metal +set logfile 2 $l
		data_container
		docs=$container/Documents/baseq2
		[ -d "$docs" ] || die "no Documents/baseq2 in the data container after start $n"
		( cd "$docs" && find . -type f | sort | while read -r f; do printf '%s %s\n' "$(wc -c < "$f" | tr -d ' ')" "$f"; done ) > "$out/documents.$n.list" || die "cannot list $docs"
		[ -f "$docs/qconsole.log" ] || die "the engine wrote no qconsole.log in Documents/baseq2 after start $n"
		cp "$docs/qconsole.log" "$out/engine.$n.log"
		if [ -d "$docs/scrnshot" ]; then
			mkdir -p "$out/frames.$n" && cp "$docs/scrnshot/"* "$out/frames.$n/" 2>/dev/null
		fi
	done
	bounded 60 "${tmp:?}/shutdown.out" xcrun simctl shutdown "$udid" || true
	bounded 60 "${tmp:?}/delete.out" xcrun simctl delete "$udid" || die "could not delete the device $udid"
	udid=
	list_devices "$out/devices.after" || die "no device list after the run"
	udids "$out/devices.after" > "${tmp:?}/udids.after"
	gone=$(comm -23 "${tmp:?}/udids.before" "${tmp:?}/udids.after" | tr '\n' ' ')
	[ -z "$gone" ] || die "devices listed before the run are gone after the run: $gone"
	grep -F -x -e "$run_udid" "${tmp:?}/udids.after" >/dev/null && die "this run's device $run_udid is still listed after its delete"
	echo "ios-sim: run ok, $n start(s) (artifacts in $out)"
	exit 0
fi

# The engine finds Documents/baseq2 by itself: no write-directory option.
args=(+set vid_renderer metal +set logfile 2 +set metal_stats 1)
for _ in $(seq 30); do args+=(+wait); done
args+=(+screenshot tga +quit)
launch_app "$smoke_s" "IOS_SIM_SMOKE_SECONDS" "" 0 "${args[@]}"

bounded 60 "${tmp:?}/container.out" xcrun simctl get_app_container "$udid" "$bid" data || die "simctl get_app_container failed"
container=$(tail -n 1 "${tmp:?}/container.out")
[ -d "$container" ] || die "no data container: $container"
log=$(find "$container" -name qconsole.log -type f | head -n 1)
[ -n "$log" ] || die "the engine wrote no qconsole.log in its data container"
cp "$log" "$out/engine.log"
tga=$(find "$container" -name 'q2_0000.tga' -type f | head -n 1)
[ -n "$tga" ] && cp "$tga" "$out/q2_0000.tga"

cleanup_device()
{
	bounded 60 "${tmp:?}/shutdown.out" xcrun simctl shutdown "$udid" || true
	bounded 60 "${tmp:?}/delete.out" xcrun simctl delete "$udid" || die "could not delete the device $udid"
	udid=
}
cleanup_device
list_devices "$out/devices.after" || die "no device list after the run"
udids "$out/devices.after" > "${tmp:?}/udids.after"

# ---- the smoke's decisions, from the artifacts --------------------------

fail=0
bad() { echo "ios-sim: smoke: $*"; fail=1; }
has() { grep -F -e "$1" "$out/engine.log" >/dev/null; }

gone=$(comm -23 "${tmp:?}/udids.before" "${tmp:?}/udids.after" | tr '\n' ' ')
[ -z "$gone" ] || bad "devices listed before the run are gone after the run: $gone"
grep -F -x -e "$run_udid" "${tmp:?}/udids.after" >/dev/null && bad "this run's device $run_udid is still listed after its delete"

has "Using the linked-in renderer: ref_metal (linked in)" || bad "engine.log does not show the linked-in ref_metal"
dev=$(grep -E '^Metal device: ' "$out/engine.log" | head -n 1)
fam=$(grep -E '^Metal supportsFamily ' "$out/engine.log" | head -n 1)
[ -n "$dev" ] || bad "engine.log has no Metal device line"
[ -n "$fam" ] || bad "engine.log has no Metal supportsFamily line"
case $fam in
*": none")
	has "is not checked in the simulator" \
		|| bad "the Metal device supports no Apple GPU family from apple5 on, and ref_metal refused it (its floor): the run stops at renderer init" ;;
esac
grep -E '^Wrote q2_0000\.tga$' "$out/engine.log" >/dev/null || bad "engine.log does not show Wrote q2_0000.tga"
size=$(sed -n 's/^Drawable size: \([0-9]*x[0-9]*\)$/\1/p' "$out/engine.log" | tail -n 1)
[ -n "$size" ] || bad "engine.log has no Drawable size line"
screen=$(python3 -I - "$out/screen.png" <<'EOF'
import struct, sys
b = open(sys.argv[1], "rb").read(32)
if b[:8] != b"\x89PNG\r\n\x1a\n" or b[12:16] != b"IHDR":
    sys.exit("not a PNG")
w, h = struct.unpack(">II", b[16:24])
print("%dx%d" % (max(w, h), min(w, h)))
EOF
) || bad "the simulator's screenshot is not a PNG"
[ -n "$size" ] && [ "$size" != "${screen:-}" ] && bad "Drawable size $size is not the screen's ${screen:-?} pixels (landscape)"
if [ -f "$out/q2_0000.tga" ]; then
	frame=$(python3 -I - "$out/q2_0000.tga" <<'EOF'
import struct, sys
b = open(sys.argv[1], "rb").read()
idlen, cmap, kind = b[0], b[1], b[2]
w, h, bpp = struct.unpack("<HHB", b[12:17])
if cmap or kind != 2 or bpp not in (24, 32):
    sys.exit("not an uncompressed true-colour TGA")
n = bpp // 8
px = b[18 + idlen:18 + idlen + w * h * n]
if len(px) != w * h * n:
    sys.exit("short TGA")
colours = len(set(px[i:i + 3] for i in range(0, len(px), n * 7)))
print("%dx%d %d" % (w, h, colours))
EOF
) || bad "q2_0000.tga cannot be read"
	fsize=${frame%% *}
	ncol=${frame##* }
	[ -n "$size" ] && [ "${fsize:-}" != "$size" ] && bad "the frame is ${fsize:-?}, not $size"
	[ "${ncol:-0}" -ge 2 ] || bad "the frame is one colour"
else
	bad "the engine wrote no q2_0000.tga in its data container"
fi
grep -F 'Metal API Validation Enabled' "$out/console.log" >/dev/null || bad "the app's output does not show Metal's validation layer on"
if grep -E -i 'failed assertion|MTLDebug|validation error' "$out/console.log" "$out/engine.log" > "${tmp:?}/val" 2>/dev/null; then
	bad "a Metal validation message: $(head -n 2 "${tmp:?}/val" | tr '\n' ' ')"
fi
stats=$(grep -E 'ref_metal: metal_stats: ' "$out/engine.log" | tail -n 1)
case $stats in
*" 0 failed command buffers"*) ;;
"") bad "engine.log has no metal_stats line" ;;
*) bad "failed command buffers: $stats" ;;
esac

printf '%s\n' "ios-sim: ${dev:-no Metal device}" "ios-sim: ${fam:-no family line}" "ios-sim: drawable ${size:-?}, screen ${screen:-?}, frame ${frame:-?}" \
	"ios-sim: devices before $(grep -c . "${tmp:?}/udids.before"), after create $(grep -c . "${tmp:?}/udids.created"), after $(grep -c . "${tmp:?}/udids.after")"
[ "$fail" -eq 0 ] || exit 1
echo "ios-sim: smoke ok (artifacts in $out)"
