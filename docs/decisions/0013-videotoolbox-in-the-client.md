# 0013. VideoToolbox is linked, weak, into the macOS client for `upscale_textures … vt`

Status: decided by the owner, 2026-11-17, on TASK167 (#437) ("TASK167:
yes"). Carried out by TASK167, slice 2 of P6. Follows
[decision 0012](0012-metalfx-in-the-client.md).
Plan: [high-resolution](../plans/high-resolution.md) (section 4, P6, and
section 6, "System frameworks").

## Context

Section 6 of the plan says a system framework (VideoToolbox among them)
needs the owner's approval when its card is filed. P6's second card adds
VideoToolbox's machine-learning super-resolution scaler
(`VTSuperResolutionScalerConfiguration`, macOS 26) as a backend of the
client's `upscale_textures`. Its model is not part of the OS install:
macOS downloads it on request, once per machine, which is also the
owner's call.

## Decision

1. `VideoToolbox.framework` is weak-linked (`-weak_framework VideoToolbox`)
   into the client executable (`quake2`) on macOS. Not in the iOS build,
   not in any renderer, the server or the game module.
2. The client uses it only for `upscale_textures <2|4> vt`, in one source
   file (`src/client/cl_upscale_vt.m`). Every call is behind
   `@available(macOS 26.0, *)`; on an older macOS, on iOS, or where
   `isSupported` says no, the backend reads as unsupported and the
   command falls back to scale2x with one line.
3. The model is fetched only by a `vt` run, after a line that says the
   operating system will download it. The menu's check and the support
   checks never fetch it; the gate's host test never fetches it.
4. The approval covered running `vt` on the M6 for testing, which made
   macOS fetch the model there once.

## Reversal

Revert TASK167's change: the `vt` argument, `cl_upscale_vt.m` and the
link flag go, and the other backends are unchanged.
