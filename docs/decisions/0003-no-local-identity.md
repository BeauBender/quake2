# 0003. Keep the working machine's identity out of what is published

Status: accepted 2026-11-13; landed as TASK71 (#161).

## Context

The agent tooling configured in `.claude/dev-loop.json` pushed telemetry
branches to this public repository whose names contained the working
machine's host name and local user name. The branches were deleted by the
owner's decision, and the owner asked for a mechanism so it cannot recur.

## Decision

- Telemetry is not published to the remote: `"vsm": { "durable": false }`.
  What is lost is cross-machine telemetry; each machine keeps its own.
- `.githooks/pre-push` refuses a push when a ref name (local or remote), the
  author or committer name or e-mail, the message of a commit being pushed, or
  the tagger line or message of an annotated tag being pushed, contains the
  working machine's host name (`hostname -s`), user name (`whoami`) or home
  path. The values are read at run time, compared case-insensitively, skipped
  when shorter than three characters, and never written to the repository or
  printed. Ref names are matched as substrings. In commit and tag text the
  host name and home path are matched as substrings and the user name as a
  whole word, so a short user name does not trip on ordinary prose; the card
  said "contains", and this narrows it for the user name in messages only.
- `tests/check-no-local-identity.sh` (in the gate) fails when a tracked text
  file or a tracked file name contains one of them (the user name as a whole
  word or path component). It reports file and line, or a count for file
  names, never the text.
- `tests/check-pre-push-hook.sh` (in the gate) proves the hook against a
  throwaway bare repository, with a synthetic name; the real names are only
  ever added to, never replaced by, the test word. Both self-tests ignore the
  invoking user's git configuration.
- The gate prints a warning when `core.hooksPath` is not `.githooks`.

## Claim branches

The tooling's card-claim branches (`refs/heads/claim/<ID>`) carry the same
identity in their commit messages, and the loop needs them. The hook exempts
the message scan, for that ref prefix only, in one marked block; ref names are
always checked. Keeping the exemption costs: those commit messages stay
public. Removing it costs: claims are refused on push, so the loop cannot
claim cards until the tooling offers a pseudonymous identity. The exemption
ends then.

## Consequences

Recommendation on the claim exemption: keep it until the tooling offers a
pseudonymous identity; the owner decides.

What the guard does not do (known limits):

- A push from an unwired checkout is not guarded; the gate warns about it. A
  push with `--no-verify` skips the hook; that is against the rules in
  `CLAUDE.md` and nothing here can prevent it.
- The commits to scan are those the remote lacks: not the remote's own tip and
  not anything already on a remote-tracking ref of that remote. That makes
  the hook depend on local remote-tracking refs. A stale one (for a branch
  deleted on the remote) still counts as published, so run
  `git fetch --prune` after a remote-side deletion. If the remote's tip is not
  known locally the push is refused: fetch the remote first.
- More than 1000 such commits cannot be scanned, so the push is refused. This
  also happens on a first push to a remote that has no tracking refs here
  (not fetched yet, or a push by URL), where the whole history counts: fetch
  the remote first, or push in steps.
- If git cannot list or read the commits, or the host or user name cannot be
  read, the push is refused (fail closed). The gate check instead warns when
  only one of the two names is unreadable, and fails when both are.
- Not scanned: file contents and file names at push time (the gate checks
  them in the working tree), note bodies, and spellings other than the
  literal one (percent or hex encodings, a name split across lines). The
  format in which the tooling writes identities is outside this repository.
- An ordinary-word name defeats it: a host or user name that is a common
  word (a generic container name, `root`, `build`) appears in this
  repository's own text and in ordinary commit messages, so the gate fails and ordinary
  messages are refused. The remedy is to give the machine a distinctive host
  or user name; an allow rule is the owner's decision and is not provided.
- History already on the public default branch from before this mechanism
  contains such identity trailers in commit messages. This change does not
  rewrite that history, and the hook never scans it (those commits are already
  published).

## Reversal

Delete the hook, the two checks and the warning in `scripts/test.sh`, and set
`vsm.durable` back. Nothing else depends on them.
