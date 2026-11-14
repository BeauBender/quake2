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
  file is binary, and skipped, when its index blob holds a NUL byte; git's
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
  a path under the home directory is still refused. The user name is still
  withheld from any file name the gate check prints.
- The skip also needs a home path that can refuse a path under this
  account's home directory: the last component of `$HOME` must equal the
  user name (case-insensitively, `home_is`). With `$HOME` unset, `/`, ending
  in `/` or naming another directory, the home-path test could not refuse
  such a path, so the user-name word test runs as on any other machine.
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
  undeterminable, not skipped when the home path is unset or elsewhere, the
  home path and the host name still refused when skipped, and the address
  forms.

## Reversal

Delete the hook, the three checks and the warning in `scripts/test.sh`.
Nothing else depends on them. Record 0003 stands on its own.
