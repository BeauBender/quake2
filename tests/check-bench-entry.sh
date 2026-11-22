#!/bin/sh
# Invariant: every heavy entry point takes the bench host lock itself
# (docs/plans/bench-harness.md, "The host lock"), so no caller can forget
# it: scripts/test.sh as "gate", scripts/coverage.sh as "coverage" and
# scripts/pins.sh as "pins-<command>" for record, compare, replay,
# footprint, where, make-demo, selftest and evidence.
#
# Hermetic: each test copies the entry points into a throwaway tree under
# mktemp -d, beside a stub scripts/bench/bench.sh that logs its call and
# runs the command with a stand-in token (once: a call that already has a
# token is a second hold, logged and refused), and with stubs of make, cc
# and gcov on PATH that log the token they ran under. No build, no engine,
# no game data. It covers:
#   - t_gate: the gate's checks and both of its makes run under one call
#     of "lock run gate", made before any of them, and see no
#     BENCH_LOCK_ENTRY; a forged BENCH_LOCK_ENTRY without a token still
#     takes the lock;
#   - t_coverage: the instrumented make runs under "lock run coverage";
#   - t_pins: each locked command makes one call with its label and its
#     whole command line, before the command runs, and the run under it
#     reaches the command; a dry run and the usage text make none;
#   - t_cov_lc, with the real bench core: coverage's instrumented make gets
#     the caller's LC_ALL back under the lock, unset or set;
#   - t_off: BENCH_LOCK=0, GitHub Actions (CI=true with GITHUB_ACTIONS=true),
#     a tree without bench.sh and no state directory (BENCH_STATE,
#     XDG_CACHE_HOME and HOME unset) make no call, for each of the three,
#     each says so in one line naming the reason, and the heavy part still
#     runs; CI=true alone, GITHUB_ACTIONS=true alone, and BENCH_STATE or
#     XDG_CACHE_HOME alone leave the lock on;
#   - t_nested, with the real bench core and a temporary BENCH_STATE: the
#     gate holds the lock once, as "gate", while the pin selftest it runs
#     passes straight through (no wait, no second hold); a gate under an
#     outer holder (a host's own wrapper) passes straight through as well;
#     a gate that finds the lock held by another job waits, gives up with
#     75 and runs nothing; the caller's LC_ALL, set or unset, reaches the
#     heavy part, though bench.sh sets it to C; the run under the lock has
#     one watcher of its lock shell, which leads a process group of its own
#     and is gone once the gate has returned;
#   - t_kgrp, with the real bench core: a KILL to the gate's process group
#     (it reaches the lock shell alone) while a check runs ends that check
#     and the scheduler, runs no make and says why; one during a make ends
#     the make, and the other never runs; the lock is free or stale after;
#   - t_grace, with the real bench core: a TERM to the lock shell, with
#     BENCH_STOP_GRACE=1 and a check that ignores TERM, leaves nothing the
#     gate started running once the lock shell has exited, and the lock
#     free; the run under the lock sees the caller's BENCH_STOP_GRACE;
#   - t_orphan, with the real bench core: a run under the lock (the marker
#     and a token) whose parent is gone before its shell starts, so its
#     parent at start is pid 1, says so in one line and exits 143 before
#     any check, the scheduler, a make or the watcher starts.
# Every pid the tests start is taken off the cleanup list once it has been
# waited for, so the list is empty after them, and cleanup signals a listed
# pid only while its start time is still the one recorded
# (t_cleanup_id); what a failing t_kgrp leaves running is stopped by pid,
# after a check of its start time and command line. Then one
# mutant per lock call (each removing it from a copy of its entry point)
# and more: the run under the lock taking the lock again (the gate,
# pins.sh), a BENCH_LOCK_ENTRY without a token taken as a hold, an unset
# LC_ALL not restored (the gate, coverage), GATE_PARENT naming the lock
# shell, no watcher, a watcher that stops the run alone and not its group,
# a run whose parent at start is pid 1 going on, the lock shell's
# BENCH_STOP_GRACE not raised and the caller's not given back, and, for
# each of the three, no state directory taking the lock, CI=true alone
# turning it off and a silent lock-off; each must fail the test that
# targets it.
#
# Usage: tests/check-bench-entry.sh [<test>...]   (named tests only, no mutants)
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C
me=check-bench-entry
ROOT=$(pwd -P)
REAL=$ROOT/scripts
SRC=$REAL

# Nothing from the caller's environment: no bench setting (the token of the
# gate's own hold would let every lock call here pass through), no CI flag,
# no pin setting and no git setting.
for _v in $(env | sed -n 's/^\(BENCH_[A-Za-z0-9_]*\)=.*/\1/p; s/^\(PINS_[A-Za-z0-9_]*\)=.*/\1/p; s/^\(GIT_[A-Za-z0-9_]*\)=.*/\1/p'); do
	unset "$_v"
done
unset CI GITHUB_ACTIONS CC LOG XDG_CACHE_HOME 2> /dev/null || true

tmp=$(mktemp -d "${TMPDIR:-/tmp}/checkbenchentry.XXXXXX")
tmp=$(cd "${tmp:?}" && pwd -P)
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
TMPDIR=${tmp:?}
export TMPDIR
: > "${tmp:?}/pids"

# pid_running <pid>: 0 while <pid> runs; 1 when it has gone or is a zombie;
# 2 when the check cannot tell: <pid> answers kill -0 but ps gives no answer
# for it, so it may be a zombie or still run (FIX177). kill -0 needs no ps
# and sends no signal; it is the shell's own (command kill), never a case's
# stand-in. It fails for a pid that has gone and for one another user runs,
# and every pid this check waits for is its own user's: so when it fails, ps
# decides when it answers, and a pid ps gives no answer for has gone. A
# zombie answers kill -0, so it is told from a running pid by ps alone. The
# same helper is in tests/check-bench.sh.
pid_running() {
	case $1 in
	'' | 0 | *[!0-9]*) return 1 ;;
	esac
	if ! command kill -0 "$1" 2> /dev/null; then
		_s=$(ps -o stat= -p "$1" 2> /dev/null) || return 1
		case $_s in
		'' | Z*) return 1 ;;
		esac
		return 0
	fi
	_s=$(ps -o stat= -p "$1" 2> /dev/null) || return 2
	case $_s in
	'') return 2 ;;
	Z*) return 1 ;;
	esac
	return 0
}

# The cleanup list holds a line per process this check started, or read
# from a file, and has not seen end: "<pid> <identity>", the identity taken
# when the pid was put on it: its start time. A pid is signalled only while
# it still runs with that start time: one that has gone, or that ps gives no
# answer for, is not, since the system may have given its pid to another
# process by then (FIX171). The command line is no part of it: a process
# this check started may exec another program after it was put on the list
# (scripts/test.sh re-execs under the lock), and it is still this check's to
# stop; another process in the pid with the same start time would need the
# pid counter to go round within that second. The same primitive is in
# tests/check-bench.sh.
# pid_id <pid>: the identity of a running <pid>, a checksum of its start
# time as ps prints it; fails when it does not run or ps gives no answer.
pid_id() {
	pid_running "$1" || return 1
	_il=$(ps -o lstart= -p "$1" 2> /dev/null) || return 1
	[ -n "$_il" ] || return 1
	id_of "$_il"
}
# id_of <start time>: the identity of a process started then, a checksum of
# the time in its one form: its words with one blank between them and none
# around them (ps pads it with blanks at the end). Every identity here,
# stray_stop's too, comes from this function (FIX171).
id_of() {
	printf '%s\n' "$1" | awk '{ $1 = $1; print }' | cksum
}
# pid_same <pid> <identity>: <pid> runs and is still the process recorded
# with <identity>; never for an empty <identity>.
pid_same() {
	[ -n "${2:-}" ] || return 1
	_is=$(pid_id "$1") || return 1
	[ "$_is" = "$2" ]
}
# record <pid> [<list>]: puts <pid> on the cleanup list (or on <list>) with
# its identity now. A pid that has already ended, or that ps gives no answer
# for, is put on it with no identity: it is never signalled, and it stays on
# the list until it is forgotten, so the check at the end that the list is
# empty still sees a test that did not forget it.
record() {
	_ri=$(pid_id "$1") || _ri=
	printf '%s %s\n' "$1" "$_ri" >> "${2:-${tmp:?}/pids}"
}
# settle <pid>: waits, at most 1 s, until a child this shell has just
# started has become its command (until its exec the child has this shell's
# command line, and env's until env execs the command), so that a signal to
# it reaches the command and not the forked shell. The identity does not
# depend on it: an exec keeps the start time.
self_args=$(ps -o args= -p "$$" 2> /dev/null || true)
settle() {
	_sn=0
	while [ "$_sn" -lt 20 ]; do
		case $(ps -o args= -p "$1" 2> /dev/null || true) in
		"$self_args" | 'env '*) ;;
		*) return 0 ;;
		esac
		sleep 0.05
		_sn=$((_sn + 1))
	done
	return 0
}
# stop_listed <list>: TERM to each pid on <list> that is still the process
# recorded there; nothing else.
stop_listed() {
	sed 's/ .*//' "$1" | sort -u | while read -r _p; do
		if listed_same "$_p" "$1"; then kill -TERM "$_p" 2> /dev/null || true; fi
	done
}
# listed_same <pid> [<list>]: <pid> runs, and one of its lines on the cleanup
# list (or on <list>) holds the identity it has now. stop_listed and reap
# both judge a pid by it, so the two agree whatever older lines the list
# holds for that pid.
listed_same() {
	_ln=$(pid_id "$1") || return 1
	grep -qxF -- "$1 $_ln" "${2:-${tmp:?}/pids}"
}

cleanup() {
	# A trap that runs this while a case's ps (or kill) stands in for the
	# system's must not use them (FIX171, round 2), so they go first.
	unset -f kill ps 2> /dev/null || true
	# Stop any process this check started that still runs; nothing else.
	stop_listed "${tmp:?}/pids"
	rm -rf "${tmp:?}"
	[ "${_ok:-}" = 1 ] || exit 1
}
trap cleanup EXIT

MUTANT=
fails=0
bad() {
	echo "$me: $*"
	fails=$((fails + 1))
	if [ -n "$MUTANT" ]; then exit 1; fi
}
# wait_gone <seconds> <pid>: 0 once <pid> is seen gone; past <seconds>, 1
# while it still runs and 2 while the check cannot tell (pid_running): a pid
# that ps gives no answer for is never taken as gone (FIX177).
wait_gone() {
	_n=0
	while :; do
		_wg=0
		pid_running "$2" || _wg=$?
		case $_wg in
		0) _wg=1 ;;
		1) break ;;
		esac
		[ "$_n" -lt $(($1 * 10)) ] || return "$_wg"
		sleep 0.1
		_n=$((_n + 1))
	done
	return 0
}
# reap <pid> [<seconds>]: ends a process this check started (TERM, which a
# lock shell's trap turns into a stop of its command; KILL after <seconds>,
# 30 by default). True once <pid> has gone: only then may the caller wait
# for it. Each of the two signals goes only while listed_same holds for
# <pid> (FIX171): the KILL comes 30 s after the TERM, time enough for the
# system to give the pid to another process. A pid that runs but is not on
# the list, was listed with no identity, has another identity now, or that
# ps gives no start time for, gets no signal; that is a failure, said, and
# reap is false, since nothing would bound a wait for that process. So is a
# pid that ps gives no answer for at all while it answers kill -0 (the check
# cannot tell whether it runs, FIX177), and a pid that still runs 5 s after
# its KILL.
reap() {
	if ! listed_same "$1"; then
		reap_skip TERM "$1"
		return
	fi
	kill -TERM "$1" 2> /dev/null || true
	wait_gone "${2:-30}" "$1" && return 0
	if ! listed_same "$1"; then
		reap_skip KILL "$1"
		return
	fi
	kill -KILL "$1" 2> /dev/null || true
	wait_gone 5 "$1" && return 0
	bad "reap: pid $1 still runs 5 s after its KILL, so it is not waited for"
	return 1
}
# reap_skip <signal> <pid>: the signal reap did not send; true when <pid>
# has gone, a failure (said) and false while it still runs or the check
# cannot tell (FIX177).
reap_skip() {
	_rk=0
	wait_gone 0 "$2" || _rk=$?
	case $_rk in
	0) return 0 ;;
	2) bad "reap: no $1 to pid $2: it answers kill -0 and ps gives no answer for it, so the check cannot tell whether it still runs, and it is not waited for" ;;
	*) bad "reap: no $1 to pid $2: it is not the process this check recorded (another identity, none, or no answer from ps), so it is not waited for" ;;
	esac
	return 1
}
# reaped <pid> [<seconds>]: reaps <pid>, a child this check started, then
# waits for it (its status in RS) and forgets it, but waits only when reap
# ended it; false, with RS 255, when it did not.
reaped() {
	RS=0
	reap "$@" || {
		RS=255
		return 1
	}
	wait "$1" 2> /dev/null || RS=$?
	forget "$1"
}
# reap_lint <file> <helpers>: prints each line of <file> that holds the word
# reap (not reaped, reap_skip or t_reap_id: a word ends at a character other
# than a letter, a digit, _ or -, and a path's /reap is not one), joined
# with the lines a trailing \ continues, unless it is a whole-line comment or
# lies in one of the functions <helpers> names (an awk alternation). A
# function runs from a line that starts with its definition (a name of
# [A-Za-z_][A-Za-z0-9_]*) to the next line that starts with }; no other line
# is exempt, the top level included, so no form of a call outside the
# helpers gets past it (FIX171).
reap_lint() {
	awk -v ok="$2" '
	/\\$/ { b = b substr($0, 1, length($0) - 1); next }
	{ $0 = b $0; b = "" }
	/^(function[ \t]+)?[A-Za-z_][A-Za-z0-9_]*[ \t]*\(\)/ || /^function[ \t]/ {
		f = $0; sub(/^function[ \t]+/, "", f); sub(/[^A-Za-z0-9_].*/, "", f)
	}
	/^}/ { f = ""; next }
	f ~ ("^(" ok ")$") || /^[ \t]*#/ { next }
	(" " $0 " ") ~ /[^A-Za-z0-9_\/-]reap[^A-Za-z0-9_-]/ { print FILENAME ":" FNR ": " $0 }
	' "$1"
}
# forget <pid>: takes a pid this check has waited for off the cleanup list,
# so that cleanup never signals a pid the system may since have given to
# another process.
forget() {
	case $1 in
	'' | *[!0-9]*) return 0 ;;
	esac
	grep -v -- "^$1 " "${tmp:?}/pids" > "${tmp:?}/pids.new" || true
	mv "${tmp:?}/pids.new" "${tmp:?}/pids"
}
# run <seconds> <command...>: runs it with its output in ${T}/out and its
# status in ST; past <seconds> it is reaped and ST is 124.
run() {
	_rs=$1
	shift
	"$@" > "${T:?}/out" 2>&1 &
	_rp=$!
	settle "$_rp"
	record "$_rp"
	if ! wait_gone "$_rs" "$_rp"; then
		reaped "$_rp" || true
		ST=124
		return 0
	fi
	ST=0
	wait "$_rp" || ST=$?
	forget "$_rp"
}

# ---- The stubs. --------------------------------------------------------
mkdir -p "${tmp:?}/mk" "${tmp:?}/cov"
cat > "${tmp:?}/stub-bench.sh" << 'EOF'
#!/bin/sh
# A stand-in for scripts/bench/bench.sh: logs the call, then runs the
# command with a stand-in token, once.
printf 'bench %s token=%s\n' "$*" "${BENCH_LOCK_TOKEN:-none}" >> "$LOG"
[ -z "${BENCH_LOCK_TOKEN:-}" ] || exit 99
[ "$#" -ge 5 ] && [ "$1" = lock ] && [ "$2" = run ] && [ "$4" = -- ] || exit 98
shift 4
BENCH_LOCK_TOKEN=stub
export BENCH_LOCK_TOKEN
exec "$@"
EOF
cat > "${tmp:?}/mk/make" << 'EOF'
#!/bin/sh
printf 'make %s token=%s entry=%s lc=%s\n' "$*" "${BENCH_LOCK_TOKEN:-none}" "${BENCH_LOCK_ENTRY-unset}" "${LC_ALL-unset}" >> "$LOG"
EOF
cat > "${tmp:?}/check-a.sh" << 'EOF'
#!/bin/sh
printf 'check token=%s entry=%s\n' "${BENCH_LOCK_TOKEN:-none}" "${BENCH_LOCK_ENTRY-unset}" >> "$LOG"
EOF
# A compiler that coverage.sh takes for gcc, and its gcov: the run goes
# through the instrumented make and then fails for want of coverage data.
printf '#!/bin/sh\necho "gcc version 0 (stub)"\n' > "${tmp:?}/cov/cc"
printf '#!/bin/sh\nexit 0\n' > "${tmp:?}/cov/gcov"
# A make that logs its pid and process group, then runs for 30 s: a make
# for a KILL to land in.
mkdir -p "${tmp:?}/mk-slow"
cat > "${tmp:?}/mk-slow/make" << 'EOF'
#!/bin/sh
printf 'make %s pid=%s pgid=%s\n' "$*" "$$" "$(ps -o pgid= -p "$$" | tr -d ' ')" >> "$LOG"
sleep 30
echo make-end >> "$LOG"
EOF
chmod +x "${tmp:?}/mk/make" "${tmp:?}/mk-slow/make" "${tmp:?}/cov/cc" "${tmp:?}/cov/gcov" "${tmp:?}/check-a.sh" "${tmp:?}/stub-bench.sh"
# GATEPATH: the stub make; COVPATH: the stub compiler too; SLOWPATH: the
# slow make.
GATEPATH=${tmp:?}/mk:$PATH
COVPATH=${tmp:?}/cov:$GATEPATH
SLOWPATH=${tmp:?}/mk-slow:$PATH

# mktree <name> [nobench|real]: a throwaway tree T holding the entry points
# from SRC, a coverage floor and one gate check; beside the stub bench.sh,
# none (nobench) or the real bench harness (real). LOG is its call log.
mktree() {
	T=${tmp:?}/$1
	rm -rf "${T:?}"
	mkdir -p "${T:?}/scripts/pins" "${T:?}/tests"
	cp "$SRC/test.sh" "$SRC/coverage.sh" "$SRC/pins.sh" "$SRC/gate-checks.sh" "${T:?}/scripts/"
	printf 'gcov 0\nllvm-cov 0\n' > "${T:?}/scripts/coverage-floor"
	cp "${tmp:?}/check-a.sh" "${T:?}/tests/check-a.sh"
	# A quick fake for each check that the gate's two lists name: the
	# scheduler (scripts/gate-checks.sh) refuses a list word that names no
	# check.
	for _n in $(sed -n -e 's/^gate_first="\(.*\)"$/\1/p' -e 's/^gate_serial="\(.*\)"$/\1/p' "$SRC/test.sh"); do
		printf '#!/bin/sh\nexit 0\n' > "${T:?}/tests/$_n"
		chmod +x "${T:?}/tests/$_n"
	done
	case ${2:-stub} in
	stub)
		mkdir -p "${T:?}/scripts/bench"
		cp "${tmp:?}/stub-bench.sh" "${T:?}/scripts/bench/bench.sh"
		;;
	real) cp -R "$REAL/bench" "${T:?}/scripts/bench" ;;
	esac
	LOG=${T:?}/log
	: > "$LOG"
	export LOG
}
# lines <pattern>: the number of lines of LOG that match the grep pattern.
lines() {
	grep -c -- "$1" "${LOG:?}" || true
}
# say: the log and the output, one line, for a failure message.
say() {
	printf 'log: %s out: %s' "$(tr '\n' '|' < "${LOG:?}")" "$(sed -n '1,4p' "${T:?}/out" | tr '\n' '|')"
}

# gate_ok <token>: the gate ran its check and both makes, each under
# <token> (none: no lock), none of them with BENCH_LOCK_ENTRY.
gate_ok() {
	[ "$(lines "^check token=$1 entry=unset\$")" -eq 1 ] || return 1
	[ "$(lines "^make -B -j[0-9]* server game token=$1 entry=unset ")" -eq 1 ] || return 1
	[ "$(lines "^make -B -j[0-9]* UPDATE_PINS= TEST_ARGS= test token=$1 entry=unset ")" -eq 1 ] || return 1
	[ "$(lines '^make ')" -eq 2 ]
}

t_gate() {
	mktree gate
	run 60 env PATH="$GATEPATH" sh "${T:?}/scripts/test.sh"
	[ "$ST" -eq 0 ] || bad "gate: exited $ST; $(say)"
	[ "$(lines '^bench ')" -eq 1 ] || bad "gate: not one call of bench.sh; $(say)"
	case $(sed -n 1p "${LOG:?}") in
	"bench lock run gate -- env LC_ALL=C BENCH_LOCK_ENTRY=gate sh "*"/scripts/test.sh token=none") ;;
	*) bad "gate: the lock is not taken first, as gate, by the gate itself; $(say)" ;;
	esac
	gate_ok stub || bad "gate: the checks and the makes did not all run under the lock; $(say)"
	# A BENCH_LOCK_ENTRY without a token is no hold: the gate takes the lock.
	mktree gate-forged
	run 60 env PATH="$GATEPATH" BENCH_LOCK_ENTRY=gate sh "${T:?}/scripts/test.sh"
	[ "$ST" -eq 0 ] && [ "$(lines '^bench lock run gate ')" -eq 1 ] && gate_ok stub ||
		bad "gate: BENCH_LOCK_ENTRY=gate without a token skipped the lock; $(say)"
	[ "$fails" -eq 0 ]
}

t_coverage() {
	mktree coverage
	run 60 env PATH="$COVPATH" sh "${T:?}/scripts/coverage.sh"
	grep -q 'coverage: no coverage data was produced' "${T:?}/out" || bad "coverage: the run did not get past the instrumented make (status $ST); $(say)"
	[ "$(lines '^bench ')" -eq 1 ] || bad "coverage: not one call of bench.sh; $(say)"
	case $(sed -n 1p "${LOG:?}") in
	"bench lock run coverage -- env LC_ALL=C BENCH_LOCK_ENTRY=coverage sh "*"/scripts/coverage.sh token=none") ;;
	*) bad "coverage: the lock is not taken first, as coverage; $(say)" ;;
	esac
	[ "$(lines '^make -B -j[0-9]* COVERAGE=1 CC=cc UPDATE_PINS= TEST_ARGS= test token=stub entry=unset ')" -eq 1 ] ||
		bad "coverage: the instrumented make did not run under the lock; $(say)"
	[ "$fails" -eq 0 ]
}

# pins_locked <label> <message> <args...>: pins.sh <args> makes one call of
# bench.sh, as <label>, with its whole command line, and the run under it
# reaches the command (which refuses the bad name it was given, <message>).
pins_locked() {
	_pl=$1
	_pm=$2
	shift 2
	mktree "pins-$_pl"
	run 60 /bin/bash "${T:?}/scripts/pins.sh" "$@"
	[ "$(lines '^bench ')" -eq 1 ] || { bad "pins $*: not one call of bench.sh; $(say)"; return 0; }
	case $(sed -n 1p "${LOG:?}") in
	"bench lock run $_pl -- env BENCH_LOCK_ENTRY=$_pl /bin/bash "*"/scripts/pins.sh $* token=none") ;;
	*) bad "pins $*: the lock is not taken as $_pl with the whole command line; $(say)" ;;
	esac
	[ "$ST" -eq 1 ] && grep -q "^pins: $_pm" "${T:?}/out" || bad "pins $*: the run under the lock did not reach the command (status $ST); $(say)"
}

t_pins() {
	PINS_STORE=${tmp:?}/pins-store
	export PINS_STORE
	pins_locked pins-record 'renderer must match' record 'BAD!' world
	pins_locked pins-compare 'renderer must match' compare 'BAD!' world
	pins_locked pins-replay 'renderer must match' replay 'BAD!' world
	pins_locked pins-footprint 'scenario must match' footprint 'BAD!' --off a --on b
	pins_locked pins-where 'scenario must match' where 'BAD!' --off a --on b --out c
	pins_locked pins-make-demo 'demo must match' make-demo 'BAD!'
	pins_locked pins-selftest 'cannot build framediff' selftest no-such-case
	pins_locked pins-evidence 'renderer must match' evidence 'BAD!' world
	# A dry run builds and runs nothing, and the usage text is no command.
	mktree pins-dry
	run 60 env PINS_DRYRUN=1 /bin/bash "${T:?}/scripts/pins.sh" record 'BAD!' world
	[ "$(lines '^bench ')" -eq 0 ] && grep -q '^pins: renderer must match' "${T:?}/out" || bad "pins: a dry run took the lock; $(say)"
	: > "${LOG:?}"
	run 60 /bin/bash "${T:?}/scripts/pins.sh"
	[ "$ST" -ne 0 ] && [ "$(lines '^bench ')" -eq 0 ] || bad "pins: the usage text took the lock (status $ST); $(say)"
	unset PINS_STORE
	[ "$fails" -eq 0 ]
}

# said <name> <reason>: the output holds one line saying the lock is off,
# "<name>: the host lock is off: <reason>", and no other such line.
said() {
	[ "$(grep -c 'the host lock is off' "${T:?}/out")" -eq 1 ] &&
		grep -qxF "$1: the host lock is off: $2" "${T:?}/out"
}
# off_case <tag> <stub|nobench> <reason> <env argument...>: with the env
# arguments (options first) each entry point takes no lock, says so once on
# stderr naming <reason>, and still runs its heavy part.
off_case() {
	_tag=$1 _kind=$2 _why=$3
	shift 3
	mktree "off-gate-$_tag" "$_kind"
	run 60 env "$@" PATH="$GATEPATH" sh "${T:?}/scripts/test.sh"
	[ "$ST" -eq 0 ] && [ "$(lines '^bench ')" -eq 0 ] && gate_ok none ||
		bad "off ($_tag): the gate took the lock, or did not run its heavy part (status $ST); $(say)"
	said test.sh "$_why" || bad "off ($_tag): the gate did not say once that the lock is off ($_why); $(say)"
	mktree "off-coverage-$_tag" "$_kind"
	run 60 env "$@" PATH="$COVPATH" sh "${T:?}/scripts/coverage.sh"
	[ "$(lines '^bench ')" -eq 0 ] && [ "$(lines '^make .* COVERAGE=1 .* token=none ')" -eq 1 ] ||
		bad "off ($_tag): coverage took the lock, or did not run its make (status $ST); $(say)"
	said coverage.sh "$_why" || bad "off ($_tag): coverage did not say once that the lock is off ($_why); $(say)"
	mktree "off-pins-$_tag" "$_kind"
	run 60 env "$@" /bin/bash "${T:?}/scripts/pins.sh" record 'BAD!' world
	[ "$(lines '^bench ')" -eq 0 ] && grep -q '^pins: renderer must match' "${T:?}/out" ||
		bad "off ($_tag): pins.sh record took the lock, or did not run (status $ST); $(say)"
	said pins "$_why" || bad "off ($_tag): pins.sh did not say once that the lock is off ($_why); $(say)"
}
# on_case <tag> <env argument...>: with the env arguments each entry point
# still takes the lock (one call of bench.sh) and says nothing about it.
on_case() {
	_tag=$1
	shift
	mktree "on-gate-$_tag"
	run 60 env "$@" PATH="$GATEPATH" sh "${T:?}/scripts/test.sh"
	[ "$ST" -eq 0 ] && [ "$(lines '^bench lock run gate ')" -eq 1 ] && gate_ok stub &&
		! grep -q 'the host lock is off' "${T:?}/out" || bad "on ($_tag): the gate did not take the lock; $(say)"
	mktree "on-coverage-$_tag"
	run 60 env "$@" PATH="$COVPATH" sh "${T:?}/scripts/coverage.sh"
	[ "$(lines '^bench lock run coverage ')" -eq 1 ] && ! grep -q 'the host lock is off' "${T:?}/out" ||
		bad "on ($_tag): coverage did not take the lock; $(say)"
	mktree "on-pins-$_tag"
	run 60 env "$@" /bin/bash "${T:?}/scripts/pins.sh" record 'BAD!' world
	[ "$(lines '^bench lock run pins-record ')" -eq 1 ] && ! grep -q 'the host lock is off' "${T:?}/out" ||
		bad "on ($_tag): pins.sh record did not take the lock; $(say)"
}

t_off() {
	PINS_STORE=${tmp:?}/pins-store
	export PINS_STORE
	off_case lock0 stub 'BENCH_LOCK=0' BENCH_LOCK=0
	off_case ci stub 'CI (GITHUB_ACTIONS=true)' CI=true GITHUB_ACTIONS=true
	off_case nobench nobench 'no bench core (scripts/bench/bench.sh)' X_UNUSED=1
	off_case nostate stub 'no state directory (BENCH_STATE, XDG_CACHE_HOME and HOME unset)' -u HOME -u BENCH_STATE -u XDG_CACHE_HOME
	# CI is GitHub Actions, which sets both: the generic CI=true alone, or
	# GITHUB_ACTIONS=true alone, leaves the lock on; so does a state
	# directory from BENCH_STATE or XDG_CACHE_HOME alone.
	on_case ci-only CI=true
	on_case gha-only GITHUB_ACTIONS=true
	on_case state-only -u HOME -u XDG_CACHE_HOME BENCH_STATE=${tmp:?}/no-such-state
	on_case xdg-only -u HOME -u BENCH_STATE XDG_CACHE_HOME=${tmp:?}/no-such-cache
	unset PINS_STORE
	[ "$fails" -eq 0 ]
}

t_nested() {
	mktree nested real
	cat > "${T:?}/tests/check-a.sh" << 'EOF'
#!/bin/sh
printf 'check token=%s entry=%s lc=%s\n' "${BENCH_LOCK_TOKEN:+set}" "${BENCH_LOCK_ENTRY-unset}" "${LC_ALL-unset}" >> "$LOG"
# who started the scheduler (this check's parent), and who started that
_sp=$(ps -o ppid= -p "$PPID" | tr -d ' ')
printf 'sched-parent=%s\n' "$(ps -o args= -p "$_sp")" >> "$LOG"
printf 'sched-grandparent=%s\n' "$(ps -o args= -p "$(ps -o ppid= -p "$_sp" | tr -d ' ')")" >> "$LOG"
# the children of the run under the lock beside the scheduler (pid:group),
# and the groups of this check and of the scheduler
printf 'kids=%s\n' "$(ps -A -o pid= -o ppid= -o pgid= | awk -v p="$_sp" -v s="$PPID" '$2 == p && $1 != s { printf "%s:%s ", $1, $3 }')" >> "$LOG"
printf 'groups=%s:%s\n' "$(ps -o pgid= -p "$$" | tr -d ' ')" "$(ps -o pgid= -p "$PPID" | tr -d ' ')" >> "$LOG"
sh scripts/bench/bench.sh lock status >> "$LOG"
st=0
/bin/bash scripts/pins.sh selftest no-such-case >> "$LOG" 2>&1 || st=$?
printf 'pins status=%s\n' "$st" >> "$LOG"
EOF
	BENCH_STATE=${T:?}/state
	BENCH_LOCK_POLL=0.1
	BENCH_LOCK_WAIT=5
	export BENCH_STATE BENCH_LOCK_POLL BENCH_LOCK_WAIT
	_b=${T:?}/scripts/bench/bench.sh
	# The gate alone, LC_ALL unset: one hold, as gate; the selftest passes
	# straight through it.
	run 120 env -u LC_ALL PATH="$GATEPATH" sh "${T:?}/scripts/test.sh"
	[ "$ST" -eq 0 ] || bad "nested: the gate exited $ST; $(say)"
	grep -qx 'check token=set entry=unset lc=unset' "${LOG:?}" || bad "nested: the check did not run under the lock with the caller's LC_ALL (unset); $(say)"
	grep -q '^lock=held label=gate .*alive=yes' "${LOG:?}" || bad "nested: the gate does not hold the lock while its checks run; $(say)"
	# The lock shell sits above the gate, not between the gate and its
	# scheduler: the scheduler's parent, which GATE_PARENT must name (the
	# scheduler ends every check at its first poll otherwise), is the run of
	# test.sh under the lock, and that run's parent is the lock shell.
	grep -qx "sched-parent=sh ${T:?}/scripts/test.sh" "${LOG:?}" &&
		grep -q '^sched-grandparent=sh scripts/bench/bench.sh lock run gate -- env ' "${LOG:?}" ||
		bad "nested: the scheduler was not started by the run of test.sh under the lock; $(say)"
	# Its one other child is the watcher of its lock shell: the leader of a
	# process group of its own (no check's, not the scheduler's), and gone
	# once the gate has returned.
	_wk=$(sed -n 's/^kids=//p' "${LOG:?}")
	# shellcheck disable=SC2086 # a list of pid:group words
	set -- $_wk
	if [ "$#" -ne 1 ]; then
		bad "nested: the run under the lock has not one watcher beside its scheduler ($_wk); $(say)"
	else
		_wp=${1%%:*}
		case ":$(sed -n 's/^groups=//p' "${LOG:?}"):" in
		*":${1#*:}:"*) bad "nested: the watcher shares a process group with a check or the scheduler; $(say)" ;;
		esac
		[ "${1#*:}" = "$_wp" ] || bad "nested: the watcher does not lead a process group of its own; $(say)"
		! pid_running "$_wp" || bad "nested: the watcher (pid $_wp) outlived the gate"
	fi
	grep -qx 'pins status=1' "${LOG:?}" && grep -q '^pins: cannot build framediff' "${LOG:?}" ||
		bad "nested: the pin selftest under the gate did not pass straight through to its command; $(say)"
	grep -q 'waiting for the lock' "${LOG:?}" "${T:?}/out" && bad "nested: a call under the gate waited for the lock; $(say)"
	[ "$(lines '^make .* token=t-')" -eq 2 ] || bad "nested: the makes did not run under the lock; $(say)"
	[ "$(sh "$_b" lock status)" = lock=free ] || bad "nested: the lock is not free after the gate"
	# LC_ALL=POSIX: the run under the lock has it back.
	: > "${LOG:?}"
	run 120 env LC_ALL=POSIX PATH="$GATEPATH" sh "${T:?}/scripts/test.sh"
	[ "$ST" -eq 0 ] && grep -qx 'check token=set entry=unset lc=POSIX' "${LOG:?}" ||
		bad "nested: the check did not get the caller's LC_ALL (POSIX; status $ST); $(say)"
	# Under an outer holder (a host's own wrapper): the gate passes straight
	# through, and the selftest with it.
	: > "${LOG:?}"
	run 120 env PATH="$GATEPATH" sh "$_b" lock run host -- sh "${T:?}/scripts/test.sh"
	[ "$ST" -eq 0 ] || bad "nested: the gate under an outer holder exited $ST; $(say)"
	grep -q '^lock=held label=host .*alive=yes' "${LOG:?}" || bad "nested: under an outer holder the gate took a hold of its own; $(say)"
	grep -qx 'pins status=1' "${LOG:?}" || bad "nested: the pin selftest under an outer holder failed; $(say)"
	# The lock held by another job: the gate waits, gives up with 75 and
	# runs nothing.
	: > "${LOG:?}"
	sh "$_b" lock run other -- sleep 60 > /dev/null 2>&1 &
	_hp=$!
	settle "$_hp"
	record "$_hp"
	_n=0
	until sh "$_b" lock status | grep -q '^lock=held label=other .*child=[0-9]'; do
		[ "$_n" -lt 100 ] || break
		sleep 0.1
		_n=$((_n + 1))
	done
	run 60 env BENCH_LOCK_WAIT=1 PATH="$GATEPATH" sh "${T:?}/scripts/test.sh"
	[ "$ST" -eq 75 ] || bad "nested: a gate that found the lock held exited $ST, not 75; $(say)"
	grep -q 'held by other' "${T:?}/out" || bad "nested: the gate did not name the holder; $(say)"
	[ ! -s "${LOG:?}" ] || bad "nested: a gate without the lock ran its checks or makes; $(say)"
	reaped "$_hp" || true
	unset BENCH_STATE BENCH_LOCK_POLL BENCH_LOCK_WAIT
	[ "$fails" -eq 0 ]
}

# t_cov_lc, with the real bench core: coverage's instrumented make runs
# under the lock with the caller's LC_ALL, unset or set, though bench.sh
# sets it to C.
t_cov_lc() {
	mktree cov-lc real
	BENCH_STATE=${T:?}/state
	export BENCH_STATE
	run 120 env -u LC_ALL PATH="$COVPATH" sh "${T:?}/scripts/coverage.sh"
	[ "$(lines '^make .* COVERAGE=1 .* token=t-[^ ]* entry=unset lc=unset$')" -eq 1 ] ||
		bad "cov-lc: the instrumented make did not run under the lock with the caller's LC_ALL (unset); $(say)"
	: > "${LOG:?}"
	run 120 env LC_ALL=POSIX PATH="$COVPATH" sh "${T:?}/scripts/coverage.sh"
	[ "$(lines '^make .* COVERAGE=1 .* token=t-[^ ]* entry=unset lc=POSIX$')" -eq 1 ] ||
		bad "cov-lc: the instrumented make did not run under the lock with the caller's LC_ALL (POSIX); $(say)"
	[ "$(sh "${T:?}/scripts/bench/bench.sh" lock status)" = lock=free ] || bad "cov-lc: the lock is not free afterwards"
	unset BENCH_STATE
	[ "$fails" -eq 0 ]
}

# wait_line <seconds> <pattern>: waits until a line of LOG matches the grep
# pattern; fails past <seconds>.
wait_line() {
	_n=0
	until grep -q -- "$2" "${LOG:?}"; do
		[ "$_n" -lt $(($1 * 10)) ] || return 1
		sleep 0.1
		_n=$((_n + 1))
	done
	return 0
}
# lock_down: the lock in T is free, or its holder is stale (its lock shell,
# its command and that command's process group all gone; the next taker
# reclaims it).
lock_down() {
	case $(sh "${T:?}/scripts/bench/bench.sh" lock status) in
	lock=free | *' alive=no '*) return 0 ;;
	esac
	return 1
}
# pid_start <pid>: the identity of <pid> (pid_id's); empty when it does not
# run or ps gives no answer.
pid_start() {
	pid_id "$1" || true
}
# stray_stop <pid> <start> <text> [<group>]: what a failed test left
# running is ended, so that it does not outlive this check: TERM to <pid>
# (to the process group <group> when given, which must be <pid>'s and not
# this check's own), only while <pid> runs, its identity is still <start>
# (pid_start's when the pid was read) and its command line holds <text>;
# then up to 15 s for it to go.
stray_stop() {
	pid_running "$1" || return 0
	pid_same "$1" "$2" || return 0
	case $(ps -o args= -p "$1" 2> /dev/null) in *"$3"*) ;; *) return 0 ;; esac
	if [ -n "${4:-}" ]; then
		[ "$(ps -o pgid= -p "$1" | tr -d ' ')" = "$4" ] && [ "$4" != "$(ps -o pgid= -p "$$" | tr -d ' ')" ] || return 0
		kill -TERM -- "-$4" 2> /dev/null || true
	else
		kill -TERM "$1" 2> /dev/null || true
	fi
	wait_gone 15 "$1" || true
}

# t_kgrp, with the real bench core (the lock on): a KILL to the gate's
# process group reaches its lock shell alone (the run under the lock leads a
# group of its own). The run of test.sh under the lock must see its lock
# shell go and stop: every check ends and no make runs; a KILL during the
# makes ends the make as well. The lock is then free, or stale.
t_kgrp() {
	mktree kgrp real
	cat > "${T:?}/tests/check-a.sh" << 'EOF'
#!/bin/sh
echo "start $$ $PPID" >> "$LOG"
sleep 30
echo "end a" >> "$LOG"
EOF
	BENCH_STATE=${T:?}/state
	export BENCH_STATE
	# a KILL while a check runs: the check (pid $1) and the scheduler (its
	# parent, $2) end within a few seconds, and no make runs
	set -m
	env GATE_JOBS=1 PATH="$GATEPATH" sh "${T:?}/scripts/test.sh" > "${T:?}/out" 2>&1 &
	_kp=$!
	set +m
	settle "$_kp"
	record "$_kp"
	wait_line 60 '^start ' || bad "kgrp: the check did not start; $(say)"
	kill -KILL -- "-$_kp" 2> /dev/null || true
	wait "$_kp" 2> /dev/null || true
	forget "$_kp"
	# shellcheck disable=SC2046 # the check's two pids
	set -- $(sed -n 's/^start //p' "${LOG:?}") 0 0
	_ks=$(pid_start "$2")
	if ! wait_gone 12 "$1" || ! wait_gone 3 "$2"; then
		stray_stop "$2" "$_ks" gate-checks.sh
		bad "kgrp: a check or the scheduler outlived a KILL to the gate's process group under the lock; $(say)"
	fi
	_n=0
	until lock_down; do
		[ "$_n" -lt 100 ] || break
		sleep 0.1
		_n=$((_n + 1))
	done
	lock_down || bad "kgrp: the lock is still held, by a live holder, after a KILL to the gate's process group; $(sh "${T:?}/scripts/bench/bench.sh" lock status)"
	[ "$(lines '^make ')" -eq 0 ] || bad "kgrp: a make ran after a KILL to the gate's process group; $(say)"
	grep -q '^test.sh: the lock shell .* is gone' "${T:?}/out" || bad "kgrp: the gate did not say why it stopped; $(say)"
	# a KILL during the first make: the make ends, and the second never runs
	mktree kgrp-make real
	BENCH_STATE=${T:?}/state
	set -m
	env GATE_JOBS=1 PATH="$SLOWPATH" sh "${T:?}/scripts/test.sh" > "${T:?}/out" 2>&1 &
	_kp=$!
	set +m
	settle "$_kp"
	record "$_kp"
	wait_line 60 '^make ' || bad "kgrp: the first make did not start; $(say)"
	kill -KILL -- "-$_kp" 2> /dev/null || true
	wait "$_kp" 2> /dev/null || true
	forget "$_kp"
	_mp=$(sed -n 's/^make .* pid=\([0-9]*\) pgid=.*/\1/p' "${LOG:?}")
	_mg=$(sed -n 's/^make .* pgid=\([0-9]*\)$/\1/p' "${LOG:?}")
	_ms=$(pid_start "${_mp:-0}")
	if ! wait_gone 12 "${_mp:-0}"; then
		stray_stop "${_mp:-0}" "$_ms" mk-slow/make "${_mg:-0}"
		bad "kgrp: a make outlived a KILL to the gate's process group under the lock; $(say)"
	fi
	_n=0
	until lock_down; do
		[ "$_n" -lt 100 ] || break
		sleep 0.1
		_n=$((_n + 1))
	done
	lock_down || bad "kgrp: the lock is still held, by a live holder, after a KILL during the makes; $(sh "${T:?}/scripts/bench/bench.sh" lock status)"
	[ "$(lines '^make ')" -eq 1 ] && [ "$(lines '^make-end')" -eq 0 ] || bad "kgrp: a make ran on after a KILL during the makes; $(say)"
	unset BENCH_STATE
	[ "$fails" -eq 0 ]
}

# t_grace, with the real bench core: a TERM to the lock shell, with
# BENCH_STOP_GRACE=1 from the caller and a check that ignores TERM. The
# scheduler KILLs that check 10 s after its TERM, so the lock's own grace
# must be longer: once the lock shell has exited, nothing the gate started
# runs on, and the lock is free. The run under the lock sees the caller's
# BENCH_STOP_GRACE.
t_grace() {
	mktree grace real
	cat > "${T:?}/tests/check-a.sh" << 'EOF'
#!/bin/sh
trap '' TERM
echo "start $$ $PPID grace=${BENCH_STOP_GRACE-unset}" >> "$LOG"
sleep 60
EOF
	BENCH_STATE=${T:?}/state
	export BENCH_STATE
	env GATE_JOBS=1 BENCH_STOP_GRACE=1 PATH="$GATEPATH" sh "${T:?}/scripts/test.sh" > "${T:?}/out" 2>&1 &
	_gp=$!
	settle "$_gp"
	record "$_gp"
	wait_line 60 '^start ' || bad "grace: the check did not start; $(say)"
	kill -TERM "$_gp" 2> /dev/null || true
	_gs=0
	if wait_gone 60 "$_gp"; then
		wait "$_gp" 2> /dev/null || _gs=$?
		forget "$_gp"
	else
		reaped "$_gp" || true
		_gs=$RS
	fi
	# shellcheck disable=SC2046 # the check's two pids
	set -- $(sed -n 's/^start \([0-9]*\) \([0-9]*\) .*/\1 \2/p' "${LOG:?}") 0 0
	_ga=
	pid_running "$1" && _ga="the check (it ignores TERM)"
	pid_running "$2" && _ga="${_ga:+$_ga and }the scheduler"
	wait_gone 15 "$1" || true
	[ -z "$_ga" ] || bad "grace: $_ga outlived the lock shell after a TERM to it, with BENCH_STOP_GRACE=1 (status $_gs); $(say)"
	[ "$(sh "${T:?}/scripts/bench/bench.sh" lock status)" = lock=free ] || bad "grace: the lock is not free after the lock shell exited"
	grep -q '^start .* grace=1$' "${LOG:?}" || bad "grace: the run under the lock did not get the caller's BENCH_STOP_GRACE (1) back; $(say)"
	[ "$_gs" -ne 0 ] || bad "grace: the gate exited 0 after a TERM; $(say)"
	unset BENCH_STATE
	[ "$fails" -eq 0 ]
}

# t_grace_refused, with the real bench core: a BENCH_STOP_GRACE that is not
# whole seconds by the core's rule (08, 10 digits, 20 digits) is not raised
# by the gate (the 9-digit cap is also bench.sh's own, so the gate's copy of
# it changes nothing a run can see), and bench.sh refuses it: status 2,
# the setting named, no check started. The check ends at once, so a gate
# that did start it ends too.
t_grace_refused() {
	mktree gracerefused real
	printf '#!/bin/sh\necho "start $$" >> "$LOG"\n' > "${T:?}/tests/check-a.sh"
	BENCH_STATE=${T:?}/state
	export BENCH_STATE
	for _gv in 08 1234567890 99999999999999999999; do
		: > "${LOG:?}"
		_gs=0
		env GATE_JOBS=1 BENCH_STOP_GRACE="$_gv" PATH="$GATEPATH" sh "${T:?}/scripts/test.sh" > "${T:?}/out" 2>&1 || _gs=$?
		[ "$_gs" -eq 2 ] || bad "grace-refused: BENCH_STOP_GRACE=$_gv gave status $_gs, not 2; $(say)"
		grep -q 'BENCH_STOP_GRACE is whole seconds' "${T:?}/out" || bad "grace-refused: BENCH_STOP_GRACE=$_gv: no refusal naming the setting; $(say)"
		! grep -q '^start ' "${LOG:?}" || bad "grace-refused: a check started under BENCH_STOP_GRACE=$_gv; $(say)"
	done
	unset BENCH_STATE
	[ "$fails" -eq 0 ]
}

# t_orphan, with the real bench core (the lock on): a KILL to the caller's
# process group that lands after bench_spawn has forked the run but before
# the run's shell has started leaves the run with launchd as its parent, so
# its lock shell reads as pid 1, which never goes, and the watcher would
# never signal. The run is started as bench_spawn starts it (the marker, a
# token, a process group of its own) by a parent that is gone before the
# run's shell starts. It must say so in one line and exit 143 before any
# check, the scheduler, a make or the watcher starts. No process but
# launchd can wait for it, so its status is read from its trace (sh -x).
t_orphan() {
	mktree orphan real
	cat > "${T:?}/tests/check-a.sh" << 'EOF'
#!/bin/sh
echo "check-start $$" >> "$LOG"
EOF
	# orphan.sh <pidfile> <command...>: writes its pid, waits (up to 10 s)
	# until its parent is gone, then becomes <command>, whose parent at start
	# is therefore pid 1.
	cat > "${T:?}/orphan.sh" << 'EOF'
#!/bin/sh
echo "$$" > "$1"
shift
_n=0
until [ "$(ps -o ppid= -p "$$" | tr -d ' ')" = 1 ]; do
	[ "$_n" -lt 100 ] || { echo "orphan: the parent is still there" >&2; exit 97; }
	sleep 0.1
	_n=$((_n + 1))
done
exec "$@"
EOF
	BENCH_STATE=${T:?}/state
	export BENCH_STATE
	# the spawning shell exits at once; the run inherits ${T}/out
	run 30 sh -c 'set -m; sh "$1" "$2" env BENCH_LOCK_ENTRY=gate BENCH_LOCK_TOKEN=t-orphan GATE_JOBS=1 PS4="+ " PATH="$3" sh -x "$4" < /dev/null & exit 0' \
		spawn "${T:?}/orphan.sh" "${T:?}/run.pid" "$GATEPATH" "${T:?}/scripts/test.sh"
	[ "$ST" -eq 0 ] || bad "orphan: the spawning shell exited $ST; $(say)"
	_n=0
	until [ -s "${T:?}/run.pid" ]; do
		[ "$_n" -lt 100 ] || break
		sleep 0.1
		_n=$((_n + 1))
	done
	_op=$(cat "${T:?}/run.pid" 2> /dev/null) || _op=0
	_os=$(pid_start "${_op:=0}")
	record "$_op"
	if ! wait_gone 60 "$_op"; then
		stray_stop "$_op" "$_os" scripts/test.sh "$_op"
		bad "orphan: the run under the lock with its parent gone still runs after 60 s; $(say)"
	fi
	forget "$_op"
	if grep -q '^orphan: the parent is still there' "${T:?}/out"; then
		bad "orphan: the run's parent was not gone before the run started; $(say)"
	else
		grep -qx 'test.sh: the lock shell is gone before the gate started' "${T:?}/out" ||
			bad "orphan: the run did not say that its lock shell was gone; $(say)"
		[ "$(sed -n '$p' "${T:?}/out")" = '+ exit 143' ] || bad "orphan: the run did not exit 143 at once; $(say)"
		[ "$(lines '^check-start ')" -eq 0 ] || bad "orphan: a check started; $(say)"
		[ "$(lines '^make ')" -eq 0 ] || bad "orphan: a make ran; $(say)"
		! grep -q 'gate-checks\.sh\|gate-watch' "${T:?}/out" || bad "orphan: the scheduler or the watcher started; $(say)"
	fi
	unset BENCH_STATE
	[ "$fails" -eq 0 ]
}

# t_cleanup_id: cleanup's stop (stop_listed) signals a listed pid only while
# it still runs with the start time recorded there, and stray_stop only a
# pid whose start time is still the one read. Live processes listed with
# another's identity (its pid given to a stranger) or with their own command
# line but another start time (the pid reused by the same command) are left
# running, as are lines that name no pid, a pid recorded after it ended
# (listed with no identity) and a stray_stop with another start time. The
# one listed with its own identity is stopped, and so is one that exec'd
# another program after it was recorded (scripts/test.sh re-execs under the
# lock): the same start time with another command line is still the
# process this check started. The processes are shells that end on TERM
# (so that no job notice is printed), and by themselves within about 30 s,
# so no wait here outlives that even when TERM is ignored.
t_cleanup_id() {
	_l=${tmp:?}/cleanup-id.list
	_lo=${tmp:?}/cleanup-id.own
	_lp=${tmp:?}/cleanup-id.loop
	: > "${_l:?}"
	: > "${_lo:?}"
	printf '%s\n' 'trap "exit 0" TERM' 'n=0' 'while [ "$n" -lt 300 ]; do sleep 0.1; n=$((n + 1)); done' > "${_lp:?}"
	set --
	# $1 to $4 the loop; $5 runs its sleep 1 when recorded, then execs it
	for _q in x y k1 k2 e; do
		case $_q in
		e) sh -c 'sleep 1; exec sh "$0" "$1"' "${_lp:?}" cleanup-id-e & ;;
		*) sh "${_lp:?}" "cleanup-id-$_q" & ;;
		esac
		set -- "$@" "$!"
		settle "$!"
		record "$!"
	done
	record "$5" "${_l:?}"
	case $(ps -o args= -p "$5" 2> /dev/null) in
	*'exec sh'*) ;;
	*) bad "cleanup-id: the exec case had exec'd, or ps gave no answer for it, before it was recorded" ;;
	esac
	record "$1" "${_lo:?}"
	record "$2" "${_lo:?}"
	true &
	_d=$!
	wait "$_d" || true
	_o=$(pid_id 1) || bad "cleanup-id: ps gave no start time for pid 1"
	printf '%s %s\n' "$1" "$_o" >> "${_l:?}"
	printf '%s %s\n' "$2" "$(id_of 'Thu Jan  1 00:00:00 2026')" >> "${_l:?}"
	printf '%s\n' '' x "0 $(pid_id "$1")" "$1" >> "${_l:?}"
	record "$_d" "${_l:?}"
	grep -qx "$_d " "${_l:?}" || bad "cleanup-id: a pid recorded after it ended is not on the list with no identity"
	record "$3" "${_l:?}"
	_n=0
	while [ "$_n" -lt 50 ]; do
		case $(ps -o args= -p "$5" 2> /dev/null) in
		*'exec sh'*) ;;
		*) break ;;
		esac
		sleep 0.1
		_n=$((_n + 1))
	done
	case $(ps -o args= -p "$5" 2> /dev/null) in
	*'exec sh'*) bad "cleanup-id: the exec case did not exec within 5 s" ;;
	*cleanup-id-e*) ;;
	*) bad "cleanup-id: ps gave no answer for the exec case" ;;
	esac
	record "$5" "${_lo:?}"
	stop_listed "${_l:?}"
	wait_gone 10 "$3" || bad "cleanup-id: the process listed with its own identity was not stopped"
	wait_gone 10 "$5" || bad "cleanup-id: a process this check started that exec'd after it was recorded was left running"
	stray_stop "$4" "$(id_of 'Thu Jan  1 00:00:00 2026')" cleanup-id-k2
	sleep 0.5
	pid_running "$1" || bad "cleanup-id: a process listed with another's identity was signalled"
	pid_running "$2" || bad "cleanup-id: a process listed with another start time was signalled"
	pid_running "$4" || bad "cleanup-id: stray_stop signalled a process with another start time"
	stray_stop "$4" "$(pid_start "$4")" cleanup-id-k2
	pid_running "$4" && bad "cleanup-id: stray_stop did not stop a process with the start time read"
	stop_listed "${_lo:?}"
	for _q in "$@"; do
		wait_gone 40 "$_q" || bad "cleanup-id: pid $_q outlived its own bound"
		wait "$_q" 2> /dev/null || true
		forget "$_q"
	done
	[ "$fails" -eq 0 ]
}

# t_reap_id: reap's TERM and its KILL each go only while the pid still has
# the identity it was recorded with on the cleanup list (FIX171). The
# check's own processes stand in for a pid the system gave to another
# process: shells that note a TERM and go on (so that only a KILL ends
# them), and end by themselves once the stop file is there, or within
# about 30 s. Listed with another's identity, one gets no TERM (t) and one
# no KILL (k), and reap says so; listed with no identity (none), not listed
# (off), or with ps giving no start time (ps), one gets neither; listed
# with its own identity (own), one gets the TERM and then the KILL.
t_reap_id() {
	_rd=${tmp:?}/reap-id
	rm -rf "${_rd:?}"
	mkdir -p "${_rd:?}"
	printf '%s\n' 'trap "echo TERM >> \"\$1\"" TERM' 'n=0' 'while [ ! -e "$2" ] && [ "$n" -lt 300 ]; do sleep 0.1; n=$((n + 1)); done' > "${_rd:?}/deaf.sh"
	set --
	for _q in t k none off ps; do
		sh "${_rd:?}/deaf.sh" "${_rd:?}/$_q.sig" "${_rd:?}/stop" &
		set -- "$@" "$!"
		settle "$!"
		record "$!"
	done
	_o=$(pid_id 1) || bad "reap-id: ps gave no start time for pid 1"
	for _q in "$1" "$3" "$4"; do forget "$_q"; done
	printf '%s %s\n' "$1" "$_o" >> "${tmp:?}/pids"
	printf '%s \n' "$3" >> "${tmp:?}/pids"
	# A reap that sends no signal to a pid that still runs is a failure and
	# false. Each runs in a subshell, so that its failure is checked here
	# and not counted.
	_rs=0
	(reap "$1" 1) > "${_rd:?}/t.out" || _rs=$?
	[ ! -s "${_rd:?}/t.sig" ] || bad "reap-id: a TERM went to a pid listed with another identity"
	grep -q "reap: no TERM to pid $1:" "${_rd:?}/t.out" && [ "$_rs" -ne 0 ] || bad "reap-id: the TERM not sent was not a failure (status $_rs): $(cat "${_rd:?}/t.out")"
	# k: listed with its own identity, with ps giving no start time from the
	# second time it is asked (after the TERM, for the KILL)
	_rs=0
	(
		ps() {
			case " $* " in
			*' lstart= '*)
				echo x >> "${_rd:?}/k.ps"
				[ $(($(wc -l < "${_rd:?}/k.ps"))) -le 1 ] || return 1
				;;
			esac
			command ps "$@"
		}
		reap "$2" 1
	) > "${_rd:?}/k.out" || _rs=$?
	grep -qx TERM "${_rd:?}/k.sig" 2> /dev/null || bad "reap-id: no TERM went to a pid listed with its own identity (k)"
	pid_running "$2" || bad "reap-id: a KILL went to a pid that ps gave no start time for after its TERM"
	grep -q "reap: no KILL to pid $2:" "${_rd:?}/k.out" && [ "$_rs" -ne 0 ] || bad "reap-id: the KILL not sent was not a failure (status $_rs): $(cat "${_rd:?}/k.out")"
	for _q in none off; do
		if [ "$_q" = none ]; then _p=$3; else _p=$4; fi
		_rs=0
		(reap "$_p" 1) > "${_rd:?}/$_q.out" || _rs=$?
		[ ! -s "${_rd:?}/$_q.sig" ] && pid_running "$_p" || bad "reap-id: a signal went to a pid listed as $_q"
		grep -q "reap: no TERM to pid $_p:" "${_rd:?}/$_q.out" && [ "$_rs" -ne 0 ] || bad "reap-id: $_q: the TERM not sent was not a failure (status $_rs): $(cat "${_rd:?}/$_q.out")"
	done
	_rs=0
	(
		ps() {
			case " $* " in
			*' lstart= '*) return 1 ;;
			esac
			command ps "$@"
		}
		reap "$5" 1
	) > "${_rd:?}/ps.out" || _rs=$?
	[ ! -s "${_rd:?}/ps.sig" ] && pid_running "$5" || bad "reap-id: a signal went to a pid that ps gave no start time for"
	grep -q "reap: no TERM to pid $5:" "${_rd:?}/ps.out" && [ "$_rs" -ne 0 ] || bad "reap-id: the signals not sent with no answer from ps were not a failure (status $_rs): $(cat "${_rd:?}/ps.out")"
	# own: started, reaped and collected in a subshell with no standard
	# error, so that the shell's job notice for the KILL is not printed
	_f0=$fails
	(
		sh "${_rd:?}/deaf.sh" "${_rd:?}/own.sig" "${_rd:?}/stop" &
		_op=$!
		settle "$_op"
		record "$_op"
		reap "$_op" 1 > /dev/null
		grep -qx TERM "${_rd:?}/own.sig" 2> /dev/null || bad "reap-id: no TERM went to a pid listed with its own identity"
		wait_gone 5 "$_op" || bad "reap-id: no KILL went to a pid listed with its own identity"
		wait "$_op" || true
		forget "$_op"
		[ "$fails" -eq "$_f0" ]
	) 2> /dev/null || bad "reap-id: the case listed with its own identity failed"
	: > "${_rd:?}/stop"
	for _q in "$@"; do
		wait_gone 40 "$_q" || bad "reap-id: pid $_q outlived its own bound"
		wait "$_q" 2> /dev/null || true
		forget "$_q"
	done
	[ "$fails" -eq 0 ]
}

# t_reap_wait: no caller of reap waits for a pid that reap did not end
# (FIX171). A child of the check's that ends only once the stop file is
# there (or after about 30 s) is listed with no identity (ps gives no start
# time the first time it is asked), or with another (ps gives another the
# first time), or while ps fails for every call (nops, FIX177: the check
# cannot tell, and reap must say so and send nothing). Each kind of caller,
# run, and reaped (which t_nested and t_grace call), must return within 10 s,
# count the failure, and take the pid as not ended (run's ST 124, reaped
# false). And no line
# outside reap, reap_skip, reaped and reap's own tests holds the word reap
# (reap_lint, checked on its own shapes): a caller that did could wait for
# its pid with nothing to bound the wait.
t_reap_wait() {
	_rd=${tmp:?}/reap-wait
	rm -rf "${_rd:?}"
	mkdir -p "${_rd:?}"
	printf '%s\n' 'trap "echo TERM >> \"\$1\"" TERM' 'n=0' 'while [ ! -e "$2" ] && [ "$n" -lt 300 ]; do sleep 0.1; n=$((n + 1)); done' > "${_rd:?}/deaf.sh"
	printf '%s\n' 'echo $$ > "$1"' 'exec sh "$2" "$3" "$4"' > "${_rd:?}/c.sh"
	for _k in run:none run:other run:nops reaped:none reaped:other reaped:nops; do
		rm -f "${_rd:?}/stop" "${_rd:?}/c.pid" "${_rd:?}/ps.n" "${_rd:?}/c.sig" "${_rd:?}/c.rv"
		(
			T=${_rd:?}
			ps() {
				# nops (FIX177): ps fails for every call, so the check
				# cannot tell whether the child runs.
				[ "${_k#*:}" != nops ] || return 1
				case " $* " in
				*' lstart= '*)
					echo x >> "${_rd:?}/ps.n"
					if [ $(($(wc -l < "${_rd:?}/ps.n"))) -le 1 ]; then
						[ "${_k#*:}" = other ] || return 1
						echo 'Thu Jan  1 00:00:00 2026'
						return 0
					fi
					;;
				esac
				command ps "$@"
			}
			_f0=$fails
			case ${_k%:*} in
			run)
				run 1 sh "${_rd:?}/c.sh" "${_rd:?}/c.pid" "${_rd:?}/deaf.sh" "${_rd:?}/c.sig" "${_rd:?}/stop"
				[ "$ST" -ne 124 ] || echo false > "${_rd:?}/c.rv"
				;;
			reaped)
				sh "${_rd:?}/c.sh" "${_rd:?}/c.pid" "${_rd:?}/deaf.sh" "${_rd:?}/c.sig" "${_rd:?}/stop" &
				settle "$!"
				record "$!"
				reaped "$!" 1 || echo false > "${_rd:?}/c.rv"
				;;
			esac
			[ "$fails" -gt "$_f0" ]
		) > "${_rd:?}/w.out" 2> /dev/null &
		_w=$!
		wait_gone 10 "$_w" || bad "reap-wait: $_k: the caller still waited 10 s after reap sent no signal"
		: > "${_rd:?}/stop"
		_ws=0
		wait "$_w" || _ws=$?
		[ "$_ws" -eq 0 ] || bad "reap-wait: $_k: the caller counted no failure: $(cat "${_rd:?}/w.out")"
		grep -q "reap: no TERM to pid " "${_rd:?}/w.out" || bad "reap-wait: $_k: reap did not say it sent no TERM: $(cat "${_rd:?}/w.out")"
		[ -s "${_rd:?}/c.rv" ] || bad "reap-wait: $_k: the caller took a pid reap did not end as ended (run's ST 124, reaped false)"
		if [ "${_k#*:}" = nops ]; then
			grep -q "so the check cannot tell whether it still runs" "${_rd:?}/w.out" || bad "reap-wait: $_k: reap did not say it cannot tell: $(cat "${_rd:?}/w.out")"
			[ ! -s "${_rd:?}/c.sig" ] || bad "reap-wait: $_k: a signal went to a pid that ps gave no answer for"
		fi
		_c=$(cat "${_rd:?}/c.pid" 2> /dev/null) || _c=
		wait_gone 5 "$_c" || bad "reap-wait: $_k: pid $_c outlived its stop file"
		forget "$_c"
	done
	_bare=$(reap_lint "$0" 'reap|reap_skip|reaped|reap_lint|t_reap_id|t_reap_wait')
	[ -z "$_bare" ] || bad "reap-wait: reap is called bare outside reaped, so a wait after it may have no bound: $_bare"
	# The lint itself (FIX171, round 2): each shape below calls reap in a
	# function after reaped's, and must be refused; reaped passes.
	lint_shape() {
		_ln=$1
		shift
		printf '%s\n' 'reaped() {' '	reap "$@"' '}' "$_ln() {" "$@" '}' > "${_rd:?}/lint.sh"
		[ -n "$(reap_lint "${_rd:?}/lint.sh" reaped)" ] || bad "reap-wait: the lint let through, in $_ln: $*"
	}
	while IFS= read -r _s; do
		lint_shape t_f "	$_s"
	done << 'SHAPES'
reap "$x"
reap "$x"; wait "$x"
reap "$x" || wait "$x"
reap "$x" 2>&1
reap "$_rp" 2>&1; wait "$_rp"
if :; then reap "$x"; fi
if :; then :; else reap "$x"; fi
{ reap "$x"; }
reap $x
eval reap "$x"
eval 'reap "$x"'
r=reap; $r "$x"
foo && reap "$x"
wait_gone 60 "$_gp" || reap "$_gp"
reap "$x" || true
SHAPES
	lint_shape t_f '	reap "$x" || true' '	wait "$x"'
	lint_shape t_f '	reap \' '		"$x"'
	lint_shape t_f '	re\' 'ap "$x"'
	lint_shape t_x2 '	reap "$x"'
	lint_shape Tx '	reap "$x"'
	printf '%s\n' 'reaped() {' '	reap "$@"' 't_x2() {' '	reap "$x"' '}' > "${_rd:?}/lint.sh"
	[ -n "$(reap_lint "${_rd:?}/lint.sh" reaped)" ] || bad "reap-wait: the lint let through a reap in t_x2() with no } before it"
	printf '%s\n' 't_f() {' '	reaped "$x" || true' '	reaped "$x" 5 || bad "x: $RS"' '	# then reap it' '}' > "${_rd:?}/lint.sh"
	_l=$(reap_lint "${_rd:?}/lint.sh" reaped)
	[ -z "$_l" ] || bad "reap-wait: the lint refused a plain reaped: $_l"
	[ "$fails" -eq 0 ]
}

# ---- The real entry points. ---------------------------------------------
all="t_cleanup_id t_reap_id t_reap_wait t_gate t_coverage t_cov_lc t_pins t_off t_nested t_kgrp t_grace t_grace_refused t_orphan"
only=
if [ "$#" -gt 0 ]; then
	all=$*
	only=1
fi
failed=
for _t in $all; do
	if ! (cd "$ROOT" && "$_t"); then
		failed="$failed $_t"
	fi
done
# Every test waits for what it started: the cleanup list is empty again.
if [ -s "${tmp:?}/pids" ]; then
	echo "$me: the cleanup list still holds pids the tests waited for: $(cut -d' ' -f1 "${tmp:?}/pids" | tr '\n' ' ')"
	failed="$failed cleanup-list"
fi
if [ -n "$failed" ]; then
	echo "$me: failed:$failed"
	exit 1
fi
if [ -n "$only" ]; then
	_ok=1
	echo "$me: ok ($all; no mutants)"
	exit 0
fi

# ---- Mutants: each must fail the test that targets it. -----------------
# lit <file> <old> <new>: replaces the first occurrence of <old> on each
# line, as fixed text; fails when there is none.
lit() {
	awk -v o="$2" -v n="$3" '{ p = index($0, o); if (p) { $0 = substr($0, 1, p - 1) n substr($0, p + length(o)); c++ } print } END { exit !c }' "$1" > "$1.new" &&
		mv "$1.new" "$1"
}
# mutant <name> <file under scripts> <old> <new> <test>
mutant() {
	_m=${tmp:?}/mutant.$1
	rm -rf "${_m:?}"
	mkdir -p "${_m:?}"
	cp "$REAL/test.sh" "$REAL/coverage.sh" "$REAL/pins.sh" "$REAL/gate-checks.sh" "${_m:?}/"
	lit "${_m:?}/$2" "$3" "$4" || { echo "$me: mutant $1 does not apply to $2"; return 1; }
	if (SRC=$_m MUTANT=1 && cd "$ROOT" && "$5") > "${tmp:?}/mutant.$1.out" 2>&1; then
		echo "$me: mutant $1 passed $5; the test does not catch it"
		return 1
	fi
	echo "$me: mutant $1: caught by $5 ($(sed -n 1p "${tmp:?}/mutant.$1.out" | sed "s/^$me: //" | cut -c1-120))"
}
mfail=0
mutant gate-no-lock test.sh 'exec sh scripts/bench/bench.sh lock run gate -- env "$@"' ':' t_gate || mfail=1
mutant coverage-no-lock coverage.sh 'exec sh scripts/bench/bench.sh lock run coverage -- env "$@"' ':' t_coverage || mfail=1
for _c in record replay make-demo; do
	mutant "pins-$_c-no-lock" pins.sh '[ -n "${PINS_DRYRUN:-}" ] || lock_self pins-'"$_c"' "$@"; ' '' t_pins || mfail=1
done
for _c in compare footprint where selftest evidence; do
	mutant "pins-$_c-no-lock" pins.sh 'lock_self pins-'"$_c"' "$@"; ' '' t_pins || mfail=1
done
# The run under the lock must go on, not take the lock again; and a
# BENCH_LOCK_ENTRY without a token is no hold.
mutant gate-relocks test.sh '[ "$lock_entry" != gate ]' 'true' t_gate || mfail=1
mutant gate-entry-no-token test.sh ' || [ -z "${BENCH_LOCK_TOKEN:-}" ]' '' t_gate || mfail=1
mutant pins-relocks pins.sh 'if [ "$entry" = "$label" ] && [ -n "${BENCH_LOCK_TOKEN:-}" ]; then return 0; fi' ':' t_pins || mfail=1
# The caller's LC_ALL, when it is unset, comes back unset under the lock.
mutant gate-lc-lost test.sh 'else set -- -u LC_ALL "$@"; fi' 'fi' t_nested || mfail=1
# GATE_PARENT names the run of test.sh under the lock, not the lock shell
# above it.
mutant gate-parent-lock-shell test.sh 'GATE_PARENT=$$ ' 'GATE_PARENT=$PPID ' t_nested || mfail=1
# The run under the lock watches its lock shell, and stops its whole process
# group (a make too) when the lock shell is gone.
mutant gate-no-watch test.sh 'sh -c "$gate_watch_sh" gate-watch' 'true' t_kgrp || mfail=1
mutant gate-watch-pid-only test.sh 'kill -s TERM -- "-$PPID" 2> /dev/null' 'kill -s TERM "$PPID" 2> /dev/null' t_kgrp || mfail=1
# A run whose lock shell was gone before it started (its parent is pid 1)
# starts nothing.
mutant gate-orphan-runs test.sh 'if [ "$gate_lock_shell" = 1 ]; then' 'if false; then' t_orphan || mfail=1
# The lock shell's grace covers the scheduler's, and the run under it gets
# the caller's BENCH_STOP_GRACE back.
mutant gate-grace-kept test.sh 'BENCH_STOP_GRACE=10' ':' t_grace || mfail=1
mutant gate-grace-lost test.sh 'set -- BENCH_STOP_GRACE="$BENCH_STOP_GRACE" "$@"' ':' t_grace || mfail=1
mutant gate-grace-old-rule test.sh "'' | *[!0-9]* | 0?*) ;;" '*[!0-9]*) ;;' t_grace_refused || mfail=1
mutant coverage-lc-lost coverage.sh 'else set -- -u LC_ALL "$@"; fi' 'fi' t_cov_lc || mfail=1
# With no state directory the lock is off; CI is GitHub Actions only; a
# lock that is off says so.
for _f in test.sh coverage.sh pins.sh; do
	mutant "$_f-nostate-locks" "$_f" 'elif [ -z "${BENCH_STATE:-}${XDG_CACHE_HOME:-}${HOME:-}" ]; then' 'elif false; then' t_off || mfail=1
	mutant "$_f-ci-generic" "$_f" 'elif [ "${CI:-}" = true ] && [ "${GITHUB_ACTIONS:-}" = true ]; then' 'elif [ "${CI:-}" = true ]; then' t_off || mfail=1
done
mutant test.sh-off-silent test.sh 'echo "test.sh: the host lock is off: $lock_off" >&2' ':' t_off || mfail=1
mutant coverage.sh-off-silent coverage.sh 'echo "coverage.sh: the host lock is off: $lock_off" >&2' ':' t_off || mfail=1
mutant pins.sh-off-silent pins.sh 'echo "pins: the host lock is off: $off" >&2' ':' t_off || mfail=1
[ "$mfail" -eq 0 ] || exit 1

_ok=1
echo "$me: ok"
