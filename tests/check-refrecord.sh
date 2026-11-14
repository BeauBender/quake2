#!/bin/sh
# Invariant: the renderer-API record format (src/client/vid/refrecord.c,
# docs/plans/refapi-record-replay.md) round-trips every record type through
# a memory buffer field by field, keeps its little-endian byte layout, and
# refuses truncated, oversized-length and out-of-range input without reading
# past the end; the recorder writes trace files (handle map, refdef_t deep
# copy, string table, frame index, header patch) that read back whole, and
# ends cleanly on a recorder limit, a write error and oversized input, also
# when the limit or the error lands inside a call (swept over every record
# of runs that warn); the replayer feeds recorded traces to a mock renderer
# in order, ids resolved to live handles and every value bounded, and
# refuses truncated, patched and byte-swept traces cleanly. This builds
# tests/refrecord/refrecord-test.c against the real refrecord.c, with ASAN
# and UBSAN when the compiler has them. Also: every vid.c wrapper that
# forwards to the renderer carries its guarded hook. Needs a C compiler;
# needs no SDL, no engine and no game data.
set -eu
cd "$(dirname "$0")/.."

fail=0
bad() { echo "check-refrecord: $*"; fail=1; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/refrecord.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

cc=${CC:-cc}
srcs="tests/refrecord/refrecord-test.c src/client/vid/refrecord.c"
flags="-std=gnu99 -Wall -Werror -g -O1 -fno-strict-aliasing -fwrapv -DREFREC_TEST_WRITE_FAULT -DYQ2OSTYPE=\"test\" -DYQ2ARCH=\"test\""
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
mkdir "$tmp/traces"
if ! ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 "$tmp/refrecord" "$tmp/traces" > "$tmp/out" 2>&1; then
	cat "$tmp/out"
	bad "tests/refrecord/refrecord-test.c failed"
fi

# Every wrapper that forwards to the renderer is hooked once, behind the
# RefRec_Recording() guard. Count the guards inside each wrapper body.
vid=src/client/vid/vid.c
for fn in VID_LoadRenderer VID_ShutdownRenderer R_BeginRegistration R_RegisterModel \
	R_RegisterSkin R_SetSky R_EndRegistration R_RenderFrame Draw_FindPic \
	Draw_GetPicSize Draw_StretchPic Draw_PicScaled Draw_PicScaledCol \
	Draw_CharScaled Draw_TileClear Draw_Fill Draw_FadeScreen Draw_StretchRaw \
	R_SetPalette R_BeginFrame R_EndWorldRenderpass R_EndFrame VID_WritePinFrame; do
	n=$(awk -v fn="$fn" '
		$0 ~ "^" fn "\\(" { body = 1; next }
		body && /^}/ { body = 0 }
		body && /RefRec_Recording\(\)/ { n++ }
		END { print n + 0 }' "$vid")
	[ "$n" = 1 ] || bad "$vid: $fn has $n RefRec_Recording() hooks, expected 1"
done
# The cvar snapshot and its restore are wired: VID_LoadRenderer takes the
# snapshot (VID_RecordCvars hands every cvar to RefRec_Cvars) and
# VID_OpenReplay gives the replay target both cvar functions before it is
# set. vid.o is linked into the client only, so no other check runs this.
body() {
	awk -v fn="$1" '
		$0 ~ "^" fn "\\(" { body = 1; next }
		body && /^}/ { body = 0 }
		body { print }' "$vid"
}
body VID_LoadRenderer | grep -q '^[[:space:]]*VID_RecordCvars();' ||
	bad "$vid: VID_LoadRenderer does not call VID_RecordCvars()"
body VID_RecordCvars | grep -q '^[[:space:]]*RefRec_Cvars(' ||
	bad "$vid: VID_RecordCvars does not hand the cvars to RefRec_Cvars"
for a in 't.CvarFlags = VID_ReplayCvarFlags;' 't.SetCvar = VID_ReplaySetCvar;'; do
	n=$(body VID_OpenReplay | awk -v a="$a" '
		{ l = $0; sub(/^[ \t]+/, "", l) }
		l == a && !set { at = NR }
		/RefRec_SetReplayTarget\(&t\)/ && !set { set = NR }
		END { print (at && set && at < set) ? 1 : 0 }')
	[ "$n" = 1 ] || bad "$vid: VID_OpenReplay does not assign '$a' before RefRec_SetReplayTarget"
done

# the window plumbing is not recorded
n=$(awk '
	/^R_IsVSyncActive\(/ { body = 1; next }
	body && /^}/ { body = 0 }
	body && /RefRec_/ { n++ }
	END { print n + 0 }' "$vid")
[ "$n" = 0 ] || bad "$vid: R_IsVSyncActive is hooked; GLimp-path members are not recorded"

# and nothing else under src/client may call into the recorder: the other
# GLimp-path members (PrepareForWindow, InitContext, GetDrawableSize,
# ShutdownContext) are not recorded either
others=$(grep -rl 'RefRec_' src/client | grep -v -e '^src/client/vid/vid\.c$' \
	-e '^src/client/vid/refrecord\.c$' -e '^src/client/vid/header/refrecord\.h$' || true)
[ -z "$others" ] || bad "RefRec_ is used outside vid.c and refrecord.[ch]: $others"

if [ "$fail" = 0 ]; then
	echo "check-refrecord: ok ($(tail -n 1 "$tmp/out" | sed 's/^refrecord: //'))"
fi
exit $fail
