# scripts/bench/core/store.sh: the result store.
#
# The store is $BENCH_STORE, else $XDG_CACHE_HOME/<repo>-bench, else
# $HOME/.cache/<repo>-bench (<repo>: see bench_repo_name), outside any
# repository. A baseline is the run files of one directory
#   <store>/<host token>/<inputs>/<case>/<case hash>/r-<UTC>-<pid>-<n>.run
# one plain key=value file per run. A file is written under a temporary
# name starting with ".tmp." in the same directory and renamed into place,
# so a reader never sees half a file; readers take only r-*.run names, so
# they never see a temporary one. A replaced baseline moves to
# superseded/<UTC>-<pid>/ beside it; the single runs of drift checks are
# kept in drift/.

BENCH_STORE_ROOT=
BENCH_STORE_SEQ=0

# bench_store_init: resolves the store root (BENCH_STORE_ROOT).
bench_store_init() {
	if [ -n "${BENCH_STORE:-}" ]; then
		BENCH_STORE_ROOT=$BENCH_STORE
	else
		BENCH_STORE_ROOT=$(bench_cache_home) || bench_die 2 "set BENCH_STORE or HOME: no per-user directory for the store"
		BENCH_STORE_ROOT=$BENCH_STORE_ROOT/$(bench_repo_name)-bench
	fi
	bench_outside_repo "$BENCH_STORE_ROOT" "the store (BENCH_STORE)"
}

# bench_store_dir <inputs>: the baseline directory of the loaded case on
# this host for <inputs>.
bench_store_dir() {
	printf '%s/%s/%s/%s/%s\n' "$BENCH_STORE_ROOT" "$BENCH_HOST_TOKEN" "$1" "$BENCH_CASE_NAME" "$BENCH_CASE_HASH"
}

# bench_store_runs <dir>: the run files of the baseline in <dir>, one path
# per line, oldest first.
bench_store_runs() {
	for _sr_f in "$1"/r-*.run; do
		if [ -f "$_sr_f" ]; then printf '%s\n' "$_sr_f"; fi
	done
}

# bench_store_count <dir>: how many runs the baseline in <dir> has.
bench_store_count() {
	bench_store_runs "$1" | awk 'END { print NR }'
}

# bench_store_write <dir> <file>: stores <file> as a new run file in <dir>.
bench_store_write() {
	mkdir -p "$1" || bench_die 2 "cannot make a store directory"
	BENCH_STORE_SEQ=$((BENCH_STORE_SEQ + 1))
	_sw_n=r-$(date -u +%Y%m%dT%H%M%SZ)-$$-$BENCH_STORE_SEQ.run
	cp "$2" "$1/.tmp.$_sw_n" || bench_die 2 "cannot write to the store"
	if ! mv -f "$1/.tmp.$_sw_n" "$1/$_sw_n"; then
		bench_die 2 "cannot rename a run file into place in the store"
	fi
}

# bench_store_supersede <dir>: moves the baseline in <dir> to
# <dir>/superseded/<UTC>-<pid>/ and prints that name.
bench_store_supersede() {
	_ss_n=$(date -u +%Y%m%dT%H%M%SZ)-$$
	mkdir -p "$1/superseded/$_ss_n" || bench_die 2 "cannot make a superseded directory"
	bench_store_runs "$1" > "$BENCH_TMP/supersede"
	while IFS= read -r _ss_f; do
		mv -f "$_ss_f" "$1/superseded/$_ss_n/" || bench_die 2 "cannot move a run to superseded"
	done < "$BENCH_TMP/supersede"
	printf '%s\n' "$_ss_n"
}

# bench_store_values <metric> <dir>: the metric's value in each run file of
# the baseline in <dir> that has it, one per line.
bench_store_values() {
	for _sv_f in "$2"/r-*.run; do
		if [ -f "$_sv_f" ]; then bench_file_kv "$_sv_f" "metric.$1"; fi
	done
}

# bench_store_metrics <dir...>: the metric names the baselines in the
# directories hold, sorted, once each.
bench_store_metrics() {
	for _sm_d in "$@"; do
		for _sm_f in "$_sm_d"/r-*.run; do
			if [ -f "$_sm_f" ]; then sed -n 's/^metric\.\([A-Za-z0-9._-]*\)=.*/\1/p' "$_sm_f"; fi
		done
	done | sort -u
}
