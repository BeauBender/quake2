# scripts/bench/core/case.sh: case files.
#
# A case file is lines of key=value; blank lines and lines starting with #
# are ignored, blanks around the key and the value are dropped and runs of
# blanks inside a value squeezed to one. Only the keys below are allowed,
# each at most once; anything else refuses the case. Values are data: they
# are never evaluated by a shell.
#
#   adapter      required; the adapter, scripts/bench/adapters/<adapter>.sh
#   runs         required; runs in a baseline, 1 to 1000
#   metric       required; the metric the drift rule and the verdict use
#   abs_tol      required; the drift rule's absolute tolerance, >= 0
#   rel_tol      required; the drift rule's relative tolerance, >= 0
#   warmup       runs made and thrown away before the counted ones (0)
#   wall         wall time of one run in seconds, for the adapter (600;
#                bench_case_wall gives it with the default)
#   better       lower or higher: which way the metric improves
#   accept       the improvement of the metric's median, in percent of the
#                before median, that compare requires (needs better; 0
#                means "not worse", a negative value a regression budget)
#   targets      build targets, for the adapter
#   build_flags  build flags, for the adapter; part of the inputs id
#   settings     every run-time setting, for the adapter
#   requires     capabilities the adapter checks in its preflight
#   description  free text
#
# The normalised form is the kept lines, sorted; the case hash is "c-" and
# 12 hex digits of its digest, so comments, blank lines, key order and
# spacing do not change it, and any value does.

BENCH_CASE_KEYS='adapter runs metric abs_tol rel_tol warmup wall better accept targets build_flags settings requires description'

BENCH_CASE_NAME=
BENCH_CASE_FILE=
BENCH_CASE_HASH=

# bench_case_get <key>: the value of <key> in the loaded case, empty if unset.
bench_case_get() {
	bench_file_kv "$BENCH_CASE_FILE" "$1"
}

# bench_case_wall: the wall time of one run, in seconds: the case's wall,
# else 600.
bench_case_wall() {
	_cw=$(bench_case_get wall)
	printf '%s\n' "${_cw:-600}"
}

# bench_case_load <case>: a case name (scripts/bench/cases/<name>.case) or
# a path holding a "/". Validates it and sets BENCH_CASE_NAME,
# BENCH_CASE_FILE (the normalised form) and BENCH_CASE_HASH.
bench_case_load() {
	case $1 in
	*/*) _cl_f=$1 ;;
	*) _cl_f=$BENCH_ROOT/cases/$1.case ;;
	esac
	_cl_n=${_cl_f##*/}
	case $_cl_n in
	*.case) _cl_n=${_cl_n%.case} ;;
	*) bench_die 2 "a case file is named <name>.case" ;;
	esac
	if ! bench_is_name "$_cl_n" || [ "${#_cl_n}" -gt 58 ]; then
		bench_die 2 "a case name is 1 to 58 of A-Z a-z 0-9 . _ - (bench.<case> is its lock label)"
	fi
	[ -f "$_cl_f" ] || bench_die 2 "no case file for the case $_cl_n"
	bench_tmp
	BENCH_CASE_NAME=$_cl_n
	BENCH_CASE_FILE=${BENCH_TMP:?}/case
	_cl_st=0
	awk -v keys="$BENCH_CASE_KEYS" '
		BEGIN { n = split(keys, k, " "); for (i = 1; i <= n; i++) ok[k[i]] = 1 }
		{ sub(/\r$/, ""); sub(/^[ \t]+/, ""); sub(/[ \t]+$/, "") }
		$0 == "" || substr($0, 1, 1) == "#" { next }
		{
			e = index($0, "=")
			key = substr($0, 1, e - 1)
			sub(/[ \t]+$/, "", key)
			if (e < 2 || key !~ /^[a-z_]+$/) {
				printf "bench: case line %d is not key=value with a lower-case key\n", NR > "/dev/stderr"
				bad = 1
				next
			}
			if (!(key in ok)) {
				printf "bench: case line %d: unknown key %s\n", NR, key > "/dev/stderr"
				bad = 1
				next
			}
			if (key in seen) {
				printf "bench: case line %d: the key %s is given twice\n", NR, key > "/dev/stderr"
				bad = 1
				next
			}
			seen[key] = 1
			val = substr($0, e + 1)
			sub(/^[ \t]+/, "", val)
			gsub(/[ \t]+/, " ", val)
			print key "=" val
		}
		END { exit bad }' "$_cl_f" > "${BENCH_TMP:?}/case.raw" || _cl_st=$?
	[ "$_cl_st" -eq 0 ] || bench_die 2 "the case $_cl_n is refused"
	sort "${BENCH_TMP:?}/case.raw" > "$BENCH_CASE_FILE"
	for _cl_k in adapter runs metric abs_tol rel_tol; do
		[ -n "$(bench_case_get "$_cl_k")" ] || bench_die 2 "the case $_cl_n has no $_cl_k"
	done
	bench_is_name "$(bench_case_get adapter)" || bench_die 2 "the case $_cl_n: adapter is a name"
	bench_is_name "$(bench_case_get metric)" || bench_die 2 "the case $_cl_n: metric is a name"
	_cl_v=$(bench_case_get runs)
	if ! bench_is_uint "$_cl_v" || [ "$_cl_v" -lt 1 ] || [ "$_cl_v" -gt 1000 ]; then
		bench_die 2 "the case $_cl_n: runs is 1 to 1000"
	fi
	for _cl_k in warmup wall; do
		_cl_v=$(bench_case_get "$_cl_k")
		[ -z "$_cl_v" ] || bench_is_uint "$_cl_v" || bench_die 2 "the case $_cl_n: $_cl_k is whole"
	done
	for _cl_k in abs_tol rel_tol; do
		_cl_v=$(bench_case_get "$_cl_k")
		case $_cl_v in -*) _cl_v=x ;; esac
		bench_is_num "$_cl_v" || bench_die 2 "the case $_cl_n: $_cl_k is a number >= 0"
	done
	_cl_v=$(bench_case_get better)
	case $_cl_v in
	'' | lower | higher) ;;
	*) bench_die 2 "the case $_cl_n: better is lower or higher" ;;
	esac
	_cl_a=$(bench_case_get accept)
	if [ -n "$_cl_a" ]; then
		bench_is_num "$_cl_a" || bench_die 2 "the case $_cl_n: accept is a number (percent)"
		[ -n "$_cl_v" ] || bench_die 2 "the case $_cl_n: accept needs better"
	fi
	BENCH_CASE_HASH=$(bench_id c- < "$BENCH_CASE_FILE") || exit 2
}
