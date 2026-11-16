#!/bin/sh
# Invariant: .github/scripts/codeql-changes.sh, which decides whether the
# CodeQL workflow analyses a change, skips the analyses (analyze=false) only
# for a pull_request event whose HEAD is a two-parent merge with a non-empty
# diff in which every changed path is Markdown under doc/ or docs/ or at the
# top level, and analyses (analyze=true) in every other case.
#
# Each case builds a throwaway repository under a private mktemp -d with the
# GIT_* repository and configuration variables unset, no global or system
# configuration, no hooks, no signing and a synthetic author: a base commit, a
# pull request commit on a branch, and a git merge --no-ff of it into main,
# the shape of the pull request checkout in Actions (HEAD^1 is the base). The
# script runs there with GITHUB_EVENT_NAME=pull_request and GITHUB_OUTPUT set
# to a file, and the case checks the one analyze= line in that file, an exit
# status of 0, a log that names no file, and no file list left behind in
# TMPDIR.
#
# Skipped (false): Markdown changed under docs/, under doc/, at the top level,
# and all of them at once with an added and a deleted page.
# Analysed (true): src/*.c, tests/unit/*.c, Makefile, third_party/*.make,
# .github/workflows/*.yml, Markdown under .github/, scripts/*.sh,
# tests/check-*.sh; config.mk plus docs/payload.h (the Makefile includes
# config.mk); a rename of src/a.c to docs/a.c and of src/a.c to docs/a.md
# (--no-renames lists the deletion); a non-ASCII src/ name; a src/ name and a
# docs/ Markdown name that hold a double quote (git quotes both); a deleted
# src/ file; an empty diff; a HEAD that is a single-parent commit, a root
# commit or a three-parent merge; a diff that fails (the base tree object
# removed); a push event and an unset event. The decision goes to stdout when
# GITHUB_OUTPUT is unset.
#
# Usage: tests/check-codeql-changes.sh
set -eu
CDPATH='' cd "$(dirname "$0")/.."
export LC_ALL=C
script=$(pwd)/.github/scripts/codeql-changes.sh
me=check-codeql-changes
tmp=$(mktemp -d "${TMPDIR:-/tmp}/codeqlchg.XXXXXX")
chmod 700 "${tmp:?}"
tmp=$(CDPATH='' cd "${tmp:?}" && pwd -P)
# A shell error must not exit 0 (bash 3.2 loses the status in an EXIT trap).
trap 'rm -rf "${tmp:?}"; [ "${_ok:-}" = 1 ] || exit 1' EXIT
trap 'exit 1' HUP INT TERM

[ -f "$script" ] || { echo "$me: missing $script" >&2; exit 1; }

unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_OBJECT_DIRECTORY \
	GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE GIT_PREFIX GIT_CONFIG \
	GIT_CONFIG_PARAMETERS GIT_CONFIG_COUNT GIT_DEFAULT_HASH GIT_TEMPLATE_DIR \
	GIT_AUTHOR_DATE GIT_COMMITTER_DATE GIT_EXTERNAL_DIFF GIT_DIFF_OPTS \
	GITHUB_OUTPUT GITHUB_EVENT_NAME RUNNER_TEMP
HOME=$tmp/home
XDG_CONFIG_HOME=$tmp/home/.config
GIT_CONFIG_GLOBAL=/dev/null
GIT_CONFIG_NOSYSTEM=1
GIT_CEILING_DIRECTORIES=$tmp
GIT_AUTHOR_NAME=selftest
GIT_AUTHOR_EMAIL=selftest@example.invalid
GIT_COMMITTER_NAME=selftest
GIT_COMMITTER_EMAIL=selftest@example.invalid
GIT_MERGE_AUTOEDIT=no
export HOME XDG_CONFIG_HOME GIT_CONFIG_GLOBAL GIT_CONFIG_NOSYSTEM \
	GIT_CEILING_DIRECTORIES GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL \
	GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL GIT_MERGE_AUTOEDIT
mkdir -p "${tmp:?}/home" "${tmp:?}/scratch"

failed=0
cases=0

bad()
{
	echo "$me: $*" >&2
	failed=$((failed + 1))
}

g()
{
	git -c core.hooksPath=/dev/null -c commit.gpgSign=false \
		-c init.defaultBranch=main "$@"
}

# The base tree every repository starts from.
base_tree()
{
	mkdir -p src docs/sub doc tests/unit third_party scripts .github/workflows
	echo 'int a;' > src/a.c
	echo 'int b;' > src/b.c
	echo '# x' > docs/x.md
	echo '# old' > docs/old.md
	echo '# y' > doc/y.md
	echo '# readme' > README.md
	echo 'all:' > Makefile
	echo 'X = 1' > third_party/x.make
	echo 'int t;' > tests/unit/t.c
	echo 'exit 0' > tests/check-x.sh
	echo 'exit 0' > scripts/s.sh
	echo 'name: w' > .github/workflows/w.yml
	echo '# template' > .github/pull_request_template.md
}

# The changes a pull request makes, one function per case, run in the
# repository on the pull request branch.
ch_docs() { echo more >> docs/x.md; }
ch_doc() { echo more >> doc/y.md; }
ch_top() { echo more >> README.md; }
ch_alldocs()
{
	echo more >> docs/x.md
	echo '# z' > docs/sub/z.md
	echo more >> doc/y.md
	echo '# notes' > NOTES.md
	rm docs/old.md
}
ch_src() { echo 'int a2;' >> src/a.c; }
ch_unit() { echo 'int t2;' >> tests/unit/t.c; }
ch_makefile() { echo 'x:' >> Makefile; }
ch_make() { echo 'Y = 2' >> third_party/x.make; }
ch_workflow() { echo 'on: push' >> .github/workflows/w.yml; }
ch_ghmd() { echo more >> .github/pull_request_template.md; }
ch_scripts() { echo 'exit 1' >> scripts/s.sh; }
ch_testsh() { echo 'exit 1' >> tests/check-x.sh; }
ch_configmk()
{
	echo 'CFLAGS += -include docs/payload.h' > config.mk
	echo 'int payload;' > docs/payload.h
	echo more >> docs/x.md
}
ch_rename_c() { g mv src/a.c docs/a.c; }
ch_rename_md() { g mv src/a.c docs/a.md; }
ch_nonascii() { echo 'int e;' > "$(printf 'src/b\303\251.c')"; }
ch_quote_src() { echo 'int q;' > 'src/a"b.c'; }
ch_quote_doc() { echo '# q' > 'docs/a"b.md'; }
ch_delete() { rm src/b.c; }
ch_empty() { :; }

# build <dir> <shape> <change>: a repository in <dir> whose HEAD has the
# <shape>: merge (the change merged --no-ff into main), commit (HEAD is the
# change's own commit, one parent), root (HEAD is the base commit, no
# parent, the change is not made), octopus (the change and a second
# documentation change merged --no-ff into main at once, three parents),
# nobase (a merge whose first parent's tree object is removed, so the diff
# fails). A failure here exits the check.
build()
{
	(
		mkdir "$1"
		cd "$1"
		g init -q
		base_tree
		g add --all
		g commit -q -m base
		if [ "$2" = root ]; then
			exit 0
		fi
		g checkout -q -b pr
		"$3"
		g add --all
		g commit -q --allow-empty -m pr
		case $2 in
		merge|nobase)
			g checkout -q main
			g merge -q --no-ff -m merge pr
			;;
		octopus)
			g checkout -q -b pr2 main
			echo other >> docs/old.md
			g add --all
			g commit -q -m pr2
			g checkout -q main
			g merge -q --no-ff -m merge pr pr2 > /dev/null
			;;
		esac
		if [ "$2" = nobase ]; then
			tree=$(g rev-parse 'HEAD^1^{tree}')
			obj=.git/objects/$(printf '%s' "$tree" | cut -c1-2)/$(printf '%s' "$tree" | cut -c3-)
			[ -f "$obj" ]
			rm -f "$obj"
		fi
	)
}

# run <dir> <event> <output>: run the script in <dir>, its stdout and
# stderr into $log. "-" as <event> leaves GITHUB_EVENT_NAME unset, "-" as
# <output> leaves GITHUB_OUTPUT unset.
run()
{
	(
		cd "$1" || exit 1
		if [ "$2" != - ]; then
			GITHUB_EVENT_NAME=$2
			export GITHUB_EVENT_NAME
		fi
		if [ "$3" != - ]; then
			GITHUB_OUTPUT=$3
			export GITHUB_OUTPUT
		fi
		TMPDIR=$tmp/scratch
		export TMPDIR
		exec bash "$script"
	) > "$log" 2>&1
}

# check <name> <want> <event> <shape> <change>
check()
{
	name=$1
	want=$2
	cases=$((cases + 1))
	build "$tmp/$name" "$4" "$5"
	out=$tmp/$name.out
	log=$tmp/$name.log
	: > "$out"
	if run "$tmp/$name" "$3" "$out"; then
		status=0
	else
		status=$?
	fi
	got=$(cat "$out")
	if [ "$status" -ne 0 ]; then
		bad "$name: exit status $status"
	fi
	if [ "$got" != "analyze=$want" ]; then
		bad "$name: want analyze=$want, got '$got'"
	fi
	if grep -qF -e / -e .md -e .mk -e .c -e .h -e Makefile -e README "$log"; then
		bad "$name: the log names a file"
	fi
	if [ -n "$(ls -A "$tmp/scratch")" ]; then
		bad "$name: a file list was left in TMPDIR"
		rm -f "${tmp:?}"/scratch/*
	fi
}

check docs false pull_request merge ch_docs
check doc false pull_request merge ch_doc
check top false pull_request merge ch_top
check alldocs false pull_request merge ch_alldocs

check src true pull_request merge ch_src
check unit true pull_request merge ch_unit
check makefile true pull_request merge ch_makefile
check make true pull_request merge ch_make
check workflow true pull_request merge ch_workflow
check github-md true pull_request merge ch_ghmd
check scripts true pull_request merge ch_scripts
check tests-sh true pull_request merge ch_testsh
check config-mk true pull_request merge ch_configmk
check rename-c true pull_request merge ch_rename_c
check rename-md true pull_request merge ch_rename_md
check non-ascii true pull_request merge ch_nonascii
check quote-src true pull_request merge ch_quote_src
check quote-doc true pull_request merge ch_quote_doc
check delete true pull_request merge ch_delete
check empty true pull_request merge ch_empty

check not-merge true pull_request commit ch_docs
check root true pull_request root ch_docs
check octopus true pull_request octopus ch_docs
check diff-fails true pull_request nobase ch_docs
check push true push merge ch_docs
check no-event true - merge ch_docs

# GITHUB_OUTPUT unset: the decision is the last line of stdout.
cases=$((cases + 1))
log=$tmp/stdout.log
if run "$tmp/docs" pull_request -; then
	if [ "$(tail -n 1 "$log")" != "analyze=false" ]; then
		bad "stdout: want analyze=false as the last line of stdout"
	fi
else
	bad "stdout: exit status $?"
fi

# A diff that writes a documentation-only list and then fails analyses.
mkdir "${tmp:?}/failgit"
realgit=$(command -v git)
printf '%s\n' '#!/bin/sh' "real='$realgit'" 'for a in "$@"; do' \
	'	[ "$a" = diff ] && { "$real" "$@"; exit 1; }' 'done' \
	'exec "$real" "$@"' > "${tmp:?}/failgit/git"
chmod 755 "${tmp:?}/failgit/git"
cases=$((cases + 1))
log=$tmp/diff-partial.log
out=$tmp/diff-partial.out
: > "$out"
if (PATH=$tmp/failgit:$PATH; export PATH; run "$tmp/docs" pull_request "$out"); then
	[ "$(cat "$out")" = analyze=true ] || bad "diff-partial: want analyze=true, got '$(cat "$out")'"
else
	bad "diff-partial: exit status $?"
fi

if [ "$failed" -ne 0 ]; then
	echo "$me: $failed failures in $cases cases" >&2
	exit 1
fi
echo "$me: ok ($cases cases)"
_ok=1
