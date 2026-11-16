#!/bin/bash
# Decides whether the CodeQL workflow (.github/workflows/codeql.yml) runs its
# analyses, and writes analyze=true or analyze=false to $GITHUB_OUTPUT, or to
# stdout when GITHUB_OUTPUT is unset or empty.
#
# The analyses are skipped (false) only when all of these hold: the event is
# pull_request; HEAD, the merge commit the pull request is checked out as, has
# exactly two parents (HEAD^1 is the base); the diff from HEAD^1 to HEAD is
# non-empty; and every changed path is on the documentation list: a Markdown
# file (*.md) under doc/ or docs/, or at the top level, whose name is spelled
# in ASCII letters, digits, '.', '_' and '-'. Everything else analyses
# (true): any other event, a HEAD with fewer or more parents, a failed git,
# mktemp or grep, an empty diff, and any other path, including every name git
# quotes (a double quote, a backslash, a control character) and every
# non-ASCII name. Renames are listed as a deletion and an addition
# (--no-renames), so moving a file out of src/ counts as a change to src/.
#
# The file list is written to a file and matched there, never through a
# pipe, so an early-exiting reader cannot turn a SIGPIPE into a decision.
# Only counts are printed: no file name from the pull request reaches the
# log. The decision is written once and the script exits 0; it exits non-zero
# only when the decision cannot be written, and the workflow then analyses.
#
# Tested by tests/check-codeql-changes.sh.
#
# Usage: .github/scripts/codeql-changes.sh   (in the checkout; bash 3.2 or later)
set -u
export LC_ALL=C

me=codeql-changes
docs='^((doc|docs)/([A-Za-z0-9._-]+/)*)?[A-Za-z0-9._-]+\.md$'
merge='^[0-9a-f]+ [0-9a-f]+ [0-9a-f]+$'
list=

# decide <true|false>: write the decision, remove the file list, exit.
decide()
{
	if [ -n "$list" ]; then
		rm -f "$list"
	fi
	if [ -n "${GITHUB_OUTPUT:-}" ]; then
		printf 'analyze=%s\n' "$1" >> "$GITHUB_OUTPUT" || exit 1
	else
		printf 'analyze=%s\n' "$1" || exit 1
	fi
	exit 0
}

if [ "${GITHUB_EVENT_NAME:-}" != "pull_request" ]; then
	echo "$me: not a pull_request event; analysing"
	decide true
fi

if ! parents=$(git rev-list --parents -n1 HEAD 2>/dev/null); then
	echo "$me: git rev-list failed; analysing"
	decide true
fi
if ! [[ $parents =~ $merge ]]; then
	echo "$me: HEAD is not a two-parent merge; analysing"
	decide true
fi

if ! list=$(mktemp "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/codeql-changes.XXXXXX"); then
	list=
	echo "$me: mktemp failed; analysing"
	decide true
fi

if ! git -c core.quotePath=false diff --no-renames --name-only 'HEAD^1' HEAD \
	> "$list" 2>/dev/null; then
	echo "$me: git diff failed; analysing"
	decide true
fi
if [ ! -s "$list" ]; then
	echo "$me: the diff is empty; analysing"
	decide true
fi

total=$(wc -l < "$list")
total=$((total))
other=$(grep -Evc -- "$docs" "$list")
status=$?
if [ "$status" -eq 1 ] && [ "$other" = 0 ]; then
	echo "$me: $total changed paths, all on the documentation list; skipping"
	decide false
fi
if [ "$status" -eq 0 ]; then
	echo "$me: $total changed paths, $other not on the documentation list; analysing"
else
	echo "$me: grep failed (status $status); analysing"
fi
decide true
