#!/bin/sh
# Invariant (TASK167): the vt backend of upscale_textures
# (src/client/cl_upscale_vt.m, VideoToolbox's super-resolution scaler)
# scales only inside a run, reads as unsupported with its reason when
# isSupported says no (and the menu's check then gives that reason),
# and where the model is on the machine scales a synthetic tile at 2x
# and 4x to the same bytes twice, with alpha 255 everywhere, which is
# why CL_UpscaleRGBA sets the alpha from the indices, and keeps the
# colour of flat tiles (TASK167 round 1: a half-float copy that read
# the bytes back near white). The menu's check greys by the factor
# only, remembering nothing a run found. It never fetches the model:
# without it the scaling cases say they skipped. Before building, it
# checks that nothing but the menu item (src/client/menu/menu.c) can
# start the model's fetch (decision 0013), and proves that check on
# mutants: the fetch guard below says exactly what it covers.
#
# It builds tests/metal/upscale_vt_test.m with the backend file alone
# and the test-only switch UPSCALE_VT_TEST (with ASAN and UBSAN where the
# compiler has them), and runs it under a time bound. What a run does
# with the checks (CL_UpscaleVTState) and the forced fallback line are
# unit tests (tests/unit/test_cl_upscale.c). Only on macOS: elsewhere
# the backend is not built and the check says it skipped.
#
# UPSCALE_VT_REQUIRE=1 (opt in) makes "unsupported here" and "the model
# is not on this machine" fail the check: set it on a host known to have
# the model, so a run that quietly tested nothing is red.
# UPSCALE_VT_CHECK_SECONDS bounds the test's run (default 120).

set -u

cd "$(dirname "$0")/.." || exit 1

tmp=$(mktemp -d "${TMPDIR:-/tmp}/upscale-vt.XXXXXX") || exit 1
trap 'chmod -R u+rwX "${tmp:?}" 2>/dev/null; rm -rf "${tmp:?}"' EXIT

# The fetch guard. The model's download starts from the menu item only
# (decision 0013), so no command, stuffed text among them, can start one.
# What it proves, over every file under src:
# - the bare word CL_UpscaleVTFetchStart (a call, a pointer to it, a macro
#   alias, a call split across lines or at column 0, a comment) appears
#   only in src/client/menu/menu.c, and else once in each of three files
#   as an exact line: the declaration in header/upscale_vt.h, the
#   definition in cl_upscale_vt.m and the stub in cl_upscale_cmd.c;
# - downloadConfigurationModel appears exactly once under src, inside the
#   body of CL_UpscaleVTFetchStart in cl_upscale_vt.m (from its
#   definition line to the first "}" at column 0);
# - a tool that fails (grep, awk, wc) fails the guard: it never reads as
#   "no call".
# It does not prove what menu.c does with the name, nor anything outside
# src. fetch_guard DIR runs it on DIR/src and returns 0 (clean), 1 (a
# finding, printed with paths relative to DIR) or 2 (a tool failed).
fg_decl='int CL_UpscaleVTFetchStart(int factor, char *reason, size_t size);'
fg_def='CL_UpscaleVTFetchStart(int factor, char *reason, size_t size)'

fetch_guard()
(
	cd "$1" || exit 2
	grep -rnw -- CL_UpscaleVTFetchStart src > "${tmp:?}/fg.hits"
	rc=$?
	if [ "$rc" -gt 1 ]; then
		echo "check-upscale-vt: grep failed (exit $rc) looking for the fetch's name"
		exit 2
	fi
	awk -v decl="$fg_decl" -v def="$fg_def" '
		{
			i = index($0, ":"); f = substr($0, 1, i - 1)
			r = substr($0, i + 1); t = substr(r, index(r, ":") + 1)
			if (f == "src/client/menu/menu.c")
				next
			if ((f == "src/client/header/upscale_vt.h") && (t == decl) && !seen[f]++)
				next
			if ((f == "src/client/cl_upscale_vt.m" || f == "src/client/cl_upscale_cmd.c") &&
				(t == def) && !seen[f]++)
				next
			print
			bad = 1
		}
		END { exit bad }' "${tmp:?}/fg.hits"
	rc=$?
	if [ "$rc" -gt 1 ]; then
		echo "check-upscale-vt: awk failed (exit $rc) reading the fetch's uses"
		exit 2
	fi
	if [ "$rc" = 1 ]; then
		echo "check-upscale-vt: the model's fetch is named outside the menu item"
		exit 1
	fi

	grep -rnoF -- downloadConfigurationModel src > "${tmp:?}/fg.dl"
	rc=$?
	if [ "$rc" -gt 1 ]; then
		echo "check-upscale-vt: grep failed (exit $rc) looking for the model's download"
		exit 2
	fi
	total=$(wc -l < "${tmp:?}/fg.dl") || exit 2
	inside=$(awk -v def="$fg_def" '
		$0 == def { body = 1 }
		body { n += gsub(/downloadConfigurationModel/, "&") }
		body && ($0 == "}") { body = 0 }
		END { print n + 0 }' src/client/cl_upscale_vt.m) || exit 2
	total=$((total + 0)) && inside=$((inside + 0)) || exit 2
	if [ "$total" -ne 1 ] || [ "$inside" -ne 1 ]; then
		cat "${tmp:?}/fg.dl"
		echo "check-upscale-vt: the model's download is named $total times, $inside of them in the fetch; it must be once, in the fetch"
		exit 1
	fi
	exit 0
)

# The guard's mutants: a copy of the four files that may name the fetch
# must pass, and each shape below put into it must be refused (1), or,
# for the unreadable file, fail as a tool error (2). Nothing is compiled.
fg_files='src/client/header/upscale_vt.h src/client/cl_upscale_vt.m src/client/cl_upscale_cmd.c src/client/menu/menu.c'
mkdir -p "${tmp:?}/ctl" || exit 1
for f in $fg_files; do
	{ mkdir -p "${tmp:?}/ctl/${f%/*}" && cp "$f" "${tmp:?}/ctl/$f"; } || exit 1
done

fetch_guard "${tmp:?}/ctl" > "${tmp:?}/fg.out" 2>&1
rc=$?
if [ "$rc" != 0 ]; then
	echo "check-upscale-vt: the fetch guard refused an unchanged copy of the tree (exit $rc), so its mutants prove nothing"
	exit 1
fi

# fg_mutant NAME WANT FILE TEXT: TEXT added to FILE (- for none) in a
# fresh copy; the guard must exit WANT
fg_refused=
fg_mutant()
{
	{ rm -rf "${tmp:?}/mut" && cp -R "${tmp:?}/ctl" "${tmp:?}/mut"; } || exit 1
	if [ "$3" != - ]; then
		{ mkdir -p "${tmp:?}/mut/${3%/*}" && printf '%s\n' "$4" >> "${tmp:?}/mut/$3"; } || exit 1
	fi
	case $1 in
		"download outside the fetch")
			{ sed '/downloadConfigurationModelWithCompletionHandler/d' \
				"${tmp:?}/ctl/src/client/cl_upscale_vt.m" > "${tmp:?}/mut/src/client/cl_upscale_vt.m" &&
				printf '%s\n' "$4" >> "${tmp:?}/mut/src/client/cl_upscale_vt.m"; } || exit 1
			;;
		"grep error")
			chmod 000 "${tmp:?}/mut/src/client/menu/menu.c" || exit 1
			;;
	esac
	fetch_guard "${tmp:?}/mut" > "${tmp:?}/fg.out" 2>&1
	rc=$?
	if [ "$rc" != "$2" ]; then
		echo "check-upscale-vt: the fetch guard let a mutant through: $1 (exit $rc, wanted $2)"
		exit 1
	fi
	fg_refused="$fg_refused, $1"
}

fg_mutant "pointer" 1 src/client/cl_upscale_cmd.c \
	'static int (*vt_go)(int, char *, size_t) = CL_UpscaleVTFetchStart;'
fg_mutant "macro alias" 1 src/client/cl_upscale.c '#define VT_GO CL_UpscaleVTFetchStart'
fg_mutant "call at column 0" 1 src/client/cl_upscale_cmd.c 'CL_UpscaleVTFetchStart(4, NULL, 0);'
fg_mutant "split call" 1 src/client/cl_upscale_cmd.c 'static int vt_x = CL_UpscaleVTFetchStart
	(4, NULL, 0);'
fg_mutant "a second definition line" 1 src/client/cl_upscale_cmd.c "$fg_def"
fg_mutant "raw download" 1 src/client/vid/vt_other.m \
	'[c downloadConfigurationModelWithCompletionHandler:^(NSError *e) { (void)e; }];'
fg_mutant "download outside the fetch" 1 - \
	'static void vt_other(id c) { [c downloadConfigurationModelWithCompletionHandler:nil]; }'
fg_mutant "grep error" 2 - ''

fetch_guard . || exit 1
echo "check-upscale-vt: the fetch guard refused its mutants (${fg_refused#, })"

if [ "$(uname -s)" != Darwin ]; then
	echo "check-upscale-vt: skipped: not macOS"
	exit 0
fi

cc=${CC:-cc}
bound=${UPSCALE_VT_CHECK_SECONDS:-120}

case $bound in
	''|*[!0-9]*)
		echo "check-upscale-vt: UPSCALE_VT_CHECK_SECONDS is not a whole number of seconds"
		exit 1
		;;
esac

san="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
printf 'int main(void){return 0;}\n' > "${tmp:?}/probe.c"

if ! { $cc $san -o "${tmp:?}/probe" "${tmp:?}/probe.c" >/dev/null 2>&1 && "${tmp:?}/probe" >/dev/null 2>&1; }; then
	echo "check-upscale-vt: $cc cannot build with ASAN and UBSAN; running without them"
	san=
fi

if ! $cc -fobjc-arc -std=gnu11 -Wall -Wextra -Werror -g -O1 $san \
	-DUPSCALE_VT_TEST -Isrc/client -o "${tmp:?}/upscale_vt_test" \
	tests/metal/upscale_vt_test.m src/client/cl_upscale_vt.m \
	-framework Foundation -framework CoreVideo -framework CoreMedia \
	-weak_framework VideoToolbox > "${tmp:?}/cc.out" 2>&1; then
	cat "${tmp:?}/cc.out"
	echo "check-upscale-vt: the host test does not build"
	exit 1
fi

status=0
ASAN_OPTIONS=abort_on_error=0 perl -e 'alarm shift; exec @ARGV or exit 127' \
	"$bound" "${tmp:?}/upscale_vt_test" > "${tmp:?}/run.out" 2>&1 || status=$?

sed 's/^/upscale_vt_test: /' "${tmp:?}/run.out"

if [ "$status" = 142 ]; then
	echo "check-upscale-vt: the host test outlived its $bound s bound and was stopped"
	exit 1
fi

if [ "$status" != 0 ]; then
	echo "check-upscale-vt: the host test failed (exit $status)"
	exit 1
fi

last=$(tail -n 1 "${tmp:?}/run.out")

case $last in
	*" passed, 0 failed") ;;
	*)
		echo "check-upscale-vt: the host test printed no result line"
		exit 1
		;;
esac

echo "check-upscale-vt: OK ($last)"
