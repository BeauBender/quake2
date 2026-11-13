#!/bin/sh
# Invariant: .githooks/pre-push refuses a push that would publish the working
# machine's identity, and lets an ordinary push through.
#
# Self-test with no network: a throwaway bare repository under mktemp -d is the
# remote and a throwaway clone has a copy of the hook as its pre-push hook, so
# real `git push` runs feed the hook git's real stdin. The identity is a
# synthetic name handed over through the hook's test-only override
# YQ2_IDENTITY_TEST_EXTRA_WORD, plus (when long enough) this machine's real
# user name built at run time, to show the real words still count. Both stay in
# the temporary directory; nothing is written to the repository.
#
# Usage: tests/check-pre-push-hook.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C

hook=$(pwd)/.githooks/pre-push
[ -x "$hook" ] || { echo "check-pre-push-hook: $hook is missing or not executable"; exit 1; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/prepush.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
syn=zzq-synthetic-box
export YQ2_IDENTITY_TEST_EXTRA_WORD=$syn

git init -q --bare "$tmp/remote.git"
git init -q "$tmp/w"
mkdir "$tmp/hooks"
cp "$hook" "$tmp/hooks/pre-push"
git -C "$tmp/w" config core.hooksPath "$tmp/hooks"
w() { git -C "$tmp/w" -c user.name=t -c user.email=t@example.invalid -c commit.gpgsign=false "$@"; }
commit() { # $1 = message
	w commit -q --allow-empty -m "$1"
}
fails=0
note() { echo "check-pre-push-hook: FAIL: $*"; fails=1; }
leaks() { # does the captured output show a word? (it must not)
	# git echoes the remote's path, which can sit under a directory named
	# after the user; that is the test's own temp path, not the hook's output.
	sed "s|$tmp|TMP|g" "$tmp/out" > "$tmp/out.clean"
	mv "$tmp/out.clean" "$tmp/out"
	if grep -qiF -e "$syn" "$tmp/out"; then note "output printed the synthetic value ($1)"; fi
	for _real in "$(hostname -s 2>/dev/null || true)" "$(whoami 2>/dev/null || true)"; do
		[ "${#_real}" -ge 3 ] || continue
		if grep -qiF -e "$_real" "$tmp/out"; then note "output printed a real identity value ($1)"; fi
	done
}
push() { # $1 = label, $2 = want (ok|refused); rest = push args
	_label=$1; _want=$2; shift 2
	_rc=0
	w push -q "$@" > "$tmp/out" 2>&1 || _rc=$?
	case $_want in
	ok) [ "$_rc" -eq 0 ] || { note "$_label: expected the push to go through"; cat "$tmp/out"; } ;;
	refused)
		[ "$_rc" -ne 0 ] || note "$_label: expected the push to be refused"
		grep -q 'pre-push: refused' "$tmp/out" || note "$_label: refusal did not come from the hook"
		;;
	esac
	leaks "$_label"
}

commit "first"
w branch -M main
w remote add origin "$tmp/remote.git"
push "normal branch" ok origin main

# --- ref names ---
w branch "telemetry/$syn"
push "ref name with the synthetic word" refused origin "telemetry/$syn"
w branch "telemetry/ZZQ-SYNTHETIC-BOX-2"
push "ref name, other case" refused origin "telemetry/ZZQ-SYNTHETIC-BOX-2"
w branch plain-topic
push "second normal branch" ok origin plain-topic

# --- messages: update of an existing branch ---
commit "fine message"
push "update with clean commits" ok origin main
commit "ran on $syn today"
push "update with a message naming the host" refused origin main
w reset -q --hard HEAD~1
commit "another fine message"
push "update after rewording" ok origin main

# --- messages: new branch scans only what the remote lacks ---
w checkout -q -b topic2
commit "work on ZZQ-Synthetic-Box"
push "new branch with a bad message" refused origin topic2
w reset -q --hard main
commit "clean new branch work"
push "new branch with clean message" ok origin topic2

# --- the real words count too (user name, as a whole word) ---
user=$(whoami 2>/dev/null || true)
if [ "${#user}" -ge 3 ]; then
	w checkout -q -b topic3 main
	commit "handoff note for $user"
	push "real user name in a message" refused origin topic3
	w reset -q --hard main
fi

# --- claim branches: message scan exempt, ref name never ---
w checkout -q -b claim-src main
commit "claimed by $syn"
push "non-claim branch with the same commit" refused origin claim-src:refs/heads/other/TASK1
push "claim branch whose name has the host" refused origin "claim-src:refs/heads/claim/$syn"
push "claim branch with the host in the message" ok origin claim-src:refs/heads/claim/TASK1

# --- deletion is allowed even for a leaked name ---
w checkout -q main
sha=$(w rev-parse main)
rc=0
printf '(delete) 0000000000000000000000000000000000000000 refs/heads/telemetry/%s %s\n' "$syn" "$sha" |
	(cd "$tmp/w" && sh "$hook" origin x) > "$tmp/out" 2>&1 || rc=$?
[ "$rc" -eq 0 ] || note "deleting a ref was refused"

# --- cap: a new branch with more commits than the cap is refused ---
w checkout -q -b long main
commit c1; commit c2; commit c3
rc=0
YQ2_IDENTITY_TEST_SCAN_CAP=2 w push -q origin long > "$tmp/out" 2>&1 || rc=$?
[ "$rc" -ne 0 ] && grep -q 'more than 2 commits' "$tmp/out" || note "scan cap was not enforced"
rc=0
YQ2_IDENTITY_TEST_SCAN_CAP=999999 w push -q origin long > "$tmp/out" 2>&1 || rc=$?
[ "$rc" -eq 0 ] || note "a test cap above the default must be ignored, not break the push"

# --- fail closed: unknown remote commit cannot be listed ---
rc=0
printf 'refs/heads/main %s refs/heads/main 1111111111111111111111111111111111111111\n' "$sha" |
	(cd "$tmp/w" && sh "$hook" origin x) > "$tmp/out" 2>&1 || rc=$?
[ "$rc" -ne 0 ] || note "an unlistable range must refuse the push"

# --- the hook runs under dash and bash 3.2 style shells too ---
for sh_bin in dash bash; do
	command -v "$sh_bin" >/dev/null 2>&1 || continue
	rc=0
	printf 'refs/heads/x %s refs/heads/telemetry/%s 0000000000000000000000000000000000000000\n' "$sha" "$syn" |
		(cd "$tmp/w" && "$sh_bin" "$hook" origin x) > "$tmp/out" 2>&1 || rc=$?
	[ "$rc" -ne 0 ] || note "$sh_bin: bad ref name was not refused"
done

[ "$fails" -eq 0 ] && echo "check-pre-push-hook: ok"
exit "$fails"
