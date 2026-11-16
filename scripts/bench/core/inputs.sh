# scripts/bench/core/inputs.sh: build-input identity.
#
# The inputs id of a commit is "i-" and 12 hex digits of the digest of:
#   inputs=v1
#   path=<path> id=<git rev-parse <commit>:<path>, or "missing">   (each path)
#   flags=<the case's build_flags, blanks squeezed; empty without a case>
# over a fixed list of paths: BENCH_INPUT_PATHS below, then every top-level
# file of the commit named <letters, digits, . _ ->.make, in the order git
# lists them. A path the commit does not have hashes as the marker
# "missing". So a commit that changes nothing under these paths (a
# documentation change, say) has the same inputs as its parent, and any
# change under them gives new ones.

BENCH_INPUT_PATHS='Makefile src third_party'

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

# bench_inputs_material <commit id> <flags>: the text the inputs id hashes.
bench_inputs_material() {
	printf 'inputs=v1\n'
	_im_extra=$(git ls-tree --name-only "$1" 2> /dev/null | grep -E '^[A-Za-z0-9._-]+\.make$' || true)
	for _im_p in $BENCH_INPUT_PATHS $_im_extra; do
		_im_id=$(git rev-parse --verify --quiet "$1:$_im_p" 2> /dev/null) || _im_id=
		printf 'path=%s id=%s\n' "$_im_p" "${_im_id:-missing}"
	done
	printf 'flags=%s\n' "$(printf '%s\n' "$2" | bench_oneline)"
}

# bench_inputs <commit id> <flags>: the inputs id.
bench_inputs() {
	bench_inputs_material "$1" "$2" | bench_id i-
}
