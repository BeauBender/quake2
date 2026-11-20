#!/bin/sh
# Invariant (FIX116, review finding AR-03): the unit suite (tests/unit, see
# doc/085_unittests.md) runs under AddressSanitizer and
# UndefinedBehaviorSanitizer on every gate and every CI run, so an
# out-of-bounds access or undefined behaviour in a hostile-input case fails
# here even when the case's output, and so its pin, happens to match.
#
# The build is the Makefile's own: "make ASAN=1 UBSAN=1 test", in a scratch
# copy of Makefile, third_party/sdl3.make, src/, tests/unit/ and
# tests/pins/unit/ (third_party/SDL3 is linked, not copied; the suite does
# not build it), so build/, release/ and debug/ of this checkout, and the
# objects of "make test", are not touched, and no config.mk of the checkout
# changes the flags. UPDATE_PINS= TEST_ARGS= on the make command line: it
# never rewrites a pin and always runs the whole suite. CFLAGS, CC and the
# sanitizer runtime options from the caller's environment are dropped.
#
# How a report fails the check:
# - UBSan: ASAN=1 UBSAN=1 builds every test object with
#   -fno-sanitize-recover=undefined (the Makefile's TEST_CFLAGS), so the
#   first report ends the run. The check reads the binary's symbols and
#   fails unless it calls the ASan runtime (__asan_init) and the aborting
#   UBSan handlers (__ubsan_handle_*_abort), so a Makefile that dropped
#   either flag would be seen.
# - The runtime options are set here, not taken from the caller:
#   halt_on_error=1 (stop at the first report) and abort_on_error=1 (end
#   by SIGABRT, a non-zero status whatever exitcode is) for both,
#   print_stacktrace=1 for UBSan.
# - LeakSanitizer is off (detect_leaks=0): it is not supported by the ASan
#   runtime on macOS on Apple silicon (an explicit detect_leaks=1 is
#   refused there with "detect_leaks is not supported on this platform"
#   and the run aborts, status 134, on macOS 27.2 arm64), so detect_leaks=0
#   is what that runtime does anyway, written out so a platform that has
#   LSan runs the same check. Whether the suite is leak-free there is not
#   known and is not what this check is for. Nothing else is relaxed.
# - log_path: both runtimes write their reports to files in the check's
#   scratch tree (asan.<pid>, ubsan.<pid>), and after the run the check
#   fails if any exists and prints the first one. ASan and UBSan share one
#   runtime in this build, and the last log_path set (UBSan's) wins for
#   every report, so an ASan report can land in a ubsan.<pid> file. A
#   forked test child that dies by a signal fails its test; its report is
#   in the log file, not in the test output.
# - A run that exits 0 but whose output still holds a sanitizer report
#   ("runtime error:", "ERROR: AddressSanitizer",
#   "SUMMARY: UndefinedBehaviorSanitizer") fails too.
#
# A missing make, cc or nm, a build that fails, or a binary that is not
# written fails the check. The first sanitizer report and the end of the
# run are printed. How to run it alone and the measured time:
# docs/runbook/FIX116.md.
set -eu
cd "$(dirname "$0")/.."

unset MAKEFLAGS GNUMAKEFLAGS MFLAGS MAKEFILES CFLAGS LDFLAGS CC ASAN_OPTIONS UBSAN_OPTIONS LSAN_OPTIONS

for t in make cc nm; do
	if ! command -v "$t" > /dev/null 2>&1; then
		echo "check-unit-sanitizers: $t not found (the system toolchain is required)"
		exit 1
	fi
done

tmp=$(mktemp -d "${TMPDIR:-/tmp}/unit-sanitizers.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT

tree="${tmp:?}/tree"
mkdir -p "${tree:?}/third_party" "${tree:?}/tests/pins" "${tmp:?}/run"
cp -R Makefile src "${tree:?}/"
cp third_party/sdl3.make "${tree:?}/third_party/"
ln -s "$PWD/third_party/SDL3" "${tree:?}/third_party/SDL3"
cp -R tests/unit "${tree:?}/tests/"
cp -R tests/pins/unit "${tree:?}/tests/pins/"

jobs=$(sysctl -n hw.ncpu 2> /dev/null || nproc 2> /dev/null || echo 4)
case $jobs in '' | *[!0-9]*) jobs=4 ;; esac

ASAN_OPTIONS=halt_on_error=1:abort_on_error=1:detect_leaks=0:log_path="${tmp:?}/run/asan"
UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1:log_path="${tmp:?}/run/ubsan"
TMPDIR="${tmp:?}/run"
export ASAN_OPTIONS UBSAN_OPTIONS TMPDIR

bin="${tree:?}/build/debug/test/yq2test"
t0=$(date +%s)
rc=0
make -C "${tree:?}" -j"$jobs" ASAN=1 UBSAN=1 UPDATE_PINS= TEST_ARGS= test > "${tmp:?}/out" 2>&1 || rc=$?
t1=$(date +%s)

if [ ! -x "$bin" ]; then
	tail -n 30 "${tmp:?}/out"
	echo "check-unit-sanitizers: the build did not write $(basename "$bin") (make status $rc)"
	exit 1
fi

nm "$bin" > "${tmp:?}/syms" 2> "${tmp:?}/syms.err" || { cat "${tmp:?}/syms.err"; echo "check-unit-sanitizers: nm failed"; exit 1; }
if ! grep -q '__asan_init' "${tmp:?}/syms"; then
	echo "check-unit-sanitizers: the test binary is not built with AddressSanitizer (no __asan_init)"
	exit 1
fi
if ! grep -q '__ubsan_handle_.*_abort' "${tmp:?}/syms"; then
	echo "check-unit-sanitizers: the test binary has no aborting UBSan handler (built without -fsanitize=undefined -fno-sanitize-recover)"
	exit 1
fi

# A report from a forked test child, or from any process whose output does not
# reach the make log, lands in a log_path file (FIX142).
logf=$(ls "${tmp:?}"/run/asan.* "${tmp:?}"/run/ubsan.* 2> /dev/null | head -n 1 || :)
if [ -n "$logf" ]; then
	echo "check-unit-sanitizers: a sanitizer log file was written: $(basename "$logf")"
	head -n 60 "$logf"
	echo "check-unit-sanitizers: FAILED: make ASAN=1 UBSAN=1 test, status $rc, in $((t1 - t0)) s"
	exit 1
fi

report=$(grep -m 1 -E 'runtime error:|ERROR: AddressSanitizer|SUMMARY: (Address|UndefinedBehavior)Sanitizer' "${tmp:?}/out" || :)
if [ "$rc" -ne 0 ] || [ -n "$report" ]; then
	[ -z "$report" ] || echo "check-unit-sanitizers: first report: $report"
	grep -m 1 -E '^SUMMARY: ' "${tmp:?}/out" || :
	tail -n 40 "${tmp:?}/out"
	echo "check-unit-sanitizers: FAILED: make ASAN=1 UBSAN=1 test, status $rc, in $((t1 - t0)) s"
	exit 1
fi

tail -n 1 "${tmp:?}/out"
echo "check-unit-sanitizers: ok: the unit suite passed under ASan and UBSan in $((t1 - t0)) s"
