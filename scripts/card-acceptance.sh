#!/bin/sh
# card-acceptance: a card closes as completed only when every one of its
# acceptance criteria carries a verdict (docs/decisions/0015-acceptance-
# verdicts.md, TASK210).
#
# A card is a GitHub issue with a state:* label. Its acceptance criteria are
# the list items of every section of its body whose heading is "Acceptance"
# or "Acceptance criteria" (levels 2 to 4, any letter case). A section ends
# at the next heading of level 1 to 4 or at a "---" line; fenced code (``` or
# ~~~) inside it is skipped. Each top-level item, and each nested task item
# (- [ ] or - [x]), is one criterion. A nested plain item, and an indented
# line that is not an item, continue the criterion above them. A criterion
# has a verdict when:
#   met       it is a ticked task item: - [x] or - [X] (also * and +);
#   moved     its text holds "moved to #N", N an issue number;
#   dropped   its text holds "dropped:" followed by a reason that is not empty.
# Anything else, an unticked box or a plain item with no marker among them,
# has none. The markers are matched in any letter case.
#
# Usage:
#   card-acceptance.sh check FILE      print "line N: text" for each criterion
#                                      of the card body in FILE with no
#                                      verdict; exit 0 when there is none, 1
#                                      when there is one, 2 on a usage error
#   card-acceptance.sh refs FILE [REPO]
#                                      print the issue numbers that the pull-
#                                      request body in FILE closes (close,
#                                      fix or resolve in any form, then
#                                      "#N" or "REPO#N"), sorted, once each
#   card-acceptance.sh closed N REASON (.github/workflows/card-acceptance.yml)
#                                      issue N was closed with REASON: when
#                                      it is a card closed as completed and a
#                                      criterion has no verdict, comment the
#                                      lines, reopen it as state:review, and
#                                      exit 1
#   card-acceptance.sh pr N            (.github/workflows/card-acceptance-
#                                      pr.yml) exit 1 when a card that pull
#                                      request N closes has a criterion with
#                                      no verdict
# The closed and pr modes call gh with GH_REPO set (owner/name) and read the
# bodies and labels into files. Text from an issue or a pull request is only
# compared by awk and grep, never run, and never passed as a gh argument
# but through a file.
set -eu
export LC_ALL=C
me=card-acceptance

usage()
{
	echo "usage: $me check FILE | refs FILE [REPO] | closed N REASON | pr N" >&2
	exit 2
}

# unverdicted FILE: the "line N: text" lines of the criteria with no verdict.
unverdicted()
{
	awk '
	function lead(s) { match(s, /^[ \t]*/); return RLENGTH }
	function flush() {
		if (open && !ok(text))
			printf "line %d: %s\n", start, substr(first, 1, 160)
		open = 0
	}
	function ok(t,   l) {
		if (ticked) return 1
		l = tolower(t)
		if (l ~ /moved to #[0-9]+/) return 1
		if (l ~ /dropped:[ \t]*[^ \t]/) return 1
		return 0
	}
	{
		sub(/\r$/, "")
		line = $0
	}
	fence != "" {
		if (line ~ ("^[ \t]*" fence)) fence = ""
		next
	}
	/^[ \t]*(```|~~~)/ {
		if (insec) { fence = substr(line, lead(line) + 1, 3) }
		next
	}
	/^(#|##|###|####)[ \t]/ {
		flush()
		h = tolower(line)
		sub(/^#+[ \t]+/, "", h)
		insec = (line ~ /^(##|###|####)[ \t]/ && h ~ /^acceptance( criteria)?[ \t]*:?[ \t]*$/)
		next
	}
	/^---[ \t]*$/ { flush(); insec = 0; next }
	!insec { next }
	{
		ind = lead(line)
		rest = substr(line, ind + 1)
		item = (rest ~ /^([-*+]|[0-9]+[.)])[ \t]/)
		task = (rest ~ /^[-*+][ \t]+\[[ xX]\]([ \t]|$)/)
		if (item && (ind < 2 || task)) {
			flush()
			open = 1; start = NR; first = rest; text = rest
			ticked = (rest ~ /^[-*+][ \t]+\[[xX]\]/)
			next
		}
		if (line ~ /^[ \t]*$/) next
		if (open && (ind >= 2 || item)) { text = text " " rest; next }
		flush()
	}
	END { flush() }
	' "$1"
}

# refs FILE REPO: the issue numbers that the pull-request body in FILE closes.
refs()
{
	awk -v repo="$(printf '%s' "$2" | tr '[:upper:]' '[:lower:]')" '
	{
		sub(/\r$/, "")
		s = tolower($0)
		while (match(s, /(^|[^a-z0-9_])(close[sd]?|fix(e[sd])?|resolve[sd]?):?[ \t]+[a-z0-9_.\/-]*#[0-9]+/)) {
			m = substr(s, RSTART, RLENGTH)
			s = substr(s, RSTART + RLENGTH)
			if (s ~ /^[a-z0-9_]/) continue
			sub(/^[^a-z]*/, "", m)
			sub(/^[a-z]+:?[ \t]+/, "", m)
			n = m; sub(/^.*#/, "", n)
			r = m; sub(/#[0-9]+$/, "", r)
			if (r == "" || (repo != "" && r == repo)) print n + 0
		}
	}' "$1" | sort -n -u
}

numeric()
{
	case $1 in
	'' | *[!0-9]*) echo "$me: not an issue number: $1" >&2; exit 2 ;;
	esac
}

need_repo()
{
	case ${GH_REPO:-} in
	?*/?*) ;;
	*) echo "$me: GH_REPO is not owner/name" >&2; exit 2 ;;
	esac
}

# card N DIR: read issue N's body and labels into DIR/body and DIR/labels.
# Fails (1) when N is a pull request or not a card (no state:* label).
card()
{
	[ "$(gh api "repos/$GH_REPO/issues/$1" --jq 'has("pull_request")' </dev/null)" = false ] || return 1
	gh api "repos/$GH_REPO/issues/$1" --jq '.body // ""' > "$2/body" </dev/null
	gh api --paginate "repos/$GH_REPO/issues/$1/labels" --jq '.[].name' > "$2/labels" </dev/null
	grep -q '^state:' "$2/labels"
}

# quote FILE: FILE's lines in a fenced block, with no backtick or tilde, so
# no text read from a card ends the fence or mentions anyone.
quote()
{
	echo '```'
	tr -d '`~' < "$1"
	echo '```'
}

[ $# -ge 1 ] || usage
mode=$1
shift
case $mode in
check)
	[ $# -eq 1 ] && [ -f "$1" ] || usage
	out=$(unverdicted "$1")
	[ -z "$out" ] && exit 0
	printf '%s\n' "$out"
	exit 1
	;;
refs)
	[ $# -ge 1 ] && [ $# -le 2 ] && [ -f "$1" ] || usage
	refs "$1" "${2:-}"
	exit 0
	;;
closed)
	[ $# -eq 2 ] || usage
	numeric "$1"
	need_repo
	n=$1 reason=$2
	[ "$reason" = completed ] || { echo "issue $n closed as ${reason:-no reason}: not checked"; exit 0; }
	tmp=$(mktemp -d "${TMPDIR:-/tmp}/cardacc.XXXXXX")
	trap 'rm -rf "$tmp"' EXIT
	# The run can start after the issue was reopened: check only a closed one.
	st=$(gh api "repos/$GH_REPO/issues/$n" --jq .state </dev/null)
	[ "$st" = closed ] || { echo "issue $n is $st: not checked"; exit 0; }
	card "$n" "$tmp" || { echo "issue $n is not a card: not checked"; exit 0; }
	if grep -Fxq state:promoted "$tmp/labels"; then
		echo "card $n was promoted: its criteria moved with it"
		exit 0
	fi
	rc=0
	unverdicted "$tmp/body" > "$tmp/missing" || rc=$?
	[ "$rc" -eq 0 ] || { echo "$me: cannot read the body of $n" >&2; exit 1; }
	[ -s "$tmp/missing" ] || { echo "card $n: every acceptance criterion has a verdict"; exit 0; }
	{
		echo "Reopened: this card was closed as completed, but these acceptance criteria have no verdict."
		echo
		quote "$tmp/missing"
		echo
		echo "Give each one a verdict in the card body, then close it again:"
		echo "- met: tick its box (\`- [x]\`);"
		echo "- moved: add \"moved to #N\", the card that now holds it;"
		echo "- dropped: add \"dropped: <reason>\", with the owner's agreement."
		echo
		echo "(.github/workflows/card-acceptance.yml, docs/decisions/0015-acceptance-verdicts.md)"
	} > "$tmp/comment"
	gh issue comment "$n" --body-file "$tmp/comment" </dev/null
	gh issue reopen "$n" </dev/null
	gh issue edit "$n" --add-label state:review </dev/null
	if grep -Fxq state:done "$tmp/labels" ||
		gh api --paginate "repos/$GH_REPO/issues/$n/labels" --jq '.[].name' </dev/null | grep -Fxq state:done; then
		gh issue edit "$n" --remove-label state:done </dev/null || true
	fi
	echo "card $n reopened: $(wc -l < "$tmp/missing" | tr -d ' ') criteria with no verdict" >&2
	cat "$tmp/missing" >&2
	exit 1
	;;
pr)
	[ $# -eq 1 ] || usage
	numeric "$1"
	need_repo
	tmp=$(mktemp -d "${TMPDIR:-/tmp}/cardacc.XXXXXX")
	trap 'rm -rf "$tmp"' EXIT
	gh api "repos/$GH_REPO/pulls/$1" --jq '.body // ""' > "$tmp/pr" </dev/null
	refs "$tmp/pr" "$GH_REPO" > "$tmp/refs"
	[ -s "$tmp/refs" ] || { echo "pull request $1 closes no issue"; exit 0; }
	bad=0
	while read -r c; do
		mkdir "$tmp/$c"
		if ! card "$c" "$tmp/$c"; then
			echo "#$c is not a card: not checked"
			continue
		fi
		rc=0
		unverdicted "$tmp/$c/body" > "$tmp/$c/missing" || rc=$?
		[ "$rc" -eq 0 ] || { echo "$me: cannot read the body of #$c" >&2; exit 1; }
		if [ -s "$tmp/$c/missing" ]; then
			echo "#$c: acceptance criteria with no verdict:" >&2
			cat "$tmp/$c/missing" >&2
			bad=1
		else
			echo "#$c: every acceptance criterion has a verdict"
		fi
	done < "$tmp/refs"
	if [ "$bad" -ne 0 ]; then
		echo "$me: give each criterion a verdict in its card (tick it, \"moved to #N\" or \"dropped: <reason>\"), then re-run this check (edit or push to the pull request)" >&2
		exit 1
	fi
	exit 0
	;;
*)
	usage
	;;
esac
