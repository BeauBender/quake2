#!/bin/sh
# dev-loop gate entry point (gate.test in .claude/dev-loop.json).
#
# The gate first runs the repo invariants in tests/check-*.sh (each runs
# in seconds on an idle host, longer on a loaded one, and can be run on its
# own; the three identity checks build throwaway git repositories), then
# builds the parts that need nothing beyond the system toolchain: the
# dedicated server (q2ded) and the game module. This compiles src/common, src/server,
# src/game and the unix backend. The client and renderers need SDL and
# are not built here. Last it builds and runs the unit-test suite
# (make test: tests/unit against the real q2ded objects, pins under
# tests/pins/unit; see doc/085_unittests.md). Coverage is a separate,
# slower command: scripts/coverage.sh. UPDATE_PINS= TEST_ARGS= on the make
# command line override anything the environment holds: the gate never
# rewrites a pin and never runs a filtered subset of the suite.
#
# Usage: scripts/test.sh            (from anywhere; exits non-zero on failure)
#
# -B forces a full rebuild: the Makefile does not emit header dependencies
# on Darwin (no -MMD), so an incremental build can leave stale objects and
# report success for a commit that does not compile. The full build of these
# two targets takes a few seconds; the suite builds and runs in a few more.
set -eu

# A closed standard error becomes /dev/null, as bench_spawn does (FIX83):
# a write to it would end this script (set -e) with no word, and the
# scheduler's start below (3>&2) would fail and leave $! the watcher's, so
# the gate would wait on that (FIX176). The lock's re-entry (an exec) keeps
# a closed one.
{ :; } 3>&2 2> /dev/null || exec 2> /dev/null

# MAKEFLAGS=-i or -n from the environment would turn a failing or empty run
# into success; the command-line overrides below are kept.
unset MAKEFLAGS GNUMAKEFLAGS MFLAGS
# GLOBIGNORE from the environment would drop checks from the glob below,
# and would reach the scheduler and every check; only the selection of the
# harness checks sets it (TASK227)
# (this host's /bin/sh, bash 3.2, imports it but does not filter globs by it)
unset GLOBIGNORE

cd "$(dirname "$0")/.."

# The host lock (docs/plans/bench-harness.md, "The host lock"): everything
# below runs as one hold of "scripts/bench/bench.sh lock run gate", so no
# timing run on this host shares the machine with the gate. Under a caller
# that holds the lock already (BENCH_LOCK_TOKEN), bench.sh passes straight
# through; so do the entry points the gate runs in turn (the pin selftest).
# The lock is off, with one line on stderr that names why, with
# BENCH_LOCK=0, on GitHub Actions (CI=true and GITHUB_ACTIONS=true: a runner
# has no other lanes), in a tree without scripts/bench/bench.sh, and with no
# directory for its state (BENCH_STATE, XDG_CACHE_HOME and HOME all unset).
# The run under the lock comes back here with BENCH_LOCK_ENTRY=gate (cleared
# at once, so nothing below inherits it) and with the caller's LC_ALL, which
# bench.sh sets to C.
#
# On a TERM, INT or HUP the lock shell gives this run its grace (twice
# BENCH_STOP_GRACE and at least BENCH_STOP_GRACE + 5 s) before it KILLs it
# and frees the lock. Stopping the checks takes up to the scheduler's own
# 10 s (gate_grace in scripts/gate-checks.sh), so the lock shell gets a
# BENCH_STOP_GRACE of at least 10 (a grace of 20 s); the run under it gets
# the caller's value back, so the checks see what the caller set.
lock_entry=${BENCH_LOCK_ENTRY:-}
unset BENCH_LOCK_ENTRY
lock_off=
if [ "$lock_entry" != gate ] || [ -z "${BENCH_LOCK_TOKEN:-}" ]; then
	if [ "${BENCH_LOCK:-}" = 0 ]; then
		lock_off="BENCH_LOCK=0"
	elif [ "${CI:-}" = true ] && [ "${GITHUB_ACTIONS:-}" = true ]; then
		lock_off="CI (GITHUB_ACTIONS=true)"
	elif [ ! -f scripts/bench/bench.sh ]; then
		lock_off="no bench core (scripts/bench/bench.sh)"
	elif [ -z "${BENCH_STATE:-}${XDG_CACHE_HOME:-}${HOME:-}" ]; then
		lock_off="no state directory (BENCH_STATE, XDG_CACHE_HOME and HOME unset)"
	else
		set -- BENCH_LOCK_ENTRY=gate sh "$PWD/scripts/test.sh" "$@"
		# A value that is not whole seconds by the core's rule (bench_is_uint:
		# digits, no leading zero but 0, at most 9) is left alone: it is not
		# raised and bench.sh refuses it at once (status 2, nothing runs).
		case ${BENCH_STOP_GRACE:-10} in
		'' | *[!0-9]* | 0?*) ;;
		??????????*) ;;
		*)
			if [ "${BENCH_STOP_GRACE:-10}" -lt 10 ]; then
				set -- BENCH_STOP_GRACE="$BENCH_STOP_GRACE" "$@"
				BENCH_STOP_GRACE=10
				export BENCH_STOP_GRACE
			fi
			;;
		esac
		if [ -n "${LC_ALL+x}" ]; then set -- LC_ALL="$LC_ALL" "$@"; else set -- -u LC_ALL "$@"; fi
		exec sh scripts/bench/bench.sh lock run gate -- env "$@"
	fi
	echo "test.sh: the host lock is off: $lock_off" >&2
fi

# Under the lock this run leads a process group of its own (bench.sh starts
# it so), and its parent is the lock shell: the caller's own pid, which ran
# this script first, or a host's own holder. A KILL to the caller's process
# group reaches the lock shell alone, so a watcher (gate_watch_sh, in a
# process group of its own) polls this run's parent every 0.5 s. Once it is
# no longer the lock shell, it sends TERM to this run's process group (to
# this run alone if it leads none): before the makes the trap below runs
# gate_stop, which ends the scheduler and every check; during a make that
# make ends with this run. The watcher signals only after ps has shown that
# this run is still its parent and that this run's parent is another
# process; then it waits to be stopped. This run stops it on every way out
# (gate_unwatch, from gate_stop and on EXIT) and waits for it; should this
# run die by KILL, it sees its own parent go and exits within one poll.
# A run whose parent at start is pid 1 lost its lock shell before its own
# shell started (a KILL to the caller's group after bench_spawn's fork):
# the watcher would take launchd for the lock shell and never signal, and
# the lock may be free, so that run says so and exits before it starts
# anything.
gate_watch=
gate_lock_shell=
gate_watch_sh='trap "exit 0" TERM
pp() { ps -o "$1=" -p "$2" 2> /dev/null | tr -d " "; }
on=1
while kill -0 "$PPID" 2> /dev/null; do # bounded-run: allowed: the gate parent watch: it polls the caller, and sends TERM only
	sleep 0.5 2> /dev/null || sleep 1
	o=$(pp ppid $$)
	[ -z "$o" ] || [ "$o" = "$PPID" ] || exit 0
	[ -n "$on" ] && [ "$o" = "$PPID" ] || continue
	l=$(pp ppid "$PPID")
	[ -n "$l" ] && [ "$l" != "$1" ] || continue
	on=
	if [ "$(pp pgid "$PPID")" = "$PPID" ]; then
		kill -s TERM -- "-$PPID" 2> /dev/null
	else
		kill -s TERM "$PPID" 2> /dev/null
	fi
done'
gate_unwatch() {
	if [ -n "$gate_watch" ]; then
		kill -s TERM -- "-$gate_watch" 2> /dev/null || kill -s TERM "$gate_watch" 2> /dev/null || :
		wait "$gate_watch" 2> /dev/null || :
		gate_watch=
	fi
}
if [ "$lock_entry" = gate ] && [ -n "${BENCH_LOCK_TOKEN:-}" ]; then
	gate_lock_shell=$PPID
	if [ "$gate_lock_shell" = 1 ]; then
		echo "test.sh: the lock shell is gone before the gate started" >&2
		exit 143
	fi
	trap gate_unwatch EXIT
	# job control, in its long spelling: the scheduler's short one below
	# must stay the one line that tests/check-gate-parallel.sh mutates
	set -o monitor
	# (its standard error is /dev/null before its fork too: see gate_sched)
	{ sh -c "$gate_watch_sh" gate-watch "$gate_lock_shell" < /dev/null > /dev/null 2>&1 & } 2> /dev/null
	gate_watch=$!
	set +o monitor
fi

# A warning, not a failure: without the hooks the commit and push guards
# (.githooks/) do not run in this checkout.
if [ "$(git config --get core.hooksPath || true)" != ".githooks" ]; then
	echo "test.sh: warning: core.hooksPath is not .githooks in this checkout, so the commit and push hooks are not wired; run scripts/setup.sh (if it says it skipped, wire them by hand: git config core.hooksPath .githooks)"
fi

gate_t0=$(date +%s)

# The build host's gate requires the Metal builds (FIX115): on macOS with
# the Metal compiler (xcrun -f metal, the test both checks use),
# tests/check-ref-metal.sh and tests/check-metal-shim.sh fail rather than
# skip their builds. A host without it still runs them, each saying that its
# build is skipped. A value already in the environment is kept.
if [ "$(uname -s)" = Darwin ] && command -v xcrun > /dev/null 2>&1 && xcrun -f metal > /dev/null 2>&1; then
	CHECK_REF_METAL_REQUIRE=${CHECK_REF_METAL_REQUIRE:-1}
	CHECK_METAL_SHIM_REQUIRE=${CHECK_METAL_SHIM_REQUIRE:-1}
	export CHECK_REF_METAL_REQUIRE CHECK_METAL_SHIM_REQUIRE
fi

# The checks run through scripts/gate-checks.sh (FIX72): GATE_JOBS at a
# time (default half the CPUs, at least 1; GATE_JOBS=1 runs them one after
# another in the glob's order, as before), each into a log of its own that
# is printed whole and in the glob's order with its wall time. The run fails
# if any check fails, and names every one that did; then nothing below runs.
#
# gate_serial lists the checks that write a fixed path another check also
# uses (a build directory, release/, debug/, a scratch path made without
# mktemp, a tracked file of the checkout): they run alone, after the others.
# None does today: each check writes only below its own mktemp -d, and the
# builds among them (autorelease-pool, metal-shim, ref-metal) copy the tree
# into it; static-modules builds in the checkout with STATIC_ROOT in it.
gate_serial=""
# gate_first: the long checks, longest first, started before the others so
# that a long one does not start late and set the gate's wall time
# (GATE_JOBS=1 ignores it). Wall times one at a time on the 6-core build VM
# (2026-11-15, load 3 to 5): metalfx-report 269 s, pins 148 s, bench 107 s,
# gate-parallel about 180 s (its own self-test, 2026-11-16), identity-env
# 46 s, ref-metal 35 s; every other check 23 s or less.
gate_first="check-metalfx-report.sh check-gate-parallel.sh check-pins.sh check-bench.sh check-identity-env.sh check-ref-metal.sh"

# The harness checks a change cannot affect are left out (TASK227), as a
# pull request's CI leaves them out (TASK211, docs/runbook/TASK211.md). The
# changed paths are the commits since the merge base of HEAD and
# refs/remotes/origin/main, and the staged, unstaged and untracked files,
# each read NUL-delimited; scripts/ci-select.sh chooses the harness checks
# from them with tests/ci-map.txt. Only a harness check is ever left out,
# and one that is chosen runs whole. Every check runs, with one line that
# says why, on GATE_ALL=1, with no selector or an empty or unreadable map,
# no origin/main, no merge base, a git that fails, and a selector that
# fails, prints a name it was not given or one twice, or chooses none. The
# checks left out are taken from the scheduler's glob below by GLOBIGNORE
# (its line stays as it is) and from gate_first, since the scheduler
# refuses a name it is not given; a shell that ignores GLOBIGNORE runs
# every check. Paths are never printed, only check names.
gate_why=
gate_skip=
gate_tmp=
gate_hall=
gate_hsel=
gate_names=
gate_odd=
for gate_t in tests/check-*.sh; do
	case ${gate_t#tests/} in *[!A-Za-z0-9._-]*) gate_odd=1 ;; esac
	if [ -f "$gate_t" ]; then
		gate_names="$gate_names${gate_names:+ }${gate_t#tests/}"
	fi
done
# gate_list <file> <names>: the lines of <file> on one line, when each is
# one of <names> and none is there twice; fails otherwise
gate_list() {
	_gl=
	while IFS= read -r _gn || [ -n "$_gn" ]; do
		case $_gn in '' | *[!A-Za-z0-9._-]*) return 1 ;; esac
		case " $2 " in *" $_gn "*) ;; *) return 1 ;; esac
		case " $_gl " in *" $_gn "*) return 1 ;; esac
		_gl="$_gl${_gl:+ }$_gn"
	done < "$1"
	printf '%s' "$_gl"
}
# gate_paths <base>: every changed path, each ended by a NUL: the tree's
# difference from <base>, every path a commit since it touched (with a
# stale origin/main the lane can hold main commits after <base>, and a path
# one of them changed and the lane set back is in CI's list, not in the
# tree's difference; --cc adds what a merge changed beyond its parents),
# the index, the work tree and the untracked files
gate_paths() {
	git diff --no-renames --no-relative --no-ext-diff --no-color --name-only -z "$1" HEAD -- &&
		git log --format= --name-only -z --no-renames --no-relative --no-ext-diff --no-color --cc "$1..HEAD" -- &&
		git diff --cached --no-renames --no-relative --no-ext-diff --no-color --name-only -z -- &&
		git diff --no-renames --no-relative --no-ext-diff --no-color --name-only -z -- &&
		git ls-files -z --others --exclude-standard --full-name
}
# (the names go to the selector unquoted: each holds no blank and no glob
# character, or every check runs)
# shellcheck disable=SC2086
if [ "${GATE_ALL:-}" = 1 ]; then
	gate_why="GATE_ALL=1"
elif [ ! -f scripts/ci-select.sh ] || [ ! -f tests/ci-map.txt ] || [ ! -r tests/ci-map.txt ] || [ ! -s tests/ci-map.txt ]; then
	gate_why="no selector, or an empty or unreadable map (scripts/ci-select.sh, tests/ci-map.txt)"
elif [ -z "$gate_names" ] || [ -n "$gate_odd" ]; then
	gate_why="no check, or a check name the selection does not take"
elif ! gate_tmp=$(mktemp -d "${TMPDIR:-/tmp}/gate-select.XXXXXX" 2> /dev/null); then
	gate_tmp=
	gate_why="no scratch directory"
elif ! bash scripts/ci-select.sh tests/ci-map.txt harness - $gate_names > "$gate_tmp/all" 2> /dev/null ||
	! gate_hall=$(gate_list "$gate_tmp/all" "$gate_names"); then
	gate_why="the selector failed on the map, or printed a name it was not given"
elif ! git rev-parse -q --verify 'refs/remotes/origin/main^{commit}' > /dev/null 2>&1; then
	gate_why="no origin/main (refs/remotes/origin/main)"
elif ! gate_base=$(git merge-base HEAD refs/remotes/origin/main 2> /dev/null) || [ -z "$gate_base" ]; then
	gate_why="no merge base of HEAD and origin/main"
elif ! gate_paths "$gate_base" > "$gate_tmp/changed" 2> /dev/null; then
	gate_why="git failed to list the changed paths"
elif ! bash scripts/ci-select.sh tests/ci-map.txt harness "$gate_tmp/changed" $gate_names > "$gate_tmp/sel" 2> /dev/null ||
	! gate_hsel=$(gate_list "$gate_tmp/sel" "$gate_hall"); then
	gate_why="the selector failed on the changed paths, or printed a name it was not given"
elif [ -z "$gate_hsel" ]; then
	gate_why="the selector chose no harness check"
fi
if [ -n "$gate_tmp" ]; then
	rm -rf "$gate_tmp"
fi
if [ -z "$gate_why" ]; then
	for gate_t in $gate_hall; do
		case " $gate_hsel " in *" $gate_t "*) ;; *) gate_skip="$gate_skip${gate_skip:+ }$gate_t" ;; esac
	done
fi
if [ -n "$gate_skip" ]; then
	# not exported (allexport from SHELLOPTS in the environment would): the
	# scheduler's glob needs it, the scheduler and the checks must not see it
	set +a
	GLOBIGNORE=
	for gate_t in $gate_skip; do
		GLOBIGNORE="$GLOBIGNORE${GLOBIGNORE:+:}tests/$gate_t"
	done
	for gate_t in tests/check-*.sh; do
		case " $gate_skip " in *" ${gate_t#tests/} "*) gate_why="this shell ignores GLOBIGNORE" ;; esac
	done
	if [ -n "$gate_why" ]; then
		unset GLOBIGNORE
		gate_skip=
	fi
fi
# gate_serial and gate_first keep only the checks that run: the scheduler
# refuses a name it is not given
gate_keep=
for gate_t in $gate_serial; do
	case " $gate_skip " in *" $gate_t "*) ;; *) gate_keep="$gate_keep${gate_keep:+ }$gate_t" ;; esac
done
gate_serial=$gate_keep
gate_keep=
for gate_t in $gate_first; do
	case " $gate_skip " in *" $gate_t "*) ;; *) gate_keep="$gate_keep${gate_keep:+ }$gate_t" ;; esac
done
gate_first=$gate_keep
if [ -n "$gate_why" ]; then
	echo "test.sh: every check runs: $gate_why" >&2
elif [ -n "$gate_skip" ]; then
	echo "test.sh: harness checks left out, as CI leaves them out: no path changed since the merge base with origin/main is one they read (tests/ci-map.txt; GATE_ALL=1 runs them): $gate_skip" >&2
else
	echo "test.sh: no harness check is left out: the change reaches every one" >&2
fi
# The scheduler runs in the background so that a SIGHUP, SIGINT or SIGTERM
# to this script is acted on at once (a trap waits for a command in the
# foreground to end): gate_stop passes it on as SIGTERM, which ends the
# scheduler and every check it runs, waits for that, and fails the gate.
# Job control is on for that one command: it leads a process group of its
# own, with the supervisor of each check it runs, so a SIGKILL to this
# script's group leaves them to end every check (below), and it keeps
# SIGINT and SIGQUIT as this script got them (a command started in the
# background without job control ignores both; each check gets every
# signal at its default from its supervisor either way). GATE_PARENT=$$ names
# this script to it: when this script dies, by any signal (SIGKILL too,
# which no trap sees), the scheduler sees it within one poll and ends every
# check, as on SIGTERM. Under the lock a SIGKILL to the caller's process
# group kills the lock shell, not this script: the watcher above sees that
# and sends this script the SIGTERM, so the same holds there.
#
# The child forked for each background start here (the watcher above, the
# scheduler below) is in a process group of its own with SIGTTOU at its
# default before its exec, and a line the shell writes there (under load
# bash's "child setpgid (<pid> to <pid>): Operation not permitted", the
# side of that race the parent's call already won) goes to this script's
# standard error: on a terminal set to tostop it would stop that child and
# hang the gate (FIX176). So that standard error is /dev/null for the fork
# alone; the scheduler gets this script's back on fd 3.
gate_sched=
gate_stop() {
	gate_unwatch
	if [ -n "$gate_lock_shell" ]; then
		_gp=$(ps -o ppid= -p "$$" 2> /dev/null | tr -d ' ')
		if [ -n "$_gp" ] && [ "$_gp" != "$gate_lock_shell" ]; then
			# the caller's output may be a pipe nobody reads any more
			trap '' PIPE
			echo "test.sh: the lock shell (pid $gate_lock_shell) is gone; ending every check" >&2 || :
		fi
	fi
	if [ -n "$gate_sched" ]; then
		kill -s TERM "$gate_sched" 2> /dev/null || :
		wait
	fi
	exit "$1"
}
trap 'gate_stop 129' HUP
trap 'gate_stop 130' INT
trap 'gate_stop 143' TERM
set -m
{ 2>&3 3>&- GATE_PARENT=$$ scripts/gate-checks.sh "$gate_serial" "$gate_first" tests/check-*.sh & } 3>&2 2> /dev/null
gate_sched=$!
set +m
wait "$gate_sched"
trap - HUP INT TERM

make -B -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)" server game
make -B -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)" UPDATE_PINS= TEST_ARGS= test
echo "test.sh: the gate passed in $(($(date +%s) - gate_t0)) s"
