#!/bin/sh
# Invariant: the compatibility identifiers stay byte-identical to upstream's,
# whatever the display name is.
#
# The fork shows itself as "Portmanbeau Quake II" (DISPLAYNAME in
# src/common/header/common.h), but existing user data and other software
# depend on strings that are not the display name: the savegame version tags
# the loader compares, the user directory names, the exec of the startup
# config, the binary names and the layout of the `version` serverinfo cvar. A rename of the
# project must not move them. Each pin below is one literal line in the
# source (a macro is also required to be defined exactly once); this fails and names the pin when the line is changed or gone.
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

# count FILE REGEX: number of lines in FILE matching the extended regex
count() { grep -cE -- "$2" "$1" || true; }

# one_define FILE MACRO: the macro has exactly one #define and no #undef,
# indented or not. A second define (or an #undef and a redefinition) after
# the pinned line would silently win.
one_define() {
	d=$(count "$1" "^[[:space:]]*#[[:space:]]*define[[:space:]]+$2([[:space:]]|\$)")
	u=$(count "$1" "^[[:space:]]*#[[:space:]]*undef[[:space:]]+$2([[:space:]]|\$)")
	if [ "$d" != 1 ] || [ "$u" != 0 ]; then
		echo "check-compat-names: $1 must define $2 exactly once, no #undef (defines $d, undefs $u)"
		fail=1
	fi
}

sg=src/game/savegame/savegame.c
pin $sg '#define SAVEGAMEVER "YQ2-5"'
one_define $sg SAVEGAMEVER
for n in 1 2 3 4 5; do
	pin $sg "{\"YQ2-$n\", $n},"
done

cm=src/common/header/common.h
# The version is a number (e.g. 8.71pre), never a name.
if [ "$(count $cm '^#define YQ2VERSION "[0-9]+\.[0-9]+[A-Za-z0-9.]*"$')" != 1 ]; then
	echo "check-compat-names: $cm: YQ2VERSION must be a plain version number"
	fail=1
fi
pin $cm '#define CFGDIRNAME_SHORT "yq2"'
pin $cm '#define CFGDIRNAME "YamagiQ2"'
pin $cm '#define CFGDIRNAME CFGDIRNAME_SHORT'
pin $cm '#define CFGDIRNAME "." CFGDIRNAME_SHORT'
pin $cm '#define BASEDIRNAME "baseq2"'
one_define $cm YQ2VERSION
one_define $cm CFGDIRNAME_SHORT
one_define $cm BASEDIRNAME
one_define $cm DISPLAYNAME
one_define $cm DISPLAYCREDIT
# CFGDIRNAME is defined in the three branches of one #if chain: exactly
# three defines, each pinned above.
n=$(count $cm '^[[:space:]]*#[[:space:]]*define[[:space:]]+CFGDIRNAME([[:space:]]|$)')
u=$(count $cm '^[[:space:]]*#[[:space:]]*undef[[:space:]]+CFGDIRNAME([[:space:]]|$)')
if [ "$n" != 3 ] || [ "$u" != 0 ]; then
	echo "check-compat-names: $cm must have exactly three CFGDIRNAME defines and no #undef (defines $n, undefs $u)"
	fail=1
fi

# The unix user directory is ~/.yq2/.
pin src/backends/unix/system.c 'Com_sprintf(dir, MAX_OSPATH, "%s/.%s/", home, CFGDIRNAME_SHORT);'
pin src/backends/unix/system.c 'char cfgdir[MAX_OSPATH] = CFGDIRNAME;'

pin src/common/frame.c 's = va("%s %s %s %s", YQ2VERSION, YQ2ARCH, BUILD_DATE, YQ2OSTYPE);'
pin src/common/frame.c 'Cvar_Get("version", s, CVAR_SERVERINFO | CVAR_NOSET);'
pin src/common/frame.c 'Cbuf_AddText("exec yq2.cfg\n");'
pin src/client/menu/menu.c 'Cbuf_AddText("exec yq2.cfg\n");'
pin src/common/cmdparser.c 'if(Cmd_Argc() > 1 && Q_strcasecmp(cmd_argv[0], "exec") == 0 && Q_strcasecmp(cmd_argv[1], "yq2.cfg") == 0)'

# Binary names: the Windows wrapper starts yquake2.exe; the Makefile builds
# the engine under these names.
pin src/win-wrapper/wrapper.c 'static const WCHAR WRAPPED_EXE[] = L"yquake2.exe";'
pin Makefile '$(BINDIR)/yquake2.exe : $(CLIENT_OBJS) icon'
pin Makefile '$(BINDIR)/quake2.exe : src/win-wrapper/wrapper.c icon'
pin Makefile '$(BINDIR)/quake2 : $(CLIENT_OBJS)'
pin Makefile '$(BINDIR)/quake2 : $(CLIENT_OBJS) $(SDL3_LIB)'
pin Makefile '$(MAKE) $(BINDIR)/quake2'
pin Makefile '$(BINDIR)/q2ded.exe : $(SERVER_OBJS) icon'
pin Makefile '$(BINDIR)/q2ded : $(SERVER_OBJS)'

# The display strings must not leak into compared or stored identifiers
# (the defines may be indented inside #if chains).
if grep -nE '^[[:space:]]*#[[:space:]]*define[[:space:]]+(YQ2VERSION|CFGDIRNAME|CFGDIRNAME_SHORT|SAVEGAMEVER|BASEDIRNAME)[[:space:]].*(DISPLAY|Portmanbeau)' \
	$cm $sg; then
	echo "check-compat-names: a compatibility constant is derived from a display string"
	fail=1
fi
pin $cm '#define DISPLAYNAME "Portmanbeau Quake II"'
pin $cm "#define DISPLAYCREDIT \"based on Yamagi Quake II, from Icculus Quake II and id Software's Quake II\""

exit $fail
