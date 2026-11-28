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

# bench_is_uint <text>: decimal digits only, no leading zero but for 0
# itself, and at most 9 digits. Shell arithmetic reads 08 as a bad octal number
# and wraps a 20-digit one, so every numeric setting goes through this before
# any arithmetic, and 08, 09, 010 and 10 digits or more are refused.
bench_is_uint() {
	bench_is_uint_max "$1" 9
}

# bench_is_uint_max <text> <digits>: bench_is_uint with its own length cap
# (the cksum fallback of bench_hash reads CRCs of up to 10 digits).
bench_is_uint_max() {
	case $1 in
	'' | *[!0-9]* | 0?*) return 1 ;;
	esac
	[ "${#1}" -le "$2" ]
}

# bench_is_num <text>: a decimal number: an optional minus, digits, an
# optional fraction. No exponent, no sign other than a leading minus.
bench_is_num() {
	printf '%s\n' "$1" | awk '{ ok = ($0 ~ /^-?[0-9]+(\.[0-9]+)?$/) } END { exit !(NR == 1 && ok) }'
}

# bench_is_dec <text>: bench_is_num of at most 12 characters, for a reading or
# a poll interval: a longer figure (a 400-digit "utilization") is not a
# measurement, and awk would turn it into inf or a wrapped number.
bench_is_dec() {
	bench_is_num "$1" && [ "${#1}" -le 12 ]
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
# one added back, so the same text always gives the same digest). The tool
# must answer with a well-formed digest (64 hex digits from shasum, two
# CRCs of at most 10 digits from cksum); a tool that fails, is missing or
# answers anything else exits 2, so no id is ever made of nothing.
bench_hash() {
	_bh_d=$(cat)
	case $(bench_hash_tool) in
	shasum)
		_bh_x=$(printf '%s\n' "$_bh_d" | shasum -a 256 2> /dev/null) || _bh_x=
		_bh_x=${_bh_x%% *}
		case $_bh_x in
		*[!0-9a-f]*) _bh_x= ;;
		esac
		[ "${#_bh_x}" -eq 64 ] || bench_die 2 "shasum -a 256 gave no digest (failed, missing or malformed)"
		printf '%s\n' "$_bh_x"
		;;
	*)
		_bh_a=$(printf '%s\n' "$_bh_d" | cksum 2> /dev/null) || _bh_a=
		_bh_b=$(printf 'bench\n%s\n' "$_bh_d" | cksum 2> /dev/null) || _bh_b=
		_bh_a=${_bh_a%% *}
		_bh_b=${_bh_b%% *}
		if ! bench_is_uint_max "$_bh_a" 10 || ! bench_is_uint_max "$_bh_b" 10; then
			bench_die 2 "cksum gave no CRC (failed, missing or malformed)"
		fi
		printf '%08x%08x\n' "$_bh_a" "$_bh_b"
		;;
	esac
}

# bench_id <prefix>: "<prefix>" and the first 12 hex digits of the digest of
# standard input; exits 2 (in the shell it runs in) when there is none.
bench_id() {
	_bi_h=$(bench_hash) || exit 2
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

# bench_outside_repo <dir> <what>: refuses a relative path, a path with a
# "." or ".." segment (past a missing directory the walk below could not
# tell where it ends), and a directory that lies inside a git repository,
# tested on its nearest existing ancestor (so nothing is made first), with
# symbolic links resolved. It fails closed: refused when that directory or
# any ancestor of it holds a .git entry, whatever git says; when git, run
# with no GIT_DIR, GIT_WORK_TREE, GIT_CEILING_DIRECTORIES or
# GIT_DISCOVERY_ACROSS_FILESYSTEM from the caller, finds a repository there
# (a work tree or a git directory); and when git fails for any other reason
# than "not a git repository". A host with no git at all is judged by the
# .git walk alone. The message names <what>, never the path, since the
# path may hold a home directory.
bench_outside_repo() {
	case $1 in
	/*) ;;
	*) bench_die 2 "$2 must be an absolute path" ;;
	esac
	case /$1/ in
	*/./* | */../*) bench_die 2 "$2 must not have a . or .. segment" ;;
	esac
	_or_d=$1
	while [ ! -d "$_or_d" ]; do
		_or_d=$(dirname "$_or_d")
	done
	_or_d=$(cd "$_or_d" 2> /dev/null && pwd -P) || bench_die 2 "cannot read the directory that holds $2"
	_or_a=$_or_d
	while :; do
		if [ -e "$_or_a/.git" ]; then
			bench_die 2 "$2 lies inside a git work tree; point it at a directory outside any repository"
		fi
		[ -n "$_or_a" ] && [ "$_or_a" != / ] || break
		_or_a=${_or_a%/*}
	done
	command -v git > /dev/null 2>&1 || return 0
	_or_st=0
	_or_out=$(
		unset GIT_DIR GIT_WORK_TREE GIT_CEILING_DIRECTORIES GIT_DISCOVERY_ACROSS_FILESYSTEM
		git -C "$_or_d" rev-parse --is-inside-work-tree 2>&1
	) || _or_st=$?
	if [ "$_or_st" -eq 0 ]; then
		bench_die 2 "$2 lies inside a git repository; point it at a directory outside any repository"
	fi
	case $_or_out in
	*'not a git repository'*) ;;
	*) bench_die 2 "cannot tell whether $2 lies inside a git work tree: git failed (status $_or_st)" ;;
	esac
}

# bench_pid_alive <pid>: kill -0 succeeds. An empty, zero or non-numeric
# pid is not alive.
bench_pid_alive() {
	case $1 in
	'' | 0 | *[!0-9]*) return 1 ;;
	esac
	kill -0 "$1" 2> /dev/null # bounded-run: allowed: the primitive: bench_pid_alive
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

# bench_nap [<seconds>]: one poll interval, 1 second by default (a decimal
# is allowed where sleep takes one; 1 second where it does not). The sleep
# runs in the background and is waited for with the wait builtin, never as
# a foreground command (FIX192), so a signal runs the trap at once; a sleep
# ended by a signal is not slept again. A trap that exits leaves that sleep
# to end by itself, within the interval. snap.sh's gate and settle polls,
# the bounded run's 1 s poll and the lock's release wait nap here.
bench_nap() {
	_bn_st=0
	sleep "${1:-1}" 2> /dev/null &
	wait "$!" || _bn_st=$?
	if [ "$_bn_st" -ne 0 ] && [ "$_bn_st" -le 128 ]; then
		sleep 1 &
		wait "$!" || :
	fi
}

BENCH_NOW=

# bench_now: the clock (date +%s) in BENCH_NOW
bench_now() {
	BENCH_NOW=$(date +%s)
}
# bench_stop_group <pid>: a caller's trap stops the bounded command
# (bench_bounded, or the command under the lock): <pid> is
# BENCH_BOUNDED_PID, and the supervisor gets TERM and is waited for
# (bench_bounded_stop). Any other pid gets nothing: since TASK221 the
# supervisor is the only stop. Returns 0.
bench_stop_group() {
	[ -n "${BENCH_BOUNDED_PID:-}" ] && [ "$1" = "$BENCH_BOUNDED_PID" ] || return 0
	bench_bounded_stop
}

BENCH_BOUNDED_PID=
BENCH_BOUNDED_REAPED=
BENCH_BOUNDED_HOW=
BENCH_BOUNDED_QUIT=
BENCH_BOUNDED_TAG=
BENCH_BOUNDED_UNTIL=
BENCH_BOUNDED_EV=
BENCH_BOUNDED_TEE=
BENCH_BOUNDED_STARTED=
BENCH_BOUNDED_EVDIR=
BENCH_SV=

# The process supervisor's library (scripts/lib, TASK221): beside this
# file's directory (bash names a sourced file), else beside BENCH_ROOT,
# else BENCH_SV_LIB, for a test that runs a copy of scripts/bench alone.
BENCH_SV_DIR=
if [ -n "${BASH_SOURCE:-}" ]; then BENCH_SV_DIR=$(cd "$(dirname "$BASH_SOURCE")/../../lib" 2> /dev/null && pwd -P) || BENCH_SV_DIR=; fi
if [ ! -r "$BENCH_SV_DIR/supervise.sh" ] && [ -n "${BENCH_ROOT:-}" ]; then BENCH_SV_DIR=$(cd "$BENCH_ROOT/../lib" 2> /dev/null && pwd -P) || BENCH_SV_DIR=; fi
if [ ! -r "$BENCH_SV_DIR/supervise.sh" ]; then BENCH_SV_DIR=${BENCH_SV_LIB:-}; fi

# bench_sv: sources the supervisor's library and builds the supervisor
# (sv_path), once per shell: BENCH_SV is its path. Exits 2 when it cannot,
# with sv_path's reason and its own line on standard error. bench_bounded
# calls it, so a caller that sends the bounded command's standard error
# away (host.sh, snap.sh) calls it first, or the reason is lost (#758).
bench_sv() {
	[ -z "$BENCH_SV" ] || return 0
	case $BENCH_SV_DIR in
	/*) [ -r "$BENCH_SV_DIR/supervise.sh" ] || bench_die 2 "cannot read the process supervisor, scripts/lib/supervise.sh" ;;
	*) bench_die 2 "cannot find the process supervisor, scripts/lib/supervise.sh" ;;
	esac
	# shellcheck source=../../lib/supervise.sh
	. "$BENCH_SV_DIR/supervise.sh"
	SV_SRC_DIR=$BENCH_SV_DIR
	BENCH_SV=$(sv_path) || bench_die 2 "cannot build the process supervisor"
	SV_BIN=$BENCH_SV
}

# bench_bounded <seconds> <command...>: runs the command, standard input
# from /dev/null, under the process supervisor (sv_start, TASK221): the
# command leads a process group of its own, with every signal at its
# default, and the supervisor owns its reap. Past <seconds> (--timeout), and
# when the command exits while members of its group are left, the
# supervisor sends the group TERM and CONT, KILL after BENCH_STOP_GRACE
# seconds (10), and returns once the group is empty. Returns the command's
# status; 124 past the wall time; 125 when the supervisor could not empty
# the group (its 126: the command may still run): its group is then
# recorded in the lock state (bench_bounded_untold), so the lock it runs
# under is kept while the group has a member, whatever this shell does next
# (FIX184), and it is said; with no group named, the lock is kept until the
# record is removed by hand (bench_bounded_untold). A trap's stop that comes
# after the status is collected acts on it (bench_bounded_stop). A command
# that cannot be run is 127. A first
# word bench_to <out> <err> sends the command's output to those files (the
# supervisor's own lines too). BENCH_BOUNDED_PID is the supervisor's pid
# while it runs: a caller's trap stops it with bench_stop_group, which
# sends it TERM and waits for it. The supervisor's events go to a file of
# this shell's (BENCH_BOUNDED_EV, under TMPDIR, removed before the bound
# returns and by a trap's stop, bench_bounded_stop); on a timeout they are
# appended to standard error. With BENCH_BOUNDED_TAP, a path (a test's
# FIFO), the events also go there, through tee (bench_bounded_tee).
# It is the one bounded-run primitive (FIX122): the adapters,
# scripts/pins.sh, scripts/ios-sim.sh and scripts/ios-device.sh run their
# commands through it, and tests/check-bounded-run.sh refuses another
# TERM, grace and KILL under scripts/. What ended the run is in
# BENCH_BOUNDED_HOW, since a command may itself exit 124, 125 or 126,
# which are told from the supervisor's own by its events
# (bench_bounded_ours): "exit" (its own status), "wall", "until" or
# "untold" (125). A stopped command
# (a stop signal) is no longer told apart: the supervisor's CONT ends the
# stop at the wall time. Two settings, each empty for none, read at a poll
# of the supervisor every second (bench_nap 1) while it runs:
# BENCH_BOUNDED_QUIT, whole seconds: at that time the supervisor gets TERM
# once (the group's stop, with its grace), the line
# "bench: <BENCH_BOUNDED_TAG>TERM after <n> s" goes to standard output, and
# the status is the command's own (the supervisor's "reaped" event; a quit
# time not under the wall time is never reached);
# BENCH_BOUNDED_UNTIL, the name of a function: once it is true at a poll,
# the group is stopped as past the wall time, the status is 124 and
# BENCH_BOUNDED_HOW "until"; one that names no function is refused before
# the command starts (status 2, as a bad wall time is), and one that is no
# longer defined at a later poll is refused too: the group is stopped, then
# the same status 2 and a line, never "wall".
bench_bounded() {
	bench_is_uint "$1" || bench_die 2 "a wall time is whole seconds"
	bench_bounded_run "$1" "${BENCH_STOP_GRACE:-10}" /dev/null "$@"
}

# bench_bounded_run <seconds or empty> <grace> <input> <seconds> <command...>:
# bench_bounded's run, also for the lock run (TASK221 slice 3): an empty
# <seconds> is no wall time (no --timeout); <grace> is the supervisor's
# --grace; <input> is /dev/null, or - for this shell's standard input. The
# fourth word, the caller's own <seconds> or a placeholder, is dropped.
# With BENCH_BOUNDED_STARTED, the name of a function, that function is
# called with the supervisor's pid once it has started.
bench_bounded_run() {
	_bb_s=$1 _bb_g=$2 _bb_i=$3
	shift 4
	_bb_q=${BENCH_BOUNDED_QUIT:-}
	if [ -n "$_bb_q" ]; then
		bench_is_uint "$_bb_q" || bench_die 2 "a quit time is whole seconds"
		[ -n "$_bb_s" ] && [ "$_bb_q" -lt "$_bb_s" ] || _bb_q=
	fi
	if [ -n "${BENCH_BOUNDED_UNTIL:-}" ] && ! command -v "$BENCH_BOUNDED_UNTIL" > /dev/null 2>&1; then
		bench_die 2 "BENCH_BOUNDED_UNTIL names no function: $BENCH_BOUNDED_UNTIL"
	fi
	bench_sv
	_bb_t0=$(date +%s)
	BENCH_BOUNDED_HOW=
	_bb_o= _bb_e=
	if [ "${1:-}" = bench_to ]; then
		_bb_o=$2 _bb_e=$3
		shift 3
	fi
	# from here a caller's trap calls bench_stop_group, which finds the
	# supervisor also in sv_start's fork window (sv_pid), and its status
	# once sv_wait has collected it (SV_STATUS, empty until then); before
	# the start it removes the events file (bench_bounded_end)
	SV_STATUS=
	BENCH_BOUNDED_PID=starting
	bench_bounded_ev || bench_die 2 "cannot make a temporary file"
	_bb_v=$BENCH_BOUNDED_EV
	if [ -n "${BENCH_BOUNDED_TAP:-}" ]; then
		# a test's FIFO: the events go to the file too, through tee, which
		# opens its FIFO before the supervisor's start does (bench_bounded_tee)
		_bb_v=$BENCH_BOUNDED_EV.tap
		if ! mkfifo -m 600 "$_bb_v"; then
			bench_bounded_end
			bench_die 2 "cannot make a FIFO"
		fi
		tee -a "$BENCH_BOUNDED_EV" < "$_bb_v" >> "$BENCH_BOUNDED_TAP" 2> /dev/null &
		BENCH_BOUNDED_TEE=$!
	fi
	# --timeout 0 is no deadline to the supervisor; a wall time of 0 is one
	if [ -n "$_bb_s" ]; then
		_bb_w=$_bb_s
		[ "$_bb_w" -ne 0 ] || _bb_w=0.001
		set -- --timeout "$_bb_w" --grace "$_bb_g" --events 9 -- "$@"
	else
		set -- --grace "$_bb_g" --events 9 -- "$@"
	fi
	if [ -z "$_bb_o" ] && [ "$_bb_i" = - ] && { :; } 2> /dev/null 3<&0; then
		# this shell's standard input, through fd 3 (an asynchronous list
		# reads /dev/null otherwise); a closed one is /dev/null below
		SV_STDIN3=1
		sv_start "$BENCH_SV" "$@" 3<&0 9>> "$_bb_v" || :
		SV_STDIN3=
	elif [ -z "$_bb_o" ]; then
		sv_start "$BENCH_SV" "$@" < /dev/null 9>> "$_bb_v" || :
	elif [ "$_bb_o" = "$_bb_e" ]; then
		sv_start "$BENCH_SV" "$@" < /dev/null > "$_bb_o" 2>&1 9>> "$_bb_v" || :
	else
		sv_start "$BENCH_SV" "$@" < /dev/null > "$_bb_o" 2> "$_bb_e" 9>> "$_bb_v" || :
	fi
	BENCH_BOUNDED_PID=$SV_PID
	if [ -z "$BENCH_BOUNDED_PID" ]; then
		# a redirection failed: no supervisor started
		bench_bounded_end
		BENCH_BOUNDED_HOW=exit
		return 1
	fi
	if [ -n "${BENCH_BOUNDED_STARTED:-}" ]; then "$BENCH_BOUNDED_STARTED" "$BENCH_BOUNDED_PID" || :; fi
	_bb_n=
	if [ -n "$_bb_q" ] || [ -n "${BENCH_BOUNDED_UNTIL:-}" ]; then
		while bench_pid_alive "$BENCH_BOUNDED_PID"; do
			if [ -n "${BENCH_BOUNDED_UNTIL:-}" ]; then
				# a test gone since the last poll is refused, not read as false
				if ! command -v "$BENCH_BOUNDED_UNTIL" > /dev/null 2>&1; then
					_bb_n=gone
					break
				fi
				if "$BENCH_BOUNDED_UNTIL"; then
					_bb_n=until
					break
				fi
			fi
			if [ -n "$_bb_q" ] && [ $(($(date +%s) - _bb_t0)) -ge "$_bb_q" ]; then
				printf 'bench: %sTERM after %s s\n' "${BENCH_BOUNDED_TAG:-}" "$_bb_q"
				_bb_n=quit
				break
			fi
			bench_nap 1
		done
		[ -z "$_bb_n" ] || sv_term
	fi
	_bb_st=0
	sv_wait || _bb_st=$?
	bench_bounded_tee
	# the supervisor's own 124, 125 or 126, by its events; else the command's
	_bb_w=exit
	if bench_bounded_ours "$_bb_st"; then _bb_w=$_bb_st; fi
	case $_bb_n:$_bb_w in
	*:126)
		BENCH_BOUNDED_HOW=untold
		bench_bounded_untold
		bench_bounded_end
		if [ -n "$_bb_s" ]; then
			bench_note "a command outlived its wall time of $_bb_s s, or what it left running did, and it could not be stopped: it may still run"
		else
			bench_note "a command, or what it left running, could not be stopped: it may still run"
		fi
		return 125
		;;
	gone:*)
		bench_bounded_end
		BENCH_BOUNDED_HOW=
		bench_die 2 "BENCH_BOUNDED_UNTIL names no function at a poll: ${BENCH_BOUNDED_UNTIL:-}; stopped the command"
		;;
	until:*)
		bench_bounded_end
		BENCH_BOUNDED_HOW=until
		return 124
		;;
	quit:*) _bb_st=$(bench_bounded_event reaped status) || _bb_st=143 ;;
	*:124)
		cat "$BENCH_BOUNDED_EV" >&2 2> /dev/null || :
		bench_bounded_end
		BENCH_BOUNDED_HOW=wall
		bench_note "stopped a command that outlived its wall time of $_bb_s s"
		return 124
		;;
	*:125) _bb_st=127 ;;
	esac
	# a stop that began after the command's own exit stopped what it left
	if [ -f "$BENCH_BOUNDED_EV" ] && awk '$1 == "exited" { e = 1 } $1 == "term-sent" && e { f = 1 } END { exit !f }' "$BENCH_BOUNDED_EV"; then
		bench_note "stopped what the command left running in its process group"
	fi
	bench_bounded_end
	BENCH_BOUNDED_HOW=exit
	return "$_bb_st"
}

# bench_bounded_event <event> <key>: the value of <key>= in the last line
# of the supervisor's events (BENCH_BOUNDED_EV) that starts with <event>;
# false when there is none
bench_bounded_event() {
	[ -f "${BENCH_BOUNDED_EV:-}" ] || return 1
	awk -v e="$1" -v k="$2=" '$1 == e { for (i = 2; i <= NF; i++) if (index($i, k) == 1) v = substr($i, length(k) + 1) } END { if (v == "") exit 1; print v }' "$BENCH_BOUNDED_EV"
}

# bench_bounded_said <event>: the supervisor's events hold a line <event>
bench_bounded_said() {
	[ -f "${BENCH_BOUNDED_EV:-}" ] && awk -v e="$1" '$1 == e { f = 1 } END { exit !f }' "$BENCH_BOUNDED_EV"
}

# bench_bounded_ours <status>: the supervisor's status <status> is its own,
# not the command's, which it passes through and which may be 124, 125 or
# 126 too (correctness B1, round 1). By its events: 124 is the wall time
# only with a "deadline" event; 125 (cannot run) only with no "spawned"
# event; 126 (cannot empty the group) only with no "reaped" event, since
# the group may then still have a member. Any other status is the command's.
bench_bounded_ours() {
	case $1 in
	124) bench_bounded_said deadline ;;
	125) ! bench_bounded_said spawned ;;
	126) ! bench_bounded_said reaped ;;
	*) return 1 ;;
	esac
}

# bench_bounded_untold: after a supervisor's 126, records the command's
# process group (its "spawned" event) in the lock state, so the lock is
# kept while the group has a member (bench_lock_record <pgid> group).
# When the supervisor named no group (a failed events write, or its
# "cannot watch the command"), nothing tells when the command is gone: the
# lock is kept until the record is removed by hand (bench_lock_record <id>
# hand, under the supervisor's pid, else this shell's; security I1, review
# round 2). A caller without the lock's code (core/lock.sh) runs under no
# lock of this core's, and records nothing.
bench_bounded_untold() {
	_bu_g=$(bench_bounded_event spawned pgid) || _bu_g=
	case $_bu_g in
	'' | 0 | 1 | *[!0-9]*)
		bench_note "the supervisor named no process group to watch"
		_bu_g=${BENCH_BOUNDED_PID:-}
		case $_bu_g in '' | 0 | 1 | *[!0-9]*) _bu_g=$$ ;; esac
		_bu_k=hand
		;;
	*) _bu_k=group ;;
	esac
	if command -v bench_lock_record > /dev/null 2>&1; then bench_lock_record "$_bu_g" "$_bu_k"; fi
}

# bench_bounded_ev: makes the events file, BENCH_BOUNDED_EV: a name mktemp
# gives (-u), set before the file is made (noclobber, mode 0600), so a
# trap that runs at any point finds its own file by that name
# (bench_bounded_end), and only that one (correctness N3, round 1). It is
# made in TMPDIR, or in BENCH_BOUNDED_EVDIR when that is set: the lock run
# keeps its events in the lock directory, where the waiters read the
# command's group from them (bench_lock_group_live, TASK221 slice 3)
bench_bounded_ev() {
	_be_n=0
	while [ "$_be_n" -lt 5 ]; do
		BENCH_BOUNDED_EV=$(mktemp -u "${BENCH_BOUNDED_EVDIR:-${TMPDIR:-/tmp}}/bench-sv.$$.XXXXXX") || BENCH_BOUNDED_EV=
		[ -n "$BENCH_BOUNDED_EV" ] || return 1
		if (umask 077 && set -C && : > "$BENCH_BOUNDED_EV") 2> /dev/null; then return 0; fi
		BENCH_BOUNDED_EV=
		_be_n=$((_be_n + 1))
	done
	return 1
}

# bench_bounded_tee: waits for the tee of BENCH_BOUNDED_TAP, once the
# supervisor is gone, so the events file is whole. A read-write open of
# its FIFO first ends a tee whose FIFO no supervisor opened (a stop before
# the start): it sees a writer come and go, and end of file.
bench_bounded_tee() {
	[ -n "$BENCH_BOUNDED_TEE" ] || return 0
	{ :; } 9<> "$BENCH_BOUNDED_EV.tap" 2> /dev/null || :
	wait "$BENCH_BOUNDED_TEE" 2> /dev/null || :
	BENCH_BOUNDED_TEE=
}

# bench_bounded_end: the bounded command is over: no pid, no events file
bench_bounded_end() {
	bench_bounded_tee
	BENCH_BOUNDED_PID=
	if [ -n "$BENCH_BOUNDED_EV" ]; then rm -f "$BENCH_BOUNDED_EV" "$BENCH_BOUNDED_EV.tap"; fi
	BENCH_BOUNDED_EV=
}

# bench_bounded_stop: bench_stop_group for the bounded command: TERM to its
# supervisor, which stops the group, and a wait for it (sv_stop); its 126
# is recorded as bench_bounded records it. When bench_bounded's sv_wait has
# already collected the supervisor's status (SV_PID empty, SV_STATUS set),
# that status is the one acted on, not sv_stop's 0 for no supervisor: a
# trap that comes after a 126 is collected and before bench_bounded has
# recorded it records it here (correctness I1, review round 2).
# SV_STATUS is tested first: sv_wait sets it before it clears SV_PID, and
# bench_bounded empties it before each run, so a set SV_STATUS is this
# run's collected status at any point of sv_wait, and the supervisor it
# belongs to is not signalled or waited for again.
# A trap that lands after sv_wait's wait has reaped the supervisor and
# before SV_STATUS is set waits again in sv_stop: bash gives the status
# again, /bin/sh (bench.sh's shell) gives 127, which is the shell's, not
# the supervisor's (it never returns 127 itself). So a status is decided
# by the supervisor's events too, never by the wait alone: with no
# "reaped" event, a 127, or any status after a "spawned" event, is taken
# as its 126 (a group record when "spawned" names one, else a hand
# record), since a command's own status always comes with "reaped"
# (review round 3, correctness I1 and security I2).
bench_bounded_stop() {
	_bs_st=0
	if [ -n "$SV_STATUS" ]; then
		_bs_st=$SV_STATUS
	else
		sv_stop || _bs_st=$?
	fi
	bench_bounded_tee
	if bench_bounded_ours "$_bs_st"; then
		[ "$_bs_st" -ne 126 ] || bench_bounded_untold
	elif ! bench_bounded_said reaped && { [ "$_bs_st" -eq 127 ] || bench_bounded_said spawned; }; then
		bench_bounded_untold
	fi
	bench_bounded_end
}

# bench_to <out> <err> <command...>: becomes the command (exec) with
# standard output to the file <out> and standard error to <err>; the same
# path twice opens it once for both. For bench_bounded's callers, so
# that a command's output goes to its log while the core's own lines stay
# on the caller's standard error. A file that cannot be opened ends it
# with a failure, and so does a command that cannot be run (127).
bench_to() {
	_bt_o=$1
	_bt_e=$2
	shift 2
	if [ "$_bt_o" = "$_bt_e" ]; then exec "$@" > "$_bt_o" 2>&1; fi
	exec "$@" > "$_bt_o" 2> "$_bt_e"
}

# bench_deferred <status>: the first call of the signal trap of a caller of
# bench_bounded outside bench.sh (FIX122). Nothing is deferred since
# TASK221 slice 3 (the supervisor's fork window is sv_pid's), so it is
# false: the trap goes on to stop the bounded command (bench_stop_group)
# and exit.
bench_deferred() {
	return 1
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
