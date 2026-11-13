#!/bin/sh
# Invariant: no tracked text file carries the working machine's identity.
#
# The repository is public. The words looked for are read at run time and
# never written anywhere: the output of `hostname -s` (substring), the output
# of `whoami` (whole word or path component only, so a short common user name
# does not flag ordinary prose) and the path in $HOME (substring). All are
# fixed strings compared case-insensitively; a word shorter than three
# characters is skipped. Binary files are not scanned. File contents are
# reported as file:line and the kind of value, file names only as a count,
# never the text. If the host name and the user name are both unreadable the
# check fails (the push hook also fails closed); if only one is, it warns.
#
# Limit: a machine whose host or user name is an ordinary word of this
# repository's text (for example a generic container name) cannot pass; give
# the machine another name (see docs/decisions/0003-no-local-identity.md).
#
# The scan is a function of explicit words, so its self-test (a throwaway
# repository under mktemp -d) uses a synthetic name and needs no environment
# override. The push-time twin of this check is .githooks/pre-push; its
# self-test is tests/check-pre-push-hook.sh.
#
# Usage: tests/check-no-local-identity.sh
set -eu
cd "$(dirname "$0")/.."
export LC_ALL=C

# scan_repo <dir> <host> <user> <home>: prints "path:line (kind)" for each hit
# in the tracked text files of the repository at <dir>, and "N tracked file
# name(s) (kind)" for file names; never the matched text. Returns 1 if any hit,
# 2 if <dir> is not a repository or the search fails (said on stderr).
scan_repo() {
	_hits=0
	git -C "$1" rev-parse --git-dir > /dev/null 2>&1 || {
		echo "check-no-local-identity: $1 is not a git repository, nothing to scan" >&2
		return 2
	}
	for _spec in "host:-iF:$2" "user:-iwF:$3" "home:-iF:$4"; do
		_kind=${_spec%%:*}
		_rest=${_spec#*:}
		_mode=${_rest%%:*}
		_word=${_rest#*:}
		[ "${#_word}" -ge 3 ] || continue
		# -z ends the path and the line number with NUL, so a colon in a path
		# cannot shift the fields; awk prints the first two fields only and
		# so never any of the matched text.
		_st=0
		# (A file, not $(...): command substitution drops the NULs.)
		_f=$(mktemp "${TMPDIR:-/tmp}/noident-grep.XXXXXX")
		git -C "$1" -c color.ui=false grep --no-color -nIz "$_mode" -e "$_word" -- . > "$_f" 2>/dev/null || _st=$?
		case $_st in
		0)
			tr '\0' '\001' < "$_f" | awk -F '\001' -v k="$_kind" '{ print $1 ":" $2 " (" k ")" }'
			_hits=1
			;;
		1) ;;
		*) rm -f "$_f"; echo "check-no-local-identity: git grep failed" >&2; return 2 ;;
		esac
		rm -f "$_f"
		# File names count too; only how many, never which.
		_n=$(git -C "$1" ls-files | grep -c "$_mode" -e "$_word" || true)
		case $_n in
		0 | '') ;;
		*) echo "$_n tracked file name(s) ($_kind)"; _hits=1 ;;
		esac
	done
	return "$_hits"
}

# ---- Self-test: a synthetic identity in a throwaway repository. ----------
# Isolated from the invoking user's git configuration (hooks path, colour,
# signing, templates).
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/noident.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
syn=zzq-synthetic-box
syn_user=zzquser
git init -q "$tmp/r"
g() { git -C "$tmp/r" -c user.name=zzqa -c user.email=zzqa@zzqmail.zzq -c commit.gpgsign=false "$@"; }
printf 'ordinary text\nthe zzquserless sentence\n' > "$tmp/r/clean.txt"
g add clean.txt
g commit -q -m zzqseed
expect() { # $1 = want (0 pass, 1 hits), $2 = label; rest = scan args
	_want=$1; _label=$2; shift 2
	_got=0
	scan_repo "$tmp/r" "$@" > "$tmp/out" 2> "$tmp/err" || _got=$?
	if [ "$_got" -ne "$_want" ]; then
		echo "check-no-local-identity: self-test '$_label' wanted status $_want, got $_got"
		exit 1
	fi
}
expect 0 "clean tree" "$syn" "$syn_user" /home/zzqhome
printf 'built on ZZQ-Synthetic-Box today\n' > "$tmp/r/leak.txt"
g add leak.txt
expect 1 "host name in a tracked file" "$syn" "$syn_user" /home/zzqhome
[ "$(cat "$tmp/out")" = "leak.txt:1 (host)" ] || { echo "check-no-local-identity: self-test did not report exactly file and line"; exit 1; }
g rm -q -f leak.txt
printf 'built in /home/zzqhome/src\n' > "$tmp/r/notes:v2.txt"
g add notes:v2.txt
expect 1 "home path in a file whose name has a colon" "$syn" "$syn_user" /home/zzqhome
[ "$(cat "$tmp/out")" = "notes:v2.txt:1 (home)" ] || { echo "check-no-local-identity: self-test mis-reported a path with a colon"; exit 1; }
g rm -q -f notes:v2.txt
printf 'path /srv/zzquser/data\n' > "$tmp/r/leak.txt"
g add leak.txt
expect 1 "user name as a path component" "$syn" "$syn_user" /home/zzqhome
g rm -q -f leak.txt
printf 'zzquserful is a different word\n' > "$tmp/r/leak.txt"
g add leak.txt
expect 0 "user name inside a longer word is not flagged" "$syn" "$syn_user" /home/zzqhome
g rm -q -f leak.txt
printf 'clean\n' > "$tmp/r/zzq-synthetic-box.txt"
g add zzq-synthetic-box.txt
expect 1 "host name in a file name" "$syn" "$syn_user" /home/zzqhome
[ "$(cat "$tmp/out")" = "1 tracked file name(s) (host)" ] || { echo "check-no-local-identity: self-test mis-reported a file name"; exit 1; }
g rm -q -f zzq-synthetic-box.txt
printf 'zz appears here\n' > "$tmp/r/leak.txt"
g add leak.txt
expect 0 "word shorter than three characters is skipped" zz zz zz
g rm -q -f leak.txt
printf 'binary \000 zzq-synthetic-box\n' > "$tmp/r/blob.bin"
g add blob.bin
expect 0 "binary files are skipped" "$syn" "$syn_user" /home/zzqhome
g rm -q -f blob.bin
# A directory that is not a repository is an error (2), not a pass.
mkdir "$tmp/plain"
_got=0
GIT_CEILING_DIRECTORIES=$tmp scan_repo "$tmp/plain" "$syn" "$syn_user" /home/zzqhome > "$tmp/out" 2> "$tmp/err" || _got=$?
[ "$_got" -eq 2 ] || { echo "check-no-local-identity: self-test 'not a repository' wanted status 2, got $_got"; exit 1; }
unset GIT_CONFIG_GLOBAL GIT_CONFIG_NOSYSTEM

# ---- The real check on this tree. -----------------------------------------
host=$(hostname -s 2>/dev/null || true)
user=$(whoami 2>/dev/null || true)
home=${HOME:-}
[ "$home" != / ] || home=
if [ -z "$host" ] && [ -z "$user" ]; then
	echo "check-no-local-identity: cannot read the host name or the user name; nothing to check against"
	exit 1
fi
if [ -z "$host" ] || [ -z "$user" ]; then
	echo "check-no-local-identity: warning: cannot read the host or user name; that word is not checked"
fi
real_st=0
scan_repo . "$host" "$user" "$home" > "$tmp/real" 2> "$tmp/realerr" || real_st=$?
case $real_st in
0) echo "check-no-local-identity: ok" ;;
1)
	echo "check-no-local-identity: tracked files contain this machine's identity (values not shown); remove them:"
	sed 's/^/  /' "$tmp/real"
	exit 1
	;;
*)
	echo "check-no-local-identity: the scan could not run:"
	sed 's/^/  /' "$tmp/realerr"
	exit 1
	;;
esac
