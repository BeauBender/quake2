#!/bin/sh
# Invariant: .githooks/pre-push refuses a push that would publish the working
# machine's identity, and lets an ordinary push through.
#
# Self-test with no network: a throwaway bare repository under mktemp -d is the
# remote and a throwaway clone has a copy of the hook as its pre-push hook, so
# real `git push` runs feed the hook git's real stdin. The identity is a
# synthetic name handed over through the hook's test-only override
# YQ2_IDENTITY_TEST_EXTRA_WORD, plus (when long enough) this machine's real
# user name built at run time, to show the real words still count. Every ref
# name and message of the fixture is built from the synthetic prefix "zzq", so
# no ordinary English word in it can equal a real host or user name. All of it
# stays in the temporary directory; nothing is written to the repository, and
# the invoking user's git configuration is not read.
#
# Usage: tests/check-pre-push-hook.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1

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
w() { git -C "$tmp/w" -c user.name=zzqa -c user.email=zzqa@zzqmail.zzq -c commit.gpgsign=false -c tag.gpgsign=false "$@"; }
commit() { # $1 = message
	w commit -q --allow-empty -m "$1"
}
real_host=$(hostname -s 2>/dev/null || true)
real_user=$(whoami 2>/dev/null || true)
real_home=${HOME:-}
fails=0
note() { echo "check-pre-push-hook: FAIL: $*"; fails=1; }
leaks() { # does the captured output show a word? (it must not)
	# The hook's own refusal lines are fixed text plus a number, a commit id
	# and a kind; they are reduced to HOOK first, so only the variable part
	# (anything else a line carries) is compared. Git echoes the remote's
	# path, which can sit under a directory named after the user; that is
	# the test's own temp path.
	_kinds='(host|user|home|test)'
	sed -E \
		-e "s|$tmp|TMP|g" \
		-e "s/^pre-push: refused: ref number [0-9]+ of this push \\(commit [0-9a-f]+\\) has a name containing this machine's $_kinds identity; rename the branch\$/HOOK/" \
		-e "s/^pre-push: refused: the message of commit [0-9a-f]+ \\(ref number [0-9]+\\) contains this machine's $_kinds identity; reword it before pushing\$/HOOK/" \
		-e "s/^pre-push: refused: the tag object [0-9a-f]+ \\(ref number [0-9]+\\) contains this machine's $_kinds identity; recreate the tag\$/HOOK/" \
		-e "s/^pre-push: refused: ref number [0-9]+ has more than [0-9]+ commits to scan; fetch the remote first, or push in smaller steps\$/HOOK/" \
		-e "s/^pre-push: refused: cannot list the (new )?commits of ref number [0-9]+\$/HOOK/" \
		-e "s/^pre-push: refused: the remote commit of ref number [0-9]+ is not known here; fetch the remote first\$/HOOK/" \
		"$tmp/out" > "$tmp/out.clean"
	if grep -qiF -e "$syn" "$tmp/out"; then note "output printed the synthetic value ($1)"; fi
	for _real in "$real_host" "$real_user" "$real_home"; do
		[ "${#_real}" -ge 3 ] || continue
		if grep -qiF -e "$_real" "$tmp/out.clean"; then note "output printed a real identity value ($1)"; fi
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

commit zzqm1
w branch -M zzqmain
w remote add origin "$tmp/remote.git"
push "normal branch" ok origin zzqmain

# --- ref names ---
w branch "zzqt/$syn"
push "ref name with the synthetic word" refused origin "zzqt/$syn"
w branch "zzqt/ZZQ-SYNTHETIC-BOX-2"
push "ref name, other case" refused origin "zzqt/ZZQ-SYNTHETIC-BOX-2"
w branch "zzql-$syn"
push "local ref name alone carries the word" refused "origin" "zzql-$syn:refs/heads/zzqplain"
w branch zzqtopic
push "second normal branch" ok origin zzqtopic
push "plain remote name from a plain local name" ok origin zzqtopic:refs/heads/zzqplain2

# --- messages: update of an existing branch ---
commit zzqm2
push "update with clean commits" ok origin zzqmain
commit "zzqm3 ran on $syn today"
push "update with a message naming the host" refused origin zzqmain
w reset -q --hard HEAD~1
commit zzqm4
push "update after rewording" ok origin zzqmain

# --- author and committer fields are scanned too ---
w checkout -q -b zzqauth zzqmain
w -c user.name="$syn" commit -q --allow-empty -m zzqm5
push "author name with the word" refused origin zzqauth
w reset -q --hard zzqmain
w -c user.email="zzqa@$syn.zzq" commit -q --allow-empty -m zzqm6
push "author e-mail with the word" refused origin zzqauth
w reset -q --hard zzqmain

# --- messages: new branch scans only what the remote lacks ---
w checkout -q -b zzqtopic2
commit "zzqm7 work on ZZQ-Synthetic-Box"
push "new branch with a bad message" refused origin zzqtopic2
w reset -q --hard zzqmain
commit zzqm8
push "new branch with clean message" ok origin zzqtopic2

# --- the real user name: a whole word counts, a part of a word does not ---
user=$real_user
if [ "${#user}" -ge 3 ]; then
	w checkout -q -b zzqtopic3 zzqmain
	commit "zzqm9 note for $user"
	push "real user name in a message" refused origin zzqtopic3
	w reset -q --hard zzqmain
	commit "zzqm10 x${user}x and ${user}2 and zz${user}"
	push "real user name inside longer words" ok origin zzqtopic3
fi

# --- the home path, as a message ---
w checkout -q -b zzqhome zzqmain
commit "zzqm11 see /zzqhome/zzqx/file"
rc=0
HOME=/zzqhome/zzqx w push -q origin zzqhome > "$tmp/out" 2>&1 || rc=$?
[ "$rc" -ne 0 ] && grep -q "pre-push: refused" "$tmp/out" || note "home path in a message was not refused"
leaks "home path"

# --- annotated tags: the tag message is scanned although no commit is new ---
w checkout -q zzqmain
w tag -a -m "tagged on $syn" zzqtagbad zzqmain
push "annotated tag with the word in its message" refused origin zzqtagbad
w tag -a -m zzqtagmsg zzqtagok zzqmain
push "annotated tag with a clean message" ok origin zzqtagok

# --- claim branches: message scan exempt, ref name never ---
w checkout -q -b zzqclaim zzqmain
commit "zzqm12 claimed by $syn"
push "non-claim branch with the same commit" refused origin zzqclaim:refs/heads/zzqother/ZZQ1
push "claim branch whose name has the host" refused origin "zzqclaim:refs/heads/claim/$syn"
push "claim branch with the host in the message" ok origin zzqclaim:refs/heads/claim/ZZQ1
commit "zzqm13 claimed by $syn"
push "claim-like prefix is not the exemption" refused origin zzqclaim:refs/heads/claims/ZZQ1

# --- an update scans only what the remote lacks ---
# The commit that names the host is already on the remote (as the claim
# branch above). A branch that was behind and now fast-forwards onto it has
# nothing new to publish; it must not be refused for what is already public.
w checkout -q -b zzqbehind zzqmain
push "branch behind, before the update" ok origin zzqbehind
w merge -q --ff-only zzqclaim~1
push "update onto a commit already public on another branch" ok origin zzqbehind

# --- deletion is allowed even for a leaked name ---
w checkout -q zzqmain
sha=$(w rev-parse zzqmain)
zero=0000000000000000000000000000000000000000
rc=0
printf '(delete) %s refs/heads/zzqt/%s %s\n' "$zero" "$syn" "$sha" |
	(cd "$tmp/w" && sh "$hook" origin x) > "$tmp/out" 2>&1 || rc=$?
[ "$rc" -eq 0 ] || note "deleting a ref was refused"

# --- cap: the test override can only lower it ---
mk3() { # $1 = branch name: three commits on top of zzqmain
	w checkout -q -b "$1" zzqmain
	commit "$1-a"; commit "$1-b"; commit "$1-c"
}
mk3 zzqlong1
rc=0
YQ2_IDENTITY_TEST_SCAN_CAP=2 w push -q origin zzqlong1 > "$tmp/out" 2>&1 || rc=$?
{ [ "$rc" -ne 0 ] && grep -q 'more than 2 commits' "$tmp/out"; } || note "scan cap was not enforced"
mk3 zzqlong2
rc=0
YQ2_IDENTITY_TEST_SCAN_CAP=02 w push -q origin zzqlong2 > "$tmp/out" 2>&1 || rc=$?
[ "$rc" -eq 0 ] || note "a test cap with a leading zero must be ignored"
mk3 zzqlong3
rc=0
YQ2_IDENTITY_TEST_SCAN_CAP=08 w push -q origin zzqlong3 > "$tmp/out" 2>&1 || rc=$?
[ "$rc" -eq 0 ] || note "a test cap that is not valid octal must be ignored"
# A test cap above the default must not raise it: 1001 commits, cap 999999,
# the refusal must name the default of 1000.
{
	i=1
	while [ "$i" -le 1001 ]; do
		printf 'commit refs/heads/zzqbig\ncommitter zzqa <zzqa@zzqmail.zzq> %d +0000\ndata 8\nzzq%05d\n' "$((1700000000 + i))" "$i"
		[ "$i" -ne 1 ] || printf 'from refs/heads/zzqmain\n'
		i=$((i + 1))
	done
} | git -C "$tmp/w" fast-import --quiet
rc=0
YQ2_IDENTITY_TEST_SCAN_CAP=999999 w push -q origin zzqbig > "$tmp/out" 2>&1 || rc=$?
{ [ "$rc" -ne 0 ] && grep -q 'more than 1000 commits' "$tmp/out"; } || note "a test cap above the default was honoured"

# --- fail closed: unknown remote commit cannot be listed ---
rc=0
printf 'refs/heads/zzqmain %s refs/heads/zzqmain 1111111111111111111111111111111111111111\n' "$sha" |
	(cd "$tmp/w" && sh "$hook" origin x) > "$tmp/out" 2>&1 || rc=$?
[ "$rc" -ne 0 ] || note "an unknown remote commit must refuse the push"
grep -q 'fetch the remote first' "$tmp/out" || note "the unknown remote commit refusal does not say what to do"

# --- a last line without a newline is still judged ---
rc=0
printf 'refs/heads/zzqx %s refs/heads/zzqt/%s %s' "$sha" "$syn" "$zero" |
	(cd "$tmp/w" && sh "$hook" origin x) > "$tmp/out" 2>&1 || rc=$?
[ "$rc" -ne 0 ] || note "a final stdin line without a newline was skipped"

# --- the hook runs under dash and bash 3.2 style shells too ---
for sh_bin in dash bash; do
	command -v "$sh_bin" >/dev/null 2>&1 || continue
	rc=0
	printf 'refs/heads/zzqx %s refs/heads/zzqt/%s %s\n' "$sha" "$syn" "$zero" |
		(cd "$tmp/w" && "$sh_bin" "$hook" origin x) > "$tmp/out" 2>&1 || rc=$?
	[ "$rc" -ne 0 ] || note "$sh_bin: bad ref name was not refused"
done

[ "$fails" -eq 0 ] && echo "check-pre-push-hook: ok"
exit "$fails"
