# scripts/bench/core/inputs.sh: build-input identity.
#
# The inputs id of a commit is "i-" and 12 hex digits of the digest of:
#   inputs=v1
#   path=<path> id=<git rev-parse <commit>:<path>, or "missing">   (each path)
#   flags=<the case's build_flags, blanks squeezed; empty without a case>
# over the paths the case's adapter names: adapter_inputs_paths <commit>
# prints them, one per line, in the order they are hashed (see measure.sh).
# The core names no project layout. Without a case, or with an adapter that
# does not define the hook or names no path, the default is the whole tree:
# one line tree=<the commit's root tree id> in place of the path lines, so
# any change at all gives new inputs. A path the commit does not have
# hashes as the marker "missing". So, with a list, a commit that changes
# nothing under its paths (a documentation change, say) has the same inputs
# as its parent, and any change under them gives new ones.

BENCH_COMMIT=

# bench_commit <rev>: resolves <rev> in the repository of the current
# directory to a full commit id (BENCH_COMMIT). Call it in the main shell.
bench_commit() {
	case $1 in
	'' | -*) bench_die 2 "not a commit: give a commit, branch or tag" ;;
	esac
	BENCH_COMMIT=$(git rev-parse --verify --quiet "$1^{commit}" 2> /dev/null) || BENCH_COMMIT=
	[ -n "$BENCH_COMMIT" ] || bench_die 2 "not a commit in this repository: $1"
}

# bench_inputs_paths <commit id>: the adapter's input paths, checked: each a
# relative path of A-Z a-z 0-9 . _ / + -, not starting with "/" or "-";
# empty when there is no hook.
bench_inputs_paths() {
	type adapter_inputs_paths > /dev/null 2>&1 || return 0
	_ip_st=0
	_ip_l=$(adapter_inputs_paths "$1") || _ip_st=$?
	[ "$_ip_st" -eq 0 ] || bench_die 3 "the adapter could not name its input paths (status $_ip_st)"
	printf '%s\n' "$_ip_l" | awk '
		$0 == "" { next }
		/^[\/-]/ || /[^A-Za-z0-9._\/+-]/ { bad = 1; next }
		{ print }
		END { exit bad }' || bench_die 3 "the adapter named an input path that is not a relative path of A-Z a-z 0-9 . _ / + -"
}

# bench_inputs_material <commit id> <flags>: the text the inputs id hashes.
bench_inputs_material() {
	_im_paths=$(bench_inputs_paths "$1") || exit "$?"
	printf 'inputs=v1\n'
	if [ -z "$_im_paths" ]; then
		_im_id=$(git rev-parse --verify --quiet "$1^{tree}" 2> /dev/null) || _im_id=
		[ -n "$_im_id" ] || bench_die 2 "cannot read the tree of the commit $1"
		printf 'tree=%s\n' "$_im_id"
	else
		set -f
		_im_ifs=$IFS
		IFS='
'
		for _im_p in $_im_paths; do
			_im_id=$(git rev-parse --verify --quiet "$1:$_im_p" 2> /dev/null) || _im_id=
			printf 'path=%s id=%s\n' "$_im_p" "${_im_id:-missing}"
		done
		IFS=$_im_ifs
		set +f
	fi
	printf 'flags=%s\n' "$(printf '%s\n' "$2" | bench_oneline)"
}

# bench_inputs <commit id> <flags>: the inputs id. Exits (the shell it runs
# in) non-zero when the material or the digest cannot be made, so no id is
# made of half the material.
bench_inputs() {
	_bi_m=$(bench_inputs_material "$1" "$2") || exit "$?"
	printf '%s\n' "$_bi_m" | bench_id i-
}
