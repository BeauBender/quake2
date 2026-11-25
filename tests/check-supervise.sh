#!/bin/bash
# tests/check-supervise.sh -- the harness's native process supervisor
# (TASK218): scripts/lib/supervise.c and its build helper
# scripts/lib/supervise.sh. bash 3.2; needs cc and nothing else.
#
# The parts, in order:
#
#   builds    supervise.c, its test build (-DSUPERVISE_TEST, the test clock),
#             the test build with a watch table of 8 (-DSV_MAXPIDS=8) and the
#             two test programs build clean with -Wall -Wextra -Werror.
#   helper    sv_path builds the binary on first use into the tool cache
#             (SUPERVISE_CACHE here, a scratch directory), keyed by the
#             source's hash; a second call builds nothing, and a source that
#             does not build is a failure with no path printed.
#   cases     tests/supervise/supervise_test.c runs every case against the
#             test build, with /bin/bash (3.2) and then /bin/sh as the
#             caller shell, the cases that need no test clock against the
#             release build, and every case against the table of 8.
#   sanitizer the same cases built with -fsanitize=address,undefined.
#   mutants   five one-line changes to a copy of supervise.c, each built
#             as the test build: each must fail its own case, for its own
#             reason (the runner's FAIL line names both).
#   shape     no case waits on time: the test programs and this file hold
#             no sleep call or sleep command and no ps.
#
# Every case synchronises on the supervisor's events (--events) or on the
# fake command's private channel, and a timeout is a byte written to the
# test clock (SUPERVISE_TEST_CLOCK_FD), so no verdict depends on the host's
# speed. The check prints its wall time; it needs no lock.
set -u
LC_ALL=C
export LC_ALL
start=$SECONDS
root=$(cd "$(dirname "$0")/.." && pwd -P) || exit 1
src=$root/scripts/lib/supervise.c
helper=$root/scripts/lib/supervise.sh
tdir=$root/tests/supervise

fail() {
	echo "check-supervise: FAIL: $*" >&2
	exit 1
}

command -v cc > /dev/null 2>&1 || fail "cc is not on PATH"
for f in "$src" "$helper" "$tdir/supervise_test.c" "$tdir/fake.c"; do
	[ -f "$f" ] || fail "${f#"$root"/} is missing"
done
tmp=$(mktemp -d "${TMPDIR:-/tmp}/check-supervise.XXXXXX") || fail "cannot make a temporary directory"
trap 'rm -rf "${tmp:?}"' EXIT

W="-Wall -Wextra -Werror"
SAN="-g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"

# builds
# shellcheck disable=SC2086 # the flag lists
{
	cc -O2 $W -o "$tmp/supervise" "$src" &&
		cc -O1 $W -DSUPERVISE_TEST -o "$tmp/supervise-test" "$src" &&
		cc -O1 $W -DSUPERVISE_TEST -DSV_MAXPIDS=8 -o "$tmp/supervise-cap" "$src" &&
		cc -O1 $W -o "$tmp/fake" "$tdir/fake.c" &&
		cc -O1 $W -o "$tmp/runner" "$tdir/supervise_test.c"
} > "$tmp/cc.log" 2>&1 || {
	cat "$tmp/cc.log" >&2
	fail "a build is not clean with $W"
}
[ -s "$tmp/cc.log" ] && {
	cat "$tmp/cc.log" >&2
	fail "a build printed diagnostics"
}
echo "check-supervise: builds clean with $W"

# helper
(
	SUPERVISE_CACHE=$tmp/cache
	export SUPERVISE_CACHE
	# shellcheck source=../scripts/lib/supervise.sh
	. "$helper"
	p=$(sv_path) || exit 1
	[ -x "$p" ] || { echo "sv_path printed $p, which is no program" >&2; exit 1; }
	case $p in "$tmp/cache/"*) ;; *) echo "sv_path built outside the cache: $p" >&2; exit 1 ;; esac
	i1=$(ls -i "$p") || exit 1
	q=$(sv_path) || exit 1
	[ "$q" = "$p" ] || { echo "a second sv_path named another binary: $q" >&2; exit 1; }
	i2=$(ls -i "$q") || exit 1
	[ "$i1" = "$i2" ] || { echo "a second sv_path built again" >&2; exit 1; }
	"$p" -- /usr/bin/true || { echo "supervise -- true gave $?" >&2; exit 1; }
	st=0
	"$p" -- /bin/sh -c 'exit 3' || st=$?
	[ "$st" -eq 3 ] || { echo "supervise -- sh -c 'exit 3' gave $st" >&2; exit 1; }
	# a source that does not build: the helper's copy beside a broken one
	mkdir -p "$tmp/broken" || exit 1
	cp "$helper" "$tmp/broken/supervise.sh" || exit 1
	printf 'this is not C\n' > "$tmp/broken/supervise.c" || exit 1
	SUPERVISE_CACHE=$tmp/cache2
	. "$tmp/broken/supervise.sh"
	st=0
	out=$(sv_path 2> "$tmp/broken.err") || st=$?
	[ "$st" -ne 0 ] || { echo "sv_path over a broken source succeeded" >&2; exit 1; }
	[ -z "$out" ] || { echo "sv_path over a broken source printed a path: $out" >&2; exit 1; }
	grep -q '^supervise: ' "$tmp/broken.err" || { echo "sv_path over a broken source said nothing" >&2; exit 1; }
	[ ! -e "$tmp/cache2" ] || [ -z "$(ls "$tmp/cache2")" ] || { echo "a failed build left a file in the cache" >&2; exit 1; }
) || fail "the build helper (scripts/lib/supervise.sh)"
echo "check-supervise: the helper builds on first use, once, and fails closed"

# cases
for sh in /bin/bash /bin/sh; do
	"$tmp/runner" "$tmp/supervise-test" "$tmp/fake" "$sh" || fail "a case failed with $sh as the caller"
done
"$tmp/runner" --no-clock "$tmp/supervise" "$tmp/fake" /bin/bash || fail "a case failed against the release build"
"$tmp/runner" "$tmp/supervise-cap" "$tmp/fake" /bin/bash || fail "a case failed against the build with a watch table of 8"

# sanitizer
# shellcheck disable=SC2086
{
	cc -O1 $W $SAN -DSUPERVISE_TEST -o "$tmp/supervise-san" "$src" &&
		cc -O1 $W $SAN -o "$tmp/fake-san" "$tdir/fake.c" &&
		cc -O1 $W $SAN -o "$tmp/runner-san" "$tdir/supervise_test.c"
} > "$tmp/san.log" 2>&1 || {
	cat "$tmp/san.log" >&2
	fail "the sanitizer build failed"
}
ASAN_OPTIONS=detect_leaks=0:abort_on_error=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp/runner-san" "$tmp/supervise-san" "$tmp/fake-san" /bin/bash ||
	fail "a case failed under -fsanitize=address,undefined"
echo "check-supervise: the cases pass under -fsanitize=address,undefined"

# mutants: <name> <case> <reason> <sed expression>; each expression must
# change exactly one line of the source
mutant() {
	_mn=$1 _mc=$2 _mr=$3 _ms=$4
	sed "$_ms" "$src" > "$tmp/m-$_mn.c" || fail "mutant $_mn: sed failed"
	_md=$(diff "$src" "$tmp/m-$_mn.c" | grep -c '^>') || _md=0
	[ "$_md" -eq 1 ] || fail "mutant $_mn changes $_md lines, not 1: its pattern no longer fits supervise.c"
	# shellcheck disable=SC2086
	cc -O1 $W -DSUPERVISE_TEST -o "$tmp/m-$_mn" "$tmp/m-$_mn.c" > "$tmp/m-$_mn.cc" 2>&1 ||
		{ cat "$tmp/m-$_mn.cc" >&2; fail "mutant $_mn does not build"; }
	_mst=0
	"$tmp/runner" "$tmp/m-$_mn" "$tmp/fake" /bin/bash "$_mc" > "$tmp/m-$_mn.out" 2>&1 || _mst=$?
	[ "$_mst" -ne 0 ] || { cat "$tmp/m-$_mn.out" >&2; fail "mutant $_mn survived case $_mc"; }
	grep -F "FAIL $_mc (/bin/bash): $_mr" "$tmp/m-$_mn.out" > /dev/null ||
		{ cat "$tmp/m-$_mn.out" >&2; fail "mutant $_mn failed case $_mc, but not for its reason ($_mr)"; }
	echo "check-supervise: mutant $_mn fails $_mc: $_mr"
}
mutant drop-kill term-ignored "a member that ignores TERM was alive after kill-sent" \
	's/sv_group(SIGKILL, "kill-sent")/sv_group(0, "kill-sent")/'
mutant reap-early leader-first "the leader was reaped while a member of its group was alive" \
	's/if (sv_exited && live == 0)$/if (sv_exited)/'
mutant no-forward-term signal-term "TERM to the supervisor was not forwarded" \
	's/^static const int sv_watch\[\] = { SIGINT, SIGTERM, SIGHUP };$/static const int sv_watch[] = { SIGINT, SIGHUP };/'
mutant timeout-zero deadline "status 0, want 124" \
	's/^\([[:space:]]*\)return 124;$/\1return 0;/'
mutant no-wnowait leader-first "no reaped line: the leader was reaped before its group was empty" \
	's/WEXITED | WNOWAIT/WEXITED/'

# shape: no wait on time in the cases. The words are put together so that
# this line does not match itself.
s1=sle s2=ep
pat="(^|[^A-Za-z_])(u|nano)?$s1$s2 *\\(|(^|[^A-Za-z_])$s1$s2 +[0-9]|(^|[^A-Za-z_./-])(/bin/)?p""s +-|/bin/p""s([^A-Za-z]|$)"
if grep -nE "$pat" "$tdir/supervise_test.c" "$tdir/fake.c" "$0"; then
	fail "a test waits on time or polls ps (above)"
fi

echo "check-supervise: ok ($((SECONDS - start)) s)"
