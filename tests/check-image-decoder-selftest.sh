#!/bin/sh
# Invariant: tests/check-image-decoder.sh fails fast, and leaves no stopped
# process behind, when leaks(1) cannot inspect a process (FIX149).
#
# Runs the whole check with a stub leaks(1) first on PATH, in scratch, each
# case with its own verdict:
# (a) a stub that hangs: the check fails at its probe, within
#     IMGDEC_LEAKS_PROBE_SECONDS, with the line naming the cause (Developer
#     Mode or an unlocked session), not at the leak run's bound;
# (b) a stub that starts its target suspended, stops it (SIGSTOP) and
#     hangs: the check fails at the probe and the probe's target is gone;
# (c) a stub that passes the probe and does (b) to the leak run's target:
#     the check fails at the leak run's bound and that target is gone, while
#     an unrelated process of the same name, started here, still runs;
# (d) a stub that behaves: the check passes;
# (e) IMGDEC_LEAKS_PROBE_SECONDS that is not a whole number, or is 0, is
#     refused;
# (f) the staged case (FIX201): a hanging stub is bounded by the supervisor
#     (scripts/lib/supervise.sh) and leaves nothing behind, checked on the
#     supervisor's events (IMGDEC_SELFTEST_EVENTS): deadline, group-empty and
#     reaped, and no escaped;
# (g) a stub whose helper leaves the process group: the check fails and names
#     the escaped pid from the events.
# (h) (i) (j) the mutants, each the check with one guard taken out and
#     refused by its own case: a plain kill in place of the supervisor (the
#     stopped target survives), --events dropped (no events), the escaped
#     scan dropped (the pid is not named).
# (k) the self-test itself interrupted (TERM) in its escape case: no escaped
#     helper outlives it.
# The hanging stubs block on a fifo nobody writes, and the supervisor ends
# them; the self-test ends any that is left.
# Off Darwin the check runs no leaks(1); there the hanging stub must leave
# the check passing.
set -eu
cd "$(dirname "$0")/.."
me=check-image-decoder-selftest
check=tests/check-image-decoder.sh

tmp=$(mktemp -d "${TMPDIR:-/tmp}/image-decoder-selftest.XXXXXX")
# ends a process this self-test started, if it still runs the program named
end_own()
{
	_c=$(ps -o comm= -p "$1" 2>/dev/null || :)
	if [ -n "$_c" ] && [ "${_c##*/}" = "$2" ]; then
		kill -KILL "$1" 2>/dev/null || :
	fi
}
cleanup()
{
	if [ -f "${tmp:?}/pids" ]; then
		while read -r _p _n; do
			end_own "$_p" "$_n"
		done < "${tmp:?}/pids"
	fi
	# a target a stub stopped, whichever program it was (a red run leaves one)
	for _f in "${tmp:?}/stop.pids" "${tmp:?}/late.pids"; do
		[ -f "$_f" ] || continue
		while read -r _p; do
			end_own "$_p" leakprobe
			end_own "$_p" imgdecode_plain
		done < "$_f"
	done
	# a hang stub the check left, and the helper case (k) saw
	for _f in "${tmp:?}/hang.pids" "${tmp:?}/k.pids"; do
		[ -f "$_f" ] || continue
		while read -r _p; do
			end_own "$_p" sh
		done < "$_f"
	done
	rm -rf "${tmp:?}"
}
trap cleanup EXIT

fails=0
fail()
{
	echo "$me: $1"
	fails=$((fails + 1))
}

# stub <name> <body>: a leaks(1) stub in its own PATH directory
stub()
{
	mkdir -p "${tmp:?}/$1"
	printf '#!/bin/sh\n%s\n' "$2" > "${tmp:?}/$1/leaks"
	chmod +x "${tmp:?}/$1/leaks"
}

# run <stub> [VAR=value...]: runs the check with that stub first on PATH,
# under an alarm of $secs seconds (120 unless set); its output in
# ${tmp}/out, its status in $rc
secs=120
run()
{
	_d=$1
	shift
	rc=0
	env PATH="${tmp:?}/$_d:$PATH" IMGDEC_CHECK_SECONDS=8 IMGDEC_LEAKS_PROBE_SECONDS=2 "$@" \
		perl -e 'alarm shift; exec @ARGV' "$secs" sh "$check" > "${tmp:?}/out" 2>&1 || rc=$?
}

# gone <pidfile> <name>: every PID the stub recorded no longer runs <name>
gone()
{
	[ -s "$1" ] || return 1
	while read -r _p; do
		printf '%s %s\n' "$_p" "$2" >> "${tmp:?}/pids"
		_c=$(ps -o comm= -p "$_p" 2>/dev/null || :)
		if [ -n "$_c" ] && [ "${_c##*/}" = "$2" ]; then
			return 1
		fi
	done < "$1"
	return 0
}

cause='leaks(1) could not inspect a process within 2 s'

# a stub that hangs on a fifo nobody writes and records its PID in
# ${tmp}/hang.pids, which cleanup reads
mkfifo "${tmp:?}/never"
hangbody="echo \$\$ >> \"${tmp:?}/hang.pids\"
read _x < \"${tmp:?}/never\""
stub hang "$hangbody"
stub good 'while [ "$#" -gt 0 ] && [ "$1" != -- ]; do shift; done
shift
"$@"
echo "Process 1: 0 leaks for 0 total leaked bytes."'

if [ "$(uname -s)" != Darwin ]; then
	run hang
	[ "$rc" -eq 0 ] || { cat "${tmp:?}/out"; fail "off Darwin, the check failed with a hanging leaks(1) on PATH (status $rc)"; }
	[ "$fails" -eq 0 ] || exit 1
	echo "$me: ok (off Darwin: the check passes with a hanging leaks(1) on PATH)"
	exit 0
fi

# the stop stub: spawns its target suspended, as leaks(1) does, stops it,
# records its PID and hangs
cat > "${tmp:?}/spawnstop.c" << 'EOF'
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <unistd.h>
extern char **environ;
int
main(int argc, char **argv)
{
	posix_spawnattr_t a;
	pid_t pid;
	FILE *f;

	if (argc < 3 || posix_spawnattr_init(&a) != 0 ||
		posix_spawnattr_setflags(&a, POSIX_SPAWN_START_SUSPENDED) != 0 ||
		posix_spawn(&pid, argv[2], NULL, &a, argv + 2, environ) != 0)
		return 2;
	kill(pid, SIGSTOP);
	if ((f = fopen(argv[1], "a")) == NULL)
		return 2;
	fprintf(f, "%d\n", (int)pid);
	fclose(f);
	for (;;)
		pause();
}
EOF
printf '#include <unistd.h>\nint main(void){pause();return 0;}\n' > "${tmp:?}/decoy.c"
mkdir -p "${tmp:?}/decoy"
cc=${CC:-cc}
if ! $cc -o "${tmp:?}/spawnstop" "${tmp:?}/spawnstop.c" ||
	! $cc -o "${tmp:?}/decoy/imgdecode_plain" "${tmp:?}/decoy.c"; then
	echo "$me: the stub helpers do not build"
	exit 1
fi
cp "${tmp:?}/decoy/imgdecode_plain" "${tmp:?}/decoy/other"

stub stop "while [ \"\$#\" -gt 0 ] && [ \"\$1\" != -- ]; do shift; done
shift
exec \"${tmp:?}/spawnstop\" \"${tmp:?}/stop.pids\" \"\$@\""
stub late "while [ \"\$#\" -gt 0 ] && [ \"\$1\" != -- ]; do shift; done
shift
case \${1##*/} in
leakprobe) \"\$@\"; exit 0 ;;
esac
exec \"${tmp:?}/spawnstop\" \"${tmp:?}/late.pids\" \"\$@\""

# the escape stub: its helper leaves the process group (setsid), records its
# pid in ${tmp}/esc.pids and blocks on the fifo nobody writes. With
# IMGDEC_SELFTEST_INTERRUPT (case (k)) the helper first opens that fifo for
# writing, writes its pid there and keeps it open across its exec, so the
# reader at the other end sees end of file once the helper has ended.
cat > "${tmp:?}/escape.pl" << 'EOF'
use POSIX qw(setsid);
use Fcntl qw(F_SETFD);
if (defined $ARGV[2] && length $ARGV[2]) {
	open(H, ">", $ARGV[2]) or exit 2;
	fcntl(H, F_SETFD, 0) or exit 2;
	syswrite(H, "$$\n");
}
setsid();
open(F, ">>", $ARGV[0]) or exit 2;
print F "$$\n";
close(F);
exec "sh", "-c", "read x < $ARGV[1]";
EOF
stub escape "echo \$\$ >> \"${tmp:?}/hang.pids\"
perl \"${tmp:?}/escape.pl\" \"${tmp:?}/esc.pids\" \"${tmp:?}/never\" \"${IMGDEC_SELFTEST_INTERRUPT:-}\" &
read _x < \"${tmp:?}/never\""

# the inner run of case (k): case (g) alone, then the self-test is
# interrupted (TERM) while the escaped helper still runs
if [ -n "${IMGDEC_SELFTEST_INTERRUPT:-}" ]; then
	: > "${tmp:?}/ev.g"
	run escape IMGDEC_SELFTEST_EVENTS="${tmp:?}/ev.g"
	kill -TERM $$
fi

# (a)
run hang
if [ "$rc" -eq 0 ] || ! grep -qF "$cause" "${tmp:?}/out" ||
	! grep -qF 'Developer Mode (DevToolsSecurity -enable)' "${tmp:?}/out" ||
	grep -qF 'outlived its 8 s bound' "${tmp:?}/out"; then
	cat "${tmp:?}/out"
	fail "(a) a hanging leaks(1): the check did not fail at its probe with the cause (status $rc)"
fi

# (b)
run stop
if [ "$rc" -eq 0 ] || ! grep -qF "$cause" "${tmp:?}/out"; then
	cat "${tmp:?}/out"
	fail "(b) a leaks(1) that stops its target: the check did not fail at its probe (status $rc)"
fi
gone "${tmp:?}/stop.pids" leakprobe || fail "(b) the probe's stopped target is still there (or was never started)"

# (c)
"${tmp:?}/decoy/imgdecode_plain" &
decoy=$!
printf '%s imgdecode_plain\n' "$decoy" >> "${tmp:?}/pids"
run late
if [ "$rc" -eq 0 ] || ! grep -qF 'leaks(1) outlived its 8 s bound' "${tmp:?}/out"; then
	cat "${tmp:?}/out"
	fail "(c) a leaks(1) that stops the leak run's target: the check did not fail at the leak run's bound (status $rc)"
fi
gone "${tmp:?}/late.pids" imgdecode_plain || fail "(c) the leak run's stopped target is still there (or was never started)"
kill -0 "$decoy" 2>/dev/null || fail "(c) an unrelated imgdecode_plain was killed"
end_own "$decoy" imgdecode_plain
{ wait "$decoy"; } 2>/dev/null || :

# (d)
run good
if [ "$rc" -ne 0 ] || ! grep -qx 'check-image-decoder: ok' "${tmp:?}/out"; then
	cat "${tmp:?}/out"
	fail "(d) a leaks(1) that behaves: the check failed (status $rc)"
fi

# (e)
for v in abc 0; do
	run good IMGDEC_LEAKS_PROBE_SECONDS="$v"
	if [ "$rc" -eq 0 ] || ! grep -q '^check-image-decoder: IMGDEC_LEAKS_PROBE_SECONDS ' "${tmp:?}/out"; then
		cat "${tmp:?}/out"
		fail "(e) IMGDEC_LEAKS_PROBE_SECONDS='$v' was not refused (status $rc)"
	fi
done

# (f) the staged case: a hanging stub, bounded and left nothing behind
: > "${tmp:?}/ev.f"
run hang IMGDEC_SELFTEST_EVENTS="${tmp:?}/ev.f"
if [ "$rc" -eq 0 ] || ! grep -qF "$cause" "${tmp:?}/out"; then
	cat "${tmp:?}/out"
	fail "(f) a hanging leaks(1): the check did not fail at its probe with the cause (status $rc)"
fi
for _e in '^deadline$' '^group-empty$' '^reaped status='; do
	grep -q "$_e" "${tmp:?}/ev.f" || fail "(f) the supervisor's events lack $_e"
done
if grep -q '^escaped ' "${tmp:?}/ev.f"; then
	fail "(f) the supervisor's events name an escaped process"
fi
gone "${tmp:?}/hang.pids" sh || fail "(f) the hanging stub is still there (or was never started)"

# (g) a stub whose helper leaves the process group: the check names it. The
# supervisor reports an escape when it ends the group, at the deadline, so
# this case ends at the probe's 2 s bound (the status is 124 there; the pid
# must be named whatever the status).
: > "${tmp:?}/ev.g"
run escape IMGDEC_SELFTEST_EVENTS="${tmp:?}/ev.g"
_esc=$(sed -n 's/^escaped pid=//p' "${tmp:?}/ev.g" | head -n 1)
if [ "$rc" -eq 0 ] || [ -z "$_esc" ] || ! grep -qF "escaped pid $_esc" "${tmp:?}/out"; then
	cat "${tmp:?}/out" "${tmp:?}/ev.g"
	fail "(g) a helper that left the group: the check did not fail naming its pid (status $rc)"
fi
end_esc()
{
	if [ -f "${tmp:?}/esc.pids" ]; then
		while read -r _p; do
			end_own "$_p" sh
		done < "${tmp:?}/esc.pids"
		rm -f "${tmp:?}/esc.pids"
	fi
}
end_esc

# (h) (i) (j) the mutants: the check with one guard taken out, each refused
# by its own case. A mutant is a copy of the check in a tree that links
# the real src, scripts and tests/image.
# mutant <name> <sed script>: ${tmp}/<name>/tests/check-image-decoder.sh
mutant()
{
	mkdir -p "${tmp:?}/$1/tests"
	ln -s "$PWD/src" "${tmp:?}/$1/src"
	ln -s "$PWD/scripts" "${tmp:?}/$1/scripts"
	ln -s "$PWD/tests/image" "${tmp:?}/$1/tests/image"
	sed "$2" "$check" > "${tmp:?}/$1/tests/check-image-decoder.sh"
	if cmp -s "$check" "${tmp:?}/$1/tests/check-image-decoder.sh"; then
		fail "($3) the mutation did not change the check"
		return 1
	fi
}
real=$check

# (h) a plain kill of the run's own pid in place of the supervisor: the
# target leaks(1) started and stopped survives, which case (b) refuses
if mutant plainkill 's|^[[:space:]]*"\$sv" --timeout .*$|( "$@" \& _p=$!; ( sleep "$limit"; kill "$_p" 2>/dev/null ) \& wait "$_p" ) \|\| rc=$?|' h; then
	check=${tmp:?}/plainkill/tests/check-image-decoder.sh
	rm -f "${tmp:?}/stop.pids"
	run stop
	check=$real
	if gone "${tmp:?}/stop.pids" leakprobe; then
		fail "(h) a plain kill in place of the supervisor was not refused: the probe's stopped target is gone"
	fi
	end_own "$(cat "${tmp:?}/stop.pids" 2>/dev/null | head -n 1)" leakprobe
fi

# (i) --events dropped: the escaped helper is not named, which case (g) refuses
if mutant noevents 's| --events 9||' i; then
	check=${tmp:?}/noevents/tests/check-image-decoder.sh
	: > "${tmp:?}/ev.i"
	run escape IMGDEC_SELFTEST_EVENTS="${tmp:?}/ev.i"
	check=$real
	if [ -s "${tmp:?}/ev.i" ] || grep -qF 'escaped pid ' "${tmp:?}/out"; then
		fail "(i) --events dropped was not refused: the supervisor's events or the escaped pid still show"
	fi
	end_esc
fi

# (j) the escaped scan dropped: the events are there, the pid is not named
if mutant noscan "s|grep -q '^escaped pid='|grep -q '^escaped-not-scanned='|" j; then
	check=${tmp:?}/noscan/tests/check-image-decoder.sh
	: > "${tmp:?}/ev.j"
	run escape IMGDEC_SELFTEST_EVENTS="${tmp:?}/ev.j"
	check=$real
	if ! grep -q '^escaped pid=' "${tmp:?}/ev.j"; then
		fail "(j) the escape was not reported by the supervisor: the case proves nothing"
	elif grep -qF 'escaped pid ' "${tmp:?}/out"; then
		fail "(j) the escaped scan dropped was not refused: the pid is still named"
	fi
	end_esc
fi

# (k) the self-test interrupted in its own escape case: a TERM to the
# self-test after case (g)'s run, while the escaped helper still runs, leaves
# no helper behind. The helper holds the fifo ${tmp}/hold open; the reader
# sees end of file only once every holder has ended, so the case waits on
# that end, under an alarm, with no sleep and no ps poll.
mkfifo "${tmp:?}/hold"
perl -e '$| = 1; alarm shift; open(F, "<", shift) or exit 2; print while <F>; exit 0' \
	"$secs" "${tmp:?}/hold" > "${tmp:?}/k.pids" &
rdr=$!
krc=0
{ env IMGDEC_SELFTEST_INTERRUPT="${tmp:?}/hold" perl -e 'alarm shift; exec @ARGV' "$secs" \
	sh tests/check-image-decoder-selftest.sh > "${tmp:?}/k.out" 2>&1; } 2>/dev/null || krc=$?
rdrc=0
{ wait "$rdr"; } 2>/dev/null || rdrc=$?
if [ "$krc" -ne 143 ]; then
	cat "${tmp:?}/k.out"
	fail "(k) the interrupted self-test did not end by its TERM (status $krc)"
elif [ ! -s "${tmp:?}/k.pids" ]; then
	fail "(k) the escaped helper never opened the hold: the case proves nothing"
elif [ "$rdrc" -ne 0 ]; then
	fail "(k) an escaped helper outlived the interrupted self-test: pid $(tr '\n' ' ' < "${tmp:?}/k.pids")"
fi

if [ "$fails" -ne 0 ]; then
	echo "$me: $fails case(s) failed"
	exit 1
fi
echo "$me: ok (11 cases: a hang, a stopped probe target, a stopped leak-run target, a good leaks(1), a bad probe bound, the staged hang on events, an escaped helper, three mutants of the check, the self-test interrupted in its escape case)"
