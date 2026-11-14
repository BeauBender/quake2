#!/bin/sh
# Invariant: the pin harness's file-naming values come from the process
# command line only. pins_dir, pins_scenario, pins_capture, pins_record and pins_replay are latched by
# CL_PinsLatch (src/client/cl_pins.c) before any config file, console command
# or server stufftext runs, and are not cvars: no other file in src/ may name
# them, so no "set pins_dir ..." (with or without flags, before or after
# registration) can redirect a write. This builds tests/pins/latch-test.c
# against the real cl_pins.c and argproc.c and checks what gets latched.
# Needs a C compiler; needs no SDL, no engine and no game data.
set -eu
cd "$(dirname "$0")/.."

fail=0
bad() { echo "check-pins-latch: $*"; fail=1; }

# 1. Nothing outside cl_pins.c names the values, so nothing reads them as cvars.
others=$(grep -rlE '"pins_(dir|scenario|capture|record|replay)"' src | grep -v '^src/client/cl_pins.c$' || true)
[ -z "$others" ] || bad "pins_* named outside src/client/cl_pins.c (read the latched CL_Pins* values instead): $others"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/pinslatch.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cc -DYQ2OSTYPE='"test"' -DYQ2ARCH='"test"' -o "$tmp/latch" \
	tests/pins/latch-test.c src/client/cl_pins.c src/common/argproc.c

# expect <expected line> <args...>
expect() {
	want=$1; shift
	got=$("$tmp/latch" "$@" | sed -n 1p)
	[ "$got" = "$want" ] || bad "args '$*': want '$want', got '$got'"
}

# expect_run <0|1> <args...>: whether the args make a pin run (CL_PinsRun)
expect_run() {
	want=$1; shift
	got=$("$tmp/latch" "$@" | sed -n 2p)
	[ "$got" = "run=$want" ] || bad "args '$*': want 'run=$want', got '$got'"
}

off='dir=|scenario=pin|capture=0|invalid=0|record=|replay='
refused_pin='dir=|scenario=pin|capture=0|invalid=1|record=|replay='
expect "$off"
expect 'dir=/s/w/gl1/x|scenario=world|capture=30|invalid=0|record=|replay=' \
	+set pins_dir /s/w/gl1/x +set pins_scenario world +set pins_capture 30
expect 'dir=/s|scenario=rt-ao|capture=5|invalid=0|record=|replay=' \
	+set pins_dir /s +set pins_scenario rt-ao +set pins_capture 5
expect 'dir=/b|scenario=pin|capture=7|invalid=0|record=|replay=' \
	+set pins_dir /a +set pins_capture 7 +set pins_dir /b
# a capture interval without a store captures nothing
expect "$off" +set pins_capture 30
# refused values switch the harness off
expect "$refused_pin" +set pins_dir /s/../etc +set pins_capture 30
expect "$refused_pin" +set pins_dir '' +set pins_capture 30
long=$(printf '%01100d' 0)
expect "$refused_pin" +set pins_dir "/$long" +set pins_capture 30
expect "$refused_pin" +set pins_dir /s +set pins_scenario a/b +set pins_capture 30
expect "$refused_pin" +set pins_dir /s +set pins_scenario -x +set pins_capture 30
expect "$refused_pin" +set pins_dir /s +set pins_capture abc
expect "$refused_pin" +set pins_dir /s +set pins_capture -1
# strtol alone would skip a leading space and accept a plus sign
expect "$refused_pin" +set pins_dir /s +set pins_capture ' 30'
expect "$refused_pin" +set pins_dir /s +set pins_capture +30
expect "$refused_pin" +set pins_dir /s +set pins_capture '30 '
# pins_record names a file inside pins_dir: a plain file name, never a path
expect 'dir=/s|scenario=pin|capture=30|invalid=0|record=trace.q2r|replay=' \
	+set pins_dir /s +set pins_capture 30 +set pins_record trace.q2r
expect 'dir=/s|scenario=pin|capture=0|invalid=0|record=Trace_1-a.q2r|replay=' \
	+set pins_dir /s +set pins_record Trace_1-a.q2r
# a trace without a store records nothing
expect "$off" +set pins_record trace.q2r
# and ends in .q2r, so it can never name a pin frame or the manifest
for r in ../trace.q2r a..b /tmp/t.q2r a/b .hidden -x 'a b' 'a:b' 'a\\b' '' \
	trace pin-000000.tga manifest.txt trace.q2r.tga trace.Q2R .q2r trace_q2r; do
	expect "$refused_pin" +set pins_dir /s +set pins_capture 30 +set pins_record "$r"
done
expect "$refused_pin" +set pins_dir /s +set pins_record "$(printf '%060d' 0).q2r"
n59=$(printf '%059d' 0)
expect "dir=/s|scenario=pin|capture=0|invalid=0|record=$n59.q2r|replay=" \
	+set pins_dir /s +set pins_record "$n59.q2r"
expect 'dir=/s|scenario=pin|capture=0|invalid=0|record=a.q2r|replay=' \
	+set pins_dir /s +set pins_record a.q2r
# pins_replay names the trace a replay reads: a path ending in .q2r, never
# "..", never beside pins_record, and only with a store to write the frames to
# (refused, the harness is off and CL_Init stops the engine: "requested")
expect 'dir=/s|scenario=pin|capture=30|invalid=0|record=|replay=/t/world/soft/a-1/trace.q2r|requested' \
	+set pins_dir /s +set pins_capture 30 +set pins_replay /t/world/soft/a-1/trace.q2r
expect 'dir=/s|scenario=pin|capture=0|invalid=0|record=|replay=rel/t_1.q2r|requested' \
	+set pins_dir /s +set pins_replay rel/t_1.q2r
for r in /t/../trace.q2r /t/trace /t/trace.q2r.tga '' .q2r '/t/a b.q2r' '/t/a;b.q2r' '/t/a"b.q2r' \
	'C:\t.q2r' /t/.q2r; do
	expect "$refused_pin|requested" +set pins_dir /s +set pins_capture 30 +set pins_replay "$r"
done
expect "$refused_pin|requested" +set pins_replay /t/trace.q2r
expect "$refused_pin|requested" +set pins_dir /s +set pins_record trace.q2r +set pins_replay /t/trace.q2r
long=$(printf '%01000d' 0)
expect "dir=/s|scenario=pin|capture=0|invalid=0|record=|replay=/$long.q2r|requested" \
	+set pins_dir /s +set pins_replay "/$long.q2r"

# A pin run is a capture or a replay, also a replay without pins_capture
# and a refused one: config.cfg is never read or written, ERR_DROP is fatal
# and no renderer fallback is tried (frame.c, cl_main.c, clientserver.c, vid.c)
expect_run 0
expect_run 0 +set pins_capture 30
expect_run 0 +set pins_dir /s +set pins_record trace.q2r
expect_run 1 +set pins_dir /s +set pins_capture 30
expect_run 1 +set pins_dir /s +set pins_replay /t/trace.q2r
expect_run 1 +set pins_replay /t/trace.q2r
expect_run 1 +set pins_dir /s +set pins_replay /t/../trace.q2r
# and those guards test that predicate, not the capture interval alone
for g in 'src/common/frame.c:if (!CL_PinsRun())' \
	'src/client/cl_main.c:if ((cls.state == ca_uninitialized) || CL_PinsRun())' \
	'src/common/clientserver.c:if ((code == ERR_DROP) && CL_PinsRun())' \
	'src/client/vid/vid.c:if (CL_PinsRun())'; do
	f=${g%%:*}
	grep -qF "${g#*:}" "$f" || bad "$f: the pin-run guard does not test CL_PinsRun()"
done
n=$(grep -rn 'CL_PinsCapture() *\(<=\|>\) *0' src | grep -v '^src/client/cl_main.c:.*quitting' || true)
[ -z "$n" ] || bad "a pin-run guard tests CL_PinsCapture() alone (use CL_PinsRun()): $n"

# only the exact "+set name value" triple counts, paired as Cbuf_AddEarlyCommands pairs it
expect "$off" set pins_dir /s +pins_dir /s
expect "$off" +set foo +set pins_dir /s

# The "does not fit" refusal: values short enough to survive COM_InitArgv
# (under MAX_TOKEN_CHARS) that CL_PinsCopy itself must refuse. pins_scenario
# is a 64-byte buffer. pins_dir cannot be made too long here: MAX_OSPATH is
# 4096 outside Windows, above MAX_TOKEN_CHARS (1024), so COM_InitArgv blanks
# such a value first (the 1101-byte case above); the dir half of the guard
# only fires with the Windows MAX_OSPATH of 256.
s63=$(printf '%063d' 0 | tr 0 a)
s64=$(printf '%064d' 0 | tr 0 a)
expect "dir=/s|scenario=$s63|capture=30|invalid=0|record=|replay=" \
	+set pins_dir /s +set pins_scenario "$s63" +set pins_capture 30
expect "$refused_pin" +set pins_dir /s +set pins_scenario "$s64" +set pins_capture 30

# Every scenario's command line, as scripts/pins.sh record builds it, is
# accepted by the real COM_InitArgv (no argc > MAX_NUM_ARGVS) and latches
# the directory, scenario and interval it names.
unset PINS_BIN PINS_DEMO PINS_WIDTH PINS_HEIGHT PINS_EVERY PINS_TIMEOUT PINS_DIFF PINS_TRACE
for cfg in scripts/pins/scenarios/*.cfg; do
	scen=$(basename "$cfg" .cfg)
	demo=demo1.dm2
	[ "$scen" != cinematic ] || demo=idlog.cin # record pairs the cinematic scenario with a .cin
	line=$(PINS_DEMO=$demo PINS_DRYRUN=1 PINS_STORE=/s PINS_BASEDIR=/b bash scripts/pins.sh record gl1 "$scen")
	set -f
	# shellcheck disable=SC2086
	set -- $line
	set +f
	shift
	got=$("$tmp/latch" "$@") || bad "scenario $scen: the engine refuses its command line ($# arguments)"
	got=$(printf '%s\n' "$got" | sed -n 1p)
	case $got in
		"dir=/s/$scen/gl1/"*"|scenario=$scen|capture=30|invalid=0|record=trace.q2r|replay=") ;;
		*) bad "scenario $scen: latched '$got'" ;;
	esac
done

[ "$fail" -eq 0 ] && echo "check-pins-latch: ok"
exit "$fail"
