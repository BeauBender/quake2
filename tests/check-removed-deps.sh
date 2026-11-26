#!/bin/sh
# Invariant: dependencies this fork removed stay removed.
#
# The trust-boundary epic (docs/plans/trust-boundary.md, decision record
# docs/decisions/0001-remove-optional-dependencies.md) deleted OpenAL,
# libcurl, minizip/miniz, stb_image_write, the GLES renderers, the CMake build and SDL2 (and
# with it the WITH_SDL3 switch); decision 0010 removed stb_image (ImageIO
# decodes, src/client/refresh/files/imgdecode.c; stb_image_resize.h stays).
# This fails if any of them comes back into the build: a source directory or
# file, a Makefile switch, an include, a call or a CI install line. SDL2 is
# matched case-insensitively for the name SDL2, SDL_VERSION_ATLEAST(2 and a
# bare <SDL.h> include, over src, Makefile, .github and stuff/misc.
#
# Usage: tests/check-removed-deps.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C

fail=0

# git grep exits 0 on a match and 1 on none; any other status (not a git
# repository, a bad pattern) is a failed scan, never a clean one.
grep_status()
{
	if [ "$1" -gt 1 ]; then
		echo "check-removed-deps: git grep failed (status $1): the scan did not run"
		exit 1
	fi
}

for path in src/client/curl src/common/unzip src/client/sound/openal.c src/client/sound/qal.c src/client/vid/header/stb_image_write.h \
	src/client/refresh/files/stb_image.h \
	src/client/refresh/gl3/glad-gles3 CMakeLists.txt; do
	if [ -e "$path" ]; then
		echo "check-removed-deps: $path is back"
		fail=1
	fi
done

# Word-bounded on purpose: "angles", "triangles_max" and stb's "unzigzag"
# must not match. stb_image is its header in any directory, its functions
# (stbi_) and its macros (STBI_, STB_IMAGE_IMPLEMENTATION); stb_image_resize's
# stbir_, STBIR_ and stb_image_resize.h do not match.
pattern='USE_CURL|WITH_CURL|curl/curl\.h|libcurl|USE_OPENAL|WITH_OPENAL|AL/alc?\.h|libopenal|WITH_SYSTEM_MINIZIP|unzip\.h|miniz\.h|-lminizip|ref_gles[0-9]|YQ2_GL3_GLES|USE_GLES|WITH_SDL3|USE_SDL3|stb_image_write|stbi_write_|STB_IMAGE_WRITE|stb_image\.h|stbi_|STBI_|STB_IMAGE_IMPLEMENTATION'
rc=0
hits=$(git grep -nE "$pattern" -- src Makefile .github stuff/misc 2>/dev/null) || rc=$?
grep_status "$rc"
if [ -n "$hits" ]; then
	echo "check-removed-deps: references to removed dependencies:"
	echo "$hits" | sed 's/^/  /'
	fail=1
fi

# SDL2 has to be caught by every route, so this scan ignores case: the name
# in any spelling (sdl2-config, libSDL2, -lSDL2, WITH_SDL2, glimp_sdl2,
# input/sdl2, sdl2-compat), a version test written for 2.x
# (SDL_VERSION_ATLEAST(2, ...)) and SDL2's bare <SDL.h> include (SDL3 headers
# are <SDL3/SDL.h>, which this does not match).
sdl2='SDL2|SDL_VERSION_ATLEAST[[:space:]]*\([[:space:]]*2|[<"]SDL\.h[>"]'
rc=0
hits=$(git grep -niE "$sdl2" -- src Makefile .github stuff/misc 2>/dev/null) || rc=$?
grep_status "$rc"
if [ -n "$hits" ]; then
	echo "check-removed-deps: references to SDL2:"
	echo "$hits" | sed 's/^/  /'
	fail=1
fi
# Homebrew: the owner has not approved it anywhere, CI included, and macOS
# needs nothing beyond Xcode's toolchain. The scan covers every tracked file,
# so every place CI can run something from (workflows, local actions wherever
# they live, scripts, tests, the Makefile) and a symlink's target is scanned
# as itself; untracked files are ignored, like the rest of this check. Only
# Markdown prose (*.md under doc/ and docs/, at any depth, and CLAUDE.md) and
# this file are left out; a script, action or YAML under doc/ or docs/ is
# scanned. third_party/ is left out whole: it is vendored and checked against
# its manifest (scripts/check-vendored.sh). Deliberately loose (any case, no word bounds): an over-refusal
# is fixed by rewording, a false pass is not.
brew='brew|gmake'
brew_excludes=':(exclude,glob)doc/**/*.md :(exclude,glob)docs/**/*.md :(exclude)third_party :(exclude)CLAUDE.md :(exclude)tests/check-removed-deps.sh'
# brew_scan_tree DIR: DIR is the repository (tracked files) or, for a mutant,
# a scratch tree scanned with --no-index.
brew_scan_tree()
{
	rc=0
	if [ "$1" = . ]; then
		out=$(git grep -I -i -n -E "$brew" -- . $brew_excludes 2>/dev/null) || rc=$?
	else
		out=$(cd "$1" && git grep --no-index -I -i -n -E "$brew" -- . $brew_excludes 2>/dev/null) || rc=$?
	fi
	if [ "$rc" -gt 1 ]; then
		echo "check-removed-deps: git grep failed (status $rc): the Homebrew scan did not run" >&2
		exit 1
	fi
	printf '%s' "$out"
}

git rev-parse --is-inside-work-tree >/dev/null 2>&1 || { echo "check-removed-deps: not a git work tree: the Homebrew scan did not run"; exit 1; }
hits=$(brew_scan_tree .)
if [ -n "$hits" ]; then
	echo "check-removed-deps: Homebrew (brew, gmake) in a tracked file:"
	echo "$hits" | sed 's/^/  /'
	fail=1
fi

# Mutants: the same scan over a scratch tree with one Homebrew line added in
# each place CI can run it from; the scan must refuse every one.
mut=$(mktemp -d)
trap 'rm -rf "$mut"' EXIT INT TERM
mutant_tree()
{
	rm -rf "$mut/t"
	mkdir -p "$mut/t/.github/actions/x" "$mut/t/stuff/x" "$mut/t/scripts" "$mut/t/tests"
	cp -R .github/workflows "$mut/t/.github/"
	cp scripts/coverage.sh "$mut/t/scripts/"
	: > "$mut/t/stuff/x/action.yml"
	: > "$mut/t/scripts/ci-x.sh"
	: > "$mut/t/.github/actions/x/action.yml"
}
expect_refused()
{
	if [ -z "$(brew_scan_tree "$mut/t")" ]; then
		echo "check-removed-deps: mutant $1 was not refused"
		fail=1
	fi
}
for m in 'brew install make' 'brew update' '/opt/homebrew/bin/brew install make' '/usr/local/bin/brew install make' 'gmake -j3'; do
	for where in .github/workflows/macos.yml .github/actions/x/action.yml stuff/x/action.yml scripts/coverage.sh; do
		mutant_tree
		printf '    - run: |\n        %s\n' "$m" >> "$mut/t/$where"
		expect_refused "'$m' in $where"
	done
done
mutant_tree
printf 'brew install make\n' >> "$mut/t/stuff/x/action.yml"
ln -s ../../stuff/x/action.yml "$mut/t/.github/actions/x/link.yml"
rm "$mut/t/.github/actions/x/action.yml"
expect_refused "in a symlinked action"
mutant_tree
printf '    - uses: Homebrew/actions/setup-homebrew@%s\n' 0123456789012345678901234567890123456789 >> "$mut/t/.github/workflows/macos.yml"
expect_refused "uses: Homebrew/actions"
mutant_tree
mkdir -p "$mut/t/docs/x"
printf '    - run: brew install make\n' > "$mut/t/docs/x/action.yml"
printf '    - uses: ./docs/x\n' >> "$mut/t/.github/workflows/macos.yml"
expect_refused "in an action under docs/"
mutant_tree
mkdir -p "$mut/t/docs"
printf 'brew install make\n' > "$mut/t/docs/x.sh"
expect_refused "in a script under docs/"
mutant_tree
mkdir -p "$mut/t/docs"
printf 'brew install make\n' > "$mut/t/docs/real.yml"
ln -s ../../../docs/real.yml "$mut/t/.github/actions/x/link.yml"
rm "$mut/t/.github/actions/x/action.yml"
expect_refused "in an action symlinked into docs/"
mutant_tree
mkdir -p "$mut/t/docs/a/b" "$mut/t/doc/c"
printf 'brew install make\n' > "$mut/t/docs/a/b/note.md"
printf 'brew install make\n' > "$mut/t/doc/c/note.md"
if [ -n "$(brew_scan_tree "$mut/t")" ]; then
	echo "check-removed-deps: a nested prose file under doc/ or docs/ was not excluded"
	fail=1
fi
[ "$fail" -eq 0 ] && echo "check-removed-deps: ok"
exit "$fail"
