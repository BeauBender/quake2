# scripts/bench/core/snap.sh: what the host is doing around a run.
#
# A snapshot is key=value lines, read without root; a value that cannot be
# read is "unknown":
#   load                  the one-minute load average
#   cpu.busy              the CPU's busy percent over one second (top, its
#                         second sample)
#   mem.free              the free memory percent of the kernel's pressure
#                         level (kern.memorystatus_level)
#   swap.used             swap in use, MB (vm.swapusage)
#   gpu.device            the GPU's device, renderer and tiler utilization,
#   gpu.renderer          percent, and its in-use system memory, MB (ioreg,
#   gpu.tiler             IOAccelerator's PerformanceStatistics)
#   gpu.mem
#   thermal, perf         the thermal and performance warning levels, "none"
#                         when none is recorded (pmset -g therm)
#   lowpower              low-power mode, 0 or 1 (pmset -g)
#   top.1 ... top.5       the processes with the most CPU, "<percent>
#                         <name>": the kernel's name for it (ps ucomm, set
#                         at exec; never the argv[0] it wrote), with no
#                         path, argument or pid, scrubbed of the host's
#                         identity words
# The gate's reading is gpu.device and cpu.busy alone. BENCH_SNAP_READER
# names an executable that prints the lines instead ("<reader> snap",
# "<reader> gate"), for the self-test.
#
# The frequencies come from the probe freq.c beside this file, built on
# first use with the host's cc into the temporary directory, or the
# executable BENCH_FREQ_PROBE names: "<probe> dump" before and after a
# window, then "<probe> delta <before> <after>" (see freq.c).

BENCH_FREQ_BIN=
BENCH_FREQ_WHY=

# bench_snap_system <snap|gate> <out file>: the host's own readings.
bench_snap_system() {
	_sy_o=$2
	_sy_t=${BENCH_TMP:?}/snap.raw
	: > "$_sy_t.top"
	: > "$_sy_t.gpu"
	# The CPU's one-second sample first, then the GPU: the driver's
	# utilization lags (on the M6 it read 100 right after an engine at full
	# load exited and 0 from 0.2 s on, TASK166), so a snapshot after a run
	# reads the GPU past the measured workload's tail.
	if command -v top > /dev/null 2>&1; then
		bench_bounded 15 top -l 2 -n 0 -s 1 > "$_sy_t.top" 2> /dev/null || : > "$_sy_t.top"
	fi
	if command -v ioreg > /dev/null 2>&1; then
		bench_bounded 15 ioreg -r -d 1 -w 0 -c IOAccelerator > "$_sy_t.gpu" 2> /dev/null || : > "$_sy_t.gpu"
	fi
	{
		awk '/^CPU usage:/ { l = $0 } END {
			n = split(l, w, " ")
			for (i = 2; i <= n; i++) if (w[i] ~ /^idle/) { v = w[i - 1]; sub(/%$/, "", v); if (v ~ /^[0-9.]+$/) { printf "cpu.busy=%.1f\n", 100 - v; exit } }
			print "cpu.busy=unknown"
		}' "$_sy_t.top"
		awk -v mode="$1" '
			function val(k,   p, s) {
				p = index($0, "\"" k "\"=")
				if (!p) return ""
				s = substr($0, p + length(k) + 3)
				sub(/[^0-9].*/, "", s)
				return s
			}
			/"PerformanceStatistics"/ && !done {
				d = val("Device Utilization %"); r = val("Renderer Utilization %"); t = val("Tiler Utilization %"); m = val("In use system memory")
				if (d != "") done = 1
			}
			END {
				printf "gpu.device=%s\n", (d != "" ? d : "unknown")
				if (mode == "gate") exit
				printf "gpu.renderer=%s\ngpu.tiler=%s\n", (r != "" ? r : "unknown"), (t != "" ? t : "unknown")
				if (m != "") printf "gpu.mem=%.0f\n", m / 1048576; else print "gpu.mem=unknown"
			}' "$_sy_t.gpu"
	} > "$_sy_o"
	[ "$1" = snap ] || return 0
	{
		printf 'load=%s\n' "$(bench_load)"
		_sy_v=$(sysctl -n kern.memorystatus_level 2> /dev/null) || _sy_v=
		bench_is_num "${_sy_v:-x}" || _sy_v=unknown
		printf 'mem.free=%s\n' "$_sy_v"
		_sy_v=$(sysctl -n vm.swapusage 2> /dev/null | awk '{
			for (i = 1; i < NF; i++) if ($i == "used" && $(i + 1) == "=") {
				v = $(i + 2); u = v; sub(/[0-9.]+/, "", u); sub(/[A-Za-z]+$/, "", v)
				f = (u == "G" ? 1024 : (u == "K" ? 1 / 1024 : 1)); printf "%.0f\n", v * f; exit
			}
		}') || _sy_v=
		printf 'swap.used=%s\n' "${_sy_v:-unknown}"
		_sy_v=$(pmset -g therm 2> /dev/null) || _sy_v=
		for _sy_k in thermal performance; do
			_sy_w=$(printf '%s\n' "$_sy_v" | awk -v k="$_sy_k" '
				tolower($0) ~ "no " k " warning level" { print "none"; exit }
				tolower($0) ~ k " warning level" { for (i = 1; i <= NF; i++) if ($i ~ /^[0-9]+\.?$/) { sub(/\.$/, "", $i); print $i; exit } }')
			[ "$_sy_k" = thermal ] || _sy_k=perf
			printf '%s=%s\n' "$_sy_k" "${_sy_w:-unknown}"
		done
		_sy_v=$(pmset -g 2> /dev/null | awk '$1 == "lowpowermode" && $2 ~ /^[01]$/ { print $2; exit }') || _sy_v=
		printf 'lowpower=%s\n' "${_sy_v:-unknown}"
		bench_identity_words "${BENCH_TMP:?}/snap.words"
		# ucomm, the kernel's name for the process, set at exec (at most 16
		# characters): comm is the argv[0] the process wrote, which on
		# macOS can carry arguments (exec -a).
		ps -A -r -o pcpu= -o ucomm= 2> /dev/null | awk 'NR <= 5 {
			p = $1; sub(/^[ \t]*[^ \t]+[ \t]+/, ""); n = $0; sub(/[ \t]+$/, "", n); sub(/.*\//, "", n); gsub(/[^ -~]/, "?", n)
			printf "top.%d=%s %s\n", NR, p, n
		}' | bench_scrub "${BENCH_TMP:?}/snap.words"
	} >> "$_sy_o"
}

# bench_snap <snap|gate> <out file>: a snapshot (or the gate's reading)
# into <out file>, from BENCH_SNAP_READER or the host. Every line must be
# <key>=<value> with a lower-case dotted key.
bench_snap() {
	if [ -n "${BENCH_SNAP_READER:-}" ]; then
		_sn_st=0
		bench_bounded 30 "$BENCH_SNAP_READER" "$1" > "$2" 2> /dev/null || _sn_st=$?
		[ "$_sn_st" -eq 0 ] || bench_die 3 "the snapshot reader failed (status $_sn_st)"
	else
		bench_snap_system "$1" "$2"
	fi
	awk '!/^[a-z][a-z0-9.]*=/ { bad = 1 } END { exit bad }' "$2" || bench_die 3 "the snapshot reader printed a line that is not <key>=<value>"
}

# bench_snap_source, bench_freq_source: which reader makes the snapshots
# and the frequencies, for the run file's snap.source and freq.source:
# "stub" when BENCH_SNAP_READER or BENCH_FREQ_PROBE stands in for the
# host's, else "system".
bench_snap_source() {
	if [ -n "${BENCH_SNAP_READER:-}" ]; then echo stub; else echo system; fi
}
bench_freq_source() {
	if [ -n "${BENCH_FREQ_PROBE:-}" ]; then echo stub; else echo system; fi
}

# bench_freq_init: the probe, once: BENCH_FREQ_PROBE, else freq.c built
# into the temporary directory. When it cannot be built, BENCH_FREQ_WHY
# says why and every window is "unavailable".
bench_freq_init() {
	[ -z "$BENCH_FREQ_BIN" ] && [ -z "$BENCH_FREQ_WHY" ] || return 0
	if [ -n "${BENCH_FREQ_PROBE:-}" ]; then
		BENCH_FREQ_BIN=$BENCH_FREQ_PROBE
		return 0
	fi
	if [ "$(uname -s 2> /dev/null || true)" != Darwin ]; then
		BENCH_FREQ_WHY=not-macos
		return 0
	fi
	if ! command -v cc > /dev/null 2>&1; then
		BENCH_FREQ_WHY=no-compiler
		return 0
	fi
	_fi_st=0
	bench_bounded 120 cc -O1 -o "${BENCH_TMP:?}/freq" "$BENCH_ROOT/core/freq.c" -framework CoreFoundation -framework IOKit \
		> "${BENCH_TMP:?}/freq.cc" 2>&1 || _fi_st=$?
	if [ "$_fi_st" -eq 0 ]; then
		BENCH_FREQ_BIN=${BENCH_TMP:?}/freq
	else
		BENCH_FREQ_WHY=build-failed
	fi
}

# bench_freq_dump <out file>: the probe's counters now (empty when there is
# no probe or it fails; the window then says so).
bench_freq_dump() {
	bench_freq_init
	: > "$1"
	[ -n "$BENCH_FREQ_BIN" ] || return 0
	bench_bounded 30 "$BENCH_FREQ_BIN" dump > "$1" 2> /dev/null || : > "$1"
}

# bench_freq_window <before> <after> <prefix>: the frequencies between two
# dumps, as <prefix>.<domain>.<key>=<value> lines, or <prefix>=unavailable
# and <prefix>.reason=<word>.
bench_freq_window() {
	_fw_o=${BENCH_TMP:?}/freq.window
	: > "$_fw_o"
	if [ -n "$BENCH_FREQ_WHY" ]; then
		printf '%s=unavailable\n%s.reason=%s\n' "$3" "$3" "$BENCH_FREQ_WHY"
		return 0
	fi
	# A dump in which the probe said why it could not read the host
	# (freq=unavailable reason=<word>) gives the window that reason, the
	# before dump's first: the delta of such a dump would say only no-dump.
	_fw_r=$(awk '/^freq=unavailable reason=[a-z0-9-]+$/ { sub(/^freq=unavailable reason=/, ""); print; exit }' "$1" "$2" 2> /dev/null) || _fw_r=
	if [ -n "$_fw_r" ]; then
		printf '%s=unavailable\n%s.reason=%s\n' "$3" "$3" "$_fw_r"
		return 0
	fi
	if [ -n "$BENCH_FREQ_BIN" ]; then
		bench_bounded 30 "$BENCH_FREQ_BIN" delta "$1" "$2" > "$_fw_o" 2> /dev/null || : > "$_fw_o"
	fi
	awk -v p="$3" '
		/^freq=unavailable( reason=[a-z0-9-]+)?$/ { r = $2; sub(/^reason=/, "", r); un = (r != "" ? r : "unknown"); next }
		/^freq\.[a-z0-9_]+\.(mhz|top|active|max)=[0-9]+(\.[0-9]+)?$/ { out[++n] = p substr($0, 5); next }
		{ un = "bad-line" }
		END {
			if (un == "" && n == 0) un = "no-reading"
			if (un != "") { printf "%s=unavailable\n%s.reason=%s\n", p, p, un; exit }
			for (i = 1; i <= n; i++) print out[i]
		}' "$_fw_o"
}

# bench_snap_warning <snapshot file>: prints "thermal" or "perf" when the
# snapshot shows a thermal or performance warning (a level other than
# none, 0 or unknown); nothing otherwise.
bench_snap_warning() {
	for _sw_k in thermal perf; do
		case $(bench_file_kv "$1" "$_sw_k") in
		'' | none | 0 | unknown) ;;
		*)
			printf '%s\n' "$_sw_k"
			return 0
			;;
		esac
	done
}

# bench_case_uint <key> <default>: the case's value of <key>, or
# <default>; bench_case_load checked that it is whole.
bench_case_uint() {
	_cu=$(bench_case_get "$1")
	printf '%s\n' "${_cu:-$2}"
}

# bench_gate: before a counted run, waits until the GPU's device
# utilization is at most gate_gpu (default 10) and the CPU's busy percent at
# most gate_cpu (default 25), both for gate_hold seconds in a row (default
# 3), for at most gate_wait seconds (default 120), reading every
# BENCH_GATE_POLL seconds (default 1; bench.sh checked it, at least 0.01),
# or the time left when that is shorter. Sets BENCH_GATE_WAITED (the seconds
# it waited) and BENCH_GATE_WHY: empty when the host was quiet, "gate" when
# the wait ran out, "unread" when a reading is unknown (it stops at once).
bench_gate() {
	_gt_gpu=$(bench_case_uint gate_gpu 10)
	_gt_cpu=$(bench_case_uint gate_cpu 25)
	_gt_hold=$(bench_case_uint gate_hold 3)
	_gt_wait=$(bench_case_uint gate_wait 120)
	_gt_t0=$(date +%s)
	_gt_since=
	BENCH_GATE_WHY=
	while :; do
		bench_snap gate "${BENCH_TMP:?}/gate"
		_gt_now=$(date +%s)
		_gt_g=$(bench_file_kv "${BENCH_TMP:?}/gate" gpu.device)
		_gt_c=$(bench_file_kv "${BENCH_TMP:?}/gate" cpu.busy)
		if ! bench_is_num "${_gt_g:-x}" || ! bench_is_num "${_gt_c:-x}"; then
			BENCH_GATE_WHY=unread
			break
		fi
		if awk -v g="$_gt_g" -v c="$_gt_c" -v lg="$_gt_gpu" -v lc="$_gt_cpu" 'BEGIN { exit !(g <= lg && c <= lc) }'; then
			[ -n "$_gt_since" ] || _gt_since=$_gt_now
			[ $((_gt_now - _gt_since)) -lt "$_gt_hold" ] || break
		else
			_gt_since=
		fi
		if [ $((_gt_now - _gt_t0)) -ge "$_gt_wait" ]; then
			BENCH_GATE_WHY=gate
			break
		fi
		# Each nap at most the time left (whole seconds, at least 1 here),
		# so the gate never waits past gate_wait and one reading.
		_gt_nap=${BENCH_GATE_POLL:-1}
		_gt_left=$((_gt_wait - (_gt_now - _gt_t0)))
		if awk -v p="$_gt_nap" -v l="$_gt_left" 'BEGIN { exit !(p > l) }'; then _gt_nap=$_gt_left; fi
		bench_nap "$_gt_nap"
	done
	BENCH_GATE_WAITED=$(($(date +%s) - _gt_t0))
}

# bench_quiet_flag <before> <after>: prints quiet=yes|no and quiet.reason
# for a run: BENCH_GATE_WHY, then gpu.before and gpu.after when that
# snapshot's gpu.device is over gate_gpu (the measured workload runs in
# neither, so that use is another client's) or unknown.
bench_quiet_flag() {
	_qf_r=$BENCH_GATE_WHY
	_qf_l=$(bench_case_uint gate_gpu 10)
	for _qf_s in before after; do
		if [ "$_qf_s" = before ]; then _qf_f=$1; else _qf_f=$2; fi
		_qf_g=$(bench_file_kv "$_qf_f" gpu.device)
		if ! bench_is_num "${_qf_g:-x}" || awk -v g="$_qf_g" -v l="$_qf_l" 'BEGIN { exit !(g > l) }'; then
			_qf_r=${_qf_r:+$_qf_r,}gpu.$_qf_s
		fi
	done
	if [ -z "$_qf_r" ]; then
		printf 'quiet=yes\nquiet.reason=none\n'
	else
		printf 'quiet=no\nquiet.reason=%s\n' "$_qf_r"
	fi
}
