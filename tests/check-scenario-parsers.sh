#!/bin/sh
# Invariant: the pin scenario format has one reader (FIX122, AR-13).
#
# A pin scenario (scripts/pins/scenarios/*.cfg) is read on five paths: the
# record of scripts/pins.sh (build_args), "scripts/pins.sh scenario-args",
# the quake2 adapter of the bench harness (q2_scenario_args in
# scripts/bench/adapters/quake2.sh) and the MetalFX report generator
# (parse_scenario and scenario_cvars in scripts/metalfx-report/mfx.py).
# Every refusal of the format lives in scenario_sets in scripts/pins.sh;
# the adapter and the generator call scenario-args and keep no list of
# their own. On a copy of scripts/ this fails when
# - refuse: a reader accepts a refuse case of scripts/pins/scenario-cases.txt
#   (the pin selftest's own cases, which hold the card's: basedir, game,
#   datadir, cfgdir, cddir, pins_*, ".." in a value, a value starting with
#   "-" that is not a number, and every line that is not
#   "set <cvar> <value>");
# - accept: a reader refuses an accept case or a scenario of the tree, or
#   passes on other cvars or values than the file's set lines;
# - closed: a scenario file that is missing is not refused by every reader,
#   or the adapter or the generator does not refuse a scenario when the
#   shared reader is missing, exits non-zero (even with good output), or
#   prints a line that is not "<cvar> <value>";
# - shared: a cvar name added to the shared reader's refusals (a canary) is
#   not refused by every reader, as when one keeps a private copy of the
#   rules.
# Then it runs its mutants, each a change to a copy that the test named
# must catch.
#
# Usage: tests/check-scenario-parsers.sh [root]   (root defaults to the repo;
# the cases are always this checkout's). Needs bash and python3; no engine
# and no game data.
set -u
export LC_ALL=C

here=$(cd "$(dirname "$0")/.." && pwd -P)
root=${1:-$here}
me=check-scenario-parsers
cases=$here/scripts/pins/scenario-cases.txt
tab=$(printf '\t')
tmp=$(mktemp -d "${TMPDIR:-/tmp}/check-scenario-parsers.XXXXXX") || exit 1
trap 'rm -rf "${tmp:?}"' EXIT
for t in bash python3 awk; do
	command -v "$t" > /dev/null 2>&1 || {
		echo "$me: FAIL: no $t"
		exit 1
	}
done
[ -f "$cases" ] || {
	echo "$me: FAIL: no $cases"
	exit 1
}

bad=0
fail() {
	echo "$me: FAIL: $*"
	bad=1
}

# lit <file> <old> <new>: replaces each <old> in <file> by <new>, literally;
# fails when there is none
lit() {
	LIT_OLD=$2 LIT_NEW=$3 awk 'BEGIN { o = ENVIRON["LIT_OLD"]; n = ENVIRON["LIT_NEW"] }
		{ p = index($0, o); if (p) { $0 = substr($0, 1, p - 1) n substr($0, p + length(o)); c++ } print } END { exit !c }' \
		"$1" > "$1.new" && mv "$1.new" "$1"
}

# tree <dir> [<from>]: a copy of <from>'s (default: root's) scripts/ in <dir>
tree() {
	rm -rf "${1:?}"
	mkdir -p "$1"
	cp -R "${2:-$root}/scripts" "$1/scripts"
}

# readers <tree> <list> <out>: runs each reader of <tree> on each file of
# <list> (one path a line). <out>/<reader>.<n> is "ok" then the
# "<cvar> <value>" lines it passes on (mfxcvars: the cvar names, sorted),
# or "refused" then why, or "error" then what failed otherwise.
readers() {
	_t=$1 _l=$2 _o=$3
	rm -rf "${_o:?}"
	mkdir -p "$_o/btmp"
	printf '#!/bin/sh\nexit 0\n' > "$_o/eng"
	chmod +x "$_o/eng"
	_n=0
	while IFS= read -r _f; do
		_n=$((_n + 1))
		if /bin/bash "$_t/scripts/pins.sh" scenario-args "$_f" > "$_o/p" 2> "$_o/e"; then
			{ echo ok; cat "$_o/p"; } > "$_o/pins.$_n"
		else
			{ echo refused; cat "$_o/e"; } > "$_o/pins.$_n"
		fi
		# the record's own path, as a dry run of a scenario of the tree
		_s=$_t/scripts/pins/scenarios/zzqcase.cfg
		rm -f "$_s"
		[ ! -f "$_f" ] || cp "$_f" "$_s"
		if PINS_BIN="$_o/eng" PINS_STORE="$_o/store" PINS_DRYRUN=1 /bin/bash "$_t/scripts/pins.sh" record gl1 zzqcase > "$_o/p" 2> "$_o/e"; then
			{
				echo ok
				awk '{ for (i = 1; i <= NF && $i != "-portable"; i++) ;
					for (i++; i + 2 <= NF && $i == "+set" && $(i + 1) != "vid_renderer"; i += 3) print $(i + 1), $(i + 2) }' "$_o/p"
			} > "$_o/record.$_n"
		else
			{ echo refused; cat "$_o/e"; } > "$_o/record.$_n"
		fi
		rm -f "$_s"
	done < "$_l"
	(
		BENCH_ROOT=$_t/scripts/bench
		. "$BENCH_ROOT/core/common.sh" || exit 1
		. "$BENCH_ROOT/adapters/quake2.sh" || exit 1
		BENCH_TMP=$_o/btmp
		_n=0
		while IFS= read -r _f; do
			_n=$((_n + 1))
			Q2_ARGS=$_o/q2args
			: > "$Q2_ARGS"
			if q2_scenario_args "$_f" 2> "$_o/e"; then
				{
					echo ok
					awk 'NR % 3 == 1 { if ($0 != "+set") { print "not +set: " $0; exit 1 } next }
						NR % 3 == 2 { k = $0; next } { print k " " $0 }' "$Q2_ARGS"
				} > "$_o/adapter.$_n"
			else
				{ echo refused; cat "$_o/e"; } > "$_o/adapter.$_n"
			fi
		done < "$_l"
	) > "$_o/adapter.log" 2>&1 || fail "the adapter of $_t could not be loaded: $(cat "$_o/adapter.log")"
	python3 -I -c '
import importlib.util, sys
t, o, l = sys.argv[1:4]
spec = importlib.util.spec_from_file_location("mfx", t + "/scripts/metalfx-report/mfx.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
with open(l) as f:
    files = f.read().split("\n")[:-1]
for n, p in enumerate(files, 1):
    for name, fn in (("mfx", lambda: "".join("%s %s\n" % kv for kv in m.parse_scenario(p))),
                     ("mfxcvars", lambda: "".join(c + "\n" for c in sorted(m.scenario_cvars(p))))):
        try:
            r = "ok\n" + fn()
        except m.Refuse as e:
            r = "refused\n%s\n" % e
        except Exception as e:
            r = "error\n%r\n" % e
        with open("%s/%s.%d" % (o, name, n), "w") as w:
            w.write(r)
' "$_t" "$_o" "$_l" > "$_o/mfx.log" 2>&1 || fail "mfx.py of $_t could not be loaded: $(cat "$_o/mfx.log")"
}

# want <out> <n> <verdict> <what> <reader>...: each reader's verdict on file <n>
want() {
	_wo=$1 _wn=$2 _wv=$3 _ww=$4
	shift 4
	for _r; do
		_got=$(sed -n 1p "$_wo/$_r.$_wn" 2> /dev/null)
		[ "$_got" = "$_wv" ] || fail "$_r: $_ww must be ${_wv}, got ${_got:-nothing}: $(sed -n 2p "$_wo/$_r.$_wn" 2> /dev/null)"
	done
}

all="pins record adapter mfx mfxcvars"

# t_refuse <tree>: every refuse case is refused by every reader
t_refuse() {
	bad=0
	: > "$tmp/l-refuse"
	_i=0
	while IFS= read -r _c || [ -n "$_c" ]; do
		case $_c in
		'' | '#'*) continue ;;
		refuse"$tab"*) ;;
		accept"$tab"*) continue ;;
		*)
			fail "scenario-cases.txt: not '<refuse|accept><tab><line>': $_c"
			continue
			;;
		esac
		_i=$((_i + 1))
		printf 'set timedemo 1\n%s\n' "${_c#*"$tab"}" > "$tmp/refuse-$_i.cfg"
		echo "$tmp/refuse-$_i.cfg" >> "$tmp/l-refuse"
	done < "$cases"
	[ "$_i" -ge 30 ] || fail "scenario-cases.txt holds $_i refuse cases"
	readers "$1" "$tmp/l-refuse" "$tmp/o-refuse"
	_i=0
	while IFS= read -r _f; do
		_i=$((_i + 1))
		want "$tmp/o-refuse" "$_i" refused "the line '$(sed -n 2p "$_f")'" $all
	done < "$tmp/l-refuse"
	return $bad
}

# t_accept <tree>: every accept case and every scenario of the tree is
# accepted by every reader, which passes on just the file's set lines
t_accept() {
	bad=0
	: > "$tmp/l-accept"
	_i=0
	while IFS= read -r _c || [ -n "$_c" ]; do
		case $_c in accept"$tab"*) ;; *) continue ;; esac
		_i=$((_i + 1))
		printf 'set timedemo 1\n%s\n' "${_c#*"$tab"}" > "$tmp/accept-$_i.cfg"
		echo "$tmp/accept-$_i.cfg" >> "$tmp/l-accept"
	done < "$cases"
	[ "$_i" -ge 1 ] || fail "scenario-cases.txt holds no accept case"
	_k=0
	for _f in "$1"/scripts/pins/scenarios/*.cfg; do
		[ -f "$_f" ] || continue
		_k=$((_k + 1))
		echo "$_f" >> "$tmp/l-accept"
	done
	[ "$_k" -ge 20 ] || fail "the tree holds $_k scenarios"
	readers "$1" "$tmp/l-accept" "$tmp/o-accept"
	_i=0
	while IFS= read -r _f; do
		_i=$((_i + 1))
		want "$tmp/o-accept" "$_i" ok "$(basename "$_f")" $all
		{
			echo ok
			sed -n 's/^set //p' "$_f"
		} > "$tmp/want"
		{
			echo ok
			sed -n 's/^set \([^ ]*\) .*/\1/p' "$_f" | sort -u
		} > "$tmp/want-names"
		for _r in pins record adapter mfx; do
			cmp -s "$tmp/want" "$tmp/o-accept/$_r.$_i" || fail "$_r: $(basename "$_f") is passed on as: $(tr '\n' ' ' < "$tmp/o-accept/$_r.$_i")"
		done
		cmp -s "$tmp/want-names" "$tmp/o-accept/mfxcvars.$_i" || fail "mfxcvars: $(basename "$_f") gives: $(tr '\n' ' ' < "$tmp/o-accept/mfxcvars.$_i")"
	done < "$tmp/l-accept"
	return $bad
}

# t_closed <tree>: a missing scenario, and a shared reader that is missing,
# fails, or prints what is not "<cvar> <value>", are each refused
t_closed() {
	bad=0
	printf 'set timedemo 1\nset cl_gun 0\n' > "$tmp/good.cfg"
	echo "$tmp/no-such-scenario.cfg" > "$tmp/l-closed"
	readers "$1" "$tmp/l-closed" "$tmp/o-closed"
	want "$tmp/o-closed" 1 refused "a missing scenario" $all
	echo "$tmp/good.cfg" > "$tmp/l-good"
	tree "$tmp/t-closed" "$1"
	rm -f "$tmp/t-closed/scripts/pins.sh"
	readers "$tmp/t-closed" "$tmp/l-good" "$tmp/o-closed"
	want "$tmp/o-closed" 1 refused "a scenario with no scripts/pins.sh" adapter mfx mfxcvars
	# <what>|<the stub's commands>
	for _stub in 'a status of 1 with good output|printf "timedemo 1\\ncl_gun 0\\n"; exit 1' \
		'a "set" line|printf "timedemo 1\\nset cl_gun 0\\n"' \
		'a line of one word|printf "timedemo 1\\ncl_gun\\n"' \
		'two spaces|printf "timedemo 1\\ncl_gun  0\\n"' \
		'an empty line|printf "timedemo 1\\n\\ncl_gun 0\\n"' \
		'a capital|printf "timedemo 1\\nCl_gun 0\\n"' \
		'a leading space|printf " timedemo 1\\n"'; do
		printf '#!/bin/sh\n%s\n' "${_stub#*|}" > "$tmp/t-closed/scripts/pins.sh"
		readers "$tmp/t-closed" "$tmp/l-good" "$tmp/o-closed"
		want "$tmp/o-closed" 1 refused "a shared reader that gives ${_stub%%|*}" adapter mfx mfxcvars
	done
	return $bad
}

# t_shared <tree>: a canary added to the shared reader's refusals is refused
# by every reader, and accepted by each without it
t_shared() {
	bad=0
	printf 'set timedemo 1\nset zzqcanary 1\n' > "$tmp/canary.cfg"
	echo "$tmp/canary.cfg" > "$tmp/l-canary"
	readers "$1" "$tmp/l-canary" "$tmp/o-canary"
	want "$tmp/o-canary" 1 ok "the canary before it is refused" $all
	tree "$tmp/t-canary" "$1"
	lit "$tmp/t-canary/scripts/pins.sh" 'case $name in basedir|cddir|game|datadir|cfgdir)' 'case $name in basedir|cddir|game|datadir|cfgdir|zzqcanary)' ||
		{
			fail "the canary does not apply to scripts/pins.sh"
			return 1
		}
	readers "$tmp/t-canary" "$tmp/l-canary" "$tmp/o-canary"
	want "$tmp/o-canary" 1 refused "the canary the shared reader refuses" $all
	return $bad
}

# ---- the tree ----------------------------------------------------------
tree "$tmp/real"
failed=
for _test in t_refuse t_accept t_closed t_shared; do
	if "$_test" "$tmp/real"; then
		echo "$me: ok   $_test"
	else
		echo "$me: FAIL $_test"
		failed="$failed $_test"
	fi
done
if [ -n "$failed" ]; then
	echo "$me: failed:$failed"
	exit 1
fi
[ "$root" = "$here" ] || {
	echo "$me: ok ($root; no mutants)"
	exit 0
}

# ---- the mutants: each must fail the test named ------------------------
# mutant <name> <file under scripts/> <old> <new> <test> [<copy of pins.sh>]
mfail=0
nmut=0
mutant() {
	nmut=$((nmut + 1))
	_m=$tmp/mutant-$1
	tree "$_m"
	if [ -n "${6:-}" ]; then
		cp "$_m/scripts/pins.sh" "$_m/scripts/$6"
	fi
	lit "$_m/scripts/$2" "$3" "$4" || {
		echo "$me: FAIL: mutant $1 does not apply to $2"
		mfail=1
		return
	}
	if "$5" "$_m" > "$tmp/mutant-$1.out" 2>&1; then
		echo "$me: FAIL: mutant $1 passed $5; the test does not catch it"
		mfail=1
		return
	fi
	echo "$me: mutant $1: caught by $5 ($(grep -m1 'FAIL: ' "$tmp/mutant-$1.out" | sed "s/^$me: FAIL: //"))"
}
mutant mfx-allows-basedir metalfx-report/mfx.py 'if p.returncode != 0:' \
	'if p.returncode != 0 and b" basedir " not in p.stderr:' t_refuse
mutant mfx-cvars-own-parse metalfx-report/mfx.py 'return set(k for k, _ in parse_scenario(path))' \
	'return set(l.split()[1] for l in open(path) if l.startswith("set ") and len(l.split()) == 3)' t_refuse
mutant dotdot-dropped pins.sh "case \$value in *..*) die \"\$label line \$n: the value must not contain '..': \$value\";; esac" ':' t_refuse
mutant mfx-skips-malformed metalfx-report/mfx.py \
	"raise Refuse(\"scripts/pins.sh scenario-args gave a line that is not '<cvar> <value>' for %s: %r\" % (path, line[:100]))" \
	'continue' t_closed
mutant mfx-ignores-status metalfx-report/mfx.py 'if p.returncode != 0:' 'if False:' t_closed
mutant adapter-skips-malformed bench/adapters/quake2.sh "*' '*' '* | ' '* | *' ') _qa_k= ;;" "*' '*' '* | ' '* | *' ') continue ;;" t_closed
mutant adapter-ignores-status bench/adapters/quake2.sh 'if ! /bin/bash "${BENCH_ROOT:?}/../pins.sh" scenario-args "$1" > "$_qa_out" 2> "$_qa_err"; then' \
	'if ! { /bin/bash "${BENCH_ROOT:?}/../pins.sh" scenario-args "$1" > "$_qa_out" 2> "$_qa_err" || :; }; then' t_closed
mutant adapter-private-copy bench/adapters/quake2.sh '/bin/bash "${BENCH_ROOT:?}/../pins.sh" scenario-args' \
	'/bin/bash "${BENCH_ROOT:?}/adapters/pins-copy.sh" scenario-args' t_shared bench/adapters/pins-copy.sh
mutant mfx-private-copy metalfx-report/mfx.py 'reader = os.path.join(ROOT, "scripts", "pins.sh")' \
	'reader = os.path.join(HERE, "pins-copy.sh")' t_shared metalfx-report/pins-copy.sh
mutant record-private-rules pins.sh 'sets=$(scenario_sets "$scen_dir/$scenario.cfg" "$scenario.cfg") || exit 1' \
	'sets=$(sed -n "s/^set //p" "$scen_dir/$scenario.cfg")' t_refuse
[ "$mfail" -eq 0 ] || {
	echo "$me: a mutant survived or did not apply"
	exit 1
}
echo "$me: ok (4 tests, $nmut mutants caught)"
