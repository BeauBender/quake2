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
#   bench.sh lock run <label> -- <command> [args...]
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
# 75 the lock wait ran out; a command run under the lock exits with its own
# status.
#
# Environment: BENCH_STATE (lock state directory), BENCH_STORE (result
# store), BENCH_LOCK_WAIT (seconds, default 3600), BENCH_LOCK_POLL (seconds,
# default 2), BENCH_LOCK_GRACE (seconds, default 10), BENCH_LOCK_TOKEN (set
# by the lock for its command), BENCH_HASH (shasum or cksum).
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
. "$BENCH_ROOT/core/measure.sh"

bench_on_exit() {
	_oe_st=$?
	bench_lock_release
	if [ -n "${BENCH_LOCK_GUARD:-}" ]; then rmdir "${BENCH_LOCK_GUARD:?}" 2> /dev/null || true; fi
	if [ -n "${BENCH_TMP:-}" ]; then rm -rf "${BENCH_TMP:?}"; fi
	exit "$_oe_st"
}

bench_on_signal() {
	trap '' INT TERM HUP
	bench_lock_stop_child
	if [ -n "${BENCH_BOUNDED_PID:-}" ]; then kill -TERM "$BENCH_BOUNDED_PID" 2> /dev/null || true; fi
	exit "$1"
}

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

# need_lock <label> <args...>: re-runs this script with <args> under the
# lock, unless this process holds it already, and exits with its status.
need_lock() {
	_nl_label=$1
	shift
	bench_lock_init
	bench_lock_mine && return 0
	_nl_st=0
	# In the background, so a signal here stops it (bench_on_signal).
	sh "$BENCH_SELF" lock run "$_nl_label" -- sh "$BENCH_SELF" "$@" < /dev/null &
	BENCH_LOCK_CHILD=$!
	wait "$BENCH_LOCK_CHILD" || _nl_st=$?
	BENCH_LOCK_CHILD=
	exit "$_nl_st"
}

# setup <case> <commit>: loads the case, the host, the commit, its inputs
# and the store; sets DIR, the baseline directory.
setup() {
	bench_case_load "$1"
	bench_commit "$2"
	bench_host_init
	bench_store_init
	BENCH_INPUTS=$(bench_inputs "$BENCH_COMMIT" "$(bench_case_get build_flags)")
	DIR=$(bench_store_dir "$BENCH_INPUTS")
}

# summary <dir>: one stats line per metric of the baseline in <dir>.
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

# store_new: supersedes the baseline in DIR (if any) and stores the runs
# measured into ${BENCH_TMP:?}/new.
store_new() {
	if [ "$(bench_store_count "$DIR")" -gt 0 ]; then
		_sn_old=$(bench_store_supersede "$DIR") || bench_die 2 "cannot supersede the old baseline"
		printf 'superseded=%s\n' "$_sn_old"
	fi
	_sn_k=1
	while [ -f "${BENCH_TMP:?}/new/$_sn_k.run" ]; do
		bench_store_write "$DIR" "${BENCH_TMP:?}/new/$_sn_k.run"
		_sn_k=$((_sn_k + 1))
	done
}

cmd_baseline_get() {
	[ "$#" -eq 2 ] || usage
	setup "$1" "$2"
	_bg_n=$(bench_store_count "$DIR")
	if [ "$_bg_n" -eq 0 ]; then
		printf 'baseline=none case=%s inputs=%s host=%s\n' "$BENCH_CASE_NAME" "$BENCH_INPUTS" "$BENCH_HOST_TOKEN"
		exit 1
	fi
	printf 'baseline=present case=%s inputs=%s host=%s runs=%d\n' "$BENCH_CASE_NAME" "$BENCH_INPUTS" "$BENCH_HOST_TOKEN" "$_bg_n"
	summary "$DIR"
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
	if [ "$(bench_store_count "$DIR")" -gt 0 ] && [ -z "$_bp_replace" ]; then
		bench_note "a baseline exists for the case $BENCH_CASE_NAME at $BENCH_INPUTS ($(bench_store_count "$DIR") runs); check it with drift, or replace it with baseline put --replace"
		exit 1
	fi
	bench_adapter_load
	bench_prepare
	measure_baseline
	store_new
	printf 'baseline=stored case=%s inputs=%s host=%s runs=%d\n' "$BENCH_CASE_NAME" "$BENCH_INPUTS" "$BENCH_HOST_TOKEN" "$(bench_store_count "$DIR")"
	summary "$DIR"
}

cmd_drift() {
	[ "$#" -eq 2 ] || usage
	bench_case_load "$1"
	bench_commit "$2"
	need_lock "bench.$BENCH_CASE_NAME" drift "$1" "$BENCH_COMMIT"
	setup "$1" "$2"
	if [ "$(bench_store_count "$DIR")" -eq 0 ]; then
		bench_note "no baseline for the case $BENCH_CASE_NAME at $BENCH_INPUTS; measure one with baseline put"
		exit 1
	fi
	bench_adapter_load
	bench_prepare
	_dr_w=$(bench_case_get warmup)
	_dr_k=1
	while [ "$_dr_k" -le "${_dr_w:-0}" ]; do
		bench_run_one warmup "$_dr_k" /dev/null
		_dr_k=$((_dr_k + 1))
	done
	bench_run_one run 1 "${BENCH_TMP:?}/drift.run"
	_dr_m=$(bench_case_get metric)
	_dr_x=$(bench_file_kv "${BENCH_TMP:?}/drift.run" "metric.$_dr_m")
	_dr_line=$(bench_store_values "$_dr_m" "$DIR" |
		bench_drift_rule "$_dr_x" "$(bench_case_get abs_tol)" "$(bench_case_get rel_tol)") ||
		bench_die 2 "the stored runs of $BENCH_CASE_NAME have no usable $_dr_m values"
	bench_store_write "$DIR/drift" "${BENCH_TMP:?}/drift.run"
	printf '%s metric=%s\n' "$_dr_line" "$_dr_m"
	case $_dr_line in
	drift=inside*) return 0 ;;
	esac
	measure_baseline
	store_new
	printf 'baseline=replaced case=%s inputs=%s runs=%d\n' "$BENCH_CASE_NAME" "$BENCH_INPUTS" "$(bench_store_count "$DIR")"
	summary "$DIR"
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
	_cp_bd=$DIR
	bench_commit "$_cp_after"
	_cp_ac=$BENCH_COMMIT
	_cp_ai=$(bench_inputs "$BENCH_COMMIT" "$(bench_case_get build_flags)")
	_cp_ad=$(bench_store_dir "$_cp_ai")
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
		_in_flags=$(bench_case_get build_flags)
	fi
	bench_commit "$1"
	if [ -n "$_in_list" ]; then
		bench_inputs_material "$BENCH_COMMIT" "$_in_flags"
		printf 'hash=%s\n' "$(bench_hash_tool)"
	fi
	bench_inputs "$BENCH_COMMIT" "$_in_flags"
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
