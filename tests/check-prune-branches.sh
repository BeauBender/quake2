#!/bin/sh
# Invariant: scripts/prune-branches.sh deletes a remote branch only when its
# work is on main, and never a marker, main, an open pull request's head or
# a young branch (TASK244); .github/workflows/prune-branches.yml deletes
# nothing unless a person asks for it.
#
#   plan      against a local bare origin: a squash-merged branch and one
#             whose deletion main also made are deleted (on-main); a merged
#             pull request's head is deleted (merged-pr); a branch with a
#             change main lacks, one that deletes a file main still has, an
#             open pull request's head, a young branch and the claim/,
#             gate-clear/ and devloop/ markers are kept; main is not listed;
#             a merged-pr line with another sha does not count; a missing
#             origin/main exits 2; a branch named for a card closed as
#             completed, or at a subject naming one, is deleted (card-done)
#             unless its tip is newer than the close; an open card's branch
#             is kept; claim/TASK1 stays a marker though TASK1 is closed;
#   apply     deletes the planned branches on origin, prints each with its
#             sha; a branch that moved since the plan is not deleted and the
#             status is 1; a marker, main or an option-shaped name in a plan
#             file is refused;
#   workflow  runs on workflow_dispatch and schedule only; deletes only when
#             the event is workflow_dispatch and its apply input is true;
#             runs for the owner or the schedule; has permissions: {} at the
#             top and no ${{ }} in a run line;
#   mutants   copies of the script with the marker guard, the deletion test,
#             the close-time test and the lease removed must each fail a case.
#
# Needs sh, git, awk, sed, grep, sort and comm (no network, no gh).
# Usage: tests/check-prune-branches.sh
set -eu
CDPATH='' cd "$(dirname "$0")/.."
export LC_ALL=C
me=check-prune-branches
script=$PWD/scripts/prune-branches.sh
wf=.github/workflows/prune-branches.yml
for f in "$script" "$wf"; do
	[ -f "$f" ] || { echo "$me: missing $f" >&2; exit 1; }
done
tmp=$(mktemp -d "${TMPDIR:-/tmp}/prune-check.XXXXXX")
chmod 700 "${tmp:?}"
trap 'rm -rf "${tmp:?}"; [ "${_ok:-}" = 1 ] || exit 1' EXIT
trap 'exit 1' HUP INT TERM
mkdir "$tmp/tpl"
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 GIT_TEMPLATE_DIR="$tmp/tpl"
export GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@example.invalid
export GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@example.invalid
old='2020-01-01T00:00:00Z'
export GIT_AUTHOR_DATE=$old GIT_COMMITTER_DATE=$old
unset PRUNE_MIN_AGE

fails=0
bad()
{
	echo "$me: $*" >&2
	fails=$((fails + 1))
}

# build: a bare origin and a work clone with every case's branch pushed.
build()
{
	rm -rf "$tmp/origin.git" "$tmp/w"
	git init -q --bare "$tmp/origin.git"
	git init -q -b main "$tmp/w"
	cd "$tmp/w"
	git remote add origin "$tmp/origin.git"
	echo a >a; echo b >b; echo gone >gone
	git add a b gone; git commit -qm base
	git push -q origin main

	# squashed: two commits, landed on main as one squash commit.
	git checkout -q -b squashed main
	echo a2 >a; git commit -qam s1
	echo n >new; git add new; git commit -qm s2
	# deletes: removes "gone"; main removes it too.
	git checkout -q -b deletes main
	git rm -q gone; git commit -qm del
	# keepdel: removes b, which main keeps.
	git checkout -q -b keepdel main
	git rm -q b; git commit -qm delb
	# unmerged: a change main never gets.
	git checkout -q -b unmerged main
	echo x >a; git commit -qam x
	# prhead: unmerged content, but the head of a merged pull request.
	git checkout -q -b prhead main
	echo pr >pr; git add pr; git commit -qm pr
	# openpr: on-main content, but an open pull request's head.
	git checkout -q -b openpr squashed
	# markers and a nested name, all on-main content.
	git branch claim/TASK1 squashed
	git branch gate-clear/TASK1 squashed
	git branch devloop/lock squashed
	git branch lane/x squashed
	# card-done cases, all with content main lacks: named for a closed
	# card; not named for one but at a subject that is; named for a card
	# closed before its tip; named for an open card.
	git checkout -q -b fix9-r2 main
	echo r2 >r; git add r; git commit -qm 'round 2'
	git checkout -q -b worktree-agent-a1 main
	echo w >w; git add w; git commit -qm 'FIX9: the fix'
	git checkout -q -b lane-FIX8 main
	echo l >l; git add l; git commit -qm 'after the close'
	git checkout -q -b task7 main
	echo t >t; git add t; git commit -qm 'open card'
	# young: on-main content, committed now.
	git checkout -q -b young squashed
	GIT_COMMITTER_DATE='' GIT_AUTHOR_DATE='' git commit -q --allow-empty -m young

	git checkout -q main
	git checkout -q squashed -- a new
	git rm -q gone
	git commit -qm 'squash of squashed and deletes'
	echo later >a; git commit -qam 'main moves on'
	git push -q origin main squashed deletes keepdel unmerged prhead openpr \
		claim/TASK1 gate-clear/TASK1 devloop/lock lane/x young \
		fix9-r2 worktree-agent-a1 lane-FIX8 task7
	git fetch -q origin
	echo openpr >"$tmp/open"
	printf 'prhead %s\nunmerged 0000000000000000000000000000000000000000\n' \
		"$(git rev-parse prhead)" >"$tmp/merged"
	t=$(git log -1 --format=%ct main)
	printf 'FIX9 %s\nFIX8 %s\nTASK1 %s\n' $((t + 60)) $((t - 60)) $((t + 60)) >"$tmp/closed"
	cd - >/dev/null
}

# verdict PLAN BRANCH: "<verdict> <reason>" of BRANCH, or "absent".
verdict()
{
	awk -v b="$2" '$2 == b { print $1, $4; f = 1 } END { if (!f) print "absent" }' "$1"
}

# plan_cases SCRIPT: 0 when every plan case holds.
plan_cases()
{
	s=$1 r=0
	(cd "$tmp/w" && sh "$s" plan "$tmp/open" "$tmp/merged" "$tmp/closed") >"$tmp/plan" 2>"$tmp/err" || {
		[ "$s" = "$script" ] && bad "plan: exit $?: $(cat "$tmp/err")"
		return 1
	}
	for want in 'squashed delete on-main' 'deletes delete on-main' \
	    'lane/x delete on-main' 'prhead delete merged-pr' \
	    'keepdel keep not-on-main' 'unmerged keep not-on-main' \
	    'openpr keep open-pr' 'young keep young' \
	    'claim/TASK1 keep marker' 'gate-clear/TASK1 keep marker' \
	    'devloop/lock keep marker' 'main absent' 'HEAD absent' \
	    'fix9-r2 delete card-done' 'worktree-agent-a1 delete card-done' \
	    'lane-FIX8 keep not-on-main' 'task7 keep not-on-main'; do
		b=${want%% *} w=${want#* }
		got=$(verdict "$tmp/plan" "$b")
		if [ "$got" != "$w" ]; then
			[ "$s" = "$script" ] && bad "plan: $b is '$got', want '$w'"
			r=1
		fi
	done
	return $r
}

build
plan_cases "$script" || :

# No origin/main.
rm -rf "$tmp/nomain"; git init -q "$tmp/nomain"
rc=0; (cd "$tmp/nomain" && sh "$script" plan "$tmp/open" "$tmp/merged") >/dev/null 2>&1 || rc=$?
[ "$rc" = 2 ] || bad "plan: no origin/main exits $rc, want 2"

# ── apply ───────────────────────────────────────────────────────────────
remote_has()
{
	git --git-dir="$tmp/origin.git" rev-parse -q --verify "refs/heads/$1" >/dev/null
}
apply_cases()
{
	s=$1 r=0
	build
	(cd "$tmp/w" && sh "$s" plan "$tmp/open" "$tmp/merged" "$tmp/closed") >"$tmp/plan" 2>/dev/null || return 1
	# Move "deletes" on origin after the plan: it must survive.
	(cd "$tmp/w" && git checkout -q deletes && echo m >m && git add m &&
	    git commit -qm moved && git push -q origin deletes && git checkout -q main)
	rc=0
	(cd "$tmp/w" && sh "$s" apply "$tmp/plan") >"$tmp/out" 2>&1 || rc=$?
	[ "$rc" = 1 ] || { [ "$s" = "$script" ] && bad "apply: exit $rc with a moved branch, want 1"; r=1; }
	remote_has deletes || { [ "$s" = "$script" ] && bad "apply: deleted a branch that moved"; r=1; }
	for b in squashed lane/x prhead; do
		if remote_has "$b"; then
			[ "$s" = "$script" ] && bad "apply: $b not deleted"; r=1
		fi
		grep -q "^deleted $b [0-9a-f]\{40\}\$" "$tmp/out" ||
			{ [ "$s" = "$script" ] && bad "apply: no 'deleted $b <sha>' line"; r=1; }
	done
	for b in keepdel unmerged openpr young claim/TASK1 gate-clear/TASK1 devloop/lock main; do
		remote_has "$b" || { [ "$s" = "$script" ] && bad "apply: deleted $b"; r=1; }
	done
	# A hand-edited plan naming a marker, main or an option.
	sha=$(git --git-dir="$tmp/origin.git" rev-parse main)
	printf 'delete claim/TASK1 %s x\ndelete main %s x\ndelete --all %s x\n' \
		"$(git --git-dir="$tmp/origin.git" rev-parse claim/TASK1)" "$sha" "$sha" >"$tmp/evil"
	rc=0
	(cd "$tmp/w" && sh "$s" apply "$tmp/evil") >/dev/null 2>&1 || rc=$?
	[ "$rc" = 1 ] || { [ "$s" = "$script" ] && bad "apply: a plan naming markers exits $rc, want 1"; r=1; }
	for b in claim/TASK1 main; do
		remote_has "$b" || { [ "$s" = "$script" ] && bad "apply: deleted $b from a hand-edited plan"; r=1; }
	done
	return $r
}
apply_cases "$script" || :

# ── workflow ────────────────────────────────────────────────────────────
on=$(awk '/^on:/{f=1; next} /^[^ #]/{f=0} f' "$wf")
printf '%s\n' "$on" | grep -q '^  workflow_dispatch:' || bad "workflow: no workflow_dispatch"
printf '%s\n' "$on" | grep -q '^  schedule:' || bad "workflow: no schedule"
others=$(printf '%s\n' "$on" | grep '^  [a-z_]*:' | grep -v '^  workflow_dispatch:\|^  schedule:' || :)
[ -z "$others" ] && : || bad "workflow: other triggers: $others"
grep -q '^permissions: {}' "$wf" || bad "workflow: no top-level permissions: {}"
awk '/run: \|/{f=1; next} /^    - /{f=0} f && /\$\{\{/' "$wf" | grep -q . &&
	bad "workflow: a \${{ }} in a run line"
grep -q "github.event_name == 'workflow_dispatch' && inputs.apply" "$wf" ||
	bad "workflow: the delete step is not limited to a dispatch with apply"
grep -q 'scripts/prune-branches.sh apply' "$wf" || bad "workflow: no apply step"
grep -q 'scripts/prune-branches.sh plan' "$wf" || bad "workflow: no plan step"
grep -q "github.event_name == 'schedule' || github.actor == github.repository_owner" "$wf" ||
	bad "workflow: not limited to the owner and the schedule"

# ── mutants ─────────────────────────────────────────────────────────────
mutant()
{
	sed "$2" "$script" >"$tmp/m.sh"
	cmp -s "$tmp/m.sh" "$script" && { bad "mutant $1: the edit changed nothing"; return; }
	ok=1
	plan_cases "$tmp/m.sh" || ok=0
	apply_cases "$tmp/m.sh" || ok=0
	[ $ok = 0 ] || bad "mutant $1: no case fails"
}
mutant no-marker-guard 's#claim/\*|gate-clear/\*|devloop/\*)$#nomarker)#'
mutant no-delete-test 's#git cat-file -e "\$main:\$path" 2>/dev/null && return 1 ;;#: ;;#'
mutant no-close-time 's#\[ -n "\$at" \] && \[ "\$2" -le "\$at" \]#[ -n "$at" ]#'
mutant no-lease 's#--force-with-lease="refs/heads/\$b:\$sha" ##'

if [ "$fails" != 0 ]; then
	echo "$me: $fails failure(s)" >&2
	exit 1
fi
echo "$me: ok"
_ok=1
