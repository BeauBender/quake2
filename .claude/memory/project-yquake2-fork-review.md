---
name: project-yquake2-fork-review
description: State of the quake2 fork work (Oct 2026) - review verdict, open defects, the 37 kanban cards (INFORM1-9, TASK1-28), the two-seam Metal port decision, hardware tiers for the Metal/RT plan, and the onboarding PR branch
metadata:
  type: project
---

~/Developer/quake2 (origin BeauBender/quake2) was byte-identical to yquake2/yquake2 master at 9dbf6992 on 2026-10-17. id's original GPL release (version 3.19, single commit 372afde) is cloned read-only at ~/Developer/quake2-id-3.21. Verdict: base work on the fork (it fixes all 7 High-severity remote defects of the 2001 code); shrink trust surface to id-derived code + SDL3 + OS.

Still live in the fork: svc_download size unchecked (cl_download.c ~815), savedir ".." check is a no-op (sv_save.c ~433/510), cinematic Huffman unbounded, areabits len, collision BSP lump/index checks partial, MD2 inner offsets. Servers can stufftext arbitrary console commands (by design).

Kanban (GitHub Issues on BeauBender/quake2, enabled 2026-10-18): INFORM1-7 are owner-gated decisions (ogg->wav, remove stb_image, remove gl3, vendor SDL3 pinned, CI scope, fork docs identity, RT approach). TASK1-25 are work cards: 1-2 security residuals, 3-6 remove curl/openal/gles/cmake+winbuild+appbundle, 7 CI pin (dep INFORM5), 8 minizip/miniz, 9 stb_image_write (dep 8), 10 stb_vorbis (dep INFORM1), 11 stb_image (dep INFORM2), 12 gl3 (dep INFORM3, 5), 13 SDL3-only, 14 vendor SDL3 (dep INFORM4, 13), 15 audit small vendored, 16 manifest+hash gate, 17-18 parser audits, 19 fork docs (dep INFORM6), 20 static-link mode, 21 gl1-on-Metal via an in-tree OpenGL-subset shim grown against GL-call pins (re-scoped 2026-10-18; dep 13, INFORM8, 27), 22 iOS/iPad (dep 20, 21), 23 tvOS/visionOS (dep 22), 24 hybrid RT compute (dep 28, INFORM7), 25 render-stage RT perf (dep 24), 26 repo access lockdown, 27 pin-harness hooks (Vid_WriteScreenshot capture, timedemo scenarios, ref-API record/replay, local pin store; dep INFORM8, INFORM9), 28 specialize the shim into native ref_metal with frame pins as acceptance (dep 21, 27). INFORM8 = two-seam pinning + shim-then-specialize decision; INFORM9 = pin fixtures (local store, synthetic traces in CI, no downloads). The generic harness is the bender-plugins gl-to-metal plugin (LOOP119-126, anchor docs/plans/gl-to-metal.md on that repo's main). GPTK 4 does not translate OpenGL (DirectX/DXIL only); its generic Metal skills in apple/game-porting-toolkit are reference material. Plan anchors: docs/plans/trust-boundary.md and docs/plans/metal-renderer.md (Phase A to be amended per INFORM8).

Hardware tiers: this Mac is a VM ("Apple Paravirtual device", macOS 27, Xcode 27): Metal families apple1-5 + mac2 only, no RT, no Metal 3. T1 = bare-metal Apple silicon without HW RT (M1 Max): Metal RT API available in compute from apple6, so all RT code can be built there. T2 = M3+ (owner's M6 mini): hardware RT, supportsRaytracingFromRender; reserve for perf validation and TASK25.

Onboarding branch worktree-devloop-gate (CLAUDE.md, .claude/dev-loop.json, scripts/test.sh gate = make server game, plan anchors) pushed; PR pending review panel as of 2026-10-18. The Stop hook already auto-pushed .claude/memory to main (7a8684ec). Plugin bugs found during init filed on BeauBender/claude-plugins as #192-#195.

**Why:** Avoid re-deriving the comparison, the card graph and the hardware constraints.
**How to apply:** Pull ready cards with /dev-loop:orchestrate; INFORM cards unblock dependents when the owner closes them. See [[feedback-review-the-fork-itself]] and [[feedback-devloop-workflow]].
