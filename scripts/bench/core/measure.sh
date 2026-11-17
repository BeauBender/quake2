# scripts/bench/core/measure.sh: one measurement through an adapter.
#
# An adapter is scripts/bench/adapters/<name>.sh, sourced, defining:
#   adapter_preflight <case>             prints pre.<name>=<value> lines;
#                                        non-zero refuses the run
#   adapter_build <commit> <dir> <targets...>
#                                        builds the commit into <dir>
#   adapter_run <case> <build dir> <log> one run, bounded by the case's wall
#                                        time (bench_bounded helps)
#   adapter_metrics <case> <log>         prints metric.<name>=<number> lines
# and may define
#   adapter_inputs_paths <commit>        prints the paths of the commit the
#                                        build compiles, one per line, for
#                                        the inputs id (inputs.sh); without
#                                        it, or with no path, the inputs are
#                                        the whole tree
#   adapter_prewarm <case> <build dir> <log> <seconds>
#                                        one piece of the pre-warm's
#                                        workload, at most <seconds> long;
#                                        without it the pre-warm runs
#                                        adapter_run
# <case> is the normalised case file; bench_case_get reads it. Each run
# sees BENCH_RUN_KIND (prewarm, warmup or run) and BENCH_RUN_INDEX (from
# 1). The caller holds the lock for the whole measurement.
#
# A run file holds, in this order: case, case_hash, commit, inputs, host,
# class, hash, kind, index, start, load.before, load.after, quiet,
# quiet.reason, gate.wait, snap.source (system, or stub for
# BENCH_SNAP_READER), every snap.before.* and snap.after.* line (the
# snapshots just before and after the run, snap.sh), freq.source (system,
# or stub for BENCH_FREQ_PROBE), the frequencies over the run (freq.*),
# the pre-warm's lines (prewarm.*, the same in every run
# of a measurement), every pre.* line, every metric.* line.

BENCH_BUILD_DIR=
BENCH_INPUTS=

# bench_adapter_load: sources the loaded case's adapter.
bench_adapter_load() {
	_al_n=$(bench_case_get adapter)
	_al_f=$BENCH_ROOT/adapters/$_al_n.sh
	[ -f "$_al_f" ] || bench_die 2 "no adapter $_al_n"
	. "$_al_f"
	for _al_fn in adapter_preflight adapter_build adapter_run adapter_metrics; do
		type "$_al_fn" > /dev/null 2>&1 || bench_die 2 "the adapter $_al_n does not define $_al_fn"
	done
}

# bench_lines_ok <prefix> <file>: every line of <file> is
# <prefix>.<name>=<value>; for "metric" the value is a decimal number.
bench_lines_ok() {
	awk -v p="$1" '
		{
			e = index($0, "=")
			name = substr($0, 1, e - 1)
			if (e < 2 || index(name, p ".") != 1 || substr(name, length(p) + 2) !~ /^[A-Za-z0-9_][A-Za-z0-9._-]*$/) bad = 1
			if (p == "metric" && substr($0, e + 1) !~ /^-?[0-9]+(\.[0-9]+)?$/) bad = 1
		}
		END { exit bad }' "$2"
}

# bench_prepare: preflight and build, once per measurement. Sets
# BENCH_BUILD_DIR; the preflight lines are kept in ${BENCH_TMP:?}/pre.
bench_prepare() {
	_bp_st=0
	adapter_preflight "$BENCH_CASE_FILE" > "${BENCH_TMP:?}/pre" || _bp_st=$?
	[ "$_bp_st" -eq 0 ] || bench_die 3 "the adapter refused the run in its preflight (status $_bp_st)"
	bench_lines_ok pre "${BENCH_TMP:?}/pre" || bench_die 3 "the preflight printed a line that is not pre.<name>=<value>"
	BENCH_BUILD_DIR=${BENCH_TMP:?}/build
	mkdir -p "$BENCH_BUILD_DIR"
	_bp_t=$(bench_case_get targets)
	set -f
	# shellcheck disable=SC2086 # the targets are words; globbing is off
	set -- $_bp_t
	set +f
	adapter_build "$BENCH_COMMIT" "$BENCH_BUILD_DIR" "$@" || _bp_st=$?
	[ "$_bp_st" -eq 0 ] || bench_die 3 "the adapter build failed (status $_bp_st)"
}

BENCH_PREWARMED=

# bench_prewarm: the pre-warm, once per process, before the first warm-up
# run: the adapter's workload (adapter_prewarm, else adapter_run) in pieces
# until the case's prewarm seconds (default 240; 0 none) have passed. A
# snapshot before each piece and after the last stops it at the first
# thermal or performance warning. Writes the prewarm.* lines every run file
# of the measurement carries to ${BENCH_TMP:?}/prewarm: the target, the
# seconds it ran, the pieces, why it stopped (time, thermal, perf; none
# without a pre-warm) and the frequencies over its last piece.
bench_prewarm() {
	# The probe first, at the top of the measuring path: without a pre-warm
	# its build (cc, up to 120 s) would otherwise fall between the before
	# snapshot and the first counted run.
	bench_freq_init
	[ -z "$BENCH_PREWARMED" ] || return 0
	BENCH_PREWARMED=1
	_pw_s=$(bench_case_uint prewarm 240)
	_pw_t0=$(date +%s)
	_pw_n=0
	_pw_stop=time
	[ "$_pw_s" -gt 0 ] || _pw_stop=none
	while [ "$_pw_stop" = time ]; do
		bench_snap snap "${BENCH_TMP:?}/prewarm.snap"
		_pw_w=$(bench_snap_warning "${BENCH_TMP:?}/prewarm.snap")
		if [ -n "$_pw_w" ]; then
			_pw_stop=$_pw_w
			bench_note "the pre-warm stopped after $(($(date +%s) - _pw_t0)) s of $_pw_s: a $_pw_w warning"
			break
		fi
		_pw_left=$((_pw_s - ($(date +%s) - _pw_t0)))
		[ "$_pw_left" -gt 0 ] || break
		_pw_n=$((_pw_n + 1))
		BENCH_RUN_KIND=prewarm
		BENCH_RUN_INDEX=$_pw_n
		export BENCH_RUN_KIND BENCH_RUN_INDEX
		bench_freq_dump "${BENCH_TMP:?}/prewarm.a"
		_pw_st=0
		if type adapter_prewarm > /dev/null 2>&1; then
			adapter_prewarm "$BENCH_CASE_FILE" "$BENCH_BUILD_DIR" "${BENCH_TMP:?}/log.prewarm" "$_pw_left" || _pw_st=$?
		else
			adapter_run "$BENCH_CASE_FILE" "$BENCH_BUILD_DIR" "${BENCH_TMP:?}/log.prewarm" || _pw_st=$?
		fi
		[ "$_pw_st" -eq 0 ] || bench_die 3 "pre-warm $_pw_n failed (status $_pw_st)"
		bench_freq_dump "${BENCH_TMP:?}/prewarm.b"
	done
	{
		printf 'prewarm.target=%s\nprewarm.seconds=%s\nprewarm.runs=%s\nprewarm.stop=%s\n' \
			"$_pw_s" "$(($(date +%s) - _pw_t0))" "$_pw_n" "$_pw_stop"
		if [ "$_pw_n" -gt 0 ]; then
			bench_freq_window "${BENCH_TMP:?}/prewarm.a" "${BENCH_TMP:?}/prewarm.b" prewarm.freq
		else
			printf 'prewarm.freq=unavailable\nprewarm.freq.reason=no-prewarm\n'
		fi
	} > "${BENCH_TMP:?}/prewarm"
}

# bench_run_one <kind> <index> <out file>: one run; for kind "run" writes
# the run file to <out file>, after the quiet gate (bench_gate) and with a
# snapshot and the frequencies around the run.
bench_run_one() {
	BENCH_RUN_KIND=$1
	BENCH_RUN_INDEX=$2
	export BENCH_RUN_KIND BENCH_RUN_INDEX
	_ro_log=${BENCH_TMP:?}/log.$1.$2
	if [ "$1" = run ]; then
		bench_gate
		bench_snap snap "${BENCH_TMP:?}/snap.before"
		bench_freq_dump "${BENCH_TMP:?}/freq.before"
	fi
	_ro_lb=$(bench_load)
	_ro_t=$(bench_utc)
	_ro_st=0
	adapter_run "$BENCH_CASE_FILE" "$BENCH_BUILD_DIR" "$_ro_log" || _ro_st=$?
	[ "$_ro_st" -eq 0 ] || bench_die 3 "$1 $2 failed (status $_ro_st)"
	[ "$1" = run ] || return 0
	bench_freq_dump "${BENCH_TMP:?}/freq.after"
	bench_snap snap "${BENCH_TMP:?}/snap.after"
	adapter_metrics "$BENCH_CASE_FILE" "$_ro_log" > "${BENCH_TMP:?}/metrics" || _ro_st=$?
	[ "$_ro_st" -eq 0 ] || bench_die 3 "the adapter could not read the metrics of run $2 (status $_ro_st)"
	bench_lines_ok metric "${BENCH_TMP:?}/metrics" || bench_die 3 "run $2 printed a line that is not metric.<name>=<number>"
	_ro_m=$(bench_case_get metric)
	[ -n "$(bench_file_kv "${BENCH_TMP:?}/metrics" "metric.$_ro_m")" ] || bench_die 3 "run $2 has no value for the metric $_ro_m"
	{
		printf 'case=%s\ncase_hash=%s\ncommit=%s\ninputs=%s\n' "$BENCH_CASE_NAME" "$BENCH_CASE_HASH" "$BENCH_COMMIT" "$BENCH_INPUTS"
		printf 'host=%s\nclass=%s\nhash=%s\n' "$BENCH_HOST_TOKEN" "$BENCH_HOST_CLASS" "$(bench_hash_tool)"
		printf 'kind=%s\nindex=%s\nstart=%s\nload.before=%s\nload.after=%s\n' "$1" "$2" "$_ro_t" "$_ro_lb" "$(bench_load)"
		bench_quiet_flag "${BENCH_TMP:?}/snap.before" "${BENCH_TMP:?}/snap.after"
		printf 'gate.wait=%s\nsnap.source=%s\n' "$BENCH_GATE_WAITED" "$(bench_snap_source)"
		sed 's/^/snap.before./' "${BENCH_TMP:?}/snap.before"
		sed 's/^/snap.after./' "${BENCH_TMP:?}/snap.after"
		printf 'freq.source=%s\n' "$(bench_freq_source)"
		bench_freq_window "${BENCH_TMP:?}/freq.before" "${BENCH_TMP:?}/freq.after" freq
		cat "${BENCH_TMP:?}/prewarm" "${BENCH_TMP:?}/pre" "${BENCH_TMP:?}/metrics"
	} > "$3"
}

# bench_measure <runs> <out dir>: the pre-warm (once per process), the
# case's warm-up runs, then <runs> counted runs, written to
# <out dir>/<k>.run.
bench_measure() {
	bench_prewarm
	_bm_w=$(bench_case_get warmup)
	_bm_k=1
	while [ "$_bm_k" -le "${_bm_w:-0}" ]; do
		bench_run_one warmup "$_bm_k" /dev/null
		_bm_k=$((_bm_k + 1))
	done
	mkdir -p "${2:?}"
	_bm_k=1
	while [ "$_bm_k" -le "$1" ]; do
		bench_run_one run "$_bm_k" "${2:?}/$_bm_k.run"
		_bm_k=$((_bm_k + 1))
	done
}
