# scripts/bench/core/stats.sh: statistics, the drift rule and the verdict.
#
# All arithmetic is awk under LC_ALL=C (bench.sh sets it), on decimal
# numbers (no exponent). Values are sorted ascending; then
#   median  the middle value; the mean of the two middle values for an even
#           count
#   p90     nearest rank: the value at rank ceil(0.9 n), 1-based, computed
#           in integers as int((9 n + 9) / 10); n = 1 gives the value itself
#   min/max the first and the last value
# and a number is printed with at most 4 decimals, trailing zeros dropped.
#
# The drift rule, for one new run value x against the stored run values S:
#   t = max(abs_tol, rel_tol * |median(S)|)
#   inside when min(S) - t <= x <= max(S) + t (both bounds inclusive, with
#   a slack of 1e-9 of the bound's size for decimal rounding)

BENCH_AWK_LIB='
function fmt(x,  s) {
	s = sprintf("%.4f", x)
	sub(/0+$/, "", s)
	sub(/\.$/, "", s)
	if (s == "-0") s = "0"
	return s
}
function sortv(n,  i, j, x) {
	for (i = 2; i <= n; i++) {
		x = v[i]
		for (j = i - 1; j >= 1 && v[j] > x; j--) v[j + 1] = v[j]
		v[j + 1] = x
	}
}
function median(n) {
	if (n % 2) return v[(n + 1) / 2]
	return (v[n / 2] + v[n / 2 + 1]) / 2
}
function slack(b) { return 1e-9 * (b < 0 ? (b < -1 ? -b : 1) : (b > 1 ? b : 1)) }
/^[ \t]*$/ { next }
!/^-?[0-9]+(\.[0-9]+)?$/ { printf "bench: not a decimal number on line %d\n", NR > "/dev/stderr"; bad = 1; exit 2 }
{ v[++n] = $0 + 0 }
'

# bench_stats: reads numbers, one per line; prints
# "n=<n> median=<m> p90=<p> min=<a> max=<b>". Exit 2 on no numbers or on a
# line that is not one.
bench_stats() {
	awk "$BENCH_AWK_LIB"'
	END {
		if (bad) exit 2
		if (n == 0) { print "bench: no values" > "/dev/stderr"; exit 2 }
		sortv(n)
		r = int((9 * n + 9) / 10)
		printf "n=%d median=%s p90=%s min=%s max=%s\n", n, fmt(median(n)), fmt(v[r]), fmt(v[1]), fmt(v[n])
	}'
}

# bench_drift_rule <x> <abs_tol> <rel_tol>: reads the stored run values;
# prints "drift=inside|outside value=<x> lo=<lo> hi=<hi> t=<t> median=<m>
# runs=<n>".
bench_drift_rule() {
	awk -v x="$1" -v a="$2" -v r="$3" "$BENCH_AWK_LIB"'
	END {
		if (bad) exit 2
		if (n == 0) { print "bench: no stored runs" > "/dev/stderr"; exit 2 }
		sortv(n)
		mn = v[1]; mx = v[n]; med = median(n)
		t = a + 0
		m = med < 0 ? -med : med
		if (r * m > t) t = r * m
		lo = mn - t; hi = mx + t
		x += 0
		inside = (x >= lo - slack(lo) && x <= hi + slack(hi))
		printf "drift=%s value=%s lo=%s hi=%s t=%s median=%s runs=%d\n", inside ? "inside" : "outside", fmt(x), fmt(lo), fmt(hi), fmt(t), fmt(med), n
	}'
}

# bench_verdict <metric> <better> <accept> <before median> <after median>:
# prints the verdict line; exit 1 when it fails.
bench_verdict() {
	awk -v m="$1" -v better="$2" -v acc="$3" -v b="$4" -v a="$5" 'BEGIN {
		if (b + 0 == 0) {
			printf "verdict=undefined metric=%s better=%s before=%s after=%s change=none accept=%s%%\n", m, better, b, a, acc
			exit 1
		}
		ch = (a - b) / (b < 0 ? -b : b) * 100
		imp = better == "lower" ? -ch : ch
		pass = (imp >= acc - 1e-9)
		printf "verdict=%s metric=%s better=%s before=%s after=%s change=%+.2f%% accept=%s%%\n", pass ? "pass" : "fail", m, better, b, a, ch, acc
		exit !pass
	}'
}
