#!/bin/sh
# Invariant: every renderer's BSP loader runs the shared checks (FIX118).
#
# The four renderers (gl1, gl3, soft, metal) each fork Mod_LoadBrushModel.
# The lump, visibility, node and face-edge checks live once, in
# src/client/refresh/files/models.c (Mod_CheckLumps, Mod_CheckVis,
# Mod_CheckNodes, Mod_CheckFaceEdges), so a hostile map is refused the same
# way by all four. This fails when
# - a Mod_LoadBrushModel body does not call all four checks, or calls one
#   after its first lump read (a Mod_Load* or Mod_CalcLumpHunkSize call);
# - a Mod_LoadBrushModel, Mod_LoadFaces or Mod_LoadLeafs definition appears
#   outside the four known loader files (a fifth fork would skip the checks);
# - the shared checks are not defined in models.c.
# Comments do not count as calls.
#
# Then it proves itself on scratch copies: a copy with one renderer's call
# dropped, one with a check moved after a lump read and one with a fifth
# loader must each be refused.
#
# Usage: tests/check-bsp-loader.sh [root]   (root defaults to the repo)
set -eu
export LC_ALL=C

here=$(cd "$(dirname "$0")/.." && pwd)
root=${1:-$here}

loaders="src/client/refresh/gl1/gl1_model.c src/client/refresh/gl3/gl3_model.c
src/client/refresh/soft/sw_model.c src/client/refresh/metal/rm_model.c"
checks="Mod_CheckLumps Mod_CheckVis Mod_CheckNodes Mod_CheckFaceEdges"
shared=src/client/refresh/files/models.c

check_tree() { # root: 0 when the tree keeps the invariant, 1 with the reasons printed
	r=$1
	bad=0

	for c in $checks; do
		if ! grep -qE "^$c[[:space:]]*\(" "$r/$shared"; then
			echo "check-bsp-loader: $shared does not define $c"
			bad=1
		fi
	done

	for f in $loaders; do
		if [ ! -f "$r/$f" ]; then
			echo "check-bsp-loader: $f is missing"
			bad=1
			continue
		fi

		n=$(grep -cE '^Mod_LoadBrushModel[[:space:]]*\(' "$r/$f" || true)
		if [ "$n" != 1 ]; then
			echo "check-bsp-loader: $f defines Mod_LoadBrushModel $n times, not once"
			bad=1
			continue
		fi

		# the body, comments stripped: from the definition to the first closing brace in column 1
		body=$(sed -n '/^Mod_LoadBrushModel[[:space:]]*(/,/^}/p' "$r/$f" |
			sed 's,/\*.*\*/,,g; s,//.*,,')
		first_read=$(printf '%s\n' "$body" |
			grep -nE '(Mod_Load[A-Za-z0-9_]*|Mod_CalcLumpHunkSize|calcTexinfoAndFacesSize)[[:space:]]*\(' |
			grep -vE ':Mod_LoadBrushModel' | head -n 1 | cut -d: -f1)

		for c in $checks; do
			at=$(printf '%s\n' "$body" | grep -nE "(^|[^A-Za-z0-9_])$c[[:space:]]*\(" | head -n 1 | cut -d: -f1)
			if [ -z "$at" ]; then
				echo "check-bsp-loader: $f: Mod_LoadBrushModel does not call $c"
				bad=1
			elif [ -n "$first_read" ] && [ "$at" -gt "$first_read" ]; then
				echo "check-bsp-loader: $f: Mod_LoadBrushModel calls $c after its first lump read"
				bad=1
			fi
		done
	done

	# no definition of the forked loaders outside the four known files
	extra=$(cd "$r" && grep -rlE '^Mod_Load(BrushModel|Faces|Leafs)[[:space:]]*\(' src | sort)
	for x in $extra; do
		case " $(echo $loaders) " in
		*" $x "*) ;;
		*)
			echo "check-bsp-loader: $x defines a BSP loader outside the four known files"
			bad=1
			;;
		esac
	done

	return $bad
}

if ! check_tree "$root"; then
	exit 1
fi

if [ "$#" -gt 0 ]; then
	echo "check-bsp-loader: OK ($root)"
	exit 0
fi

# selftest on scratch copies of the tree's loader files
tmp=$(mktemp -d "${TMPDIR:-/tmp}/check-bsp-loader.XXXXXX")
trap 'rm -rf "${tmp:?}"' EXIT

copy() { # dir: a scratch tree with the loader files and models.c
	for f in $loaders $shared; do
		mkdir -p "$1/$(dirname "$f")"
		cp "$root/$f" "$1/$f"
	done
}

expect_refused() { # what dir
	if check_tree "$2" > "${tmp:?}/out" 2>&1; then
		echo "check-bsp-loader: selftest: $1 was not refused"
		exit 1
	fi
}

copy "${tmp:?}/same"
if ! check_tree "${tmp:?}/same" > "${tmp:?}/out" 2>&1; then
	cat "${tmp:?}/out"
	echo "check-bsp-loader: selftest: an unchanged copy was refused"
	exit 1
fi

copy "${tmp:?}/dropped"
sed 's/!Mod_CheckNodes(mod_base, header, why, sizeof(why)) ||//' \
	"$root/src/client/refresh/gl3/gl3_model.c" > "${tmp:?}/dropped/src/client/refresh/gl3/gl3_model.c"
expect_refused "gl3 without Mod_CheckNodes" "${tmp:?}/dropped"
grep -q "gl3_model.c: Mod_LoadBrushModel does not call Mod_CheckNodes" "${tmp:?}/out" ||
	{ echo "check-bsp-loader: selftest: wrong reason: $(cat "${tmp:?}/out")"; exit 1; }

copy "${tmp:?}/late"
# a lump read before the checks
awk '/^Mod_LoadBrushModel/ { inb = 1 }
	inb && /the shared BSP checks/ && !done { print "\tMod_CalcLumpHunkSize(&header->lumps[0], 1, 1, 0);"; done = 1 }
	{ print }' "$root/src/client/refresh/soft/sw_model.c" > "${tmp:?}/late/src/client/refresh/soft/sw_model.c"
expect_refused "soft reading a lump before the checks" "${tmp:?}/late"

copy "${tmp:?}/fifth"
mkdir -p "${tmp:?}/fifth/src/client/refresh/vk"
printf 'static void\nMod_LoadFaces(model_t *m, byte *b, lump_t *l)\n{\n}\n' > "${tmp:?}/fifth/src/client/refresh/vk/vk_model.c"
expect_refused "a fifth loader" "${tmp:?}/fifth"

echo "check-bsp-loader: OK (4 loaders call the shared checks first; 3 mutants refused)"
