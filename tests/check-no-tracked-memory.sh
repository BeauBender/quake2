#!/bin/sh
# Invariant: agent memory is not part of this public repository.
#
# Fails when any tracked path (an index entry, so a file only staged counts)
# is a .claude/memory directory or lies under one, in any letter case and at
# any depth (.claude/memory/x, .Claude/memory/x, .claude/Memory/x,
# sub/.claude/memory/x), including a symbolic link or a submodule at that
# path; when a file created at .claude/memory/ would not be ignored; or when
# git, tr or grep fails or the script is interrupted (nothing is counted as
# clean then). Memory is kept in the owner's private store (see CLAUDE.md),
# never in a commit.
#
# The self-test runs first, in a subshell with the GIT_* repository
# variables, GIT_DEFAULT_HASH and GIT_TEMPLATE_DIR unset, no global or
# system configuration, and HOME and XDG_CONFIG_HOME inside a private
# mktemp -d, on throwaway repositories there. It shows the check fails on
# each spelling above, on a symlink and a submodule at .claude/memory, on a
# missing ignore rule, outside a repository, and when ls-files,
# check-ignore, tr or grep alone fails (a shim first on PATH), and passes
# on a clean repository. The real check then reads the index git hands it.
#
# Usage: tests/check-no-tracked-memory.sh
set -eu
CDPATH='' cd "$(dirname "$0")/.."
me=check-no-tracked-memory
tmp=$(mktemp -d "${TMPDIR:-/tmp}/nomem.XXXXXX")
chmod 700 "${tmp:?}"
# Cleanup runs only on exit; a signal exits non-zero (and so cleans up)
# instead of resuming with the scratch files gone.
trap 'rm -rf "${tmp:?}"' EXIT
trap 'exit 1' HUP INT TERM

# check <repo-dir>: prints a reason and returns 1 on a violation.
check()
{
	if ! git -C "$1" ls-files -z > "${tmp:?}/ls" 2>/dev/null; then
		echo "$me: git ls-files failed; cannot tell whether agent memory is tracked" >&2
		return 1
	fi
	if ! tr '\0' '\n' < "${tmp:?}/ls" > "${tmp:?}/paths" 2>/dev/null; then
		echo "$me: tr failed; cannot scan the tracked paths" >&2
		return 1
	fi
	# grep reads its input as an operand and prints only a count, so a
	# missing or unreadable file is status 2, never "no match".
	rc=0
	n=$(grep -Eic '(^|/)\.claude/memory(/|$)' "${tmp:?}/paths") || rc=$?
	case "$rc" in
	0)
		echo "$me: $n tracked path(s) under a .claude/memory directory (any case, any depth); agent memory belongs in the owner's private store (see CLAUDE.md), not in a commit (git rm -r --cached <path>)" >&2
		return 1
		;;
	1) ;;
	*)
		echo "$me: cannot scan the tracked paths" >&2
		return 1
		;;
	esac
	rc=0
	git -C "$1" check-ignore -q -- .claude/memory/probe.md 2>/dev/null || rc=$?
	case "$rc" in
	0) ;;
	1)
		echo "$me: .claude/memory/ is not ignored; add it to .gitignore" >&2
		return 1
		;;
	*)
		echo "$me: git check-ignore failed; cannot tell whether .claude/memory/ is ignored" >&2
		return 1
		;;
	esac
	return 0
}

selftest()
{
	unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_OBJECT_DIRECTORY \
		GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE GIT_CONFIG GIT_CONFIG_PARAMETERS \
		GIT_CONFIG_COUNT GIT_DEFAULT_HASH GIT_TEMPLATE_DIR
	HOME=$tmp/home
	XDG_CONFIG_HOME=$tmp/home/.config
	GIT_CONFIG_GLOBAL=/dev/null
	GIT_CONFIG_NOSYSTEM=1
	GIT_CEILING_DIRECTORIES=$tmp
	export HOME XDG_CONFIG_HOME GIT_CONFIG_GLOBAL GIT_CONFIG_NOSYSTEM GIT_CEILING_DIRECTORIES
	mkdir -p "${tmp:?}/home"

	# mkrepo <name>: a repository with the ignore rule.
	mkrepo()
	{
		mkdir -p "${tmp:?}/$1"
		git -C "${tmp:?}/$1" init -q .
		printf '%s\n' '.claude/memory' > "${tmp:?}/$1/.gitignore"
	}

	# entry <name> <mode> <path>: an index entry with no file behind it, so
	# letter-case variants do not collide on a case-insensitive filesystem.
	# A gitlink's oid is not type-checked, so a blob's serves for every mode
	# and under either hash.
	entry()
	{
		oid=$(printf 'x\n' | git -C "${tmp:?}/$1" hash-object -w --stdin)
		git -C "${tmp:?}/$1" update-index --add --cacheinfo "$2,$oid,$3"
	}

	expect_reject()
	{
		if check "$tmp/$1" 2>/dev/null; then
			echo "$me: selftest: $2 was not detected" >&2
			exit 1
		fi
	}

	mkrepo clean
	check "$tmp/clean" 2>/dev/null ||
		{ echo "$me: selftest: a clean repository was rejected" >&2; exit 1; }

	mkrepo staged
	mkdir -p "${tmp:?}/staged/.claude/memory"
	printf 'x\n' > "${tmp:?}/staged/.claude/memory/note.md"
	git -C "${tmp:?}/staged" add -f .claude/memory/note.md
	expect_reject staged "a staged .claude/memory/ file"

	i=0
	for p in .Claude/memory/MEMORY.md .claude/Memory/MEMORY.md .CLAUDE/MEMORY/x.md \
		sub/.claude/memory/MEMORY.md a/b/.Claude/Memory/x.md; do
		i=$((i + 1))
		mkrepo "case$i"
		entry "case$i" 100644 "$p"
		expect_reject "case$i" "a tracked $p"
	done

	mkrepo symlink
	entry symlink 120000 .claude/memory
	expect_reject symlink "a symbolic link at .claude/memory"

	mkrepo symlinkcase
	entry symlinkcase 120000 sub/.Claude/Memory
	expect_reject symlinkcase "a symbolic link at sub/.Claude/Memory"

	mkrepo submodule
	entry submodule 160000 .claude/memory
	expect_reject submodule "a submodule at .claude/memory"

	mkdir -p "${tmp:?}/noignore"
	git -C "${tmp:?}/noignore" init -q .
	expect_reject noignore "a missing ignore rule"

	mkdir -p "${tmp:?}/notarepo"
	expect_reject notarepo "a git failure (not a repository)"

	# One command fails while the rest work, on the clean repository: a
	# shim first on PATH exits non-zero for that command only.
	realgit=$(command -v git)
	for c in ls-files check-ignore tr grep; do
		mkdir -p "${tmp:?}/shim-$c"
		case "$c" in
		tr) printf '#!/bin/sh\nexit 1\n' > "${tmp:?}/shim-$c/tr" ;;
		grep) printf '#!/bin/sh\nexit 2\n' > "${tmp:?}/shim-$c/grep" ;;
		*)
			printf '#!/bin/sh\ncase " $* " in *" %s "*) exit 128 ;; esac\nexec "%s" "$@"\n' \
				"$c" "$realgit" > "${tmp:?}/shim-$c/git"
			;;
		esac
		chmod 755 "${tmp:?}/shim-$c/"*
		if (PATH=$tmp/shim-$c:$PATH; check "$tmp/clean") 2>/dev/null; then
			echo "$me: selftest: a failing $c was counted as clean" >&2
			exit 1
		fi
	done
}

( selftest ) || exit 1
check . || exit 1
echo "$me: ok"
