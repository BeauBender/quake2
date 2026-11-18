#!/bin/sh
# Invariant: .github/workflows/card-close-labels.yml, which gives a closed
# card issue its terminal state label, keeps the shape that makes it safe,
# and its label decision does what docs/runbook/FIX108.md says.
#
# The workflow file is read with awk alone (no YAML library, no network, no
# gh). The lint is an allow-list: what it cannot place is refused. It must
# have:
#   trigger      on: issues: types: [closed], and nothing else;
#   top-perms    a top-level permissions: {} and no other top-level one;
#   job-perms    every job's permissions exactly issues: write, and no
#                permissions key anywhere else;
#   uses         no uses key in any spelling (quoted, in a flow mapping, on
#                any line but a comment): no action at all;
#   expr         every ${{ }} in the file, comments and run blocks included,
#                exactly one of ${{ github.workflow }},
#                ${{ github.event.issue.number }},
#                ${{ github.event.issue.state_reason }}, ${{ github.token }}
#                and ${{ github.repository }}, on an env: value line or the
#                concurrency group line (so no github[...], no toJSON, no
#                title, body, labels or user, nothing in a run block);
#   run          exactly one run block, its value exactly | or |- (no plain,
#                quoted or folded scalar, no anchor);
#   shell        exactly one shell:, and it is sh;
#   flow         outside the run block no flow collection but
#                permissions: {} and types: [closed];
#   anchor       outside the run block no anchor, alias or tag (& * !);
#   key          outside the run block every line a comment, or a plain
#                unquoted key and its value;
#   container    no container: and no services: key;
#   if, owner    no if: but the job's, which is exactly
#                if: github.actor == github.repository_owner;
#   state-read   the run block reads the issue state;
#   timeout      a timeout-minutes on every job;
#   runner       every job on ubuntu-latest (a free standard runner);
#   concurrency  a concurrency group keyed by github.event.issue.number;
#   labels       state:backlog, state:staged, state:in-progress,
#                state:review and state:done all named.
# Each assertion has at least one mutant: a scratch copy of the workflow,
# edited, that the check must refuse under that assertion's tag. A mutant
# that leaves the file unchanged fails the check. A state:* label added to
# the repository later must be added to decide and to the lint's list.
#
# The whole run block is cut out of the workflow (so the copy tested is
# the copy that runs), checked with /bin/sh -n, and run with /bin/sh -e (as
# Actions runs shell: sh) against a stub gh first on PATH, which logs its
# arguments and serves the labels and the issue state. Cases: a number that
# is not one (exit 1, no gh call); completed with backlog, with all four
# non-terminal labels; not_planned; reopened since (exit 0, no edit); a
# failed removal whose label stays (exit 1) or is gone (exit 0); a failed
# add (exit 1). No gh call may read its stdin. Run-block mutants delete the
# number guard, the state check, the removal re-read and its test, the
# stdin redirect and a fi, and must each fail a case (tag run-block).
#
# The label decision is the decide function of the workflow's run block,
# between the "card-close-labels: decide begin" and "end" marker lines. It
# is cut out and run with /bin/sh -e against a labels file per case:
# completed with backlog, with review, with promoted and backlog, with all
# four non-terminal labels; not_planned with backlog; duplicate and an
# empty reason; no state label; already done; look-alike label names; label
# names holding shell syntax, which must be compared and never run.
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
# It is an allow-list: a line it cannot place is refused.
lint()
{
	awk -v q="'" '
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
		ne = split("github.workflow github.event.issue.number github.event.issue.state_reason github.token github.repository", ex, " ")
		for (i = 1; i <= ne; i++)
			okexpr[" " ex[i] " "] = 1
		uses_re = "(^|[^A-Za-z0-9_-])[\"" q "]?uses[\"" q "]?[ \t]*:"
		quoted_re = "^[\"" q "]"
		owner = "    if: github.actor == github.repository_owner"
		state = "st=$(gh api \"repos/$GH_REPO/issues/$N\" --jq .state)"
		runind = -1
		envind = -1
	}
	{
		line = $0
		n = ind(line)
		blank = (strip(line) == "")
		inrun = 0
		if (runind >= 0) {
			if (line ~ /^[ \t]*$/ || n > runind)
				inrun = 1
			else
				runind = -1
		}
		if (!inrun && !blank && envind >= 0 && n <= envind)
			envind = -1
		inenv = (!inrun && !blank && envind >= 0)
		k = ""

		# Checks on every line, run blocks included.
		if (line !~ /^[ \t]*#/ && line ~ uses_re)
			fail("uses", "a uses key")
		for (i = 1; i <= nl; i++)
			if (named(line, lab[i]))
				seen[lab[i]] = 1
		if (inrun) {
			t = line
			sub(/^[ \t]+/, "", t)
			if (t == state)
				state_seen = 1
		} else if (!blank) {
			# A structural line is a plain key and its value, nothing else.
			s = line
			sub(/(^|[ \t])#.*$/, "", s)
			sub(/[ \t]+$/, "", s)
			match(s, /^ *(-[ \t]+)?/)
			kcol = RLENGTH
			t = substr(s, kcol + 1)
			f = t
			gsub(/\$\{\{[^}]*\}\}/, "", f)
			if (f ~ /[][{}]/ && t != "permissions: {}" && t != "types: [closed]")
				fail("flow", "a flow collection")
			if (f ~ /[&*!]/)
				fail("anchor", "an anchor, alias or tag")
			if (t !~ /^[A-Za-z0-9_-]+[ \t]*:([ \t]|$)/) {
				fail("key", t ~ quoted_re ? "a quoted key" : "not a plain key line")
			} else {
				k = t
				sub(/[ \t]*:.*$/, "", k)
				v = t
				sub(/^[^:]*:[ \t]*/, "", v)
			}
			if (n == 0)
				top = k
			if (k == "container" || k == "services")
				fail("container", "a " k " key")
			if (k == "run") {
				if (v != "|" && v != "|-") {
					fail("run", "a run value that is not | or |-")
				} else {
					runind = kcol
					nrun++
				}
			}
			if (k == "env" && v == "")
				envind = kcol
			if (k == "shell") {
				nshell++
				if (v != "sh")
					fail("shell", "a shell that is not sh")
			}
			if (k == "if") {
				if (top == "jobs" && n == 4 && line == owner)
					ownerok[job] = 1
				else
					fail("if", "an if other than the owner check of a job")
			}
		}

		# Every expression in the file, comments and run blocks included:
		# one of the five, on an env: value line or the concurrency group.
		isgroup = (!inrun && top == "concurrency" && n == 2 && k == "group")
		s = line
		while ((i = index(s, "${{")) > 0) {
			s = substr(s, i + 3)
			j = index(s, "}}")
			if (!j) {
				fail("expr", "an expression with no }} on its line")
				break
			}
			e = substr(s, 1, j - 1)
			s = substr(s, j + 2)
			if (!(e in okexpr))
				fail("expr", "${{" e "}} is not an allowed expression")
			else if (!inenv && !isgroup)
				fail("expr", "an expression outside env: and the concurrency group")
			else if (isgroup && e == " github.event.issue.number ")
				group_ok = 1
		}
		if (inrun || blank)
			next

		# The structure, outside run blocks.
		if (n == 0) {
			if (k == "on") {
				on_seen++
				on_inline = val(line)
			} else if (k == "permissions") {
				tp_seen++
				tp_val = val(line)
			}
			next
		}
		if (top == "on")
			on_body = on_body "|" strip(line)
		else if (top == "permissions")
			fail("top-perms", "the top-level permissions has a block")

		if (line ~ /^[ \t]*(-[ \t]+)?permissions[ \t]*:/ && !(top == "jobs" && n == 4))
			fail("job-perms", "a permissions key outside a job")
		if (top != "jobs")
			next
		if (n == 2) {
			job = k
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
			if (!ownerok[k])
				endfail("owner", "job " k ": no if: github.actor == github.repository_owner")
		}
		if (nrun != 1)
			endfail("run", "not exactly one run block")
		if (nshell != 1)
			endfail("shell", "not exactly one shell: sh")
		if (!state_seen)
			endfail("state-read", "the run block does not read the issue state")
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
		return 0
	fi
	if [ "$2" = run-block ]; then
		mo=$(runcases "$3") || :
	else
		mo=$(lint "$3") || :
	fi
	if [ -z "$mo" ]; then
		bad "mutant $1 passes"
	elif ! printf '%s\n' "$mo" | grep -q "^$2: "; then
		bad "mutant $1 is not refused under $2: $mo"
	fi
}

# cutrun FILE: the run block of FILE, the lines after its first "run: |"
# key up to the first line indented no deeper than that key, with the
# block's own indentation taken off.
cutrun()
{
	awk '
	function ind(s) { match(s, /^ */); return RLENGTH }
	on && $0 !~ /^[ \t]*$/ && ind($0) <= key { exit }
	on {
		if (cut < 0 && $0 !~ /^[ \t]*$/)
			cut = ind($0)
		print (cut < 0 ? "" : substr($0, cut + 1))
	}
	!on && $0 ~ /^[ \t]*(-[ \t]+)?run[ \t]*:[ \t]*\|-?[ \t]*$/ {
		on = 1
		cut = -1
		key = ind($0) + ($0 ~ /^[ \t]*-[ \t]/ ? 2 : 0)
	}' "$1"
}

# The stub gh for the run block: it logs its arguments, and any line it
# can read from stdin, to $GHLOG; gh api serves the labels file $GHLABELS
# for a labels path and the issue state $GHSTATE for the issue itself; gh
# issue edit fails as $GHFAIL says: add (the add fails), kept (a removal
# fails and the label stays), gone (a removal fails, the label is gone).
mkdir "${tmp:?}/bin"
printf '%s\n' '#!/bin/sh' \
	'printf "%s\n" "$*" >> "$GHLOG"' \
	'if read -r x; then printf "stdin %s\n" "$x" >> "$GHLOG"; fi' \
	'case $1 in' \
	'api)' \
	'	case "$*" in' \
	'	*/labels*) cat "$GHLABELS" ;;' \
	'	*) printf "%s\n" "$GHSTATE" ;;' \
	'	esac' \
	'	;;' \
	'issue)' \
	'	case $4:$GHFAIL in' \
	'	--add-label:add | --remove-label:kept) exit 1 ;;' \
	'	--remove-label:gone)' \
	'		grep -Fxv -- "$5" "$GHLABELS" > "$GHLABELS.new" || :' \
	'		mv "$GHLABELS.new" "$GHLABELS"' \
	'		exit 1' \
	'		;;' \
	'	esac' \
	'	;;' \
	'esac' > "${tmp:?}/bin/gh"
chmod +x "${tmp:?}/bin/gh"

# rcase NAME STATUS EDITS STATE FAIL N REASON LABEL...: the run block, run
# with /bin/sh -e as Actions runs it, for issue N closed with REASON, now
# in STATE, with these labels and the stub failing as FAIL says (- for
# never), exits STATUS, and its gh issue edit calls, joined with ';', are
# EDITS ("none": no gh call at all). No gh call may read its stdin (the
# decisions file). Prints one "run-block: " line per failure.
rcase()
{
	nr=$((nr + 1))
	name=$1 want=$2 wedits=$3 state=$4 fail=$5 num=$6 reason=$7
	shift 7
	c=${tmp:?}/case
	rm -rf "${c:?}"
	mkdir "${c:?}" "${c:?}/rt"
	: > "${c:?}/labels"
	for l in "$@"; do
		printf '%s\n' "$l" >> "${c:?}/labels"
	done
	: > "${c:?}/log"
	[ "$fail" != - ] || fail=
	rst=0
	PATH="${tmp:?}/bin:$PATH" GHLOG="${c:?}/log" GHLABELS="${c:?}/labels" \
		GHSTATE="$state" GHFAIL="$fail" RUNNER_TEMP="${c:?}/rt" GH_REPO=o/r \
		N="$num" REASON="$reason" /bin/sh -e "${tmp:?}/run.sh" \
		< /dev/null > "${c:?}/out" 2>&1 || rst=$?
	edits=$(grep '^issue edit' "${c:?}/log" | tr '\n' ';') || :
	if [ "$rst" -ne "$want" ]; then
		echo "run-block: case $name: exit status $rst, want $want: $(tr '\n' ' ' < "${c:?}/out")"
	fi
	if [ "$wedits" = none ]; then
		if [ -s "${c:?}/log" ]; then
			echo "run-block: case $name: gh was called: $(tr '\n' ';' < "${c:?}/log")"
		fi
	elif [ "$edits" != "$wedits" ]; then
		echo "run-block: case $name: edits '$edits', want '$wedits'"
	fi
	if grep -q '^stdin ' "${c:?}/log"; then
		echo "run-block: case $name: a gh call read its stdin"
	fi
}

# runcases FILE: the run block of FILE passes sh -n and every case.
runcases()
{
	cutrun "$1" > "${tmp:?}/run.sh"
	if ! grep -q 'card-close-labels: decide begin' "${tmp:?}/run.sh"; then
		echo "run-block: no run block holding decide"
		return 0
	fi
	if ! e=$(/bin/sh -n "${tmp:?}/run.sh" 2>&1); then
		echo "run-block: sh -n: $e"
		return 0
	fi
	e7='issue edit 7'
	rcase bad-number 1 none closed - '7;id' completed state:backlog
	rcase completed 0 "$e7 --add-label state:done;$e7 --remove-label state:backlog;" \
		closed - 7 completed state:backlog gate:auto
	rcase completed-all 0 "$e7 --add-label state:done;$e7 --remove-label state:backlog;$e7 --remove-label state:staged;$e7 --remove-label state:in-progress;$e7 --remove-label state:review;" \
		closed - 7 completed state:review state:in-progress state:staged state:backlog
	rcase not-planned 0 "$e7 --remove-label state:staged;$e7 --remove-label state:review;" \
		closed - 7 not_planned state:review state:staged
	rcase reopened 0 '' open - 7 completed state:backlog
	rcase remove-kept 1 "$e7 --add-label state:done;$e7 --remove-label state:backlog;" \
		closed kept 7 completed state:backlog
	rcase remove-gone 0 "$e7 --add-label state:done;$e7 --remove-label state:backlog;" \
		closed gone 7 completed state:backlog
	rcase add-fails 1 "$e7 --add-label state:done;" closed add 7 completed state:backlog
}

nr=0
runcases "$wf" > "${tmp:?}/runcases.out" || :
while IFS= read -r l; do
	bad "$wf: $l"
done < "${tmp:?}/runcases.out"

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
msub title expr 's/github\.event\.issue\.state_reason/github.event.issue.title/'
msub body expr 's/github\.event\.issue\.state_reason/github.event.issue.body/'
msub label-list expr 's/github\.event\.issue\.state_reason/github.event.issue.labels/'
msub user expr 's/github\.event\.issue\.state_reason/github.event.issue.user.login/'
msub tojson expr 's/github\.event\.issue\.state_reason/toJSON(github.event)/'
mafter run-line expr '# card-close-labels: decide begin' \
	'        echo "${{ github.event.issue.number }}"'
mafter run-comment expr '# card-close-labels: decide begin' \
	'        # ${{ github.token }}'
msub no-timeout timeout '/^    timeout-minutes:/d'
msub macos runner 's/^    runs-on: ubuntu-latest/    runs-on: macos-latest/'
msub group concurrency 's/^\(  group:.*\)github\.event\.issue\.number/\1github.run_id/'
msub review labels 's/state:review/state:reveiw/g'
msub done labels 's/state:done/state:closed/g'
# Review round 1: the same things in other valid YAML and expression
# spellings, each refused by the allow-lists.
st=' - name: Set the terminal state label'
mafter uses-flow uses '^    steps:' '    - {name: x, uses: actions/checkout@v4}'
mafter uses-quoted uses '^    steps:' '    - "uses": actions/checkout@v4'
mafter index expr '^        N: ' "        T: \${{ github['event']['issue']['title'] }}"
mafter index-mixed expr '^        N: ' "        T: \${{ github['event'].issue.title }}"
mafter tojson-all expr '^        N: ' '        T: ${{ toJSON(github) }}'
msub expr-space expr 's/\${{ github\.token }}/${{github.token}}/'
msub expr-place expr 's/^    name: Labels/    name: ${{ github.repository }}/'
mafter flow-run flow '^    steps:' \
	"    - {name: y, run: \"echo \${{ github['event'].issue.body }}\"}"
msub flow-seq flow 's/^    runs-on: ubuntu-latest/    runs-on: [ubuntu-latest]/'
mafter plain-run run '^    steps:' \
	"    - name: y\n      run:\n        echo \"\${{ github['event']['issue']['title'] }}\""
mafter anchor-run run '^    steps:' \
	'    - name: z\n      run: &s |\n        echo ${{ github.token }}'
mafter second-run run '^    steps:' '    - name: w\n      run: |\n        true'
mafter alias anchor '^    timeout-minutes:' '    environment: *e'
mafter container container '^    timeout-minutes:' '    container: example/image:latest'
mafter services container '^    timeout-minutes:' '    services:\n      db:\n        image: example/db'
mafter quoted-run key '^    steps:' \
	"    - name: q\n      \"run\": |\n        echo \${{ github['event'].issue.title }}"
msub owner-missing owner '/^    if: /d'
msub owner-always owner 's/^    if: .*/    if: always()/'
mafter step-if if "^   $st\$" "      if: github.event.issue.title != ''"
msub shell-bash shell 's/^      shell: sh$/      shell: bash/'
msub no-state-read state-read '/^        st=\$(gh api /d'
# The run block's own steps, refused by its cases.
msub no-guard run-block '/^        case \$N in$/,/^        esac$/d'
msub no-state-check run-block '/^        \[ "\$st" = closed \]/d'
msub no-reread run-block '/^              gh api --paginate/d'
msub reread-skipped run-block \
	's/^\(              \)if grep -Fxq -- "\$label" "\$labels"; then/\1if false; then/'
msub stdin run-block 's|--remove-label "\$label" </dev/null|--remove-label "$label"|'
msub syntax run-block '/^              fi$/d'

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
	if got=$(cd "${tmp:?}" && /bin/sh -ec '. ./decide.sh && decide "$1" labels' \
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
echo "$me: ok (lint, $nm mutants, $nd decisions, $nr run-block cases)"
