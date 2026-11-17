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
#     ceiling, a failing git; a host without git uses the .git walk);
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
#     eval.
# The tests that do not test the host facts use a fast stand-in for
# system_profiler (fast_profiler). Then fifteen mutants, each applied to a
# copy of scripts/bench/ and each required to fail the test it targets: the
# lock made neither exclusively nor linked, the holder renamed over
# another's, the command spawned without its own process group, its fork
# left with the caller's standard error (with standard input open, and
# closed) or a closed one not given /dev/null, a signal
# in the spawn window dropped, or acted on at once, re-entry accepts any
# token, the drift rule drops t, the core names a game, the reclaim guard
# made with mkdir -p, the holder-less grace dropped, the store's
# outside-repository check deleted, the leading-zero refusal dropped; and one to this test's own fixture, gc.sh
# with its standard error left open.
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
	# Stop any holder this check started that still runs; nothing else.
	while read -r _p; do
		if pid_running "$_p"; then kill -TERM "$_p" 2> /dev/null || true; fi
	done < "${tmp:?}/pids"
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
B() {
	sh "$R/bench.sh" "$@"
}
# start <command...>: runs it in the background (BG), remembered for cleanup.
start() {
	"$@" &
	BG=$!
	echo "$BG" >> "${tmp:?}/pids"
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
# reap <pid>: after a failed wait, ends a process this check started (TERM,
# which its trap turns into a stop of its command; KILL after 30 s), so the
# wait that follows cannot hang.
reap() {
	kill -TERM "$1" 2> /dev/null || true
	wait_gone 30 "$1" || kill -KILL "$1" 2> /dev/null || true
}
# wait_held <seconds> <label>: the lock shows <label> with its child pid.
wait_held() {
	_n=0
	while :; do
		case $(B lock status 2> /dev/null) in
		*"label=$2 "*"child=none "*) ;;
		*"label=$2 "*) return 0 ;;
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
	wait "$_a" || _sa=$?
	_sb=0
	wait "$_b" || _sb=$?
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
		wait "${_ip#*:}" || _st=$?
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
	wait "$_h" || _st=$?
	[ "$_st" -eq 143 ] || bad "release: wanted exit 143 on TERM, got $_st"
	[ "$(B lock status)" = lock=free ] || bad "release: TERM did not release the lock"
	wait_gone 5 "$_c" || bad "release: the command outlived its holder"
	start sh "$R/bench.sh" lock run holder-hup -- sleep 30 > "${E:?}/hup.out" 2>&1
	_h=$BG
	wait_held 5 holder-hup || bad "release: the HUP holder never showed"
	kill -HUP "$_h"
	wait_gone 15 "$_h" || bad "release: the holder did not exit on HUP"
	_st=0
	wait "$_h" || _st=$?
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
	kill -TERM "$(cat "${E:?}/gc.quiet")" 2> /dev/null || bad "stop: the quiet probe's grandchild is gone"
	if ! wait_gone 15 "$_h"; then
		bad "stop: the quiet probe's lock shell did not exit"
		reap "$_h"
	fi
	_st=0
	wait "$_h" || _st=$?
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
	wait "$_h" || _st=$?
	[ "$_st" -eq 143 ] || bad "stop: wanted exit 143 on TERM, got $_st"
	[ "$(B lock status)" = lock=free ] || bad "stop: TERM did not release the lock"
	if pid_running "$(cat "${E:?}/gc.term")"; then bad "stop: a grandchild of the command outlived the lock (TERM)"; fi
	# A command that catches TERM and goes on is killed after the grace,
	# which is at least BENCH_STOP_GRACE + 5 s even when that is 0, so a
	# nested bench.sh stopping a group of its own finishes first. It runs
	# under a lock of its own (s2) while the tests below run, and its end
	# is checked after them.
	mkdir "${E:?}/s2"
	start sh -c 'st=0; env BENCH_STATE="$1/state" BENCH_STOP_GRACE=0 sh "$2/bench.sh" lock run stubborn -- sh "$3" "$1/pid" "$1/log" > "$1/out" 2>&1 || st=$?; echo "$st" > "$1/st"; date +%s > "$1/end"' x "${E:?}/s2" "$R" "${E:?}/stubborn.sh"
	wait_file 5 "${E:?}/s2/pid" || bad "stop: the stubborn command never started"
	_sl=$(sed -n 's/^pid=//p' "${E:?}/s2/state/lock/holder")
	echo "$_sl" >> "${tmp:?}/pids"
	_t0=$(date +%s)
	pid_running "$_sl" && kill -TERM "$_sl" || bad "stop: the stubborn command's lock shell is not running"
	# Ctrl-C at a terminal: INT to the lock shell's process group.
	if [ -n "$INT_OK" ]; then
		set -m
		sh "$R/bench.sh" lock run cc -- sh "${E:?}/gc.sh" "${E:?}/gc.int" "${E:?}/log" int > "${E:?}/int.out" 2>&1 &
		_h=$!
		set +m
		echo "$_h" >> "${tmp:?}/pids"
		wait_file 5 "${E:?}/gc.int" || bad "stop: the grandchild never started (INT)"
		kill -s INT -- "-$_h"
		wait_gone 15 "$_h" || bad "stop: the lock shell did not exit on INT to its group"
		_st=0
		wait "$_h" 2> /dev/null || _st=$?
		[ "$_st" -eq 130 ] || bad "stop: wanted exit 130 on INT, got $_st"
		[ "$(B lock status)" = lock=free ] || bad "stop: INT did not release the lock"
		if pid_running "$(cat "${E:?}/gc.int")"; then bad "stop: a grandchild of the command outlived the lock (INT)"; fi
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
	_c=$(sed -n 's/^child=//p' "${E:?}/state/lock/holder")
	kill -s STOP -- "-$_c"
	if ! wait_gone 10 "$_h"; then
		bad "stop: lock run still waits on a stopped command"
		reap "$_h"
	fi
	_st=0
	wait "$_h" || _st=$?
	[ "$_st" -eq 74 ] || bad "stop: wanted exit 74 from a stopped command, got $_st"
	grep -q '^bench: the command stopped' "${E:?}/stopped.out" || bad "stop: a stopped command was not said"
	[ "$(B lock status)" = lock=free ] || bad "stop: a stopped command's lock was not released"
	if pid_running "$(cat "${E:?}/gc.stop")"; then bad "stop: the stopped command's grandchild outlived the lock"; fi
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
		if ! wait_gone 15 "$_h"; then
			bad "stop: a command that touched the terminal held the lock"
			reap "$_h"
		fi
		wait_file 10 "${E:?}/tty.st" || bad "stop: the terminal test never ended"
		[ "$(cat "${E:?}/tty.st" 2> /dev/null)" = 74 ] || bad "stop: a command that touched the terminal: wanted 74, got $(cat "${E:?}/tty.st" 2> /dev/null)"
		grep -q '^bench: the command stopped' "${E:?}/tty.out" || bad "stop: a command that touched the terminal was not said"
		grep -q after-stty "${E:?}/tty.out" && bad "stop: a command that touched the terminal went on"
		[ "$(B lock status)" = lock=free ] || bad "stop: a command that touched the terminal left the lock held"
	else
		echo "$me: no working script(1) here, so the terminal test is skipped"
	fi
	# The command that catches TERM, started above: killed after the floor.
	wait_file 15 "${E:?}/s2/end" || bad "stop: the lock shell did not exit after TERM to a command that ignores it"
	_t1=$(cat "${E:?}/s2/end" 2> /dev/null || echo 0)
	[ "$(cat "${E:?}/s2/st" 2> /dev/null)" = 143 ] || bad "stop: wanted exit 143 from the stubborn command's lock shell, got $(cat "${E:?}/s2/st" 2> /dev/null)"
	[ $((_t1 - _t0)) -ge 4 ] || bad "stop: with BENCH_STOP_GRACE=0 the lock shell killed its command after $((_t1 - _t0)) s, under its floor of 5 s"
	[ $((_t1 - _t0)) -le 9 ] || bad "stop: a command that ignores TERM was stopped after $((_t1 - _t0)) s, not within the grace floor of 5 s and a margin"
	[ "$(env BENCH_STATE="${E:?}/s2/state" sh "$R/bench.sh" lock status)" = lock=free ] || bad "stop: the stubborn command's lock was not released"
	grep -q got-term "${E:?}/s2/log" || bad "stop: the stubborn command never got TERM"
	if pid_running "$(cat "${E:?}/s2/pid")"; then bad "stop: the stubborn command outlived the lock"; fi
	# The command that left one that catches TERM running, started above.
	wait_file 20 "${E:?}/l2/end" || bad "stop: lock run did not return after a command left one that ignores TERM running"
	_t3=$(cat "${E:?}/l2/end" 2> /dev/null || echo 0)
	[ "$(cat "${E:?}/l2/st" 2> /dev/null)" = 0 ] || bad "stop: wanted exit 0 from a command that left one that ignores TERM running, got $(cat "${E:?}/l2/st" 2> /dev/null)"
	[ $((_t3 - _t2)) -ge 6 ] || bad "stop: with BENCH_STOP_GRACE=0 the work a command left running was killed after $((_t3 - _t2)) s, under the 1 s it is given and the floor of 5 s"
	[ "$(env BENCH_STATE="${E:?}/l2/state" sh "$R/bench.sh" lock status)" = lock=free ] || bad "stop: the lock was not released after a command left one that ignores TERM running"
	grep -q got-term "${E:?}/l2/log" || bad "stop: the work a command left running never got TERM"
	if pid_running "$(cat "${E:?}/l2/pid" 2> /dev/null)"; then bad "stop: the work a command left running, which ignores TERM, outlived the lock"; fi
	# Commands that print nothing themselves: no job notice either.
	if foreign "${E:?}/stopped.out" "${E:?}/term.out" "${E:?}/left.err" > "${E:?}/foreign"; then
		cat "${E:?}/foreign"
		bad "stop: the lock shell printed a line that is not its own (a job notice), above"
	fi
	# By now each grandchild would have written its line.
	while [ "$(date +%s)" -le $((_tl + 3)) ]; do sleep 0.2; done
	if grep -q 'late$' "${E:?}/log"; then bad "stop: something wrote after the lock was free: $(grep 'late$' "${E:?}/log" | tr '\n' ' ')"; fi
	if pid_running "$(cat "${E:?}/gc.left" 2> /dev/null)"; then bad "stop: the work a command left running outlived the lock"; fi
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
	if ! wait_gone 15 "$_h"; then
		bad "defer: the lock shell did not exit on a TERM in the window"
		reap "$_h"
	fi
	_st=0
	wait "$_h" || _st=$?
	[ "$_st" -eq 143 ] || bad "defer: wanted exit 143 on a TERM in the window, got $_st"
	[ "$(B lock status)" = lock=free ] || bad "defer: the lock was not released"
	if pid_running "$(cat "${E:?}/d.gc")"; then bad "defer: a TERM in the window left the command's grandchild running"; fi
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
		if pid_running "$(cat "${E:?}/b.gc" 2> /dev/null)"; then echo "$me: bounded: a grandchild outlived the wall bound"; exit 1; fi
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
		if pid_running "$(cat "${E:?}/b.leader" 2> /dev/null)"; then echo "$me: bounded: a stopped command outlived the bound"; exit 1; fi
		if pid_running "$(cat "${E:?}/b.stop" 2> /dev/null)"; then echo "$me: bounded: a stopped command's grandchild outlived the bound"; exit 1; fi
		if grep -q bstop-went-on "${E:?}/log"; then echo "$me: bounded: a stopped command went on after the bound stopped it"; exit 1; fi
		while [ "$(date +%s)" -le $((_tl + 3)) ]; do sleep 0.2; done
		if grep -q 'late$' "${E:?}/log"; then echo "$me: bounded: something wrote after the bound returned: $(grep 'late$' "${E:?}/log" | tr '\n' ' ')"; exit 1; fi
		if pid_running "$(cat "${E:?}/b.left" 2> /dev/null)"; then echo "$me: bounded: the work a command left running outlived the bound"; exit 1; fi
	) || bad "bounded: the wall-time bound does not stop the whole command"
	[ "$fails" -eq 0 ]
}

t_lock_reclaim() {
	fresh reclaim
	mkdir -p "${E:?}/state"
	sh -c 'exit 0' &
	_dead=$!
	wait "$_dead" || true
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
		wait "$_p1" || _s1=$?
		_s2=0
		wait "$_p2" || _s2=$?
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
	wait "$_dead" || true
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
	wait "$_g" 2> /dev/null || true
	wait_file 5 "${E:?}/member" || bad "guard: the group member never started"
	cat "${E:?}/member" >> "${tmp:?}/pids"
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
	wait "$_w" 2> /dev/null || _st=$?
	[ "$_st" -eq 75 ] || bad "poll: the waiter with a long poll exited $_st, not 75"
	kill -TERM "$_h"
	wait_gone 15 "$_h" || bad "poll: the holder did not exit on TERM"
	wait "$_h" 2> /dev/null || true
	[ "$fails" -eq 0 ]
}

t_lock_stall() {
	fresh stall
	# A maker that stalls between its mkdir and its holder write for longer
	# than the grace, while a waiter reclaims the directory and makes it
	# again, backs off and leaves the taker's holder file as it was: the two
	# never hold the lock at once.
	_s=${tmp:?}/stall
	rm -rf "${_s:?}"
	cp -R "$R" "$_s"
	lit "${_s:?}/core/lock.sh" 'BENCH_LOCK_SINCE=$(bench_utc)' 'BENCH_LOCK_SINCE=$(bench_utc); sleep 2.5' || bad "stall: the injected stall does not apply"
	: > "${E:?}/slog"
	start sh "$_s/bench.sh" lock run maker -- sh -c 'echo N-start >> "$1"; sleep 0.3; echo N-end >> "$1"' x "${E:?}/slog" > "${E:?}/maker.out" 2>&1
	_mk=$BG
	_k=0
	while [ ! -d "${E:?}/state/lock" ] && [ "$_k" -lt 50 ]; do
		sleep 0.1
		_k=$((_k + 1))
	done
	start env BENCH_LOCK_GRACE=1 sh "$R/bench.sh" lock run taker -- sh -c 'echo C-start >> "$1"; sleep 3.5; echo C-end >> "$1"' x "${E:?}/slog" > "${E:?}/taker.out" 2>&1
	_c=$BG
	_k=0
	while ! grep -q 'waiting for the lock held by' "${E:?}/maker.out" && [ "$_k" -lt 80 ]; do
		sleep 0.1
		_k=$((_k + 1))
	done
	grep -q 'waiting for the lock held by taker ' "${E:?}/maker.out" || bad "stall: the maker that backed off does not wait for the taker: $(cat "${E:?}/maker.out")"
	case $(B lock status) in
	*'label=taker '*) ;;
	*) bad "stall: the taker's holder file did not survive the stalled maker: $(B lock status)" ;;
	esac
	wait_gone 20 "$_mk" || bad "stall: the stalled maker did not finish"
	wait_gone 20 "$_c" || bad "stall: the taker did not finish"
	_s1=0
	wait "$_mk" || _s1=$?
	_s2=0
	wait "$_c" || _s2=$?
	[ "$_s1" -eq 0 ] && [ "$_s2" -eq 0 ] || bad "stall: the maker and the taker exited $_s1 and $_s2"
	case $(tr '\n' ' ' < "${E:?}/slog") in
	"C-start C-end N-start N-end " | "N-start N-end C-start C-end ") ;;
	*) bad "stall: a stalled maker and a taker held the lock at once: $(tr '\n' ' ' < "${E:?}/slog")" ;;
	esac
	[ "$(B lock status)" = lock=free ] || bad "stall: the lock was left held"
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
	_want="case case_hash commit inputs host class hash kind index start load.before load.after quiet quiet.reason gate.wait"
	for _k in $SNAP_KEYS; do _want="$_want snap.before.$_k"; done
	for _k in $SNAP_KEYS; do _want="$_want snap.after.$_k"; done
	_want="$_want freq.gpu.mhz freq.gpu.top freq.gpu.active freq.gpu.max prewarm.target prewarm.seconds prewarm.runs prewarm.stop prewarm.freq prewarm.freq.reason pre.null metric.ms metric.frames "
	for _f in "${_d:?}"/set-1/r-*.run; do
		_keys=$(sed 's/=.*//' "$_f" | tr '\n' ' ')
		[ "$_keys" = "$_want" ] || bad "store: a run file has the keys: $_keys"
		for _l in quiet=yes quiet.reason=none gate.wait=0 snap.before.gpu.device=0 snap.after.top.5='1.0 zzqproc5' freq.gpu.mhz=1000 \
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
		[ "$_out" = "drift=inside value=${_x#*:} lo=9.5 hi=12.5 t=0.5 median=11 runs=3 metric=ms quiet=yes baseline.quiet=yes" ] || bad "drift: ${_x%%:*} gave: $_out"
	done
	[ "$(ls "${_d:?}"/set-*)" = "$_runs" ] || bad "drift: a run inside the range changed the baseline"
	[ "$(ls "${_d:?}/drift" | grep -c '^r-.*\.run$')" -eq 3 ] || bad "drift: the drift runs are not kept in drift/"
	_out=$(env BENCH_NULL_VALUES=12.51 sh "$R/bench.sh" drift null-sleep "$cA" 2> "${E:?}/err") || bad "drift: an outside run failed"
	printf '%s\n' "$_out" | grep -qx 'drift=outside value=12.51 lo=9.5 hi=12.5 t=0.5 median=11 runs=3 metric=ms quiet=yes baseline.quiet=yes' || bad "drift: 12.51 gave: $(printf '%s\n' "$_out" | sed -n 1p)"
	printf '%s\n' "$_out" | grep -qx "baseline=replaced case=null-sleep inputs=$IA runs=3" || bad "drift: an outside run did not replace the baseline"
	B baseline get null-sleep "$cA" | grep -x 'metric=ms n=3 median=12.51 p90=12.51 min=12.51 max=12.51' > /dev/null || bad "drift: the new baseline is not the re-measured one"
	[ "$(ls "${_d:?}/set-1" | grep -c '^r-.*\.run$')" -eq 3 ] && [ -d "${_d:?}/set-2" ] || bad "drift: the old baseline is not kept as set-1 beside the new set-2"
	# The relative term: t = max(0, 0.1 * 11) = 1.1, so the range is [8.9, 13.1].
	sed 's/^abs_tol=.*/abs_tol=0/; s/^rel_tol=.*/rel_tol=0.1/' "$R/cases/null-sleep.case" > "${E:?}/rel.case"
	env BENCH_NULL_VALUES=10,12,11 sh "$R/bench.sh" baseline put "${E:?}/rel.case" "$cA" > /dev/null || bad "drift: put of the relative case failed"
	_out=$(env BENCH_NULL_VALUES=13.1 sh "$R/bench.sh" drift "${E:?}/rel.case" "$cA")
	[ "$_out" = 'drift=inside value=13.1 lo=8.9 hi=13.1 t=1.1 median=11 runs=3 metric=ms quiet=yes baseline.quiet=yes' ] || bad "drift: 13.1 (relative) gave: $_out"
	_out=$(env BENCH_NULL_VALUES=8.8 sh "$R/bench.sh" drift "${E:?}/rel.case" "$cA" | sed -n 1p)
	[ "$_out" = 'drift=outside value=8.8 lo=8.9 hi=13.1 t=1.1 median=11 runs=3 metric=ms quiet=yes baseline.quiet=yes' ] || bad "drift: 8.8 (relative) gave: $_out"
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
		printf 'quiet before=yes after=yes\n'
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
	for _l in prewarm=x prewarm=1.5 gate_gpu=101 gate_cpu=-1 gate_hold=x gate_wait=2.5; do
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
	if wait_gone 30 "$BG"; then wait "$BG" || _st=$?; else
		reap "$BG"
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
	printf '0\n' > "${STUB:?}/gpu.gate"
	printf '50\n' > "${STUB:?}/gpu.snap"
	_st=0
	B baseline put "${E:?}/g.case" "$cA" > /dev/null 2> "${E:?}/err" || _st=$?
	[ "$_st" -eq 1 ] && grep -q 'quiet.reason=gpu.before,gpu.after)' "${E:?}/err" || bad "gate: GPU use in the snapshots was not refused (status $_st): $(cat "${E:?}/err")"
	# drift and compare print the flag.
	rm -f "${STUB:?}/gpu.snap" "${STUB:?}/gpu.gate"
	B baseline put "${E:?}/g.case" "$cA" > /dev/null || bad "gate: a quiet put failed"
	B baseline put "${E:?}/g.case" "$cC" > /dev/null || bad "gate: a quiet put failed"
	printf '0\n50\n' > "${STUB:?}/gpu.snap"
	_st=0
	_out=$(B drift "${E:?}/g.case" "$cA" 2> "${E:?}/err") || _st=$?
	case $_out in
	*' quiet=no quiet.reason=gpu.after baseline.quiet=yes') ;;
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
			reap "$BG"
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
		for _s in BENCH_STOP_GRACE BENCH_LOCK_WAIT BENCH_LOCK_GRACE; do
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
		for _s in BENCH_LOCK_WAIT BENCH_LOCK_GRACE BENCH_STOP_GRACE; do
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

# ---- The real tree. ---------------------------------------------------
all="t_hygiene t_generic t_lock_exclusion t_lock_tokens t_lock_stop t_spawn_stderr t_lock_defer t_bounded t_lock_reclaim t_lock_guard t_lock_stall t_lock_state_dir t_store_guard t_host t_inputs t_case t_stats t_store t_drift t_compare t_leading_zero t_quiet_case t_gate t_prewarm t_quiet_marker t_freq t_snap_system"
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
mutant reclaim-guard core/lock.sh 'if ! mkdir "${BENCH_LOCK_GUARD:?}" 2> /dev/null; then' 'if ! mkdir -p "${BENCH_LOCK_GUARD:?}" 2> /dev/null; then' t_lock_guard || mfail=1
mutant bare-no-grace core/lock.sh '-ge "$_la_grace" ]; then' '-ge 0 ]; then' t_lock_guard || mfail=1
mutant leading-zero-accepted core/common.sh "'' | *[!0-9]* | 0?*) return 1 ;;" "'' | *[!0-9]*) return 1 ;;" t_leading_zero || mfail=1
# Last: it writes into the throwaway repository's work tree.
mutant store-in-repo core/store.sh 'bench_outside_repo "$BENCH_STORE_ROOT" "the store (BENCH_STORE)"' ':' t_store_guard || mfail=1
[ "$mfail" -eq 0 ] || exit 1

_ok=1
echo "$me: ok"
