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

## Pak directory and paths (`src/common/filesystem.c`)

`FS_AddDirToSearchPath` loads `pak0.pak` to `pak99.pak` (marked protected) and
then every other `*.pak` in the directory returned by `FS_ListFiles`, so a
`.pak` that lands in the write directory (for example through a download, see
[sources](#where-each-asset-can-come-from)) is opened on the next filesystem
start. `fsPackFile_t` holds `name[MAX_QPATH]`, `size`, `offset`; a
`dpackfile_t` on disk is `name[56]`, `filepos`, `filelen`.

| Function | Field | Use | Check (quoted) or absence | Verdict |
|---|---|---|---|---|
| `FS_LoadPAK` | header read | `fread(&header, 1, sizeof(dpackheader_t), handle)` | return value ignored; a file shorter than 12 bytes leaves `dirofs`/`dirlen` as uninitialised stack | hardening (the ident test catches most short files; a 4-byte `PACK` file does not) |
| `FS_LoadPAK` | `ident` | format gate | `LittleLong(header.ident) != IDPAKHEADER` raises `ERR_FATAL` | ok for memory; a garbage `.pak` in the write directory stops the engine at every start (see D5) |
| `FS_LoadPAK` | `dirlen`, `dirofs` | `numFiles = header.dirlen / sizeof(dpackfile_t)`, `malloc(header.dirlen)`, `fseek(handle, header.dirofs, SEEK_SET)` | `(numFiles == 0) \|\| (header.dirlen < 0) \|\| (header.dirofs < 0)` | ok for sign; no upper bound against the file size (see next rows) |
| `FS_LoadPAK` | `numFiles` | `Z_Malloc(numFiles * sizeof(fsPackFile_t))` | `numFiles > MAX_FILES_IN_PACK` only prints a message; the product is computed in `size_t` and cannot wrap on 64-bit hosts (at most 33 554 431 entries of 72 bytes, about 2.4 GB) | **defect D5** (resource exhaustion: up to 2 GB `malloc` plus 2.4 GB `calloc` from a 12-byte header; `Z_TagMalloc` turns a failed allocation into `ERR_FATAL`) |
| `FS_LoadPAK` | directory bytes | `fread(info, 1, header.dirlen, handle)` into `malloc`ed (not zeroed) memory | return value ignored; a directory that runs past the end of the file leaves the tail of `info` uninitialised | **defect D4** (feeds the next row) |
| `FS_LoadPAK` | `name[56]` | `Q_strlcpy(files[i].name, info[i].name, Q_min(sizeof(files[i].name), sizeof(files[i].name)))` | the destination is bounded (the `Q_min` compares the same size twice, presumably meant to name the 56-byte source), but `Q_strlcpy` reads the source up to its NUL; a name with no NUL in the last entry, or uninitialised bytes from the short read above, reads past the end of the `info` allocation | **defect D4** (heap over-read in a loader of a downloadable file) |
| `FS_LoadPAK` | `filepos`, `filelen` | `files[i].offset`, `files[i].size` | none; neither is compared with the pack file size, and negatives are accepted | **defect D6** (see `FS_FOpenFile` and `FS_LoadFile2` rows) |
| `FS_FOpenFile` | `pack->files[i].offset` | `fseek(handle->file, pack->files[i].offset, SEEK_SET)` | return of `fseek` ignored; a negative offset leaves the stream at 0 and the caller reads the wrong bytes | D6 |
| `FS_FOpenFile` | `pack->files[i].size` | returned as the file length to every loader | none | D6 |
| `FS_LoadFile2` | size from `FS_FOpenFile` | `Z_Malloc(size + (pad < 0 ? 0 : pad))`, `FS_Read(*buffer, size, f)` | `if (size <= 0)` returns early, but only `!size` closes the handle, so a negative pak entry size leaks the open handle and `FILE *` (32 such lookups exhaust `fs_handles` and every later open fails with `ERR_DROP` "none free"); `size + 1` with `size == INT_MAX` is signed overflow; a size past the end of the pack makes `FS_Read` hit end of file twice and raise `ERR_FATAL` | D6 (crash of the engine via `ERR_FATAL`, handle leak) |
| `FS_Read` | short read | loop on `fread` | second zero-length read raises `ERR_FATAL` | ok for memory (the error is the DoS in D6) |
| `FS_FOpenFile` | requested name (from configstrings, models, maps) | normalised copy into `name[MAX_OSPATH]`; `rawname[input + 1]` look-ahead | `if (namelen > sizeof(name) - 1)` rejects long names; the look-ahead reads at most the terminating NUL | ok |
| `FS_FOpenFile` | requested name | `Com_sprintf(path, sizeof(path), "%s/%s", search->path, handle->name)` then `Q_fopen` | `..` is not removed or refused, so a server-supplied model, sound or image name can read files outside the search paths | hardening (read only; the bytes are parsed as an asset and never sent back) |
| `FS_PackQuickSearch` | pack names | binary search | `FS_SortPack` runs before the pack is searched | ok |
| `FS_CreatePath` | path | `Sys_Mkdir` on each prefix | `if (strstr(path, "..") != NULL)` refuses | ok |
| `FS_SetGamedir` | `game` (set from the server's serverdata) | search path and write directory | `!strcmp(dir, ".") \|\| strstr(dir, "..") \|\| strstr(dir, "/") \|\| strstr(dir, "\\")` | ok |

## Renderer BSP

Each renderer loads the world map a second time, from the same file, in its
own `Mod_LoadBrushModel` (`gl1_model.c`, `gl3_model.c` with `GL3_` names,
`sw_model.c`), with lump readers shared from `src/client/refresh/files/models.c`
(`Mod_LoadVertexes`, `Mod_LoadEdges`, `Mod_LoadSurfedges`, `Mod_LoadLighting`,
`Mod_LoadPlanes`, `Mod_LoadTexinfo`, `Mod_LoadVisibility`, `Mod_LoadNodes`) and
vis decompression from `files/pvs.c`. gl1 and gl3 are the same code apart
from names; where soft differs the row says so. Memory comes from
`Hunk_Begin`/`Hunk_Alloc` (`src/backends/unix/shared/hunk.c`): one `mmap`
sized from the lump lengths, and `Hunk_Alloc` raises `Sys_Error` (fatal) when
an allocation would pass the reservation, so a loop that fills a freshly
allocated array is bounded; reads and writes through file-sourced indices are
not.

The renderer has none of the lump bounds `collision.c` got from TASK2. What
protects it is order: a BSP reaches the renderer only as the world model
(`Mod_ForName` refuses a later one, "Loaded a brush model after the world"),
and on the client `CM_LoadMap` always loads the same `maps/<name>.bsp` before
`CL_PrepRefresh` calls `R_BeginRegistration` (`CL_RequestNextDownload` in
`cl_download.c`, `CL_Precache_f` in `cl_main.c`). So `CMod_CheckLumps` (every
lump inside the file), the collision count limits (leafs and nodes at most
65536, models at most `MAX_MAP_MODELS`, texinfo at most `MAX_MAP_TEXINFO`,
visibility at most `MAX_MAP_VISIBILITY` bytes) and the collision node child
checks hold for the renderer too. Everything collision does not read (faces,
edges, surfedges, vertexes, lighting, leaf faces, `nexttexinfo`, node and
submodel face ranges, the vis offsets) reaches the renderer unchecked. A
collision fix that refuses a map also protects the renderer, which is why
several rows below cite FIX3.

| Function | Field | Use | Check (quoted) or absence | Verdict |
|---|---|---|---|---|
| `Mod_ForName` | first 4 bytes | `LittleLong(*(unsigned *)buf)` format switch | none; `FS_LoadFile` allocates `length + 1`, so a 1 or 2 byte file is read up to 2 bytes past the allocation (a BSP is protected by `CM_LoadMap`; this matters for MD2 and SP2, see the next section) | **defect D7** |
| `Mod_ForName` | requested name | `strcpy(mod->name, name)` into `MAX_QPATH` | names come from configstrings (at most `MAX_QPATH - 1`) or from `Com_sprintf` into `MAX_QPATH` buffers | ok-by-construction |
| `Mod_ForName` | `*N` inline model | `&parent_model->submodels[i]` | `if (i < 1 \|\| i >= parent_model->numsubmodels)` | ok |
| `Mod_LoadBrushModel` | file length | header copy and lump swap over `sizeof(dheader_t)` | none in the renderer; `CM_LoadMap` refuses a file shorter than the header first | ok-by-construction (collision-first order) |
| `Mod_LoadBrushModel` | every `fileofs`, `filelen` | `mod_base + l->fileofs` in every `Mod_Load*` | none in the renderer; `CMod_CheckLumps` checks the same lumps of the same file first | hardening (safe only because of the call order) |
| `Mod_LoadBrushModel` | lump lengths | `hunkSize` sum via `Mod_CalcLumpHunkSize` and `calcTexinfoAndFacesSize` | `int` arithmetic without an overflow test; a lump of several hundred MB wraps the sum and a later `Hunk_Alloc` fails with `Sys_Error` | hardening (fatal error, not memory corruption) |
| `Mod_CalcLumpHunkSize`, all `Mod_Load*` | lump length modulo record size | record count | `if (l->filelen % sizeof(*in))` ("funny lump size") | ok |
| `Mod_LoadVertexes`, `Mod_LoadEdges`, `Mod_LoadSurfedges`, `Mod_LoadPlanes` | counts | array fills through `Hunk_Alloc((count + extra) * size)` | the allocation is sized from the count; there is no upper limit such as `MAX_MAP_SURFEDGES` (the comment "else it errors out later anyway" is not backed by a check; the hunk runs out and `Sys_Error`s) | ok for memory |
| `Mod_LoadPlanes` | `type` (int, stored in `byte`) | `p[plane->type]` when `< 3` | truncation to `byte` | ok-by-construction |
| `Mod_LoadEdges` | `v[0]`, `v[1]` (unsigned short) | `vertexes[edge.v[k]]` in every face walker below | none against `numvertexes` | **defect D11** |
| `Mod_LoadTexinfo` | `texture[32]` | `GetTexImage` builds `textures/%s.wal` with `Com_sprintf`; `Com_Printf("%s")` on failure | destination bounded; an unterminated name reads on into the next records, at worst to the pad byte `FS_LoadFile` appends | hardening |
| `Mod_LoadTexinfo` | `nexttexinfo` | `out->next = *texinfo + next`, then the frame count walk `for (step = out->next; step && step != out; step = step->next)`, later `R_TextureAnimation` | `if (next > 0)` only; no upper bound, and a chain that loops without coming back to its start never ends | **defect D13** |
| `Mod_LoadFaces` | `numedges` (short) | gl1/gl3 `LM_BuildPolygonFromSurface` sizes `Hunk_Alloc(sizeof(glpoly_t) + (lnumverts - 4) * ...)`; loop bound in every face walker | soft: `if (out->numedges < 3)`; gl1/gl3: none, so a negative count gives a negative `Hunk_Alloc` size, which passes the overflow test and moves the hunk cursor backwards (later allocations overlap earlier ones) | **defect D11** |
| `Mod_LoadFaces` | `numedges` of a `SURF_WARP` face | gl1 `R_SubdivideSurface`, gl3 `GL3_SubdivideSurface` copy every edge vertex into `vec3_t verts[64]` on the stack | the only limit, `if (numverts > MAX_SUBDIVIDE_VERTS)` (60), is in `R_SubdividePolygon`, called after the copy; `calcTexinfoAndFacesSize` sees `numverts > 60` but only returns 0 for the hunk estimate | **defect D10** (stack buffer overflow write of vertex coordinates from the file) |
| `Mod_LoadFaces` | `firstedge` (int) with `numedges` | `surfedges[firstedge + i]` in `Mod_CalcSurfaceExtents`, `LM_BuildPolygonFromSurface`, `R_SubdivideSurface`; soft `R_RenderFace`, `R_RenderBmodelFace`, `R_DrawSolidClippedSubmodelPolygons`, `R_BuildPolygonFromSurface` (`sw_poly.c`) | none against `numsurfedges` | **defect D11** |
| same walkers | each surfedge value `e` | `edges[e]` or `edges[-e]` | none against `numedges`; `e == INT_MIN` negates to itself | **defect D11** |
| `Mod_CalcSurfaceExtents` | vertex positions, texinfo vectors | `extents[]`, `texturemins[]`, lightmap size | soft: `extents < 16` raised to 16, `extents > 256` refused ("Bad surface extents") for lit faces; gl1/gl3: no limit, but `LM_AllocBlock` fails for a lightmap wider than the block (then `ERR_FATAL`) and `R_BuildLightMap` tests `size > (sizeof(s_blocklights) >> 4)`; a gl1/gl3 face with no edges leaves mins at 999999, so both extents are negative, `smax * tmax` is positive and `LM_AllocBlock` accepts it with `*x` past the block | soft ok; gl1/gl3 large extents ok, the no-edge case folded into D11 |
| `Mod_LoadFaces` | `styles[4]` (byte) | `r_newrefdef.lightstyles[style]` (256 entries) | byte range; 255 ends the list | ok-by-construction |
| `Mod_LoadFaces` | `lightofs` | `samples = lightdata + i`; `R_BuildLightMap` (gl1, soft) and `GL3_BuildLightMap` read `smax * tmax * 3` bytes per style; `R_LightPoint` / `GL3_LightPoint` read at `3 * (dt * smax + ds)` | only `-1` is special; no test against the lighting lump length, nor for other negative values | **defect D12** (heap over-read; the bytes become lightmap texels, so they are shown on screen) |
| `Mod_LoadFaces` | `planenum`, `texinfo` | `planes + planenum`, `texinfo + ti` | `planenum < 0 \|\| planenum >= loadmodel->numplanes`; `(ti < 0) \|\| (ti >= loadmodel->numtexinfo)` | ok |
| `Mod_LoadMarksurfaces` | each face index (short) | `surfaces + j`; soft `R_RecursiveWorldNode` writes `(*mark)->visframe` | gl1/gl3: `(j < 0) \|\| (j >= loadmodel->numsurfaces)`; soft tested only `j >= loadmodel->numsurfaces` | gl1/gl3 ok; soft **defect D15, fixed in this branch** (a write before the surfaces array) |
| `Mod_LoadLeafs` | `firstleafface`, `numleaffaces` | `firstmarksurface` and the mark loops | `(firstleafface + out->nummarksurfaces) > loadmodel->nummarksurfaces` (both widened to unsigned) | ok |
| `Mod_LoadLeafs` | `cluster` (short) | `bitofs[cluster]` in `Mod_ClusterPVS`; `vis[cluster >> 3]` in `R_MarkLeaves` (gl1, gl3, soft) | `cluster == -1` skipped; nothing else | carded as FIX3 item 1 / FIX10 item 1 (the collision check also closes the renderer, because `CM_LoadMap` runs first) |
| `Mod_LoadLeafs` | `area` (short) | `areabits[area >> 3]` in `R_AreaVisible` (`files/surf.c`); `areabits` is the client's 32-byte `frame_t.areabits` | none | carded as FIX3 item 1 / FIX10 item 1 (same reason) |
| `Mod_LoadVisibility` | `numclusters` | swap loop over `out->bitofs[i]` for `i < numclusters` in a `Hunk_Alloc(l->filelen)` block | none against the lump length: reads, and writes back, past the allocation | carded as FIX3 item 3 (its collision check of `numclusters` against the lump length closes this through the call order) |
| `Mod_ClusterPVS` | `bitofs[cluster][DVIS_PVS]` | `(byte *)model->vis + bitofs` as the input of `Mod_DecompressVis` | none | carded as FIX3 item 3 (same reason) |
| `Mod_ClusterPVS` | `numclusters` | `row = (numclusters + 7) >> 3` passed to `Mod_DecompressVis` | none; even after FIX3 item 3, `numclusters` can reach `MAX_MAP_VISIBILITY / 8` (131072), a 16384-byte row | **defect D8** |
| `Mod_DecompressVis` | run bytes | writes into `static byte decompressed[MAX_MAP_LEAFS / 8]` (8192 bytes) | `while (out - decompressed < row)` only, no bound against the buffer; a zero run is written whole, so even a legal 8192-byte row can overshoot by 254 bytes; the input is never bounded by the lump | **defect D8** (global buffer overflow write; non-zero bytes are copied from the file verbatim) |
| `R_MarkLeaves` (gl1, gl3) | `numleafs` | `memcpy(fatvis, vis, (numleafs + 7) / 8)` and an `int` OR loop into an 8192-byte stack array | none in the renderer; collision limits leafs to 65536, exactly the array | ok-by-construction (collision-first order) |
| `Mod_LoadNodes` | `planenum`, `children[2]` | `planes + planenum`, child pointers | `planenum < 0 \|\| planenum >= numplanes`; a child `>= *numnodes` or a leaf `>= numleafs` is refused | ok for range |
| `Mod_LoadNodes` | child graph | `Mod_SetParent`, `Mod_NumberLeafs` (recursive), `Mod_PointInLeaf` (loop) | a cycle recurses until the stack is gone, or loops forever | carded as FIX3 item 4 (a collision cycle check refuses the map first) |
| `Mod_NumberLeafs` | child graph | writes `r_leaftovis[leafnum]` and `r_vistoleaf[*numvisleafs]`, two `int [MAX_MAP_LEAFS]` arrays on the stack of `Mod_LoadNodes` | none; without any cycle, nodes that share children (a DAG) are walked once per path, so 17 nodes whose two children are both the next node reach the last leaf 2^17 times and `numvisleafs` runs past 65536 | **defect D9** (stack buffer overflow write; a depth-bound fix for FIX3 item 4 does not refuse a DAG; both arrays are unused after the walk) |
| `Mod_LoadNodes` | `firstface`, `numfaces` (unsigned short) | `surfaces + node->firstsurface` for `numsurfaces` entries in `R_RecursiveWorldNode` (gl1, gl3, soft), `R_MarkSurfaceLights`, `R_RecursiveLightPoint` (gl1, gl3, soft) | none against `numsurfaces` | **defect D14** (heap over-read; gl1/gl3 `R_RecursiveWorldNode` links a surface whose `visframe` matches into `image->texturechain`, a write through the stray pointer) |
| `Mod_LoadSubmodels` | `headnode` | `nodes + firstnode` for `R_MarkLights` in `R_DrawBrushModel` (gl1, gl3) and `R_PushDlights` (soft) | `if (out->firstnode >= loadmodel->numnodes)`; no lower bound, and collision accepts a negative headnode that names a leaf | **defect D14** (a pointer before the node array, read as a node) |
| `Mod_LoadSubmodels` | `firstface`, `numfaces` (int) | `&surfaces[firstmodelsurface]` for `nummodelsurfaces` entries in `R_DrawInlineBModel` (gl1, gl3), `R_DrawSolidClippedSubmodelPolygons` and `R_DrawSubmodelPolygons` (soft) | none | **defect D14** |
| `Mod_LoadSubmodels` | count | `submodels[i]` fills | sized allocation; collision limits it to `MAX_MAP_MODELS` | ok |
| `R_InitSkyBox` (soft) | counts plus the six sky faces | writes into the `extra` slots the loaders reserved | `loadmodel->numedges > MAX_MAP_EDGES` and the matching surface and vertex tests raise `ERR_DROP` | ok |

Proof for D8, D9, D10 and the two `nexttexinfo` failures of D13: host-side
harnesses (not committed) compiled the real `files/models.c`, `files/pvs.c`
and `gl1/gl1_warp.c` with stubs for `Hunk_Alloc`, `Com_Error` and the GL
entry points and ran them under `-fsanitize=address`:

```text
Mod_LoadNodes, 17 nodes, both children of node i = node i+1, last node -> leaf 1:
ERROR: AddressSanitizer: stack-buffer-overflow ... WRITE of size 4
    #0 in Mod_NumberLeafs models.c:385  #1..#3 in Mod_NumberLeafs models.c:390
Address ... is located in stack of thread T0 at offset 524576 in frame
    #0 in Mod_LoadNodes models.c:405

Mod_LoadVisibility, 4-byte lump with numclusters = 1000000:
ERROR: AddressSanitizer: heap-buffer-overflow ... READ of size 4
    #0 in Mod_LoadVisibility models.c:508
... located 0 bytes after 4-byte region

Mod_LoadTexinfo, texinfo 0 -> 1 -> 1 -> ...: no return (killed by a 10 s timeout)

Mod_LoadTexinfo, one texinfo with nexttexinfo = 100000:
ERROR: AddressSanitizer: BUS on unknown address ... READ memory access
    #0 in Mod_LoadTexinfo models.c:651

Mod_DecompressVis, row 25000 (numclusters 200000), literal 0x41 bytes:
ERROR: AddressSanitizer: global-buffer-overflow ... WRITE of size 1
    #0 in Mod_DecompressVis pvs.c:60
... located 0 bytes after global variable 'Mod_DecompressVis.decompressed' ... of size 8192

R_SubdivideSurface, one SURF_WARP face with 200 edges:
ERROR: AddressSanitizer: stack-buffer-overflow ... WRITE of size 4
    #0 in R_SubdivideSurface gl1_warp.c:271
Address ... is located in stack of thread T0 at offset 800 in frame
    #0 in R_SubdivideSurface gl1_warp.c:246
```

D11, D12 and D14 are static traces, high confidence: each is an array index
taken from the file with no comparison anywhere between the read and the use.
D15 is the one-line fix in this branch (soft `Mod_LoadMarksurfaces` now tests
`j < 0` like gl1 and gl3).
