#!/bin/sh
# Invariant: .githooks/pre-push refuses a push that would publish the working
# machine's identity, in a ref name, a commit, a tag, or a file or file name
# a pushed commit adds, under any ref name, and lets an ordinary push through.
#
# Self-test with no network, in a throwaway directory under mktemp -d: a bare
# repository is the remote and a clone carries a copy of the hook as its
# pre-push hook, with a copy of tests/check-no-local-identity.sh beside it as
# the hook expects. Real `git push` runs prove the wiring (git's stdin and
# arguments, a refusal stops the push, pushed refs become published); every
# other case feeds the hook what git gives it (the remote name and URL as
# arguments, one "<local ref> <local sha> <remote ref> <remote sha>" line per
# ref on stdin, as githooks(5) gives the form) directly, many refs per run,
# and checks the line the hook prints for each ref by its number.
#
# The identity is synthetic: the hook's test-only YQ2_IDENTITY_TEST_HOST and
# YQ2_IDENTITY_TEST_USER add a made-up host and user name to the real ones,
# and HOME is a made-up path, so the hook really reads them at run time.
# Every name and message is built from the prefix "zzq". The identity shim is
# first on PATH throughout, as it is for every marker writer: its `hostname`
# prints a machine token and its `whoami` prints `agent`. The hook reads the
# real names by absolute path, so the shim changes nothing: markers in the
# token form pass, and a marker naming this machine's real host or user name
# (built here at run time from /bin/hostname and /usr/bin/id, never printed,
# kept in a throwaway repository) is refused. The output of each run is
# checked for the synthetic values and for this machine's real ones. A
# failing case shows a redacted copy of the output.
#
# The test is immune to the caller's git environment and configuration:
# every GIT_* variable is unset (a pre-commit hook exports GIT_INDEX_FILE),
# global and system configuration and attributes are not read
# (GIT_CONFIG_GLOBAL=/dev/null, GIT_CONFIG_NOSYSTEM, GIT_ATTR_NOSYSTEM),
# repositories are made with `git init --template=`, and HOME and
# XDG_CONFIG_HOME point where nothing is.
#
# Usage: tests/check-pre-push-hook.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C

hook=$(pwd)/.githooks/pre-push
[ -x "$hook" ] || { echo "check-pre-push-hook: .githooks/pre-push is missing or not executable"; exit 1; }

# Read by absolute path, as the hook reads them (a shim on the caller's PATH
# cannot stand in), and before HOME is replaced; only ever compared or put in
# a throwaway repository, never printed.
real_host=$(/bin/hostname -s 2>/dev/null || true)
real_user=$(/usr/bin/id -un 2>/dev/null || true)
real_words="$real_host
$real_user
${HOME:-}"

# ---- Isolation from the caller. -------------------------------------------
for _v in $(env | sed -n 's/^\(GIT_[A-Za-z0-9_]*\)=.*/\1/p'); do
	unset "$_v"
done
# The directory is under /tmp, not $TMPDIR: git prints its paths (the remote's
# address in a push), and a $TMPDIR that holds the login name would put a real
# identity value into the output that leaks() rightly refuses. The path is
# checked against the real values and refused if it holds one.
tmp=$(mktemp -d /tmp/q2-prepush.XXXXXX)
# A shell error must not exit 0 (bash 3.2 loses the status in an EXIT trap).
trap 'rm -rf "${tmp:?}"; [ "${_ok:-}" = 1 ] || exit 1' EXIT
_tl=$(printf '%s' "$tmp" | tr '[:upper:]' '[:lower:]')
printf '%s\n' "$real_words" | while IFS= read -r _w; do
	[ "${#_w}" -ge 3 ] || continue
	_w=$(printf '%s' "$_w" | tr '[:upper:]' '[:lower:]')
	case $_tl in *"$_w"*) echo x ;; esac
done | grep -q x && {
	echo "check-pre-push-hook: the temporary directory's path holds an identity value; this check needs a /tmp path without one"
	exit 1
}
mkdir "$tmp/tpl" "$tmp/bin" "$tmp/hooks" "$tmp/tests"
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 GIT_ATTR_NOSYSTEM=1 GIT_TEMPLATE_DIR="$tmp/tpl"
export XDG_CONFIG_HOME="$tmp/xdg"

syn=zzq-synthetic-box
login=zzqlogin
synhome=/zzqhome/zzqx
shim() { # $1 = command, $2 = what it prints ("" = fails)
	if [ -n "$2" ]; then
		printf '#!/bin/sh\necho %s\n' "$2" > "$tmp/bin/$1"
	else
		printf '#!/bin/sh\nexit 1\n' > "$tmp/bin/$1"
	fi
	chmod +x "$tmp/bin/$1"
}
# The identity shim (.claude/rules/no-machine-names.md), first on PATH.
tok=m-0a1b2c3d4e5f
shim hostname "$tok"
shim whoami agent
PATH="$tmp/bin:$PATH"
HOME=$synhome
mkdir "$tmp/hooktmp"
export PATH HOME YQ2_IDENTITY_TEST_EXTRA_WORD=zzqextra TMPDIR="$tmp/hooktmp"
export YQ2_IDENTITY_TEST_HOST=Zzq-Synthetic-Box YQ2_IDENTITY_TEST_USER="$login"

fails=0
note() { echo "check-pre-push-hook: FAIL: $*"; fails=$((fails + 1)); }

# redact < file: the tmp path becomes TMP; a line that holds any synthetic or
# real value is replaced whole. Used for everything this test prints.
redact() {
	YQ2_T=$tmp YQ2_R="$real_words
$syn
$login
$synhome
zzqextra" awk '
	BEGIN { t = ENVIRON["YQ2_T"]; n = split(ENVIRON["YQ2_R"], w, "\n") }
	{
		while ((p = index($0, t)) > 0) $0 = substr($0, 1, p - 1) "TMP" substr($0, p + length(t))
		l = tolower($0)
		for (i = 1; i <= n; i++)
			if (length(w[i]) >= 3 && index(l, tolower(w[i]))) { print "  <line withheld: it holds an identity value>"; next }
		print "  " $0
	}'
}
# leaks <label>: the output must hold no synthetic or real value. Commit ids
# are blanked first (hex can spell a short word by chance).
leaks() {
	sed -E 's/(^|[^0-9a-f])[0-9a-f]{10}([^0-9a-f]|$)/\1ID\2/g' "$tmp/out" > "$tmp/out.ids"
	for _w in "$syn" "$login" "$synhome" zzqextra; do
		grep -qiF -e "$_w" "$tmp/out.ids" && note "$1: output printed a synthetic value"
	done
	printf '%s\n' "$real_words" | while IFS= read -r _w; do
		[ "${#_w}" -ge 3 ] || continue
		grep -qiF -e "$_w" "$tmp/out.ids" && echo x
	done | grep -q x && note "$1: output printed a real identity value"
	return 0
}

git init -q --template= --bare "$tmp/remote.git"
git init -q --template= "$tmp/w"
cp "$hook" "$tmp/hooks/pre-push"
cp tests/check-no-local-identity.sh "$tmp/tests/"
w() { git -C "$tmp/w" "$@"; }
w config core.hooksPath "$tmp/hooks"
w config user.name zzqa
w config user.email zzqa@zzqmail.zzq
w config commit.gpgsign false
w config tag.gpgsign false
w remote add origin "$tmp/remote.git"
empty=$(w hash-object -t tree -w /dev/null)

# mk <parent|-> <message> [-p <parent>...]: a commit; AN, AE, CN set author
# and committer, TR the tree (the empty tree by default).
mk() {
	_p=$1; _m=$2; shift 2
	if [ "$_p" != - ]; then set -- -p "$_p" "$@"; fi
	GIT_AUTHOR_NAME=${AN:-zzqa} GIT_AUTHOR_EMAIL=${AE:-zzqa@zzqmail.zzq} \
		GIT_COMMITTER_NAME=${CN:-zzqa} GIT_COMMITTER_EMAIL=zzqa@zzqmail.zzq \
		GIT_AUTHOR_DATE='1700000000 +0000' GIT_COMMITTER_DATE='1700000000 +0000' \
		w commit-tree "${TR:-$empty}" "$@" -m "$_m"
}
# tree <path> <content> [<path> <content>...]: a tree holding those files
# (paths may have directories); content "link:<target>" makes a symbolic link.
tree() {
	rm -f "$tmp/tree.idx"
	while [ "$#" -ge 2 ]; do
		case $2 in
		link:*)
			_b=$(printf '%s' "${2#link:}" | w hash-object -w --stdin)
			GIT_INDEX_FILE=$tmp/tree.idx w update-index --add --cacheinfo "120000,$_b,$1"
			;;
		*)
			_b=$(printf '%s\n' "$2" | w hash-object -w --stdin)
			GIT_INDEX_FILE=$tmp/tree.idx w update-index --add --cacheinfo "100644,$_b,$1"
			;;
		esac
		shift 2
	done
	GIT_INDEX_FILE=$tmp/tree.idx w write-tree
}
# raw <text>: a commit object written as is.
raw() { printf '%s' "$1" | w hash-object -t commit -w --stdin; }
# tag <object> <type> <name> <message>: an annotated tag object.
tag() {
	printf 'object %s\ntype %s\ntag %s\ntagger zzqa <zzqa@zzqmail.zzq> 1700000000 +0000\n\n%s\n' "$1" "$2" "$3" "$4" |
		w mktag
}

zero=0000000000000000000000000000000000000000
m1=$(mk - zzqm1)
w update-ref refs/heads/zzqmain "$m1"
w update-ref refs/heads/zzqx "$m1"

# ---- Real pushes: the wiring. ---------------------------------------------
rpush() { # $1 = label, $2 = want (ok|refused); rest = push args
	_label=$1; _want=$2; shift 2
	_rc=0
	w push -q "$@" > "$tmp/out" 2>&1 || _rc=$?
	case $_want in
	ok) [ "$_rc" -eq 0 ] || { note "$_label: expected the push to go through"; redact < "$tmp/out"; } ;;
	*)
		[ "$_rc" -ne 0 ] || note "$_label: expected the push to be refused"
		grep -q "pre-push: refused: ref number 1: $_want" "$tmp/out" || { note "$_label: no refusal from the hook"; redact < "$tmp/out"; }
		;;
	esac
	leaks "$_label"
}
# Tooling branches whose commit is a root commit on the empty tree (the shape
# of a release tombstone; claims and gate-clears have the shape tested under
# "The identity shim on PATH" below) and whose message names a machine token.
claim=$(mk - "claimed ZZQ1 by m-0123456789ab/agent/zzq at 2026-01-01T00:00:00Z")
gclear=$(mk - "gate-clear ZZQ2 by m-0123456789ab/agent/zzq at 2026-01-01T00:00:00Z")
rpush "real push of a normal branch" ok origin zzqmain
rpush "real push of a leaking ref name" "the remote ref name contains this machine's host identity" \
	origin "zzqx:refs/heads/zzqt/$syn"
rpush "real push of several refs, tooling branches among them" ok origin \
	"$claim:refs/heads/claim/ZZQ1" "$gclear:refs/heads/gate-clear/ZZQ2" "$m1:refs/heads/zzqbehind"
[ "$(w rev-parse -q --verify refs/remotes/origin/claim/ZZQ1 || true)" = "$claim" ] ||
	note "the real push did not record the claim branch as published"
# A file a pushed commit adds: refused under every ref name, the tooling
# prefixes included, and a refused push publishes nothing, so a later push of
# the same commit to another ref is scanned and refused too.
filebad=$(TR=$(tree scripts/x.sh "echo built on $syn") mk "$m1" zzqm40)
FB="a file a pushed commit adds (blob"
rpush "real push of a commit adding a file that holds the word" "$FB" origin "$filebad:refs/heads/zzqf1"
rpush "real push of that commit to claim/" "$FB" origin "$filebad:refs/heads/claim/ZZQX"
rpush "real push of that commit to gate-clear/" "$FB" origin "$filebad:refs/heads/gate-clear/ZZQX"
[ -z "$(w for-each-ref refs/remotes/origin/claim/ZZQX refs/remotes/origin/gate-clear/ZZQX)" ] ||
	note "a refused push recorded a tooling branch as published"
rpush "real push of that commit to a normal branch, after the tooling pushes" "$FB" origin "$filebad:refs/heads/zzqf2"

# ---- The identity shim on PATH: the real names still decide. --------------
# The shim is on PATH (above), as for every marker writer. The markers have
# the shape the tooling writes: a commit on the tree of
# refs/remotes/origin/main with it as parent, the message of claim-card.sh
# claim ("claim <ID> by <holder> at <UTC time> (lease 6h) [nonce:<16 hex>]",
# the holder "<token>/agent/<session>") or of kanban.sh gate-clear
# ("gate-clear <ID> by <host>/<user> at <UTC time>", under the shim
# "<token>/agent"); a release deletes the claim ref. One real push per case.
trunk=$(TR=$(tree README.md "zzq trunk" src/zzq.c "/* zzq */") mk "$m1" zzqtrunk)
rpush "real push of main" ok origin "$trunk:refs/heads/main"
[ "$(w rev-parse -q --verify refs/remotes/origin/main || true)" = "$trunk" ] ||
	note "the real push of main did not record it as published"
marker() { # $1 = message
	TR="refs/remotes/origin/main^{tree}" mk "$(w rev-parse refs/remotes/origin/main)" "$1"
}
sess=0a1b2c3d-4e5f-4a6b-8c7d-9e0f1a2b3c4d
when=2026-11-17T00:00:00Z
nonce=0123456789abcdef
rpush "the shim on PATH: a claim marker in the token form" ok origin \
	"$(marker "claim ZZQ9 by $tok/agent/$sess at $when (lease 6h) [nonce:$nonce]"):refs/heads/claim/ZZQ9"
rpush "the shim on PATH: a gate-clear marker in the token form" ok origin \
	"$(marker "gate-clear ZZQ9 by $tok/agent at $when"):refs/heads/gate-clear/ZZQ9"
rpush "the shim on PATH: a release (the claim ref deleted)" ok origin :refs/heads/claim/ZZQ9
[ -z "$(git --git-dir="$tmp/remote.git" for-each-ref refs/heads/claim/ZZQ9)" ] ||
	note "the shim on PATH: the release left the claim ref on the remote"
if [ "${#real_host}" -ge 3 ]; then
	_c=$(marker "claim ZZQ9 by $real_host/agent/$sess at $when (lease 6h) [nonce:$nonce]")
	rpush "the shim on PATH: a claim marker naming the real host" \
		"the message of commit $(printf '%.10s' "$_c") contains this machine's host identity" origin "$_c:refs/heads/claim/ZZQ9"
else
	echo "check-pre-push-hook: not run: the real-host marker case (the hook does not compare a host name shorter than three characters)"
fi
if [ "${#real_user}" -ge 3 ] && [ "$real_user" != agent ]; then
	_c=$(marker "claim ZZQ9 by $tok/$real_user/$sess at $when (lease 6h) [nonce:$nonce]")
	rpush "the shim on PATH: a claim marker naming the real user as a path component" \
		"the message of commit $(printf '%.10s' "$_c") contains this machine's user identity" origin "$_c:refs/heads/claim/ZZQ9"
	rpush "the shim on PATH: a file holding the word agent and the token, on a normal branch" ok origin \
		"$(TR=$(tree README.md "zzq trunk" src/zzq.c "/* zzq */" scripts/zzq-agent.txt "run by agent on $tok") mk "$trunk" zzqm70):refs/heads/zzqagent"
else
	echo "check-pre-push-hook: not run: the real-user marker and agent-file cases (the user name is shorter than three characters, which the hook does not compare, or is agent itself)"
fi
[ -z "$(git --git-dir="$tmp/remote.git" for-each-ref refs/heads/claim/ZZQ9)" ] ||
	note "the shim on PATH: a refused marker reached the remote"
# A commit replaced locally by a clean twin (refs/replace/): the scan reads
# what the push sends, the commit itself, and refuses its file.
_rx=$(TR=$(tree README.md "zzq trunk" src/zzq.c "/* zzq */" scripts/zzq-r.sh "cd $synhome/src") mk "$trunk" zzqm71)
_ry=$(TR=$(tree README.md "zzq trunk" src/zzq.c "/* zzq */" scripts/zzq-r.sh "cd /zzq/src") mk "$trunk" zzqm71)
_rb=$(w rev-parse "$_rx^{tree}:scripts/zzq-r.sh")
w replace "$_rx" "$_ry"
rpush "a commit replaced locally by a clean twin (git replace)" \
	"a file a pushed commit adds (blob $(printf '%.10s' "$_rb")) contains this machine's home identity" \
	origin "$_rx:refs/heads/zzqreplaced"
w replace -d "$_rx" > /dev/null

# ---- Direct runs: a batch of refs, one expectation each. ------------------
# case_ <want> <local ref> <local sha> <remote ref> <remote sha>; want is "ok"
# or the text that must follow "ref number N: " (one line, exactly).
: > "$tmp/in"
: > "$tmp/want"
case_() {
	printf '%s\n' "$1" >> "$tmp/want"
	printf '%s %s %s %s\n' "$2" "$3" "$4" "$5" >> "$tmp/in"
}
# run <label> [env assignments...]: feeds $tmp/in (its last newline dropped,
# so a final line without one is judged too) to the hook under sh, or under
# each shell of $shells that is installed.
run() {
	_label=$1; shift
	_wantrc=0
	grep -qv '^ok$' "$tmp/want" && _wantrc=1
	printf '%s' "$(cat "$tmp/in")" > "$tmp/in.nonl"
	_f0=$fails
	for _sh in ${shells:-sh}; do
		command -v "$_sh" > /dev/null 2>&1 || continue
		_rc=0
		(cd "$tmp/w" && env "$@" "$_sh" "$hook" origin "$tmp/remote.git") < "$tmp/in.nonl" > "$tmp/out" 2>&1 || _rc=$?
		[ "$_rc" -eq "$_wantrc" ] || note "$_label ($_sh): exit status $_rc, wanted $_wantrc"
		_n=0
		while IFS= read -r _want; do
			_n=$((_n + 1))
			_lines=$(grep -c "^pre-push: refused: ref number $_n: " "$tmp/out" || true)
			if [ "$_want" = ok ]; then
				[ "$_lines" -eq 0 ] || note "$_label ($_sh): ref $_n was refused, wanted ok"
			else
				[ "$_lines" -eq 1 ] && grep -qF "pre-push: refused: ref number $_n: $_want" "$tmp/out" ||
					note "$_label ($_sh): ref $_n: wanted one line \"$_want\""
			fi
		done < "$tmp/want"
		[ "$(grep -cv '^pre-push: refused: ref number [0-9]*: ' "$tmp/out" || true)" -eq 0 ] ||
			note "$_label ($_sh): unexpected output"
		leaks "$_label ($_sh)"
		[ "$fails" -eq "$_f0" ] || { redact < "$tmp/out"; break; }
	done
	: > "$tmp/in"
	: > "$tmp/want"
}
H="this machine's host identity"
U="this machine's user identity"
M="the message of commit"
A="the author or committer field of commit"
T="the tag object"

m2=$(mk "$m1" zzqm2)
case_ ok refs/heads/zzqtopic "$m2" refs/heads/zzqtopic "$zero"
case_ "the remote ref name contains $H" refs/heads/zzqx "$m1" "refs/heads/zzqt/$syn" "$zero"
case_ "the remote ref name contains $H" refs/heads/zzqx "$m1" refs/heads/zzqt/ZZQ-SYNTHETIC-BOX-2 "$zero"
case_ "the local ref name contains $H" "refs/heads/zzql-$syn" "$m1" refs/heads/zzqplain "$zero"
case_ "the remote ref name contains $U" refs/heads/zzqx "$m1" "refs/heads/zzqt/x${login}x" "$zero"
case_ ok refs/heads/zzqmain "$m2" refs/heads/zzqmain "$m1"
m3=$(mk "$m1" "zzqm3 ran on $syn today")
case_ "$M $(printf '%.10s' "$m3") contains $H" refs/heads/zzqmain "$m3" refs/heads/zzqmain "$m1"
case_ "$A" refs/heads/zzqa1 "$(AN=$syn mk "$m1" zzqm5)" refs/heads/zzqa1 "$zero"
case_ "$A" refs/heads/zzqa2 "$(AE="zzqa@$syn.zzq" mk "$m1" zzqm6)" refs/heads/zzqa2 "$zero"
case_ "$A" refs/heads/zzqa3 "$(CN="zzq $syn" mk "$m1" zzqm6c)" refs/heads/zzqa3 "$zero"
case_ "$A" refs/heads/zzqa4 "$(AN="$login" mk "$m1" zzqm6u)" refs/heads/zzqa4 "$zero"
case_ "$M" refs/heads/zzqn1 "$(mk "$m1" "zzqm7 work on ZZQ-Synthetic-Box")" refs/heads/zzqn1 "$zero"
case_ "$M" refs/heads/zzqn2 "$(mk "$m1" "zzqm9 note for $login")" refs/heads/zzqn2 "$zero"
case_ "$M" refs/heads/zzqn3 "$(mk "$m1" "zzqm9 path /srv/$login/data")" refs/heads/zzqn3 "$zero"
case_ "$M" refs/heads/zzqn4 "$(mk "$m1" "$login starts the line")" refs/heads/zzqn4 "$zero"
case_ ok refs/heads/zzqn5 "$(mk "$m1" "zzqm10 x${login}x and ${login}2 and zz${login} and ${login}_a")" refs/heads/zzqn5 "$zero"
case_ "$M" refs/heads/zzqn5b "$(mk "$m1" "zzqm10 ${login}x first, then $login alone")" refs/heads/zzqn5b "$zero"
case_ "$M" refs/heads/zzqn6 "$(mk "$m1" "zzqm11 see $synhome/file")" refs/heads/zzqn6 "$zero"
case_ "$M" refs/heads/zzqn7 "$(mk "$m1" "zzqm11 see /ZZQHOME/zzqX/file")" refs/heads/zzqn7 "$zero"
case_ "$M" refs/heads/zzqn8 "$(mk "$m1" "zzqm11 the zzqextra word")" refs/heads/zzqn8 "$zero"
# a bad commit below a clean tip, and two bad commits: one line per ref
bad=$(mk "$m1" "zzqm13 on $syn")
case_ "$M $(printf '%.10s' "$bad")" refs/heads/zzqn9 "$(mk "$bad" zzqm14)" refs/heads/zzqn9 "$zero"
case_ "$M" refs/heads/zzqn10 "$(mk "$bad" "zzqm15 on $syn")" refs/heads/zzqn10 "$zero"
# what the remote already has is not scanned: its tip, and tracking refs
# (here a commit published before this hook, as old history is)
pub=$(mk "$m1" "zzqm12 published on $syn")
w update-ref refs/remotes/origin/zzqpub "$pub"
case_ ok refs/heads/zzqn11 "$(mk "$bad" zzqm16)" refs/heads/zzqn11 "$bad"
case_ ok refs/heads/zzqnew "$(mk "$pub" zzqm17)" refs/heads/zzqnew "$zero"
case_ ok refs/heads/zzqbehind "$pub" refs/heads/zzqbehind "$m1"
# ... but only what THIS remote has: another remote's tracking refs do not count
other=$(mk "$m1" "zzqm27 on $syn")
w update-ref refs/remotes/zzqother/zzqx "$other"
case_ "$M $(printf '%.10s' "$other")" refs/heads/zzqo "$(mk "$other" zzqm28)" refs/heads/zzqo "$zero"
# tooling branches: scanned like any other ref
case_ "$M" refs/heads/zzqc "$bad" refs/heads/claim/ZZQ3 "$zero"
case_ "$M" refs/heads/zzqc "$bad" refs/heads/gate-clear/ZZQ4 "$zero"
case_ ok refs/heads/zzqc "$(mk - "claimed ZZQ3 by m-0123456789ab/agent/zzq")" refs/heads/claim/ZZQ3 "$zero"
case_ ok refs/heads/zzqc "$(mk - "gate-clear ZZQ4 by m-0123456789ab/agent/zzq")" refs/heads/gate-clear/ZZQ4 "$zero"
case_ "the remote ref name contains $H" refs/heads/zzqc "$bad" "refs/heads/claim/$syn" "$zero"
case_ "the remote ref name contains $H" refs/heads/zzqc "$bad" "refs/heads/gate-clear/$syn" "$zero"
case_ "$M" refs/heads/zzqc "$bad" refs/heads/claims/ZZQ5 "$zero"
case_ "$M" refs/heads/zzqc "$bad" refs/heads/gate-clears/ZZQ5 "$zero"
case_ "$M" refs/heads/zzqc "$bad" refs/heads/zzq/claim/ZZQ5 "$zero"
case_ "$M" refs/heads/claim/ZZQ6 "$bad" refs/heads/zzqc6 "$zero"
# deletion
case_ ok "(delete)" "$zero" "refs/heads/zzqt/$syn" "$m1"
# tags: each level of a tag of a tag, a tag on a blob or a tree
tbad=$(tag "$m1" commit zzqt1 "tagged on $syn")
tok=$(tag "$m1" commit zzqt2 zzqclean)
case_ "$T $(printf '%.10s' "$tbad")" refs/tags/zzqt1 "$tbad" refs/tags/zzqt1 "$zero"
case_ ok refs/tags/zzqt2 "$tok" refs/tags/zzqt2 "$zero"
case_ "$T $(printf '%.10s' "$tbad")" refs/tags/zzqt3 "$(tag "$tbad" tag zzqt3 zzqouter)" refs/tags/zzqt3 "$zero"
case_ ok refs/tags/zzqt4 "$(tag "$tok" tag zzqt4 zzqouter)" refs/tags/zzqt4 "$zero"
case_ "$T" refs/tags/zzqt5 "$(tag "$m1" commit "zzq$syn" zzqclean)" refs/tags/zzqt5 "$zero"
case_ "$T" refs/tags/zzqt6 "$(tag "$m1" commit zzqt6 "$login was here")" refs/tags/zzqt6 "$zero"
tbad7=$(tag "$(mk "$m1" "zzqm18 $syn")" commit zzqt7 zzqclean)
case_ "$M" refs/tags/zzqt7 "$tbad7" refs/tags/zzqt7 "$zero"
blobbad=$(printf 'zzq\000 on %s\n' "$syn" | w hash-object -w --stdin)
blobok=$(printf 'zzq clean\n' | w hash-object -w --stdin)
case_ "the blob $(printf '%.10s' "$blobbad") the ref points at contains $H" refs/tags/zzqb1 "$(tag "$blobbad" blob zzqb1 zzqclean)" refs/tags/zzqb1 "$zero"
case_ "the blob" refs/tags/zzqb2 "$blobbad" refs/tags/zzqb2 "$zero"
case_ ok refs/tags/zzqb3 "$(tag "$blobok" blob zzqb3 zzqclean)" refs/tags/zzqb3 "$zero"
case_ "it points at a tree" refs/tags/zzqtr "$(tag "$empty" tree zzqtr zzqclean)" refs/tags/zzqtr "$zero"
# raw commit headers: mergetag scanned, signatures skipped, others scanned
sp=' '
hdr="tree $empty
parent $m1
author zzqa <zzqa@zzqmail.zzq> 1700000000 +0000
committer zzqa <zzqa@zzqmail.zzq> 1700000000 +0000
"
case_ "the merged tag (mergetag header) of commit" refs/heads/zzqr1 "$(raw "${hdr}mergetag object $m1
 type commit
 tag zzqmt
 tagger zzqa <zzqa@zzqmail.zzq> 1700000000 +0000
$sp
 merged on $syn

zzqm19
")" refs/heads/zzqr1 "$zero"
case_ ok refs/heads/zzqr2 "$(raw "${hdr}mergetag object $m1
 type commit
 tag zzqmt
 tagger zzqa <zzqa@zzqmail.zzq> 1700000000 +0000
$sp
 zzqclean
 -----BEGIN PGP SIGNATURE-----
 zzq$syn
 -----END PGP SIGNATURE-----

zzqm20
")" refs/heads/zzqr2 "$zero"
case_ ok refs/heads/zzqr3 "$(raw "${hdr}gpgsig -----BEGIN PGP SIGNATURE-----
 zzq$syn
 -----END PGP SIGNATURE-----

zzqm21
")" refs/heads/zzqr3 "$zero"
case_ ok refs/heads/zzqr3b "$(raw "${hdr}gpgsig-sha256 -----BEGIN PGP SIGNATURE-----
 zzq$syn
 -----END PGP SIGNATURE-----

zzqm21b
")" refs/heads/zzqr3b "$zero"
case_ "a header of commit" refs/heads/zzqr3c "$(raw "${hdr}gzzq zzq$syn

zzqm21c
")" refs/heads/zzqr3c "$zero"
case_ "a header of commit" refs/heads/zzqr4 "$(raw "${hdr}encoding zzq$syn

zzqm22
")" refs/heads/zzqr4 "$zero"
case_ "$M" refs/heads/zzqr5 "$(raw "${hdr}gpgsig -----BEGIN PGP SIGNATURE-----
 zzqsig
 -----END PGP SIGNATURE-----

zzqm23
-----BEGIN PGP SIGNATURE-----
$syn
-----END PGP SIGNATURE-----
")" refs/heads/zzqr5 "$zero"
ttsig=$(tag "$m1" commit zzqt8 "zzqclean
-----BEGIN PGP SIGNATURE-----
zzq$syn
-----END PGP SIGNATURE-----")
case_ ok refs/tags/zzqt8 "$ttsig" refs/tags/zzqt8 "$zero"
case_ "$T" refs/tags/zzqt9 "$(tag "$m1" commit zzqt9 "zzqclean
-----BEGIN PGP SIGNATURE-----
zzqsig
-----END PGP SIGNATURE-----
text after it on $syn")" refs/tags/zzqt9 "$zero"
# last, so it is the line without a final newline
case_ "the remote ref name contains $H" refs/heads/zzqx "$m1" "refs/heads/zzqlast-$syn" "$zero"
run "main batch"

# The same judgements under dash and bash (bash 3.2 on macOS).
case_ ok refs/heads/zzqtopic "$m2" refs/heads/zzqtopic "$zero"
case_ "the local ref name contains $H" "refs/heads/zzql-$syn" "$m1" refs/heads/zzqplain "$zero"
case_ "$M" refs/heads/zzqn2 "$(mk "$m1" "zzqm9 note for $login")" refs/heads/zzqn2 "$zero"
case_ ok refs/heads/zzqn5 "$(mk "$m1" "zzqm10 x${login}x and ${login}2 and zz${login} and ${login}_a")" refs/heads/zzqn5 "$zero"
case_ "$T" refs/tags/zzqt3 "$(tag "$tbad" tag zzqt3 zzqouter)" refs/tags/zzqt3 "$zero"
case_ "$M" refs/heads/zzqc "$bad" refs/heads/gate-clear/ZZQ4 "$zero"
case_ "$FB" refs/heads/zzqf3 "$filebad" refs/heads/zzqf3 "$zero"
echo >> "$tmp/in" # an empty line is skipped and does not count as a ref
case_ "the remote ref name contains $H" refs/heads/zzqx "$m1" "refs/heads/zzqlast-$syn" "$zero"
shells="dash bash"
run "other shells"
shells=sh

# ---- Files the pushed commits add. -----------------------------------------
# The blobs the remote lacks go to tests/check-no-local-identity.sh, so its
# rules hold: the host name and home path as substrings, the user name as a
# whole word, binary files (a NUL byte) not scanned.
FN="a file name a pushed commit adds contains"
case_ "$FB $(printf '%.10s' "$(w rev-parse "$filebad:scripts/x.sh")")) contains $H" refs/heads/zzqf4 "$filebad" refs/heads/zzqf4 "$zero"
case_ "$FB" refs/heads/zzqf5 "$(TR=$(tree stuff/x.c "/* for $login */") mk "$m1" zzqm42)" refs/heads/zzqf5 "$zero"
case_ "$FB" refs/heads/zzqf6 "$(TR=$(tree doc/x.md "see /ZZQHOME/zzqX/file") mk "$m1" zzqm43)" refs/heads/zzqf6 "$zero"
case_ ok refs/heads/zzqf7 "$(TR=$(tree scripts/x.sh "x${login}x and ${login}2 and zz$login") mk "$m1" zzqm44)" refs/heads/zzqf7 "$zero"
case_ "$FB" refs/heads/zzqf8 "$(TR=$(tree third_party/x "link:/srv/$syn/x") mk "$m1" zzqm45)" refs/heads/zzqf8 "$zero"
# every new commit counts, not only the tip: a file added, then removed
mid=$(TR=$(tree scripts/x.sh "built on $syn, then removed") mk "$m1" zzqm46)
case_ "$FB" refs/heads/zzqf9 "$(mk "$mid" zzqm47)" refs/heads/zzqf9 "$zero"
# file names, a directory's included
case_ "$FN $H" refs/heads/zzqf10 "$(TR=$(tree "scripts/zzq-$syn/x.sh" "echo clean") mk "$m1" zzqm48)" refs/heads/zzqf10 "$zero"
case_ "$FN" refs/heads/zzqf11 "$(TR=$(tree "zzq-$login.txt" "zzq clean") mk "$m1" zzqm49)" refs/heads/zzqf11 "$zero"
case_ ok refs/heads/zzqf12 "$(TR=$(tree "zzq-${login}x.txt" "zzq clean") mk "$m1" zzqm50)" refs/heads/zzqf12 "$zero"
# a published commit (as old history is) whose tree holds a file with the word
pubf=$(TR=$(tree clean.txt "zzq clean words" "notes/zzq-$syn.txt" "built on $syn") mk "$m1" zzqm51)
w update-ref refs/remotes/origin/zzqpubf "$pubf"
# a blob the remote has is not scanned again: the remote's commit pubf has it
# in its tree, so `git rev-list --objects <tip> --not <remote sha>
# --remotes=origin` does not list it
case_ ok refs/heads/zzqf13 "$(TR=$(tree clean.txt "zzq clean words" "notes/zzq-$syn.txt" "built on $syn" notes/copy.txt "built on $syn") mk "$pubf" zzqm52)" refs/heads/zzqf13 "$zero"
# ... but a new name for it is a new path
case_ "$FN" refs/heads/zzqf14 "$(TR=$(tree clean.txt "zzq clean words" "notes/zzq-$syn.txt" "built on $syn" "zzq-$syn-2.txt" "zzq clean words") mk "$pubf" zzqm53)" refs/heads/zzqf14 "$zero"
# a commit that only deletes that file publishes nothing: accepted
case_ ok refs/heads/zzqf15 "$(TR=$(tree clean.txt "zzq clean words") mk "$pubf" zzqm54)" refs/heads/zzqf15 "$zero"
# a merge whose result holds a path neither parent has
ma=$(TR=$(tree a.txt "zzq a") mk "$m1" zzqm55)
mb=$(TR=$(tree b.txt "zzq b") mk "$m1" zzqm56)
case_ "$FN" refs/heads/zzqf16 "$(TR=$(tree a.txt "zzq a" b.txt "zzq b" "zzq-$syn.txt" "zzq clean") mk "$ma" zzqm57 -p "$mb")" refs/heads/zzqf16 "$zero"
case_ ok refs/heads/zzqf17 "$(TR=$(tree a.txt "zzq a" b.txt "zzq b") mk "$ma" zzqm58 -p "$mb")" refs/heads/zzqf17 "$zero"
# a binary file is not scanned (the gate check's rule)
_bin=$(printf 'zzq\000 on %s\n' "$syn" | w hash-object -w --stdin)
rm -f "$tmp/tree.idx"
GIT_INDEX_FILE=$tmp/tree.idx w update-index --add --cacheinfo "100644,$_bin,zzq.bin"
case_ ok refs/heads/zzqf18 "$(TR=$(GIT_INDEX_FILE=$tmp/tree.idx w write-tree) mk "$m1" zzqm59)" refs/heads/zzqf18 "$zero"
# two refs in one push, each judged on its own files
case_ ok refs/heads/zzqf19 "$(TR=$(tree scripts/y.sh "echo zzq clean") mk "$m1" zzqm60)" refs/heads/zzqf19 "$zero"
case_ "$FB" refs/heads/zzqf20 "$filebad" refs/heads/zzqf20 "$zero"
run "files"

# A clean push of 50 commits that add 200 files.
{
	i=1
	while [ "$i" -le 50 ]; do
		printf 'commit refs/heads/zzqmany\ncommitter zzqa <zzqa@zzqmail.zzq> %d +0000\ndata 8\nzzq%05d\n' "$((1700000000 + i))" "$i"
		[ "$i" -ne 1 ] || printf 'from %s\n' "$m1"
		j=1
		while [ "$j" -le 4 ]; do
			printf 'M 100644 inline scripts/zzqmany/f%02d-%d.sh\ndata <<ZZQEND\n#!/bin/sh\necho zzq clean file %d of commit %d\nZZQEND\n' "$i" "$j" "$j" "$i"
			j=$((j + 1))
		done
		i=$((i + 1))
	done
} | w fast-import --quiet
[ "$(w ls-tree -r --name-only zzqmany | wc -l)" -eq 200 ] || note "the 50-commit push does not add 200 files"
case_ ok refs/heads/zzqmany "$(w rev-parse zzqmany)" refs/heads/zzqmany "$zero"
run "a clean push of 50 commits adding 200 files"

# The user name in one's own configured author identity is already public.
mine=$(AN="Zzqa $login Zzqb" AE="$login@zzqmail.zzq" mk "$m1" zzqm24)
case_ ok refs/heads/zzqo1 "$mine" refs/heads/zzqo1 "$zero"
case_ "$M" refs/heads/zzqo2 "$(AN="Zzqa $login Zzqb" mk "$m1" "zzqm25 $login")" refs/heads/zzqo2 "$zero"
case_ "$A" refs/heads/zzqo3 "$(AN="Zzqa $syn" mk "$m1" zzqm26)" refs/heads/zzqo3 "$zero"
run "own author identity (user.name)" GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=user.name "GIT_CONFIG_VALUE_0=Zzqa $login Zzqb"
case_ ok refs/heads/zzqo1 "$mine" refs/heads/zzqo1 "$zero"
run "own author identity (user.email)" GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=user.email "GIT_CONFIG_VALUE_0=$login@zzqmail.zzq"
case_ "$A" refs/heads/zzqo1 "$mine" refs/heads/zzqo1 "$zero"
run "not one's own: e-mail domain only" GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=user.email "GIT_CONFIG_VALUE_0=zzqa@$login.zzq"
case_ "$A" refs/heads/zzqo1 "$mine" refs/heads/zzqo1 "$zero"
run "not one's own: part of a longer word" GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=user.name "GIT_CONFIG_VALUE_0=Zzqa ${login}x"

# The user name that is the owner handle of origin's GitHub address is public:
# it is skipped as a whole word; host name and home path are still refused.
# The address forms themselves are proven by tests/check-no-local-identity.sh,
# whose owner_is must be the same text as the hook's.
sed -n '/^# ---- owner_is: the same/,/^# ---- end of owner_is/p' "$hook" > "$tmp/owner.hook"
sed -n '/^# ---- owner_is: the same/,/^# ---- end of owner_is/p' tests/check-no-local-identity.sh > "$tmp/owner.check"
{ [ -s "$tmp/owner.hook" ] && cmp -s "$tmp/owner.hook" "$tmp/owner.check"; } ||
	note "owner_is differs between the hook and tests/check-no-local-identity.sh"
# The two read the names the same way, by the same text.
sed -n '/^# ---- real_names: the same/,/^# ---- end of real_names/p' "$hook" > "$tmp/names.hook"
sed -n '/^# ---- real_names: the same/,/^# ---- end of real_names/p' tests/check-no-local-identity.sh > "$tmp/names.check"
{ [ -s "$tmp/names.hook" ] && cmp -s "$tmp/names.hook" "$tmp/names.check"; } ||
	note "real_names differs between the hook and tests/check-no-local-identity.sh"
Hm="this machine's home identity"
owner_cases() {
	case_ ok refs/heads/zzqw1 "$(mk "$m1" "zzqm30 note for $login")" refs/heads/zzqw1 "$zero"
	case_ ok refs/heads/zzqw2 "$(AN="$login" mk "$m1" zzqm31)" refs/heads/zzqw2 "$zero"
	case_ ok refs/tags/zzqw3 "$(tag "$m1" commit zzqw3 "$login was here")" refs/tags/zzqw3 "$zero"
	# a longer word holding the user name refuses in a ref name only
	case_ ok refs/heads/zzqw5 "$(mk "$m1" "zzqm33 x${login}x")" refs/heads/zzqw5 "$zero"
	case_ ok refs/heads/zzqx "$m1" "refs/heads/zzqt/$login/zzqtopic" "$zero"
	case_ "the remote ref name contains $U" refs/heads/zzqx "$m1" "refs/heads/zzqt/x${login}x" "$zero"
	case_ "the local ref name contains $U" "refs/heads/${login}2" "$m1" refs/heads/zzqplain "$zero"
	case_ "the remote ref name contains $U" refs/heads/zzqx "$m1" "refs/heads/zzqt/zz${login}" "$zero"
	case_ "$M $(printf '%.10s' "$m3") contains $H" refs/heads/zzqmain "$m3" refs/heads/zzqmain "$m1"
	_hm=$(mk "$m1" "zzqm32 see /zzqhome/$login/file")
	case_ "$M $(printf '%.10s' "$_hm") contains $Hm" refs/heads/zzqw4 "$_hm" refs/heads/zzqw4 "$zero"
	case_ "the remote ref name contains $H" refs/heads/zzqx "$m1" "refs/heads/$login/$syn" "$zero"
	# in files: the user name as a word is skipped, the home path is not
	case_ ok refs/heads/zzqw6 "$(TR=$(tree "scripts/$login.sh" "echo by $login") mk "$m1" zzqm34)" refs/heads/zzqw6 "$zero"
	case_ "$FB $(printf '%.10s' "$(printf 'see /zzqhome/%s/file\n' "$login" | w hash-object --stdin)")) contains $Hm" \
		refs/heads/zzqw7 "$(TR=$(tree scripts/x.sh "see /zzqhome/$login/file") mk "$m1" zzqm35)" refs/heads/zzqw7 "$zero"
}
w remote set-url origin "https://github.com/Zzq${login#zzq}/zzqrepo.git"
owner_cases
shells="dash bash sh"
run "the user name is the owner handle" HOME="/zzqhome/$login"
shells=sh
# Each user name is judged on its own: the test user name being the owner
# handle does not take the real one out of the check.
if [ "${#real_user}" -ge 3 ]; then
	case_ "$M" refs/heads/zzqw8 "$(mk "$m1" "zzqm36 note for $real_user")" refs/heads/zzqw8 "$zero"
	run "the owner handle skips only its own name: the real user name is still refused" HOME="/zzqhome/$login"
fi
# Skipped only when the last component of HOME is the user name: with HOME
# unset, with another last component or ending in /, the user name is checked.
for _h in unset /zzqhome/zzqx "/zzqhome/$login/"; do
	case_ "$M" refs/heads/zzqw1 "$(mk "$m1" "zzqm30 note for $login")" refs/heads/zzqw1 "$zero"
	case_ "the remote ref name contains $U" refs/heads/zzqx "$m1" "refs/heads/zzqt/$login/zzqtopic" "$zero"
	case $_h in
	unset) run "the owner handle, HOME unset" -u HOME ;;
	*/) run "the owner handle, HOME ends in /" HOME="$_h" ;;
	*) run "the owner handle, HOME elsewhere" HOME="$_h" ;;
	esac
done
# Not skipped: another owner, an undeterminable owner, two URLs. The home
# ends in the user name, so only owner_is can keep the skip off.
_i=0
for _url in https://github.com/zzqother/zzqrepo.git "zzqalias:$login/zzqrepo.git" "file:///zzq/$login/zzqrepo"; do
	_i=$((_i + 1))
	w remote set-url origin "$_url"
	case_ "$M" refs/heads/zzqw1 "$(mk "$m1" "zzqm30 note for $login")" refs/heads/zzqw1 "$zero"
	case_ "the remote ref name contains $U" refs/heads/zzqx "$m1" "refs/heads/zzqt/$login/zzqtopic" "$zero"
	run "the owner handle is not the user name (address $_i of 3)" HOME="/zzqhome/$login"
done
# Two URLs, the owner's first (`get-url` without --all would give only it),
# and a home ending in the user name, so only owner_is can keep the skip off.
w remote set-url origin "https://github.com/$login/zzqrepo.git"
w config --add remote.origin.url "zzqalias:$login/zzqrepo.git"
case_ "$M" refs/heads/zzqw1 "$(mk "$m1" "zzqm30 note for $login")" refs/heads/zzqw1 "$zero"
run "origin with two URLs" HOME="/zzqhome/$login"
w config --unset-all remote.origin.url
w config remote.origin.url "$tmp/remote.git"

# Tag chains: TAG_DEPTH (10) levels are followed, one more is refused.
t=$tok
i=1
while [ "$i" -le 10 ]; do
	t=$(tag "$t" tag "zzqd$i" zzqclean)
	[ "$i" -ne 9 ] || t9=$t
	i=$((i + 1))
done
case_ ok refs/tags/zzqd9 "$t9" refs/tags/zzqd9 "$zero"
run "a chain of ten tags"
printf 'refs/tags/zzqd10 %s refs/tags/zzqd10 %s\n' "$t" "$zero" > "$tmp/in"
rc=0
(cd "$tmp/w" && sh "$hook" origin x) < "$tmp/in" > "$tmp/out" 2>&1 || rc=$?
{ [ "$rc" -ne 0 ] && grep -q 'chain of more than 10 tags' "$tmp/out"; } || note "a chain of eleven tags was not refused"
leaks "tag chain"
: > "$tmp/in"

# Scan cap: the test override can only lower it.
c3=$(mk "$(mk "$(mk "$m1" zzqk1)" zzqk2)" zzqk3)
case_ "more than 2 commits to scan" refs/heads/zzqk "$c3" refs/heads/zzqk "$zero"
case_ ok refs/heads/zzqk "$c3" refs/heads/zzqk "$(w rev-parse "$c3~1")"
case_ ok refs/heads/zzqk "$c3" refs/heads/zzqk "$(w rev-parse "$c3~2")"
run "cap 2" YQ2_IDENTITY_TEST_SCAN_CAP=2
for cap in 02 08 2x ''; do
	case_ ok refs/heads/zzqk "$c3" refs/heads/zzqk "$zero"
	run "cap '$cap' is ignored" "YQ2_IDENTITY_TEST_SCAN_CAP=$cap"
done
{
	i=1
	while [ "$i" -le 1001 ]; do
		printf 'commit refs/heads/zzqbig\ncommitter zzqa <zzqa@zzqmail.zzq> %d +0000\ndata 8\nzzq%05d\n' "$((1700000000 + i))" "$i"
		[ "$i" -ne 1 ] || printf 'from %s\n' "$m1"
		i=$((i + 1))
	done
} | w fast-import --quiet
big=$(w rev-parse zzqbig)
case_ "more than 1000 commits to scan" refs/heads/zzqbig "$big" refs/heads/zzqbig "$zero"
run "a cap above the default is ignored" YQ2_IDENTITY_TEST_SCAN_CAP=999999

# ---- Fail closed. ----------------------------------------------------------
closed() { # $1 = label, $2 = text the refusal must hold, [$3 = hook]; stdin of the hook in $tmp/in
	_rc=0
	(cd "$tmp/w" && sh "${3:-$hook}" origin x) < "$tmp/in" > "$tmp/out" 2>&1 || _rc=$?
	{ [ "$_rc" -ne 0 ] && grep -q "^pre-push: refused: .*$2" "$tmp/out"; } || { note "$1: not refused"; redact < "$tmp/out"; }
	leaks "$1"
}
printf 'refs/heads/zzqmain %s refs/heads/zzqmain 1111111111111111111111111111111111111111\n' "$m2" > "$tmp/in"
closed "unknown remote commit" 'is not known here; fetch the remote first'
dangling=$(printf 'object 2222222222222222222222222222222222222222\ntype commit\ntag zzqdg\ntagger zzqa <zzqa@zzqmail.zzq> 1700000000 +0000\n\nzzq\n' |
	w hash-object -t tag -w --literally --stdin)
printf 'refs/tags/zzqdg %s refs/tags/zzqdg %s\n' "$dangling" "$zero" > "$tmp/in"
closed "tag on a missing object" 'cannot read object'
printf 'refs/heads/zzqmiss 3333333333333333333333333333333333333333 refs/heads/zzqmiss %s\n' "$zero" > "$tmp/in"
closed "unknown local object" 'cannot read object'
# A blob a pushed commit adds that git cannot read.
_gone=$(printf 'zzq gone\n' | w hash-object -w --stdin)
rm -f "$tmp/tree.idx"
GIT_INDEX_FILE=$tmp/tree.idx w update-index --add --cacheinfo "100644,$_gone,zzq-gone.txt"
gone=$(TR=$(GIT_INDEX_FILE=$tmp/tree.idx w write-tree) mk "$m1" zzqm62)
rm -f "$tmp/w/.git/objects/$(printf '%s' "$_gone" | cut -c1-2)/$(printf '%s' "$_gone" | cut -c3-)"
printf 'refs/heads/zzqgone %s refs/heads/zzqgone %s\n' "$gone" "$zero" > "$tmp/in"
closed "a blob that cannot be read" 'cannot list the new objects of ref number 1'
# A blob whose file is there but corrupt: git rev-list lists it, git cat-file
# cannot read it.
_bad=$(printf 'zzq corrupt\n' | w hash-object -w --stdin)
rm -f "$tmp/tree.idx"
GIT_INDEX_FILE=$tmp/tree.idx w update-index --add --cacheinfo "100644,$_bad,zzq-corrupt.txt"
corrupt=$(TR=$(GIT_INDEX_FILE=$tmp/tree.idx w write-tree) mk "$m1" zzqm63)
_of="$tmp/w/.git/objects/$(printf '%s' "$_bad" | cut -c1-2)/$(printf '%s' "$_bad" | cut -c3-)"
chmod u+w "$_of"
printf 'zzq not a zlib stream\n' > "$_of"
printf 'refs/heads/zzqcorrupt %s refs/heads/zzqcorrupt %s\n' "$corrupt" "$zero" > "$tmp/in"
closed "a blob whose object is corrupt" 'cannot read an object of ref number 1'
# The gate check beside the hook is missing, fails, ends without its verdict,
# or reports a match the hook cannot place: refused. The commit is clean.
fileok=$(TR=$(tree scripts/ok.sh "echo zzq clean") mk "$m1" zzqm61)
printf 'refs/heads/zzqok %s refs/heads/zzqok %s\n' "$fileok" "$zero" > "$tmp/in"
mkdir -p "$tmp/alt/.githooks" "$tmp/alt/tests"
cp "$hook" "$tmp/alt/.githooks/pre-push"
closed "the file scan is missing" 'tests/check-no-local-identity.sh, which scans the files, is missing' "$tmp/alt/.githooks/pre-push"
stub() { printf '#!/bin/sh\n%s\n' "$1" > "$tmp/alt/tests/check-no-local-identity.sh"; }
stub 'echo zzq stub; exit 2'
closed "the file scan fails" 'the file scan (tests/check-no-local-identity.sh) could not run' "$tmp/alt/.githooks/pre-push"
stub 'exit 0'
closed "the file scan ends without its verdict" 'the file scan (tests/check-no-local-identity.sh) could not run' "$tmp/alt/.githooks/pre-push"
stub "echo \"check-no-local-identity: tracked files contain this machine's identity (values not shown); remove them:\"; echo '  zzq:1 (host)'; exit 1"
closed "the file scan reports a match it cannot place" 'a match this hook cannot place' "$tmp/alt/.githooks/pre-push"
stub "echo \"check-no-local-identity: tracked files contain this machine's identity (values not shown); remove them:\"; exit 1"
closed "the file scan reports no match it can place" 'a match this hook cannot place' "$tmp/alt/.githooks/pre-push"
# A push that adds no blob and no path runs no file scan (the failing stub
# would refuse it): a commit the remote has, under a new name, a tooling
# commit on the empty tree, and claim and gate-clear markers on main's tree
# with main as parent (they change no path).
stub 'echo zzq stub; exit 2'
printf 'refs/heads/zzqpub2 %s refs/heads/zzqpub2 %s\nrefs/heads/zzqc %s refs/heads/claim/ZZQ9 %s\n' \
	"$pub" "$zero" "$(mk - "claimed ZZQ9 by m-0123456789ab/agent/zzq")" "$zero" > "$tmp/in"
printf 'refs/heads/zzqc %s refs/heads/claim/ZZQ8 %s\nrefs/heads/zzqc %s refs/heads/gate-clear/ZZQ8 %s\n' \
	"$(marker "claim ZZQ8 by $tok/agent/$sess at $when (lease 6h) [nonce:$nonce]")" "$zero" \
	"$(marker "gate-clear ZZQ8 by $tok/agent at $when")" "$zero" >> "$tmp/in"
_rc=0
(cd "$tmp/w" && sh "$tmp/alt/.githooks/pre-push" origin x) < "$tmp/in" > "$tmp/out" 2>&1 || _rc=$?
{ [ "$_rc" -eq 0 ] && [ ! -s "$tmp/out" ]; } || { note "a push that adds no file ran the file scan"; redact < "$tmp/out"; }
leaks "a push that adds no file"
# A temporary directory whose path holds a colon cannot be named in git's
# colon-split list of alternates (the paths blob would go unread): refused.
mkdir "$tmp/hooktmp/zzq:x"
printf 'refs/heads/zzqok %s refs/heads/zzqok %s\n' "$fileok" "$zero" > "$tmp/in"
_rc=0
(cd "$tmp/w" && TMPDIR="$tmp/hooktmp/zzq:x" sh "$hook" origin x) < "$tmp/in" > "$tmp/out" 2>&1 || _rc=$?
{ [ "$_rc" -ne 0 ] && grep -q "^pre-push: refused: the temporary directory's path holds a colon" "$tmp/out"; } ||
	{ note "a temporary directory with a colon: not refused"; redact < "$tmp/out"; }
leaks "a temporary directory with a colon"
rmdir "$tmp/hooktmp/zzq:x"
# The real names' tools missing or printing nothing (the test-only
# YQ2_IDENTITY_TEST_TOOL puts a path below /dev/null, or /usr/bin/true, in
# place of one): refused, one case each.
printf 'refs/heads/zzqmain %s refs/heads/zzqmain %s\n' "$m2" "$m1" > "$tmp/in"
export YQ2_IDENTITY_TEST_TOOL
for YQ2_IDENTITY_TEST_TOOL in host-missing host-empty; do
	closed "the host-name tool: $YQ2_IDENTITY_TEST_TOOL" "cannot read this machine's host name"
done
for YQ2_IDENTITY_TEST_TOOL in user-missing user-empty; do
	closed "the user-name tool: $YQ2_IDENTITY_TEST_TOOL" "cannot read the local user name"
done
unset YQ2_IDENTITY_TEST_TOOL
case_ ok refs/heads/zzqmain "$m2" refs/heads/zzqmain "$m1"
run "an unknown YQ2_IDENTITY_TEST_TOOL value is ignored" YQ2_IDENTITY_TEST_TOOL=zzq-none
# A `hostname` and a `whoami` on PATH that fail, or print a blank, are not
# read at all (the round-1 hook refused here).
shim hostname ''
shim whoami ''
case_ ok refs/heads/zzqmain "$m2" refs/heads/zzqmain "$m1"
run "a failing hostname and whoami on PATH are not read"
shim hostname ' '
shim whoami ' '
case_ ok refs/heads/zzqmain "$m2" refs/heads/zzqmain "$m1"
run "a hostname and whoami on PATH printing a blank are not read"
shim hostname "$tok"
shim whoami agent
# Words shorter than three characters, and HOME=/, are not compared.
case_ ok refs/heads/zzqs "$(mk "$m1" "zz zzq zz/")" refs/heads/zzqs "$zero"
run "short words" HOME=/ YQ2_IDENTITY_TEST_EXTRA_WORD= YQ2_IDENTITY_TEST_HOST=zz YQ2_IDENTITY_TEST_USER=zz

# The hook removes its temporary files, refused or not.
[ -z "$(ls -A "$tmp/hooktmp")" ] || note "the hook left temporary files behind"

[ "$fails" -eq 0 ] || exit 1
_ok=1
echo "check-pre-push-hook: ok"
