#!/bin/sh
# Invariant: the two identity checks are immune to the git environment and
# configuration of whoever runs them, and the real scan still reads the index
# it is handed.
#
# A pre-commit hook runs the checks with GIT_INDEX_FILE (and in some cases
# GIT_DIR, GIT_WORK_TREE, GIT_PREFIX) exported; a caller can also export
# GIT_CONFIG_* and GIT_AUTHOR_*/GIT_COMMITTER_* or have global or system
# configuration that turns on hooks, signing or colour. This runs
# tests/check-no-local-identity.sh and tests/check-pre-push-hook.sh with all
# of that set to hostile values: GIT_INDEX_FILE names a copy of the index in
# a temporary directory, the configuration points hooks at scripts that fail,
# signs with a program that fails, forces colour, and the author identity
# holds the self-tests' synthetic host name. Both must pass and the index
# copy must be byte-identical afterwards. Last, the real part of the gate
# check is run with `hostname` and `whoami` shims (synthetic names), to show
# that a hit fails without printing the value, that unreadable names fail
# closed, that a user name equal to origin's GitHub owner is skipped with a
# note that does not print it, and that another user name is not skipped even
# when the home ends in it. Nothing in the repository is written.
#
# Usage: tests/check-identity-env.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C
me=check-identity-env

top=$(pwd -P)
gitdir=$(cd "$(git rev-parse --git-dir)" && pwd -P)
index=${GIT_INDEX_FILE:-$(git rev-parse --git-path index)}
[ -f "$index" ] || { echo "$me: cannot find the index"; exit 1; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/identenv.XXXXXX")
# A shell error must not exit 0 (bash 3.2 loses the status in an EXIT trap).
trap 'rm -rf "$tmp"; [ "${_ok:-}" = 1 ] || exit 1' EXIT
cp "$index" "$tmp/index"
cp "$index" "$tmp/index.orig"
mkdir "$tmp/hooks"
for h in pre-commit commit-msg pre-push post-commit; do
	printf '#!/bin/sh\necho hostile-hook-ran >&2\nexit 1\n' > "$tmp/hooks/$h"
	chmod +x "$tmp/hooks/$h"
done
cat > "$tmp/hostile.gitconfig" <<EOF
[core]
	hooksPath = $tmp/hooks
[commit]
	gpgSign = true
[tag]
	gpgSign = true
[gpg]
	program = false
[color]
	ui = always
[init]
	templateDir = $tmp/hooks-template
[grep]
	column = true
	lineNumber = true
[core]
	excludesFile = $tmp/excludes
EOF
printf '*\n' > "$tmp/excludes"
# The same configuration as the caller's own global files, for git to find
# if a check let it fall back to them. The home directory ends in the user
# name, as a real one does (the owner-handle skip of the gate check needs
# that; see home_is there).
hhome=$tmp/home/$(whoami 2>/dev/null || true)
mkdir -p "$hhome" "$tmp/xdg/git"
cp "$tmp/hostile.gitconfig" "$hhome/.gitconfig"
cp "$tmp/hostile.gitconfig" "$tmp/xdg/git/config"

# redact: paths of this checkout and the temp directory become placeholders;
# a line holding the host or user name or the home path is withheld whole.
redact() {
	YQ2_P="$tmp
$top
$gitdir" YQ2_R="$(hostname -s 2>/dev/null || true)
$(whoami 2>/dev/null || true)
${HOME:-}" awk '
	BEGIN { np = split(ENVIRON["YQ2_P"], p, "\n"); nr = split(ENVIRON["YQ2_R"], r, "\n") }
	{
		for (i = 1; i <= np; i++)
			while (p[i] != "" && (k = index($0, p[i])) > 0) $0 = substr($0, 1, k - 1) "<path>" substr($0, k + length(p[i]))
		l = tolower($0)
		for (i = 1; i <= nr; i++)
			if (length(r[i]) >= 3 && index(l, tolower(r[i]))) { print "  <line withheld: it holds an identity value>"; next }
		print "  " $0
	}'
}

fails=0
for check in tests/check-no-local-identity.sh tests/check-pre-push-hook.sh; do
	rc=0
	env GIT_INDEX_FILE="$tmp/index" GIT_DIR="$gitdir" GIT_WORK_TREE="$top" GIT_PREFIX=src/ \
		GIT_CONFIG_GLOBAL="$tmp/hostile.gitconfig" GIT_CONFIG_SYSTEM="$tmp/hostile.gitconfig" \
		GIT_CONFIG_COUNT=2 GIT_CONFIG_KEY_0=core.hooksPath GIT_CONFIG_VALUE_0="$tmp/hooks" \
		GIT_CONFIG_KEY_1=commit.gpgSign GIT_CONFIG_VALUE_1=true \
		GIT_AUTHOR_NAME=zzq-synthetic-box GIT_AUTHOR_EMAIL=zzqa@zzq-synthetic-box.zzq \
		GIT_COMMITTER_NAME=zzq-synthetic-box GIT_COMMITTER_EMAIL=zzqa@zzq-synthetic-box.zzq \
		GIT_ALTERNATE_OBJECT_DIRECTORIES="$tmp/none" HOME="$hhome" XDG_CONFIG_HOME="$tmp/xdg" \
		"$check" > "$tmp/out" 2>&1 || rc=$?
	if [ "$rc" -ne 0 ]; then
		echo "$me: FAIL: $check failed under a hostile git environment; its output:"
		redact < "$tmp/out"
		fails=1
	fi
	if ! cmp -s "$tmp/index" "$tmp/index.orig"; then
		echo "$me: FAIL: $check changed the index named by GIT_INDEX_FILE"
		fails=1
		cp "$tmp/index.orig" "$tmp/index"
	fi
done
# The real part of the gate check, with `hostname` and `whoami` shims: a hit
# fails and is reported without the value; unreadable names fail closed
# (both) or warn (one).
mkdir "$tmp/bin"
shim() { # $1 = command, $2 = what it prints ("" = fails)
	if [ -n "$2" ]; then
		printf '#!/bin/sh\necho %s\n' "$2" > "$tmp/bin/$1"
	else
		printf '#!/bin/sh\nexit 1\n' > "$tmp/bin/$1"
	fi
	chmod +x "$tmp/bin/$1"
}
gate() { # $1 = label, $2 = wanted status, $3 = text the output must hold, [$4 = HOME]
	rc=0
	PATH="$tmp/bin:$PATH" HOME="${4:-/zzqno""home}" tests/check-no-local-identity.sh > "$tmp/out" 2>&1 || rc=$?
	if [ "$rc" -ne "$2" ] || ! grep -q "$3" "$tmp/out"; then
		echo "$me: FAIL: $1: status $rc, wanted $2 and \"$3\"; its output:"
		redact < "$tmp/out"
		fails=1
	fi
	if grep -qi zzq-synthetic-box "$tmp/out"; then
		echo "$me: FAIL: $1: the output printed the matched value"
		fails=1
	fi
}
# The self-tests of these checks hold the synthetic host name, so it is a hit.
# The other two values are split in this file so the tree does not hold them.
shim hostname zzq-synthetic-box
shim whoami "zzqno""body"
gate "a host name found in tracked files" 1 "tracked files contain this machine's identity"
shim hostname ''
gate "host name unreadable" 0 "warning: cannot read the host or user name"
shim whoami ''
gate "host and user name unreadable" 1 "cannot read the host name or the user name"
# A user name that is not origin's owner, with a home ending in it: nothing is
# skipped, so no note (the note needs owner_is as well as home_is).
shim hostname "zzqno""host"
shim whoami "zzqno""body"
gate "a user name that is not the owner, home ending in it" 0 "check-no-local-identity: ok" "/zzqno""home/zzqno""body"
if grep -q "skipped" "$tmp/out"; then
	echo "$me: FAIL: a user name that is not the owner, home ending in it: the skip note was printed"
	fails=1
fi
# The user name is the owner handle of origin's GitHub address and the last
# component of the home path: the user-name word test is skipped and a note
# says so, without the value. When origin names no GitHub owner these tests
# are not run, and a line says so.
owner=$(git remote get-url origin 2>/dev/null | sed -nE 's#^.*github\.com[:/]([A-Za-z0-9-]+)/[^/]+$#\1#p') || owner=
if [ -n "$owner" ]; then
	shim hostname "zzqno""host"
	shim whoami "$owner"
	gate "the user name is the owner handle" 0 "the user-name word test is skipped" "/zzqno""home/$owner"
	if grep -qiF -e "$owner" "$tmp/out"; then
		echo "$me: FAIL: the user name is the owner handle: the output printed the user name"
		fails=1
	fi
	# A home path that does not end in the user name: nothing is skipped.
	gate "the owner handle, home elsewhere" 1 "tracked files contain this machine's identity"
	if grep -q "skipped" "$tmp/out"; then
		echo "$me: FAIL: the owner handle, home elsewhere: the skip note was printed"
		fails=1
	fi
else
	echo "$me: not run: the owner-handle note tests (origin names no GitHub owner)"
fi

[ "$fails" -eq 0 ] || exit 1
_ok=1
echo "$me: ok"
