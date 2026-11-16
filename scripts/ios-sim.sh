#!/bin/bash
# scripts/ios-sim.sh -- the iOS simulator app on a throwaway device (TASK146).
#
#   scripts/ios-sim.sh smoke --out DIR
#   scripts/ios-sim.sh run [--data DIR] [--out DIR] [-- ENGINE ARGS...]
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
# screenshot (its screen size) and from the device list:
#   - the linked-in ref_metal, the Metal device and family lines, a
#     Drawable size of the screen in pixels (landscape) and "Wrote
#     q2_0000.tga" are in engine.log;
#   - the frame is that size and not one colour;
#   - Metal's validation layer was on (its line in the app's own output,
#     DIR/console.log), no validation message, and 0 failed command
#     buffers in ref_metal's metal_stats line;
#   - "simctl list devices" lists the same devices before and after.
# run: the same app, without the scripted quit, for a person to look at in
# Simulator.app; with --data, DIR's baseq2/ (the owner's paks) goes into
# the bundle of this local build and nowhere else. It waits until the app
# exits or IOS_SIM_RUN_SECONDS pass (default 300), and with IOS_SIM_SHOT=N
# saves the simulator's screen after N seconds to DIR/run.png.
#
# Rules: every wait is bounded; the app is watched by its PID; nothing is
# killed but a PID this script started, and the app only through "simctl
# terminate" on this script's device; no other device is touched (the
# trap shuts down and deletes this one by its UDID); DIR must be outside
# the checkout, and the build goes to DIR/build, never to release/ or
# debug/. A failed build, install or launch is a failure, not a skip.
set -u
export LC_ALL=C

repo=$(cd "$(dirname "$0")/.." && pwd -P) || exit 1
die() { echo "ios-sim: $*" >&2; exit 1; }
usage() { echo "usage: scripts/ios-sim.sh smoke --out DIR | run [--data DIR] [--out DIR] [-- ENGINE ARGS...]" >&2; exit 2; }

[ $# -ge 1 ] || usage
cmd=$1
shift
case "$cmd" in smoke|run) ;; *) usage ;; esac
out=
data=
extra_args=()
while [ $# -gt 0 ]; do
	case "$1" in
	--out) [ $# -ge 2 ] || usage; out=$2; shift 2 ;;
	--data) [ "$cmd" = run ] && [ $# -ge 2 ] || usage; data=$2; shift 2 ;;
	--) shift; extra_args=("$@"); break ;;
	*) usage ;;
	esac
done
[ "$cmd" = smoke ] && [ -z "$out" ] && usage
[ -n "$out" ] || out=$(mktemp -d "${TMPDIR:-/tmp}/ios-run.XXXXXX") || die "no scratch directory"

for tool in xcrun python3 make plutil; do
	command -v "$tool" >/dev/null 2>&1 || die "$tool not found: a missing tool is a failure"
done

mkdir -p "${out:?}" || die "cannot create $out"
out=$(cd "${out:?}" && pwd -P) || die "cannot enter $out"
case "$out/" in "$repo/"*) die "--out must be outside the checkout ($repo), not $out" ;; esac
if [ -n "$data" ]; then
	[ -d "$data/baseq2" ] || die "--data needs a directory holding baseq2/, not $data"
	data=$(cd "$data" && pwd -P) || die "cannot enter $data"
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/ios-sim.XXXXXX") || die "no scratch directory"
udid=
lpid=
cleanup()
{
	if [ -n "$lpid" ] && kill -0 "$lpid" 2>/dev/null; then
		kill "$lpid" 2>/dev/null
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

# bounded <seconds> <log> <command>...: runs it, kills it (our PID) past the bound
bounded()
{
	local limit=$1 log=$2 pid t=0 w status=0
	shift 2
	"$@" > "$log" 2>&1 &
	pid=$!
	while kill -0 "$pid" 2>/dev/null; do
		if [ "$t" -ge "$limit" ]; then
			kill "$pid" 2>/dev/null
			w=0; while kill -0 "$pid" 2>/dev/null && [ "$w" -lt 5 ]; do sleep 1; w=$((w + 1)); done
			kill -0 "$pid" 2>/dev/null && kill -9 "$pid" 2>/dev/null
			wait "$pid" 2>/dev/null
			echo "ios-sim: timed out after $limit s: $*" >&2
			return 124
		fi
		sleep 1
		t=$((t + 1))
	done
	wait "$pid" || status=$?
	return $status
}

# the default device set's devices, one "<udid> <name>" per line
devices()
{
	xcrun simctl list devices -j | python3 -I -c '
import json, sys
for rt, devs in json.load(sys.stdin)["devices"].items():
    for d in devs:
        print(d["udid"], d["name"])' | sort
}

# ---- the data and the build --------------------------------------------

if [ "$cmd" = smoke ]; then
	python3 -I "$repo/scripts/ios/smokepak.py" --out "${tmp:?}/data" > "${tmp:?}/smokepak.out" 2>&1 \
		|| { cat "${tmp:?}/smokepak.out" >&2; die "smokepak.py failed"; }
	data=$tmp/data
fi
build=$out/build
makeargs=(-C "$repo" -j"${IOS_SIM_JOBS:-3}" QUIET=1 ios IOS_SDK=iphonesimulator IOS_ROOT="$build")
[ -n "$data" ] && makeargs+=(IOS_BUNDLE_DATA="$data")
echo "ios-sim: make ios (into $build)"
if ! (unset MAKEFLAGS GNUMAKEFLAGS MFLAGS; make "${makeargs[@]}") > "$out/build.log" 2>&1; then
	tail -n 20 "$out/build.log" >&2
	die "make ios failed (log: $out/build.log)"
fi
app=$build/Quake2.app
[ -x "$app/quake2" ] || die "make ios wrote no $app/quake2"
bid=$(plutil -extract CFBundleIdentifier raw -o - "$app/Info.plist" 2>/dev/null) || die "no CFBundleIdentifier in $app/Info.plist"

# ---- the device ----------------------------------------------------------

devices > "$out/devices.before" || die "simctl list devices failed"
xcrun simctl list devicetypes -j > "${tmp:?}/types.json" || die "simctl list devicetypes failed"
xcrun simctl list runtimes -j > "${tmp:?}/runtimes.json" || die "simctl list runtimes failed"
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
bounded 120 "${tmp:?}/create.out" xcrun simctl create "$name" "$type_id" "$rt_id" || { cat "${tmp:?}/create.out" >&2; die "simctl create failed"; }
udid=$(tail -n 1 "${tmp:?}/create.out")
printf '%s\n' "$udid" | grep -E -x '[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}' >/dev/null || { udid=; die "simctl create gave no UDID"; }
echo "ios-sim: device $udid ($type_id, $rt_name)"
bounded "${IOS_SIM_BOOT_SECONDS:-300}" "$out/boot.log" xcrun simctl bootstatus "$udid" -b || die "the device did not boot (log: $out/boot.log)"
bounded 60 "${tmp:?}/shot.out" xcrun simctl io "$udid" screenshot "$out/screen.png" || die "simctl io screenshot failed"
bounded 180 "${tmp:?}/install.out" xcrun simctl install "$udid" "$app" || { cat "${tmp:?}/install.out" >&2; die "simctl install failed"; }

# ---- the run ---------------------------------------------------------------

# The write directory: the default ~/.local/share/YamagiQ2 is not made
# in a fresh app container (Sys_Mkdir makes one level), Library/ is there.
args=(-cfgdir Library/YamagiQ2 +set vid_renderer metal +set logfile 2 +set metal_stats 1)
if [ "$cmd" = smoke ]; then
	for _ in $(seq 30); do args+=(+wait); done
	args+=(+screenshot tga +quit)
	limit=${IOS_SIM_SMOKE_SECONDS:-180}
else
	open -a Simulator --args -CurrentDeviceUDID "$udid" > /dev/null 2>&1 || echo "ios-sim: Simulator.app did not open; the device runs headless" >&2
	limit=${IOS_SIM_RUN_SECONDS:-300}
fi
args+=(${extra_args[@]+"${extra_args[@]}"})
SIMCTL_CHILD_MTL_DEBUG_LAYER=1 xcrun simctl launch --console-pty "$udid" "$bid" "${args[@]}" > "$out/console.log" 2>&1 &
lpid=$!
apid=
t=0
while [ "$t" -lt 60 ]; do
	apid=$(sed -n "s/^$bid: \([0-9][0-9]*\).*/\1/p" "$out/console.log" | head -n 1)
	[ -n "$apid" ] && break
	kill -0 "$lpid" 2>/dev/null || break
	sleep 1
	t=$((t + 1))
done
[ -n "$apid" ] || { tail -n 5 "$out/console.log" >&2; die "the app did not start (log: $out/console.log)"; }
echo "ios-sim: app $bid pid $apid"
t=0
shot=${IOS_SIM_SHOT:-}
while kill -0 "$apid" 2>/dev/null; do
	if [ -n "$shot" ] && [ "$t" -eq "$shot" ]; then
		bounded 60 "${tmp:?}/shot2.out" xcrun simctl io "$udid" screenshot "$out/run.png" || echo "ios-sim: no screenshot at $t s" >&2
	fi
	if [ "$t" -ge "$limit" ]; then
		echo "ios-sim: the app ran past $limit s; terminating it" >&2
		bounded 30 "${tmp:?}/term.out" xcrun simctl terminate "$udid" "$bid" || true
		[ "$cmd" = smoke ] && die "the app did not quit within $limit s"
		break
	fi
	sleep 1
	t=$((t + 1))
done
w=0
while kill -0 "$lpid" 2>/dev/null && [ "$w" -lt 30 ]; do sleep 1; w=$((w + 1)); done
kill -0 "$lpid" 2>/dev/null && kill "$lpid" 2>/dev/null
wait "$lpid" 2>/dev/null
lpid=

bounded 60 "${tmp:?}/container.out" xcrun simctl get_app_container "$udid" "$bid" data || die "simctl get_app_container failed"
container=$(tail -n 1 "${tmp:?}/container.out")
[ -d "$container" ] || die "no data container: $container"
log=$(find "$container" -name qconsole.log -type f | head -n 1)
[ -n "$log" ] || die "the engine wrote no qconsole.log in its data container"
cp "$log" "$out/engine.log"
if [ "$cmd" = smoke ]; then
	tga=$(find "$container" -name 'q2_0000.tga' -type f | head -n 1)
	[ -n "$tga" ] && cp "$tga" "$out/q2_0000.tga"
fi

cleanup_device()
{
	bounded 60 "${tmp:?}/shutdown.out" xcrun simctl shutdown "$udid" || true
	bounded 60 "${tmp:?}/delete.out" xcrun simctl delete "$udid" || die "could not delete the device $udid"
	udid=
}
cleanup_device
devices > "$out/devices.after" || die "simctl list devices failed"

if [ "$cmd" = run ]; then
	cmp -s "$out/devices.before" "$out/devices.after" || echo "ios-sim: the device list changed during the run" >&2
	echo "ios-sim: run done (engine log: $out/engine.log)"
	exit 0
fi

# ---- the smoke's decisions, from the artifacts --------------------------

fail=0
bad() { echo "ios-sim: smoke: $*"; fail=1; }
has() { grep -F -e "$1" "$out/engine.log" >/dev/null; }

has "Using the linked-in renderer: ref_metal (linked in)" || bad "engine.log does not show the linked-in ref_metal"
dev=$(grep -E '^Metal device: ' "$out/engine.log" | head -n 1)
fam=$(grep -E '^Metal supportsFamily ' "$out/engine.log" | head -n 1)
[ -n "$dev" ] || bad "engine.log has no Metal device line"
[ -n "$fam" ] || bad "engine.log has no Metal supportsFamily line"
case $fam in
*": none") bad "the simulator's Metal device supports no Apple GPU family from apple5 on, and ref_metal refuses it (its floor): the run stops at renderer init" ;;
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
cmp -s "$out/devices.before" "$out/devices.after" || bad "simctl list devices differs before and after the run"

printf '%s\n' "ios-sim: ${dev:-no Metal device}" "ios-sim: ${fam:-no family line}" "ios-sim: drawable ${size:-?}, screen ${screen:-?}, frame ${frame:-?}"
[ "$fail" -eq 0 ] || exit 1
echo "ios-sim: smoke ok (artifacts in $out)"
