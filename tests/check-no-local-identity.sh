#!/bin/sh
# Invariant: no tracked text file carries the working machine's identity.
#
# The repository is public. The words looked for are read at run time and
# never written anywhere: the output of `hostname -s` (substring), the output
# of `whoami` (whole word or path component only, so a short common user name
# does not flag ordinary prose) and the path in $HOME (substring). All are
# fixed strings compared case-insensitively; a word shorter than three
# characters is skipped. Binary files and third_party/ are not scanned. A
# match is reported as file:line and the kind of value, never the text.
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
# in the tracked text files of the repository at <dir>; returns 1 if any.
scan_repo() {
	_hits=0
	for _spec in "host:-iF:$2" "user:-iwF:$3" "home:-iF:$4"; do
		_kind=${_spec%%:*}
		_rest=${_spec#*:}
		_mode=${_rest%%:*}
		_word=${_rest#*:}
		[ "${#_word}" -ge 3 ] || continue
		_out=$(git -C "$1" grep -nI "$_mode" -e "$_word" -- . ':(exclude)third_party' 2>/dev/null) || _st=$?
		case ${_st:-0} in
		0) ;;
		1) _st=0; continue ;;
		*) echo "check-no-local-identity: git grep failed"; return 2 ;;
		esac
		printf '%s\n' "$_out" | sed -E "s/^([^:]*:[0-9]+):.*/\\1 ($_kind)/"
		_hits=1
	done
	return "$_hits"
}

# ---- Self-test: a synthetic identity in a throwaway repository. ----------
tmp=$(mktemp -d "${TMPDIR:-/tmp}/noident.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
syn=zzq-synthetic-box
syn_user=zzquser
git init -q "$tmp/r"
g() { git -C "$tmp/r" -c user.name=t -c user.email=t@example.invalid -c commit.gpgsign=false "$@"; }
printf 'ordinary text\nthe zzquserless sentence\n' > "$tmp/r/clean.txt"
g add clean.txt
g commit -q -m seed
expect() { # $1 = want (0 pass, 1 fail), $2 = label; rest = scan args
	_want=$1; _label=$2; shift 2
	_got=0
	scan_repo "$tmp/r" "$@" > "$tmp/out" || _got=$?
	if [ "$_got" -ne "$_want" ]; then
		echo "check-no-local-identity: self-test '$_label' wanted status $_want, got $_got"
		exit 1
	fi
}
expect 0 "clean tree" "$syn" "$syn_user" /home/zzqhome
printf 'built on ZZQ-Synthetic-Box today\n' > "$tmp/r/leak.txt"
g add leak.txt
expect 1 "host name in a tracked file" "$syn" "$syn_user" /home/zzqhome
grep -q '^leak.txt:1 (host)$' "$tmp/out" || { echo "check-no-local-identity: self-test did not report file and line"; exit 1; }
if grep -qi "$syn" "$tmp/out"; then
	echo "check-no-local-identity: self-test output printed the matched value"
	exit 1
fi
g rm -q -f leak.txt
printf 'see /home/zzqhome/project/x\n' > "$tmp/r/leak.txt"
g add leak.txt
expect 1 "home path in a tracked file" "$syn" "$syn_user" /home/zzqhome
g rm -q -f leak.txt
printf 'path /srv/zzquser/data\n' > "$tmp/r/leak.txt"
g add leak.txt
expect 1 "user name as a path component" "$syn" "$syn_user" /home/zzqhome
g rm -q -f leak.txt
printf 'zzquserful is a different word\n' > "$tmp/r/leak.txt"
g add leak.txt
expect 0 "user name inside a longer word is not flagged" "$syn" "$syn_user" /home/zzqhome
g rm -q -f leak.txt
printf 'zz appears here\n' > "$tmp/r/leak.txt"
g add leak.txt
expect 0 "word shorter than three characters is skipped" zz zz zz
g rm -q -f leak.txt
printf 'binary \000 zzq-synthetic-box\n' > "$tmp/r/blob.bin"
g add blob.bin
expect 0 "binary files are skipped" "$syn" "$syn_user" /home/zzqhome
g rm -q -f blob.bin
mkdir "$tmp/r/third_party"
printf 'zzq-synthetic-box\n' > "$tmp/r/third_party/v.txt"
g add third_party/v.txt
expect 0 "third_party is skipped" "$syn" "$syn_user" /home/zzqhome

# ---- The real check on this tree. -----------------------------------------
host=$(hostname -s 2>/dev/null || true)
user=$(whoami 2>/dev/null || true)
home=${HOME:-}
[ "$home" != / ] || home=
if [ -z "$host" ] || [ -z "$user" ]; then
	echo "check-no-local-identity: warning: cannot read the host or user name; that word is not checked"
fi
if scan_repo . "$host" "$user" "$home" > "$tmp/real"; then
	echo "check-no-local-identity: ok"
else
	echo "check-no-local-identity: tracked files contain this machine's identity (values not shown); remove them:"
	sed 's/^/  /' "$tmp/real"
	exit 1
fi
