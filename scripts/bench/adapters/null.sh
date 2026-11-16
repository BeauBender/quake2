# scripts/bench/adapters/null.sh: the null adapter.
#
# It builds nothing and launches nothing. A run runs a shell command that
# sleeps and prints three known samples, so every path of the core (the
# lock, the store, the statistics, drift, compare, a preflight refusal and
# the wall-time bound) can be tested without an engine or game data;
# tests/check-bench.sh drives it with scripts/bench/cases/null-sleep.case.
#
# The case's settings line holds words of name=value:
#   sleep=<seconds>       how long a run sleeps (0)
#   values=<v1,v2,...>    run k reports the k-th value, cycling
# Run k prints the samples v-1, v and v+1 ("sample ms=<value>" lines), so
# its metric.ms (the median of its samples) is v and metric.frames is 3. A
# warm-up run reports 0. For the self-test, BENCH_NULL_VALUES replaces
# values=, and BENCH_NULL_REFUSE=1 makes the preflight refuse.

# null_setting <name>: the value of <name>= in the case's settings.
null_setting() {
	bench_case_get settings | tr ' ' '\n' | awk -v k="$1=" 'index($0, k) == 1 { print substr($0, length(k) + 1); exit }'
}

adapter_preflight() {
	if [ -n "${BENCH_NULL_REFUSE:-}" ]; then
		echo "null: the preflight refuses (BENCH_NULL_REFUSE is set)" >&2
		return 1
	fi
	echo pre.null=ok
}

adapter_build() {
	printf 'commit=%s\n' "$1" > "${2:?}/built"
}

adapter_run() {
	_nr_log=$3
	_nr_vals=${BENCH_NULL_VALUES:-$(null_setting values)}
	_nr_sleep=$(null_setting sleep)
	if [ "$BENCH_RUN_KIND" = warmup ]; then
		_nr_v=0
	else
		_nr_v=$(printf '%s\n' "$_nr_vals" | awk -F, -v k="$BENCH_RUN_INDEX" '{ print $(((k - 1) % NF) + 1) }')
	fi
	bench_is_num "$_nr_v" || { echo "null: not a number: a value of values=" >&2; return 1; }
	set -- $(awk -v v="$_nr_v" 'BEGIN { print v - 1, v + 0, v + 1 }')
	bench_bounded "$(bench_case_get wall)" sh -c 'sleep "$1"; shift; for s; do printf "sample ms=%s\n" "$s"; done' \
		null "${_nr_sleep:-0}" "$@" > "${_nr_log:?}"
}

adapter_metrics() {
	sed -n 's/^sample ms=//p' "${2:?}" > "${2:?}.ms"
	_nm_s=$(bench_stats < "${2:?}.ms") || return 1
	printf 'metric.ms=%s\n' "$(printf '%s\n' "$_nm_s" | sed 's/.* median=\([^ ]*\) .*/\1/')"
	printf 'metric.frames=%s\n' "$(awk 'END { print NR }' "${2:?}.ms")"
}
