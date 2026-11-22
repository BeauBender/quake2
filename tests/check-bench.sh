#!/bin/sh
# Invariant: the bench harness (scripts/bench/, plan
# docs/plans/bench-harness.md) keeps its contract.
#
# Hermetic: a temporary lock state directory, a temporary store and a
# throwaway git repository under mktemp -d; no engine, no game data, no
# network. The null adapter and case (scripts/bench/adapters/null.sh,
# scripts/bench/cases/null-sleep.case) stand in for a game. It covers:
#   - the lock: two concurrent holders run one after the other; a waiter
#     gives up with 75 naming the holder; re-entry through BENCH_LOCK_TOKEN,
#     and a forged or stale token waits like anyone; release on EXIT, TERM
#     and HUP; the stop of the command's whole process group, before the
#     lock is free, on TERM and on INT to the lock shell's group (Ctrl-C),
#     a grandchild and a command that catches TERM included, with the
#     lock's grace floor; a TERM held in the window before the pid is
#     stored; a stopped command (a STOP to its group, and stty on a pty
#     from script when there is one) said and ended with 74; work a
#     command leaves running on a normal exit stopped before the lock is
#     free, with the same grace floor when it catches TERM; no line but
#     the lock shell's own on its output (the lines found shown; the
#     fixture's own shells silenced, and a probe that they are); the fork
#     of the command with its standard error on /dev/null (seen with
#     xtrace), the command with the caller's; the command's own INT trap;
#     reclaim of a dead holder, logged, and of two waiters that reclaim at
#     once exactly one gets the lock; a held reclaim guard, a holder whose
#     command's group still runs, and a holder-less lock inside its grace
#     left alone, and that lock reclaimed after it; the BENCH_LOCK_POLL
#     limits; a maker stalled between its mkdir and its write backs off;
#     the default state directory and its override, and the
#     outside-repository guard (a relative path, a . or .. segment, a git
#     ceiling, a failing git; a host without git uses the .git walk); the
#     queue (FIX106): a holder that releases and asks again goes behind
#     the waiter, three waiters get the lock in arrival order against
#     their pids and their polls, a waiter KILLed while queued is skipped,
#     reaped or not (a zombie), one that gives up or is TERMed leaves no
#     ticket, a nested call under the token takes none, lock status lists
#     the waiters in order, a maker that backs off and a waiter whose
#     ticket is removed keep their place, a stale ticket with the
#     waiter's own pid is removed, a FIFO or a link in the queue is never
#     read, and (review round 2) a reader whose ps fails or prints nothing
#     keeps a live waiter's ticket and temporary, and lock status skips a
#     zombie;
#   - the wall-time bound: it stops a grandchild, bounds a command that
#     ignores TERM, stops the work a command leaves running, ends a
#     stopped command and its group with 74, and shows no job notice;
#   - the host token and class line: stable, from system facts only, and
#     free of the identity values tests/check-no-local-identity.sh refuses
#     (hostname -s, whoami, $HOME), with the scrub and the salt tried on
#     synthetic values;
#   - the inputs: the adapter's paths (equal across a documentation-only
#     commit, changed by a src/ edit, a missing path as the "missing"
#     marker), the whole tree without a case, the case's flags, with shasum
#     and with cksum, and a failing, malformed or missing digest tool as an
#     error;
#   - case files: the key whitelist, no evaluation of values, the hash of
#     the normalised content, the name cap of 58;
#   - the store: its layout, a store inside a repository, relative or with
#     a .. segment refused, a set stored whole or not at all (a failed copy or rename
#     leaves the old baseline live), reads that take only the runs of the
#     live set, the live set by number, the runs oldest first, a preflight
#     refusal, the wall-time bound and its default;
#   - statistics against hand-computed values; the drift rule inside, at
#     the edge and outside; compare's table and verdict;
#   - a leading zero (08, 010) refused by every numeric setting and case key
#     the core reads, before anything runs;
#   - the core and bench.sh name no game and no project layout, and use no
#     eval;
#   - the quiet evidence (TASK166), with stubbed readers (snapstub,
#     freqstub; no GPU needed): every new run-file key, the gate's wait,
#     its hold and its bound, a not-quiet set refused by baseline put
#     (the gate ran out, GPU use in a snapshot), the flag in drift and
#     compare (unknown for a set stored without it), the pre-warm's length,
#     its one lock hold with the runs, its thermal stop and drift's
#     pre-warm, the quiet marker's fields and its removal on every exit
#     (0, another status, a stopped command, TERM and HUP to the lock
#     shell, an outer hold, a measurement), the new case keys, the
#     frequency probe's arithmetic and failures on synthetic dumps, and the
#     host's own snapshot reader printing the stub's keys; and, from the
#     review (round 1): a top line that takes the kernel's name and not an
#     argv[0] the process rewrote, the probe's own reason in the run file,
#     drift's own not-quiet guard, BENCH_GATE_POLL refused before anything
#     runs and each gate nap cut to the time left, the probe built before
#     the gate without a pre-warm, and the readers (snap.source,
#     freq.source) in the run file, drift and compare.
# The tests that do not test the host facts use a fast stand-in for
# system_profiler (fast_profiler). Then sixty-four mutants, each applied to a
# copy of scripts/bench/ and each required to fail the test it targets: the
# lock made neither exclusively nor linked, the holder renamed over
# another's, the command spawned without its own process group, its fork
# left with the caller's standard error (with standard input open, and
# closed) or a closed one not given /dev/null, a signal
# in the spawn window dropped, or acted on at once, re-entry accepts any
# token, the drift rule drops t, the core names a game, the reclaim guard
# made with mkdir -p, the holder-less grace dropped, the queue with no
# ticket check, ordered by pid, its ticket left on give-up, a dead or a
# zombie ticket kept, a ticket put back under a new key (after a back-off
# or a removal), a ticket with its own pid kept, a FIFO read (by a waiter
# or by lock status), a link read as a ticket (likewise), a failed or an
# empty ps read as dead (the helper, the head check, a temporary), a
# zombie listed by lock status, the store's
# outside-repository check deleted, the leading-zero refusal dropped; the
# gate without its hold, its bound or its reading, GPU use in a snapshot
# ignored, baseline put or drift's re-measure storing a not-quiet set,
# compare's unknown flag lost, the pre-warm not run, without its thermal
# stop or missing from drift, the snapshots or the frequencies not
# written, the marker not written or not removed, the probe's mean not
# weighted, the host's reader with a key of its own, a top line read from
# comm, the probe's own reason lost, BENCH_GATE_POLL or BENCH_QUIET_TTL
# unchecked, a gate nap not cut, the probe built after the gate, the
# snapshot's reader not named, a stub reader not printed by drift; and
# one to this test's own fixture, gc.sh with
# its standard error left open.
#
# Usage: tests/check-bench.sh [<test>...]   (named tests only, no mutants)
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C
me=check-bench
REAL=$(pwd -P)/scripts/bench

# The identity values, read before anything changes HOME.
id_host=$(hostname -s 2> /dev/null || true)
id_user=$(whoami 2> /dev/null || true)
id_home=${HOME:-}

# Nothing from the caller's environment: no bench setting (a lock token
# from an outer lock would let every lock call here pass through) and no
# git setting or configuration.
for _v in $(env | sed -n 's/^\(BENCH_[A-Za-z0-9_]*\)=.*/\1/p; s/^\(GIT_[A-Za-z0-9_]*\)=.*/\1/p'); do
	unset "$_v"
done
unset XDG_CACHE_HOME

tmp=$(mktemp -d "${TMPDIR:-/tmp}/checkbench.XXXXXX")
mkdir "${tmp:?}/tpl"
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 GIT_TEMPLATE_DIR="${tmp:?}/tpl"
: > "${tmp:?}/pids"

# pid_running <pid>: 0 while <pid> runs; 1 when it has gone or is a zombie;
# 2 when the check cannot tell: <pid> answers kill -0 but ps gives no answer
# for it, so it may be a zombie or still run (FIX177); when ps gives no
# answer, kill -0 is asked once more, since the pid may have ended and been
# reaped between the two probes, and then it has gone (1). kill -0 needs no ps
# and sends no signal; it is the shell's own (command kill), never a case's
# stand-in. It fails for a pid that has gone and for one another user runs,
# and every pid this check waits for is its own user's: so when it fails, ps
# decides when it answers, and a pid ps gives no answer for has gone. A
# zombie answers kill -0, so it is told from a running pid by ps alone.
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
	_s=$(ps -o stat= -p "$1" 2> /dev/null) || _s=
	case $_s in
	'')
		# ps gave no answer: the pid may have ended since the first probe
		# (and been reaped), so ask kill -0 once more (FIX177, round 1).
		command kill -0 "$1" 2> /dev/null || return 1
		return 2
		;;
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
# (bench.sh baseline put re-execs under the lock), and it is still this
# check's to stop; another process in the pid with the same start time would
# need the pid counter to go round within that second. The same primitive is
# in tests/check-bench-entry.sh.
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
# around them (ps pads it with blanks at the end). Every identity here comes
# from this function (FIX171).
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
# the list until it is forgotten.
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
# forget <pid>: takes <pid> off the cleanup list once it has been waited for
# or seen gone.
forget() {
	case $1 in
	'' | *[!0-9]*) return 0 ;;
	esac
	grep -v -- "^$1 " "${tmp:?}/pids" > "${tmp:?}/pids.new" || true
	mv "${tmp:?}/pids.new" "${tmp:?}/pids"
}
# collect <pid>: waits for a child this check started (the status is
# wait's), then forgets it.
collect() {
	_cs=0
	wait "$1" || _cs=$?
	forget "$1"
	return "$_cs"
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
	# A signal inside a decoy window runs this with decoy's kill and ps
	# still defined, so they go first (FIX171, round 2; t_decoy_term).
	unset -f kill ps 2> /dev/null || true
	# Stop any process this check started that still runs; nothing else.
	stop_listed "${tmp:?}/pids"
	rm -rf "${tmp:?}"
	[ "${_ok:-}" = 1 ] || exit 1
}
trap cleanup EXIT

R=$REAL
MUTANT=
fails=0
bad() {
	echo "$me: $*"
	fails=$((fails + 1))
	if [ -n "$MUTANT" ]; then exit 1; fi
}
# outlived <pid> <text> [<reporter>]: a negative assertion, that <pid> has
# ended (FIX179). <reporter> (default bad) is called with <text> while <pid>
# runs, and with "cannot tell whether pid <pid> outlived it ..." in front of
# <text> while pid_running cannot tell (2): a pid ps gives no answer for is
# never taken as ended.
outlived() {
	_ov=0
	pid_running "$1" || _ov=$?
	case $_ov in
	0) "${3:-bad}" "$2" ;;
	2) "${3:-bad}" "cannot tell whether pid $1 outlived it, ps gives no answer for it: $2" ;;
	esac
	return 0
}
# forget_ended <pid>: takes <pid> off the cleanup list once it has gone (1).
# While it runs (0) it stays; while pid_running cannot tell (2) it stays too,
# and that is a failure, since a pid dropped from the list is never stopped
# by the cleanup (FIX179).
forget_ended() {
	_fe=0
	pid_running "$1" || _fe=$?
	case $_fe in
	1) forget "$1" ;;
	2) bad "cannot tell whether pid $1 ended, ps gives no answer for it: it stays on the cleanup list" ;;
	esac
	return 0
}
B() {
	sh "$R/bench.sh" "$@"
}
# start <command...>: runs it in the background (BG), recorded for cleanup.
start() {
	"$@" &
	BG=$!
	settle "$BG"
	record "$BG"
}
# wait_gone <seconds> <pid>: 0 once <pid> is seen gone, and then it is
# forgotten; past <seconds>, 1 while it still runs and 2 while the check
# cannot tell (pid_running): a pid that ps gives no answer for is never
# taken as gone (FIX177).
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
	forget "$2"
	return 0
}
# reap <pid> [<seconds>]: after a failed wait, ends a process this check
# started (TERM, which its trap turns into a stop of its command; KILL after
# <seconds>, 30 by default). True once <pid> has gone: only then may the
# caller wait for it. Each of the two signals goes only while listed_same
# holds for <pid> (FIX171): the KILL comes 30 s after the TERM, time enough
# for the system to give the pid to another process. A pid that runs but is
# not on the list, was listed with no identity, has another identity now,
# or that ps gives no start time for, gets no signal; that is a failure,
# said, and reap is false, since nothing would bound a wait for that
# process. So is a pid that ps gives no answer for at all while it answers
# kill -0 (the check cannot tell whether it runs, FIX177), and a pid that
# still runs 5 s after its KILL.
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
# decoy <how> <identity>: sets _id, the identity one run of a signal site
# checks, for a site that signals a pid read from a file (FIX171, round 1):
# another (other), none (none), <identity> with ps giving no answer (nops),
# or <identity> itself (own). For each but own, kill only notes the signal
# it is asked for in $E/decoy.sig and sends nothing, so the site runs on
# the real process and no signal reaches it.
decoy() {
	_id=$2
	: > "${E:?}/decoy.sig"
	case $1 in
	own) return 0 ;;
	other) _id=$(id_of 'Thu Jan  1 00:00:00 2026') ;;
	none) _id= ;;
	nops) ps() { return 1; } ;;
	esac
	kill() { printf '%s\n' "$*" >> "${E:?}/decoy.sig"; }
}
# decoy_end <how> <text>: after that run, kill and ps are the system's
# again, and a signal the site asked for is a failure, <text>.
decoy_end() {
	[ "$1" != own ] || return 0
	unset -f kill ps
	[ ! -s "${E:?}/decoy.sig" ] || bad "$2: $1: a signal went to a process that is not the one read: $(tr '\n' ' ' < "${E:?}/decoy.sig")"
}
# ended <seconds> <pid> [<text>]: true once <pid>, a child this check
# started, has ended within <seconds>. Past them, <text> (when given) is a
# failure, <pid> is reaped, and ended is true only when reap ended it: the
# caller waits for <pid> (collect) only then.
ended() {
	wait_gone "$1" "$2" && return 0
	[ -z "${3:-}" ] || bad "$3"
	reap "$2"
}
# reap_lint <file> <helpers>: prints each line of <file> that holds the word
# reap (not reaped, reap_skip or t_reap_id: a word ends at a character other
# than a letter, a digit, _ or -, and a path's /reap is not one), joined
# with the lines a trailing \
# continues, unless it is a whole-line comment or lies in one of the
# functions <helpers> names (an awk alternation). A function runs from a line
# that starts with its definition (a name of [A-Za-z_][A-Za-z0-9_]*) to the
# next line that starts with }; no other line is exempt, the top level
# included, so no form of a call outside the helpers gets past it (FIX171).
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
# wait_held <seconds> <label>: the lock shows <label> with its child pid
# (the holder's line, the first: a waiter's line has a label too).
wait_held() {
	_n=0
	while :; do
		case $(B lock status 2> /dev/null) in
		"lock=held label=$2 "*"child=none "*) ;;
		"lock=held label=$2 "*) return 0 ;;
		esac
		[ "$_n" -lt $(($1 * 10)) ] || return 1
		sleep 0.1
		_n=$((_n + 1))
	done
}
# wait_file <seconds> <file>: the file exists and is not empty.
wait_file() {
	_n=0
	while [ ! -s "$2" ]; do
		[ "$_n" -lt $(($1 * 10)) ] || return 1
		sleep 0.1
		_n=$((_n + 1))
	done
	return 0
}
# Whether SIGINT reaches a command here: when this check itself started
# with it ignored, so does everything it starts, and the INT tests cannot
# run (they are skipped, and said).
INT_OK=
sh -c 'kill -INT $$; exit 0' 2> /dev/null || INT_OK=1
# fast_profiler: puts a system_profiler on PATH (for the rest of the test)
# that answers at once with a fixed GPU; the real one takes a third of a
# second on every call that reads the host facts. Only for the tests that
# do not test the facts themselves.
fast_profiler() {
	mkdir -p "${E:?}/sp"
	printf '#!/bin/sh\necho "      Chipset Model: Zzq GPU"\n' > "${E:?}/sp/system_profiler"
	chmod +x "${E:?}/sp/system_profiler"
	PATH=${E:?}/sp:$PATH
	export PATH
}
# The stubbed readers (TASK166). snapstub <snap|gate> prints a snapshot
# with every key the host's reader prints (t_snap_system checks that); the
# values of gpu.<mode>, cpu.<mode> and thermal come from the files of that
# name in $STUB, one a line: the n-th call of a mode takes line n, the last
# line repeats. It logs each call (mode, run kind, lock token) to
# $STUB/log and keeps a copy of a quiet marker it sees. freqstub dump|delta
# stands in for the frequency probe.
cat > "${tmp:?}/snapstub" << 'EOF'
#!/bin/sh
m=$1
n=$(($(cat "$STUB/n.$m" 2> /dev/null || echo 0) + 1))
echo "$n" > "$STUB/n.$m"
printf '%s %s %s\n' "$m" "${BENCH_RUN_KIND:-none}" "${BENCH_LOCK_TOKEN:-none}" >> "$STUB/log"
if [ -f "$BENCH_STATE/quiet" ]; then cp "$BENCH_STATE/quiet" "$STUB/marker.seen"; fi
v() {
	if [ -f "$STUB/$1" ]; then
		awk -v n="$n" '{ l = $0 } NR == n { print; f = 1; exit } END { if (!f) print l }' "$STUB/$1"
	else
		echo "$2"
	fi
}
printf 'cpu.busy=%s\ngpu.device=%s\n' "$(v "cpu.$m" 5)" "$(v "gpu.$m" 0)"
[ "$m" = snap ] || exit 0
printf 'gpu.renderer=0\ngpu.tiler=0\ngpu.mem=100\nload=1.5\nmem.free=80\nswap.used=0\n'
printf 'thermal=%s\nperf=none\nlowpower=0\n' "$(v thermal none)"
printf 'top.%d=1.0 zzqproc%d\n' 1 1 2 2 3 3 4 4 5 5
EOF
cat > "${tmp:?}/freqstub" << 'EOF'
#!/bin/sh
case $1 in
dump) echo 'dump.gpu=100 1000:100' ;;
delta) printf 'freq.gpu.mhz=1000\nfreq.gpu.top=100.0\nfreq.gpu.active=50.0\nfreq.gpu.max=1000\n' ;;
*) exit 2 ;;
esac
EOF
chmod +x "${tmp:?}/snapstub" "${tmp:?}/freqstub"
SNAP_KEYS='cpu.busy gpu.device gpu.renderer gpu.tiler gpu.mem load mem.free swap.used thermal perf lowpower top.1 top.2 top.3 top.4 top.5'

# fresh <name>: a new directory E with its own lock state and store, and
# the stubbed readers with their own files (STUB).
fresh() {
	E=${tmp:?}/$1
	rm -rf "${E:?}"
	mkdir -p "${E:?}/stub"
	BENCH_STATE=${E:?}/state
	BENCH_STORE=${E:?}/store
	BENCH_LOCK_POLL=0.1
	BENCH_LOCK_WAIT=10
	BENCH_SNAP_READER=${tmp:?}/snapstub
	BENCH_FREQ_PROBE=${tmp:?}/freqstub
	BENCH_GATE_POLL=0.05
	STUB=${E:?}/stub
	export BENCH_STATE BENCH_STORE BENCH_LOCK_POLL BENCH_LOCK_WAIT BENCH_SNAP_READER BENCH_FREQ_PROBE BENCH_GATE_POLL STUB
	unset BENCH_LOCK_TOKEN BENCH_NULL_VALUES BENCH_NULL_REFUSE BENCH_HASH BENCH_SESSION_ID CLAUDE_CODE_SESSION_ID BENCH_QUIET_TTL 2> /dev/null || true
}
# runfile <case name>: the one run file of the live set of <case name> in
# the store (the first, when there are more).
runfile() {
	ls "$BENCH_STORE"/*/*/"$1"/*/set-*/r-*.run 2> /dev/null | sed -n '$p'
}
# foreign <file...>: prints the lines of the files that are not the lock
# shell's own ("bench: ..."), at most five, each cut to 200 characters and
# shown after the name of the file it came from (the name is not part of
# the 200), with this check's temporary directory as <tmp> and the bench
# root as <bench>; fails when there is none. A file it cannot read counts
# as a finding, so the caller fails.
foreign() {
	for _f in "$@"; do
		[ -r "$_f" ] || {
			printf '%s:   foreign: cannot read %s\n' "$me" "${_f##*/}"
			return 0
		}
	done
	awk -v me="$me" -v t="$tmp" -v b="$REAL" '
	function mask(s, p, r,   i) {
		while ((i = index(s, p)) > 0) s = substr(s, 1, i - 1) r substr(s, i + length(p))
		return s
	}
	!/^bench: / && n < 5 {
		f = FILENAME; sub(/.*\//, "", f)
		s = mask(mask($0, t, "<tmp>"), b, "<bench>")
		print me ":   " f ": " substr(s, 1, 200); n++
	} END { exit !n }' "$@"
}
# lit <file> <old> <new>: replaces the first occurrence of <old> on each
# line, as fixed text; fails when there is none.
lit() {
	awk -v o="$2" -v n="$3" '{ p = index($0, o); if (p) { $0 = substr($0, 1, p - 1) n substr($0, p + length(o)); c++ } print } END { exit !c }' "$1" > "$1.new" &&
		mv "$1.new" "$1"
}

# ---- A throwaway repository: commits A (base), B (docs only), C (a src/
# edit), D (adds third_party/), F (adds a top-level .make file). ----------
repo=${tmp:?}/zzqrepo
gitc() {
	git -C "${repo:?}" -c user.name=zzq -c user.email=zzq@example.invalid -c commit.gpgsign=false "$@"
}
git -c init.defaultBranch=main init -q "${repo:?}"
mkdir "${repo:?}/src" "${repo:?}/docs"
printf 'all:\n' > "${repo:?}/Makefile"
printf 'int a;\n' > "${repo:?}/src/a.c"
printf 'doc\n' > "${repo:?}/docs/x.md"
gitc add -A
gitc commit -q -m A
cA=$(gitc rev-parse HEAD)
printf 'more doc\n' >> "${repo:?}/docs/x.md"
gitc commit -q -am B
cB=$(gitc rev-parse HEAD)
printf 'int b;\n' >> "${repo:?}/src/a.c"
gitc commit -q -am C
cC=$(gitc rev-parse HEAD)
mkdir "${repo:?}/third_party"
printf 'x\n' > "${repo:?}/third_party/x"
gitc add -A
gitc commit -q -m D
cD=$(gitc rev-parse HEAD)
printf 'x:\n' > "${repo:?}/extra.make"
gitc add -A
gitc commit -q -m F
cF=$(gitc rev-parse HEAD)

# ---- Tests. Each runs in a subshell and fails by status. ------------------

t_hygiene() {
	for _f in "$R/bench.sh" "$R"/core/*.sh "$R"/adapters/*.sh; do
		sh -n "$_f" || bad "syntax error in ${_f#"$R"/}"
	done
	[ -x "$R/bench.sh" ] || bad "scripts/bench/bench.sh is not executable"
	if grep -nwE 'eval' "$R/bench.sh" "$R"/core/*.sh > "${tmp:?}/eval.out"; then
		bad "eval in the core or bench.sh: $(sed 's/:.*//' "${tmp:?}/eval.out" | tr '\n' ' ')"
	fi
	[ "$fails" -eq 0 ]
}

t_generic() {
	if grep -inE 'quake|q2|yquake|ref_|gl1' "$R/bench.sh" "$R"/core/*.sh > "${tmp:?}/game.out"; then
		bad "the core or bench.sh names a game: $(cut -d: -f1,2 "${tmp:?}/game.out" | sed "s#^$R/##" | tr '\n' ' ')"
	fi
	if grep -inE 'third_party|makefile' "$R/bench.sh" "$R"/core/*.sh > "${tmp:?}/layout.out"; then
		bad "the core or bench.sh names a project layout (adapter_inputs_paths does): $(cut -d: -f1,2 "${tmp:?}/layout.out" | sed "s#^$R/##" | tr '\n' ' ')"
	fi
	[ "$fails" -eq 0 ]
}

# t_cleanup_id: cleanup's stop (stop_listed) signals a listed pid only while
# it still runs with the start time recorded there. Live processes listed
# with another's identity (its pid given to a stranger) or with their own
# command line but another start time (the pid reused by the same command)
# are left running, as are lines that name no pid and a pid recorded after
# it ended (listed with no identity). The one listed with its own identity
# is stopped, and so is one that exec'd another program after it was
# recorded (bench.sh baseline put re-execs under the lock): the same start
# time with another command line is still the process this check started.
# The processes are shells that end on TERM, and by themselves within about
# 30 s, so no wait here outlives that even when TERM is ignored.
t_cleanup_id() {
	fresh cleanup-id
	: > "${E:?}/list"
	: > "${E:?}/own"
	printf '%s\n' 'trap "exit 0" TERM' 'n=0' 'while [ "$n" -lt 300 ]; do sleep 0.1; n=$((n + 1)); done' > "${E:?}/loop.sh"
	start sh "${E:?}/loop.sh" cleanup-id-x
	_x=$BG
	start sh "${E:?}/loop.sh" cleanup-id-y
	_y=$BG
	start sh "${E:?}/loop.sh" cleanup-id-k
	_k=$BG
	# runs its sleep 1 when recorded, then execs the loop
	start sh -c 'sleep 1; exec sh "$0" "$1"' "${E:?}/loop.sh" cleanup-id-e
	_e=$BG
	record "$_e" "${E:?}/list"
	case $(ps -o args= -p "$_e" 2> /dev/null) in
	*'exec sh'*) ;;
	*) bad "cleanup-id: the exec case had exec'd, or ps gave no answer for it, before it was recorded" ;;
	esac
	record "$_x" "${E:?}/own"
	record "$_y" "${E:?}/own"
	true &
	_d=$!
	wait "$_d" || true
	_o=$(pid_id 1) || bad "cleanup-id: ps gave no start time for pid 1"
	printf '%s %s\n' "$_x" "$_o" >> "${E:?}/list"
	printf '%s %s\n' "$_y" "$(id_of 'Thu Jan  1 00:00:00 2026')" >> "${E:?}/list"
	printf '%s\n' '' x "0 $(pid_id "$_x")" "$_x" >> "${E:?}/list"
	record "$_d" "${E:?}/list"
	grep -qx "$_d " "${E:?}/list" || bad "cleanup-id: a pid recorded after it ended is not on the list with no identity"
	record "$_k" "${E:?}/list"
	_n=0
	while [ "$_n" -lt 50 ]; do
		case $(ps -o args= -p "$_e" 2> /dev/null) in
		*'exec sh'*) ;;
		*) break ;;
		esac
		sleep 0.1
		_n=$((_n + 1))
	done
	case $(ps -o args= -p "$_e" 2> /dev/null) in
	*'exec sh'*) bad "cleanup-id: the exec case did not exec within 5 s" ;;
	*cleanup-id-e*) ;;
	*) bad "cleanup-id: ps gave no answer for the exec case" ;;
	esac
	record "$_e" "${E:?}/own"
	stop_listed "${E:?}/list"
	wait_gone 10 "$_k" || bad "cleanup-id: the process listed with its own identity was not stopped"
	wait_gone 10 "$_e" || bad "cleanup-id: a process this check started that exec'd after it was recorded was left running"
	sleep 0.5
	pid_running "$_x" || bad "cleanup-id: a process listed with another's identity was signalled"
	pid_running "$_y" || bad "cleanup-id: a process listed with another start time was signalled"
	stop_listed "${E:?}/own"
	for _q in "$_x" "$_y" "$_k" "$_e"; do
		wait_gone 40 "$_q" || bad "cleanup-id: pid $_q outlived its own bound"
		collect "$_q" 2> /dev/null || true
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
	fresh reap-id
	printf '%s\n' 'trap "echo TERM >> \"\$1\"" TERM' 'n=0' 'while [ ! -e "$2" ] && [ "$n" -lt 300 ]; do sleep 0.1; n=$((n + 1)); done' > "${E:?}/deaf.sh"
	set --
	for _q in t k none off ps; do
		start sh "${E:?}/deaf.sh" "${E:?}/$_q.sig" "${E:?}/stop"
		set -- "$@" "$BG"
	done
	_o=$(pid_id 1) || bad "reap-id: ps gave no start time for pid 1"
	for _q in "$1" "$3" "$4"; do forget "$_q"; done
	printf '%s %s\n' "$1" "$_o" >> "${tmp:?}/pids"
	printf '%s \n' "$3" >> "${tmp:?}/pids"
	# A reap that sends no signal to a pid that still runs is a failure and
	# false. Each runs in a subshell, so that its failure is checked here
	# and not counted.
	_rs=0
	(reap "$1" 1) > "${E:?}/t.out" || _rs=$?
	[ ! -s "${E:?}/t.sig" ] || bad "reap-id: a TERM went to a pid listed with another identity"
	grep -q "reap: no TERM to pid $1:" "${E:?}/t.out" && [ "$_rs" -ne 0 ] || bad "reap-id: the TERM not sent was not a failure (status $_rs): $(cat "${E:?}/t.out")"
	# k: listed with its own identity, with ps giving no start time from the
	# second time it is asked (after the TERM, for the KILL)
	_rs=0
	(
		ps() {
			case " $* " in
			*' lstart= '*)
				echo x >> "${E:?}/k.ps"
				[ $(($(wc -l < "${E:?}/k.ps"))) -le 1 ] || return 1
				;;
			esac
			command ps "$@"
		}
		reap "$2" 1
	) > "${E:?}/k.out" || _rs=$?
	grep -qx TERM "${E:?}/k.sig" 2> /dev/null || bad "reap-id: no TERM went to a pid listed with its own identity (k)"
	pid_running "$2" || bad "reap-id: a KILL went to a pid that ps gave no start time for after its TERM"
	grep -q "reap: no KILL to pid $2:" "${E:?}/k.out" && [ "$_rs" -ne 0 ] || bad "reap-id: the KILL not sent was not a failure (status $_rs): $(cat "${E:?}/k.out")"
	for _q in none off; do
		if [ "$_q" = none ]; then _p=$3; else _p=$4; fi
		_rs=0
		(reap "$_p" 1) > "${E:?}/$_q.out" || _rs=$?
		[ ! -s "${E:?}/$_q.sig" ] && pid_running "$_p" || bad "reap-id: a signal went to a pid listed as $_q"
		grep -q "reap: no TERM to pid $_p:" "${E:?}/$_q.out" && [ "$_rs" -ne 0 ] || bad "reap-id: $_q: the TERM not sent was not a failure (status $_rs): $(cat "${E:?}/$_q.out")"
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
	) > "${E:?}/ps.out" || _rs=$?
	[ ! -s "${E:?}/ps.sig" ] && pid_running "$5" || bad "reap-id: a signal went to a pid that ps gave no start time for"
	grep -q "reap: no TERM to pid $5:" "${E:?}/ps.out" && [ "$_rs" -ne 0 ] || bad "reap-id: the signals not sent with no answer from ps were not a failure (status $_rs): $(cat "${E:?}/ps.out")"
	# own: started, reaped and collected in a subshell with no standard
	# error, so that the shell's job notice for the KILL is not printed
	_f0=$fails
	(
		start sh "${E:?}/deaf.sh" "${E:?}/own.sig" "${E:?}/stop"
		reap "$BG" 1 > /dev/null
		grep -qx TERM "${E:?}/own.sig" 2> /dev/null || bad "reap-id: no TERM went to a pid listed with its own identity"
		wait_gone 5 "$BG" || bad "reap-id: no KILL went to a pid listed with its own identity"
		collect "$BG" || true
		[ "$fails" -eq "$_f0" ]
	) 2> /dev/null || bad "reap-id: the case listed with its own identity failed"
	: > "${E:?}/stop"
	for _q in "$@"; do
		wait_gone 40 "$_q" || bad "reap-id: pid $_q outlived its own bound"
		collect "$_q" 2> /dev/null || true
	done
	[ "$fails" -eq 0 ]
}

# t_reap_wait: no caller of reap waits for a pid that reap did not end
# (FIX171). A child of the check's that ends only once the stop file is
# there (or after about 30 s) is listed with no identity, or with another's,
# or with its own while ps fails for every call (nops, FIX177: the check
# cannot tell, and reap must say so and send nothing). Each kind of caller,
# finish and ended (then collect, as each caller of ended does), must return
# within 10 s and count the failure. And no line
# outside reap, reap_skip, ended and reap's own tests holds the word reap
# (reap_lint, checked on its own shapes): a caller that did would wait for
# its pid with nothing to bound the wait.
t_reap_wait() {
	fresh reap-wait
	printf '%s\n' 'trap "echo TERM >> \"\$1\"" TERM' 'n=0' 'while [ ! -e "$2" ] && [ "$n" -lt 300 ]; do sleep 0.1; n=$((n + 1)); done' > "${E:?}/deaf.sh"
	_o=$(pid_id 1) || bad "reap-wait: ps gave no start time for pid 1"
	for _k in finish:none finish:other finish:nops ended:none ended:other ended:nops; do
		rm -f "${E:?}/stop" "${E:?}/c.pid" "${E:?}/c.sig" "${E:?}/c.rv"
		(
			start sh "${E:?}/deaf.sh" "${E:?}/c.sig" "${E:?}/stop"
			echo "$BG" > "${E:?}/c.pid"
			case ${_k#*:} in
			none) forget "$BG" && printf '%s \n' "$BG" >> "${tmp:?}/pids" ;;
			other) forget "$BG" && printf '%s %s\n' "$BG" "$_o" >> "${tmp:?}/pids" ;;
			# Listed with its own identity, then ps fails for every call
			# (FIX177): the check cannot tell whether the child runs.
			nops) ps() { return 1; } ;;
			esac
			_f0=$fails
			case ${_k%:*} in
			finish) finish reap-wait "$BG" 0 1 || echo false > "${E:?}/c.rv" ;;
			ended) if ended 1 "$BG"; then collect "$BG" || true; fi ;;
			esac
			[ "$fails" -gt "$_f0" ]
		) > "${E:?}/w.out" 2> /dev/null &
		_w=$!
		wait_gone 10 "$_w" || bad "reap-wait: $_k: the caller still waited 10 s after reap sent no signal"
		: > "${E:?}/stop"
		_ws=0
		wait "$_w" || _ws=$?
		[ "$_ws" -eq 0 ] || bad "reap-wait: $_k: the caller counted no failure: $(cat "${E:?}/w.out")"
		grep -q "reap: no TERM to pid " "${E:?}/w.out" || bad "reap-wait: $_k: reap did not say it sent no TERM: $(cat "${E:?}/w.out")"
		[ "${_k%:*}" != finish ] || [ -s "${E:?}/c.rv" ] || bad "reap-wait: $_k: finish was true for a pid reap did not end"
		if [ "${_k#*:}" = nops ]; then
			grep -q "so the check cannot tell whether it still runs" "${E:?}/w.out" || bad "reap-wait: $_k: reap did not say it cannot tell: $(cat "${E:?}/w.out")"
			[ ! -s "${E:?}/c.sig" ] || bad "reap-wait: $_k: a signal went to a pid that ps gave no answer for"
		fi
		_c=$(cat "${E:?}/c.pid" 2> /dev/null) || _c=
		wait_gone 5 "$_c" || bad "reap-wait: $_k: pid $_c outlived its stop file"
		forget "$_c"
	done
	# A pid that ends between the two probes (FIX177, round 1): kill -0
	# answers, the pid ends and is reaped while ps is slow, and ps then
	# gives no answer. The second kill -0 fails, so it has gone (1), not
	# "cannot tell" (2). A child of this shell, and one that is not.
	for _k in child other; do
		_r=$(
			ps() {
				sleep 0.5
				command ps "$@"
			}
			if [ "$_k" = child ]; then
				sleep 0.2 &
				_p=$!
			else
				_p=$(sh -c 'sleep 0.2 > /dev/null 2>&1 & echo $!')
			fi
			_rv=0
			pid_running "$_p" || _rv=$?
			echo "$_rv"
		)
		[ "$_r" = 1 ] || bad "reap-wait: a $_k pid that ended between the two probes read as $_r, not 1 (gone)"
	done
	_bare=$(reap_lint "$0" 'reap|reap_skip|ended|reap_lint|t_reap_id|t_reap_wait')
	[ -z "$_bare" ] || bad "reap-wait: reap is called outside ended, so a wait after it may have no bound: $_bare"
	# The lint itself (FIX171, round 2): each shape below calls reap in a
	# function after ended's, and must be refused; ended and collect pass.
	lint_shape() {
		_ln=$1
		shift
		printf '%s\n' 'ended() {' '	reap "$2"' '}' "$_ln() {" "$@" '}' > "${E:?}/lint.sh"
		[ -n "$(reap_lint "${E:?}/lint.sh" ended)" ] || bad "reap-wait: the lint let through, in $_ln: $*"
	}
	while IFS= read -r _s; do
		lint_shape t_f "	$_s"
	done << 'SHAPES'
reap "$x"
reap "$x"; collect "$x"
reap "$x" || wait "$x"
reap "$x" 2>&1
reap "$x" 2>&1; wait "$x"
if :; then reap "$x"; fi
if :; then :; else reap "$x"; fi
{ reap "$x"; }
reap $x
eval reap "$x"
eval 'reap "$x"'
r=reap; $r "$x"
foo && reap "$x"
reap "$BG" || true
SHAPES
	lint_shape t_f '	reap "$x" || true' '	collect "$x"'
	lint_shape t_f '	reap "$x" || true' '	wait "$x"'
	lint_shape t_f '	reap \' '		"$x"'
	lint_shape t_f '	re\' 'ap "$x"'
	lint_shape t_x2 '	reap "$x"'
	lint_shape Tx '	reap "$x"'
	printf '%s\n' 'ended() {' '	reap "$2"' 't_x2() {' '	reap "$x"' '}' > "${E:?}/lint.sh"
	[ -n "$(reap_lint "${E:?}/lint.sh" ended)" ] || bad "reap-wait: the lint let through a reap in t_x2() with no } before it"
	printf '%s\n' 't_f() {' '	if ended 10 "$x"; then collect "$x" || true; fi' '	ended 0 "$BG" || true' '	# then reap it' '}' > "${E:?}/lint.sh"
	_l=$(reap_lint "${E:?}/lint.sh" ended)
	[ -z "$_l" ] || bad "reap-wait: the lint refused a plain ended: $_l"
	[ "$fails" -eq 0 ]
}

# t_decoy_term: a signal inside a decoy window still lets cleanup stop what
# the cleanup list holds (FIX171, round 2). decoy defines kill and, for nops,
# ps, and an EXIT trap a signal runs there runs with them defined. A shell
# the case starts sets the check's own cleanup trap on a list of its own,
# lists a stand-in, opens a decoy window and waits in it (10 s at most); the
# case sends that shell a TERM. A subshell's EXIT trap runs on a signal only
# through a trap of its own for that signal (the system sh installs no
# handler again in a subshell), so the shell also has trap 'exit 1' TERM.
# The stand-in ends on TERM, and by itself within about 30 s.
t_decoy_term() {
	fresh decoy-term
	printf '%s\n' 'trap "exit 0" TERM' 'n=0' 'while [ "$n" -lt 300 ]; do sleep 0.1; n=$((n + 1)); done' > "${E:?}/loop.sh"
	for _how in other none nops; do
		rm -f "${E:?}/sd.pid" "${E:?}/open"
		(
			tmp=${E:?}/sub.$_how
			mkdir "${tmp:?}" || exit 1
			trap cleanup EXIT
			trap 'exit 1' TERM
			start sh "${E:?}/loop.sh" decoy-term-sd
			echo "$BG" > "${E:?}/sd.pid"
			decoy "$_how" "$(pid_id "$BG")"
			echo open > "${E:?}/open"
			_n=0
			while [ "$_n" -lt 100 ]; do
				sleep 0.1
				_n=$((_n + 1))
			done
		) > /dev/null 2>&1 &
		_s=$!
		wait_file 10 "${E:?}/open" || bad "decoy-term: $_how: the shell never opened its decoy window"
		_sd=$(cat "${E:?}/sd.pid" 2> /dev/null) || _sd=
		if [ -n "$_sd" ]; then record "$_sd"; else bad "decoy-term: $_how: the shell listed no stand-in"; fi
		kill -TERM "$_s" 2> /dev/null || true
		wait_gone 5 "$_sd" || bad "decoy-term: $_how: a TERM inside a decoy window left the process on the cleanup list running 5 s later"
		wait_gone 15 "$_s" || bad "decoy-term: $_how: the shell outlived its own bound"
		collect "$_s" 2> /dev/null || true
		wait_gone 40 "$_sd" || bad "decoy-term: $_how: pid $_sd outlived its own bound"
	done
	[ "$fails" -eq 0 ]
}

t_lock_exclusion() {
	fresh excl
	start sh "$R/bench.sh" lock run first -- sh -c 'echo first-start >> "$1"; sleep 2; echo first-end >> "$1"' x "${E:?}/log" > "${E:?}/first.out" 2>&1
	_a=$BG
	wait_held 5 first || bad "exclusion: the first holder never showed in lock status"
	start sh "$R/bench.sh" lock run second -- sh -c 'echo second-start >> "$1"; echo second-end >> "$1"' x "${E:?}/log" > "${E:?}/second.out" 2>&1
	_b=$BG
	wait_gone 15 "$_a" || bad "exclusion: the first holder did not finish"
	wait_gone 15 "$_b" || bad "exclusion: the second holder did not finish"
	_sa=0
	collect "$_a" || _sa=$?
	_sb=0
	collect "$_b" || _sb=$?
	[ "$_sa" -eq 0 ] && [ "$_sb" -eq 0 ] || bad "exclusion: the holders exited $_sa and $_sb"
	_got=$(tr '\n' ' ' < "${E:?}/log")
	[ "$_got" = "first-start first-end second-start second-end " ] || bad "exclusion: the two commands overlapped: $_got"
	grep -q 'waiting for the lock held by first (pid ' "${E:?}/second.out" || bad "exclusion: the waiter did not name the holder"
	[ "$(B lock status)" = lock=free ] || bad "exclusion: the lock was not released"
	[ "$fails" -eq 0 ]
}

t_lock_tokens() {
	fresh tokens
	B lock run early -- sh -c 'printf "%s\n" "$BENCH_LOCK_TOKEN" > "$1"' x "${E:?}/stale" || bad "tokens: a plain lock run failed"
	_stale=$(cat "${E:?}/stale")
	case $_stale in
	t-*) ;;
	*) bad "tokens: the lock exported no BENCH_LOCK_TOKEN" ;;
	esac
	# Re-entry: with no wait at all, a nested call must pass straight through.
	_st=0
	_out=$(env BENCH_LOCK_WAIT=0 sh "$R/bench.sh" lock run outer -- sh "$R/bench.sh" lock run inner -- echo inner-ran 2> "${E:?}/reenter.err") || _st=$?
	[ "$_st" -eq 0 ] && [ "$_out" = inner-ran ] || bad "re-entry: a nested call with the holder's token did not pass through (status $_st)"
	start sh "$R/bench.sh" lock run holder-h -- sleep 30 > "${E:?}/h.out" 2>&1
	_h=$BG
	wait_held 5 holder-h || bad "tokens: the holder never showed in lock status"
	# The three intruders wait at the same time.
	_ips=
	for _tok in forged:t-1-1-forged "stale:$_stale" "empty:"; do
		_kind=${_tok%%:*}
		start env BENCH_LOCK_TOKEN="${_tok#*:}" BENCH_LOCK_WAIT=1 sh "$R/bench.sh" lock run intruder -- echo ran > "${E:?}/int.$_kind.out" 2> "${E:?}/int.$_kind.err"
		_ips="$_ips $_kind:$BG"
	done
	for _ip in $_ips; do
		_kind=${_ip%%:*}
		wait_gone 10 "${_ip#*:}" || bad "tokens: the $_kind-token waiter did not give up"
		_st=0
		collect "${_ip#*:}" || _st=$?
		[ "$_st" -eq 75 ] || bad "tokens: a $_kind token: wanted exit 75, got $_st"
		[ ! -s "${E:?}/int.$_kind.out" ] || bad "tokens: a $_kind token ran its command under someone else's lock"
		grep -q 'still held by holder-h (pid ' "${E:?}/int.$_kind.err" || bad "tokens: the $_kind-token waiter did not name the holder"
	done
	# Release on a signal: TERM to the holder stops its command and frees the lock.
	_c=$(sed -n 's/^child=//p' "${E:?}/state/lock/holder")
	pid_running "$_c" || bad "tokens: the holder's command is not running"
	kill -TERM "$_h"
	wait_gone 15 "$_h" || bad "release: the holder did not exit on TERM"
	_st=0
	collect "$_h" || _st=$?
	[ "$_st" -eq 143 ] || bad "release: wanted exit 143 on TERM, got $_st"
	[ "$(B lock status)" = lock=free ] || bad "release: TERM did not release the lock"
	wait_gone 5 "$_c" || bad "release: the command outlived its holder"
	start sh "$R/bench.sh" lock run holder-hup -- sleep 30 > "${E:?}/hup.out" 2>&1
	_h=$BG
	wait_held 5 holder-hup || bad "release: the HUP holder never showed"
	kill -HUP "$_h"
	wait_gone 15 "$_h" || bad "release: the holder did not exit on HUP"
	_st=0
	collect "$_h" || _st=$?
	[ "$_st" -eq 129 ] || bad "release: wanted exit 129 on HUP, got $_st"
	[ "$(B lock status)" = lock=free ] || bad "release: HUP did not release the lock"
	# EXIT: a failing command gives its status and frees the lock.
	_st=0
	B lock run failing -- sh -c 'exit 7' || _st=$?
	[ "$_st" -eq 7 ] || bad "release: wanted the command's status 7, got $_st"
	[ "$(B lock status)" = lock=free ] || bad "release: a failing command left the lock held"
	# A command killed by a signal gives its status, and the lock shell
	# prints no job notice (it names an internal path) on the caller's
	# standard error.
	_st=0
	B lock run killed -- sh -c 'sleep 0.3; kill -KILL $$' 2> "${E:?}/killed.err" || _st=$?
	[ "$_st" -eq 137 ] || bad "release: wanted status 137 from a command killed by KILL, got $_st"
	[ ! -s "${E:?}/killed.err" ] || bad "release: the lock shell printed a job notice on standard error"
	# The command reads the caller's standard input, not /dev/null.
	_out=$(printf 'from-stdin\n' | B lock run reader -- cat)
	[ "$_out" = from-stdin ] || bad "lock run: the command did not get the caller's standard input"
	_st=0
	B lock run 'bad label' -- true 2> /dev/null || _st=$?
	[ "$_st" -eq 2 ] || bad "tokens: a label with a space was not refused"
	[ "$fails" -eq 0 ]
}

# grandchild <dir>: writes <dir>/gc.sh, a command whose grandchild writes
# its pid to $1, then "$3-late" to the log $2 after $4 seconds (1.5); and
# <dir>/stubborn.sh, a command that catches TERM, says so in $2 and goes on
# (its pid in $1).
# gc.sh's shells print nothing: its first line, GC_QUIET, sends their
# standard error to /dev/null. A shell prints a job notice ("<script>: line
# 1: <pid> Terminated: 15 <command>") for a foreground child that a signal
# it does not trap ends while the shell lives on, and a TERM to a process
# group reaches its members one after the other, so under load gc.sh, or
# its sh -c, can see its child end first (FIX83, docs/runbook/FIX83.md).
# Its notice would land in the lock shell's output, which t_lock_stop
# requires to hold the lock shell's own lines alone. The gc-notice mutant
# clears GC_QUIET.
GC_QUIET='exec 2> /dev/null'
grandchild() {
	{
		printf '%s\n' "$GC_QUIET"
		cat << 'EOF'
sh -c 'echo $$ > "$1"; sleep "${4:-1.5}"; echo "$3-late" >> "$2"' gc "$@"
EOF
	} > "${1:?}/gc.sh"
	cat > "${1:?}/stubborn.sh" << 'EOF'
trap 'echo got-term >> "$2"' TERM
echo $$ > "$1"
while :; do sleep 0.1; done
EOF
}

t_lock_stop() {
	fresh stop
	grandchild "${E:?}"
	: > "${E:?}/log"
	# The fixture prints no job notice itself, so the check at the end sees
	# the lock shell's lines alone: its grandchild ends by TERM while gc.sh
	# lives on (the order a TERM to the group can take under load), under
	# the lock and into one file, as below. gc.sh then exits with 143, and
	# its leftover sleep is stopped by the lock.
	start sh "$R/bench.sh" lock run quiet -- sh "${E:?}/gc.sh" "${E:?}/gc.quiet" "${E:?}/log" quiet 3 > "${E:?}/quiet.out" 2>&1
	_h=$BG
	wait_file 5 "${E:?}/gc.quiet" || bad "stop: the quiet probe's grandchild never started"
	# the pid read with its start time, and signalled only while it still
	# has it (FIX171)
	_gq=$(cat "${E:?}/gc.quiet" 2> /dev/null) || _gq=
	_gi=$(pid_id "$_gq") || _gi=
	{ pid_same "$_gq" "$_gi" && kill -TERM "$_gq" 2> /dev/null; } || bad "stop: the quiet probe's grandchild is gone, or ps gave no answer for it"
	_st=0
	if ended 15 "$_h" "stop: the quiet probe's lock shell did not exit"; then collect "$_h" || _st=$?; fi
	[ "$_st" -eq 143 ] || bad "stop: the quiet probe: wanted exit 143 from gc.sh after its grandchild's TERM, got $_st"
	if foreign "${E:?}/quiet.out" > "${E:?}/foreign"; then
		cat "${E:?}/foreign"
		bad "stop: the fixture printed a line itself (a job notice), above; the check below would blame the lock shell for it"
	fi
	# TERM to the lock shell stops the command's whole process group before
	# the lock is free.
	start sh "$R/bench.sh" lock run gc -- sh "${E:?}/gc.sh" "${E:?}/gc.term" "${E:?}/log" term > "${E:?}/term.out" 2>&1
	_h=$BG
	wait_file 5 "${E:?}/gc.term" || bad "stop: the grandchild never started"
	kill -TERM "$_h"
	wait_gone 15 "$_h" || bad "stop: the lock shell did not exit on TERM"
	_st=0
	collect "$_h" || _st=$?
	[ "$_st" -eq 143 ] || bad "stop: wanted exit 143 on TERM, got $_st"
	[ "$(B lock status)" = lock=free ] || bad "stop: TERM did not release the lock"
	outlived "$(cat "${E:?}/gc.term")" "stop: a grandchild of the command outlived the lock (TERM)"
	# A command that catches TERM and goes on is killed after the grace,
	# which is at least BENCH_STOP_GRACE + 5 s even when that is 0, so a
	# nested bench.sh stopping a group of its own finishes first. It runs
	# under a lock of its own (s2) while the tests below run, and its end
	# is checked after them.
	mkdir "${E:?}/s2"
	start sh -c 'st=0; env BENCH_STATE="$1/state" BENCH_STOP_GRACE=0 sh "$2/bench.sh" lock run stubborn -- sh "$3" "$1/pid" "$1/log" > "$1/out" 2>&1 || st=$?; echo "$st" > "$1/st"; date +%s > "$1/end"' x "${E:?}/s2" "$R" "${E:?}/stubborn.sh"
	wait_file 5 "${E:?}/s2/pid" || bad "stop: the stubborn command never started"
	_sl=$(sed -n 's/^pid=//p' "${E:?}/s2/state/lock/holder")
	record "$_sl"
	_sli=$(pid_id "$_sl") || _sli=
	# The TERM goes only while the lock shell still has the identity read
	# with it (FIX171): run with that identity replaced (decoy), the site
	# sends nothing; the last run, own, sends it. The grace floor is timed
	# from that TERM, not from before the decoy runs.
	for _how in other none nops own; do
		[ "$_how" != own ] || _t0=$(date +%s)
		decoy "$_how" "$_sli"
		{ pid_same "$_sl" "$_id" && kill -TERM "$_sl"; } || [ "$_how" != own ] || bad "stop: the stubborn command's lock shell is not running, or is not the process read (FIX171)"
		decoy_end "$_how" "stop: the TERM to the stubborn command's lock shell"
	done
	# Ctrl-C at a terminal: INT to the lock shell's process group.
	if [ -n "$INT_OK" ]; then
		set -m
		sh "$R/bench.sh" lock run cc -- sh "${E:?}/gc.sh" "${E:?}/gc.int" "${E:?}/log" int > "${E:?}/int.out" 2>&1 &
		_h=$!
		set +m
		settle "$_h"
		record "$_h"
		wait_file 5 "${E:?}/gc.int" || bad "stop: the grandchild never started (INT)"
		kill -s INT -- "-$_h"
		wait_gone 15 "$_h" || bad "stop: the lock shell did not exit on INT to its group"
		_st=0
		collect "$_h" 2> /dev/null || _st=$?
		[ "$_st" -eq 130 ] || bad "stop: wanted exit 130 on INT, got $_st"
		[ "$(B lock status)" = lock=free ] || bad "stop: INT did not release the lock"
		outlived "$(cat "${E:?}/gc.int")" "stop: a grandchild of the command outlived the lock (INT)"
		# The command's own INT trap runs: INT is not ignored under the lock.
		_st=0
		_out=$(B lock run trapper -- sh -c 'trap "echo handled; exit 5" INT; kill -INT $$; echo unhandled') || _st=$?
		[ "$_st" -eq 5 ] && [ "$_out" = handled ] || bad "stop: the command's own INT trap did not run under lock run (status $_st, output $_out)"
	else
		echo "$me: SIGINT is ignored where this check runs, so the INT tests are skipped"
	fi
	# A command that stops (a STOP to its group here; at a terminal, one
	# that touches it from its background process group) is said, stopped
	# with its group, and the lock freed with 74: never a wait without end.
	start env BENCH_STOP_GRACE=0 sh "$R/bench.sh" lock run stopped -- sh "${E:?}/gc.sh" "${E:?}/gc.stop" "${E:?}/log" stopped 30 > "${E:?}/stopped.out" 2>&1
	_h=$BG
	wait_held 5 stopped || bad "stop: the stopped command's holder never showed"
	wait_file 5 "${E:?}/gc.stop" || bad "stop: the stopped command's grandchild never started"
	# the group leader read with its identity, and the group stopped only
	# while the leader still has it (FIX171), after the decoy runs, as for
	# the lock shell above
	_c=$(sed -n 's/^child=//p' "${E:?}/state/lock/holder")
	_ci=$(pid_id "$_c") || _ci=
	for _how in other none nops own; do
		decoy "$_how" "$_ci"
		{ pid_same "$_c" "$_id" && kill -s STOP -- "-$_c"; } || [ "$_how" != own ] || bad "stop: the stopped command's group leader is gone, or ps gave no answer for it"
		decoy_end "$_how" "stop: the STOP to the stopped command's group"
	done
	_st=0
	if ended 10 "$_h" "stop: lock run still waits on a stopped command"; then collect "$_h" || _st=$?; fi
	[ "$_st" -eq 74 ] || bad "stop: wanted exit 74 from a stopped command, got $_st"
	grep -q '^bench: the command stopped' "${E:?}/stopped.out" || bad "stop: a stopped command was not said"
	[ "$(B lock status)" = lock=free ] || bad "stop: a stopped command's lock was not released"
	outlived "$(cat "${E:?}/gc.stop")" "stop: the stopped command's grandchild outlived the lock"
	# A command that exits and leaves work running in its group (its line
	# due after 3 s, past the 1 to 2 s it is given): the work is stopped
	# before the lock is free. Checked below, once it would have written.
	_st=0
	B lock run left -- sh -c 'sh "$1" "$2" "$3" left 3 & echo left-end >> "$3"' x "${E:?}/gc.sh" "${E:?}/gc.left" "${E:?}/log" 2> "${E:?}/left.err" || _st=$?
	[ "$_st" -eq 0 ] || bad "stop: a command that left work running exited $_st"
	[ "$(B lock status)" = lock=free ] || bad "stop: the lock was not released after a command that left work running"
	grep -q '^bench: stopped what the command left running' "${E:?}/left.err" || bad "stop: the work a command left running was not said"
	_tl=$(date +%s)
	# A command that exits and leaves one that catches TERM running in its
	# group: after the 1 to 2 s it is given, the leftover gets the same grace
	# floor as on a signal (at least BENCH_STOP_GRACE + 5 s, even when that
	# is 0), so a nested bench.sh left running stops its own bounded group
	# first. Under a lock of its own (l2), checked below with s2.
	mkdir "${E:?}/l2"
	printf '%s\n' 'sh "$1" "$2" "$3" & exit 0' > "${E:?}/leave.sh"
	_t2=$(date +%s)
	start sh -c 'st=0; env BENCH_STATE="$1/state" BENCH_STOP_GRACE=0 sh "$2/bench.sh" lock run left2 -- sh "$4" "$3" "$1/pid" "$1/log" > "$1/out" 2>&1 || st=$?; echo "$st" > "$1/st"; date +%s > "$1/end"' x "${E:?}/l2" "$R" "${E:?}/stubborn.sh" "${E:?}/leave.sh"
	# At a terminal (a pty from script, when there is one): a command that
	# touches it from its background process group is said and stopped.
	if script -q /dev/null true < /dev/null > /dev/null 2>&1; then
		printf '%s\n' 'st=0; sh "$1/bench.sh" lock run tty -- sh -c "stty -echo < /dev/tty; echo after-stty" > "$2/tty.out" 2>&1 || st=$?; echo "$st" > "$2/tty.st"' > "${E:?}/tty.sh"
		start env BENCH_STOP_GRACE=0 script -q /dev/null sh "${E:?}/tty.sh" "$R" "${E:?}" < /dev/null > /dev/null 2>&1
		_h=$BG
		ended 15 "$_h" "stop: a command that touched the terminal held the lock" || true
		wait_file 10 "${E:?}/tty.st" || bad "stop: the terminal test never ended"
		[ "$(cat "${E:?}/tty.st" 2> /dev/null)" = 74 ] || bad "stop: a command that touched the terminal: wanted 74, got $(cat "${E:?}/tty.st" 2> /dev/null)"
		grep -q '^bench: the command stopped' "${E:?}/tty.out" || bad "stop: a command that touched the terminal was not said"
		grep -q after-stty "${E:?}/tty.out" && bad "stop: a command that touched the terminal went on"
		[ "$(B lock status)" = lock=free ] || bad "stop: a command that touched the terminal left the lock held"
	else
		[ "${CHECK_BENCH_TTY_REQUIRE:-0}" != 1 ] || { bad "stop: CHECK_BENCH_TTY_REQUIRE=1 and there is no working script(1) for the terminal test"; return 1; }
		echo "$me: no working script(1) here, so the terminal test is skipped"
	fi
	# The command that catches TERM, started above: killed after the floor.
	wait_file 15 "${E:?}/s2/end" || bad "stop: the lock shell did not exit after TERM to a command that ignores it"
	forget_ended "$_sl"
	_t1=$(cat "${E:?}/s2/end" 2> /dev/null || echo 0)
	[ "$(cat "${E:?}/s2/st" 2> /dev/null)" = 143 ] || bad "stop: wanted exit 143 from the stubborn command's lock shell, got $(cat "${E:?}/s2/st" 2> /dev/null)"
	[ $((_t1 - _t0)) -ge 4 ] || bad "stop: with BENCH_STOP_GRACE=0 the lock shell killed its command after $((_t1 - _t0)) s, under its floor of 5 s"
	[ $((_t1 - _t0)) -le 9 ] || bad "stop: a command that ignores TERM was stopped after $((_t1 - _t0)) s, not within the grace floor of 5 s and a margin"
	[ "$(env BENCH_STATE="${E:?}/s2/state" sh "$R/bench.sh" lock status)" = lock=free ] || bad "stop: the stubborn command's lock was not released"
	grep -q got-term "${E:?}/s2/log" || bad "stop: the stubborn command never got TERM"
	outlived "$(cat "${E:?}/s2/pid")" "stop: the stubborn command outlived the lock"
	# The command that left one that catches TERM running, started above.
	wait_file 20 "${E:?}/l2/end" || bad "stop: lock run did not return after a command left one that ignores TERM running"
	_t3=$(cat "${E:?}/l2/end" 2> /dev/null || echo 0)
	[ "$(cat "${E:?}/l2/st" 2> /dev/null)" = 0 ] || bad "stop: wanted exit 0 from a command that left one that ignores TERM running, got $(cat "${E:?}/l2/st" 2> /dev/null)"
	[ $((_t3 - _t2)) -ge 6 ] || bad "stop: with BENCH_STOP_GRACE=0 the work a command left running was killed after $((_t3 - _t2)) s, under the 1 s it is given and the floor of 5 s"
	[ "$(env BENCH_STATE="${E:?}/l2/state" sh "$R/bench.sh" lock status)" = lock=free ] || bad "stop: the lock was not released after a command left one that ignores TERM running"
	grep -q got-term "${E:?}/l2/log" || bad "stop: the work a command left running never got TERM"
	outlived "$(cat "${E:?}/l2/pid" 2> /dev/null)" "stop: the work a command left running, which ignores TERM, outlived the lock"
	# Commands that print nothing themselves: no job notice either.
	if foreign "${E:?}/stopped.out" "${E:?}/term.out" "${E:?}/left.err" > "${E:?}/foreign"; then
		cat "${E:?}/foreign"
		bad "stop: the lock shell printed a line that is not its own (a job notice), above"
	fi
	# By now each grandchild would have written its line.
	while [ "$(date +%s)" -le $((_tl + 3)) ]; do sleep 0.2; done
	if grep -q 'late$' "${E:?}/log"; then bad "stop: something wrote after the lock was free: $(grep 'late$' "${E:?}/log" | tr '\n' ' ')"; fi
	outlived "$(cat "${E:?}/gc.left" 2> /dev/null)" "stop: the work a command left running outlived the lock"
	[ "$fails" -eq 0 ]
}

# The fork of the command (bench_spawn) runs with standard error on
# /dev/null, so nothing the forked shell says before it becomes the command
# reaches the caller: under load, bash's "child setpgid (<pid> to <pid>):
# Operation not permitted" (FIX83). It is seen here with xtrace, which bash
# prints in the forked shell, for the command, on that standard error
# before the command's own redirections; with standard input open and
# closed (the two forms of the fork). The command itself gets the caller's
# standard error, fds 3 and 9 closed (standard input open), and /dev/null
# when standard error is closed.
t_spawn_stderr() {
	fresh spawn
	for _in in open closed; do
		_st=0
		if [ "$_in" = open ]; then
			env PS4='+ ' sh -x "$R/bench.sh" lock run xt -- sh -c ': fork-probe' < /dev/null > /dev/null 2> "${E:?}/xt.$_in" || _st=$?
		else
			env PS4='+ ' sh -x "$R/bench.sh" lock run xt -- sh -c ': fork-probe' <&- > /dev/null 2> "${E:?}/xt.$_in" || _st=$?
		fi
		[ "$_st" -eq 0 ] || bad "spawn: the xtrace run (standard input $_in) exited $_st"
		grep -q 'bench_spawn sh -c .*fork-probe' "${E:?}/xt.$_in" || bad "spawn: the xtrace run (standard input $_in) shows no call of bench_spawn, so it shows nothing"
		if grep -q "^+* sh -c '*: fork-probe'*\$" "${E:?}/xt.$_in"; then bad "spawn: the forked shell's own lines reach the caller's standard error (standard input $_in)"; fi
	done
	_out=$(B lock run err -- sh -c 'echo to-stderr >&2; (: >&3) 2> /dev/null && echo fd3-open; (: >&9) 2> /dev/null && echo fd9-open' < /dev/null 2>&1 3< /dev/null 9> /dev/null)
	[ "$_out" = to-stderr ] || bad "spawn: wanted the command's standard error and fds 3 and 9 closed, got: $_out"
	_st=0
	B lock run noerr -- sh -c 'echo ran > "$1"; echo to-stderr >&2' x "${E:?}/noerr" 2>&- || _st=$?
	[ "$_st" -eq 0 ] && [ "$(cat "${E:?}/noerr" 2> /dev/null)" = ran ] || bad "spawn: with standard error closed, wanted the command run with /dev/null for it (status 0 and its line), got status $_st"
	# foreign itself: it masks the bench root and the scratch directory, it
	# fails closed on a file it cannot read, and the file name is not part
	# of the 200 characters of the line.
	printf '%s\n' "$REAL/core/common.sh: line 9: x: ${tmp:?}/y: nope" > "${E:?}/f.one"
	_out=$(foreign "${E:?}/f.one") || _out=
	case $_out in
	*"f.one: <bench>/core/common.sh: line 9: x: <tmp>/y: nope") ;;
	*) bad "spawn: foreign did not mask the bench root and the scratch directory, got: $_out" ;;
	esac
	case $_out in *"$REAL"* | *"$tmp"*) bad "spawn: foreign printed a path" ;; esac
	foreign "${E:?}/f.one" "${E:?}/f.missing" > "${E:?}/f.out" || bad "spawn: foreign passed over a file it cannot read"
	grep -q 'cannot read f.missing' "${E:?}/f.out" || bad "spawn: foreign did not name the file it cannot read"
	awk 'BEGIN { for (i = 0; i < 200; i++) printf "z"; print "END" }' > "${E:?}/f.long"
	foreign "${E:?}/f.long" > "${E:?}/f.out" || true
	case $(cat "${E:?}/f.out") in *"f.long: zzzz"*) ;; *) bad "spawn: foreign did not show the line after the file name" ;; esac
	[ "$(awk '{ print length($0) }' "${E:?}/f.out")" -eq $((${#me} + 4 + 6 + 2 + 200)) ] || bad "spawn: foreign counted the file name in the 200 characters"
	[ "$fails" -eq 0 ]
}

t_lock_defer() {
	fresh defer
	grandchild "${E:?}"
	: > "${E:?}/log"
	# A TERM that arrives between the "&" and the stored pid is held and
	# acted on once the pid is stored: the command is stopped, not lost.
	_s=${tmp:?}/defer-bench
	rm -rf "${_s:?}"
	cp -R "$R" "$_s"
	lit "${_s:?}/core/common.sh" 'BENCH_SPAWN_PID=$!' 'sleep 1; BENCH_SPAWN_PID=$!' || bad "defer: the injected stall does not apply"
	start env BENCH_STOP_GRACE=1 sh "$_s/bench.sh" lock run defer -- sh "${E:?}/gc.sh" "${E:?}/d.gc" "${E:?}/log" defer 4 > "${E:?}/defer.out" 2>&1
	_h=$BG
	wait_file 5 "${E:?}/d.gc" || bad "defer: the grandchild never started"
	kill -TERM "$_h"
	_st=0
	if ended 15 "$_h" "defer: the lock shell did not exit on a TERM in the window"; then collect "$_h" || _st=$?; fi
	[ "$_st" -eq 143 ] || bad "defer: wanted exit 143 on a TERM in the window, got $_st"
	[ "$(B lock status)" = lock=free ] || bad "defer: the lock was not released"
	outlived "$(cat "${E:?}/d.gc")" "defer: a TERM in the window left the command's grandchild running"
	if grep -q 'defer-late$' "${E:?}/log"; then bad "defer: the command ran on after a TERM in the window"; fi
	[ "$fails" -eq 0 ]
}

t_bounded() {
	fresh bounded
	grandchild "${E:?}"
	: > "${E:?}/log"
	(
		BENCH_ROOT=$R
		. "$R/core/common.sh"
		BENCH_STOP_GRACE=1
		bfail() {
			echo "$me: $*"
			exit 1
		}
		# A command that exits and leaves work running in its group (its
		# line due after 2.5 s, past the 1 to 2 s it is given): the work is
		# stopped before the bound returns. Checked at the end, once it
		# would have written.
		_st=0
		bench_bounded 5 sh -c 'sh "$1" "$2" "$3" bleft 2.5 &' x "${E:?}/gc.sh" "${E:?}/b.left" "${E:?}/log" 2> "${E:?}/bleft.err" || _st=$?
		[ "$_st" -eq 0 ] || { echo "$me: bounded: a command that left work running gave $_st, not 0"; exit 1; }
		grep -q '^bench: stopped what the command left running' "${E:?}/bleft.err" || { echo "$me: bounded: the work a command left running was not said"; exit 1; }
		_tl=$(date +%s)
		# The wall bound (stops between 1 and 2.5 s) stops the command's
		# grandchild too.
		_st=0
		bench_bounded 1 sh "${E:?}/gc.sh" "${E:?}/b.gc" "${E:?}/log" wall 3.5 2> /dev/null || _st=$?
		[ "$_st" -eq 124 ] || { echo "$me: bounded: wanted 124 from a command over its wall time, got $_st"; exit 1; }
		outlived "$(cat "${E:?}/b.gc" 2> /dev/null)" "bounded: a grandchild outlived the wall bound" bfail
		# A command killed by a signal gives its status, and no job notice
		# (it names an internal path) reaches standard error.
		_st=0
		bench_bounded 5 sh -c 'sleep 0.3; kill -KILL $$' 2> "${E:?}/killed.err" || _st=$?
		[ "$_st" -eq 137 ] || { echo "$me: bounded: wanted 137 from a command killed by KILL, got $_st"; exit 1; }
		[ ! -s "${E:?}/killed.err" ] || { echo "$me: bounded: a job notice reached standard error"; exit 1; }
		# A command that ignores TERM is killed after the grace: a bound
		# (a wall time of 0 stops it within 1.5 s, the grace adds 1 to 2).
		_t0=$(date +%s)
		_st=0
		bench_bounded 0 sh -c 'trap "" TERM; sleep 8' 2> /dev/null || _st=$?
		_t1=$(date +%s)
		[ "$_st" -eq 124 ] || { echo "$me: bounded: wanted 124 from a command that ignores TERM, got $_st"; exit 1; }
		[ $((_t1 - _t0)) -le 5 ] || { echo "$me: bounded: a command that ignores TERM ran $((_t1 - _t0)) s past a wall time of 0 s"; exit 1; }
		_st=0
		bench_bounded 5 sh -c 'exit 7' || _st=$?
		[ "$_st" -eq 7 ] || { echo "$me: bounded: wanted the command's status 7, got $_st"; exit 1; }
		# A command that is stopped (a STOP to its own group, once its
		# grandchild runs) is said, stopped with its group, and gives 74
		# well inside its wall time; nothing of it runs on.
		_st=0
		bench_bounded 20 sh -c 'echo $$ > "$4"; sh "$1" "$2" "$3" bstop 30 & while [ ! -s "$2" ]; do sleep 0.05; done; kill -s STOP -- "-$$"; echo bstop-went-on >> "$3"' x "${E:?}/gc.sh" "${E:?}/b.stop" "${E:?}/log" "${E:?}/b.leader" 2> "${E:?}/bstop.err" || _st=$?
		[ "$_st" -eq 74 ] || { echo "$me: bounded: wanted 74 from a stopped command, got $_st"; exit 1; }
		grep -q '^bench: the command stopped' "${E:?}/bstop.err" || { echo "$me: bounded: a stopped command was not said"; exit 1; }
		outlived "$(cat "${E:?}/b.leader" 2> /dev/null)" "bounded: a stopped command outlived the bound" bfail
		outlived "$(cat "${E:?}/b.stop" 2> /dev/null)" "bounded: a stopped command's grandchild outlived the bound" bfail
		if grep -q bstop-went-on "${E:?}/log"; then echo "$me: bounded: a stopped command went on after the bound stopped it"; exit 1; fi
		while [ "$(date +%s)" -le $((_tl + 3)) ]; do sleep 0.2; done
		if grep -q 'late$' "${E:?}/log"; then echo "$me: bounded: something wrote after the bound returned: $(grep 'late$' "${E:?}/log" | tr '\n' ' ')"; exit 1; fi
		outlived "$(cat "${E:?}/b.left" 2> /dev/null)" "bounded: the work a command left running outlived the bound" bfail
	) || bad "bounded: the wall-time bound does not stop the whole command"
	[ "$fails" -eq 0 ]
}

# FIX172: the core signals only the command bench_spawn started, or the
# process group it led at spawn. core_decoy <name> [deaf] starts a shell of
# this check's own, as bench_spawn starts a command (the leader of its own
# process group), with a sleep in that group, from a subshell that exits at
# once, so it is no child of this shell, as a process that took a reused
# pid would not be. Both end by themselves within 30 s; "deaf" ones ignore
# TERM. It sets _dp (the shell), _dm (the sleep) and _ds (the shell's start
# time, bench_proc_read's form), and records both pids.
core_decoy() {
	rm -f "${E:?}/m.$1" "${E:?}/d.$1"
	_dt=
	if [ "${2:-}" = deaf ]; then _dt='trap "" TERM; '; fi
	(
		bench_spawn sh -c "$_dt"'sleep 30 & printf "%s\n" "$!" > "$1"; wait' x "${E:?}/m.$1"
		printf '%s\n' "$BENCH_SPAWN_PID" > "${E:?}/d.$1"
	) < /dev/null > /dev/null 2>&1
	wait_file 5 "${E:?}/m.$1" && wait_file 5 "${E:?}/d.$1" || return 1
	_dp=$(cat "${E:?}/d.$1")
	_dm=$(cat "${E:?}/m.$1")
	record "$_dp"
	record "$_dm"
	bench_proc_read "$_dp" || return 1
	_ds=$BENCH_PS_START
}
# core_decoy_end <pid>...: KILLs each pid that is still the process recorded
# (listed_same), and waits for it to go.
core_decoy_end() {
	for _de in "$@"; do
		if listed_same "$_de"; then command kill -KILL "$_de" 2> /dev/null || true; fi
		wait_gone 5 "$_de" || nok "a decoy ($_de) still ran 5 s after its KILL"
	done
}
# t_core_id: a pid whose identity is not the one bench_spawn recorded (a
# decoy given the slot with another start time, with none, or while ps is
# silent) gets no signal from bench_watch, bench_stop_group, bench_end_group
# or bench_lock_stop_child, and the watch and the stop say when they cannot
# tell; the command's own pid and group are still stopped, as a group when
# it leads one, and by its pid alone when it leads none. kill only notes
# what it sends, and sends it: every process here is this check's own.
t_core_id() {
	fresh coreid
	: > "${E:?}/sig"
	(
		BENCH_ROOT=$R
		. "$R/core/common.sh"
		. "$R/core/lock.sh"
		BENCH_STOP_GRACE=1
		_f=0
		nok() {
			echo "$me: core-id: $*"
			_f=$((_f + 1))
			if [ -n "$MUTANT" ]; then exit 1; fi
		}
		# sig_none <case>: no signal went out since the last check.
		sig_none() {
			if [ -s "${E:?}/sig" ]; then nok "$1: a signal went out: $(tr '\n' ' ' < "${E:?}/sig")"; fi
			: > "${E:?}/sig"
		}
		_other='Thu Jan 1 00:00:00 1970'
		if bench_proc_read 1; then _other=$BENCH_PS_START; fi
		core_decoy d1 || { echo "$me: core-id: the first decoy did not start"; exit 1; }
		_d=$_dp _dm1=$_dm _ds1=$_ds
		core_decoy d2 || { echo "$me: core-id: the second decoy did not start"; exit 1; }
		_d2=$_dp _dm2=$_dm _ds2=$_ds
		core_decoy d3 deaf || { echo "$me: core-id: the deaf decoy did not start"; exit 1; }
		_d3=$_dp _dm3=$_dm _ds3=$_ds
		core_decoy d4 deaf || { echo "$me: core-id: the second deaf decoy did not start"; exit 1; }
		_d4=$_dp _dm4=$_dm _ds4=$_ds
		[ "$_ds1" != "$_other" ] || { echo "$me: core-id: the decoy has PID 1's start time"; exit 1; }
		kill() {
			case $1 in
			-0) ;;
			*) printf '%s\n' "$*" >> "${E:?}/sig" ;;
			esac
			command kill "$@"
		}

		# The command's own identity at spawn, and the lock's stop of it:
		# TERM to its group, nothing to its pid alone.
		bench_spawn sh -c 'sleep 30 & printf "%s\n" "$!" > "$1"; wait' x "${E:?}/m.own" < /dev/null
		_o=$BENCH_SPAWN_PID
		record "$_o"
		wait_file 5 "${E:?}/m.own" || nok "spawn: the command's group member never started"
		_om=$(cat "${E:?}/m.own" 2> /dev/null) || _om=
		record "$_om"
		bench_proc_read "$_o" || nok "spawn: ps gave no start time for the command"
		[ "$BENCH_SPAWN_START" = "$BENCH_PS_START" ] || nok "spawn: the start time recorded ($BENCH_SPAWN_START) is not the command's ($BENCH_PS_START)"
		[ "$BENCH_SPAWN_GROUP" = "$_o" ] || nok "spawn: the group recorded ($BENCH_SPAWN_GROUP) is not the one the command leads ($_o)"
		case $BENCH_SPAWNED in
		*";$_o|$_o|$BENCH_PS_START") ;;
		*) nok "spawn: BENCH_SPAWNED does not end with the command's entry: $BENCH_SPAWNED" ;;
		esac
		_st=0
		bench_watch "$_o" "$(($(date +%s) - 1))" 0.05 || _st=$?
		[ "$_st" -eq 1 ] || nok "watch: own: the running command was not seen running past its end (status $_st)"
		: > "${E:?}/sig"
		BENCH_LOCK_CHILD=$_o
		BENCH_LOCK_REAPED=
		bench_lock_stop_child
		wait_gone 5 "$_o" || nok "lock: own: the command was not stopped"
		wait_gone 5 "$_om" || nok "lock: own: the command's group member outlived the stop"
		grep -qx -- "-s TERM -- -$_o" "${E:?}/sig" || nok "lock: own: no TERM went to the command's group: $(tr '\n' ' ' < "${E:?}/sig")"
		if grep -q -- "^-s [A-Z]* $_o\$" "${E:?}/sig"; then nok "lock: own: a signal went to the command's pid alone"; fi
		: > "${E:?}/sig"

		# A decoy in the slot, with another start time: gone to the watch,
		# and no signal from any stop, with or without a group, reaped or
		# not, nor from the lock's stop, nor for a pid never spawned.
		BENCH_SPAWNED=";$_d|$_d|$_other"
		_t0=$(date +%s)
		_st=0
		bench_watch "$_d" "$((_t0 + 3))" 0.05 || _st=$?
		[ "$_st" -eq 0 ] || nok "watch: other: a pid with another start time was not taken as gone (status $_st)"
		bench_stop_group "$_d"
		sig_none "stop: other"
		_st=0
		bench_end_group "$_d" 1 || _st=$?
		[ "$_st" -eq 0 ] || nok "end-group: other: a group led by a pid with another start time was taken as the command's (status $_st)"
		sig_none "end-group: other"
		BENCH_LOCK_CHILD=$_d
		BENCH_LOCK_REAPED=
		bench_lock_stop_child
		sig_none "lock: other"
		BENCH_SPAWNED=";$_d|-|$_other"
		bench_stop_group "$_d"
		sig_none "stop: other, no group"
		BENCH_SPAWNED=";$_d|-|$_ds1"
		bench_stop_group "$_d" "" group
		sig_none "stop: no group, reaped"
		_st=0
		bench_end_group "$_d" 1 || _st=$?
		[ "$_st" -eq 0 ] || nok "end-group: no group: a pid that led no group at spawn had its group stopped (status $_st)"
		sig_none "end-group: no group"
		BENCH_SPAWNED=
		bench_stop_group "$_d"
		sig_none "stop: never spawned"

		# Cannot tell, for a command that led no group: no start time read
		# at spawn, or ps silent now. The watch says so and goes on to its
		# end; the stop says so at once, sends nothing and returns 2.
		for _how in none nops; do
			if [ "$_how" = none ]; then
				BENCH_SPAWNED=";$_d|-|?"
			else
				BENCH_SPAWNED=";$_d|-|$_ds1"
				ps() { return 1; }
			fi
			_st=0
			bench_watch "$_d" "$(($(date +%s) - 1))" 0.05 2> "${E:?}/w.$_how" || _st=$?
			[ "$_st" -eq 1 ] || nok "watch: $_how: wanted 1 at its end, got $_st"
			grep -q 'cannot tell whether pid.*it is not signalled while that lasts' "${E:?}/w.$_how" || nok "watch: $_how: the watch did not say it cannot tell, and sends nothing"
			_t0=$(date +%s)
			_st=0
			bench_stop_group "$_d" 5 2> "${E:?}/s.$_how" || _st=$?
			_t1=$(date +%s)
			unset -f ps
			[ "$_st" -eq 2 ] || nok "stop: $_how: wanted 2 (could not tell), got $_st"
			grep -q 'cannot tell whether pid' "${E:?}/s.$_how" || nok "stop: $_how: the stop did not say it cannot tell"
			[ $((_t1 - _t0)) -lt 3 ] || nok "stop: $_how: the stop took $((_t1 - _t0)) s of its 5 s grace after it could not tell"
			sig_none "stop: $_how"
		done
		pid_running "$_d" || nok "decoy: the first decoy was ended"
		pid_running "$_dm1" || nok "decoy: the first decoy's group member was ended"

		# A command that led no group, still itself: TERM to its pid alone.
		BENCH_SPAWNED=";$_d2|-|$_ds2"
		bench_stop_group "$_d2"
		grep -qx -- "-s TERM $_d2" "${E:?}/sig" || nok "stop: no group: no TERM went to the pid: $(tr '\n' ' ' < "${E:?}/sig")"
		if grep -q -- " -$_d2\$" "${E:?}/sig"; then nok "stop: no group: a signal went to a group"; fi
		wait_gone 5 "$_d2" || nok "stop: no group: the pid was not stopped"
		pid_running "$_dm2" || nok "stop: no group: a process in the pid's group was ended"
		: > "${E:?}/sig"

		# ps goes silent once the TERM has gone to the pid of a command
		# that led no group and ignores it: no KILL, it is said, and the
		# stop returns 2.
		BENCH_SPAWNED=";$_d3|-|$_ds3"
		ps() {
			if grep -q TERM "${E:?}/sig"; then return 1; fi
			command ps "$@"
		}
		_st=0
		bench_stop_group "$_d3" 1 2> "${E:?}/k.err" || _st=$?
		unset -f ps
		[ "$_st" -eq 2 ] || nok "stop: deaf: wanted 2 (could not tell), got $_st"
		grep -qx -- "-s TERM $_d3" "${E:?}/sig" || nok "stop: deaf: no TERM went to the pid: $(tr '\n' ' ' < "${E:?}/sig")"
		if grep -q KILL "${E:?}/sig"; then nok "stop: deaf: a KILL went out once ps was silent"; fi
		grep -q 'cannot tell whether pid' "${E:?}/k.err" || nok "stop: deaf: the stop did not say it cannot tell before its KILL"
		pid_running "$_d3" || nok "stop: deaf: the deaf decoy was ended"
		: > "${E:?}/sig"

		# The same with a command that leads its own group (FIX172, round
		# 1): the group is the command's while it has a member, so the
		# KILL still goes to it once ps is silent, and the group is gone.
		BENCH_SPAWNED=";$_d4|$_d4|$_ds4"
		ps() {
			if grep -q TERM "${E:?}/sig"; then return 1; fi
			command ps "$@"
		}
		_st=0
		bench_stop_group "$_d4" 1 2> "${E:?}/k4.err" || _st=$?
		unset -f ps
		[ "$_st" -eq 0 ] || nok "stop: deaf group: wanted 0 (stopped), got $_st"
		grep -qx -- "-s KILL -- -$_d4" "${E:?}/sig" || nok "stop: deaf group: no KILL went to the group once ps was silent: $(tr '\n' ' ' < "${E:?}/sig")"
		if grep -q -- "^-s [A-Z]* $_d4\$" "${E:?}/sig"; then nok "stop: deaf group: a signal went to the pid alone"; fi
		if grep -q 'cannot tell whether pid' "${E:?}/k4.err"; then nok "stop: deaf group: the stop said it sends nothing more, and it went on"; fi
		wait_gone 5 "$_d4" || nok "stop: deaf group: the leader outlived the KILL"
		wait_gone 5 "$_dm4" || nok "stop: deaf group: the group member outlived the KILL"
		: > "${E:?}/sig"

		# ps gives no answer at all (FIX172, round 1, security I1): a
		# command that leads its own group is still stopped past its wall
		# time, as a group, and the bound says so truly.
		ps() { return 1; }
		_t0=$(date +%s)
		_st=0
		bench_bounded 1 sh -c 'printf "%s\n" "$$" > "$1"; exec sleep 8' x "${E:?}/b.pid" 2> "${E:?}/b.err" || _st=$?
		_t1=$(date +%s)
		unset -f ps
		_bp=$(cat "${E:?}/b.pid" 2> /dev/null) || _bp=
		record "$_bp"
		[ "$_st" -eq 124 ] || nok "bounded: ps silent: wanted 124 past the wall time, got $_st"
		[ $((_t1 - _t0)) -lt 6 ] || nok "bounded: ps silent: the bound took $((_t1 - _t0)) s for a wall time of 1 s"
		grep -qx -- "-s TERM -- -$_bp" "${E:?}/sig" || nok "bounded: ps silent: no TERM went to the command's group: $(tr '\n' ' ' < "${E:?}/sig")"
		wait_gone 2 "$_bp" || nok "bounded: ps silent: the command outlived its bound"
		grep -q 'stopped a command that outlived its wall time' "${E:?}/b.err" || nok "bounded: ps silent: the bound did not say it stopped the command"
		if grep -q 'could not be stopped\|sends it no more signals\|it is not signalled while that lasts' "${E:?}/b.err"; then nok "bounded: ps silent: the bound said it could not stop, or would not signal, a command it stopped"; fi
		grep -q 'a stop goes to its process group while that has a member' "${E:?}/b.err" || nok "bounded: ps silent: the watch did not say the stop still goes to the group"
		: > "${E:?}/sig"

		# A command that exits before the spawn's first ps reads it
		# (correctness I1; a slow ps under load): its group is recorded
		# while it has a member, and bench_end_group stops what it left.
		ps() {
			sleep 0.3
			command ps "$@"
		}
		bench_spawn sh -c 'sleep 20 & printf "%s\n" "$!" > "$1"; exit 0' x "${E:?}/e.pid" < /dev/null
		_ep=$BENCH_SPAWN_PID
		unset -f ps
		bench_undefer
		wait "$_ep" 2> /dev/null || :
		wait_file 5 "${E:?}/e.pid" || nok "end-group: gone: the command's group member never started"
		_em=$(cat "${E:?}/e.pid" 2> /dev/null) || _em=
		record "$_em"
		[ "$BENCH_SPAWN_GROUP" = "$_ep" ] || nok "spawn: gone: the group of a command that exited before ps read it was not recorded ($BENCH_SPAWN_GROUP|$BENCH_SPAWN_START)"
		_st=0
		bench_end_group "$_ep" 1 || _st=$?
		[ "$_st" -eq 1 ] || nok "end-group: gone: wanted 1 (it stopped what was left), got $_st"
		wait_gone 5 "$_em" || nok "end-group: gone: the group member outlived bench_end_group"
		: > "${E:?}/sig"

		unset -f kill

		# The record (FIX184): a command recorded as one that could not be
		# stopped keeps the lock alive while its pid answers kill -0 and ps
		# shows it with the recorded start time, or cannot tell; a stale
		# record (its pid gone, or another start time) holds nothing, and
		# goes with the lock. The holder's shell and command here are a pid
		# that has exited, so the record alone decides.
		bench_lock_init
		sh -c 'exit 0' &
		_gp=$!
		wait "$_gp" 2> /dev/null || :
		# srec <token> <pid> <start> [<written>]: a lock held by <token>, with
		# one record, written at <written> (bench_proc_read's form; the
		# recorded command's own start when not given, empty when empty).
		srec() {
			mkdir -p "${BENCH_LOCK_DIR:?}"
			rm -f "${BENCH_LOCK_DIR:?}"/untold.*
			printf 'label=stale\npid=%s\nchild=%s\nstart=x\ntoken=%s\n' "$_gp" "$_gp" "$1" > "${BENCH_LOCK_DIR:?}/holder"
			printf 'pid=%s\nstart=%s\nwritten=%s\ntoken=%s\n' "$2" "$3" "${4-$_ds1}" "$1" > "${BENCH_LOCK_DIR:?}/untold.$2"
		}
		# unknown: a record with no start time, written when its pid's
		# process had started (not later than that), holds; so does one with
		# no written time (FIX184, round 1).
		for _how in same unknown unwritten nops; do
			case $_how in
			same) srec t-stale "$_d" "$_ds1" ;;
			unknown) srec t-stale "$_d" '?' ;;
			unwritten) srec t-stale "$_d" '?' '' ;;
			nops)
				srec t-stale "$_d" "$_ds1"
				ps() { return 1; }
				;;
			esac
			bench_lock_alive "$(bench_lock_holder)" || nok "record: $_how: a live recorded pid did not keep the lock alive"
			if bench_lock_reclaim t-stale 2> /dev/null; then nok "record: $_how: a waiter reclaimed the lock while a recorded pid may still run"; fi
			[ -d "$BENCH_LOCK_DIR" ] || nok "record: $_how: the lock was removed while a recorded pid may still run"
			rm -f "${BENCH_LOCK_DIR:?}/holder"
			if bench_lock_reclaim "" 2> /dev/null; then nok "record: $_how: a lock with no holder file was reclaimed while a recorded pid may still run"; fi
			unset -f ps
		done
		srec t-stale "$_d" "$_ds1"
		_l=$(bench_lock_status)
		case $_l in
		"lock=held label=stale pid=$_gp child=$_gp alive=yes since=x untold=$_d") ;;
		*) nok "record: lock status does not show the live recorded pid: $_l" ;;
		esac
		# Only a live record holds a lock with no holder file: lock status
		# names it (FIX184, round 1).
		srec t-stale "$_d" '?'
		rm -f "${BENCH_LOCK_DIR:?}/holder"
		_l=$(bench_lock_status)
		case $_l in
		"lock=held label=none holder=unwritten untold=$_d") ;;
		*) nok "record: lock status does not name the record that holds a lock with no holder file: $_l" ;;
		esac
		# reused (FIX184, round 1): a record with no start time, written
		# before its pid's process started, while ps answers: that process
		# is not the recorded command (the lock shell was KILLed, and the
		# pid taken again), and the record holds nothing.
		for _how in other gone reused; do
			case $_how in
			other) srec t-stale "$_d" "$_other" ;;
			gone) srec t-stale "$_gp" "$_ds1" ;;
			reused) srec t-stale "$_d" '?' 'Sat Jan 1 00:00:00 2000' ;;
			esac
			if bench_lock_alive "$(bench_lock_holder)"; then nok "record: $_how: a stale record kept the lock alive"; fi
			bench_lock_reclaim t-stale 2> /dev/null || nok "record: $_how: a lock kept only by a stale record was not reclaimed"
			[ ! -e "$BENCH_LOCK_DIR" ] || nok "record: $_how: the reclaim left the lock directory: $(ls "$BENCH_LOCK_DIR" | tr '\n' ' ')"
		done
		# This shell's own lock, with a stale record: the release frees it
		# at once.
		srec t-own "$_gp" "$_ds1"
		BENCH_LOCK_OWNED=1 BENCH_LOCK_MINE=t-own
		_t0=$(date +%s)
		bench_lock_release 2> "${E:?}/rel.err"
		_t1=$(date +%s)
		BENCH_LOCK_MINE=
		[ ! -e "$BENCH_LOCK_DIR" ] || nok "record: release: a stale record kept this shell's lock"
		[ $((_t1 - _t0)) -lt 2 ] || nok "record: release: the release took $((_t1 - _t0)) s over a stale record"
		if [ -s "${E:?}/rel.err" ]; then nok "record: release: the release said it held for a stale record: $(cat "${E:?}/rel.err")"; fi
		# What a failed case above left, so the cases below start free.
		rm -rf "${BENCH_LOCK_DIR:?}"

		# The window (FIX184): bench_bounded clears its pid before it forgets
		# the command, so a signal between the two finds no bounded command,
		# not a pid the table no longer knows (whose stop would send nothing
		# and hold nothing). A forget that looks at the bounded pid stands in
		# for that signal.
		: > "${E:?}/window"
		(
			eval "$(sed -n '/^bench_spawn_forget() {/,/^}/p' "$R/core/common.sh" | sed 's/^bench_spawn_forget()/core_forget()/')"
			bench_spawn_forget() {
				core_forget "$1"
				if [ -n "$BENCH_BOUNDED_PID" ] && ! bench_spawned "$BENCH_BOUNDED_PID"; then
					printf '%s\n' "$BENCH_BOUNDED_PID" >> "${E:?}/window"
				fi
			}
			bench_bounded 1 sh -c 'exec sleep 8' 2> /dev/null || :
			bench_bounded 5 true || :
		)
		[ ! -s "${E:?}/window" ] || nok "window: bench_bounded forgot a command while its pid was still set: a signal then finds a pid the table no longer knows"

		# lock run, ps silent (FIX172, round 1, security I1): the command
		# leads its own group, so a TERM to the lock shell stops it before
		# the lock is free.
		psshim 1
		start env PATH="$E/shim:$PATH" sh "$R/bench.sh" lock run psg -- sh -c 'printf "%s\n" "$$" > "$1"; exec sleep 23' x "${E:?}/l.pid" > "${E:?}/l.out" 2>&1
		_lh=$BG
		wait_file 5 "${E:?}/l.pid" || nok "lock: ps silent: the command never started"
		_lp=$(cat "${E:?}/l.pid" 2> /dev/null) || _lp=
		record "$_lp"
		command kill -TERM "$_lh"
		wait_gone 15 "$_lh" || nok "lock: ps silent: the lock shell did not exit on TERM"
		_st=0
		collect "$_lh" || _st=$?
		[ "$_st" -eq 143 ] || nok "lock: ps silent: wanted 143 on TERM, got $_st"
		outlived "$_lp" "lock: ps silent: the command outlived the lock" nok
		[ "$(B lock status)" = lock=free ] || nok "lock: ps silent: TERM did not release the lock"
		wait_gone 2 "$_lp" || :

		# lock run, ps silent, and the command leads no group (dash with no
		# terminal): it is never signalled, the stop says so on the signal
		# path too (security N1), and the lock is kept until it ends.
		start env PATH="$E/shim:$PATH" dash "$R/bench.sh" lock run psn -- sh -c 'printf "%s\n" "$$" > "$1"; exec sleep 3' x "${E:?}/n.pid" > "${E:?}/n.out" 2>&1
		_nh=$BG
		wait_file 5 "${E:?}/n.pid" || nok "lock: no group: the command never started"
		_np=$(cat "${E:?}/n.pid" 2> /dev/null) || _np=
		record "$_np"
		# The TERM comes while the lock shell watches the command, so its
		# trap runs with the watch's standard error on /dev/null.
		_n=0
		while ! grep -q 'it is not signalled while that lasts' "${E:?}/n.out" && [ "$_n" -lt 50 ]; do
			sleep 0.1
			_n=$((_n + 1))
		done
		command kill -TERM "$_nh"
		# At most 1.5 s for the hold's line, well inside the command's 3 s,
		# so the lock is still looked at while the command runs.
		_n=0
		while ! grep -q 'keeping the lock while pid' "${E:?}/n.out" && pid_running "$_nh" && [ "$_n" -lt 15 ]; do
			sleep 0.1
			_n=$((_n + 1))
		done
		_l=$(B lock status)
		if pid_running "$_np"; then
			case $_l in
			lock=held*) ;;
			*) nok "lock: no group: the lock was freed while the command it could not stop still ran: $_l" ;;
			esac
		else
			nok "lock: no group: the command was gone before the check of the lock"
		fi
		wait_gone 15 "$_nh" || nok "lock: no group: the lock shell did not exit after the command ended"
		_st=0
		collect "$_nh" || _st=$?
		[ "$_st" -eq 143 ] || nok "lock: no group: wanted 143 on TERM, got $_st"
		outlived "$_np" "lock: no group: the lock was freed while the command still ran" nok
		[ "$(B lock status)" = lock=free ] || nok "lock: no group: the lock was not freed once the command ended"
		grep -q 'cannot tell whether pid.*so the stop sends it no more signals' "${E:?}/n.out" || nok "lock: no group: the stop's line did not reach the lock shell's standard error: $(tr '\n' '|' < "${E:?}/n.out")"
		grep -q 'keeping the lock while pid' "${E:?}/n.out" || nok "lock: no group: the hold was not said"
		wait_gone 2 "$_np" || :

		# bench_bounded, ps silent, a command that leads no group: not
		# signalled, never said to be stopped, and 125 within its bound.
		_t0=$(date +%s)
		_st=0
		env PATH="$E/shim:$PATH" dash -c 'BENCH_ROOT=$1; . "$1/core/common.sh"; BENCH_STOP_GRACE=1; st=0; bench_bounded 1 sh -c "printf \"%s\\n\" \"\$\$\" > \"\$1\"; exec sleep 3" x "$2" || st=$?; exit "$st"' x "$R" "${E:?}/q.pid" > "${E:?}/q.out" 2>&1 || _st=$?
		_t1=$(date +%s)
		_qp=$(cat "${E:?}/q.pid" 2> /dev/null) || _qp=
		record "$_qp"
		[ "$_st" -eq 125 ] || nok "bounded: no group: wanted 125 (it could not stop it), got $_st: $(tr '\n' '|' < "${E:?}/q.out")"
		[ $((_t1 - _t0)) -lt 3 ] || nok "bounded: no group: the bound took $((_t1 - _t0)) s for a wall time of 1 s"
		grep -q 'could not be stopped' "${E:?}/q.out" || nok "bounded: no group: the bound did not say it could not stop the command"
		if grep -q 'stopped a command that outlived' "${E:?}/q.out"; then nok "bounded: no group: the bound said it stopped a command it sent nothing"; fi
		wait_gone 5 "$_qp" || nok "bounded: no group: the command did not end by itself"

		# A TERM to a bench.sh that runs a bounded command which leads no
		# group, while ps is silent (FIX181): under dash (no job control) the
		# lock shell and the bench.sh it runs (host, whose bounded
		# system_profiler is a stand-in here) are not signalled as groups,
		# and the stop of the bounded command can send nothing. The lock
		# stays held until the bounded command ends, then is freed. The TERM
		# goes to the inner bench.sh only, once it is seen to be the lock
		# shell's child. The hold's line goes where the bound's own lines go,
		# the standard error host gives the bound (/dev/null), so the lock's
		# status is what is looked at here; "lock run, no group" checks the
		# line itself.
		mkdir -p "${E:?}/bshim"
		printf '#!/bin/sh\nexit 1\n' > "${E:?}/bshim/ps"
		printf '#!/bin/sh\nprintf "%%s %%s\\n" "$$" "$PPID" > "$FIX181_PID.tmp"\nmv "$FIX181_PID.tmp" "$FIX181_PID"\nexec sleep 3\n' > "${E:?}/bshim/system_profiler"
		chmod +x "${E:?}/bshim/ps" "${E:?}/bshim/system_profiler"
		start env PATH="$E/bshim:$PATH" FIX181_PID="${E:?}/h.pid" dash "$R/bench.sh" lock run psb -- dash "$R/bench.sh" host > "${E:?}/h.out" 2>&1
		_hh=$BG
		wait_file 5 "${E:?}/h.pid" || nok "bounded hold: the bounded command never started"
		_hp= _hi=
		read -r _hp _hi < "${E:?}/h.pid" 2> /dev/null || :
		record "$_hp"
		record "$_hi"
		_hpp=$(ps -o ppid= -p "$_hi" 2> /dev/null | tr -d ' ') || _hpp=
		if [ -n "$_hi" ] && [ "$_hpp" = "$_hh" ]; then
			command kill -TERM "$_hi"
		else
			nok "bounded hold: the bounded command's parent ($_hi) is not the lock shell's command (its parent is ${_hpp:-unknown}, the lock shell $_hh)"
		fi
		# At most 1.5 s for the inner bench.sh's trap to run, well inside the
		# bounded command's 3 s, so the lock is looked at while it runs.
		_n=0
		while pid_running "$_hi" && [ "$_n" -lt 15 ]; do
			sleep 0.1
			_n=$((_n + 1))
		done
		_l=$(B lock status)
		if pid_running "$_hp"; then
			case $_l in
			lock=held*) ;;
			*) nok "bounded hold: the lock was freed while the bounded command it could not stop still ran: $_l" ;;
			esac
		else
			nok "bounded hold: the bounded command was gone before the check of the lock"
		fi
		wait_gone 15 "$_hh" || nok "bounded hold: the lock shell did not exit after the bounded command ended"
		_st=0
		collect "$_hh" || _st=$?
		[ "$_st" -eq 143 ] || nok "bounded hold: wanted 143 from the lock shell (its command's status on TERM), got $_st: $(tr '\n' '|' < "${E:?}/h.out")"
		outlived "$_hp" "bounded hold: the lock was freed while the bounded command still ran" nok
		[ "$(B lock status)" = lock=free ] || nok "bounded hold: the lock was not freed once the bounded command ended"
		wait_gone 2 "$_hi" || :
		wait_gone 2 "$_hp" || :

		# The same hold, then a TERM to the lock shell once ps answers again
		# (FIX181, round 1). The lock shell stops its command, the bench.sh
		# that holds, with the lock's grace (at least BENCH_STOP_GRACE + 5,
		# 6 s here); the hold renews its stop of the bounded command with
		# the bounded command's grace (BENCH_STOP_GRACE, 1 s), so its KILL
		# to the bounded command, which ignores TERM, comes before the lock
		# shell's KILL to it. Here ps is silent only for the bounded command,
		# while a flag file is there, and the flag goes 1.5 s after the
		# TERM to the lock shell: a hold that renewed its stop with the
		# lock's grace would send its KILL at 7.5 s at the earliest, after
		# the lock shell's at 7 s at the latest, and the lock shell would
		# free the lock while the bounded command still ran. Since FIX184 the
		# lock shell's release keeps the lock for the recorded bounded
		# command instead, until its own end, 15 s after its start: so the
		# lock shell must also be gone within 8 s of the flag's removal,
		# which only the hold's renewed stop achieves.
		mkdir -p "${E:?}/cshim"
		printf '#!/bin/sh\nif [ -e "$FIX181_FLAG" ] && read -r _p _q < "$FIX181_PID" 2> /dev/null; then for _a; do [ "$_a" != "$_p" ] || exit 1; done; fi\nexec /bin/ps "$@"\n' > "${E:?}/cshim/ps"
		printf '#!/bin/sh\ntrap "" TERM\nprintf "%%s %%s\\n" "$$" "$PPID" > "$FIX181_PID.tmp"\nmv "$FIX181_PID.tmp" "$FIX181_PID"\nexec sleep 15\n' > "${E:?}/cshim/system_profiler"
		chmod +x "${E:?}/cshim/ps" "${E:?}/cshim/system_profiler"
		start env PATH="$E/cshim:$PATH" BENCH_STOP_GRACE=1 FIX181_PID="${E:?}/c.pid" FIX181_FLAG="${E:?}/c.flag" dash "$R/bench.sh" lock run psc -- dash "$R/bench.sh" host > "${E:?}/c.out" 2>&1
		_ch=$BG
		wait_file 5 "${E:?}/c.pid" || nok "bounded hold, lock shell: the bounded command never started"
		_cp= _ci=
		read -r _cp _ci < "${E:?}/c.pid" 2> /dev/null || :
		record "$_cp"
		record "$_ci"
		_cpid=$(pid_id "$_cp") || _cpid=
		: > "${E:?}/c.flag"
		_cpp=$(ps -o ppid= -p "$_ci" 2> /dev/null | tr -d ' ') || _cpp=
		if [ -n "$_ci" ] && [ "$_cpp" = "$_ch" ]; then
			command kill -TERM "$_ci"
		else
			nok "bounded hold, lock shell: the bounded command's parent ($_ci) is not the lock shell's command (its parent is ${_cpp:-unknown}, the lock shell $_ch)"
		fi
		# The inner bench.sh's trap runs and its stop sends nothing (ps is
		# silent for the bounded command); it holds.
		sleep 1.5
		_l=$(B lock status)
		case $_l in
		lock=held*) ;;
		*) nok "bounded hold, lock shell: the lock was freed before the TERM to the lock shell: $_l" ;;
		esac
		command kill -TERM "$_ch"
		sleep 1.5
		rm -f "${E:?}/c.flag"
		_cf=$(date +%s)
		wait_gone 15 "$_ch" || nok "bounded hold, lock shell: the lock shell did not exit"
		_ce=$(date +%s)
		[ $((_ce - _cf)) -le 8 ] || nok "bounded hold, lock shell: the lock shell took $((_ce - _cf)) s after ps answered again: the hold did not stop the bounded command, which ran to its own end"
		_cr=0
		pid_running "$_cp" || _cr=$?
		# A command left running is ended here, by its identity, before the
		# report, which ends a mutant's run.
		if pid_same "$_cp" "$_cpid"; then command kill -KILL "$_cp" 2> /dev/null || :; fi
		case $_cr in
		0) nok "bounded hold, lock shell: the lock shell exited, and so freed the lock, while the bounded command still ran" ;;
		2) nok "bounded hold, lock shell: cannot tell whether pid $_cp outlived the lock shell, ps gives no answer for it" ;;
		esac
		collect "$_ch" || :
		[ "$(B lock status)" = lock=free ] || nok "bounded hold, lock shell: the lock was not freed once the bounded command ended"
		wait_gone 2 "$_ci" || :
		wait_gone 2 "$_cp" || :

		# A TERM to the lock shell while the bench.sh it runs holds for a
		# bounded command it could not stop (FIX184). ps is silent for the
		# bounded command (a stand-in for system_profiler that ignores TERM)
		# from the moment it has written its pid, and answers for every
		# other pid; under dash nothing leads a group. The inner bench.sh
		# records the bounded command in the lock state and holds, ignoring
		# TERM; the lock shell's stop of it ends with a KILL after the lock's
		# grace (6 s here), and the bounded command runs on. The lock stays
		# held, by the lock shell's release, until the bounded command ends,
		# then is freed; lock status shows the recorded pid meanwhile.
		mkdir -p "${E:?}/dshim"
		printf '#!/bin/sh\nif read -r _p _q < "$FIX184_PID" 2> /dev/null; then for _a; do [ "$_a" != "$_p" ] || exit 1; done; fi\nexec /bin/ps "$@"\n' > "${E:?}/dshim/ps"
		printf '#!/bin/sh\ntrap "" TERM\nprintf "%%s %%s\\n" "$$" "$PPID" > "$FIX184_PID.tmp"\nmv "$FIX184_PID.tmp" "$FIX184_PID"\nexec sleep 12\n' > "${E:?}/dshim/system_profiler"
		chmod +x "${E:?}/dshim/ps" "${E:?}/dshim/system_profiler"
		start env PATH="$E/dshim:$PATH" BENCH_STOP_GRACE=1 FIX184_PID="${E:?}/t.pid" dash "$R/bench.sh" lock run psd -- dash "$R/bench.sh" host > "${E:?}/t.out" 2>&1
		_th=$BG
		wait_file 5 "${E:?}/t.pid" || nok "lock shell TERM: the bounded command never started"
		_tp= _ti=
		read -r _tp _ti < "${E:?}/t.pid" 2> /dev/null || :
		record "$_tp"
		record "$_ti"
		_tpp=$(ps -o ppid= -p "$_ti" 2> /dev/null | tr -d ' ') || _tpp=
		if [ -n "$_ti" ] && [ "$_tpp" = "$_th" ]; then
			# The inner bench.sh is in its bound's watch by now.
			sleep 0.5
			command kill -TERM "$_th"
		else
			nok "lock shell TERM: the bounded command's parent ($_ti) is not the lock shell's command (its parent is ${_tpp:-unknown}, the lock shell $_th)"
		fi
		# The lock shell's KILL to the inner bench.sh comes 6 to 7 s after
		# the TERM; the bounded command runs until 12 s after its start.
		wait_gone 9 "$_ti" || nok "lock shell TERM: the inner bench.sh outlived the lock shell's KILL"
		sleep 0.5
		_l=$(B lock status)
		if pid_running "$_tp"; then
			case $_l in
			lock=held*" untold=$_tp") ;;
			lock=held*) nok "lock shell TERM: lock status does not show the recorded bounded command: $_l" ;;
			*) nok "lock shell TERM: the lock was freed while the bounded command it could not stop still ran: $_l" ;;
			esac
		else
			nok "lock shell TERM: the bounded command was gone before the check of the lock"
		fi
		wait_gone 15 "$_th" || nok "lock shell TERM: the lock shell did not exit after the bounded command ended"
		_st=0
		collect "$_th" || _st=$?
		[ "$_st" -eq 143 ] || nok "lock shell TERM: wanted 143 from the lock shell, got $_st: $(tr '\n' '|' < "${E:?}/t.out")"
		outlived "$_tp" "lock shell TERM: the lock was freed while the bounded command still ran" nok
		[ "$(B lock status)" = lock=free ] || nok "lock shell TERM: the lock was not freed once the bounded command ended"
		grep -q 'keeping the lock while recorded pid' "${E:?}/t.out" || nok "lock shell TERM: the release's hold was not said: $(tr '\n' '|' < "${E:?}/t.out")"
		wait_gone 2 "$_tp" || :

		# The 125 path (FIX184), with no signal at all: a direct caller of
		# bench_bounded under the lock, ps silent throughout (dash: the
		# command leads no group), whose bound passes. The stop can send
		# nothing and returns 2, the bound returns 125, and the caller exits,
		# as host.sh does with bench_die 2. The lock shell's release keeps
		# the lock until the bounded command ends.
		start env PATH="$E/shim:$PATH" dash "$R/bench.sh" lock run p125 -- dash -c 'BENCH_ROOT=$1; . "$1/core/common.sh"; . "$1/core/lock.sh"; BENCH_STOP_GRACE=1; st=0; bench_bounded 1 sh -c "printf \"%s\\n\" \"\$\$\" > \"\$1\"; exec sleep 6" x "$2" || st=$?; exit "$st"' x "$R" "${E:?}/v.pid" > "${E:?}/v.out" 2>&1
		_vh=$BG
		wait_file 5 "${E:?}/v.pid" || nok "125: the bounded command never started"
		_vp=$(cat "${E:?}/v.pid" 2> /dev/null) || _vp=
		record "$_vp"
		_n=0
		while ! grep -q 'keeping the lock while recorded pid' "${E:?}/v.out" && pid_running "$_vh" && [ "$_n" -lt 50 ]; do
			sleep 0.1
			_n=$((_n + 1))
		done
		_l=$(B lock status)
		if pid_running "$_vp"; then
			case $_l in
			lock=held*" untold=$_vp") ;;
			*) nok "125: the lock was not held for the bounded command that could not be stopped: $_l" ;;
			esac
		else
			nok "125: the bounded command was gone before the check of the lock"
		fi
		wait_gone 10 "$_vh" || nok "125: the lock shell did not exit after the bounded command ended"
		_st=0
		collect "$_vh" || _st=$?
		[ "$_st" -eq 125 ] || nok "125: wanted 125 from the lock shell (its command's status), got $_st: $(tr '\n' '|' < "${E:?}/v.out")"
		outlived "$_vp" "125: the lock was freed while the bounded command still ran" nok
		[ "$(B lock status)" = lock=free ] || nok "125: the lock was not freed once the bounded command ended"
		grep -q "recorded pid $_vp in the lock state" "${E:?}/v.out" || nok "125: the record was not said"
		wait_gone 2 "$_vp" || :

		# The identity table (correctness I2): 2000 bounded commands leave
		# no entry behind, and a lookup costs what it did before them.
		BENCH_SPAWNED=
		bench_spawn sh -c 'exec sleep 30' < /dev/null
		_tp=$BENCH_SPAWN_PID
		record "$_tp"
		bench_undefer
		_look() {
			_j=0
			while [ "$_j" -lt 100 ]; do
				bench_spawned "$_tp" || nok "table: the long command's entry is gone"
				_j=$((_j + 1))
			done
		}
		_now() { perl -MTime::HiRes=time -e 'printf "%.4f\n", time'; }
		_a0=$(_now)
		_look
		_a1=$(_now)
		_i=0
		while [ "$_i" -lt 2000 ]; do
			bench_bounded 5 true || nok "table: a bounded true failed"
			_i=$((_i + 1))
			case $_i in
			*00)
				_c=$(printf '%s' "$BENCH_SPAWNED" | tr -cd ';' | wc -c | tr -d ' ')
				if [ "$_c" -gt 1 ]; then
					nok "table: $_c entries after $_i bounded commands"
					break
				fi
				;;
			esac
		done
		_b0=$(_now)
		_look
		_b1=$(_now)
		awk -v a0="$_a0" -v a1="$_a1" -v b0="$_b0" -v b1="$_b1" 'BEGIN { exit !((b1 - b0) <= 3 * (a1 - a0) + 0.05) }' ||
			nok "table: 100 lookups took $_a0..$_a1 before 2000 bounded commands and $_b0..$_b1 after"
		BENCH_LOCK_CHILD=$_tp
		BENCH_LOCK_REAPED=
		bench_lock_stop_child
		wait_gone 5 "$_tp" || nok "table: the long command was not stopped"
		case $BENCH_SPAWNED in
		*";$_tp|"*) nok "table: the stopped command's entry was left: $BENCH_SPAWNED" ;;
		esac
		core_decoy_end "$_d" "$_dm1" "$_dm2" "$_d3" "$_dm3" "$_d4" "$_dm4"
		[ "$_f" -eq 0 ]
	) || bad "core-id: the core signals a process that is not the command it started, or does not stop the command (above)"
	[ "$fails" -eq 0 ]
}

t_lock_reclaim() {
	fresh reclaim
	mkdir -p "${E:?}/state"
	sh -c 'exit 0' &
	_dead=$!
	collect "$_dead" || true
	ghost() {
		mkdir "${E:?}/state/lock"
		printf 'label=ghost\npid=%s\nchild=\nstart=2020-01-01T00:00:00Z\ntoken=t-%s-1-ghost%s\n' "$_dead" "$_dead" "$1" > "${E:?}/state/lock/holder"
	}
	ghost 0
	_st=0
	_out=$(B lock run after -- echo ran 2> "${E:?}/err") || _st=$?
	[ "$_st" -eq 0 ] && [ "$_out" = ran ] || bad "reclaim: a lock whose holder is gone was not reclaimed (status $_st)"
	grep -q "reclaimed a stale lock: label=ghost pid=$_dead (gone)" "${E:?}/err" || bad "reclaim: not logged on standard error"
	[ "$(grep -c 'label=ghost' "${E:?}/state/lock.log")" -eq 1 ] || bad "reclaim: not logged once in lock.log"
	# Two waiters that find the same dead holder at once.
	for _i in 1 2 3; do
		ghost "$_i"
		start sh "$R/bench.sh" lock run w1 -- sh -c 'echo w1-start >> "$1"; sleep 0.5; echo w1-end >> "$1"' x "${E:?}/log$_i" > "${E:?}/w1.$_i" 2>&1
		_p1=$BG
		start sh "$R/bench.sh" lock run w2 -- sh -c 'echo w2-start >> "$1"; sleep 0.5; echo w2-end >> "$1"' x "${E:?}/log$_i" > "${E:?}/w2.$_i" 2>&1
		_p2=$BG
		wait_gone 15 "$_p1" || bad "reclaim race $_i: a waiter did not finish"
		wait_gone 15 "$_p2" || bad "reclaim race $_i: a waiter did not finish"
		_s1=0
		collect "$_p1" || _s1=$?
		_s2=0
		collect "$_p2" || _s2=$?
		[ "$_s1" -eq 0 ] && [ "$_s2" -eq 0 ] || bad "reclaim race $_i: the waiters exited $_s1 and $_s2"
		case $(tr '\n' ' ' < "${E:?}/log$_i") in
		"w1-start w1-end w2-start w2-end " | "w2-start w2-end w1-start w1-end ") ;;
		*) bad "reclaim race $_i: both waiters held the lock at once: $(tr '\n' ' ' < "${E:?}/log$_i")" ;;
		esac
		[ "$(grep -c 'label=ghost' "${E:?}/state/lock.log")" -eq $((_i + 1)) ] || bad "reclaim race $_i: not exactly one reclaim logged"
	done
	ls "${E:?}/state" | grep -q '^reclaim\.' && bad "reclaim: a reclaim guard was left behind"
	# A lock directory without a holder file, after the grace period.
	mkdir "${E:?}/state/lock"
	_st=0
	_out=$(env BENCH_LOCK_GRACE=1 sh "$R/bench.sh" lock run late -- echo ran 2> "${E:?}/err") || _st=$?
	[ "$_st" -eq 0 ] && [ "$_out" = ran ] || bad "reclaim: a holder-less lock was not reclaimed (status $_st)"
	grep -q 'reclaimed a stale lock: label=none' "${E:?}/err" || bad "reclaim: the holder-less reclaim was not logged"
	[ "$fails" -eq 0 ]
}

t_lock_guard() {
	fresh guard
	mkdir -p "${E:?}/state"
	sh -c 'exit 0' &
	_dead=$!
	collect "$_dead" || true
	# A dead holder's lock whose reclaim guard another waiter holds: left alone.
	mkdir "${E:?}/state/lock" "${E:?}/state/reclaim.t-$_dead-1-guarded"
	printf 'label=ghost\npid=%s\nchild=\nstart=2020-01-01T00:00:00Z\ntoken=t-%s-1-guarded\n' "$_dead" "$_dead" > "${E:?}/state/lock/holder"
	_st=0
	env BENCH_LOCK_WAIT=1 sh "$R/bench.sh" lock run g -- echo ran > "${E:?}/out" 2> /dev/null || _st=$?
	[ "$_st" -eq 75 ] || bad "guard: a waiter reclaimed a lock whose reclaim guard another waiter holds (status $_st)"
	[ ! -s "${E:?}/out" ] || bad "guard: the command ran past a held reclaim guard"
	grep -q '^token=t-.*-guarded$' "${E:?}/state/lock/holder" 2> /dev/null || bad "guard: the guarded lock was removed"
	rm -rf "${E:?}/state/lock" "${E:?}/state/reclaim.t-$_dead-1-guarded"
	# A holder whose two pids are gone while its command's process group
	# still has a member is alive: left alone.
	set -m
	sh -c 'sleep 30 & echo $! > "$1"' x "${E:?}/member" > /dev/null 2>&1 &
	_g=$!
	set +m
	collect "$_g" 2> /dev/null || true
	wait_file 5 "${E:?}/member" || bad "guard: the group member never started"
	_m=$(cat "${E:?}/member")
	record "$_m"
	mkdir "${E:?}/state/lock"
	printf 'label=ghost\npid=%s\nchild=%s\nstart=2020-01-01T00:00:00Z\ntoken=t-%s-1-group\n' "$_dead" "$_g" "$_dead" > "${E:?}/state/lock/holder"
	_st=0
	env BENCH_LOCK_WAIT=1 sh "$R/bench.sh" lock run g -- echo ran > "${E:?}/out" 2> /dev/null || _st=$?
	[ "$_st" -eq 75 ] || bad "guard: a lock whose command's group still runs was reclaimed (status $_st)"
	[ ! -s "${E:?}/out" ] || bad "guard: the command ran beside a holder's live group"
	case $(B lock status) in
	*' alive=yes '*) ;;
	*) bad "guard: lock status does not call a holder with a live group alive" ;;
	esac
	kill -TERM -- "-$_g" 2> /dev/null || true
	wait_gone 5 "$_m" || bad "guard: the group member outlived a TERM to its group"
	rm -rf "${E:?}/state/lock"
	# A lock directory with no holder file, inside its grace: left alone.
	mkdir "${E:?}/state/lock"
	_st=0
	env BENCH_LOCK_GRACE=5 BENCH_LOCK_WAIT=1 sh "$R/bench.sh" lock run g -- echo ran > "${E:?}/out" 2> /dev/null || _st=$?
	[ "$_st" -eq 75 ] || bad "grace: a holder-less lock was reclaimed inside its grace (status $_st)"
	[ -d "${E:?}/state/lock" ] || bad "grace: the holder-less lock was removed inside its grace"
	rmdir "${E:?}/state/lock" 2> /dev/null || true
	# BENCH_LOCK_POLL: at least 0.01 s, and cut to the wait when longer.
	for _p in 0 0.0 0.009 -1 1e1 x; do
		_st=0
		env BENCH_LOCK_POLL="$_p" sh "$R/bench.sh" lock run p -- true 2> /dev/null || _st=$?
		[ "$_st" -eq 2 ] || bad "poll: BENCH_LOCK_POLL=$_p was not refused (status $_st)"
	done
	env BENCH_LOCK_POLL=0.01 sh "$R/bench.sh" lock run p -- true || bad "poll: BENCH_LOCK_POLL=0.01 was refused"
	start sh "$R/bench.sh" lock run long -- sleep 30 > /dev/null 2>&1
	_h=$BG
	wait_held 5 long || bad "poll: the holder never showed"
	start env BENCH_LOCK_POLL=60 BENCH_LOCK_WAIT=1 sh "$R/bench.sh" lock run waiter -- true > /dev/null 2>&1
	_w=$BG
	if ! wait_gone 6 "$_w"; then
		bad "poll: a poll of 60 s against a wait of 1 s was not cut to the wait"
		kill -KILL "$_w" 2> /dev/null || true
	fi
	_st=0
	collect "$_w" 2> /dev/null || _st=$?
	[ "$_st" -eq 75 ] || bad "poll: the waiter with a long poll exited $_st, not 75"
	kill -TERM "$_h"
	wait_gone 15 "$_h" || bad "poll: the holder did not exit on TERM"
	collect "$_h" 2> /dev/null || true
	[ "$fails" -eq 0 ]
}

# stall <copy>: the copy's maker stalls between its mkdir and its holder
# write until $E/unstall exists (60 s at most), not for a fixed time that
# a loaded host can outlast before the taker has reclaimed (FIX128).
stall() {
	lit "${1:?}/core/lock.sh" 'BENCH_LOCK_SINCE=$(bench_utc)' "BENCH_LOCK_SINCE=\$(bench_utc); _fx_k=0; while [ ! -e '${E:?}/unstall' ] && [ \"\$_fx_k\" -lt 1200 ]; do sleep 0.05; _fx_k=\$((_fx_k + 1)); done"
}

t_lock_stall() {
	fresh stall
	# FIX128 review round 1: under this case's own bounds the maker waits
	# 5 + 30 + 30 + 20 s at most, more than fresh's 10 s; 300 s keeps the
	# stated polls the binding bounds.
	BENCH_LOCK_WAIT=300
	# A maker that stalls between its mkdir and its holder write for longer
	# than the grace, while a waiter reclaims the directory and makes it
	# again, backs off and leaves the taker's holder file as it was: the two
	# never hold the lock at once.
	_s=${tmp:?}/stall
	rm -rf "${_s:?}"
	cp -R "$R" "$_s"
	stall "$_s" || bad "stall: the injected stall does not apply"
	: > "${E:?}/slog"
	start sh "$_s/bench.sh" lock run maker -- sh -c 'echo N-start >> "$1"; sleep 0.3; echo N-end >> "$1"' x "${E:?}/slog" > "${E:?}/maker.out" 2>&1
	_mk=$BG
	_k=0
	while [ ! -d "${E:?}/state/lock" ] && [ "$_k" -lt 50 ]; do
		sleep 0.1
		_k=$((_k + 1))
	done
	start env BENCH_LOCK_GRACE=1 sh "$R/bench.sh" lock run taker -- sh -c 'echo C-start >> "$1"; k=0; while [ ! -e "$2" ] && [ "$k" -lt 2400 ]; do sleep 0.05; k=$((k + 1)); done; echo C-end >> "$1"' x "${E:?}/slog" "${E:?}/release" > "${E:?}/taker.out" 2>&1
	_c=$BG
	wait_held 30 taker || bad "stall: the taker never reclaimed the stalled maker's lock"
	: > "${E:?}/unstall"
	_k=0
	while ! grep -q 'waiting for the lock held by' "${E:?}/maker.out" && [ "$_k" -lt 300 ]; do
		sleep 0.1
		_k=$((_k + 1))
	done
	grep -q 'waiting for the lock held by taker ' "${E:?}/maker.out" || bad "stall: the maker that backed off does not wait for the taker: $(cat "${E:?}/maker.out")"
	case $(B lock status) in
	'lock=held label=taker '*) ;;
	*) bad "stall: the taker's holder file did not survive the stalled maker: $(B lock status)" ;;
	esac
	: > "${E:?}/release"
	_s1=0
	if ended 20 "$_mk" "stall: the stalled maker did not finish"; then collect "$_mk" || _s1=$?; fi
	_s2=0
	if ended 20 "$_c" "stall: the taker did not finish"; then collect "$_c" || _s2=$?; fi
	[ "$_s1" -eq 0 ] && [ "$_s2" -eq 0 ] || bad "stall: the maker and the taker exited $_s1 and $_s2"
	case $(tr '\n' ' ' < "${E:?}/slog") in
	"C-start C-end N-start N-end " | "N-start N-end C-start C-end ") ;;
	*) bad "stall: a stalled maker and a taker held the lock at once: $(tr '\n' ' ' < "${E:?}/slog")" ;;
	esac
	[ "$(B lock status)" = lock=free ] || bad "stall: the lock was left held"
	[ "$fails" -eq 0 ]
}

# ---- The queue (FIX106): waiters are served in arrival order. ------------
# qfix: the scripts the queue tests run, in E. rec.sh <log> <label> appends
# "<label> start <time>", sleeps 0.2 s, then "<label> end <time>" (times
# from Time::HiRes); hold.sh <file> <log> <label> does the same, holding
# until <file> exists; late.sh <file> <command...> waits for <file>, then
# becomes the command (exec), so the lock shell keeps late.sh's pid: one
# spawned first and released last arrives last with the lowest pid.
qfix() {
	cat > "${E:?}/rec.sh" << 'EOF'
t() { perl -MTime::HiRes=time -e 'printf "%.6f\n", time'; }
printf '%s start %s\n' "$2" "$(t)" >> "$1"
sleep 0.2
printf '%s end %s\n' "$2" "$(t)" >> "$1"
EOF
	cat > "${E:?}/hold.sh" << 'EOF'
t() { perl -MTime::HiRes=time -e 'printf "%.6f\n", time'; }
printf '%s start %s\n' "$3" "$(t)" >> "$2"
while [ ! -e "$1" ]; do sleep 0.05; done
printf '%s end %s\n' "$3" "$(t)" >> "$2"
EOF
	cat > "${E:?}/late.sh" << 'EOF'
while [ ! -e "$1" ]; do sleep 0.05; done
shift
exec "$@"
EOF
	: > "${E:?}/log"
}
# tickets: the names of the queue's tickets, in order, one a line.
tickets() {
	ls "${E:?}/state/queue" 2> /dev/null | grep '^w\.' || true
}
# ticket_of <pid>: the name of the ticket that carries pid=<pid>, if any.
ticket_of() {
	for _tk in $(tickets); do
		if grep -qx "pid=$1" "${E:?}/state/queue/$_tk" 2> /dev/null; then
			echo "$_tk"
			return 0
		fi
	done
	return 1
}
# wait_said <seconds> <file>: <file> says its caller waits for the lock.
wait_said() {
	_n=0
	while ! grep -q 'waiting for the lock' "$2" 2> /dev/null; do
		[ "$_n" -lt $(($1 * 10)) ] || return 1
		sleep 0.1
		_n=$((_n + 1))
	done
}
# order: the labels of the log's start lines, by their times.
order() {
	awk '$2 == "start" { print $3, $1 }' "${E:?}/log" | sort -n | awk '{ printf "%s ", $2 }'
}
# serial: by time, the log runs start, end, start, end, each pair with one
# label: no two commands held the lock at once.
serial() {
	awk '{ print $3, $1, $2 }' "${E:?}/log" | sort -n | awk '
		o == "" { if ($3 != "start") b = 1; o = $2; next }
		{ if ($3 != "end" || $2 != o) b = 1; o = "" }
		END { exit b || o != "" }'
}
# holder <label>: a holder that keeps the lock until $E/release exists.
holder() {
	start sh "$R/bench.sh" lock run "$1" -- sh "$E/hold.sh" "$E/release" "$E/log" "$1" > "${E:?}/$1.out" 2>&1
	HOLDER=$BG
	wait_held 5 "$1" || bad "queue: the holder $1 never held the lock"
}
# finish <test> <pid> <status> [<seconds>]: waits for <pid> (at most
# <seconds>, 30 by default, then reaps it), wants <status>. False, the
# failure counted by ended, when reap did not end <pid> (FIX177).
finish() {
	_st=0
	ended "${4:-30}" "$2" "$1: pid $2 did not finish" || return 1
	collect "$2" || _st=$?
	[ "$_st" -eq "$3" ] || bad "$1: pid $2 exited $_st, not $3"
}

t_lock_fifo() {
	fresh fifo
	qfix
	# A holds; B waits, with a poll of 3 s, so it naps while A releases and
	# asks again at once; B must get the lock before A's second call.
	start sh -c 'sh "$1/bench.sh" lock run A1 -- sh "$2/hold.sh" "$2/release" "$2/log" A1; sh "$1/bench.sh" lock run A2 -- sh "$2/rec.sh" "$2/log" A2' x "$R" "$E" > "${E:?}/a.out" 2>&1
	_a=$BG
	wait_held 5 A1 || bad "fifo: A never held the lock"
	start env BENCH_LOCK_POLL=3 sh "$R/bench.sh" lock run B -- sh "$E/rec.sh" "$E/log" B > "${E:?}/b.out" 2>&1
	_b=$BG
	wait_said 10 "${E:?}/b.out" || bad "fifo: B never waited"
	: > "${E:?}/release"
	finish fifo "$_a" 0
	finish fifo "$_b" 0
	[ "$(order)" = "A1 B A2 " ] || bad "fifo: a holder that released and asked again went before the waiter: $(order)"
	serial || bad "fifo: two commands held the lock at once: $(tr '\n' ' ' < "${E:?}/log")"
	[ -z "$(tickets)" ] || bad "fifo: tickets left behind: $(tickets | tr '\n' ' ')"
	[ "$fails" -eq 0 ]
}

t_lock_order() {
	fresh order
	qfix
	holder H
	# C3 spawned first and C1 last, so the pids run against the arrival
	# order; each later arrival polls faster, so would win a plain race.
	_ps=
	for _c in C3:0.1 C2:1 C1:2; do
		start sh "$E/late.sh" "$E/go.${_c%%:*}" env BENCH_LOCK_POLL="${_c#*:}" sh "$R/bench.sh" lock run "${_c%%:*}" -- sh "$E/rec.sh" "$E/log" "${_c%%:*}" > "${E:?}/${_c%%:*}.out" 2>&1
		_ps="$_ps $BG"
	done
	for _c in C1 C2 C3; do
		: > "${E:?}/go.$_c"
		wait_said 10 "${E:?}/$_c.out" || bad "order: $_c never waited"
	done
	: > "${E:?}/release"
	for _p in $_ps; do finish order "$_p" 0; done
	finish order "$HOLDER" 0
	[ "$(order)" = "H C1 C2 C3 " ] || bad "order: three waiters did not get the lock in arrival order: $(order)"
	serial || bad "order: two commands held the lock at once: $(tr '\n' ' ' < "${E:?}/log")"
	[ -z "$(tickets)" ] || bad "order: tickets left behind: $(tickets | tr '\n' ' ')"
	[ "$fails" -eq 0 ]
}

t_lock_dead_waiter() {
	fresh deadw
	qfix
	# FIX128 review round 1: W2 waits 10 + 30 s at most under this case's
	# bounds, more than fresh's 10 s; 300 s keeps those bounds binding.
	BENCH_LOCK_WAIT=300
	holder H
	start env BENCH_LOCK_GRACE=1 sh "$R/bench.sh" lock run W1 -- sh "$E/rec.sh" "$E/log" W1 > "${E:?}/w1.out" 2>&1
	_w1=$BG
	wait_said 10 "${E:?}/w1.out" || bad "dead waiter: W1 never waited"
	start env BENCH_LOCK_GRACE=1 sh "$R/bench.sh" lock run W2 -- sh "$E/rec.sh" "$E/log" W2 > "${E:?}/w2.out" 2>&1
	_w2=$BG
	wait_said 10 "${E:?}/w2.out" || bad "dead waiter: W2 never waited"
	[ "$(ticket_of "$_w1")" = "$(tickets | sed -n 1p)" ] && [ -n "$(tickets | sed -n 1p)" ] ||
		bad "dead waiter: the first waiter's ticket is not first in the queue: $(tickets | tr '\n' ' ')"
	kill -KILL "$_w1"
	# Reaped: a zombie still answers kill -0.
	collect "$_w1" 2> /dev/null || true
	: > "${E:?}/release"
	finish "dead waiter" "$_w2" 0
	finish "dead waiter" "$HOLDER" 0
	[ "$(order)" = "H W2 " ] || bad "dead waiter: the waiter behind a killed one did not get the lock: $(order)"
	# Within one poll (0.1 s) and the grace (1 s) of the release, with 1 s
	# for the holder's lock shell to see its command end and 1 s of slack.
	_d=$(awk '$1 == "H" && $2 == "end" { e = $3 } $1 == "W2" && $2 == "start" { s = $3 } END { printf "%.2f", s - e }' "${E:?}/log")
	awk -v d="$_d" 'BEGIN { exit !(d <= 3.1) }' || bad "dead waiter: the next waiter took the lock $_d s after the release"
	[ -z "$(tickets)" ] || bad "dead waiter: the killed waiter's ticket was left behind: $(tickets | tr '\n' ' ')"
	[ "$fails" -eq 0 ]
}

# clockshim <seconds>: $E/clock/date, a date whose +%s stands still until
# $E/tick exists and then reads <seconds> on; every other form runs the
# real date, /bin/date when there is one, so a date on the caller's PATH
# cannot stand in for it. The shim quotes that path.
clockshim() {
	mkdir -p "${E:?}/clock"
	_cs_d=/bin/date
	[ -x "$_cs_d" ] || _cs_d=$(command -v date) || _cs_d=
	case $_cs_d in
	*"'"*) bad "clockshim: the real date's path has a quote in it: $_cs_d"; return 1 ;;
	/*) ;;
	*) bad "clockshim: no real date: $_cs_d"; return 1 ;;
	esac
	_cs_t=$("$_cs_d" +%s)
	printf '#!/bin/sh\nif [ "$#" -eq 1 ] && [ "$1" = +%%s ]; then\n\tif [ -e "%s/tick" ]; then echo %s; else echo %s; fi\n\texit 0\nfi\nexec '\''%s'\'' "$@"\n' "$E" "$((_cs_t + ${1:?}))" "$_cs_t" "$_cs_d" > "${E:?}/clock/date"
	chmod +x "${E:?}/clock/date"
}

t_lock_giveup() {
	fresh giveup
	qfix
	# G's wait is 1 s, and the tick moves its clock on by exactly 1 s: a
	# give-up that ignores BENCH_LOCK_WAIT, or that wants more than it
	# (-gt for -ge), never comes.
	clockshim 1
	holder H
	# FIX128: G's clock stands still until $E/tick, so G cannot give up
	# before this test has seen its ticket, however slow the host. On the
	# real clock a second boundary just after its enqueue (load makes one
	# likelier) let G give up within milliseconds. The bound, 300 polls (at
	# least 30 s), is only for a waiter that never queues; the other cases
	# give a waiter 10 s to say it waits.
	start env PATH="$E/clock:$PATH" BENCH_LOCK_WAIT=1 sh "$R/bench.sh" lock run G -- echo ran > "${E:?}/g.out" 2>&1
	_g=$BG
	_seen=
	_n=0
	while [ "$_n" -lt 300 ] && pid_running "$_g"; do
		_tk=$(ticket_of "$_g") || _tk=
		if [ -n "$_tk" ] && grep -qx 'label=G' "${E:?}/state/queue/$_tk" 2> /dev/null; then
			_seen=1
			break
		fi
		sleep 0.1
		_n=$((_n + 1))
	done
	[ -n "$_seen" ] || bad "give-up: the waiter showed no ticket (its pid= and label=) while it waited"
	pid_running "$_g" || bad "give-up: the waiter gave up while its clock stood still"
	: > "${E:?}/tick"
	finish give-up "$_g" 75
	grep -q 'still held by H (pid ' "${E:?}/g.out" || bad "give-up: the waiter did not name the holder: $(cat "${E:?}/g.out")"
	[ -z "$(tickets)" ] || bad "give-up: a waiter that gave up left its ticket: $(tickets | tr '\n' ' ')"
	start env BENCH_LOCK_WAIT=60 sh "$R/bench.sh" lock run T -- echo ran > "${E:?}/t.out" 2>&1
	_w=$BG
	wait_said 10 "${E:?}/t.out" || bad "give-up: the TERM waiter never waited"
	[ -n "$(ticket_of "$_w")" ] || bad "give-up: the TERM waiter showed no ticket while it waited"
	kill -TERM "$_w"
	finish give-up "$_w" 143
	[ -z "$(ls -A "${E:?}/state/queue" 2> /dev/null)" ] || bad "give-up: a waiter TERMed while it waited left a file in the queue: $(ls -A "${E:?}/state/queue" | tr '\n' ' ')"
	! grep -q '^ran$' "${E:?}/t.out" || bad "give-up: the TERMed waiter ran its command"
	: > "${E:?}/release"
	finish give-up "$HOLDER" 0
	[ "$fails" -eq 0 ]
}

t_lock_queue_reentry() {
	fresh reentq
	qfix
	# FIX128 review round 1: W waits 10 + 10 + 30 + 30 s at most under this
	# case's bounds, more than fresh's 10 s; 300 s keeps those binding.
	BENCH_LOCK_WAIT=300
	# The holder's command waits for $E/go, then runs a nested lock run
	# with no wait at all, which lists the queue's tickets from inside.
	cat > "${E:?}/nest.sh" << 'EOF'
while [ ! -e "$1/go" ]; do sleep 0.05; done
BENCH_LOCK_WAIT=0 sh "$2/bench.sh" lock run inner -- sh -c '{ ls "$1/state/queue" 2> /dev/null || true; } | grep "^w\." > "$1/inner.q"; echo inner-ran' x "$1" > "$1/inner.out" 2>&1
echo "$?" > "$1/inner.st"
EOF
	start sh "$R/bench.sh" lock run H -- sh "$E/nest.sh" "$E" "$R" > "${E:?}/h.out" 2>&1
	_h=$BG
	wait_held 5 H || bad "re-entry: the holder never held the lock"
	start sh "$R/bench.sh" lock run W -- echo w-ran > "${E:?}/w.out" 2>&1
	_w=$BG
	wait_said 10 "${E:?}/w.out" || bad "re-entry: the waiter never waited"
	# FIX128: W's ticket is read here, while H holds: once the nested call
	# ends, H releases, W takes the lock and its ticket goes.
	_wt=$(ticket_of "$_w") || _wt=
	[ -n "$_wt" ] || bad "re-entry: the waiter showed no ticket"
	: > "${E:?}/go"
	wait_file 10 "${E:?}/inner.st" || bad "re-entry: the nested call never finished"
	[ "$(cat "${E:?}/inner.st" 2> /dev/null)" = 0 ] && grep -q '^inner-ran$' "${E:?}/inner.out" ||
		bad "re-entry: a nested call under the holder's token, behind a waiter, did not pass through: $(cat "${E:?}/inner.out")"
	[ "$(cat "${E:?}/inner.q" 2> /dev/null)" = "$_wt" ] || bad "re-entry: the nested call took a ticket: $(tr '\n' ' ' < "${E:?}/inner.q")"
	! grep -q 'waiting for the lock' "${E:?}/inner.out" || bad "re-entry: the nested call waited"
	finish re-entry "$_h" 0
	finish re-entry "$_w" 0
	[ -z "$(tickets)" ] || bad "re-entry: tickets left behind: $(tickets | tr '\n' ' ')"
	[ "$fails" -eq 0 ]
}

t_lock_queue_status() {
	fresh qstatus
	qfix
	# FIX128 review round 1: S3 waits 10 + 3 * 30 s at most under this
	# case's bounds, more than fresh's 10 s; 300 s keeps those binding.
	BENCH_LOCK_WAIT=300
	holder H
	_ps=
	for _c in S1 S2 S3; do
		start sh "$R/bench.sh" lock run "$_c" -- true > "${E:?}/$_c.out" 2>&1
		_ps="$_ps $BG"
		wait_said 10 "${E:?}/$_c.out" || bad "status: $_c never waited"
	done
	B lock status > "${E:?}/status"
	sed -n 1p "${E:?}/status" | grep -Eq '^lock=held label=H pid=[0-9]+ child=[0-9]+ alive=yes since=[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$' ||
		bad "status: the holder's line changed: $(sed -n 1p "${E:?}/status")"
	_i=0
	for _p in $_ps; do
		_i=$((_i + 1))
		sed -n "$((_i + 1))p" "${E:?}/status" | grep -Eq "^waiter=$_i label=S$_i pid=$_p since=[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z\$" ||
			bad "status: line $((_i + 1)) is not waiter $_i (S$_i, pid $_p): $(sed -n "$((_i + 1))p" "${E:?}/status")"
	done
	[ "$(wc -l < "${E:?}/status")" -eq 4 ] || bad "status: wanted the holder and three waiters, got: $(tr '\n' '|' < "${E:?}/status")"
	: > "${E:?}/release"
	for _p in $_ps; do finish status "$_p" 0; done
	finish status "$HOLDER" 0
	[ "$(B lock status)" = lock=free ] || bad "status: not lock=free once the queue is empty: $(B lock status)"
	[ "$fails" -eq 0 ]
}

# FIX106 review round 1. A waiter KILLed and not yet reaped (a zombie,
# which still answers kill -0) is skipped like a dead one: Z is the child
# of a perl parent that reaps it only once $E/reap exists (30 s at most).
t_lock_zombie_waiter() {
	fresh zombiew
	qfix
	# FIX128 review round 1: Z must still wait after wait_said's 10 s and
	# W waits 10 + 30 s at most, more than fresh's 10 s; 300 s keeps this
	# case's own bounds binding.
	BENCH_LOCK_WAIT=300
	holder H
	start perl -e '
		my $d = shift;
		my $p = fork;
		defined $p or exit 2;
		if (!$p) { exec @ARGV; exit 127 }
		open(my $f, ">", "$d/z.pid") or exit 2;
		print $f "$p\n";
		close $f;
		for (1 .. 300) { last if -e "$d/reap"; select(undef, undef, undef, 0.1) }
		waitpid($p, 0);
		exit 0' "$E" sh "$R/bench.sh" lock run Z -- true > "${E:?}/z.out" 2>&1
	_zp=$BG
	wait_said 10 "${E:?}/z.out" || bad "zombie waiter: Z never waited"
	_z=$(cat "${E:?}/z.pid" 2> /dev/null) || _z=
	# the pid read with its start time, and signalled only while it still
	# has it (FIX171)
	_zi=$(pid_id "$_z") || _zi=
	[ -n "$_z" ] && [ -n "$(ticket_of "$_z")" ] || bad "zombie waiter: Z showed no ticket: $(tickets | tr '\n' ' ')"
	if pid_same "$_z" "$_zi"; then kill -KILL "$_z" 2> /dev/null || true; fi
	wait_gone 5 "$_z" || bad "zombie waiter: Z did not die"
	kill -0 "$_z" 2> /dev/null || bad "zombie waiter: Z was reaped at once, so this tests no zombie"
	# Review round 2: lock status skips the zombie's ticket too. No waiter
	# polls yet, so the ticket is still there for it to read.
	[ -n "$(ticket_of "$_z")" ] || bad "zombie waiter: Z's ticket went before lock status read it, so this tests nothing"
	B lock status > "${E:?}/zstatus" 2>&1
	! grep -q " pid=$_z " "${E:?}/zstatus" || bad "zombie waiter: lock status lists the zombie as a waiter: $(tr '\n' '|' < "${E:?}/zstatus")"
	start sh "$R/bench.sh" lock run W -- sh "$E/rec.sh" "$E/log" W > "${E:?}/w.out" 2>&1
	_w=$BG
	wait_said 10 "${E:?}/w.out" || bad "zombie waiter: W never waited"
	: > "${E:?}/release"
	finish "zombie waiter" "$_w" 0
	: > "${E:?}/reap"
	finish "zombie waiter" "$_zp" 0
	finish "zombie waiter" "$HOLDER" 0
	[ "$(order)" = "H W " ] || bad "zombie waiter: the waiter behind a zombie did not get the lock: $(order)"
	_d=$(awk '$1 == "H" && $2 == "end" { e = $3 } $1 == "W" && $2 == "start" { s = $3 } END { printf "%.2f", s - e }' "${E:?}/log")
	awk -v d="$_d" 'BEGIN { exit !(d <= 3.1) }' || bad "zombie waiter: the next waiter took the lock $_d s after the release"
	[ -z "$(tickets)" ] || bad "zombie waiter: the zombie's ticket was left behind: $(tickets | tr '\n' ' ')"
	[ "$fails" -eq 0 ]
}

# A maker that backs off keeps its place: N makes the lock and stalls
# before its holder write until $E/unstall; C (a grace of 1 s) reclaims the bare directory
# and holds; W queues behind C; then N backs off and puts its ticket back
# under its first key, which sorts before W's.
t_lock_stall_place() {
	fresh stallp
	qfix
	# FIX128 review round 1: under this case's bounds N waits at most
	# 5 + 30 + 10 + 30 + 30 + 30 s (the stall's poll, wait_held, wait_said,
	# the poll for its wait, finish C, its own finish) and W 130 s, more
	# than the 30 s they had; 300 s keeps those bounds binding.
	BENCH_LOCK_WAIT=300
	_s=${tmp:?}/stallp.bench
	rm -rf "${_s:?}"
	cp -R "$R" "$_s"
	stall "$_s" || bad "stall-place: the injected stall does not apply"
	start sh "$_s/bench.sh" lock run N -- sh "$E/rec.sh" "$E/log" N > "${E:?}/n.out" 2>&1
	_mk=$BG
	_k=0
	while [ ! -d "${E:?}/state/lock" ] && [ "$_k" -lt 50 ]; do
		sleep 0.1
		_k=$((_k + 1))
	done
	start env BENCH_LOCK_GRACE=1 sh "$R/bench.sh" lock run C -- sh "$E/hold.sh" "$E/release" "$E/log" C > "${E:?}/c.out" 2>&1
	_c=$BG
	wait_held 30 C || bad "stall-place: the taker never held the lock"
	start sh "$R/bench.sh" lock run W -- sh "$E/rec.sh" "$E/log" W > "${E:?}/w.out" 2>&1
	_w=$BG
	wait_said 10 "${E:?}/w.out" || bad "stall-place: W never waited"
	! grep -q 'waiting for the lock' "${E:?}/n.out" || bad "stall-place: N backed off before W queued, so this tests nothing: $(cat "${E:?}/n.out")"
	: > "${E:?}/unstall"
	_k=0
	while ! grep -q 'waiting for the lock held by C ' "${E:?}/n.out" && [ "$_k" -lt 300 ]; do
		sleep 0.1
		_k=$((_k + 1))
	done
	grep -q 'waiting for the lock held by C ' "${E:?}/n.out" || bad "stall-place: the maker that backed off does not wait for C: $(cat "${E:?}/n.out")"
	: > "${E:?}/release"
	finish stall-place "$_c" 0
	finish stall-place "$_mk" 0
	finish stall-place "$_w" 0
	[ "$(order)" = "C N W " ] || bad "stall-place: a maker that backed off lost its place: $(order)"
	serial || bad "stall-place: two commands held the lock at once: $(tr '\n' ' ' < "${E:?}/log")"
	[ -z "$(tickets)" ] || bad "stall-place: tickets left behind: $(tickets | tr '\n' ' ')"
	[ "$fails" -eq 0 ]
}

# A waiter whose ticket is gone (removed by hand) puts it back under its
# first key and keeps its place ahead of a later waiter.
t_lock_ticket_back() {
	fresh tback
	qfix
	# FIX128 review round 1: W2 waits 10 + 5 + 30 + 30 s at most under this
	# case's bounds, more than fresh's 10 s; 300 s keeps those binding.
	BENCH_LOCK_WAIT=300
	holder H
	start sh "$R/bench.sh" lock run W1 -- sh "$E/rec.sh" "$E/log" W1 > "${E:?}/w1.out" 2>&1
	_w1=$BG
	wait_said 10 "${E:?}/w1.out" || bad "ticket back: W1 never waited"
	start sh "$R/bench.sh" lock run W2 -- sh "$E/rec.sh" "$E/log" W2 > "${E:?}/w2.out" 2>&1
	_w2=$BG
	wait_said 10 "${E:?}/w2.out" || bad "ticket back: W2 never waited"
	_tk=$(ticket_of "$_w1") || _tk=
	if [ -n "$_tk" ] && [ "$_tk" = "$(tickets | sed -n 1p)" ]; then
		# The key is w.<time>.<pid>; the random part after it is new.
		rm -f "${E:?}/state/queue/${_tk:?}"
		_n=0
		_tk2=
		while [ "$_n" -lt 50 ]; do
			_tk2=$(ticket_of "$_w1") && break
			sleep 0.1
			_n=$((_n + 1))
		done
		[ "${_tk2%.*}" = "${_tk%.*}" ] || bad "ticket back: W1 did not put its ticket back under its first key: ${_tk%.*} then ${_tk2:-none}"
	else
		bad "ticket back: W1's ticket is not first in the queue: $(tickets | tr '\n' ' ')"
	fi
	: > "${E:?}/release"
	finish "ticket back" "$_w1" 0
	finish "ticket back" "$_w2" 0
	finish "ticket back" "$HOLDER" 0
	[ "$(order)" = "H W1 W2 " ] || bad "ticket back: a waiter that put its ticket back lost its place: $(order)"
	[ -z "$(tickets)" ] || bad "ticket back: tickets left behind: $(tickets | tr '\n' ' ')"
	[ "$fails" -eq 0 ]
}

# A ticket that carries the waiter's own pid but is not its own ticket is
# stale (the pid of a dead waiter, reused by this one): it is removed, not
# counted ahead. late.sh keeps its pid through exec, so the pid is known
# before the waiter queues.
t_lock_own_pid() {
	fresh ownpid
	qfix
	holder H
	# FIX128 review round 1: W waits 10 + 30 s at most under this case's
	# bounds (wait_said, then finish); its old 5 s could end first.
	start sh "$E/late.sh" "$E/go" env BENCH_LOCK_WAIT=300 sh "$R/bench.sh" lock run W -- sh "$E/rec.sh" "$E/log" W > "${E:?}/w.out" 2>&1
	_w=$BG
	mkdir -p "${E:?}/state/queue"
	_sk=${E:?}/state/queue/w.000000000001.000000.$(printf '%010d' "$_w").stale0
	printf 'pid=%s\nlabel=stale\nsince=2000-01-01T00:00:00Z\n' "$_w" > "${_sk:?}"
	: > "${E:?}/go"
	wait_said 10 "${E:?}/w.out" || bad "own pid: W never waited"
	: > "${E:?}/release"
	finish "own pid" "$_w" 0
	finish "own pid" "$HOLDER" 0
	[ "$(order)" = "H W " ] || bad "own pid: a stale ticket with the waiter's own pid kept it from the lock: $(order) $(sed -n '$p' "${E:?}/w.out")"
	[ ! -e "$_sk" ] || bad "own pid: the stale ticket was left in the queue"
	[ "$fails" -eq 0 ]
}

# unfifo <fifo>: frees every reader an open of <fifo> blocks: moves it out
# of the ticket names, opens it for writing (non-blocking: there may be no
# reader) a few times, then removes it.
unfifo() {
	_uf=${E:?}/state/fifo.gone
	mv -f "$1" "${_uf:?}" 2> /dev/null || return 0
	perl -MFcntl -e 'for (1 .. 5) { if (sysopen(my $f, $ARGV[0], O_WRONLY | O_NONBLOCK)) { close $f } select(undef, undef, undef, 0.1) }' "$_uf" 2> /dev/null || true
	rm -f "${_uf:?}"
}

# A queue entry that is not a regular file (a FIFO named like a ticket, which
# blocks whoever opens it) is never read: lock status and a waiter skip it.
t_lock_queue_fifo() {
	fresh qfifo
	qfix
	mkdir -p "${E:?}/state/queue"
	_ff=${E:?}/state/queue/w.000000000001.000000.0000000001.fifo0
	mkfifo "${_ff:?}" || bad "fifo ticket: cannot make a FIFO"
	# Bounded by waiting, not by a signal: a shell blocked in a read of the
	# FIFO defers a signal until the read ends. unfifo frees both below.
	_msg=
	start sh "$R/bench.sh" lock status > "${E:?}/status" 2>&1
	_sp=$BG
	wait_gone 5 "$_sp" || _msg="lock status read a FIFO in the queue and hung"
	start sh "$R/bench.sh" lock run W -- sh "$E/rec.sh" "$E/log" W > "${E:?}/w.out" 2>&1
	_w=$BG
	wait_gone 5 "$_w" || _msg="${_msg:+$_msg; }a waiter read a FIFO in the queue and hung"
	[ -p "$_ff" ] || _msg="${_msg:+$_msg; }the FIFO was removed, not skipped"
	unfifo "$_ff"
	finish "fifo ticket" "$_sp" 0
	finish "fifo ticket" "$_w" 0
	[ -z "$_msg" ] || bad "fifo ticket: $_msg"
	[ "$(cat "${E:?}/status")" = lock=free ] || bad "fifo ticket: lock status printed: $(tr '\n' '|' < "${E:?}/status")"
	[ "$(order)" = "W " ] || bad "fifo ticket: the waiter did not run: $(order)"
	[ "$fails" -eq 0 ]
}

# ---- FIX106 review round 2: a failing ps never makes a live waiter dead. --
# psshim <status>: $E/shim/ps, a ps that prints nothing and exits <status>
# (put on one caller's PATH: not on PATH, a fork failure, a sandbox).
psshim() {
	mkdir -p "${E:?}/shim"
	printf '#!/bin/sh\nexit %s\n' "$1" > "${E:?}/shim/ps"
	chmod +x "${E:?}/shim/ps"
}

# W1 queues first and naps (a poll of 3 s); W2, whose ps exits 1 (then, in
# a second round, exits 0 and prints nothing), polls every 0.1 s. W2 must
# keep W1's ticket and wait behind it, not remove it and go first.
t_lock_ps_fails() {
	for _pst in 1 0; do
		fresh "psfails$_pst"
		qfix
		psshim "$_pst"
		holder H
		start env BENCH_LOCK_POLL=3 sh "$R/bench.sh" lock run W1 -- sh "$E/rec.sh" "$E/log" W1 > "${E:?}/w1.out" 2>&1
		_w1=$BG
		wait_said 10 "${E:?}/w1.out" || bad "ps exits $_pst: W1 never waited"
		start env PATH="$E/shim:$PATH" sh "$R/bench.sh" lock run W2 -- sh "$E/rec.sh" "$E/log" W2 > "${E:?}/w2.out" 2>&1
		_w2=$BG
		wait_said 10 "${E:?}/w2.out" || bad "ps exits $_pst: W2 never waited"
		: > "${E:?}/release"
		finish "ps exits $_pst" "$_w1" 0
		finish "ps exits $_pst" "$_w2" 0
		finish "ps exits $_pst" "$HOLDER" 0
		[ "$(order)" = "H W1 W2 " ] || bad "ps exits $_pst: a waiter whose ps failed or said nothing went before a live one: $(order)"
		serial || bad "ps exits $_pst: two commands held the lock at once: $(tr '\n' ' ' < "${E:?}/log")"
		[ -z "$(tickets)" ] || bad "ps exits $_pst: tickets left behind: $(tickets | tr '\n' ' ')"
	done
	[ "$fails" -eq 0 ]
}

# A live enqueuer's temporary (tmp.<pid>.*, between its write and its
# rename) survives a reader whose ps fails: the pid is this test's shell.
t_lock_ps_tmp() {
	fresh pstmp
	qfix
	psshim 1
	mkdir -p "${E:?}/state/queue"
	_tt=${E:?}/state/queue/tmp.$$.live00
	printf 'pid=%s\n' "$$" > "${_tt:?}"
	start env PATH="$E/shim:$PATH" sh "$R/bench.sh" lock run W -- sh "$E/rec.sh" "$E/log" W > "${E:?}/w.out" 2>&1
	finish "ps tmp" "$BG" 0
	[ "$(order)" = "W " ] || bad "ps tmp: the waiter did not run: $(order) $(sed -n '$p' "${E:?}/w.out")"
	[ -f "$_tt" ] || bad "ps tmp: a reader whose ps fails removed a live enqueuer's temporary"
	rm -f "${_tt:?}"
	[ "$fails" -eq 0 ]
}

# A ticket name that is a symbolic link to a regular file (here one naming
# this test's live shell) is never read: lock status and a waiter skip it
# and leave it in place.
t_lock_queue_link() {
	fresh qlink
	qfix
	mkdir -p "${E:?}/state/queue"
	printf 'pid=%s\nlabel=link\nsince=2000-01-01T00:00:00Z\n' "$$" > "${E:?}/target"
	_lk=${E:?}/state/queue/w.000000000001.000000.0000000001.link0
	ln -s "${E:?}/target" "${_lk:?}" || bad "link ticket: cannot make a link"
	B lock status > "${E:?}/status" 2>&1
	[ "$(cat "${E:?}/status")" = lock=free ] || bad "link ticket: lock status read a link in the queue: $(tr '\n' '|' < "${E:?}/status")"
	start env BENCH_LOCK_WAIT=3 sh "$R/bench.sh" lock run W -- sh "$E/rec.sh" "$E/log" W > "${E:?}/w.out" 2>&1
	finish "link ticket" "$BG" 0
	[ "$(order)" = "W " ] || bad "link ticket: a waiter read a link in the queue and waited behind it: $(order) $(sed -n '$p' "${E:?}/w.out")"
	[ -h "$_lk" ] && [ "$(cat "${E:?}/target")" = "$(printf 'pid=%s\nlabel=link\nsince=2000-01-01T00:00:00Z' "$$")" ] ||
		bad "link ticket: the link or its target was changed, not skipped"
	rm -f "${_lk:?}"
	[ "$fails" -eq 0 ]
}

t_lock_state_dir() {
	fresh state
	mkdir "${E:?}/home"
	env HOME="${E:?}/home" BENCH_STATE= XDG_CACHE_HOME= sh "$R/bench.sh" lock run d -- true || bad "state: a lock run with the default state directory failed"
	[ -d "${E:?}/home/.cache/bench-state" ] || bad "state: the default is not \$HOME/.cache/bench-state"
	env HOME="${E:?}/home" BENCH_STATE= XDG_CACHE_HOME="${E:?}/xdg" sh "$R/bench.sh" lock run d -- true || bad "state: a lock run under XDG_CACHE_HOME failed"
	[ -d "${E:?}/xdg/bench-state" ] || bad "state: XDG_CACHE_HOME is not honoured"
	[ -d "${E:?}/state" ] && bad "state: BENCH_STATE was used where it was empty"
	B lock run d -- true || bad "state: a lock run with BENCH_STATE failed"
	[ -d "${E:?}/state" ] || bad "state: BENCH_STATE is not honoured"
	_st=0
	env BENCH_STATE="${repo:?}/state" sh "$R/bench.sh" lock status 2> /dev/null || _st=$?
	[ "$_st" -eq 2 ] || bad "state: a state directory inside a repository was not refused"
	[ -e "${repo:?}/state" ] && bad "state: the refused state directory was made"
	_st=0
	(cd "${E:?}" && env BENCH_STATE=relative sh "$R/bench.sh" lock status 2> /dev/null) || _st=$?
	[ "$_st" -eq 2 ] || bad "state: a relative state directory was not refused"
	# The guard fails closed: neither a git ceiling nor a failing git lets
	# a state directory into a repository.
	_st=0
	env GIT_CEILING_DIRECTORIES="${repo:?}" BENCH_STATE="${repo:?}/src/zz/state" sh "$R/bench.sh" lock status > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 2 ] || bad "state: a state directory inside a repository was accepted under GIT_CEILING_DIRECTORIES (status $_st)"
	[ ! -e "${repo:?}/src/zz" ] || bad "state: the state directory refused under a ceiling was made"
	mkdir "${E:?}/gitshim"
	printf '#!/bin/sh\necho "fatal: zzq" >&2\nexit 128\n' > "${E:?}/gitshim/git"
	chmod +x "${E:?}/gitshim/git"
	_st=0
	env PATH="${E:?}/gitshim:$PATH" sh "$R/bench.sh" lock status > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 2 ] || bad "state: a failing git was read as 'outside any repository' (status $_st)"
	# A . or .. segment is refused before anything is made: past a missing
	# directory the guard cannot tell where the path ends.
	for _p in "${E:?}/nx/../../zzqrepo/state" "${E:?}/nx/./x" "${E:?}/nx/.."; do
		_st=0
		env BENCH_STATE="$_p" sh "$R/bench.sh" lock status > /dev/null 2>&1 || _st=$?
		[ "$_st" -eq 2 ] || bad "state: a state directory with a . or .. segment was not refused (status $_st)"
	done
	[ ! -e "${repo:?}/state" ] && [ ! -e "${E:?}/nx" ] || bad "state: a state directory with a .. segment was made"
	# A host without git: the .git walk alone decides.
	mkdir "${E:?}/nogit"
	for _d in /bin /usr/bin /usr/sbin /sbin; do
		ln -s "$_d"/* "${E:?}/nogit/" 2> /dev/null || true
	done
	rm -f "${E:?}/nogit/git"
	_st=0
	env PATH="${E:?}/nogit" sh "$R/bench.sh" lock run nogit -- true > /dev/null 2> "${E:?}/nogit.err" || _st=$?
	[ "$_st" -eq 0 ] || bad "state: a host without git cannot take the lock (status $_st)"
	_st=0
	env PATH="${E:?}/nogit" BENCH_STATE="${repo:?}/src/zz/state" sh "$R/bench.sh" lock status > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 2 ] || bad "state: without git, a state directory inside a work tree was accepted (status $_st)"
	[ "$fails" -eq 0 ]
}

t_store_guard() {
	fresh sguard
	fast_profiler
	cd "${repo:?}" || exit 1
	_st=0
	env BENCH_STORE="${repo:?}/store" sh "$R/bench.sh" baseline put null-sleep "$cA" > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 2 ] || bad "store guard: a store inside a repository was not refused (status $_st)"
	[ ! -e "${repo:?}/store" ] || bad "store guard: the refused store was made"
	_st=0
	env BENCH_STORE=relstore sh "$R/bench.sh" baseline put null-sleep "$cA" > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 2 ] || bad "store guard: a relative store was not refused (status $_st)"
	[ ! -e "${repo:?}/relstore" ] || bad "store guard: the refused relative store was made"
	_st=0
	env BENCH_STORE="${E:?}/nx/../../zzqrepo/store" sh "$R/bench.sh" baseline put null-sleep "$cA" > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 2 ] || bad "store guard: a store with a .. segment was not refused (status $_st)"
	[ ! -e "${repo:?}/store" ] && [ ! -e "${E:?}/nx" ] || bad "store guard: the refused store with a .. segment was made"
	[ "$fails" -eq 0 ]
}

t_host() {
	fresh host
	_a=$(B host)
	_b=$(B host)
	[ "$_a" = "$_b" ] || bad "host: the output changed between two runs"
	[ "$(printf '%s\n' "$_a" | awk 'END { print NR }')" -eq 2 ] || bad "host: not two lines"
	_tok=$(printf '%s\n' "$_a" | sed -n 1p)
	_cls=$(printf '%s\n' "$_a" | sed -n 2p)
	printf '%s\n' "$_tok" | grep -Eq '^h-[0-9a-f]{12}$' || bad "host: the token is not h- and 12 hex digits"
	printf '%s\n' "$_cls" | grep -Eq '^[^,]+, [^,]+ GPU, [^,]+, (physical|vm|unknown)$' || bad "host: the class line is not '<cpu>, <gpu> GPU, <os>, physical|vm'"
	if [ "$(uname -s)" = Darwin ]; then
		printf '%s\n' "$_cls" | grep -q ', macOS [0-9]' || bad "host: the class line names no macOS version"
	fi
	_c=$(cd "${E:?}" && env HOME="${E:?}/elsewhere" sh "$R/bench.sh" host)
	[ "$_c" = "$_a" ] || bad "host: the output depends on HOME or the current directory"
	_f=$(B host --facts)
	printf '%s\n' "$_f" | grep -Eq '^hash=(shasum|cksum)$' || bad "host: --facts does not say the hash tool"
	for _k in cpu gpu mem os build vm; do
		printf '%s\n' "$_f" | grep -q "^$_k=" || bad "host: --facts has no $_k"
	done
	for _w in "$id_host" "$id_user" "$id_home"; do
		[ "${#_w}" -ge 3 ] || continue
		if printf '%s\n%s\n' "$_a" "$_f" | grep -qiF -e "$_w"; then
			bad "host: the token, the class line or the facts hold this machine's identity (value not shown)"
		fi
	done
	_k1=$(env BENCH_HASH=cksum sh "$R/bench.sh" host | sed -n 1p)
	_k2=$(env BENCH_HASH=cksum sh "$R/bench.sh" host | sed -n 1p)
	[ "$_k1" = "$_k2" ] || bad "host: the cksum token is not stable"
	printf '%s\n' "$_k1" | grep -Eq '^h-[0-9a-f]{12}$' || bad "host: the cksum token is not h- and 12 hex digits"
	env BENCH_HASH=cksum sh "$R/bench.sh" host --facts | grep -x 'hash=cksum' > /dev/null || bad "host: --facts does not say cksum"
	_st=0
	env BENCH_HASH=md5 sh "$R/bench.sh" host > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 2 ] || bad "host: BENCH_HASH=md5 was not refused"
	# The scrub and the salt, on synthetic identity values.
	(
		BENCH_ROOT=$R
		. "$R/core/common.sh"
		. "$R/core/host.sh"
		printf 'zzqbox\nZzqUser\n/home/zzq\nab\n' > "${E:?}/words"
		_s=$(printf 'Apple zzqbox, ZZQUSER GPU, /home/zzq/x, physical ab\n' | bench_scrub "${E:?}/words")
		[ "$_s" = 'Apple ***, *** GPU, ***/x, physical ab' ] || { echo "$me: host: the scrub gave: $_s"; exit 1; }
		BENCH_TMP=${E:?}/ht
		mkdir "${BENCH_TMP:?}"
		bench_host_init
		_t0=$BENCH_HOST_TOKEN
		_w=$(printf '%s\n' "$_t0" | cut -c3-6)
		bench_identity_words() { printf '%s\n' "$_w" > "$1"; }
		BENCH_HOST_TOKEN=
		bench_host_init
		_t1=$BENCH_HOST_TOKEN
		case $_t1 in
		*"$_w"* | "$_t0") echo "$me: host: a token that spells an identity value was not hashed again"; exit 1 ;;
		esac
		BENCH_HOST_TOKEN=
		bench_host_init
		[ "$BENCH_HOST_TOKEN" = "$_t1" ] || { echo "$me: host: the salted token is not stable"; exit 1; }
	) || bad "host: the synthetic identity tests failed"
	[ "$fails" -eq 0 ]
}

t_inputs() {
	fresh inputs
	fast_profiler
	cd "${repo:?}" || exit 1
	# The paths are the adapter's (the null adapter names a make layout).
	_nf=${E:?}/nf.case
	printf 'adapter=null\nruns=1\nmetric=ms\nabs_tol=0\nrel_tol=0\n' > "$_nf"
	_iA=$(B inputs "$cA" "$_nf")
	_iB=$(B inputs "$cB" "$_nf")
	_iC=$(B inputs "$cC" "$_nf")
	_iD=$(B inputs "$cD" "$_nf")
	_iF=$(B inputs "$cF" "$_nf")
	printf '%s\n' "$_iA" | grep -Eq '^i-[0-9a-f]{12}$' || bad "inputs: not i- and 12 hex digits"
	[ "$_iA" = "$_iB" ] || bad "inputs: a documentation-only commit changed the inputs"
	[ "$_iA" != "$_iC" ] || bad "inputs: a src/ edit did not change the inputs"
	[ "$_iC" != "$_iD" ] || bad "inputs: adding third_party/ did not change the inputs"
	[ "$_iD" != "$_iF" ] || bad "inputs: a top-level .make file did not change the inputs"
	_l=$(B inputs --list "$cA" "$_nf")
	printf '%s\n' "$_l" | grep -qx 'path=third_party id=missing' || bad "inputs: a missing path is not the marker 'missing'"
	printf '%s\n' "$_l" | grep -qx "path=src id=$(gitc rev-parse "$cA:src")" || bad "inputs: src is not hashed by its tree id"
	printf '%s\n' "$_l" | grep -Eqx 'hash=(shasum|cksum)' || bad "inputs: --list does not say the hash tool"
	[ "$(printf '%s\n' "$_l" | sed -n '$p')" = "$_iA" ] || bad "inputs: --list does not end with the id"
	B inputs --list "$cF" "$_nf" | grep -x "path=extra.make id=$(gitc rev-parse "$cF:extra.make")" > /dev/null || bad "inputs: a top-level .make file is not listed"
	# Without a case (no adapter, so no list), the whole tree.
	_tA=$(B inputs "$cA")
	B inputs --list "$cA" | grep -x "tree=$(gitc rev-parse "$cA^{tree}")" > /dev/null || bad "inputs: without a case, the whole tree is not hashed"
	[ "$_tA" != "$(B inputs "$cB")" ] || bad "inputs: without a case, a documentation-only commit kept the inputs"
	printf 'adapter=null\nruns=1\nmetric=ms\nabs_tol=0\nrel_tol=0\nbuild_flags=X=1\n' > "${E:?}/f1.case"
	printf 'adapter=null\nruns=1\nmetric=ms\nabs_tol=0\nrel_tol=0\nbuild_flags=X=2\n' > "${E:?}/f2.case"
	_f1=$(B inputs "$cA" "${E:?}/f1.case")
	[ "$_f1" != "$(B inputs "$cA" "${E:?}/f2.case")" ] || bad "inputs: the case's flags are not part of the inputs"
	[ "$_f1" != "$_iA" ] || bad "inputs: flags and no flags gave the same inputs"
	B inputs --list "$cA" "${E:?}/f1.case" | grep -x 'flags=X=1' > /dev/null || bad "inputs: --list does not show the flags"
	_kA=$(env BENCH_HASH=cksum sh "$R/bench.sh" inputs "$cA" "$_nf")
	_kB=$(env BENCH_HASH=cksum sh "$R/bench.sh" inputs "$cB" "$_nf")
	_kC=$(env BENCH_HASH=cksum sh "$R/bench.sh" inputs "$cC" "$_nf")
	printf '%s\n' "$_kA" | grep -Eq '^i-[0-9a-f]{12}$' || bad "inputs: the cksum id is not i- and 12 hex digits"
	[ "$_kA" = "$_kB" ] && [ "$_kA" != "$_kC" ] || bad "inputs: the cksum ids do not follow the rule"
	_st=0
	B inputs no-such-commit 2> /dev/null || _st=$?
	[ "$_st" -eq 2 ] || bad "inputs: an unknown commit was not refused"
	_st=0
	B inputs --output=x 2> /dev/null || _st=$?
	[ "$_st" -eq 2 ] || bad "inputs: an option in place of the commit was not refused"
	# A failing, missing or malformed digest tool is an error, never an
	# empty id.
	mkdir "${E:?}/fail" "${E:?}/junk" "${E:?}/nosha"
	for _t in shasum cksum; do
		printf '#!/bin/sh\nexit 1\n' > "${E:?}/fail/$_t"
		printf '#!/bin/sh\necho "zz%s  -"\n' "$_t" > "${E:?}/junk/$_t"
	done
	chmod +x "${E:?}/fail"/* "${E:?}/junk"/*
	for _d in fail junk; do
		for _t in shasum cksum; do
			for _c in host "inputs $cA $_nf" "case null-sleep"; do
				_st=0
				# shellcheck disable=SC2086 # the command is words
				env PATH="${E:?}/$_d:$PATH" BENCH_HASH="$_t" sh "$R/bench.sh" $_c > "${E:?}/hash.out" 2> /dev/null || _st=$?
				[ "$_st" -eq 2 ] || bad "inputs: $_c with a $_d $_t exited $_st, not 2: $(tr '\n' ' ' < "${E:?}/hash.out")"
			done
		done
	done
	for _d in /bin /usr/bin /usr/sbin /sbin; do
		ln -s "$_d"/* "${E:?}/nosha/" 2> /dev/null || true
	done
	rm -f "${E:?}/nosha/shasum" "${E:?}/nosha/system_profiler"
	ln -s "${E:?}/sp/system_profiler" "${E:?}/nosha/system_profiler"
	_st=0
	env PATH="${E:?}/nosha" BENCH_HASH=shasum sh "$R/bench.sh" host > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 2 ] || bad "inputs: BENCH_HASH=shasum with no shasum exited $_st, not 2"
	env PATH="${E:?}/nosha" sh "$R/bench.sh" host --facts | grep -x 'hash=cksum' > /dev/null || bad "inputs: with no shasum the hash is not cksum"
	[ "$fails" -eq 0 ]
}

t_case() {
	fresh case
	_base='adapter=null\nruns=3\nmetric=ms\nabs_tol=0.5\nrel_tol=0.02\n'
	_good='adapter=null|metric=ms|abs_tol=0.5|rel_tol=0.02'
	for _bad in "$_good|runs=3|colour=red" "$_good|runs=3|runs=4" "$_good|runs=0" "$_good|runs=x" \
		"$_good|runs=3|better=sideways" "$_good|runs=3|accept=5" "$_good|runs=3|Runs=3" \
		"$_good|runs=3|no equals sign" "$_good" 'adapter=null|metric=ms|abs_tol=-1|rel_tol=0|runs=1' \
		'adapter=../x|metric=ms|abs_tol=0|rel_tol=0|runs=1'; do
		printf '%s\n' "$_bad" | tr '|' '\n' > "${E:?}/bad.case"
		_st=0
		B case "${E:?}/bad.case" > /dev/null 2> "${E:?}/err" || _st=$?
		[ "$_st" -eq 2 ] || bad "case: '$_bad' was not refused (status $_st)"
	done
	printf '%s|runs=3|colour=red\n' "$_good" | tr '|' '\n' > "${E:?}/bad.case"
	B case "${E:?}/bad.case" > /dev/null 2> "${E:?}/err" || true
	grep -q 'unknown key colour' "${E:?}/err" || bad "case: an unknown key is not named"
	# Values are data: nothing in them runs.
	{ printf '%b' "$_base"; printf 'description=$(touch %s/pwned) `touch %s/pwned2`\nsettings=;touch %s/pwned3\n' "$E" "$E" "$E"; } > "${E:?}/data.case"
	B case "${E:?}/data.case" > "${E:?}/data.out" || bad "case: a case with shell text in its values was refused"
	[ ! -e "${E:?}/pwned" ] && [ ! -e "${E:?}/pwned2" ] && [ ! -e "${E:?}/pwned3" ] || bad "case: a value was evaluated"
	# The hash is of the normalised content.
	printf '# a comment\nadapter = null\n\nrel_tol=0.02\n  metric=ms\nruns=3\nabs_tol=0.5   \n' > "${E:?}/n1.case"
	printf 'adapter=null\nruns=3\nmetric=ms\nabs_tol=0.5\nrel_tol=0.02\n' > "${E:?}/n2.case"
	printf 'adapter=null\nruns=3\nmetric=ms\nabs_tol=0.6\nrel_tol=0.02\n' > "${E:?}/n3.case"
	_h1=$(B case "${E:?}/n1.case" | sed -n '1s/.* hash=//p')
	_h2=$(B case "${E:?}/n2.case" | sed -n '1s/.* hash=//p')
	_h3=$(B case "${E:?}/n3.case" | sed -n '1s/.* hash=//p')
	printf '%s\n' "$_h1" | grep -Eq '^c-[0-9a-f]{12}$' || bad "case: the hash is not c- and 12 hex digits"
	[ "$_h1" = "$_h2" ] || bad "case: comments, blanks or key order changed the hash"
	[ "$_h1" != "$_h3" ] || bad "case: a changed value kept the hash"
	B case null-sleep > /dev/null || bad "case: the null-sleep case is refused"
	_st=0
	printf '%b' "$_base" > "${E:?}/noext"
	B case "${E:?}/noext" > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 2 ] || bad "case: a case file not named .case was not refused"
	[ "$fails" -eq 0 ]
}

t_stats() {
	fresh stats
	_chk() {
		_g=$(printf '%b' "$1" | B stats) || _g="status $?"
		[ "$_g" = "$2" ] || bad "stats: '$1' gave '$_g', wanted '$2'"
	}
	_chk '7\n' 'n=1 median=7 p90=7 min=7 max=7'
	_chk '3\n1\n2\n' 'n=3 median=2 p90=3 min=1 max=3'
	_chk '4\n1\n3\n2\n' 'n=4 median=2.5 p90=4 min=1 max=4'
	_chk '10\n9\n8\n7\n6\n5\n4\n3\n2\n1\n' 'n=10 median=5.5 p90=9 min=1 max=10'
	_chk '6\n11\n1\n10\n2\n9\n3\n8\n4\n7\n5\n' 'n=11 median=6 p90=10 min=1 max=11'
	_chk '10.75\n10.25\n' 'n=2 median=10.5 p90=10.75 min=10.25 max=10.75'
	_chk '-1\n-3\n' 'n=2 median=-2 p90=-1 min=-3 max=-1'
	_chk '0.33333\n2\n' 'n=2 median=1.1667 p90=2 min=0.3333 max=2'
	for _x in 'x\n' '' '1e3\n' '1,5\n'; do
		_st=0
		printf '%b' "$_x" | B stats > /dev/null 2>&1 || _st=$?
		[ "$_st" -eq 2 ] || bad "stats: '$_x' was not refused (status $_st)"
	done
	[ "$fails" -eq 0 ]
}

# store_setup: the host token, the inputs and the case hash the tests expect.
store_setup() {
	cd "${repo:?}" || exit 1
	fast_profiler
	TOK=$(B host | sed -n 1p)
	IA=$(B inputs "$cA" null-sleep)
	IC=$(B inputs "$cC" null-sleep)
	CH=$(B case null-sleep | sed -n '1s/.* hash=//p')
}

t_store() {
	fresh store
	store_setup
	mkdir "${E:?}/home"
	_st=0
	_out=$(env HOME="${E:?}/home" BENCH_STORE= XDG_CACHE_HOME= BENCH_NULL_VALUES=10,12,11 sh "$R/bench.sh" baseline put null-sleep "$cA" 2> "${E:?}/err") || _st=$?
	[ "$_st" -eq 0 ] || bad "store: baseline put failed (status $_st): $(cat "${E:?}/err")"
	_want="baseline=stored case=null-sleep inputs=$IA host=$TOK runs=3
metric=frames n=3 median=3 p90=3 min=3 max=3
metric=ms n=3 median=11 p90=12 min=10 max=12"
	[ "$_out" = "$_want" ] || bad "store: put printed: $_out"
	_d=${E:?}/home/.cache/zzqrepo-bench/$TOK/$IA/null-sleep/$CH
	[ "$(ls "${_d:?}/set-1" | grep -c '^r-.*\.run$')" -eq 3 ] || bad "store: not three run files in <store>/<host>/<inputs>/<case>/<case-hash>/set-1/ under \$HOME/.cache/<repo>-bench"
	ls -a "${_d:?}" "${_d:?}/set-1" | grep -q '^\.tmp\.' && bad "store: a temporary file or staging directory was left"
	_want="case case_hash commit inputs host class hash kind index start load.before load.after quiet quiet.reason gpu.after.settle gate.wait snap.source"
	for _k in $SNAP_KEYS; do _want="$_want snap.before.$_k"; done
	for _k in $SNAP_KEYS; do _want="$_want snap.after.$_k"; done
	_want="$_want freq.source freq.gpu.mhz freq.gpu.top freq.gpu.active freq.gpu.max prewarm.target prewarm.seconds prewarm.runs prewarm.stop prewarm.freq prewarm.freq.reason pre.null metric.ms metric.frames "
	for _f in "${_d:?}"/set-1/r-*.run; do
		_keys=$(sed 's/=.*//' "$_f" | tr '\n' ' ')
		[ "$_keys" = "$_want" ] || bad "store: a run file has the keys: $_keys"
		for _l in quiet=yes quiet.reason=none snap.source=stub freq.source=stub snap.before.gpu.device=0 snap.after.top.5='1.0 zzqproc5' freq.gpu.mhz=1000 \
			prewarm.target=0 prewarm.stop=none prewarm.freq=unavailable prewarm.freq.reason=no-prewarm; do
			grep -qx "$_l" "$_f" || bad "store: a run file has no line $_l"
		done
		grep -qx "commit=$cA" "$_f" || bad "store: a run file does not name its commit"
	done
	BENCH_STORE=${E:?}/home/.cache/zzqrepo-bench
	export BENCH_STORE
	B baseline get null-sleep "$cA" > "${E:?}/get" || bad "store: baseline get found nothing"
	[ "$(sed -n 1p "${E:?}/get")" = "baseline=present case=null-sleep inputs=$IA host=$TOK runs=3" ] || bad "store: get printed: $(sed -n 1p "${E:?}/get")"
	grep -qx 'metric=ms n=3 median=11 p90=12 min=10 max=12' "${E:?}/get" || bad "store: get gave other statistics"
	B baseline get null-sleep "$cB" > /dev/null || bad "store: the documentation-only commit does not share the baseline"
	_st=0
	B baseline get null-sleep "$cC" > "${E:?}/get" || _st=$?
	[ "$_st" -eq 1 ] && grep -q '^baseline=none' "${E:?}/get" || bad "store: get of a commit with no baseline did not say none and exit 1"
	_st=0
	B baseline put null-sleep "$cA" > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 1 ] || bad "store: a second put without --replace was not refused"
	# Reads take only r-*.run files of set-<number> directories: not a
	# temporary file, another .run name, a staging directory or another set
	# name, each of which would change the runs or the statistics.
	for _p in set-1/.tmp.r-zzq.run set-1/zzq.run .tmp.999/r-zzq-1.run set-x/r-zzq-1.run set-/r-zzq-1.run; do
		mkdir -p "${_d:?}/${_p%/*}" && printf 'metric.ms=999\n' > "${_d:?}/$_p" || bad "store: cannot plant $_p"
	done
	B baseline get null-sleep "$cA" > "${E:?}/get" || true
	grep -qx 'metric=ms n=3 median=11 p90=12 min=10 max=12' "${E:?}/get" && grep -q 'runs=3$' "${E:?}/get" || bad "store: a read took a file that is not a run of the live set: $(tr '\n' ' ' < "${E:?}/get")"
	rm -rf "${_d:?}/set-1/.tmp.r-zzq.run" "${_d:?}/set-1/zzq.run" "${_d:?}/.tmp.999" "${_d:?}/set-x" "${_d:?}/set-"
	# A store is all or nothing: a failed copy of the second run, or a
	# failed rename of the set into place, leaves the old baseline live and
	# no staging directory.
	mkdir "${E:?}/shim"
	printf '#!/bin/sh\nfor a; do l=$a; done\ncase $l in *.run | */set-*) exit 1 ;; esac\nexec /bin/mv "$@"\n' > "${E:?}/shim/mv"
	mkdir "${E:?}/cpshim"
	printf '#!/bin/sh\nc=%s\nfor a; do l=$a; done\ncase $l in */.tmp.*.run) n=$(($(cat "$c" 2> /dev/null || echo 0) + 1)); echo "$n" > "$c"; [ "$n" -ne 2 ] || exit 1 ;; esac\nexec /bin/cp "$@"\n' "${E:?}/cpshim/count" > "${E:?}/cpshim/cp"
	chmod +x "${E:?}/shim/mv" "${E:?}/cpshim/cp"
	for _sh in cpshim shim; do
		_st=0
		env PATH="${E:?}/$_sh:$PATH" BENCH_NULL_VALUES=20 sh "$R/bench.sh" baseline put --replace null-sleep "$cA" > /dev/null 2>&1 || _st=$?
		[ "$_st" -eq 2 ] || bad "store: a --replace whose store failed ($_sh) exited $_st, not 2"
		B baseline get null-sleep "$cA" > "${E:?}/get" || true
		grep -qx 'metric=ms n=3 median=11 p90=12 min=10 max=12' "${E:?}/get" && grep -q 'runs=3$' "${E:?}/get" || bad "store: a failed --replace ($_sh) did not leave the old baseline live: $(tr '\n' ' ' < "${E:?}/get")"
		ls -a "${_d:?}" | grep -q '^\.tmp\.' && bad "store: a failed store ($_sh) left its staging directory"
	done
	[ "$(cat "${E:?}/cpshim/count")" -ge 2 ] || bad "store: the copy shim never failed a copy"
	_st=0
	env PATH="${E:?}/shim:$PATH" sh "$R/bench.sh" baseline put null-sleep "$cC" > /dev/null 2>&1 || _st=$?
	[ "$_st" -ne 0 ] || bad "store: a put whose rename failed succeeded"
	_st=0
	B baseline get null-sleep "$cC" > /dev/null || _st=$?
	[ "$_st" -eq 1 ] || bad "store: a failed rename left a readable run"
	# A preflight refusal stores nothing.
	_st=0
	env BENCH_NULL_REFUSE=1 sh "$R/bench.sh" baseline put null-sleep "$cC" > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 3 ] || bad "store: a preflight refusal did not exit 3 (got $_st)"
	_st=0
	B baseline get null-sleep "$cC" > /dev/null || _st=$?
	[ "$_st" -eq 1 ] || bad "store: a refused run was stored"
	# The wall-time bound stops a run that outlives it; a case without a
	# wall gets the default (600 s).
	sed 's/^settings=.*/settings=sleep=5 values=1/; s/^wall=.*/wall=1/; s/^warmup=.*/warmup=0/' "$R/cases/null-sleep.case" > "${E:?}/slow.case"
	_st=0
	env BENCH_STORE="${E:?}/slowstore" sh "$R/bench.sh" baseline put "${E:?}/slow.case" "$cA" > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 3 ] || bad "store: a run over its wall time did not exit 3 (got $_st)"
	grep -v '^wall=' "$R/cases/null-sleep.case" | sed 's/^runs=.*/runs=1/; s/^warmup=.*/warmup=0/' > "${E:?}/nowall.case"
	_st=0
	env BENCH_STORE="${E:?}/nowallstore" sh "$R/bench.sh" baseline put "${E:?}/nowall.case" "$cA" > /dev/null 2> "${E:?}/err" || _st=$?
	[ "$_st" -eq 0 ] || bad "store: a case without wall= failed (status $_st): $(cat "${E:?}/err")"
	# A case name at the cap (58) measures under the lock (bench.<case> is a
	# label of 64); one longer is refused.
	_n58=$(awk 'BEGIN { while (n++ < 58) printf "c"; print "" }')
	sed 's/^runs=.*/runs=1/; s/^warmup=.*/warmup=0/' "$R/cases/null-sleep.case" > "${E:?}/$_n58.case"
	cp "${E:?}/$_n58.case" "${E:?}/${_n58}d.case"
	_st=0
	env BENCH_STORE="${E:?}/longstore" sh "$R/bench.sh" baseline put "${E:?}/$_n58.case" "$cA" > /dev/null 2> "${E:?}/err" || _st=$?
	[ "$_st" -eq 0 ] || bad "store: a case name of 58 characters could not be measured (status $_st): $(cat "${E:?}/err")"
	_st=0
	B case "${E:?}/${_n58}d.case" > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 2 ] || bad "store: a case name of 59 characters was not refused (status $_st)"
	# --replace makes a new set live and keeps the old one as it was; the
	# live set is the highest number (set-10 after set-9, not set-9).
	env BENCH_NULL_VALUES=20 sh "$R/bench.sh" baseline put --replace null-sleep "$cA" > "${E:?}/rep" || bad "store: put --replace failed"
	grep -qx 'superseded=set-1' "${E:?}/rep" || bad "store: --replace did not name the set it superseded"
	B baseline get null-sleep "$cA" | grep -x 'metric=ms n=3 median=20 p90=20 min=20 max=20' > /dev/null || bad "store: --replace did not store the new runs"
	[ "$(ls "${_d:?}/set-1" | grep -c '^r-.*\.run$')" -eq 3 ] || bad "store: set-1 does not keep the three superseded runs"
	[ "$(ls "${_d:?}/set-2" | grep -c '^r-.*\.run$')" -eq 3 ] || bad "store: set-2 does not hold the three new runs"
	cp -R "${_d:?}/set-1" "${_d:?}/set-9" || bad "store: cannot copy set-1 to set-9"
	env BENCH_NULL_VALUES=30 sh "$R/bench.sh" baseline put --replace null-sleep "$cA" > /dev/null || bad "store: put --replace after set-9 failed"
	[ -d "${_d:?}/set-10" ] || bad "store: the set after set-9 is not set-10"
	B baseline get null-sleep "$cA" | grep -x 'metric=ms n=3 median=30 p90=30 min=30 max=30' > /dev/null || bad "store: the live set is not the highest number (set-10)"
	# The runs of a set are listed oldest first, by number (2 before 10).
	(
		BENCH_ROOT=$R
		. "$R/core/common.sh"
		. "$R/core/store.sh"
		mkdir "${E:?}/order"
		for _k in 10 2 1; do : > "${E:?}/order/r-20260101T000000Z-7-$_k.run"; done
		_o=$(bench_store_runs "${E:?}/order" | sed 's/.*-//' | tr '\n' ' ')
		[ "$_o" = "1.run 2.run 10.run " ] || { echo "$me: store: the runs are listed as $_o"; exit 1; }
	) || bad "store: the runs of a set are not listed oldest first"
	[ "$fails" -eq 0 ]
}

t_drift() {
	fresh drift
	store_setup
	env BENCH_NULL_VALUES=10,12,11 sh "$R/bench.sh" baseline put null-sleep "$cA" > /dev/null || bad "drift: baseline put failed"
	_d=$BENCH_STORE/$TOK/$IA/null-sleep/$CH
	_runs=$(ls "${_d:?}"/set-*)
	# t = max(0.5, 0.02 * 11) = 0.5, so the range is [9.5, 12.5].
	for _x in 12.3:12.3 12.5:12.5 9.5:9.5; do
		_st=0
		_out=$(env BENCH_NULL_VALUES="${_x%%:*}" sh "$R/bench.sh" drift null-sleep "$cA" 2> "${E:?}/err") || _st=$?
		[ "$_st" -eq 0 ] || bad "drift: status $_st for ${_x%%:*}"
		[ "$_out" = "drift=inside value=${_x#*:} lo=9.5 hi=12.5 t=0.5 median=11 runs=3 metric=ms quiet=yes source=stub baseline.quiet=yes baseline.source=stub" ] || bad "drift: ${_x%%:*} gave: $_out"
	done
	[ "$(ls "${_d:?}"/set-*)" = "$_runs" ] || bad "drift: a run inside the range changed the baseline"
	[ "$(ls "${_d:?}/drift" | grep -c '^r-.*\.run$')" -eq 3 ] || bad "drift: the drift runs are not kept in drift/"
	_out=$(env BENCH_NULL_VALUES=12.51 sh "$R/bench.sh" drift null-sleep "$cA" 2> "${E:?}/err") || bad "drift: an outside run failed"
	printf '%s\n' "$_out" | grep -qx 'drift=outside value=12.51 lo=9.5 hi=12.5 t=0.5 median=11 runs=3 metric=ms quiet=yes source=stub baseline.quiet=yes baseline.source=stub' || bad "drift: 12.51 gave: $(printf '%s\n' "$_out" | sed -n 1p)"
	printf '%s\n' "$_out" | grep -qx "baseline=replaced case=null-sleep inputs=$IA runs=3" || bad "drift: an outside run did not replace the baseline"
	B baseline get null-sleep "$cA" | grep -x 'metric=ms n=3 median=12.51 p90=12.51 min=12.51 max=12.51' > /dev/null || bad "drift: the new baseline is not the re-measured one"
	[ "$(ls "${_d:?}/set-1" | grep -c '^r-.*\.run$')" -eq 3 ] && [ -d "${_d:?}/set-2" ] || bad "drift: the old baseline is not kept as set-1 beside the new set-2"
	# The relative term: t = max(0, 0.1 * 11) = 1.1, so the range is [8.9, 13.1].
	sed 's/^abs_tol=.*/abs_tol=0/; s/^rel_tol=.*/rel_tol=0.1/' "$R/cases/null-sleep.case" > "${E:?}/rel.case"
	env BENCH_NULL_VALUES=10,12,11 sh "$R/bench.sh" baseline put "${E:?}/rel.case" "$cA" > /dev/null || bad "drift: put of the relative case failed"
	_out=$(env BENCH_NULL_VALUES=13.1 sh "$R/bench.sh" drift "${E:?}/rel.case" "$cA")
	[ "$_out" = 'drift=inside value=13.1 lo=8.9 hi=13.1 t=1.1 median=11 runs=3 metric=ms quiet=yes source=stub baseline.quiet=yes baseline.source=stub' ] || bad "drift: 13.1 (relative) gave: $_out"
	_out=$(env BENCH_NULL_VALUES=8.8 sh "$R/bench.sh" drift "${E:?}/rel.case" "$cA" | sed -n 1p)
	[ "$_out" = 'drift=outside value=8.8 lo=8.9 hi=13.1 t=1.1 median=11 runs=3 metric=ms quiet=yes source=stub baseline.quiet=yes baseline.source=stub' ] || bad "drift: 8.8 (relative) gave: $_out"
	_st=0
	B drift null-sleep "$cC" > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 1 ] || bad "drift: no baseline did not exit 1"
	[ "$fails" -eq 0 ]
}

t_compare() {
	fresh compare
	store_setup
	_cls=$(B host | sed -n 2p)
	env BENCH_NULL_VALUES=10,12,11 sh "$R/bench.sh" baseline put null-sleep "$cA" > /dev/null || bad "compare: put A failed"
	env BENCH_NULL_VALUES=9,9.5,8.5 sh "$R/bench.sh" baseline put null-sleep "$cC" > /dev/null || bad "compare: put C failed"
	_st=0
	B compare null-sleep --before "$cA" --after "$cC" > "${E:?}/out" || _st=$?
	[ "$_st" -eq 0 ] || bad "compare: a passing verdict exited $_st"
	{
		printf 'compare case=null-sleep host=%s class=%s\n' "$TOK" "$_cls"
		printf 'before=%s inputs=%s\nafter=%s inputs=%s\n' "$cA" "$IA" "$cC" "$IC"
		printf 'quiet before=yes after=yes\nsource before=stub after=stub\n'
		printf '%-16s %-6s %4s %12s %12s %12s %12s\n' metric side runs median p90 min max
		printf '%-16s %-6s %4s %12s %12s %12s %12s\n' frames before 3 3 3 3 3
		printf '%-16s %-6s %4s %12s %12s %12s %12s\n' frames after 3 3 3 3 3
		printf '%-16s %-6s %4s %12s %12s %12s %12s\n' ms before 3 11 12 10 12
		printf '%-16s %-6s %4s %12s %12s %12s %12s\n' ms after 3 9 9.5 8.5 9.5
		printf 'verdict=pass metric=ms better=lower before=11 after=9 change=-18.18%% accept=5%%\n'
	} > "${E:?}/want"
	cmp -s "${E:?}/want" "${E:?}/out" || bad "compare: the table differs: $(diff "${E:?}/want" "${E:?}/out" | sed -n '2,6p' | tr '\n' '|')"
	_st=0
	B compare null-sleep --after "$cA" --before "$cC" > "${E:?}/out" || _st=$?
	[ "$_st" -eq 1 ] || bad "compare: a failing verdict exited $_st"
	grep -qx 'verdict=fail metric=ms better=lower before=9 after=11 change=+22.22% accept=5%' "${E:?}/out" || bad "compare: the failing verdict line is: $(sed -n '$p' "${E:?}/out")"
	B compare null-sleep --before "$cA" --after "$cB" > "${E:?}/out" 2>&1 || true
	grep -qx 'note=the two commits have the same inputs, so one baseline' "${E:?}/out" || bad "compare: same inputs are not noted"
	grep -v '^accept=' "$R/cases/null-sleep.case" | sed 's/^runs=.*/runs=1/; s/^warmup=.*/warmup=0/' > "${E:?}/noacc.case"
	env BENCH_NULL_VALUES=10 sh "$R/bench.sh" baseline put "${E:?}/noacc.case" "$cA" > /dev/null || bad "compare: put of the no-acceptance case failed"
	env BENCH_NULL_VALUES=9 sh "$R/bench.sh" baseline put "${E:?}/noacc.case" "$cC" > /dev/null || bad "compare: put of the no-acceptance case failed"
	_st=0
	B compare "${E:?}/noacc.case" --before "$cA" --after "$cC" > "${E:?}/out" || _st=$?
	[ "$_st" -eq 0 ] || bad "compare: without an acceptance it exited $_st"
	grep -q '^verdict=' "${E:?}/out" && bad "compare: a verdict without an acceptance"
	grep -q "^ms  *after  *1  *9 " "${E:?}/out" || bad "compare: the no-acceptance table is wrong"
	_st=0
	B compare null-sleep --before "$cA" --after "$cD" > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 1 ] || bad "compare: a side without a baseline did not exit 1"
	[ "$fails" -eq 0 ]
}

# ---- TASK166: the quiet evidence. ------------------------------------------

# A copy of the null case with <sed expression> applied, as E/<name>.case.
qcase() {
	sed "$2" "$R/cases/null-sleep.case" > "${E:?}/$1.case"
}

t_quiet_case() {
	fresh qcase
	for _l in prewarm=x prewarm=1.5 gate_gpu=101 gate_cpu=-1 gate_hold=x gate_wait=2.5 gate_settle=2.5 gate_settle=1234567890; do
		printf '%s\n' "$_l" | cat "$R/cases/null-sleep.case" - | grep -v "^${_l%%=*}=0\$" > "${E:?}/bad.case"
		_st=0
		B case "${E:?}/bad.case" > /dev/null 2>&1 || _st=$?
		[ "$_st" -eq 2 ] || bad "case: $_l was not refused (status $_st)"
	done
	qcase ok 's/^gate_hold=.*/gate_hold=2/'
	printf 'gate_gpu=100\ngate_cpu=0\ngate_wait=5\n' >> "${E:?}/ok.case"
	B case "${E:?}/ok.case" > /dev/null || bad "case: the gate keys at their bounds were refused"
	[ "$fails" -eq 0 ]
}

t_gate() {
	fresh gate
	store_setup
	# It waits: the GPU busy for three readings, then quiet; held 1 s.
	qcase g 's/^gate_hold=.*/gate_hold=1/; s/^runs=.*/runs=1/; s/^warmup=.*/warmup=0/'
	printf '50\n50\n50\n0\n' > "${STUB:?}/gpu.gate"
	B baseline put "${E:?}/g.case" "$cA" > /dev/null 2> "${E:?}/err" || bad "gate: a put that waited for a quiet host failed: $(cat "${E:?}/err")"
	_f=$(runfile g)
	[ -n "$_f" ] && grep -qx quiet=yes "$_f" || bad "gate: the run after the wait is not quiet=yes"
	_w=$(sed -n 's/^gate.wait=//p' "${_f:-/dev/null}")
	[ "${_w:-0}" -ge 1 ] || bad "gate: gate.wait=${_w:-none}, shorter than the hold"
	[ "$(cat "${STUB:?}/n.gate")" -ge 5 ] || bad "gate: $(cat "${STUB:?}/n.gate") readings; it did not wait out the busy ones"
	# Its bound: never quiet, gate_wait=2; the run starts after about 2 s as
	# quiet=no (gate), and the put stores nothing.
	fresh gate2
	store_setup
	qcase g 's/^runs=.*/runs=1/; s/^warmup=.*/warmup=0/'
	printf 'gate_wait=2\n' >> "${E:?}/g.case"
	printf '50\n' > "${STUB:?}/gpu.gate"
	_t0=$(date +%s)
	start sh "$R/bench.sh" baseline put "${E:?}/g.case" "$cA" > /dev/null 2> "${E:?}/err"
	_st=0
	if wait_gone 30 "$BG"; then collect "$BG" || _st=$?; else
		ended 0 "$BG" || true
		bad "gate: the gate waited past its bound of 2 s"
	fi
	_t=$(($(date +%s) - _t0))
	[ "$_t" -ge 2 ] && [ "$_t" -le 20 ] || bad "gate: the bounded wait took $_t s, not about 2"
	[ "$_st" -eq 1 ] || bad "gate: a set with a run outside the gate was not refused (status $_st)"
	grep -q 'run 1 was not quiet (quiet.reason=gate)' "${E:?}/err" || bad "gate: the refusal does not name the run and the gate: $(cat "${E:?}/err")"
	_st=0
	B baseline get "${E:?}/g.case" "$cA" > /dev/null || _st=$?
	[ "$_st" -eq 1 ] || bad "gate: a set that was not quiet was stored"
	# Foreign GPU use in the snapshots, with the gate quiet: refused, both named.
	printf 'gate_settle=1\n' >> "${E:?}/g.case"
	rm -f "${STUB:?}"/n.*
	printf '0\n50\n' > "${STUB:?}/gpu.gate"
	printf '50\n' > "${STUB:?}/gpu.snap"
	_st=0
	B baseline put "${E:?}/g.case" "$cA" > /dev/null 2> "${E:?}/err" || _st=$?
	[ "$_st" -eq 1 ] && grep -q 'quiet.reason=gpu.before,gpu.after)' "${E:?}/err" || bad "gate: GPU use in the snapshots was not refused (status $_st): $(cat "${E:?}/err")"
	# drift and compare print the flag.
	rm -f "${STUB:?}/gpu.snap" "${STUB:?}/gpu.gate"
	B baseline put "${E:?}/g.case" "$cA" > /dev/null || bad "gate: a quiet put failed"
	B baseline put "${E:?}/g.case" "$cC" > /dev/null || bad "gate: a quiet put failed"
	rm -f "${STUB:?}"/n.*
	printf '0\n50\n' > "${STUB:?}/gpu.snap"
	printf '0\n50\n' > "${STUB:?}/gpu.gate"
	_st=0
	_out=$(B drift "${E:?}/g.case" "$cA" 2> "${E:?}/err") || _st=$?
	case $_out in
	*' quiet=no quiet.reason=gpu.after source=stub baseline.quiet=yes baseline.source=stub') ;;
	*) bad "drift: the line of a run that was not quiet is: $_out" ;;
	esac
	B compare "${E:?}/g.case" --before "$cA" --after "$cC" > "${E:?}/out" || true
	grep -qx 'quiet before=yes after=yes' "${E:?}/out" || bad "compare: no quiet line: $(tr '\n' '|' < "${E:?}/out")"
	for _f in $(ls "$BENCH_STORE"/*/"$IC"/g/*/set-*/r-*.run); do
		grep -v '^quiet' "$_f" > "$_f.x" && mv "$_f.x" "$_f"
	done
	B compare "${E:?}/g.case" --before "$cA" --after "$cC" > "${E:?}/out" || true
	grep -qx 'quiet before=yes after=unknown' "${E:?}/out" || bad "compare: a set without flags is not unknown: $(grep quiet "${E:?}/out")"
	[ "$fails" -eq 0 ]
}

# The "after" side of the quiet flag waits for the driver's figure to settle
# (TASK166 round 2): a display-synced run keeps the GPU about 30 % busy, and the
# snapshot read at once after it still shows that. The stub's gpu.snap is the
# snapshots (before, after) and its gpu.gate the gate's reading, then the
# settle's; the case has gate_hold=0, so the gate passes on its first reading.
# The put runs with a sleep on PATH that records each nap's length in $E/naps
# and then naps (FIX126), so a case can read the naps the settle took
# instead of timing the put on a host that may be loaded.
settle_put() {
	# settle_put <before,after snapshot values> <gate readings, \n-separated> <extra case lines> [<poll>]
	fresh settle
	store_setup
	qcase g 's/^runs=.*/runs=1/; s/^warmup=.*/warmup=0/'
	printf '%s\n' "$3" >> "${E:?}/g.case"
	printf '%s\n' "$1" > "${STUB:?}/gpu.snap"
	printf "$2" > "${STUB:?}/gpu.gate"
	mkdir -p "${E:?}/napbin"
	_sl=/bin/sleep
	[ -x "$_sl" ] || _sl=$(command -v sleep)
	printf '#!/bin/sh\nprintf "%%s\\n" "$1" >> "%s"\nexec "%s" "$@"\n' "${E:?}/naps" "$_sl" > "${E:?}/napbin/sleep"
	chmod +x "${E:?}/napbin/sleep"
	: > "${E:?}/naps"
	_t0=$(date +%s)
	_st=0
	# Bounded: a settle wait without its bound would not end.
	BENCH_GATE_POLL=${4:-0.05}
	export BENCH_GATE_POLL
	start env PATH="${E:?}/napbin:$PATH" sh "$R/bench.sh" baseline put "${E:?}/g.case" "$cA" > /dev/null 2> "${E:?}/err"
	if wait_gone 40 "$BG"; then collect "$BG" || _st=$?; else
		ended 0 "$BG" || true
		_st=99
	fi
	_took=$(($(date +%s) - _t0))
}
t_settle() {
	# No check here bounds the time a put takes (FIX126): a loaded host can
	# take seconds between two of the settle's readings, and a gate run once
	# took 5 s over an unknown reading that ends the settle at once. A
	# reading that falls, or an unknown one, ends the settle long before a
	# gate_settle of 60 s, and the put's own bound (40 s, settle_put) reaps
	# a settle that does not end. The count of readings (n.gate) and the naps
	# taken ($E/naps) say what the settle did. The one wall-clock check left
	# is a lower bound (it waited out gate_settle=2), which load only lengthens.
	# A reading that falls from 30 to 5: quiet, with a settle time; the at-once snapshot stays the record.
	settle_put '0
30' '0\n30\n30\n5\n' 'gate_settle=60'
	[ "$_st" -eq 0 ] || bad "settle: a reading that fell from 30 to 5 was refused (status $_st): $(cat "${E:?}/err")"
	_f=$(runfile g)
	grep -qx quiet=yes "${_f:-/dev/null}" || bad "settle: the settled run is not quiet=yes"
	grep -qx quiet.reason=none "${_f:-/dev/null}" || bad "settle: the settled run has a reason"
	grep -Eqx 'gpu.after.settle=[0-9]+' "${_f:-/dev/null}" || bad "settle: no settle time: $(grep settle "${_f:-/dev/null}")"
	grep -qx snap.after.gpu.device=30 "${_f:-/dev/null}" || bad "settle: the snapshot taken at once is not the record"
	# A reading that stays at 30: not quiet, gpu.after, unsettled; the wait is gate_settle.
	settle_put '0
30' '0\n30\n' 'gate_settle=2'
	[ "$_st" -eq 1 ] && grep -q 'quiet.reason=gpu.after)' "${E:?}/err" || bad "settle: a reading that stayed at 30 was not refused with gpu.after (status $_st): $(cat "${E:?}/err")"
	[ "$_took" -ge 2 ] || bad "settle: it did not wait out gate_settle=2 ($_took s)"
	# An unknown reading: not quiet, at once or while settling.
	settle_put '0
unknown' '0\n5\n' 'gate_settle=60'
	[ "$_st" -eq 1 ] && grep -q 'quiet.reason=gpu.after)' "${E:?}/err" || bad "settle: an unknown reading was not refused (status $_st): $(cat "${E:?}/err")"
	[ "$(cat "${STUB:?}/n.gate")" -eq 1 ] || bad "settle: an unknown reading read the GPU $(cat "${STUB:?}/n.gate") times, the gate's alone is 1"
	settle_put '0
30' '0\nunknown\n5\n' 'gate_settle=60'
	[ "$_st" -eq 1 ] && grep -q 'quiet.reason=gpu.after)' "${E:?}/err" || bad "settle: an unknown reading while settling was not refused (status $_st)"
	[ "$(cat "${STUB:?}/n.gate")" -eq 2 ] || bad "settle: an unknown reading while settling did not end it: $(cat "${STUB:?}/n.gate") readings, the gate's and the unknown one are 2"
	# gate_settle=0: today's judgment, at once, with no further reading.
	settle_put '0
30' '0\n5\n' 'gate_settle=0'
	[ "$_st" -eq 1 ] && grep -q 'quiet.reason=gpu.after)' "${E:?}/err" || bad "settle: gate_settle=0 did not judge at once (status $_st)"
	[ "$(cat "${STUB:?}/n.gate")" -eq 1 ] || bad "settle: gate_settle=0 read the GPU $(cat "${STUB:?}/n.gate") times, the gate's alone is 1"
	# Each wait is cut to the time left: a poll of 20 s with gate_settle=4 naps at most 4 s.
	settle_put '0
30' '0\n30\n' 'gate_settle=4' 20
	[ "$_st" -eq 1 ] || bad "settle: a reading that stayed at 30 with a poll of 20 s was not refused (status $_st)"
	awk '$1 > 4 { exit 1 }' "${E:?}/naps" || bad "settle: a poll of 20 s was not cut to the settle time of 4 s (naps: $(tr '\n' ' ' < "${E:?}/naps"))"
	grep -Eqx '[1-4]' "${E:?}/naps" || bad "settle: the poll of 20 s took no nap"
	[ "$fails" -eq 0 ]
}

t_prewarm() {
	fresh prewarm
	store_setup
	# Its length: prewarm=2 of runs of 0.3 s, in the measured runs' hold.
	qcase p 's/^prewarm=.*/prewarm=2/; s/^settings=.*/settings=sleep=0.3 values=1/; s/^runs=.*/runs=1/; s/^warmup=.*/warmup=0/'
	B baseline put "${E:?}/p.case" "$cA" > /dev/null 2> "${E:?}/err" || bad "prewarm: the put failed: $(cat "${E:?}/err")"
	_f=$(runfile p)
	for _l in prewarm.target=2 prewarm.stop=time prewarm.freq.gpu.mhz=1000; do
		grep -qx "$_l" "${_f:-/dev/null}" || bad "prewarm: no line $_l"
	done
	_s=$(sed -n 's/^prewarm.seconds=//p' "${_f:-/dev/null}")
	_n=$(sed -n 's/^prewarm.runs=//p' "${_f:-/dev/null}")
	[ "${_s:-0}" -ge 2 ] && [ "${_s:-0}" -le 8 ] || bad "prewarm: it ran ${_s:-none} s, not its 2"
	[ "${_n:-0}" -ge 2 ] || bad "prewarm: ${_n:-no} pieces of 0.3 s in 2 s"
	grep -q '^snap prewarm ' "${STUB:?}/log" || bad "prewarm: no snapshot was read in it"
	[ "$(awk '{ print $3 }' "${STUB:?}/log" | sort -u | grep -c -v '^none$')" -eq 1 ] && ! grep -q ' none$' "${STUB:?}/log" ||
		bad "prewarm: the pre-warm and the runs did not run in one lock hold: $(awk '{ print $2, $3 }' "${STUB:?}/log" | sort | uniq -c | tr '\n' '|')"
	# drift pre-warms too.
	B drift "${E:?}/p.case" "$cA" > /dev/null 2>&1 || bad "prewarm: the drift failed"
	grep -qx prewarm.stop=time "$(ls "$BENCH_STORE"/*/*/p/*/drift/r-*.run | sed -n 1p)" || bad "prewarm: the drift run did not pre-warm"
	# Its thermal stop: a warning in the third snapshot of prewarm=30.
	fresh prewarm2
	store_setup
	qcase p 's/^prewarm=.*/prewarm=30/; s/^settings=.*/settings=sleep=0.2 values=1/; s/^runs=.*/runs=1/; s/^warmup=.*/warmup=0/'
	printf 'none\nnone\n3\n' > "${STUB:?}/thermal"
	_t0=$(date +%s)
	B baseline put "${E:?}/p.case" "$cA" > /dev/null 2> "${E:?}/err" || bad "prewarm: the put after a thermal stop failed: $(cat "${E:?}/err")"
	[ $(($(date +%s) - _t0)) -lt 20 ] || bad "prewarm: a thermal warning did not stop it early"
	_f=$(runfile p)
	grep -qx prewarm.stop=thermal "${_f:-/dev/null}" && grep -qx prewarm.runs=2 "${_f:-/dev/null}" || bad "prewarm: the thermal stop is not marked: $(grep '^prewarm' "${_f:-/dev/null}" | tr '\n' ' ')"
	grep -q 'the pre-warm stopped after .* a thermal warning' "${E:?}/err" || bad "prewarm: the stop is not said"
	[ "$fails" -eq 0 ]
}

t_quiet_marker() {
	fresh marker
	_q=${BENCH_STATE:?}/quiet
	# Written before the command starts, with its fields; removed when it
	# exits, with 0 or another status.
	_now=$(date +%s)
	env BENCH_SESSION_ID=zzq-session.1 BENCH_QUIET_TTL=100 sh "$R/bench.sh" lock run --quiet zzqlabel -- \
		sh -c 'cat "$1" > "$2"; printf "%s\n" "$BENCH_LOCK_TOKEN" > "$2.tok"' x "$_q" "${E:?}/seen" || bad "marker: lock run --quiet failed"
	[ "$(sed 's/=.*//' "${E:?}/seen" 2> /dev/null | tr '\n' ' ')" = 'pid session label start expires hold token ' ] || bad "marker: its keys are: $(sed 's/=.*//' "${E:?}/seen" 2> /dev/null | tr '\n' ' ')"
	grep -qx session=zzq-session.1 "${E:?}/seen" && grep -qx label=zzqlabel "${E:?}/seen" || bad "marker: the session or label is wrong"
	grep -qx "hold=$(cat "${E:?}/seen.tok")" "${E:?}/seen" || bad "marker: hold is not the lock's token"
	_x=$(sed -n 's/^expires=//p' "${E:?}/seen")
	[ "${_x:-0}" -ge $((_now + 99)) ] && [ "${_x:-0}" -le $((_now + 110)) ] || bad "marker: expires=$_x is not now plus BENCH_QUIET_TTL"
	[ ! -e "$_q" ] || bad "marker: left after the command exited 0"
	_st=0
	env CLAUDE_CODE_SESSION_ID='zzq bad' sh "$R/bench.sh" lock run --quiet zzq -- sh -c 'cp "$1" "$2"; exit 3' x "$_q" "${E:?}/seen" || _st=$?
	[ "$_st" -eq 3 ] || bad "marker: the command's status 3 came back as $_st"
	grep -qx session=none "${E:?}/seen" || bad "marker: a session id with a space was kept"
	[ ! -e "$_q" ] || bad "marker: left after the command exited 3"
	B lock run zzq -- sh -c '[ ! -e "$1" ]' x "$_q" || bad "marker: written without --quiet"
	# A stopped command (74), and TERM or HUP to the lock shell.
	_st=0
	B lock run --quiet zzq -- sh -c 'kill -STOP $$' > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 74 ] && [ ! -e "$_q" ] || bad "marker: left after a stopped command (status $_st)"
	for _sig in TERM HUP; do
		start sh "$R/bench.sh" lock run --quiet zzqsig -- sleep 30 2> /dev/null
		wait_file 10 "$_q" || bad "marker: not written by a lock shell for $_sig"
		kill -s "$_sig" "$BG" 2> /dev/null || true
		wait_gone 40 "$BG" || {
			ended 0 "$BG" || true
			bad "marker: the lock shell did not end on $_sig"
		}
		[ ! -e "$_q" ] || bad "marker: left after $_sig to the lock shell"
	done
	# Under an outer hold: written by the inner --quiet call naming the hold,
	# removed by the outer lock shell.
	B lock run zzqouter -- sh -c 'sh "$1" lock run --quiet zzqinner -- cat "$2" > "$3"; [ -e "$2" ]' x "$R/bench.sh" "$_q" "${E:?}/inner" ||
		bad "marker: the inner call wrote no marker that lasts for the hold"
	grep -qx label=zzqinner "${E:?}/inner" || bad "marker: the inner call's marker is: $(tr '\n' ' ' < "${E:?}/inner")"
	[ ! -e "$_q" ] || bad "marker: the outer lock shell left the inner call's marker"
	# A measurement holds one while it measures.
	cd "${repo:?}" || exit 1
	fast_profiler
	B baseline put null-sleep "$cA" > /dev/null || bad "marker: the put failed"
	grep -qx label=bench.null-sleep "${STUB:?}/marker.seen" || bad "marker: the measurement held none"
	[ ! -e "$_q" ] || bad "marker: left after the measurement"
	[ "$fails" -eq 0 ]
}

# The frequency probe's arithmetic and its failures, on synthetic dumps;
# skipped (and said) without cc or off macOS.
t_freq() {
	fresh freq
	if [ "$(uname -s)" != Darwin ] || ! command -v cc > /dev/null 2>&1; then
		[ "${CHECK_BENCH_REQUIRE:-0}" != 1 ] || { bad "t_freq: CHECK_BENCH_REQUIRE=1 and there is no cc or this is not macOS"; return 1; }
		echo "$me: t_freq skipped: no cc or not macOS"
		return 0
	fi
	cc -O1 -o "${E:?}/freq" "$R/core/freq.c" -framework CoreFoundation -framework IOKit 2> "${E:?}/cc" || bad "freq: freq.c does not build: $(head -3 "${E:?}/cc")"
	printf 'dump.gpu=100 500:0 1000:0\ndump.ecpu=10 900:5\n' > "${E:?}/a"
	printf 'dump.gpu=200 500:100 1000:300\ndump.ecpu=10 900:5\n' > "${E:?}/b"
	"${E:?}/freq" delta "${E:?}/a" "${E:?}/b" > "${E:?}/out" || bad "freq: delta failed"
	printf 'freq.gpu.mhz=875\nfreq.gpu.top=75.0\nfreq.gpu.active=80.0\nfreq.gpu.max=1000\nfreq.ecpu.mhz=0\nfreq.ecpu.top=0.0\nfreq.ecpu.active=0.0\nfreq.ecpu.max=900\n' |
		cmp -s - "${E:?}/out" || bad "freq: delta printed: $(tr '\n' ' ' < "${E:?}/out")"
	_o=$("${E:?}/freq" delta "${E:?}/b" "${E:?}/a")
	[ "$_o" = 'freq=unavailable reason=counters-went-back' ] || bad "freq: counters that went back gave: $_o"
	_o=$("${E:?}/freq" delta "${E:?}/a" "${E:?}/none")
	[ "$_o" = 'freq=unavailable reason=no-dump' ] || bad "freq: a missing dump gave: $_o"
	_st=0
	"${E:?}/freq" dump > "${E:?}/dump" || _st=$?
	[ "$_st" -eq 0 ] && { grep -q '^dump\.[a-z0-9_]*=[0-9]' "${E:?}/dump" || grep -q '^freq=unavailable reason=' "${E:?}/dump"; } ||
		bad "freq: dump on this host exited $_st with: $(head -2 "${E:?}/dump")"
	[ "$fails" -eq 0 ]
}

# The host's own reader prints the keys the stub prints, a name and no path
# in each top line.
t_snap_system() {
	fresh snapsys
	(
		BENCH_ROOT=$R
		. "$R/core/common.sh"
		. "$R/core/host.sh"
		. "$R/core/snap.sh"
		bench_tmp
		bench_snap_system snap "${E:?}/snap"
		bench_snap_system gate "${E:?}/gate"
		rm -rf "${BENCH_TMP:?}"
	)
	_k=$(sed 's/=.*//' "${E:?}/snap" | tr '\n' ' ')
	[ "$_k" = "$SNAP_KEYS " ] || bad "snap: the host's reader prints the keys: $_k"
	[ "$(sed 's/=.*//' "${E:?}/gate" | tr '\n' ' ')" = 'cpu.busy gpu.device ' ] || bad "snap: the gate's reading is: $(tr '\n' ' ' < "${E:?}/gate")"
	grep '^top\.' "${E:?}/snap" | grep -q / && bad "snap: a top line holds a path"
	awk -F= '/^(load|cpu\.busy|mem\.free|swap\.used|gpu\.[a-z]*)=/ && $2 != "unknown" && $2 !~ /^[0-9]+(\.[0-9]+)?$/ { exit 1 }' "${E:?}/snap" || bad "snap: a value is neither a number nor unknown: $(tr '\n' ' ' < "${E:?}/snap")"
	[ "$fails" -eq 0 ]
}

# A process cannot put an argument into a top line by rewriting its argv[0]
# (TASK166 review round 1): the reader takes the kernel's name for it. A
# stand-in ps points the reader's own columns at one process of this test,
# whose argv[0] carries argument-like words (exec -a); the test ends it by
# its own pid.
t_snap_argv() {
	fresh snapargv
	if ! command -v bash > /dev/null 2>&1 || [ ! -x /bin/ps ]; then
		[ "${CHECK_BENCH_REQUIRE:-0}" != 1 ] || { bad "t_snap_argv: CHECK_BENCH_REQUIRE=1 and there is no bash or no /bin/ps"; return 1; }
		echo "$me: t_snap_argv skipped: no bash for exec -a, or no /bin/ps"
		return 0
	fi
	mkdir -p "${E:?}/ps"
	cat > "${E:?}/ps/ps" << 'EOF'
#!/bin/sh
# The reader's arguments without -A and -r, for the one process ZZQ_PID.
n=$#
while [ "$n" -gt 0 ]; do
	a=$1
	shift
	n=$((n - 1))
	case $a in
	-A | -r) ;;
	*) set -- "$@" "$a" ;;
	esac
done
exec /bin/ps -p "$ZZQ_PID" "$@"
EOF
	chmod +x "${E:?}/ps/ps"
	bash -c 'exec -a "zzqfake --token=zzqsecret1 zzq@example.invalid" sleep 30' &
	_p=$!
	_n=0
	until /bin/ps -p "$_p" -o comm= 2> /dev/null | grep -q zzqsecret1; do
		[ "$_n" -lt 50 ] || break
		sleep 0.1
		_n=$((_n + 1))
	done
	/bin/ps -p "$_p" -o comm= 2> /dev/null | grep -q zzqsecret1 || bad "snap argv: the process's argv[0] was not rewritten; the test proves nothing"
	(
		ZZQ_PID=$_p
		PATH=${E:?}/ps:$PATH
		export ZZQ_PID PATH
		BENCH_ROOT=$R
		. "$R/core/common.sh"
		. "$R/core/host.sh"
		. "$R/core/snap.sh"
		bench_tmp
		bench_snap_system snap "${E:?}/snap"
		rm -rf "${BENCH_TMP:?}"
	)
	kill "$_p" 2> /dev/null || true
	collect "$_p" 2> /dev/null || true
	grep -q '^top\.1=[0-9.]* sleep$' "${E:?}/snap" || bad "snap argv: the top line is not the kernel's name: $(grep '^top\.1=' "${E:?}/snap")"
	grep '^top\.' "${E:?}/snap" | grep -q 'zzqsecret1\|--token\|zzqfake\|example' && bad "snap argv: a top line holds the rewritten argv[0]: $(grep '^top\.' "${E:?}/snap" | tr '\n' ' ')"
	[ "$fails" -eq 0 ]
}

# A probe that cannot read the host says why in its dump, and the run file
# carries that reason, not the delta's no-dump (TASK166 review round 1).
t_freq_reason() {
	fresh freqwhy
	store_setup
	cat > "${E:?}/whyprobe" << 'EOF'
#!/bin/sh
case $1 in
dump) echo 'freq=unavailable reason=no-table-fits' ;;
delta) echo 'freq=unavailable reason=no-dump' ;;
*) exit 2 ;;
esac
EOF
	chmod +x "${E:?}/whyprobe"
	qcase w 's/^runs=.*/runs=1/; s/^warmup=.*/warmup=0/'
	env BENCH_FREQ_PROBE="${E:?}/whyprobe" sh "$R/bench.sh" baseline put "${E:?}/w.case" "$cA" > /dev/null 2> "${E:?}/err" ||
		bad "freq reason: the put failed: $(cat "${E:?}/err")"
	_f=$(runfile w)
	grep -qx freq=unavailable "${_f:-/dev/null}" && grep -qx freq.reason=no-table-fits "${_f:-/dev/null}" ||
		bad "freq reason: the run file says: $(grep '^freq' "${_f:-/dev/null}" | tr '\n' ' ')"
	[ "$fails" -eq 0 ]
}

# drift's own guard: a re-measured set that was not quiet does not replace
# the baseline (TASK166 review round 1; put's guard is t_gate's).
t_drift_quiet() {
	fresh driftq
	store_setup
	env BENCH_NULL_VALUES=10,12,11 sh "$R/bench.sh" baseline put null-sleep "$cA" > /dev/null || bad "driftq: put failed"
	_d=$BENCH_STORE/$TOK/$IA/null-sleep/$CH
	echo 50 > "${STUB:?}/gpu.snap"
	_st=0
	env BENCH_NULL_VALUES=20 sh "$R/bench.sh" drift null-sleep "$cA" > "${E:?}/out" 2> "${E:?}/err" || _st=$?
	rm -f "${STUB:?}/gpu.snap"
	[ "$_st" -eq 1 ] && [ ! -d "${_d:?}/set-2" ] || bad "drift: a re-measured set that was not quiet replaced the baseline (status $_st)"
	grep -q 'the re-measured set: run 1 was not quiet' "${E:?}/err" || bad "drift: the refusal does not name the re-measured set: $(cat "${E:?}/err")"
	[ "$fails" -eq 0 ]
}

# BENCH_GATE_POLL is refused before anything runs unless it is a number of
# at least 0.01 s, and a nap is cut to the gate's time left (TASK166 review
# round 1).
t_gate_poll() {
	fresh gatepoll
	for _v in x 0 0.001 -1 1e1 08x 1.2345678901234 99999999999999999999; do
		rm -f "${E:?}/ran"
		_st=0
		env BENCH_GATE_POLL="$_v" sh "$R/bench.sh" lock run zzq -- touch "${E:?}/ran" > /dev/null 2> "${E:?}/err" || _st=$?
		[ "$_st" -eq 2 ] && [ ! -e "${E:?}/ran" ] || bad "gate poll: BENCH_GATE_POLL='$_v' was not refused before the command (status $_st)"
		grep -q 'BENCH_GATE_POLL is a number of seconds, at least 0.01' "${E:?}/err" || bad "gate poll: '$_v': the refusal does not name the setting: $(sed -n 1p "${E:?}/err")"
	done
	env BENCH_GATE_POLL=0.01 sh "$R/bench.sh" lock run zzq -- true || bad "gate poll: BENCH_GATE_POLL=0.01 was refused"
	store_setup
	qcase g 's/^runs=.*/runs=1/; s/^warmup=.*/warmup=0/'
	# gate_wait=4 and a stub that stays busy: the gate always naps (FIX111).
	# With a wait of 1 s the gate broke before its first nap whenever its
	# first snapshot crossed a whole-second boundary, and the uncut nap never
	# ran. A busy reading and 4 s leave no boundary that skips the nap: the
	# uncut nap is 20 s, the cut ones total 4 s. High-resolution time.
	printf 'gate_wait=4\n' >> "${E:?}/g.case"
	printf '50\n' > "${STUB:?}/gpu.gate"
	_t0=$(perl -MTime::HiRes=time -e 'printf "%.3f", time')
	start env BENCH_GATE_POLL=20 sh "$R/bench.sh" baseline put "${E:?}/g.case" "$cA" > /dev/null 2> "${E:?}/err"
	if wait_gone 12 "$BG"; then collect "$BG" || true; else
		ended 0 "$BG" || true
		bad "gate poll: a poll of 20 s was not cut to the gate's wait of 4 s"
	fi
	_t=$(perl -MTime::HiRes=time -e 'printf "%.3f", time - $ARGV[0]' "$_t0")
	case $_t in '' | *[!0-9.]*) bad "gate poll: no elapsed time ($_t)" ;; esac
	# 4 s of wait plus 6 s for the readings and the put on a loaded host: under
	# the 12 s wall bound, well under the 20 s uncut nap.
	perl -e 'exit !($ARGV[0] < 4 + 6)' "$_t" || bad "gate poll: the gate with gate_wait=4 took $_t s, over its wait and one reading"
	# The gate really napped: at least 2 readings, and it timed out on its wait.
	[ "$(cat "${STUB:?}/n.gate")" -ge 2 ] && grep -q 'quiet.reason=gate' "${E:?}/err" || bad "gate poll: the gate did not nap and time out"
	[ "$fails" -eq 0 ]
}

# The probe is built before the gate's first reading also without a
# pre-warm (TASK166 review round 1): a build between the before snapshot
# and the run would land in the run's window. A stand-in cc logs its call
# beside the snapshot stub's and builds the stub probe; the run file then
# names the built probe (freq.source=system).
t_probe_first() {
	fresh probefirst
	if [ "$(uname -s)" != Darwin ]; then
		[ "${CHECK_BENCH_REQUIRE:-0}" != 1 ] || { bad "t_probe_first: CHECK_BENCH_REQUIRE=1 and this is not macOS"; return 1; }
		echo "$me: t_probe_first skipped: not macOS"
		return 0
	fi
	store_setup
	mkdir -p "${E:?}/cc"
	cat > "${E:?}/cc/cc" << 'EOF'
#!/bin/sh
echo cc >> "$STUB/log"
while [ "$#" -gt 1 ]; do
	if [ "$1" = -o ]; then cp "$ZZQ_PROBE" "$2"; fi
	shift
done
EOF
	chmod +x "${E:?}/cc/cc"
	qcase c 's/^runs=.*/runs=1/; s/^warmup=.*/warmup=0/'
	(
		unset BENCH_FREQ_PROBE
		ZZQ_PROBE=${tmp:?}/freqstub
		PATH=${E:?}/cc:$PATH
		export ZZQ_PROBE PATH
		sh "$R/bench.sh" baseline put "${E:?}/c.case" "$cA" > /dev/null 2> "${E:?}/err"
	) || bad "probe first: the put failed: $(cat "${E:?}/err")"
	[ "$(sed -n 1p "${STUB:?}/log" 2> /dev/null)" = cc ] || bad "probe first: the probe was built after: $(tr '\n' '|' < "${STUB:?}/log")"
	_f=$(runfile c)
	grep -qx freq.source=system "${_f:-/dev/null}" && grep -qx snap.source=stub "${_f:-/dev/null}" && grep -qx freq.gpu.mhz=1000 "${_f:-/dev/null}" ||
		bad "probe first: the run file does not name its readers: $(grep 'source=\|^freq' "${_f:-/dev/null}" | tr '\n' ' ')"
	[ "$fails" -eq 0 ]
}

# A numeric setting is `0` or digits that do not start with 0: shell
# arithmetic reads 08 as a bad octal, so it is refused before any
# arithmetic, with the status and message of any other non-number (2).
# zcase <key> <value>: a case file E/z.case that sets the key to the value.
zcase() {
	{
		printf 'adapter=null\nmetric=ms\nabs_tol=0.5\nrel_tol=0.02\n'
		[ "$1" = runs ] || printf 'runs=3\n'
		printf '%s=%s\n' "$1" "$2"
	} > "${E:?}/z.case"
}
t_leading_zero() {
	fresh zero
	# the one validator, direct: 08 09 010 00 refused, 0 8 10 accepted
	for _v in 08 09 010 00 007; do
		( . "$R/core/common.sh" 2> /dev/null; bench_is_uint "$_v" ) && bad "zero: bench_is_uint accepts $_v"
	done
	for _v in 0 8 10 1000; do
		( . "$R/core/common.sh" 2> /dev/null; bench_is_uint "$_v" ) || bad "zero: bench_is_uint refuses $_v"
	done
	for _v in 08 09 010; do
		# each setting the core reads, with a command that must never run
		for _s in BENCH_STOP_GRACE BENCH_LOCK_WAIT BENCH_LOCK_GRACE BENCH_QUIET_TTL; do
			rm -f "${E:?}/ran"
			_st=0
			env "$_s=$_v" sh "$R/bench.sh" lock run zero -- touch "${E:?}/ran" > "${E:?}/out" 2> "${E:?}/err" || _st=$?
			[ "$_st" -eq 2 ] || bad "zero: $_s=$_v was not refused with 2 (status $_st)"
			[ ! -e "${E:?}/ran" ] || bad "zero: $_s=$_v let the command run"
			grep -q "$_s is whole seconds" "${E:?}/err" || bad "zero: $_s=$_v: the refusal does not name the setting: $(sed -n 1p "${E:?}/err")"
		done
		# the case keys: runs, warmup, wall
		for _k in runs warmup wall; do
			zcase "$_k" "$_v"
			_st=0
			B case "${E:?}/z.case" > /dev/null 2> "${E:?}/err" || _st=$?
			[ "$_st" -eq 2 ] || bad "zero: the case key $_k=$_v was not refused with 2 (status $_st)"
		done
		# a wall time, through the bound
		_st=0
		( . "$R/core/common.sh" 2> /dev/null; bench_bounded "$_v" true ) > /dev/null 2>&1 || _st=$?
		[ "$_st" -eq 2 ] || bad "zero: a wall time of $_v was not refused with 2 (status $_st)"
	done
	for _v in 0 8 10; do
		for _s in BENCH_LOCK_WAIT BENCH_LOCK_GRACE BENCH_STOP_GRACE BENCH_QUIET_TTL; do
			_st=0
			env "$_s=$_v" sh "$R/bench.sh" lock run zero -- true > /dev/null 2>&1 || _st=$?
			[ "$_st" -eq 0 ] || bad "zero: $_s=$_v was refused (status $_st)"
		done
		for _k in warmup wall; do
			zcase "$_k" "$_v"
			_st=0
			B case "${E:?}/z.case" > /dev/null 2> "${E:?}/err" || _st=$?
			[ "$_st" -eq 0 ] || bad "zero: the case key $_k=$_v was refused ($(sed -n 1p "${E:?}/err"))"
		done
	done
	for _v in 1 8 10; do
		zcase runs "$_v"
		B case "${E:?}/z.case" > /dev/null 2> "${E:?}/err" || bad "zero: the case key runs=$_v was refused"
	done
	# the card's case end to end: a TERM-ignoring command under BENCH_STOP_GRACE=08
	# is refused at start and never reaches a stop; nothing is left running
	cat > "${E:?}/deaf.sh" <<'DEAF'
#!/bin/sh
trap '' TERM
echo $$ > "$1"
sleep 20
DEAF
	rm -f "${E:?}/deaf.pid"
	_st=0
	env BENCH_STOP_GRACE=08 sh "$R/bench.sh" lock run deaf -- sh "${E:?}/deaf.sh" "${E:?}/deaf.pid" > "${E:?}/deaf.out" 2> "${E:?}/deaf.err" || _st=$?
	[ "$_st" -eq 2 ] || bad "zero: BENCH_STOP_GRACE=08 with a TERM-ignoring command was not refused at start (status $_st)"
	if [ -s "${E:?}/deaf.pid" ]; then
		bad "zero: the TERM-ignoring command was started under BENCH_STOP_GRACE=08"
	fi
	[ "$(B lock status 2> /dev/null)" = lock=free ] || bad "zero: the lock is not free after the refusal"
	[ "$fails" -eq 0 ]
}

# A numeric setting is also at most 9 digits (as the quake2 adapter's own cap is): a
# longer value wraps in shell arithmetic (a grace of 7.8e18 s, or -2) or makes a test
# fail inside its `if`, so a wait never gave up. Refused before any arithmetic, status 2.
t_digit_cap() {
	fresh cap
	for _v in 1234567890 99999999999999999999; do
		( . "$R/core/common.sh" 2> /dev/null; bench_is_uint "$_v" ) && bad "cap: bench_is_uint accepts $_v"
		for _s in BENCH_STOP_GRACE BENCH_LOCK_WAIT BENCH_LOCK_GRACE; do
			rm -f "${E:?}/ran"
			_st=0
			env "$_s=$_v" sh "$R/bench.sh" lock run cap -- touch "${E:?}/ran" > "${E:?}/out" 2> "${E:?}/err" || _st=$?
			[ "$_st" -eq 2 ] || bad "cap: $_s=$_v was not refused with 2 (status $_st)"
			[ ! -e "${E:?}/ran" ] || bad "cap: $_s=$_v let the command run"
			grep -q "$_s is whole seconds" "${E:?}/err" || bad "cap: $_s=$_v: the refusal does not name the setting"
		done
		# runs: before the cap, a 20-digit value made its 1-to-1000 test error inside the if and pass
		for _k in warmup wall runs; do
			zcase "$_k" "$_v"
			_st=0
			B case "${E:?}/z.case" > /dev/null 2> "${E:?}/err" || _st=$?
			[ "$_st" -eq 2 ] || bad "cap: the case key $_k=$_v was not refused with 2 (status $_st)"
		done
		_st=0
		( . "$R/core/common.sh" 2> /dev/null; bench_bounded "$_v" true ) > /dev/null 2>&1 || _st=$?
		[ "$_st" -eq 2 ] || bad "cap: a wall time of $_v was not refused with 2 (status $_st)"
	done
	( . "$R/core/common.sh" 2> /dev/null; bench_is_uint 999999999 ) || bad "cap: bench_is_uint refuses 999999999"
	for _s in BENCH_STOP_GRACE BENCH_LOCK_WAIT BENCH_LOCK_GRACE; do
		_st=0
		env "$_s=999999999" sh "$R/bench.sh" lock run cap -- true > /dev/null 2>&1 || _st=$?
		[ "$_st" -eq 0 ] || bad "cap: $_s=999999999 was refused (status $_st)"
	done
	for _k in warmup wall; do
		zcase "$_k" 999999999
		_st=0
		B case "${E:?}/z.case" > /dev/null 2> "${E:?}/err" || _st=$?
		[ "$_st" -eq 0 ] || bad "cap: the case key $_k=999999999 was refused"
	done
	# the cksum fallback reads CRCs of up to 10 digits: the cap is not theirs
	mkdir -p "${E:?}/fakebin"
	printf '#!/bin/sh\ncat > /dev/null\necho "4294967295 3"\n' > "${E:?}/fakebin/cksum"
	chmod +x "${E:?}/fakebin/cksum"
	_h=$(printf 'x\n' | ( PATH="${E:?}/fakebin:$PATH" BENCH_HASH=cksum; export PATH BENCH_HASH; . "$R/core/common.sh" 2> /dev/null; bench_hash ) 2> /dev/null) || _h=
	[ "$_h" = ffffffffffffffff ] || bad "cap: a 10-digit CRC from cksum gave '$_h'"
	[ "$fails" -eq 0 ]
}

# t_cant_tell (FIX179): a negative assertion (outlived) and forget_ended, over
# a live process this case started, with ps failing for every call, so
# pid_running says 2: the check cannot tell. Each counts a failure that says
# so, sends no signal and (forget_ended) keeps the pid on the cleanup list.
t_cant_tell() {
	fresh cant-tell
	for _k in outlived forget; do
		rm -f "${E:?}/c.sig" "${E:?}/c.pid" "${E:?}/w.out"
		_r=0
		(
			start sleep 30
			echo "$BG" > "${E:?}/c.pid"
			kill() { echo "$*" >> "${E:?}/c.sig"; }
			ps() { return 1; }
			_f0=$fails
			case $_k in
			outlived) outlived "$BG" "cant-tell: the sleeper outlived" ;;
			forget) forget_ended "$BG" ;;
			esac
			[ "$fails" -gt "$_f0" ]
		) > "${E:?}/w.out" 2>&1 || _r=$?
		_c=$(cat "${E:?}/c.pid" 2> /dev/null) || _c=
		[ "$_r" -eq 0 ] || bad "cant-tell: $_k: no failure was counted for a live pid ps gave no answer for: $(cat "${E:?}/w.out")"
		grep -q "cannot tell whether pid $_c " "${E:?}/w.out" || bad "cant-tell: $_k: the failure did not say it cannot tell: $(cat "${E:?}/w.out")"
		[ ! -s "${E:?}/c.sig" ] || bad "cant-tell: $_k: a signal was sent: $(cat "${E:?}/c.sig")"
		[ "$_k" != forget ] || grep -q "^$_c " "${tmp:?}/pids" || bad "cant-tell: $_k: the pid was taken off the cleanup list"
		command kill -0 "$_c" 2> /dev/null || bad "cant-tell: $_k: the sleeper was ended"
		command kill -TERM "$_c" 2> /dev/null || true
		wait_gone 5 "$_c" || bad "cant-tell: $_k: pid $_c outlived its TERM"
		forget "$_c"
	done
	[ "$fails" -eq 0 ]
}

# ---- The real tree. ---------------------------------------------------
all="t_hygiene t_generic t_cleanup_id t_reap_id t_reap_wait t_decoy_term t_lock_exclusion t_lock_tokens t_lock_stop t_spawn_stderr t_lock_defer t_bounded t_core_id t_lock_reclaim t_lock_guard t_lock_stall t_lock_fifo t_lock_order t_lock_dead_waiter t_lock_giveup t_lock_queue_reentry t_lock_queue_status t_lock_zombie_waiter t_lock_stall_place t_lock_ticket_back t_lock_own_pid t_lock_queue_fifo t_lock_ps_fails t_lock_ps_tmp t_lock_queue_link t_lock_state_dir t_store_guard t_host t_inputs t_case t_stats t_store t_drift t_compare t_leading_zero t_quiet_case t_gate t_prewarm t_quiet_marker t_freq t_snap_system t_snap_argv t_freq_reason t_drift_quiet t_gate_poll t_probe_first t_digit_cap t_settle t_cant_tell"
only=
if [ "$#" -gt 0 ]; then
	all=$*
	only=1
fi
failed=
for _t in $all; do
	if ! (cd "$REAL/../.." && "$_t"); then
		failed="$failed $_t"
	fi
done
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
# mutant <name> <file under scripts/bench> <old> <new> <test> [<old2> <new2>];
# <old> empty appends <new> as a line; <old2> <new2>, a second change in the
# same file.
mutant() {
	_m=${tmp:?}/mutant.$1
	rm -rf "${_m:?}"
	cp -R "$REAL" "$_m"
	if [ -n "$3" ]; then
		lit "${_m:?}/$2" "$3" "$4" || { echo "$me: mutant $1 does not apply to $2"; return 1; }
	else
		printf '%s\n' "$4" >> "${_m:?}/$2"
	fi
	if [ "$#" -ge 7 ]; then
		lit "${_m:?}/$2" "$6" "$7" || { echo "$me: mutant $1 (second change) does not apply to $2"; return 1; }
	fi
	if (R=$_m MUTANT=1 && cd "$REAL/../.." && "$5") > "${tmp:?}/mutant.$1.out" 2>&1; then
		echo "$me: mutant $1 passed $5; the test does not catch it"
		return 1
	fi
	echo "$me: mutant $1: caught by $5 ($(sed -n 1p "${tmp:?}/mutant.$1.out" | sed "s/^$me: //"))"
}
mfail=0
# The lock is exclusive twice over (the mkdir, and the holder linked into
# place), so this one drops both.
mutant lock-not-exclusive core/lock.sh 'if mkdir "${BENCH_LOCK_DIR:?}" 2> /dev/null; then' 'if mkdir -p "${BENCH_LOCK_DIR:?}" 2> /dev/null; then' t_lock_exclusion \
	'ln "$_lw_t" "${BENCH_LOCK_DIR:?}/holder"' 'mv -f "$_lw_t" "${BENCH_LOCK_DIR:?}/holder"' || mfail=1
mutant holder-replaced core/lock.sh 'ln "$_lw_t" "${BENCH_LOCK_DIR:?}/holder"' 'mv -f "$_lw_t" "${BENCH_LOCK_DIR:?}/holder"' t_lock_stall || mfail=1
mutant spawn-no-group core/common.sh '	set -m' '	set +m' t_lock_stop || mfail=1
# The fork's standard error (FIX83): left on the caller's, in either form of
# the fork, or a closed one not given /dev/null (the command never starts).
mutant fork-stderr core/common.sh '{ "$@" <&3 3<&- 2>&9 9>&- & } 3<&0 9>&2 2> /dev/null' '{ "$@" <&3 3<&- & } 3<&0' t_spawn_stderr || mfail=1
mutant fork-stderr-nostdin core/common.sh '{ "$@" < /dev/null 2>&9 9>&- & } 9>&2 2> /dev/null' '"$@" < /dev/null &' t_spawn_stderr || mfail=1
mutant fork-stderr-closed core/common.sh '{ :; } 9>&2 2> /dev/null || exec 2> /dev/null' ':' t_spawn_stderr || mfail=1
# The test's own fixture (FIX83): gc.sh with its standard error left open
# prints the job notice of a grandchild that TERM ends before it.
if (GC_QUIET=: MUTANT=1 && cd "$REAL/../.." && t_lock_stop) > "${tmp:?}/mutant.gc-notice.out" 2>&1; then
	echo "$me: mutant gc-notice passed t_lock_stop; the test does not catch it"
	mfail=1
else
	echo "$me: mutant gc-notice: caught by t_lock_stop ($(grep -v "^$me:   " "${tmp:?}/mutant.gc-notice.out" | sed -n 1p | sed "s/^$me: //"))"
fi
# A signal in the window between the "&" and the stored pid: dropped by
# bench_undefer, or acted on at once (the command is left running).
mutant defer-signal-lost core/common.sh 'bench_on_signal "$_ud_s"' ':' t_lock_defer || mfail=1
mutant defer-off bench.sh 'if [ -n "${BENCH_DEFER:-}" ]; then' 'if false; then' t_lock_defer || mfail=1
mutant reentry-any-token core/lock.sh '[ "$(bench_kv "$(bench_lock_holder)" token)" = "$BENCH_LOCK_TOKEN" ]' '[ -n "$BENCH_LOCK_TOKEN" ]' t_lock_tokens || mfail=1
mutant drift-no-t core/stats.sh 'lo = mn - t; hi = mx + t' 'lo = mn; hi = mx' t_drift || mfail=1
mutant core-names-game core/common.sh '' '# the Quake II adapter' t_generic || mfail=1
# FIX172: each site that reads the command's identity, put back to its
# form before it (the watch and the signal as on origin/main, the stop and
# the end of a group taking no notice of what the signal could not send).
mutant spawn-no-id core/common.sh '	bench_spawn_id "$BENCH_SPAWN_PID"' '	:' t_core_id || mfail=1
mutant watch-no-id core/common.sh 'bench_proc_same "$_bw_p" "$_bw_s" || _bw_r=$?' 'bench_pid_alive "$_bw_p" || _bw_r=1; BENCH_PS_F=$(ps -o stat= -p "$_bw_p") || :' t_core_id || mfail=1
mutant signal-no-id core/common.sh '	bench_spawned "$2" || return 1' '	kill -s "$1" -- "-$2" 2> /dev/null && return 0; [ "${3:-}" = group ] || kill -s "$1" "$2" 2> /dev/null || true; return 0' t_core_id || mfail=1
mutant stop-term-untold core/common.sh '	bench_signal_group TERM "$_sg_p" "$_sg_o" || _sg_r=$?' '	bench_signal_group TERM "$_sg_p" "$_sg_o" || :' t_core_id || mfail=1
mutant stop-kill-untold core/common.sh '		bench_signal_group KILL "$_sg_p" "$_sg_o" || _sg_r=$?' '		bench_signal_group KILL "$_sg_p" "$_sg_o" || :' t_core_id || mfail=1
mutant alive-slot-taken core/common.sh '		[ "$_ga_r" -ne 3 ] || return 1' '		:' t_core_id || mfail=1
mutant end-group-any core/common.sh '	bench_group_alive "$1" group || return 0' '	kill -0 -- "-$1" 2> /dev/null || return 0' t_core_id \
	'	while bench_group_alive "$1" group; do' '	while kill -0 -- "-$1" 2> /dev/null; do' || mfail=1
# FIX172, round 1: a leader's group still stopped while ps is silent, the
# group of a command gone before the first ps, the table's entries leaving,
# and a command that leads no group kept under the lock, said, and not
# reported stopped.
mutant spawn-gone-group core/common.sh 'if kill -0 -- "-$1" 2> /dev/null; then BENCH_SPAWN_GROUP=$1; else BENCH_SPAWN_GROUP=-; fi' 'BENCH_SPAWN_GROUP=-' t_core_id || mfail=1
mutant signal-leader-untold core/common.sh '		[ "$_sg2_r" -ne 3 ] || return 1' '		case $_sg2_r in 2 | 3) return "$_sg2_r" ;; esac' t_core_id || mfail=1
mutant watch-leader-note core/common.sh 'if [ "$BENCH_ID_GROUP" = "$_bw_p" ]; then' 'if false; then' t_core_id || mfail=1
mutant forget-none core/common.sh '	BENCH_SPAWNED=$_sf_h$_sf_t' '	:' t_core_id || mfail=1
mutant bounded-says-stopped core/common.sh 'if [ "$_bb_k" -ne 0 ]; then' 'if false; then' t_core_id || mfail=1
mutant lock-no-hold core/lock.sh 'if [ "$_lc_r" -eq 2 ]; then bench_lock_hold_untold' 'if false; then bench_lock_hold_untold' t_core_id || mfail=1
mutant hold-ends-early core/lock.sh '		2) ;;' '		2) break ;;' t_core_id || mfail=1
mutant trap-no-fd8 bench.sh 'if (: >&8) 2> /dev/null; then exec 2>&8; fi' ':' t_core_id || mfail=1
# FIX181: a TERM while a bounded command that could not be stopped runs
# keeps the lock until it ends.
mutant bounded-no-hold bench.sh 'if [ "$_os_r" -eq 2 ]; then bench_lock_hold_untold' 'if false; then bench_lock_hold_untold' t_core_id || mfail=1
# FIX181, round 1: the hold renews its stop of the bounded command with the
# bounded command's grace, not the lock's, which the lock shell gives it.
mutant bounded-hold-lock-grace bench.sh 'bench_lock_hold_untold "$BENCH_BOUNDED_PID" "${BENCH_STOP_GRACE:-10}"' 'bench_lock_hold_untold "$BENCH_BOUNDED_PID" "$(bench_lock_grace)"' t_core_id || mfail=1
# FIX184: a bounded command that could not be stopped is recorded in the
# lock state, and the lock is kept while it may run: the record not
# written, bench_lock_alive or the lock shell's release not honouring it,
# and the pid forgotten before it is cleared.
mutant record-not-written core/lock.sh 'mv -f "$_lk_t" "${BENCH_LOCK_DIR:?}/untold.$1"' 'false' t_core_id || mfail=1
mutant alive-no-record core/lock.sh '	bench_lock_untold_live && return 0' '	:' t_core_id || mfail=1
mutant release-no-wait core/lock.sh 'while bench_lock_untold_live; do bench_nap 1; done' ':' t_core_id || mfail=1
mutant forget-before-clear core/common.sh "$(printf '\t\t')bench_spawn_forget \"\$_bb_p\"" "$(printf '\t\t'):" t_core_id \
	"$(printf '\t\t')BENCH_BOUNDED_PID=" "$(printf '\t\t')bench_spawn_forget \"\$_bb_p\"; BENCH_BOUNDED_PID=" || mfail=1
mutant reclaim-guard core/lock.sh 'if ! mkdir "${BENCH_LOCK_GUARD:?}" 2> /dev/null; then' 'if ! mkdir -p "${BENCH_LOCK_GUARD:?}" 2> /dev/null; then' t_lock_guard || mfail=1
mutant bare-no-grace core/lock.sh '-ge "$_la_grace" ]; then' '-ge 0 ]; then' t_lock_guard || mfail=1
mutant queue-no-ticket-check core/lock.sh 'if bench_lock_head "$_la_label"; then _la_head=1; fi' 'bench_lock_head "$_la_label" || true; _la_head=1' t_lock_fifo || mfail=1
mutant queue-by-pid core/lock.sh "BENCH_LOCK_KEY=\$_lq_t.\$(printf '%010d' \"\$\$\")" "BENCH_LOCK_KEY=\$(printf '%010d' \"\$\$\").\$_lq_t" t_lock_order || mfail=1
mutant queue-ticket-left core/lock.sh 'bench_lock_unqueue; return 75' 'return 75' t_lock_giveup 'then bench_lock_unqueue; fi' 'then :; fi' || mfail=1
mutant queue-dead-kept core/lock.sh 'if [ "$_lh_p" = "$$" ] || ! bench_lock_waiter_running "$_lh_p"; then' 'if false; then' t_lock_dead_waiter || mfail=1
mutant queue-zombie-alive core/lock.sh 'if [ "$_lh_p" = "$$" ] || ! bench_lock_waiter_running "$_lh_p"; then' 'if [ "$_lh_p" = "$$" ] || ! bench_lock_alive "$_lh_h"; then' t_lock_zombie_waiter || mfail=1
mutant queue-own-pid-kept core/lock.sh 'if [ "$_lh_p" = "$$" ] || ! bench_lock_waiter_running "$_lh_p"; then' 'if ! bench_lock_waiter_running "$_lh_p"; then' t_lock_own_pid || mfail=1
mutant queue-fifo-head core/lock.sh 'if [ ! -f "$_lh_f" ] || [ -h "$_lh_f" ]; then continue; fi' ':' t_lock_queue_fifo || mfail=1
mutant queue-fifo-status core/lock.sh 'if [ ! -f "$_ls_f" ] || [ -h "$_ls_f" ]; then continue; fi' ':' t_lock_queue_fifo || mfail=1
# Review round 2: a failed or empty ps reads as dead (the helper, or the
# head check back to bench_pid_running), at the temporaries, in lock
# status for a zombie, and a link read as a ticket.
mutant queue-ps-fail-dead core/lock.sh '|| { bench_pid_alive "$1"; return; }' '|| return 1' t_lock_ps_fails || mfail=1
mutant queue-ps-empty-dead core/lock.sh 'case $_wr_s in Z*) return 1 ;; esac' "case \$_wr_s in '' | Z*) return 1 ;; esac" t_lock_ps_fails || mfail=1
mutant queue-ps-head core/lock.sh '! bench_lock_waiter_running "$_lh_p"; then' '! bench_pid_running "$_lh_p"; then' t_lock_ps_fails || mfail=1
mutant queue-ps-tmp core/lock.sh 'bench_lock_waiter_running "${_lh_p%%.*}" || rm -f "$_lh_f"' 'bench_pid_running "${_lh_p%%.*}" || rm -f "$_lh_f"' t_lock_ps_tmp || mfail=1
mutant queue-zombie-status core/lock.sh 'bench_lock_waiter_running "$(bench_kv "$_ls_q" pid)" || continue' 'bench_pid_alive "$(bench_kv "$_ls_q" pid)" || continue' t_lock_zombie_waiter || mfail=1
mutant queue-link-head core/lock.sh 'if [ ! -f "$_lh_f" ] || [ -h "$_lh_f" ]; then continue; fi' 'if [ ! -f "$_lh_f" ]; then continue; fi' t_lock_queue_link || mfail=1
mutant queue-link-status core/lock.sh 'if [ ! -f "$_ls_f" ] || [ -h "$_ls_f" ]; then continue; fi' 'if [ ! -f "$_ls_f" ]; then continue; fi' t_lock_queue_link || mfail=1
# A maker that backs off (three tabs: the arrival's own enqueue has one), and a waiter whose ticket is gone, take a new key.
mutant queue-backoff-new-key core/lock.sh "$(printf '\t\t\t')bench_lock_enqueue \"\$_la_label\"" "$(printf '\t\t\t')BENCH_LOCK_KEY=; bench_lock_enqueue \"\$_la_label\"" t_lock_stall_place || mfail=1
mutant queue-back-new-key core/lock.sh 'then bench_lock_enqueue "$1"; fi' 'then BENCH_LOCK_KEY=; bench_lock_enqueue "$1"; fi' t_lock_ticket_back || mfail=1
mutant leading-zero-accepted core/common.sh "'' | *[!0-9]* | 0?*) return 1 ;;" "'' | *[!0-9]*) return 1 ;;" t_leading_zero || mfail=1
# TASK166: the quiet evidence.
mutant gate-no-hold core/snap.sh '[ $((_gt_now - _gt_since)) -lt "$_gt_hold" ] || break' 'break' t_gate || mfail=1
mutant gate-no-bound core/snap.sh 'if [ $((_gt_now - _gt_t0)) -ge "$_gt_wait" ]; then' 'if false; then' t_gate || mfail=1
mutant gate-no-reading core/snap.sh "'BEGIN { exit !(g <= lg && c <= lc) }'" "'BEGIN { exit 0 }'" t_gate || mfail=1
mutant foreign-gpu-ignored core/snap.sh "'BEGIN { exit !(g > l) }'" "'BEGIN { exit 1 }'" t_gate || mfail=1
mutant put-not-quiet bench.sh 'if not_quiet "${BENCH_TMP:?}/new" new; then' 'if false; then' t_gate || mfail=1
mutant drift-not-quiet bench.sh 'if not_quiet "${BENCH_TMP:?}/new" re-measured; then' 'if false; then' t_drift_quiet || mfail=1
mutant compare-flag-unknown bench.sh "do printf '%s" "do : '%s" t_gate || mfail=1
mutant prewarm-not-run core/measure.sh '[ "$_pw_left" -gt 0 ] || break' 'break' t_prewarm || mfail=1
mutant prewarm-no-thermal-stop core/measure.sh 'if [ -n "$_pw_w" ]; then' 'if false; then' t_prewarm || mfail=1
mutant drift-no-prewarm bench.sh '	bench_prewarm' '	:' t_prewarm || mfail=1
mutant no-snapshot core/measure.sh "sed 's/^/snap.before./'" ": sed 's/^/snap.before./'" t_store || mfail=1
mutant no-freq core/measure.sh 'bench_freq_window "${BENCH_TMP:?}/freq.before"' ': "${BENCH_TMP:?}/freq.before"' t_store || mfail=1
mutant marker-not-written core/lock.sh 'if [ -n "$_ru_quiet" ]; then bench_quiet_mark "$_ru_label" "$$"; fi' ':' t_quiet_marker || mfail=1
mutant marker-not-removed core/lock.sh '	bench_quiet_release' '	:' t_quiet_marker || mfail=1
mutant freq-unweighted core/freq.c 'wsum += r * y->mhz[k];' 'wsum += r;' t_freq || mfail=1
mutant snap-key-drift core/snap.sh "'lowpower=%s" "'lowpwr=%s" t_snap_system || mfail=1
# Review round 1 (TASK166).
mutant top-argv core/snap.sh 'ps -A -r -o pcpu= -o ucomm=' 'ps -A -r -o pcpu= -o comm=' t_snap_argv || mfail=1
mutant freq-reason-lost core/snap.sh 'if [ -n "$_fw_r" ]; then' 'if false; then' t_freq_reason || mfail=1
mutant gate-poll-unchecked bench.sh 'bench_die 2 "BENCH_GATE_POLL is a number of seconds, at least 0.01"' ':' t_gate_poll || mfail=1
mutant gate-nap-uncut core/snap.sh 'then _gt_nap=$_gt_left; fi' 'then :; fi' t_gate_poll || mfail=1
mutant ttl-unchecked bench.sh 'bench_is_uint "${BENCH_QUIET_TTL:-3600}" ||' 'true ||' t_leading_zero || mfail=1
mutant probe-after-gate core/measure.sh '	bench_freq_init' '	:' t_probe_first || mfail=1
mutant snap-source-lost core/snap.sh 'if [ -n "${BENCH_SNAP_READER:-}" ]; then echo stub; else echo system; fi' 'echo system' t_store || mfail=1
mutant source-stub-lost bench.sh '{ st = 1 }' '{ st = 0 }' t_drift || mfail=1
mutant settle-no-poll core/snap.sh 'bench_snap gate "${BENCH_TMP:?}/settle"' ': > "${BENCH_TMP:?}/settle"' t_settle || mfail=1
mutant settle-no-bound core/snap.sh '[ "$_st_left" -gt 0 ] || return 0' ':' t_settle || mfail=1
mutant settle-not-flagged core/snap.sh '[ "$BENCH_SETTLE" != unsettled ] ||' 'true ||' t_settle || mfail=1
mutant settle-nap-uncut core/snap.sh 'then _st_nap=$_st_left; fi' 'then :; fi' t_settle || mfail=1
mutant settle-zero-polls core/measure.sh '	bench_settle "${BENCH_TMP:?}/snap.after"' '	BENCH_SETTLE=0' t_settle || mfail=1
mutant settle-no-nap core/snap.sh '		bench_nap "$_st_nap"' '		:' t_settle || mfail=1
mutant poll-digits-unbounded core/common.sh '[ "${#1}" -le 12 ]' 'true' t_gate_poll || mfail=1
mutant digit-cap-dropped core/common.sh '	bench_is_uint_max "$1" 9' '	bench_is_uint_max "$1" 99' t_digit_cap || mfail=1
mutant digit-cap-crc core/common.sh '! bench_is_uint_max "$_bh_a" 10' '! bench_is_uint_max "$_bh_a" 9' t_digit_cap || mfail=1
# Last: it writes into the throwaway repository's work tree.
mutant store-in-repo core/store.sh 'bench_outside_repo "$BENCH_STORE_ROOT" "the store (BENCH_STORE)"' ':' t_store_guard || mfail=1
[ "$mfail" -eq 0 ] || exit 1

_ok=1
echo "$me: ok"
