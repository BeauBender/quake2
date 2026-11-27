#!/bin/sh
# Invariant (FIX217): an incremental build rebuilds an object whose header
# changed, for the client, the server and the game module on every host. The
# Makefile adds -MMD to CFLAGS, each object's compile then writes its .d file,
# and -include reads it. On macOS the flag was once left out ("unsupported by
# the Clang shipped with OS X"), so an edit to a header rebuilt ref_metal but
# kept a stale vidrules.o.
#
# Builds nothing: every decision is read from `make -n` output. For one client
# object (src/client/menu/vidrules.c), one server object (src/server/sv_main.c)
# and one game object (src/game/g_main.c), the compile command of the dry run
# must carry -MMD, so its .d file is written; the Makefile must -include the
# .d files of the three object sets (a .d file nobody reads tracks nothing);
# and `make -n -W <header>`, for a header vidrules.c includes, must name the
# vidrules.o compile.
#
# Mutant: the Makefile with its `override CFLAGS += -MMD` line removed, in a
# scratch copy, must fail the flag check, so the check cannot pass without the
# flag doing the work.
set -u
cd "$(dirname "$0")/.." || exit 1

fail=0
ok() { printf 'ok   %s\n' "$1"; }
bad() { printf 'FAIL %s\n' "$1"; fail=1; }

# The objects whose compile lines lack -MMD under the Makefile $1, one per
# word; empty when every one carries it.
missing() {
	for pair in client:src/client/menu/vidrules server:src/server/sv_main game:src/game/g_main; do
		tgt=${pair%%:*}
		obj=${pair#*:}
		line=$(make -n -f "$1" MAKE="make -f $1" "$tgt" 2>&1 | grep -e "$obj\\.c" | grep -e ' -c ' | head -1)
		case $line in
		"") printf '%s(no-compile-line) ' "$obj" ;;
		*" -MMD"*) ;;
		*) printf '%s ' "$obj" ;;
		esac
	done
}

m=$(missing Makefile)
if [ -n "$m" ]; then
	bad "compiled without -MMD: $m"
else
	ok "vidrules.o, sv_main.o and g_main.o compile with -MMD"
fi

for set in CLIENT SERVER GAME; do
	if grep -q "^-include \$(${set}_DEPS)" Makefile; then
		ok "-include \$(${set}_DEPS)"
	else
		bad "the Makefile does not -include \$(${set}_DEPS)"
	fi
done

hdr=src/client/menu/header/vidrules.h
if ! grep -q 'header/vidrules\.h' src/client/menu/vidrules.c; then
	bad "vidrules.c no longer includes header/vidrules.h"
elif make -n -W "$hdr" client 2>&1 | grep -q 'vidrules\.o'; then
	ok "touching vidrules.h names vidrules.o"
else
	bad "touching vidrules.h does not name vidrules.o"
fi

T=$(mktemp -d) || exit 1
trap 'rm -rf "${T:?}"' EXIT
sed -e 's/^override CFLAGS += -MMD$/override CFLAGS += -DNO_MMD_MUTANT/' Makefile > "$T/Makefile.mut"
if cmp -s Makefile "$T/Makefile.mut"; then
	bad "mutant: the Makefile has no 'override CFLAGS += -MMD' line to remove"
elif [ -z "$(missing "$T/Makefile.mut")" ]; then
	bad "mutant: a Makefile without -MMD passed the check"
else
	ok "mutant: a Makefile without -MMD is caught"
fi

[ "$fail" -eq 0 ] && echo "check-build-deps: ok"
exit "$fail"
