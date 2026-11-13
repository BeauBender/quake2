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
- `.githooks/pre-push` refuses a push when a ref name, or the message of a
  commit being pushed, contains the working machine's host name
  (`hostname -s`), user name (`whoami`) or home path. The values are read at
  run time, compared case-insensitively, skipped when shorter than three
  characters, and never written to the repository or printed.
- `tests/check-no-local-identity.sh` (in the gate) fails when a tracked text
  file contains one of them. The user name counts only as a whole word or path
  component, so a short common name does not flag ordinary prose.
- `tests/check-pre-push-hook.sh` (in the gate) proves the hook against a
  throwaway bare repository, with a synthetic name; the real names are only
  ever added to, never replaced by, the test word.
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

- A push from an unwired checkout is not guarded; the gate warns about it.
- A new branch with more than 1000 unpushed commits is refused, since the
  commits past the cap cannot be scanned; push it in steps.
- If git cannot list the commits to scan, the push is refused (fail closed).

## Reversal

Delete the hook, the two checks and the warning in `scripts/test.sh`, and set
`vsm.durable` back. Nothing else depends on them.
