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
# that a hit fails without printing the value and that unreadable names fail
# closed. Nothing in the repository is written.
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
trap 'rm -rf "$tmp"' EXIT
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
		GIT_ALTERNATE_OBJECT_DIRECTORIES="$tmp/none" \
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
gate() { # $1 = label, $2 = wanted status, $3 = text the output must hold
	rc=0
	PATH="$tmp/bin:$PATH" HOME="/zzqno""home" tests/check-no-local-identity.sh > "$tmp/out" 2>&1 || rc=$?
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

[ "$fails" -eq 0 ] || exit 1
echo "$me: ok"
