#!/bin/bash
# tests/check-pin-chars.sh -- invariant (FIX157): a pin run does not depend on
# console notify text, and a trace whose captured demo frame draws characters
# nobody declared is refused. Checks that:
#   - the record command line passes con_notifytime -1 (so no notify line is
#     drawn, whatever the engine prints while the demo plays, FIX155);
#   - pins.sh check-text, over small traces made by tests/refrecord/charscan.c
#     with the real recorder, fails on text in a captured demo frame (312
#     characters, the size of FIX155's Retro report line) and passes on the
#     same text in a demo frame no pin captures, and on a trace with none;
#   - (a demo with no table in scripts/pins/text/ declares no text, here synth.dm2;)
#   - it refuses a file that is not a trace, and half of a real one, with its own message, for a demo with a table and for demos without one.
# Needs a C compiler; needs no engine and no game data.
set -eu
cd "$(dirname "$0")/.."

fail=0
bad() { echo "check-pin-chars: $*"; fail=1; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/pinchars.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT
export PINS_STORE="$tmp/store" BENCH_LOCK=0

line=$(PINS_DRYRUN=1 /bin/bash scripts/pins.sh record gl1 world) || bad "the record dry run failed"
case $line in
	*" +set con_notifytime -1 "*) ;;
	*) bad "the record command line must pass +set con_notifytime -1: $line" ;;
esac

mkdir -p "$tmp/store/bin"
cc -std=gnu99 -Wall -Werror -O1 -DYQ2OSTYPE='"test"' -DYQ2ARCH='"test"' -o "$tmp/make" \
	tests/refrecord/charscan.c src/client/vid/refrecord.c -lm || bad "charscan does not build"

# --make captures every 3rd demo frame from the first (capture numbers 0, 1, ...): frame 6 is capture 2, frame 4 is not captured
"$tmp/make" --make "$tmp/stray.q2r" 10 312 6 > /dev/null 2>&1
"$tmp/make" --make "$tmp/quiet-frame.q2r" 10 312 4 > /dev/null 2>&1
"$tmp/make" --make "$tmp/none.q2r" 10 0 0 > /dev/null 2>&1
printf 'not a trace\n' > "$tmp/junk.q2r"
# a trace the recorder cut short (REFREC_FLAG_TRUNCATED, header byte 16) is refused, as replay refuses it
cp "$tmp/none.q2r" "$tmp/trunc.q2r"; printf '\001' | dd of="$tmp/trunc.q2r" bs=1 seek=16 count=1 conv=notrunc 2>/dev/null

if /bin/bash scripts/pins.sh check-text "$tmp/stray.q2r" synth.dm2 > "$tmp/out" 2>&1; then
	bad "text in a captured demo frame was accepted"
else
	grep -q '^< pin 2 312$' "$tmp/out" || { cat "$tmp/out"; bad "the refusal does not name the frame (pin 2, 312 characters)"; }
fi
/bin/bash scripts/pins.sh check-text "$tmp/quiet-frame.q2r" synth.dm2 > "$tmp/out" 2>&1 || { cat "$tmp/out"; bad "text in a demo frame that no pin captures was refused"; }
/bin/bash scripts/pins.sh check-text "$tmp/none.q2r" synth.dm2 > "$tmp/out" 2>&1 || { cat "$tmp/out"; bad "a trace with no text was refused"; }
# a trace the scanner cannot read is refused with its own message, for a demo
# with a table (demo1) and for demos without one (synth, water): half of a
# real trace is an unfinished one
head -c "$(( $(wc -c < "$tmp/stray.q2r") / 2 ))" "$tmp/stray.q2r" > "$tmp/half.q2r"
for t in junk half; do
	for d in synth.dm2 water.dm2 demo1.dm2; do
		if /bin/bash scripts/pins.sh check-text "$tmp/$t.q2r" "$d" > "$tmp/out" 2>&1; then
			bad "$t.q2r was accepted with $d"
		elif ! grep -q 'charscan could not read the trace' "$tmp/out"; then
			cat "$tmp/out"; bad "$t.q2r with $d was refused, but not for being unreadable"
		fi
	done
done

for d in synth.dm2 demo1.dm2; do
	if /bin/bash scripts/pins.sh check-text "$tmp/trunc.q2r" "$d" > "$tmp/out" 2>&1; then
		bad "a truncated trace was accepted with $d"
	elif ! grep -q 'recorder stopped at its limit' "$tmp/out"; then
		cat "$tmp/out"; bad "a truncated trace with $d was refused, but not for the recorder's limit"
	fi
done

# the declared tables hold only "pin <number> <number>" lines and comments
for f in scripts/pins/text/*.txt; do
	[ -f "$f" ] || continue
	if grep -vE '^(//.*|pin [0-9]+ [0-9]+)$' "$f" > /dev/null; then
		bad "$f: a line that is neither a comment nor 'pin <capture> <characters>'"
	fi
done

if [ "$fail" = 0 ]; then
	echo "check-pin-chars: ok"
fi
exit "$fail"
