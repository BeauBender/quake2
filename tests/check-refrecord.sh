#!/bin/sh
# Invariant: the renderer-API record format (src/client/vid/refrecord.c,
# docs/plans/refapi-record-replay.md) round-trips every record type through
# a memory buffer field by field, keeps its little-endian byte layout, and
# refuses truncated, oversized-length and out-of-range input without reading
# past the end. Recording is off: RefRec_Recording() is false and the
# recorder entry points do nothing. This builds tests/refrecord/refrecord-test.c
# against the real refrecord.c, with ASAN and UBSAN when the compiler has
# them. Needs a C compiler; needs no SDL, no engine and no game data.
set -eu
cd "$(dirname "$0")/.."

fail=0
bad() { echo "check-refrecord: $*"; fail=1; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/refrecord.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

cc=${CC:-cc}
srcs="tests/refrecord/refrecord-test.c src/client/vid/refrecord.c"
flags="-std=gnu99 -Wall -Werror -g -O1 -fno-strict-aliasing -fwrapv -DYQ2OSTYPE=\"test\" -DYQ2ARCH=\"test\""
san="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"

# sanitizers when this compiler can build and run with them
printf 'int main(void){return 0;}\n' > "$tmp/probe.c"
if $cc $san -o "$tmp/probe" "$tmp/probe.c" >/dev/null 2>&1 && "$tmp/probe" >/dev/null 2>&1; then
	flags="$flags $san"
else
	echo "check-refrecord: $cc cannot build with ASAN and UBSAN; running without them"
fi

# shellcheck disable=SC2086 # flags and srcs are word lists
$cc $flags -o "$tmp/refrecord" $srcs -lm
if ! ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 "$tmp/refrecord" > "$tmp/out" 2>&1; then
	cat "$tmp/out"
	bad "tests/refrecord/refrecord-test.c failed"
fi

if [ "$fail" = 0 ]; then
	echo "check-refrecord: ok ($(tail -n 1 "$tmp/out" | sed 's/^refrecord: //'))"
fi
exit $fail
