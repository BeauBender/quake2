#!/bin/sh
# Invariant: the compatibility identifiers stay byte-identical to upstream's,
# whatever the display name is.
#
# The fork shows itself as "Portmanbeau Quake II" (DISPLAYNAME in
# src/common/header/common.h), but existing user data and other software
# depend on strings that are not the display name: the savegame version tags
# the loader compares, the user directory names, the exec of the startup
# config and the layout of the `version` serverinfo cvar. A rename of the
# project must not move them. Each pin below is one literal line in the
# source; this fails and names the pin when the line is changed or gone.
#
# Usage: tests/check-compat-names.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C

fail=0
pin() { # file, fixed string that must occur as a whole line (after trim)
	if ! sed 's/^[[:space:]]*//; s/[[:space:]]*$//' "$1" | grep -qxF -- "$2"; then
		echo "check-compat-names: $1 no longer contains: $2"
		fail=1
	fi
}

sg=src/game/savegame/savegame.c
pin $sg '#define SAVEGAMEVER "YQ2-5"'
for n in 1 2 3 4 5; do
	pin $sg "{\"YQ2-$n\", $n},"
done

cm=src/common/header/common.h
pin $cm '#define CFGDIRNAME_SHORT "yq2"'
pin $cm '#define CFGDIRNAME "YamagiQ2"'
pin $cm '#define BASEDIRNAME "baseq2"'

pin src/common/frame.c 's = va("%s %s %s %s", YQ2VERSION, YQ2ARCH, BUILD_DATE, YQ2OSTYPE);'
pin src/common/frame.c 'Cvar_Get("version", s, CVAR_SERVERINFO | CVAR_NOSET);'
pin src/common/frame.c 'Cbuf_AddText("exec yq2.cfg\n");'

# The display strings must not leak into compared or stored identifiers.
if grep -nE '^#define[[:space:]]+(YQ2VERSION|CFGDIRNAME|CFGDIRNAME_SHORT|SAVEGAMEVER)[[:space:]].*(DISPLAY|Portmanbeau)' \
	$cm $sg; then
	echo "check-compat-names: a compatibility constant is derived from a display string"
	fail=1
fi
pin $cm '#define DISPLAYNAME "Portmanbeau Quake II"'
pin $cm '#define DISPLAYCREDIT "based on Yamagi Quake II"'

exit $fail
