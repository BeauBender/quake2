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
# pid only while its start time and command line are still those recorded
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

pid_running() {
	case $1 in
	'' | 0 | *[!0-9]*) return 1 ;;
	esac
	_s=$(ps -o stat= -p "$1" 2> /dev/null) || return 1
	case $_s in
	'' | Z*) return 1 ;;
	esac
	return 0
}

# The cleanup list holds a line per process this check started, or read
# from a file, and has not seen end: "<pid> <identity>", the identity taken
# when the pid was put on it. A pid is signalled only while it is still that
# process, with the same start time and the same command line: one that has
# gone, or that ps gives no answer for, is not, since the system may have
# given its pid to another process by then (FIX171). The same primitive is
# in tests/check-bench.sh.
# pid_id <pid>: the identity of a running <pid>, a checksum of its start
# time and its command line as ps prints them; fails when it does not run or
# ps gives no answer for either.
pid_id() {
	pid_running "$1" || return 1
	_il=$(ps -o lstart= -p "$1" 2> /dev/null) || return 1
	_ia=$(ps -o args= -p "$1" 2> /dev/null) || return 1
	[ -n "$_il" ] && [ -n "$_ia" ] || return 1
	id_of "$_il" "$_ia"
}
# id_of <start time> <command line>: the identity of a process with them.
id_of() {
	printf '%s|%s\n' "$1" "$2" | cksum
}
# pid_same <pid> <identity>: <pid> runs and is still the process recorded
# with <identity>.
pid_same() {
	[ -n "${2:-}" ] || return 1
	_is=$(pid_id "$1") || return 1
	[ "$_is" = "$2" ]
}
# record <pid> [<list>]: puts a running <pid> on the cleanup list (or on
# <list>) with its identity now; a pid that does not run is not put on it.
record() {
	_ri=$(pid_id "$1") || return 0
	printf '%s %s\n' "$1" "$_ri" >> "${2:-${tmp:?}/pids}"
}
# settle <pid>: waits, at most 1 s, until a child this shell has just
# started has become its command, so that the identity recorded is the
# command's: until its exec the child has this shell's command line, and
# env's until env execs the command.
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
	while read -r _p _i; do
		if pid_same "$_p" "$_i"; then kill -TERM "$_p" 2> /dev/null || true; fi
	done < "$1"
}

cleanup() {
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
# wait_gone <seconds> <pid>
wait_gone() {
	_n=0
	while pid_running "$2"; do
		[ "$_n" -lt $(($1 * 10)) ] || return 1
		sleep 0.1
		_n=$((_n + 1))
	done
	return 0
}
# reap <pid>: ends a process this check started (TERM, which a lock
# shell's trap turns into a stop of its command; KILL after 30 s).
reap() {
	kill -TERM "$1" 2> /dev/null || true
	wait_gone 30 "$1" || kill -KILL "$1" 2> /dev/null || true
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
		reap "$_rp"
		wait "$_rp" 2> /dev/null || true
		forget "$_rp"
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
	reap "$_hp"
	wait "$_hp" 2> /dev/null || true
	forget "$_hp"
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
# pid_start <pid>: the start time of <pid> as ps prints it; empty when it
# does not run or ps gives no answer.
pid_start() {
	pid_running "$1" || return 0
	ps -o lstart= -p "$1" 2> /dev/null || true
}
# stray_stop <pid> <start> <text> [<group>]: what a failed test left
# running is ended, so that it does not outlive this check: TERM to <pid>
# (to the process group <group> when given, which must be <pid>'s and not
# this check's own), only while <pid> runs, its start time is still <start>
# (pid_start's when the pid was read) and its command line holds <text>;
# then up to 15 s for it to go.
stray_stop() {
	pid_running "$1" || return 0
	[ -n "$2" ] && [ "$(pid_start "$1")" = "$2" ] || return 0
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
	wait_gone 60 "$_gp" || reap "$_gp"
	_gs=0
	wait "$_gp" 2> /dev/null || _gs=$?
	forget "$_gp"
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
# it is still the process recorded there, and stray_stop only a pid whose
# start time is still the one read. Live processes listed with another's
# identity (its pid given to a stranger), with their own command line but
# another start time (the pid reused by the same command), or with their own
# start time but another command line are left running, as are lines that
# name no pid and a stray_stop with another start time; the one listed with
# its own identity is stopped.
t_cleanup_id() {
	_l=${tmp:?}/cleanup-id.list
	: > "${_l:?}"
	set --
	# three sleeps, and two shells that exit 0 on TERM (so that no job notice
	# is printed for the two this test stops)
	for _q in 300 301 302 k1 k2; do
		case $_q in
		k*) sh -c 'trap "exit 0" TERM; while :; do sleep 0.1; done' "cleanup-id-$_q" & ;;
		*) sleep "$_q" & ;;
		esac
		set -- "$@" "$!"
		settle "$!"
		record "$!"
	done
	printf '%s %s\n' "$1" "$(pid_id "$$")" >> "${_l:?}"
	printf '%s %s\n' "$2" "$(id_of 'Thu Jan  1 00:00:00 2026' "$(ps -o args= -p "$2")")" >> "${_l:?}"
	printf '%s %s\n' "$3" "$(id_of "$(ps -o lstart= -p "$3")" 'sleep 9')" >> "${_l:?}"
	printf '%s\n' '' x "0 $(pid_id "$1")" "$1" >> "${_l:?}"
	record "$4" "${_l:?}"
	stop_listed "${_l:?}"
	wait_gone 10 "$4" || bad "cleanup-id: the process listed with its own identity was not stopped"
	stray_stop "$5" 'Thu Jan  1 00:00:00 2026' cleanup-id-k2
	sleep 0.5
	pid_running "$1" || bad "cleanup-id: a process listed with another's identity was signalled"
	pid_running "$2" || bad "cleanup-id: a process listed with another start time was signalled"
	pid_running "$3" || bad "cleanup-id: a process listed with another command line was signalled"
	pid_running "$5" || bad "cleanup-id: stray_stop signalled a process with another start time"
	stray_stop "$5" "$(pid_start "$5")" cleanup-id-k2
	pid_running "$5" && bad "cleanup-id: stray_stop did not stop a process with the start time read"
	# PIPE, which the shell prints no job notice for
	for _q in "$1" "$2" "$3"; do
		kill -PIPE "$_q" 2> /dev/null || true
	done
	for _q in "$@"; do
		wait "$_q" 2> /dev/null || true
		forget "$_q"
	done
	[ "$fails" -eq 0 ]
}

# ---- The real entry points. ---------------------------------------------
all="t_cleanup_id t_gate t_coverage t_cov_lc t_pins t_off t_nested t_kgrp t_grace t_grace_refused t_orphan"
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
