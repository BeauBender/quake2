# scripts/bench/core/lock.sh: the host lock, one heavy job at a time.
#
# The lock is the directory <state>/lock, made with mkdir (atomic: of two
# callers, exactly one makes it). Its holder file, <state>/lock/holder,
# carries
#   label=<label> pid=<lock shell> child=<command> start=<UTC> token=<token>
# one per line. The maker writes it to a temporary name and links it into
# place (ln, which fails when a holder file is there already, so it never
# replaces another maker's), then checks that the lock directory is still
# the one it made (its inode, read right after the mkdir) and still carries
# its token; otherwise it takes its holder back out and waits like any
# caller. So a maker that stalls between the mkdir and the write for longer
# than the grace below, while a waiter reclaims the directory and a third
# caller makes it again, backs off instead of holding the lock with it. The
# holder then rewrites the file with the command's pid by a rename. The
# state directory is $BENCH_STATE, else $XDG_CACHE_HOME/bench-state, else
# $HOME/.cache/bench-state: per user, the same for every repository and
# worktree, and refused inside a git work tree.
#
# Waiting: a caller that finds the lock held polls every BENCH_LOCK_POLL
# seconds (default 2; at least 0.01, and a poll longer than the whole wait
# is cut to it), in the foreground, for at most BENCH_LOCK_WAIT seconds
# (default 3600), then exits 75 naming the holder's label.
#
# Re-entry: the holder exports BENCH_LOCK_TOKEN to its command. A nested
# call passes straight through only when that variable equals the token in
# the holder file; any other value (empty, stale or forged) waits like any
# caller.
#
# Reclaim: a lock whose holder pid and child pid are both gone (kill -0
# fails) is reclaimed and the reclaim is logged, on standard error and in
# <state>/lock.log. Two waiters that see the same dead holder race for the
# guard directory <state>/reclaim.<token> (mkdir again): the winner reads
# the holder again under the guard and removes the lock only if it still
# carries that token and its pids are still gone; then every waiter goes
# back to mkdir, so exactly one gets the lock. A lock directory that has no
# holder file for BENCH_LOCK_GRACE seconds (default 10; its maker died
# between mkdir and the write) is reclaimed the same way, by rmdir, which
# fails if a slow maker wrote its holder meanwhile. A caller killed with
# SIGKILL in the middle of a reclaim leaves its guard behind and blocks that
# one reclaim; remove the reclaim.* directory by hand.
#
# The command: lock run starts it in its own process group (bench_spawn),
# so it keeps the signal dispositions of the caller (SIGINT and SIGQUIT are
# not ignored, and its own traps run) and the lock reaches everything it
# starts. Its standard input is the caller's, or /dev/null when that is a
# terminal: a command in a background process group cannot read the
# terminal. A terminal's Ctrl-C reaches the lock shell, not the command.
#
# Release: the EXIT, INT, TERM and HUP traps of bench.sh call
# bench_lock_release, which removes the lock only while the holder file
# still carries this process's token. On INT, TERM or HUP the lock shell
# first stops the command's whole process group (bench_stop_group: TERM,
# then KILL after twice BENCH_STOP_GRACE, so that a command stopping a
# group of its own with that grace, as bench_bounded does, finishes first),
# so nothing the command started outlives the lock.

BENCH_STATE_DIR=
BENCH_LOCK_DIR=
BENCH_LOCK_LOG=
BENCH_LOCK_OWNED=
BENCH_LOCK_MINE=
BENCH_LOCK_SINCE=
BENCH_LOCK_CHILD=
BENCH_LOCK_REAPED=
BENCH_LOCK_GUARD=

# bench_lock_init: resolves and makes the state directory.
bench_lock_init() {
	if [ -n "${BENCH_STATE:-}" ]; then
		BENCH_STATE_DIR=$BENCH_STATE
	else
		BENCH_STATE_DIR=$(bench_cache_home) || bench_die 2 "set BENCH_STATE or HOME: no per-user directory for the lock"
		BENCH_STATE_DIR=${BENCH_STATE_DIR:?}/bench-state
	fi
	bench_outside_repo "$BENCH_STATE_DIR" "the lock state directory (BENCH_STATE)"
	mkdir -p "${BENCH_STATE_DIR:?}" || bench_die 2 "cannot make the lock state directory"
	BENCH_LOCK_DIR=${BENCH_STATE_DIR:?}/lock
	BENCH_LOCK_LOG=${BENCH_STATE_DIR:?}/lock.log
}

# bench_lock_holder: the holder file's text, empty when there is none.
bench_lock_holder() {
	cat "${BENCH_LOCK_DIR:?}/holder" 2> /dev/null || true
}

# bench_lock_mine: this process runs under the lock already, through the
# token its holder exported.
bench_lock_mine() {
	[ -n "${BENCH_LOCK_TOKEN:-}" ] || return 1
	[ "$(bench_kv "$(bench_lock_holder)" token)" = "$BENCH_LOCK_TOKEN" ]
}

# bench_lock_write <label> <child> [new]: writes the holder file, whole, to
# a temporary name, then into place: with "new" by a hard link, which fails
# when there is a holder file already; else by a rename, which replaces
# this process's own.
bench_lock_write() {
	_lw_t=${BENCH_LOCK_DIR:?}/holder.tmp.$$
	printf 'label=%s\npid=%s\nchild=%s\nstart=%s\ntoken=%s\n' \
		"$1" "$$" "$2" "$BENCH_LOCK_SINCE" "$BENCH_LOCK_MINE" > "$_lw_t" 2> /dev/null || return 1
	if [ "${3:-}" = new ]; then
		_lw_st=0
		ln "$_lw_t" "${BENCH_LOCK_DIR:?}/holder" 2> /dev/null || _lw_st=1
		rm -f "$_lw_t"
		return "$_lw_st"
	fi
	mv -f "$_lw_t" "${BENCH_LOCK_DIR:?}/holder" 2> /dev/null
}

# bench_dir_id <dir>: the inode of <dir>, empty when it is gone.
bench_dir_id() {
	ls -di "$1" 2> /dev/null | awk '{ print $1 }'
}

# bench_lock_reclaim <token>: removes the lock left by the dead holder that
# carried <token> (empty: a lock with no holder file). Returns 0 when it
# removed the lock.
bench_lock_reclaim() {
	case $1 in
	*[!A-Za-z0-9-]*) return 1 ;;
	esac
	BENCH_LOCK_GUARD=${BENCH_STATE_DIR:?}/reclaim.${1:-noholder}
	if ! mkdir "${BENCH_LOCK_GUARD:?}" 2> /dev/null; then
		BENCH_LOCK_GUARD=
		return 1
	fi
	_lr_st=1
	_lr_h=$(bench_lock_holder)
	_lr_tok=$(bench_kv "$_lr_h" token)
	if [ -n "$1" ]; then
		if [ "$_lr_tok" = "$1" ] && ! bench_lock_alive "$_lr_h"; then
			rm -f "${BENCH_LOCK_DIR:?}/holder" "${BENCH_LOCK_DIR:?}"/holder.tmp.*
			if rmdir "${BENCH_LOCK_DIR:?}" 2> /dev/null; then _lr_st=0; fi
		fi
	elif [ -d "${BENCH_LOCK_DIR:?}" ] && [ ! -e "${BENCH_LOCK_DIR:?}/holder" ]; then
		rm -f "${BENCH_LOCK_DIR:?}"/holder.tmp.*
		if rmdir "${BENCH_LOCK_DIR:?}" 2> /dev/null; then _lr_st=0; fi
	fi
	rmdir "${BENCH_LOCK_GUARD:?}" 2> /dev/null || true
	BENCH_LOCK_GUARD=
	if [ "$_lr_st" -eq 0 ]; then
		_lr_l=$(bench_kv "$_lr_h" label)
		_lr_p=$(bench_kv "$_lr_h" pid)
		_lr_line="reclaimed a stale lock: label=${_lr_l:-none} pid=${_lr_p:-none} (gone) by pid=$$"
		bench_note "$_lr_line"
		printf '%s %s\n' "$(bench_utc)" "$_lr_line" >> "${BENCH_LOCK_LOG:?}" 2> /dev/null || true
	fi
	return "$_lr_st"
}

# bench_lock_acquire <label>: takes the lock, waiting as described above.
# Returns 0 with the lock held (BENCH_LOCK_OWNED=1), 75 when the wait ran
# out, 2 on a bad setting.
bench_lock_acquire() {
	_la_label=$1
	_la_wait=${BENCH_LOCK_WAIT:-3600}
	_la_poll=${BENCH_LOCK_POLL:-2}
	_la_grace=${BENCH_LOCK_GRACE:-10}
	bench_is_uint "$_la_wait" || bench_die 2 "BENCH_LOCK_WAIT is whole seconds"
	bench_is_uint "$_la_grace" || bench_die 2 "BENCH_LOCK_GRACE is whole seconds"
	if ! bench_is_num "$_la_poll" || ! awk -v p="$_la_poll" 'BEGIN { exit !(p >= 0.01) }'; then
		bench_die 2 "BENCH_LOCK_POLL is a number of seconds, at least 0.01"
	fi
	if [ "$_la_wait" -gt 0 ] && awk -v p="$_la_poll" -v w="$_la_wait" 'BEGIN { exit !(p > w) }'; then
		_la_poll=$_la_wait
	fi
	_la_t0=$(date +%s)
	_la_rand=$(od -An -N6 -tx1 /dev/urandom 2> /dev/null | tr -dc '0-9a-f') || _la_rand=
	BENCH_LOCK_MINE=t-$$-$_la_t0-${_la_rand:-0}
	_la_said=
	_la_bare=
	while :; do
		if mkdir "${BENCH_LOCK_DIR:?}" 2> /dev/null; then
			_la_id=$(bench_dir_id "$BENCH_LOCK_DIR")
			BENCH_LOCK_OWNED=1
			BENCH_LOCK_SINCE=$(bench_utc)
			if bench_lock_write "$_la_label" "" new; then
				if [ -n "$_la_id" ] && [ "$(bench_dir_id "$BENCH_LOCK_DIR")" = "$_la_id" ] &&
					[ "$(bench_kv "$(bench_lock_holder)" token)" = "$BENCH_LOCK_MINE" ]; then
					return 0
				fi
				# The holder landed in a directory this process did not make
				# (a waiter reclaimed this one during a stall, and another
				# caller made it again): take it back out.
				if [ "$(bench_kv "$(bench_lock_holder)" token)" = "$BENCH_LOCK_MINE" ]; then
					rm -f "${BENCH_LOCK_DIR:?}/holder"
				fi
			fi
			# A reclaimer took the directory before the holder file landed.
			BENCH_LOCK_OWNED=
		fi
		_la_h=$(bench_lock_holder)
		_la_tok=$(bench_kv "$_la_h" token)
		if [ -n "$_la_tok" ]; then
			_la_bare=
			if ! bench_lock_alive "$_la_h"; then
				if bench_lock_reclaim "$_la_tok"; then continue; fi
			fi
		elif [ -d "${BENCH_LOCK_DIR:?}" ]; then
			_la_now=$(date +%s)
			if [ -z "$_la_bare" ]; then
				_la_bare=$_la_now
			elif [ $((_la_now - _la_bare)) -ge "$_la_grace" ]; then
				_la_bare=
				if bench_lock_reclaim ""; then continue; fi
			fi
		else
			# Released meanwhile: try again at once, unless mkdir itself keeps
			# failing (the state directory is not writable).
			_la_miss=$((${_la_miss:-0} + 1))
			[ "$_la_miss" -ge 50 ] && bench_die 2 "cannot make the lock directory in the state directory"
			continue
		fi
		_la_miss=0
		if [ -n "$_la_h" ]; then
			_la_who="$(bench_kv "$_la_h" label) (pid $(bench_kv "$_la_h" pid), since $(bench_kv "$_la_h" start))"
		else
			_la_who="a holder that has not written its holder file yet"
		fi
		if [ $(($(date +%s) - _la_t0)) -ge "$_la_wait" ]; then
			bench_note "the lock is still held by $_la_who after $_la_wait s; giving up"
			return 75
		fi
		if [ -z "$_la_said" ]; then
			bench_note "waiting for the lock held by $_la_who; at most $_la_wait s"
			_la_said=1
		fi
		bench_nap "$_la_poll"
	done
}

# bench_lock_release: removes the lock if this process holds it.
bench_lock_release() {
	[ "${BENCH_LOCK_OWNED:-}" = 1 ] || return 0
	BENCH_LOCK_OWNED=
	if [ "$(bench_kv "$(bench_lock_holder)" token)" = "$BENCH_LOCK_MINE" ]; then
		rm -f "${BENCH_LOCK_DIR:?}/holder" "${BENCH_LOCK_DIR:?}/holder.tmp.$$"
		rmdir "${BENCH_LOCK_DIR:?}" 2> /dev/null || true
	fi
	return 0
}

# bench_lock_stop_child: stops the command this process runs under the lock
# and its whole process group: TERM, up to twice BENCH_STOP_GRACE and at
# least BENCH_STOP_GRACE + 5 s for the group to go, then KILL; then reaps
# it (once it is reaped, BENCH_LOCK_REAPED, the group only). The floor
# lets a nested bench.sh stop its own bounded group, which takes up to its
# grace, a second of clock and 2 s after its KILL, before this KILL ends it.
bench_lock_stop_child() {
	[ -n "${BENCH_LOCK_CHILD:-}" ] || return 0
	_sc_g=${BENCH_STOP_GRACE:-10}
	_sc_l=$((2 * _sc_g))
	[ "$_sc_l" -ge $((_sc_g + 5)) ] || _sc_l=$((_sc_g + 5))
	bench_stop_group "$BENCH_LOCK_CHILD" "$_sc_l" "${BENCH_LOCK_REAPED:-}"
	BENCH_LOCK_CHILD=
}

# bench_lock_alive <holder text>: the holder's lock shell or its command is
# alive, or the command's process group still has a member (the lock
# shell was killed and the command exited, but what it started runs on).
bench_lock_alive() {
	_lv_c=$(bench_kv "$1" child)
	bench_pid_alive "$(bench_kv "$1" pid)" && return 0
	bench_pid_alive "$_lv_c" && return 0
	case $_lv_c in
	'' | 0 | 1 | *[!0-9]*) return 1 ;;
	esac
	kill -0 -- "-$_lv_c" 2> /dev/null
}

# bench_lock_run <label> -- <command...>: runs the command under the lock
# and exits with its status (75 when the wait ran out).
bench_lock_run() {
	if [ "$#" -lt 3 ] || [ "$2" != -- ]; then
		bench_die 2 "usage: bench.sh lock run <label> -- <command> [args...]"
	fi
	_ru_label=$1
	shift 2
	bench_is_label "$_ru_label" || bench_die 2 "a lock label is 1 to 64 of A-Z a-z 0-9 . _ : @ + -"
	bench_lock_init
	if bench_lock_mine; then
		exec "$@"
	fi
	_ru_st=0
	bench_lock_acquire "$_ru_label" || _ru_st=$?
	[ "$_ru_st" -eq 0 ] || exit "$_ru_st"
	BENCH_LOCK_TOKEN=$BENCH_LOCK_MINE
	export BENCH_LOCK_TOKEN
	# In the background, in its own process group, so a signal reaches the
	# trap at once (wait returns) and the trap reaches the whole command.
	if [ -t 0 ]; then
		bench_spawn "$@" < /dev/null
	else
		bench_spawn "$@"
	fi
	BENCH_LOCK_CHILD=$BENCH_SPAWN_PID
	BENCH_LOCK_REAPED=
	bench_undefer
	bench_lock_write "$_ru_label" "$BENCH_LOCK_CHILD" || true
	# Polled, not a plain wait: with job control off, wait never returns
	# for a stopped child, and lock run has no wall time.
	_ru_w=0
	bench_watch "$BENCH_LOCK_CHILD" "" 1 || _ru_w=$?
	if [ "$_ru_w" -eq 2 ]; then
		bench_note "the command stopped (a stop signal, or it used the terminal, which lock run gives it only in the background); stopping it and freeing the lock"
		bench_lock_stop_child
		exit 74
	fi
	wait "$BENCH_LOCK_CHILD" 2> /dev/null || _ru_st=$?
	BENCH_LOCK_REAPED=group
	bench_end_group "$BENCH_LOCK_CHILD" || bench_note "stopped what the command left running in its process group"
	BENCH_LOCK_CHILD=
	exit "$_ru_st"
}

# bench_lock_status: prints "lock=free", or "lock=held" with the holder.
bench_lock_status() {
	bench_lock_init
	if [ ! -d "${BENCH_LOCK_DIR:?}" ]; then
		echo lock=free
		return 0
	fi
	_ls_h=$(bench_lock_holder)
	if [ -z "$_ls_h" ]; then
		echo "lock=held label=none holder=unwritten"
		return 0
	fi
	_ls_p=$(bench_kv "$_ls_h" pid)
	_ls_c=$(bench_kv "$_ls_h" child)
	_ls_a=no
	if bench_lock_alive "$_ls_h"; then _ls_a=yes; fi
	printf 'lock=held label=%s pid=%s child=%s alive=%s since=%s\n' \
		"$(bench_kv "$_ls_h" label)" "$_ls_p" "${_ls_c:-none}" "$_ls_a" "$(bench_kv "$_ls_h" start)"
}
