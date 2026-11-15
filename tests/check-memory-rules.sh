#!/bin/sh
# Invariant: the agent memory store is synced only through exact,
# pre-approved commands, and nothing wider is pre-approved.
#
# Agent memory lives in the store's one clone, ~/.config/agents/memory-store
# (the memory rule in CLAUDE.md; decision 0009). This check:
#
# 1. Extracts every store command (a code span naming memory-store that is
#    not one of the store paths the rule names) from the CLAUDE.md memory
#    rule and from .claude/hooks/session-context.sh, requires the two lists
#    to be equal and in the same order, and requires each command to be
#    allowed by an exact rule in .claude/settings.json (the rm command's
#    <file> stands for the one wildcard of its rule).
# 2. Fails on a cp allow rule; on any store rule outside the card's list
#    (TASK127, pinned below), reporting on its own a store rule with a
#    wildcard (`git -C <store> *` or `:*`), a second -C, a -c other than
#    core.editor=true on rebase --continue and a push with arguments; and on
#    any allow rule at all (Bash, bare Bash, Edit or Write) that admits one
#    of the probe commands, or an Edit or Write of the clone's .git or the
#    slug file.
# 3. Self-tests, in subshells with the GIT_* repository, configuration,
#    editor, ssh and identity variables unset or fixed, no global or system
#    configuration, hooks off (core.hooksPath=/dev/null), signing off and
#    HOME in a private mktemp -d: parts 1 and 2 reject a fixture for each
#    case they name, a parse error and a failing jq. Then a bare origin and
#    two clones stand in for two machines, each with its memory directory a
#    symbolic link into its clone, and the documented commands run in the
#    documented order with the clone for the store, as word lists (no shell
#    parses them). It shows that edits on both sides merge and an older copy
#    does not revert a newer one, that a deletion reaches the other side and
#    stays gone, that a conflict resolves with the documented conflict
#    steps, and that the origin only ever moves forward.
#
# Fails closed: a missing tool, a parse error, a memory rule not found, an
# unbalanced code span or a signal is a failure. Output names files and
# rule or command positions only, never a rule's or command's text, git's
# output, a remote or a local path; the store's name is never read.
#
# Usage: tests/check-memory-rules.sh
set -eu
CDPATH='' cd "$(dirname "$0")/.."
me=check-memory-rules
say() { echo "$me: $*" >&2; }
for t in jq git awk sed sort cmp mktemp; do
	command -v "$t" > /dev/null 2>&1 || { say "$t not found; cannot check the memory rules"; exit 1; }
done
tmp=$(mktemp -d "${TMPDIR:-/tmp}/memrules.XXXXXX")
chmod 700 "${tmp:?}"
tmp=$(CDPATH='' cd "${tmp:?}" && pwd)
trap 'rm -rf "${tmp:?}"' EXIT
trap 'exit 1' HUP INT TERM

# shellcheck disable=SC2088 # the rules and the documents spell ~ literally
store='~/.config/agents/memory-store'
pre="git -C $store"
rmrule="Bash($pre rm -q quake2/memory/*)"

# The card's list (TASK127): the only allow rules that name the store. The
# rm rule is the one with a wildcard: git rm removes only paths inside the
# clone's work tree, and an option after the subcommand can neither move it
# to another repository, set configuration nor push. Edit and Write stop at
# quake2/memory, so neither reaches the clone's .git (config, hooks), which
# the exact git commands would then run.
pinned()
{
	printf '%s\n' "Bash($pre status --porcelain)" "Bash($pre add -A quake2/memory)" \
		"Bash($pre commit -q -m quake2-memory-sync)" "Bash($pre pull --rebase -q)" \
		"Bash($pre push -q)" "Bash($pre -c core.editor=true rebase --continue)" "$rmrule" \
		"Read($store/**)" "Edit($store/quake2/memory/**)" "Write($store/quake2/memory/**)"
}

# What no allow rule may admit. Edit and Write rules are one kind here.
probes()
{
	printf '%s\n' "Bash($pre -C /x push -q)" "Bash($pre -c core.pager=x status --porcelain)" \
		"Bash($pre -c core.editor=x rebase --continue)" "Bash($pre push -q https://example.invalid/x)" \
		"Bash($pre push --force)" "Bash($pre commit -q -m x --no-verify)" \
		"Bash($pre config core.hooksPath x)" "Bash(cp x $store/quake2/memory/x)" \
		"Bash(cp -p ~/.claude/projects/x/memory/x.md $store/quake2/memory/)" \
		"Edit($store/.git/config)" "Edit($store/.git/hooks/pre-commit)" \
		"Edit(~/.config/agents/memory_repo)"
}

# allow_rules <settings>: permissions.allow, one rule per line, in $tmp/allow.
allow_rules()
{
	jq -r '.permissions.allow | if type == "array" then .[] else error("allow") end
		| if type == "string" and (test("[\n\r]") | not) then . else error("rule") end' \
		"$1" > "${tmp:?}/allow" 2> /dev/null ||
		{ say "cannot read permissions.allow (a parse error, a missing list, or an entry that is not a one-line string)"; return 1; }
}

# check_rules <settings>: part 2.
check_rules()
{
	allow_rules "$1" || return 1
	probes > "${tmp:?}/probes" || return 1
	i=0
	while IFS= read -r r; do
		i=$((i + 1))
		case "$r" in
		"Bash(cp)" | "Bash(cp "* | "Bash(cp:"*)
			say "allow rule $i is a cp rule; memory is never copied (see CLAUDE.md)"
			return 1 ;;
		"Bash($pre:"*)
			say "allow rule $i is a store rule with a wildcard (* or :*); only the rm rule has one"
			return 1 ;;
		"Bash($pre "*")")
			rest=${r#"Bash($pre "}
			rest=${rest%")"}
			if [ "$r" != "$rmrule" ]; then
				case "$rest" in *"*"*) say "allow rule $i is a store rule with a wildcard (* or :*); only the rm rule has one"; return 1 ;; esac
			fi
			case " $rest" in *" -C"*) say "allow rule $i lets a store command take a second -C"; return 1 ;; esac
			case " $rest" in *" -c"* | *" --config-env"*)
				[ "$rest" = "-c core.editor=true rebase --continue" ] ||
					{ say "allow rule $i lets a store command take a -c other than core.editor=true on rebase --continue"; return 1; } ;;
			esac
			case "$rest" in push | "push "*)
				[ "$rest" = "push -q" ] || { say "allow rule $i lets the store push with arguments"; return 1; } ;;
			esac ;;
		"Bash("*memory-store*)
			say "allow rule $i names the store but is not of the form git -C <store> <command>"
			return 1 ;;
		esac
		# A rule's wildcards, and anything a case pattern would treat
		# specially, become *, so the match errs towards admitting.
		case "$r" in
		*"("*")") k=${r%%"("*} b=${r#*"("} b=${b%")"} ;;
		*) k=$r b='*' ;;
		esac
		[ "$k" != Write ] || k=Edit
		pat=$(printf '%s' "$b" | sed -e 's/[][?\\*]/*/g' -e 's/:\*$/*/') ||
			{ say "sed failed"; return 1; }
		j=0
		while IFS= read -r p; do
			j=$((j + 1))
			[ "${p%%"("*}" = "$k" ] || continue
			pb=${p#*"("}
			case "${pb%")"}" in $pat) say "allow rule $i admits probe $j of this script, which no rule may"; return 1 ;; esac
		done < "${tmp:?}/probes"
	done < "${tmp:?}/allow"
	awk 'index($0, "memory-store")' "${tmp:?}/allow" > "${tmp:?}/named" &&
		LC_ALL=C sort "${tmp:?}/named" > "${tmp:?}/have" &&
		pinned | LC_ALL=C sort > "${tmp:?}/want" || { say "awk or sort failed"; return 1; }
	cmp -s "${tmp:?}/want" "${tmp:?}/have" ||
		{ say "the allow rules that name the store are not exactly the card's list (TASK127, pinned in this script)"; return 1; }
}

# spans <file> <rule|all>: the store commands in the code spans of <file>
# (rule: of the CLAUDE.md memory rule only); status 2 on an unbalanced
# span, 3 when the memory rule is not found.
spans()
{
	awk -v mode="$2" -v st="$store" '
	mode == "rule" {
		if (!on) { if ($0 !~ /^- Agent memory /) next; on = 1 }
		else if ($0 !~ /^  /) exit
	}
	{
		n = split($0, f, "`")
		if (n > 0 && n % 2 == 0) { bad = 1; exit }
		for (i = 2; i < n; i += 2)
			if (index(f[i], "memory-store") && f[i] != st && f[i] != st "/quake2/memory" &&
				f[i] != st "/quake2/memory/")
				print f[i]
	}
	END { if (bad) exit 2; if (mode == "rule" && !on) exit 3 }' "$1"
}

# check_docs <claude-md> <hook> <settings>: part 1; the commands stay in
# $tmp/doc.
check_docs()
{
	rc=0
	spans "$1" rule > "${tmp:?}/doc" || rc=$?
	case "$rc" in
	0) ;;
	3) say "cannot find the memory rule (the bullet that starts '- Agent memory ') in CLAUDE.md"; return 1 ;;
	*) say "cannot read the code spans of the CLAUDE.md memory rule (an unbalanced backtick?)"; return 1 ;;
	esac
	spans "$2" all > "${tmp:?}/hook" || { say "cannot read the code spans of session-context.sh"; return 1; }
	[ -s "${tmp:?}/doc" ] || { say "the CLAUDE.md memory rule names no store command"; return 1; }
	cmp -s "${tmp:?}/doc" "${tmp:?}/hook" ||
		{ say "session-context.sh does not spell out the store commands of the CLAUDE.md memory rule, the same and in the same order"; return 1; }
	allow_rules "$3" || return 1
	j=0
	while IFS= read -r c; do
		j=$((j + 1))
		! grep -Fxq -- "Bash($c)" "${tmp:?}/allow" || continue
		case "$c" in *"<file>") ! grep -Fxq -- "Bash(${c%"<file>"}*)" "${tmp:?}/allow" || continue ;; esac
		say "store command $j of the CLAUDE.md memory rule is not allowed by an exact rule in .claude/settings.json"
		return 1
	done < "${tmp:?}/doc"
}

isolate()
{
	unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR GIT_OBJECT_DIRECTORY \
		GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE GIT_PREFIX GIT_CONFIG \
		GIT_CONFIG_PARAMETERS GIT_DEFAULT_HASH GIT_TEMPLATE_DIR GIT_EDITOR \
		GIT_SEQUENCE_EDITOR GIT_SSH GIT_SSH_COMMAND GIT_ASKPASS SSH_ASKPASS \
		GIT_EXEC_PATH GIT_REFLOG_ACTION GIT_TRACE GIT_AUTHOR_DATE GIT_COMMITTER_DATE
	HOME=$tmp/home XDG_CONFIG_HOME=$tmp/home/.config GIT_CEILING_DIRECTORIES=$tmp
	GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 GIT_TERMINAL_PROMPT=0
	GIT_PAGER=cat PAGER=cat EDITOR=false VISUAL=false LC_ALL=C
	GIT_AUTHOR_NAME=selftest GIT_AUTHOR_EMAIL=selftest@example.invalid
	GIT_COMMITTER_NAME=selftest GIT_COMMITTER_EMAIL=selftest@example.invalid
	GIT_CONFIG_COUNT=3 GIT_CONFIG_KEY_0=core.hooksPath GIT_CONFIG_VALUE_0=/dev/null
	GIT_CONFIG_KEY_1=commit.gpgSign GIT_CONFIG_VALUE_1=false
	GIT_CONFIG_KEY_2=init.defaultBranch GIT_CONFIG_VALUE_2=main
	export HOME XDG_CONFIG_HOME GIT_CEILING_DIRECTORIES GIT_CONFIG_GLOBAL GIT_CONFIG_NOSYSTEM \
		GIT_TERMINAL_PROMPT GIT_PAGER PAGER EDITOR VISUAL LC_ALL GIT_AUTHOR_NAME \
		GIT_AUTHOR_EMAIL GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL GIT_CONFIG_COUNT \
		GIT_CONFIG_KEY_0 GIT_CONFIG_VALUE_0 GIT_CONFIG_KEY_1 GIT_CONFIG_VALUE_1 \
		GIT_CONFIG_KEY_2 GIT_CONFIG_VALUE_2
	mkdir -p "${tmp:?}/home" || exit 1
}

selftest_checks()
{
	isolate
	# expect <what> <message> <command>...: <command> fails, and with
	# <message>, so each case is caught by its own test.
	expect()
	{
		w=$1 msg=$2
		shift 2
		if out=$("$@" 2>&1); then say "selftest: $w was not detected"; exit 1; fi
		case "$out" in *"$msg"*) ;; *) say "selftest: $w was detected, but not by its own test"; exit 1 ;; esac
	}
	# reject <what> <message> <jq filter> [<rule>]: part 2 fails with
	# <message> on the tracked settings changed by <filter> ($r is <rule>).
	reject()
	{
		jq --arg r "${4:-}" "$3" .claude/settings.json > "${tmp:?}/s.json" 2> /dev/null ||
			{ say "selftest: cannot build the fixture for $1"; exit 1; }
		expect "$1" "$2" check_rules "${tmp:?}/s.json"
	}
	add='.permissions.allow += [$r]'
	wild="a store rule with a wildcard"
	reject "a cp rule" "is a cp rule" "$add" "Bash(cp -p a b)"
	reject "a git -C <store> * rule" "$wild" "$add" "Bash($pre *)"
	reject "a git -C <store>:* rule" "$wild" "$add" "Bash($pre:*)"
	reject "a widened rule" "$wild" '.permissions.allow |= map(if endswith("pull --rebase -q)") then .[:-1] + " *)" else . end)'
	reject "a second -C" "a second -C" "$add" "Bash($pre -C /x status --porcelain)"
	reject "a -c other than core.editor=true" "a -c other than" "$add" "Bash($pre -c core.pager=x status --porcelain)"
	reject "a push with arguments" "push with arguments" "$add" "Bash($pre push -q origin)"
	reject "a store rule of another form" "is not of the form" "$add" "Bash(env $pre push -q)"
	reject "an Edit rule on the whole store" "admits probe" "$add" "Edit($store/**)"
	reject "a Write rule on the whole store" "admits probe" "$add" "Write($store/**)"
	reject "a rule that admits the store without naming it" "admits probe" "$add" "Bash(git -C *)"
	reject "a bare Bash rule" "admits probe" "$add" "Bash"
	reject "an extra store rule" "not exactly the card's list" "$add" "Bash($pre fetch -q)"
	reject "a missing store rule" "not exactly the card's list" '.permissions.allow |= map(select(endswith(" push -q)") | not))'
	reject "an allow list that is not a list" "cannot read permissions.allow" '.permissions.allow = "x"'
	printf '{\n' > "${tmp:?}/s.json" || exit 1
	expect "a parse error" "cannot read permissions.allow" check_rules "${tmp:?}/s.json"
	mkdir -p "${tmp:?}/shim" || exit 1
	printf '#!/bin/sh\nexit 127\n' > "${tmp:?}/shim/jq" && chmod 755 "${tmp:?}/shim/jq" || exit 1
	shimmed() { PATH=$tmp/shim:$PATH; check_rules .claude/settings.json; }
	expect "a failing jq" "cannot read permissions.allow" shimmed
	# docmut <what> <message> <sed script> <both|hook>: part 1 fails with
	# <message> on copies of the documents, <sed script> applied to the hook
	# and, with both, to CLAUDE.md too.
	docmut()
	{
		if [ "$4" = both ]; then
			sed "$3" CLAUDE.md > "${tmp:?}/c.md" || exit 1
			! cmp -s CLAUDE.md "${tmp:?}/c.md" || { say "selftest: the fixture for $1 changed nothing"; exit 1; }
		else
			cp CLAUDE.md "${tmp:?}/c.md" || exit 1
		fi
		sed "$3" .claude/hooks/session-context.sh > "${tmp:?}/h.sh" || exit 1
		! cmp -s .claude/hooks/session-context.sh "${tmp:?}/h.sh" || { say "selftest: the fixture for $1 changed nothing"; exit 1; }
		expect "$1" "$2" check_docs "${tmp:?}/c.md" "${tmp:?}/h.sh" .claude/settings.json
	}
	docmut "a command spelt unlike its rule" "not allowed by an exact rule" 's/pull --rebase -q`/pull --rebase --quiet`/' both
	docmut "a hook that spells a command differently" "does not spell out" 's/push -q`/push --quiet`/' hook
	docmut "an unbalanced code span" "unbalanced backtick" 's/`git -C ~/git -C ~/' both
	docmut "a memory rule not found" "cannot find the memory rule" 's/^- Agent memory /- Memory /' both
}

# run <machine> <command> [<file>]: a documented store command in the clone
# of <machine>, as a word list (no shell parses it), <file> for <file>; its
# output in $tmp/out.
run()
{
	case "$2" in *[!-A-Za-z0-9\ ._/=~\<\>]*) say "selftest: a store command holds a character the runner refuses"; return 1 ;; esac
	m=$1 f=${3:-} old=$IFS
	IFS=' '
	set -f
	# shellcheck disable=SC2086 # split into words on purpose, globbing off
	set -- $2
	set +f
	IFS=$old
	[ $# -ge 4 ] && [ "$1" = git ] && [ "$2" = -C ] && [ "$3" = "$store" ] ||
		{ say "selftest: a store command is not git -C <store> <command>"; return 1; }
	shift 3
	case "$1" in
	status | add | commit | pull | push | rm) ;;
	-c) [ "$*" = "-c core.editor=true rebase --continue" ] || { say "selftest: a store command takes a -c the runner refuses"; return 1; } ;;
	*) say "selftest: a store command runs a git command the runner refuses"; return 1 ;;
	esac
	n=$#
	for a do
		case "$a" in *"<file>") a=${a%"<file>"}$f ;; esac
		set -- "$@" "$a"
	done
	shift "$n"
	git -C "${tmp:?}/$m/store" "$@" < /dev/null > "${tmp:?}/out" 2> "${tmp:?}/err"
}

# sync <machine>: the documented sync steps in order, the commit skipped when
# the status step printed nothing; status 2 when a step stops on a rebase
# conflict, 1 on any other failure.
sync()
{
	dirty=
	while IFS= read -r c; do
		case "${c#"$pre "}" in commit*) [ -n "$dirty" ] || continue ;; esac
		if ! run "$1" "$c"; then
			[ ! -d "${tmp:?}/$1/store/.git/rebase-merge" ] || return 2
			return 1
		fi
		case "${c#"$pre "}" in status*) [ ! -s "${tmp:?}/out" ] || dirty=1 ;; esac
	done < "${tmp:?}/seg-sync"
}

selftest_sync()
{
	isolate
	# The documented commands, split at the pushes: the sync steps, then the
	# conflict steps; the rm command apart.
	: > "${tmp:?}/seg-sync" && : > "${tmp:?}/seg-conflict" && : > "${tmp:?}/seg-rm" || exit 1
	awk -v pre="$pre" -v d="$tmp" '
	{ split(substr($0, length(pre) + 2), w, " "); s = (w[1] == "-c") ? w[3] : w[1] }
	index($0, "<file>") { print > (d "/seg-rm"); next }
	seg == 0 { print > (d "/seg-sync"); if (s == "push") seg = 1; next }
	seg == 1 { print > (d "/seg-conflict"); if (s == "push") seg = 2; next }
	{ extra = 1 }
	END { exit extra }' "${tmp:?}/doc" || { say "selftest: a store command follows the conflict steps"; exit 1; }
	[ -s "${tmp:?}/seg-sync" ] && [ -s "${tmp:?}/seg-conflict" ] && read -r rmc < "${tmp:?}/seg-rm" ||
		{ say "selftest: the memory rule lacks the sync steps, the conflict steps or the rm command"; exit 1; }
	# A bare origin and two clones (two machines), each memory directory a
	# symbolic link into its clone.
	x() { "$@" > /dev/null 2>&1 || { say "selftest: a setup step failed"; exit 1; }; }
	x git init -q --bare --template= "${tmp:?}/origin.git"
	x git init -q --template= "${tmp:?}/seed"
	x mkdir -p "${tmp:?}/seed/quake2/memory"
	printf '# index\n' > "${tmp:?}/seed/quake2/memory/MEMORY.md" || exit 1
	x git -C "${tmp:?}/seed" add -A
	x git -C "${tmp:?}/seed" commit -q -m seed
	x git -C "${tmp:?}/seed" push -q "${tmp:?}/origin.git" HEAD:refs/heads/main
	for m in A B; do
		x mkdir -p "${tmp:?}/$m"
		x git clone -q --template= "${tmp:?}/origin.git" "${tmp:?}/$m/store"
		x ln -s "${tmp:?}/$m/store/quake2/memory" "${tmp:?}/$m/memory"
	done
	tip=
	# forward: the origin's main moved only forward since the last call.
	forward()
	{
		new=$(git -C "${tmp:?}/origin.git" rev-parse -q --verify refs/heads/main) ||
			{ say "selftest: the origin has no main"; exit 1; }
		if [ -n "$tip" ] && ! git -C "${tmp:?}/origin.git" merge-base --is-ancestor "$tip" "$new"; then
			say "selftest: a step moved the origin back (it reverted the other side)"; exit 1
		fi
		tip=$new
	}
	step() { sync "$1" || { say "selftest: the documented sync on machine $1 failed ($2)"; exit 1; }; forward; }
	# put <machine> <file> <line>...: a memory written through the link.
	put() { m=$1 f=$2; shift 2; printf '%s\n' "$@" > "${tmp:?}/$m/memory/$f" || exit 1; }
	want() { [ "$(cat "${tmp:?}/$1/memory/$2" 2> /dev/null)" = "$3" ] || { say "selftest: $4"; exit 1; }; }
	gone() { [ ! -e "${tmp:?}/$1/memory/$2" ] || { say "selftest: $3"; exit 1; }; }

	put A a.md 'a v1'; step A "a new memory"
	step B "nothing to commit"
	put A a.md 'a v2'; step A "an edit"
	put B b.md 'b v1'; step B "an edit, beside an older copy of the other side's"
	step A "nothing to commit"
	for m in A B; do
		want "$m" a.md 'a v2' "machine $m lost the newer edit (an older copy reverted it)"
		want "$m" b.md 'b v1' "machine $m lacks the other side's edit"
	done

	run A "$rmc" a.md || { say "selftest: the documented rm command failed"; exit 1; }
	step A "a removal"; step B "a removal upstream"
	gone B a.md "a removal on machine A did not reach machine B"
	put B c.md 'c v1'; step B "an edit after the removal"; step A "nothing to commit"
	for m in A B; do
		gone "$m" a.md "a removed memory came back on machine $m"
		want "$m" c.md 'c v1' "machine $m lacks the edit made after the removal"
	done

	put A MEMORY.md '# index' '- from A'
	put B MEMORY.md '# index' '- from B'
	step A "an edit to the index"
	rc=0
	sync B || rc=$?
	[ "$rc" -eq 2 ] || { say "selftest: an edit of the same line on both sides did not stop the sync on a rebase conflict"; exit 1; }
	put B MEMORY.md '# index' '- from A' '- from B'
	while IFS= read -r c; do
		run B "$c" || { say "selftest: a documented conflict step failed"; exit 1; }
	done < "${tmp:?}/seg-conflict"
	forward
	step A "the resolution upstream"
	both=$(printf '%s\n' '# index' '- from A' '- from B')
	for m in A B; do
		want "$m" MEMORY.md "$both" "machine $m does not hold both sides of the resolved conflict"
		[ "$(git -C "${tmp:?}/$m/store" rev-parse HEAD 2> /dev/null)" = "$tip" ] &&
			[ -z "$(git -C "${tmp:?}/$m/store" status --porcelain 2> /dev/null)" ] ||
			{ say "selftest: machine $m did not end clean at the origin's main"; exit 1; }
	done
}

( selftest_checks ) || exit 1
check_docs CLAUDE.md .claude/hooks/session-context.sh .claude/settings.json || exit 1
check_rules .claude/settings.json || exit 1
( selftest_sync ) || exit 1
echo "$me: ok"
