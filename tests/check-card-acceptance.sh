#!/bin/sh
# Invariant: a card cannot close as completed with an acceptance criterion
# that has no verdict (docs/decisions/0015-acceptance-verdicts.md, TASK210).
#
# Checks scripts/card-acceptance.sh, which holds the rule, and the two
# workflows that run it:
#   parse     the check mode against card bodies: ticked, unticked, moved
#             to #N, dropped: with and without a reason, plain items,
#             continuation lines, nested items, fenced code, other
#             sections, heading variants, --- ends, CRLF, no section, and
#             text holding shell syntax, which must never run;
#   refs      the refs mode against pull-request bodies: every closing
#             keyword form, "#N" and "REPO#N", another repository's
#             number, a number glued to a word, duplicates;
#   closed    the closed mode against a stub gh first on PATH, which logs
#             its arguments and serves the state, the body and the labels:
#             a card with a criterion and no verdict is commented on,
#             reopened, labelled state:review and loses state:done, and the
#             mode exits 1; every criterion with a verdict, not_planned, an
#             issue reopened since, a pull request, an issue with no state
#             label and a promoted card exit 0 with no edit; a number that
#             is not one exits 2 with no gh call; no gh call reads stdin;
#   pr        the pr mode: a pull request that closes a card with a
#             criterion and no verdict exits 1 and names it; one whose
#             cards all have verdicts, or that closes no issue, exits 0;
#   workflow  .github/workflows/card-acceptance.yml runs on issues closed
#             only, shares card-close-labels.yml's concurrency group, runs
#             only for the owner, and its one run line calls the closed
#             mode; card-acceptance-pr.yml runs on pull_request opened,
#             edited, synchronize and reopened and calls the pr mode;
#             neither has a ${{ }} in a run line, both have permissions: {}
#             at the top, and neither job can write contents;
#   mutants   copies of the script with the reopen removed, the unticked
#             test removed, the dropped reason not required and the state
#             check removed must each fail a case.
#
# Needs sh, awk, sed, grep and sort only (no network, no gh, no YAML).
# Usage: tests/check-card-acceptance.sh
set -eu
CDPATH='' cd "$(dirname "$0")/.."
export LC_ALL=C
me=check-card-acceptance
script=scripts/card-acceptance.sh
wf=.github/workflows/card-acceptance.yml
wfpr=.github/workflows/card-acceptance-pr.yml
wfcl=.github/workflows/card-close-labels.yml
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cardacc-check.XXXXXX")
chmod 700 "${tmp:?}"
# A shell error must not exit 0 (bash 3.2 loses the status in an EXIT trap).
trap 'rm -rf "${tmp:?}"; [ "${_ok:-}" = 1 ] || exit 1' EXIT
trap 'exit 1' HUP INT TERM

for f in "$script" "$wf" "$wfpr" "$wfcl"; do
	[ -f "$f" ] || { echo "$me: missing $f" >&2; exit 1; }
done
fails=0
bad()
{
	echo "$me: $*" >&2
	fails=$((fails + 1))
}

# ── parse ───────────────────────────────────────────────────────────────
# expect SCRIPT TAG RC WANT BODY: the check mode on BODY exits RC and prints
# WANT (the line numbers of the criteria with no verdict, space-separated).
expect()
{
	printf '%s\n' "$5" > "$tmp/body"
	rc=0
	sh "$1" check "$tmp/body" > "$tmp/out" 2>&1 || rc=$?
	got=$(sed -n 's/^line \([0-9]*\):.*/\1/p' "$tmp/out" | tr '\n' ' ' | sed 's/ $//')
	if [ "$rc" != "$3" ] || [ "$got" != "$4" ]; then
		[ "$1" = "$script" ] && bad "parse: $2: exit $rc lines '$got', want exit $3 lines '$4'"
		return 1
	fi
	return 0
}
parse_cases()
{
	s=$1 r=0
	expect "$s" all-ticked 0 "" '# FIX — t
## Acceptance criteria
- [x] one
- [X] two' || r=1
	expect "$s" unticked 1 "3" '# FIX — t
## Acceptance criteria
- [ ] one
- [x] two' || r=1
	expect "$s" moved 0 "" '## Acceptance criteria
- [ ] one, moved to #12
- [ ] two — Moved To #3.' || r=1
	expect "$s" moved-no-number 1 "2" '## Acceptance criteria
- [ ] one, moved to #' || r=1
	expect "$s" dropped 0 "" '## Acceptance criteria
- [ ] one — dropped: the owner agreed on 2026-11-25' || r=1
	expect "$s" dropped-empty 1 "2 3" '## Acceptance criteria
- [ ] one — dropped:
- [ ] two — dropped:   ' || r=1
	expect "$s" plain-item 1 "2" '## Acceptance criteria
- plain item
1. [x] numbered is not a task item, moved to #4' || r=1
	expect "$s" continuation 0 "" '## Acceptance criteria
- [ ] one
  goes on, moved to #9
  - plain nested note
- [x] two' || r=1
	expect "$s" nested-task 1 "4" '## Acceptance criteria
- [x] one
  - [x] nested met
  - [ ] nested open' || r=1
	expect "$s" fence 0 "" '## Acceptance criteria
```
- [ ] inside a fence
```
~~~
- [ ] inside a tilde fence
~~~
- [x] one' || r=1
	expect "$s" other-section 0 "" '## Scope
- [ ] a scope item
## Acceptance criteria
- [x] one
## Notes
- [ ] a note' || r=1
	expect "$s" headings 1 "2 4" '### Acceptance
- [ ] level three
#### ACCEPTANCE CRITERIA:
- [ ] level four
## Acceptance tests
- [ ] not an acceptance heading' || r=1
	expect "$s" level-one-ends 0 "" '## Acceptance criteria
- [x] one
# Another title
- [ ] outside' || r=1
	expect "$s" rule-ends 0 "" '## Acceptance criteria
- [x] one
---
- [ ] after the rule' || r=1
	expect "$s" crlf 1 "3" "$(printf '## Acceptance criteria\r\n- [x] one\r\n- [ ] two\r')" || r=1
	expect "$s" no-section 0 "" '# FIX — t
## Goal
- [ ] not acceptance' || r=1
	expect "$s" empty-section 0 "" '## Acceptance criteria
Nothing to accept.' || r=1
	expect "$s" star-plus 1 "4" '## Acceptance criteria
* [x] star
+ [X] plus
* [ ] open star' || r=1
	expect "$s" shell-text 1 "2" '## Acceptance criteria
- [ ] $(touch '"$tmp"'/pwned) `touch '"$tmp"'/pwned2` ; touch '"$tmp"'/pwned3' || r=1
	if [ -e "$tmp/pwned" ] || [ -e "$tmp/pwned2" ] || [ -e "$tmp/pwned3" ]; then
		[ "$s" = "$script" ] && bad "parse: shell-text: text from a card was run"
		r=1
	fi
	return $r
}
parse_cases "$script" || true

# usage errors
for args in "" "check" "check $tmp/none" "refs" "closed 1" "pr" "nope x"; do
	rc=0
	# shellcheck disable=SC2086 # the words are the arguments
	sh "$script" $args > /dev/null 2>&1 || rc=$?
	[ "$rc" = 2 ] || bad "usage: '$args' exits $rc, want 2"
done

# ── refs ────────────────────────────────────────────────────────────────
printf '%s\n' \
	'Closes #12, fixes: #3 and Resolves BeauBender/quake2#7.' \
	'close #1 closed #2 fix #4 fixed #5 resolve #6 resolved #8' \
	'other/repo#9 is not ours; closes other/repo#10' \
	'fixed #11x is no ref; prefix#13; reclose #14' \
	'Closes #12 again' > "$tmp/pr"
got=$(sh "$script" refs "$tmp/pr" BeauBender/quake2 | tr '\n' ' ')
want='1 2 3 4 5 6 7 8 12 '
[ "$got" = "$want" ] || bad "refs: got '$got', want '$want'"
got=$(sh "$script" refs "$tmp/pr" | tr '\n' ' ')
want='1 2 3 4 5 6 8 12 '
[ "$got" = "$want" ] || bad "refs without a repository: got '$got', want '$want'"

# ── stub gh ─────────────────────────────────────────────────────────────
mkdir "$tmp/bin" "$tmp/srv"
cat > "$tmp/bin/gh" <<'EOF'
#!/bin/sh
# Serves $SRV/<n>/{state,body,labels,pr}; logs every call to $SRV/log.
printf '%s\n' "$*" >> "$SRV/log"
if [ ! -t 0 ] && [ -n "$(head -c 1 2>/dev/null)" ]; then
	echo "stdin-read $*" >> "$SRV/log"
fi
case "$1 $2" in
"api repos/"* | "api --paginate")
	path=$2
	[ "$path" = --paginate ] && path=$3
	n=${path##*/}
	case $path in
	*/labels) n=${path%/labels}; n=${n##*/}; cat "$SRV/$n/labels" 2>/dev/null; exit 0 ;;
	*/pulls/*) cat "$SRV/$n/pr"; exit 0 ;;
	esac
	case "$*" in
	*'--jq .state'*) cat "$SRV/$n/state" ;;
	*has*) if [ -e "$SRV/$n/is-pr" ]; then echo true; else echo false; fi ;;
	*'.body'*) cat "$SRV/$n/body" ;;
	*) echo "unexpected api call" >&2; exit 1 ;;
	esac
	;;
"issue comment")
	while [ $# -gt 0 ]; do
		[ "$1" = --body-file ] && cp "$2" "$SRV/comment"
		shift
	done
	;;
"issue reopen" | "issue edit") ;;
*) echo "unexpected gh call: $*" >&2; exit 1 ;;
esac
EOF
chmod +x "$tmp/bin/gh"

# serve N STATE LABELS BODY
serve()
{
	mkdir -p "$tmp/srv/$1"
	printf '%s\n' "$2" > "$tmp/srv/$1/state"
	printf '%s' "$3" | tr ',' '\n' > "$tmp/srv/$1/labels"
	[ -z "$3" ] || echo >> "$tmp/srv/$1/labels"
	printf '%s\n' "$4" > "$tmp/srv/$1/body"
}
open_body='# FIX — t
## Acceptance criteria
- [x] one
- [ ] two @someone `x`'
done_body='# FIX — t
## Acceptance criteria
- [x] one
- [ ] two, moved to #99'
serve 1 closed state:done,id:FIX1 "$open_body"
serve 2 closed state:done "$done_body"
serve 3 open state:review "$open_body"
serve 4 closed "" "$open_body"
serve 5 closed state:promoted,state:done "$open_body"
serve 6 closed state:done "$open_body"
touch "$tmp/srv/6/is-pr"

# run SCRIPT MODE ARGS...: run the script's mode against the stub; sets rc.
run()
{
	s=$1
	shift
	rm -f "$tmp/srv/log" "$tmp/srv/comment"
	: > "$tmp/srv/log"
	rc=0
	PATH="$tmp/bin:$PATH" SRV="$tmp/srv" GH_REPO=o/r sh "$s" "$@" \
		> "$tmp/out" 2>&1 < "$tmp/stdin" || rc=$?
}
echo 'stdin must not be read' > "$tmp/stdin"
logged() { grep -q -- "$1" "$tmp/srv/log"; }

closed_cases()
{
	s=$1 r=0
	run "$s" closed 1 completed
	if [ "$rc" != 1 ] || ! logged '^issue comment 1 --body-file' ||
		! logged '^issue reopen 1$' || ! logged '^issue edit 1 --add-label state:review$' ||
		! logged '^issue edit 1 --remove-label state:done$'; then
		[ "$s" = "$script" ] && bad "closed: open criterion: exit $rc, log: $(tr '\n' '|' < "$tmp/srv/log")"
		r=1
	fi
	if [ "$s" = "$script" ]; then
		grep -q 'line 4: - \[ \] two' "$tmp/srv/comment" 2>/dev/null ||
			bad "closed: the comment does not name the criterion"
		if grep -q '`x`' "$tmp/srv/comment" 2>/dev/null; then
			bad "closed: the comment keeps a backtick from the card"
		fi
	fi
	for c in "2 completed" "1 not_planned" "1 duplicate" "3 completed" "4 completed" "5 completed" "6 completed"; do
		# shellcheck disable=SC2086 # the words are the arguments
		run "$s" closed $c
		if [ "$rc" != 0 ] || logged '^issue '; then
			[ "$s" = "$script" ] && bad "closed: $c: exit $rc, log: $(tr '\n' '|' < "$tmp/srv/log")"
			r=1
		fi
	done
	run "$s" closed 1x completed
	if [ "$rc" != 2 ] || [ -s "$tmp/srv/log" ]; then
		[ "$s" = "$script" ] && bad "closed: a bad number: exit $rc, gh called"
		r=1
	fi
	if grep -q '^stdin-read' "$tmp/srv/log"; then
		[ "$s" = "$script" ] && bad "closed: a gh call read stdin"
		r=1
	fi
	return $r
}
closed_cases "$script" || true

mkdir -p "$tmp/srv/50" "$tmp/srv/51" "$tmp/srv/52"
printf 'Closes #1\nFixes #2\n' > "$tmp/srv/50/pr"
printf 'Closes #2, closes #4 and #6 fixes #6\n' > "$tmp/srv/51/pr"
printf 'No closing keyword: #1\n' > "$tmp/srv/52/pr"
run "$script" pr 50
{ [ "$rc" = 1 ] && grep -q '^#1: acceptance criteria with no verdict' "$tmp/out" &&
	grep -q '^#2: every acceptance criterion has a verdict' "$tmp/out"; } ||
	bad "pr: an open criterion: exit $rc, $(tr '\n' '|' < "$tmp/out")"
run "$script" pr 51
[ "$rc" = 0 ] || bad "pr: verdicts, a non-card and a pull request: exit $rc, $(tr '\n' '|' < "$tmp/out")"
run "$script" pr 52
[ "$rc" = 0 ] || bad "pr: closes nothing: exit $rc"
logged '^issue ' && bad "pr: an issue was edited"

# ── workflow ────────────────────────────────────────────────────────────
# lines FILE: the file without comment lines.
lines() { grep -v '^[[:space:]]*#' "$1"; }
has() { lines "$1" | grep -Fxq -- "$2" || bad "workflow: $1 lacks the line '$2'"; }
has "$wf" 'on:'
has "$wf" '  issues:'
has "$wf" '    types: [closed]'
has "$wf" "    if: github.actor == github.repository_owner"
has "$wf" '        scripts/card-acceptance.sh closed "$N" "$REASON"'
has "$wfpr" '  pull_request:'
has "$wfpr" '    types: [opened, edited, synchronize, reopened]'
has "$wfpr" '        scripts/card-acceptance.sh pr "$N"'
for f in "$wf" "$wfpr"; do
	has "$f" 'permissions: {}'
	has "$f" '      contents: read'
	has "$f" '        persist-credentials: false'
	has "$f" '      shell: sh'
	lines "$f" | grep -q 'contents: write' && bad "workflow: $f lets a job write contents"
	lines "$f" | grep -q 'pull_request_target' && bad "workflow: $f runs on pull_request_target"
	[ "$(lines "$f" | grep -c '^on:')" = 1 ] || bad "workflow: $f has not one on:"
	[ "$(lines "$f" | grep -c 'run: |$')" = 1 ] || bad "workflow: $f has not one run block"
	# The run block: the lines after "run: |" indented deeper than it.
	awk '/^ *run: \|$/ { ind = index($0, "r"); inrun = 1; next }
		inrun { match($0, /^ */); if ($0 != "" && RLENGTH < ind) inrun = 0; else print }' \
		"$f" > "$tmp/run"
	grep -q '\${{' "$tmp/run" && bad "workflow: $f expands an expression in its run block"
	[ "$(grep -c '[^[:space:]]' "$tmp/run")" = 1 ] || bad "workflow: $f's run block is not one line"
done
on_pr=$(lines "$wf" | sed -n '/^on:/,/^[a-z]/p' | grep -c 'pull_request\|push\|workflow_dispatch\|schedule' || true)
[ "$on_pr" = 0 ] || bad "workflow: $wf runs on more than issues closed"
cl_name=$(sed -n 's/^name: //p' "$wfcl")
lines "$wfcl" | grep -Fxq '  group: ${{ github.workflow }}-${{ github.event.issue.number }}' ||
	bad "workflow: $wfcl's concurrency group is not the one this check expects"
has "$wf" "  group: $cl_name-\${{ github.event.issue.number }}"

# ── mutants ─────────────────────────────────────────────────────────────
# mutant TAG SED-EXPR CASES: a copy of the script edited by SED-EXPR must
# fail CASES (parse_cases or closed_cases).
mutant()
{
	sed "$2" "$script" > "$tmp/mutant.sh"
	if cmp -s "$script" "$tmp/mutant.sh"; then
		bad "mutant $1: the edit changed nothing"
		return
	fi
	if "$3" "$tmp/mutant.sh"; then
		bad "mutant $1: not caught"
	fi
}
mutant no-reopen '/gh issue reopen/d' closed_cases
mutant no-unticked 's/if (ticked) return 1/return 1/' parse_cases
mutant no-dropped-reason 's|dropped:\[ \\t\]\*\[^ \\t\]|dropped:|' parse_cases
mutant no-state-check 's/\[ "\$st" = closed \] ||/true ||/' closed_cases
mutant no-fence '/^	\/\^\[ \\t\]\*(```|~~~)\/ {$/,/^	}$/d' parse_cases

if [ "$fails" -ne 0 ]; then
	echo "$me: $fails failure(s)" >&2
	exit 1
fi
echo "$me: ok"
_ok=1
