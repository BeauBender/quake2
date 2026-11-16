#!/bin/bash
# scripts/ios-sim.sh -- the iOS simulator app on a throwaway device (TASK146).
#
#   scripts/ios-sim.sh smoke --out DIR
#   scripts/ios-sim.sh selftest
#
# smoke: writes the synthetic data (scripts/ios/smokepak.py, no id Software
# content) to a scratch directory, builds "make ios IOS_BUNDLE_DATA=<it>"
# with IOS_ROOT=DIR/build, creates a device of its own with "simctl create"
# (IOS_SIM_TYPE, default "iPhone 17", on IOS_SIM_RUNTIME, default the newest
# available iOS runtime), boots it, installs the app and launches it with
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
#     "simctl list devices -j"): every device listed before the run is
#     listed after it, and this run's device is in the list taken after
#     "simctl create", not in the one before and not in the one after.
#     Devices that others create or delete meanwhile (Xcode Previews) are
#     not this run's. A list that fails, cannot be read or names no
#     device is a failure.
# A "run" subcommand (the app for a person to look at, optionally with the
# owner's data in the local bundle) is left to a later card.
#
# selftest: runs "smoke" against a fake xcrun and a fake make (no
# simulator, no build, seconds) and checks its rules: a hung "simctl
# list" is timed out, a failed or empty device list fails, another device
# appearing meanwhile passes, a device from before that disappears fails,
# this run's device left behind fails, the device of a create that timed
# out is deleted, a create whose UDID is not listed after it, or was
# listed before it, fails (and leaves that device alone), ref_metal's
# floor refusal fails, a drawable that is not the window less the insets
# fails, a screenshot with a lit pixel outside the safe rectangle fails,
# one with nothing inside it fails, a stretched, cropped or shifted one
# fails and one within the colour tolerance passes, the turn that fits is
# found when the other has nothing lit inside, a hung get_app_container is
# timed out within the run's limit, an app killed, crashed or not quitting
# fails, one vid_restart passes and three fail, and a limit that is not
# digits is refused.
# tests/check-ios.sh runs it.
#
# Limits, in seconds (digits only, or the script refuses):
# IOS_SIM_BOOT_SECONDS (300), IOS_SIM_SMOKE_SECONDS (180, the app's run),
# IOS_SIM_LIST_SECONDS (60, each "simctl list"), IOS_SIM_CREATE_SECONDS
# (120).
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
usage() { echo "usage: scripts/ios-sim.sh smoke --out DIR | selftest" >&2; exit 2; }
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
bootstatus|install|terminate|shutdown) ;;
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
	# launch --console-pty <udid> <bundle id> <args>...
	c=$st/container/Library/YamagiQ2/baseq2
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
		*) printf '%s\n' "" "----------- shutting down ----------" \
			"ref_metal: metal_stats: 31 frames, 0 without a drawable, 0 skipped, 0 failed command buffers, " ;;
		esac
	} > "$c/qconsole.log"
	python3 -I "$(dirname "$0")/fakepix.py" tga "$c/scrnshot/q2_0000.tga" "$sc"
	[ "$sc" = launchterm ] && trap "" TERM
	if [ "$sc" = appstays ]; then
		echo "Metal API Validation Enabled"
		exec sleep 30
	fi
	sleep 1 &
	p=$!
	echo "$4: $p"
	echo "Metal API Validation Enabled"
	wait "$p"
	# a launch client that ignores TERM and outlives the app
	[ "$sc" = launchterm ] && { echo $$ > "$st/hang.pid"; exec sleep 60; }
	exit 0 ;;
get_app_container)
	# containerhang: the first call hangs
	if [ "$sc" = containerhang ] && [ ! -f "$st/hung" ]; then
		: > "$st/hung"
		echo $$ > "$st/hang.pid"
		exec sleep 30
	fi
	echo "$st/container" ;;
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
		local name=$1 sc=$2 want=$3 text=$4 max=$5 d rc=0 t0 why=
		shift 5
		ncase=$((ncase + 1))
		d=$sroot/$name
		mkdir -p "${d:?}/state" || die "cannot create $d"
		printf '%s\n' "11111111-1111-1111-1111-111111111111 Owner iPhone" \
			"22222222-2222-2222-2222-222222222222 Owner iPad" > "${d:?}/state/devices"
		t0=$(date +%s)
		bounded 60 "${d:?}/run.log" env PATH="$sroot/bin:$PATH" FAKE_STATE="$d/state" FAKE_SCENARIO="$sc" \
			IOS_SIM_SETTLE_SECONDS=0 IOS_SIM_LIST_SECONDS=2 IOS_SIM_CREATE_SECONDS=2 IOS_SIM_BOOT_SECONDS=10 IOS_SIM_SMOKE_SECONDS=10 "$@" \
			bash "$self" smoke --out "$d/out" || rc=$?
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
	st_case extra-device-meanwhile extra ok "smoke ok" 30
	st_case list-empty empty fail "simctl list devices: no device list" 30
	st_case list-fails listfail fail "simctl list devices failed" 30
	st_case list-hangs hang fail "timed out after 2 s: xcrun simctl list devices" 10
	st_case device-from-before-gone vanish fail "gone after the run: 22222222-2222-2222-2222-222222222222" 30
	st_case own-device-left leftover fail "is still listed after its delete" 30
	st_case create-times-out createhang fail "deleting AAAAAAAA-0000-0000-0000-" 30
	st_case created-device-not-listed ghost fail "is not in the device list taken after simctl create" 30
	st_case created-device-listed-before reuse fail "was already listed before simctl create" 30
	st_case lost-device-listed-before samename fail "not deleting 55555555-5555-5555-5555-555555555555" 30
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
[ $# -eq 3 ] && [ "$1" = smoke ] && [ "$2" = --out ] && [ -n "$3" ] || usage
out=$3

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
digits IOS_SIM_BOOT_SECONDS "$boot_s"
digits IOS_SIM_SMOKE_SECONDS "$smoke_s"
digits IOS_SIM_LIST_SECONDS "$list_s"
digits IOS_SIM_CREATE_SECONDS "$create_s"
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

python3 -I "$repo/scripts/ios/smokepak.py" --out "${tmp:?}/data" > "${tmp:?}/smokepak.out" 2>&1 \
	|| { cat "${tmp:?}/smokepak.out" >&2; die "smokepak.py failed"; }
# the run's commands (TASK147): about 30 frames to the console, the frame written, then at least
# 900 frames of 16 ms ("wait", about 15 s) at the console, in which the simulator's screenshot of it is
# taken, and the app quits through its own quit path
{
	for _ in $(seq 30); do echo wait; done
	echo "screenshot tga"
	for _ in $(seq 900); do echo wait; done
	echo quit
} > "${tmp:?}/data/baseq2/smoke.cfg" || die "cannot write smoke.cfg"
build=$out/build
makeargs=(-C "$repo" -j"${IOS_SIM_JOBS:-3}" QUIET=1 ios IOS_SDK=iphonesimulator IOS_ROOT="$build" IOS_BUNDLE_DATA="$tmp/data")
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

# The write directory: the default ~/.local/share/YamagiQ2 is not made
# in a fresh app container (Sys_Mkdir makes one level), Library/ is there.
# r_mode 4 and vid_fullscreen 1 are what the real data's default.cfg sets: the screen is its own size whatever they say (TASK147)
args=(-cfgdir Library/YamagiQ2 +set vid_renderer metal +set logfile 2 +set metal_stats 1 +set r_mode 4 +set vid_fullscreen 1 +exec smoke.cfg)
SIMCTL_CHILD_MTL_DEBUG_LAYER=1 xcrun simctl launch --console-pty "$udid" "$bid" "${args[@]}" > "$out/console.log" 2>&1 &
lpid=$!
# TASK147: the app stays at the console for smoke.cfg's frames, then quits by itself (the launch
# client prints the app's pid only when it ends, so the app is watched through the launch client).
# Once the engine's log shows the frame written, the simulator's own screenshot of it; then the app
# is waited for. Every wait is wall time, to the run's deadline, and every simctl call is bounded by
# the time left
deadline=$((SECONDS + smoke_s))
left() { local l=$((deadline - SECONDS)); [ "$l" -ge 1 ] || l=1; echo "$l"; }
shutdown_line='----------- shutting down ----------'
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
	bounded 60 "${tmp:?}/appshot.out" xcrun simctl io "$udid" screenshot "$out/app.png" || die "simctl io screenshot of the app failed"
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
