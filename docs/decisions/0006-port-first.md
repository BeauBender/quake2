# 0006. Port first

Status: decided by the owner, 2026-11-13.

Number 0005 is taken by a record that lands separately.

## Context

The work that matters now is the Metal port of `ref_gl1` (the rows of
`docs/plans/metal-port-cards.md`). The loop's defaults spread effort evenly:
every card is equally ready, every change over 400 lines is reviewed as a
risky one, and each card's pull request targets `main`. Renderer-only
changes are large and low in risk, and the shim cards S3 to S6 each build on
their predecessor.

## Decision

1. Port cards are pulled first. A card outside the port queue is parked.
2. Review depth follows the tier, and the tier follows the paths. A
   renderer-only change stays tier L and gets the correctness and
   specification passes. A change that touches the build, a script, a gate
   check, or any path in `risk.mediumPaths` or `risk.highPaths` is tier M or
   H and gets all three passes (correctness, security, specification).
3. Shim cards S3 to S6 are stacked: each is cut from, and its pull request
   targets, the open pull request of its predecessor.

## What each key does

All three live in `.claude/dev-loop.json`.

- `risk.largeDiffLines` is 1500. Size only raises a tier when a change is
  over this many lines, so a source-only change under it keeps the tier its
  paths give it (L for the renderer).
- `risk.mediumPaths` replaces the plugin default when set, so it holds the
  whole default list followed by the source that parses untrusted input:
  `src/client/cl_parse.c`, `src/client/cl_download.c`,
  `src/client/cl_network.c`, `src/server/*`, `src/common/*`,
  `src/client/refresh/files/*`, `src/client/vid/refrecord.c` and
  `src/game/savegame/*`. A change there is always tier M and keeps the
  security pass. The classifier matches with shell `case` globs, where `*`
  also crosses `/`, so `src/common/*` covers its subdirectories.
- `prs.stacking` is `always`: a card whose predecessor has an open pull
  request is cut from that branch and opens its pull request against it.

## Parked cards

A card outside the port queue carries the label `gate:manual:port-first`
and is not ready. The label is removed (the card is released) only when the
card blocks a port card, or when no port card can start. A review finding
outside a port card's scope goes to a parked card, never into the port card.
Each card gets a review round budget of two; after that its scope is cut
and the rest goes to a parked card.

## Consequences

- Port changes under 1500 lines of renderer source get two review passes,
  not three. The security pass is skipped only where no untrusted input is
  parsed.
- Parked work waits, possibly for a long time.
- A stacked card cannot merge before its predecessor.

## Reversal

- Parking: remove the label `gate:manual:port-first` from the cards.
- Review depth: delete `risk.largeDiffLines` (the plugin default, 400,
  returns) and `risk.mediumPaths` (the plugin default list returns).
- Stacking: set `prs.stacking` to `auto` or `off`, or delete the key.
- The review round budget is a rule of this record; a later record that
  names this one changes it.
