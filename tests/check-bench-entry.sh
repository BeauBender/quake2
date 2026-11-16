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
#     heavy part, though bench.sh sets it to C.
# Every pid the tests start is taken off the cleanup list once it has been
# waited for, so the list is empty after them. Then one mutant per lock call
# (each removing it from a copy of its entry point) and more: the run under
# the lock taking the lock again (the gate, pins.sh), a BENCH_LOCK_ENTRY
# without a token taken as a hold, an unset LC_ALL not restored (the gate,
# coverage), and, for each of the three, no state directory taking the
# lock, CI=true alone turning it off and a silent lock-off; each must fail
# the test that targets it.
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

cleanup() {
	# Stop any process this check started that still runs; nothing else.
	while read -r _p; do
		if pid_running "$_p"; then kill -TERM "$_p" 2> /dev/null || true; fi
	done < "${tmp:?}/pids"
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
	grep -vx -- "$1" "${tmp:?}/pids" > "${tmp:?}/pids.new" || true
	mv "${tmp:?}/pids.new" "${tmp:?}/pids"
}
# run <seconds> <command...>: runs it with its output in ${T}/out and its
# status in ST; past <seconds> it is reaped and ST is 124.
run() {
	_rs=$1
	shift
	"$@" > "${T:?}/out" 2>&1 &
	_rp=$!
	echo "$_rp" >> "${tmp:?}/pids"
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
chmod +x "${tmp:?}/mk/make" "${tmp:?}/cov/cc" "${tmp:?}/cov/gcov" "${tmp:?}/check-a.sh" "${tmp:?}/stub-bench.sh"
# GATEPATH: the stub make; COVPATH: the stub compiler too.
GATEPATH=${tmp:?}/mk:$PATH
COVPATH=${tmp:?}/cov:$GATEPATH

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
	echo "$_hp" >> "${tmp:?}/pids"
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

# ---- The real entry points. ---------------------------------------------
all="t_gate t_coverage t_cov_lc t_pins t_off t_nested"
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
	echo "$me: the cleanup list still holds pids the tests waited for: $(tr '\n' ' ' < "${tmp:?}/pids")"
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
