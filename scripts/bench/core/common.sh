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

# bench_proc_read <pid> [<field>]: one ps call for <pid>: BENCH_PS_F, its
# <field> (stat by default), and BENCH_PS_START, its start time (ps -o
# lstart=, which exec does not change) in one form: its words one blank
# apart (ps pads it, and puts two blanks before a day of one digit). False
# when ps gives no answer. Every start time the core compares comes from
# here (FIX172).
bench_proc_read() {
	_pq_o=$(ps -o "${2:-stat}=" -o lstart= -p "$1" 2> /dev/null) || return 1
	case $- in
	*f*) _pq_f= ;;
	*)
		_pq_f=1
		set -f
		;;
	esac
	# shellcheck disable=SC2086 # split into words; globbing is off
	set -- $_pq_o
	if [ -n "$_pq_f" ]; then set +f; fi
	[ "$#" -ge 2 ] || return 1
	BENCH_PS_F=$1
	shift
	BENCH_PS_START=$*
}

# bench_proc_same <pid> <start>: whether <pid> is still the process that
# had the start time <start> (bench_proc_read's form): 0 it is (a zombie
# too: BENCH_PS_F holds its state), 1 <pid> is gone (kill -0 fails), 3
# another process has <pid> (another start time; "-" matches none), 2 this
# shell cannot tell: <pid> answers kill -0 but ps gives no answer for it,
# or no start time was read for it ("?" or empty). Liveness comes from
# kill -0 and never from a silent ps (FIX177's rule).
bench_proc_same() {
	bench_pid_alive "$1" || return 1
	if ! bench_proc_read "$1"; then
		bench_pid_alive "$1" || return 1
		return 2
	fi
	case ${2:-} in
	'' | '?') return 2 ;;
	esac
	[ "$BENCH_PS_START" = "$2" ] || return 3
	return 0
}

BENCH_DEFER=
BENCH_DEFERRED=
BENCH_SPAWN_PID=
BENCH_SPAWN_SAID=
BENCH_SPAWN_START=
BENCH_SPAWN_GROUP=
# What bench_spawn started, one entry per command: ";<pid>|<group>|<start>"
# (bench_spawn_id). The watch and the stops read a pid's identity here and
# nowhere else, so a pid this shell did not start is never signalled.
BENCH_SPAWNED=

# bench_spawn_id <pid>: records the command bench_spawn has just started as
# <pid> (FIX172): BENCH_SPAWN_START, its start time (bench_proc_read's form;
# "-" when it has gone already, "?" when ps gives no answer for it while it
# runs, after three tries 0.1 s apart), and BENCH_SPAWN_GROUP, <pid> when
# it leads its own process group ("-" when it does not or has gone, "?"
# when ps gave no answer). Both are appended to BENCH_SPAWNED.
bench_spawn_id() {
	BENCH_SPAWN_START='?'
	BENCH_SPAWN_GROUP='?'
	_si_n=0
	while :; do
		if bench_proc_read "$1" pgid; then
			BENCH_SPAWN_START=$BENCH_PS_START
			if [ "$BENCH_PS_F" = "$1" ]; then BENCH_SPAWN_GROUP=$1; else BENCH_SPAWN_GROUP=-; fi
			break
		fi
		if ! bench_pid_alive "$1"; then
			BENCH_SPAWN_START=-
			BENCH_SPAWN_GROUP=-
			break
		fi
		_si_n=$((_si_n + 1))
		[ "$_si_n" -lt 3 ] || break
		bench_nap 0.1
	done
	BENCH_SPAWNED="$BENCH_SPAWNED;$1|$BENCH_SPAWN_GROUP|$BENCH_SPAWN_START"
}

# bench_spawned <pid>: the identity bench_spawn recorded for <pid>, its
# latest entry: BENCH_ID_GROUP and BENCH_ID_START. False for a pid it did
# not start.
bench_spawned() {
	case $BENCH_SPAWNED in
	*";$1|"*) ;;
	*) return 1 ;;
	esac
	_sd_r=${BENCH_SPAWNED##*";$1|"}
	BENCH_ID_GROUP=${_sd_r%%|*}
	_sd_r=${_sd_r#*|}
	BENCH_ID_START=${_sd_r%%;*}
}

# bench_spawn <command...>: starts the command in the background as the
# leader of a new process group, and sets BENCH_SPAWN_PID (its pid, which
# is also the group id). This relies on job control in a non-interactive
# shell (set -m), which bash (3.2 and later, also as sh) and zsh have: the
# command then starts with the signal dispositions of this shell (an
# asynchronous list without job control starts with SIGINT and SIGQUIT
# ignored), and bench_stop_group reaches its whole process group (not a
# descendant that moves itself into another group or session, with
# setsid or a job-control shell). A shell
# without it (dash with no terminal) leaves the command in this shell's
# group; it is said once, and the stop then reaches only the command's pid.
# The command gets this shell's standard input (through fd 3), or /dev/null
# when standard input is closed, and this shell's standard error (through
# fd 9), or /dev/null when that is closed; it does not inherit fd 9, nor
# fd 3 when standard input is open. The fork itself runs with standard
# error on /dev/null: the forked
# shell sets its own process group before it becomes the command, and when
# that setpgid fails (EPERM, seen under load on macOS while this shell's
# setpgid makes the same group, which it does) bash says "child setpgid
# (<pid> to <pid>): Operation not permitted" on the standard error it has
# then, a line that names an internal path (FIX83). It sets BENCH_DEFER:
# bench_on_signal (in bench.sh) only notes a signal that arrives before the
# caller has stored the pid, and the caller's bench_undefer acts on it, so
# no signal falls between the "&" and "$!". It records the command's
# identity (bench_spawn_id): every later signal to it, or to its group,
# goes only while that pid is still this command (FIX172).
bench_spawn() {
	BENCH_DEFER=1
	{ :; } 9>&2 2> /dev/null || exec 2> /dev/null
	set -m
	if { :; } 2> /dev/null 3<&0; then
		{ "$@" <&3 3<&- 2>&9 9>&- & } 3<&0 9>&2 2> /dev/null
	else
		{ "$@" < /dev/null 2>&9 9>&- & } 9>&2 2> /dev/null
	fi
	BENCH_SPAWN_PID=$!
	set +m
	bench_spawn_id "$BENCH_SPAWN_PID"
	if [ "$BENCH_SPAWN_GROUP" = - ] && [ "$BENCH_SPAWN_START" != - ] && [ -z "$BENCH_SPAWN_SAID" ]; then
		BENCH_SPAWN_SAID=1
		bench_note "this shell has no job control (set -m), so a stop reaches only the command's own pid"
	fi
}

# bench_undefer: ends the window bench_spawn opened, and acts on a signal
# that arrived in it.
bench_undefer() {
	BENCH_DEFER=
	if [ -n "$BENCH_DEFERRED" ]; then
		_ud_s=$BENCH_DEFERRED
		BENCH_DEFERRED=
		bench_on_signal "$_ud_s"
	fi
}

# bench_group_alive <pid> [group]: something of the command bench_spawn
# started as <pid> is left to stop. When it led its own process group at
# spawn: that group still has a member (bash reaps an exited child at
# once, so a dead leader is not one), and no other process has <pid>.
# Otherwise (a shell without job control): <pid> itself, while it is still
# that command and not a zombie, or while this shell cannot tell; with
# "group" (it is reaped), nothing.
bench_group_alive() {
	bench_spawned "$1" || return 1
	_ga_r=0
	bench_proc_same "$1" "$BENCH_ID_START" || _ga_r=$?
	if [ "$BENCH_ID_GROUP" = "$1" ]; then
		[ "$_ga_r" -ne 3 ] || return 1
		kill -0 -- "-$1" 2> /dev/null
		return
	fi
	[ "${2:-}" != group ] || return 1
	case $_ga_r in
	0)
		case $BENCH_PS_F in
		Z*) return 1 ;;
		esac
		return 0
		;;
	2) return 0 ;;
	esac
	return 1
}

# bench_signal_group <signal> <pid> [group]: the signal to the command
# bench_spawn started as <pid> (FIX172). When it led its own process group
# at spawn: to that group only, never to the pid alone, and only while
# <pid> is still that command or no process has <pid> (its leader was
# reaped; the system gives no new process the id of a process group that
# still has a member). When it led none (a shell without job control): to
# <pid> alone, only while it is still that command, and never with "group"
# (it is reaped). Returns 0 once sent; 1 when nothing of the command is left
# to signal (it is gone, another process has <pid>, or bench_spawn did not
# start <pid>); 2 when this shell cannot tell (bench_proc_same), and then
# nothing is sent.
bench_signal_group() {
	bench_spawned "$2" || return 1
	_sg2_r=0
	bench_proc_same "$2" "$BENCH_ID_START" || _sg2_r=$?
	if [ "$BENCH_ID_GROUP" = "$2" ]; then
		case $_sg2_r in
		0 | 1)
			kill -s "$1" -- "-$2" 2> /dev/null || return 1
			return 0
			;;
		3) return 1 ;;
		esac
		return 2
	fi
	[ "${3:-}" != group ] || return 1
	case $_sg2_r in
	0)
		kill -s "$1" "$2" 2> /dev/null || return 1
		return 0
		;;
	2) return 2 ;;
	esac
	return 1
}

# bench_untold <pid>: says that a stop of <pid> sent no signal because this
# shell cannot tell whether <pid> is still the command it started.
bench_untold() {
	bench_note "cannot tell whether pid $1 is still the command this shell started (ps gives no answer for it, or none was read at its start), so it was sent no signal"
}

# bench_stop_group <pid> [<grace>] [group]: stops the command bench_spawn
# started as <pid>, and everything in its process group: TERM (and CONT, so
# a stopped member gets it) to the group, up to <grace> seconds (default
# BENCH_STOP_GRACE, else 10; whole seconds by the clock, so between <grace>
# and one more) for the group to go, then KILL to the group, and up to 2 s
# for it to go; then reaps the leader. A shell without job control falls
# back to the pid alone. With "group" (the leader is reaped already), the
# group only, and no reap. Every signal goes through bench_signal_group, so
# only while <pid> is still the command (FIX172): when nothing of it is
# left, the stop sends nothing more and reaps nothing; when this shell
# cannot tell, it says so (bench_untold) and sends nothing more, and does
# not reap, since a wait for a command that still runs, stopped, would not
# return. Its standard error is /dev/null (the shell's notice of a job
# killed by a signal names an internal path), all but that line.
bench_stop_group() {
	_sg_p=$1
	_sg_g=${2:-${BENCH_STOP_GRACE:-10}}
	_sg_o=${3:-}
	case $_sg_p in
	'' | 0 | 1 | *[!0-9]*) return 0 ;;
	esac
	_sg_r=0
	bench_signal_group TERM "$_sg_p" "$_sg_o" || _sg_r=$?
	case $_sg_r in
	1) return 0 ;;
	2)
		bench_untold "$_sg_p" 2>&8
		return 0
		;;
	esac
	bench_signal_group CONT "$_sg_p" "$_sg_o" || true
	_sg_end=$(($(date +%s) + _sg_g))
	while bench_group_alive "$_sg_p" "$_sg_o" && [ "$(date +%s)" -le "$_sg_end" ]; do
		bench_nap 0.1
	done
	if bench_group_alive "$_sg_p" "$_sg_o"; then
		_sg_r=0
		bench_signal_group KILL "$_sg_p" "$_sg_o" || _sg_r=$?
		if [ "$_sg_r" -eq 2 ]; then
			bench_untold "$_sg_p" 2>&8
			return 0
		fi
		_sg_n=0
		while bench_group_alive "$_sg_p" "$_sg_o" && [ "$_sg_n" -lt 20 ]; do
			bench_nap 0.1
			_sg_n=$((_sg_n + 1))
		done
	fi
	[ "$_sg_o" = group ] || wait "$_sg_p" || true
} 8>&2 2> /dev/null

# bench_watch <pid> <end> <cap>: polls the command bench_spawn started as
# <pid> until it exits (0), it is stopped (2: state T, a stop signal, or
# the terminal it touched from its background process group), or the clock
# passes <end> (1; seconds since the epoch, empty for none). Only the
# leader's state is read, so a stop of a single member of the group, or a
# terminal stop of a member whose leader catches or ignores SIGTTOU, is not
# detected. It polls after
# 0.01 s, 0.02 s, then every 0.05 s up to the 40th poll (about 2 s), so a
# short command is seen to end at once, every 0.2 s for the next 100
# (about 20 s), then every
# <cap> seconds, so a long one costs little. The command is the one
# bench_spawn recorded (FIX172): once <pid> has gone, or another process has
# it (another start time), the command has exited (0). While this shell
# cannot tell (bench_proc_same), it says so once and polls on, and
# reports a stop only of the command itself. Its standard error is
# /dev/null (the shell's job notices), all but that line.
bench_watch() {
	_bw_p=$1
	_bw_end=$2
	_bw_cap=$3
	_bw_n=0
	_bw_told=
	bench_spawned "$_bw_p" || return 0
	_bw_s=$BENCH_ID_START
	while :; do
		_bw_r=0
		bench_proc_same "$_bw_p" "$_bw_s" || _bw_r=$?
		case $_bw_r in
		0)
			case $BENCH_PS_F in
			Z*) return 0 ;;
			T*) return 2 ;;
			esac
			;;
		2)
			if [ -z "$_bw_told" ]; then
				_bw_told=1
				bench_note "cannot tell whether pid $_bw_p is still the command this shell started (ps gives no answer for it, or none was read at its start); it is not signalled while that lasts" 2>&8
			fi
			;;
		*) return 0 ;;
		esac
		if [ -n "$_bw_end" ] && [ "$(date +%s)" -gt "$_bw_end" ]; then
			return 1
		fi
		_bw_n=$((_bw_n + 1))
		if [ "$_bw_n" -le 2 ]; then
			bench_nap "0.0$_bw_n"
		elif [ "$_bw_n" -le 40 ]; then
			bench_nap 0.05
		elif [ "$_bw_n" -le 140 ]; then
			bench_nap 0.2
		else
			bench_nap "$_bw_cap"
		fi
	done
	return 0
} 8>&2 2> /dev/null

# bench_end_group <pid> <grace>: after the leader <pid> exited and was
# reaped, gives what it left running in its process group 1 s (by the
# clock, so 1 to 2 s) to end, then stops it (bench_stop_group with the
# caller's <grace>, the group only). Returns 1 when it had to stop
# something. Nothing to do without job control: the command then leads no
# group. The group is the one it led at spawn (bench_group_alive), so a
# group another process leads under the same id once the command's group
# is gone is left alone (FIX172).
bench_end_group() {
	bench_group_alive "$1" group || return 0
	_eg_end=$(($(date +%s) + 1))
	while bench_group_alive "$1" group; do
		if [ "$(date +%s)" -gt "$_eg_end" ]; then
			bench_stop_group "$1" "$2" group
			return 1
		fi
		bench_nap 0.1
	done
	return 0
}

BENCH_BOUNDED_PID=
BENCH_BOUNDED_REAPED=

# bench_bounded <seconds> <command...>: runs the command, standard input
# from /dev/null, in its own process group (bench_spawn), and stops the
# group (bench_stop_group) once the command has run more than <seconds>
# (whole seconds by the clock, so between <seconds> and one more), or when
# it is stopped (bench_watch). Returns its status; 124 past the wall time,
# 74 when it was stopped. Once the command has exited, what it left running
# in its group is stopped too (bench_end_group, with the same
# BENCH_STOP_GRACE) before the bound returns.
# For adapters. The poll settles at every 0.5 s.
bench_bounded() {
	_bb_s=$1
	shift
	bench_is_uint "$_bb_s" || bench_die 2 "a wall time is whole seconds"
	_bb_end=$(($(date +%s) + _bb_s))
	bench_spawn "$@" < /dev/null
	BENCH_BOUNDED_PID=$BENCH_SPAWN_PID
	BENCH_BOUNDED_REAPED=
	bench_undefer
	_bb_w=0
	bench_watch "$BENCH_BOUNDED_PID" "$_bb_end" 0.5 || _bb_w=$?
	if [ "$_bb_w" -ne 0 ]; then
		bench_stop_group "$BENCH_BOUNDED_PID"
		BENCH_BOUNDED_PID=
		if [ "$_bb_w" -eq 2 ]; then
			bench_note "the command stopped (a stop signal, or the terminal); stopped it"
			return 74
		fi
		bench_note "stopped a command that outlived its wall time of $_bb_s s"
		return 124
	fi
	_bb_st=0
	wait "$BENCH_BOUNDED_PID" 2> /dev/null || _bb_st=$?
	BENCH_BOUNDED_REAPED=group
	bench_end_group "$BENCH_BOUNDED_PID" "${BENCH_STOP_GRACE:-10}" || bench_note "stopped what the command left running in its process group"
	BENCH_BOUNDED_PID=
	BENCH_BOUNDED_REAPED=
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
