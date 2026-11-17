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
# The queue (FIX106): waiters are served in arrival order. On arrival a
# waiter makes a ticket in <state>/queue with mktemp (tmp.<pid>.XXXXXX),
# writes pid=, label= and since= (UTC) into it, and renames it to
# w.<time>.<pid>.<random>: the time from Time::HiRes, zero-padded to
# 12.6 digits (whole seconds from date when perl fails), the pid to 10, so
# the names sort by arrival. Only the waiter whose ticket sorts first among
# the live ones tries the mkdir, and the reclaims below; every other one
# naps. A ticket (or temporary) whose waiter is gone (bench_pid_running on
# its pid: kill -0 fails, or it is a zombie, KILLed and not yet reaped) is
# removed by whichever waiter sees it, so a waiter killed while queued
# holds up the next one for one poll. A waiter takes
# its ticket out the moment its mkdir succeeds, and the EXIT trap takes it
# out on every other exit (giving up, TERM, INT, HUP); a maker that backs
# off puts it back under its first name, so it keeps its place. A holder
# that releases and asks again so gets a later ticket than every waiter.
#
# Re-entry: the holder exports BENCH_LOCK_TOKEN to its command. A nested
# call passes straight through only when that variable equals the token in
# the holder file; any other value (empty, stale or forged) waits like any
# caller.
#
# Reclaim: a lock whose holder pid and child pid are both gone (kill -0
# fails), and whose command's process group has no member left, is
# reclaimed and the reclaim is logged, on standard error and in
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
# not ignored, and its own traps run) and the lock reaches its process
# group: a descendant that moves itself into another group or session
# (setsid, a shell with job control) is out of its reach. Its standard
# input is the caller's, or /dev/null when that is a terminal. The group
# is in the background: a command that uses the terminal (reads it, sets
# it with stty, or writes to it under stty tostop) is stopped by the
# terminal. The lock shell polls the command (bench_watch) instead of
# waiting on it, so it sees such a stop: it says so, stops the group as on
# a signal, and exits 74 with the lock freed. A terminal's Ctrl-C reaches
# the lock shell, not the command.
#
# Release: the EXIT, INT, TERM and HUP traps of bench.sh call
# bench_lock_release, which removes the lock only while the holder file
# still carries this process's token. On INT, TERM or HUP the lock shell
# first stops the command's whole process group (bench_stop_group: TERM,
# then KILL after bench_lock_grace, twice BENCH_STOP_GRACE and at least
# BENCH_STOP_GRACE + 5 s, so that a nested bench.sh stopping a group of
# its own with that grace, as bench_bounded does, finishes first). When
# the command exits, what it left running in its group gets 1 s to end
# and is then stopped the same way, with the same grace (bench_end_group),
# before the lock is released. So nothing left in the command's process
# group outlives the lock.

BENCH_STATE_DIR=
BENCH_LOCK_DIR=
BENCH_LOCK_LOG=
BENCH_LOCK_OWNED=
BENCH_LOCK_MINE=
BENCH_LOCK_SINCE=
BENCH_LOCK_CHILD=
BENCH_LOCK_REAPED=
BENCH_LOCK_GUARD=
BENCH_LOCK_QUEUE=
BENCH_LOCK_KEY=
BENCH_LOCK_QSINCE=
BENCH_LOCK_TICKET=
BENCH_LOCK_TICKET_TMP=
BENCH_LOCK_AHEAD=0

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
	BENCH_LOCK_QUEUE=${BENCH_STATE_DIR:?}/queue
}

# bench_lock_enqueue <label>: puts this waiter's ticket in the queue (see
# the top), under the name it took first (BENCH_LOCK_KEY), so a ticket put
# back keeps its place.
bench_lock_enqueue() {
	if [ -z "$BENCH_LOCK_KEY" ]; then
		_lq_t=$(perl -MTime::HiRes=time -e 'printf "%019.6f", time' 2> /dev/null) || _lq_t=
		case $_lq_t in
		[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9].[0-9][0-9][0-9][0-9][0-9][0-9]) ;;
		*) _lq_t=$(printf '%012d.000000' "$(date +%s)") ;;
		esac
		BENCH_LOCK_KEY=$_lq_t.$(printf '%010d' "$$")
		BENCH_LOCK_QSINCE=$(bench_utc)
	fi
	mkdir -p "${BENCH_LOCK_QUEUE:?}" 2> /dev/null || bench_die 2 "cannot make the lock queue in the state directory"
	BENCH_LOCK_TICKET_TMP=$(mktemp "${BENCH_LOCK_QUEUE:?}/tmp.$$.XXXXXX" 2> /dev/null) || BENCH_LOCK_TICKET_TMP=
	if [ -z "$BENCH_LOCK_TICKET_TMP" ] ||
		! printf 'pid=%s\nlabel=%s\nsince=%s\n' "$$" "$1" "$BENCH_LOCK_QSINCE" > "$BENCH_LOCK_TICKET_TMP" 2> /dev/null; then
		bench_die 2 "cannot write a ticket in the lock queue"
	fi
	# Named before the rename, so the EXIT trap removes it from then on.
	BENCH_LOCK_TICKET=${BENCH_LOCK_QUEUE:?}/w.$BENCH_LOCK_KEY.${BENCH_LOCK_TICKET_TMP##*.}
	mv -f "$BENCH_LOCK_TICKET_TMP" "$BENCH_LOCK_TICKET" 2> /dev/null || bench_die 2 "cannot write a ticket in the lock queue"
	BENCH_LOCK_TICKET_TMP=
}

# bench_lock_unqueue: takes this process's ticket (and its temporary) out
# of the queue.
bench_lock_unqueue() {
	if [ -n "$BENCH_LOCK_TICKET" ]; then rm -f "${BENCH_LOCK_TICKET:?}"; fi
	if [ -n "$BENCH_LOCK_TICKET_TMP" ]; then rm -f "${BENCH_LOCK_TICKET_TMP:?}"; fi
	BENCH_LOCK_TICKET=
	BENCH_LOCK_TICKET_TMP=
	return 0
}

# bench_lock_head <label>: this waiter's ticket comes first among the live
# ones. Puts the ticket back when it is gone; removes every ticket and
# temporary whose waiter is gone; counts the live tickets ahead of this
# one (BENCH_LOCK_AHEAD). The glob sorts by name (bench.sh sets LC_ALL=C).
bench_lock_head() {
	if [ -z "$BENCH_LOCK_TICKET" ] || [ ! -e "$BENCH_LOCK_TICKET" ]; then bench_lock_enqueue "$1"; fi
	BENCH_LOCK_AHEAD=0
	_lh_mine=${BENCH_LOCK_TICKET##*/}
	_lh_past=
	for _lh_f in "${BENCH_LOCK_QUEUE:?}"/*; do
		_lh_n=${_lh_f##*/}
		case $_lh_n in
		"$_lh_mine") _lh_past=1 ;;
		w.*)
			_lh_h=$(cat "$_lh_f" 2> /dev/null) || continue
			_lh_p=$(bench_kv "$_lh_h" pid)
			# Gone: kill -0 fails, or a zombie (KILLed, not yet reaped).
			if ! bench_pid_running "$_lh_p"; then
				rm -f "$_lh_f"
			elif [ -z "$_lh_past" ]; then
				BENCH_LOCK_AHEAD=$((BENCH_LOCK_AHEAD + 1))
			fi
			;;
		tmp.*)
			_lh_p=${_lh_n#tmp.}
			bench_pid_running "${_lh_p%%.*}" || rm -f "$_lh_f"
			;;
		esac
	done
	[ "$BENCH_LOCK_AHEAD" -eq 0 ]
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
	BENCH_LOCK_KEY=
	bench_lock_enqueue "$_la_label"
	while :; do
		_la_head=
		if bench_lock_head "$_la_label"; then _la_head=1; fi
		if [ -z "$_la_head" ]; then
			# Behind an earlier waiter: nap below.
			_la_bare=
		elif mkdir "${BENCH_LOCK_DIR:?}" 2> /dev/null; then
			# Out of the queue the moment the directory is made, so the next
			# waiter comes first; a maker that backs off below puts its ticket
			# back under its first name.
			bench_lock_unqueue
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
			bench_lock_enqueue "$_la_label"
		fi
		_la_h=$(bench_lock_holder)
		_la_tok=$(bench_kv "$_la_h" token)
		if [ -z "$_la_head" ]; then
			:
		elif [ -n "$_la_tok" ]; then
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
			_la_who="held by $(bench_kv "$_la_h" label) (pid $(bench_kv "$_la_h" pid), since $(bench_kv "$_la_h" start))"
		elif [ -d "${BENCH_LOCK_DIR:?}" ]; then
			_la_who="held by a holder that has not written its holder file yet"
		else
			_la_who=free
		fi
		_la_q=
		if [ "$BENCH_LOCK_AHEAD" -gt 0 ]; then
			_la_q=", behind $BENCH_LOCK_AHEAD earlier waiter"
			[ "$BENCH_LOCK_AHEAD" -eq 1 ] || _la_q=${_la_q}s
		fi
		if [ $(($(date +%s) - _la_t0)) -ge "$_la_wait" ]; then
			bench_note "the lock is still $_la_who after $_la_wait s$_la_q; giving up"
			bench_lock_unqueue; return 75
		fi
		if [ -z "$_la_said" ]; then
			if [ "$_la_who" = free ]; then
				bench_note "waiting for the lock$_la_q; at most $_la_wait s"
			else
				bench_note "waiting for the lock $_la_who$_la_q; at most $_la_wait s"
			fi
			_la_said=1
		fi
		bench_nap "$_la_poll"
	done
}

# The quiet marker: <state>/quiet, written by lock run --quiet while it
# holds the lock (and by a measuring command under a hold of its own), so
# that the tools of other sessions on the host can hold their work while
# something is measured. key=value lines:
#   pid=<the writer's pid>  session=<id or none>  label=<lock label>
#   start=<UTC>  expires=<seconds since the epoch>  hold=<the lock's token>
#   token=<the marker's own token>
# A reader honours it only while expires is in the future and pid is alive.
# The session is BENCH_SESSION_ID, else CLAUDE_CODE_SESSION_ID: up to 128
# of A-Z a-z 0-9 . _ : -, else none. expires is now plus BENCH_QUIET_TTL
# seconds (default 3600; bench.sh checks it before the lock is taken). It
# is written whole to a temporary file mktemp makes in the state directory
# and renamed into place, and removed by bench_lock_release on every exit of
# its writer (the EXIT, INT, TERM and HUP traps), and by the lock shell
# whose hold it names.

BENCH_QUIET_MINE=

# bench_quiet_live <file>: the marker <file> is there, unexpired, and its
# writer is alive.
bench_quiet_live() {
	_ql_h=$(cat "$1" 2> /dev/null) || return 1
	_ql_e=$(bench_kv "$_ql_h" expires)
	bench_is_uint "${_ql_e:-x}" || return 1
	[ "$_ql_e" -gt "$(date +%s)" ] || return 1
	bench_pid_alive "$(bench_kv "$_ql_h" pid)"
}

# bench_quiet_mark <label> <pid>: writes the marker for <pid>. The lock
# shell that has just taken the lock writes it whatever is there; under a
# hold of its own (a token, not this process's lock) a caller writes it
# only when no live marker is there.
bench_quiet_mark() {
	_qm_f=${BENCH_STATE_DIR:?}/quiet
	if [ "${BENCH_LOCK_OWNED:-}" != 1 ] && bench_quiet_live "$_qm_f"; then
		return 0
	fi
	_qm_ttl=${BENCH_QUIET_TTL:-3600}
	bench_is_uint "$_qm_ttl" || bench_die 2 "BENCH_QUIET_TTL is whole seconds"
	_qm_s=${BENCH_SESSION_ID:-${CLAUDE_CODE_SESSION_ID:-}}
	case $_qm_s in
	'' | *[!A-Za-z0-9._:-]*) _qm_s=none ;;
	esac
	[ "${#_qm_s}" -le 128 ] || _qm_s=none
	_qm_now=$(date +%s)
	_qm_r=$(od -An -N6 -tx1 /dev/urandom 2> /dev/null | tr -dc '0-9a-f') || _qm_r=
	BENCH_QUIET_MINE=q-$$-$_qm_now-${_qm_r:-0}
	# A temporary file mktemp makes (a name no other process knows ahead, so
	# none can plant a link there), renamed into place.
	_qm_t=$(mktemp "${BENCH_STATE_DIR:?}/quiet.tmp.XXXXXX" 2> /dev/null) || _qm_t=
	if [ -z "$_qm_t" ] || ! printf 'pid=%s\nsession=%s\nlabel=%s\nstart=%s\nexpires=%s\nhold=%s\ntoken=%s\n' \
		"$2" "$_qm_s" "$1" "$(bench_utc)" "$((_qm_now + _qm_ttl))" "${BENCH_LOCK_TOKEN:-none}" "$BENCH_QUIET_MINE" > "$_qm_t" 2> /dev/null ||
		! mv -f "$_qm_t" "$_qm_f" 2> /dev/null; then
		if [ -n "$_qm_t" ]; then rm -f "$_qm_t"; fi
		BENCH_QUIET_MINE=
		bench_note "cannot write the quiet marker in the lock state directory"
	fi
}

# bench_quiet_release: removes the marker when it is the one this process
# wrote, or, in the lock shell, one that names this process's hold.
bench_quiet_release() {
	[ -n "${BENCH_STATE_DIR:-}" ] || return 0
	_qx_h=$(cat "${BENCH_STATE_DIR:?}/quiet" 2> /dev/null) || return 0
	if { [ -n "$BENCH_QUIET_MINE" ] && [ "$(bench_kv "$_qx_h" token)" = "$BENCH_QUIET_MINE" ]; } ||
		{ [ "${BENCH_LOCK_OWNED:-}" = 1 ] && [ "$(bench_kv "$_qx_h" hold)" = "$BENCH_LOCK_MINE" ]; }; then
		rm -f "${BENCH_STATE_DIR:?}/quiet"
	fi
	BENCH_QUIET_MINE=
}

# bench_lock_release: takes this process's ticket out of the queue (a
# waiter that gave up, or a signal while it waited), removes the quiet
# marker this process wrote, then the lock if this process holds it.
bench_lock_release() {
	if [ -n "${BENCH_LOCK_TICKET:-}${BENCH_LOCK_TICKET_TMP:-}" ]; then bench_lock_unqueue; fi
	bench_quiet_release
	[ "${BENCH_LOCK_OWNED:-}" = 1 ] || return 0
	BENCH_LOCK_OWNED=
	if [ "$(bench_kv "$(bench_lock_holder)" token)" = "$BENCH_LOCK_MINE" ]; then
		rm -f "${BENCH_LOCK_DIR:?}/holder" "${BENCH_LOCK_DIR:?}/holder.tmp.$$"
		rmdir "${BENCH_LOCK_DIR:?}" 2> /dev/null || true
	fi
	return 0
}

# bench_lock_grace: prints the seconds the lock gives the command's process
# group between TERM and KILL, on a signal, a stop and for what the command
# left running alike: twice BENCH_STOP_GRACE (default 10), and at least
# BENCH_STOP_GRACE + 5. The floor lets a nested bench.sh stop its own
# bounded group, which takes up to its grace, a second of clock and 2 s
# after its KILL, before this KILL ends it.
bench_lock_grace() {
	_lg_g=${BENCH_STOP_GRACE:-10}
	_lg_l=$((2 * _lg_g))
	[ "$_lg_l" -ge $((_lg_g + 5)) ] || _lg_l=$((_lg_g + 5))
	printf '%s\n' "$_lg_l"
}

# bench_lock_stop_child: stops the command this process runs under the lock
# and its whole process group: TERM, up to bench_lock_grace seconds for the
# group to go, then KILL; then reaps it (once it is reaped,
# BENCH_LOCK_REAPED, the group only).
bench_lock_stop_child() {
	[ -n "${BENCH_LOCK_CHILD:-}" ] || return 0
	bench_stop_group "$BENCH_LOCK_CHILD" "$(bench_lock_grace)" "${BENCH_LOCK_REAPED:-}"
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

# bench_lock_run [--quiet] <label> -- <command...>: runs the command under
# the lock and exits with its status (75 when the wait ran out, 74 when the
# command stopped). With --quiet the quiet marker is written once the lock
# is held, before the command starts.
bench_lock_run() {
	_ru_quiet=
	if [ "${1:-}" = --quiet ]; then
		_ru_quiet=1
		shift
	fi
	if [ "$#" -lt 3 ] || [ "$2" != -- ]; then
		bench_die 2 "usage: bench.sh lock run [--quiet] <label> -- <command> [args...]"
	fi
	_ru_label=$1
	shift 2
	bench_is_label "$_ru_label" || bench_die 2 "a lock label is 1 to 64 of A-Z a-z 0-9 . _ : @ + -"
	bench_lock_init
	if bench_lock_mine; then
		# The command becomes this process, so the marker names this pid and
		# the hold; the lock shell of that hold removes it.
		if [ -n "$_ru_quiet" ]; then bench_quiet_mark "$_ru_label" "$$"; fi
		BENCH_QUIET_MINE=
		exec "$@"
	fi
	_ru_st=0
	bench_lock_acquire "$_ru_label" || _ru_st=$?
	[ "$_ru_st" -eq 0 ] || exit "$_ru_st"
	BENCH_LOCK_TOKEN=$BENCH_LOCK_MINE
	export BENCH_LOCK_TOKEN
	if [ -n "$_ru_quiet" ]; then bench_quiet_mark "$_ru_label" "$$"; fi
	# In the background, in its own process group, so the trap reaches the
	# whole command. The lock shell polls it (bench_watch, below), so a TERM
	# to the lock shell alone runs its trap once the current nap's sleep
	# ends, at most 1 s later; a signal to the lock shell's process group
	# (Ctrl-C) ends that sleep too, so its trap runs at once.
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
	bench_end_group "$BENCH_LOCK_CHILD" "$(bench_lock_grace)" || bench_note "stopped what the command left running in its process group"
	BENCH_LOCK_CHILD=
	exit "$_ru_st"
}

# bench_lock_status: prints "lock=free", or "lock=held" with the holder;
# then a line for each live waiter, in the order they get the lock:
# "waiter=<n> label=<label> pid=<pid> since=<UTC>".
bench_lock_status() {
	bench_lock_init
	if [ ! -d "${BENCH_LOCK_DIR:?}" ]; then
		echo lock=free
	else
		_ls_h=$(bench_lock_holder)
		if [ -z "$_ls_h" ]; then
			echo "lock=held label=none holder=unwritten"
		else
			_ls_p=$(bench_kv "$_ls_h" pid)
			_ls_c=$(bench_kv "$_ls_h" child)
			_ls_a=no
			if bench_lock_alive "$_ls_h"; then _ls_a=yes; fi
			printf 'lock=held label=%s pid=%s child=%s alive=%s since=%s\n' \
				"$(bench_kv "$_ls_h" label)" "$_ls_p" "${_ls_c:-none}" "$_ls_a" "$(bench_kv "$_ls_h" start)"
		fi
	fi
	_ls_i=0
	for _ls_f in "${BENCH_LOCK_QUEUE:?}"/w.*; do
		_ls_q=$(cat "$_ls_f" 2> /dev/null) || continue
		bench_pid_running "$(bench_kv "$_ls_q" pid)" || continue
		_ls_i=$((_ls_i + 1))
		printf 'waiter=%s label=%s pid=%s since=%s\n' "$_ls_i" \
			"$(bench_kv "$_ls_q" label)" "$(bench_kv "$_ls_q" pid)" "$(bench_kv "$_ls_q" since)"
	done
}
