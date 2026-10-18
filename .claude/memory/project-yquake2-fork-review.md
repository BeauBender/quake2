---
name: project-yquake2-fork-review
description: State of the quake2 security review (Oct 2026) - fork is an unmodified yquake2 mirror; id 3.19 original cloned alongside; three-way comparison done; verdict and open defects
metadata:
  node_type: memory
  type: project
  originSessionId: 4bb2b1af-a9c5-43ee-ab2c-fa8fae19d86e
  modified: 2026-10-18T04:51:05.804Z
---

~/Developer/quake2 (origin BeauBender/quake2) was byte-identical to yquake2/yquake2 master at 9dbf6992 on 2026-10-17. id's original GPL release (version 3.19, single commit 372afde, Jan 2012) is cloned read-only at ~/Developer/quake2-id-3.21 for comparison. The user is deciding whether to base work on the fork or the 2001 original, and wants minimal trusted dependencies.

Three-way comparison results (2026-10-17): fork is ~47% id-derived, ~35% new yquake2 code, ~18% vendored (stb_*, miniz/minizip, glad, HandmadeMath, DG_dynarr, gyro.c). Fork fixes all 7 High-severity remote defects in 3.19 (configstring strcpy, Cbuf_Execute line[1024], baseline entity OOB, server $cvar expansion, raw savegame fn pointers, PCX RLE overflow, TGA int overflow). Still live in BOTH trees: svc_download size unchecked vs cursize (src/client/cl_download.c ~815), savedir ".." check is a no-op (src/server/sv_save.c ~433/510), cinematic Huffman decoder unbounded (cl_cin.c), areabits len, unfiltered stufftext (by design). Partial: CM_LoadMap lump offsets, MD2 inner offsets.

Minimal-trust build the Makefile supports: `make WITH_CURL=no WITH_OPENAL=no WITH_EXECINFO=no WITH_RPATH=no client game ref_soft` leaves SDL + libc + libdl. Vendored libs cannot be switched off; removal is a source edit.

**Why:** Saves re-deriving the whole comparison when work resumes; records which defects remain to fix.
**How to apply:** Recommendation given: base on fork, strip optional deps, fix the shared residual defects, then audit parsers. See [[feedback-review-the-fork-itself]].
