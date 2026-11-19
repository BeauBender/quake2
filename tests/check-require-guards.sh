#!/bin/sh
# Invariant (FIX136): every <NAME>_REQUIRE variable that CI or the gate sets
# turns its check's skip into a failure, as the check runs.
#
# tests/check-ci-parity.sh checks the shape of each guard; this check runs
# it. The variables are read at run time from the two files that set them:
# every env map (workflow, job and step) of .github/workflows/macos.yml and
# every NAME_REQUIRE= prefix of a step's run, read by ruby's yaml as
# tests/check-ci-parity.sh reads the file (no ruby is a failure), and the
# assignments of scripts/test.sh outside comments (NAME_REQUIRE=..., also
# after export, and : "${NAME_REQUIRE:=...}"); a _REQUIRE token there that
# is neither an assignment, an expansion nor a name in an export list fails.
#
# For each variable, a case below forces the check's skip condition with a
# stub ahead of PATH or a stripped PATH (uname printing Linux, an xcrun that
# refuses one SDK or the Metal compiler, a git whose plain cat-file fails, a
# PATH without python3 or bash), runs the real check with the variable set
# and wants a non-zero exit and the guard's own message ("<test>: NAME=1
# and" for a case that names a test, "NAME=1 and" otherwise), then runs it
# with the variable unset and wants exit 0. One run proves one guard: a case
# that claims another number of guard lines, or names more than one test,
# fails. Every other NAME_REQUIRE of the caller is unset first, so the
# gate's own CHECK_REF_METAL_REQUIRE=1 reaches no case.
#
# It fails when a variable CI or the gate sets has no case, when a case
# names a variable neither sets, and when the lines of a check that read a
# set variable (an expansion of it, $NAME or ${NAME...}, outside comments)
# are more or fewer than the cases and the unforced list below account for,
# so a new guard needs a case too. A guard that cannot be forced cheaply is
# on the unforced list with its reason; an entry whose variable its check no
# longer reads fails.
#
# Then every case is run against mutants, each on a scratch tree (every
# top-level entry of the tree linked, tests/ a directory of links with the
# one check under test a changed copy; the real files are never changed).
# In each tree the unmutated copy runs first as a control, which the case
# must accept, so a tree that fails early cannot pass for a refusal; then
# the case must refuse the mutant:
#  - per case, its guard turned back into a guard that only prints
#    (round 1 of FIX115);
#  - check-bench.sh's t_probe_first guard alone print-only, which a case
#    that also ran t_freq accepted;
#  - check-bench.sh's t_freq guard in round 1's shape: "|| bad ..." and
#    then the skip's "return 0" (bad only counts; a sub-test fails by its
#    status);
#  - check-evidence.sh's guard with inverted polarity ("= 1 ] || exit 1"),
#    which fails only when the variable is unset;
#  - check-autorelease-pool.sh's guard replaced by a one-line
#    "if [ ... ]; then exit 1; fi" on the line after the skip's message;
#  - check-metal-shim.sh's "|| bad" inside a piped while loop, whose
#    fail=1 is lost with the loop's subshell.
# And the accounting is run against mutated copies of the files it reads,
# each of which it must refuse: a quoted key ("X": 1, 'X': 1) and a
# NAME=1 prefix of the run in the workflow, a : "${X:=1}" and an
# unclassifiable token in scripts/test.sh, and a new $NAME guard in a check.
#
# Every run is bounded by perl's alarm (REQUIRE_GUARDS_WALL seconds, 600 by
# default); a run that outlives it fails the check by name. The alarm kills
# the check, not its process group: every run takes the check's skip path,
# which no check reaches after starting a long-lived child, and the logs are
# files, so a child left behind cannot hold this check; a process group of
# its own would also take the check out of the terminal's, and Ctrl-C would
# no longer reach it.
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
# <variable>|<check>|<force>|<guard lines it proves, 1>|<test, or none>
printf '%s\n' \
	'CHECK_IOS_REQUIRE|check-ios.sh|linux|1|' \
	'CHECK_IOS_REQUIRE|check-ios.sh|nosim|1|' \
	'CHECK_EVIDENCE_REQUIRE|check-evidence.sh|nopython|1|' \
	'CHECK_AUTORELEASE_POOL_REQUIRE|check-autorelease-pool.sh|linux|1|' \
	'CHECK_STATIC_MODULES_REQUIRE|check-static-modules.sh|linux|1|' \
	'CHECK_BENCH_REQUIRE|check-bench.sh|linux|1|t_freq' \
	'CHECK_BENCH_REQUIRE|check-bench.sh|linux|1|t_probe_first' \
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

while IFS='|' read -r _v _c _f _g _a; do
	case $_v in '' | *[!A-Z0-9_]*) bad "case table: bad variable '$_v'"; continue ;; esac
	[ "$_g" = 1 ] || bad "case table: $_c${_a:+ $_a} ($_f) claims '$_g' guard lines; one run proves one guard (1)"
	case $_a in *[!A-Za-z0-9_]*) bad "case table: $_c ($_f) names '$_a'; one run proves one guard (one test at most)" ;; esac
	[ -f "tests/$_c" ] || bad "case table: tests/$_c does not exist"
done < "${tmp:?}/cases"

# ---- The variables CI and the gate set, and the accounting. -------------
wf=.github/workflows/macos.yml
for _f in "$wf" scripts/test.sh; do
	[ -f "$_f" ] || { echo "$me: $_f is missing"; exit 1; }
done

# The workflow, parsed by ruby's yaml as tests/check-ci-parity.sh parses it:
# the _REQUIRE keys of every env map (workflow, job, step) and every
# NAME_REQUIRE= in a step's run, one name a line.
wf_prog='
begin
  require "yaml"
rescue LoadError
  puts "ruby cannot load its yaml library; nothing was read"
  exit 3
end
begin
  tree = YAML.safe_load(File.read(ARGV[0]), permitted_classes: [], aliases: true)
rescue StandardError => e
  puts "cannot be read as YAML (" + e.class.to_s + "); nothing was read"
  exit 1
end
def map(v)
  v.is_a?(Hash) ? v : {}
end
$out = []
def env(h)
  map(map(h)["env"]).each_key { |k| $out << k.to_s if k.to_s =~ /_REQUIRE\z/ }
end
def runs(s)
  return unless s.is_a?(String)
  s.scan(/(?:\A|[^A-Za-z0-9_$])([A-Za-z_][A-Za-z0-9_]*_REQUIRE):?=/) { |m| $out << m[0] }
end
env(tree)
map(map(tree)["jobs"]).each_value do |job|
  env(job)
  steps = map(job)["steps"]
  next unless steps.is_a?(Array)
  steps.each { |st| env(st); runs(map(st)["run"]) }
end
puts $out.uniq.sort
'

# scripts/test.sh, outside comments: "v <name>" for each assignment
# (${NAME:=...} or ${NAME=...}, NAME=..., also after export), "u <line>:
# <text>" for a line with a _REQUIRE token that is neither an assignment, an
# expansion ($NAME, ${NAME...}) nor a name in an export list.
sh_prog='
/^[ \t]*#/ || !/_REQUIRE/ { next }
{
	s = $0
	while (match(s, /\$\{[A-Za-z_][A-Za-z0-9_]*_REQUIRE:?=/)) {
		t = substr(s, RSTART + 2, RLENGTH - 2)
		sub(/:?=$/, "", t)
		print "v " t
		s = substr(s, 1, RSTART - 1) " " substr(s, RSTART + RLENGTH)
	}
	gsub(/\$\{?[A-Za-z_][A-Za-z0-9_]*_REQUIRE/, " ", s)
	while (match(s, /(^|[^A-Za-z0-9_])[A-Za-z_][A-Za-z0-9_]*_REQUIRE=/)) {
		t = substr(s, RSTART, RLENGTH - 1)
		sub(/^[^A-Za-z_]/, "", t)
		print "v " t
		s = substr(s, 1, RSTART - 1) " " substr(s, RSTART + RLENGTH)
	}
	if (s ~ /^[ \t]*export[ \t]/) {
		t = s
		sub(/^[ \t]*export[ \t]+/, "", t)
		if (t ~ /^([A-Za-z_][A-Za-z0-9_]*[ \t]*)+;?[ \t]*$/)
			s = ""
	}
	if (s ~ /_REQUIRE/)
		print "u " NR ": " $0
}
'

# reads <tests dir> <check> <variable>: the lines of the check that read the
# variable, an expansion ($NAME, ${NAME}, ${NAME:-...}, ${NAME-...}),
# outside comments.
reads() {
	grep -v '^[[:space:]]*#' "$1/$2" | grep -cE '\$\{?'"$3"'([^A-Za-z0-9_]|$)' || :
}

# account <workflow> <test.sh> <tests dir> <work dir>: prints every
# accounting problem of those files against the tables above and fails on
# any; the variables go to <work dir>/vars.
account() {
	_aw=$1 _at=$2 _ad=$3 _ao=$4 _ap=0
	if ! command -v ruby > /dev/null 2>&1; then
		echo "no ruby on PATH (its yaml reads the env maps of $_aw); nothing was checked"
		return 1
	fi
	_s=0
	ruby --disable-gems -e "$wf_prog" -- "$_aw" > "${_ao:?}/vars.wf" || _s=$?
	if [ "$_s" -ne 0 ]; then
		echo "$_aw: $(head -n 1 "${_ao:?}/vars.wf") (ruby status $_s)"
		return 1
	fi
	awk "$sh_prog" "$_at" > "${_ao:?}/vars.sh"
	if grep -q '^u ' "${_ao:?}/vars.sh"; then
		sed -n "s|^u |$_at:|p" "${_ao:?}/vars.sh" | sed 's/$/ (a _REQUIRE token that is neither an assignment nor an expansion)/'
		_ap=1
	fi
	{ cat "${_ao:?}/vars.wf"; sed -n 's/^v //p' "${_ao:?}/vars.sh"; } | sort -u > "${_ao:?}/vars"
	if ! [ -s "${_ao:?}/vars" ]; then
		echo "no NAME_REQUIRE variable found in $_aw or $_at"
		return 1
	fi
	while IFS='|' read -r _v _c _f _g _a; do
		grep -qx "$_v" "${_ao:?}/vars" || { echo "a case names $_v, which neither $_aw nor $_at sets"; _ap=1; }
	done < "${tmp:?}/cases"
	while IFS='|' read -r _c _v _r; do
		[ -n "$_r" ] || { echo "unforced list: $_c $_v has no reason"; _ap=1; }
		[ -f "$_ad/$_c" ] || { echo "unforced list: $_ad/$_c does not exist"; _ap=1; continue; }
		[ "$(reads "$_ad" "$_c" "$_v")" -ge 1 ] || { echo "unforced list: $_ad/$_c no longer reads $_v (stale entry)"; _ap=1; }
	done < "${tmp:?}/unforced"
	while read -r _v; do
		awk -F'|' -v v="$_v" '$1 == v { n++ } END { exit !n }' "${tmp:?}/cases" ||
			{ echo "$_v is set by $_aw or $_at and has no case here"; _ap=1; }
		for _p in "$_ad"/check-*.sh; do
			_c=${_p##*/}
			[ "$_c" != "$me.sh" ] || continue
			_n=$(reads "$_ad" "$_c" "$_v")
			_e=$(awk -F'|' -v v="$_v" -v c="$_c" '$1 == v && $2 == c { n += $4 } END { print n + 0 }' "${tmp:?}/cases")
			_u=$(awk -F'|' -v v="$_v" -v c="$_c" '$1 == c && $2 == v { n++ } END { print n + 0 }' "${tmp:?}/unforced")
			[ "$_n" -eq $((_e + _u)) ] ||
				{ echo "$_ad/$_c reads $_v on $_n lines; the cases prove $_e and the unforced list holds $_u"; _ap=1; }
		done
	done < "${_ao:?}/vars"
	return "$_ap"
}

if ! account "$wf" scripts/test.sh tests "${tmp:?}" > "${tmp:?}/acct"; then
	sed "s/^/$me: /" "${tmp:?}/acct"
	fail=1
fi

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
# A git that fails a plain "git cat-file ...", as check-cvar-docs-selftest.sh
# asks for the upstream commit; check-cvar-docs.sh's own calls start with -c
# and pass, so its self-test runs as before.
mkdir "${tmp:?}/bin-nocatfile"
printf '#!/bin/sh\n[ "${1:-}" != cat-file ] || exit 1\nexec '\''%s'\'' "$@"\n' "$real_git" > "${tmp:?}/bin-nocatfile/git"
chmod +x "${tmp:?}/bin-nocatfile/git"
# A PATH of /usr/bin and /bin without one tool (and /usr/sbin, /sbin).
stripped() { # stripped <dir> <tool>
	mkdir "${1:?}"
	ln -s /usr/bin/* "${1:?}/"
	ln -sf /bin/* "${1:?}/"
	rm -f "${1:?}/$2" "${1:?}/$2".*
}
stripped "${tmp:?}/path-nopython" python3
stripped "${tmp:?}/path-nobash" bash

# run <log> <tree> <check> <force> <variable or -> [<test>]: the check's
# status, 142 when it outlived the wall bound.
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
	_rs=0
	(
		PATH=$_path
		TMPDIR=${tmp:?}/t.$n
		export PATH TMPDIR
		if [ "$_var" != - ]; then
			export "$_var=1"
		fi
		exec perl -e 'alarm shift @ARGV; exec @ARGV or exit 127' "$wall" "$_tree/tests/$_chk" "$@"
	) > "$_log" 2>&1 < /dev/null || _rs=$?
	rm -rf "${tmp:?}/t.$n"
	return "$_rs"
}

# judged <log> <status> <variable> <test or empty>: the case's judgement of
# a run with the variable set. It accepts a failure with a line that shows
# the guard's own message, and otherwise prints why not and fails.
judged() {
	if [ "$2" -eq 142 ]; then
		echo "outlived ${wall}s"
		return 1
	fi
	if [ "$2" -eq 0 ]; then
		echo "passed: the guard does not fail"
		return 1
	fi
	_jm="$3=1 and"
	[ -z "$4" ] || _jm="$4: $_jm"
	if ! grep -qF "$_jm" "$1"; then
		echo "failed with status $2, but no line shows the guard's message '$_jm'"
		return 1
	fi
}

# ---- The real checks. ---------------------------------------------------
ncase=0
while IFS='|' read -r _v _c _f _g _a; do
	ncase=$((ncase + 1))
	_l=${tmp:?}/case.$ncase
	# shellcheck disable=SC2086 # the test name, or none
	_st=0; run "$_l.set" "$root" "$_c" "$_f" "$_v" $_a || _st=$?
	if ! _why=$(judged "$_l.set" "$_st" "$_v" "$_a"); then
		tail -n 5 "$_l.set"
		bad "$_c${_a:+ $_a} ($_f, $_v=1) $_why"
	fi
	# shellcheck disable=SC2086
	_su=0; run "$_l.unset" "$root" "$_c" "$_f" - $_a || _su=$?
	if [ "$_su" -ne 0 ]; then
		tail -n 5 "$_l.unset"
		bad "$_c${_a:+ $_a} ($_f, $_v unset) failed with status $_su; it should pass"
	fi
	echo "$me: $_c${_a:+ $_a} ($_f): $_v=1 status $_st, unset status $_su"
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
	chmod +x "${1:?}/tests/$2.orig"
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
# refused <name> <dir> <check> <force> <variable> [<test>]: the case accepts
# the unmutated copy in the mutant's tree (the control) and refuses the
# mutant.
refused() {
	_mn=$1 _md=$2 _mc=$3 _mf=$4 _mv=$5 _mt=${6:-}
	nmut=$((nmut + 1))
	# shellcheck disable=SC2086 # the test name, or none
	_cs=0; run "$_md.control" "$_md" "$_mc.orig" "$_mf" "$_mv" $_mt || _cs=$?
	if ! _why=$(judged "$_md.control" "$_cs" "$_mv" "$_mt"); then
		tail -n 5 "$_md.control"
		bad "mutant $_mn: the control (the unmutated copy in the same tree) $_why; the tree, not the mutant, decides"
		return 0
	fi
	# shellcheck disable=SC2086
	_st=0; run "$_md.log" "$_md" "$_mc" "$_mf" "$_mv" $_mt || _st=$?
	if _why=$(judged "$_md.log" "$_st" "$_mv" "$_mt"); then
		tail -n 5 "$_md.log"
		bad "mutant $_mn: the case accepts it (status $_st, with the guard's message)"
	else
		echo "$me: mutant $_mn refused: $_why"
	fi
}

ncase=0
while IFS='|' read -r _v _c _f _g _a; do
	ncase=$((ncase + 1))
	_d=${tmp:?}/m.print.$ncase
	tree "$_d" "$_c"
	if [ -n "$_a" ]; then
		# only the guard of this case's test
		sed "/$_a: $_v=1 and/s/\(\${$_v:-0}\" != 1 ] ||\).*/\1 echo \"$_v=1 (prints only)\"/" "$_d/tests/$_c.orig" > "$_d/tests/$_c"
	else
		sed "s/\(\${$_v:-0}\" != 1 ] ||\).*/\1 echo \"$_v=1 (prints only)\"/" "$_d/tests/$_c.orig" > "$_d/tests/$_c"
	fi
	if ! mutated "$_d" "$_c"; then
		bad "mutant print-only ($_c${_a:+ $_a}, $_v): no guard of the shape [ \"\${$_v:-0}\" != 1 ] || ... to change"
		continue
	fi
	# shellcheck disable=SC2086 # the test name, or none
	refused "print-only ($_c${_a:+ $_a}, $_f)" "$_d" "$_c" "$_f" "$_v" $_a
done < "${tmp:?}/cases"

# t_probe_first's guard alone print-only, run as the old two-test case ran
# it: t_freq's guard still fails, so that case accepted the mutant.
_d=${tmp:?}/m.probe-first
tree "$_d" check-bench.sh
sed '/t_probe_first: CHECK_BENCH_REQUIRE=1 and/s/|| { bad "[^"]*"; return 1; }$/|| echo "t_probe_first (prints only)"/' "$_d/tests/check-bench.sh.orig" > "$_d/tests/check-bench.sh"
if mutated "$_d" check-bench.sh; then
	refused "probe-first" "$_d" check-bench.sh linux CHECK_BENCH_REQUIRE t_probe_first
	_st=0; run "$_d.both" "$_d" check-bench.sh linux CHECK_BENCH_REQUIRE t_freq t_probe_first || _st=$?
	if [ "$_st" -eq 0 ] || ! grep -qF 't_freq: CHECK_BENCH_REQUIRE=1 and' "$_d.both"; then
		bad "mutant probe-first: run with t_freq too, it does not fail on t_freq's guard (status $_st); the old case's blind spot is not shown"
	fi
else
	bad "mutant probe-first does not apply to check-bench.sh"
fi

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

# ---- Accounting mutants. ------------------------------------------------
# unaccounted <name> <dir> <workflow> <test.sh> <tests dir> <message>: the
# accounting of the mutated files fails with <message>.
unaccounted() {
	nmut=$((nmut + 1))
	_as=0; account "$3" "$4" "$5" "${2:?}" > "${2:?}/acct" 2>&1 || _as=$?
	if [ "$_as" -eq 0 ]; then
		bad "mutant $1: the accounting accepts it"
	elif ! grep -qF "$6" "${2:?}/acct"; then
		tail -n 5 "${2:?}/acct"
		bad "mutant $1: the accounting fails, but not with '$6'"
	else
		echo "$me: mutant $1 refused: $6"
	fi
}
# wfmut <name> <awk program>: the workflow rewritten into a scratch
# directory; the accounting must refuse CHECK_FOO_REQUIRE for lacking a case.
wfmut() {
	_d=${tmp:?}/a.$1
	mkdir "${_d:?}"
	awk "$2" "$wf" > "${_d:?}/macos.yml"
	if cmp -s "$wf" "${_d:?}/macos.yml"; then
		bad "mutant $1 does not apply to $wf"
		return 0
	fi
	unaccounted "$1" "$_d" "${_d:?}/macos.yml" scripts/test.sh tests "CHECK_FOO_REQUIRE is set by"
}
wfmut wf-dquote '{ print } /^[ \t]*CHECK_IOS_REQUIRE: 1$/ { print "        \"CHECK_FOO_REQUIRE\": 1" }'
wfmut wf-squote '{ print } /^[ \t]*CHECK_IOS_REQUIRE: 1$/ { print "        '"'"'CHECK_FOO_REQUIRE'"'"': 1" }'
wfmut wf-run-prefix '$0 == "        scripts/ci-checks.sh" { print "        CHECK_FOO_REQUIRE=1 scripts/ci-checks.sh"; next } { print }'

# shmut <name> <line> <message>: <line> added after scripts/test.sh's export
# of the gate's variables; the accounting must refuse it with <message>.
shmut() {
	_d=${tmp:?}/a.$1
	mkdir "${_d:?}"
	awk -v ins="$2" '{ print } /^[ \t]*export CHECK_REF_METAL_REQUIRE/ { print ins }' scripts/test.sh > "${_d:?}/test.sh"
	if cmp -s scripts/test.sh "${_d:?}/test.sh"; then
		bad "mutant $1 does not apply to scripts/test.sh"
		return 0
	fi
	unaccounted "$1" "$_d" "$wf" "${_d:?}/test.sh" tests "$3"
}
shmut sh-assign-default '	: "${CHECK_FOO_REQUIRE:=1}"' "CHECK_FOO_REQUIRE is set by"
shmut sh-export '	export CHECK_FOO_REQUIRE=1' "CHECK_FOO_REQUIRE is set by"
shmut sh-unclassified '	echo CHECK_FOO_REQUIRE' "neither an assignment nor an expansion"

# A new guard of a set variable, written as $NAME, in a copy of
# check-evidence.sh: one more line reads the variable than the cases prove.
_d=${tmp:?}/a.dollar
tree "$_d" check-evidence.sh
awk '{ print } index($0, "${CHECK_EVIDENCE_REQUIRE:-0}") { print "\t[ \"$CHECK_EVIDENCE_REQUIRE\" != 1 ] || exit 1" }' "$_d/tests/check-evidence.sh.orig" > "$_d/tests/check-evidence.sh"
if mutated "$_d" check-evidence.sh; then
	unaccounted dollar "$_d" "$wf" scripts/test.sh "$_d/tests" "check-evidence.sh reads CHECK_EVIDENCE_REQUIRE on 2 lines"
else
	bad "mutant dollar does not apply to check-evidence.sh"
fi

t1=$(date +%s)
if [ "$fail" -ne 0 ]; then
	echo "$me: FAILED ($((t1 - t0))s)"
	exit 1
fi
nvar=$(wc -l < "${tmp:?}/vars" | tr -d ' ')
nun=$(wc -l < "${tmp:?}/unforced" | tr -d ' ')
echo "$me: ok ($nvar variables, $ncase cases, $nun unforced guards, $nmut mutants refused, $((t1 - t0))s)"
