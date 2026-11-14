#!/bin/sh
# Invariant: no tracked text file carries the working machine's identity.
#
# The repository is public. The words looked for are read at run time and
# never written anywhere: the output of `hostname -s` (substring), the output
# of `whoami` (whole word or path component only, so a short common user name
# does not flag ordinary prose) and the path in $HOME (substring). All are
# fixed strings compared case-insensitively; a word shorter than three
# characters is skipped. The content scanned is the staged content (the
# index blobs), not the working tree: regular files through `git grep
# --cached`, and the target of every tracked symbolic link (mode 120000)
# read from its blob with `git cat-file`. Binary files (a NUL byte in the
# index blob) are not scanned; the decision is made here, not by git, so a
# committed .gitattributes (`-diff`, `binary`) cannot take a text file out of
# the scan. A link target is reported as line 1 of the link.
# File contents are reported as file:line and the kind of value, file names
# only as a count; a file whose own name holds a value is reported as
# "<withheld file name>:line". Nothing printed carries the matched text. If
# the host name and the user name are both unreadable the check fails (the
# push hook also fails closed); if only one is, it warns.
#
# The real scan reads the index git hands it, so from a pre-commit hook it
# sees the blobs being committed, whatever the working tree holds. The self-test runs first, in a subshell
# with every GIT_* variable unset and no global or system configuration, on a
# throwaway repository under mktemp -d with synthetic names.
#
# The owner handle: when the user name equals, case-insensitively, the owner
# segment of the GitHub address of the remote "origin" (see owner_is; read
# from the repository's configuration, never from tracked content), it is a
# public handle and is in the tree by necessity (links, credits), so the
# user-name word test is skipped and a note says so (without the value). The
# host-name and home-path tests still run, and the user name is still
# withheld from any file name printed. The skip needs a home path that the
# home-path test can refuse (see home_is: its last component is the user
# name); without one, or if the owner cannot be determined, nothing is
# skipped. The push hook applies the same rule.
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
nl='
'

# ---- owner_is: the same text in tests/check-no-local-identity.sh and in
# .githooks/pre-push (tests/check-pre-push-hook.sh compares them). ----------
# owner_is <dir> <user>: true when <user> equals, ASCII case-insensitively,
# the owner segment of the GitHub address of the remote "origin" of the
# repository at <dir>. The address is the one git itself resolves (`git
# remote get-url`, so a url.<base>.insteadOf rewrite applies) from the
# repository's own configuration, which no commit or push can change. Only
# https://, ssh:// and git:// URLs and the scp-like form whose host is
# github.com and whose path is exactly <owner>/<repository> count. Anything
# else is undeterminable and false: no origin, more than one URL, a local path
# or file:// URL, another host (an ssh alias included), or a git error.
owner_is() {
	_ou=$(git -C "$1" remote get-url --all origin 2>/dev/null) || return 1
	case $_ou in
	*"$nl"*) return 1 ;;
	esac
	_oo=$(printf '%s\n' "$_ou" | tr 'A-Z' 'a-z' | sed -nE \
		-e 's#^(https|ssh|git)://([^/@]*@)?github\.com(:[0-9]+)?/([a-z0-9-]+)/[a-z0-9._-]+/?$#\4#p' \
		-e 's#^([^/@:]*@)?github\.com:([a-z0-9-]+)/[a-z0-9._-]+/?$#\2#p') || return 1
	[ -n "$_oo" ] && [ "$_oo" = "$(printf '%s' "$2" | tr 'A-Z' 'a-z')" ]
}
# home_is <home> <user>: true when the last path component of <home> equals
# <user>, ASCII case-insensitively, so that the home-path test refuses a path
# under this account's home directory. False for an empty <user> and for a
# <home> that is empty, `/`, ends in `/` or names another directory; the
# user-name word test is then not skipped (it is what still sees such a path).
home_is() {
	[ -n "$2" ] && [ "$(printf '%s' "${1##*/}" | tr 'A-Z' 'a-z')" = "$(printf '%s' "$2" | tr 'A-Z' 'a-z')" ]
}
# ---- end of owner_is. ------------------------------------------------------

# scan_repo <dir> <host> <user> <home>: prints "path:line (kind)" for each hit
# in the staged text files and link targets of the repository at <dir>, and
# "N tracked file name(s) (kind)" for file names; never the matched text. Returns 1 if any
# hit, 2 if <dir> is not a repository or a git or grep call fails (said on
# stderr). The user-name word test is skipped when owner_is <dir> <user> and
# home_is <home> <user>; the user name is then still withheld from the file
# names printed.
scan_repo() {
	_hits=0
	_skipu=0
	if owner_is "$1" "$3" && home_is "$4" "$3"; then _skipu=1; fi
	: > "$tmp/hits"
	: > "$tmp/words"
	# One listing gives the names and the symbolic links (mode 120000):
	# "<mode> <id> <stage><TAB><path>", each entry ended by NUL.
	git -C "$1" ls-files -s -z > "$tmp/ls.z" 2>/dev/null || { echo "$me: git ls-files failed" >&2; return 2; }
	tr '\000' '\n' < "$tmp/ls.z" > "$tmp/ls"
	sed 's/^[0-7]* [0-9a-f]* [0-3]	//' "$tmp/ls" > "$tmp/names"
	sed -n "s/^120000 \([0-9a-f]*\) [0-3]	/\1$us/p" "$tmp/ls" > "$tmp/links"
	for _spec in "host:-iF:$2" "user:-iwF:$3" "home:-iF:$4"; do
		_kind=${_spec%%:*}
		_rest=${_spec#*:}
		_mode=${_rest%%:*}
		_word=${_rest#*:}
		[ "${#_word}" -ge 3 ] || continue
		printf '%s\n' "$_word" >> "$tmp/words"
		if [ "$_kind" = user ] && [ "$_skipu" = 1 ]; then continue; fi
		# -z ends the path and the line number with NUL, so a colon in a path
		# cannot shift the fields; awk keeps the first two fields only. -a:
		# git's own binary test honours .gitattributes, so it is not used.
		_st=0
		git -C "$1" -c color.ui=false grep --cached --no-color -a -nz "$_mode" -e "$_word" -- . > "$tmp/grep" 2>/dev/null || _st=$?
		case $_st in
		0) tr '\000' '\037' < "$tmp/grep" | awk -F '\037' -v k="$_kind" '{ print $1 "\037" $2 "\037" k }' >> "$tmp/hits" ;;
		1) ;;
		*) echo "$me: git grep failed" >&2; return 2 ;;
		esac
		# git grep skips symbolic links; their target is the link's blob.
		while IFS=$us read -r _id _lp; do
			git -C "$1" cat-file blob "$_id" > "$tmp/link" 2>/dev/null || { echo "$me: git cat-file failed" >&2; return 2; }
			_st=0
			grep -q "$_mode" -e "$_word" < "$tmp/link" || _st=$?
			case $_st in
			0) printf '%s\037%s\037%s\n' "$_lp" 1 "$_kind" >> "$tmp/hits" ;;
			1) ;;
			*) echo "$me: grep failed" >&2; return 2 ;;
			esac
		done < "$tmp/links"
		# File names count too; only how many, never which.
		_st=0
		_n=$(grep -c "$_mode" -e "$_word" < "$tmp/names") || _st=$?
		case $_st in
		0) echo "$_n tracked file name(s) ($_kind)"; _hits=1 ;;
		1) ;;
		*) echo "$me: grep failed" >&2; return 2 ;;
		esac
	done
	# Contents: skip binary index blobs; withhold a path that itself holds a
	# value. ":0:" names the stage-0 entry, so no path is read as a stage.
	while IFS=$us read -r _p _l _k; do
		git -C "$1" cat-file blob ":0:$_p" > "$tmp/blob" 2>/dev/null || { echo "$me: git cat-file failed" >&2; return 2; }
		if [ "$(tr -cd '\000' < "$tmp/blob" | wc -c)" -gt 0 ]; then
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

	# Staged content counts, not the working tree; and link targets count.
	r=$tmp/r2
	git init -q "$r"
	printf 'clean\n' > "$r/clean.txt"
	ln -s ../zzquserful/x "$r/clean-link"
	git -C "$r" add -A
	expect 0 "a clean symbolic link" "$tmp/none" "$syn" zzquser /home/zzqhome
	ln -s "/srv/zzquser/$syn" "$r/leak-link"
	git -C "$r" add -A
	cat > "$tmp/want" <<-EOF
	leak-link:1 (host)
	leak-link:1 (user)
	EOF
	expect 1 "a symbolic link whose target holds a value" "$tmp/want" "$syn" zzquser /home/zzqhome
	git -C "$r" rm -q --cached leak-link
	rm "$r/leak-link"
	for _f in staged staged-nul staged-gone 1:staged; do
		printf 'built on %s\n' "$syn" > "$r/$_f.txt"
	done
	git -C "$r" add -A
	printf 'clean\n' > "$r/staged.txt"
	printf 'clean \000\n' > "$r/staged-nul.txt"
	rm "$r/staged-gone.txt"
	cat > "$tmp/want" <<-EOF
	staged.txt:1 (host)
	staged-nul.txt:1 (host)
	staged-gone.txt:1 (host)
	1:staged.txt:1 (host)
	EOF
	expect 1 "staged blobs, working files cleaned or removed" "$tmp/want" "$syn" zzquser /home/zzqhome
	r=$tmp/r3
	git init -q "$r"
	_id=$(printf 'zzq/target' | git -C "$r" hash-object -w --stdin)
	git -C "$r" update-index --add --cacheinfo "120000,$_id,dangling-link"
	rm -f "$r/.git/objects/$(printf '%s' "$_id" | cut -c1-2)/$(printf '%s' "$_id" | cut -c3-)"
	expect 2 "an unreadable link blob" "$tmp/none" "$syn" zzquser /home/zzqhome
	rm -f "$r/.git/index"
	printf 'built on %s\n' "$syn" > "$r/zzq
nl.txt"
	git -C "$r" add -A
	expect 2 "an index blob not found by its path" "$tmp/none" "$syn" zzquser /home/zzqhome

	# Which addresses name an owner. "y <url>": the owner is zzquser. sed keeps
	# the unmatched text, so "zzq" before an owner "user" would spell zzquser
	# if a pattern lost its leading anchor, and an owner "zzq" before a
	# leftover "/user" if it lost its trailing one. A userinfo part holds no
	# "/" (nor ":" in the scp-like form), so the host cannot be skipped over.
	r=$tmp/r5
	git init -q "$r"
	while read -r _want _url; do
		git -C "$r" config remote.origin.url "$_url"
		_got=n
		if owner_is "$r" zzquser; then _got=y; fi
		[ "$_got" = "$_want" ] || { echo "$me: self-test 'owner of an address' wanted $_want, got $_got for $_url"; exit 1; }
	done <<-EOF
	y https://github.com/zzquser/zzqrepo
	y https://github.com/zzquser/zzqrepo.git
	y HTTPS://GitHub.COM/ZZQUSER/zzqrepo.git/
	y https://zzqtoken@github.com/zzquser/zzqrepo.git
	y ssh://zzqgit@github.com/zzquser/zzqrepo.git
	y ssh://zzqgit@github.com:22/zzquser/zzqrepo.git
	y git://github.com/zzquser/zzqrepo.git
	y zzqgit@github.com:zzquser/zzqrepo.git
	y github.com:zzquser/zzq.repo
	n https://github.com/zzqother/zzqrepo.git
	n https://github.com/zzquserx/zzqrepo.git
	n https://github.com/xzzquser/zzqrepo.git
	n https://zzquser@github.com/zzqother/zzqrepo.git
	n https://github.com/zzqother/zzquser.git
	n https://github.com/zzquser
	n https://github.com/zzquser/zzqrepo/zzqmore
	n https://github.com//zzquser/zzqrepo
	n http://github.com/zzquser/zzqrepo.git
	n https://zzq.example/zzquser/zzqrepo.git
	n https://github.com.zzq.example/zzquser/zzqrepo.git
	n https://zzqgithub.com/zzquser/zzqrepo.git
	n https://github.com/zzqo/zzqrepo#github.com/zzquser/zzqrepo
	n zzqalias:zzquser/zzqrepo.git
	n /home/zzquser/zzqrepo
	n file:///home/zzquser/zzqrepo
	n ../zzquser/zzqrepo
	n zzquser/zzqrepo
	n zzqhttps://github.com/user/zzqrepo
	n zzqext::https://github.com/zzquser/zzqrepo.git
	n zzqgithub.com:user/zzqrepo
	n /zzq/github.com:zzquser/zzqrepo
	n https://githubxcom/zzquser/zzqrepo
	n zzqgit@githubxcom:zzquser/zzqrepo.git
	n https://github.com/zzq/zzqrepo/user
	n github.com:zzq/zzqrepo/user
	n https://zzq.example/@github.com/zzquser/zzqrepo
	n zzqalias:x@github.com:zzquser/zzqrepo
	EOF
	# Two URLs, the owner's first: `get-url` without --all would give only it.
	git -C "$r" config remote.origin.url https://github.com/zzquser/zzqrepo.git
	git -C "$r" config --add remote.origin.url zzqalias:zzquser/zzqrepo.git
	if owner_is "$r" zzquser; then echo "$me: self-test 'origin with two URLs' named an owner"; exit 1; fi
	git -C "$r" config --unset-all remote.origin.url
	git -C "$r" config remote.zzqup.url https://github.com/zzquser/zzqrepo.git
	if owner_is "$r" zzquser; then echo "$me: self-test 'no origin, another remote' named an owner"; exit 1; fi
	git -C "$r" config remote.origin.url zzqgh:zzquser/zzqrepo
	git -C "$r" config url.https://github.com/.insteadOf zzqgh:
	owner_is "$r" zzquser || { echo "$me: self-test 'an insteadOf rewrite' named no owner"; exit 1; }
	owner_is "$r" ZZQUSER || { echo "$me: self-test 'the user name in other case' named no owner"; exit 1; }
	if owner_is "$r" ''; then echo "$me: self-test 'an empty user name' matched"; exit 1; fi
	git -C "$r" config remote.origin.url zzqalias:zzquser/zzqrepo
	if owner_is "$r" ''; then echo "$me: self-test 'an empty user name, no owner' matched"; exit 1; fi
	if owner_is "$tmp/plain-none" zzquser; then echo "$me: self-test 'not a repository' named an owner"; exit 1; fi

	# The user name that is the owner handle of origin's GitHub address is
	# public: only the user-name word test is skipped.
	r=$tmp/r4
	git init -q "$r"
	printf 'see github.com/zzquser/zzqrepo, by zzquser\n' > "$r/credits.txt"
	git -C "$r" add -A
	printf 'credits.txt:1 (user)\n' > "$tmp/want"
	expect 1 "no origin: the user name is checked" "$tmp/want" "$syn" zzquser /home/zzquser
	git -C "$r" remote add origin https://github.com/ZzqUser/zzqrepo.git
	expect 0 "the user name is the owner handle: skipped" "$tmp/none" "$syn" zzquser /home/zzquser
	# Skipped only when the last component of the home is the user name.
	expect 1 "the owner handle, home unset: checked" "$tmp/want" "$syn" zzquser ''
	expect 1 "the owner handle, home elsewhere: checked" "$tmp/want" "$syn" zzquser /home/zzqhome
	expect 1 "the owner handle, home is /: checked" "$tmp/want" "$syn" zzquser /
	expect 1 "the owner handle, home ends in /: checked" "$tmp/want" "$syn" zzquser /home/zzquser/
	expect 0 "the owner handle, home in other case: skipped" "$tmp/none" "$syn" zzquser /home/ZzqUser
	git -C "$r" remote set-url origin https://github.com/zzqother/zzqrepo.git
	expect 1 "the owner handle is another name: checked" "$tmp/want" "$syn" zzquser /home/zzquser
	git -C "$r" remote set-url origin zzqalias:zzquser/zzqrepo.git
	expect 1 "the owner is undeterminable: checked" "$tmp/want" "$syn" zzquser /home/zzquser
	git -C "$r" remote set-url origin https://github.com/zzquser/zzqrepo.git
	printf 'built in /home/zzquser/src\n' > "$r/zzquser-notes.txt"
	printf 'built on %s\n' "$syn" > "$r/host.txt"
	git -C "$r" add -A
	cat > "$tmp/want" <<-EOF
	<withheld file name>:1 (home)
	host.txt:1 (host)
	EOF
	expect 1 "skipped: the home path and the host name are still refused" "$tmp/want" "$syn" zzquser /home/zzquser
	r=$tmp/r

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
if [ "${#user}" -ge 3 ] && owner_is . "$user" && home_is "$home" "$user"; then
	echo "$me: note: the user name is the owner handle of this repository's GitHub address (remote origin), which is public, and the last component of the home path; the user-name word test is skipped, the host name and home path are still checked"
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
