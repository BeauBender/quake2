# scripts/bench/core/store.sh: the result store.
#
# The store is $BENCH_STORE, else $XDG_CACHE_HOME/<repo>-bench, else
# $HOME/.cache/<repo>-bench (<repo>: see bench_repo_name), outside any
# repository. The baseline directory of a case on a host for some inputs,
#   <store>/<host token>/<inputs>/<case>/<case hash>/
# holds numbered sets, set-1/, set-2/, ...: each the run files of one full
# measurement,
#   set-<n>/r-<UTC>-<pid>-<k>.run   (k: the run's index, from 1)
# one plain key=value file per run. The baseline is the set with the
# highest number; the lower ones are the baselines it superseded, kept as
# they were. A set is written whole into a staging directory, .tmp.<pid>/
# beside the sets, and made live by one rename to set-<n+1>, so a reader
# (baseline get and compare take no lock) sees all of a set or none of it,
# and a store that fails at any step leaves the old baseline live. The
# single runs of drift checks are kept in drift/, each written under a
# ".tmp." name in that directory and renamed into place. Readers take only
# set-<number> directories and r-*.run files.

BENCH_STORE_ROOT=
BENCH_STORE_SEQ=0
BENCH_STORE_SET=

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

# bench_store_sets <dir>: the numbers of the sets in the baseline directory
# <dir>, ascending.
bench_store_sets() {
	for _ss_d in "$1"/set-*; do
		_ss_n=${_ss_d##*/set-}
		case $_ss_n in
		'' | *[!0-9]*) continue ;;
		esac
		if [ -d "$_ss_d" ]; then printf '%s\n' "$_ss_n"; fi
	done | sort -n
}

# bench_store_live <dir>: the live set of the baseline directory <dir> (the
# highest-numbered), as a path; nothing when there is none.
bench_store_live() {
	_sl_n=$(bench_store_sets "$1" | tail -n 1)
	if [ -n "$_sl_n" ]; then printf '%s/set-%s\n' "$1" "$_sl_n"; fi
}

# bench_store_runs <set>: the run files of the set <set> (a path; empty:
# none), one path per line, oldest first: by the time in the name, then by
# the run index, as numbers.
bench_store_runs() {
	[ -n "$1" ] || return 0
	for _sr_f in "$1"/r-*.run; do
		if [ -f "$_sr_f" ]; then printf '%s\n' "$_sr_f"; fi
	done | awk -F/ '{ n = split($NF, p, "-"); print p[2], p[n] + 0, $0 }' | sort -k1,1 -k2,2n | sed 's/^[^ ]* [^ ]* //'
}

# bench_store_count <set>: how many runs the set <set> has (0: none).
bench_store_count() {
	bench_store_runs "$1" | awk 'END { print NR }'
}

# bench_store_write <dir> <file>: stores <file> as a new run file in <dir>,
# under a ".tmp." name first, then renamed into place.
bench_store_write() {
	mkdir -p "${1:?}" || bench_die 2 "cannot make a store directory"
	BENCH_STORE_SEQ=$((BENCH_STORE_SEQ + 1))
	_sw_n=r-$(date -u +%Y%m%dT%H%M%SZ)-$$-$BENCH_STORE_SEQ.run
	cp "$2" "${1:?}/.tmp.$_sw_n" || bench_die 2 "cannot write to the store"
	if ! mv -f "${1:?}/.tmp.$_sw_n" "${1:?}/$_sw_n"; then
		rm -f "${1:?}/.tmp.$_sw_n"
		bench_die 2 "cannot rename a run file into place in the store"
	fi
}

# bench_store_put <dir> <runs dir>: stores the runs <runs dir>/<k>.run (k
# from 1) as the new live set of the baseline directory <dir>: copies them
# into <dir>/.tmp.<pid>/, then renames that to set-<n+1>. Sets
# BENCH_STORE_SET to the new set's name. On a failure it removes the
# staging directory and exits 2; the old live set stays live.
bench_store_put() {
	mkdir -p "${1:?}" || bench_die 2 "cannot make a store directory"
	_sp_t=${1:?}/.tmp.$$
	rm -rf "${_sp_t:?}"
	mkdir "${_sp_t:?}" || bench_die 2 "cannot make a staging directory in the store"
	_sp_u=$(date -u +%Y%m%dT%H%M%SZ)
	_sp_k=1
	while [ -f "${2:?}/$_sp_k.run" ]; do
		if ! cp "${2:?}/$_sp_k.run" "${_sp_t:?}/r-$_sp_u-$$-$_sp_k.run"; then
			rm -rf "${_sp_t:?}"
			bench_die 2 "cannot write to the store; the old baseline stands"
		fi
		_sp_k=$((_sp_k + 1))
	done
	_sp_n=$(bench_store_sets "$1" | tail -n 1)
	BENCH_STORE_SET=set-$((${_sp_n:-0} + 1))
	if [ -e "${1:?}/$BENCH_STORE_SET" ] || ! mv "${_sp_t:?}" "${1:?}/$BENCH_STORE_SET"; then
		rm -rf "${_sp_t:?}"
		bench_die 2 "cannot make the new set live in the store; the old baseline stands"
	fi
}

# bench_store_values <metric> <set>: the metric's value in each run file of
# the set <set> that has it, one per line.
bench_store_values() {
	[ -n "$2" ] || return 0
	for _sv_f in "$2"/r-*.run; do
		if [ -f "$_sv_f" ]; then bench_file_kv "$_sv_f" "metric.$1"; fi
	done
}

# bench_store_metrics <set...>: the metric names the sets hold, sorted,
# once each.
bench_store_metrics() {
	for _sm_d in "$@"; do
		[ -n "$_sm_d" ] || continue
		for _sm_f in "$_sm_d"/r-*.run; do
			if [ -f "$_sm_f" ]; then sed -n 's/^metric\.\([A-Za-z0-9._-]*\)=.*/\1/p' "$_sm_f"; fi
		done
	done | sort -u
}
