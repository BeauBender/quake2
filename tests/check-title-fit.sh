#!/bin/sh
# Invariant: the longest window title a renderer formats fits its buffer.
#
# Each renderer builds its window title with
#   snprintf(title, sizeof(title), <format with one %s>, YQ2VERSION);
# into a fixed `char title[N]`. A longer DISPLAYNAME or YQ2VERSION would be
# cut off silently. This extracts N and the format from the source, builds
# the title as a string literal from DISPLAYNAME and YQ2VERSION and compiles
# a static assertion that it (with its NUL) fits in N bytes. A self-test
# then asserts the same check fails for a 40-byte buffer, so the check
# cannot pass vacuously.
#
# Usage: tests/check-title-fit.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc=${CC:-cc}

# try SIZE LITERAL: succeeds when sizeof(LITERAL) <= SIZE
try() {
	{
		echo '#include "common.h"'
		echo "_Static_assert(sizeof($2) <= $1, \"window title does not fit\");"
	} >"$tmp/t.c"
	$cc -fsyntax-only -DYQ2OSTYPE='"x"' -DYQ2ARCH='"x"' -Isrc/common/header "$tmp/t.c" 2>"$tmp/err"
}

fail=0
for f in src/client/refresh/gl1/gl1_sdl.c src/client/refresh/gl3/gl3_sdl.c \
	src/client/refresh/soft/sw_main.c; do
	size=$(sed -n 's/^[[:space:]]*char title\[\([0-9][0-9]*\)\].*/\1/p' "$f")
	fmt=$(sed -n 's/.*snprintf(title, sizeof(title), \(.*\), YQ2VERSION);.*/\1/p' "$f")
	if [ -z "$size" ] || [ -z "$fmt" ]; then
		echo "check-title-fit: $f: cannot find the title buffer or its snprintf"
		fail=1
		continue
	fi
	lit=$(printf '%s' "$fmt" | sed 's/%s/" YQ2VERSION "/')
	if ! try "$size" "$lit"; then
		echo "check-title-fit: $f: title ($lit) does not fit char title[$size]"
		sed 's/^/  /' "$tmp/err"
		fail=1
	fi
done

# Self-test: the longest title must not fit 40 bytes, or the check is blind.
if try 40 'DISPLAYNAME " " YQ2VERSION " - Soft Render"' || ! grep -q "does not fit" "$tmp/err"; then
	echo "check-title-fit: self-test failed: a 40-byte buffer was accepted or the compile broke"
	fail=1
fi

exit $fail
