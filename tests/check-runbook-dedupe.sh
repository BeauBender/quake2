#!/bin/sh
# Invariant: scripts/dedupe-runbook-index.sh turns the duplicate row a union
# merge of main leaves in docs/runbook/INDEX.md into main's row, and refuses
# (changing nothing) a duplicate main has no row for.
#
# Cases, each in a scratch git repository that holds copies of the real
# .gitattributes, the dedupe script and tests/check-runbook-index.sh:
#   1. main edits a row the branch carries and the branch adds a row next to
#      it; merging main keeps both versions of the row (the fixture asserts
#      it), the index check fails with "indexed twice", the dedupe leaves
#      exactly main's row, and the index check passes. No temporary file is
#      left in the index's directory.
#   2. a row indexed twice that main has no row for: the dedupe exits non-zero,
#      names the card, and the index is byte for byte as it was; the check
#      still fails.
#   3. case 1 again with GIT_DIR, GIT_WORK_TREE and GIT_INDEX_FILE naming a
#      decoy repository: same result, decoy untouched.
#   4. no duplicate: the dedupe exits 0 and changes nothing.
#   5. main only mentions "- [FIX10](" in a header and has no FIX10 row while
#      the branch has two: refused, index unchanged.
#   6. a revision that is a git option (--output=x): refused, nothing written.
#   7. docs/runbook or INDEX.md a symbolic link: refused.
#
# The dedupe runs (dd) use the same scratch HOME and configuration. The
# scratch git calls (sg) unset every inherited GIT_* variable, point HOME
# and XDG_CONFIG_HOME at scratch, read no global or system configuration, and
# git init takes no template.
#
# Usage: tests/check-runbook-dedupe.sh
set -eu
CDPATH='' cd "$(dirname "$0")/.."
export LC_ALL=C
me=check-runbook-dedupe

unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_OBJECT_DIRECTORY \
	GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE GIT_PREFIX GIT_CONFIG \
	GIT_CONFIG_PARAMETERS GIT_CONFIG_COUNT GIT_DEFAULT_HASH GIT_TEMPLATE_DIR \
	GIT_AUTHOR_DATE GIT_COMMITTER_DATE GIT_EXTERNAL_DIFF GIT_DIFF_OPTS \
	GIT_SSH GIT_SSH_COMMAND GIT_ASKPASS GIT_EDITOR GIT_SEQUENCE_EDITOR

real=$(pwd -P)
index=docs/runbook/INDEX.md
tmp=$(mktemp -d "${TMPDIR:-/tmp}/runbookdedupe.XXXXXX")
tmp=$(CDPATH='' cd "${tmp:?}" && pwd -P)
trap 'rm -rf "${tmp:?}"' EXIT
mkdir -p "${tmp:?}/home/.config"

sg()
{
	HOME=$tmp/home XDG_CONFIG_HOME=$tmp/home/.config \
		GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 GIT_ATTR_NOSYSTEM=1 \
		GIT_CEILING_DIRECTORIES=$tmp GIT_MERGE_AUTOEDIT=no \
		GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@t \
		GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@t \
		git "$@"
}

# dd DIR ARG...: the dedupe script in DIR under the same scratch HOME and
# configuration as sg. Extra variables go in a subshell around dd.
dd()
{
	_d=$1
	shift
	(CDPATH='' cd "$_d" &&
		HOME=$tmp/home XDG_CONFIG_HOME=$tmp/home/.config \
		GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 GIT_ATTR_NOSYSTEM=1 \
		GIT_CEILING_DIRECTORIES=$tmp \
		sh scripts/dedupe-runbook-index.sh "$@")
}

fail=0
bad()
{
	echo "$me: $*"
	fail=1
}

row9_old='- [FIX9](../runbook/FIX9.md): old title'
row9_new='- [FIX9](../runbook/FIX9.md): new title'
row10='- [FIX10](../runbook/FIX10.md): ten'

# fixture DIR: a repository on branch "br" with main's merge done, so the index
# holds the union duplicate of FIX9. Returns 2 when a setup step fails.
fixture()
{
	d=$1
	sg init -q --template= "$d" || return 2
	mkdir -p "$d/docs/runbook" "$d/scripts" "$d/tests" || return 2
	cp .gitattributes "$d/.gitattributes" || return 2
	cp scripts/dedupe-runbook-index.sh "$d/scripts/" || return 2
	cp tests/check-runbook-index.sh "$d/tests/" || return 2
	printf '# Index\n\n%s\n' "$row9_old" > "$d/$index" || return 2
	printf '# FIX9\n' > "$d/docs/runbook/FIX9.md" || return 2
	printf '# FIX10\n' > "$d/docs/runbook/FIX10.md" || return 2
	printf '# README\n' > "$d/docs/runbook/README.md" || return 2
	sg -C "$d" add -A || return 2
	sg -C "$d" commit -q -m base || return 2
	sg -C "$d" branch -M main || return 2
	sg -C "$d" checkout -q -b br || return 2
	printf '%s\n' "$row10" >> "$d/$index" || return 2
	sg -C "$d" commit -q -a -m branch || return 2
	sg -C "$d" checkout -q main || return 2
	printf '# Index\n\n%s\n' "$row9_new" > "$d/$index" || return 2
	sg -C "$d" commit -q -a -m main || return 2
	sg -C "$d" checkout -q br || return 2
	sg -C "$d" merge -q -m merge main > /dev/null 2>&1 || return 2
}

check_in()
{
	(CDPATH='' cd "$1" && sh tests/check-runbook-index.sh --no-selftest)
}

count()
{
	grep -c -F -- "$2" "$1/$index" || true
}

# Case 1.
d=$tmp/c1
if ! fixture "$d"; then
	bad "case 1: fixture setup failed"
else
	if [ "$(count "$d" '- [FIX9](')" -ne 2 ]; then
		bad "case 1: the union merge did not leave two FIX9 rows, so the fixture proves nothing"
	fi
	if check_in "$d" > "${tmp:?}/o1" 2>&1 || ! grep -q 'indexed twice: FIX9' "${tmp:?}/o1"; then
		bad "case 1: the index check did not fail with 'indexed twice: FIX9' on the duplicate"
	fi
	rc=0
	dd "$d" main > "${tmp:?}/o1d" 2>&1 || rc=$?
	if [ "$rc" -ne 0 ]; then
		bad "case 1: the dedupe exited $rc: $(cat "${tmp:?}/o1d")"
	fi
	if [ "$(count "$d" '- [FIX9](')" -ne 1 ] ||
		! grep -Fqx -- "$row9_new" "$d/$index" ||
		grep -Fqx -- "$row9_old" "$d/$index" ||
		! grep -Fqx -- "$row10" "$d/$index"; then
		bad "case 1: the index is not main's FIX9 row plus the branch's FIX10 row:"
		cat "$d/$index"
	fi
	if ! check_in "$d" > "${tmp:?}/o1c" 2>&1; then
		bad "case 1: the index check fails after the dedupe: $(cat "${tmp:?}/o1c")"
	fi
	if [ "$(ls -A "$d/docs/runbook" | wc -l | tr -d ' ')" -ne 4 ]; then
		bad "case 1: the dedupe left a stray file in docs/runbook: $(ls -A "$d/docs/runbook" | tr '\n' ' ')"
	fi
fi

# Case 2: a duplicate main has no row for.
d=$tmp/c2
if ! fixture "$d"; then
	bad "case 2: fixture setup failed"
else
	printf '%s\n' "$row10" >> "$d/$index"
	cp "$d/$index" "${tmp:?}/c2.before"
	rc=0
	dd "$d" main > "${tmp:?}/o2d" 2>&1 || rc=$?
	if [ "$rc" -eq 0 ] || ! grep -q 'FIX10' "${tmp:?}/o2d"; then
		bad "case 2: the dedupe did not refuse a duplicate main has no row for (exit $rc): $(cat "${tmp:?}/o2d")"
	fi
	if ! cmp -s "$d/$index" "${tmp:?}/c2.before"; then
		bad "case 2: the refused dedupe changed the index"
	fi
	if check_in "$d" > "${tmp:?}/o2" 2>&1; then
		bad "case 2: the index check passed on a real duplicate"
	fi
fi

# Case 3: a decoy GIT_DIR must change nothing.
d=$tmp/c3
decoy=$tmp/decoy
sg init -q --template= "$decoy"
printf '%s\n' decoy > "$decoy/a.txt"
sg -C "$decoy" add a.txt
sg -C "$decoy" commit -q -m decoy
(CDPATH='' cd "$decoy" && find . -print | sort && find . -type f -exec cksum {} + | sort) > "${tmp:?}/decoy.before"
if ! fixture "$d"; then
	bad "case 3: fixture setup failed"
else
	rc=0
	(GIT_DIR=$decoy/.git GIT_WORK_TREE=$decoy GIT_INDEX_FILE=$decoy/.git/index
		GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.bare GIT_CONFIG_VALUE_0=true
		GIT_OBJECT_DIRECTORY=$decoy/.git/objects
		export GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_CONFIG_COUNT GIT_CONFIG_KEY_0 \
			GIT_CONFIG_VALUE_0 GIT_OBJECT_DIRECTORY
		dd "$d" main) > "${tmp:?}/o3d" 2>&1 || rc=$?
	if [ "$rc" -ne 0 ] || [ "$(count "$d" '- [FIX9](')" -ne 1 ] ||
		! grep -Fqx -- "$row9_new" "$d/$index"; then
		bad "case 3: with a decoy GIT_DIR the dedupe exited $rc or kept the wrong row: $(cat "${tmp:?}/o3d")"
	fi
fi
(CDPATH='' cd "$decoy" && find . -print | sort && find . -type f -exec cksum {} + | sort) > "${tmp:?}/decoy.after"
if ! cmp -s "${tmp:?}/decoy.before" "${tmp:?}/decoy.after"; then
	bad "case 3: the dedupe changed the decoy repository"
fi

# Case 4: no duplicate.
d=$tmp/c4
if ! fixture "$d"; then
	bad "case 4: fixture setup failed"
else
	dd "$d" main > /dev/null 2>&1 || true
	cp "$d/$index" "${tmp:?}/c4.before"
	rc=0
	dd "$d" main > "${tmp:?}/o4" 2>&1 || rc=$?
	if [ "$rc" -ne 0 ] || ! cmp -s "$d/$index" "${tmp:?}/c4.before"; then
		bad "case 4: the dedupe on an index with no duplicate exited $rc or changed it"
	fi
fi

# refused DIR NAME ARG...: the dedupe must exit non-zero and leave the index
# byte for byte as it was.
refused()
{
	_d=$1
	_n=$2
	shift 2
	cp "$_d/$index" "${tmp:?}/$_n.before"
	_rc=0
	dd "$_d" "$@" > "${tmp:?}/$_n.out" 2>&1 || _rc=$?
	if [ "$_rc" -eq 0 ]; then
		bad "$_n: the dedupe did not refuse (exit 0): $(cat "${tmp:?}/$_n.out")"
	fi
	if ! cmp -s "$_d/$index" "${tmp:?}/$_n.before"; then
		bad "$_n: the refused dedupe changed the index"
	fi
}

# Case 5: main mentions "- [FIX10](" in a header line but has no FIX10 row,
# and the branch has two FIX10 rows: refused, nothing changed.
d=$tmp/c5
if ! fixture "$d"; then
	bad "case 5: fixture setup failed"
else
	printf '# Index\n\nA row looks like: `- [FIX10](../runbook/FIX10.md): title`\n\n%s\n' "$row9_new" > "$d/$index"
	sg -C "$d" commit -q -a -m prose
	printf '%s\n%s\n' "$row10" "$row10" >> "$d/$index"
	refused "$d" case5 HEAD
	if ! grep -q 'FIX10' "${tmp:?}/case5.out"; then
		bad "case 5: the refusal does not name FIX10: $(cat "${tmp:?}/case5.out")"
	fi
fi

# Case 6: a revision that is a git option is refused, nothing is written.
d=$tmp/c6
if ! fixture "$d"; then
	bad "case 6: fixture setup failed"
else
	mkdir -p "$d/outx:docs/runbook"
	refused "$d" case6 --output=outx
	if [ -n "$(ls -A "$d/outx:docs/runbook")" ]; then
		bad "case 6: the option revision wrote a file"
	fi
fi

# Case 7: a symbolic link for docs/runbook or INDEX.md is refused.
d=$tmp/c7
if ! fixture "$d"; then
	bad "case 7: fixture setup failed"
elif mv "$d/docs/runbook" "$d/rb" && ln -s ../rb "$d/docs/runbook"; then
	_rc=0
	dd "$d" main > "${tmp:?}/o7" 2>&1 || _rc=$?
	[ "$_rc" -ne 0 ] || bad "case 7: a symlinked docs/runbook was accepted"
	rm "$d/docs/runbook" && mv "$d/rb" "$d/docs/runbook"
	mv "$d/$index" "$d/real-index" && ln -s ../../real-index "$d/$index"
	_rc=0
	dd "$d" main > "${tmp:?}/o7b" 2>&1 || _rc=$?
	[ "$_rc" -ne 0 ] || bad "case 7: a symlinked INDEX.md was accepted"
else
	echo "$me: case 7 skipped: cannot create a symbolic link here"
fi

cd "$real"
[ "$fail" -eq 0 ] && echo "$me: ok"
exit "$fail"
