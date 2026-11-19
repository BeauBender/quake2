#!/bin/sh
# Invariant: every renderer's BSP loader runs the shared checks (FIX118).
#
# The four renderers (gl1, gl3, soft, metal) each fork Mod_LoadBrushModel.
# The lump, visibility, node, face-edge and leaf checks live once, in
# src/client/refresh/files/models.c (Mod_CheckLumps, Mod_CheckVis,
# Mod_CheckNodes, Mod_CheckFaceEdges, and FIX134's Mod_CheckLeafs), so a
# hostile map is refused the same way by all four. This fails when
# - a Mod_LoadBrushModel body does not call all five checks, or calls one
#   after its first lump read (a Mod_Load* or Mod_CalcLumpHunkSize call),
#   or discards a check's result (a call that is not !Mod_Check...(), such
#   as (void)Mod_CheckNodes(...) or a bare statement);
# - a Mod_LoadBrushModel, Mod_LoadFaces or Mod_LoadLeafs definition appears
#   outside the four known loader files (a fifth fork would skip the checks);
# - the shared checks are not defined in models.c.
# Calls in comments (one line or several) and in #if 0 regions do not count.
#
# It also requires gl1, gl3 and soft to use the shared load-in-progress
# helpers of models.c (FIX135), so a refused map leaves no named empty slot.
#
# Then it proves itself on scratch copies: a copy with one renderer's call
# dropped, one with a check moved after a lump read, one with a fifth
# loader, one with a call inside a multi-line comment, one with a call under
# #if 0 and one with a (void) call must each be refused.
#
# Usage: tests/check-bsp-loader.sh [root]   (root defaults to the repo)
set -eu
export LC_ALL=C

here=$(cd "$(dirname "$0")/.." && pwd)
root=${1:-$here}

loaders="src/client/refresh/gl1/gl1_model.c src/client/refresh/gl3/gl3_model.c
src/client/refresh/soft/sw_model.c src/client/refresh/metal/rm_model.c"
checks="Mod_CheckLumps Mod_CheckVis Mod_CheckNodes Mod_CheckFaceEdges Mod_CheckLeafs"
shared=src/client/refresh/files/models.c

# stdin to stdout with only the code the compiler sees: /* */ comments
# removed across lines, // comments removed, and every #if 0 region (nested
# #if, #ifdef and #ifndef inside it included) dropped up to its #endif, or
# up to an #else or #elif of its own level, whose branch is kept. A line
# keeps its place (a dropped line is printed empty). String literals are
# not parsed: none in a loader body holds a comment marker.
strip_dead() {
	awk '
	{
		line = $0
		out = ""
		while (line != "") {
			if (incomment) {
				e = index(line, "*/")
				if (!e) { line = ""; break }
				line = substr(line, e + 2)
				incomment = 0
				continue
			}
			s = index(line, "/*")
			d = index(line, "//")
			if (d && (!s || d < s)) { out = out substr(line, 1, d - 1); line = ""; break }
			if (!s) { out = out line; line = ""; break }
			out = out substr(line, 1, s - 1) " "
			line = substr(line, s + 2)
			incomment = 1
		}
		if (dead) {
			if (out ~ /^[ \t]*#[ \t]*if/) depth++
			else if (out ~ /^[ \t]*#[ \t]*endif/) { if (--depth == 0) dead = 0 }
			else if (depth == 1 && out ~ /^[ \t]*#[ \t]*(else|elif)/) dead = 0
			print ""
			next
		}
		if (out ~ /^[ \t]*#[ \t]*if[ \t]+0([^0-9A-Za-z_xX]|$)/) { dead = 1; depth = 1; print ""; next }
		print out
	}'
}

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

		# the body, from the definition to the first closing brace in column 1,
		# with its live code only (strip_dead)
		body=$(sed -n '/^Mod_LoadBrushModel[[:space:]]*(/,/^}/p' "$r/$f" | strip_dead)
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

			# every call's result feeds the refusal: each call is negated, !Mod_Check...(
			if printf '%s\n' "$body" | sed -E "s/![[:space:]]*$c[[:space:]]*\(//g" |
				grep -qE "(^|[^A-Za-z0-9_])$c[[:space:]]*\("; then
				echo "check-bsp-loader: $f: Mod_LoadBrushModel discards the result of $c (call it as !$c(...) in the refusal's condition)"
				bad=1
			fi
		done
	done

	# a refused map leaves no named slot (FIX135): gl1, gl3 and soft record
	# each load and take back an unfinished one in Mod_ForName and
	# Mod_FreeAll (ref_metal frees its own, RM_FreeUnfinished)
	for f in src/client/refresh/gl1/gl1_model.c src/client/refresh/gl3/gl3_model.c \
		src/client/refresh/soft/sw_model.c; do
		if [ ! -f "$r/$f" ]; then
			echo "check-bsp-loader: $f is missing"
			bad=1
			continue
		fi
		live=$(strip_dead < "$r/$f")
		for u in Mod_LoadBegin Mod_LoadEnd Mod_ReclaimUnfinished; do
			if ! printf '%s\n' "$live" | grep -qE "(^|[^A-Za-z0-9_])$u[[:space:]]*\("; then
				echo "check-bsp-loader: $f does not use $u (a refused map would leave a named empty slot)"
				bad=1
			fi
		done
	done

	# the reclaim runs first (FIX140): in Mod_ForName before the cached-name
	# lookup (else a refused load's named slot is returned as a cache hit),
	# in Mod_FreeAll before the free loop (gl3 names it GL3_Mod_FreeAll)
	for f in src/client/refresh/gl1/gl1_model.c src/client/refresh/gl3/gl3_model.c \
		src/client/refresh/soft/sw_model.c; do
		[ -f "$r/$f" ] || continue
		live=$(strip_dead < "$r/$f")
		for fn in Mod_ForName:'strcmp[[:space:]]*\(' Mod_FreeAll:'for[[:space:]]*\('; do
			name=${fn%%:*}
			first=${fn#*:}
			body=$(printf '%s\n' "$live" |
				sed -n "/^\(GL3_\)\{0,1\}$name[[:space:]]*(/,/^}/p")
			rec=$(printf '%s\n' "$body" | grep -nE '(^|[^A-Za-z0-9_])Mod_ReclaimUnfinished[[:space:]]*\(' |
				head -n 1 | cut -d: -f1)
			look=$(printf '%s\n' "$body" | grep -nE "$first" | head -n 1 | cut -d: -f1)
			if [ -z "$body" ] || [ -z "$rec" ] || [ -z "$look" ] || [ "$rec" -gt "$look" ]; then
				echo "check-bsp-loader: $f: $name must call Mod_ReclaimUnfinished before its first $first"
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

copy "${tmp:?}/noslot"
sed 's/Mod_ReclaimUnfinished(/Mod_Other(/' \
	"$root/src/client/refresh/gl1/gl1_model.c" > "${tmp:?}/noslot/src/client/refresh/gl1/gl1_model.c"
expect_refused "gl1 without Mod_ReclaimUnfinished" "${tmp:?}/noslot"

copy "${tmp:?}/fifth"
mkdir -p "${tmp:?}/fifth/src/client/refresh/vk"
printf 'static void\nMod_LoadFaces(model_t *m, byte *b, lump_t *l)\n{\n}\n' > "${tmp:?}/fifth/src/client/refresh/vk/vk_model.c"
expect_refused "a fifth loader" "${tmp:?}/fifth"

# mutate file in a copy at dir: the Mod_CheckNodes line of its condition
# replaced by the lines of repl, and before printed ahead of the
# condition's first line when it is not empty
mutate() { # dir file repl before
	# through the environment: awk -v refuses a value with a newline
	REPL=$3 BEFORE=$4 awk '
		index($0, "!Mod_CheckLumps(mod_base, header, modfilelen, why, sizeof(why)) ||") && ENVIRON["BEFORE"] != "" { print ENVIRON["BEFORE"] }
		index($0, "!Mod_CheckNodes(mod_base, header, why, sizeof(why)) ||") { print ENVIRON["REPL"]; next }
		{ print }' "$root/$2" > "$1/$2"
	cmp -s "$root/$2" "$1/$2" &&
		{ echo "check-bsp-loader: selftest: the mutant of $2 changed nothing"; exit 1; }
	return 0
}

expect_reason() { # what reason
	grep -qF "$2" "${tmp:?}/out" ||
		{ echo "check-bsp-loader: selftest: $1: wrong reason: $(cat "${tmp:?}/out")"; exit 1; }
}

# the call inside a comment that spans two lines
copy "${tmp:?}/comment"
mutate "${tmp:?}/comment" src/client/refresh/gl1/gl1_model.c \
	"$(printf '\t\t/* no node check:\n\t\t!Mod_CheckNodes(mod_base, header, why, sizeof(why)) */')" ""
expect_refused "gl1 with Mod_CheckNodes in a multi-line comment" "${tmp:?}/comment"
expect_reason "the multi-line comment" "gl1_model.c: Mod_LoadBrushModel does not call Mod_CheckNodes"

# the call inside #if 0, after a nested #ifdef ... #endif
copy "${tmp:?}/ifzero"
mutate "${tmp:?}/ifzero" src/client/refresh/soft/sw_model.c \
	"$(printf '#if 0\n#ifdef NODES\n#endif\n\t\t!Mod_CheckNodes(mod_base, header, why, sizeof(why)) ||\n#endif')" ""
expect_refused "soft with Mod_CheckNodes under #if 0" "${tmp:?}/ifzero"
expect_reason "#if 0" "sw_model.c: Mod_LoadBrushModel does not call Mod_CheckNodes"

# the call made before the condition, its result cast away
copy "${tmp:?}/void"
mutate "${tmp:?}/void" src/client/refresh/metal/rm_model.c "" \
	"$(printf '\t(void)Mod_CheckNodes(mod_base, header, why, sizeof(why));')"
expect_refused "metal discarding Mod_CheckNodes" "${tmp:?}/void"
expect_reason "(void)" "rm_model.c: Mod_LoadBrushModel discards the result of Mod_CheckNodes"

# the reclaim moved below the cached-name lookup of gl1's Mod_ForName
copy "${tmp:?}/late-reclaim"
awk '/^Mod_ForName/ { inb = 1 }
	inb && /^\tMod_ReclaimUnfinished\(/ { held = $0; next }
	inb && /^\t\/\* find a free model slot spot \*\// && held != "" { print held; held = ""; inb = 0 }
	{ print }' "$root/src/client/refresh/gl1/gl1_model.c" > "${tmp:?}/late-reclaim/src/client/refresh/gl1/gl1_model.c"
cmp -s "$root/src/client/refresh/gl1/gl1_model.c" "${tmp:?}/late-reclaim/src/client/refresh/gl1/gl1_model.c" &&
	{ echo "check-bsp-loader: selftest: the late-reclaim mutant changed nothing"; exit 1; }
expect_refused "gl1 reclaiming after the cached-name lookup" "${tmp:?}/late-reclaim"
expect_reason "late reclaim" "gl1_model.c: Mod_ForName must call Mod_ReclaimUnfinished before"

# a renderer's loader file missing
copy "${tmp:?}/nofile"
rm "${tmp:?}/nofile/src/client/refresh/soft/sw_model.c"
expect_refused "a missing loader file" "${tmp:?}/nofile"
expect_reason "missing file" "sw_model.c is missing"

echo "check-bsp-loader: OK (4 loaders call the shared checks first; 8 mutants refused)"
