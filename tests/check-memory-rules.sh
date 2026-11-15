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
#    allowed by an exact rule in .claude/settings.json. Both documents must
#    also name, each as a code span, every unmerged code of git status (UU,
#    AA, DD, AU, UA, DU, UD) and the line the second check prints while a
#    rebase is stopped.
# 2. Fails on a cp allow rule (its first word cp, or a path ending /cp); on
#    a store rm rule (a memory is removed only with the owner present); on a
#    `git -C <store>` rule with a wildcard (* or :*), a second -C, a -c other
#    than core.editor=true on rebase --continue or a push with arguments; on
#    any allow rule at all (Bash, bare Bash, Edit or Write) that admits one
#    of the probe commands, or an Edit or Write of the clone's .git or the
#    slug file; on any store rule outside the card's list (TASK127, pinned
#    below); and on that list not being there exactly once. A store rule is
#    any allow rule whose body names .config/agents (in any case), any Bash
#    rule that runs git with -C, --git-dir or --work-tree before its
#    subcommand, and any Read, Edit or Write rule that is bare or under a
#    home or absolute path (~, $, % or /).
# 3. Self-tests, in subshells with the GIT_* repository, configuration,
#    editor, ssh and identity variables unset or fixed, no global or system
#    configuration, hooks off (core.hooksPath=/dev/null), signing off and
#    HOME in a private mktemp -d. A bare origin and two clones stand in for
#    two machines, each with its memory directory a symbolic link into its
#    clone, and the documented commands run in the documented order with
#    the clone for the store, as word lists (no shell parses them). It shows
#    that edits on both sides merge and an older copy does not revert a
#    newer one; that a removal made with the owner present reaches the other
#    side and stays gone; that the first check and the commit leave another
#    project's staged change in a shared clone alone; that after an
#    interrupted conflict (an edit of the same line, its resolution staged;
#    an add/add; a modify/delete; a merge left stopped, with no rebase) the
#    next sync runs neither add nor commit and git's unmerged code is one
#    the memory rule names; that the documented conflict steps resolve each
#    stopped rebase with no conflict marker reaching the origin;
#    and that the origin only ever moves forward. Then parts 1 and 2 reject
#    a fixture for each case they name (settings holding the card's list
#    plus one change; copies of the documents with one change), a parse
#    error and a failing jq or tr, each with the message of its own test.
#
# The real checks run first, so a fault in a tracked file is reported as
# itself rather than as a self-test failure.
#
# Fails closed: a missing tool, a parse error, a memory rule not found, an
# unbalanced code span or a signal is a failure. Output names files, rule
# or command positions and git's unmerged codes only, never a rule's or
# command's text, git's output, a remote or a local path; the store's name
# is never read.
#
# Usage: tests/check-memory-rules.sh
set -eu
CDPATH='' cd "$(dirname "$0")/.."
me=check-memory-rules
say() { echo "$me: $*" >&2; }
for t in jq git awk sed sort cmp grep tr mktemp; do
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
detached='# branch.head (detached)'

# The card's list (TASK127): the only store rules. No rule removes a file: a
# memory is removed only with the owner present. The two checks and the
# commit are limited to quake2/memory, so another project's changes in a
# shared clone are neither shown nor committed; the second check shows a
# stopped rebase whose conflicts are staged, which the first cannot. Edit
# and Write stop at quake2/memory, so neither reaches the clone's .git
# (config, hooks), which the exact git commands would then run.
pinned()
{
	printf '%s\n' "Bash($pre status --porcelain quake2/memory)" \
		"Bash($pre status --porcelain=v2 --branch quake2/memory)" "Bash($pre add -A quake2/memory)" \
		"Bash($pre commit -q -m quake2-memory-sync quake2/memory)" "Bash($pre pull --rebase -q)" \
		"Bash($pre push -q)" "Bash($pre -c core.editor=true rebase --continue)" \
		"Read($store/**)" "Edit($store/quake2/memory/**)" "Write($store/quake2/memory/**)"
}

# What no allow rule may admit. Edit and Write rules are one kind here.
probes()
{
	printf '%s\n' "Bash($pre -C /x push -q)" "Bash($pre -c core.pager=x status --porcelain)" \
		"Bash($pre -c core.editor=x rebase --continue)" "Bash($pre push -q https://example.invalid/x)" \
		"Bash($pre push --force)" "Bash($pre commit -q -m x --no-verify)" \
		"Bash($pre config core.hooksPath x)" "Bash($pre rm -q quake2/memory/x)" \
		"Bash(cp x $store/quake2/memory/x)" "Edit($store/.git/config)" \
		"Edit($store/.git/hooks/pre-commit)" "Edit(~/.config/agents/memory_repo)"
}

# allow_rules <settings>: permissions.allow, one rule per line, in $tmp/allow.
allow_rules()
{
	jq -r '.permissions.allow | if type == "array" then .[] else error("allow") end
		| if type == "string" and (test("[\n\r]") | not) then . else error("rule") end' \
		"$1" > "${tmp:?}/allow" 2> /dev/null ||
		{ say "cannot read permissions.allow (a parse error, a missing list, or an entry that is not a one-line string)"; return 1; }
}

# store_rule <tool> <body> <bare>: status 0 when the rule is a store rule
# (see part 2 above), 1 when it is not, 2 when tr fails.
store_rule()
{
	lc=$(printf '%s' "$2" | tr '[:upper:]' '[:lower:]') || return 2
	case "$lc" in *.config/agents*) return 0 ;; esac
	case "$1" in
	Bash)
		g='' skip=''
		set -f
		# shellcheck disable=SC2086 # split into words on purpose, globbing off
		set -- $2
		set +f
		for sw do
			if [ -n "$skip" ]; then skip=''; continue; fi
			if [ -z "$g" ]; then
				case "$sw" in git | */git) g=1 ;; esac
				continue
			fi
			case "$sw" in
			-C* | --git-dir* | --work-tree*) return 0 ;;
			-c) skip=1 ;;
			-*) ;;
			*) g='' ;;
			esac
		done ;;
	Read | Edit | Write)
		[ -z "$3" ] || return 0
		case "$2" in "~"* | /* | '$'* | %*) return 0 ;; esac ;;
	esac
	return 1
}

# check_rules <settings>: part 2.
check_rules()
{
	allow_rules "$1" || return 1
	probes > "${tmp:?}/probes" && pinned > "${tmp:?}/pin" && : > "${tmp:?}/named" || return 1
	notin="is a store rule (it names .config/agents, runs git -C, or reads or edits a home or absolute path) outside the card's list (TASK127, pinned in this script)"
	i=0
	while IFS= read -r r; do
		i=$((i + 1))
		# The tool, and the rule's body (a bare tool name allows it all).
		case "$r" in
		*"("*")") k=${r%%"("*} b=${r#*"("} b=${b%")"} bare='' ;;
		*) k=$r b='*' bare=1 ;;
		esac
		w=${b%% *} w=${w%%:*}
		case "$k:$w" in Bash:cp | Bash:*/cp)
			say "allow rule $i is a cp rule; memory is never copied (see CLAUDE.md)"
			return 1 ;;
		esac
		case "$r" in
		"Bash($pre:"*)
			say "allow rule $i is a store rule with a wildcard (* or :*)"
			return 1 ;;
		"Bash($pre "*")")
			rest=${r#"Bash($pre "}
			rest=${rest%")"}
			case "$rest" in rm | "rm "*)
				say "allow rule $i is a store rm rule; a memory is removed only with the owner present (see CLAUDE.md)"
				return 1 ;;
			esac
			case "$rest" in *"*"*) say "allow rule $i is a store rule with a wildcard (* or :*)"; return 1 ;; esac
			case " $rest" in *" -C"*) say "allow rule $i lets a store command take a second -C"; return 1 ;; esac
			case " $rest" in *" -c"* | *" --config-env"*)
				[ "$rest" = "-c core.editor=true rebase --continue" ] ||
					{ say "allow rule $i lets a store command take a -c other than core.editor=true on rebase --continue"; return 1; } ;;
			esac
			case "$rest" in push | "push "*)
				[ "$rest" = "push -q" ] || { say "allow rule $i lets the store push with arguments"; return 1; } ;;
			esac ;;
		esac
		# A rule's wildcards, and anything a case pattern would treat
		# specially, become *, so the match errs towards admitting.
		kp=$k
		[ "$kp" != Write ] || kp=Edit
		pat=$(printf '%s' "$b" | sed -e 's/[][?\\*]/*/g' -e 's/:\*$/*/') ||
			{ say "sed failed"; return 1; }
		j=0
		while IFS= read -r p; do
			j=$((j + 1))
			[ "${p%%"("*}" = "$kp" ] || continue
			pb=${p#*"("}
			case "${pb%")"}" in $pat) say "allow rule $i admits probe $j of this script, which no rule may"; return 1 ;; esac
		done < "${tmp:?}/probes"
		sr=0
		store_rule "$k" "$b" "$bare" || sr=$?
		case "$sr" in
		0) ;;
		1) continue ;;
		*) say "tr failed"; return 1 ;;
		esac
		grep -Fxq -- "$r" "${tmp:?}/pin" || { say "allow rule $i $notin"; return 1; }
		printf '%s\n' "$r" >> "${tmp:?}/named" || return 1
	done < "${tmp:?}/allow"
	LC_ALL=C sort "${tmp:?}/named" > "${tmp:?}/have" &&
		LC_ALL=C sort "${tmp:?}/pin" > "${tmp:?}/want" || { say "sort failed"; return 1; }
	cmp -s "${tmp:?}/want" "${tmp:?}/have" ||
		{ say "the store rules are not exactly the card's list, each once (TASK127, pinned in this script)"; return 1; }
}

# spans <file> <rule|all>: the code spans of <file> (rule: of the CLAUDE.md
# memory rule only), one per line; status 2 on an unbalanced span, 3 when
# the memory rule is not found.
spans()
{
	awk -v mode="$2" '
	mode == "rule" {
		if (!on) { if ($0 !~ /^- Agent memory /) next; on = 1 }
		else if ($0 !~ /^  /) exit
	}
	{
		n = split($0, f, "`")
		if (n > 0 && n % 2 == 0) { bad = 1; exit }
		for (i = 2; i < n; i += 2) print f[i]
	}
	END { if (bad) exit 2; if (mode == "rule" && !on) exit 3 }' "$1"
}

# commands <spans> <out>: the store commands among <spans>.
commands()
{
	awk -v st="$store" 'index($0, "memory-store") && $0 != st && $0 != st "/quake2/memory" &&
		$0 != st "/quake2/memory/"' "$1" > "$2" || { say "awk failed"; return 1; }
}

# check_docs <claude-md> <hook> <settings>: part 1; the commands stay in
# $tmp/doc, the memory rule's unmerged codes in $tmp/codes.
check_docs()
{
	rc=0
	spans "$1" rule > "${tmp:?}/spans-doc" || rc=$?
	case "$rc" in
	0) ;;
	3) say "cannot find the memory rule (the bullet that starts '- Agent memory ') in CLAUDE.md"; return 1 ;;
	*) say "cannot read the code spans of the CLAUDE.md memory rule (an unbalanced backtick?)"; return 1 ;;
	esac
	spans "$2" all > "${tmp:?}/spans-hook" || { say "cannot read the code spans of session-context.sh"; return 1; }
	commands "${tmp:?}/spans-doc" "${tmp:?}/doc" && commands "${tmp:?}/spans-hook" "${tmp:?}/hook" || return 1
	[ -s "${tmp:?}/doc" ] || { say "the CLAUDE.md memory rule names no store command"; return 1; }
	cmp -s "${tmp:?}/doc" "${tmp:?}/hook" ||
		{ say "session-context.sh does not spell out the store commands of the CLAUDE.md memory rule, the same and in the same order"; return 1; }
	for d in doc hook; do
		[ "$d" = doc ] && n="the CLAUDE.md memory rule" || n=session-context.sh
		for t in UU AA DD AU UA DU UD "$detached"; do
			grep -Fxq -- "$t" "${tmp:?}/spans-$d" && continue
			[ "$t" = "$detached" ] && what="the line the second check prints while a rebase is stopped" ||
				what="the unmerged code $t"
			say "$n does not name, as a code span, $what"
			return 1
		done
	done
	awk '/^[ADU][ADU]$/' "${tmp:?}/spans-doc" > "${tmp:?}/codes" || { say "awk failed"; return 1; }
	allow_rules "$3" || return 1
	j=0
	while IFS= read -r c; do
		j=$((j + 1))
		grep -Fxq -- "Bash($c)" "${tmp:?}/allow" && continue
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
	# <message> on settings holding the card's list, changed by <filter>
	# ($r is <rule>).
	pinned > "${tmp:?}/pinned" && jq -R . "${tmp:?}/pinned" > "${tmp:?}/rules.json" &&
		jq -s '{permissions: {allow: .}}' "${tmp:?}/rules.json" > "${tmp:?}/base.json" 2> /dev/null &&
		check_rules "${tmp:?}/base.json" || { say "selftest: the card's list alone was rejected"; exit 1; }
	reject()
	{
		jq --arg r "${4:-}" "$3" "${tmp:?}/base.json" > "${tmp:?}/s.json" 2> /dev/null ||
			{ say "selftest: cannot build the fixture for $1"; exit 1; }
		expect "$1" "$2" check_rules "${tmp:?}/s.json"
	}
	add='.permissions.allow += [$r]'
	wild="a store rule with a wildcard"
	notin="outside the card's list"
	reject "a cp rule" "is a cp rule" "$add" "Bash(cp -p a b)"
	reject "a cp rule by path" "is a cp rule" "$add" "Bash(/bin/cp:*)"
	reject "a cp* rule" "admits probe" "$add" "Bash(cp*)"
	reject "the rm rule added back" "a store rm rule" "$add" "Bash($pre rm -q quake2/memory/*)"
	reject "an exact rm rule" "a store rm rule" "$add" "Bash($pre rm -q quake2/memory/x.md)"
	reject "a git -C <store> * rule" "$wild" "$add" "Bash($pre *)"
	reject "a git -C <store>:* rule" "$wild" "$add" "Bash($pre:*)"
	reject "a widened rule" "$wild" '.permissions.allow |= map(if endswith("pull --rebase -q)") then .[:-1] + " *)" else . end)'
	reject "a second -C" "a second -C" "$add" "Bash($pre -C /x status --porcelain)"
	reject "a -c other than core.editor=true" "a -c other than" "$add" "Bash($pre -c core.pager=x status --porcelain)"
	reject "a push with arguments" "push with arguments" "$add" "Bash($pre push -q origin)"
	reject "the status without its pathspec" "$notin" "$add" "Bash($pre status --porcelain)"
	reject "the commit without its pathspec" "$notin" "$add" "Bash($pre commit -q -m quake2-memory-sync)"
	reject "a store rule of another form" "$notin" "$add" "Bash(env $pre push -q)"
	# shellcheck disable=SC2016 # $HOME is the rule's text, not expanded
	reject "an absolute store path (m3)" "$notin" "$add" 'Bash(git -C /Users/*/.config/agents/memory-*)'
	# shellcheck disable=SC2016 # $HOME is the rule's text, not expanded
	reject "a store path through \$HOME (m4)" "$notin" "$add" 'Bash(git -C $HOME/.config/agents/memory-*)'
	reject "a store path in another case" "$notin" "$add" "Bash(cat ~/.Config/Agents/memory_repo)"
	reject "a git -C after another option" "$notin" "$add" "Bash(/usr/bin/git -c x=y -C /x push -q)"
	reject "a Read rule under the home directory" "$notin" "$add" "Read(~/**)"
	reject "a bare Read rule" "$notin" "$add" "Read"
	reject "an Edit rule on the whole store" "admits probe" "$add" "Edit($store/**)"
	reject "a Write rule on the whole store" "admits probe" "$add" "Write($store/**)"
	reject "a rule that admits the store without naming it" "admits probe" "$add" "Bash(git -C *)"
	reject "a bare Bash rule" "admits probe" "$add" "Bash"
	reject "an extra store rule" "$notin" "$add" "Bash($pre fetch -q)"
	reject "a missing store rule" "not exactly the card's list" '.permissions.allow |= map(select(endswith(" push -q)") | not))'
	reject "a store rule twice" "not exactly the card's list" "$add" "Bash($pre push -q)"
	reject "an allow list that is not a list" "cannot read permissions.allow" '.permissions.allow = "x"'
	printf '{\n' > "${tmp:?}/s.json" || exit 1
	expect "a parse error" "cannot read permissions.allow" check_rules "${tmp:?}/s.json"
	for t in jq tr; do
		mkdir -p "${tmp:?}/shim-$t" || exit 1
		printf '#!/bin/sh\nexit 127\n' > "${tmp:?}/shim-$t/$t" && chmod 755 "${tmp:?}/shim-$t/$t" || exit 1
	done
	shimmed() { PATH=$tmp/shim-$1:$PATH; check_rules "${tmp:?}/base.json"; }
	expect "a failing jq" "cannot read permissions.allow" shimmed jq
	expect "a failing tr" "tr failed" shimmed tr
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
	rmc="\`$pre rm -q quake2/memory/x.md\`"
	docmut "an rm command back in the documents" "not allowed by an exact rule" \
		"s#delete its file from the memory directory#run $rmc#;s#(deleting its file asks for approval)#($rmc)#" both
	docmut "an unmerged code left out" "does not name, as a code span, the unmerged code AA" 's/`AA`, //' both
	docmut "a hook that leaves out an unmerged code" "session-context.sh does not name, as a code span, the unmerged code UD" \
		's/`UD`/UD/g' hook
	docmut "the stopped-rebase line left out" "the line the second check prints" "s/\`$detached\`/the detached line/" both
}

# run <machine> <command>: a documented store command in the clone of
# <machine>, as a word list (no shell parses it); its output in $tmp/out.
run()
{
	case "$2" in *[!-A-Za-z0-9\ ._/=~]*) say "selftest: a store command holds a character the runner refuses"; return 1 ;; esac
	old=$IFS
	IFS=' '
	set -f
	# shellcheck disable=SC2086 # split into words on purpose, globbing off
	set -- "$1" $2
	set +f
	IFS=$old
	[ $# -ge 5 ] && [ "$2" = git ] && [ "$3" = -C ] && [ "$4" = "$store" ] ||
		{ say "selftest: a store command is not git -C <store> <command>"; return 1; }
	m=$1
	shift 4
	case "$1" in
	status | add | commit | pull | push | rebase) ;;
	-c) [ "$*" = "-c core.editor=true rebase --continue" ] || { say "selftest: a store command takes a -c the runner refuses"; return 1; } ;;
	*) say "selftest: a store command runs a git command the runner refuses"; return 1 ;;
	esac
	git -C "${tmp:?}/$m/store" "$@" < /dev/null > "${tmp:?}/out" 2> "${tmp:?}/err"
}

# sync <machine>: the documented sync steps in order. A line of the first
# status that starts with an unmerged code the memory rule names, or the
# line $detached from the second, stops it before add and commit with
# status 3 (take the conflict steps); add and commit run only when the first
# status printed anything. Status 2 when a step stops on a rebase conflict,
# 1 on any other failure. The subcommands it ran are in $tmp/ran.
sync()
{
	dirty=''
	: > "${tmp:?}/ran" || return 1
	while IFS= read -r c; do
		s=${c#"$pre "}
		case "$s" in add* | commit*) [ -n "$dirty" ] || continue ;; esac
		printf '%s\n' "${s%% *}" >> "${tmp:?}/ran" || return 1
		if ! run "$1" "$c"; then
			[ ! -d "${tmp:?}/$1/store/.git/rebase-merge" ] || return 2
			return 1
		fi
		sc=0
		case "$s" in
		"status --porcelain=v2"*)
			grep -Fxq -- "$detached" "${tmp:?}/out" || sc=$? ;;
		status*)
			awk 'NR == FNR { c[$0]; next } (substr($0, 1, 2) in c) { f = 1 } END { exit !f }' \
				"${tmp:?}/rulecodes" "${tmp:?}/out" || sc=$?
			[ ! -s "${tmp:?}/out" ] || dirty=1 ;;
		*) sc=1 ;;
		esac
		case "$sc" in 0) return 3 ;; 1) ;; *) say "selftest: grep or awk failed"; return 1 ;; esac
	done < "${tmp:?}/seg-sync"
}

selftest_sync()
{
	isolate
	# The documented commands, split at the pushes: the sync steps, then the
	# conflict steps.
	: > "${tmp:?}/seg-sync" && : > "${tmp:?}/seg-conflict" || exit 1
	awk -v pre="$pre" -v d="$tmp" '
	{ split(substr($0, length(pre) + 2), w, " "); s = (w[1] == "-c") ? w[3] : w[1] }
	seg == 0 { print > (d "/seg-sync"); if (s == "push") seg = 1; next }
	seg == 1 { print > (d "/seg-conflict"); if (s == "push") seg = 2; next }
	{ extra = 1 }
	END { exit extra }' "${tmp:?}/cmds" || { say "selftest: a store command follows the conflict steps"; exit 1; }
	[ -s "${tmp:?}/seg-sync" ] && [ -s "${tmp:?}/seg-conflict" ] && [ -s "${tmp:?}/rulecodes" ] ||
		{ say "selftest: the memory rule lacks the sync steps, the conflict steps or the unmerged codes"; exit 1; }
	# pick <subcommand>: the first sync step that runs it.
	pick()
	{
		awk -v pre="$pre" -v w="$1" '{ split(substr($0, length(pre) + 2), a, " ") }
		a[1] == w { print; f = 1; exit }
		END { exit !f }' "${tmp:?}/seg-sync"
	}
	st=$(pick status) && addc=$(pick add) && commitc=$(pick commit) ||
		{ say "selftest: the sync steps lack a status, an add or a commit"; exit 1; }
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
	tip=''
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
	# under <file> <column> <what>: every path in <file>, from <column> on,
	# is under quake2/memory/.
	under()
	{
		awk -v c="$2" 'substr($0, c) !~ /^quake2\/memory\// { bad = 1 } END { exit bad }' "$1" ||
			{ say "selftest: $3"; exit 1; }
	}
	# stop <machine> <what>: the sync stops on a rebase conflict, and the
	# session ends there.
	stop()
	{
		sc=0
		sync "$1" || sc=$?
		[ "$sc" -eq 2 ] || { say "selftest: $2 did not stop the sync on a rebase conflict"; exit 1; }
	}
	# next <machine> <what>: the next session's sync stops at the checks,
	# before add and commit.
	next()
	{
		sc=0
		sync "$1" || sc=$?
		[ "$sc" -eq 3 ] && awk '$0 == "add" || $0 == "commit" { f = 1 } END { exit f }' "${tmp:?}/ran" ||
			{ say "selftest: after $2, the next sync on machine $1 did not stop at the checks, before add and commit"; exit 1; }
	}
	# named <machine> <what>: each line of the first check is unmerged, with
	# a code the memory rule names.
	named()
	{
		run "$1" "$st" || { say "selftest: the first check failed on machine $1"; exit 1; }
		awk 'NR == FNR { c[$0]; next } { n++; if (!(substr($0, 1, 2) in c)) bad = 1 } END { exit bad || !n }' \
			"${tmp:?}/rulecodes" "${tmp:?}/out" || { say "selftest: git shows an unmerged code for $2 that the memory rule does not name"; exit 1; }
	}
	# resolve <machine>: the documented conflict steps, in order.
	resolve()
	{
		while IFS= read -r c; do
			run "$1" "$c" || { say "selftest: a documented conflict step failed on machine $1"; exit 1; }
		done < "${tmp:?}/seg-conflict"
		forward
	}
	# nomarkers: no commit on the origin's main adds a conflict marker.
	nomarkers()
	{
		git -C "${tmp:?}/origin.git" log -p --format= refs/heads/main > "${tmp:?}/log" 2> /dev/null ||
			{ say "selftest: cannot read the origin's history"; exit 1; }
		sc=0
		grep -Eq '^[+](<{7}|={7}|>{7})' "${tmp:?}/log" || sc=$?
		case "$sc" in
		1) ;;
		0) say "selftest: a conflict marker reached the origin"; exit 1 ;;
		*) say "selftest: grep failed"; exit 1 ;;
		esac
	}

	put A a.md 'a v1'; step A "a new memory"
	step B "nothing to commit"
	put A a.md 'a v2'; step A "an edit"
	put B b.md 'b v1'; step B "an edit, beside an older copy of the other side's"
	step A "nothing to commit"
	for m in A B; do
		want "$m" a.md 'a v2' "machine $m lost the newer edit (an older copy reverted it)"
		want "$m" b.md 'b v1' "machine $m lacks the other side's edit"
	done

	# A removal, made with the owner present: the file deleted through the
	# link, then a sync.
	rm -f "${tmp:?}/A/memory/a.md" || exit 1
	step A "a removal"; step B "a removal upstream"
	gone B a.md "a removal on machine A did not reach machine B"
	put B c.md 'c v1'; step B "an edit after the removal"; step A "nothing to commit"
	for m in A B; do
		gone "$m" a.md "a removed memory came back on machine $m"
		want "$m" c.md 'c v1' "machine $m lacks the edit made after the removal"
	done

	# A shared clone: another project's staged change is neither shown by
	# the first check nor committed.
	x mkdir -p "${tmp:?}/A/store/other"
	printf 'o\n' > "${tmp:?}/A/store/other/o.md" || exit 1
	x git -C "${tmp:?}/A/store" add other/o.md
	put A s.md 's v1'
	run A "$st" || { say "selftest: the first check failed"; exit 1; }
	under "${tmp:?}/out" 4 "the first check shows another project's change in a shared clone"
	run A "$addc" && run A "$commitc" || { say "selftest: the add or the commit step failed"; exit 1; }
	git -C "${tmp:?}/A/store" diff --name-only HEAD^ HEAD > "${tmp:?}/names" 2> /dev/null ||
		{ say "selftest: cannot read the commit"; exit 1; }
	under "${tmp:?}/names" 1 "the commit took another project's staged change in a shared clone"
	x git -C "${tmp:?}/A/store" commit -q -m other -- other
	step A "beside another project's commit"; step B "another project's commit upstream"
	want B s.md 's v1' "machine B lacks the edit made beside another project's change"

	# An edit of the same line on both sides; the session stages its
	# resolution and ends before rebase --continue.
	put A MEMORY.md '# index' '- from A'
	put B MEMORY.md '# index' '- from B'
	step A "an edit to the index"
	stop B "an edit of the same line on both sides"
	put B MEMORY.md '# index' '- from A' '- from B'
	read -r c < "${tmp:?}/seg-conflict" && run B "$c" || { say "selftest: the first conflict step failed"; exit 1; }
	next B "a resolution staged but not continued"
	resolve B
	step A "the resolution upstream"
	for m in A B; do
		want "$m" MEMORY.md "$(printf '%s\n' '# index' '- from A' '- from B')" \
			"machine $m does not hold both sides of the resolved conflict"
	done

	# An add/add (the same new file on both sides), the session ending at
	# the stop (the reviewer's case).
	put A n.md 'n from A'; step A "a new memory"
	put B n.md 'n from B'
	stop B "the same new memory on both sides (add/add)"
	next B "an add/add conflict"
	named B "an add/add conflict"
	put B n.md 'n from A' 'n from B'
	resolve B
	step A "the add/add resolution upstream"
	for m in A B; do
		want "$m" n.md "$(printf '%s\n' 'n from A' 'n from B')" "machine $m does not hold both sides of the add/add"
	done

	# A modify/delete: machine A removes a memory (with the owner present)
	# that machine B edits; the session ending at the stop.
	put A d.md 'd v1'; step A "a new memory"; step B "a new memory upstream"
	rm -f "${tmp:?}/A/memory/d.md" || exit 1
	step A "a removal"
	put B d.md 'd v2'
	stop B "an edit of a memory removed on the other side (modify/delete)"
	next B "a modify/delete conflict"
	named B "a modify/delete conflict"
	resolve B
	step A "the modify/delete resolution upstream"
	for m in A B; do
		want "$m" d.md 'd v2' "machine $m lacks the edit the modify/delete resolution keeps"
	done

	# An unmerged file with no rebase in progress (a merge the owner left
	# stopped): only the first check shows it. Undone by hand afterwards.
	b="${tmp:?}/B/store"
	x git -C "$b" checkout -q -b side
	put B MEMORY.md '# index' '- from side'
	x git -C "$b" commit -q -a -m side
	x git -C "$b" checkout -q main
	put B MEMORY.md '# index' '- from main'
	x git -C "$b" commit -q -a -m main
	! git -C "$b" merge -q side > /dev/null 2>&1 || { say "selftest: the merge fixture did not conflict"; exit 1; }
	next B "a merge left stopped on a conflict"
	named B "a merge left stopped on a conflict"
	x git -C "$b" merge --abort
	x git -C "$b" reset -q --hard "$tip"
	x git -C "$b" branch -q -D side

	nomarkers
	for m in A B; do
		[ "$(git -C "${tmp:?}/$m/store" rev-parse HEAD 2> /dev/null)" = "$tip" ] &&
			[ -z "$(git -C "${tmp:?}/$m/store" status --porcelain 2> /dev/null)" ] ||
			{ say "selftest: machine $m did not end clean at the origin's main"; exit 1; }
	done
}

# The real checks first, so a fault in the tracked files is reported as
# such; the self-tests then show the checks and the sequence work.
check_docs CLAUDE.md .claude/hooks/session-context.sh .claude/settings.json || exit 1
cp "${tmp:?}/doc" "${tmp:?}/cmds" && cp "${tmp:?}/codes" "${tmp:?}/rulecodes" || exit 1
check_rules .claude/settings.json || exit 1
( selftest_sync ) || exit 1
( selftest_checks ) || exit 1
echo "$me: ok"
