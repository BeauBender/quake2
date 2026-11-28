#!/bin/sh
# scripts/bench/bench.sh: the one entry point of the bench harness.
#
# Quiet, repeatable measurements: a host-wide lock so one heavy job runs at
# a time, a host token and a build-inputs id that say what a result belongs
# to, and a per-host store of measured baselines with drift and compare.
# The plan, the store layout and the rules are in
# docs/plans/bench-harness.md; this script and scripts/bench/core/ name no
# game (tests/check-bench.sh checks it), adapters and cases do.
#
# Usage:
#   bench.sh lock run [--quiet] <label> -- <command> [args...]
#   bench.sh lock status
#   bench.sh host [--facts]
#   bench.sh inputs [--list] <commit> [<case>]
#   bench.sh case <case>
#   bench.sh baseline get <case> <commit>
#   bench.sh baseline put [--replace] <case> <commit>
#   bench.sh drift <case> <commit>
#   bench.sh compare <case> --before <commit> --after <commit>
#   bench.sh stats                       (numbers on standard input)
#
# <case> is a name (scripts/bench/cases/<name>.case) or a path with a "/".
# Commits are read in the repository of the current directory.
#
# Exit status: 0 done; 1 compare's verdict failed, or baseline get, drift
# or compare found no baseline, or baseline put found one already; 2 a
# usage, case, commit or setting error; 3 the adapter refused or failed;
# 75 the lock wait ran out; 125 the supervisor of a command run under the
# lock could not empty its group (the lock is kept while it has a member);
# a command run under the lock otherwise exits with its own status.
#
# Environment: BENCH_STATE (lock state directory), BENCH_STORE (result
# store), BENCH_LOCK_WAIT (seconds, default 3600), BENCH_LOCK_POLL (seconds,
# default 2, at least 0.01), BENCH_LOCK_GRACE (seconds, default 10),
# BENCH_STOP_GRACE (seconds a stopped command gets between TERM and KILL,
# default 10; the lock gives its command twice that, and at least that
# plus 5), BENCH_LOCK_TOKEN (set
# by the lock for its command), BENCH_HASH (shasum or cksum),
# BENCH_QUIET_TTL (seconds a quiet marker lasts, default 3600),
# BENCH_SESSION_ID (the session the marker names, else
# CLAUDE_CODE_SESSION_ID), BENCH_GATE_POLL (seconds between the quiet
# gate's readings, default 1, at least 0.01), BENCH_SNAP_READER and
# BENCH_FREQ_PROBE (stand-ins for the host's snapshot reader and the
# frequency probe, for the self-test; a run file names them as
# snap.source=stub and freq.source=stub). A run set pre-warms, gates and records the host around
# each run (core/snap.sh, core/measure.sh; docs/plans/bench-harness.md,
# "Determinism"); baseline put refuses a set with a run that was not quiet.
set -eu
LC_ALL=C
export LC_ALL

BENCH_ROOT=$(cd "$(dirname "$0")" && pwd -P)
BENCH_SELF=$BENCH_ROOT/bench.sh

. "$BENCH_ROOT/core/common.sh"
. "$BENCH_ROOT/core/lock.sh"
. "$BENCH_ROOT/core/host.sh"
. "$BENCH_ROOT/core/inputs.sh"
. "$BENCH_ROOT/core/case.sh"
. "$BENCH_ROOT/core/store.sh"
. "$BENCH_ROOT/core/stats.sh"
. "$BENCH_ROOT/core/snap.sh"
. "$BENCH_ROOT/core/measure.sh"

bench_on_exit() {
	_oe_st=$?
	bench_lock_release
	if [ -n "${BENCH_LOCK_GUARD:-}" ]; then rmdir "${BENCH_LOCK_GUARD:?}" 2> /dev/null || true; fi
	if [ -n "${BENCH_TMP:-}" ]; then rm -rf "${BENCH_TMP:?}"; fi
	exit "$_oe_st"
}

# bench_on_signal <status>: stops what this process started (the command
# under the lock, or a bounded command: each runs under its supervisor,
# TASK221), then exits; the EXIT trap releases the lock after that. The
# stop sends the supervisor TERM and waits for it (bench_stop_group), also
# in sv_start's fork window (sv_pid), so the lock is never freed before the
# supervisor's "reaped" event. A group it could not empty (its 126) is
# recorded in the lock state first (FIX184), so the lock is kept while that
# group has a member, whatever ends this shell.
bench_on_signal() {
	trap '' INT TERM HUP
	if [ -n "${BENCH_BOUNDED_PID:-}" ]; then bench_stop_group "$BENCH_BOUNDED_PID" || :; fi
	exit "$1"
}

exec 8>&-
trap bench_on_exit EXIT
trap 'bench_on_signal 129' HUP
trap 'bench_on_signal 130' INT
trap 'bench_on_signal 143' TERM

usage() {
	sed -n '/^# Usage:/,/^# <case>/p' "$BENCH_SELF" | sed '$d; s/^# \{0,1\}//' >&2
	exit 2
}

case ${BENCH_HASH:-shasum} in
shasum | cksum) ;;
*) bench_die 2 "BENCH_HASH is shasum or cksum" ;;
esac
bench_is_uint "${BENCH_STOP_GRACE:-10}" || bench_die 2 "BENCH_STOP_GRACE is whole seconds"
# The quiet marker's lifetime and the gate's poll, before the lock is taken
# or anything runs (bench_quiet_mark and bench_gate read them later).
bench_is_uint "${BENCH_QUIET_TTL:-3600}" || bench_die 2 "BENCH_QUIET_TTL is whole seconds"
if ! bench_is_dec "${BENCH_GATE_POLL:-1}" || ! awk -v p="${BENCH_GATE_POLL:-1}" 'BEGIN { exit !(p >= 0.01) }'; then
	bench_die 2 "BENCH_GATE_POLL is a number of seconds, at least 0.01"
fi

# need_lock <label> <args...>: unless this process holds the lock already,
# becomes (exec) a quiet lock shell (lock run --quiet) that runs this
# script with <args> under the lock, so a signal to this process reaches
# the lock shell's trap. Under a hold of its own it writes the quiet
# marker when no live one is there, and removes it when it exits.
need_lock() {
	_nl_label=$1
	shift
	bench_lock_init
	if bench_lock_mine; then
		bench_quiet_mark "$_nl_label" "$$"
		return 0
	fi
	if [ -n "${BENCH_TMP:-}" ]; then
		rm -rf "${BENCH_TMP:?}"
		BENCH_TMP=
	fi
	exec sh "$BENCH_SELF" lock run --quiet "$_nl_label" -- sh "$BENCH_SELF" "$@" < /dev/null
}

# not_quiet <runs dir> <which>: says which runs of the <which> set of a
# measurement were not quiet (any flag but quiet=yes). Succeeds (status 0)
# when there is one, so "if not_quiet ...; then refuse"; fails when every
# run is quiet.
not_quiet() {
	_nq_bad=
	for _nq_f in "${1:?}"/*.run; do
		[ -f "$_nq_f" ] || continue
		if [ "$(bench_file_kv "$_nq_f" quiet)" != yes ]; then
			_nq_k=${_nq_f##*/}
			bench_note "the $2 set: run ${_nq_k%.run} was not quiet (quiet.reason=$(bench_file_kv "$_nq_f" quiet.reason))"
			_nq_bad=1
		fi
	done
	[ -n "$_nq_bad" ]
}

# quiet_of <set>: yes when every run of the set is quiet=yes, no when one
# is quiet=no, unknown when a run has no flag (a set stored before runs
# carried one); "none" for no set.
quiet_of() {
	[ -n "$1" ] || { echo none; return 0; }
	bench_store_runs "$1" | while IFS= read -r _qo_f; do printf '%s\n' "$(bench_file_kv "$_qo_f" quiet)"; done |
		awk '$0 == "no" { no = 1 } $0 != "yes" && $0 != "no" { un = 1 } END { print (no ? "no" : (un ? "unknown" : "yes")) }'
}

# source_of <set or run file>: the readers of the set's runs, or of the one
# run file: stub when a run's snap.source or freq.source is stub (a
# stand-in of the self-test made it), unknown when a run has no such line
# (a set stored before runs carried them), else system; "none" for no set.
source_of() {
	[ -n "$1" ] || { echo none; return 0; }
	if [ -f "$1" ]; then printf '%s\n' "$1"; else bench_store_runs "$1"; fi | while IFS= read -r _so_f; do
		printf '%s\n%s\n' "$(bench_file_kv "$_so_f" snap.source)" "$(bench_file_kv "$_so_f" freq.source)"
	done | awk '$0 == "stub" { st = 1 } $0 != "stub" && $0 != "system" { un = 1 } END { print (st ? "stub" : (un ? "unknown" : "system")) }'
}

# setup <case> <commit>: loads the case and its adapter, the host, the
# commit, its inputs and the store; sets DIR, the baseline directory, and
# LIVE, its live set (empty when there is none). A reader resolves LIVE
# once and reads only in it.
setup() {
	bench_case_load "$1"
	bench_adapter_load
	bench_commit "$2"
	bench_host_init
	bench_store_init
	BENCH_INPUTS=$(bench_inputs "$BENCH_COMMIT" "$(bench_case_get build_flags)") || exit "$?"
	DIR=$(bench_store_dir "$BENCH_INPUTS")
	LIVE=$(bench_store_live "$DIR")
}

# summary <set>: one stats line per metric of the set <set>.
summary() {
	for _su_m in $(bench_store_metrics "$1"); do
		printf 'metric=%s %s\n' "$_su_m" "$(bench_store_values "$_su_m" "$1" | bench_stats)"
	done
}

# measure_baseline: measures a full baseline into ${BENCH_TMP:?}/new and
# prints nothing; the caller stores it.
measure_baseline() {
	rm -rf "${BENCH_TMP:?}/new"
	bench_measure "$(bench_case_get runs)" "${BENCH_TMP:?}/new"
}

# store_new: stores the runs measured into ${BENCH_TMP:?}/new as the new
# live set of DIR (the old one, if any, stays as the set it superseded),
# and points LIVE at it.
store_new() {
	_sn_old=$LIVE
	bench_store_put "$DIR" "${BENCH_TMP:?}/new"
	if [ -n "$_sn_old" ]; then printf 'superseded=%s\n' "${_sn_old##*/}"; fi
	LIVE=$DIR/$BENCH_STORE_SET
}

cmd_baseline_get() {
	[ "$#" -eq 2 ] || usage
	setup "$1" "$2"
	_bg_n=$(bench_store_count "$LIVE")
	if [ "$_bg_n" -eq 0 ]; then
		printf 'baseline=none case=%s inputs=%s host=%s\n' "$BENCH_CASE_NAME" "$BENCH_INPUTS" "$BENCH_HOST_TOKEN"
		exit 1
	fi
	printf 'baseline=present case=%s inputs=%s host=%s runs=%d\n' "$BENCH_CASE_NAME" "$BENCH_INPUTS" "$BENCH_HOST_TOKEN" "$_bg_n"
	summary "$LIVE"
}

cmd_baseline_put() {
	_bp_replace=
	if [ "${1:-}" = --replace ]; then
		_bp_replace=--replace
		shift
	fi
	[ "$#" -eq 2 ] || usage
	# The case and the commit are checked before the wait for the lock; the
	# rest, the existing baseline included, under it.
	bench_case_load "$1"
	bench_commit "$2"
	if [ -n "$_bp_replace" ]; then
		need_lock "bench.$BENCH_CASE_NAME" baseline put --replace "$1" "$BENCH_COMMIT"
	else
		need_lock "bench.$BENCH_CASE_NAME" baseline put "$1" "$BENCH_COMMIT"
	fi
	setup "$1" "$2"
	_bp_n=$(bench_store_count "$LIVE")
	if [ "$_bp_n" -gt 0 ] && [ -z "$_bp_replace" ]; then
		bench_note "a baseline exists for the case $BENCH_CASE_NAME at $BENCH_INPUTS ($_bp_n runs); check it with drift, or replace it with baseline put --replace"
		exit 1
	fi
	bench_prepare
	measure_baseline
	if not_quiet "${BENCH_TMP:?}/new" new; then
		bench_note "a set with a run that was not quiet is not stored; the case $BENCH_CASE_NAME at $BENCH_INPUTS keeps the baseline it had"
		exit 1
	fi
	store_new
	printf 'baseline=stored case=%s inputs=%s host=%s runs=%d\n' "$BENCH_CASE_NAME" "$BENCH_INPUTS" "$BENCH_HOST_TOKEN" "$(bench_store_count "$LIVE")"
	summary "$LIVE"
}

cmd_drift() {
	[ "$#" -eq 2 ] || usage
	bench_case_load "$1"
	bench_commit "$2"
	need_lock "bench.$BENCH_CASE_NAME" drift "$1" "$BENCH_COMMIT"
	setup "$1" "$2"
	if [ "$(bench_store_count "$LIVE")" -eq 0 ]; then
		bench_note "no baseline for the case $BENCH_CASE_NAME at $BENCH_INPUTS; measure one with baseline put"
		exit 1
	fi
	bench_prepare
	bench_prewarm
	_dr_w=$(bench_case_get warmup)
	_dr_k=1
	while [ "$_dr_k" -le "${_dr_w:-0}" ]; do
		bench_run_one warmup "$_dr_k" /dev/null
		_dr_k=$((_dr_k + 1))
	done
	bench_run_one run 1 "${BENCH_TMP:?}/drift.run"
	_dr_m=$(bench_case_get metric)
	_dr_x=$(bench_file_kv "${BENCH_TMP:?}/drift.run" "metric.$_dr_m")
	_dr_line=$(bench_store_values "$_dr_m" "$LIVE" |
		bench_drift_rule "$_dr_x" "$(bench_case_get abs_tol)" "$(bench_case_get rel_tol)") ||
		bench_die 2 "the stored runs of $BENCH_CASE_NAME have no usable $_dr_m values"
	bench_store_write "$DIR/drift" "${BENCH_TMP:?}/drift.run"
	# The drift run's quiet flag (and its reason when it is not quiet) and
	# readers, and the stored set's (quiet_of, source_of).
	_dr_q=$(bench_file_kv "${BENCH_TMP:?}/drift.run" quiet)
	[ "$_dr_q" = yes ] || _dr_q="no quiet.reason=$(bench_file_kv "${BENCH_TMP:?}/drift.run" quiet.reason)"
	printf '%s metric=%s quiet=%s source=%s baseline.quiet=%s baseline.source=%s\n' "$_dr_line" "$_dr_m" "$_dr_q" \
		"$(source_of "${BENCH_TMP:?}/drift.run")" "$(quiet_of "$LIVE")" "$(source_of "$LIVE")"
	case $_dr_line in
	drift=inside*) return 0 ;;
	esac
	measure_baseline
	if not_quiet "${BENCH_TMP:?}/new" re-measured; then
		bench_note "the re-measured set has a run that was not quiet, so it is not stored; the baseline stands"
		exit 1
	fi
	store_new
	printf 'baseline=replaced case=%s inputs=%s runs=%d\n' "$BENCH_CASE_NAME" "$BENCH_INPUTS" "$(bench_store_count "$LIVE")"
	summary "$LIVE"
}

cmd_compare() {
	[ "$#" -ge 1 ] || usage
	_cp_case=$1
	shift
	_cp_before=
	_cp_after=
	while [ "$#" -gt 0 ]; do
		case $1 in
		--before) [ "$#" -ge 2 ] || usage; _cp_before=$2; shift 2 ;;
		--after) [ "$#" -ge 2 ] || usage; _cp_after=$2; shift 2 ;;
		*) usage ;;
		esac
	done
	[ -n "$_cp_before" ] && [ -n "$_cp_after" ] || usage
	setup "$_cp_case" "$_cp_before"
	_cp_bc=$BENCH_COMMIT
	_cp_bi=$BENCH_INPUTS
	_cp_bd=$LIVE
	bench_commit "$_cp_after"
	_cp_ac=$BENCH_COMMIT
	_cp_ai=$(bench_inputs "$BENCH_COMMIT" "$(bench_case_get build_flags)") || exit "$?"
	_cp_ad=$(bench_store_live "$(bench_store_dir "$_cp_ai")")
	for _cp_side in before after; do
		if [ "$_cp_side" = before ]; then _cp_d=$_cp_bd; _cp_r=$_cp_before; else _cp_d=$_cp_ad; _cp_r=$_cp_after; fi
		if [ "$(bench_store_count "$_cp_d")" -eq 0 ]; then
			bench_note "no baseline for --$_cp_side $_cp_r; measure it with: bench.sh baseline put $BENCH_CASE_NAME $_cp_r"
			exit 1
		fi
	done
	printf 'compare case=%s host=%s class=%s\n' "$BENCH_CASE_NAME" "$BENCH_HOST_TOKEN" "$BENCH_HOST_CLASS"
	printf 'before=%s inputs=%s\n' "$_cp_bc" "$_cp_bi"
	printf 'after=%s inputs=%s\n' "$_cp_ac" "$_cp_ai"
	[ "$_cp_bi" != "$_cp_ai" ] || echo "note=the two commits have the same inputs, so one baseline"
	printf 'quiet before=%s after=%s\n' "$(quiet_of "$_cp_bd")" "$(quiet_of "$_cp_ad")"
	printf 'source before=%s after=%s\n' "$(source_of "$_cp_bd")" "$(source_of "$_cp_ad")"
	printf '%-16s %-6s %4s %12s %12s %12s %12s\n' metric side runs median p90 min max
	_cp_bm=
	_cp_am=
	for _cp_m in $(bench_store_metrics "$_cp_bd" "$_cp_ad"); do
		for _cp_side in before after; do
			if [ "$_cp_side" = before ]; then _cp_d=$_cp_bd; else _cp_d=$_cp_ad; fi
			_cp_s=$(bench_store_values "$_cp_m" "$_cp_d" | bench_stats 2> /dev/null) || _cp_s=
			if [ -z "$_cp_s" ]; then
				printf '%-16s %-6s %4s %12s %12s %12s %12s\n' "$_cp_m" "$_cp_side" 0 - - - -
				continue
			fi
			printf '%s\n' "$_cp_s" | awk -v m="$_cp_m" -v side="$_cp_side" '{
				for (i = 1; i <= NF; i++) { e = index($i, "="); f[substr($i, 1, e - 1)] = substr($i, e + 1) }
				printf "%-16s %-6s %4s %12s %12s %12s %12s\n", m, side, f["n"], f["median"], f["p90"], f["min"], f["max"]
			}'
			if [ "$_cp_m" = "$(bench_case_get metric)" ]; then
				_cp_med=$(printf '%s\n' "$_cp_s" | sed 's/.* median=\([^ ]*\) .*/\1/')
				if [ "$_cp_side" = before ]; then _cp_bm=$_cp_med; else _cp_am=$_cp_med; fi
			fi
		done
	done
	_cp_acc=$(bench_case_get accept)
	[ -n "$_cp_acc" ] || return 0
	[ -n "$_cp_bm" ] && [ -n "$_cp_am" ] || bench_die 1 "verdict=undefined: a side has no $(bench_case_get metric) values"
	bench_verdict "$(bench_case_get metric)" "$(bench_case_get better)" "$_cp_acc" "$_cp_bm" "$_cp_am"
}

cmd_inputs() {
	_in_list=
	if [ "${1:-}" = --list ]; then
		_in_list=1
		shift
	fi
	[ "$#" -ge 1 ] && [ "$#" -le 2 ] || usage
	_in_flags=
	if [ "$#" -eq 2 ]; then
		bench_case_load "$2"
		bench_adapter_load
		_in_flags=$(bench_case_get build_flags)
	fi
	bench_commit "$1"
	if [ -n "$_in_list" ]; then
		_in_m=$(bench_inputs_material "$BENCH_COMMIT" "$_in_flags") || exit "$?"
		printf '%s\nhash=%s\n' "$_in_m" "$(bench_hash_tool)"
	fi
	_in_id=$(bench_inputs "$BENCH_COMMIT" "$_in_flags") || exit "$?"
	printf '%s\n' "$_in_id"
}

cmd_host() {
	bench_host_init
	if [ "${1:-}" = --facts ]; then
		printf '%s\nhash=%s\n' "$BENCH_HOST_FACTS" "$(bench_hash_tool)"
	elif [ "$#" -ne 0 ]; then
		usage
	fi
	printf '%s\n%s\n' "$BENCH_HOST_TOKEN" "$BENCH_HOST_CLASS"
}

cmd_case() {
	[ "$#" -eq 1 ] || usage
	bench_case_load "$1"
	printf 'case=%s hash=%s\n' "$BENCH_CASE_NAME" "$BENCH_CASE_HASH"
	cat "$BENCH_CASE_FILE"
}

[ "$#" -ge 1 ] || usage
_cmd=$1
shift
case $_cmd in
lock)
	[ "$#" -ge 1 ] || usage
	_sub=$1
	shift
	case $_sub in
	run) bench_lock_run "$@" ;;
	status) [ "$#" -eq 0 ] || usage; bench_lock_status ;;
	*) usage ;;
	esac
	;;
host) cmd_host "$@" ;;
inputs) cmd_inputs "$@" ;;
case) cmd_case "$@" ;;
baseline)
	[ "$#" -ge 1 ] || usage
	_sub=$1
	shift
	case $_sub in
	get) cmd_baseline_get "$@" ;;
	put) cmd_baseline_put "$@" ;;
	*) usage ;;
	esac
	;;
drift) cmd_drift "$@" ;;
compare) cmd_compare "$@" ;;
stats) [ "$#" -eq 0 ] || usage; bench_stats ;;
-h | --help | help) usage ;;
*) usage ;;
esac
