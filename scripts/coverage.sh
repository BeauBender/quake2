#!/bin/sh
# Line coverage of the engine code the unit-test suite links (tests/unit,
# see doc/085_unittests.md), with a floor that only ratchets up.
#
# Builds an instrumented copy of the test binary under build/coverage
# (make COVERAGE=1), runs the suite, and prints line coverage per source
# directory, per top-level directory and in total. Uses only what the
# compiler's own toolchain ships, nothing is installed or downloaded:
#   clang on macOS:  -fprofile-instr-generate -fcoverage-mapping,
#                    xcrun llvm-profdata, xcrun llvm-cov
#   clang elsewhere: the same, llvm-profdata / llvm-cov from PATH
#                    (optionally with clang's major version as suffix)
#   gcc:             --coverage and the gcov that comes with gcc
# The script fails with a message when a tool is missing; it never skips.
#
# The floor is scripts/coverage-floor, one line per tool ("llvm-cov N",
# "gcov N"), because the two count lines differently. The script exits 1
# when the total is below the floor for the tool in use, and prints the
# new value to record when the total has risen by a whole percent or
# more.
#
# Usage: scripts/coverage.sh            (from anywhere)
#        CC=gcc scripts/coverage.sh     (pick the compiler, as for make)
set -eu

cd "$(dirname "$0")/.."

jobs=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
cc=${CC:-cc}
outdir=build/coverage
floorfile=scripts/coverage-floor

fail() {
	echo "coverage: $*" >&2
	exit 1
}

if "$cc" -v 2>&1 | grep -q "clang version"; then
	tool=llvm-cov
	if [ "$(uname -s)" = Darwin ]; then
		profdata=$(xcrun -f llvm-profdata 2>/dev/null) || fail "xcrun cannot find llvm-profdata (install the Xcode command line tools)"
		llvmcov=$(xcrun -f llvm-cov 2>/dev/null) || fail "xcrun cannot find llvm-cov (install the Xcode command line tools)"
	else
		major=$("$cc" -dumpversion | cut -d. -f1)
		profdata=
		llvmcov=
		for suffix in "" "-$major"; do
			if [ -z "$profdata" ] && command -v "llvm-profdata$suffix" >/dev/null 2>&1; then
				profdata=$(command -v "llvm-profdata$suffix")
			fi
			if [ -z "$llvmcov" ] && command -v "llvm-cov$suffix" >/dev/null 2>&1; then
				llvmcov=$(command -v "llvm-cov$suffix")
			fi
		done
		[ -n "$profdata" ] || fail "no llvm-profdata or llvm-profdata-$major in PATH for $cc"
		[ -n "$llvmcov" ] || fail "no llvm-cov or llvm-cov-$major in PATH for $cc"
	fi
elif "$cc" -v 2>&1 | grep -qE "gcc version|gcc-Version"; then
	tool=gcov
	# gcov must match the compiler: gcc-13 pairs with gcov-13.
	case $(basename "$cc") in
		*gcc*) gcov=$(echo "$cc" | sed 's/gcc\([^/]*\)$/gcov\1/') ;;
		*) gcov=gcov ;;
	esac
	command -v "$gcov" >/dev/null 2>&1 || fail "no $gcov for $cc"
else
	fail "unknown compiler '$cc': need clang or gcc"
fi

[ -r "$floorfile" ] || fail "$floorfile is missing"
floor=$(awk -v t="$tool" '$1 == t { print $2 }' "$floorfile")
[ -n "$floor" ] || fail "$floorfile has no floor for $tool"

# Fresh counters: stale profiles from an earlier run would inflate the result.
rm -rf "$outdir/profiles"
mkdir -p "$outdir/profiles"
if [ -d "$outdir" ]; then
	find "$outdir" -name '*.gcda' -exec rm -f {} +
fi

# -B for the same reason as in scripts/test.sh: no header dependencies
# on Darwin. %m merges the profiles of the forked children (t_expect_exit)
# into the parent's file.
LLVM_PROFILE_FILE="$PWD/$outdir/profiles/yq2test-%m.profraw" \
	make -B -j"$jobs" COVERAGE=1 CC="$cc" test

# Per-file "<path> <lines> <missed>" for engine sources (not tests/).
summary="$outdir/profiles/files.txt"
if [ "$tool" = llvm-cov ]; then
	"$profdata" merge -sparse -o "$outdir/profiles/merged.profdata" "$outdir"/profiles/*.profraw
	"$llvmcov" export -format=lcov -summary-only "$outdir/yq2test" \
		-instr-profile="$outdir/profiles/merged.profdata" \
		-ignore-filename-regex='(^|/)tests/' |
		awk -v root="$PWD/" '
			/^SF:/ { f = substr($0, 4); if (index(f, root) == 1) f = substr(f, length(root) + 1) }
			/^LF:/ { n = substr($0, 4) }
			/^LH:/ { if (f ~ /^src\/.*\.c$/) print f, n, n - substr($0, 4) }' > "$summary"
else
	: > "$summary"
	for gcda in $(find "$outdir/src" -name '*.gcda' | sort); do
		obj=${gcda%.gcda}
		src=${obj#"$outdir"/}.c
		"$gcov" -n -o "$(dirname "$gcda")" "$src" 2>/dev/null |
			awk -v want="$src" '
				/^File / { f = $2; gsub(/\047/, "", f); next }
				/^Lines executed:/ && f == want {
					split($2, a, ":"); pct = a[2]; sub(/%/, "", pct)
					n = $NF
					hit = int(pct * n / 100 + 0.5)
					print f, n, n - hit
					f = ""
				}' >> "$summary"
	done
fi

[ -s "$summary" ] || fail "no coverage data was produced"

echo
echo "Line coverage of the code linked into the unit-test binary ($tool):"
awk '
	function dir(p) { sub(/\/[^\/]*$/, "", p); return p }
	function top(p,  a) { split(p, a, "/"); return a[1] "/" a[2] }
	{
		d = dir($1); t = top($1)
		lines[d] += $2; missed[d] += $3
		tl[t] += $2; tm[t] += $3
		all += $2; allm += $3
	}
	function pct(l, m) { return l ? 100 * (l - m) / l : 0 }
	END {
		printf "  %-28s %8s %8s %8s\n", "directory", "lines", "covered", "percent"
		n = 0
		for (d in lines) keys[n++] = d
		for (i = 0; i < n; i++)
			for (j = i + 1; j < n; j++)
				if (keys[j] < keys[i]) { s = keys[i]; keys[i] = keys[j]; keys[j] = s }
		for (i = 0; i < n; i++)
			printf "  %-28s %8d %8d %7.2f%%\n", keys[i], lines[keys[i]], lines[keys[i]] - missed[keys[i]], pct(lines[keys[i]], missed[keys[i]])
		print "  by top-level directory:"
		n = 0
		for (d in tl) tk[n++] = d
		for (i = 0; i < n; i++)
			for (j = i + 1; j < n; j++)
				if (tk[j] < tk[i]) { s = tk[i]; tk[i] = tk[j]; tk[j] = s }
		for (i = 0; i < n; i++)
			printf "  %-28s %8d %8d %7.2f%%\n", tk[i] "/**", tl[tk[i]], tl[tk[i]] - tm[tk[i]], pct(tl[tk[i]], tm[tk[i]])
		printf "TOTAL %.2f%% of %d lines\n", pct(all, allm), all
	}' "$summary" | tee "$outdir/profiles/report.txt"

total=$(awk '/^TOTAL/ { sub(/%/, "", $2); print $2 }' "$outdir/profiles/report.txt")

if awk -v t="$total" -v f="$floor" 'BEGIN { exit !(t < f) }'; then
	echo "coverage: FAIL: total $total% is below the floor of $floor% ($tool) in $floorfile" >&2
	exit 1
fi

if awk -v t="$total" -v f="$floor" 'BEGIN { exit !(int(t) > f) }'; then
	echo "coverage: total $total% is above the floor of $floor%; ratchet it up:"
	echo "coverage:   set the '$tool' line in $floorfile to $(awk -v t="$total" 'BEGIN { print int(t) }')"
fi

echo "coverage: ok ($total% >= floor $floor%, $tool)"
