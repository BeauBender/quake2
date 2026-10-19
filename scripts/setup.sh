#!/bin/sh
# One-time setup for a fresh checkout. Checks the toolchain the gate and the
# client need and wires the commit hook in .githooks/. It installs nothing:
# a missing tool is reported with the command that would install it.
#
# Usage: scripts/setup.sh            (re-running it is harmless)
set -eu
cd "$(dirname "$0")/.."

missing=0
have() { command -v "$1" >/dev/null 2>&1; }
need() { # $1 = tool, $2 = why, $3 = install hint
	if have "$1"; then
		echo "setup: ok   $1"
	else
		echo "setup: miss $1 -- $2 (install: $3)"
		missing=1
	fi
}

need cc "the gate builds q2ded and the game module" "Xcode command line tools, or build-essential"
need make "GNU Make is the only build" "Xcode command line tools, or build-essential"
need git "worktrees and the commit hook" "your package manager"
need gh "cards are GitHub Issues; work lands through pull requests" "brew install gh, or apt install gh"

# The client and renderers need SDL3 (or SDL2 with WITH_SDL3=no); the gate
# does not.
if have sdl3-config || { have pkg-config && pkg-config --exists sdl3; } ||
	{ have brew && [ -d "$(brew --prefix 2>/dev/null)/include/SDL3" ]; } ||
	[ -d /usr/include/SDL3 ]; then
	echo "setup: ok   SDL3"
else
	echo "setup: miss SDL3 -- only the client and renderers need it; the gate does not (install: brew install sdl3, or apt install libsdl3-dev)"
fi

# Wire .githooks/ unless the checkout already uses another hooks path or
# has its own hooks in .git/hooks that a hooksPath would silently disable.
current=$(git config --get core.hooksPath || true)
if [ "$current" = ".githooks" ]; then
	echo "setup: ok   core.hooksPath=.githooks"
elif [ -n "$current" ]; then
	echo "setup: skip core.hooksPath is already '$current'; leaving it"
elif ls "$(git rev-parse --git-common-dir)/hooks" 2>/dev/null | grep -qv '\.sample$'; then
	echo "setup: skip .git/hooks has its own hooks; setting core.hooksPath would disable them"
else
	git config core.hooksPath .githooks
	echo "setup: set  core.hooksPath=.githooks (undo: git config --unset core.hooksPath)"
fi

exit "$missing"
