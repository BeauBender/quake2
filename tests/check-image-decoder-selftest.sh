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
# (f) the check's reap function, called directly: it signals nothing when
#     the PID it is given is not the caller's child (a PID the caller has
#     reaped and the system has reused), and of a stopped run's children it
#     kills only those of the named program.
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
# under a 120 s alarm; its output in ${tmp}/out, its status in $rc
run()
{
	_d=$1
	shift
	rc=0
	env PATH="${tmp:?}/$_d:$PATH" IMGDEC_CHECK_SECONDS=8 IMGDEC_LEAKS_PROBE_SECONDS=2 "$@" \
		perl -e 'alarm 120; exec @ARGV' sh "$check" > "${tmp:?}/out" 2>&1 || rc=$?
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

stub hang 'while :; do sleep 1; done'
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
printf '#include <unistd.h>\nint main(void){sleep(120);return 0;}\n' > "${tmp:?}/decoy.c"
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

# (f) the reap function alone, as the check defines it
sed -n '/^reap()$/,/^}$/p' "$check" > "${tmp:?}/reap.sh"
if ! grep -q '^reap()$' "${tmp:?}/reap.sh"; then
	fail "(f) $check defines no reap function"
else
	cat > "${tmp:?}/reap-probe.sh" << 'EOF'
set -eu
. "$1"
bin=$2
out=$3
# a PID that is not this shell's child: an orphan, as a reaped and reused PID would be
sh -c '"$1" & echo $! > "$2"' sh "$bin/decoy/imgdecode_plain" "$out/orphan.pid"
orphan=$(cat "$out/orphan.pid")
reap "$orphan" imgdecode_plain
kill -0 "$orphan" 2>/dev/null && echo "orphan: alive" || echo "orphan: killed"
kill -KILL "$orphan" 2>/dev/null || :
# a run that is this shell's child, with two children: the named program and another
sh -c '"$1" & "$2" & wait' sh "$bin/decoy/imgdecode_plain" "$bin/decoy/other" &
run=$!
n=0
while [ "$(ps -A -o ppid= | awk -v p="$run" '$1 == p' | wc -l)" -lt 2 ] && [ "$n" -lt 50 ]; do
	sleep 0.1
	n=$((n + 1))
done
named=$(ps -A -o pid= -o ppid= -o comm= | awk -v p="$run" '$2 == p && $3 ~ /\/imgdecode_plain$/ { print $1 }')
other=$(ps -A -o pid= -o ppid= -o comm= | awk -v p="$run" '$2 == p && $3 ~ /\/other$/ { print $1 }')
reap "$run" imgdecode_plain
wait "$run" 2>/dev/null || :
sleep 0.2
kill -0 "$run" 2>/dev/null && echo "run: alive" || echo "run: killed"
ps -o stat= -p "$named" > /dev/null 2>&1 && [ "$(ps -o stat= -p "$named" | cut -c1)" != Z ] && echo "named child: alive" || echo "named child: killed"
kill -0 "$other" 2>/dev/null && echo "other child: alive" || echo "other child: killed"
kill -KILL "$other" 2>/dev/null || :
EOF
	sh "${tmp:?}/reap-probe.sh" "${tmp:?}/reap.sh" "${tmp:?}" "${tmp:?}" > "${tmp:?}/reap.out" 2>&1 || :
	if [ "$(cat "${tmp:?}/reap.out")" != "orphan: alive
run: killed
named child: killed
other child: alive" ]; then
		cat "${tmp:?}/reap.out"
		fail "(f) reap signalled a process that is not the run's, or missed the run's named child"
	fi
fi

if [ "$fails" -ne 0 ]; then
	echo "$me: $fails case(s) failed"
	exit 1
fi
echo "$me: ok (6 cases: a hang, a stopped probe target, a stopped leak-run target, a good leaks(1), a bad probe bound, reap)"
