# scripts/bench/core/common.sh: helpers shared by the bench core.
#
# Sourced by scripts/bench/bench.sh. POSIX sh plus awk, safe on bash 3.2.
# The core names no game and no repository layout beyond what a caller
# passes in; see docs/plans/bench-harness.md.

BENCH_TMP=

# bench_die <status> <message...>: says why on standard error and exits.
bench_die() {
	_bd_st=$1
	shift
	printf 'bench: %s\n' "$*" >&2
	exit "$_bd_st"
}

# bench_note <message...>: one line on standard error.
bench_note() {
	printf 'bench: %s\n' "$*" >&2
}

# bench_is_name <text>: a case, adapter or repository name: 1 to 64 of
# A-Z a-z 0-9 . _ -, not starting with a dot.
bench_is_name() {
	case $1 in
	'' | .* | *[!A-Za-z0-9._-]*) return 1 ;;
	esac
	[ "${#1}" -le 64 ]
}

# bench_is_label <text>: a lock label: 1 to 64 of A-Z a-z 0-9 . _ : @ + -.
# No space and no newline, so a label cannot add a line to the holder file.
bench_is_label() {
	case $1 in
	'' | *[!A-Za-z0-9._:@+-]*) return 1 ;;
	esac
	[ "${#1}" -le 64 ]
}

# bench_is_uint <text>: digits only.
bench_is_uint() {
	case $1 in
	'' | *[!0-9]*) return 1 ;;
	esac
	return 0
}

# bench_is_num <text>: a decimal number: an optional minus, digits, an
# optional fraction. No exponent, no sign other than a leading minus.
bench_is_num() {
	printf '%s\n' "$1" | awk '{ ok = ($0 ~ /^-?[0-9]+(\.[0-9]+)?$/) } END { exit !(NR == 1 && ok) }'
}

# bench_tmp: makes this process's temporary directory once (BENCH_TMP); the
# EXIT trap in bench.sh removes it. Call it in the main shell, not in $(...).
bench_tmp() {
	if [ -z "$BENCH_TMP" ]; then
		BENCH_TMP=$(mktemp -d "${TMPDIR:-/tmp}/bench.XXXXXX") || bench_die 2 "cannot make a temporary directory"
	fi
}

# bench_kv <text> <key>: the value of the first "<key>=" line of <text>.
bench_kv() {
	printf '%s\n' "$1" | awk -v k="$2=" 'index($0, k) == 1 { print substr($0, length(k) + 1); exit }'
}

# bench_file_kv <file> <key>: the value of the first "<key>=" line of <file>.
bench_file_kv() {
	awk -v k="$2=" 'index($0, k) == 1 { print substr($0, length(k) + 1); exit }' "$1"
}

# bench_hash_tool: the digest tool, said in every listing that hashes:
# "shasum" (shasum -a 256) when it is present, else "cksum" (POSIX CRC-32,
# taken twice over the text with two different prefixes to make 16 hex
# digits). BENCH_HASH=shasum or BENCH_HASH=cksum forces one; bench.sh checks
# the value before any command runs.
bench_hash_tool() {
	if [ -n "${BENCH_HASH:-}" ]; then
		printf '%s\n' "$BENCH_HASH"
	elif command -v shasum > /dev/null 2>&1; then
		echo shasum
	else
		echo cksum
	fi
}

# bench_hash: the hex digest of standard input (trailing newlines dropped,
# one added back, so the same text always gives the same digest).
bench_hash() {
	_bh_d=$(cat)
	case $(bench_hash_tool) in
	shasum)
		printf '%s\n' "$_bh_d" | shasum -a 256 | awk '{ print $1 }'
		;;
	*)
		_bh_a=$(printf '%s\n' "$_bh_d" | cksum | awk '{ print $1 }')
		_bh_b=$(printf 'bench\n%s\n' "$_bh_d" | cksum | awk '{ print $1 }')
		printf '%08x%08x\n' "$_bh_a" "$_bh_b"
		;;
	esac
}

# bench_id <prefix>: "<prefix>" and the first 12 hex digits of the digest of
# standard input.
bench_id() {
	_bi_h=$(bench_hash)
	printf '%s%s\n' "$1" "$(printf '%s\n' "$_bi_h" | cut -c1-12)"
}

# bench_cache_home: the per-user cache directory, outside any repository:
# $XDG_CACHE_HOME, else $HOME/.cache.
bench_cache_home() {
	if [ -n "${XDG_CACHE_HOME:-}" ]; then
		printf '%s\n' "$XDG_CACHE_HOME"
	elif [ -n "${HOME:-}" ]; then
		printf '%s/.cache\n' "$HOME"
	else
		return 1
	fi
}

# bench_repo_name: the name of the repository holding the current directory,
# the same in every worktree of it (the directory that holds the common git
# directory); "repo" when there is none or the name is unusual.
bench_repo_name() {
	_rn_c=$(git rev-parse --git-common-dir 2> /dev/null) || _rn_c=
	if [ -n "$_rn_c" ]; then
		_rn_c=$(cd "$_rn_c" 2> /dev/null && pwd -P) || _rn_c=
	fi
	_rn_b=${_rn_c##*/}
	if [ "$_rn_b" = .git ]; then
		_rn_c=${_rn_c%/*}
		_rn_b=${_rn_c##*/}
	else
		_rn_b=${_rn_b%.git}
	fi
	if bench_is_name "$_rn_b"; then
		printf '%s\n' "$_rn_b"
	else
		echo repo
	fi
}

# bench_outside_repo <dir> <what>: refuses a relative path, and a directory
# that lies inside a git work tree (tested on its nearest existing
# ancestor, so nothing is made first). The message names <what>, never the
# path, since the path may hold a home directory.
bench_outside_repo() {
	case $1 in
	/*) ;;
	*) bench_die 2 "$2 must be an absolute path" ;;
	esac
	_or_d=$1
	while [ ! -d "$_or_d" ]; do
		_or_d=$(dirname "$_or_d")
	done
	_or_in=$(
		unset GIT_DIR GIT_WORK_TREE
		git -C "$_or_d" rev-parse --is-inside-work-tree 2> /dev/null
	) || _or_in=false
	if [ "$_or_in" = true ]; then
		bench_die 2 "$2 lies inside a git work tree; point it at a directory outside any repository"
	fi
}

# bench_pid_alive <pid>: kill -0 succeeds. An empty, zero or non-numeric
# pid is not alive.
bench_pid_alive() {
	case $1 in
	'' | 0 | *[!0-9]*) return 1 ;;
	esac
	kill -0 "$1" 2> /dev/null
}

# bench_pid_running <pid>: alive and not a zombie (a child that exited and
# is not yet reaped still answers kill -0).
bench_pid_running() {
	bench_pid_alive "$1" || return 1
	_pr_s=$(ps -o stat= -p "$1" 2> /dev/null) || return 1
	case $_pr_s in
	'' | Z*) return 1 ;;
	esac
	return 0
}

# bench_nap: one poll interval, BENCH_LOCK_POLL seconds (a decimal is
# allowed where sleep takes one; 1 second where it does not).
bench_nap() {
	sleep "${1:-1}" 2> /dev/null || sleep 1
}

BENCH_BOUNDED_PID=

# bench_bounded <seconds> <command...>: runs the command, standard input
# from /dev/null, and stops it with TERM once it has run more than
# <seconds> (whole seconds by the clock, so between <seconds> and one more).
# Returns its status, or 124 when it was stopped. For adapters. The poll
# backs off from 0.02 s to 1 s, so a short command costs little.
bench_bounded() {
	_bb_s=$1
	shift
	bench_is_uint "$_bb_s" || bench_die 2 "a wall time is whole seconds"
	_bb_end=$(($(date +%s) + _bb_s))
	"$@" < /dev/null &
	BENCH_BOUNDED_PID=$!
	_bb_nap=0.02
	while bench_pid_running "$BENCH_BOUNDED_PID"; do
		if [ "$(date +%s)" -gt "$_bb_end" ]; then
			kill -TERM "$BENCH_BOUNDED_PID" 2> /dev/null || true
			wait "$BENCH_BOUNDED_PID" 2> /dev/null || true
			BENCH_BOUNDED_PID=
			bench_note "stopped a command that outlived its wall time of $_bb_s s"
			return 124
		fi
		bench_nap "$_bb_nap"
		case $_bb_nap in
		0.02) _bb_nap=0.05 ;;
		0.05) _bb_nap=0.1 ;;
		0.1) _bb_nap=0.2 ;;
		0.2) _bb_nap=0.5 ;;
		*) _bb_nap=1 ;;
		esac
	done
	_bb_st=0
	wait "$BENCH_BOUNDED_PID" || _bb_st=$?
	BENCH_BOUNDED_PID=
	return "$_bb_st"
}

# bench_load: the one-minute load average, or "unknown".
bench_load() {
	_ld=$(sysctl -n vm.loadavg 2> /dev/null | awk '{ print $2 }') || _ld=
	if [ -z "$_ld" ] && [ -r /proc/loadavg ]; then
		_ld=$(awk '{ print $1 }' /proc/loadavg)
	fi
	if bench_is_num "${_ld:-x}"; then
		printf '%s\n' "$_ld"
	else
		echo unknown
	fi
}

# bench_utc: the time now, UTC, ISO 8601 to the second.
bench_utc() {
	date -u +%Y-%m-%dT%H:%M:%SZ
}
