#!/bin/sh
# Invariant: every reference a workflow in .github/workflows/ pulls from
# outside this repository is pinned to an immutable id.
#
# STUB: scan() below accepts everything; the real scan follows.
#
# Usage: tests/check-workflow-pins.sh               (self-test, then the tree)
#        tests/check-workflow-pins.sh --selftest    (the self-test alone)
set -eu
CDPATH='' cd "$(dirname "$0")/.."
export LC_ALL=C
me=check-workflow-pins

case $# in
0) mode=tree ;;
1)
	case $1 in
	--selftest) mode=selftest ;;
	*) echo "usage: $0 [--selftest]" >&2; exit 2 ;;
	esac
	;;
*) echo "usage: $0 [--selftest]" >&2; exit 2 ;;
esac

tmp=$(mktemp -d "${TMPDIR:-/tmp}/wfpins.XXXXXX")
# Absolute, so the fixture paths the self-test compares do not depend on
# a relative TMPDIR.
tmp=$(CDPATH='' cd "${tmp:?}" && pwd)
trap 'chmod -R u+rwx "${tmp:?}" 2>/dev/null; rm -rf "${tmp:?}"' EXIT
trap 'exit 1' HUP INT TERM

# scan <dir>: STUB.
scan()
{
	return 0
}

selftest()
{
	# A commit SHA and an image digest as they are pinned on main, and
	# one-character mutations of each.
	sha=3d3c42e5aac5ba805825da76410c181273ba90b1
	sha39=${sha%?}
	sha41=${sha}0
	shauc=3D${sha#3d}
	dig=2d46f43b677a047cfab870549eea0d83d0a9bea2f55b3f481efc901043916c2a
	dig63=${dig%?}
	dig65=${dig}0
	diguc=2D${dig#2d}
	failed=0

	# wf <dir> <file> <line>...: a workflow <dir>/<file> in the shape of
	# .github/workflows/macos.yml, with <line>s as its jobs.
	wf()
	{
		mkdir -p "${tmp:?}/$1"
		_wd=$1
		_wf=$2
		shift 2
		printf '%s\n' 'name: Fixture' 'run-name: fixture' 'on:' '  push:' \
			'    branches:' "      - 'main'" '  pull_request:' '    types:' \
			'      - opened' '      - synchronize' 'jobs:' "$@" > "${tmp:?}/$_wd/$_wf"
	}

	# step <dir> <line>...: a workflow whose one job runs the step <line>s.
	step()
	{
		_sd=$1
		shift
		wf "$_sd" w.yml '  build_macos_aarch64:' '    runs-on: macos-latest' \
			'    permissions:' '      contents: read' '    steps:' "$@"
	}

	# expect <accept|refuse> <case> <dir>: one assertion.
	expect()
	{
		if (scan "$tmp/$3") > /dev/null 2>&1; then
			_got=accept
		else
			_got=refuse
		fi
		if [ "$_got" != "$1" ]; then
			echo "$me: selftest: $2: expected $1, got $_got" >&2
			failed=1
		fi
	}

	# Actions, in the forms of macos.yml and codeql.yml on main and of
	# GitHub's workflow syntax reference.
	step sha '    - name: Check out repository code' "      uses: actions/checkout@$sha"
	expect accept "uses: actions/checkout@<40 hex>" sha
	step shacomment '    - name: Check out repository code' "      uses: actions/checkout@$sha # v7.0.1"
	expect accept "uses: actions/checkout@<40 hex> # v7.0.1" shacomment
	step dquote "      - uses: \"actions/checkout@$sha\""
	expect accept "- uses: \"actions/checkout@<40 hex>\"" dquote
	step squote "      - uses: 'actions/checkout@$sha'"
	expect accept "- uses: 'actions/checkout@<40 hex>'" squote
	step tag '    - name: Check out repository code' '      uses: actions/checkout@v7'
	expect refuse "uses: actions/checkout@v7" tag
	step sha39 "      - uses: actions/checkout@$sha39"
	expect refuse "uses: actions/checkout@<39 hex>" sha39
	step sha41 "      - uses: actions/checkout@$sha41"
	expect refuse "uses: actions/checkout@<41 hex>" sha41
	step shauc "      - uses: actions/checkout@$shauc"
	expect refuse "uses: actions/checkout@<40 hex, one uppercase>" shauc
	step noref '    - name: Check out repository code' '      uses: actions/checkout'
	expect refuse "uses: actions/checkout (no ref)" noref
	step local '    - name: Local action' '      uses: ./.github/actions/x'
	expect accept "uses: ./.github/actions/x" local
	wf reusable w.yml '  call-workflow:' \
		"    uses: octo-org/this-repo/.github/workflows/workflow-1.yml@$sha"
	expect accept "uses: owner/repo/.github/workflows/x.yml@<40 hex>" reusable
	wf reusablemain w.yml '  call-workflow:' \
		'    uses: octo-org/this-repo/.github/workflows/workflow-1.yml@main'
	expect refuse "uses: owner/repo/.github/workflows/x.yml@main" reusablemain
	step docker '      - name: Docker action' "        uses: docker://debian@sha256:$dig"
	expect accept "uses: docker://debian@sha256:<64 hex>" docker
	step dockertag '      - name: Docker action' '        uses: docker://debian:testing'
	expect refuse "uses: docker://debian:testing" dockertag
	step commented '    # uses: actions/checkout@v7' '    - name: Check out repository code' \
		"      uses: actions/checkout@$sha"
	expect accept "# uses: actions/checkout@v7 (a comment)" commented

	# Images, in the forms of codeql.yml's cppcheck job on main and of
	# the container and services sections of the syntax reference.
	cjob()
	{
		_cd=$1
		shift
		wf "$_cd" w.yml '  cppcheck:' '    name: Cppcheck' '    runs-on: ubuntu-latest' \
			"$@" '    permissions:' '      contents: read' '    steps:' \
			'    - name: Check out repository code' "      uses: actions/checkout@$sha"
	}
	cjob ctag '    container:' '      image: debian:testing'
	expect refuse "container: image: debian:testing" ctag
	cjob cdig '    container:' "      image: debian@sha256:$dig  # debian:testing" \
		'      env:' '        NODE_ENV: development'
	expect accept "container: image: debian@sha256:<64 hex>" cdig
	cjob cstr '    container: debian:testing'
	expect refuse "container: debian:testing" cstr
	cjob cdig63 '    container:' "      image: debian@sha256:$dig63"
	expect refuse "container: image: debian@sha256:<63 hex>" cdig63
	cjob cdig65 '    container:' "      image: debian@sha256:$dig65"
	expect refuse "container: image: debian@sha256:<65 hex>" cdig65
	cjob cdiguc '    container:' "      image: debian@sha256:$diguc"
	expect refuse "container: image: debian@sha256:<64 hex, one uppercase>" cdiguc
	cjob cnext '    container:' '      debian:testing'
	expect refuse "container: with the image on the next line" cnext
	cjob svc '    services:' '      nginx:' '        image: nginx' '        ports:' \
		'          - 8080:80'
	expect refuse "services: nginx: image: nginx" svc
	cjob svcdig '    services:' '      nginx:' "        image: nginx@sha256:$dig" \
		'        ports:' '          - 8080:80'
	expect accept "services: nginx: image: nginx@sha256:<64 hex>" svcdig

	# Forms the scan cannot read on one line are refused.
	step flow "      - { name: Check out repository code, uses: actions/checkout@$sha }"
	expect refuse "a step in a flow mapping" flow
	step twoword "      - uses: actions/checkout@$sha v7.0.1"
	expect refuse "uses: with a second word after the reference" twoword
	step unclosed "      - uses: \"actions/checkout@$sha"
	expect refuse "uses: with an unclosed quote" unclosed
	step empty '      - uses:' "          actions/checkout@$sha"
	expect refuse "uses: with the reference on the next line" empty

	# The directory, the file names and the files.
	mkdir -p "${tmp:?}/nowf"
	printf '%s\n' 'not a workflow' > "${tmp:?}/nowf/README.md"
	expect refuse "a directory with no .yml or .yaml file" nowf
	expect refuse "a missing directory" missing
	step unreadable '    - name: Check out repository code' "      uses: actions/checkout@$sha"
	chmod 000 "${tmp:?}/unreadable/w.yml"
	if [ -r "${tmp:?}/unreadable/w.yml" ]; then
		echo "$me: selftest: the unreadable-file case is skipped: this user reads a mode 000 file" >&2
	else
		expect refuse "a file the scan cannot read" unreadable
	fi
	step notfile '    - name: Check out repository code' "      uses: actions/checkout@$sha"
	mkdir -p "${tmp:?}/notfile/d.yml"
	expect refuse "a .yml that is not a regular file" notfile
	step yaml '    - name: Check out repository code' "      uses: actions/checkout@$sha"
	mv "${tmp:?}/yaml/w.yml" "${tmp:?}/yaml/w.yaml"
	expect accept "a pinned .yaml file alone" yaml
	step mixyaml '    - name: Check out repository code' "      uses: actions/checkout@$sha"
	wf mixyaml bad.yaml '  build:' '    runs-on: macos-latest' '    steps:' \
		'    - uses: actions/checkout@v7'
	expect refuse "an unpinned .yaml beside a pinned .yml" mixyaml
	step mixyml '    - name: Check out repository code' "      uses: actions/checkout@$sha"
	mv "${tmp:?}/mixyml/w.yml" "${tmp:?}/mixyml/w.yaml"
	wf mixyml bad.yml '  build:' '    runs-on: macos-latest' '    steps:' \
		'    - uses: actions/checkout@v7'
	expect refuse "an unpinned .yml beside a pinned .yaml" mixyml
	step upper '    - name: Check out repository code' "      uses: actions/checkout@$sha"
	wf upper BAD.YML '  build:' '    runs-on: macos-latest' '    steps:' \
		'    - uses: actions/checkout@v7'
	expect refuse "an unpinned .YML" upper
	step dot '    - name: Check out repository code' "      uses: actions/checkout@$sha"
	wf dot .bad.yml '  build:' '    runs-on: macos-latest' '    steps:' \
		'    - uses: actions/checkout@v7'
	expect refuse "an unpinned dot-file .yml" dot

	# A failing awk is a failed scan, never a clean one.
	mkdir -p "${tmp:?}/shim"
	printf '%s\n' '#!/bin/sh' 'exit 2' > "${tmp:?}/shim/awk"
	chmod 755 "${tmp:?}/shim/awk"
	if (PATH=$tmp/shim:$PATH; scan "$tmp/sha") > /dev/null 2>&1; then
		echo "$me: selftest: a failing awk: expected refuse, got accept" >&2
		failed=1
	fi

	# A refusal names the file, the line and the reference, and nothing
	# else from the file.
	_out=$( (scan "$tmp/tag") 2>&1) || :
	_want="$me: $tmp/tag/w.yml:18: actions/checkout@v7: not pinned to a 40-hex commit SHA"
	if [ "$_out" != "$_want" ]; then
		echo "$me: selftest: the refusal of uses: actions/checkout@v7 printed something else" >&2
		failed=1
	fi

	return "$failed"
}

if [ "$mode" = selftest ]; then
	selftest
	echo "$me: selftest ok"
	exit 0
fi
( selftest ) || { echo "$me: the self-test failed; the tree was not checked" >&2; exit 1; }
scan .github/workflows || exit 1
echo "$me: ok"
