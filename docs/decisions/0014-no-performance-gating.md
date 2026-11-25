# 0014. No feature is gated on performance

Status: decided by the owner, 2026-11-22.
Plans: [the ray-tracing design](../plans/metal-raytracing-design.md)
(sections 2, 6 and 8), [the presentation modes plan](../plans/presentation-modes.md)
(sections 2.1, 2.3, 3, 4.5, 7 and call P3) and
[the port card list](../plans/metal-port-cards.md) (rows RT7, RT9 and RT10).

## Context

The plans tied features to what a device was thought able to handle. The
ray-tracing design made a frame rate an acceptance criterion: RT7 was to
hold every effect on at 60 fps at 1080p on apple7, and RT9 was to hold the
M6's cell of the target matrix by the default policy. The presentation
modes plan greyed the RT-primary presentation on apple6 to apple8, which
have ray tracing, because nothing had been measured there, and left the
rule to a measurement on an apple7 Mac (PM10). Several cards could
therefore not close without an M1-class Mac.

An M1 (apple7) supports Metal ray tracing (`supportsRaytracing`). What it
lacks is hardware intersection and the render-stage path, which change
how fast the traversal runs, not whether it runs.

## Decision

1. No feature is gated on what we think a device can handle. The player
   chooses the settings and makes their own trade-offs.
2. Gating is by capability only. A feature is unavailable only where it
   cannot run at all: for example where `supportsRaytracing` is false
   (TASK60), or where there is no extended-range display for HDR (it falls
   back to SDR, as now). apple7 supports Metal ray tracing, so apple7 is
   excluded from no ray-traced feature or presentation, and neither is any
   other family with ray tracing.
3. Performance targets are not acceptance criteria for any card. A time,
   a rate or a budget a card records is information. A final, optional
   performance round may come later; it optimizes and never gates.
4. No card needs an M1-class Mac.

## Filed with this decision

The owner asked for a built-in, non-agentic settings optimizer, filed as
a feature, as cards. The player ranks priorities and sets a target frame
rate; a fixed in-engine benchmark adjusts the settings over bounded
rounds and saves the result as a named preset. It is a tool the player
runs: its result is a starting point, and it never removes a choice. This
record does not design it. How shipped defaults are chosen, including
whether they may differ by device class, is the open question below.

## Supersedes

Earlier records are not rewritten; the plan text below is marked in place
as replaced by this record, and every measured number in it is kept.

- RT7's "everything on at least 60 fps at 1080p on apple7" in
  [the ray-tracing design](../plans/metal-raytracing-design.md): the RT7
  row of section 6 ("fps with everything on ≥ 60 at 1080p on apple7,
  number recorded"), the "RT7 and RT9" paragraph of section 2 that
  measures it as a cell of the target matrix, and the RT7 row of section
  8's verdicts table that judges it. The same target in the RT7 row of
  [the port card list](../plans/metal-port-cards.md) ("the fps target in
  the design document is met on the T1 host").
- The apple9-only limit for the RT-primary presentation in
  [the presentation modes plan](../plans/presentation-modes.md): the
  menu rule of section 2.3 ("RT-primary on apple6 to apple8 [...]: needs
  hardware ray tracing"), the apple6 to apple8 row of the section 3
  hardware-class table ("greyed until TASK106 and a device run"), row
  PM10 of section 7 ("the gating rule of section 2.3 set from it"), and
  call P3 of section 9 ("The call: apple9 and later, greyed with its
  reason elsewhere, until PM10"), with the text in sections 2.1 and 4.5
  that states the same limit.
- The owner decision recorded on GitHub issue #539 (2026-11-18), as far as
  it sets this rule: "RT-primary only on hardware ray tracing (apple9 and
  later) until PM10 measures more". The issue's other decisions (P1, P2,
  P4, the menu names, the stock default, greying a mode until the owner
  accepts its look) are not changed.

## Consequences

- RT-primary is offered on every device with ray tracing and the other
  capabilities its mode needs; on apple6 to apple8 it runs on the compute
  path. Section 2.3's "needs ray tracing" rule still greys it where
  `supportsRaytracing` is false.
- RT7, RT9 and RT10 record their times and rates as information. RT9's
  "holds the cell by the default policy" and RT7's 60 fps are not pass
  marks. The target matrix's budgets and section 8's verdicts stay as
  measurements and as analysis.
- A card that asked for an apple7 or other M1-class host (TASK106, #249;
  PM10, #539; the apple7 columns of RT10, RT11 and RT13) needs none: such
  a measurement is optional, taken when a host is available, and its
  absence fails nothing. The orchestrator amends the cards' acceptance
  lines on GitHub.
- A menu reason that rests on performance or GPU family rather than
  capability is out of line with this record. The menu rules in
  `src/client/menu/vidrules.c` and the `doc/040_cvarlist.md` text are
  surveyed separately and changed by their own cards, not here.
- Defaults chosen from measured cost (the design's default policy, option
  A's per-family heights) are not gates: they never remove or grey a
  choice. How shipped defaults are chosen is the open question below.

## Reversal

A later record that names this one restores performance gating, and the
plan text marked as replaced by 0014 is unmarked. Nothing in the engine
changes with this record.

## Open

- May default settings still differ by device class, as long as nothing
  is forced or hidden?

Answered. The owner answered on 2026-11-24: yes (reading (a) on #697).
Shipped cvar defaults may differ by device class, and that covers the
optimizer's starting point. Nothing may be forced or hidden; a capability
stays the only reason an item is unavailable.
