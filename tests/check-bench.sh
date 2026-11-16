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
#     and HUP (INT is ignored by a background job, so it is not driven
#     here); reclaim of a dead holder, logged, and of two waiters that
#     reclaim at once exactly one gets the lock; a holder-less lock after
#     the grace period; the default state directory and its override;
#   - the host token and class line: stable, from system facts only, and
#     free of the identity values tests/check-no-local-identity.sh refuses
#     (hostname -s, whoami, $HOME), with the scrub and the salt tried on
#     synthetic values;
#   - the inputs: equal across a documentation-only commit, changed by a
#     src/ edit, a missing path as the "missing" marker, the case's flags,
#     with shasum and with cksum;
#   - case files: the key whitelist, no evaluation of values, the hash of
#     the normalised content;
#   - the store layout, atomic writes (a failing rename leaves no run),
#     reads that ignore temporary files, superseded/, a preflight refusal
#     and the wall-time bound;
#   - statistics against hand-computed values; the drift rule inside, at
#     the edge and outside; compare's table and verdict;
#   - the core and bench.sh name no game and use no eval.
# Then four mutants, each applied to a copy of scripts/bench/ and each
# required to fail the test it targets: the lock skips the exclusive mkdir
# (mkdir -p), re-entry accepts any token, the drift rule drops t, the core
# names a game.
#
# Usage: tests/check-bench.sh
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
# fresh <name>: a new directory E with its own lock state and store.
fresh() {
	E=${tmp:?}/$1
	rm -rf "${E:?}"
	mkdir -p "${E:?}"
	BENCH_STATE=${E:?}/state
	BENCH_STORE=${E:?}/store
	BENCH_LOCK_POLL=0.1
	BENCH_LOCK_WAIT=10
	export BENCH_STATE BENCH_STORE BENCH_LOCK_POLL BENCH_LOCK_WAIT
	unset BENCH_LOCK_TOKEN BENCH_NULL_VALUES BENCH_NULL_REFUSE BENCH_HASH 2> /dev/null || true
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
	for _tok in forged:t-1-1-forged "stale:$_stale" "empty:"; do
		_kind=${_tok%%:*}
		_st=0
		_out=$(env BENCH_LOCK_TOKEN="${_tok#*:}" BENCH_LOCK_WAIT=1 sh "$R/bench.sh" lock run intruder -- echo ran 2> "${E:?}/err") || _st=$?
		[ "$_st" -eq 75 ] || bad "tokens: a $_kind token: wanted exit 75, got $_st"
		[ -z "$_out" ] || bad "tokens: a $_kind token ran its command under someone else's lock"
		grep -q 'still held by holder-h (pid ' "${E:?}/err" || bad "tokens: the $_kind-token waiter did not name the holder"
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
	# The command reads the caller's standard input, not /dev/null.
	_out=$(printf 'from-stdin\n' | B lock run reader -- cat)
	[ "$_out" = from-stdin ] || bad "lock run: the command did not get the caller's standard input"
	_st=0
	B lock run 'bad label' -- true 2> /dev/null || _st=$?
	[ "$_st" -eq 2 ] || bad "tokens: a label with a space was not refused"
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
	cd "${repo:?}" || exit 1
	_iA=$(B inputs "$cA")
	_iB=$(B inputs "$cB")
	_iC=$(B inputs "$cC")
	_iD=$(B inputs "$cD")
	_iF=$(B inputs "$cF")
	printf '%s\n' "$_iA" | grep -Eq '^i-[0-9a-f]{12}$' || bad "inputs: not i- and 12 hex digits"
	[ "$_iA" = "$_iB" ] || bad "inputs: a documentation-only commit changed the inputs"
	[ "$_iA" != "$_iC" ] || bad "inputs: a src/ edit did not change the inputs"
	[ "$_iC" != "$_iD" ] || bad "inputs: adding third_party/ did not change the inputs"
	[ "$_iD" != "$_iF" ] || bad "inputs: a top-level .make file did not change the inputs"
	_l=$(B inputs --list "$cA")
	printf '%s\n' "$_l" | grep -qx 'path=third_party id=missing' || bad "inputs: a missing path is not the marker 'missing'"
	printf '%s\n' "$_l" | grep -qx "path=src id=$(gitc rev-parse "$cA:src")" || bad "inputs: src is not hashed by its tree id"
	printf '%s\n' "$_l" | grep -Eqx 'hash=(shasum|cksum)' || bad "inputs: --list does not say the hash tool"
	[ "$(printf '%s\n' "$_l" | sed -n '$p')" = "$_iA" ] || bad "inputs: --list does not end with the id"
	B inputs --list "$cF" | grep -x "path=extra.make id=$(gitc rev-parse "$cF:extra.make")" > /dev/null || bad "inputs: a top-level .make file is not listed"
	printf 'adapter=null\nruns=1\nmetric=ms\nabs_tol=0\nrel_tol=0\nbuild_flags=X=1\n' > "${E:?}/f1.case"
	printf 'adapter=null\nruns=1\nmetric=ms\nabs_tol=0\nrel_tol=0\nbuild_flags=X=2\n' > "${E:?}/f2.case"
	_f1=$(B inputs "$cA" "${E:?}/f1.case")
	[ "$_f1" != "$(B inputs "$cA" "${E:?}/f2.case")" ] || bad "inputs: the case's flags are not part of the inputs"
	[ "$_f1" != "$_iA" ] || bad "inputs: flags and no flags gave the same inputs"
	B inputs --list "$cA" "${E:?}/f1.case" | grep -x 'flags=X=1' > /dev/null || bad "inputs: --list does not show the flags"
	_kA=$(env BENCH_HASH=cksum sh "$R/bench.sh" inputs "$cA")
	_kB=$(env BENCH_HASH=cksum sh "$R/bench.sh" inputs "$cB")
	_kC=$(env BENCH_HASH=cksum sh "$R/bench.sh" inputs "$cC")
	printf '%s\n' "$_kA" | grep -Eq '^i-[0-9a-f]{12}$' || bad "inputs: the cksum id is not i- and 12 hex digits"
	[ "$_kA" = "$_kB" ] && [ "$_kA" != "$_kC" ] || bad "inputs: the cksum ids do not follow the rule"
	_st=0
	B inputs no-such-commit 2> /dev/null || _st=$?
	[ "$_st" -eq 2 ] || bad "inputs: an unknown commit was not refused"
	_st=0
	B inputs --output=x 2> /dev/null || _st=$?
	[ "$_st" -eq 2 ] || bad "inputs: an option in place of the commit was not refused"
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
	[ "$(ls "${_d:?}" | grep -c '^r-.*\.run$')" -eq 3 ] || bad "store: not three run files in <store>/<host>/<inputs>/<case>/<case-hash>/ under \$HOME/.cache/<repo>-bench"
	ls -a "${_d:?}" | grep -q '^\.tmp\.' && bad "store: a temporary file was left"
	for _f in "${_d:?}"/r-*.run; do
		_keys=$(sed 's/=.*//' "$_f" | tr '\n' ' ')
		[ "$_keys" = "case case_hash commit inputs host class hash kind index start load.before load.after pre.null metric.ms metric.frames " ] ||
			bad "store: a run file has the keys: $_keys"
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
	# Reads ignore temporary files.
	printf 'metric.ms=999\n' > "${_d:?}/.tmp.r-zzq.run"
	B baseline get null-sleep "$cA" > "${E:?}/get" || true
	grep -qx 'metric=ms n=3 median=11 p90=12 min=10 max=12' "${E:?}/get" && grep -q 'runs=3$' "${E:?}/get" || bad "store: a temporary file was read"
	# Writes are a rename: when it fails, no run file appears.
	mkdir "${E:?}/shim"
	printf '#!/bin/sh\nfor a; do l=$a; done\ncase $l in *.run) exit 1 ;; esac\nexec /bin/mv "$@"\n' > "${E:?}/shim/mv"
	chmod +x "${E:?}/shim/mv"
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
	# The wall-time bound stops a run that outlives it.
	sed 's/^settings=.*/settings=sleep=5 values=1/; s/^wall=.*/wall=1/; s/^warmup=.*/warmup=0/' "$R/cases/null-sleep.case" > "${E:?}/slow.case"
	_st=0
	env BENCH_STORE="${E:?}/slowstore" sh "$R/bench.sh" baseline put "${E:?}/slow.case" "$cA" > /dev/null 2>&1 || _st=$?
	[ "$_st" -eq 3 ] || bad "store: a run over its wall time did not exit 3 (got $_st)"
	# --replace keeps the old baseline in superseded/.
	env BENCH_NULL_VALUES=20 sh "$R/bench.sh" baseline put --replace null-sleep "$cA" > /dev/null || bad "store: put --replace failed"
	B baseline get null-sleep "$cA" | grep -x 'metric=ms n=3 median=20 p90=20 min=20 max=20' > /dev/null || bad "store: --replace did not store the new runs"
	[ "$(ls "${_d:?}/superseded"/*/ | grep -c '^r-.*\.run$')" -eq 3 ] || bad "store: superseded/ does not hold the three old runs"
	[ "$fails" -eq 0 ]
}

t_drift() {
	fresh drift
	store_setup
	env BENCH_NULL_VALUES=10,12,11 sh "$R/bench.sh" baseline put null-sleep "$cA" > /dev/null || bad "drift: baseline put failed"
	_d=$BENCH_STORE/$TOK/$IA/null-sleep/$CH
	_runs=$(ls "${_d:?}" | grep '^r-')
	# t = max(0.5, 0.02 * 11) = 0.5, so the range is [9.5, 12.5].
	for _x in 12.3:12.3 12.5:12.5 9.5:9.5; do
		_st=0
		_out=$(env BENCH_NULL_VALUES="${_x%%:*}" sh "$R/bench.sh" drift null-sleep "$cA" 2> "${E:?}/err") || _st=$?
		[ "$_st" -eq 0 ] || bad "drift: status $_st for ${_x%%:*}"
		[ "$_out" = "drift=inside value=${_x#*:} lo=9.5 hi=12.5 t=0.5 median=11 runs=3 metric=ms" ] || bad "drift: ${_x%%:*} gave: $_out"
	done
	[ "$(ls "${_d:?}" | grep '^r-')" = "$_runs" ] || bad "drift: a run inside the range changed the baseline"
	[ "$(ls "${_d:?}/drift" | grep -c '^r-.*\.run$')" -eq 3 ] || bad "drift: the drift runs are not kept in drift/"
	_out=$(env BENCH_NULL_VALUES=12.51 sh "$R/bench.sh" drift null-sleep "$cA" 2> "${E:?}/err") || bad "drift: an outside run failed"
	printf '%s\n' "$_out" | grep -qx 'drift=outside value=12.51 lo=9.5 hi=12.5 t=0.5 median=11 runs=3 metric=ms' || bad "drift: 12.51 gave: $(printf '%s\n' "$_out" | sed -n 1p)"
	printf '%s\n' "$_out" | grep -qx "baseline=replaced case=null-sleep inputs=$IA runs=3" || bad "drift: an outside run did not replace the baseline"
	B baseline get null-sleep "$cA" | grep -x 'metric=ms n=3 median=12.51 p90=12.51 min=12.51 max=12.51' > /dev/null || bad "drift: the new baseline is not the re-measured one"
	[ "$(ls "${_d:?}/superseded"/*/ | grep -c '^r-.*\.run$')" -eq 3 ] || bad "drift: the old baseline is not in superseded/"
	# The relative term: t = max(0, 0.1 * 11) = 1.1, so the range is [8.9, 13.1].
	sed 's/^abs_tol=.*/abs_tol=0/; s/^rel_tol=.*/rel_tol=0.1/' "$R/cases/null-sleep.case" > "${E:?}/rel.case"
	env BENCH_NULL_VALUES=10,12,11 sh "$R/bench.sh" baseline put "${E:?}/rel.case" "$cA" > /dev/null || bad "drift: put of the relative case failed"
	_out=$(env BENCH_NULL_VALUES=13.1 sh "$R/bench.sh" drift "${E:?}/rel.case" "$cA")
	[ "$_out" = 'drift=inside value=13.1 lo=8.9 hi=13.1 t=1.1 median=11 runs=3 metric=ms' ] || bad "drift: 13.1 (relative) gave: $_out"
	_out=$(env BENCH_NULL_VALUES=8.8 sh "$R/bench.sh" drift "${E:?}/rel.case" "$cA" | sed -n 1p)
	[ "$_out" = 'drift=outside value=8.8 lo=8.9 hi=13.1 t=1.1 median=11 runs=3 metric=ms' ] || bad "drift: 8.8 (relative) gave: $_out"
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

# ---- The real tree. ---------------------------------------------------
all="t_hygiene t_generic t_lock_exclusion t_lock_tokens t_lock_reclaim t_lock_state_dir t_host t_inputs t_case t_stats t_store t_drift t_compare"
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

# ---- Mutants: each must fail the test that targets it. -----------------
# mutant <name> <file under scripts/bench> <old> <new> <test>; <old> empty
# appends <new> as a line.
mutant() {
	_m=${tmp:?}/mutant.$1
	rm -rf "${_m:?}"
	cp -R "$REAL" "$_m"
	if [ -n "$3" ]; then
		lit "${_m:?}/$2" "$3" "$4" || { echo "$me: mutant $1 does not apply to $2"; return 1; }
	else
		printf '%s\n' "$4" >> "${_m:?}/$2"
	fi
	if (R=$_m MUTANT=1 && cd "$REAL/../.." && "$5") > "${tmp:?}/mutant.$1.out" 2>&1; then
		echo "$me: mutant $1 passed $5; the test does not catch it"
		return 1
	fi
	echo "$me: mutant $1: caught by $5 ($(sed -n 1p "${tmp:?}/mutant.$1.out" | sed "s/^$me: //"))"
}
mfail=0
mutant lock-mkdir core/lock.sh 'if mkdir "${BENCH_LOCK_DIR:?}" 2> /dev/null; then' 'if mkdir -p "${BENCH_LOCK_DIR:?}" 2> /dev/null; then' t_lock_exclusion || mfail=1
mutant reentry-any-token core/lock.sh '[ "$(bench_kv "$(bench_lock_holder)" token)" = "$BENCH_LOCK_TOKEN" ]' '[ -n "$BENCH_LOCK_TOKEN" ]' t_lock_tokens || mfail=1
mutant drift-no-t core/stats.sh 'lo = mn - t; hi = mx + t' 'lo = mn; hi = mx' t_drift || mfail=1
mutant core-names-game core/common.sh '' '# the Quake II adapter' t_generic || mfail=1
[ "$mfail" -eq 0 ] || exit 1

_ok=1
echo "$me: ok"
