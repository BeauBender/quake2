#!/bin/sh
# Invariant: .github/workflows/card-close-labels.yml, which gives a closed
# card issue its terminal state label, keeps the shape that makes it safe,
# and its label decision does what docs/runbook/FIX108.md says.
#
# The workflow file is read with awk alone (no YAML library, no network, no
# gh). It must have:
#   trigger      on: issues: types: [closed], and nothing else;
#   top-perms    a top-level permissions: {} and no other top-level one;
#   job-perms    every job's permissions exactly issues: write, and no
#                permissions key anywhere else;
#   uses         no uses: line (no action at all, not even a first-party one);
#   event-expr   no github.event reference but github.event.issue.number and
#                github.event.issue.state_reason (so no issue title, body,
#                labels or user, and no toJSON(github.event));
#   run-expr     no ${{ }} expression inside any run: block, a comment in it
#                included (Actions expands those too): values come in
#                through env: only;
#   timeout      a timeout-minutes on every job;
#   runner       every job on ubuntu-latest (a free standard runner);
#   concurrency  a concurrency group keyed by github.event.issue.number;
#   labels       state:backlog, state:staged, state:in-progress,
#                state:review and state:done all named.
# Each assertion has at least one mutant: a scratch copy of the workflow,
# edited, that the check must refuse under that assertion's tag. A mutant
# that leaves the file unchanged fails the check.
#
# The label decision is the decide function of the workflow's run block,
# between the "card-close-labels: decide begin" and "end" marker lines. It
# is cut out of the workflow (so the copy tested is the copy that runs) and
# run with /bin/sh against a labels file per case: completed with backlog,
# with review, with promoted and backlog, with all four non-terminal labels;
# not_planned with backlog; duplicate and an empty reason; no state label;
# already done; look-alike label names; label names holding shell syntax,
# which must be compared and never run.
#
# Usage: tests/check-card-close-labels.sh [workflow-file]
set -eu
CDPATH='' cd "$(dirname "$0")/.."
export LC_ALL=C
me=check-card-close-labels
wf=${1:-.github/workflows/card-close-labels.yml}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cardclose.XXXXXX")
chmod 700 "${tmp:?}"
# A shell error must not exit 0 (bash 3.2 loses the status in an EXIT trap).
trap 'rm -rf "${tmp:?}"; [ "${_ok:-}" = 1 ] || exit 1' EXIT
trap 'exit 1' HUP INT TERM

[ -f "$wf" ] || { echo "$me: missing $wf" >&2; exit 1; }
fails=0 nm=0 nd=0
bad()
{
	echo "$me: $*" >&2
	fails=$((fails + 1))
}

# lint FILE: one "tag: reason" line per violation on stdout; status 1 if any.
lint()
{
	awk '
	function ind(s) { match(s, /^ */); return RLENGTH }
	function strip(s) { sub(/(^|[ \t])#.*$/, "", s); gsub(/[ \t]/, "", s); return s }
	function val(s) { sub(/^[^:]*:/, "", s); return strip(s) }
	function fail(tag, why) { printf "%s: line %d: %s\n", tag, NR, why; bad = 1 }
	function endfail(tag, why) { printf "%s: %s\n", tag, why; bad = 1 }
	function named(s, l,   i, c) {
		while ((i = index(s, l)) > 0) {
			c = substr(s, i + length(l), 1)
			if (c !~ /[a-z0-9_-]/)
				return 1
			s = substr(s, i + length(l))
		}
		return 0
	}
	BEGIN {
		nl = split("state:backlog state:staged state:in-progress state:review state:done", lab, " ")
		runind = -1
	}
	{
		line = $0
		n = ind(line)
		blank = (strip(line) == "")
		inrun = 0
		if (runind >= 0) {
			if (blank || n > runind)
				inrun = 1
			else
				runind = -1
		}

		# Checks on every line, run blocks included.
		s = line
		while (match(s, /github\.event[A-Za-z0-9_.]*/)) {
			ref = substr(s, RSTART, RLENGTH)
			if (ref != "github.event.issue.number" && ref != "github.event.issue.state_reason")
				fail("event-expr", "the workflow reads " ref)
			s = substr(s, RSTART + RLENGTH)
		}
		if (line ~ /^[ \t]*(-[ \t]+)?uses[ \t]*:/)
			fail("uses", "a uses: line")
		for (i = 1; i <= nl; i++)
			if (named(line, lab[i]))
				seen[lab[i]] = 1
		if (inrun) {
			if (index(line, "${{"))
				fail("run-expr", "an expression inside a run block")
			next
		}
		if (blank)
			next

		# The structure, outside run blocks.
		if (line ~ /^[ \t]*(-[ \t]+)?run[ \t]*:/) {
			v = line
			sub(/^[ \t]*(-[ \t]+)?run[ \t]*:[ \t]*/, "", v)
			if (v ~ /^[|>]/)
				runind = n + (line ~ /^[ \t]*-[ \t]/ ? 2 : 0)
			else if (index(v, "${{"))
				fail("run-expr", "an expression inside a run line")
		}
		if (n == 0) {
			top = line
			sub(/[ \t]*:.*$/, "", top)
			if (top == "on") {
				on_seen++
				on_inline = val(line)
			} else if (top == "permissions") {
				tp_seen++
				tp_val = val(line)
			}
			next
		}
		if (top == "on")
			on_body = on_body "|" strip(line)
		else if (top == "permissions")
			fail("top-perms", "the top-level permissions has a block")
		else if (top == "concurrency" && line ~ /^[ \t]*group[ \t]*:/ && index(line, "github.event.issue.number"))
			group_ok = 1

		if (line ~ /^[ \t]*(-[ \t]+)?permissions[ \t]*:/ && !(top == "jobs" && n == 4))
			fail("job-perms", "a permissions key outside a job")
		if (top != "jobs")
			next
		if (n == 2) {
			job = strip(line)
			sub(/:.*$/, "", job)
			jobs[++nj] = job
			inperm = 0
			next
		}
		if (n <= 4)
			inperm = 0
		if (n == 4 && line ~ /^    permissions[ \t]*:/) {
			jp_seen[job]++
			jp_inline[job] = val(line)
			inperm = 1
		} else if (n == 4 && line ~ /^    timeout-minutes[ \t]*:[ \t]*[1-9][0-9]*[ \t]*$/) {
			timeout[job] = 1
		} else if (n == 4 && line ~ /^    runs-on[ \t]*:/) {
			runs_on[job] = val(line)
		} else if (inperm && n > 4) {
			jp_body[job] = jp_body[job] "|" strip(line)
		}
	}
	END {
		if (on_seen != 1 || on_inline != "" || on_body != "|issues:|types:[closed]")
			endfail("trigger", "the trigger is not exactly issues: types: [closed]")
		if (tp_seen != 1 || tp_val != "{}")
			endfail("top-perms", "the top-level permissions is not exactly {}")
		if (nj == 0)
			endfail("job-perms", "no job")
		for (j = 1; j <= nj; j++) {
			k = jobs[j]
			if (jp_seen[k] != 1 || jp_inline[k] != "" || jp_body[k] != "|issues:write")
				endfail("job-perms", "job " k ": permissions is not exactly issues: write")
			if (!timeout[k])
				endfail("timeout", "job " k ": no timeout-minutes")
			if (runs_on[k] != "ubuntu-latest")
				endfail("runner", "job " k ": not on ubuntu-latest")
		}
		if (!group_ok)
			endfail("concurrency", "no concurrency group keyed by the issue number")
		for (i = 1; i <= nl; i++)
			if (!seen[lab[i]])
				endfail("labels", lab[i] " is not named")
		exit bad
	}' "$1"
}

if out=$(lint "$wf"); then
	:
else
	printf '%s\n' "$out" | while IFS= read -r l; do
		echo "$me: $wf: $l" >&2
	done
	fails=$((fails + 1))
fi

# mutant NAME TAG FILE: FILE is a mutant of the workflow, which the check
# must refuse under TAG.
mutant()
{
	nm=$((nm + 1))
	if cmp -s "$wf" "$3"; then
		bad "mutant $1 changes nothing"
	elif mo=$(lint "$3"); then
		bad "mutant $1 passes"
	elif ! printf '%s\n' "$mo" | grep -q "^$2: "; then
		bad "mutant $1 is not refused under $2: $mo"
	fi
}

# msub NAME TAG SED-EXPRESSION
msub()
{
	sed -e "$3" "$wf" > "${tmp:?}/mutant"
	mutant "$1" "$2" "${tmp:?}/mutant"
}

# mafter NAME TAG REGEX TEXT: TEXT inserted after the first line that
# matches REGEX.
mafter()
{
	awk -v re="$3" -v add="$4" '{ print } !done && $0 ~ re { print add; done = 1 }' \
		"$wf" > "${tmp:?}/mutant"
	mutant "$1" "$2" "${tmp:?}/mutant"
}

msub reopened trigger 's/types: \[closed\]/types: [closed, reopened]/'
msub event trigger 's/^  issues:/  issue_comment:/'
mafter target trigger '^on:' '  pull_request_target:'
msub top-write-all top-perms 's/^permissions: {}/permissions: write-all/'
msub top-missing top-perms '/^permissions: {}/d'
mafter job-contents job-perms '^      issues: write' '      contents: read'
msub job-read job-perms 's/^      issues: write/      issues: read/'
msub job-write-all job-perms 's/^    permissions:.*/    permissions: write-all/'
mafter checkout uses '^    steps:' '    - uses: actions/checkout@v7'
mafter run-uses uses '^    steps:' '    - name: x\n      uses: ./local'
msub title event-expr 's/github\.event\.issue\.state_reason/github.event.issue.title/'
msub body event-expr 's/github\.event\.issue\.state_reason/github.event.issue.body/'
msub label-list event-expr 's/github\.event\.issue\.state_reason/github.event.issue.labels/'
msub user event-expr 's/github\.event\.issue\.state_reason/github.event.issue.user.login/'
msub tojson event-expr 's/github\.event\.issue\.state_reason/toJSON(github.event)/'
mafter run-line run-expr '# card-close-labels: decide begin' \
	'        echo "${{ github.event.issue.number }}"'
mafter run-comment run-expr '# card-close-labels: decide begin' \
	'        # ${{ github.token }}'
msub no-timeout timeout '/^    timeout-minutes:/d'
msub macos runner 's/^    runs-on: ubuntu-latest/    runs-on: macos-latest/'
msub group concurrency 's/^\(  group:.*\)github\.event\.issue\.number/\1github.run_id/'
msub review labels 's/state:review/state:reveiw/g'
msub done labels 's/state:done/state:closed/g'

# The label decision, cut out of the run block with its indentation.
awk '
	/# card-close-labels: decide begin/ { on = 1; match($0, /^ */); n = RLENGTH }
	on { print substr($0, n + 1) }
	/# card-close-labels: decide end/ { on = 0 }
' "$wf" > "${tmp:?}/decide.sh"
if ! grep -q '^decide()' "${tmp:?}/decide.sh"; then
	bad "$wf: no decide function between the marker lines"
fi

# decision NAME REASON EXPECTED LABEL...: decide with REASON and these
# labels prints EXPECTED (lines joined with ';') and exits 0.
decision()
{
	nd=$((nd + 1))
	name=$1 reason=$2 want=$3
	shift 3
	: > "${tmp:?}/labels"
	for l in "$@"; do
		printf '%s\n' "$l" >> "${tmp:?}/labels"
	done
	if got=$(cd "${tmp:?}" && /bin/sh -c '. ./decide.sh && decide "$1" labels' \
		decide "$reason" 2>&1); then
		got=$(printf '%s' "$got" | tr '\n' ';')
		[ "$got" = "$want" ] || bad "decision $name: got '$got', want '$want'"
	else
		bad "decision $name: exit status $?: $got"
	fi
}

done_add='--add-label state:done'
decision completed-backlog completed "$done_add;--remove-label state:backlog" \
	state:backlog gate:auto id:FIX1
decision completed-review completed "$done_add;--remove-label state:review" \
	state:review
decision completed-promoted completed '--remove-label state:backlog' \
	state:promoted state:backlog
decision completed-all completed "$done_add;--remove-label state:backlog;--remove-label state:staged;--remove-label state:in-progress;--remove-label state:review" \
	state:review state:in-progress state:staged state:backlog
decision not-planned not_planned '--remove-label state:backlog' \
	state:backlog id:INFORM5
decision duplicate duplicate '--remove-label state:staged;--remove-label state:in-progress' \
	state:in-progress state:staged
decision empty-reason '' '--remove-label state:backlog' state:backlog
decision no-state completed '' gate:auto epic:x
decision no-state-not-planned not_planned '' gate:auto
decision no-labels completed ''
decision already-done completed '' state:done gate:auto
decision done-and-backlog completed '--remove-label state:backlog' \
	state:done state:backlog
decision look-alike not_planned '' \
	state:backlog-old xstate:review 'state:staged x' ' state:in-progress'
# shellcheck disable=SC2016
decision shell-syntax completed "$done_add;--remove-label state:backlog" \
	state:backlog '$(touch pwned)' '`touch pwned`' 'state:x;touch pwned'
[ ! -e "${tmp:?}/pwned" ] || bad "decision shell-syntax: a label name was run"

if [ "$fails" -ne 0 ]; then
	echo "$me: $fails failure(s)" >&2
	exit 1
fi
_ok=1
echo "$me: ok (lint, $nm mutants, $nd decisions)"
