# 0005. Keep the working machine's identity out of what is published

Status: accepted 2026-11-13; landed as TASK77 (#170).

## Context

The agent tooling configured in `.claude/dev-loop.json` pushed telemetry
branches to this public repository whose names contained the working
machine's host name and local user name. The branches were deleted by the
owner's decision, and the owner asked for a mechanism so it cannot recur.
[Record 0003](0003-no-local-identity.md) stopped publishing telemetry and
left the push-time refusal to a separate card; this record is that card.

## Decision

- `.githooks/pre-push` refuses a push when the working machine's host name
  (`hostname -s`), user name (`whoami`) or home path appears in what the push
  publishes: a ref name (local or remote); for each new commit its author and
  committer lines, its other headers (a `mergetag` header carries a whole
  merged tag) and its message; for an annotated tag its name, tagger line
  and message, at every level of a tag of a tag (up to ten levels; more is
  refused); a blob that a tag or ref points at. A tag or ref on a tree is
  refused, since it cannot be scanned there. Signature blocks (`gpgsig`
  headers, the signature appended to a tag) are not scanned: they are base64
  and a short name would match them by chance.
- The values are read at run time, compared case-insensitively (ASCII),
  skipped when shorter than three characters, and never written to the
  repository. Nothing the hook or the checks print carries one: no value, no
  ref name, file name or author field that holds one. A refusal names the
  field that matched (local or remote ref name, message, author or committer
  field, merged tag, other header, tag, blob), the kind of value (host, user,
  home), the ref's position in the push and the commit or object id.
- Ref names are matched as substrings. In object text the host name and home
  path are matched as substrings and the user name as a whole word, so a short
  user name does not trip on ordinary prose; the card said "contains", and
  this narrows it for the user name in object text only.
- One's own author name: in the author and committer lines only, the user
  name is not compared when it is a whole word of the checkout's `user.name`
  or of the part of `user.email` before the `@`. That word is already
  published, by choice, as the author identity, and rewording cannot clear
  such a hit. The host name and home path are still compared there, and the
  user name still counts everywhere else (messages, tags, ref names). Without
  a configured `user.name`/`user.email` nothing is exempt.
- `tests/check-no-local-identity.sh` (in the gate) fails when a tracked text
  file or a tracked file name contains one of them (the user name as a whole
  word or path component). It reports file and line (a file whose own name
  holds a value as `<withheld file name>`), and a count for file names. A
  file is binary when its index blob holds a NUL byte, and is scanned as bytes (FIX125); git's
  own test is not used, because a committed `.gitattributes` (`-diff`,
  `binary`) would take a text file out of it. It reads the staged content,
  not the working tree: the index blobs of regular files (`git grep
  --cached`) and the target of every tracked symbolic link (its blob, read
  with `git cat-file`), so from a pre-commit hook it checks what is being
  committed.
- `tests/check-pre-push-hook.sh` (in the gate) proves the hook against a
  throwaway bare repository, with synthetic names served by `hostname` and
  `whoami` shims; three real pushes prove the wiring and the rest feeds the
  hook directly, many refs per run.
- `tests/check-identity-env.sh` (in the gate) runs both checks with a hostile
  git environment (a `GIT_INDEX_FILE` copy, `GIT_DIR`, `GIT_WORK_TREE`,
  `GIT_CONFIG_*` and global and system configuration with failing hooks,
  failing signing and forced colour, `GIT_AUTHOR_*`/`GIT_COMMITTER_*`) and
  requires both to pass and the index copy to be unchanged. Both self-tests
  unset every `GIT_*` variable and read no global or system configuration or
  templates; the real scan keeps the caller's index.
- Every tool error fails closed: in the hook the push is refused, in the gate
  check the scan reports that it could not run.
- The gate prints a warning when `core.hooksPath` is not `.githooks`.

## Tooling branches

The tooling's card-claim branches (`refs/heads/claim/<ID>`) and gate-clear
branches (`refs/heads/gate-clear/<ID>`) carry the same identity in their
commit messages, and the loop needs both. The hook exempts them from the
object scan, for these two ref prefixes only (keyed on the remote ref name),
in one marked block; ref names are always checked. Keeping the exemption
costs: those commit messages stay public, and a commit pushed under an exempt
ref is from then on on a remote-tracking ref, so it counts as published and is
not scanned when another ref (a pull request branch, `main`) pushes it later.
Removing it costs: claims and gate clearances are refused on push, so the loop
cannot claim cards or record a cleared gate until the tooling offers a
pseudonymous identity. The exemption ends then.

## Consequences

Recommendation on the tooling exemption: keep it until the tooling offers a
pseudonymous identity; the owner decides.

What the guard does not do (known limits):

- A push from an unwired checkout is not guarded; the gate warns about it. A
  push with `--no-verify` skips the hook; that is against the rules in
  `CLAUDE.md` and nothing here can prevent it.
- `core.hooksPath=.githooks` is relative, so the hook that runs is the one in
  the tree checked out. A worktree on a commit older than this change has no
  `pre-push` and is not guarded, and the gate warning does not fire there
  (the setting is still `.githooks`).
- The commits to scan are those the remote lacks: not the remote's own tip and
  not anything already on a remote-tracking ref of that remote. That makes
  the hook depend on local remote-tracking refs. A stale one (for a branch
  deleted on the remote) still counts as published, so run
  `git fetch --prune` after a remote-side deletion. If the remote's tip is not
  known locally the push is refused: fetch the remote first.
- The published set is read from the tracking refs of the remote's name. A
  remote whose `pushurl` points somewhere other than where it fetches from
  is trusted to hold what was fetched; such a setup is not used here.
- More than 1000 such commits cannot be scanned, so the push is refused. This
  also happens on a first push to a remote that has no tracking refs here
  (not fetched yet, or a push by URL), where the whole history counts: fetch
  the remote first, or push in steps.
- If git cannot list or read an object, or the host or user name cannot be
  read, the push is refused (fail closed). The gate check instead warns when
  only one of the two names is unreadable, and fails when both are.
- Not scanned: file contents and file names of pushed trees at push time (the
  gate checks them in the index), note bodies, signature blocks, and
  spellings other than the literal one (percent or hex encodings, other text
  encodings such as UTF-16, a name split across lines, non-ASCII case
  folding). The format in which the tooling writes identities is outside
  this repository.
- An ordinary-word name defeats it: a host or user name that is a common
  word (a generic container name, `root`, `build`) appears in this
  repository's own text and in ordinary commit messages, so the gate fails
  and ordinary messages are refused. The same holds for CI-style user names
  (`runner` appears in this repository's text), so if the gate is ever run
  in CI, the identity check needs a decision there first. The remedy is to
  give the machine a distinctive host or user name; an allow rule is the
  owner's decision and is not provided.
- History already on the public default branch from before this mechanism
  contains such identity trailers in commit messages. This change does not
  rewrite that history, and the hook never scans it (those commits are already
  published).

## Addendum 2026-11-14: the owner handle (FIX44, #229)

On a machine whose account name equals the owner segment of this
repository's GitHub address, the user name is in the tree by necessity
(the repository slug in configuration, documentation links, credits), so
the gate check failed on every head there and an account name cannot be
renamed. A value that is the public owner handle is not a secret of the
machine, so:

- The user-name word test is skipped exactly when the user name equals,
  ASCII case-insensitively, the owner segment of the GitHub address of the
  remote `origin`, and then only as that whole word. In the gate check this
  is the whole-word test on file contents, link targets and file names; in
  the hook it is the whole-word test on object text, and in ref names (a
  substring test) only an occurrence inside a longer word still refuses.
  The host-name and home-path tests are unchanged and still apply there, so
  a path under `$HOME`, spelled as `$HOME` spells it, is still refused. The
  user name is still withheld from any file name the gate check prints.
- The skip also needs the last path component of `$HOME` to equal the user
  name (ASCII case-insensitively, `home_is`); nothing else about `$HOME` is
  checked. Under this record's threat model `$HOME` is trusted to be the
  account's home directory, so the home-path test then refuses a path under
  it. With `$HOME` unset, `/`, ending in `/` or with another last component,
  nothing is skipped and the user-name word test runs as on any other
  machine. Not covered, so that with the skip on a path in one of these
  forms is not refused: a `$HOME` elsewhere that ends in the user name (the
  account's real home is then no home word), a symlinked or non-canonical
  spelling of the home directory, and the dash-encoded form of the home path
  that some tools write in directory names (`/home/zzq` as `-home-zzq`). The
  follow-up card "Identity check: take the home directory from the user
  database and catch its dash-encoded form" tracks them.
- The owner segment is read from what `git remote get-url --all origin`
  resolves from the repository's configuration (`.git/config`, and the
  owner's own global configuration for `url.<base>.insteadOf`). No commit or
  push can change that configuration, and the owner's machine and
  environment are trusted; tracked content, which can arrive from elsewhere,
  is not consulted (no allow-list, attribute or configuration file in the
  tree). The URL given to the hook as its second argument is not used, so
  the hook and the gate check judge from the same address.
- Only `https://`, `ssh://` and `git://` URLs and the scp-like form whose host
  is `github.com` and whose path is exactly `<owner>/<repository>` name an
  owner. Anything else is undeterminable and nothing is skipped: no `origin`,
  more than one URL for it, another host (an ssh host alias included), a
  local path or `file://` URL, or a git error. A fork's `origin` names the
  fork's owner, whose handle is just as public.
- The gate check prints a note that the user-name word test was skipped and
  why, without the value. The hook says nothing extra; its refusals are
  unchanged in form.
- The functions that read the owner and judge the home path (`owner_is`,
  `home_is`) are the same text in both scripts, and
  `tests/check-pre-push-hook.sh` fails if they differ. The self-tests cover:
  skipped when equal, not skipped when different, not skipped when
  undeterminable (two URLs for `origin` included), not skipped when `$HOME`
  is unset, `/`, ends in `/` or has another last component, the home path
  and the host name still refused when skipped, a longer word holding the
  user name refused in a ref name only, no note for a user name that is not
  the owner, and the address forms.

## Reversal

Delete the hook, the three checks and the warning in `scripts/test.sh`.
Nothing else depends on them. Record 0003 stands on its own.

## Addendum 2026-11-17: every commit, every pushed file, no tooling exemption (FIX114, #492)

The architecture review of 2026-11-17 (findings AR-07, AR-08 and AR-11)
found three routes around this guard: the commit hook ran the identity check
only for the paths of its invariant filter, so a commit touching only
`scripts/`, `stuff/`, `.githooks/` or `third_party/` skipped it; the push
hook scanned no file a pushed commit adds, and a branch push comes before
the gate; and the tooling exemption above let a commit be published under a
`claim/` or `gate-clear/` ref and then skipped as published when a pull
request branch or `main` pushed it later. The commit hook's secret pattern
also missed fine-grained GitHub tokens. So, superseding the section
"Tooling branches" and, for file contents and file names, the first item of
"Not scanned" in the known limits:

- The commit hook runs `tests/check-no-local-identity.sh` on every commit,
  beside the memory check, whatever paths are staged; it reads the index
  being committed. The other invariants keep their path filter, so a commit
  under `scripts/` runs these two checks and no other. Measured at this
  repository's size, a commit touching only `scripts/` takes 4.2 to 4.8 s
  in the hook against 1.5 to 1.6 s before (three runs each); the difference
  is the check's own self-test and scan.
- The secret pattern gains `github_pat_` followed by 22 or more of
  `[A-Za-z0-9_]`. The staged diff is read once into a file, as text and
  without colour, external diff or textconv, so a git error, a committed
  `.gitattributes` (`-diff`, `binary`, a `diff=` driver), a local diff
  setting or a change of type alone (a symbolic link replaced by a file) no
  longer takes a file out of the scan; a diff that cannot be read
  and a `grep` error refuse the commit.
- The push hook scans the files the pushed commits add, under every ref
  name. For each ref, the blobs the remote lacks (`git rev-list --objects
  <tip> --not <remote sha> --remotes=<remote>`, the walk git itself makes to
  choose what to send: a blob in the tree of a published parent is not
  scanned again) and the paths its new commits add or change, every
  directory name included, one with no file below it too (`git diff-tree -r
  -t -c --root`, deletions left out, since deleting a file publishes
  nothing; for a merge, the paths that differ from every parent) are
  scanned by `tests/check-no-local-identity.sh`
  itself, so there is one matcher, the gate's: the host name and home path
  as substrings, the user name as a whole word, the owner-handle rule above,
  binary files (a NUL byte) scanned as bytes (FIX125). The hook hands the blobs to the
  check under placeholder names made of punctuation (no name can hold a
  value) in a temporary index, and the paths as the text of one more blob
  per ref in a temporary object directory given to git as an alternate of
  the repository's objects; nothing is written to the repository. One run
  of the check covers the whole push, and a push that adds no blob and no
  path runs none. A missing check, a check that fails or ends without its
  verdict, a match the hook cannot place, an object git cannot list or read
  (a corrupt one included), and a temporary path that git's colon-separated
  list of alternates cannot carry refuse the push; the commit cap is
  unchanged. Measured, a push of 50 commits adding 200 files to a scratch
  bare remote takes 3.3 to 3.8 s in the hook against 0.8 to 0.9 s before
  (four runs each).
- The `refs/heads/claim/*` and `refs/heads/gate-clear/*` exemption is
  removed: the tooling now offers the pseudonymous identity this record
  waited for, the identity shim of `.claude/rules/no-machine-names.md`. The
  tooling writes a claim (`claim-card.sh claim`) and a gate clearance
  (`kanban.sh gate-clear`) as a commit on the tree of
  `refs/remotes/origin/main`, with that commit as its parent, so it adds no
  blob and changes no path; the messages are `claim <ID> by <holder> at <UTC
  time> (lease 6h) [nonce:<16 hex digits>]`, the holder
  `<token>/agent/<session>`, and `gate-clear <ID> by <host>/<user> at <UTC
  time>`, which under the shim reads `<token>/agent`. A release deletes the
  claim ref, or, where the delete is refused, writes a tombstone `released
  <ID> by ...` as a root commit on the empty tree. Before the removal, the
  commits that the 92 current tooling refs add over every other published
  ref (88 commits) were scanned without printing a value: 0 host-name, 0
  user-name and 0 home-path matches, and the new hook passes all 92 refs. A
  tooling ref is scanned like any other ref, so a commit that holds a value
  is refused at its first push, under any name, and is not published for a
  later push to skip.
- The host name and the user name are read from `/bin/hostname -s` and
  `/usr/bin/id -un`, by absolute path, in the push hook and in
  `tests/check-no-local-identity.sh` alike (one block of text, `real_names`,
  that `tests/check-pre-push-hook.sh` compares between the two), never by a
  `PATH` lookup. The shim is first on `PATH` for every call that writes a
  marker, and its `hostname` prints the token and its `whoami` prints
  `agent`: a hook that looked them up on `PATH` compared each marker against
  its own pseudonym, so it refused every claim, heartbeat, release, adoption
  and gate clearance made under the shim, and it compared nothing against the
  real names, so a marker that named the real machine passed. `/bin/hostname
  -s` is the program `hostname -s` ran without the shim (on the machine this
  was written on it also equals `scutil --get LocalHostName`), and `id -un`
  is the POSIX spelling of `whoami`. A tool that is missing, fails or prints
  nothing refuses the push, and now also fails the gate check (it used to
  warn and check the other name only), so the commit hook refuses too.
- Both hooks export `GIT_NO_REPLACE_OBJECTS=1`, which the checks they run
  inherit: a local `refs/replace/` entry changes what a scan reads but not
  what a push sends or a commit records, so the scans read the real objects.
  (With git 2.54, `git cat-file`, `git rev-list --objects`, `git diff-tree`
  and `git grep --cached` read a replacement; `git diff --cached`, which the
  secret scan reads, did not.)
- The commit hook's secret pattern also gains `sk-ant-` followed by 20 or
  more of `[A-Za-z0-9_-]`, `AIza` followed by 35 of `[0-9A-Za-z_-]`, `glpat-`
  followed by 20 or more of `[A-Za-z0-9_-]` and `sk_live_` followed by 24 or
  more of `[A-Za-z0-9]`.
- The self-tests no longer serve synthetic names through `hostname` and
  `whoami` shims on `PATH`, which the hooks no longer read. The hook and the
  gate check take test-only words instead: `YQ2_IDENTITY_TEST_HOST` and
  `YQ2_IDENTITY_TEST_USER` add one host and one user name to the real ones,
  under the rules of their kind (the owner-handle and own-author rules are
  judged for each user name on its own, so a test word cannot change how the
  real one is compared), and `YQ2_IDENTITY_TEST_TOOL` (`host-missing`,
  `host-empty`, `user-missing`, `user-empty`) runs a path that cannot exist,
  or `/usr/bin/true`, in place of one tool. None of them can take a real
  name out of a check: they add a word or make the check refuse.
- Known limits, unchanged in kind: a binary file is scanned as bytes (FIX125;
  before, the gate check skipped it); `YQ2_IDENTITY_TEST_EXTRA_WORD` is not looked for in files,
  since the gate check has no test word; the gate check refuses an index
  blob it cannot read (FIX124: `git grep` reports a missing blob on stderr
  and exits 1, its no-match status, so the check refuses any word on
  `git grep`'s stderr and any blob `git cat-file --batch-check` reports
  missing), and the push hook hands it only blobs git has listed and read,
  and the commit hook's own read of the staged diff refuses a staged blob
  git cannot read;
  the hooks run the checks in the working tree, so a working-tree change to
  a check or a hook changes what runs, and `tests/check-pre-commit-hook.sh`
  and `tests/check-pre-push-hook.sh` in the gate refuse a hook that no
  longer refuses.
- `tests/check-pre-commit-hook.sh` (in the gate) proves the commit hook with
  real `git commit` runs in throwaway repositories: the identity word under
  each formerly skipped path, which checks run for which paths, one
  synthetic sample per token family (built at run time, so no token-shaped
  string is in the tree), the `.gitattributes` and local diff settings and
  the change of type above, and every fail-closed path. `tests/check-pre-push-hook.sh` adds
  real pushes of a commit that adds a file holding the word to a normal
  branch, `claim/` and `gate-clear/` and then a normal branch again (all
  refused, none published), file contents, symbolic link targets, file and
  directory names (a directory with no file below it included, alone and in
  a merge), an intermediate commit, a merge, a deletion, a blob the
  remote has, two refs in one push, a 50-commit push and the fail-closed
  paths of the file scan. Both run with the identity shim first on `PATH`
  throughout: markers in the tooling's shape and token form, a release and a
  file holding `agent` and the token pass; a marker naming the real host, or
  the real user as a path component (built at run time from the absolute
  tools and never printed), a staged file naming either, a pushed commit or
  a staged file replaced locally by a clean twin, and each broken tool are
  refused; and one synthetic sample per new token family is refused while
  one a character short is not.

Reversal of this addendum: put the identity check back under the path
filter in `.githooks/pre-commit`, delete `collect`, `filescan` and their
calls from `.githooks/pre-push`, restore the exemption block, and delete
`tests/check-pre-commit-hook.sh`. Reading the names by absolute path is not
reversed on its own: with the shim on `PATH`, a `PATH` lookup refuses every
marker again.
