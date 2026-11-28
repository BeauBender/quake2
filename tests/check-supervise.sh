#!/bin/bash
# tests/check-supervise.sh -- the harness's native process supervisor
# (TASK218): scripts/lib/supervise.c and its build helper
# scripts/lib/supervise.sh. bash 3.2; needs cc and nothing else.
#
# The parts, in order:
#
#   builds    supervise.c, its test build (-DSUPERVISE_TEST, the test clock),
#             the test build with a watch table of 8 (-DSV_MAXPIDS=8), the
#             two test programs and the held fake (-DFAKE_HOLD) build clean
#             with -Wall -Wextra -Werror.
#   helper    sv_path builds the binary on first use into the tool cache
#             (SUPERVISE_CACHE here, a scratch directory), keyed by the
#             source's hash; a second call builds nothing, and a source that
#             does not build is a failure with no path printed.
#   refusals  a parent of the cache writable by others (refused unless
#             sticky), an allow ACL on the cache or the binary (refused),
#             and a cache named through a symbolic link (the path printed
#             is the physical one); then two one-line mutants of a copy of
#             supervise.sh, each failing its own case.
#   cases     tests/supervise/supervise_test.c runs every case against the
#             test build, with /bin/bash (3.2) and then /bin/sh as the
#             caller shell, the cases that need no test clock against the
#             release build, every case against the table of 8, and every
#             case against the held fake (FIX225), which stops just after
#             each hello until the runner writes to it or closes it, so a
#             signal sent in that window finds the fake as a loaded host
#             could leave it: the hello must mean the fake is ready.
#   sanitizer the same cases built with -fsanitize=address,undefined.
#   mutants   six one-line changes to a copy of supervise.c, each built
#             as the test build (no-forget with the table of 8): each must
#             fail its own case, for its own reason (the runner's FAIL line
#             names both).
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
# its physical path: sv_path prints the physical path of the binary
rtmp=$(cd "$tmp" && pwd -P) || fail "cannot read the temporary directory's physical path"

W="-Wall -Wextra -Werror"
SAN="-g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"

# builds
# shellcheck disable=SC2086 # the flag lists
{
	cc -O2 $W -o "$tmp/supervise" "$src" &&
		cc -O1 $W -DSUPERVISE_TEST -o "$tmp/supervise-test" "$src" &&
		cc -O1 $W -DSUPERVISE_TEST -DSV_MAXPIDS=8 -o "$tmp/supervise-cap" "$src" &&
		cc -O1 $W -o "$tmp/fake" "$tdir/fake.c" &&
		cc -O1 $W -DFAKE_HOLD -o "$tmp/fake-hold" "$tdir/fake.c" &&
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
	case $p in "$rtmp/cache/"*) ;; *) echo "sv_path built outside the cache: $p" >&2; exit 1 ;; esac
	i1=$(ls -i "$p") || exit 1
	q=$(sv_path) || exit 1
	[ "$q" = "$p" ] || { echo "a second sv_path named another binary: $q" >&2; exit 1; }
	i2=$(ls -i "$q") || exit 1
	[ "$i1" = "$i2" ] || { echo "a second sv_path built again" >&2; exit 1; }
	"$p" -- /usr/bin/true || { echo "supervise -- true gave $?" >&2; exit 1; }
	st=0
	"$p" -- /bin/sh -c 'exit 3' || st=$?
	[ "$st" -eq 3 ] || { echo "supervise -- sh -c 'exit 3' gave $st" >&2; exit 1; }
	# refused <what>: sv_path fails, prints no path, and says why in supervise: lines that name no path
	refused() {
		st=0
		out=$(sv_path 2> "$tmp/refused.err") || st=$?
		[ "$st" -ne 0 ] || { echo "sv_path accepted $1: $out" >&2; return 1; }
		[ -z "$out" ] || { echo "sv_path printed a path for $1: $out" >&2; return 1; }
		grep -q '^supervise: ' "$tmp/refused.err" || { echo "sv_path said nothing for $1" >&2; return 1; }
		if grep -v '^supervise: ' "$tmp/refused.err" | grep -q . || grep -qF -e "$tmp" -e "$rtmp" "$tmp/refused.err"; then
			echo "sv_path printed a line that is not its own, or a path, for $1" >&2
			return 1
		fi
	}
	m=$(ls -ld "$tmp/cache") || exit 1
	case $m in drwx------*) ;; *) echo "the tool cache was made as $m, not mode 0700" >&2; exit 1 ;; esac
	# a binary planted in a cache directory that others can write, then a group-writable one
	SUPERVISE_CACHE=$tmp/c777
	p7=$(sv_path) || exit 1
	chmod 777 "$tmp/c777" || exit 1
	printf '#!/bin/sh\necho PLANTED-RAN\n' > "$p7" || exit 1
	refused "a binary in a directory of mode 0777" || exit 1
	chmod 700 "$tmp/c777" && chmod 775 "$p7" || exit 1
	refused "a group-writable binary" || exit 1
	# a symbolic link where the binary should be
	SUPERVISE_CACHE=$tmp/clink
	pl=$(sv_path) || exit 1
	mv "$pl" "$pl.real" && ln -s "$pl.real" "$pl" || exit 1
	refused "a symbolic link in place of the binary" || exit 1
	# a relative SUPERVISE_CACHE is refused; a relative XDG_CACHE_HOME is ignored, as the XDG spec says
	mkdir "$tmp/cwd" || exit 1
	(
		cd "$tmp/cwd" || exit 1
		SUPERVISE_CACHE=rel
		refused "a relative SUPERVISE_CACHE" || exit 1
		unset SUPERVISE_CACHE
		XDG_CACHE_HOME=rel HOME=$tmp/home
		export XDG_CACHE_HOME HOME
		px=$(sv_path) || exit 1
		case $px in "$rtmp/home/.cache/quake2-tools/"*) ;; *) echo "a relative XDG_CACHE_HOME gave $px" >&2; exit 1 ;; esac
	) || exit 1
	[ -z "$(ls "$tmp/cwd")" ] || { echo "a relative cache path made a directory" >&2; exit 1; }
	SUPERVISE_CACHE=$tmp/cache
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

# refusals (TASK221 slice 2, review round 1, security I1 and I3): the
# cache's parents and the ACLs. Each case has a directory of its own
# under $tmp; a cache the case expects to be accepted is seeded with the
# binary built above, so nothing is built again. ACLs are set with chmod
# +a on files and directories under $tmp only.
# sv_refusals <helper>: runs each case with the helper <helper> (a copy of
# supervise.sh, beside which no source need be: SV_SRC_DIR is the real
# one); prints "ok <case>" or "FAIL <case>: <why>" for each, and returns 1
# when one failed
sv_refusals() (
	# shellcheck source=../scripts/lib/supervise.sh
	. "$1"
	SV_SRC_DIR=$root/scripts/lib
	SUPERVISE_CACHE=$tmp/cache
	export SUPERVISE_CACHE
	bin=$(sv_path) || { echo "FAIL setup: no binary in the cache"; exit 1; }
	name=${bin##*/}
	d=$(mktemp -d "$tmp/ref.XXXXXX") || exit 1
	bad=0
	# seed <dir>: a cache directory <dir> of mode 0700 holding the binary
	seed() { mkdir -p "$1" && chmod 700 "$1" && cp "$bin" "$1/$name" && chmod 755 "$1/$name"; }
	# verdict <case> refused|accepted [<real dir>]: sv_path's answer for the
	# cache SUPERVISE_CACHE names; an accepted path must be in <real dir>
	verdict() {
		st=0
		out=$(sv_path 2> "$d/err") || st=$?
		case $2:$st in
		refused:0) echo "FAIL $1: accepted: $out"; bad=1 ;;
		refused:*)
			if [ -n "$out" ] || ! grep -q '^supervise: ' "$d/err"; then echo "FAIL $1: refused without its line, or with a path"; bad=1; else echo "ok $1: refused"; fi
			;;
		accepted:0)
			if [ "${out%/*}" = "$3" ] && [ -x "$out" ]; then echo "ok $1: accepted in the real directory"; else echo "FAIL $1: accepted as $out, not in $3"; bad=1; fi
			;;
		accepted:*) echo "FAIL $1: refused: $(tr '\n' ' ' < "$d/err")"; bad=1 ;;
		esac
	}
	# a parent writable by others and not sticky
	mkdir -p "$d/p777" && chmod 777 "$d/p777" && seed "$d/p777/cache" || exit 1
	SUPERVISE_CACHE=$d/p777/cache
	verdict parent-0777 refused
	# a sticky parent writable by others, as /tmp is
	mkdir -p "$d/p1777" && chmod 1777 "$d/p1777" && seed "$d/p1777/cache" || exit 1
	SUPERVISE_CACHE=$d/p1777/cache
	verdict parent-1777 accepted "$(cd "$d/p1777/cache" && pwd -P)"
	# an allow ACL on the cache directory
	seed "$d/cacl" && chmod +a "everyone allow list" "$d/cacl" || exit 1
	SUPERVISE_CACHE=$d/cacl
	verdict acl-cache refused
	# an allow ACL on the binary
	seed "$d/bacl" && chmod +a "everyone allow read" "$d/bacl/$name" || exit 1
	SUPERVISE_CACHE=$d/bacl
	verdict acl-binary refused
	# the cache named through a symbolic link in a directory that others
	# can write (mode 0777, not sticky), whose link they could swap: the
	# path given is in the real directory, never through the link
	mkdir -p "$d/open" "$d/safe" && chmod 777 "$d/open" && seed "$d/safe/c" && ln -s "$d/safe/c" "$d/open/cache" || exit 1
	SUPERVISE_CACHE=$d/open/cache
	verdict symlink-parent accepted "$(cd "$d/safe/c" && pwd -P)"
	exit "$bad"
)
sv_refusals "$helper" > "$tmp/refusals.out" 2>&1 || {
	cat "$tmp/refusals.out" >&2
	fail "the helper's refusals (above)"
}
sed 's/^/check-supervise: refusal: /' "$tmp/refusals.out"
# helper mutants: <name> <case> <sed expression>; the expression must
# change exactly one line of a copy of supervise.sh, and the copy must
# fail <case>
hmutant() {
	_hn=$1 _hc=$2
	mkdir -p "$tmp/hm-$_hn" || fail "helper mutant $_hn: no directory"
	sed "$3" "$helper" > "$tmp/hm-$_hn/supervise.sh" || fail "helper mutant $_hn: sed failed"
	_hd=$(diff "$helper" "$tmp/hm-$_hn/supervise.sh" | grep -c '^>') || _hd=0
	[ "$_hd" -eq 1 ] || fail "helper mutant $_hn changes $_hd lines, not 1: its pattern no longer fits supervise.sh"
	sv_refusals "$tmp/hm-$_hn/supervise.sh" > "$tmp/hm-$_hn.out" 2>&1 && { cat "$tmp/hm-$_hn.out" >&2; fail "helper mutant $_hn survived"; }
	grep -q "^FAIL $_hc: " "$tmp/hm-$_hn.out" || { cat "$tmp/hm-$_hn.out" >&2; fail "helper mutant $_hn did not fail $_hc"; }
	echo "check-supervise: helper mutant $_hn fails $_hc"
}
hmutant parents-ok parent-0777 's/^	_sp_d=\$1$/	return 0/'
hmutant acl-unread acl-cache 's/^	\[ "\${_sv_n:-0}" -eq 1 \]$/	:/'

# cases
for sh in /bin/bash /bin/sh; do
	"$tmp/runner" "$tmp/supervise-test" "$tmp/fake" "$sh" || fail "a case failed with $sh as the caller"
done
"$tmp/runner" --no-clock "$tmp/supervise" "$tmp/fake" /bin/bash || fail "a case failed against the release build"
"$tmp/runner" "$tmp/supervise-cap" "$tmp/fake" /bin/bash || fail "a case failed against the build with a watch table of 8"
"$tmp/runner" "$tmp/supervise-test" "$tmp/fake-hold" /bin/bash || fail "a case failed against the held fake"

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

# mutants: <name> <case> <reason> <sed expression> [cc flags]; each
# expression must change exactly one line of the source
mutant() {
	_mn=$1 _mc=$2 _mr=$3 _ms=$4 _mf=${5:-}
	sed "$_ms" "$src" > "$tmp/m-$_mn.c" || fail "mutant $_mn: sed failed"
	_md=$(diff "$src" "$tmp/m-$_mn.c" | grep -c '^>') || _md=0
	[ "$_md" -eq 1 ] || fail "mutant $_mn changes $_md lines, not 1: its pattern no longer fits supervise.c"
	# shellcheck disable=SC2086
	cc -O1 $W -DSUPERVISE_TEST $_mf -o "$tmp/m-$_mn" "$tmp/m-$_mn.c" > "$tmp/m-$_mn.cc" 2>&1 ||
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
# No mutant removes the SIGCHLD reset: on macOS exec keeps an ignored
# SIGCHLD ignored but clears its no-wait, so without the reset the leader
# is still left to waitid, and the case chld-ignored passes either way.
mutant no-forget cap "the last member's exit woke nothing" \
	's/^	return sv_known\[k\] != sv_leader \&\& sv_gone\[k\]/	return 0 \&\& sv_gone[k]/' -DSV_MAXPIDS=8
mutant no-relist untracked "group-empty came while the child of an unwatched member ran" \
	's/more |= j == nseen;/more |= 0;/' -DSV_MAXPIDS=8

# shape: no wait on time in the cases. The words are put together so that
# this line does not match itself.
s1=sle s2=ep
pat="(^|[^A-Za-z_])(u|nano)?$s1$s2 *\\(|(^|[^A-Za-z_])$s1$s2 +[0-9]|(^|[^A-Za-z_./-])(/bin/)?p""s +-|/bin/p""s([^A-Za-z]|$)"
if grep -nE "$pat" "$tdir/supervise_test.c" "$tdir/fake.c" "$0"; then
	fail "a test waits on time or polls ps (above)"
fi

echo "check-supervise: ok ($((SECONDS - start)) s)"
