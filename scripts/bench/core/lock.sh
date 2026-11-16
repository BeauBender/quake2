# scripts/bench/core/lock.sh: the host lock, one heavy job at a time.
#
# The lock is the directory <state>/lock, made with mkdir (atomic: of two
# callers, exactly one makes it). Its holder file, <state>/lock/holder,
# written to a temporary name and renamed into place, carries
#   label=<label> pid=<lock shell> child=<command> start=<UTC> token=<token>
# one per line. The state directory is $BENCH_STATE, else
# $XDG_CACHE_HOME/bench-state, else $HOME/.cache/bench-state: per user, the
# same for every repository and worktree, and refused inside a git work tree.
#
# Waiting: a caller that finds the lock held polls every BENCH_LOCK_POLL
# seconds (default 2), in the foreground, for at most BENCH_LOCK_WAIT seconds
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
# Release: the EXIT, INT, TERM and HUP traps of bench.sh call
# bench_lock_release, which removes the lock only while the holder file
# still carries this process's token.

BENCH_STATE_DIR=
BENCH_LOCK_DIR=
BENCH_LOCK_LOG=
BENCH_LOCK_OWNED=
BENCH_LOCK_MINE=
BENCH_LOCK_SINCE=
BENCH_LOCK_CHILD=
BENCH_LOCK_GUARD=

# bench_lock_init: resolves and makes the state directory.
bench_lock_init() {
	if [ -n "${BENCH_STATE:-}" ]; then
		BENCH_STATE_DIR=$BENCH_STATE
	else
		BENCH_STATE_DIR=$(bench_cache_home) || bench_die 2 "set BENCH_STATE or HOME: no per-user directory for the lock"
		BENCH_STATE_DIR=$BENCH_STATE_DIR/bench-state
	fi
	bench_outside_repo "$BENCH_STATE_DIR" "the lock state directory (BENCH_STATE)"
	mkdir -p "$BENCH_STATE_DIR" || bench_die 2 "cannot make the lock state directory"
	BENCH_LOCK_DIR=$BENCH_STATE_DIR/lock
	BENCH_LOCK_LOG=$BENCH_STATE_DIR/lock.log
}

# bench_lock_holder: the holder file's text, empty when there is none.
bench_lock_holder() {
	cat "$BENCH_LOCK_DIR/holder" 2> /dev/null || true
}

# bench_lock_mine: this process runs under the lock already, through the
# token its holder exported.
bench_lock_mine() {
	[ -n "${BENCH_LOCK_TOKEN:-}" ] || return 1
	[ "$(bench_kv "$(bench_lock_holder)" token)" = "$BENCH_LOCK_TOKEN" ]
}

# bench_lock_write <label> <child>: writes the holder file, atomically.
bench_lock_write() {
	_lw_t=$BENCH_LOCK_DIR/holder.tmp.$$
	printf 'label=%s\npid=%s\nchild=%s\nstart=%s\ntoken=%s\n' \
		"$1" "$$" "$2" "$BENCH_LOCK_SINCE" "$BENCH_LOCK_MINE" > "$_lw_t" 2> /dev/null || return 1
	mv -f "$_lw_t" "$BENCH_LOCK_DIR/holder" 2> /dev/null
}

# bench_lock_reclaim <token>: removes the lock left by the dead holder that
# carried <token> (empty: a lock with no holder file). Returns 0 when it
# removed the lock.
bench_lock_reclaim() {
	case $1 in
	*[!A-Za-z0-9-]*) return 1 ;;
	esac
	BENCH_LOCK_GUARD=$BENCH_STATE_DIR/reclaim.${1:-noholder}
	if ! mkdir "$BENCH_LOCK_GUARD" 2> /dev/null; then
		BENCH_LOCK_GUARD=
		return 1
	fi
	_lr_st=1
	_lr_h=$(bench_lock_holder)
	_lr_tok=$(bench_kv "$_lr_h" token)
	if [ -n "$1" ]; then
		if [ "$_lr_tok" = "$1" ] && ! bench_pid_alive "$(bench_kv "$_lr_h" pid)" &&
			! bench_pid_alive "$(bench_kv "$_lr_h" child)"; then
			rm -f "$BENCH_LOCK_DIR/holder" "$BENCH_LOCK_DIR"/holder.tmp.*
			if rmdir "$BENCH_LOCK_DIR" 2> /dev/null; then _lr_st=0; fi
		fi
	elif [ -d "$BENCH_LOCK_DIR" ] && [ ! -e "$BENCH_LOCK_DIR/holder" ]; then
		rm -f "$BENCH_LOCK_DIR"/holder.tmp.*
		if rmdir "$BENCH_LOCK_DIR" 2> /dev/null; then _lr_st=0; fi
	fi
	rmdir "$BENCH_LOCK_GUARD" 2> /dev/null || true
	BENCH_LOCK_GUARD=
	if [ "$_lr_st" -eq 0 ]; then
		_lr_l=$(bench_kv "$_lr_h" label)
		_lr_p=$(bench_kv "$_lr_h" pid)
		_lr_line="reclaimed a stale lock: label=${_lr_l:-none} pid=${_lr_p:-none} (gone) by pid=$$"
		bench_note "$_lr_line"
		printf '%s %s\n' "$(bench_utc)" "$_lr_line" >> "$BENCH_LOCK_LOG" 2> /dev/null || true
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
	if ! bench_is_num "$_la_poll" || [ "$_la_poll" = 0 ]; then
		bench_die 2 "BENCH_LOCK_POLL is a positive number of seconds"
	fi
	_la_t0=$(date +%s)
	_la_rand=$(od -An -N6 -tx1 /dev/urandom 2> /dev/null | tr -dc '0-9a-f') || _la_rand=
	BENCH_LOCK_MINE=t-$$-$_la_t0-${_la_rand:-0}
	_la_said=
	_la_bare=
	while :; do
		if mkdir "$BENCH_LOCK_DIR" 2> /dev/null; then
			BENCH_LOCK_OWNED=1
			BENCH_LOCK_SINCE=$(bench_utc)
			if bench_lock_write "$_la_label" "" &&
				[ "$(bench_kv "$(bench_lock_holder)" token)" = "$BENCH_LOCK_MINE" ]; then
				return 0
			fi
			# A reclaimer took the directory before the holder file landed.
			BENCH_LOCK_OWNED=
		fi
		_la_h=$(bench_lock_holder)
		_la_tok=$(bench_kv "$_la_h" token)
		if [ -n "$_la_tok" ]; then
			_la_bare=
			if ! bench_pid_alive "$(bench_kv "$_la_h" pid)" && ! bench_pid_alive "$(bench_kv "$_la_h" child)"; then
				if bench_lock_reclaim "$_la_tok"; then continue; fi
			fi
		elif [ -d "$BENCH_LOCK_DIR" ]; then
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
		rm -f "$BENCH_LOCK_DIR/holder" "$BENCH_LOCK_DIR/holder.tmp.$$"
		rmdir "$BENCH_LOCK_DIR" 2> /dev/null || true
	fi
	return 0
}

# bench_lock_stop_child: on a signal, stops the command this process runs
# under the lock: TERM, up to 10 s for it to go, then KILL; then reaps it.
bench_lock_stop_child() {
	[ -n "${BENCH_LOCK_CHILD:-}" ] || return 0
	kill -TERM "$BENCH_LOCK_CHILD" 2> /dev/null || true
	_sc_n=0
	while bench_pid_running "$BENCH_LOCK_CHILD" && [ "$_sc_n" -lt 50 ]; do
		bench_nap 0.2
		_sc_n=$((_sc_n + 1))
	done
	if bench_pid_running "$BENCH_LOCK_CHILD"; then
		kill -KILL "$BENCH_LOCK_CHILD" 2> /dev/null || true
	fi
	wait "$BENCH_LOCK_CHILD" 2> /dev/null || true
	BENCH_LOCK_CHILD=
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
	# In the background so a signal reaches the trap at once (wait returns),
	# with this shell's standard input (through fd 3), not the /dev/null an
	# async list gets by default; /dev/null when standard input is closed.
	if { :; } 2> /dev/null 3<&0; then
		{ "$@" <&3 3<&- & } 3<&0
	else
		"$@" < /dev/null &
	fi
	BENCH_LOCK_CHILD=$!
	bench_lock_write "$_ru_label" "$BENCH_LOCK_CHILD" || true
	wait "$BENCH_LOCK_CHILD" || _ru_st=$?
	BENCH_LOCK_CHILD=
	exit "$_ru_st"
}

# bench_lock_status: prints "lock=free", or "lock=held" with the holder.
bench_lock_status() {
	bench_lock_init
	if [ ! -d "$BENCH_LOCK_DIR" ]; then
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
	if bench_pid_alive "$_ls_p" || bench_pid_alive "$_ls_c"; then _ls_a=yes; fi
	printf 'lock=held label=%s pid=%s child=%s alive=%s since=%s\n' \
		"$(bench_kv "$_ls_h" label)" "$_ls_p" "${_ls_c:-none}" "$_ls_a" "$(bench_kv "$_ls_h" start)"
}
