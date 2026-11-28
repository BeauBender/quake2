#!/bin/sh
# Deletes the remote work branches whose work is already on main
# (TASK244). .github/workflows/prune-branches.yml runs it; a person can run
# it from any checkout of this repository too.
#
#   plan OPEN MERGED [CLOSED]
#                      prints one line a branch of the remote, in name order:
#                        delete <branch> <sha> <reason>
#                        keep <branch> <sha> <reason>
#                      OPEN names the head branch of every open pull request,
#                      one a line; MERGED holds "<branch> <sha>", the head of
#                      every merged pull request, one a line. CLOSED, when
#                      given, holds "<card id> <unix time>", each card closed
#                      as completed and when. All are read as data only. It reads the remote's branches from
#                      refs/remotes/origin/ (fetch them first) and needs
#                      refs/remotes/origin/main.
#   apply PLAN         deletes each "delete" branch of PLAN on origin, leased
#                      on its sha: a branch that has moved since the plan is
#                      left alone and reported. Prints "deleted <branch>
#                      <sha>" for each one, so a branch can be pushed back
#                      from the log.
#
# A branch is kept, whatever else holds, when it is main, a loop marker
# (claim/, gate-clear/, devloop/: never deleted, CLAUDE.md), the head of an
# open pull request, or its tip commit is younger than PRUNE_MIN_AGE seconds
# (default 86400), so a branch being worked on is never taken. Otherwise it
# is deleted when either holds:
#   merged-pr  its tip is the head sha of a merged pull request of that name;
#   on-main    every file it changes from its merge base with main is on
#              main: each added or changed blob is reachable from main, and
#              each deleted path is absent from main's tip. A squash merge
#              leaves the branch's commits off main but its blobs on it,
#              which is how most work here lands;
#   card-done  its name names a card (an optional "lane-", then letters and
#              digits: fix115-r3b is FIX115, lane-TASK121 is TASK121), or a
#              name that names none whose tip commit's subject starts with a
#              card id (worktree-agent-a01 at "FIX66: ..." is FIX66), that
#              CLOSED lists, and its tip commit is no newer than that close:
#              the review rounds a card left behind once it was done.
# Every other branch is kept ("not-on-main"). A submodule or a type change
# counts as not on main.
#
# Exit status: 0, 1 when a deletion failed (apply), 2 on a usage error or a
# missing ref. Needs git, sh, awk, sort and comm.
# Usage: scripts/prune-branches.sh plan OPEN MERGED [CLOSED]
#        scripts/prune-branches.sh apply PLAN
set -eu
export LC_ALL=C
me=prune-branches
main=refs/remotes/origin/main
min_age=${PRUNE_MIN_AGE:-86400}

usage()
{
	echo "usage: $me plan OPEN MERGED | apply PLAN" >&2
	exit 2
}

case $min_age in
''|*[!0-9]*) echo "$me: PRUNE_MIN_AGE is not a number of seconds" >&2; exit 2 ;;
esac

plan()
{
	open=$1 merged=$2 closed=${3:-/dev/null}
	[ -f "$open" ] && [ -f "$merged" ] && [ -r "$closed" ] || usage
	git rev-parse -q --verify "$main^{commit}" >/dev/null || {
		echo "$me: no $main; fetch the remote's branches first" >&2
		exit 2
	}
	tmp=$(mktemp -d "${TMPDIR:-/tmp}/prune.XXXXXX")
	trap 'rm -rf "${tmp:?}"' EXIT
	trap 'exit 1' HUP INT TERM
	# Every object reachable from main, once.
	git rev-list --objects "$main" | cut -d' ' -f1 | sort -u >"$tmp/onmain"
	now=$(date +%s)
	git for-each-ref --format='%(refname:lstrip=3) %(objectname) %(committerdate:unix)' \
		refs/remotes/origin/ | sort >"$tmp/refs"
	while read -r b sha when; do
		case $b in
		HEAD|main) continue ;;
		claim/*|gate-clear/*|devloop/*)
			echo "keep $b $sha marker"; continue ;;
		esac
		if grep -Fqx -- "$b" "$open"; then
			echo "keep $b $sha open-pr"; continue
		fi
		if [ $((now - when)) -lt "$min_age" ]; then
			echo "keep $b $sha young"; continue
		fi
		if grep -Fqx -- "$b $sha" "$merged"; then
			echo "delete $b $sha merged-pr"; continue
		fi
		if on_main "$sha"; then
			echo "delete $b $sha on-main"
		elif card_done "$b" "$when" "$sha"; then
			echo "delete $b $sha card-done"
		else
			echo "keep $b $sha not-on-main"
		fi
	done <"$tmp/refs"
}

# card_done BRANCH WHEN SHA: BRANCH's card closed as completed at or after
# WHEN.
card_done()
{
	id=$(printf '%s\n' "${1#lane-}" |
		sed -n 's/^\([A-Za-z][A-Za-z]*[0-9][0-9]*\).*/\1/p' | tr a-z A-Z)
	[ -n "$id" ] || id=$(git log -1 --format=%s "$3" |
		sed -n 's/^\([A-Z][A-Z]*[0-9][0-9]*\)[^A-Za-z0-9].*/\1/p')
	[ -n "$id" ] || return 1
	at=$(awk -v id="$id" '$1 == id && $2 ~ /^[0-9]+$/ { print $2; exit }' "$closed")
	[ -n "$at" ] && [ "$2" -le "$at" ]
}

# on_main SHA: every change SHA makes from its merge base with main is on main.
on_main()
{
	base=$(git merge-base "$main" "$1") || return 1
	git diff --raw --no-abbrev --no-renames "$base" "$1" >"$tmp/raw" || return 1
	: >"$tmp/need"
	# :oldmode newmode oldsha newsha status<TAB>path
	while IFS='	' read -r meta path; do
		set -- $meta
		case $5 in
		D)
			git cat-file -e "$main:$path" 2>/dev/null && return 1 ;;
		A|M)
			case $2 in 160000) return 1 ;; esac
			echo "$4" >>"$tmp/need" ;;
		*)
			return 1 ;;
		esac
	done <"$tmp/raw"
	sort -u "$tmp/need" -o "$tmp/need"
	[ -z "$(comm -23 "$tmp/need" "$tmp/onmain")" ]
}

apply()
{
	[ -f "$1" ] || usage
	fails=0 n=0
	while read -r verdict b sha _; do
		[ "$verdict" = delete ] || continue
		case $b in
		''|main|HEAD|claim/*|gate-clear/*|devloop/*|-*)
			echo "$me: refusing to delete '$b'" >&2; fails=$((fails + 1)); continue ;;
		esac
		case $sha in
		*[!0-9a-f]*|'') echo "$me: bad sha for $b" >&2; fails=$((fails + 1)); continue ;;
		esac
		if git push -q --force-with-lease="refs/heads/$b:$sha" origin ":refs/heads/$b"; then
			echo "deleted $b $sha"; n=$((n + 1))
		else
			echo "$me: not deleted (moved or refused): $b $sha" >&2
			fails=$((fails + 1))
		fi
	done <"$1"
	echo "$me: deleted $n, failed $fails"
	[ "$fails" = 0 ]
}

[ $# -ge 1 ] || usage
mode=$1; shift
case $mode in
plan) { [ $# = 2 ] || [ $# = 3 ]; } || usage; plan "$@" ;;
apply) [ $# = 1 ] || usage; apply "$1" ;;
*) usage ;;
esac
