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
# The identity is synthetic: `hostname` and `whoami` are shims on PATH and
# HOME is a made-up path, so the identity check really reads them at run
# time; the names are in none of the copied files. The credential samples
# are built at run time from pieces, so this file holds no string of a
# token's shape. The output of every case is checked for the synthetic
# values, the samples and this machine's real values (read here, only ever
# compared, never printed).
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

# Read before HOME and PATH are replaced; only ever compared, never printed.
real_words="$(hostname -s 2>/dev/null || true)
$(whoami 2>/dev/null || true)
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
shim hostname Zzqpc-Synthetic-Host
shim whoami "$login"
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

# attempt <label> <want> <repo> <file> <content>: stages <file> holding
# <content> and runs git commit. <want> is "ok" or the text that one line of
# the output must hold. A refused commit's file is unstaged and removed.
attempt() {
	_label=$1; _want=$2; _r=$3; _f=$4
	mkdir -p "$(dirname "$_r/$_f")"
	printf '%s\n' "$5" > "$_r/$_f"
	git -C "$_r" add -- "$_f"
	_before=$(git -C "$_r" rev-parse HEAD)
	: > "$ZZQ_RAN"
	_rc=0
	(cd "$_r" && git commit -q -m "zzq $_label") > "$tmp/out" 2>&1 || _rc=$?
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
# A committed .gitattributes that marks the file binary or without a diff
# does not take it out of the secret scan.
_s=$(awk -F '\t' '$1 == "ghp_ token" { print $2 }' "$tmp/samples")
attempt "a committed .gitattributes" ok "$tmp/st" .gitattributes "*.txt -diff
*.dat binary"
attempt "a staged token in a file marked -diff" "$S" "$tmp/st" scripts/zzq-tok2.txt "token = $_s"
attempt "a staged token in a file marked binary" "$S" "$tmp/st" scripts/zzq-tok.dat "token = $_s"
# The staged diff cannot be read: refused, not taken as clean.
_id=$(printf 'zzq gone\n' | git -C "$tmp/st" hash-object -w --stdin)
git -C "$tmp/st" update-index --add --cacheinfo "100644,$_id,scripts/zzq-gone.txt"
rm -f "$tmp/st/.git/objects/$(printf '%s' "$_id" | cut -c1-2)/$(printf '%s' "$_id" | cut -c3-)"
_rc=0
(cd "$tmp/st" && git commit -q -m "zzq unreadable") > "$tmp/out" 2>&1 || _rc=$?
{ [ "$_rc" -ne 0 ] && grep -qF "pre-commit: cannot read the staged diff" "$tmp/out"; } ||
	{ note "an unreadable staged blob: wanted the secret scan to refuse"; redact < "$tmp/out"; }
leaks "an unreadable staged blob (secret scan)"
git -C "$tmp/st" rm -q --cached -- scripts/zzq-gone.txt

[ "$fails" -eq 0 ] || exit 1
_ok=1
echo "$me: ok"
