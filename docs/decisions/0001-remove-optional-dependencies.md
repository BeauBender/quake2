# 0001. Remove optional external and vendored dependencies

Status: accepted 2026-10-18; landed 2026-10-18 to 2026-10-19. Plan: [trust-boundary](../plans/trust-boundary.md).

## Context

The fork started byte-identical to yquake2. About 18% of the tree was
vendored third-party code, and several features loaded external libraries at
run time. The owner wants to run and extend the engine without trusting code
that cannot be read in full.

## Decision

Remove the optional pieces whose feature the game does not need:

| Removed | Card | Replacement |
|---|---|---|
| OpenAL sound backend | TASK4 (#43) | SDL audio, already the default |
| GLES1 and GLES3 renderers and their glad loaders | TASK5 (#53) | `gl1`, `gl3` and `soft` |
| libcurl and HTTP downloads | TASK3 (#54) | UDP downloads through the game protocol |
| minizip and miniz, `.pk3`/`.zip` packs | TASK8 (#55) | `.pak` files only; zip packs must be extracted |

Further removals (stb libraries, the GL3 renderer, SDL provenance) are open
cards in the plan, several gated on owner decisions.

## Consequences

- Smaller trust surface: no HTTP client, no zip parser, no second audio or
  GL stack.
- Lost features: HTTP downloads from servers that advertise them, zip-based
  mods without extracting them, GLES devices.
- `tests/check-removed-deps.sh` fails the gate if any of these comes back.

## Reversal

Each removal is one squashed commit on `main`; reverting it restores the
feature. The check above must be updated in the same change.
