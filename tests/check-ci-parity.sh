#!/bin/sh
# Invariant (FIX115, review findings AR-01 and AR-02): CI runs the gate's
# invariants, and a check that cannot run says so as a failure wherever it is
# required to run.
#
# (a) Every tests/check-*.sh is named in exactly one of two lists:
#     tests/ci-checks.txt, the checks the invariants job of
#     .github/workflows/macos.yml runs (through scripts/ci-checks.sh), one
#     name per line; or tests/ci-local-only.txt, the checks that run on the
#     build host's gate only, one "name: reason" per line, the reason not
#     empty. Every name in a list is an existing check, and is listed once.
#     So a new check is refused until it is placed in one of them.
# (b) A job of .github/workflows/macos.yml runs scripts/ci-checks.sh, and
#     every job that does checks out the full history (fetch-depth: 0):
#     tests/check-cvar-docs.sh reads the upstream base commit and fails on a
#     shallow clone. That job sets CHECK_IOS_REQUIRE to 1.
# (c) Every skip line of a tests/check-*.sh is paired with a <NAME>_REQUIRE
#     variable that turns the skip into a failure. A skip line is an echo or
#     printf, at the start of a statement and not in a comment, whose text
#     holds "skipped:" (the gate's convention for a part that a missing host
#     tool leaves out), or holds "skip" with "exit 0" on the same line or the
#     next one. It is paired when that line or one of the three before it
#     names a variable ending in _REQUIRE.
#
# Then a self-test on scratch copies of those files (never the real tree):
# the copy passes, and each mutant is refused: an unlisted check, a listed
# check with a skip and no _REQUIRE, a local-only entry with no reason, a
# name listed twice, and a shallow checkout in the invariants job.
#
# Needs only sh, awk, sed, grep and cp. Usage: tests/check-ci-parity.sh
set -eu
me=check-ci-parity
cd "$(dirname "$0")/.."

# list_names <file>: the names of a list, without comments and blank lines
list_names() {
	sed -e 's/#.*//' -e 's/:.*//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' "$1" | grep -v '^$' || :
}

# skips <file>: "<line>: <text>" for every skip line with no _REQUIRE pair
skips() {
	awk '
		{ l[NR] = $0 }
		END {
			st = "(^|[;{]|then|else|do|[|][|]|&&)[ \t]*(echo|printf)[ \t]"
			for (i = 1; i <= NR; i++) {
				s = l[i]
				if (s ~ /^[ \t]*#/) continue
				if (s !~ st) continue
				hit = 0
				if (s ~ /skipped:/) hit = 1
				else if (s ~ /skip/ && (s ~ /exit 0/ || l[i + 1] ~ /^[ \t]*exit 0/)) hit = 1
				if (!hit) continue
				ok = 0
				for (j = i; j >= i - 3 && j >= 1; j--)
					if (l[j] ~ /[A-Z0-9_]_REQUIRE/) ok = 1
				if (!ok) { sub(/^[ \t]*/, "", s); print i ": " s }
			}
		}' "$1"
}

# jobs_ok <workflow>: the invariants job is there, deep and requires iOS
jobs_ok() {
	awk '
		function finish() {
			if (job != "" && run) {
				runs++
				if (!depth) { print "job " job " runs scripts/ci-checks.sh on a shallow checkout (no fetch-depth: 0)"; bad = 1 }
				if (!ios) { print "job " job " runs scripts/ci-checks.sh without CHECK_IOS_REQUIRE: 1"; bad = 1 }
			}
			job = ""; run = 0; depth = 0; ios = 0
		}
		/^jobs:/ { injobs = 1; next }
		injobs && /^  [A-Za-z0-9_-]+:[ \t]*$/ { finish(); job = $1; sub(/:$/, "", job); next }
		injobs && /^[^ \t#]/ { finish(); injobs = 0 }
		/^[ \t]*#/ { next }
		job != "" && /scripts\/ci-checks\.sh/ { run = 1 }
		job != "" && /^[ \t]+fetch-depth:[ \t]*0[ \t]*(#.*)?$/ { depth = 1 }
		job != "" && /^[ \t]+CHECK_IOS_REQUIRE:[ \t]*["\047]?1["\047]?[ \t]*(#.*)?$/ { ios = 1 }
		END {
			finish()
			if (!runs) { print "no job runs scripts/ci-checks.sh"; bad = 1 }
			exit bad
		}' "$1"
}

# parity <root>: prints every problem of the tree at <root>; fails on any
parity() {
	_r=$1 _pf=0
	_ci=$_r/tests/ci-checks.txt _lo=$_r/tests/ci-local-only.txt
	for _l in "$_ci" "$_lo"; do
		[ -f "$_l" ] || { echo "$me: ${_l#"$_r"/} is missing"; return 1; }
	done
	# a local-only entry has a reason
	_nr=$(sed -e 's/#.*//' "$_lo" | awk -F: 'NF && $0 !~ /^[ \t]*$/ { r = $0; sub(/^[^:]*:?/, "", r); gsub(/[ \t]/, "", r); if (index($0, ":") == 0 || r == "") print $1 }')
	for _n in $_nr; do
		echo "$me: tests/ci-local-only.txt: $_n has no reason (name: reason)"
		_pf=1
	done
	# every listed name is a check, and is listed once
	for _n in $( (list_names "$_ci"; list_names "$_lo") | sort | uniq -d); do
		echo "$me: $_n is listed more than once in tests/ci-checks.txt and tests/ci-local-only.txt"
		_pf=1
	done
	for _n in $(list_names "$_ci") $(list_names "$_lo"); do
		case $_n in
			check-*.sh) [ -f "$_r/tests/$_n" ] || { echo "$me: $_n is listed but tests/$_n does not exist"; _pf=1; } ;;
			*) echo "$me: '$_n' in a list is not a tests/check-*.sh name"; _pf=1 ;;
		esac
	done
	# every check is listed
	for _f in "$_r"/tests/check-*.sh; do
		[ -f "$_f" ] || continue
		_n=${_f##*/}
		if ! (list_names "$_ci"; list_names "$_lo") | grep -qxF "$_n"; then
			echo "$me: tests/$_n is neither in tests/ci-checks.txt (run in CI) nor in tests/ci-local-only.txt (with a reason)"
			_pf=1
		fi
		[ "$_n" != "$me.sh" ] || continue
		_s=$(skips "$_f")
		if [ -n "$_s" ]; then
			printf '%s\n' "$_s" | sed "s|^|$me: tests/$_n:|; s|\$| (a skip with no <NAME>_REQUIRE that fails it)|"
			_pf=1
		fi
	done
	_wf=$_r/.github/workflows/macos.yml
	if [ ! -f "$_wf" ]; then
		echo "$me: .github/workflows/macos.yml is missing"
		_pf=1
	elif ! _jo=$(jobs_ok "$_wf"); then
		printf '%s\n' "$_jo" | sed "s|^|$me: .github/workflows/macos.yml: |"
		_pf=1
	fi
	return $_pf
}

fail=0
if ! parity .; then
	fail=1
fi

# the self-test, on scratch copies
tmp=$(mktemp -d "${TMPDIR:-/tmp}/ci-parity.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT
mutants=0
# copy <dir>: the files parity reads, copied to <dir>
copy() {
	mkdir -p "${1:?}/tests" "$1/.github/workflows"
	cp tests/check-*.sh tests/ci-checks.txt tests/ci-local-only.txt "$1/tests/"
	cp .github/workflows/macos.yml "$1/.github/workflows/"
}
# refused <name> <expected text>: the mutant in $tmp/<name> is refused, with
# a line that holds <expected text>
refused() {
	mutants=$((mutants + 1))
	if parity "$tmp/$1" > "$tmp/$1.out" 2>&1; then
		echo "$me: selftest: mutant $1 passed"
		fail=1
	elif ! grep -qF "$2" "$tmp/$1.out"; then
		echo "$me: selftest: mutant $1 was refused, but no line says '$2':"
		sed 's/^/  | /' "$tmp/$1.out"
		fail=1
	fi
}
if [ "$fail" = 0 ]; then
	copy "$tmp/base"
	if ! parity "$tmp/base" > "$tmp/base.out" 2>&1; then
		echo "$me: selftest: the unchanged copy is refused:"
		sed 's/^/  | /' "$tmp/base.out"
		fail=1
	fi

	copy "$tmp/unlisted"
	printf '#!/bin/sh\necho ok\n' > "$tmp/unlisted/tests/check-x.sh"
	refused unlisted "tests/check-x.sh is neither in"

	copy "$tmp/skip"
	printf '#!/bin/sh\nif ! command -v zz >/dev/null 2>&1; then\n\techo "check-y: %s: no zz"\n\texit 0\nfi\n' skipped > "$tmp/skip/tests/check-y.sh"
	echo check-y.sh >> "$tmp/skip/tests/ci-checks.txt"
	refused skip "tests/check-y.sh:3:"

	copy "$tmp/skip0"
	printf '#!/bin/sh\n[ -x /zz ] || { echo "check-y: zz not found, %s"; exit 0; }\n' skipped > "$tmp/skip0/tests/check-y.sh"
	echo check-y.sh >> "$tmp/skip0/tests/ci-checks.txt"
	refused skip0 "tests/check-y.sh:2:"

	copy "$tmp/noreason"
	printf '#!/bin/sh\necho ok\n' > "$tmp/noreason/tests/check-x.sh"
	echo 'check-x.sh:' >> "$tmp/noreason/tests/ci-local-only.txt"
	refused noreason "check-x.sh has no reason"

	copy "$tmp/twice"
	list_names tests/ci-checks.txt | head -n 1 | sed 's/$/: listed twice/' >> "$tmp/twice/tests/ci-local-only.txt"
	refused twice "listed more than once"

	copy "$tmp/shallow"
	grep -v 'fetch-depth: 0' .github/workflows/macos.yml > "$tmp/shallow/.github/workflows/macos.yml"
	refused shallow "on a shallow checkout"

	copy "$tmp/noios"
	grep -v 'CHECK_IOS_REQUIRE: ' .github/workflows/macos.yml > "$tmp/noios/.github/workflows/macos.yml"
	refused noios "without CHECK_IOS_REQUIRE"

	copy "$tmp/norun"
	grep -v 'scripts/ci-checks.sh' .github/workflows/macos.yml > "$tmp/norun/.github/workflows/macos.yml"
	refused norun "no job runs scripts/ci-checks.sh"
fi

if [ "$fail" != 0 ]; then
	exit 1
fi
echo "$me: ok ($(list_names tests/ci-checks.txt | wc -l | tr -d ' ') checks in CI, $(list_names tests/ci-local-only.txt | wc -l | tr -d ' ') local only; $mutants mutants refused)"
