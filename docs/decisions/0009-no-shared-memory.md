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
- The same switch stops the loop's memory-bridge hook, which linked an
  agent's memory directory to the tracked `.claude/memory/` and copied any
  notes it found there into it. A link made before this change stays until
  it is removed by hand on each machine that has one; until then an agent's
  memory writes still change tracked files in the working tree of the main
  checkout (they are no longer committed). Each machine removes its link
  after pulling this change.

## Reversal

Set `memory.shared` back to `true` in `.claude/dev-loop.json` and add a
record that names this one.

The loop also reads the switch from the environment variable
`DEVLOOP_MEMORY_SHARED`, which takes precedence over the file. Setting it to
`true` anywhere re-enables syncing, so it is not set.

## Addendum 2026-11-14: the tracked notes leave the tree (TASK103, #235)

The owner has since decided that agent memory is not kept in this
repository at all, so point 2 is superseded: the notes that were tracked
under `.claude/memory/` are removed from the tree, `.claude/memory/` is
ignored, and `tests/check-no-tracked-memory.sh` fails when any file there
is tracked. Agent memory is persisted outside the repository, as
`CLAUDE.md` describes in its working rules. Points 1 and 3 stand, and
`memory.shared` stays `false`. The removed notes stay in history; history
is not rewritten.

The check also refuses any embedded repository (a gitlink, mode 160000,
anywhere in the index) and requires the top-level `.gitignore` itself to
ignore `.claude/memory/` as a directory: the rule that decides a file there
must be the directory rule `.claude/memory` (with an optional leading or
trailing `/`), so that no later rule can re-include a file in it. A rule
only in `.git/info/exclude`, a global ignore file or a nested `.gitignore`
does not count, nor does a narrower one such as `.claude/memory/*.md`, or
`.claude/memory/*` with a negated pattern after it. The Consequences
bullet above, that memory writes still change tracked files in the
working tree, no longer applies once a checkout has this change: nothing
under `.claude/memory/` is tracked.
