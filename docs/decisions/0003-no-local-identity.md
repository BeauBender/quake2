# 0003. Agent telemetry is not published to the remote

Status: decided by the owner, 2026-11-13.

## Context

The agent tooling configured in `.claude/dev-loop.json` published telemetry
branches to the remote. Their names carried the working machine's host name
and local user name. The repository is public. The owner had those branches
deleted.

## Decision

Telemetry is not published to the remote. `.claude/dev-loop.json` sets
`vsm.durable` to `false`.

## Consequences

- Telemetry is no longer shared across machines. It stays local to each one.
- This change does not refuse a push that carries the identity. A hook and a
  gate check for that land with a separate card.
- The tooling's card-claim, gate-clear and cadence-lock branches still carry
  the identity in their commit messages until the tooling offers a
  pseudonymous identity.
- A receipt the tooling posts as a comment on a pull request carries the
  identity in its actors row. The loop builds receipts locally and does not
  post them until the tooling offers a pseudonymous identity.
- Commit history already on the public default branch from before this
  decision carries such identity in trailers, and in the author and
  committer email of some commits. It is not rewritten.

## Reversal

Remove the `vsm` key from `.claude/dev-loop.json`.
