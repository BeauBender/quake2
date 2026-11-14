#!/bin/sh
# Invariant: .githooks/pre-push refuses a push that would publish the working
# machine's identity, and lets an ordinary push through.
#
# Self-test with no network, in a throwaway directory under mktemp -d: a bare
# repository is the remote and a clone carries a copy of the hook as its
# pre-push hook. Three real `git push` runs prove the wiring (git's stdin, a
# refusal stops the push, pushed refs become published); every other case
# feeds the hook its stdin directly, many refs per run, and checks the line
# the hook prints for each ref by its number.
#
# The identity is synthetic: `hostname` and `whoami` are shims on PATH and
# HOME is a made-up path, so the hook really reads them at run time and no
# real value is ever used as a fixture. Every name and message is built from
# the prefix "zzq". The output of each run is checked for the synthetic
# values and for this machine's real ones (read here, never printed). A
# failing case shows a redacted copy of the output.
#
# The test is immune to the caller's git environment and configuration:
# every GIT_* variable is unset (a pre-commit hook exports GIT_INDEX_FILE),
# global and system configuration are not read, templates are empty.
#
# Usage: tests/check-pre-push-hook.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C

hook=$(pwd)/.githooks/pre-push
[ -x "$hook" ] || { echo "check-pre-push-hook: .githooks/pre-push is missing or not executable"; exit 1; }

# Read before HOME and PATH are replaced; only ever compared, never printed.
real_words="$(hostname -s 2>/dev/null || true)
$(whoami 2>/dev/null || true)
${HOME:-}"

# ---- Isolation from the caller. -------------------------------------------
for _v in $(env | sed -n 's/^\(GIT_[A-Za-z0-9_]*\)=.*/\1/p'); do
	unset "$_v"
done
tmp=$(mktemp -d "${TMPDIR:-/tmp}/prepush.XXXXXX")
# A shell error must not exit 0 (bash 3.2 loses the status in an EXIT trap).
trap 'rm -rf "$tmp"; [ "${_ok:-}" = 1 ] || exit 1' EXIT
mkdir "$tmp/tpl" "$tmp/bin" "$tmp/hooks"
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 GIT_TEMPLATE_DIR="$tmp/tpl"
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
shim hostname Zzq-Synthetic-Box
shim whoami "$login"
PATH="$tmp/bin:$PATH"
HOME=$synhome
mkdir "$tmp/hooktmp"
export PATH HOME YQ2_IDENTITY_TEST_EXTRA_WORD=zzqextra TMPDIR="$tmp/hooktmp"

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

git init -q --bare "$tmp/remote.git"
git init -q "$tmp/w"
cp "$hook" "$tmp/hooks/pre-push"
w() { git -C "$tmp/w" "$@"; }
w config core.hooksPath "$tmp/hooks"
w config user.name zzqa
w config user.email zzqa@zzqmail.zzq
w config commit.gpgsign false
w config tag.gpgsign false
w remote add origin "$tmp/remote.git"
empty=$(w hash-object -t tree -w /dev/null)

# mk <parent|-> <message>: a commit; AN, AE, CN set author and committer.
mk() {
	if [ "$1" = - ]; then set -- "$2"; else set -- "$2" -p "$1"; fi
	_m=$1; shift
	GIT_AUTHOR_NAME=${AN:-zzqa} GIT_AUTHOR_EMAIL=${AE:-zzqa@zzqmail.zzq} \
		GIT_COMMITTER_NAME=${CN:-zzqa} GIT_COMMITTER_EMAIL=zzqa@zzqmail.zzq \
		GIT_AUTHOR_DATE='1700000000 +0000' GIT_COMMITTER_DATE='1700000000 +0000' \
		w commit-tree "$empty" "$@" -m "$_m"
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
claim=$(mk "$m1" "zzqm12 claimed by $syn")
gclear=$(mk "$m1" "zzq gate-clear by $syn/$login")
rpush "real push of a normal branch" ok origin zzqmain
rpush "real push of a leaking ref name" "the remote ref name contains this machine's host identity" \
	origin "zzqx:refs/heads/zzqt/$syn"
rpush "real push of several refs, tooling branches among them" ok origin \
	"$claim:refs/heads/claim/ZZQ1" "$gclear:refs/heads/gate-clear/ZZQ2" "$m1:refs/heads/zzqbehind"
[ "$(w rev-parse -q --verify refs/remotes/origin/claim/ZZQ1 || true)" = "$claim" ] ||
	note "the real push did not record the claim branch as published"

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
case_ ok refs/heads/zzqn11 "$(mk "$bad" zzqm16)" refs/heads/zzqn11 "$bad"
case_ ok refs/heads/zzqnew "$(mk "$claim" zzqm17)" refs/heads/zzqnew "$zero"
case_ ok refs/heads/zzqbehind "$claim" refs/heads/zzqbehind "$m1"
# ... but only what THIS remote has: another remote's tracking refs do not count
other=$(mk "$m1" "zzqm27 on $syn")
w update-ref refs/remotes/zzqother/zzqx "$other"
case_ "$M $(printf '%.10s' "$other")" refs/heads/zzqo "$(mk "$other" zzqm28)" refs/heads/zzqo "$zero"
# tooling branches: object scan exempt, names never
case_ ok refs/heads/zzqc "$bad" refs/heads/claim/ZZQ3 "$zero"
case_ ok refs/heads/zzqc "$bad" refs/heads/gate-clear/ZZQ4 "$zero"
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
case_ ok refs/heads/zzqc "$bad" refs/heads/gate-clear/ZZQ4 "$zero"
echo >> "$tmp/in" # an empty line is skipped and does not count as a ref
case_ "the remote ref name contains $H" refs/heads/zzqx "$m1" "refs/heads/zzqlast-$syn" "$zero"
shells="dash bash"
run "other shells"
shells=sh

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
}
w remote set-url origin "https://github.com/Zzq${login#zzq}/zzqrepo.git"
owner_cases
shells="dash bash sh"
run "the user name is the owner handle" HOME="/zzqhome/$login"
shells=sh
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
closed() { # $1 = label, $2 = text the refusal must hold; stdin of the hook in $tmp/in
	_rc=0
	(cd "$tmp/w" && sh "$hook" origin x) < "$tmp/in" > "$tmp/out" 2>&1 || _rc=$?
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
printf 'refs/heads/zzqmain %s refs/heads/zzqmain %s\n' "$m2" "$m1" > "$tmp/in"
shim hostname ''
closed "host name unreadable" "cannot read this machine's host name"
shim hostname ' '
closed "host name empty" "cannot read this machine's host name"
shim hostname Zzq-Synthetic-Box
shim whoami ''
closed "user name unreadable" "cannot read the local user name"
shim whoami "$login"
# Words shorter than three characters, and HOME=/, are not compared.
shim hostname zz
shim whoami zz
case_ ok refs/heads/zzqs "$(mk "$m1" "zz zzq zz/")" refs/heads/zzqs "$zero"
run "short words" HOME=/ YQ2_IDENTITY_TEST_EXTRA_WORD=
shim hostname Zzq-Synthetic-Box
shim whoami "$login"

# The hook removes its temporary files, refused or not.
[ -z "$(ls -A "$tmp/hooktmp")" ] || note "the hook left temporary files behind"

[ "$fails" -eq 0 ] || exit 1
_ok=1
echo "check-pre-push-hook: ok"
