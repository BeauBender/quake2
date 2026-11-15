# 0011. MetalFX is linked into `ref_metal` on Apple platforms

Status: decided by the owner, 2026-11-14. Carried out by TASK111 (#265).
Plans: [high-resolution](../plans/high-resolution.md) (section 6, "System
frameworks"), [ray-tracing design](../plans/metal-raytracing-design.md)
(section 2, "Internal resolution and upscaling with MetalFX").

## Context

The high-resolution plan lists MetalFX among the system frameworks that add
no vendored code and says each needs the owner's approval when its card is
filed. ImageIO was approved by [decision 0010](0010-imageio-decoder.md),
whose approval covers ImageIO only.

The ray-tracing design measured MetalFX's scalers on the M6 (section 8,
"MetalFX upscaling to the native size") and left the choice between them
open (INFORM12, #262) until they can be compared on real frames, on the M6
and on modest hardware (an M1-family Mac, later an older iPhone).

On 2026-11-14 the owner wrote: "while we're developing and debugging, let's
give me menu options for the different types of MetalFX scalers, and also
for the denoiser."

## Decision

1. The owner approves MetalFX (`MetalFX.framework`) as a system-framework
   dependency of the Metal renderer.
2. Scope: `MetalFX.framework` is linked into `ref_metal` only, on Apple
   platforms. The client, the server, the game module and the other
   renderers (`ref_gl1` with or without the Metal shim, `ref_gl3`,
   `ref_soft`) do not link it.
3. It is weak-linked (`-weak_framework MetalFX`), because a target OS may
   lack it (tvOS has it only from 27.1, and visionOS has no temporal
   scaler). Where its classes are missing, every mode reads as unsupported
   and the renderer draws as with MetalFX off.
4. Every MetalFX mode is off by default (`r_metalfx 0`,
   `r_metalfx_interp 0`); with them off no MetalFX object is created.

## What this does not change

- The renderer API (`API_VERSION 8`), the network protocol and savegames.
- The stock look: with MetalFX off every pin is unchanged.
- No code is vendored: MetalFX is maintained and patched with the operating
  system.

## Consequences

- The scalers can be compared on real frames (TASK111, and the menu in
  TASK112); INFORM12 stays open until the comparison report (TASK114).
- A fault inside MetalFX is fixed by an operating-system update, not by
  this repository.
- TASK111 updates [`docs/DEPENDENCIES.md`](../DEPENDENCIES.md).

## Reversal

Revert TASK111's commits: `ref_metal` links no MetalFX and the `r_metalfx`
cvars are gone. A later record that names this one changes the decision.
