#!/bin/sh
# Invariant: .githooks/pre-commit refuses a commit that stages the working
# machine's identity, under any path, or a credential, and lets an ordinary
# commit through without running more of the gate than the paths it touches
# call for.
#
# Self-test with no network, in a throwaway directory under mktemp -d.
# Throwaway repositories hold a copy of the hook and of what it runs, and
# wire the hook with core.hooksPath. Every case stages one file and runs a
# real `git commit`, so the hook gets what git gives it: no arguments, no
# standard input, GIT_INDEX_FILE naming the index being committed, the top
# of the work tree as its directory. A case's verdict is the commit's exit
# status, whether HEAD moved, and the line the hook (or the check it ran)
# prints for that verdict; one assertion per case.
#
# Repository "id" holds a copy of tests/check-no-local-identity.sh, the real
# identity check, and proves that it refuses the word under each path the
# hook once skipped. In repository "st" the two checks the hook runs on every
# commit and a third, tests/check-zzq-sentinel.sh, are stand-ins that record
# that they ran and pass (or fail, when ZZQ_FAIL names them): a commit under
# scripts/ is shown to run the identity and the memory check and not the
# rest of tests/check-*.sh, one under doc/ to run the rest, a failing check
# to refuse the commit, and each credential case judges the secret pattern
# alone. The identity check runs its own self-test, a few seconds, on every
# commit; the stand-ins keep the cases that do not need it fast.
#
# The identity is synthetic: the identity check's test-only
# YQ2_IDENTITY_TEST_HOST and YQ2_IDENTITY_TEST_USER add a made-up host and
# user name to the real ones, and HOME is a made-up path, so the check really
# reads them at run time; the names are in none of the copied files. The
# identity shim is first on PATH throughout, as it is for every marker
# writer (its `hostname` prints a machine token, its `whoami` prints
# `agent`): the check reads the real names by absolute path, so a staged file
# naming this machine's real host or user name (built at run time from
# /bin/hostname and /usr/bin/id, kept in a throwaway repository, never
# printed) is refused, and one holding the token and `agent` is not. The
# credential samples are built at run time from pieces, so this file holds
# no string of a token's shape. The output of every case is checked for the
# synthetic values, the samples and this machine's real values.
#
# Isolation from the caller: every GIT_* variable is unset (a pre-commit hook
# exports GIT_INDEX_FILE), repositories are made with `git init --template=`,
# no global or system configuration or attributes are read
# (GIT_CONFIG_GLOBAL=/dev/null, GIT_CONFIG_NOSYSTEM, GIT_ATTR_NOSYSTEM), and
# HOME and XDG_CONFIG_HOME point where nothing is.
#
# Usage: tests/check-pre-commit-hook.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C
me=check-pre-commit-hook
top=$(pwd)
hook=$top/.githooks/pre-commit
[ -f "$hook" ] || { echo "$me: .githooks/pre-commit is missing"; exit 1; }

# Read by absolute path, as the check reads them (a shim on the caller's PATH
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
# Under /tmp, not $TMPDIR, as in tests/check-pre-push-hook.sh: a path that
# holds the login name would put a real value into the output.
tmp=$(mktemp -d /tmp/q2-precommit.XXXXXX)
# A shell error must not exit 0 (bash 3.2 loses the status in an EXIT trap).
trap 'rm -rf "${tmp:?}"; [ "${_ok:-}" = 1 ] || exit 1' EXIT
_tl=$(printf '%s' "$tmp" | tr '[:upper:]' '[:lower:]')
printf '%s\n' "$real_words" | while IFS= read -r _w; do
	[ "${#_w}" -ge 3 ] || continue
	_w=$(printf '%s' "$_w" | tr '[:upper:]' '[:lower:]')
	case $_tl in *"$_w"*) echo x ;; esac
done | grep -q x && {
	echo "$me: the temporary directory's path holds an identity value; this check needs a /tmp path without one"
	exit 1
}
mkdir "${tmp:?}/bin" "${tmp:?}/t"
syn=zzqpc-synthetic-host
login=zzqpclogin
synhome=/zzqpchome/zzqpcx
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
YQ2_IDENTITY_TEST_HOST=Zzqpc-Synthetic-Host
YQ2_IDENTITY_TEST_USER=$login
export YQ2_IDENTITY_TEST_HOST YQ2_IDENTITY_TEST_USER
PATH="$tmp/bin:$PATH"
HOME=$synhome
XDG_CONFIG_HOME=$tmp/xdg
GIT_CONFIG_GLOBAL=/dev/null
GIT_CONFIG_NOSYSTEM=1
GIT_ATTR_NOSYSTEM=1
TMPDIR=$tmp/t
ZZQ_RAN=$tmp/ran
export PATH HOME XDG_CONFIG_HOME GIT_CONFIG_GLOBAL GIT_CONFIG_NOSYSTEM GIT_ATTR_NOSYSTEM TMPDIR ZZQ_RAN

fails=0
note() { echo "$me: FAIL: $*"; fails=$((fails + 1)); }

# ---- Credential samples, built from pieces. --------------------------------
# rep <count> <alphabet>: <count> characters, cycling through <alphabet>.
rep() {
	awk -v n="$1" -v a="$2" 'BEGIN { for (i = 0; i < n; i++) printf "%s", substr(a, i % length(a) + 1, 1) }'
}
up=ABCDEFGHIJKLMNOPQRSTUVWXYZ
lo=abcdefghijklmnopqrstuvwxyz
dg=0123456789
: > "$tmp/samples"
sample() { # $1 = label; rest = pieces, joined
	_l=$1; shift
	printf '%s\t' "$_l" >> "$tmp/samples"
	printf '%s' "$@" >> "$tmp/samples"
	printf '\n' >> "$tmp/samples"
}
sample "private key header" '-----BEGIN RSA ' 'PRIV' 'ATE KEY-----'
sample "AWS access key" 'AK' 'IA' "$(rep 16 "$dg$up")"
for _c in p o u s r; do
	sample "gh${_c}_ token" 'g' 'h' "$_c" '_' "$(rep 36 "$up$lo$dg")"
done
sample "fine-grained token" 'git' 'hub_' 'pat_' "$(rep 82 "$up${lo}_$dg")"
for _c in b a p r s; do
	sample "xox${_c}- token" 'xo' 'x' "$_c" '-' "$(rep 12 "$up$lo$dg-")"
done
sample "Anthropic API key" 'sk' '-an' 't-' "$(rep 40 "$up${lo}_$dg-")"
sample "Google API key" 'AI' 'za' "$(rep 35 "$dg$up${lo}_-")"
sample "GitLab token" 'gl' 'pat' '-' "$(rep 20 "$up${lo}_$dg-")"
sample "Stripe live key" 'sk' '_li' 've_' "$(rep 24 "$up$lo$dg")"

# redact < file: the tmp path becomes TMP; a line that holds a synthetic or
# real value or a sample is replaced whole.
redact() {
	YQ2_T=$tmp YQ2_R="$real_words
$syn
$login
$synhome
$(cut -f2 "$tmp/samples")" awk '
	BEGIN { t = ENVIRON["YQ2_T"]; n = split(ENVIRON["YQ2_R"], w, "\n") }
	{
		while ((p = index($0, t)) > 0) $0 = substr($0, 1, p - 1) "TMP" substr($0, p + length(t))
		l = tolower($0)
		for (i = 1; i <= n; i++)
			if (length(w[i]) >= 3 && index(l, tolower(w[i]))) { print "  <line withheld: it holds an identity value or a sample>"; next }
		print "  " $0
	}'
}
# leaks <label>: the output must hold no synthetic or real value and no
# sample. Commit ids are blanked first (hex can spell a short word).
leaks() {
	sed -E 's/(^|[^0-9a-f])[0-9a-f]{7,40}([^0-9a-f]|$)/\1ID\2/g' "$tmp/out" > "$tmp/out.ids"
	for _w in "$syn" "$login" "$synhome"; do
		grep -qiF -e "$_w" "$tmp/out.ids" && note "$1: output printed a synthetic value"
	done
	cut -f2 "$tmp/samples" | while IFS= read -r _w; do
		grep -qF -e "$_w" "$tmp/out.ids" && echo x
	done | grep -q x && note "$1: output printed a credential sample"
	printf '%s\n' "$real_words" | while IFS= read -r _w; do
		[ "${#_w}" -ge 3 ] || continue
		grep -qiF -e "$_w" "$tmp/out.ids" && echo x
	done | grep -q x && note "$1: output printed a real identity value"
	return 0
}

# ---- Throwaway repositories. -----------------------------------------------
# stand_in <file> <name>: a check that appends <name> to $ZZQ_RAN, then
# fails with a line saying so when $ZZQ_FAIL is <name> and passes otherwise.
stand_in() {
	printf '#!/bin/sh\necho %s >> "$ZZQ_RAN"\n[ "${ZZQ_FAIL:-}" != %s ] || { echo "zzq stand-in %s failed"; exit 1; }\n' "$2" "$2" "$2" > "$1"
	chmod +x "$1"
}
mkrepo() { # $1 = directory, $2 = id or st
	git init -q --template= "$1"
	mkdir "$1/.githooks" "$1/tests"
	cp "$hook" "$1/.githooks/pre-commit"
	chmod +x "$1/.githooks/pre-commit"
	if [ "$2" = id ]; then
		cp tests/check-no-local-identity.sh "$1/tests/"
		chmod +x "$1/tests/check-no-local-identity.sh"
	else
		stand_in "$1/tests/check-no-local-identity.sh" ident
	fi
	stand_in "$1/tests/check-no-tracked-memory.sh" mem
	stand_in "$1/tests/check-zzq-sentinel.sh" sentinel
	printf '.claude/memory/\n' > "$1/.gitignore"
	git -C "$1" config user.name zzqa
	git -C "$1" config user.email zzqa@zzqmail.zzq
	git -C "$1" config commit.gpgsign false
	git -C "$1" add -A
	git -C "$1" commit -q -m zzq-base
	# Wired only now: the base commit runs no hook.
	git -C "$1" config core.hooksPath .githooks
}
mkrepo "$tmp/id" id
mkrepo "$tmp/st" st
# The synthetic names are in no file of the repositories.
for _w in "$syn" "$login" "$synhome"; do
	if git -C "$tmp/id" grep -qiF -e "$_w" HEAD -- . || git -C "$tmp/st" grep -qiF -e "$_w" HEAD -- .; then
		note "a copied file holds a synthetic value; this test needs other names"
	fi
done

# attempt <label> <want> <repo> <file> <content> [replace]: stages <file>
# holding <content> and runs git commit. <want> is "ok" or the text that one
# line of the output must hold. With "replace", the staged blob is first
# replaced locally (git replace) by a clean twin, and the replacement is
# removed after the commit. A refused commit's file is unstaged and removed.
attempt() {
	_label=$1; _want=$2; _r=$3; _f=$4
	mkdir -p "$(dirname "$_r/$_f")"
	printf '%s\n' "$5" > "$_r/$_f"
	git -C "$_r" add -- "$_f"
	_rb=
	if [ "${6:-}" = replace ]; then
		_rb=$(git -C "$_r" rev-parse ":$_f")
		git -C "$_r" replace "$_rb" "$(printf 'zzq clean twin\n' | git -C "$_r" hash-object -w --stdin)"
	fi
	_before=$(git -C "$_r" rev-parse HEAD)
	: > "$ZZQ_RAN"
	_rc=0
	(cd "$_r" && git commit -q -m "zzq $_label") > "$tmp/out" 2>&1 || _rc=$?
	[ -z "$_rb" ] || git -C "$_r" replace -d "$_rb" > /dev/null
	_after=$(git -C "$_r" rev-parse HEAD)
	_f0=$fails
	if [ "$_want" = ok ]; then
		{ [ "$_rc" -eq 0 ] && [ "$_after" != "$_before" ]; } || note "$_label: wanted the commit made, got status $_rc"
	else
		{ [ "$_rc" -ne 0 ] && [ "$_after" = "$_before" ]; } || note "$_label: wanted the commit refused"
		grep -qF -e "$_want" "$tmp/out" || note "$_label: no line holding \"$_want\""
	fi
	leaks "$_label"
	[ "$fails" -eq "$_f0" ] || redact < "$tmp/out"
	git -C "$_r" reset -q --hard
}
# ran <label> <want>: the stand-in checks that ran in the last attempt, in
# order, one word each, must be <want>.
ran() {
	_got=$(tr '\n' ' ' < "$ZZQ_RAN" | sed 's/ $//')
	[ "$_got" = "$2" ] || note "$1: the checks that ran were \"$_got\", wanted \"$2\""
}

I="check-no-local-identity: tracked files contain this machine's identity"
S="pre-commit: the staged diff looks like it contains a secret"

# ---- The identity check runs on every commit, whatever the paths. --------
attempt "identity word under scripts/" "$I" "$tmp/id" scripts/zzq-x.sh "echo built on $syn"
attempt "identity word under stuff/" "$I" "$tmp/id" stuff/zzq/x.c "/* built on $syn */"
attempt "identity word under .githooks/" "$I" "$tmp/id" .githooks/zzq-note "built on $syn"
attempt "identity word under third_party/" "$I" "$tmp/id" third_party/zzq/x.h "/* built on $syn */"
attempt "identity word under scripts/pins/scenarios/" "$I" "$tmp/id" scripts/pins/scenarios/zzq.cfg "// built on $syn"
attempt "a clean change under scripts/, the real identity check" ok "$tmp/id" scripts/zzq-clean.sh "echo zzq clean"

# ---- The identity shim on PATH: the real names still decide. --------------
# The refusal must name the staged file and the kind of value, so a hit
# elsewhere in the tree cannot stand in for it.
if [ "${#real_host}" -ge 3 ]; then
	attempt "the shim on PATH: a staged file naming the real host" "  scripts/zzq-rh.sh:1 (host)" "$tmp/id" scripts/zzq-rh.sh "echo built on $real_host"
else
	echo "$me: not run: the real-host case (the check does not compare a host name shorter than three characters)"
fi
if [ "${#real_user}" -ge 3 ] && [ "$real_user" != agent ]; then
	attempt "the shim on PATH: a staged file naming the real user as a path component" "  scripts/zzq-ru.sh:1 (user)" "$tmp/id" scripts/zzq-ru.sh "cd /srv/$real_user/zzq"
	attempt "the shim on PATH: a staged file holding the token and agent" ok "$tmp/id" scripts/zzq-agent.sh "echo run by agent on $tok"
else
	echo "$me: not run: the real-user and agent cases (the user name is shorter than three characters, which the check does not compare, or is agent itself)"
fi
# The real names' tools missing or printing nothing (the check's test-only
# YQ2_IDENTITY_TEST_TOOL): the commit is refused, one case each.
export YQ2_IDENTITY_TEST_TOOL
for YQ2_IDENTITY_TEST_TOOL in host-missing host-empty; do
	attempt "the host-name tool: $YQ2_IDENTITY_TEST_TOOL" "cannot read this machine's host name" "$tmp/id" "scripts/zzq-t-$YQ2_IDENTITY_TEST_TOOL.sh" "echo zzq clean"
done
for YQ2_IDENTITY_TEST_TOOL in user-missing user-empty; do
	attempt "the user-name tool: $YQ2_IDENTITY_TEST_TOOL" "cannot read the local user name" "$tmp/id" "scripts/zzq-t-$YQ2_IDENTITY_TEST_TOOL.sh" "echo zzq clean"
done
unset YQ2_IDENTITY_TEST_TOOL
# A staged file replaced locally by a clean twin (refs/replace/): `git grep
# --cached`, which the check uses, reads the replacement unless the hook
# turns replacements off, and the commit records the real blob.
attempt "a staged file replaced locally by a clean twin" "  scripts/zzq-rp.sh:1 (host)" "$tmp/id" scripts/zzq-rp.sh "echo built on $syn" replace

# ---- Which checks run: every commit runs the identity and memory checks, the
# rest only for the paths they cover (stand-ins). ---------------------------
attempt "a clean change under scripts/" ok "$tmp/st" scripts/zzq-clean.sh "echo zzq clean"
ran "a clean change under scripts/" "mem ident"
attempt "a clean change under stuff/" ok "$tmp/st" stuff/zzq.c "/* zzq clean */"
ran "a clean change under stuff/" "mem ident"
attempt "a clean change under doc/" ok "$tmp/st" doc/zzq.md "zzq clean"
ran "a clean change under doc/" "mem ident sentinel"

# ---- Fail closed. ----------------------------------------------------------
ZZQ_FAIL=ident
export ZZQ_FAIL
attempt "the identity check fails" "zzq stand-in ident failed" "$tmp/st" scripts/zzq-f1.sh "echo zzq clean"
ZZQ_FAIL=mem
attempt "the memory check fails" "zzq stand-in mem failed" "$tmp/st" scripts/zzq-f2.sh "echo zzq clean"
unset ZZQ_FAIL
mv "$tmp/st/tests/check-no-local-identity.sh" "$tmp/st-ident"
attempt "the identity check is missing" "check-no-local-identity.sh" "$tmp/st" scripts/zzq-f3.sh "echo zzq clean"
mv "$tmp/st-ident" "$tmp/st/tests/check-no-local-identity.sh"
# grep fails while the hook scans the staged diff (a shim fails the hook's
# own call, grep -aqE, and passes every other one on).
printf '#!/bin/sh\n[ "$1" != -aqE ] || exit 2\nexec %s "$@"\n' "$(command -v grep)" > "$tmp/bin/grep"
chmod +x "$tmp/bin/grep"
attempt "grep fails on the staged diff" "pre-commit: cannot scan the staged diff" "$tmp/st" scripts/zzq-f4.sh "echo zzq clean"
rm -f "$tmp/bin/grep"

# ---- Credentials: one sample per token family. -----------------------------
while IFS="$(printf '\t')" read -r _label _s; do
	attempt "a staged $_label" "$S" "$tmp/st" scripts/zzq-tok.txt "token = $_s" < /dev/null
done < "$tmp/samples"
# The fine-grained family needs 22 characters after the prefix.
_s=$(printf '%s' 'git' 'hub_' 'pat_' "$(rep 21 "$up$lo$dg")")
attempt "21 characters after the fine-grained prefix" ok "$tmp/st" scripts/zzq-tok.txt "token = $_s"
# ... and the four families added with it, one character short of each.
_s=$(printf '%s' 'sk' '-an' 't-' "$(rep 19 "$up$lo$dg")")
attempt "19 characters after the Anthropic prefix" ok "$tmp/st" scripts/zzq-tok.txt "token = $_s"
_s=$(printf '%s' 'AI' 'za' "$(rep 34 "$dg$up$lo")")
attempt "34 characters after the Google prefix" ok "$tmp/st" scripts/zzq-tok.txt "token = $_s"
_s=$(printf '%s' 'gl' 'pat' '-' "$(rep 19 "$up$lo$dg")")
attempt "19 characters after the GitLab prefix" ok "$tmp/st" scripts/zzq-tok.txt "token = $_s"
_s=$(printf '%s' 'sk' '_li' 've_' "$(rep 23 "$up$lo$dg")")
attempt "23 characters after the Stripe prefix" ok "$tmp/st" scripts/zzq-tok.txt "token = $_s"
# A committed .gitattributes that marks the file binary or without a diff
# does not take it out of the secret scan.
_s=$(awk -F '\t' '$1 == "ghp_ token" { print $2 }' "$tmp/samples")
attempt "a committed .gitattributes" ok "$tmp/st" .gitattributes "*.txt -diff
*.dat binary"
attempt "a staged token in a file marked -diff" "$S" "$tmp/st" scripts/zzq-tok2.txt "token = $_s"
attempt "a staged token in a file marked binary" "$S" "$tmp/st" scripts/zzq-tok.dat "token = $_s"
# Nor does local diff configuration: colour, an external diff, or a textconv
# driver that a committed .gitattributes names.
printf '#!/bin/sh\nexit 0\n' > "$tmp/bin/zzq-nodiff"
chmod +x "$tmp/bin/zzq-nodiff"
git -C "$tmp/st" config color.diff always
attempt "a staged token, color.diff=always" "$S" "$tmp/st" scripts/zzq-tok3.txt "token = $_s"
git -C "$tmp/st" config --unset color.diff
git -C "$tmp/st" config diff.external "$tmp/bin/zzq-nodiff"
attempt "a staged token, an external diff" "$S" "$tmp/st" scripts/zzq-tok4.txt "token = $_s"
git -C "$tmp/st" config --unset diff.external
attempt "a committed .gitattributes naming a diff driver" ok "$tmp/st" .gitattributes "*.txt -diff
*.dat binary
*.tc diff=zzq"
git -C "$tmp/st" config diff.zzq.textconv "$tmp/bin/zzq-nodiff"
attempt "a staged token, a textconv driver" "$S" "$tmp/st" scripts/zzq-tok.tc "token = $_s"
git -C "$tmp/st" config --unset diff.zzq.textconv
# The staged diff cannot be read: refused, not taken as clean. The blob's
# file is there but corrupt, so git would still write the tree and make the
# commit; only the hook's refusal stops it.
_id=$(printf 'zzq gone\n' | git -C "$tmp/st" hash-object -w --stdin)
git -C "$tmp/st" update-index --add --cacheinfo "100644,$_id,scripts/zzq-gone.txt"
_of="$tmp/st/.git/objects/$(printf '%s' "$_id" | cut -c1-2)/$(printf '%s' "$_id" | cut -c3-)"
chmod u+w "$_of"
printf 'zzq not a zlib stream\n' > "$_of"
_rc=0
(cd "$tmp/st" && git commit -q -m "zzq unreadable") > "$tmp/out" 2>&1 || _rc=$?
{ [ "$_rc" -ne 0 ] && grep -qF "pre-commit: cannot read the staged diff" "$tmp/out"; } ||
	{ note "an unreadable staged blob: wanted the secret scan to refuse"; redact < "$tmp/out"; }
leaks "an unreadable staged blob (secret scan)"
git -C "$tmp/st" rm -q --cached -- scripts/zzq-gone.txt

[ "$fails" -eq 0 ] || exit 1
_ok=1
echo "$me: ok"
