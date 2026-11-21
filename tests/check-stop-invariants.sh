#!/bin/sh
# Invariant: the Stop hook .claude/hooks/stop-invariants.sh does its job
# inside the timeout .claude/settings.json gives it (FIX60). A hook that
# outlasts its timeout is stopped and blocks nothing, so a stop with a
# failing invariant would go through without a word.
#
# This fails when
#  - the hook's list ($checks) is empty, or names a check twice, names
#    this check (which runs the hook), or names anything that is not an
#    executable tests/check-*.sh;
#  - the Stop entry's timeout, or the hook's deadline and grace, cannot be
#    read, or the deadline plus the grace is more than three quarters of
#    the timeout;
#  - a self-test, on a copy of the hook with stub checks and a 2 s deadline
#    in a throwaway repository, does not show that the hook runs nothing on
#    a clean tree; on a change under src/ exits 0 and prints nothing when
#    the checks pass, also when one leaves a process behind (which must not
#    hold the hook's output open); blocks (exit 2) on a failing check and
#    names it, and with stop_hook_active true names it and exits 0; names
#    a missing listed check and blocks; stops a check that hangs and
#    ignores TERM at the deadline, with every process in its group, names
#    it and the checks it kept from running, and blocks; on TERM, kills the
#    running check's group and exits 0; with no job control (a copy with
#    set -m taken out) runs no check, says so and blocks once; falls back
#    to /tmp when $TMPDIR is missing, and with no temporary directory at
#    all (a mktemp that fails) runs no check, says so and blocks once;
#    exits 0 on its own error (here a date command that prints nothing);
#    and each signal to a pid read earlier (the process check-leave.sh
#    leaves, the hook run in the background, the hanging check's group)
#    goes only while it still has the start time read with it, at the
#    site itself, with that start time replaced by another, by none, and
#    with ps giving no answer, which the check says it cannot tell; and a
#    copy of the hook whose TERM trap leaves the check's group running is
#    seen to (FIX171);
#  - the real hook, run on a copy of HEAD with the working tree's hook and
#    listed checks put in and a new file under src/, does not exit 0
#    silently, or takes more than half the timeout; or, with an
#    undocumented cvar added under src/, does not block naming
#    tests/check-cvar-docs.sh.
#
# Usage: tests/check-stop-invariants.sh
set -eu
CDPATH='' cd "$(dirname "$0")/.."
root=$(pwd)
hook=.claude/hooks/stop-invariants.sh
settings=.claude/settings.json
me=check-stop-invariants
tmp=$(mktemp -d "${TMPDIR:-/tmp}/stopck.XXXXXX")
tmp=$(CDPATH='' cd "${tmp:?}" && pwd -P)
trap 'rm -rf "${tmp:?}"' EXIT
trap 'exit 1' HUP INT TERM

fail()
{
	echo "$me: $*" >&2
	exit 1
}

now()
{
	date +%s
}

# start_form: the one form of a start time, made from what ps -o lstart=
# printed (on stdin): its words with one blank between them and none around
# them. ps pads the time with blanks at the end, and read strips them when a
# stored time is read back, so a time in any other form never matches itself
# after a trip through a file (FIX171). Every start time here, stored, read
# or compared, comes from this function and from nowhere else.
start_form()
{
	awk '{ $1 = $1; print }'
}

# start_of <pid>: the start time of <pid>, in start_form; empty when it is
# not a pid, does not run, or ps gives no answer.
start_of()
{
	case $1 in
	'' | *[!0-9]*) return 0 ;;
	esac
	ps -o lstart= -p "$1" 2> /dev/null | start_form
}

# judge <pid> <start>: whether a pid read earlier, with <start>, may be
# signalled (FIX171): by then the system may have given the pid to another
# process. 0: it still runs with <start>. 1: it has gone, or runs with
# another start time; the process read has ended, so it gets no signal.
# 2: this check cannot tell: no pid or no start time was read, or the pid
# answers kill -0 but ps gives no start time for it now.
judge()
{
	case $1 in
	'' | *[!0-9]*) return 2 ;;
	esac
	[ -n "$2" ] || return 2
	_js=$(start_of "$1")
	if [ -z "$_js" ]; then
		if kill -0 "$1" 2> /dev/null; then return 2; fi
		return 1
	fi
	[ "$_js" = "$2" ] || return 1
}

# untold <case> <pid>: the failure when judge cannot tell; no signal went.
untold()
{
	fail "selftest: $1: cannot tell whether pid $2 is still the process read (no start time was read with it, or ps gives none now), so it gets no signal"
}

# leave_stop <pid> <start>: TERM to the process check-leave.sh left behind
# while it is still that process.
leave_stop()
{
	_j=0
	judge "$1" "$2" || _j=$?
	case $_j in
	0) kill "$1" 2> /dev/null || true ;;
	2) untold "a check that leaves a process behind" "$1" ;;
	esac
}

# hook_sig <signal>: <signal> to the hook started in the background ($hp)
# while it still has the start time read when it was started ($hps).
hook_sig()
{
	_j=0
	judge "$hp" "$hps" || _j=$?
	case $_j in
	0) kill -s "$1" "$hp" 2> /dev/null || true ;;
	2) untold "TERM to the hook" "$hp" ;;
	esac
}

# group_ids <pids> <ids>: each pid in <pids> (the hanging check's group)
# with its start time now, as "<pid> <start>" lines, into <ids>.
group_ids()
{
	while read -r _p; do
		printf '%s %s\n' "$_p" "$(start_of "$_p")"
	done < "$1" > "$2"
}

# strays <ids>: KILL to each process in <ids> (as group_ids writes it) that
# still runs as that process, with its pid on stdout: what the hook left
# running of the check's group.
strays()
{
	while read -r _p _s; do
		_j=0
		judge "$_p" "$_s" || _j=$?
		case $_j in
		0)
			kill -KILL "$_p" 2> /dev/null || true
			printf ' %s' "$_p"
			;;
		2) untold "TERM to the hook" "$_p" ;;
		esac
	done < "$1"
}

# alive <pid> <start>: <pid> still runs, not a zombie, with <start>.
alive()
{
	[ "$(start_of "$1")" = "$2" ] || return 1
	case $(ps -o stat= -p "$1" 2> /dev/null) in
	'' | Z*) return 1 ;;
	esac
}

# standin <file>: starts a sleep in a subshell of its own, with no output
# (so no job notice is printed for it), that ends by itself within 10 s:
# a process of this check's that stands in for a pid the system gave to
# another process. Sets sp (the subshell), sd (the sleep) and sds (its
# start time).
standin()
{
	rm -f "$1"
	(sleep 10 & echo $! > "$1" && wait) > /dev/null 2>&1 &
	sp=$!
	n=0
	until [ -s "$1" ]; do
		n=$((n + 1))
		[ "$n" -le 50 ] || fail "selftest: a stand-in did not start"
		sleep 0.1
	done
	sd=$(cat "$1")
	sds=$(start_of "$sd")
	[ -n "$sds" ] || fail "selftest: ps gave no start time for a stand-in ($sd)"
}

# standin_end: stops the stand-in while it is still that process, and waits
# for its subshell.
standin_end()
{
	if judge "$sd" "$sds"; then kill "$sd" 2> /dev/null || true; fi
	wait "$sp" || true
}

[ -f "$hook" ] || fail "$hook is missing"

# The list: the lines between checks=' and the closing quote.
awk '/^checks='"'"'$/ { on = 1; next } on && /^'"'"'$/ { exit } on { print }' "$hook" > "${tmp:?}/list" ||
	fail "cannot read the list in $hook"
[ -s "${tmp:?}/list" ] || fail "the list of checks in $hook is empty or cannot be found"
while IFS= read -r c; do
	case $c in
	tests/$me.sh) fail "the list in $hook names $c, which runs the hook" ;;
	tests/check-*.sh) ;;
	*) fail "the list in $hook names $c, which is not a tests/check-*.sh" ;;
	esac
	[ -f "$c" ] || fail "the list in $hook names $c, which does not exist"
	[ -x "$c" ] || fail "the list in $hook names $c, which is not executable"
done < "${tmp:?}/list"
dup=$(sort "${tmp:?}/list" | uniq -d)
[ -z "$dup" ] || fail "the list in $hook names a check twice: $dup"

# one <file> <sed program>: the one number the program prints, or fail.
one()
{
	v=$(sed -n "$2" "$1")
	case $v in
	'' | *[!0-9]*) fail "cannot read one number from $1 with $2 (got: $v)" ;;
	esac
	echo "$v"
}
timeout=$(one "$settings" 's/.*stop-invariants\.sh".*"timeout":[[:space:]]*\([0-9][0-9]*\).*/\1/p')
deadline=$(one "$hook" 's/^deadline=\([0-9][0-9]*\)$/\1/p')
grace=$(one "$hook" 's/^grace=\([0-9][0-9]*\)$/\1/p')
[ "$deadline" -gt 0 ] && [ "$grace" -gt 0 ] || fail "the deadline and the grace in $hook must be positive"
[ $((4 * (deadline + grace))) -le $((3 * timeout)) ] ||
	fail "the deadline ($deadline s) plus the grace ($grace s) in $hook is more than three quarters of the Stop timeout ($timeout s) in $settings"

# Git sees only the throwaway repositories, with no configuration of the
# host's; the real run below keeps HOME for the compiler.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_OBJECT_DIRECTORY \
	GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE GIT_PREFIX GIT_CONFIG \
	GIT_CONFIG_PARAMETERS GIT_CONFIG_COUNT
GIT_CONFIG_GLOBAL=/dev/null
GIT_CONFIG_NOSYSTEM=1
GIT_CEILING_DIRECTORIES=$tmp
export GIT_CONFIG_GLOBAL GIT_CONFIG_NOSYSTEM GIT_CEILING_DIRECTORIES
g()
{
	git -c core.hooksPath=/dev/null -c commit.gpgSign=false -c user.name=selftest \
		-c user.email=selftest@example.invalid "$@"
}

# run <dir> <json> [NAME=value...]: run the copy of the hook in <dir> with
# <json> on stdin; sets rc, out (its stderr and stdout) and secs.
run()
{
	d=$1
	j=$2
	shift 2
	rc=0
	t0=$(now)
	out=$(cd "${tmp:?}/$d" && printf '%s\n' "$j" | env "$@" "$hook" 2>&1) || rc=$?
	secs=$(($(now) - t0))
}
says()
{
	case $out in
	*"$2"*) ;;
	*) fail "selftest: $1: expected \"$2\" in the hook's output, got: $out" ;;
	esac
}
exits()
{
	[ "$rc" -eq "$2" ] || fail "selftest: $1: the hook exited $rc, not $2; it printed: $out"
}
quiet()
{
	exits "$1" 0
	[ -z "$out" ] || fail "selftest: $1: the hook printed: $out"
}

# idtest: the stray site, group_ids then strays, on a stand-in listed as a
# member of the hanging check's group, through the file that group_ids
# writes and strays reads back, as in the TERM case below (FIX171). Listed
# with another start time, it gets no KILL and is not named; with none, or
# with ps giving no answer, it gets no KILL and the check says it cannot
# tell; with the start time group_ids wrote, it is KILLed and named.
idtest()
{
	i=${tmp:?}/id
	mkdir -p "$i"
	standin "$i/sd.pid"
	cp "$i/sd.pid" "$i/pids"
	group_ids "$i/pids" "$i/ids"
	for how in other none nops own; do
		case $how in
		other) printf '%s %s\n' "$sd" 'Thu Jan 1 00:00:00 2026' > "$i/ids.$how" ;;
		none) printf '%s \n' "$sd" > "$i/ids.$how" ;;
		*) cp "$i/ids" "$i/ids.$how" ;;
		esac
		sr=0
		got=$(
			if [ "$how" = nops ]; then ps() { return 1; }; fi
			strays "$i/ids.$how" 2>&1
		) || sr=$?
		case $how:$sr in
		other:0) [ -z "$got" ] || fail "selftest: stray-id: other: a process with another start time was named: $got" ;;
		none:0 | nops:0) fail "selftest: stray-id: $how: the check did not say it cannot tell (it printed: $got)" ;;
		none:* | nops:*)
			case $got in
			*'cannot tell'*) ;;
			*) fail "selftest: stray-id: $how: expected \"cannot tell\", got: $got" ;;
			esac
			;;
		own:0) [ "$got" = " $sd" ] || fail "selftest: stray-id: own: the process listed with the start time group_ids wrote was not named (got \"$got\")" ;;
		*) fail "selftest: stray-id: own: strays failed: $got" ;;
		esac
		[ "$how" = own ] || alive "$sd" "$sds" || fail "selftest: stray-id: $how: a KILL went to a process that is not the one read"
	done
	n=0
	while alive "$sd" "$sds"; do
		n=$((n + 1))
		[ "$n" -le 30 ] || fail "selftest: stray-id: own: no KILL went to the process read"
		sleep 0.1
	done
	wait "$sp" || true
}

# The self-test: a copy of the hook with the list replaced by stubs, a
# 2 s deadline and a 1 s grace, in a throwaway repository.
selftest()
{
	idtest
	r=${tmp:?}/st
	mkdir -p "$r/.claude/hooks" "$r/tests" "$r/src"
	printf '#!/bin/sh\necho "$0: ok"\n' > "$r/tests/check-pass.sh"
	printf '#!/bin/sh\necho "$0: ok"\n' > "$r/tests/check-pass2.sh"
	printf '#!/bin/sh\necho "stub: the thing is broken"\nexit 1\n' > "$r/tests/check-fail.sh"
	# The stubs find $tmp in the environment, so no text of $TMPDIR's is
	# ever read as shell code.
	STOPCK_TMP=$tmp
	export STOPCK_TMP
	# Ignores TERM, as does the sleep it starts, so only KILL stops them.
	printf '%s\n' '#!/bin/sh' 'trap "" TERM' 'echo $$ > "$STOPCK_TMP/hang.pids"' 'sleep 20 &' \
		'echo $! >> "$STOPCK_TMP/hang.pids"' 'wait' > "$r/tests/check-hang.sh"
	# Passes, and leaves a process behind that would hold the hook's output.
	# Its pid goes into leave.pid, with its start time beside it.
	printf '%s\n' '#!/bin/sh' 'sleep 15 &' 'ps -o lstart= -p $! > "$STOPCK_TMP/leave.start" 2> /dev/null' \
		'echo $! > "$STOPCK_TMP/leave.pid"' 'echo "$0: ok"' > "$r/tests/check-leave.sh"
	# Leaves a mark that it ran, and fails.
	printf '%s\n' '#!/bin/sh' ': > "$STOPCK_TMP/ran"' 'exit 1' > "$r/tests/check-mark.sh"
	chmod 755 "$r"/tests/check-*.sh
	printf '%s\n' 'int x;' > "$r/src/a.c"
	g init -q --template= "$r"
	g -C "$r" add -A
	g -C "$r" commit -q -m seed

	# hookwith <list...>: put in $r the copy of the hook with that list.
	hookwith()
	{
		awk -v list="$*" '
			/^checks='"'"'$/ { print; n = split(list, l, " "); for (i = 1; i <= n; i++) print l[i]; on = 1; next }
			on && /^'"'"'$/ { on = 0 }
			on { next }
			/^deadline=[0-9]+$/ { print "deadline=2"; next }
			/^grace=[0-9]+$/ { print "grace=1"; next }
			{ print }' "$root/$hook" > "$r/$hook"
		chmod 755 "$r/$hook"
		grep -qx 'deadline=2' "$r/$hook" && grep -qx 'grace=1' "$r/$hook" ||
			fail "selftest: cannot set the deadline and the grace in the copy of $hook"
	}
	no='{"session_id":"x","stop_hook_active":false}'
	yes='{"session_id":"x","stop_hook_active":true}'

	hookwith tests/check-fail.sh
	g -C "$r" add -A
	g -C "$r" commit -q -m hook
	run st "$no"
	quiet "a clean tree"

	printf '%s\n' 'int y;' >> "$r/src/a.c"
	hookwith tests/check-pass.sh tests/check-pass2.sh
	run st "$no"
	quiet "passing checks"

	hookwith tests/check-pass.sh tests/check-leave.sh
	run st "$no"
	quiet "a check that leaves a process behind"
	[ "$secs" -le 6 ] ||
		fail "selftest: a check that leaves a process behind: the hook's output stayed open $secs s, held by that process"
	# The stop of what it left, read from leave.pid and leave.start (FIX171):
	# first with the start time read replaced by another, by none, and with
	# ps giving no answer, and each time the process must run on (and the
	# check say it cannot tell, for the last two); then as the stub wrote it.
	lp=$(cat "${tmp:?}/leave.pid" 2> /dev/null) || lp=
	cp "${tmp:?}/leave.start" "${tmp:?}/leave.own" 2> /dev/null || : > "${tmp:?}/leave.own"
	lps=$(start_form < "${tmp:?}/leave.own")
	[ -n "$lps" ] || fail "selftest: a check that leaves a process behind: no start time was written for the process it left ($lp)"
	for how in other none nops own; do
		case $how in
		other) printf '%s\n' 'Thu Jan  1 00:00:00 2026    ' > "${tmp:?}/leave.start" ;;
		none) : > "${tmp:?}/leave.start" ;;
		*) cp "${tmp:?}/leave.own" "${tmp:?}/leave.start" ;;
		esac
		sr=0
		got=$(
			if [ "$how" = nops ]; then ps() { return 1; }; fi
			leave_stop "$(cat "${tmp:?}/leave.pid" 2> /dev/null)" "$(start_form < "${tmp:?}/leave.start")" 2>&1
		) || sr=$?
		case $how:$sr in
		other:0 | own:0) ;;
		none:0 | nops:0) fail "selftest: leave-id: $how: the check did not say it cannot tell" ;;
		none:* | nops:*)
			case $got in
			*'cannot tell'*) ;;
			*) fail "selftest: leave-id: $how: expected \"cannot tell\", got: $got" ;;
			esac
			;;
		*) fail "selftest: a check that leaves a process behind: $got" ;;
		esac
		[ "$how" = own ] || alive "$lp" "$lps" || fail "selftest: leave-id: $how: a signal went to a process that is not the one read"
	done
	n=0
	while alive "$lp" "$lps"; do
		n=$((n + 1))
		[ "$n" -le 30 ] || fail "selftest: leave-id: own: the process check-leave.sh left still runs after its TERM"
		sleep 0.1
	done

	hookwith tests/check-pass.sh tests/check-fail.sh tests/check-pass2.sh
	run st "$no"
	exits "a failing check" 2
	says "a failing check" "FAILED: tests/check-fail.sh"
	says "a failing check" "stub: the thing is broken"
	case $out in
	*check-pass*) fail "selftest: a failing check: the hook named a check that passed: $out" ;;
	esac
	run st "$yes"
	exits "a failing check, stop_hook_active" 0
	says "a failing check, stop_hook_active" "not blocking again"
	says "a failing check, stop_hook_active" "FAILED: tests/check-fail.sh"
	run st '{"stop_hook_active": true}'
	exits "a failing check, stop_hook_active with a space" 0

	hookwith tests/check-pass.sh tests/check-gone.sh
	run st "$no"
	exits "a missing check" 2
	says "a missing check" "MISSING: tests/check-gone.sh"
	run st "$yes"
	exits "a missing check, stop_hook_active" 0
	says "a missing check, stop_hook_active" "MISSING: tests/check-gone.sh"

	hookwith tests/check-pass.sh tests/check-hang.sh tests/check-pass2.sh
	run st "$no"
	exits "a check that hangs" 2
	says "a check that hangs" "DID NOT FINISH: tests/check-hang.sh"
	says "a check that hangs" "NOT RUN: tests/check-pass2.sh"
	[ "$secs" -le 10 ] || fail "selftest: a check that hangs: the hook took $secs s with a 2 s deadline and a 1 s grace"
	[ -s "${tmp:?}/hang.pids" ] || fail "selftest: a check that hangs: the stub did not start"
	# A killed process can take a moment to be reaped.
	while read -r p; do
		n=0
		while kill -0 "$p" 2> /dev/null; do
			n=$((n + 1))
			[ "$n" -le 3 ] || fail "selftest: a check that hangs: process $p of its group still runs"
			sleep 1
		done
	done < "${tmp:?}/hang.pids"

	# TERM to the hook, as at a timeout: it kills the running check's group
	# on the way out. A 20 s deadline keeps the watcher from doing it first.
	#
	# term_hook all|own: runs the copy of the hook in $r in the background,
	# with check-hang.sh listed, and sends it TERM once the check's group
	# runs. Sets rc (the hook's status) and stray (the pids of the group it
	# left running, now KILLed). Each signal to a pid read earlier goes only
	# while it still has the start time read with it (FIX171). With all, the
	# TERM to the hook is sent first with that start time replaced by
	# another, by none, and with ps giving no answer, and the hook must run
	# on each time. In both, a stand-in listed with the group, with another
	# start time, must be neither KILLed nor named. With own (the second run, on a
	# copy of the hook whose TERM trap leaves the group), the TERM to the
	# hook is sent once, as read: the first run's decoys cover that site.
	term_hook()
	{
		th=$1
		rm -f "${tmp:?}/hang.pids"
		standin "${tmp:?}/sd.pid"
		(cd "$r" && exec "$hook" < "${tmp:?}/no.json" > /dev/null 2>&1) &
		hp=$!
		hps=$(start_of "$hp")
		[ -n "$hps" ] || fail "selftest: TERM to the hook: ps gave no start time for the hook ($hp)"
		hsig=TERM
		n=0
		until [ -s "${tmp:?}/hang.pids" ] && [ $(($(wc -l < "${tmp:?}/hang.pids"))) -ge 2 ]; do
			n=$((n + 1))
			[ "$n" -le 100 ] || {
				hsig=KILL
				break
			}
			sleep 0.1
		done
		# The group's pids, read with their start times while they run (they
		# ignore TERM, and the hook has not been signalled yet).
		[ "$hsig" = KILL ] || group_ids "${tmp:?}/hang.pids" "${tmp:?}/hang.ids"
		hown=$hps
		hows='other none nops'
		[ "$th" != own ] || hows=
		for how in $hows own; do
			case $how in
			other) hps='Thu Jan 1 00:00:00 2026' ;;
			none) hps= ;;
			*) hps=$hown ;;
			esac
			sr=0
			got=$(
				if [ "$how" = nops ]; then ps() { return 1; }; fi
				hook_sig "$hsig" 2>&1
			) || sr=$?
			case $how:$sr in
			other:0 | own:0) ;;
			none:0 | nops:0) fail "selftest: hook-id: $how: the check did not say it cannot tell" ;;
			none:* | nops:*)
				case $got in
				*'cannot tell'*) ;;
				*) fail "selftest: hook-id: $how: expected \"cannot tell\", got: $got" ;;
				esac
				;;
			*) fail "selftest: TERM to the hook: $got" ;;
			esac
			[ "$how" != nops ] || {
				sleep 0.5
				alive "$hp" "$hown" || fail "selftest: hook-id: a signal went to the hook read with another start time, with none, or with no answer from ps"
			}
		done
		[ "$hsig" = TERM ] || fail "selftest: TERM to the hook: the stub did not start"
		rc=0
		wait "$hp" || rc=$?
		sleep 1
		# The stand-in, listed with another start time.
		echo "$sd" >> "${tmp:?}/hang.pids"
		printf '%s %s\n' "$sd" 'Thu Jan 1 00:00:00 2026' >> "${tmp:?}/hang.ids"
		stray=$(strays "${tmp:?}/hang.ids")
		case " $stray " in
		*" $sd "*) fail "selftest: stray-id: the stand-in listed with another start time was named as left running" ;;
		esac
		alive "$sd" "$sds" || fail "selftest: stray-id: a KILL went to the stand-in listed with another start time"
		standin_end
	}
	hookwith tests/check-hang.sh
	sed 's/^deadline=2$/deadline=20/' "$r/$hook" > "${tmp:?}/h" && cat "${tmp:?}/h" > "$r/$hook"
	grep -qx 'deadline=20' "$r/$hook" || fail "selftest: TERM to the hook: cannot set the deadline in the copy of $hook"
	printf '%s\n' "$no" > "${tmp:?}/no.json"
	term_hook all
	[ "$rc" -eq 0 ] || fail "selftest: TERM to the hook: the hook exited $rc, not 0"
	[ -z "$stray" ] || fail "selftest: TERM to the hook: process$stray of the check's group still ran 1 s later"
	# The same with a copy whose TERM trap leaves the check's group running:
	# the case above must see the group left (FIX171).
	awk -v s='[ -z "$c" ] || kill -KILL -- "-$c"; ' '
		/^trap / && (i = index($0, s)) { $0 = substr($0, 1, i - 1) substr($0, i + length(s)); n++ }
		{ print }
		END { exit n != 1 }' "$r/$hook" > "${tmp:?}/h" && cat "${tmp:?}/h" > "$r/$hook" ||
		fail "selftest: cannot take the group's KILL out of the TERM trap in the copy of $hook"
	term_hook own
	[ -n "$stray" ] || fail "selftest: TERM to a hook that leaves the check's group running: the group was not seen left running"

	# No job control: a copy of the hook with set -m taken out.
	hookwith tests/check-mark.sh
	sed 's/^(set -m) && set -m$/:/' "$r/$hook" > "${tmp:?}/h" && cat "${tmp:?}/h" > "$r/$hook"
	grep -qx ':' "$r/$hook" && ! grep -q 'set -m$' "$r/$hook" ||
		fail "selftest: cannot take set -m out of the copy of $hook"
	rm -f "${tmp:?}/ran"
	run st "$no"
	exits "no job control" 2
	says "no job control" "NOT RUN: any listed check, this shell has no job control"
	[ ! -e "${tmp:?}/ran" ] || fail "selftest: no job control: the hook ran a check; it printed: $out"
	run st "$yes"
	exits "no job control, stop_hook_active" 0
	says "no job control, stop_hook_active" "not blocking again"
	says "no job control, stop_hook_active" "this shell has no job control"

	# $TMPDIR is missing: the hook falls back to /tmp and runs the checks.
	hookwith tests/check-fail.sh
	run st "$no" TMPDIR="${tmp:?}/missing"
	exits "a missing TMPDIR" 2
	says "a missing TMPDIR" "FAILED: tests/check-fail.sh"
	# No temporary directory anywhere (a mktemp that fails): no check runs.
	mkdir -p "${tmp:?}/nomktemp"
	printf '#!/bin/sh\nexit 1\n' > "${tmp:?}/nomktemp/mktemp"
	chmod 755 "${tmp:?}/nomktemp/mktemp"
	run st "$no" PATH="${tmp:?}/nomktemp:$PATH"
	exits "no temporary directory" 2
	says "no temporary directory" "NOT RUN: any listed check, no temporary directory could be made"
	run st "$yes" PATH="${tmp:?}/nomktemp:$PATH"
	exits "no temporary directory, stop_hook_active" 0
	says "no temporary directory, stop_hook_active" "not blocking again"

	mkdir -p "${tmp:?}/nodate"
	printf '#!/bin/sh\nexit 1\n' > "${tmp:?}/nodate/date"
	chmod 755 "${tmp:?}/nodate/date"
	hookwith tests/check-fail.sh
	run st "$no" PATH="${tmp:?}/nodate:$PATH"
	exits "its own error" 0
}
( selftest )

# The real hook and the listed checks, from the working tree, on a copy of
# HEAD with a new file under src/.
r=${tmp:?}/real
mkdir -p "$r"
git archive -o "${tmp:?}/head.tar" HEAD || fail "cannot archive HEAD"
tar -x -f "${tmp:?}/head.tar" -C "$r" || fail "cannot unpack the archive of HEAD"
mkdir -p "$r/.claude/hooks"
cp -p "$hook" "$r/$hook"
while IFS= read -r c; do
	cp -p "$c" "$r/$c"
done < "${tmp:?}/list"
g -C "$r" init -q --template=
g -C "$r" add -A -f
g -C "$r" commit -q -m seed
printf '%s\n' 'FIX60 probe' > "$r/src/stop-invariants-probe.txt"
run real '{"session_id":"x","stop_hook_active":false}'
[ "$rc" -eq 0 ] && [ -z "$out" ] ||
	fail "the hook exited $rc on a copy of HEAD with a change under src/; it printed: $out"
[ $((2 * secs)) -le "$timeout" ] ||
	fail "the hook took $secs s on a copy of HEAD with a change under src/, more than half of its $timeout s timeout"
took=$secs
# The listed checks really run: an undocumented cvar blocks the stop.
printf '%s\n' 'static void probe(void) { Cvar_Get("fix60_probe_cvar", "0", 0); }' > "$r/src/stop-invariants-probe.c"
run real '{"session_id":"x","stop_hook_active":false}'
case $rc:$out in
2:*"FAILED: tests/check-cvar-docs.sh"*) ;;
*) fail "with an undocumented cvar under src/, the hook exited $rc, not 2 naming tests/check-cvar-docs.sh; it printed: $out" ;;
esac
echo "$me: ok (the hook took $took s of $timeout s)"
