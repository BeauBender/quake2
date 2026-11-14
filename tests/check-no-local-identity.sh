#!/bin/sh
# Invariant: no tracked text file carries the working machine's identity.
#
# The repository is public. The words looked for are read at run time and
# never written anywhere: the output of `hostname -s` (substring), the output
# of `whoami` (whole word or path component only, so a short common user name
# does not flag ordinary prose) and the path in $HOME (substring). All are
# fixed strings compared case-insensitively; a word shorter than three
# characters is skipped. Binary files (a NUL byte in the working-tree file)
# are not scanned; the decision is made here, not by git, so a committed
# .gitattributes (`-diff`, `binary`) cannot take a text file out of the scan.
# File contents are reported as file:line and the kind of value, file names
# only as a count; a file whose own name holds a value is reported as
# "<withheld file name>:line". Nothing printed carries the matched text. If
# the host name and the user name are both unreadable the check fails (the
# push hook also fails closed); if only one is, it warns.
#
# The real scan reads the index git hands it, so from a pre-commit hook it
# sees the files being committed. The self-test runs first, in a subshell
# with every GIT_* variable unset and no global or system configuration, on a
# throwaway repository under mktemp -d with synthetic names.
#
# Limit: a machine whose host or user name is an ordinary word of this
# repository's text (for example a generic container or CI name) cannot
# pass; give the machine another name (see
# docs/decisions/0005-push-identity-hook.md).
#
# The push-time twin of this check is .githooks/pre-push; its self-test is
# tests/check-pre-push-hook.sh.
#
# Usage: tests/check-no-local-identity.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C
me=check-no-local-identity
tmp=$(mktemp -d "${TMPDIR:-/tmp}/noident.XXXXXX")
# A shell error must not exit 0 (bash 3.2 loses the status in an EXIT trap).
trap 'rm -rf "$tmp"; [ "${_ok:-}" = 1 ] || exit 1' EXIT
us=$(printf '\037')

# scan_repo <dir> <host> <user> <home>: prints "path:line (kind)" for each hit
# in the tracked text files of the repository at <dir>, and "N tracked file
# name(s) (kind)" for file names; never the matched text. Returns 1 if any
# hit, 2 if <dir> is not a repository or a git or grep call fails (said on
# stderr).
scan_repo() {
	_hits=0
	: > "$tmp/hits"
	: > "$tmp/words"
	git -C "$1" ls-files -z > "$tmp/names.z" 2>/dev/null || { echo "$me: git ls-files failed" >&2; return 2; }
	tr '\000' '\n' < "$tmp/names.z" > "$tmp/names"
	for _spec in "host:-iF:$2" "user:-iwF:$3" "home:-iF:$4"; do
		_kind=${_spec%%:*}
		_rest=${_spec#*:}
		_mode=${_rest%%:*}
		_word=${_rest#*:}
		[ "${#_word}" -ge 3 ] || continue
		printf '%s\n' "$_word" >> "$tmp/words"
		# -z ends the path and the line number with NUL, so a colon in a path
		# cannot shift the fields; awk keeps the first two fields only. -a:
		# git's own binary test honours .gitattributes, so it is not used.
		_st=0
		git -C "$1" -c color.ui=false grep --no-color -a -nz "$_mode" -e "$_word" -- . > "$tmp/grep" 2>/dev/null || _st=$?
		case $_st in
		0) tr '\000' '\037' < "$tmp/grep" | awk -F '\037' -v k="$_kind" '{ print $1 "\037" $2 "\037" k }' >> "$tmp/hits" ;;
		1) ;;
		*) echo "$me: git grep failed" >&2; return 2 ;;
		esac
		# File names count too; only how many, never which.
		_st=0
		_n=$(grep -c "$_mode" -e "$_word" < "$tmp/names") || _st=$?
		case $_st in
		0) echo "$_n tracked file name(s) ($_kind)"; _hits=1 ;;
		1) ;;
		*) echo "$me: grep failed" >&2; return 2 ;;
		esac
	done
	# Contents: skip binary files; withhold a path that itself holds a value.
	while IFS=$us read -r _p _l _k; do
		if [ -f "$1/$_p" ] && [ "$(tr -cd '\000' < "$1/$_p" | wc -c)" -gt 0 ]; then
			continue
		fi
		_shown=$_p
		_st=0
		printf '%s\n' "$_p" | grep -qiF -f "$tmp/words" || _st=$?
		case $_st in
		0) _shown='<withheld file name>' ;;
		1) ;;
		*) echo "$me: grep failed" >&2; return 2 ;;
		esac
		echo "$_shown:$_l ($_k)"
		_hits=1
	done < "$tmp/hits"
	return "$_hits"
}

# ---- Self-test: a synthetic identity in a throwaway repository. ----------
(
	for _v in $(env | sed -n 's/^\(GIT_[A-Za-z0-9_]*\)=.*/\1/p'); do
		unset "$_v"
	done
	mkdir "$tmp/tpl"
	export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 GIT_TEMPLATE_DIR="$tmp/tpl" XDG_CONFIG_HOME="$tmp/xdg"
	r=$tmp/r
	syn=zzq-synthetic-box
	git init -q "$r"
	expect() { # $1 = want status, $2 = label, $3 = file of wanted output; rest = scan args
		_want=$1; _label=$2; _wantf=$3; shift 3
		_got=0
		scan_repo "$r" "$@" > "$tmp/out" 2> "$tmp/err" || _got=$?
		[ "$_got" -eq "$_want" ] || { echo "$me: self-test '$_label' wanted status $_want, got $_got"; exit 1; }
		sort "$tmp/out" > "$tmp/out.s"
		sort "$_wantf" | cmp -s - "$tmp/out.s" || { echo "$me: self-test '$_label' printed other lines than wanted"; exit 1; }
	}
	: > "$tmp/none"
	printf 'ordinary text\nthe zzquserless sentence\n' > "$r/clean.txt"
	git -C "$r" add clean.txt
	expect 0 "clean tree" "$tmp/none" "$syn" zzquser /home/zzqhome

	printf 'built on ZZQ-Synthetic-Box today\n' > "$r/leak.txt"
	printf 'line one\nbuilt in /home/zzqhome/src\n' > "$r/notes:v2.txt"
	printf 'path /srv/zzquser/data\n' > "$r/comp.txt"
	printf 'zzquserful and zzquser_x and xzzquser\n' > "$r/longer.txt"
	printf 'inzzq-synthetic-box2 and x/home/zzqhomey\n' > "$r/sub.txt"
	printf 'clean\n' > "$r/$syn.txt"
	mkdir "$r/$syn"
	printf 'built on %s\n' "$syn" > "$r/$syn/notes.txt"
	printf 'clean\n' > "$r/$(printf '\303\251')zzquser.txt"
	printf 'binary \000 %s\n' "$syn" > "$r/blob.bin"
	printf 'attr.txt -diff\nattr2.txt binary\n' > "$r/.gitattributes"
	printf 'on %s\n' "$syn" > "$r/attr.txt"
	printf 'on %s\n' "$syn" > "$r/attr2.txt"
	git -C "$r" add -A
	cat > "$tmp/want" <<-EOF
	leak.txt:1 (host)
	notes:v2.txt:2 (home)
	comp.txt:1 (user)
	sub.txt:1 (host)
	sub.txt:1 (home)
	<withheld file name>:1 (host)
	attr.txt:1 (host)
	attr2.txt:1 (host)
	2 tracked file name(s) (host)
	1 tracked file name(s) (user)
	EOF
	expect 1 "every kind of hit, nothing else" "$tmp/want" "$syn" zzquser /home/zzqhome
	cat > "$tmp/want" <<-EOF
	notes:v2.txt:2 (home)
	sub.txt:1 (home)
	EOF
	expect 1 "content hits alone" "$tmp/want" zzqnone zzqnone /home/zzqhome
	expect 0 "words shorter than three characters are skipped" "$tmp/none" zz zz zz
	expect 0 "empty words are skipped" "$tmp/none" '' '' ''

	mkdir "$tmp/plain"
	_got=0
	GIT_CEILING_DIRECTORIES=$tmp scan_repo "$tmp/plain" "$syn" zzquser /home/zzqhome > "$tmp/out" 2> "$tmp/err" || _got=$?
	[ "$_got" -eq 2 ] || { echo "$me: self-test 'not a repository' wanted status 2, got $_got"; exit 1; }
	printf 'zzq garbage\n' > "$r/.git/index"
	_got=0
	scan_repo "$r" "$syn" zzquser /home/zzqhome > "$tmp/out" 2> "$tmp/err" || _got=$?
	[ "$_got" -eq 2 ] || { echo "$me: self-test 'git fails' wanted status 2, got $_got"; exit 1; }
)

# ---- The real check on this tree. -----------------------------------------
host=$(hostname -s 2>/dev/null || true)
user=$(whoami 2>/dev/null || true)
home=${HOME:-}
if [ -z "$host" ] && [ -z "$user" ]; then
	echo "$me: cannot read the host name or the user name; nothing to check against"
	exit 1
fi
if [ -z "$host" ] || [ -z "$user" ]; then
	echo "$me: warning: cannot read the host or user name; that word is not checked"
fi
real_st=0
scan_repo . "$host" "$user" "$home" > "$tmp/real" 2> "$tmp/realerr" || real_st=$?
case $real_st in
0) _ok=1; echo "$me: ok" ;;
1)
	echo "$me: tracked files contain this machine's identity (values not shown); remove them:"
	sed 's/^/  /' "$tmp/real"
	exit 1
	;;
*)
	echo "$me: the scan could not run:"
	sed 's/^/  /' "$tmp/realerr"
	exit 1
	;;
esac
