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

BENCH_POLL_PID=

# bench_poll <command...>: runs the command (a function of this shell, or
# any command) in the background and waits for it with the wait builtin,
# never as a foreground command or a command substitution (FIX192). With
# job control off, bash 3.2 swaps its own SIGINT handler in while it waits
# for a foreground command, and that handler only notes an INT; the note
# becomes a trap call only when a job is reaped, and a command
# substitution has no job, so an INT from the end of its output to its
# reap is dropped. In the wait builtin an INT runs the trap at once. The
# command runs in this shell's process group with INT and QUIT ignored (a
# background job without job control), so a Ctrl-C reaches this shell's
# trap and not the poll, and its pid is BENCH_POLL_PID until it is
# collected: a trap that ends this shell stops it first (bench_poll_end).
# A trap that returns ends the wait early with a status above 128 and
# leaves the command running or uncollected; the wait then goes on while
# the pid is there (bench_pid_alive: running, or exited and not yet
# reaped), and once it is not, one more wait collects the status (bash
# keeps it; a shell that does not says 127, and the status above 128 is
# kept). At most 100 waits: each one past the first needs a signal that
# came in the one before, or a pid the system gave again. Returns the
# command's status (above 128: ended by a signal).
bench_poll() {
	"$@" &
	BENCH_POLL_PID=$!
	_po_p=$BENCH_POLL_PID
	_po_st=0
	wait "$_po_p" 2> /dev/null || _po_st=$?
	[ "$_po_st" -gt 128 ] || bench_poll_done "$_po_p"
	_po_n=0
	while [ "$_po_st" -gt 128 ] && [ "$_po_n" -lt 100 ]; do
		_po_n=$((_po_n + 1))
		if bench_pid_alive "$_po_p"; then
			_po_st=0
			wait "$_po_p" 2> /dev/null || _po_st=$?
			[ "$_po_st" -gt 128 ] || bench_poll_done "$_po_p"
			continue
		fi
		_po_s2=0
		wait "$_po_p" 2> /dev/null || _po_s2=$?
		bench_poll_done "$_po_p"
		[ "$_po_s2" -eq 127 ] || _po_st=$_po_s2
		break
	done
	bench_poll_done "$_po_p"
	return "$_po_st"
}

# bench_poll_done <pid>: bench_poll's job <pid> is collected: it is
# BENCH_POLL_PID no more, so bench_poll_end sends nothing to it. Called
# right after the wait that collects it, before the loop's next test
# (FIX192, round 2).
bench_poll_done() {
	if [ "$BENCH_POLL_PID" = "$1" ]; then BENCH_POLL_PID=; fi
}

BENCH_POLL_DIR=
BENCH_POLL_LINE=
BENCH_POLL_SEQ=0

# bench_poll_line <command...>: bench_poll for a command whose output is
# wanted: its first line goes to BENCH_POLL_LINE (empty when it printed
# none), through a file in a directory of this shell's own, made for the
# call and removed after it (mode 700, under TMPDIR; mkdir makes it or
# fails, and never follows a name that is there already). So the read
# waits for no command substitution, and an INT runs the trap at once
# (FIX192, round 2). When no directory can be made (three names tried),
# the command runs in a command substitution, as before, with bash 3.2's
# window. Returns the command's status (above 128: ended by a signal).
bench_poll_line() {
	BENCH_POLL_LINE=
	_pl_n=0
	_pl_d=
	while [ "$_pl_n" -lt 3 ]; do
		_pl_n=$((_pl_n + 1))
		BENCH_POLL_SEQ=$((BENCH_POLL_SEQ + 1))
		_pl_d=${TMPDIR:-/tmp}/bench-poll.$$.$BENCH_POLL_SEQ
		if mkdir -m 700 "$_pl_d" 2> /dev/null; then break; fi
		_pl_d=
	done
	_pl_st=0
	if [ -z "$_pl_d" ]; then
		_pl_o=$("$@") || _pl_st=$?
		BENCH_POLL_LINE=${_pl_o%%"
"*}
		return "$_pl_st"
	fi
	BENCH_POLL_DIR=$_pl_d
	bench_poll bench_poll_to "$_pl_d/out" "$@" || _pl_st=$?
	if [ -s "$_pl_d/out" ]; then
		{ IFS= read -r BENCH_POLL_LINE < "$_pl_d/out"; } 2> /dev/null || :
	fi
	rm -rf "${_pl_d:?}"
	BENCH_POLL_DIR=
	return "$_pl_st"
}

# bench_poll_to <file> <command...>: for bench_poll's job: the command,
# its standard output to <file>.
bench_poll_to() {
	_pt_f=$1
	shift
	"$@" > "$_pt_f"
}

# bench_poll_end: a trap's first step, from bench_deferred (or bench.sh's
# bench_on_signal) once the signal is not kept for bench_undefer: stops the
# poll bench_poll was waiting for when the signal came, with TERM, and
# collects it, so the trap's exit leaves no poll running, and removes
# bench_poll_line's directory when one is there. The poll is a
# child of this shell that the wait has not collected (a ps, a date, a
# sleep of one poll interval at most): its pid is given to no other
# process while it is uncollected, and once it has exited and bash has
# reaped it, only after the system has given out its other pids. Its
# standard error is /dev/null (the shell's notice of a job ended by a
# signal names an internal path). A poll that has exited may be reaped
# already, though no wait has collected it: the shell reaps a child when
# it exits (bash) or in its next wait for any child (dash, in the wait the
# signal came in), and keeps its status for the wait. Its pid is then gone,
# and the TERM fails; that failure is not this shell's, and must not end a
# trap that runs under set -e (bench.sh's) before its stop (FIX196).
bench_poll_end() {
	if [ -n "${BENCH_POLL_PID:-}" ]; then
		_pe_p=$BENCH_POLL_PID
		BENCH_POLL_PID=
		kill -s TERM "$_pe_p" || :
		wait "$_pe_p" || :
	fi
	if [ -n "${BENCH_POLL_DIR:-}" ]; then
		rm -rf "${BENCH_POLL_DIR:?}"
		BENCH_POLL_DIR=
	fi
	# bench_bounded's events file, made but no bounded command set yet;
	# with one, its stop removes it (TASK221). Only this run's file, by
	# its name: a sibling subshell's has the same $$ (round 1, N3).
	if [ -z "${BENCH_BOUNDED_PID:-}" ] && [ -n "${BENCH_BOUNDED_EV:-}" ]; then
		rm -f "$BENCH_BOUNDED_EV" "$BENCH_BOUNDED_EV.tap" || :
		BENCH_BOUNDED_EV=
	fi
} 2> /dev/null

# bench_nap: one poll interval, BENCH_LOCK_POLL seconds (a decimal is
# allowed where sleep takes one; 1 second where it does not). The sleep is
# waited for through bench_poll (FIX192), so a signal runs the trap at
# once; a sleep ended by a signal is not slept again. Only the sleep's own
# standard error is /dev/null (bench_sleep_quiet), not the wait's: a trap
# that runs in the wait prints its lines to the caller's (FIX192, round 2).
bench_nap() {
	_bn_st=0
	bench_poll bench_sleep_quiet "${1:-1}" || _bn_st=$?
	if [ "$_bn_st" -ne 0 ] && [ "$_bn_st" -le 128 ]; then bench_poll sleep 1; fi
}

# bench_sleep_quiet <seconds>: for bench_poll's job: becomes sleep, its
# standard error on /dev/null (a sleep that takes no decimal says so).
bench_sleep_quiet() {
	exec sleep "$1" 2> /dev/null
}

BENCH_NOW=

# bench_now: the clock (date +%s) in BENCH_NOW, read through
# bench_poll_line, so an INT in the read runs the trap at once (FIX192,
# round 2); a read that gives no number is made again in a command
# substitution.
bench_now() {
	bench_poll_line date +%s || :
	case $BENCH_POLL_LINE in
	'' | *[!0-9]*) BENCH_NOW=$(date +%s) ;;
	*) BENCH_NOW=$BENCH_POLL_LINE ;;
	esac
}

# bench_past <seconds>: the clock (date +%s) is past <seconds> since the
# epoch. For bench_poll: the date is read in the poll, not in this shell.
bench_past() {
	[ "$(date +%s)" -gt "$1" ]
}

# bench_proc_read <pid> [<field>]: one ps call for <pid>: BENCH_PS_F, its
# <field> (stat by default), and BENCH_PS_START, its start time (ps -o
# lstart=, which exec does not change) in one form: its words one blank
# apart (ps pads it, and puts two blanks before a day of one digit). False
# when ps gives no answer. Every start time the core compares comes from
# here (FIX172), in UTC (TZ=UTC0) whatever the caller's TZ, so a time read
# under one TZ compares with one read under another (FIX184, round 2).
bench_proc_read() {
	_pq_o=$(TZ=UTC0 ps -o "${2:-stat}=" -o lstart= -p "$1" 2> /dev/null) || return 1
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
# What bench_spawn started and the core may still be asked about, one
# entry per command: ";<pid>|<group>|<start>" (bench_spawn_id). The watch
# and the stops read a pid's identity here and nowhere else, so a pid this
# shell did not start is never signalled. An entry leaves when its command
# is collected (bench_spawn_forget), so the table holds the command under
# the lock and the bounded one, and a lookup costs the same in a long run.
BENCH_SPAWNED=

# bench_spawn_id <pid>: records the command bench_spawn has just started as
# <pid> (FIX172): BENCH_SPAWN_START, its start time (bench_proc_read's form;
# "-" when it has gone already, "?" when ps gives no answer for it while it
# runs, after three tries 0.1 s apart), and BENCH_SPAWN_GROUP, <pid> when
# it leads its own process group, "-" when it does not. When ps gives no
# answer, or <pid> has gone already, the group is <pid> while kill -0 --
# -<pid> answers: <pid> was this shell's child an instant ago, so a group
# with its id was made by it, and the system gives no new process the id of
# a process group that still has a member. Both replace any entry for <pid>
# in BENCH_SPAWNED. The ps read waits through bench_poll_line, so an INT
# in it runs the trap at once (FIX192, round 2): in bench_spawn's window,
# the trap notes it, and bench_undefer acts on it.
bench_spawn_id() {
	BENCH_SPAWN_START='?'
	BENCH_SPAWN_GROUP=
	_si_n=0
	while :; do
		if bench_poll_line bench_spawn_read "$1" && [ -n "$BENCH_POLL_LINE" ]; then
			BENCH_SPAWN_START=${BENCH_POLL_LINE#*|}
			if [ "${BENCH_POLL_LINE%%|*}" = "$1" ]; then BENCH_SPAWN_GROUP=$1; else BENCH_SPAWN_GROUP=-; fi
			break
		fi
		if ! bench_pid_alive "$1"; then
			BENCH_SPAWN_START=-
			break
		fi
		_si_n=$((_si_n + 1))
		[ "$_si_n" -lt 3 ] || break
		bench_nap 0.1
	done
	if [ -z "$BENCH_SPAWN_GROUP" ]; then
		if kill -0 -- "-$1" 2> /dev/null; then BENCH_SPAWN_GROUP=$1; else BENCH_SPAWN_GROUP=-; fi # bounded-run: allowed: the primitive: the group a spawn made
	fi
	bench_spawn_forget "$1"
	BENCH_SPAWNED="$BENCH_SPAWNED;$1|$BENCH_SPAWN_GROUP|$BENCH_SPAWN_START"
}

# bench_spawn_read <pid>: for bench_poll_line: bench_proc_read's process
# group and start time for <pid>, as "<pgid>|<start>"; false when ps gives
# no answer.
bench_spawn_read() {
	bench_proc_read "$1" pgid || return 1
	printf '%s|%s\n' "$BENCH_PS_F" "$BENCH_PS_START"
}

# bench_spawn_forget <pid>: takes <pid>'s entry out of BENCH_SPAWNED, once
# its command is collected: the core is asked about it no more, and a later
# stop of <pid> sends nothing.
bench_spawn_forget() {
	case $BENCH_SPAWNED in
	*";$1|"*) ;;
	*) return 0 ;;
	esac
	_sf_h=${BENCH_SPAWNED%%";$1|"*}
	_sf_t=${BENCH_SPAWNED#*";$1|"}
	case $_sf_t in
	*';'*) _sf_t=";${_sf_t#*;}" ;;
	*) _sf_t= ;;
	esac
	BENCH_SPAWNED=$_sf_h$_sf_t
}

# bench_spawned <pid>: the identity bench_spawn recorded for <pid>:
# BENCH_ID_GROUP and BENCH_ID_START. False for a pid it did not start, or
# whose command is collected.
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
# "group" (it is reaped), nothing. The identity is read in bench_poll's
# job (bench_watch_read), so an INT in the read runs the trap at once
# (FIX192, round 2); a read ended by a signal is "cannot tell".
bench_group_alive() {
	bench_spawned "$1" || return 1
	_ga_r=0
	_ga_z=
	bench_poll bench_watch_read "$1" "$BENCH_ID_START" || _ga_r=$?
	case $_ga_r in
	10) _ga_r=0 _ga_z=1 ;;
	11) _ga_r=0 ;;
	0 | 1 | 2 | 3) ;;
	*) _ga_r=2 ;;
	esac
	if [ "$BENCH_ID_GROUP" = "$1" ]; then
		[ "$_ga_r" -ne 3 ] || return 1
		kill -0 -- "-$1" 2> /dev/null # bounded-run: allowed: the primitive: a group still has a member
		return
	fi
	[ "${2:-}" != group ] || return 1
	case $_ga_r in
	0)
		[ -z "$_ga_z" ] || return 1
		return 0
		;;
	2) return 0 ;;
	esac
	return 1
}

# bench_signal_group <signal> <pid> [group]: the signal to the command
# bench_spawn started as <pid> (FIX172). When it led its own process group
# at spawn: to that group only, never to the pid alone, while the group
# has a member, unless another process has <pid> (another start time): the
# system gives no new process the id of a process group that still has a
# member, so while no other process is seen to have <pid>, that group is
# the command's, also while ps gives no answer (FIX172, round 1). When it
# led none (a shell without job control): to <pid> alone, only while it is
# still that command, and never with "group" (it is reaped). Returns 0 once
# sent; 1 when nothing of the command is left to signal (it is gone,
# another process has <pid>, or bench_spawn did not start <pid>); 2 when
# this shell cannot tell (bench_proc_same) whether the pid of a command
# that led no group is still that command, and then nothing is sent. The
# identity is read as bench_group_alive reads it (FIX192, round 2).
bench_signal_group() {
	bench_spawned "$2" || return 1
	_sg2_r=0
	bench_poll bench_watch_read "$2" "$BENCH_ID_START" || _sg2_r=$?
	case $_sg2_r in
	10 | 11) _sg2_r=0 ;;
	0 | 1 | 2 | 3) ;;
	*) _sg2_r=2 ;;
	esac
	if [ "$BENCH_ID_GROUP" = "$2" ]; then
		[ "$_sg2_r" -ne 3 ] || return 1
		kill -s "$1" -- "-$2" 2> /dev/null || return 1 # bounded-run: allowed: the primitive: bench_signal_group, to the group
		return 0
	fi
	[ "${3:-}" != group ] || return 1
	case $_sg2_r in
	0)
		kill -s "$1" "$2" 2> /dev/null || return 1 # bounded-run: allowed: the primitive: bench_signal_group, to the pid
		return 0
		;;
	2) return 2 ;;
	esac
	return 1
}

# bench_untold <pid>: says that a stop of <pid> sends no more signals because
# this shell cannot tell whether <pid> is still the command it started.
bench_untold() {
	bench_note "cannot tell whether pid $1 is still the command this shell started (ps gives no answer for it, or none was read at its start), so the stop sends it no more signals"
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
# return. Returns 0 when the command is stopped or nothing of it was left,
# 2 when it could not tell and so may have left the command running (only
# a command that led no group: bench_signal_group). Its standard error is
# /dev/null (the shell's notice of a job killed by a signal names an
# internal path), all but that line. It is bench_stop_groups for one pid.
bench_stop_group() {
	# the bounded command runs under a supervisor (TASK221): it stops it
	if [ -n "${BENCH_BOUNDED_PID:-}" ] && [ "$1" = "$BENCH_BOUNDED_PID" ]; then bench_bounded_stop && return 0; fi
	bench_stop_groups "${2:-${BENCH_STOP_GRACE:-10}}" "${3:-}" "$1"
}

# bench_stop_groups <grace> <how> <pid>...: bench_stop_group (above) for
# every <pid> at once, and the one TERM, grace and KILL code of the core
# (FIX186): TERM and CONT to each, one grace of <grace> seconds for all of
# them, KILL to each one that is left, up to 2 s for all to go, then the
# reap of each (none with <how> "group"; otherwise <how> is empty). So each
# gets its TERM once, and the last its KILL as soon as the first. A pid that
# is empty, 0, 1 or not a number is skipped. Returns 2 when this shell could
# not tell for one of them (that one is left as bench_stop_group leaves
# it), else 0. The gate's scheduler (scripts/gate-checks.sh) stops its
# checks' process groups through it, each recorded at its start with
# bench_spawn_id.
bench_stop_groups() {
	_sg_g=$1 _sg_o=$2
	shift 2
	_sg_l= _sg_rv=0
	for _sg_p; do
		case $_sg_p in
		'' | 0 | 1 | *[!0-9]*) continue ;;
		esac
		_sg_r=0
		bench_signal_group TERM "$_sg_p" "$_sg_o" || _sg_r=$?
		case $_sg_r in
		0)
			bench_signal_group CONT "$_sg_p" "$_sg_o" || true
			_sg_l="$_sg_l $_sg_p"
			;;
		2)
			bench_untold "$_sg_p" 2>&8
			_sg_rv=2
			;;
		esac
	done
	[ -n "$_sg_l" ] || return "$_sg_rv"
	bench_now
	_sg_end=$((BENCH_NOW + _sg_g))
	# shellcheck disable=SC2086 # a list of pids
	while bench_groups_alive "$_sg_o" $_sg_l && ! bench_poll bench_past "$_sg_end"; do
		bench_nap 0.1
	done
	_sg_k=
	for _sg_p in $_sg_l; do
		if bench_group_alive "$_sg_p" "$_sg_o"; then
			_sg_r=0
			bench_signal_group KILL "$_sg_p" "$_sg_o" || _sg_r=$? # bounded-run: allowed: the primitive: bench_stop_groups, the KILL after the grace
			if [ "$_sg_r" -eq 2 ]; then
				bench_untold "$_sg_p" 2>&8
				_sg_rv=2
				continue
			fi
		fi
		_sg_k="$_sg_k $_sg_p"
	done
	_sg_n=0
	# shellcheck disable=SC2086
	while bench_groups_alive "$_sg_o" $_sg_k && [ "$_sg_n" -lt 20 ]; do
		bench_nap 0.1
		_sg_n=$((_sg_n + 1))
	done
	if [ "$_sg_o" != group ]; then
		for _sg_p in $_sg_k; do wait "$_sg_p" || true; done
	fi
	return "$_sg_rv"
} 8>&2 2> /dev/null

# bench_groups_alive <how> <pid>...: bench_group_alive, with <how>, for at
# least one of them
bench_groups_alive() {
	_gs_o=$1
	shift
	for _gs_p; do
		if bench_group_alive "$_gs_p" "$_gs_o"; then return 0; fi
	done
	return 1
}

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
# reports a stop only of the command itself. With <until>, the name of a
# function, it calls that function at every poll while the command runs,
# and returns 3 once it is true (FIX122); one that names no function or
# command, before the first poll or at any later one, is refused: a line,
# and status 4 (2 is a stopped command). Its standard error is
# /dev/null (the shell's job notices), all but that line.
# Every step of a poll of its own, the ps read, the clock and the nap,
# runs through bench_poll (FIX192): it waits for no foreground command or
# command substitution of its own, so a signal runs the trap at once,
# whatever the poll is doing. The until test is the caller's, and runs in
# this shell as before (what it sets is kept, and it may unset itself): a
# foreground command in it keeps bash 3.2's window. A poll ended by a
# signal (status above 128) is read as no answer, and the next poll asks
# again.
bench_watch() {
	_bw_p=$1
	_bw_end=$2
	_bw_cap=$3
	_bw_until=${4:-}
	if [ -n "$_bw_until" ] && ! command -v "$_bw_until" > /dev/null 2>&1; then
		bench_note "bench_watch: the until test $_bw_until is not defined" 2>&8
		return 4
	fi
	_bw_n=0
	_bw_told=
	bench_spawned "$_bw_p" || return 0
	_bw_s=$BENCH_ID_START
	while :; do
		_bw_r=0
		bench_poll bench_watch_read "$_bw_p" "$_bw_s" || _bw_r=$?
		case $_bw_r in
		0) ;;
		10) return 0 ;;
		11) return 2 ;;
		2)
			if [ -z "$_bw_told" ]; then
				_bw_told=1
				if [ "$BENCH_ID_GROUP" = "$_bw_p" ]; then
					bench_note "cannot tell whether pid $_bw_p is still the command this shell started (ps gives no answer for it, or none was read at its start); a stop goes to its process group while that has a member" 2>&8
				else
					bench_note "cannot tell whether pid $_bw_p is still the command this shell started (ps gives no answer for it, or none was read at its start); it is not signalled while that lasts" 2>&8
				fi
			fi
			;;
		1 | 3) return 0 ;;
		esac
		if [ -n "$_bw_until" ]; then
			# a test gone since the last poll is refused, not read as false
			command -v "$_bw_until" > /dev/null 2>&1 || {
				bench_note "bench_watch: the until test $_bw_until is not defined" 2>&8
				return 4
			}
			if "$_bw_until"; then return 3; fi
		fi
		if [ -n "$_bw_end" ] && bench_poll bench_past "$_bw_end"; then
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

# bench_watch_read <pid> <start>: one poll of bench_watch, in bench_poll's
# background job: bench_proc_same's status (1, 2 or 3), or for <pid> still
# the command (0), 10 when it is a zombie (it has exited), 11 when it is
# stopped (state T), else 0. The state goes back as the status, since the
# job's variables do not.
bench_watch_read() {
	_wr_r=0
	bench_proc_same "$1" "$2" || _wr_r=$?
	[ "$_wr_r" -eq 0 ] || return "$_wr_r"
	case $BENCH_PS_F in
	Z*) return 10 ;;
	T*) return 11 ;;
	esac
	return 0
}

# bench_end_group <pid> <grace>: after the leader <pid> exited and was
# reaped, gives what it left running in its process group 1 s (by the
# clock, so 1 to 2 s) to end, then stops it (bench_stop_group with the
# caller's <grace>, the group only). Returns 1 when it had to stop
# something. Nothing to do without job control: the command then leads no
# group. The group is the one it led at spawn (bench_group_alive), so a
# group another process leads under the same id once the command's group
# is gone is left alone (FIX172). Its reads, the group's and the clock's,
# wait through bench_poll (FIX192, round 2): it runs after the command
# has exited, with the caller's trap in place.
bench_end_group() {
	bench_group_alive "$1" group || return 0
	bench_now
	_eg_end=$((BENCH_NOW + 1))
	while bench_group_alive "$1" group; do
		if bench_poll bench_past "$_eg_end"; then
			bench_stop_group "$1" "$2" group
			return 1
		fi
		bench_nap 0.1
	done
	return 0
}

BENCH_BOUNDED_PID=
BENCH_BOUNDED_REAPED=
BENCH_BOUNDED_HOW=
BENCH_BOUNDED_QUIT=
BENCH_BOUNDED_TAG=
BENCH_BOUNDED_UNTIL=
BENCH_BOUNDED_EV=
BENCH_BOUNDED_TEE=
BENCH_SV=

# The process supervisor's library (scripts/lib, TASK221): beside this
# file's directory (bash names a sourced file), else beside BENCH_ROOT,
# else BENCH_SV_LIB, for a test that runs a copy of scripts/bench alone.
BENCH_SV_DIR=
if [ -n "${BASH_SOURCE:-}" ]; then BENCH_SV_DIR=$(cd "$(dirname "$BASH_SOURCE")/../../lib" 2> /dev/null && pwd -P) || BENCH_SV_DIR=; fi
if [ ! -r "$BENCH_SV_DIR/supervise.sh" ] && [ -n "${BENCH_ROOT:-}" ]; then BENCH_SV_DIR=$(cd "$BENCH_ROOT/../lib" 2> /dev/null && pwd -P) || BENCH_SV_DIR=; fi
if [ ! -r "$BENCH_SV_DIR/supervise.sh" ]; then BENCH_SV_DIR=${BENCH_SV_LIB:-}; fi

# bench_sv: sources the supervisor's library and builds the supervisor
# (sv_path), once per shell: BENCH_SV is its path. Exits 2 when it cannot.
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
# recorded in the lock state (bench_record_untold), so the lock it runs
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
# returns and by bench_poll_end on a trap's way out); on a timeout they are
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
	_bb_s=$1
	shift
	bench_is_uint "$_bb_s" || bench_die 2 "a wall time is whole seconds"
	_bb_q=${BENCH_BOUNDED_QUIT:-}
	if [ -n "$_bb_q" ]; then
		bench_is_uint "$_bb_q" || bench_die 2 "a quit time is whole seconds"
		[ "$_bb_q" -lt "$_bb_s" ] || _bb_q=
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
	bench_bounded_ev || bench_die 2 "cannot make a temporary file"
	# from here a caller's trap calls bench_stop_group, which finds the
	# supervisor also in sv_start's fork window (sv_pid), and its status
	# once sv_wait has collected it (SV_STATUS, empty until then)
	SV_STATUS=
	BENCH_BOUNDED_PID=starting
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
	_bb_w=$_bb_s
	[ "$_bb_w" -ne 0 ] || _bb_w=0.001
	set -- --timeout "$_bb_w" --grace "${BENCH_STOP_GRACE:-10}" --events 9 -- "$@"
	if [ -z "$_bb_o" ]; then
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
		bench_note "a command outlived its wall time of $_bb_s s, or what it left running did, and it could not be stopped: it may still run"
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
# kept while the group has a member (bench_record_untold <pgid> group).
# When the supervisor named no group (a failed events write, or its
# "cannot watch the command"), nothing tells when the command is gone: the
# lock is kept until the record is removed by hand (bench_record_untold
# <id> hand, under the supervisor's pid, else this shell's; security I1,
# review round 2).
bench_bounded_untold() {
	_bu_g=$(bench_bounded_event spawned pgid) || _bu_g=
	case $_bu_g in
	'' | 0 | 1 | *[!0-9]*)
		bench_note "the supervisor named no process group to watch"
		_bu_g=${BENCH_BOUNDED_PID:-}
		case $_bu_g in '' | 0 | 1 | *[!0-9]*) _bu_g=$$ ;; esac
		bench_record_untold "$_bu_g" hand
		;;
	*) bench_record_untold "$_bu_g" group ;;
	esac
}

# bench_bounded_ev: makes the events file, BENCH_BOUNDED_EV: a name mktemp
# gives (-u), set before the file is made (noclobber, mode 0600), so a
# trap that runs at any point finds its own file by that name
# (bench_poll_end), and only that one (correctness N3, round 1)
bench_bounded_ev() {
	_be_n=0
	while [ "$_be_n" -lt 5 ]; do
		BENCH_BOUNDED_EV=$(mktemp -u "${TMPDIR:-/tmp}/bench-sv.$$.XXXXXX") || BENCH_BOUNDED_EV=
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
# path twice opens it once for both. For bench_bounded and bench_spawn, so
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

# bench_spawn_running <pid>: the command bench_spawn started as <pid> (not
# yet collected) is itself still running: <pid> is still that command and
# not a zombie, or this shell cannot tell (bench_proc_same returns 2), as
# kill -0 would then answer. For a caller that watches a command it spawned
# while it does other work.
bench_spawn_running() {
	bench_spawned "$1" || return 1
	_sr_r=0
	bench_proc_same "$1" "$BENCH_ID_START" || _sr_r=$?
	case $_sr_r in
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

BENCH_AWAIT_STATUS=

# bench_await <pid> <seconds>: waits up to <seconds> (by the clock) for the
# command bench_spawn started as <pid> to exit (bench_watch). 0 once it has:
# it is reaped (its status in BENCH_AWAIT_STATUS), what it left running in
# its process group is stopped (bench_end_group, with BENCH_STOP_GRACE), and
# it is collected. 1 while it still runs, 2 when it is stopped (a stop
# signal); it is then neither reaped nor collected, and the caller stops it
# (bench_stop_group) or waits again.
bench_await() {
	_ba_w=0
	bench_watch "$1" "$(($(date +%s) + $2))" 0.5 || _ba_w=$?
	[ "$_ba_w" -eq 0 ] || return "$_ba_w"
	BENCH_AWAIT_STATUS=0
	wait "$1" 2> /dev/null || BENCH_AWAIT_STATUS=$?
	bench_end_group "$1" "${BENCH_STOP_GRACE:-10}" || bench_note "stopped what the command left running in its process group"
	bench_spawn_forget "$1"
	return 0
}

# bench_deferred <status>: the first call of the signal trap of a caller of
# bench_spawn or bench_bounded outside bench.sh (FIX122). While bench_spawn
# starts a command it keeps the signal for bench_undefer, which calls the
# caller's bench_on_signal <status> with it once the pid is stored, and is
# true: the trap then returns. Otherwise it stops the poll the signal came
# in (bench_poll_end), puts standard error back to the caller's when the
# signal came inside bench_watch or bench_stop_group (fd 8, as bench.sh
# does), and is false: the trap goes on to stop what it started
# (bench_stop_group) and exit. The caller closes an inherited fd 8
# once, before its first spawn (exec 8>&-).
bench_deferred() {
	if [ -n "${BENCH_DEFER:-}" ]; then
		BENCH_DEFERRED=$1
		return 0
	fi
	bench_poll_end
	if (: >&8) 2> /dev/null; then exec 2>&8; fi
	return 1
}

# bench_record_untold <pid>: records a command the stop of <pid> could not
# tell about in the lock state (bench_lock_record, in core/lock.sh), when
# this shell has the lock's code; a caller of bench_bounded without it runs
# under no lock of this core's.
bench_record_untold() {
	if command -v bench_lock_record > /dev/null 2>&1; then bench_lock_record "$1" ${2:+"$2"}; fi
	return 0
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
