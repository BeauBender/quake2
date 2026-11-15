# 0010. ImageIO decodes PNG, JPEG and TGA on Apple platforms

Status: decided by the owner, 2026-11-14. Redirects INFORM2 (#2). Carried
out by TASK11 (#18). Plan: [high-resolution](../plans/high-resolution.md),
P2.

## Context

INFORM2 recorded option B: remove the vendored `stb_image.h` (8,046 lines,
with a GIF decoder that has an unpatched heap overflow, CVE-2026-5185) and
`stb_image_resize.h`, keep only the native PCX, WAL, M8 and M32 loaders,
and add a native TGA loader only if the stock data needs one. That would
also remove every PNG, JPEG and TGA replacement that `r_retexturing` loads
(`LoadHiColorImage` in `stb.c`, since TASK11
[`image.c`](../../src/client/refresh/files/image.c)).

The high-resolution plan measured what that costs:

- The stock skies are TGA files: the 1997 release ships each of its 60 sky
  faces as a 256x256 TGA and as a 256x256 PCX, and the OpenGL and Metal
  renderers load the TGA (`GetSkyImage`).
- The 2023 re-release has no upscaled 1997 texture (3 of its 4,466 WAL
  textures have a high-colour twin, none larger). Sharper textures can only
  come from texture packs or from textures upscaled on the player's own
  machine (plan P6), and both are PNG, JPEG or TGA files.
- This fork targets Apple platforms only
  ([decision 0008](0008-apple-platforms-only.md)), where ImageIO, a system
  framework, decodes these formats.

## Decision

1. On Apple platforms PNG, JPEG and TGA are decoded by ImageIO, through
   its C API (`CGImageSource`), in the shared loaders every renderer links
   (the role of `stb.c`, since TASK11
   [`image.c`](../../src/client/refresh/files/image.c)) and in the
   client's [`cl_image.c`](../../src/client/cl_image.c). Only these three
   types are accepted; the type is checked before decoding, and any other
   type ImageIO could read (GIF among them) is refused. The bounds the
   renderers already apply to a decoded image stay.
2. The vendored `stb_image.h` is removed.
3. The owner approves ImageIO as a system-framework dependency. Decision
   0001 removed vendored code that cannot be read in full; ImageIO is not
   vendored, and it is maintained and patched with the operating system.
   This approval covers ImageIO only. The other frameworks the plan names
   are approved, or not, when their cards are filed.
4. `stb_image_resize.h` is not decided here: its fate is left to TASK11.
5. INFORM2's "textures limited to the stock PCX/WAL formats" no longer
   holds: `r_retexturing` and high-colour replacements stay.

## What this does not change

- The native PCX, WAL, M8 and M32 loaders, and the stock game's look.
- The renderer API (`API_VERSION 8`), the network protocol and savegames.
- No non-Apple source is removed (decision 0008).

## Consequences

- Texture packs, the plan's locally upscaled textures and the stock TGA
  skies keep loading on Apple platforms.
- The decoding runs in-process in Apple's code. A fault in it is fixed by
  an operating-system update, not by this repository; the three-type list
  keeps the reachable decoders few.
- On the unmaintained non-Apple platforms there is no PNG, JPEG or TGA
  decoder once `stb_image.h` is gone. TASK11 keeps the stock skies drawing
  there from their PCX faces, or states the loss.
- TASK11 updates [`docs/DEPENDENCIES.md`](../DEPENDENCIES.md), the
  [trust-boundary plan](../plans/trust-boundary.md),
  [`doc/030_configuration.md`](../../doc/030_configuration.md) and
  [`CLAUDE.md`](../../CLAUDE.md), which name `stb_image`.

## Reversal

Revert TASK11's commit: `stb_image.h` and its decoding return. A later
record that names this one changes the decision.
