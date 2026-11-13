#!/bin/sh
# Invariant: the pin harness's file-naming values come from the process
# command line only. pins_dir, pins_scenario and pins_capture are latched by
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
others=$(grep -rlE '"pins_(dir|scenario|capture)"' src | grep -v '^src/client/cl_pins.c$' || true)
[ -z "$others" ] || bad "pins_* named outside src/client/cl_pins.c (read the latched CL_Pins* values instead): $others"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/pinslatch.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cc -DYQ2OSTYPE='"test"' -DYQ2ARCH='"test"' -o "$tmp/latch" \
	tests/pins/latch-test.c src/client/cl_pins.c src/common/argproc.c

# expect <expected line> <args...>
expect() {
	want=$1; shift
	got=$("$tmp/latch" "$@")
	[ "$got" = "$want" ] || bad "args '$*': want '$want', got '$got'"
}

off='dir=|scenario=pin|capture=0|invalid=0'
refused_pin='dir=|scenario=pin|capture=0|invalid=1'
expect "$off"
expect 'dir=/s/w/gl1/x|scenario=world|capture=30|invalid=0' \
	+set pins_dir /s/w/gl1/x +set pins_scenario world +set pins_capture 30
expect 'dir=/s|scenario=rt-ao|capture=5|invalid=0' \
	+set pins_dir /s +set pins_scenario rt-ao +set pins_capture 5
expect 'dir=/b|scenario=pin|capture=7|invalid=0' \
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
# only the exact "+set name value" triple counts, paired as Cbuf_AddEarlyCommands pairs it
expect "$off" set pins_dir /s +pins_dir /s
expect "$off" +set foo +set pins_dir /s

[ "$fail" -eq 0 ] && echo "check-pins-latch: ok"
exit "$fail"
