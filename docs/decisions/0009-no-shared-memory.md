# 0009. No shared memory

Status: decided by the owner, 2026-11-14.

## Context

`.claude/dev-loop.json` set `memory.shared` to `true`. With it, the loop's
memory-sync hook committed every change under `.claude/memory/` on `main` as
`memory: sync <time>` and pushed it, with no pull request, no review and no
gate. An agent's memory directory is bridged to that tracked directory, so
whatever an agent noted for itself was published.

On 2026-11-14 a note carrying a machine path reached `main` this way, and the
local-identity check then failed on that machine.

## Decision

1. Agent memory is not synced through the repository.
2. The notes tracked under `.claude/memory/` are a frozen, reviewed set that
   changes only by pull request.
3. No hook commits or pushes on `main`.

`.claude/dev-loop.json` sets `memory.shared` to `false`.

## Consequences

- Agents on different machines no longer share notes through the repository.
- Durable guidance lives in the tracked rules and decision records, which
  change by pull request.
- The earlier note stays in history; history is not rewritten.

## Reversal

Set `memory.shared` back to `true` in `.claude/dev-loop.json` and add a
record that names this one.
