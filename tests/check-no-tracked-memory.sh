#!/bin/sh
# Invariant: agent memory is not part of this public repository.
#
# Fails when any tracked path (an index entry, so a file only staged counts)
# is a .claude/memory directory or lies under one, in any letter case and at
# any depth (.claude/memory/x, .Claude/memory/x, .claude/Memory/x,
# sub/.claude/memory/x), including a symbolic link or a submodule at that
# path; when the index holds a gitlink anywhere (mode 160000: a submodule,
# or a clone made inside a checkout and staged as an embedded repository,
# whose path would name what was cloned; this repository has none); unless
# the top-level .gitignore itself ignores .claude/memory/ as a directory,
# that is, the rule that decides a file there is .claude/memory (with an
# optional leading and/or trailing /), so git excludes the directory and
# no later rule can re-include a file in it (a rule only in
# .git/info/exclude, a global ignore file or a nested .gitignore does not
# count, nor does one that ignores only some files there, such as
# .claude/memory/*.md, or .claude/memory/* with a negation after it, nor
# one that ignores all of .claude/); or when git, tr or grep fails or the
# script is interrupted (nothing is counted as clean then). Memory is kept
# in the owner's private store (see CLAUDE.md), never in a commit.
#
# The self-test runs first, in a subshell with the GIT_* repository
# variables, GIT_DEFAULT_HASH and GIT_TEMPLATE_DIR unset, no global or
# system configuration, and HOME and XDG_CONFIG_HOME inside a private
# mktemp -d, on throwaway repositories there. It shows the check fails on
# each spelling above, on a symlink and a submodule at .claude/memory, on a
# nested clone staged with add -A, on a missing ignore rule, on negated or
# narrower rules that leave memory files not ignored (.claude/memory/*.md,
# .claude/memory/probe.md, .claude/memory/* with a negation after it), on
# a rule only in .git/info/exclude, a global ignore file or
# .claude/.gitignore, outside a repository, when ls-files, ls-files -s,
# check-ignore, tr or either grep alone fails (a shim first on PATH); and
# that it passes on a clean repository, also when a global ignore file or
# .git/info/exclude lists .claude/. The real check then reads the index git
# hands it.
#
# Usage: tests/check-no-tracked-memory.sh
set -eu
CDPATH='' cd "$(dirname "$0")/.."
me=check-no-tracked-memory
tab=$(printf '\t')
tmp=$(mktemp -d "${TMPDIR:-/tmp}/nomem.XXXXXX")
chmod 700 "${tmp:?}"
# Absolute, so a relative TMPDIR is not resolved again against a -C directory.
tmp=$(CDPATH='' cd "$tmp" && pwd)
# Cleanup runs only on exit; a signal exits non-zero (and so cleans up)
# instead of resuming with the scratch files gone.
trap 'rm -rf "${tmp:?}"' EXIT
trap 'exit 1' HUP INT TERM

# A throwaway git directory made without templates (so no info/exclude)
# for the ignore probe in check().
if ! (unset GIT_DIR GIT_WORK_TREE GIT_COMMON_DIR GIT_INDEX_FILE GIT_OBJECT_DIRECTORY
	git init -q --bare --template= "${tmp:?}/probe.git") > /dev/null 2>&1; then
	echo "$me: git init failed; cannot tell whether .claude/memory/ is ignored" >&2
	exit 1
fi

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
	# Without -z, ls-files -s quotes a path that holds a newline, so each
	# entry is one line and starts with its mode.
	if ! git -C "$1" ls-files -s > "${tmp:?}/stage" 2>/dev/null; then
		echo "$me: git ls-files -s failed; cannot tell whether the index holds a gitlink" >&2
		return 1
	fi
	rc=0
	n=$(grep -c '^160000 ' "${tmp:?}/stage") || rc=$?
	case "$rc" in
	0)
		echo "$me: $n gitlink(s) (mode 160000, a submodule or an embedded repository) in the index; this repository has none, and a clone of the memory store belongs outside every checkout (see CLAUDE.md); find them with git ls-files -s and git rm --cached <path>" >&2
		return 1
		;;
	1) ;;
	*)
		echo "$me: cannot scan the index entries" >&2
		return 1
		;;
	esac
	# The rule must be in the tree's own top-level .gitignore. The probe
	# runs on the work tree with the throwaway git directory and no global
	# ignore file, which would otherwise stand in for a missing rule or
	# decide first (a global ".claude/" wins over the tree's rule); -v
	# names the deciding rule.
	rc=0
	rule=$(unset GIT_COMMON_DIR GIT_INDEX_FILE GIT_OBJECT_DIRECTORY
		git -C "$1" --git-dir="${tmp:?}/probe.git" --work-tree=. \
			-c core.excludesFile=/dev/null check-ignore --no-index -v \
			-- .claude/memory/probe.md 2>/dev/null) || rc=$?
	case "$rc" in
	0) ;;
	1)
		echo "$me: .claude/memory/ is not ignored by .gitignore; add .claude/memory to .gitignore" >&2
		return 1
		;;
	*)
		echo "$me: git check-ignore failed; cannot tell whether .claude/memory/ is ignored" >&2
		return 1
		;;
	esac
	# -v prints "<source>:<line>:<pattern><TAB><path>" and exits 0 also when
	# the deciding pattern is a negation, which means "not ignored".
	case "$rule" in
	.gitignore:*) ;;
	*)
		echo "$me: .claude/memory/ is ignored only by a nested .gitignore; add .claude/memory to the top-level .gitignore" >&2
		return 1
		;;
	esac
	# The probe names one file, so a rule that ignores only some files
	# (.claude/memory/*.md, or .claude/memory/* with a negation after it)
	# could decide it. Only the directory rule itself counts: git then
	# excludes the whole directory, and no later rule can re-include a file
	# in it. The path holds no TAB, so the last TAB ends the pattern.
	line=${rule#*:}
	pat=${line#*:}
	line=${line%%:*}
	pat=${pat%"$tab"*}
	case "$pat" in
	.claude/memory | /.claude/memory | .claude/memory/ | /.claude/memory/) ;;
	*)
		echo "$me: the rule that decides .claude/memory/probe.md (.gitignore line $line) is not the directory rule .claude/memory this check requires (a narrower or negated rule can leave files there unignored); add .claude/memory to .gitignore" >&2
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

	# globalignore <name> <rule>: a global ignore file (the XDG default,
	# which GIT_CONFIG_GLOBAL does not turn off) holding <rule>.
	globalignore()
	{
		mkdir -p "${tmp:?}/$1/git"
		printf '%s\n' "$2" > "${tmp:?}/$1/git/ignore"
	}

	mkrepo clean
	check "$tmp/clean" 2>/dev/null ||
		{ echo "$me: selftest: a clean repository was rejected" >&2; exit 1; }

	globalignore xdg-claude .claude/
	(XDG_CONFIG_HOME=$tmp/xdg-claude; check "$tmp/clean") 2>/dev/null ||
		{ echo "$me: selftest: a global ignore of .claude/ hid the tree's rule" >&2; exit 1; }

	mkrepo excludeshadow
	mkdir -p "${tmp:?}/excludeshadow/.git/info"
	printf '%s\n' .claude/ >> "${tmp:?}/excludeshadow/.git/info/exclude"
	check "$tmp/excludeshadow" 2>/dev/null ||
		{ echo "$me: selftest: a .claude/ rule in .git/info/exclude hid the tree's rule" >&2; exit 1; }

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

	# A clone made inside a checkout and staged with add -A becomes a
	# gitlink at the clone's path.
	git init -q "${tmp:?}/origin"
	git -C "${tmp:?}/origin" -c user.name=selftest -c user.email=selftest@example.invalid \
		commit -q --allow-empty -m selftest
	mkrepo nested
	git clone -q "${tmp:?}/origin" "${tmp:?}/nested/store" 2>/dev/null
	git -C "${tmp:?}/nested" add -A 2>/dev/null
	expect_reject nested "a nested clone staged as an embedded repository"

	mkdir -p "${tmp:?}/noignore"
	git -C "${tmp:?}/noignore" init -q .
	expect_reject noignore "a missing ignore rule"

	mkdir -p "${tmp:?}/excludeonly"
	git -C "${tmp:?}/excludeonly" init -q .
	mkdir -p "${tmp:?}/excludeonly/.git/info"
	printf '%s\n' '.claude/memory' >> "${tmp:?}/excludeonly/.git/info/exclude"
	expect_reject excludeonly "an ignore rule only in .git/info/exclude"

	globalignore xdg-memory .claude/memory
	if (XDG_CONFIG_HOME=$tmp/xdg-memory; check "$tmp/noignore") 2>/dev/null; then
		echo "$me: selftest: an ignore rule only in a global ignore file was not detected" >&2
		exit 1
	fi

	mkdir -p "${tmp:?}/nestedignore/.claude"
	git -C "${tmp:?}/nestedignore" init -q .
	printf '%s\n' memory > "${tmp:?}/nestedignore/.claude/.gitignore"
	expect_reject nestedignore "an ignore rule only in .claude/.gitignore"

	mkdir -p "${tmp:?}/negall"
	git -C "${tmp:?}/negall" init -q .
	printf '%s\n' '!*.md' > "${tmp:?}/negall/.gitignore"
	expect_reject negall "a .gitignore holding only a negated pattern"

	mkdir -p "${tmp:?}/negmem"
	git -C "${tmp:?}/negmem" init -q .
	printf '%s\n' '.claude/memory/*' '!.claude/memory/*.md' > "${tmp:?}/negmem/.gitignore"
	expect_reject negmem "a negated rule re-including .claude/memory/*.md"

	# Rules that ignore the probed name but leave other memory files
	# unignored.
	# partial <name> <line>...: a repository whose .gitignore holds <line>s.
	partial()
	{
		mkdir -p "${tmp:?}/$1"
		git -C "${tmp:?}/$1" init -q .
		d=$1
		shift
		printf '%s\n' "$@" > "${tmp:?}/$d/.gitignore"
	}
	partial negone '.claude/memory/*' '!.claude/memory/MEMORY.md'
	expect_reject negone "a .claude/memory/* rule with MEMORY.md re-included"
	partial mdonly '.claude/memory/*.md'
	expect_reject mdonly "a rule ignoring only .claude/memory/*.md"
	partial probeonly '.claude/memory/probe.md'
	expect_reject probeonly "a rule ignoring only .claude/memory/probe.md"

	mkdir -p "${tmp:?}/notarepo"
	expect_reject notarepo "a git failure (not a repository)"

	# One command fails while the rest work, on the clean repository: a
	# shim first on PATH exits non-zero when its arguments hold <word> and
	# runs the real command otherwise.
	# shim <name> <command> <word> <status>
	shim()
	{
		real=$(command -v "$2")
		mkdir -p "${tmp:?}/shim-$1"
		printf '#!/bin/sh\ncase " $* " in *"%s"*) exit %s ;; esac\nexec "%s" "$@"\n' \
			"$3" "$4" "$real" > "${tmp:?}/shim-$1/$2"
		chmod 755 "${tmp:?}/shim-$1/$2"
		if (PATH=$tmp/shim-$1:$PATH; check "$tmp/clean") 2>/dev/null; then
			echo "$me: selftest: a failing $1 was counted as clean" >&2
			exit 1
		fi
	}
	shim ls-files git ' ls-files -z ' 128
	shim ls-files-s git ' ls-files -s ' 128
	shim check-ignore git ' check-ignore ' 128
	shim tr tr '' 1
	shim grep-paths grep 'claude/memory' 2
	shim grep-gitlinks grep '^160000' 2
}

( selftest ) || exit 1
check . || exit 1
echo "$me: ok"
