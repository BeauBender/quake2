#!/bin/sh
# Invariant (FIX136): every <NAME>_REQUIRE variable that CI or the gate sets
# turns its check's skip into a failure, as the check runs.
#
# tests/check-ci-parity.sh checks the shape of each guard; this check runs
# it. The variables are read at run time from the two files that set them:
# the NAME_REQUIRE keys of .github/workflows/macos.yml and the
# NAME_REQUIRE= assignments of scripts/test.sh, outside comments. For each
# one, a case below forces the check's skip condition with a stub ahead of
# PATH or a stripped PATH (uname printing Linux, an xcrun that refuses one
# SDK or the Metal compiler, a git whose cat-file fails, a PATH without
# python3 or bash), runs the real check with the variable set and wants a
# non-zero exit and a line naming the variable, then runs it with the
# variable unset and wants exit 0. Every other NAME_REQUIRE of the caller is
# unset first, so the gate's own CHECK_REF_METAL_REQUIRE=1 reaches no case.
#
# It fails when a variable CI or the gate sets has no case, when a case
# names a variable neither sets, and when the lines of a check that read a
# set variable (an expansion of it, outside comments) are more or fewer than
# the cases and the unforced list below account for, so a new guard needs a
# case too. A guard that cannot be forced cheaply is on the unforced list
# with its reason; an entry whose variable its check no longer reads fails.
#
# Then every case is run against mutants, each on a scratch tree (every
# top-level entry of the tree linked, tests/ a directory of links with the
# one check under test a changed copy; the real files are never changed),
# and each must let the forced skip pass with the variable set, which is
# what the case refuses:
#  - per case, its guards turned back into a guard that only prints
#    (round 1 of FIX115);
#  - check-bench.sh's t_freq guard in round 1's shape: "|| bad ..." and
#    then the skip's "return 0" (bad only counts; a sub-test fails by its
#    status);
#  - check-evidence.sh's guard with inverted polarity ("= 1 ] || exit 1"),
#    which fails only when the variable is unset;
#  - check-autorelease-pool.sh's guard replaced by a one-line
#    "if [ ... ]; then exit 1; fi" on the line after the skip's message;
#  - check-metal-shim.sh's "|| bad" inside a piped while loop, whose
#    fail=1 is lost with the loop's subshell.
#
# Every run is bounded by perl's alarm (REQUIRE_GUARDS_WALL seconds, 600 by
# default); a run that outlives it fails the check by name.
set -eu
cd "$(dirname "$0")/.."
root=$(pwd -P)
me=check-require-guards
export LC_ALL=C

for _v in $(env | sed -n 's/^\([A-Za-z0-9_]*_REQUIRE\)=.*/\1/p'); do
	unset "$_v"
done

wall=${REQUIRE_GUARDS_WALL:-600}
case $wall in
'' | *[!0-9]*) echo "$me: REQUIRE_GUARDS_WALL is a number of seconds" >&2; exit 2 ;;
esac

tmp=$(mktemp -d "${TMPDIR:-/tmp}/require-guards.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT
trap 'exit 1' HUP INT TERM

fail=0
bad() { echo "$me: $*"; fail=1; }
t0=$(date +%s)

# ---- The cases. ---------------------------------------------------------
# <variable>|<check>|<force>|<guard lines it proves>|<arguments>
printf '%s\n' \
	'CHECK_IOS_REQUIRE|check-ios.sh|linux|1|' \
	'CHECK_IOS_REQUIRE|check-ios.sh|nosim|1|' \
	'CHECK_EVIDENCE_REQUIRE|check-evidence.sh|nopython|1|' \
	'CHECK_AUTORELEASE_POOL_REQUIRE|check-autorelease-pool.sh|linux|1|' \
	'CHECK_STATIC_MODULES_REQUIRE|check-static-modules.sh|linux|1|' \
	'CHECK_BENCH_REQUIRE|check-bench.sh|linux|2|t_freq t_probe_first' \
	'CHECK_BENCH_REQUIRE|check-bench.sh|nobash|1|t_snap_argv' \
	'CHECK_CVAR_DOCS_SELFTEST_REQUIRE|check-cvar-docs-selftest.sh|nocatfile|1|' \
	'CHECK_REF_METAL_REQUIRE|check-ref-metal.sh|linux|1|' \
	'CHECK_METAL_SHIM_REQUIRE|check-metal-shim.sh|nometal|1|' \
	> "${tmp:?}/cases"

# The guards not forced here: <check>|<variable>|<reason>. Each counts as one
# guard line of its check.
printf '%s\n' \
	'check-ios.sh|CHECK_IOS_REQUIRE|the device parts: the guard is reached only after the simulator build of the client, game and ref_metal objects, minutes of compiling (forced once in docs/runbook/FIX115.md)' \
	'check-ref-metal.sh|CHECK_REF_METAL_DEVICE_REQUIRE|the colour-space probe without a Metal device needs a host that has none; neither CI nor the gate sets it' \
	'check-upscale-metalfx.sh|UPSCALE_METALFX_REQUIRE|the no-Metal-device guard needs a host that has none; neither CI nor the gate sets it' \
	'check-workflow-pins.sh|CHECK_WORKFLOW_PINS_REQUIRE|the self-test case of a mode 000 file needs root; neither CI nor the gate sets it' \
	> "${tmp:?}/unforced"

# ---- The variables CI and the gate set. ----------------------------------
wf=.github/workflows/macos.yml
for _f in "$wf" scripts/test.sh; do
	[ -f "$_f" ] || { echo "$me: $_f is missing"; exit 1; }
done
{
	grep -v '^[[:space:]]*#' "$wf" | grep -o '[A-Z][A-Z0-9_]*_REQUIRE[[:space:]]*:' | sed 's/[[:space:]]*:$//' || :
	grep -v '^[[:space:]]*#' scripts/test.sh | grep -o '[A-Z][A-Z0-9_]*_REQUIRE=' | sed 's/=$//' || :
} | sort -u > "${tmp:?}/vars"
[ -s "${tmp:?}/vars" ] || { echo "$me: no NAME_REQUIRE variable found in $wf or scripts/test.sh"; exit 1; }

# The lines of a check that read a variable: an expansion, outside comments.
reads() { # reads <check> <variable>
	grep -v '^[[:space:]]*#' "tests/$1" | grep -c "\${$2[}:]" || :
}

while IFS='|' read -r _v _c _f _g _a; do
	case $_v in '' | *[!A-Z0-9_]*) bad "case table: bad variable '$_v'"; continue ;; esac
	grep -qx "$_v" "${tmp:?}/vars" || bad "a case names $_v, which neither $wf nor scripts/test.sh sets"
done < "${tmp:?}/cases"
while IFS='|' read -r _c _v _r; do
	[ -n "$_r" ] || bad "unforced list: $_c $_v has no reason"
	[ -f "tests/$_c" ] || { bad "unforced list: tests/$_c does not exist"; continue; }
	[ "$(reads "$_c" "$_v")" -ge 1 ] || bad "unforced list: tests/$_c no longer reads $_v (stale entry)"
done < "${tmp:?}/unforced"

while read -r _v; do
	awk -F'|' -v v="$_v" '$1 == v { n++ } END { exit !n }' "${tmp:?}/cases" ||
		bad "$_v is set by $wf or scripts/test.sh and has no case here"
	for _p in tests/check-*.sh; do
		_c=${_p#tests/}
		[ "$_c" != "$me.sh" ] || continue
		_n=$(reads "$_c" "$_v")
		_e=$(awk -F'|' -v v="$_v" -v c="$_c" '$1 == v && $2 == c { n += $4 } END { print n + 0 }' "${tmp:?}/cases")
		_u=$(awk -F'|' -v v="$_v" -v c="$_c" '$1 == c && $2 == v { n++ } END { print n + 0 }' "${tmp:?}/unforced")
		[ "$_n" -eq $((_e + _u)) ] ||
			bad "tests/$_c reads $_v on $_n lines; the cases prove $_e and the unforced list holds $_u"
	done
done < "${tmp:?}/vars"

# ---- The stubs. ---------------------------------------------------------
real_uname=$(command -v uname) || real_uname=
real_xcrun=$(command -v xcrun) || real_xcrun=
real_git=$(command -v git) || real_git=
[ -n "$real_uname" ] && [ -n "$real_xcrun" ] && [ -n "$real_git" ] ||
	{ echo "$me: uname, xcrun and git are needed to force the cases (macOS with Xcode)"; exit 1; }
stub() { # stub <dir> <name> <word it refuses> <real tool>
	mkdir -p "${1:?}"
	printf '#!/bin/sh\nfor a; do [ "$a" != %s ] || exit 1; done\nexec '\''%s'\'' "$@"\n' "$3" "$4" > "${1:?}/$2"
	chmod +x "${1:?}/$2"
}
mkdir "${tmp:?}/bin-linux"
printf '#!/bin/sh\ncase ${1:-} in "" | -s) echo Linux; exit 0 ;; esac\nexec '\''%s'\'' "$@"\n' "$real_uname" > "${tmp:?}/bin-linux/uname"
chmod +x "${tmp:?}/bin-linux/uname"
stub "${tmp:?}/bin-nosim" xcrun iphonesimulator "$real_xcrun"
stub "${tmp:?}/bin-nometal" xcrun metal "$real_xcrun"
stub "${tmp:?}/bin-nocatfile" git cat-file "$real_git"
# A PATH of /usr/bin and /bin without one tool (and /usr/sbin, /sbin).
stripped() { # stripped <dir> <tool>
	mkdir "${1:?}"
	ln -s /usr/bin/* "${1:?}/"
	ln -sf /bin/* "${1:?}/"
	rm -f "${1:?}/$2" "${1:?}/$2".*
}
stripped "${tmp:?}/path-nopython" python3
stripped "${tmp:?}/path-nobash" bash

# run <log> <tree> <check> <force> <variable or -> [<argument>...]: the
# check's status, 142 when it outlived the wall bound.
n=0
run() {
	_log=$1 _tree=$2 _chk=$3 _force=$4 _var=$5
	shift 5
	case $_force in
	linux | nosim | nometal | nocatfile) _path=${tmp:?}/bin-$_force:$PATH ;;
	nopython | nobash) _path=${tmp:?}/path-$_force:/usr/sbin:/sbin ;;
	*) echo "$me: unknown force '$_force'" > "$_log"; return 99 ;;
	esac
	n=$((n + 1))
	mkdir "${tmp:?}/t.$n"
	_st=0
	(
		PATH=$_path
		TMPDIR=${tmp:?}/t.$n
		export PATH TMPDIR
		if [ "$_var" != - ]; then
			export "$_var=1"
		fi
		exec perl -e 'alarm shift @ARGV; exec @ARGV or exit 127' "$wall" "$_tree/tests/$_chk" "$@"
	) > "$_log" 2>&1 < /dev/null || _st=$?
	rm -rf "${tmp:?}/t.$n"
	return "$_st"
}

# ---- The real checks. ---------------------------------------------------
ncase=0
while IFS='|' read -r _v _c _f _g _a; do
	ncase=$((ncase + 1))
	_l=${tmp:?}/case.$ncase
	# shellcheck disable=SC2086 # the arguments are test names
	_st=0; run "$_l.set" "$root" "$_c" "$_f" "$_v" $_a || _st=$?
	if [ "$_st" -eq 142 ]; then
		bad "$_c ($_f, $_v=1) outlived ${wall}s"
	elif [ "$_st" -eq 0 ]; then
		tail -n 5 "$_l.set"
		bad "$_c ($_f, $_v=1) passed: the guard does not fail"
	elif ! grep -q "$_v" "$_l.set"; then
		tail -n 5 "$_l.set"
		bad "$_c ($_f, $_v=1) failed with status $_st, but no line names $_v"
	fi
	# shellcheck disable=SC2086
	_st=0; run "$_l.unset" "$root" "$_c" "$_f" - $_a || _st=$?
	if [ "$_st" -ne 0 ]; then
		tail -n 5 "$_l.unset"
		bad "$_c ($_f, $_v unset) failed with status $_st; it should pass"
	fi
done < "${tmp:?}/cases"

# ---- Mutants. -----------------------------------------------------------
# tree <dir> <check>: a scratch tree whose tests/<check> is a copy, at
# <dir>/tests/<check>.orig, for the caller to write the mutant from.
tree() {
	mkdir "${1:?}" "${1:?}/tests"
	for _e in "$root"/* "$root"/.[!.]*; do
		[ -e "$_e" ] || [ -L "$_e" ] || continue
		case ${_e##*/} in tests | .git) continue ;; esac
		ln -s "$_e" "${1:?}/${_e##*/}"
	done
	for _e in "$root"/tests/* "$root"/tests/.[!.]*; do
		[ -e "$_e" ] || [ -L "$_e" ] || continue
		[ "${_e##*/}" != "$2" ] || continue
		ln -s "$_e" "${1:?}/tests/${_e##*/}"
	done
	cp "$root/tests/$2" "${1:?}/tests/$2.orig"
}
# mutated <dir> <check>: the mutant written, executable, and not the
# original.
mutated() {
	chmod +x "${1:?}/tests/$2"
	if cmp -s "${1:?}/tests/$2" "${1:?}/tests/$2.orig"; then
		return 1
	fi
}
nmut=0
# refused <name> <dir> <check> <force> <variable> [<argument>...]: the case
# refuses the mutant when its forced skip passes with the variable set.
refused() {
	_mn=$1 _md=$2
	shift 2
	nmut=$((nmut + 1))
	_st=0; run "$_md.log" "$_md" "$@" || _st=$?
	if [ "$_st" -ne 0 ]; then
		tail -n 5 "$_md.log"
		bad "mutant $_mn: the forced run failed (status $_st), so the case would accept it"
	fi
}

ncase=0
while IFS='|' read -r _v _c _f _g _a; do
	ncase=$((ncase + 1))
	_d=${tmp:?}/m.print.$ncase
	tree "$_d" "$_c"
	sed "s/\(\${$_v:-0}\" != 1 ] ||\).*/\1 echo \"$_v=1 (prints only)\"/" "$_d/tests/$_c.orig" > "$_d/tests/$_c"
	if ! mutated "$_d" "$_c"; then
		bad "mutant print-only ($_c, $_v): no guard of the shape [ \"\${$_v:-0}\" != 1 ] || ... to change"
		continue
	fi
	# shellcheck disable=SC2086
	refused "print-only ($_c, $_f)" "$_d" "$_c" "$_f" "$_v" $_a
done < "${tmp:?}/cases"

_d=${tmp:?}/m.bench-round1
tree "$_d" check-bench.sh
sed '/t_freq: CHECK_BENCH_REQUIRE=1/s/|| { bad \("[^"]*"\); return 1; }$/|| bad \1/' "$_d/tests/check-bench.sh.orig" > "$_d/tests/check-bench.sh"
if mutated "$_d" check-bench.sh; then
	refused "bench-round1" "$_d" check-bench.sh linux CHECK_BENCH_REQUIRE t_freq
else
	bad "mutant bench-round1 does not apply to check-bench.sh"
fi

_d=${tmp:?}/m.inverted
tree "$_d" check-evidence.sh
sed 's/\(\${CHECK_EVIDENCE_REQUIRE:-0}"\) != 1 ]/\1 = 1 ]/' "$_d/tests/check-evidence.sh.orig" > "$_d/tests/check-evidence.sh"
if mutated "$_d" check-evidence.sh; then
	refused "inverted" "$_d" check-evidence.sh nopython CHECK_EVIDENCE_REQUIRE
else
	bad "mutant inverted does not apply to check-evidence.sh"
fi

_d=${tmp:?}/m.next-line-if
tree "$_d" check-autorelease-pool.sh
awk -v ins='	if [ "${CHECK_AUTORELEASE_POOL_REQUIRE:-0}" = yes ]; then exit 1; fi' '
	index($0, "${CHECK_AUTORELEASE_POOL_REQUIRE:-0}") { after = 1; next }
	{ print }
	after { print ins; after = 0 }
' "$_d/tests/check-autorelease-pool.sh.orig" > "$_d/tests/check-autorelease-pool.sh"
if mutated "$_d" check-autorelease-pool.sh; then
	refused "next-line-if" "$_d" check-autorelease-pool.sh linux CHECK_AUTORELEASE_POOL_REQUIRE
else
	bad "mutant next-line-if does not apply to check-autorelease-pool.sh"
fi

_d=${tmp:?}/m.piped-while
tree "$_d" check-metal-shim.sh
sed '/\${CHECK_METAL_SHIM_REQUIRE:-0}" != 1 ] || bad /s/|| bad \(.*\)$/|| echo x | while read -r _l; do bad \1; done/' "$_d/tests/check-metal-shim.sh.orig" > "$_d/tests/check-metal-shim.sh"
if mutated "$_d" check-metal-shim.sh; then
	refused "piped-while" "$_d" check-metal-shim.sh nometal CHECK_METAL_SHIM_REQUIRE
else
	bad "mutant piped-while does not apply to check-metal-shim.sh"
fi

t1=$(date +%s)
nvar=$(wc -l < "${tmp:?}/vars" | tr -d ' ')
nun=$(wc -l < "${tmp:?}/unforced" | tr -d ' ')
if [ "$fail" -ne 0 ]; then
	echo "$me: FAILED"
	exit 1
fi
echo "$me: ok ($nvar variables, $ncase cases, $nun unforced guards, $nmut mutants refused, $((t1 - t0))s)"
