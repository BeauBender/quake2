#!/bin/bash
# scripts/ios-sim.sh -- the iOS simulator app on a throwaway device (TASK146).
#
#   scripts/ios-sim.sh smoke --out DIR
#   scripts/ios-sim.sh run --out DIR [--data DIR | --no-data] [--launch "ARGS"]... [--shot SECONDS] [--background SECONDS]
#   scripts/ios-sim.sh selftest
#
# smoke: writes the synthetic data (scripts/ios/smokepak.py, no id Software
# content) to a scratch directory, builds "make ios IOS_BUNDLE_DATA=<it>"
# with IOS_ROOT=DIR/build, creates a device of its own with "simctl create"
# (IOS_SIM_TYPE, default "iPhone 17", on IOS_SIM_RUNTIME, default the newest
# available iOS runtime that lists that device type; on a version tie the
# one that lists the most types, and a runtime with no list counts as
# general; a version or name in IOS_SIM_RUNTIME is picked the same way
# among the runtimes it matches, a runtime that does not list the type is
# skipped, and a candidate with no numeric version is refused by name;
# IOS_SIM_SDK=appletvsimulator, TASK23, builds for the Apple TV simulator
# and picks among the tvOS runtimes, the default type "Apple TV 4K (4th
# generation)"; IOS_SIM_SDK=xrsimulator, TASK175, builds for the Apple
# Vision Pro simulator and picks among the visionOS (xrOS) runtimes, the
# default type "Apple Vision Pro"; any IOS_SIM_SDK but iphonesimulator or
# those is refused),
# boots it, installs the app and launches it with
# --console-pty and Metal's validation layer (SIMCTL_CHILD_MTL_DEBUG_LAYER=1)
# on "+set vid_renderer metal" and "+exec smoke.cfg" (written into the
# data: about 30 frames, "screenshot tga", 900 frames at the console, then
# "quit"): once the engine's log shows the frame written it takes the
# simulator's screenshot of the app (DIR/app.png) and waits for the app
# to quit by itself (TASK147; one that has not by the run's limit is
# terminated, and fails). Every wait is wall time. It then copies the
# engine's own log (qconsole.log, logfile 2) and frame from the app's data
# container to DIR/engine.log and DIR/q2_0000.tga and decides from those,
# from the simulator's own screenshot (its screen size) and from the
# device lists:
#   - the linked-in ref_metal, the Metal device and family lines (with
#     no family from apple5 on, ref_metal's line that the simulator does
#     not check that floor), a Drawable size of the screen in pixels
#     (landscape) and "Wrote q2_0000.tga" are in engine.log;
#   - the frame is that size and not one colour;
#   - the screen's safe area (TASK147): engine.log's "safe area" line
#     gives the window in pixels, which is the screen's size, and the
#     insets; "Drawable size:" is the window less the insets, the frame
#     is that size, and in DIR/app.png every pixel outside the safe
#     rectangle is black while the rectangle holds the console (not
#     black); and the rectangle, turned as the framebuffer is, is the
#     frame q2_0000.tga pixel for pixel, each channel within 8 (a colour
#     conversion), so a stretched, cropped or shifted present fails (the
#     smoke data's console is its background alone, scripts/ios/smokepak.py);
#   - the app ended through its own quit path (TASK147): engine.log has
#     Com_Quit's "shutting down" line and after it the renderer's close
#     (its metal_stats line); a kill or a crash leaves neither;
#   - at most one vid_restart (each loads the renderer again, so its
#     "refresher initialization" line is counted): a fresh window that is
#     measured before UIKit's first layout gets its insets in one later
#     SDL_EVENT_WINDOW_SAFE_AREA_CHANGED (safeAreaInsetsDidChange in
#     SDL_uikitview.m), which may restart the video once; more is a loop;
#   - Metal's validation layer was on (its line in the app's own output,
#     DIR/console.log), no validation message, and 0 failed command
#     buffers in ref_metal's metal_stats line;
#   - the device lists (DIR/devices.{before,created,after}.json, each
#     "simctl list devices -j"): this run's device is in the list taken
#     after "simctl create", not in the one before and not in the one
#     after. Other devices come and go (the owner's Xcode Previews
#     devices): one listed before the run and missing after it is a
#     warning naming its UDID, never a failure. This script's own
#     mutating simctl calls (shutdown, delete, erase, uninstall,
#     terminate, boot, bootstatus -b, install, launch, and the lookup of
#     a lost device) name only this run's
#     UDID, never one of the others. A list that fails, cannot be read or
#     names no device is a failure.
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
# with no argument, the app until the person quits it. Each word of ARGS
# is passed as it is (a * or ? matches no file). A start that ended on its
# own must show Com_Quit's "shutting down" line in its engine log
# (engine.<n>.log, the engine's own, not the launcher's output): without
# it the app was killed or crashed, and the run fails. --shot SECONDS:
# a simctl screenshot that long after each start (DIR/screen.<n>.png),
# then simctl terminate (that start needs no quit path; an app that ended
# before the shot says so and takes none). After each start DIR/engine.<n>.log is the
# engine's own log (qconsole.log), DIR/documents.<n>.list the files of
# Documents/baseq2/ with their sizes (names, never contents) and
# DIR/frames.<n>/ its scrnshot/ files. With --data these hold the
# owner's data: DIR is the artifacts folder, never published.
# --background SECONDS (TASK148): simctl has no Home button, so a start of
# the app is sent to the background the way a person leaves it: another
# app (com.apple.Preferences) is launched IOS_SIM_BACKGROUND_AFTER (15)
# seconds after the start, the script waits SECONDS of wall time, and
# launches the app again, which brings the running process back to the
# front. --shot then counts from that return. What the app saw is in its
# own log (engine.<n>.log), not in this script's output; with --background
# the shot is always taken (0 s after the return when none is given), and
# DIR/oslog.<n>.txt is the system's log of the app (simctl spawn log show).
#
# selftest: runs "smoke" against a fake xcrun and a fake make (no
# simulator, no build, seconds) and checks its rules: a hung "simctl
# list" is timed out, a failed or empty device list fails, another device
# appearing meanwhile passes, a device from before that disappears warns and passes,
# a mutating simctl call naming any UDID but this run's fails,
# this run's device left behind fails, the device of a create that timed
# out is deleted, a create whose UDID is not listed after it, or was
# listed before it, fails (and leaves that device alone), ref_metal's
# floor refusal fails, a drawable that is not the window less the insets
# fails, a screenshot with a lit pixel outside the safe rectangle fails,
# one with nothing inside it fails, a stretched, cropped or shifted one
# fails and one within the colour tolerance passes, the turn that fits is
# found when the other has nothing lit inside, a hung get_app_container is
# timed out within the run's limit, an app killed, crashed, not quitting or
# not closing its renderer fails, one vid_restart passes and two fail, and a
# limit that is not digits is refused; for run:
# the synthetic data without --data, the pak and the three folders of
# --data copied into Documents/baseq2 and nothing else (not a stray file,
# not another folder, not the bundle), a --data without baseq2 or without
# a pak refused before a device is made, a container that cannot be found
# fails and still deletes the device, an app that outlives
# IOS_SIM_RUN_SECONDS is terminated and fails, each --launch is a start
# with its own log and listing, --shot ends the app and keeps the frame,
# --background launches the other app and the app again, in that order; a
# --launch word is not glob-expanded; a start that ended on its own with no
# quit path in its log fails, one the shot ended does not, and an app that
# ended before the shot says so; the runtime pick follows one rule, in the
# default and within a version or name given; a bad version names its
# runtime and a runtime that cannot be a candidate is skipped.
# tests/check-ios.sh runs it.
#
# Limits, in seconds (digits only, or the script refuses):
# IOS_SIM_BOOT_SECONDS (300), IOS_SIM_SMOKE_SECONDS (180, the app's run),
# IOS_SIM_LIST_SECONDS (60, each "simctl list"), IOS_SIM_CREATE_SECONDS
# (120), IOS_SIM_RUN_SECONDS (3600, each start of "run").
#
# The launcher (TASK180, both modes, every SDK): before the device is deleted
# the script prints one line about the device's launcher process (SpringBoard
# on iOS, PineBoard on tvOS, SurfBoard on visionOS), read from the device's own launchd
# ("simctl spawn <udid> launchctl list", this run's UDID only): a baseline
# after the boot against the same read at the end.
#   launcher: no crash
#   launcher: <n> crash(es): <process>, pid <old> -> <new>, last exit status <s>, seen at <time>
#   launcher: unknown (<reason>)
# A crash, and an unknown (the device cannot be queried, the baseline could
# not be read, the launcher is not listed or not running at the start), fail
# the run, as bad as a missing frame: an unknown is never a pass.
# IOS_SIM_LAUNCHER_CRASH_OK=1 reports the line and lets both pass. The count
# is 1: launchd keeps the last exit only. A run that stops early prints the
# line too, and the device is shut down and deleted on every path.
# IOS_SIM_DISPLAY (TASK172): the display each "simctl io screenshot" takes,
# passed as --display. A device with two integrated displays (the iPhone
# Duo: "primary", the outer, and "primary-1", the inner) has no default one
# and simctl refuses a screenshot without it. Unset, no --display is given.
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
	echo "       scripts/ios-sim.sh run --out DIR [--data DIR | --no-data] [--launch \"ARGS\"]... [--shot SECONDS] [--background SECONDS]" >&2
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
	devicetypes)
		i='{"name": "iPhone 17", "identifier": "com.apple.CoreSimulator.SimDeviceType.iPhone-17"}'
		tv='{"name": "Apple TV 4K (4th generation)", "identifier": "com.apple.CoreSimulator.SimDeviceType.Apple-TV-4K-4th-generation"}'
		# simctl's two Apple Vision Pro types (TASK175): the name is matched whole, so the default
		# "Apple Vision Pro" is the 4K one, which the visionOS runtime lists, not the 2732x2048 one first
		xr='{"name": "Apple Vision Pro (at 2732x2048)", "identifier": "com.apple.CoreSimulator.SimDeviceType.Apple-Vision-Pro"}, {"name": "Apple Vision Pro", "identifier": "com.apple.CoreSimulator.SimDeviceType.Apple-Vision-Pro-4K"}'
		case $sc in
		tvos|tvtypes) echo "{\"devicetypes\": [$i, $tv]}" ;;
		xros|xrtypes) echo "{\"devicetypes\": [$i, $xr]}" ;;
		*) echo "{\"devicetypes\": [$i]}" ;;
		esac ;;
	runtimes)
		g='{"isAvailable": true, "platform": "iOS", "version": "27.2", "name": "iOS 27.2", "identifier": "com.apple.CoreSimulator.SimRuntime.iOS-27-2", "supportedDeviceTypes": [{"identifier": "com.apple.CoreSimulator.SimDeviceType.iPhone-17"}, {"identifier": "com.apple.CoreSimulator.SimDeviceType.iPad-Pro"}]}'
		b='{"isAvailable": true, "platform": "iOS", "version": "%s", "name": "iOS %s bound", "identifier": "com.apple.CoreSimulator.SimRuntime.iOS-bound-%s", "supportedDeviceTypes": [{"identifier": "com.apple.CoreSimulator.SimDeviceType.iPhone19-4"}]}'
		case $sc in
		rttie) echo "{\"runtimes\": [$(printf "$b" 27.2 27.2 27.2), $g]}" ;;
		rtnewer) echo "{\"runtimes\": [$(printf "$b" 27.10 27.10 27.10), $g]}" ;;
		rttielist) echo "{\"runtimes\": [$(printf "$b" 27.2 27.2 27.2 | sed 's/iPhone19-4/iPhone-17/'), $g]}" ;;
		rtnolist) echo "{\"runtimes\": [$(printf "$b" 27.2 27.2 27.2 | sed 's/iPhone19-4/iPhone-17/'), $(printf '%s' "$g" | sed 's/, "supportedDeviceTypes.*}$/}/')]}" ;;
		rtnum) echo "{\"runtimes\": [$(printf '%s' "$g" | sed 's/27\.2/27.10/g'), $(printf '%s' "$g" | sed 's/27\.2/27.9/g; s/iOS-27-2/iOS-27-9/')]}" ;;
		rtnone) echo "{\"runtimes\": [$(printf "$b" 27.2 27.2 27.2)]}" ;;
		rtempty) echo '{"runtimes": []}' ;;
		rtbadnon) echo "{\"runtimes\": [$(printf "$b" x x x), $g]}" ;;
		rtbadver) echo '{"runtimes": [{"isAvailable": true, "platform": "iOS", "version": "x", "name": "iOS x", "identifier": "com.apple.CoreSimulator.SimRuntime.iOS-x"}]}' ;;
		# the Apple TV runtime next to the iOS one (TASK23); tvtypes: the Apple TV type, only iOS runtimes
		tvos) echo "{\"runtimes\": [$g, {\"isAvailable\": true, \"platform\": \"tvOS\", \"version\": \"27.2\", \"name\": \"tvOS 27.2\", \"identifier\": \"com.apple.CoreSimulator.SimRuntime.tvOS-27-2\", \"supportedDeviceTypes\": [{\"identifier\": \"com.apple.CoreSimulator.SimDeviceType.Apple-TV-4K-4th-generation\"}]}]}" ;;
		tvtypes) echo "{\"runtimes\": [$g]}" ;;
		# the visionOS runtime next to the iOS one (TASK175; its platform is "xrOS"); xrtypes: the
		# Apple Vision Pro type, only iOS runtimes
		xros) echo "{\"runtimes\": [$g, {\"isAvailable\": true, \"platform\": \"xrOS\", \"version\": \"27.2\", \"name\": \"visionOS 27.2\", \"identifier\": \"com.apple.CoreSimulator.SimRuntime.xrOS-27-2\", \"supportedDeviceTypes\": [{\"identifier\": \"com.apple.CoreSimulator.SimDeviceType.Apple-Vision-Pro-4K\"}]}]}" ;;
		xrtypes) echo "{\"runtimes\": [$g]}" ;;
		*) echo '{"runtimes": [{"isAvailable": true, "platform": "iOS", "version": "27.2", "name": "iOS 27.2", "identifier": "com.apple.CoreSimulator.SimRuntime.iOS-27-2", "supportedDeviceTypes": [{"identifier": "com.apple.CoreSimulator.SimDeviceType.iPhone-17"}]}]}' ;;
		esac ;;
	*) exit 1 ;;
	esac ;;
create)
	u=$(printf 'AAAAAAAA-0000-0000-0000-%012d' $$)
	# the UDID this create hands out is this run's: the case checks every
	# mutating call against it
	case $sc in
	ghost) u=BBBBBBBB-0000-0000-0000-000000000000 ;;
	reuse) u=11111111-1111-1111-1111-111111111111 ;;
	esac
	echo "$u" > "$st/created"
	case $sc in
	ghost|reuse) echo "$u"; exit 0 ;;
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
terminate)
	# only the process this fake started: the PID in app.pid still runs the command recorded in app.cmd
	# (a PID that is gone may belong to anyone by now)
	if [ -f "$st/app.pid" ] && [ -f "$st/app.cmd" ] && [ "$(ps -o command= -p "$(cat "$st/app.pid")" 2>/dev/null)" = "$(cat "$st/app.cmd")" ]; then
		kill "$(cat "$st/app.pid")" 2>/dev/null
	fi
	echo "$*" >> "$st/terminates"; exit 0 ;;
bootstatus|install|shutdown) ;;
spawn)
	# simctl spawn <udid> launchctl list (TASK180): the device's launchd, "PID Status Label" per job, of
	# this run's device only; FAKE_LAUNCHER picks the outcome: the first call is the baseline, later
	# ones are the check: crash (a new pid, last exit status -11), crashdead (no pid, -11), crashstatus
	# (the same pid, a new status), qfail (the check cannot query), qfailbase (the baseline cannot),
	# nolabel (the launcher is not listed), garbage (a line that cannot be read), norun (not running
	# at the baseline)
	if [ "${3:-}" = launchctl ] && [ "${4:-}" = list ]; then
		[ "${2:-}" = "$(cat "$st/created" 2>/dev/null)" ] || { echo "fake xcrun: not the device this run created" >&2; exit 1; }
		n=$(cat "$st/lcalls" 2>/dev/null || echo 0); n=$((n + 1)); echo "$n" > "$st/lcalls"
		lab=com.apple.SpringBoard
		[ "${IOS_SIM_SDK:-}" = appletvsimulator ] && lab=com.apple.PineBoard
		[ "${IOS_SIM_SDK:-}" = xrsimulator ] && lab=com.apple.SurfBoard
		lp=100; ls=0
		case ${FAKE_LAUNCHER:-ok} in
		crash) [ "$n" -gt 1 ] && { lp=200; ls=-11; } ;;
		crashdead) [ "$n" -gt 1 ] && { lp=-; ls=-11; } ;;
		crashstatus) [ "$n" -gt 1 ] && ls=-11 ;;
		qfail) [ "$n" -gt 1 ] && { echo "fake xcrun: the device is not booted" >&2; exit 1; } ;;
		qfailbase) [ "$n" -eq 1 ] && { echo "fake xcrun: the device is not booted" >&2; exit 1; } ;;
		nolabel) lab=com.apple.other ;;
		garbage) lp=x ;;
		norun) [ "$n" -eq 1 ] && lp=- ;;
		esac
		printf 'PID\tStatus\tLabel\n-\t0\tcom.apple.Board.other\n%s\t%s\t%s\n' "$lp" "$ls" "$lab"
		exit 0
	fi
	echo "fake log: quake2 line" ;;
io)
		case $4 in
		*/app.png)
			# the app's screenshot (fakepix.py): the frame in the safe rectangle, unless the scenario says otherwise
			python3 -I "$(dirname "$0")/fakepix.py" png "$4" "$sc" ;;
		*)
			# io <udid> screenshot <png>: the head of a 16x8 PNG
			printf '\211PNG\r\n\032\n\000\000\000\015IHDR\000\000\000\020\000\000\000\010' > "$4" ;;
		esac ;;
launch)
	# launch <udid> <bundle id>: another app, or the running app brought back (no --console-pty)
	if [ "${2:-}" != --console-pty ]; then
		printf '%s\n' "$*" >> "$st/relaunches"
		[ "$sc" = bgfail ] && [ "${3:-}" = com.apple.Preferences ] && exit 1
		exit 0
	fi
	# launch --console-pty <udid> <bundle id> <args>...
	printf '%s\n' "$*" >> "$st/launches"
	c=$st/container/Documents/baseq2
	mkdir -p "$c/scrnshot"
	skip="ref_metal: the device floor (apple5) is not checked in the simulator"
		drawable=12x7
		[ "$sc" = wrongdrawable ] && drawable=16x8
		safeline="ref_metal: safe area: window 16x8 pixels, insets left 2 top 0 right 2 bottom 1"
		[ "$sc" = nosafeline ] && safeline="ref_metal: nothing"
	[ "$sc" = floorrefused ] && skip="ref_metal: the Metal device supports no Apple GPU family from apple5 on; ref_metal needs one"
	loads=1
	[ "$sc" = restartonce ] && loads=2
	[ "$sc" = restartloop ] && loads=3
	{
		for _ in $(seq "$loads"); do
			printf '%s\n' "----- refresher initialization -----" "Using the linked-in renderer: ref_metal (linked in)" \
				"Metal device: Fake GPU" "Metal supportsFamily (apple5 to apple9): none" "$skip" "$safeline" "Drawable size: $drawable"
		done
		printf '%s\n' "Wrote q2_0000.tga" "ref_metal: metal_stats: 30 frames, 0 without a drawable, 0 skipped, 0 failed command buffers, "
		# the app's own quit path (Com_Quit, then the renderer's close), unless it was killed, crashed or stays
		case $sc in
		killed|appstays) ;;
		crashed) printf '%s\n' "Received signal 11, exiting..." ;;
		noclose) printf '%s\n' "" "----------- shutting down ----------" ;;
		*) printf '%s\n' "" "----------- shutting down ----------" \
			"ref_metal: metal_stats: 31 frames, 0 without a drawable, 0 skipped, 0 failed command buffers, " ;;
		esac
	} > "$c/qconsole.log"
	# no game data: the engine writes only the missing-data text, shows the alert and ends (VID_CheckGameData, Sys_Quit)
	if [ "$sc" = nodata ]; then
		printf '%s\n' "Quake II needs its game data. Copy pak0.pak from the baseq2 folder of Quake II into Files > On My iPhone (or iPad) > Quake II > baseq2, then start the game again." > "$c/qconsole.log"
	fi
	python3 -I "$(dirname "$0")/fakepix.py" tga "$c/scrnshot/q2_0000.tga" "$sc"
	[ "$sc" = launchterm ] && trap "" TERM
	# an app that stays at the console: appstays, and containerhang (the frame wait must end at its limit, not on the app's end);
	# the launch client is the app (--console-pty), so a terminate ends it
	if [ "$sc" = appstays ] || [ "$sc" = containerhang ]; then
		echo "Metal API Validation Enabled"
		echo $$ > "$st/app.pid"
		echo "sleep 30.$$" > "$st/app.cmd"
		exec sleep "30.$$"
	fi
	# a run with --background (IOS_SIM_BACKGROUND_AFTER is set) keeps the app alive through the background;
	# bgdies is the app that ends during it
	life=1
	[ -n "${IOS_SIM_BACKGROUND_AFTER-}" ] && life=4
	[ "$sc" = bgshot ] && life=5
	[ "$sc" = bgdies ] && life=1
	sleep "$life.$$" &
	p=$!
	echo $p > "$st/app.pid"
	echo "sleep $life.$$" > "$st/app.cmd"
	echo "$4: $p"
	echo "Metal API Validation Enabled"
	wait "$p"
	# a launch client that ignores TERM and outlives the app
	[ "$sc" = launchterm ] && { echo $$ > "$st/hang.pid"; exec sleep 60; }
	exit 0 ;;
get_app_container)
	[ "$sc" = nocontainer ] && { echo "fake xcrun: no such container" >&2; exit 1; }
	# containerhang: the first call hangs
	if [ "$sc" = containerhang ] && [ ! -f "$st/hung" ]; then
		: > "$st/hung"
		echo $$ > "$st/hang.pid"
		exec sleep 30
	fi
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
for a; do case $a in IOS_BUNDLE_DATA=*) cp -R "${a#IOS_BUNDLE_DATA=}/." "$r/Quake2.app/" || exit 1 ;; esac; done
printf '#!/bin/sh\nexit 0\n' > "$r/Quake2.app/quake2" && chmod +x "$r/Quake2.app/quake2" || exit 1
printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
	'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	'<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>org.example.selftest</string></dict></plist>' \
	> "$r/Quake2.app/Info.plist"
EOF
	cat > "${sroot:?}/bin/fakepix.py" <<'EOF'
import struct, sys, zlib
# fakepix.py tga|png <path> <scenario>: the engine's frame (q2_0000.tga) and the simulator's
# screenshot of the app (app.png) of "scripts/ios-sim.sh selftest": a 16x8 window whose safe
# rectangle (insets 2 left, 0 top, 2 right, 1 bottom) is 12x7, the frame's size
mode, path, sc = sys.argv[1:4]
sw, sh, L, T, R, B = 16, 8, 2, 0, 2, 1
fw, fh = sw - L - R, sh - T - B


def frame(x, y):
    """the frame: every pixel lit and of its own colour; with ccwtoprow only its top row"""
    if not (0 <= x < fw and 0 <= y < fh) or (sc == "ccwtoprow" and y != 0):
        return (0, 0, 0)
    return (40 + 17 * x, 40 + 23 * y, 40 + (7 * x + 11 * y) % 150)


def screen(x, y):
    """the window, landscape: the frame at the safe rectangle's place, black around it, unless the scenario says otherwise"""
    if sc == "litoutside" and (x, y) == (0, 0):
        return (200, 200, 200)
    if sc == "blackapp" or not (L <= x < sw - R and T <= y < sh - B):
        return (0, 0, 0)
    fx, fy = x - L, y - T
    if sc == "stretched":
        fx = fx * (fw - 2) // fw        # the frame shown wider: its right two columns lost
    elif sc == "cropped" and (fx >= fw - 2 or fy < 1):
        return (0, 0, 0)                # its right two columns and its top row not copied
    elif sc == "shifted":
        fx -= 1                         # one column to the right
    c = frame(fx, fy)
    if sc == "colourshift" and c != (0, 0, 0):
        c = tuple(min(255, v + 3) for v in c)   # a colour conversion, within the tolerance
    return c


if mode == "tga":
    # as VID_WriteTGA writes it: uncompressed, 24 bits, rows top down
    px = bytearray()
    for y in range(fh):
        for x in range(fw):
            r, g, b = frame(x, y)
            px += bytes((b, g, r))
    open(path, "wb").write(bytes([0, 0, 2] + [0] * 9) + struct.pack("<HHBB", fw, fh, 24, 0x20) + bytes(px))
elif mode == "png":
    # portrait and ccwtoprow: the framebuffer stays portrait with the app turned in it (clockwise, counter-clockwise)
    w, h = (sh, sw) if sc in ("portrait", "ccwtoprow") else (sw, sh)
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            if sc == "portrait":
                c = screen(y, sh - 1 - x)
            elif sc == "ccwtoprow":
                c = screen(sw - 1 - y, x)
            else:
                c = screen(x, y)
            row += bytes(c) + b"\xff"
        rows.append(b"\0" + bytes(row))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d))
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b""))
else:
    sys.exit("fakepix: tga or png")
EOF
	chmod +x "${sroot:?}/bin/xcrun" "${sroot:?}/bin/make" || die "cannot make the fakes executable"

	nfail=0
	ncase=0
	# <name> <scenario> <ok|fail> <text the output must hold> <max seconds> [VAR=value]...
	st_case()
	{
		local name=$1 sc=$2 want=$3 text=$4 max=$5 d rc=0 t0 why= other=
		shift 5
		ncase=$((ncase + 1))
		d=$sroot/$name
		mkdir -p "${d:?}/state" || die "cannot create $d"
		printf '%s\n' "11111111-1111-1111-1111-111111111111 Owner iPhone" \
			"22222222-2222-2222-2222-222222222222 Owner iPad" > "${d:?}/state/devices"
		t0=$(date +%s)
		cmd=${ST_CMD:-smoke --out @D@/out}; cmd=${cmd//@D@/$d}
		# ST_CWD: the directory the script runs in; no pathname expansion of the words of the command
		( set -f; cd "${ST_CWD:-.}" && bounded 60 "${d:?}/run.log" env -u IOS_SIM_SDK -u IOS_SIM_LAUNCHER_CRASH_OK -u FAKE_LAUNCHER PATH="$sroot/bin:$PATH" FAKE_STATE="$d/state" FAKE_SCENARIO="$sc" \
			IOS_SIM_SETTLE_SECONDS=0 IOS_SIM_LIST_SECONDS=20 IOS_SIM_CREATE_SECONDS=20 IOS_SIM_BOOT_SECONDS=10 IOS_SIM_SMOKE_SECONDS=10 "$@" \
			bash "$self" $cmd ) || rc=$?
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
		elif other=$(awk '$1 == "simctl" && ($2 == "shutdown" || $2 == "delete" || $2 == "erase" || $2 == "uninstall" || $2 == "terminate" || $2 == "boot" || $2 == "bootstatus" || $2 == "install" || $2 == "launch") { x = ($2 == "launch" && $3 ~ /^--/) ? $4 : $3; if (x != u) print $2 " " x }' u="$(cat "${d:?}/state/created" 2>/dev/null)" "${d:?}/state/calls" 2>/dev/null) && [ -n "$other" ]; then
			why="a mutating simctl call names a device that is not this run's: $other"
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
		line1) sed -n 1p "$f" 2>/dev/null | grep -F -e "$text" >/dev/null || why="$3's first line does not hold \"$text\"" ;;
		line2) sed -n 2p "$f" 2>/dev/null | grep -F -e "$text" >/dev/null || why="$3's second line does not hold \"$text\"" ;;
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
	ST_CMD="run --out @D@/out --no-data --data /x" st_case run-no-data-and-data extra fail "usage: scripts/ios-sim.sh" 5
	# --background: the other app, then the app again, in that order, once each
	ST_CMD="run --out @D@/out --no-data --background 1 --shot 0" st_case run-background extra ok "run ok, 1 start(s)" 30 IOS_SIM_BACKGROUND_AFTER=0
	st_has run-background grep state/relaunches "com.apple.Preferences"
	st_has run-background file out/screen.1.png
	st_has run-background grep out/oslog.1.txt "fake log: quake2 line"
	ST_CMD="run --out @D@/out --no-data --background 1" st_case run-background-default-shot extra ok "run ok, 1 start(s)" 30 IOS_SIM_BACKGROUND_AFTER=0
	st_has run-background-default-shot file out/oslog.1.txt
	ST_CMD="run --out @D@/out --no-data --background 1 --shot 0" st_case run-background-order extra ok "run ok, 1 start(s)" 30 IOS_SIM_BACKGROUND_AFTER=0
	st_has run-background-order line1 state/relaunches "com.apple.Preferences"
	st_has run-background-order line2 state/relaunches "org.example.selftest"
	# the shot is after the return, not before the background (the app lives 5 s here)
	ST_CMD="run --out @D@/out --no-data --background 1 --shot 0" st_case run-background-before-shot bgshot ok "run ok, 1 start(s)" 30 IOS_SIM_BACKGROUND_AFTER=2
	st_has run-background-before-shot grep state/relaunches "com.apple.Preferences"
	ST_CMD="run --out @D@/out --no-data --background 1 --shot 0" st_case run-background-fails bgfail fail "simctl launch of the other app failed" 30 IOS_SIM_BACKGROUND_AFTER=0
	# the app ends during the background: the return is a restart, the run fails before any shot
	ST_CMD="run --out @D@/out --no-data --background 2 --shot 0" st_case run-background-app-dies bgdies fail "the app did not survive the background" 30 IOS_SIM_BACKGROUND_AFTER=0
	st_has run-background-app-dies nofile out/screen.1.png
	ST_CMD="run --out @D@/out --no-data" st_case run-no-background extra ok "run ok, 1 start(s)" 30
	st_has run-no-background nofile state/relaunches
	ST_CMD="run --out @D@/out --background 3s" st_case run-background-not-digits extra fail "--background must be a number of seconds" 5
	ST_CMD="run --out @D@/out --no-data --background 1" st_case run-background-after-not-digits extra fail "IOS_SIM_BACKGROUND_AFTER must be" 5 IOS_SIM_BACKGROUND_AFTER=x
	ST_CMD="run --out @D@/out --shot 3s" st_case run-shot-not-digits extra fail "--shot must be a number of seconds" 5
	ST_CMD="run --out @D@/out" st_case run-limit-not-digits extra fail "IOS_SIM_RUN_SECONDS must be" 5 IOS_SIM_RUN_SECONDS=1h
	# a --launch word is split at spaces and never matched against the files of the directory the script runs in
	mkdir -p "$sroot/glob-cwd" && : > "$sroot/glob-cwd/+xfile"
	ST_CWD="$sroot/glob-cwd" ST_CMD="run --out @D@/out --no-data --launch +x*" st_case run-launch-no-glob extra ok "run ok, 1 start(s)" 30
	st_has run-launch-no-glob grep state/launches "+x*"
	st_has run-launch-no-glob nogrep state/launches "+xfile"
	# a start that ended on its own must show the engine's own quit path in its log, or it was killed or it crashed
	ST_CMD="run --out @D@/out --no-data" st_case run-app-killed killed fail "does not show its own quit path" 30
	# no game data: the app writes only the missing-data text and quits; that is its own quit path
	ST_CMD="run --out @D@/out --no-data" st_case run-missing-data-quits nodata ok "run ok, 1 start(s)" 30
	ST_CMD="run --out @D@/out --no-data" st_case run-app-crashed crashed fail "does not show its own quit path" 30
	ST_CMD="run --out @D@/out --no-data --launch +a --launch +b" st_case run-second-start-killed killed fail "start 1: the app ended and engine.1.log does not show" 30
	# with --shot the script ends the app itself: no quit path is due; an app that ended before the shot is said so
	ST_CMD="run --out @D@/out --no-data --shot 0" st_case run-shot-ends-app-no-quit-path killed ok "run ok, 1 start(s)" 30
	st_has run-shot-ends-app-no-quit-path file out/screen.1.png
	ST_CMD="run --out @D@/out --no-data --shot 6" st_case run-ended-before-shot extra ok "the app ended before the screenshot" 30
	st_has run-ended-before-shot nofile out/screen.1.png
	ST_CMD="run --out @D@/out --no-data --shot 6" st_case run-killed-before-shot killed fail "does not show its own quit path" 30
	# the fake's terminate kills only the process it started: a PID in app.pid that now runs something else survives
	ncase=$((ncase + 1))
	mkdir -p "$sroot/fake-terminate-stale/state"
	sleep 30 &
	spid=$!
	printf '%s\n' "$spid" > "$sroot/fake-terminate-stale/state/app.pid"
	printf '%s\n' "sleep 31.1" > "$sroot/fake-terminate-stale/state/app.cmd"
	FAKE_STATE="$sroot/fake-terminate-stale/state" "$sroot/bin/xcrun" simctl terminate AAAAAAAA-0000-0000-0000-000000000001 com.example
	# a kill and its reap are asynchronous: give a wrongly sent signal up to 3 s to show before deciding
	i=0
	while kill -0 "$spid" 2>/dev/null && [ "$i" -lt 6 ]; do
		sleep 0.5
		i=$((i + 1))
	done
	if kill -0 "$spid" 2>/dev/null; then
		echo "ios-sim: selftest: ok fake-terminate-stale"
	else
		echo "ios-sim: selftest: FAIL fake-terminate-stale: the fake's terminate killed a PID that is not its app"
		nfail=$((nfail + 1))
	fi
	kill "$spid" 2>/dev/null; wait "$spid" 2>/dev/null
	# the runtime pick: the newest runtime that lists the device type, the general one on a tie
	st_case runtime-tie-picks-general rttie ok "iOS 27.2)" 30
	st_case runtime-newer-bound-picks-general rtnewer ok "iOS 27.2)" 30
	st_case runtime-tie-both-list-picks-most rttielist ok "iOS 27.2)" 30
	st_case runtime-tie-unlisted-picks-unlisted rtnolist ok "iOS 27.2)" 30
	st_case runtime-version-numeric rtnum ok "iOS 27.10)" 30
	st_case runtime-none-lists-type rtnone fail "iPhone 17 is not supported by any available iOS runtime; tried iOS 27.2 bound (com.apple.CoreSimulator.SimRuntime.iOS-bound-27.2)" 30
	st_case runtime-list-empty rtempty fail "no available iOS runtime" 30
	st_case runtime-bad-version rtbadver fail "the iOS runtime iOS x (com.apple.CoreSimulator.SimRuntime.iOS-x) has no numeric version" 30
	st_case runtime-bad-version-not-candidate-skipped rtbadnon ok "iOS 27.2)" 30
	st_case runtime-set-version-tie-picks-general rttie ok "iOS 27.2)" 30 IOS_SIM_RUNTIME=27.2
	st_case runtime-set-name-picks-general rttie ok "iOS 27.2)" 30 "IOS_SIM_RUNTIME=iOS 27.2"
	st_case runtime-set-version-tie-both-list-picks-most rttielist ok "iOS 27.2)" 30 IOS_SIM_RUNTIME=27.2
	st_case runtime-set-bound-refused rttie fail "iPhone 17 is not supported by iOS 27.2 bound" 30 IOS_SIM_RUNTIME=com.apple.CoreSimulator.SimRuntime.iOS-bound-27.2
	st_case runtime-set-general rttie ok "iOS 27.2)" 30 IOS_SIM_RUNTIME=com.apple.CoreSimulator.SimRuntime.iOS-27-2
	# the Apple TV build (TASK23): IOS_SIM_SDK=appletvsimulator builds for that SDK and picks the
	# tvOS runtime and the Apple TV type; unset, the same lists give the iPhone on iOS
	st_case tvos-picks-tvos-runtime tvos ok "(com.apple.CoreSimulator.SimDeviceType.Apple-TV-4K-4th-generation, tvOS 27.2)" 30 IOS_SIM_SDK=appletvsimulator
	st_has tvos-picks-tvos-runtime grep state/calls "com.apple.CoreSimulator.SimRuntime.tvOS-27-2"
	st_has tvos-picks-tvos-runtime grep out/build/make.args " IOS_SDK=appletvsimulator "
	st_case tvos-lists-default-is-iphone tvos ok "(com.apple.CoreSimulator.SimDeviceType.iPhone-17, iOS 27.2)" 30
	st_has tvos-lists-default-is-iphone grep out/build/make.args " IOS_SDK=iphonesimulator "
	st_case tvos-without-tvos-runtime tvtypes fail "no available iOS runtime" 30 IOS_SIM_SDK=appletvsimulator
	# the Apple Vision Pro build (TASK175): IOS_SIM_SDK=xrsimulator builds for that SDK and picks
	# the visionOS (xrOS) runtime and the Apple Vision Pro type; unset, the same lists give the
	# iPhone on iOS; the tvOS build on them finds no Apple TV type
	st_case xros-picks-xros-runtime xros ok "(com.apple.CoreSimulator.SimDeviceType.Apple-Vision-Pro-4K, visionOS 27.2)" 30 IOS_SIM_SDK=xrsimulator
	st_has xros-picks-xros-runtime grep state/calls "com.apple.CoreSimulator.SimRuntime.xrOS-27-2"
	st_has xros-picks-xros-runtime grep out/build/make.args " IOS_SDK=xrsimulator "
	st_case xros-lists-default-is-iphone xros ok "(com.apple.CoreSimulator.SimDeviceType.iPhone-17, iOS 27.2)" 30
	st_has xros-lists-default-is-iphone grep out/build/make.args " IOS_SDK=iphonesimulator "
	st_case xros-lists-tvos-no-type xros fail "no device type Apple TV 4K (4th generation)" 30 IOS_SIM_SDK=appletvsimulator
	st_case xros-without-xros-runtime xrtypes fail "no available iOS runtime" 30 IOS_SIM_SDK=xrsimulator
	st_case sdk-iphoneos-refused extra fail "IOS_SIM_SDK must be iphonesimulator (the default), appletvsimulator or xrsimulator" 5 IOS_SIM_SDK=iphoneos
	st_has sdk-iphoneos-refused line1 run.log "IOS_SIM_SDK must be"
	st_has sdk-iphoneos-refused nofile out/build/make.args
	st_case sdk-xros-refused extra fail "IOS_SIM_SDK must be iphonesimulator (the default), appletvsimulator or xrsimulator" 5 IOS_SIM_SDK=xros
	st_has sdk-xros-refused nofile out/build/make.args
	st_case sdk-shell-text-refused extra fail "IOS_SIM_SDK must be iphonesimulator (the default), appletvsimulator or xrsimulator" 5 'IOS_SIM_SDK=$(touch sdk-ran)'
	st_has sdk-shell-text-refused line1 run.log "IOS_SIM_SDK must be"
	st_has sdk-shell-text-refused nofile out/build/make.args
	st_case list-empty empty fail "simctl list devices: no device list" 30
	st_case list-fails listfail fail "simctl list devices failed" 30
	st_case list-hangs hang fail "timed out after 2 s: xcrun simctl list devices" 10 IOS_SIM_LIST_SECONDS=2
	st_case device-from-before-gone vanish ok "not a failure): 22222222-2222-2222-2222-222222222222" 30
	ST_CMD="run --out @D@/out" st_case run-device-from-before-gone vanish ok "not a failure): 22222222-2222-2222-2222-222222222222" 30
	# the launcher of the device (TASK180): one line before the device is deleted, from the device's own
	# launchd (simctl spawn on this run's UDID), for each SDK: no crash, a crash, a query that fails
	for sdk in iphonesimulator:extra:SpringBoard appletvsimulator:tvos:PineBoard xrsimulator:xros:SurfBoard; do
		l_sdk=${sdk%%:*}; l_rest=${sdk#*:}; l_sc=${l_rest%%:*}; l_lab=${l_rest#*:}; l_n=${l_sdk%simulator}
		st_case launcher-no-crash-$l_n "$l_sc" ok "launcher: no crash" 30 IOS_SIM_SDK="$l_sdk"
		st_case launcher-crash-$l_n "$l_sc" fail "launcher: 1 crash(es): com.apple.$l_lab, pid 100 -> 200, last exit status -11" 30 IOS_SIM_SDK="$l_sdk" FAKE_LAUNCHER=crash
		st_has launcher-crash-$l_n grep state/calls "simctl delete AAAAAAAA-"
		st_case launcher-unknown-$l_n "$l_sc" fail "launcher: unknown (launchctl list failed in the device)" 30 IOS_SIM_SDK="$l_sdk" FAKE_LAUNCHER=qfail
	done
	# the same in run, and the device is shut down and deleted after a crash, on each path
	ST_CMD="run --out @D@/out" st_case launcher-run-no-crash extra ok "launcher: no crash" 30
	ST_CMD="run --out @D@/out" st_case launcher-run-crash extra fail "launcher: 1 crash(es): com.apple.SpringBoard" 30 FAKE_LAUNCHER=crash
	st_has launcher-run-crash grep state/calls "simctl shutdown AAAAAAAA-"
	st_has launcher-run-crash grep state/calls "simctl delete AAAAAAAA-"
	st_has launcher-run-crash nogrep out/devices.after.json "AAAAAAAA-"
	ST_CMD="run --out @D@/out" st_case launcher-run-unknown extra fail "launcher: unknown (the device's launchd lists no com.apple.SpringBoard)" 30 FAKE_LAUNCHER=nolabel
	# a crash by its other signs: no pid, or the same pid with a new status
	st_case launcher-crash-no-pid extra fail "pid 100 -> -, last exit status -11" 30 FAKE_LAUNCHER=crashdead
	st_case launcher-crash-new-status extra fail "launcher: 1 crash(es)" 30 FAKE_LAUNCHER=crashstatus
	# the baseline that cannot be read, a line that cannot be read, a launcher not running at the start
	st_case launcher-unknown-baseline extra fail "launcher: unknown (launchctl list failed in the device)" 30 FAKE_LAUNCHER=qfailbase
	st_case launcher-unknown-garbage extra fail "launcher: unknown (the device's launchd line for com.apple.SpringBoard cannot be read)" 30 FAKE_LAUNCHER=garbage
	st_case launcher-unknown-not-running extra fail "launcher: unknown (com.apple.SpringBoard was not running at the start)" 30 FAKE_LAUNCHER=norun
	# IOS_SIM_LAUNCHER_CRASH_OK=1 lets a crash pass (the line stays), and an unknown too; nothing else does
	st_case launcher-crash-ok extra ok "launcher: 1 crash(es)" 30 FAKE_LAUNCHER=crash IOS_SIM_LAUNCHER_CRASH_OK=1
	st_case launcher-unknown-ok extra ok "launcher: unknown (" 30 FAKE_LAUNCHER=qfail IOS_SIM_LAUNCHER_CRASH_OK=1
	st_case launcher-crash-ok-not-one extra fail "launcher: 1 crash(es)" 30 FAKE_LAUNCHER=crash IOS_SIM_LAUNCHER_CRASH_OK=yes
	# a run that dies on another failure after the boot still says what its launcher did and deletes its device
	st_case launcher-line-on-early-failure nocontainer fail "launcher: no crash" 30
	# only this run's device is asked
	st_has launcher-no-crash-iphone nogrep state/calls "spawn 11111111-"
	st_has launcher-no-crash-iphone grep state/calls "simctl spawn AAAAAAAA-"
	st_case own-device-left leftover fail "is still listed after its delete" 30
	ST_CMD="run --out @D@/out" st_case run-own-device-left leftover fail "is still listed after its delete" 30
	st_case create-times-out createhang fail "deleting AAAAAAAA-0000-0000-0000-" 30 IOS_SIM_CREATE_SECONDS=2
	st_case created-device-not-listed ghost fail "is not in the device list taken after simctl create" 30
	st_case created-device-listed-before reuse fail "was already listed before simctl create" 30
	st_case lost-device-listed-before samename fail "not deleting 55555555-5555-5555-5555-555555555555" 30 IOS_SIM_CREATE_SECONDS=2
	st_case launch-ignores-term launchterm ok "the launch client did not exit after the app" 40
	st_case family-refused floorrefused fail "ref_metal refused it (its floor)" 30
	st_case portrait-framebuffer portrait ok "smoke ok" 30
	st_case drawable-not-window-less-insets wrongdrawable fail "is not the window less the insets" 30
	st_case no-safe-area-line nosafeline fail "engine.log has no safe area line" 30
	st_case lit-pixel-outside-safe-area litoutside fail "outside the safe rectangle" 30
	st_case nothing-inside-safe-area blackapp fail "nothing inside the safe rectangle" 30
	st_case present-stretched stretched fail "is not the frame q2_0000.tga pixel for pixel" 30
	st_case present-cropped cropped fail "is not the frame q2_0000.tga pixel for pixel" 30
	st_case present-shifted shifted fail "is not the frame q2_0000.tga pixel for pixel" 30
	st_case present-colour-within-tolerance colourshift ok "smoke ok" 30
	st_case turn-ccw-other-turn-nothing-inside ccwtoprow ok "(turn ccw)" 30
	st_case container-hangs containerhang fail "s: xcrun simctl get_app_container" 25
	st_case app-killed killed fail "it was killed or it crashed" 30
	st_case app-crashed crashed fail "it was killed or it crashed" 30
	st_case app-does-not-quit appstays fail "it was killed or it crashed" 40
	st_case quit-without-renderer-close noclose fail "it was killed or it crashed" 30
	st_case one-vid-restart restartonce ok "smoke ok" 30
	st_case vid-restart-loop restartloop fail "a restart loop" 30
	st_case smoke-limit-not-digits extra fail "IOS_SIM_SMOKE_SECONDS must be" 5 IOS_SIM_SMOKE_SECONDS=3m
	st_case boot-limit-not-digits extra fail "IOS_SIM_BOOT_SECONDS must be" 5 IOS_SIM_BOOT_SECONDS=1e3
	st_case list-limit-not-digits extra fail "IOS_SIM_LIST_SECONDS must be" 5 IOS_SIM_LIST_SECONDS=-1
	st_case create-limit-not-digits extra fail "IOS_SIM_CREATE_SECONDS must be" 5 "IOS_SIM_CREATE_SECONDS= 9"
	st_case settle-limit-not-digits extra fail "IOS_SIM_SETTLE_SECONDS must be" 5 IOS_SIM_SETTLE_SECONDS=x
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
bg=
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
		--background) [ $# -ge 2 ] && [ -z "$bg" ] || usage; bg=$2; shift 2 ;;
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
[ -z "$bg" ] || digits "--background" "$bg"
bgafter=${IOS_SIM_BACKGROUND_AFTER-15}
digits IOS_SIM_BACKGROUND_AFTER "$bgafter"
# the SDK of the build (TASK23): the iOS simulator's, or the Apple TV simulator's; nothing else
# reaches make or the runtime pick
case ${IOS_SIM_SDK:-} in
''|iphonesimulator) ;;
appletvsimulator) ;;
xrsimulator) ;;
*) die "IOS_SIM_SDK must be iphonesimulator (the default), appletvsimulator or xrsimulator" ;;
esac

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
digits IOS_SIM_SETTLE_SECONDS "${IOS_SIM_SETTLE_SECONDS-3}"

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
		# a run that stopped early still says what its launcher did (its exit status is already a failure)
		[ -z "$launcher_booted" ] || launcher_report || true
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
makeargs=(-C "$repo" -j"${IOS_SIM_JOBS:-3}" QUIET=1 ios IOS_SDK="${IOS_SIM_SDK:-iphonesimulator}" IOS_ROOT="$build")
# the bundle's data: the synthetic paks (smoke, run without --data); with
# --data or --no-data the bundle holds none
if [ -z "$data" ] && [ "$nodata" = 0 ]; then
	python3 -I "$repo/scripts/ios/smokepak.py" --out "${tmp:?}/data" > "${tmp:?}/smokepak.out" 2>&1 \
		|| { cat "${tmp:?}/smokepak.out" >&2; die "smokepak.py failed"; }
	# the smoke's commands (TASK147): about 30 frames to the console, the frame written, then at least
	# 900 frames of 16 ms ("wait", about 15 s) at the console, in which the simulator's screenshot of it is
	# taken, and the app quits through its own quit path. Only smoke runs it (+exec smoke.cfg)
	{
		for _ in $(seq 30); do echo wait; done
		echo "screenshot tga"
		for _ in $(seq 900); do echo wait; done
		echo quit
	} > "${tmp:?}/data/baseq2/smoke.cfg" || die "cannot write smoke.cfg"
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
# the runtime: the newest that lists the device type, on a version tie the one that lists the
# most types (a runtime with no list counts as general); IOS_SIM_RUNTIME narrows the set, then
# the same rule picks within it
# TASK23: IOS_SIM_SDK=appletvsimulator runs the tvOS build on a tvOS runtime and an Apple TV device type
case ${IOS_SIM_SDK:-iphonesimulator} in
appletvsimulator) sim_platform=tvOS; sim_type_default="Apple TV 4K (4th generation)" ;;
# TASK175: IOS_SIM_SDK=xrsimulator, a visionOS runtime (simctl's platform "xrOS") and the Apple Vision Pro type
xrsimulator) sim_platform=xrOS; sim_type_default="Apple Vision Pro" ;;
*) sim_platform=iOS; sim_type_default="iPhone 17" ;;
esac
pick=$(python3 -I - "${tmp:?}/types.json" "${tmp:?}/runtimes.json" "${IOS_SIM_TYPE:-$sim_type_default}" "${IOS_SIM_RUNTIME:-}" "$sim_platform" <<'EOF'
import json, sys
types = json.load(open(sys.argv[1]))["devicetypes"]
runtimes = json.load(open(sys.argv[2]))["runtimes"]
want_type, want_rt, want_platform = sys.argv[3], sys.argv[4], sys.argv[5]
t = [d for d in types if want_type in (d["name"], d["identifier"])]
if not t:
    sys.exit("no device type " + want_type)
ios = [r for r in runtimes if r.get("isAvailable") and r.get("platform", "") == want_platform]
if want_rt:
    ios = [r for r in ios if want_rt in (r.get("identifier"), r.get("version"), r.get("name"))]
if not ios:
    sys.exit("no available iOS runtime" + (" " + want_rt if want_rt else ""))
tid = t[0]["identifier"]
def label(r):
    return str(r.get("name")) + " (" + str(r.get("identifier")) + ")"
def lists(r):
    # a runtime with no list of device types stays a candidate
    ok = [s["identifier"] for s in r.get("supportedDeviceTypes", [])]
    return not ok or tid in ok
def ver(r):
    try:
        return [int(x) for x in r["version"].split(".")]
    except (KeyError, ValueError, AttributeError):
        sys.exit("the iOS runtime " + label(r) + " has no numeric version")
# the same rule for the default and for a version or name given: only a runtime that lists the
# device type is a candidate (the others are skipped, whatever their version says); newest first;
# on a version tie the runtime that lists the most device types (the general one)
cand = [r for r in ios if lists(r)]
if not cand:
    if want_rt:
        sys.exit(t[0]["name"] + " is not supported by " + ", ".join(r["name"] for r in ios))
    sys.exit(t[0]["name"] + " is not supported by any available iOS runtime; tried " + ", ".join(label(r) for r in ios))
rt = max(cand, key=lambda r: (ver(r), len(r["supportedDeviceTypes"]) if r.get("supportedDeviceTypes") else 1 << 30))
print(t[0]["identifier"], rt["identifier"], rt["name"])
EOF
) || die "no device for the run"
read -r type_id rt_id rt_name <<< "$pick"
# TASK180: did the launcher of this run's device crash or restart while the run was in progress?
# The launcher is SpringBoard on iOS, PineBoard on tvOS and SurfBoard on visionOS (the label launchd of the
# device knows it by, found from "simctl spawn <udid> launchctl list" on a throwaway device of each
# platform, docs/runbook/TASK180.md). The device's own launchd keeps, per job, its pid and the last
# exit status: a baseline taken once the device has booted and the same read before the device is
# deleted differ if the launcher exited in between (a new pid, or a last exit status that is not
# the baseline's: -11 is a SIGSEGV), whatever the engine's log says. One "simctl spawn" on this
# run's UDID, bounded; nothing on the host is read. Any read that fails is "unknown", never a pass.
case ${IOS_SIM_SDK:-iphonesimulator} in
appletvsimulator) launcher_label=com.apple.PineBoard ;;
xrsimulator) launcher_label=com.apple.SurfBoard ;;
*) launcher_label=com.apple.SpringBoard ;;
esac
launcher_booted=
launcher_base=
launcher_base_why=
launcher_done=
launcher_bad=0
# launcher_state: "<pid> <last exit status>" of the launcher into launcher_cur, or launcher_why
launcher_state()
{
	launcher_cur=
	launcher_why=
	bounded 30 "${tmp:?}/launchctl.out" xcrun simctl spawn "$udid" launchctl list || { launcher_why="launchctl list failed in the device"; return 1; }
	launcher_cur=$(awk -v l="$launcher_label" '$3 == l { print $1, $2; exit }' "${tmp:?}/launchctl.out")
	[ -n "$launcher_cur" ] || { launcher_why="the device's launchd lists no $launcher_label"; return 1; }
	printf '%s\n' "$launcher_cur" | grep -E -x '([0-9]+|-) -?[0-9]+' >/dev/null || { launcher_why="the device's launchd line for $launcher_label cannot be read"; launcher_cur=; return 1; }
	return 0
}
# launcher_baseline: once the device has booted
launcher_baseline()
{
	launcher_booted=1
	if launcher_state; then
		launcher_base=$launcher_cur
		case $launcher_base in
		-\ *) launcher_base=; launcher_base_why="$launcher_label was not running at the start" ;;
		esac
	else
		launcher_base_why=$launcher_why
	fi
}
# launcher_report: the one line, before the device is deleted (once); launcher_bad=1 when it
# decides against the run: a crash, or unknown, unless IOS_SIM_LAUNCHER_CRASH_OK=1
launcher_report()
{
	local bp bs cp cs line
	[ -z "$launcher_done" ] || return 0
	launcher_done=1
	if [ -z "$launcher_base" ]; then
		line="launcher: unknown (${launcher_base_why:-no baseline})"
		launcher_bad=1
	elif ! launcher_state; then
		line="launcher: unknown ($launcher_why)"
		launcher_bad=1
	else
		read -r bp bs <<< "$launcher_base"
		read -r cp cs <<< "$launcher_cur"
		if [ "$cp" = "$bp" ] && [ "$cs" = "$bs" ]; then
			line="launcher: no crash"
		else
			line="launcher: 1 crash(es): $launcher_label, pid $bp -> $cp, last exit status $cs, seen at $(date -u +%H:%M:%SZ)"
			launcher_bad=1
		fi
	fi
	echo "ios-sim: $line"
	if [ "${IOS_SIM_LAUNCHER_CRASH_OK-}" = 1 ] && [ "$launcher_bad" = 1 ]; then
		echo "ios-sim: IOS_SIM_LAUNCHER_CRASH_OK=1: the launcher line does not fail the run"
		launcher_bad=0
	fi
	return 0
}
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
launcher_baseline
bounded 60 "${tmp:?}/shot.out" xcrun simctl io "$udid" screenshot ${IOS_SIM_DISPLAY:+"--display=$IOS_SIM_DISPLAY"} "$out/screen.png" || die "simctl io screenshot failed"
bounded 180 "${tmp:?}/install.out" xcrun simctl install "$udid" "$app" || { cat "${tmp:?}/install.out" >&2; die "simctl install failed"; }

# ---- the run ---------------------------------------------------------------

# Com_Quit's line in the engine's log: the app ended through its own quit path
shutdown_line='----------- shutting down ----------'
# a start with no game data ends through VID_CheckGameData: it prints this text, shows the alert and quits
# (Sys_Quit, never Com_Quit), so this line is that quit path
missing_data_line='Quake II needs its game data.'
# data_container: the app's data container into $container (fail closed)
data_container()
{
	bounded 60 "${tmp:?}/container.out" xcrun simctl get_app_container "$udid" "$bid" data || die "simctl get_app_container failed"
	container=$(tail -n 1 "${tmp:?}/container.out")
	[ -d "$container" ] || die "no data container: $container"
}

# launch_app <limit seconds> <what the limit is> <shot seconds or empty>
# <n> <engine argument>...: one start of the app for run, the engine's output
# in $out/console.<n>.log; waits for the app to end (smoke has its own
# bounded frame wait and quit handling, TASK147)
launch_app()
{
	local limit=$1 what=$2 shotat=$3 n=$4 log t t0 w=0 shotbase=0 bgdone=0
	shift 4
	shot_taken=0
	log=$out/console.$n.log
	SIMCTL_CHILD_MTL_DEBUG_LAYER=1 xcrun simctl launch --console-pty "$udid" "$bid" "$@" > "$log" 2>&1 &
	lpid=$!
	# the launch client lives as long as the app (--console-pty) and
	# its pid line may come only when the app ends: watch the client
	apid=$lpid
	echo "ios-sim: app $bid pid $apid"
	t=0
	t0=$SECONDS
	while kill -0 "$apid" 2>/dev/null; do
		if [ -n "$bg" ] && [ "$bgdone" = 0 ] && [ "$t" -ge "$bgafter" ]; then
			# the app goes to the background as a person's Home does: another app takes the screen
			bounded 30 "${tmp:?}/bg.out" xcrun simctl launch "$udid" com.apple.Preferences || die "simctl launch of the other app failed"
			sleep "$bg"
			t=$((t + bg))
			bounded 30 "${tmp:?}/fg.out" xcrun simctl launch "$udid" "$bid" || die "simctl launch of the app (the return) failed"
			bgdone=1
			shotbase=$t
			# the same process must have lived through the background: a launch of a dead app starts a new one, which is a restart, not a resume
			kill -0 "$apid" 2>/dev/null || die "the app did not survive the background (the process $apid ended; the return started it again)"
			# the system's own log of the app (a command buffer submitted in the background is reported there)
			[ -n "$shotat" ] || shotat=0
		fi
		# with --background the shot waits for the return
		if [ -n "$shotat" ] && { [ -z "$bg" ] || [ "$bgdone" = 1 ]; } && [ "$t" -ge $((shotat + shotbase)) ]; then
			bounded 60 "${tmp:?}/shot.out" xcrun simctl io "$udid" screenshot ${IOS_SIM_DISPLAY:+"--display=$IOS_SIM_DISPLAY"} "$out/screen.$n.png" || die "simctl io screenshot failed"
			if [ -n "$bg" ]; then
				bounded 90 "$out/oslog.$n.txt" xcrun simctl spawn "$udid" log show --last 5m --predicate 'process == "quake2"' || die "simctl spawn log show failed"
			fi
			shot_taken=1
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
	echo "ios-sim: start $n: the app ran $((SECONDS - t0)) s"
	# the app ended on its own before the screenshot was due
	if [ -n "$shotat" ] && [ "$shot_taken" = 0 ]; then
		echo "ios-sim: start $n: the app ended before the screenshot (due $shotat s after the start or the return): none was taken" >&2
	fi
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
		# split at spaces by read, not by an unquoted expansion: a * or ? is not a pattern here
		words=()
		read -r -a words <<< "$l"
		launch_app "$run_s" "IOS_SIM_RUN_SECONDS" "$shot" "$n" +set vid_renderer metal +set logfile 2 ${words[@]+"${words[@]}"}
		data_container
		docs=$container/Documents/baseq2
		[ -d "$docs" ] || die "no Documents/baseq2 in the data container after start $n"
		( cd "$docs" && find . -type f | sort | while read -r f; do printf '%s %s\n' "$(wc -c < "$f" | tr -d ' ')" "$f"; done ) > "$out/documents.$n.list" || die "cannot list $docs"
		[ -f "$docs/qconsole.log" ] || die "the engine wrote no qconsole.log in Documents/baseq2 after start $n"
		cp "$docs/qconsole.log" "$out/engine.$n.log"
		# a start the script ended itself (the shot, then terminate) has no quit path to show; one that
		# ended on its own did, or it was killed or it crashed (decided from the engine's own log)
		if [ "$shot_taken" = 0 ] && ! grep -F -x -e "$shutdown_line" "$out/engine.$n.log" >/dev/null \
			&& ! grep -F -e "$missing_data_line" "$out/engine.$n.log" >/dev/null; then
			die "start $n: the app ended and engine.$n.log does not show its own quit path (\"$shutdown_line\", or the missing-data text): it was killed or it crashed"
		fi
		if [ -d "$docs/scrnshot" ]; then
			mkdir -p "$out/frames.$n" && cp "$docs/scrnshot/"* "$out/frames.$n/" 2>/dev/null
		fi
	done
	launcher_report
	bounded 60 "${tmp:?}/shutdown.out" xcrun simctl shutdown "$udid" || true
	bounded 60 "${tmp:?}/delete.out" xcrun simctl delete "$udid" || die "could not delete the device $udid"
	udid=
	list_devices "$out/devices.after" || die "no device list after the run"
	udids "$out/devices.after" > "${tmp:?}/udids.after"
	gone=$(comm -23 "${tmp:?}/udids.before" "${tmp:?}/udids.after" | tr '\n' ' ')
	[ -z "$gone" ] || echo "ios-sim: warning: devices listed before the run are gone after it (not this run's, not a failure): $gone" >&2
	grep -F -x -e "$run_udid" "${tmp:?}/udids.after" >/dev/null && die "this run's device $run_udid is still listed after its delete"
	[ "$launcher_bad" = 0 ] || die "the launcher line above fails the run (IOS_SIM_LAUNCHER_CRASH_OK=1 accepts it)"
	echo "ios-sim: run ok, $n start(s) (artifacts in $out)"
	exit 0
fi

# The engine finds Documents/baseq2 by itself: no write-directory option.
# r_mode 4 and vid_fullscreen 1 are what the real data's default.cfg sets: the screen is its own size whatever they say (TASK147)
args=(+set vid_renderer metal +set logfile 2 +set metal_stats 1 +set r_mode 4 +set vid_fullscreen 1 +exec smoke.cfg)
SIMCTL_CHILD_MTL_DEBUG_LAYER=1 xcrun simctl launch --console-pty "$udid" "$bid" "${args[@]}" > "$out/console.log" 2>&1 &
lpid=$!
# TASK147: the app stays at the console for smoke.cfg's frames, then quits by itself (the launch
# client prints the app's pid only when it ends, so the app is watched through the launch client).
# Once the engine's log shows the frame written, the simulator's own screenshot of it; then the app
# is waited for. Every wait is wall time, to the run's deadline, and every simctl call is bounded by
# the time left
deadline=$((SECONDS + smoke_s))
left() { local l=$((deadline - SECONDS)); [ "$l" -ge 1 ] || l=1; echo "$l"; }
# engine_log_has <line>: the engine's log in the app's data container holds that whole line
engine_log_has()
{
	local c
	bounded "$(left)" "${tmp:?}/container.out" -e "${tmp:?}/container.err" xcrun simctl get_app_container "$udid" "$bid" data || return 1
	c=$(tail -n 1 "${tmp:?}/container.out")
	[ -d "$c" ] && find "$c" -name qconsole.log -type f -exec grep -l -F -x -e "$1" {} + 2>/dev/null | grep . >/dev/null
}
framed=
quit_seen=
while [ "$SECONDS" -lt "$deadline" ]; do
	if engine_log_has 'Wrote q2_0000.tga'; then
		framed=1
		break
	fi
	kill -0 "$lpid" 2>/dev/null || break
	sleep 1
done
if [ -n "$framed" ]; then
	sleep "${IOS_SIM_SETTLE_SECONDS:-3}"   # the console is drawn and presented
	bounded 60 "${tmp:?}/appshot.out" xcrun simctl io "$udid" screenshot ${IOS_SIM_DISPLAY:+"--display=$IOS_SIM_DISPLAY"} "$out/app.png" || die "simctl io screenshot of the app failed"
	# the app quits by itself (smoke.cfg's quit): its shutdown line, or its end
	while kill -0 "$lpid" 2>/dev/null && [ "$SECONDS" -lt "$deadline" ]; do
		if engine_log_has "$shutdown_line"; then
			quit_seen=1
			break
		fi
		sleep 1
	done
elif kill -0 "$lpid" 2>/dev/null; then
	bounded 30 "${tmp:?}/term.out" xcrun simctl terminate "$udid" "$bid" || true
	die "the engine did not write its frame within $smoke_s s; terminated the app"
fi
# an app that has not quit by itself is terminated (this device only; engine.log then shows no quit
# path, which fails below); the launch client ends with the app
if [ -z "$quit_seen" ] && kill -0 "$lpid" 2>/dev/null; then
	echo "ios-sim: the app did not quit by itself within $smoke_s s; terminating it" >&2
	bounded 30 "${tmp:?}/term.out" xcrun simctl terminate "$udid" "$bid" || true
fi
w=0
while kill -0 "$lpid" 2>/dev/null && [ "$w" -lt 10 ]; do sleep 1; w=$((w + 1)); done
apid=$(sed -n "s/^$bid: \([0-9][0-9]*\).*/\1/p" "$out/console.log" | head -n 1)
[ -n "$apid" ] || echo "ios-sim: no pid line from the launch client (log: $out/console.log)" >&2
echo "ios-sim: app $bid pid ${apid:-?}"
if kill -0 "$lpid" 2>/dev/null; then
	bounded 30 "${tmp:?}/term.out" xcrun simctl terminate "$udid" "$bid" || true
	echo "ios-sim: the launch client did not exit after the app; stopping it" >&2
	stop_pid "$lpid" || true
else
	wait "$lpid" 2>/dev/null
fi
lpid=

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
	launcher_report
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
[ -z "$gone" ] || echo "ios-sim: warning: devices listed before the run are gone after it (not this run's, not a failure): $gone" >&2
grep -F -x -e "$run_udid" "${tmp:?}/udids.after" >/dev/null && bad "this run's device $run_udid is still listed after its delete"
[ "$launcher_bad" = 0 ] || bad "the launcher line above fails the run (IOS_SIM_LAUNCHER_CRASH_OK=1 accepts it)"

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
safe=$(sed -n 's/^ref_metal: safe area: window \([0-9]*\)x\([0-9]*\) pixels, insets left \([0-9]*\) top \([0-9]*\) right \([0-9]*\) bottom \([0-9]*\)$/\1 \2 \3 \4 \5 \6/p' "$out/engine.log" | tail -n 1)
if [ -z "$safe" ]; then
	bad "engine.log has no safe area line"
else
	read -r sw sh sl st sr sb <<< "$safe"
	[ "${sw}x${sh}" = "${screen:-}" ] || bad "the window ${sw}x${sh} is not the screen's ${screen:-?} pixels (landscape)"
	[ -n "$size" ] && [ "$size" != "$((sw - sl - sr))x$((sh - st - sb))" ] \
		&& bad "Drawable size $size is not the window less the insets, $((sw - sl - sr))x$((sh - st - sb))"
	# the screenshot of the app: black outside the safe rectangle, the console inside it
	if [ -f "$out/app.png" ]; then
		shot=$(python3 -I - "$out/app.png" "$sw" "$sh" "$sl" "$st" "$sr" "$sb" "$out/q2_0000.tga" 8 <<'EOF'
import struct, sys, zlib
b = open(sys.argv[1], "rb").read()
if b[:8] != b"\x89PNG\r\n\x1a\n":
    sys.exit("not a PNG")
w = h = ct = None
idat = b""
i = 8
while i + 8 <= len(b):
    n, t = struct.unpack(">I4s", b[i:i + 8])
    d = b[i + 8:i + 8 + n]
    if t == b"IHDR":
        w, h, depth, ct, _, _, il = struct.unpack(">IIBBBBB", d)
        if depth != 8 or ct not in (2, 6) or il:
            sys.exit("not an 8-bit non-interlaced RGB or RGBA PNG")
    elif t == b"IDAT":
        idat += d
    i += 12 + n
if w is None:
    sys.exit("no IHDR")
sw, sh, l, t, r, bt = [int(x) for x in sys.argv[2:8]]
# the simulator's screenshot is of the device's framebuffer: an iPad's stays portrait, with the
# landscape app turned in it, either way up; the window's coordinates are mapped to it, each turn
# tried, and the one that fits is reported
if (w, h) == (sw, sh):
    turns = [("none", lambda x, y: (x, y))]
elif (w, h) == (sh, sw):
    turns = [("cw", lambda x, y: (sh - 1 - y, x)), ("ccw", lambda x, y: (y, sw - 1 - x))]
else:
    sys.exit("the screenshot is %dx%d, not the window's %dx%d" % (w, h, sw, sh))
bpp = 3 if ct == 2 else 4
raw = zlib.decompress(idat)
stride = w * bpp
if len(raw) != h * (stride + 1):
    sys.exit("short PNG data")
prev = bytearray(stride)
rows = []
for y in range(h):
    f = raw[y * (stride + 1)]
    row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
    for x in range(stride):
        a = row[x - bpp] if x >= bpp else 0
        u = prev[x]
        c = prev[x - bpp] if x >= bpp else 0
        if f == 1:
            row[x] = (row[x] + a) & 255
        elif f == 2:
            row[x] = (row[x] + u) & 255
        elif f == 3:
            row[x] = (row[x] + (a + u) // 2) & 255
        elif f == 4:
            p = a + u - c
            pa, pb, pc = abs(p - a), abs(p - u), abs(p - c)
            pr = a if pa <= pb and pa <= pc else (u if pb <= pc else c)
            row[x] = (row[x] + pr) & 255
        elif f != 0:
            sys.exit("bad PNG filter")
    prev = row
    rows.append(row)
best = None
for name, turn in turns:
    out_lit = in_lit = in_n = 0
    for y in range(sh):
        for x in range(sw):
            px, py = turn(x, y)
            o = py * 0 + px * bpp
            lit = rows[py][o] or rows[py][o + 1] or rows[py][o + 2]
            if l <= x < sw - r and t <= y < sh - bt:
                in_n += 1
                in_lit += 1 if lit else 0
            elif lit:
                out_lit += 1
    # a turn fits with no lit pixel outside the rectangle and some inside; the first that fits is kept
    if best is None or (out_lit == 0 and in_lit > 0 and not (best[0] == 0 and best[1] > 0)):
        best = (out_lit, in_lit, in_n, name, turn)
out_lit, in_lit, in_n, name, turn = best
# TASK147: the safe rectangle after that turn against the engine's frame, pixel for pixel, each
# channel within tol (a colour conversion); -1 when there is no frame of the rectangle's size
tol = int(sys.argv[9])
ndiff, ncmp, maxd = -1, 0, -1
try:
    g = open(sys.argv[8], "rb").read()
except OSError:
    g = b""
if len(g) >= 18 and g[1] == 0 and g[2] == 2 and g[16] in (24, 32):
    fw, fh = struct.unpack("<HH", g[12:16])
    n = g[16] // 8
    top = g[17] & 0x20
    base = 18 + g[0]
    if (fw, fh) == (sw - l - r, sh - t - bt) and len(g) >= base + fw * fh * n:
        ndiff = maxd = 0
        for y in range(fh):
            q = base + (y if top else fh - 1 - y) * fw * n
            for x in range(fw):
                px, py = turn(l + x, t + y)
                o = px * bpp
                row = rows[py]
                d = max(abs(row[o] - g[q + 2]), abs(row[o + 1] - g[q + 1]), abs(row[o + 2] - g[q]))
                q += n
                ncmp += 1
                if d > maxd:
                    maxd = d
                if d > tol:
                    ndiff += 1
print(out_lit, in_lit, in_n, name, ndiff, ncmp, maxd)
EOF
) || bad "app.png cannot be read: $shot"
		read -r out_lit in_lit in_n turn ndiff ncmp maxd <<< "${shot:-0 0 0 none -1 0 -1}"
		[ "${out_lit:-1}" -eq 0 ] || bad "app.png has $out_lit lit pixels outside the safe rectangle"
		[ "${in_lit:-0}" -gt 0 ] || bad "app.png has nothing inside the safe rectangle (the console is not drawn there)"
		case ${ndiff:--1} in
		0) ;;
		-1) bad "app.png cannot be compared with the frame q2_0000.tga (none of the safe rectangle's size)" ;;
		*) bad "app.png's safe rectangle is not the frame q2_0000.tga pixel for pixel: $ndiff of $ncmp pixels differ by more than 8 (the largest by $maxd): a stretched, cropped or shifted present" ;;
		esac
	else
		bad "no app.png: the app's screenshot was not taken"
	fi
fi
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
# TASK147: the app ended through its own quit path (smoke.cfg's quit): Com_Quit's line, then the
# renderer's close (its metal_stats line); a kill or a crash leaves neither
awk -v s="$shutdown_line" '$0 == s { q = 1; next } q && /^ref_metal: metal_stats: / { m = 1 } END { exit !(q && m) }' "$out/engine.log" \
	|| bad "engine.log does not show the app's own quit path (\"$shutdown_line\", then the renderer's metal_stats line): it was killed or it crashed"
# TASK147: every vid_restart loads the renderer again (VID_CheckChanges); one is allowed (the header says why)
loads=$(grep -c -F -x -e '----- refresher initialization -----' "$out/engine.log") || loads=0
[ "$loads" -le 2 ] || bad "engine.log shows $((loads - 1)) vid_restarts, more than one: a restart loop"
stats=$(grep -E 'ref_metal: metal_stats: ' "$out/engine.log" | tail -n 1)
case $stats in
*" 0 failed command buffers"*) ;;
"") bad "engine.log has no metal_stats line" ;;
*) bad "failed command buffers: $stats" ;;
esac

printf '%s\n' "ios-sim: ${dev:-no Metal device}" "ios-sim: ${fam:-no family line}" "ios-sim: drawable ${size:-?}, screen ${screen:-?}, frame ${frame:-?}, safe area ${safe:-?}, app.png (turn ${turn:-?}) outside ${out_lit:-?} inside ${in_lit:-?}/${in_n:-?}, against the frame ${ndiff:-?} of ${ncmp:-?} differ (largest ${maxd:-?}), vid_restarts $((${loads:-1} - 1))" \
	"ios-sim: devices before $(grep -c . "${tmp:?}/udids.before"), after create $(grep -c . "${tmp:?}/udids.created"), after $(grep -c . "${tmp:?}/udids.after")"
[ "$fail" -eq 0 ] || exit 1
echo "ios-sim: smoke ok (artifacts in $out)"
