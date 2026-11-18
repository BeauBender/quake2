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
   `@available(macOS 26.0, *)`. Where `isSupported` says no, where the
   OS lacks the class, on iOS, or at a factor the scaler does not list,
   the backend reads as unsupported and the command falls back to
   scale2x with one line. The weak link is what allows a build with a
   lower deployment target to start on a macOS before 26; the Makefile
   sets none, so a default build's minimum is the build host's macOS.
3. **Who starts the model's download.** Only a press of the menu item
   (Options, "upscale textures", "VideoToolbox ML") starts it, after a
   line that says the operating system will download the model. A key
   press in the menu is a user action that stuffed text cannot make: a
   server can run console commands on a client by design
   (`svc_stufftext`), so no command fetches the model. `upscale_textures
   … vt` with the model missing, or still downloading, refuses with one
   line that says to fetch it from the menu, and writes nothing. The
   download runs on its own, so the client stays responsive (window
   events, quit); the item is greyed with "macOS is downloading the
   model" meanwhile, and pressable again when it is done, failed, or
   outlived its 900 s bound (a failure is shown, never remembered as
   unsupported). The support checks, the menu's per-frame check and the
   gate's host test never fetch it; `tests/check-upscale-vt.sh` fails if
   a file other than the menu calls the fetch. The report generator
   never fetches it either: without the model its `vt` run stops with a
   line that says so.
4. The approval covered running `vt` on the M6 for testing, which made
   macOS fetch the model there once.

## Reversal

Revert TASK167's change: the `vt` argument, `cl_upscale_vt.m` and the
link flag go, and the other backends are unchanged.
