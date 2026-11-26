#!/bin/bash
# The check selector of the invariants job (TASK211). scripts/ci-checks.sh
# runs it, with CI_CHECKS_GROUP set, to choose the checks one job of the
# matrix in .github/workflows/macos.yml runs; tests/check-ci-map.sh tests it.
#
# Usage: scripts/ci-select.sh <map> <group> <changed> <check>...
#   <map>      tests/ci-map.txt, or a copy of it
#   <group>    fast, harness or ios
#   <changed>  a file of the paths a pull request changes, each one ended by
#              a NUL (git diff -z --name-only), or - for every path (a push,
#              the nightly run, or a change that could not be read)
#   <check>    the candidates: the names of tests/ci-checks.txt, in order
# Prints the candidates the group runs, one name a line, in the order given,
# and on standard error one line that says why. Prints a path never, only
# counts: a name a pull request chose is not echoed into the log.
#
# The groups. The map puts every check of tests/ci-checks.txt in one group:
#   ios      the checks named by an "ios" line (tests/check-ios.sh);
#   harness  the checks named by a "harness" line, the slow self-tests of the
#            harness, each with the path globs that can change its result;
#   fast     every other candidate, a "fast" line or no line at all, so a
#            check the map does not name still runs, in the fast job.
# fast and ios do not depend on the change. harness does:
#   - with <changed> -, every harness check;
#   - otherwise every harness check when the list holds no path, when it is
#     not ended by a NUL, or when one path is the map, tests/ci-checks.txt,
#     tests/ci-local-only.txt, scripts/ci-checks.sh, this script or under
#     .github/ (those are fixed here, so a change to the map cannot remove
#     them), or matches an "all" glob, or matches neither a "harness" glob
#     nor a "product" glob (a path no harness check reads): fail closed;
#   - otherwise the harness checks whose globs match a path.
# A glob is a shell case pattern over the whole path ("*" matches "/" too),
# compared without regard to letter case: the macOS runner's file system
# folds case, so scripts/PINS.sh would be checked out over scripts/pins.sh.
# Each path is data: it is read NUL-delimited, never split, never expanded,
# never evaluated.
#
# The map, one entry a line; a "#" starts a comment, blanks (space, tab)
# separate the fields, a blank line is skipped, a carriage return anywhere
# is refused:
#   fast <check>            ios <check>
#   harness <check> <glob>  all <glob>  product <glob>
# A <check> is a tests/check-*.sh name (no "/"); a check is in one group
# only, and a harness check may have many lines. A <glob> starts with a
# letter, a digit, "." or "_", so no glob matches every path.
#
# Exits 2 on a usage error, an unreadable map or changes file, or a map that
# breaks a rule above, and prints nothing on standard output then: the
# caller runs every candidate.
set -u
set -f
shopt -s nocasematch
me=ci-select

die() {
	printf '%s: %s\n' "$me" "$1" >&2
	exit 2
}

[ $# -ge 3 ] || die "usage: scripts/ci-select.sh <map> <group> <changed> <check>..."
map=$1
group=$2
changed=$3
shift 3
case $group in
	fast | harness | ios) ;;
	*) die "unknown group (fast, harness or ios)" ;;
esac
[ -f "$map" ] && [ -r "$map" ] || die "cannot read the map"
cr=$(printf '\r')

# ---- The map, as parallel arrays: kind, check name (or ""), glob (or ""). --
k=()
c=()
g=()
ln=0
while IFS= read -r line || [ -n "$line" ]; do
	ln=$((ln + 1))
	case $line in
		*"$cr"*) die "map line $ln holds a carriage return" ;;
	esac
	line=${line%%#*}
	IFS=$' \t' read -r -a f <<< "$line"
	[ ${#f[@]} -gt 0 ] || continue
	case ${f[0]} in
		fast | ios) [ ${#f[@]} -eq 2 ] || die "map line $ln: '${f[0]} <check>'" ;;
		harness) [ ${#f[@]} -eq 3 ] || die "map line $ln: 'harness <check> <glob>'" ;;
		all | product) [ ${#f[@]} -eq 2 ] || die "map line $ln: '${f[0]} <glob>'" ;;
		*) die "map line $ln: an unknown kind (fast, ios, harness, all, product)" ;;
	esac
	n="" p=""
	case ${f[0]} in
		fast | ios) n=${f[1]} ;;
		harness) n=${f[1]} p=${f[2]} ;;
		*) p=${f[1]} ;;
	esac
	if [ -n "$n" ]; then
		case $n in
			*/*) die "map line $ln: a check name holds a '/'" ;;
			check-*.sh) ;;
			*) die "map line $ln: not a tests/check-*.sh name" ;;
		esac
	fi
	if [ -n "$p" ]; then
		case ${p:0:1} in
			[A-Za-z0-9._]) ;;
			*) die "map line $ln: a glob starts with a letter, a digit, '.' or '_'" ;;
		esac
	fi
	k[${#k[@]}]=${f[0]}
	c[${#c[@]}]=$n
	g[${#g[@]}]=$p
done < "$map"

# group_of <check>: fast, ios or harness from the map, or "" for none
group_of() {
	_r=""
	for ((i = 0; i < ${#k[@]}; i++)); do
		[ "${c[i]}" = "$1" ] || continue
		case ${k[i]} in
			harness) _w=harness ;;
			*) _w=${k[i]} ;;
		esac
		if [ -n "$_r" ] && [ "$_r" != "$_w" ]; then
			die "the map puts $1 in two groups ($_r, $_w)"
		fi
		_r=$_w
	done
	printf '%s' "$_r"
}

# the candidates' groups, in order (and every named check's, so a conflict
# is an error whatever the candidates)
for ((j = 0; j < ${#c[@]}; j++)); do
	[ -z "${c[j]}" ] || group_of "${c[j]}" > /dev/null || exit 2
done
cand=()
cgrp=()
for n; do
	case $n in
		*/* | *[[:space:]]*) die "a candidate is not a check name" ;;
		check-*.sh) ;;
		*) die "a candidate is not a check name" ;;
	esac
	cand[${#cand[@]}]=$n
	cgrp[${#cgrp[@]}]=$(group_of "$n") || exit 2
done
[ ${#cand[@]} -gt 0 ] || die "no candidate"

# emit <want>: prints the candidates whose group is <want> ("fast" also takes
# those with none); for harness, only those in $sel unless $every is 1
emit() {
	_o=0
	for ((j = 0; j < ${#cand[@]}; j++)); do
		_w=${cgrp[j]}
		[ -n "$_w" ] || _w=fast
		[ "$_w" = "$1" ] || continue
		if [ "$1" = harness ] && [ "$every" != 1 ]; then
			case $sel in *" ${cand[j]} "*) ;; *) continue ;; esac
		fi
		printf '%s\n' "${cand[j]}"
		_o=$((_o + 1))
	done
	count=$_o
}

every=1
sel=" "
count=0
if [ "$group" != harness ]; then
	emit "$group"
	printf '%s: %s: %d checks\n' "$me" "$group" "$count" >&2
	exit 0
fi
if [ "$changed" = - ]; then
	emit harness
	printf '%s: harness: every harness check (%d): no list of changed paths (a push, the schedule, or a change that could not be read)\n' "$me" "$count" >&2
	exit 0
fi
[ -f "$changed" ] && [ -r "$changed" ] || die "cannot read the changed paths"

# ---- The changed paths. ---------------------------------------------------
why=""
np=0
done_all=0
p=""
while IFS= read -r -d '' p; do
	np=$((np + 1))
	case $p in
		tests/ci-map.txt | tests/ci-checks.txt | tests/ci-local-only.txt | \
			scripts/ci-checks.sh | scripts/ci-select.sh | .github/*)
			why="path $np changes the selector, its lists or .github/"
			done_all=1
			break
			;;
	esac
	acc=0
	for ((i = 0; i < ${#k[@]}; i++)); do
		case $p in
			${g[i]}) ;;
			*) continue ;;
		esac
		case ${k[i]} in
			all)
				why="path $np matches an \"all\" glob of the map (line kind all)"
				done_all=1
				break 2
				;;
			harness)
				case $sel in *" ${c[i]} "*) ;; *) sel="$sel${c[i]} " ;; esac
				acc=1
				;;
			product) acc=1 ;;
		esac
	done
	if [ "$acc" != 1 ]; then
		why="path $np is in no map entry (fail closed)"
		done_all=1
		break
	fi
	p=""
done < "$changed"
if [ "$done_all" != 1 ] && [ -n "$p" ]; then
	why="the list of changed paths is not ended by a NUL"
	done_all=1
fi
if [ "$done_all" != 1 ] && [ "$np" -eq 0 ]; then
	why="the change holds no path"
	done_all=1
fi
if [ "$done_all" = 1 ]; then
	every=1
	emit harness
	printf '%s: harness: every harness check (%d): %s\n' "$me" "$count" "$why" >&2
	exit 0
fi
every=0
emit harness
printf '%s: harness: %d checks for the %d changed paths\n' "$me" "$count" "$np" >&2
exit 0
