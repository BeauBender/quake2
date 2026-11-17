# 0012. MetalFX is also linked, weak, into the macOS client for `upscale_textures … metalfx`

Status: decided by the owner, 2026-11-17, in INFORM13 (#438), option A
("INFORM13: A"). Carried out by TASK124 (#280), slice 1. Extends
[decision 0011](0011-metalfx-framework.md).
Plan: [high-resolution](../plans/high-resolution.md) (section 4, P6, and
section 6, "System frameworks").

## Context

Decision 0011 approved MetalFX (`MetalFX.framework`) for `ref_metal` only:
"The client, the server, the game module and the other renderers [...] do
not link it."

P6 of the high-resolution plan makes MetalFX's spatial scaler a backend of
the client's `upscale_textures` command (TASK124), which writes upscaled
copies of the 8-bit textures as PNG files. The command runs in the client
executable, whichever renderer is loaded, and the renderer API
(`API_VERSION 8`) must not change. TASK124's design note
([runbook](../runbook/TASK124.md)) found no place for the backend that 0011
allows, and put three options to the owner in INFORM13: extend 0011 to the
macOS client (A), run the scaler inside `ref_metal` (B), or drop the backend
(C). The owner chose A.

## Decision

1. `MetalFX.framework` is weak-linked (`-weak_framework MetalFX`) into the
   client executable (`quake2`) on macOS, in addition to `ref_metal`.
2. The client uses it only for `upscale_textures <2|4> metalfx`: one
   source file (`src/client/cl_upscale_metalfx.m`) makes its own Metal
   device with `MTLCreateSystemDefaultDevice()` and a
   `MTLFXSpatialScaler`, scales the command's tiles and releases them when
   the run ends. No MetalFX or Metal object is made by the client at any
   other time.
3. Where the framework, the descriptor class or a Metal device is missing,
   or `supportsDevice:` says no, the backend is unsupported: the command
   says so in one line and goes on with scale2x, and the menu shows the
   backend greyed with its reason.
4. The other targets are unchanged: the dedicated server (`q2ded`), the
   game module, `ref_gl1` (with or without the Metal shim), `ref_gl3` and
   `ref_soft` do not link MetalFX, and neither does the client on any
   platform other than macOS.

## What this does not change

- The renderer API (`API_VERSION 8`), `ref.h`, every renderer, the network
  protocol and savegames.
- The stock look: `upscale_textures` writes files only when it is run, and
  with no generated file present every pin is unchanged.
- No code is vendored: MetalFX is maintained and patched with the operating
  system. The client already links Metal through the vendored SDL3, so
  MetalFX is the one framework this adds.

## Consequences

- `upscale_textures` can offer MetalFX's spatial scaler beside scale2x
  with any renderer loaded, and slice 2 of TASK124 (TASK167) can compare
  them.
- A fault inside MetalFX is fixed by an operating-system update, not by
  this repository.
- TASK124 updates [`docs/DEPENDENCIES.md`](../DEPENDENCIES.md).

## Reversal

Revert TASK124's slice-1 commits: the client links no MetalFX, the
`metalfx` backend argument is gone and `upscale_textures` is scale2x only.
Decision 0011 then stands as written. A later record that names this one
changes the decision.
