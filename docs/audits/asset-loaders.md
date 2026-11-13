# Static audit of the asset loaders (TASK18)

A malformed-input audit of every file-format parser that remains in the tree
after the dependency removals (TASK3, TASK4, TASK5, TASK8; TASK11, the
stb_image removal, has not landed). Each loader was read in full against the
question "what does a hostile file make this code do". Every value read from a
file and then used as a size, loop bound, index, pointer offset, allocation
size or path is listed with the check that guards it, or the absence of one.

Plan anchor: [trust-boundary plan](../plans/trust-boundary.md), "Audits after
removal". Threat model: the "Security-relevant boundaries" section of
`CLAUDE.md`.

## Contents

1. [Method and coverage](#method-and-coverage)
2. [Where each asset can come from](#where-each-asset-can-come-from)
3. [Collision BSP](#collision-bsp-srccommoncollisionc)
4. [Pak directory and paths](#pak-directory-and-paths-srccommonfilesystemc)
5. [Renderer BSP](#renderer-bsp)
6. [MD2 and SP2 models](#md2-and-sp2-models)
7. [Images: PCX, WAL, M8, M32 and the stb_image callers](#images-pcx-wal-m8-m32-and-the-stb_image-callers)
8. [WAV](#wav-srcclientsoundwavec)
9. [Cinematics](#cinematics-srcclientcl_cinc)
10. [Entity strings and map-fix overrides](#entity-strings-and-map-fix-overrides)
11. [By design / out of scope](#by-design--out-of-scope)
12. [Defects](#defects)
13. [Cards to file](#cards-to-file)

Verdicts: `ok` (a check bounds the value before the use), `ok-by-construction`
(the value cannot be out of range, reason given), `defect` (a malformed file
reaches an out-of-bounds access, an overflow or a hang; numbered `Dn` and
listed under [Defects](#defects)), `hardening` (not exploitable as written, but
fragile or relying on an unstated invariant). Already-carded findings are marked
"carded as FIXn" and not counted again as new defects.

Card references: FIX1 is issue #40 (download header), FIX3 is issue #44
(remaining collision and MD2 indices), FIX10 is issue #73 (leaf area/cluster,
MD2 hunk leak). TASK2 (#45 on main) added `CMod_CheckLumps`,
`CMod_CheckReferences`, node/brushside plane and child checks, and the MD2,
cinematic and areabits bounds that this audit treats as the baseline.

## Method and coverage

Filled in at the end of the audit.

## Where each asset can come from

Filled in at the end of the audit.

## Collision BSP (`src/common/collision.c`)

Loaded by `CM_LoadMap` on the server and, with `clientload`, on the client for
every map it connects to. The whole file is read with `FS_LoadFile`, which
allocates `length + 1` bytes with `calloc` (`FS_LoadFile2`, `Z_TagMalloc`), so
the buffer is zero-filled and has one NUL past the end. Fixed-size static
arrays hold the result (`map_leafs[MAX_MAP_LEAFS]`, `map_planes[MAX_MAP_PLANES+12]`,
`map_nodes[MAX_MAP_NODES+6]`, `map_areaportals[MAX_MAP_AREAPORTALS]`,
`map_visibility[MAX_MAP_VISIBILITY]`, and so on). Load order:
texinfo, leafs, leafbrushes, planes, brushes, brushsides, models, nodes,
`CMod_CheckReferences`, areas, areaportals, visibility, entities, box hull,
area flood.

| Function | Field | Use | Check (quoted) or absence | Verdict |
|---|---|---|---|---|
| `CM_LoadMap` | file length | header copy `header = *(dheader_t *)buf` | `if (length < (int)sizeof(dheader_t))` | ok |
| `CM_LoadMap` | `header.version` | format gate | `if (header.version != BSPVERSION)` | ok |
| `CMod_CheckLumps` | every `lumps[i].fileofs`, `.filelen` | base pointer `cmod_base + l->fileofs` and lump length in every loader | `l->fileofs < 0 \|\| l->filelen < 0 \|\| (long long)l->fileofs + (long long)l->filelen > (long long)length` | ok |
| all `CMod_Load*` | `fileofs` alignment | `in = (void *)(cmod_base + l->fileofs)` cast to `int`/`float` structs | none; an odd offset gives misaligned loads | hardening (UB, harmless on x86 and arm64, flagged by UBSAN) |
| `CMod_LoadSubmodels` | model count | `map_cmodels[i]` writes | `l->filelen % sizeof(*in)`, `count < 1`, `count > MAX_MAP_MODELS` | ok |
| `CMod_LoadSubmodels` | `headnode` | start node of every trace and leaf query on a submodel | `CMod_CheckReferences`: `m->headnode >= numnodes \|\| (m->headnode < 0 && (-1 - m->headnode) >= numleafs)` | ok |
| `CMod_LoadSubmodels` | `firstface`, `numfaces` | not used by collision | n/a | ok |
| `CMod_LoadSurfaces` | texinfo count | `map_surfaces[i]` writes | `count < 1`, `count > MAX_MAP_TEXINFO` | ok |
| `CMod_LoadSurfaces` | `texture[32]` | `Q_strlcpy(out->c.name, in->texture, sizeof(out->c.name))` | destination bounded; `Q_strlcpy` scans the source to its NUL, so an unterminated name reads on into the next records | hardening (the read stops at the zero pad byte `FS_LoadFile` appends at the end of the buffer) |
| `CMod_LoadSurfaces` | `nexttexinfo` | not read by collision | n/a | ok |
| `CMod_LoadLeafs` | leaf count | `map_leafs[i]` writes | `count < 1`, `count > MAX_MAP_PLANES` (the constant is misnamed but equals `MAX_MAP_LEAFS`) | ok for the loop; see `CM_InitBoxHull` |
| `CMod_LoadLeafs` | `cluster` (short) | `numclusters = out->cluster + 1`; `bitofs[cluster]` in `CM_ClusterPVS`/`PHS`; `visbits[cluster >> 3]` in `CM_HeadnodeVisible` and server PVS code | none; values below -1 index before the arrays | carded as FIX3 item 1 / FIX10 item 1 |
| `CMod_LoadLeafs` | `area` (short) | `map_areas[area]` in `CM_WriteAreaBits`, `CM_AreasConnected` (via `CM_LeafArea`) | none at load; `CM_AreasConnected` tests `area1 > numareas` (no lower bound, off by one), `CM_WriteAreaBits` tests nothing | carded as FIX3 item 1 / FIX10 item 1 |
| `CMod_LoadLeafs` | `firstleafbrush`, `numleafbrushes` (unsigned short) | `map_leafbrushes[leaf->firstleafbrush + k]` in `CM_TraceToLeaf`, `CM_TestInLeaf` | `CMod_CheckReferences`: `(int)l->firstleafbrush + (int)l->numleafbrushes > numleafbrushes` | ok |
| `CMod_LoadLeafs` | `contents` | `map_leafs[0].contents != CONTENTS_SOLID`, empty-leaf search | both enforced with `Com_Error` | ok |
| `CMod_LoadLeafBrushes` | count | `map_leafbrushes[i]` writes | `count < 1`, `count > MAX_MAP_LEAFBRUSHES` | ok |
| `CMod_LoadLeafBrushes` | each brush index | `map_brushes[brushnum]` in traces | `CMod_CheckReferences`: `map_leafbrushes[i] >= numbrushes` | ok |
| `CMod_LoadPlanes` | count | `map_planes[i]` writes | `count < 1`, `count > MAX_MAP_PLANES` | ok |
| `CMod_LoadPlanes` | `type` (int, stored in `byte`) | `p[plane->type]` when `plane->type < 3` in `CM_PointLeafnum_r`, `CM_BoxLeafnums_r`, `CM_RecursiveHullCheck` | truncation to `byte` makes the `< 3` test an unsigned range check | ok-by-construction |
| `CMod_LoadBrushes` | count | `map_brushes[i]` writes | `count > MAX_MAP_BRUSHES` | ok |
| `CMod_LoadBrushes` | `firstside`, `numsides` (unsigned) | `map_brushsides[brush->firstbrushside + i]` in `CM_ClipBoxToBrush`, `CM_TestBoxInBrush` | `CMod_CheckReferences`: `(long long)b->firstbrushside + b->numsides > numbrushsides` | ok |
| `CMod_LoadBrushSides` | count | `map_brushsides[i]` writes | `count > MAX_MAP_BRUSHSIDES` | ok |
| `CMod_LoadBrushSides` | `planenum` (unsigned short) | `&map_planes[num]` | `if (num >= numplanes)` (planes are loaded first) | ok |
| `CMod_LoadBrushSides` | `texinfo` (short) | `&map_surfaces[j]` | `if (j >= numtexinfo)`; negative maps to `&nullsurface` | ok |
| `CMod_LoadNodes` | count | `map_nodes[i]` writes | `count < 1`, `count > MAX_MAP_NODES` | ok |
| `CMod_LoadNodes` | `planenum` | `map_planes + planenum` | `planenum < 0 \|\| planenum >= numplanes` | ok |
| `CMod_LoadNodes` | `children[2]` | node walk in every query | `(child >= 0 && child >= count) \|\| (child < 0 && (-1 - child) >= numleafs)` | ok for range; a child that points back to itself or an ancestor makes `CM_PointLeafnum_r` and `CM_BoxLeafnums_r` loop forever and `CM_RecursiveHullCheck`/`CM_HeadnodeVisible` recurse until the stack overflows: carded as FIX3 item 4 |
| `CMod_LoadAreas` | count | `map_areas[i]` writes | `count > MAX_MAP_AREAS` | ok |
| `CMod_LoadAreas` | `firstareaportal`, `numareaportals` | `&map_areaportals[area->firstareaportal]` and the loop bound in `FloodArea_r` | none | carded as FIX3 item 2 |
| `CMod_LoadAreaPortals` | count | `memcpy(out, in, sizeof(dareaportal_t) * count)` into `map_areaportals[MAX_MAP_AREAPORTALS]` | `count > MAX_MAP_AREAS` (256, stricter than the 1024-entry array) | ok (the wrong constant only refuses legal maps with more than 256 portals) |
| `CMod_LoadAreaPortals` | `portalnum`, `otherarea` | `portalopen[p->portalnum]`, `&map_areas[p->otherarea]` in `FloodArea_r` | none | carded as FIX3 item 2 |
| `CMod_LoadVisibility` | lump length | `memcpy(map_visibility, ..., l->filelen)` | `if (l->filelen > MAX_MAP_VISIBILITY)` | ok |
| `CMod_LoadVisibility` | `numclusters` | swapped, not used for bounds (the leaf-derived `numclusters` is) | n/a | ok |
| `CMod_LoadVisibility` | `bitofs[c][DVIS_PVS/PHS]` | `map_visibility + bitofs` in `CM_ClusterPVS`/`PHS` | none | carded as FIX3 item 3 |
| `CM_DecompressVis` | run bytes `in[0]`, `in[1]` | output writes and input advance | output bounded: `if ((out_p - out) + c > row)`; input is never bounded by `numvisibility`, and a `00 00` pair (zero-length run) advances the input without producing output | **defect D1** |
| `CM_DecompressVis` | `row = (numclusters + 7) >> 3` | output length into `pvsrow`/`phsrow[MAX_MAP_LEAFS / 8]` | `numclusters` is at most 32768 (largest short + 1), so `row` is at most 4096 of 8192 | ok-by-construction |
| `CMod_LoadEntityString` | lump length | `memcpy(map_entitystring, ..., numentitychars)` and the NUL after it | `l->filelen < 0`, `l->filelen >= sizeof(map_entitystring)` | ok |
| `CMod_LoadEntityString` | `.ent` override length | `memcpy(map_entitystring, buffer, bufLen)` | `bufLen > 1 && bufLen < sizeof(map_entitystring) - 1` | ok |
| `CMod_LoadEntityString` | map name | `strcmp(name + nameLen - 4, ".bsp")`, `memcpy(namewe, name, nameLen - 4)` | `nameLen > (MAX_QPATH - 1)` rejects long names; a name shorter than 4 would read before the string, but the server builds it as `maps/%s.bsp` | ok-by-construction |
| `CM_InitBoxHull` | `numleafs` | `box_leaf = &map_leafs[numleafs]` and three writes through it | the room check tested nodes, brushes, leafbrushes, brushsides and planes but not leafs | **defect D2, fixed in this branch** |
| `CM_SetAreaPortalState` | `portalnum` (from the map's `func_areaportal` `style` key, see [entity strings](#entity-strings-and-map-fix-overrides)) | `portalopen[portalnum] = open` | `if (portalnum > numareaportals)`: no lower bound; the upper test is off by one but stays inside the 1024-entry array because `numareaportals` is at most 256 | **defect D3, negative side fixed in this branch** |
| `CM_ReadPortalState` | savegame bytes | `FS_Read(portalopen, sizeof(portalopen), f)` | fixed size | ok |
| `CM_InlineModel` | `*N` model number (configstring / entity `model` key) | `&map_cmodels[num]` | `(num < 1) \|\| (num >= numcmodels)` | ok |

Runtime walkers (`CM_PointLeafnum_r`, `CM_BoxLeafnums_r`, `CM_TraceToLeaf`,
`CM_TestInLeaf`, `CM_ClipBoxToBrush`, `CM_TestBoxInBrush`,
`CM_RecursiveHullCheck`, `CM_HeadnodeVisible`) read only indices validated
above; `CM_BoxLeafnums_r` bounds its output with `leaf_count >= leaf_maxcount`.

Proof for D1, D2 and D3: a host-side harness (not committed) compiled the real
`collision.c` with stubs for the filesystem, cvars and `Com_Error`, and loaded
crafted in-memory BSPs under `-fsanitize=address`. Before the two fixes in this
branch:

```text
mode 1 (MAX_MAP_LEAFS leafs):
ERROR: AddressSanitizer: global-buffer-overflow
WRITE of size 4 ... #0 in CM_InitBoxHull collision.c:345  #1 in CM_LoadMap collision.c:1912
... located 0 bytes after global variable 'map_leafs' ... of size 1048576

mode 2 (CM_SetAreaPortalState(-1, true) after loading a valid map):
ERROR: AddressSanitizer: global-buffer-overflow
WRITE of size 4 ... #0 in CM_SetAreaPortalState collision.c:193
... located 4 bytes before global variable 'portalopen' ... of size 4096

mode 3 (12-byte visibility lump: numclusters 1, bitofs {12, 12}; CM_ClusterPVS(0)):
ERROR: AddressSanitizer: global-buffer-overflow
READ of size 1 ... #0 in CM_DecompressVis collision.c:2019  #1 in CM_ClusterPVS collision.c:2052
... located 0 bytes after global variable 'map_visibility' ... of size 1048576
```

After the fixes, modes 1 and 2 end in `Com_Error` ("Not enough room for box
tree", "areaportal > numareaportals"); mode 3 still overflows (filed, not fixed).
Mode 3 passes even a strict FIX3-style `bitofs < numvisibility` check if the
lump carries two extra zero bytes for the offset to point at, so D1 is distinct
from FIX3 item 3.
