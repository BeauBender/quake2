#!/bin/sh
# Invariant: every reference a workflow in .github/workflows/ pulls from
# outside this repository is pinned to an immutable id, and so is every
# reference of every action in this repository a workflow uses.
#
# Every file in .github/workflows/ whose name ends in .yml or .yaml (any
# letter case, dot-files too) is parsed by ruby's yaml (Psych over
# libyaml, YAML.safe_load with aliases allowed), so every spelling of a key
# and every kind of line break counts as YAML reads it. The parsed tree is
# walked, and in every mapping at any depth:
#   uses       is a string: <owner>/<repo>[/<path>]@<40 lowercase hex> (an
#              action or a reusable workflow), docker://<image>, or
#              ./<path>, a path from the repository root to a reusable
#              workflow file or to a directory with action.yml or
#              action.yaml. That file is walked by the same rules, each
#              file once, so a cycle ends; its physical path must be in
#              the repository (no .. or symbolic link out). Any other
#              form, $/<path> among them, is refused.
#   image      is a string <image>, whatever mapping holds it: a container,
#              a service, an action input (stricter than needed: an input
#              called image is held to the rule too).
#   container  is an <image> string, or a mapping with an image key.
#   services   is a mapping whose every value is an <image> string or a
#              mapping with an image key.
# An <image> ends in @sha256: and 64 lowercase hex digits and holds no
# ${{ expression. In a Docker action (runs.using is
# docker), runs.image is docker://<image>, or a Dockerfile path from the
# action's directory, in the repository, whose every FROM line names an
# <image> and no ARG ($); a FROM of a build stage or of scratch is refused
# too (stricter than needed). A wrong type or a missing value is refused.
#
# The scan fails closed: no ruby on PATH, ruby's yaml that cannot be
# loaded, a file that cannot be read or parsed (a syntax error, a tag
# safe_load refuses), a missing directory, a directory with no workflow
# file, a workflow name that is not a regular file, and any non-zero ruby
# status are refusals, never a clean result. Nothing in the tree can turn
# it off: it reads no configuration and honours no marker in the files it
# checks, and RUBYOPT and RUBYLIB are cleared. A refusal prints the file,
# the reference (printable ASCII, at most 200 characters) and a fixed
# reason, and nothing else from the file.
#
# The self-test runs first, every time, over fixture repositories it
# writes into a mktemp -d directory: each case is one assertion with the
# verdict it expects, in the forms of the workflows on main, of GitHub's
# workflow syntax reference and of the YAML spellings libyaml accepts. A
# wrong verdict fails the run before the tree is read, so a ruby that
# misreads the files cannot pass the tree.
#
# Usage: tests/check-workflow-pins.sh               (self-test, then the tree)
#        tests/check-workflow-pins.sh --selftest    (the self-test alone)
set -eu
CDPATH='' cd "$(dirname "$0")/.."
export LC_ALL=C
unset RUBYOPT RUBYLIB
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
trap 'chmod -R u+rwx "${tmp:?}" 2>/dev/null; rm -rf "${tmp:?}"' EXIT
trap 'exit 1' HUP INT TERM
# Absolute, so the fixture paths do not depend on a relative TMPDIR.
tmp=$(CDPATH='' cd "${tmp:?}" && pwd)

# The walk. ARGV: the repository root, then the workflow files relative to
# it. It prints a line for each refusal and exits 1 when it printed one,
# 3 when yaml cannot be loaded.
prog='
ME = "check-workflow-pins"
begin
  require "yaml"
rescue LoadError
  puts ME + ": ruby cannot load its yaml library; nothing was checked"
  exit 3
end
REMOTE = %r{\A[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+(/[^@]*)?@[0-9a-f]{40}\z}
IMAGE = /@sha256:[0-9a-f]{64}\z/
ROOT = File.realpath(ARGV.shift)
Dir.chdir(ROOT)
$bad = false
$done = {}

def show(v)
  return "(not a string)" unless v.is_a?(String)
  v.scrub("?").gsub(/[^ -~]/, "?")[0, 200]
end

def refuse(file, v, why)
  puts ME + ": " + show(file) + ": " + show(v) + ": " + why
  $bad = true
end

def pinned_image?(v)
  v.is_a?(String) && !v.include?("${{") && IMAGE.match?(v)
end

def image(file, v)
  refuse(file, v, "not pinned by a sha256 digest") unless pinned_image?(v)
end

# inside(path): true when the physical path of path, which exists, is in
# ROOT.
def inside(path)
  File.realpath(path).start_with?(ROOT + "/")
end

def uses(file, v)
  if !v.is_a?(String)
    refuse(file, v, "not a string")
  elsif v.start_with?("./")
    local(file, v)
  elsif v.start_with?("docker://")
    refuse(file, v, "not pinned by a sha256 digest") unless pinned_image?(v)
  elsif !REMOTE.match?(v)
    refuse(file, v, "not pinned to a 40-hex commit SHA")
  end
end

def local(file, v)
  path = File.join(".", v[2..-1])
  target = path if File.file?(path)
  target ||= ["action.yml", "action.yaml"].map { |n| File.join(path, n) }.find { |p| File.file?(p) }
  if !target
    refuse(file, v, "no action.yml or action.yaml there")
  elsif !inside(target)
    refuse(file, v, "outside the repository")
  else
    walk_file(target)
  end
end

def container(file, v)
  if v.is_a?(String)
    image(file, v)
  elsif !(v.is_a?(Hash) && v.key?("image"))
    refuse(file, v, "neither an image nor a mapping with an image")
  end
end

def services(file, v)
  if v.is_a?(Hash)
    v.each_value { |s| container(file, s) }
  else
    refuse(file, v, "not a mapping of services")
  end
end

def docker(file, runs)
  img = runs["image"]
  if !img.is_a?(String)
    refuse(file, img, "a Docker action with no image")
  elsif img.start_with?("docker://")
    refuse(file, img, "not pinned by a sha256 digest") unless pinned_image?(img)
  else
    dockerfile(File.join(File.dirname(file), img), img, file)
  end
end

def dockerfile(path, img, file)
  return refuse(file, img, "no such Dockerfile in the repository") unless File.file?(path) && inside(path)
  File.binread(path).split(/\r\n|\r|\n/).each do |line|
    next unless line =~ /\A\s*from\s+(.*)/i
    base = $1.split.reject { |w| w.start_with?("--") }[0]
    if base.to_s.include?("$")
      refuse(path, base, "a FROM named by an ARG")
    else
      image(path, base)
    end
  end
end

def walk(file, node, seen, exempt)
  return unless node.is_a?(Hash) || node.is_a?(Array)
  return if seen[node]
  seen[node] = true
  if node.is_a?(Array)
    node.each { |e| walk(file, e, seen, exempt) }
    return
  end
  node.each do |k, v|
    case k
    when "uses" then uses(file, v)
    when "image" then image(file, v) unless node.equal?(exempt)
    when "container" then container(file, v)
    when "services" then services(file, v)
    end
    walk(file, v, seen, exempt)
  end
end

def walk_file(file)
  real = File.realpath(file)
  return if $done[real]
  $done[real] = true
  tree = YAML.safe_load(File.read(file, encoding: "UTF-8"), permitted_classes: [], aliases: true)
  runs = tree["runs"] if tree.is_a?(Hash) && tree["runs"].is_a?(Hash) && tree["runs"]["using"] == "docker"
  docker(file, runs) if runs
  walk(file, tree, {}.compare_by_identity, runs)
rescue StandardError, SystemStackError => e
  refuse(file, "(the file)", "cannot be read or parsed (" + e.class.name + ")")
end

ARGV.each { |f| walk_file(f) }
exit($bad ? 1 : 0)
'

# scan <root>: prints a line for each refusal in the workflow files under
# <root>/.github/workflows and the local actions they use, and returns 1
# when there is one, or when the scan cannot run: no ruby, no workflow
# file (no directory either), a workflow name that is not a regular file,
# or a ruby that fails.
scan()
{
	if ! command -v ruby > /dev/null 2>&1; then
		echo "$me: no ruby on PATH (its yaml parses the workflows); nothing was checked" >&2
		return 1
	fi
	_n=0
	_rc=0
	set -- "$1"
	for _f in "$1"/.github/workflows/* "$1"/.github/workflows/.*; do
		case ${_f##*/} in
		*.[Yy][Mm][Ll] | *.[Yy][Aa][Mm][Ll]) ;;
		*) continue ;;
		esac
		_n=$((_n + 1))
		if [ ! -f "$_f" ]; then
			echo "$me: ${_f#"$1"/}: not a regular file; it was not checked" >&2
			_rc=1
			continue
		fi
		set -- "$@" "${_f#"$1"/}"
	done
	if [ "$_n" -eq 0 ]; then
		echo "$me: .github/workflows: no .yml or .yaml file (or no such directory); nothing was checked" >&2
		return 1
	fi
	_s=0
	ruby --disable-gems -e "$prog" -- "$@" >&2 || _s=$?
	if [ "$_s" -gt 1 ]; then
		echo "$me: ruby failed (status $_s); the workflows were not checked" >&2
	fi
	[ "$_s" -eq 0 ] || _rc=1
	return "$_rc"
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

	# wf <repo> <file> <line>...: a workflow <repo>/.github/workflows/<file>
	# in the shape of .github/workflows/macos.yml, with <line>s as its jobs.
	wf()
	{
		mkdir -p "${tmp:?}/$1/.github/workflows"
		_wd=$1
		_wf=$2
		shift 2
		printf '%s\n' 'name: Fixture' 'run-name: fixture' 'on:' '  push:' \
			'    branches:' "      - 'main'" '  pull_request:' '    types:' \
			'      - opened' '      - synchronize' 'jobs:' "$@" \
			> "${tmp:?}/$_wd/.github/workflows/$_wf"
	}

	# step <repo> <line>...: a workflow whose one job runs the step <line>s.
	step()
	{
		_sd=$1
		shift
		wf "$_sd" w.yml '  build_macos_aarch64:' '    runs-on: macos-latest' \
			'    permissions:' '      contents: read' '    steps:' "$@"
	}

	# act <repo> <file> <line>...: the action file <repo>/<file>.
	act()
	{
		mkdir -p "${tmp:?}/$1/${2%/*}"
		_ad=$1
		_af=$2
		shift 2
		printf '%s\n' "$@" > "${tmp:?}/$_ad/$_af"
	}

	# A composite action whose one step is <line>.
	comp()
	{
		act "$1" "$2" 'name: Fixture action' 'description: fixture' 'runs:' \
			'  using: composite' '  steps:' "$3"
	}

	# Rewrites <file>'s line breaks as <break> (an awk string).
	breaks()
	{
		awk -v b="$2" '{ printf "%s%s", $0, b }' "${tmp:?}/$1" > "${tmp:?}/$1.new"
		mv "${tmp:?}/$1.new" "${tmp:?}/$1"
	}

	# expect <accept|refuse> <case> <repo>: one assertion.
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

	pinstep="      uses: actions/checkout@$sha"
	pincomp="    - uses: actions/checkout@$sha"

	# Actions, in the forms of macos.yml and codeql.yml on main and of
	# GitHub's workflow syntax reference.
	step sha '    - name: Check out repository code' "$pinstep"
	expect accept "uses: actions/checkout@<40 hex>" sha
	step shacomment '    - name: Check out repository code' "$pinstep # v7.0.1"
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
	comp local .github/actions/x/action.yml "$pincomp"
	expect accept "uses: ./.github/actions/x (a pinned composite action)" local
	step dollar '      - uses: $/.github/actions/hello-world-action'
	comp dollar .github/actions/hello-world-action/action.yml "$pincomp"
	expect refuse "- uses: \$/.github/actions/hello-world-action" dollar
	step dollarsha "      - uses: \$/.github/actions/x@$sha"
	expect refuse "- uses: \$/.github/actions/x@<40 hex>" dollarsha
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
		"$pinstep"
	expect accept "# uses: actions/checkout@v7 (a comment)" commented
	step runline '    - name: Show the step' '      run: |' '        echo "uses: actions/checkout@v7"' \
		'        uses: actions/checkout@v7'
	expect accept "run: | with uses: lines in the script (not a key)" runline
	step usesmap '      - uses:' '          name: actions/checkout@v7'
	expect refuse "uses: with a mapping as its value" usesmap

	# Every spelling of the key that libyaml reads as uses.
	step qkey "      - \"uses\": actions/checkout@v7"
	expect refuse "- \"uses\": actions/checkout@v7 (a quoted key)" qkey
	wf flowseq w.yml '  build:' '    runs-on: macos-latest' '    steps: [uses: actions/checkout@v7]'
	expect refuse "steps: [uses: actions/checkout@v7]" flowseq
	step complex '      - ? uses' '        : actions/checkout@v7'
	expect refuse "- ? uses / : actions/checkout@v7 (a complex key)" complex
	step anchor '      - &k uses: actions/checkout@v7'
	expect refuse "- &k uses: actions/checkout@v7 (an anchored key)" anchor
	step strtag '      - !!str uses: actions/checkout@v7'
	expect refuse "- !!str uses: actions/checkout@v7 (a tagged key)" strtag
	step aliaskey "      - &k uses: actions/checkout@$sha" '      - *k : actions/checkout@v7'
	expect refuse "- *k : actions/checkout@v7 (an alias key)" aliaskey
	step flowcomplex '      - { ? uses : actions/checkout@v7 }'
	expect refuse "- { ? uses : actions/checkout@v7 }" flowcomplex
	step flow "      - { name: Check out repository code, uses: actions/checkout@$sha }"
	expect accept "a pinned step in a flow mapping" flow
	step flowbad '      - { name: Check out repository code, uses: actions/checkout@v7 }'
	expect refuse "an unpinned step in a flow mapping" flowbad

	# Line breaks: CRLF, CR alone and U+2028 are all line breaks to libyaml.
	step crlf '    - name: Check out repository code' "$pinstep"
	breaks crlf/.github/workflows/w.yml '\r\n'
	expect accept "a pinned file with CRLF line ends" crlf
	step cr '    - name: Check out repository code' "$pinstep"
	breaks cr/.github/workflows/w.yml '\r'
	expect accept "a pinned file with CR line ends" cr
	step crbad '    - name: Check out repository code' '      uses: actions/checkout@v7'
	breaks crbad/.github/workflows/w.yml '\r'
	expect refuse "an unpinned file with CR line ends" crbad
	step ls '    - name: Check out repository code' "$pinstep"
	breaks ls/.github/workflows/w.yml '\342\200\250'
	expect accept "a pinned file with U+2028 line ends" ls
	step lsbad '    - name: Check out repository code' '      uses: actions/checkout@v7'
	breaks lsbad/.github/workflows/w.yml '\342\200\250'
	expect refuse "an unpinned file with U+2028 line ends" lsbad

	# Images, in the forms of codeql.yml's cppcheck job on main and of
	# the container and services sections of the syntax reference.
	cjob()
	{
		_cd=$1
		shift
		wf "$_cd" w.yml '  cppcheck:' '    name: Cppcheck' '    runs-on: ubuntu-latest' \
			"$@" '    permissions:' '      contents: read' '    steps:' \
			'    - name: Check out repository code' "$pinstep"
	}
	cjob ctag '    container:' '      image: debian:testing'
	expect refuse "container: image: debian:testing" ctag
	cjob cdig '    container:' '      # The tag is in the comment.' \
		"      image: debian@sha256:$dig  # debian:testing" \
		'      env:' '        NODE_ENV: development'
	expect accept "container: image: debian@sha256:<64 hex>" cdig
	cjob cstr '    container: debian:testing'
	expect refuse "container: debian:testing" cstr
	cjob cstrdig "    container: debian@sha256:$dig"
	expect accept "container: debian@sha256:<64 hex>" cstrdig
	cjob cdig63 '    container:' "      image: debian@sha256:$dig63"
	expect refuse "container: image: debian@sha256:<63 hex>" cdig63
	cjob cdig65 '    container:' "      image: debian@sha256:$dig65"
	expect refuse "container: image: debian@sha256:<65 hex>" cdig65
	cjob cdiguc '    container:' "      image: debian@sha256:$diguc"
	expect refuse "container: image: debian@sha256:<64 hex, one uppercase>" cdiguc
	cjob cnext '    container:' '      debian:testing'
	expect refuse "container: with the image on the next line" cnext
	cjob ccomment '    container:  # the image is pinned below' "      image: debian@sha256:$dig"
	expect accept "container: # a comment, the pinned mapping below" ccomment
	cjob cnoimage '    container:' '      image:' '      env:' '        NODE_ENV: development'
	expect refuse "container: image: with no value" cnoimage
	cjob cnokey '    container:' '      env:' '        NODE_ENV: development'
	expect refuse "container: a mapping with no image key" cnokey
	cjob cseq '    container: [debian:testing]'
	expect refuse "container: a sequence" cseq
	cjob cexpr '    container:' "      image: \${{vars.REGISTRY}}/debian@sha256:$dig"
	expect refuse "container: image: \${{vars.REGISTRY}}/debian@sha256:<64 hex>" cexpr
	cjob svc '    services:' '      nginx:' '        image: nginx' '        ports:' \
		'          - 8080:80'
	expect refuse "services: nginx: image: nginx" svc
	cjob svcdig '    services:' '      nginx:' "        image: nginx@sha256:$dig" \
		'        ports:' '          - 8080:80'
	expect accept "services: nginx: image: nginx@sha256:<64 hex>" svcdig
	cjob svcstr '    services:' '      redis: redis:7'
	expect refuse "services: redis: redis:7 (the string form)" svcstr
	cjob svcstrdig '    services:' "      redis: redis@sha256:$dig"
	expect accept "services: redis: redis@sha256:<64 hex>" svcstrdig
	cjob svcexpr '    services: ${{ fromJSON(vars.SVC) }}'
	expect refuse "services: \${{ fromJSON(vars.SVC) }}" svcexpr
	step input '    - name: Scan an image' "$pinstep" '      with:' '        image: debian:testing'
	expect refuse "an action input image: debian:testing" input

	# Forms the old line scan could not read; the parser reads them.
	step twoword "      - uses: actions/checkout@$sha v7.0.1"
	expect refuse "uses: with a second word after the reference" twoword
	step unclosed "      - uses: \"actions/checkout@$sha"
	expect refuse "uses: with an unclosed quote (a parse error)" unclosed
	step nextline '      - uses:' "          actions/checkout@$sha"
	expect accept "uses: with the pinned reference on the next line" nextline
	step nextlinebad '      - uses:' '          actions/checkout@v7'
	expect refuse "uses: with an unpinned reference on the next line" nextlinebad
	step date '    - name: Check out repository code' "$pinstep" '      with:' \
		'        since: 2001-12-14'
	expect refuse "a value safe_load refuses (a date)" date
	step recursive '    - name: Check out repository code' "$pinstep" '      with:' \
		'        list: &a [x, *a]'
	expect accept "a pinned file with a recursive alias (the walk ends)" recursive

	# Local actions and reusable workflows in this repository.
	step localyaml '      - uses: ./.github/actions/y'
	comp localyaml .github/actions/y/action.yaml "$pincomp"
	expect accept "uses: ./.github/actions/y (action.yaml)" localyaml
	step localbad '      - uses: ./.github/actions/x'
	comp localbad .github/actions/x/action.yml '    - uses: actions/checkout@v7'
	expect refuse "a local composite action with an unpinned uses:" localbad
	step localmissing '      - uses: ./.github/actions/none'
	expect refuse "a local action with no action file" localmissing
	step dotdot/repo '      - uses: ./../outside'
	comp dotdot outside/action.yml "$pincomp"
	expect refuse "uses: ./../outside (out of the repository by ..)" dotdot/repo
	step symout/repo '      - uses: ./.github/actions/out'
	comp symout elsewhere/action.yml "$pincomp"
	mkdir -p "${tmp:?}/symout/repo/.github/actions"
	ln -s "${tmp:?}/symout/elsewhere" "${tmp:?}/symout/repo/.github/actions/out"
	expect refuse "a local action that is a symbolic link out of the repository" symout/repo
	step cycle '      - uses: ./.github/actions/a'
	comp cycle .github/actions/a/action.yml '    - uses: ./.github/actions/b'
	comp cycle .github/actions/b/action.yml '    - uses: ./.github/actions/a'
	expect accept "two local actions that use each other (the walk ends)" cycle
	wf calls w.yml '  call-workflow-2-in-local-repo:' '    uses: ./.github/workflows/called.yml'
	wf calls called.yml '  build:' '    runs-on: macos-latest' '    steps:' "$pinstep"
	expect accept "uses: ./.github/workflows/called.yml (a local reusable workflow)" calls

	# Local Docker actions.
	dact()
	{
		_dr=$1
		shift
		step "$_dr" '      - uses: ./.github/actions/d'
		act "$_dr" .github/actions/d/action.yml 'name: Fixture action' 'description: fixture' \
			'runs:' '  using: docker' "$@"
	}
	dfile()
	{
		_df=$1
		shift
		printf '%s\n' "$@" > "${tmp:?}/$_df/.github/actions/d/Dockerfile"
	}
	dact dhub "  image: docker://debian@sha256:$dig"
	expect accept "a Docker action, image: docker://debian@sha256:<64 hex>" dhub
	dact dhubtag '  image: docker://debian:testing'
	expect refuse "a Docker action, image: docker://debian:testing" dhubtag
	dact dnone '  env:' '    X: y'
	expect refuse "a Docker action with no image" dnone
	dact dfpin '  image: Dockerfile'
	dfile dfpin "FROM --platform=linux/amd64 debian@sha256:$dig AS build" \
		'RUN echo "built from this repository"' \
		"FROM debian@sha256:$dig"
	expect accept "a Dockerfile whose every FROM is pinned" dfpin
	dact dftag '  image: Dockerfile'
	dfile dftag "FROM debian@sha256:$dig AS build" 'FROM debian:testing'
	expect refuse "a Dockerfile with a second FROM unpinned" dftag
	dact dflower '  image: Dockerfile'
	dfile dflower 'from debian:testing'
	expect refuse "a Dockerfile with from debian:testing (lower case)" dflower
	dact dfarg '  image: Dockerfile'
	dfile dfarg 'ARG REGISTRY=docker.io' "FROM \${REGISTRY}/debian@sha256:$dig"
	expect refuse "a Dockerfile FROM named by an ARG" dfarg
	dact dfcr '  image: Dockerfile'
	dfile dfcr "FROM debian@sha256:$dig" 'FROM debian:testing'
	breaks dfcr/.github/actions/d/Dockerfile '\r'
	expect refuse "a Dockerfile with CR line ends and an unpinned FROM" dfcr
	dact dfmissing '  image: Dockerfile'
	expect refuse "a Docker action whose Dockerfile is missing" dfmissing
	dact dfout '  image: Dockerfile'
	printf '%s\n' "FROM debian@sha256:$dig" > "${tmp:?}/dfout.Dockerfile"
	ln -s "${tmp:?}/dfout.Dockerfile" "${tmp:?}/dfout/.github/actions/d/Dockerfile"
	expect refuse "a Dockerfile that is a symbolic link out of the repository" dfout

	# The directory, the file names and the files.
	mkdir -p "${tmp:?}/nowf/.github/workflows"
	printf '%s\n' 'not a workflow' > "${tmp:?}/nowf/.github/workflows/README.md"
	expect refuse "a directory with no .yml or .yaml file" nowf
	expect refuse "a missing directory" missing
	step unreadable '    - name: Check out repository code' "$pinstep"
	chmod 000 "${tmp:?}/unreadable/.github/workflows/w.yml"
	if [ -r "${tmp:?}/unreadable/.github/workflows/w.yml" ]; then
		echo "$me: selftest: the unreadable-file case is skipped: this user reads a mode 000 file" >&2
	else
		expect refuse "a file the scan cannot read" unreadable
	fi
	step notfile '    - name: Check out repository code' "$pinstep"
	mkdir -p "${tmp:?}/notfile/.github/workflows/d.yml"
	expect refuse "a .yml that is not a regular file" notfile
	step yaml '    - name: Check out repository code' "$pinstep"
	mv "${tmp:?}/yaml/.github/workflows/w.yml" "${tmp:?}/yaml/.github/workflows/w.yaml"
	expect accept "a pinned .yaml file alone" yaml
	step mixyaml '    - name: Check out repository code' "$pinstep"
	wf mixyaml bad.yaml '  build:' '    runs-on: macos-latest' '    steps:' \
		'    - uses: actions/checkout@v7'
	expect refuse "an unpinned .yaml beside a pinned .yml" mixyaml
	step mixyml '    - name: Check out repository code' "$pinstep"
	mv "${tmp:?}/mixyml/.github/workflows/w.yml" "${tmp:?}/mixyml/.github/workflows/w.yaml"
	wf mixyml bad.yml '  build:' '    runs-on: macos-latest' '    steps:' \
		'    - uses: actions/checkout@v7'
	expect refuse "an unpinned .yml beside a pinned .yaml" mixyml
	step upper '    - name: Check out repository code' "$pinstep"
	wf upper BAD.YML '  build:' '    runs-on: macos-latest' '    steps:' \
		'    - uses: actions/checkout@v7'
	expect refuse "an unpinned .YML" upper
	step dot '    - name: Check out repository code' "$pinstep"
	wf dot .bad.yml '  build:' '    runs-on: macos-latest' '    steps:' \
		'    - uses: actions/checkout@v7'
	expect refuse "an unpinned dot-file .yml" dot

	# No ruby, a ruby without its yaml, and a failing ruby are failed
	# scans, never clean ones.
	mkdir -p "${tmp:?}/noruby" "${tmp:?}/shim" "${tmp:?}/noyaml/lib"
	if (PATH=$tmp/noruby; scan "$tmp/sha") > /dev/null 2>&1; then
		echo "$me: selftest: no ruby on PATH: expected refuse, got accept" >&2
		failed=1
	fi
	printf '%s\n' '#!/bin/sh' 'exit 2' > "${tmp:?}/shim/ruby"
	chmod 755 "${tmp:?}/shim/ruby"
	if (PATH=$tmp/shim:$PATH; scan "$tmp/sha") > /dev/null 2>&1; then
		echo "$me: selftest: a failing ruby: expected refuse, got accept" >&2
		failed=1
	fi
	printf '%s\n' 'raise LoadError, "no yaml here"' > "${tmp:?}/noyaml/lib/yaml.rb"
	printf '%s\n' '#!/bin/sh' "exec \"$(command -v ruby)\" -I \"$tmp/noyaml/lib\" \"\$@\"" \
		> "${tmp:?}/noyaml/ruby"
	chmod 755 "${tmp:?}/noyaml/ruby"
	if (PATH=$tmp/noyaml:$PATH; scan "$tmp/sha") > /dev/null 2>&1; then
		echo "$me: selftest: a ruby that cannot load yaml: expected refuse, got accept" >&2
		failed=1
	fi

	# A refusal names the file and the reference, and nothing else from
	# the file.
	_out=$( (scan "$tmp/tag") 2>&1) || :
	_want="$me: .github/workflows/w.yml: actions/checkout@v7: not pinned to a 40-hex commit SHA"
	if [ "$_out" != "$_want" ]; then
		echo "$me: selftest: the refusal of uses: actions/checkout@v7 printed something else" >&2
		failed=1
	fi
	# A reference is printed as printable ASCII, at most 200 characters.
	_xs=$(printf '%300s' '' | tr ' ' x)
	step long "      - uses: \"actions/checkout@v7\\t$_xs\""
	_out=$( (scan "$tmp/long") 2>&1) || :
	_want="$me: .github/workflows/w.yml: actions/checkout@v7?$(printf '%180s' '' | tr ' ' x): not pinned to a 40-hex commit SHA"
	if [ "$_out" != "$_want" ]; then
		echo "$me: selftest: a reference with a tab and 300 more characters was not printed as 200 printable ones" >&2
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
scan . || exit 1
echo "$me: ok"
