#!/bin/sh
# Invariant (TASK167): the upscale_textures report generator
# (scripts/upscale-report.sh, scripts/upscale-report/report.py) works
# offline and keeps its report local. Against a fake engine (a script that
# writes a small PNG under the names a run writes, and for vt says the
# fallback line), with no game data and no network, it checks that:
#
# - a run of three backends writes one self-contained HTML report (the
#   images embedded, no script, no external URL) with every backend's
#   times, the fallback line and the matched crops;
# - an output inside a git work tree (a fresh repository, and this
#   checkout itself) is refused and nothing is made there, as is a path
#   with "..", a bad factor and a backend that does not exist;
# - the generator has no network or git code (it never uploads or
#   commits anything).

set -u

cd "$(dirname "$0")/.." || exit 1
root=$(pwd -P)

tmp=$(mktemp -d "${TMPDIR:-/tmp}/upscale-report.XXXXXX") || exit 1
trap 'rm -rf "${tmp:?}"' EXIT
tmp=$(cd "${tmp:?}" && pwd -P)
fail=0
bad() { echo "check-upscale-report: $*"; fail=1; }
gen="$root/scripts/upscale-report.sh"

if [ -n "$(cd "${tmp:?}" && git rev-parse --show-toplevel 2>/dev/null)" ]; then
	echo "check-upscale-report: skipped: the temporary directory is inside a git work tree"
	exit 0
fi

# a 4 x 4 RGBA PNG the fake engine copies
python3 -I -c '
import struct, sys, zlib
def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
raw = b"".join(b"\0" + bytes(sum(([40 * x, 60 * y, 90, 255] for x in range(4)), [])) for y in range(4))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
' "${tmp:?}/tile.png" || { echo "check-upscale-report: cannot make the test PNG"; exit 1; }

mkdir -p "${tmp:?}/eng" "${tmp:?}/data/baseq2" || exit 1
cat > "${tmp:?}/eng/quake2" <<'EOF'
#!/bin/sh
game=; up=; f=; b=
while [ $# -gt 0 ]; do
	case $1 in
		+set) [ "$2" = game ] && game=$3; shift 3 ;;
		+upscale_textures) f=$2; b=$3; up=1; shift 4 ;;
		*) shift ;;
	esac
done
[ -n "$up" ] || exit 0
d=$(cd "$(dirname "$0")" && pwd -P)
by=" with $b"
if [ "$b" = vt ]; then
	echo "upscale_textures: vt is unsupported here (the self-test's fallback); using scale2x"
	by=
fi
for n in textures/e1u1/floor1_3 env/unit1_ft models/monsters/soldier/skin textures/e2u1/x; do
	mkdir -p "$d/$game/${n%/*}" && cp "$FAKE_TILE" "$d/$game/$n.png"
done
echo "upscale_textures: 4 written, 0 skipped, 0 refused, 0 failed, 400 bytes, ${f}x$by, in $d/$game"
EOF
chmod +x "${tmp:?}/eng/quake2"
export FAKE_TILE="${tmp:?}/tile.png"

# 1. a run of every backend
out="${tmp:?}/out"
if ! sh "$gen" run --out "$out" --basedir "${tmp:?}/data" --engine-dir "${tmp:?}/eng" \
	--runs 3 --bound 60 > "${tmp:?}/run.log" 2>&1; then
	cat "${tmp:?}/run.log"
	bad "a run against the fake engine failed"
fi

rep="$out/upscale-report-2x.html"
if [ ! -f "$rep" ]; then
	bad "no report at $rep"
else
	for want in '<th>scale2x</th>' '<th>metalfx</th>' '<th>vt</th>' \
		'vt is unsupported here' 'data:image/png;base64,' '<td>start-up</td>' \
		'textures/e1u1/floor1_3.png' 'models/monsters/soldier/skin.png' 'Load average'; do
		grep -q -F "$want" "$rep" || bad "the report lacks: $want"
	done
	for no in 'http://' 'https://' '<script' '<link' 'src="'; do
		! grep -q -F "$no" "$rep" || bad "the report is not self-contained: $no"
	done
	[ "$(grep -c 'class="crop"' "$rep")" = 12 ] || bad "not 4 names by 3 backends of matched crops"
fi
grep -q 'scale2x: .* to .* s (median' "${tmp:?}/run.log" || bad "the run printed no time range"
ls "$out" | grep -q '\.html$' || bad "no html in the output"

# 2. refusals: nothing is made
mkdir -p "${tmp:?}/repo" && git -C "${tmp:?}/repo" init -q || exit 1
for o in "${tmp:?}/repo/sub/out" "$root/upscale-report-out" "${tmp:?}/a/../b"; do
	if sh "$gen" run --out "$o" --basedir "${tmp:?}/data" --engine-dir "${tmp:?}/eng" \
		--runs 1 > "${tmp:?}/ref.log" 2>&1; then
		bad "an output was not refused: $o"
	fi
	grep -q -e 'inside a git work tree' -e "with '..'" "${tmp:?}/ref.log" || bad "no refusal line for $o"
done
[ ! -e "${tmp:?}/repo/sub" ] || bad "something was made inside the work tree"
[ ! -e "$root/upscale-report-out" ] || bad "something was made inside this checkout"
sh "$gen" check-out "$root/x" > /dev/null 2>&1 && bad "check-out accepted this checkout"
sh "$gen" check-out "${tmp:?}/fine" > /dev/null 2>&1 || bad "check-out refused a path outside every work tree"

for args in "--factor 3" "--backends scale2x,bogus" "--runs 0"; do
	# shellcheck disable=SC2086
	if sh "$gen" run --out "${tmp:?}/o2" --basedir "${tmp:?}/data" \
		--engine-dir "${tmp:?}/eng" $args > /dev/null 2>&1; then
		bad "not refused: $args"
	fi
done

# 3. no network or git code in the generator
if grep -n -E 'urllib|http\.client|socket|requests|"git"|curl|scp|(^|[^a-z])gh ' \
	"$root/scripts/upscale-report/report.py" "$gen"; then
	bad "the generator has network or git code"
fi

if [ "$fail" != 0 ]; then
	exit 1
fi

echo "check-upscale-report: OK (a fake run of 3 backends, the refusals, no network or git code)"
