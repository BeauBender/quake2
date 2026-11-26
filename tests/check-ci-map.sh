#!/bin/bash
# Invariant (TASK211): the invariants job of .github/workflows/macos.yml runs
# as a matrix of three groups, fast, harness and ios, and a pull request
# still runs every check its change can affect. scripts/ci-checks.sh runs a
# group's checks; scripts/ci-select.sh chooses them from tests/ci-map.txt.
#
# (a) The map. Every tests/check-*.sh is in the map (a "fast", "harness" or
#     "ios" line) or in tests/ci-local-only.txt, so a new check in neither
#     group fails here. Every check the map names exists, is in
#     tests/ci-checks.txt and not in tests/ci-local-only.txt, and is in one
#     group; every name of tests/ci-checks.txt is in the map. The harness
#     group holds at least check-metalfx-report, check-bench,
#     check-bounded-run, check-gate-parallel, check-require-guards and
#     check-bench-entry; the ios group is check-ios.sh alone. The selector
#     reads the map without an error.
# (b) The workflows. macos.yml runs on a push to main, on a pull request and
#     on a schedule; its invariants_macos job has the matrix group [fast,
#     harness, ios], with no include or exclude and fail-fast false, and the
#     step that runs scripts/ci-checks.sh sets CI_CHECKS_GROUP to the matrix
#     group. macos.yml and codeql.yml have the concurrency group below, with
#     cancel-in-progress true: a push to main shares one group, so a newer
#     push cancels the older main run; a pull request has a group of its own
#     number, so only its own newer push cancels it; any other run (the
#     schedule) has a group of its own run id. The committed ruleset
#     requires the three matrix jobs by name. Read with ruby's yaml, as
#     tests/check-workflow-pins.sh reads the workflows.
# (c) The selector, run on lists of changed paths (NUL-ended, as git diff
#     -z writes them): a path no map entry accounts for, a change to the
#     map, the selector, scripts/ci-checks.sh, the two lists or .github/
#     (even when a map says such a path is "product"), a harness check's own
#     file, an empty list and a list not ended by a NUL all run every
#     harness check; a Markdown file at the top runs none; a change under
#     src/client/refresh/metal/ runs the harness checks whose globs match
#     it; names with blanks, a newline, a leading "-" or glob characters
#     are each one path; letter case does not hide a path.
# (d) scripts/ci-checks.sh --select, in a scratch git repository with a
#     merge commit as a pull request checkout is: the harness group reads
#     HEAD^1..HEAD, a push or a HEAD that is not a merge commit runs every
#     harness check, and a selector that fails or prints a name it was not
#     given runs every check of tests/ci-checks.txt. Then two runs through
#     scripts/gate-checks.sh on stub checks (a push, and the pull request)
#     pass, with the long checks named first.
# (e) scripts/test.sh (TASK227), in scratch repositories with a stub
#     scheduler: a change under src/ alone, a staged, an unstaged and an
#     untracked change alone, and untracked names with a blank, a newline
#     or a leading "-" leave out exactly the harness checks the selector
#     does not choose, from the checks and from gate_first, on one line
#     that names them and no path; a changed harness path leaves out none;
#     a path a commit since the merge base changed and the lane set back
#     (a stale origin/main) still counts;
#     GATE_ALL=1, no origin/main, no merge base, a git error, a change for
#     which the selector chooses no harness check, a selector that fails or
#     prints a name it was not given, a name twice or none, and an empty or
#     missing map run every check, on one line that says why.
# Then each mutant below must be refused by (a) to (e).
#
# Needs bash, awk, git and ruby (its yaml).
# Usage: tests/check-ci-map.sh
set -u
cd "$(dirname "$0")/.." || exit 1
root=$(pwd -P)
me=check-ci-map
export LC_ALL=C
unset RUBYOPT RUBYLIB

fail=0
bad() {
	printf '%s: %s\n' "$me" "$*"
	fail=1
}

MAP=tests/ci-map.txt
SEL=scripts/ci-select.sh
REQ_HARNESS="check-metalfx-report.sh check-bench.sh check-bounded-run.sh check-gate-parallel.sh check-require-guards.sh check-bench-entry.sh"
# the one concurrency group of both workflows
GROUP="\${{ github.workflow }}-\${{ github.event_name == 'push' && 'main' || github.event.pull_request.number || github.run_id }}"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/ci-map.XXXXXX") || exit 1
trap 'rm -rf "${tmp:?}"' EXIT
trap 'exit 1' HUP INT TERM

# names <file> <ci|local>: the names of a list, as tests/check-ci-parity.sh
# reads them
names() {
	awk -v kind="$2" '
		/\r/ { next }
		{
			sub(/#.*/, "")
			if (kind != "ci")
				sub(/:.*/, "")
			sub(/^[ \t]+/, "")
			sub(/[ \t]+$/, "")
			if ($0 != "")
				print
		}' "$1"
}

# mapped <map>: "<group> <check>" for every fast, harness and ios line
mapped() {
	awk '
		{
			sub(/#.*/, "")
			if (($1 == "fast" || $1 == "ios" || $1 == "harness") && $2 != "")
				print $1, $2
		}' "$1" | sort -u
}

# ---- (a) and (b): static_ok <root> ----------------------------------------
wf_prog='
begin
  require "yaml"
rescue LoadError
  puts "ruby cannot load its yaml library; nothing was checked"
  exit 3
end
GROUP = ARGV[0]
$bad = false
def err(m)
  puts m
  $bad = true
end
def map(v)
  v.is_a?(Hash) ? v : {}
end
def load(f)
  YAML.safe_load(File.read(f), permitted_classes: [], aliases: true)
rescue StandardError => e
  puts f + ": cannot be read as YAML (" + e.class.to_s + ")"
  exit 1
end
def conc(t, f)
  c = map(map(t)["concurrency"])
  err(f + ": the concurrency group is not the one of tests/check-ci-map.sh") unless c["group"] == GROUP
  err(f + ": concurrency cancel-in-progress is not true") unless c["cancel-in-progress"] == true
end
mac = ARGV[1]
t = load(mac)
on = map(t)["on"] || map(t)[true]
on = map(on)
br = map(on["push"])["branches"]
err(mac + ": push is not on main alone") unless br == ["main"]
err(mac + ": no pull_request trigger") unless on.key?("pull_request")
s = on["schedule"]
unless s.is_a?(Array) && !s.empty? && s.all? { |x| map(x)["cron"].is_a?(String) }
  err(mac + ": no schedule with a cron (the nightly run of every check)")
end
conc(t, mac)
job = map(map(t)["jobs"])["invariants_macos"]
if !job.is_a?(Hash)
  err(mac + ": no invariants_macos job")
else
  st = map(job["strategy"])
  m = map(st["matrix"])
  err(mac + ": invariants_macos: the matrix group is not [fast, harness, ios]") unless m["group"] == ["fast", "harness", "ios"]
  err(mac + ": invariants_macos: the matrix has other keys than group") unless m.keys.map(&:to_s) == ["group"]
  err(mac + ": invariants_macos: fail-fast is not false") unless st["fail-fast"] == false
  steps = job["steps"].is_a?(Array) ? job["steps"] : []
  run = steps.select { |x| map(x)["run"].is_a?(String) && x["run"].strip == "scripts/ci-checks.sh" }
  if run.length != 1
    err(mac + ": invariants_macos: not one step whose run is scripts/ci-checks.sh")
  else
    v = map(run[0]["env"])["CI_CHECKS_GROUP"]
    err(mac + ": invariants_macos: CI_CHECKS_GROUP is not ${{ matrix.group }}") unless v.is_a?(String) && v.strip == "${{ matrix.group }}"
  end
end
conc(load(ARGV[2]), ARGV[2])
exit($bad ? 1 : 0)
'

static_ok() {
	local r=$1 m=$1/$MAP n g c st
	if [ ! -f "$m" ]; then
		echo "$MAP is missing: no check is in a group"
		return 1
	fi
	local sf=0
	names "$r/tests/ci-checks.txt" ci > "$tmp/s.ci"
	names "$r/tests/ci-local-only.txt" local > "$tmp/s.lo"
	mapped "$m" > "$tmp/s.map"
	# the selector reads the map
	# shellcheck disable=SC2046 # the names have no blank
	if ! bash "$r/$SEL" "$m" ios - $(cat "$tmp/s.ci") > /dev/null 2> "$tmp/s.err"; then
		sed 's/^/the selector refuses the map: /' "$tmp/s.err"
		sf=1
	fi
	for c in "$r"/tests/check-*.sh; do
		n=${c##*/}
		if ! grep -qxF "$n" "$tmp/s.lo" && ! awk -v n="$n" '$2 == n { f = 1 } END { exit !f }' "$tmp/s.map"; then
			echo "tests/$n is in neither group: no fast, harness or ios line of $MAP, and not in tests/ci-local-only.txt"
			sf=1
		fi
	done
	while read -r g n; do
		[ -f "$r/tests/$n" ] || { echo "$MAP names $n, which is not a check (no tests/$n)"; sf=1; }
		grep -qxF "$n" "$tmp/s.ci" || { echo "$MAP names $n, which is not in tests/ci-checks.txt"; sf=1; }
		! grep -qxF "$n" "$tmp/s.lo" || { echo "$MAP names $n, which is in tests/ci-local-only.txt"; sf=1; }
	done < "$tmp/s.map"
	awk '{ print $2 }' "$tmp/s.map" | sort | uniq -d | while read -r n; do
		echo "$MAP puts $n in more than one group"
	done | grep . && sf=1
	while read -r n; do
		awk -v n="$n" '$2 == n { f = 1 } END { exit !f }' "$tmp/s.map" || { echo "$n of tests/ci-checks.txt is not in $MAP"; sf=1; }
	done < "$tmp/s.ci"
	for n in $REQ_HARNESS; do
		grep -qxF "harness $n" "$tmp/s.map" || { echo "$n is not in the harness group of $MAP"; sf=1; }
	done
	if [ "$(awk '$1 == "ios" { print $2 }' "$tmp/s.map")" != check-ios.sh ]; then
		echo "the ios group of $MAP is not check-ios.sh alone"
		sf=1
	fi
	# (b)
	if ! command -v ruby > /dev/null 2>&1; then
		echo "no ruby on PATH (its yaml reads the workflows); nothing was checked"
		sf=1
	else
		st=0
		ruby --disable-gems -e "$wf_prog" -- "$GROUP" "$r/.github/workflows/macos.yml" "$r/.github/workflows/codeql.yml" || st=$?
		[ "$st" -eq 0 ] || sf=1
	fi
	for g in fast harness ios; do
		grep -qF "\"context\": \"invariants_macos ($g)\"" "$r/.github/rulesets/main-protect.json" ||
			{ echo ".github/rulesets/main-protect.json does not require invariants_macos ($g)"; sf=1; }
	done
	! grep -qF '"context": "invariants_macos"' "$r/.github/rulesets/main-protect.json" ||
		{ echo ".github/rulesets/main-protect.json requires invariants_macos, a job name the matrix no longer has"; sf=1; }
	return $sf
}

if [ ! -f "$SEL" ]; then
	bad "$SEL is missing"
fi
if ! static_ok "$root" > "$tmp/static.out" 2>&1; then
	sed "s/^/$me: /" "$tmp/static.out"
	fail=1
fi
if [ ! -f "$MAP" ] || [ ! -f "$SEL" ]; then
	exit 1
fi

# ---- (c) the selector on lists of paths ------------------------------------
CI_NAMES=$(names tests/ci-checks.txt ci | tr '\n' ' ')
# the harness checks of the list, in its order
mapped "$MAP" > "$tmp/root.map"
HALL=$(for n in $CI_NAMES; do grep -qxF "harness $n" "$tmp/root.map" && printf '%s ' "$n"; done)
HALL=${HALL% }
[ -n "$HALL" ] || bad "the map has no harness check"

# paths <file> <path>...: writes the paths, each ended by a NUL
paths() {
	local f=$1
	shift
	: > "$f"
	[ $# -eq 0 ] || printf '%s\0' "$@" > "$f"
}

# sel <selector> <map> <group> <changed>: the selector's output on one line
sel() {
	local o
	# shellcheck disable=SC2086 # the names have no blank
	o=$(bash "$1" "$2" "$3" "$4" $CI_NAMES 2> /dev/null) || { printf 'STATUS%s' "$?"; return; }
	printf '%s' "$o" | tr '\n' ' ' | sed 's/ $//'
}

# expect <label> <want> <got>
expect() {
	if [ "$3" != "$2" ]; then
		printf 'case %s: want [%s], got [%s]\n' "$1" "$2" "$3"
		return 1
	fi
	return 0
}

# sel_cases <selector> <map>: every case of (c); prints the failures
sel_cases() {
	local s=$1 m=$2 cf=0 l=$tmp/l
	# a map that names the fixed paths as product: the selector's own list
	# must win over it
	cp "$m" "$tmp/m.product"
	printf '%s\n' 'product .github/*' 'product tests/*' 'product scripts/*' >> "$tmp/m.product"
	paths "$l" README.md
	expect top-md "" "$(sel "$s" "$m" harness "$l")" || cf=1
	paths "$l" README.md CLAUDE.md docs/runbook/INDEX.md
	expect docs "check-require-guards.sh" "$(sel "$s" "$m" harness "$l")" || cf=1
	paths "$l" src/client/refresh/metal/metal_rt.m src/client/refresh/metal/header/local.h
	expect metal "check-require-guards.sh" "$(sel "$s" "$m" harness "$l")" || cf=1
	paths "$l" src/client/refresh/metal/metal_rt.m zz-unmapped/new.txt
	expect unmapped "$HALL" "$(sel "$s" "$m" harness "$l")" || cf=1
	paths "$l" .gitattributes
	expect unmapped-top "$HALL" "$(sel "$s" "$m" harness "$l")" || cf=1
	local p
	for p in .github/workflows/macos.yml tests/ci-map.txt tests/ci-checks.txt tests/ci-local-only.txt scripts/ci-checks.sh scripts/ci-select.sh; do
		paths "$l" README.md "$p"
		expect "fixed:$p" "$HALL" "$(sel "$s" "$tmp/m.product" harness "$l")" || cf=1
	done
	paths "$l" .GitHub/workflows/x.yml
	expect fixed-case "$HALL" "$(sel "$s" "$tmp/m.product" harness "$l")" || cf=1
	for p in tests/check-bench.sh tests/check-metalfx-report.sh scripts/bench/core/common.sh scripts/gate-checks.sh; do
		paths "$l" "$p"
		expect "harness-script:$p" "$HALL" "$(sel "$s" "$m" harness "$l")" || cf=1
	done
	# one path each: a blank, a newline, a leading -, glob characters
	paths "$l" 'README.md' 'src/a b.c' "src/x
.github/y" 'src/-n' 'src/*' 'src/[a]?.c'
	expect odd-names "check-require-guards.sh" "$(sel "$s" "$m" harness "$l")" || cf=1
	paths "$l" '*'
	expect star "$HALL" "$(sel "$s" "$m" harness "$l")" || cf=1
	paths "$l" '-'
	expect dash "$HALL" "$(sel "$s" "$m" harness "$l")" || cf=1
	# letter case: scripts/PINS.sh is checked out over scripts/pins.sh
	paths "$l" scripts/PINS.sh
	case " $(sel "$s" "$m" harness "$l") " in
		*" check-metalfx-report.sh "*) ;;
		*) echo "case fold: scripts/PINS.sh does not run check-metalfx-report.sh"; cf=1 ;;
	esac
	paths "$l"
	expect empty "$HALL" "$(sel "$s" "$m" harness "$l")" || cf=1
	printf 'README.md\0CLAUDE.md' > "$l"
	expect unterminated "$HALL" "$(sel "$s" "$m" harness "$l")" || cf=1
	expect every "$HALL" "$(sel "$s" "$m" harness -)" || cf=1
	expect nofile STATUS2 "$(sel "$s" "$m" harness "$tmp/none")" || cf=1
	expect ios check-ios.sh "$(sel "$s" "$m" ios -)" || cf=1
	# fast: every candidate that is neither harness nor ios, a name the map
	# does not have included
	local want="" n
	for n in $CI_NAMES check-zz-new.sh; do
		grep -qxF "harness $n" "$tmp/root.map" || [ "$n" = check-ios.sh ] || want="$want $n"
	done
	# shellcheck disable=SC2086
	expect fast "${want# }" "$(bash "$s" "$m" fast - $CI_NAMES check-zz-new.sh 2> /dev/null | tr '\n' ' ' | sed 's/ $//')" || cf=1
	# a map that would match every path, or that names a check twice
	cp "$m" "$tmp/m.bad"
	echo 'product *' >> "$tmp/m.bad"
	expect map-star STATUS2 "$(sel "$s" "$tmp/m.bad" harness -)" || cf=1
	cp "$m" "$tmp/m.bad"
	echo 'fast check-ios.sh' >> "$tmp/m.bad"
	expect map-twice STATUS2 "$(sel "$s" "$tmp/m.bad" ios -)" || cf=1
	return $cf
}

if ! sel_cases "$SEL" "$MAP" > "$tmp/sel.out" 2>&1; then
	sed "s/^/$me: selector: /" "$tmp/sel.out"
	fail=1
fi

# ---- (d) scripts/ci-checks.sh --select in a scratch repository -------------
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_OBJECT_DIRECTORY \
	GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE GIT_PREFIX GIT_CONFIG \
	GIT_CONFIG_PARAMETERS GIT_CONFIG_COUNT GIT_CEILING_DIRECTORIES
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 GIT_TEMPLATE_DIR=$tmp/tpl
export HOME=$tmp/home XDG_CONFIG_HOME=$tmp/home/.config
mkdir -p "$HOME" "$GIT_TEMPLATE_DIR"

# repo <dir> <ci-checks.sh> <ci-select.sh>: a scratch repository whose HEAD
# is a merge commit: main adds .github/x after the base, the branch pr adds
# a file under src/client/refresh/metal/ and one with odd characters under
# src/, and main merges pr with --no-ff
repo() {
	local d=$1 n
	mkdir -p "$d/scripts" "$d/tests" "$d/src"
	cp "$2" "$d/scripts/ci-checks.sh"
	cp "$3" "$d/scripts/ci-select.sh"
	cp scripts/gate-checks.sh "$d/scripts/"
	mkdir -p "$d/scripts/bench/core"
	cp scripts/bench/core/common.sh "$d/scripts/bench/core/"
	cp tests/ci-checks.txt "$MAP" "$d/tests/"
	for n in $CI_NAMES; do
		printf '#!/bin/sh\nexit 0\n' > "$d/tests/$n"
		chmod +x "$d/tests/$n"
	done
	chmod +x "$d/scripts/ci-checks.sh"
	echo base > "$d/README.md"
	g "$d" init -q -b main && g "$d" add -A && g "$d" commit -q -m base &&
		g "$d" checkout -q -b pr && mkdir -p "$d/src/client/refresh/metal" &&
		echo x > "$d/src/client/refresh/metal/x.m" &&
		echo y > "$d/src/-x y
.github" && g "$d" add -A && g "$d" commit -q -m pr &&
		g "$d" checkout -q main && mkdir "$d/.github" && echo m > "$d/.github/x" &&
		g "$d" add -A && g "$d" commit -q -m main && g "$d" merge -q --no-ff -m merge pr
}
g() {
	local d=$1
	shift
	git -C "$d" -c core.hooksPath=/dev/null -c commit.gpgsign=false -c user.name=ci-map \
		-c user.email=ci-map@example.invalid "$@"
}
# cs <dir> <event> <group>: ci-checks.sh --select there, on one line
cs() {
	local o
	o=$(cd "$1" && env GITHUB_EVENT_NAME="$2" CI_CHECKS_GROUP="$3" sh scripts/ci-checks.sh --select 2> /dev/null) ||
		{ printf 'STATUS%s' "$?"; return; }
	printf '%s' "$o" | tr '\n' ' ' | sed 's/ $//'
}

# e2e_cases <ci-checks.sh> <ci-select.sh>: every case of (d)
e2e_cases() {
	local d=$tmp/e2e cf=0 all
	rm -rf "${d:?}"
	repo "$d" "$1" "$2" > "$tmp/repo.out" 2>&1 || { cat "$tmp/repo.out"; echo "cannot make the scratch repository"; return 1; }
	all=$(printf '%s' "$CI_NAMES" | sed 's/ $//')
	expect pr-merge check-require-guards.sh "$(cs "$d" pull_request harness)" || cf=1
	expect push "$HALL" "$(cs "$d" push harness)" || cf=1
	expect schedule "$HALL" "$(cs "$d" schedule harness)" || cf=1
	expect ios check-ios.sh "$(cs "$d" pull_request ios)" || cf=1
	expect unset "$all" "$(cs "$d" pull_request "")" || cf=1
	expect bogus STATUS2 "$(cs "$d" pull_request bogus)" || cf=1
	g "$d" checkout -q pr
	expect pr-not-merge "$HALL" "$(cs "$d" pull_request harness)" || cf=1
	g "$d" checkout -q main
	# a selector that fails, prints a name it was not given, or prints none
	# for fast: every check of the list
	cp "$d/scripts/ci-select.sh" "$tmp/sel.keep"
	printf '#!/bin/bash\nexit 1\n' > "$d/scripts/ci-select.sh"
	expect sel-fails "$all" "$(cs "$d" pull_request harness)" || cf=1
	printf '#!/bin/bash\necho check-nope.sh\n' > "$d/scripts/ci-select.sh"
	expect sel-stranger "$all" "$(cs "$d" pull_request harness)" || cf=1
	printf '#!/bin/bash\nexit 0\n' > "$d/scripts/ci-select.sh"
	expect sel-none-fast "$all" "$(cs "$d" pull_request fast)" || cf=1
	cp "$tmp/sel.keep" "$d/scripts/ci-select.sh"
	# a run through the scheduler, the long checks named first
	local o n
	o=$(cd "$d" && env GITHUB_EVENT_NAME=push CI_CHECKS_GROUP=harness GATE_JOBS=2 sh scripts/ci-checks.sh 2>&1) ||
		{ printf '%s\n' "$o" | tail -n 3; echo "case run-harness: the run failed"; cf=1; }
	n=$(printf '%s\n' $HALL | wc -l | tr -d ' ')
	case $o in
		*"gate: checks: all $n passed"*) ;;
		*) echo "case run-harness: no 'all $n passed' line"; cf=1 ;;
	esac
	o=$(cd "$d" && env GITHUB_EVENT_NAME=pull_request CI_CHECKS_GROUP=harness GATE_JOBS=2 sh scripts/ci-checks.sh 2>&1) ||
		{ printf '%s\n' "$o" | tail -n 3; echo "case run-pr: the run failed"; cf=1; }
	case $o in
		*"gate: checks: all 1 passed"*) ;;
		*) echo "case run-pr: no 'all 1 passed' line"; cf=1 ;;
	esac
	return $cf
}

if ! command -v git > /dev/null 2>&1; then
	bad "no git on PATH; the end-to-end cases were not run"
elif ! e2e_cases scripts/ci-checks.sh "$SEL" > "$tmp/e2e.out" 2>&1; then
	sed "s/^/$me: ci-checks.sh: /" "$tmp/e2e.out"
	fail=1
fi

# ---- (e) scripts/test.sh selects the harness checks as CI does -------------
# Each case is a scratch repository with a copy of scripts/test.sh, the real
# selector and map, an empty tests/check-*.sh for every name of
# tests/ci-checks.txt, of the gate's long-check list and check-zz-local.sh
# (a check the map does not name, as tests/ci-local-only.txt's are), a stub
# scheduler that records what test.sh gives it, and a make that does
# nothing; its main is its own origin/main and HEAD is the branch work.
GFIRST=$(sed -n 's/^gate_first="\(.*\)"$/\1/p' scripts/test.sh)
[ -n "$GFIRST" ] || bad "scripts/test.sh has no gate_first line"
# shellcheck disable=SC2086 # the names have no blank
GNAMES=$(printf '%s\n' $CI_NAMES $GFIRST check-zz-local.sh | sort -u | tr '\n' ' ')
GNAMES=${GNAMES% }
REALGIT=$(command -v git)
mkdir -p "$tmp/gbin" "$tmp/gitbad"
printf '#!/bin/sh\nexit 0\n' > "$tmp/gbin/make"
# a git that fails to list the untracked files and works otherwise
printf '#!/bin/sh\ncase " $* " in *" ls-files "*) exit 128 ;; esac\nexec "%s" "$@"\n' "$REALGIT" > "$tmp/gitbad/git"
cat > "$tmp/gstub" << 'EOF'
#!/bin/sh
# records what scripts/test.sh gives the scheduler; refuses a long-check
# word it is not given, as scripts/gate-checks.sh does
first=$2
shift 2
for w in $first; do
	case " $* " in *" tests/$w "*) ;; *) echo "stub scheduler: $w names no check" >&2; exit 2 ;; esac
done
{
	printf 'first %s\n' "$first"
	for t; do printf '%s\n' "${t#tests/}"; done
} > "$GATE_REC"
EOF
chmod +x "$tmp/gbin/make" "$tmp/gitbad/git" "$tmp/gstub"

# minus <list> <words>: the words of <list> that are not in <words>, in order
minus() {
	local n o=""
	for n in $1; do
		case " $2 " in *" $n "*) ;; *) o="$o $n" ;; esac
	done
	printf '%s' "${o# }"
}

# gt_repo <dir> <test.sh>: the scratch repository of a case
gt_repo() {
	local d=$1 n
	rm -rf "${d:?}" "$d.rec" "$d.out"
	mkdir -p "$d/scripts" "$d/tests" "$d/src"
	cp "$2" "$d/scripts/test.sh"
	cp "$SEL" "$d/scripts/ci-select.sh"
	cp "$MAP" "$d/tests/ci-map.txt"
	cp "$tmp/gstub" "$d/scripts/gate-checks.sh"
	for n in $GNAMES; do : > "$d/tests/$n"; done
	echo base > "$d/README.md"
	echo base > "$d/src/a.c"
	echo 'scratch/' > "$d/.gitignore"
	echo base > "$d/scripts/coverage.sh"
	g "$d" init -q -b main && g "$d" add -A && g "$d" commit -q -m base &&
		g "$d" update-ref refs/remotes/origin/main HEAD && g "$d" checkout -q -b work
}

# gt_run <dir> [<env>...]: runs that test.sh there; GR is what the scheduler
# was given (the checks on one line), GF its long checks, GO the output
gt_run() {
	local d=$1 st=0
	shift
	mkdir -p "$tmp/gtd"
	(cd "$d" && env -u GATE_ALL -u BENCH_LOCK_ENTRY -u BENCH_LOCK_TOKEN PATH="$tmp/gbin:$PATH" BENCH_LOCK=0 \
		TMPDIR="$tmp/gtd" GATE_REC="$d.rec" "$@" sh scripts/test.sh) > "$d.out" 2>&1 || st=$?
	GO=$(cat "$d.out")
	if [ "$st" -ne 0 ] || [ ! -f "$d.rec" ]; then
		GR="STATUS$st" GF=""
		return
	fi
	GF=$(sed -n 's/^first //p' "$d.rec")
	GR=$(grep -v '^first ' "$d.rec" | tr '\n' ' ' | sed 's/ $//')
}

# gt_left <label> <dir> <left out>: the run left out those harness checks
# and no other, gave the scheduler only the long checks that run, and said
# so on one line that names each one left out and no scratch path
gt_left() {
	local c=0 n line
	expect "$1" "$(minus "$GNAMES" "$3")" "$GR" || c=1
	expect "$1:first" "$(minus "$GFIRST" "$3")" "$GF" || c=1
	line=$(printf '%s\n' "$GO" | grep '^test\.sh: .*left out')
	if [ "$(printf '%s\n' "$GO" | grep -c '^test\.sh: .*left out')" -ne 1 ]; then
		echo "case $1: not one 'left out' line: $(printf '%s' "$GO" | tr '\n' ' ')"
		c=1
	fi
	for n in $3; do
		case " $line " in *" $n"[\ ,.]*) ;; *) echo "case $1: the line does not name $n: $line"; c=1 ;; esac
	done
	case $GO in *"$tmp"*) echo "case $1: the output holds a scratch path"; c=1 ;; esac
	gt_clean "$1" || c=1
	return $c
}

# gt_all <label> <dir> <reason>: the run ran every check, gave the scheduler
# every long check, and said why on one line, with no scratch path
gt_all() {
	local c=0
	expect "$1" "$GNAMES" "$GR" || c=1
	expect "$1:first" "$GFIRST" "$GF" || c=1
	if [ "$(printf '%s\n' "$GO" | grep -c "^test\\.sh: every check runs: .*$3")" -ne 1 ]; then
		echo "case $1: not one 'every check runs: ...$3' line: $(printf '%s' "$GO" | tr '\n' ' ')"
		c=1
	fi
	case $GO in *"$tmp"*) echo "case $1: the output holds a scratch path"; c=1 ;; esac
	gt_clean "$1" || c=1
	return $c
}

# gt_clean <label>: the run left nothing in its TMPDIR
gt_clean() {
	if [ -n "$(ls -A "$tmp/gtd")" ]; then
		echo "case $1: test.sh left its scratch directory"
		rm -rf "${tmp:?}/gtd"
		return 1
	fi
}

# gt_cases <test.sh> [<case>]: every case of (e), or the one named
gt_cases() {
	local t=$1 only=${2-} d=$tmp/gt cf=0 rg=check-require-guards.sh
	want() { [ -z "$only" ] || [ "$only" = "$1" ]; }
	mk() { gt_repo "$d" "$t" || { echo "case $1: cannot make the repository"; return 1; }; }
	# a change under src/ alone: the harness checks that read nothing
	# there are left out
	if want src-only; then
		mk src-only && echo y > "$d/src/a.c" && g "$d" commit -q -am src || return 1
		gt_run "$d"
		gt_left src-only "$d" "$(minus "$HALL" $rg)" || cf=1
	fi
	# each of the other three sources alone: a staged change (.gitignore,
	# read by check-metalfx-report only), an unstaged one
	# (scripts/coverage.sh: check-bench-entry, and scripts/* of
	# check-bounded-run and check-require-guards) and an untracked file
	# (Makefile: check-require-guards)
	if want staged; then
		mk staged && echo y >> "$d/.gitignore" && g "$d" add .gitignore || return 1
		gt_run "$d"
		gt_left staged "$d" "$(minus "$HALL" check-metalfx-report.sh)" || cf=1
	fi
	if want unstaged; then
		mk unstaged && echo y >> "$d/scripts/coverage.sh" || return 1
		gt_run "$d"
		gt_left unstaged "$d" "$(minus "$HALL" "check-bench-entry.sh check-bounded-run.sh $rg")" || cf=1
	fi
	if want untracked; then
		mk untracked && echo y > "$d/Makefile" || return 1
		gt_run "$d"
		gt_left untracked "$d" "$(minus "$HALL" $rg)" || cf=1
	fi
	# one untracked name each: a blank, a newline (whose second line, split
	# off, would be a .github/ path that runs every check), a leading "-"
	if want name-blank; then
		mk name-blank && echo y > "$d/src/a b.c" || return 1
		gt_run "$d"
		gt_left name-blank "$d" "$(minus "$HALL" $rg)" || cf=1
	fi
	if want name-newline; then
		mk name-newline && echo y > "$d/src/x
.github" || return 1
		gt_run "$d"
		gt_left name-newline "$d" "$(minus "$HALL" $rg)" || cf=1
	fi
	if want name-dash; then
		mk name-dash && echo y > "$d/src/-n" && echo y > "$d/src/--output=.github" || return 1
		gt_run "$d"
		gt_left name-dash "$d" "$(minus "$HALL" $rg)" || cf=1
	fi
	# a harness path changed: every check
	if want harness-path; then
		mk harness-path && echo y > "$d/src/a.c" && echo '# y' >> "$d/tests/check-bench.sh" &&
			g "$d" commit -q -am harness || return 1
		gt_run "$d"
		# (the selector chooses every harness check: none is left out)
		expect harness-path "$GNAMES" "$GR" || cf=1
		expect harness-path:first "$GFIRST" "$GF" || cf=1
		printf '%s\n' "$GO" | grep -q '^test\.sh: no harness check is left out' ||
			{ echo "case harness-path: no 'no harness check is left out' line"; cf=1; }
		gt_clean harness-path || cf=1
	fi
	# a stale origin/main: main changed scripts/coverage.sh after it, and
	# the lane holds that commit, sets the path back and changes src/; CI
	# sees both paths, so the checks that read scripts/coverage.sh run
	if want stale-base; then
		mk stale-base && g "$d" checkout -q main && echo B > "$d/scripts/coverage.sh" &&
			g "$d" commit -q -am main && g "$d" checkout -q work && g "$d" merge -q --ff-only main &&
			echo base > "$d/scripts/coverage.sh" && echo y > "$d/src/a.c" &&
			g "$d" commit -q -am lane || return 1
		gt_run "$d"
		gt_left stale-base "$d" "$(minus "$HALL" "check-bench-entry.sh check-bounded-run.sh $rg")" || cf=1
	fi
	# fail closed
	if want gate-all; then
		mk gate-all && echo y > "$d/src/a.c" && g "$d" commit -q -am src || return 1
		gt_run "$d" GATE_ALL=1
		gt_all gate-all "$d" "GATE_ALL=1" || cf=1
	fi
	if want no-origin; then
		mk no-origin && echo y > "$d/src/a.c" && g "$d" commit -q -am src &&
			g "$d" update-ref -d refs/remotes/origin/main || return 1
		gt_run "$d"
		gt_all no-origin "$d" "no origin/main" || cf=1
	fi
	if want no-base; then
		# origin/main an orphan: HEAD and it share no commit
		mk no-base && echo y > "$d/src/a.c" && g "$d" commit -q -am src &&
			g "$d" update-ref refs/remotes/origin/main "$(printf 'orphan\n' | g "$d" commit-tree "$(g "$d" mktree < /dev/null)")" || return 1
		gt_run "$d"
		gt_all no-base "$d" "no merge base" || cf=1
	fi
	if want git-error; then
		mk git-error && echo y > "$d/src/a.c" && g "$d" commit -q -am src || return 1
		gt_run "$d" PATH="$tmp/gitbad:$tmp/gbin:$PATH"
		gt_all git-error "$d" "git failed" || cf=1
	fi
	if want top-md; then
		# the selector chooses no harness check: every check, as a selector
		# that prints nothing never means none
		mk top-md && echo y > "$d/README.md" && g "$d" commit -q -am md || return 1
		gt_run "$d"
		gt_all top-md "$d" "chose no harness check" || cf=1
	fi
	# a selector that fails on the map, and one that on the changed paths
	# fails, prints a name it was not given, a name twice, or none
	local s
	for s in fails-map fails stranger twice none; do
		want "sel-$s" || continue
		mk "sel-$s" && echo y > "$d/src/a.c" && g "$d" commit -q -am src || return 1
		mv "$d/scripts/ci-select.sh" "$d/scripts/ci-select-real.sh"
		{
			printf '#!/bin/bash\n'
			[ "$s" = fails-map ] || printf '[ "$3" != - ] || exec bash "${0%%/*}/ci-select-real.sh" "$@"\n'
			case $s in
				fails*) printf 'exit 2\n' ;;
				stranger) printf 'echo check-nope.sh\n' ;;
				twice) printf 'echo check-bench.sh\necho check-bench.sh\n' ;;
				none) printf 'exit 0\n' ;;
			esac
		} > "$d/scripts/ci-select.sh"
		gt_run "$d"
		gt_all "sel-$s" "$d" "the selector" || cf=1
	done
	for s in empty missing; do
		want "map-$s" || continue
		mk "map-$s" && echo y > "$d/src/a.c" && g "$d" commit -q -am src || return 1
		if [ "$s" = empty ]; then : > "$d/tests/ci-map.txt"; else rm "$d/tests/ci-map.txt"; fi
		gt_run "$d"
		gt_all "map-$s" "$d" "map" || cf=1
	done
	return $cf
}

if ! command -v git > /dev/null 2>&1; then
	:
elif ! gt_cases scripts/test.sh > "$tmp/gtcases.out" 2>&1; then
	sed "s/^/$me: test.sh: /" "$tmp/gtcases.out"
	fail=1
fi

# ---- the mutants -----------------------------------------------------------
mutants=0
# swap <file> <old line> <new line>: replaces the one line equal to <old>
swap() {
	awk -v o="$2" -v n="$3" '$0 == o { print n; c++; next } { print } END { exit c != 1 }' "$1" > "$1.new" &&
		mv "$1.new" "$1"
}
# tree <dir>: a copy of what static_ok reads
tree() {
	mkdir -p "$1/tests" "$1/scripts" "$1/.github/workflows" "$1/.github/rulesets"
	cp tests/check-*.sh tests/ci-checks.txt tests/ci-local-only.txt "$MAP" "$1/tests/"
	cp "$SEL" "$1/scripts/"
	cp .github/workflows/macos.yml .github/workflows/codeql.yml "$1/.github/workflows/"
	cp .github/rulesets/main-protect.json "$1/.github/rulesets/"
}
# refused <name> <part> <text>: the mutant in $tmp/mut is refused by the
# part, with <text> in what it prints
refused() {
	local o st=0
	mutants=$((mutants + 1))
	case $2 in
		static) o=$(static_ok "$tmp/mut" 2>&1) || st=1 ;;
		sel) o=$(sel_cases "$tmp/mut/$SEL" "$MAP" 2>&1) || st=1 ;;
		e2e) o=$(e2e_cases "$tmp/mut/scripts/ci-checks.sh" "$SEL" 2>&1) || st=1 ;;
	esac
	if [ "$st" -eq 0 ]; then
		bad "mutant $1 passed"
	else
		case $o in
			*"$3"*) ;;
			*) bad "mutant $1 refused, but not for '$3': $(printf '%s' "$o" | head -n 3 | tr '\n' ' ')" ;;
		esac
	fi
	rm -rf "${tmp:?}/mut"
}
# mut <file> <old> <new>: a tree copy with one line of <file> swapped
mut() {
	rm -rf "${tmp:?}/mut"
	tree "$tmp/mut"
	cp scripts/ci-checks.sh "$tmp/mut/scripts/"
	[ -z "${1-}" ] || swap "$tmp/mut/$1" "$2" "$3" || bad "mutant: no single line '$2' in $1"
}

# a fail-open selector: a path no entry accounts for runs nothing
mut "$SEL" '	if [ "$acc" != 1 ]; then' '	if false; then'
refused fail-open sel "case unmapped:"
# a check in neither group
mut "$MAP" 'fast check-doc-links.sh' ''
refused neither static "tests/check-doc-links.sh is in neither group"
# a map entry naming a check that does not exist
mut "$MAP" 'fast check-doc-links.sh' 'fast check-doc-linkz.sh'
refused no-such-check static "names check-doc-linkz.sh, which is not a check"
# a harness check removed from the map
mut "" "" ""
awk '!/^harness check-bench\.sh /' "$tmp/mut/$MAP" > "$tmp/mut/m" && mv "$tmp/mut/m" "$tmp/mut/$MAP"
refused harness-removed static "check-bench.sh is not in the harness group"
# no map at all (the tree before TASK211)
mut "" "" ""
rm "$tmp/mut/$MAP"
refused no-map static "$MAP is missing"
# the changed paths split on blanks and newlines
mut "$SEL" "while IFS= read -r -d '' p; do" 'for p in $(tr "\0" "\n" < "$changed"); do'
refused word-split sel "case odd-names:"
# the fixed list left to the map
mut "$SEL" '			scripts/ci-checks.sh | scripts/ci-select.sh | .github/*)' '			scripts/ci-checks.sh | scripts/ci-select.sh)'
refused fixed-github sel "case fixed:.github/workflows/macos.yml:"
# an empty list, and one not ended by a NUL, run nothing
mut "$SEL" 'if [ "$done_all" != 1 ] && [ "$np" -eq 0 ]; then' 'if false; then'
refused empty sel "case empty:"
mut "$SEL" 'if [ "$done_all" != 1 ] && [ -n "$p" ]; then' 'if false; then'
refused unterminated sel "case unterminated:"
# letter case
mut "$SEL" 'shopt -s nocasematch' ':'
refused case-fold sel "case fold:"
# ci-checks.sh: the wrong parent, and a fallback that runs nothing
mut scripts/ci-checks.sh "			git diff --no-renames --no-relative --name-only -z 'HEAD^1' HEAD > \"\$tmp/changed\" 2> /dev/null; then" "			git diff --no-renames --no-relative --name-only -z 'HEAD^2' HEAD > \"\$tmp/changed\" 2> /dev/null; then"
refused wrong-parent e2e "case pr-merge:"
mut scripts/ci-checks.sh '		for n in $cands; do set -- "$@" "tests/$n"; done' '		:'
refused fallback-none e2e "case sel-fails:"
mut scripts/ci-checks.sh '	case " $* " in *" tests/$n "*) first="$first${first:+ }$n" ;; esac' '	first="$first${first:+ }$n"'
refused first-unfiltered e2e "case run-pr:"
# the workflow: a group that cancels across events, a matrix without harness
mut .github/workflows/macos.yml "  group: $GROUP" '  group: ${{ github.workflow }}'
refused group-workflow static "macos.yml: the concurrency group"
mut .github/workflows/codeql.yml "  group: $GROUP" '  group: ${{ github.workflow }}-${{ github.event.pull_request.number || github.sha }}'
refused group-codeql static "codeql.yml: the concurrency group"
mut .github/workflows/macos.yml '        group: [fast, harness, ios]' '        group: [fast, ios]'
refused matrix static "the matrix group is not"
mut .github/workflows/macos.yml '        CI_CHECKS_GROUP: ${{ matrix.group }}' '        CI_CHECKS_GROUP: fast'
refused group-env static "CI_CHECKS_GROUP is not"
mut .github/workflows/macos.yml '      fail-fast: false # a failing group stops no other' '      fail-fast: true'
refused fail-fast static "fail-fast is not false"

# scripts/test.sh (e): tmut <name> <case> <old line> <new line>: the gate
# with that one line swapped must fail the case
tmut() {
	local o st=0
	mutants=$((mutants + 1))
	cp scripts/test.sh "$tmp/test-mut.sh"
	if ! swap "$tmp/test-mut.sh" "$3" "$4"; then
		bad "mutant $1: no single line '$3' in scripts/test.sh"
		return
	fi
	o=$(gt_cases "$tmp/test-mut.sh" "$2" 2>&1) || st=1
	if [ "$st" -eq 0 ]; then
		bad "mutant $1 passed"
	else
		case $o in
			*"case $2"*) ;;
			*) bad "mutant $1 refused, but not by case $2: $(printf '%s' "$o" | head -n 3 | tr '\n' ' ')" ;;
		esac
	fi
}
if command -v git > /dev/null 2>&1; then
	# fail open: a missing merge base runs no harness check
	tmut no-base-open no-base '	gate_why="no merge base of HEAD and origin/main"' '	gate_skip=$gate_hall'
	tmut gate-all-ignored gate-all 'if [ "${GATE_ALL:-}" = 1 ]; then' 'if false; then'
	tmut git-error-ignored git-error '	gate_why="git failed to list the changed paths"' '	:'
	tmut none-trusted top-md '	gate_why="the selector chose no harness check"' '	:'
	tmut stranger-trusted sel-stranger '	! gate_hsel=$(gate_list "$gate_tmp/sel" "$gate_hall"); then' '	! gate_hsel=$(cat "$gate_tmp/sel"); then'
	tmut no-untracked untracked '		git ls-files -z --others --exclude-standard --full-name' '		:'
	tmut no-staged staged '		git diff --cached --no-renames --no-relative --no-ext-diff --no-color --name-only -z -- &&' '		: &&'
	tmut no-unstaged unstaged '		git diff --no-renames --no-relative --no-ext-diff --no-color --name-only -z -- &&' '		: &&'
	tmut newline-delimited name-newline '		git ls-files -z --others --exclude-standard --full-name' '		git ls-files --others --exclude-standard --full-name | tr "\n" "\0"'
	tmut first-unfiltered src-only 'gate_first=$gate_keep' ':'
fi

if [ "$fail" -ne 0 ]; then
	echo "$me: FAILED"
	exit 1
fi
echo "$me: ok ($(wc -l < "$tmp/s.map" | tr -d ' ') checks in the map, $mutants mutants refused)"
