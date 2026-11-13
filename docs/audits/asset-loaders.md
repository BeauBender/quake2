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
13. [Cards filed](#cards-filed)

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

Each loader was located by symbol, read in full (not grepped), and every value
read from a file that is later used as a size, count, loop bound, index,
pointer offset, allocation size or path was tabled with the check that guards
it or the note that none does. Where a function compiles without SDL or GL, a
throwaway host-side harness (in a scratch directory, not committed) linked the
real function against minimal stubs and ran crafted in-memory inputs under
`-fsanitize=address`; its output is quoted in the relevant section. Harnesses
were built for `collision.c` (the earlier commits), `files/models.c`,
`files/pvs.c`, `files/pcx.c` and `gl1/gl1_warp.c`. Renderer surface, light and sound code
pulls in SDL/GL or the full client import struct and was not harnessed; those
findings are static traces with a stated confidence.

Files read in full: `src/common/collision.c`, `src/common/filesystem.c`,
`src/client/refresh/files/{models.c,pcx.c,wal.c,stb.c,pvs.c,surf.c,common.c}`,
`src/client/refresh/{gl1,gl3,soft}/*_model.c`, the `_surf.c`, `_light.c`,
`_lightmap.c`, `_mesh.c`, `_warp.c`, `_image.c`, `sw_alias.c`, `sw_poly.c`,
`sw_polyset.c`, `sw_rast.c`, `sw_bsp.c`, `sw_sprite.c` consumers of the fields
the loaders set, `src/client/sound/{wave.c,sound.c,sdl.c}`,
`src/client/cl_cin.c`, `src/game/g_spawn.c`, `src/common/shared/shared.c`
(`COM_Parse`), and `src/client/cl_download.c`.

Out of scope, not read as loaders: the vendored `stb_image.h`,
`stb_image_resize.h` and `stb_vorbis.h` internals (cards exist to remove them;
only the repo code that calls them is audited), the gl3 GLSL shader text, and
the OGG decode path in `sound/ogg.c` beyond the `wavinfo_t` it produces (it is
`stb_vorbis`, vendored). The savegame reader (`src/game/savegame/savegame.c`)
is a separate trust story (local files) and is not an asset loader.

## Where each asset can come from

This sets the severity of every finding, so it is stated precisely and
verified in `src/client/cl_download.c`.

On the client, the model, sound and image names are the server's
configstrings (`cl.configstrings[CS_MODELS..]`, `CS_SOUNDS..`, `CS_IMAGES..`,
plus the sky in `CS_SKY` and player skins). `CL_RequestNextDownload` walks
those configstrings and, for each name, calls `CL_CheckOrDownloadFile`, whose
only gate is `CL_DownloadFilter`. That filter rejects a name that starts with
`.` or `/`, contains `:` or `..`, contains a library extension anywhere
(`.dll`, `.dylib`, `.lib`, `.so`, `.a`, tested with `strstr`, so `x.so.wav`
is refused too), matches the `cl_nodownload_list` cvar, or already exists. It
does **not** restrict the asset extension. Model configstrings are requested
verbatim, so they can name a `.md2`, `.sp2` or `.bsp` (the map is
`CS_MODELS + 1`); the other classes are requested under names the client
builds: sounds as `sound/%s`, pics as `pics/%s.pcx`, sky faces as
`env/%s%s.pcx` or `.tga`, wall textures as `textures/%s.wal`, and MD2 skins
by the names inside the downloaded model. Any other name, for example an
`.m8`, `.m32`, `.png`, `.jpg` or `.cin`, can be fetched with the `download`
console command, which takes an arbitrary name and which a server can issue
through `svc_stufftext`. `allow_download` is created with default `"1"`
(`Cvar_Get("allow_download", "1", CVAR_ARCHIVE)` in `sv_main.c`, shared with
the client in the integrated engine), `allow_download_models`, `_sounds` and
`_maps` default to `"1"` and `allow_download_players` to `"0"`; they gate
only whether a class is fetched, not what is accepted. So with stock settings
effectively every format in this audit can arrive over the network from a
server the user connected to, as a downloaded file that a loader then reads.

Even with `allow_download 0`, the same loaders run on whatever files are
already in the search path, and a map, model or sound referenced by the server
is loaded from a `.pak` or loose file the user may have installed from an
untrusted source (custom maps and mods are routinely shared as `.pak`). The
`.pak` directory itself (`FS_LoadPAK`) is parsed from any `*.pak` that lands in
a search directory, including the write directory a download writes to.

On the server, the untrusted asset is the map: `CM_LoadMap` parses the BSP and
its entity string, and the `.ent` override is a loose file. A dedicated server
that loads a community map runs the collision and entity-string parsers over
attacker-authored bytes.

The practical conclusion: the renderer BSP, MD2/SP2, image, WAV and cinematic
findings are all reachable from a malicious or compromised server against a
default client, which is why the server-reachable memory-corruption findings
are rated high (see the `sev` column under [Defects](#defects)). The
pak-directory findings (D4, D5, D6) need a hostile `.pak` in a search path, a
lower but real bar, and none of them is a memory-corruption write, so they are
medium.

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

mode 3 (visibility lump whose cluster offsets point at its very end; CM_ClusterPVS(0)):
ERROR: AddressSanitizer: global-buffer-overflow
READ of size 1 ... #0 in CM_DecompressVis collision.c:2019  #1 in CM_ClusterPVS collision.c:2052
... located 0 bytes after global variable 'map_visibility' ... of size 1048576
```

After the fixes, modes 1 and 2 end in `Com_Error` ("Not enough room for box
tree", "areaportal > numareaportals"); mode 3 still overflows (filed, not fixed).
Mode 3 passes even a strict FIX3-style `bitofs < numvisibility` check if the
lump carries a little padding after the offsets for the offset to point at, so
D1 is distinct from FIX3 item 3.

## Pak directory and paths (`src/common/filesystem.c`)

`FS_AddDirToSearchPath` loads `pak0.pak` to `pak99.pak` (marked protected) and
then every other `*.pak` in the directory returned by `FS_ListFiles`, so a
`.pak` that lands in the write directory (for example through a download, see
[sources](#where-each-asset-can-come-from)) is opened on the next filesystem
start. `fsPackFile_t` holds `name[MAX_QPATH]`, `size`, `offset`; a
`dpackfile_t` on disk is `name[56]`, `filepos`, `filelen`.

| Function | Field | Use | Check (quoted) or absence | Verdict |
|---|---|---|---|---|
| `FS_LoadPAK` | header read | `fread(&header, 1, sizeof(dpackheader_t), handle)` | return value ignored; a file shorter than 12 bytes leaves `dirofs`/`dirlen` as uninitialised stack | hardening (the ident test catches most short files; a file holding only the ident does not; carded under FIX22) |
| `FS_LoadPAK` | `ident` | format gate | `LittleLong(header.ident) != IDPAKHEADER` raises `ERR_FATAL` | ok for memory; a file named `*.pak` that is not a pack, in a search path, stops the engine at every start (see "By design / out of scope") |
| `FS_LoadPAK` | `dirlen`, `dirofs` | `numFiles = header.dirlen / sizeof(dpackfile_t)`, `malloc(header.dirlen)`, `fseek(handle, header.dirofs, SEEK_SET)` | `(numFiles == 0) \|\| (header.dirlen < 0) \|\| (header.dirofs < 0)` | ok for sign; no upper bound against the file size (see next rows) |
| `FS_LoadPAK` | `numFiles` | `Z_Malloc(numFiles * sizeof(fsPackFile_t))` | `numFiles > MAX_FILES_IN_PACK` only prints a message; the product is computed in `size_t` and cannot wrap on 64-bit hosts (at most 33 554 431 entries of 72 bytes, about 2.4 GB) | **defect D5** (resource exhaustion: up to 2 GB `malloc` plus 2.4 GB `calloc` from a header alone; `Z_TagMalloc` turns a failed allocation into `ERR_FATAL`) |
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
| `Mod_LoadBrushModel` | lump lengths | `hunkSize` sum via `Mod_CalcLumpHunkSize` and `calcTexinfoAndFacesSize` | `int` arithmetic without an overflow test; a lump of several hundred MB wraps the sum and a later `Hunk_Alloc` fails with `Sys_Error` | hardening (fatal error, not memory corruption; carded under FIX17: sum in 64-bit and refuse past a limit) |
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
| `Mod_NumberLeafs` | child graph | writes `r_leaftovis[leafnum]` and `r_vistoleaf[*numvisleafs]`, two `int [MAX_MAP_LEAFS]` arrays on the stack of `Mod_LoadNodes` | none; without any cycle, nodes that share children (a DAG) are walked once per path, so a chain of nodes whose two children both name the next node reaches the last leaf exponentially often and `numvisleafs` runs past 65536 | **defect D9** (stack buffer overflow write; a depth-bound fix for FIX3 item 4 does not refuse a DAG; both arrays are unused after the walk) |
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
Mod_LoadNodes, a short chain of nodes with shared children reaching a leaf:
ERROR: AddressSanitizer: stack-buffer-overflow ... WRITE of size 4
    #0 in Mod_NumberLeafs models.c:385  #1..#3 in Mod_NumberLeafs models.c:390
Address ... is located in stack of thread T0 at offset 524576 in frame
    #0 in Mod_LoadNodes models.c:405

Mod_LoadVisibility, a lump far shorter than its numclusters implies:
ERROR: AddressSanitizer: heap-buffer-overflow ... READ of size 4
    #0 in Mod_LoadVisibility models.c:508
... located 0 bytes after 4-byte region

Mod_LoadTexinfo, a texinfo whose nexttexinfo chain loops: no return (killed by a 10 s timeout)

Mod_LoadTexinfo, a nexttexinfo far beyond the texinfo count:
ERROR: AddressSanitizer: BUS on unknown address ... READ memory access
    #0 in Mod_LoadTexinfo models.c:651

Mod_DecompressVis, a row length far above the 8192-byte output buffer:
ERROR: AddressSanitizer: global-buffer-overflow ... WRITE of size 1
    #0 in Mod_DecompressVis pvs.c:60
... located 0 bytes after global variable 'Mod_DecompressVis.decompressed' ... of size 8192

R_SubdivideSurface, one SURF_WARP face with far more edges than the 64-entry array:
ERROR: AddressSanitizer: stack-buffer-overflow ... WRITE of size 4
    #0 in R_SubdivideSurface gl1_warp.c:271
Address ... is located in stack of thread T0 at offset 800 in frame
    #0 in R_SubdivideSurface gl1_warp.c:246
```

D11, D12 and D14 are static traces, high confidence: each is an array index
taken from the file with no comparison anywhere between the read and the use.
D15 is the one-line fix in this branch (soft `Mod_LoadMarksurfaces` now tests
`j < 0` like gl1 and gl3).

## MD2 and SP2 models

`Mod_ForName` in each renderer dispatches on the first four bytes (D7) to
`Mod_LoadMD2` or `Mod_LoadSP2` in `src/client/refresh/files/models.c`; the
three renderers share both loaders. Unlike a BSP, a model is loaded by the
renderer alone, so nothing in `collision.c` checks it first. MD5 models are
named in the card's context, but no MD5 loader exists in this tree: a
case-insensitive search for `md5` under `src/` finds nothing, and
`Mod_ForName` knows only `IDALIASHEADER`, `IDSPRITEHEADER` and
`IDBSPHEADER`. MD2 and SP2 are the only model formats.

### MD2 (`Mod_LoadMD2`)

TASK2 added the size, count and range checks quoted below. The copy goes
into `Hunk_Alloc(ofs_end)` at the same offsets as the file, so a range that
lies inside `ofs_end` in the file also lies inside the copy.

| Function | Field | Use | Check (quoted) or absence | Verdict |
|---|---|---|---|---|
| `Mod_LoadMD2` | file length | header copy | `if (modfilelen < (int)sizeof(dmdl_t))` | ok |
| `Mod_LoadMD2` | `version` | format gate | `if (version != ALIAS_VERSION)` | ok |
| `Mod_LoadMD2` | `ofs_end` | `Hunk_Alloc(ofs_end)`, then the header swap writes `sizeof(dmdl_t)` bytes into it; the bound of every range test | `if (ofs_end < 0 \|\| ofs_end > modfilelen)`: no lower bound, so any `0 <= ofs_end <= modfilelen` is accepted and an `ofs_end` below `sizeof(dmdl_t)` makes the header copy write past the `Hunk_Alloc` block (it stays inside the `Hunk_Begin(modfilelen)` reservation, so later hunk allocations overlap it) | hardening (carded under FIX18: refuse `ofs_end < sizeof(dmdl_t)`) |
| `Mod_LoadMD2` | `ofs_skins`, `ofs_st`, `ofs_tris`, `ofs_frames`, `ofs_glcmds` | destination of each section copy into the hunk block | `MD2_RANGE_BAD` tests only `ofs < 0` and the end against `ofs_end`; an offset below `sizeof(dmdl_t)` is accepted, so a section copy overwrites the validated, byte-swapped header. The glcmd copy runs before the skin copy: with `ofs_glcmds` inside the header it restores the file's unclamped `num_skins`, and the skin `memcpy` and the loop `skins[i] = find_image(...)` then run past the `MAX_MD2SKINS` clamp | **defect D31** (writes image pointers past the caller's `skins[MAX_MD2SKINS]` array in `model_t`; the skin `memcpy` also outruns the range it was checked for) |
| `Mod_LoadMD2` | every early `return NULL` after `Hunk_Begin` | the hunk is not freed | none | carded as FIX10 item 2 |
| `Mod_LoadMD2` | `skinheight` | soft `R_AliasSetupSkin`: `scaleheight = asset_height / s_pmdl->skinheight` | `if (pheader->skinheight > MAX_LBM_HEIGHT)`; zero and negatives pass | **defect D17** |
| `Mod_LoadMD2` | `skinwidth` | soft `R_AliasSetupSkin`: `scalewidth = asset_width / s_pmdl->skinwidth` | none | **defect D17** |
| `Mod_LoadMD2` | `num_xyz` | `s_lerped[MAX_VERTS]` (gl1/gl3), soft `verts + num_xyz` | `num_xyz <= 0`, `num_xyz > MAX_VERTS`; soft also tests `(verts + s_pmdl->num_xyz) >= verts_max` | ok |
| `Mod_LoadMD2` | `num_st`, `num_tris`, `num_frames`, `num_glcmds` | loop bounds and ranges | `<= 0` each, then `MD2_RANGE_BAD(ofs, cnt, size)` in 64-bit arithmetic against `ofs_end` | ok |
| `Mod_LoadMD2` | `num_skins` | `skins[MAX_MD2SKINS]` fill, skin name range | clamped by `if (pheader->num_skins > MAX_MD2SKINS)`; `num_skins < 0` refused; range test on `ofs_skins` | ok |
| `Mod_LoadMD2` | `framesize` | stride of the frame copy and of every frame lookup | `pheader->framesize < (int)(sizeof(daliasframe_t) + (pheader->num_xyz - 1) * sizeof(dtrivertx_t))` refused, range test over `num_frames * framesize` | ok |
| `Mod_LoadMD2` | `index_xyz[3]`, `index_st[3]` | soft `R_AliasPreparePoints`: `verts[index_xyz]`, `pstverts[index_st]` | none on the values | carded as FIX3 item 5 |
| `Mod_LoadMD2` | glcmd stream | gl1 `R_DrawAliasFrameLerp`, gl3 `DrawAliasFrameLerp`: vertex count words and `order[2]` as `s_lerped[index_xyz]` | the last word is only reported (`possible last element issues`), the walk is not bounded by `num_glcmds` | carded as FIX3 item 5 |
| `Mod_LoadMD2` | skin names (`MAX_SKINNAME` bytes each) | `find_image` on the copied name | not terminated by the loader; an unterminated last name reads into the bytes that follow in the hunk copy | hardening |
| `Mod_LoadMD2` | frame `name[16]` | `memcpy` of a fixed 16 bytes | fixed size | ok |
| `Mod_LoadMD2` | vertex `lightnormalindex` (byte) | `r_avertexnormals[lightnormalindex]` in gl1/gl3 `R_LerpVerts` (shell effect) and soft `R_AliasTransformFinalVerts`; the table has `NUMVERTEXNORMALS` (162) rows | none; values 162 to 255 read up to 1116 bytes past the static table (`shadedots[...]` has 256 columns and is fine) | **defect D16** (static over-read, garbage normals) |
| `Mod_LoadMD2` | `st` coordinates (short) | soft `R_ProcessLeftEdge` (called from `R_RasterizeAliasPolySmooth`): texel address `(s >> 16) + (t >> 16) * skinwidth` into the skin pixels, with `s = st.s * scalewidth` | none; the coordinates are never compared with the skin size (gl1/gl3 pass them to the GPU, which clamps or wraps) | **defect D17** (heap over-read of texels, drawn on screen; confidence medium, static trace through the rasterizer setup) |
| `R_DrawAliasModel` (gl1), `GL3_DrawAliasModel`, `R_AliasSetupFrames` (soft) | entity frame vs `num_frames` | frame lookup | `(e->frame >= paliashdr->num_frames) \|\| (e->frame < 0)` resets to 0, same for `oldframe` | ok |

### SP2 (`Mod_LoadSP2`)

`dsprite_t` is three ints followed by `numframes` records of four ints and a
64-byte name. Nothing compares the file length with either.

| Function | Field | Use | Check (quoted) or absence | Verdict |
|---|---|---|---|---|
| `Mod_LoadSP2` | file length | reads `ident`, `version`, `numframes` and then `sprin->frames[i]`; writes `sprout->frames[i]` into `Hunk_Alloc(modfilelen)` | none; a file shorter than 12 bytes, or with fewer frame records than `numframes`, is read past its end, and the copy is written past the `modfilelen`-byte allocation | **defect D18** |
| `Mod_LoadSP2` | `version` | format gate | `if (sprout->version != SPRITE_VERSION)`; the early return leaks the hunk like the MD2 paths | ok for memory; hunk leak in the class of FIX10 item 2, which names only MD2, so carded under FIX18 (hardening) |
| `Mod_LoadSP2` | `numframes` | frame loop, `skins[i]` (`MAX_MD2SKINS` entries), later `currententity->frame %= psprite->numframes` in gl1 `R_DrawSpriteModel`, `GL3_DrawSpriteModel` and soft `R_DrawSprite` | upper clamp only: `if (sprout->numframes > MAX_MD2SKINS)`; zero is accepted, a division by zero (SIGFPE on x86-64); a negative count makes `frame % numframes` fall in `0 .. -numframes - 1`, so `frames[frame]` and `currentmodel->skins[frame]` index past the 32-entry array and the result is used as an image pointer | **defect D18** |
| `Mod_LoadSP2` | frame `name[64]` | `find_image(name, it_sprite)` | `memcpy` of 64 bytes, not terminated | hardening |
| `R_DrawSprite` (soft) | frame `width`, `height`, `origin_x`, `origin_y` | polygon size and texel span | `Q_min(s_psprframe->width, skin->width)` and the same for height bound the texel reads; gl1/gl3 use them only as float geometry | ok |

Proof for D18, with the harness from the renderer BSP section: an SP2 file
holding only its header, whose frame count claims more records than the file has.

```text
ERROR: AddressSanitizer: heap-buffer-overflow ... READ of size 4
    #0 in Mod_LoadSP2 models.c:292
... located 0 bytes after 13-byte region   (the 12-byte file plus the pad byte)
```

Proof for D31, with the same harness: a well-formed small MD2 whose skin
count is above `MAX_MD2SKINS` and whose glcmd section starts inside the header,
with the caller's skin array allocated at its real `MAX_MD2SKINS` size:

```text
ERROR: AddressSanitizer: heap-buffer-overflow ... WRITE of size 8
    #0 in Mod_LoadMD2 models.c:227
... located 0 bytes after 256-byte region
```

D31 is high: an `.md2` is requested from a server verbatim from the
`CS_MODELS` configstrings, and the write is a pointer store into the
following `model_t` fields. The fix is the lower bound on every section
offset (`ofs_* >= sizeof(dmdl_t)`) and on `ofs_end` (`ofs_end >=
sizeof(dmdl_t)`).

D16 is a static trace with high confidence (a byte indexes a 162-row table
with no test). D7 needs a 1 or 2 byte `.md2`/`.sp2` (any file name a server
names as a model): high confidence, low impact.

## Images: PCX, WAL, M8, M32 and the stb_image callers

Images arrive as skins named in MD2/SP2 files, as wall textures named in BSP
texinfo, as pics named in configstrings and HUD layouts, and as sky faces.
`R_LoadImage` (`files/stb.c`) dispatches on the extension; `R_FindImage` in
each renderer calls it and then `load_image` (`R_LoadPic8` soft,
`LoadWal`/`R_LoadPic` gl1/gl3) copies and scales the result. The `.wal`,
`.m8`, `.m32` and `.pcx` loaders are the repo's own code and are audited in
full. The TGA/PNG/JPG path goes through the vendored `stb_image.h` and the
resampler `stb_image_resize.h`: the stb internals are out of scope (cards
exist to remove the vendored files), so only the repo code around the stb
calls (dimension handling, the `width * height` the repo computes, the
scale2x/scale3x and resize buffers) is listed.

### PCX (`files/pcx.c`, `PCX_Decode`)

The decode allocates one output buffer from the header dimensions and fills
it through `PCX_RLE_Decode`, which bounds both ends of every run.

| Function | Field | Use | Check (quoted) or absence | Verdict |
|---|---|---|---|---|
| `PCX_Decode` | file length | header read | `if (len < sizeof(pcx_t))` | ok |
| `PCX_Decode` | `manufacturer`, `version`, `encoding` | format gate | explicit equality tests | ok |
| `PCX_Decode` | `xmax-xmin`, `ymax-ymin`, `bytes_per_line`, `color_planes`, `bits_per_pixel` | dimensions, loop bounds | each tested `<= 0` and rejected with "Bad pcx file" | ok |
| `PCX_Decode` | `full_size = (h+1)*(w+1)` (`*4` for 24/32-bit) | `out = malloc(full_size)`, then every row written at `pix += (pcx_width + 1)` (`* 4`) | `int`, no overflow test; each dimension is a difference of two file-supplied shorts, so `pcx_width + 1` and `pcx_height + 1` can be large, and the product (with or without the `* 4`) can wrap to a small value. The row writes do not depend on the RLE input: the 8-bit path `memcpy(pix, line, pcx_width + 1)` and the 24/32-bit plane loop write a full row whether or not `PCX_RLE_Decode` stopped at the end of the input, so the header dimensions alone decide it | **defect D19** (heap buffer overflow write, ASAN-confirmed in the 8-bit and the 24-bit mode, see below) |
| `PCX_RLE_Decode` | run byte, data byte | writes `pix[x++]`, reads `*raw++` | `if (raw >= raw_max)` before each read, `if (pix_max <= (pix + x))` before each write; both set `image_issues` and stop | ok |
| `PCX_Decode` (24/32-bit) | `line[x + bytes_per_line * j]` | plane assembly into `pix[4*x+j]` | `line` is `malloc(Q_max(bytes_per_line, pcx_width+1) * color_planes)`, and the inner index stays below that; `bytes_per_line <= pcx_width` only sets `image_issues` | ok |
| `PCX_Decode` (8-bit palette) | palette at `pcx + len - 768`, marker at `pcx + len - 769` | `memcpy(*palette, ..., 768)` | guarded by `if (len > 768)`; else `image_issues` | ok |
| `PCX_Decode` (1/2/4-bit) | `line[i * bytes_per_line + (x >> 3)]`, `line[x / div]` | bit unpack | `line` is `bytes_per_line * color_planes` (or `bytes_per_line`); `x <= pcx_width` and `bytes_per_line > 0` keep the index inside, since `x/div <= pcx_width` is only safe when `bytes_per_line > pcx_width/div` — a tiny `bytes_per_line` with a large width reads `line[x/div]` past the 1-byte row | **defect D20** (heap over-read of the decode line buffer; low severity, read only, bounded by `pcx_width`) |
| `PCX_Decode` | trailing-data check | `if (data - (byte *)pcx > len)` frees and nulls | after the fact | ok |
| `fixQuitScreen` | fixed offsets into a 320x240 buffer | `memcpy` of 11 rows | reached only when `LoadPCX` confirms `width == 320 && height == 240` and the block checksum matches `pics/quit.pcx` | ok-by-construction |
| `GetPCXInfo` | `xmax`, `ymax` | `*width = xmax + 1`, `*height = ymax + 1` | no header-size check before the read (`FS_LoadFile` returned the bytes, but a file shorter than `pcx_t` is read past its end) | **defect D21** (over-read of up to `sizeof(pcx_t)` bytes; the values seed `realwidth`/`realheight` in `LoadHiColorImage`, which only gates whether a hi-res replacement is used) |

### WAL / M8 / M32 (`files/wal.c`)

All three share the shape: header, then `offsets[0]` to the first mip. The
body check `((size - ofs) / height) < width` rejects a body too small for
`width * height`; the M32 variant multiplies `width * 4` in `int` and is
bypassed by a wrapping width (D27).

| Function | Field | Use | Check (quoted) or absence | Verdict |
|---|---|---|---|---|
| `LoadWal` | file length | header read | `if (size < sizeof(miptex_t))` | ok |
| `LoadWal` | `width`, `height`, `offsets[0]` | `load_image((byte*)mt + ofs, width, height, size - ofs, ...)` | `(ofs <= 0) \|\| (width <= 0) \|\| (height <= 0) \|\| (((size - ofs) / height) < width)` | ok |
| `LoadM8` | `version` | format gate | `LittleLong(mt->version) != M8_VERSION` | ok |
| `LoadM8` | `width[0]`, `height[0]`, `offsets[0]` | palette expand loop and `load_image` | same body check as `LoadWal` | ok |
| `LoadM8` | each source byte | `mt->palette[value]` | `value` is a `byte`, the palette has 256 entries; `image_buffer = malloc((size - ofs) * 4)` matches the loop `i < (size - ofs)` | ok |
| `LoadM32` | `version` | format gate | `LittleLong(mt->version) != M32_VERSION` | ok |
| `LoadM32` | `width[0]`, `height[0]`, `offsets[0]` | `load_image((byte *)mt + ofs, width, height, (size - ofs) / 4, ...)`; the renderer then walks `width * height` 4-byte pixels of the file buffer | `((size - ofs) / height) < (width * 4)`; `width * 4` is `int` and wraps for any `width` above `INT_MAX / 4`, so a hostile width with a small product passes against a small body. gl1 `R_LoadPic` → `R_Upload32` (→ `SmoothColorImage` with `r_scale8bittextures`) → `R_Upload32Native` → `R_LightScaleTexture` rewrites `width * height` pixels in place, then scans the alpha bytes and hands the buffer to `glTexImage2D`; without NPOT support `R_Upload32Soft` resamples from the same buffer. gl3 `GL3_Upload32` scans the alpha bytes over `width * height` pixels and passes the buffer to `glTexImage2D`. soft `R_LoadPic` converts only `data_size` pixels and `R_LoadPic8` allocates `R_GetImageMipsSize(width * height)` itself, so it stays in bounds (one very large allocation, `ERR_FATAL` if it fails) | **defect D27** (gl1: heap buffer overflow write past the file buffer; gl3: heap over-read; the fix is the comparison in 64-bit arithmetic, `(long long)width * height * 4 > size - ofs`) |
| `GetWalInfo` | `width`, `height` | `*width = LittleLong(mt->width)`, `*height = ...`, which become `realwidth`/`realheight` in `LoadHiColorImage` (`files/stb.c`) | `if (size < sizeof(miptex_t))` bounds the header read; no version test (`miptex_t` has none) and the values are not checked: zero, negative and huge pass | hardening (see the verdict note below the table) |
| `GetM8Info` | `width[0]`, `height[0]` | as `GetWalInfo` | `size < sizeof(m8tex_t) \|\| LittleLong(mt->version) != M8_VERSION`; values not checked | hardening |
| `GetM32Info` | `width[0]`, `height[0]` | as `GetWalInfo` | `size < sizeof(m32tex_t) \|\| LittleLong(mt->version) != M32_VERSION`; values not checked | hardening |

The three `Get*Info` functions read only the header they size-check, so they
are memory-safe themselves. Their results matter only when a TGA, PNG or JPG
replacement with the same base name loads: `LoadHiColorImage` uses the
replacement if `width >= realwidth && height >= realheight`, which bounds
positive values by the stb dimensions but lets zero (replaced by the stb size)
and negative values through. A negative `realwidth`/`realheight` then reaches
the renderer: gl1 and gl3 copy it into `image->width` only when it is smaller,
soft `R_LoadPic` uses it as the resize target (`malloc(uploadwidth *
uploadheight * 4)`, `ResizeSTB`) and as `asset_width`/`asset_height`, the
skin size of the soft alias rasterizer. Static trace, medium confidence on the
downstream effect; carded under FIX19 as hardening (reject non-positive
dimensions in the three functions).

### stb_image callers (repo code only)

| Function | Field | Use | Check (quoted) or absence | Verdict |
|---|---|---|---|---|
| `LoadSTB` | `rawsize`, returned `w`, `h` | `stbi_load_from_memory`, then `*width = w`, `*height = h` | stb validates its own input and dimensions; the repo trusts the returned `w`/`h` | out of scope (stb internal), but the dimensions flow into the next rows |
| `R_LoadImage`, `LoadHiColorImage` | `width * height` | passed as `size` to `load_image` | `int`; stb caps a dimension at `1 << 24` by default, so two large images multiply past `INT_MAX` | **defect D22** (the product is used as a copy length and mip base; the fix is a 64-bit guard like `R_Convert8to32` already has) |
| `LoadHiColorImage` | `width >= realwidth && height >= realheight` | gate on using the replacement | compares the stb result with the PCX/WAL/M8/M32 real size | ok |
| `R_Convert8to32` (gl1, gl3) | `width`, `height` | `malloc(width * height * sizeof(unsigned))` | `if (height == 0 \|\| width > INT_MAX / sizeof(*trans) / height)` raises `ERR_DROP` | ok |
| `scale2x`, `scale3x` (`files/stb.c`) | `width`, `height` | reads `src[width*y + x]`, writes `dst` up to `(width*height) << 2` / `* 9` | no internal bound; callers size the destination and guard the product (next rows) | ok by caller |
| `gl1_image.c`, `gl3_image.c` scale path | `width`, `height`, `scale` | `malloc(width * height * scale * scale)` then `scale2x`/`scale3x` | `if (height == 0 \|\| scale == 0 \|\| width > INT_MAX / height / scale / scale)` raises `ERR_DROP` | ok |
| `sw_image.c` `R_LoadPic8` scale path | `width * height * 4` | `malloc` then `scale2x` | no overflow guard on `width * height * 4` here (the gl renderers added one; soft did not) | **defect D22** (same class) |
| `sw_image.c` `R_LoadPic8` | `size`, `data_size` | `full_size = R_GetImageMipsSize(size)`, `malloc(full_size)`, `memcpy(pixels[0], pic, data_size)` | `data_size > full_size` is clamped to `full_size`; `size` and `data_size` both come from the loader as `width * height`-class values and agree | ok |
| `sw_image.c` resize path | `uploadwidth * uploadheight * 4` | `malloc(pic32)`, `ResizeSTB` | dimensions are `realwidth`/`realheight` or halved copies of the loader size; no overflow guard, same `int` product class | hardening (carded under FIX19 with D22: the same 64-bit guard) |

Severity note: D19 is high. The wrapped `full_size` is a small allocation, not
an enormous one, so nothing fails first: the header dimensions alone
decide the allocation size, and each row write lands past the
allocation. A PCX is requested from a server as `pics/%s.pcx` for every
`CS_IMAGES` configstring and as a model skin, so this is a server-reachable
heap write against a default client. Proof: a host-side harness (not
committed) compiled the real `files/pcx.c` with stubs for `ri`,
`Com_Printf` and the byte-order helpers and called `PCX_Decode` on a
small in-memory file under `-fsanitize=address`:

```text
8-bit, dimensions at the header maximum:
ERROR: AddressSanitizer: heap-buffer-overflow ... WRITE of size 65536
    #0 in __asan_memcpy  #1 in PCX_Decode pcx.c:372
... located 0 bytes after 1-byte region

24-bit, large dimensions:
ERROR: AddressSanitizer: heap-buffer-overflow ... WRITE of size 1
    #0 in PCX_Decode pcx.c:275
... located 0 bytes after 1-byte region
```

D27 (M32) is a static trace with high confidence: the wrap is plain `int`
arithmetic, and the gl1 in-place rewrite of `width * height` pixels follows
from `R_Upload32Native` and `R_LightScaleTexture` with no other size in
between. It is high: `.m32` is never named by the download walk (wall
textures are requested as `textures/%s.wal`, and `GetTexImage` tries
`textures/%s.m32` when no `.wal` loads), but the `download` console command,
which a server can issue through `svc_stufftext`, fetches any name the filter
accepts. The write needs gl1 (`vid_renderer gl1`; the default is gl3, where
the same file gives a heap over-read). D22 depends on stb's own dimension cap
and is medium. D20 and D21 are read-only and low. The WAL, M8 and stb paths
were not harnessed (they go through `load_image` into a renderer); the
`PCX_RLE_Decode` bounds were confirmed by reading every read and write in the
decode loop.

## WAV (`src/client/sound/wave.c`)

`GetWavinfo` is a classic RIFF walker over the whole file (`FS_LoadFile`
allocates `wavlength + 1`). `FindNextChunk` advances by the chunk's own length
field, and the readers `GetLittleShort`/`GetLittleLong` read 2 or 4 bytes
without any end check of their own. The result `wavinfo_t` is consumed by
`S_LoadSound` (`sound.c`), which passes `data + info.dataofs` and `info.samples`
to `S_GetVolume`, `S_GetStatistics`, `S_IsShortSilence` and `SDL_Cache`
(`sdl.c`); none of those re-checks the length.

| Function | Field | Use | Check (quoted) or absence | Verdict |
|---|---|---|---|---|
| `FindNextChunk` | `last_chunk + 4` | base of the next header; `strncmp(data_p, name, 4)` | `if (data_p >= iff_end)` before the length read, but `strncmp` and `GetLittleLong` then read up to 4 bytes, so with `data_p == iff_end - 1` the compare and the length read run past the buffer | **defect D23** (over-read; low, bounded to a few bytes past a `wavlength + 1` allocation) |
| `FindNextChunk` | `iff_chunk_len` | `last_chunk = data_p + 8 + ((iff_chunk_len + 1) & ~1)` | `if (iff_chunk_len < 0)` stops; no upper bound, but the next loop's `data_p >= iff_end` catches an overshoot before another read | ok (given the next-iteration check) |
| `GetWavinfo` | "RIFF"/"WAVE" | `strncmp(data_p + 8, "WAVE", 4)` | `data_p` is non-NULL, but `data_p + 8 .. + 11` is not bounded against `iff_end` | D23 |
| `GetWavinfo` | `fmt ` body | `data_p += 8`, then `GetLittleShort`/`GetLittleLong` for `format`, `channels`, `rate`, `width` | no check that the `fmt ` chunk holds these 16 bytes inside the file | D23 |
| `GetWavinfo` | `format` | format gate | `if (format != 1)` | ok |
| `GetWavinfo` | `channels` | `sc->stereo = channels - 1`, the `len * width * channels` allocation in `SDL_Cache`, divisor in `S_GetStatistics` | none in `GetWavinfo`; `S_LoadSound` (`sound.c`) refuses `info.channels < 1 \|\| info.channels > 2` before any consumer runs | ok |
| `GetWavinfo` | `rate` (`GetLittleLong`) | `SDL_Cache`: `stepscale = (float)info->rate / sound.speed`, `len = (int)(info->samples / stepscale)`, `len = len * info->width * info->channels`, `Z_Malloc(len + sizeof(sfxcache_t))`, then the resample loop `for (i = 0; i < (int)(info->samples / stepscale); i++)` writes `sc->data[i]`; `sc->begin = begin_length * 1000 / info->rate` and three more integer divisions; `S_IsShortSilence`: `(info->samples * 1000) / info->rate` | none anywhere (only the cinematic path bounds its own `s_rate`) | **defect D28** (a low rate inflates the frame count; the `int` product wraps to a small positive allocation while the loop bound is recomputed from the un-multiplied count, a heap overflow write that the D25 clamp does not close) and **defect D29** (a zero rate reaches integer divisions by zero) |
| `GetWavinfo` | `width = GetLittleShort() / 8` | divisor of `samples`, element size in every reader | `if (info.width <= 0)`; nothing refuses a width other than 1 or 2. `S_GetVolume` and `S_GetStatistics` skip such a width, and `SDL_Cache` takes its byte branch (`data[srcsample]`, `srcsample` below `info.samples`, which was divided by the width) and writes bytes into an allocation of `len * width * channels`, so a width of 3 or 4 decodes wrong audio but stays in bounds | hardening (carded under FIX20: refuse a width other than 1 or 2) |
| `GetWavinfo` | `cue ` body, `LIST`/`mark` | `data_p += 32`, `GetLittleLong` for `loopstart`; `data_p += 24`, `GetLittleLong` for loop samples | the `mark` read is guarded by `((data_p - wav) + 32 <= wavlength)`, but the `cue ` `data_p += 32; GetLittleLong()` for `loopstart` is not bounded | D23 |
| `GetWavinfo` | `loopstart` | `info.samples = loopstart + i`; `sc->loopstart` in `SDL_Cache`, used as a replay offset in `sdl.c` `S_PaintChannels` | no range check; a loopstart past the sample count makes `SDL_PaintChannelFrom*` read `sc->data` out of range during mixing | **defect D24** (the loop offset is divided by `stepscale` and used as `ch->pos`/`ch->end`; a crafted cue loop points the mixer past the cached sample) |
| `GetWavinfo` | data chunk length | `samples = GetLittleLong() / info.width` | `data_p += 4` then the read; `samples` is never compared with `wavlength - dataofs`, so the field alone sets how many samples every consumer believes exist; a negative length word gives a negative `samples` | **defect D25**; negative values **D30** |
| `GetWavinfo` | early returns | `return info` after "Missing data chunk" (and the earlier failures) | `info` is returned as filled so far: when a `cue `/`LIST`/`mark` sequence was found, `info.samples = info.loopstart + i` is already set and `dataofs` is still 0, and `channels` is valid, so `S_LoadSound` accepts it and every consumer reads `info.samples` elements from the start of the file buffer; nothing validates on any return path | **defect D30** |
| `GetWavinfo` | `samples` vs `info.samples` (cue) | loop-length sanity | `if (samples < info.samples) Com_Error(...)` only when a cue set `info.samples`; otherwise `info.samples = samples` unchecked | D25 |
| `GetWavinfo` | `dataofs = data_p - wav` | base pointer `data + info.dataofs` for all consumers | no check that `dataofs <= wavlength` | **defect D25** (feeds the over-read below) |
| `S_GetVolume` (`sound.c`) | `info.samples` | `sound_end = sound_data + sound_length`, reads every element | none; trusts `info.samples` | **defect D25** (heap over-read) |
| `S_GetStatistics` (`sound.c`) | `info.samples` | four scans over `[sound_data, sound_data + sound_length]` | none | **defect D25** |
| `S_IsShortSilence` (`sound.c`) | `info.samples` | `sample_count = info.samples * info.width`, reads each | tests `info.samples > rate * S_MAX_LEN...` to skip long sounds, but the short-silence branch still reads `sample_count` bytes for a sound whose `rate` is large; the first test divides by `info->rate` unchecked (reached only for a mono sound under one of the muzzle-flash names listed in `S_IsSilencedMuzzleFlash`) | **defect D25**; the division **D29** |
| `SDL_Cache` (`sdl.c`) | `info.samples` | resample loop reads `data[srcsample]` / `((short *)data)[srcsample]` for `srcsample` up to about `info.samples` | zero-length rejected; nothing compares `info.samples` with the data actually present | **defect D25** (heap over-read on the source) |
| `SDL_Cache` (`sdl.c`) | `len * info->width * info->channels` | `Z_Malloc(len + sizeof(sfxcache_t))` for a loop that writes `(int)(info->samples / stepscale)` elements of `sc->width` bytes | `int` multiplication with no overflow test, and the loop bound is recomputed from the un-multiplied frame count, so the destination is not matched to the loop once the product wraps; a negative product becomes a huge `size_t` and `Z_TagMalloc` fails with `ERR_FATAL` | **defect D28** (heap overflow write; with `info.samples` consistent with the file, so D25's clamp leaves it open) |
| `SDL_Cache` (`sdl.c`) | `info->rate == 0` | `stepscale = 0`, `(int)(info->samples / 0.0f)` (undefined conversion of infinity), then `begin_length * 1000 / info->rate` and the `end`, `fade`, `attack` lines | none | **defect D29** (integer division by zero: SIGFPE on x86-64, a silent 0 on arm64, where `SDL_Cache` then continues into the resample loop with an undefined frame count against an allocation sized from the wrapped product, the D28 class: a write past the allocation, not only a denial of service) |

D25 is the substantive WAV finding: the data-chunk length word, and the
`dataofs`, are trusted without any comparison to `wavlength`. A `.wav` named
in a server sound configstring, downloadable with `allow_download_sounds`,
sets `info.samples` to any 31-bit value, and four consumers then read that many
elements from `data + info.dataofs`. Static trace, high confidence. D23 and
D24 are the same class (unbounded chunk reads, unbounded loop point); D23 is a
few bytes, D24 reaches the mixer. The clamp for D25 alone is not enough:
reject `dataofs > wavlength`, and `samples > (wavlength - dataofs) / (width *
channels)`; and bound `loopstart` to `samples`.

D28 is independent of D25. `info.rate` is read and never checked. In
`SDL_Cache`, `stepscale = rate / sound.speed`, so a rate far below the output
rate makes the resampled frame count `(int)(samples / stepscale)` many times
`samples`; `len * width * channels` is then an `int` product that wraps, and
the allocation can come out small and positive while the loop still writes
the full frame count into `sc->data`. The source index stays near 0 (the
step rounds to 0), so the reads stay inside a file whose `samples` is honest
and only the write overflows. Checked numerically with the same expressions
for a stereo 16-bit file of a few tens of KB at the two usual output rates:
at 48 kHz the product wraps to about 313 MB allocated for about 2.3 GB of
writes; at 44.1 kHz it wraps negative and `Z_TagMalloc` fails with
`ERR_FATAL`. That holds for that sample count only: the sign of the wrapped
product depends on the count, so the default rate (`s_khz` 44, 44100 Hz) is
affected at other counts, where the product wraps to a small positive size
and the write overflows; `Z_TagMalloc` only refuses a size of zero or one
near `SIZE_MAX`. A `.wav` is requested from a server as `sound/%s` for every
`CS_SOUNDS` configstring, so D28 is server-reachable: high, static trace with
high confidence (the arithmetic was checked, `SDL_Cache` was not run). D29 is
the zero rate: `begin_length * 1000 / info->rate` and its three siblings in
`SDL_Cache` (every SDL-loaded sound) and the first test of
`S_IsShortSilence` divide by zero; that is a SIGFPE crash on x86-64 and a
silent 0 on arm64. On arm64 the divisions do not stop the build's execution:
the infinite step count converts to an undefined frame count (`len` is
non-zero, so the zero-length test passes), `len * width * channels` wraps, and
`SDL_Cache` enters the resample loop whose bound is the undefined count
against an allocation sized from the wrapped product. That is the D28 class
(a write past the allocation), so D29 is high on arm64 and a denial of
service on x86-64; high confidence (static; `SDL_Cache` was not run). D30 is
`GetWavinfo` handing back a partly filled `info` from an early return (a cue
loop length but no data chunk leaves `samples` set and `dataofs` 0) or a
negative `samples` from a negative length word; medium, high confidence.

FIX20 therefore validates the complete `wavinfo_t` on every return path of
`GetWavinfo` (or in one place in `S_LoadSound` before any consumer): rate in
a sane range (for example 1 kHz to 192 kHz), width 1 or 2, channels 1 or 2,
`samples > 0`, `dataofs` and `samples * width * channels` inside the file,
`loopstart` below `samples`; and `SDL_Cache` computes its allocation in
64-bit and compares it with a limit before allocating, deriving the loop
bound from the same value.

No host harness was run: `wave.c`, `sound.c` and `sdl.c` include the client
and SDL sound headers and do not link standalone without a large stub surface.
The reads above were traced by hand from `info.samples`/`info.dataofs` to each
`sound_end`/`srcsample` use, and the D28 wrap was checked with a standalone
program evaluating the same `float` and `int` expressions.

## Cinematics (`src/client/cl_cin.c`)

A `.cin` is a width/height/sound header, a 64 KB Huffman count table, then
per-frame records (optional palette, a compressed size, the compressed bytes,
a sound chunk). `SCR_PlayCinematic` is reached from `CL_ParseServerData` (`cl_parse.c`) when
the server's `svc_serverdata` message carries `playernum == -1`; the level
name string of that message becomes `arg`, so the file name and the file are
both server-supplied. No configstring names a `.cin`, but the `download`
console command (which a server can issue through `svc_stufftext`) fetches
one. TASK2 added most of the bounds below (the baseline), so most
rows are `ok`; the exception is the file-name handling.

| Function | Field | Use | Check (quoted) or absence | Verdict |
|---|---|---|---|---|
| `SCR_PlayCinematic` | `arg` (server string), still-image branch | `memcpy(namewe, name, strlen(name) - strlen(dot))` into `char namewe[256]` | the length is `5 + offset-of-first-dot-in-arg`; `name` is `MAX_OSPATH`, which is 256 only under `_WIN32` and **4096** on every other build, macOS included (`shared.h`), so there `Com_sprintf` keeps the whole `pics/` + `arg` and any `arg` of more than about 250 characters ending in the still-image extension overruns the 256-byte stack buffer. The real bound on `arg`: `MSG_ReadString` returns from a 2048-byte static, and the whole `svc_serverdata` message fits in `net_message_buffer[MAX_MSGLEN]` (1400 bytes, shared with the netchan header and the other serverdata fields), so the copy is at most about 1380 bytes, an overrun of up to about 1100 bytes | **defect D26** (stack buffer overflow, server-triggered, on every non-Windows build; on Windows `Com_sprintf` truncates `name` to 255 and the copy stays inside; reached only for a name ending exactly `.pcx`/`.tga`/`.jpg`/`.png` because the branch uses `strcmp(dot, ...)`) |
| `SCR_PlayCinematic` | `arg`, video branch | `Com_sprintf(name, sizeof(name), "video/%s", arg)`, `FS_FOpenFile` | `Com_sprintf` truncates to `MAX_OSPATH`; `FS_FOpenFile` applies the path rules audited in the filesystem section | ok |
| `SCR_PlayCinematic` | `width`, `height` | `cin.width`, `cin.height`, `maxcount` in decompress, image size | `cin.width <= 0 \|\| cin.height <= 0 \|\| (long long)width * height > 16 * 1024 * 1024` | ok |
| `SCR_PlayCinematic` | `s_rate`, `s_width`, `s_channels` | sound chunk size per frame, `S_RawSamples` | `s_rate <= 0 \|\| s_rate > 22050`, `s_width != 1 && != 2`, `s_channels != 1 && != 2` | ok |
| `Huff1TableInit` | 256 rows of 256 count bytes | `cin.h_count[j] = counts[j]`, node build into `hnodes1` | `FS_Read(counts, 256, ...)` fixed; `hnodes1 = Z_Malloc(256*256*2*4)`, `nodebase = hnodes1 + prev*256*2` with `prev < 256` and the inner node index at most `(510-256)*2`, so the top write is below the 131072-int allocation | ok |
| `Huff1TableInit` | `h_count` sums | `numhnodes` grows to at most 511 | the `while (numhnodes != 511)` loop and `SmallestNode1` returning -1 bound it; `h_count[512]`, `h_used[512]` | ok |
| `SCR_ReadNextFrame` | `command` | `1` palette, `2` end, else decompress | `LittleLong`; any other value falls through to a frame read, which is then bounded by the size check | ok |
| `SCR_ReadNextFrame` | palette | `FS_Read(cl.cinematicpalette, sizeof(...), ...)` | fixed 768 bytes | ok |
| `SCR_ReadNextFrame` | `size` | `FS_Read(compressed, size, ...)`, `compressed[size] = 0` into `byte compressed[0x20000 + 1]` | `((size_t)size > sizeof(compressed) - 1) \|\| (size < 1)` raises `Com_Error` | ok |
| `SCR_ReadNextFrame` | sound chunk | `FS_Read(samples, bytes, ...)` into `byte samples[22050 / 14 * 4]` | `bytes = (long long)count * s_width * s_channels`; `count < 0 \|\| bytes < 0 \|\| bytes > sizeof(samples)` returns; the header limits make `bytes` at most `(22050/14 + 1) * 2 * 2`, inside the 6300-byte buffer | ok |
| `Huff1Decompress` | `in.count` | header read | `if (in.count < 4)` | ok |
| `Huff1Decompress` | `ucount` (decompressed size, first 4 bytes) | `count`, output length | `ucount == 0 \|\| (long long)ucount > maxcount` where `maxcount = width * height`; output is `Z_Malloc(maxcount)` | ok |
| `Huff1Decompress` | input advance | `*input++` | `if (input - in.data > in.count)` stops with one tolerated overread byte (the `compressed[size] = 0` spare) | ok |
| `Huff1Decompress` | `nodenum` | `hnodes[nodenum * 2 + bit]`, `*out_p++ = nodenum` | `if (nodenum < 0 \|\| nodenum > 510)` per step; `out_p` is bounded because `count` (<= `maxcount`) drives the write and decrements to zero | ok |
| `SCR_LoadHiColor` | `cin.width`, `cin.height`, `*bitsPerPixel` | `Z_Malloc(h * w * bpp / 8)`, `memcpy(pic, data, h * w * bpp / 8)` | `cin.width`/`cin.height` were set by `SCR_LoadImageWithPalette` from the same image `data`, so the copy length matches the source; `bitsPerPixel` is 8 or 32 from the loader | ok |

The cinematic loader is the best-hardened asset parser in the tree: the frame,
sound and decompression paths all carry explicit bounds. The one finding is
D26, the still-image name copy, which predates and sits outside those bounds.
No host harness was run (the file pulls in the client, sound and filesystem
imports); D26 is a static trace with high confidence (`MAX_OSPATH` is 4096 in
`shared.h` for every build except `_WIN32`, macOS included, and `namewe` is a
fixed 256).
The minimal fix is to size `namewe` as `MAX_OSPATH` or to copy with
`Q_strlcpy` into a bounded buffer.

## Entity strings and map-fix overrides

The entity lump is text: `{ "key" "value" ... }` blocks. The server reads it
in `collision.c` (`CMod_LoadEntityString`), optionally replacing it with a
`.ent` override file, then hands it to the game module's `SpawnEntities`
(`g_spawn.c`), which tokenizes with `COM_Parse` (`shared.c`) and writes each
value into a fixed struct field chosen by name. The entity text comes from the
map, which a client downloads from the server, so it is untrusted on the
server; the `.ent` override is a loose file in the game directory. Every write
below lands at a compile-time struct offset selected by the key, never at an
offset taken from the file, which is why the section is almost entirely `ok`.

| Function | Field | Use | Check (quoted) or absence | Verdict |
|---|---|---|---|---|
| `CMod_LoadEntityString` | map `name` | `strcmp(name + nameLen - 4, ".bsp")`, `memcpy(namewe, name, nameLen - 4)` | `nameLen > (MAX_QPATH - 1)` rejects long names; a name shorter than 4 underflows `name + nameLen - 4`, but the server builds it as `maps/%s.bsp` (covered in the collision section) | ok-by-construction |
| `CMod_LoadEntityString` | `.ent` CRC | `snprintf(entname, sizeof(entname) - 1, "%s@%04x.ent", namewe, crc)` | `CRC_Block` over `l->filelen - 1`, `namewe` is `MAX_QPATH`, `entname` is 256; `snprintf` bounds the write | ok |
| `CMod_LoadEntityString` | `.ent` file length | `memcpy(map_entitystring, buffer, bufLen)`, `map_entitystring[bufLen] = 0` | `bufLen > 1 && bufLen < sizeof(map_entitystring) - 1` | ok |
| `CMod_LoadEntityString` | BSP entity lump length | `memcpy(map_entitystring, ..., numentitychars)` | `l->filelen < 0`, `l->filelen >= sizeof(map_entitystring)` (collision section) | ok |
| `COM_Parse` (`shared.c`) | each token | writes `com_token[MAX_TOKEN_CHARS]` (1024) | `if (len < MAX_TOKEN_CHARS)` guards every write, the quoted-string branch keeps reading but stops writing, and `if (len == MAX_TOKEN_CHARS) len = 0` before `com_token[len] = 0` | ok (a token longer than 1023 is silently truncated, not a memory error) |
| `ED_ParseEdict` | key token | `Q_strlcpy(keyname, com_token, sizeof(keyname))` into `char keyname[256]` | bounded copy | ok |
| `ED_ParseEdict` | `}` / EOF | loop control | `com_token[0] == '}'`, `!data` with `gi.error` | ok |
| `ED_ParseEdict` | leading `_` keys | `continue` (discarded) | `if (keyname[0] == '_')` | ok |
| `ED_ParseField` | key | `FindSpawntempField(key)` then `FindSpawnfield(key)`; `b = &st + f->ofs` or `(byte *)ent + f->ofs` | the offset comes from the compile-time `fields[]` table, not the file; an unknown key is reported and dropped | ok-by-construction |
| `ED_ParseField` | `F_LSTRING` value | `ED_NewString(value)` | see `ED_NewString` | ok |
| `ED_ParseField` | `F_VECTOR`, `F_ANGLEHACK` | `sscanf(value, "%f %f %f", &vec[0..2])`, `strtod` | writes three `vec_t` at the field offset; a short value zero-fills the vector | ok |
| `ED_ParseField` | `F_INT`, `F_FLOAT` | `strtol` / `strtod` into the field | fixed-size write | ok |
| `ED_NewString` | value | `gi.TagMalloc(strlen(value) + 1)`, escape expansion into it | each `\\x` consumes two input bytes for one output byte, so the output never exceeds the `strlen + 1` allocation; the `i < l - 1` guard keeps the look-ahead inside the string | ok |
| `SpawnEntities` | `entities` text | `COM_Parse` loop; `ent = G_Spawn()` per block | `{` expected or `gi.error`; `G_Spawn` raises `gi.error` when `num_edicts >= maxentities`, so a map with too many entities drops rather than overrunning `g_edicts` | ok |
| `SpawnEntities` | `mapname`, `spawnpoint` | `Q_strlcpy(level.mapname, ...)`, `Q_strlcpy(game.spawnpoint, ...)` | bounded copies | ok |
| `ED_CallSpawn` | `ent->classname` | linear search of `itemlist` and `spawns[]` by `strcmp` | string compare only; an unknown classname is reported, the entity gets no spawn function | ok |
| map hacks (`SpawnEntities`, individual spawn functions) | `ent->model`, `level.mapname` | `Q_stricmp(ent->model, "*27")` etc.; `ent->model` as `*N` reaches `CM_InlineModel` | string compares; the inline-model number is range-checked in `CM_InlineModel` (collision section) | ok |

No defect found in the entity-string path. The parser's safety rests on two
invariants that hold in this tree: `COM_Parse` bounds its token buffer, and
`ED_ParseField` writes only to offsets from the static `fields[]` table keyed
by name, so a hostile value can set a field to a garbage number but cannot
choose where it lands. The downstream effect of a garbage field value (for
example an out-of-range `style` on `func_areaportal`, which becomes a
`CM_SetAreaPortalState` index) is accounted for at the sink, in the collision
section (D3). No host harness was run: `g_spawn.c` is the game module and links
against the full `game_import_t`; the writes were traced to the `fields[]`
table, which is compile-time data.

## By design / out of scope

- **`svc_stufftext` runs arbitrary console commands on the client.** This is
  the engine's design (`CLAUDE.md`, "Security-relevant boundaries") and is not
  a loader defect; it is why the save-name and download filters matter, but it
  is not filtered here.
- **`FS_FOpenFile` does not strip `..` from an asset name.** A server-named
  model/sound/image path can read a file outside the search paths. The bytes
  are parsed as an asset and never sent back, so this is a read-only
  information path, noted as hardening in the filesystem section, not a new
  defect. Downloads (the write path) are filtered by `CL_DownloadFilter`.
- **Vendored `stb_image`, `stb_image_resize`, `stb_vorbis`.** The TGA/PNG/JPG
  and OGG decoders are third-party code slated for removal; only the repo code
  around their calls is audited. A fuzzing pass on the stb internals belongs
  with the removal cards, not here.
- **Misaligned loads.** Several loaders cast `cmod_base + fileofs` or
  `mod_base + fileofs` to typed structs without an alignment check. This is
  undefined behaviour that UBSAN flags but is harmless on x86-64 and arm64;
  listed as hardening, not carded.
- **Unterminated fixed-size name fields** (`texinfo.texture[32]`, MD2/SP2 skin
  names, surface names) read to the next NUL. `FS_LoadFile` appends one zero
  byte past the buffer, which bounds the worst case; listed as hardening.
- **A non-pack file named `*.pak`.** `FS_LoadPAK` raises `ERR_FATAL` ("is not a pack file") when the ident does not match, so a stray file with that extension in a search path stops the engine at every start. That is fatal by design today; FIX22 lists it as a behaviour to decide (skip it with a warning instead of `ERR_FATAL`).
- **Savegames** (`savegame.c`) read local files the user chose and are a
  different trust model; not an asset loader.
- **Hardening rows with a card.** These hardening rows are folded into the
  matching card rather than left open: the renderer `hunkSize` `int` sum
  (FIX17), the MD2 `ofs_end` lower bound and the SP2 early-return hunk leak
  (FIX18), the soft `sw_image.c` resize product and the unchecked
  `Get*Info` dimensions (FIX19), and the WAV sample width other than 1 or 2
  (FIX20). The short pak header read goes with FIX22, which already checks
  every other `FS_LoadPAK` read. The `LoadM32` row is a defect (D27), not
  hardening. The remaining hardening rows (misaligned loads, unterminated
  names, `..` in a read path) are not carded for the reasons given in the
  bullets above; the renderer's reliance on `CMod_CheckLumps` running first
  is not carded because `CM_LoadMap` always runs first on the client and
  FIX17 adds the renderer's own index checks where they matter.

## Defects

Severity is `sev` (low/medium/high) for the memory-safety impact against the
reachability in [Where each asset can come from](#where-each-asset-can-come-from);
`conf` is confidence that the finding is real.

| ID | Where | One line | sev | conf | Card |
|---|---|---|---|---|---|
| D1 | `collision.c` `CM_DecompressVis` | input over-read only: the input pointer is never bounded by `numvisibility`, and a `00 00` pair (zero-length run) advances the input without producing output, so the decoder walks past `map_visibility`; the output is clamped to `row` (at most 4096 of 8192 bytes) | medium | high (ASAN, READ) | FIX23 |
| D2 | `collision.c` `CM_InitBoxHull` | box-hull leaf written past `map_leafs` when the map uses every leaf slot | high | high (ASAN) — **fixed in this branch** | fixed |
| D3 | `collision.c` `CM_SetAreaPortalState` | `portalnum` has no lower bound and an off-by-one upper bound | medium | high (ASAN) — negative side **fixed in this branch**; off-by-one remains | FIX23 (off-by-one) |
| D4 | `filesystem.c` `FS_LoadPAK` | directory read past EOF leaves `info` uninitialised; `Q_strlcpy` then over-reads an unterminated pack name | medium | high | FIX22 |
| D5 | `filesystem.c` `FS_LoadPAK` | a bare header asks for a ~2 GB `malloc` plus ~2.4 GB `calloc`; failure is `ERR_FATAL` | medium | high | FIX22 |
| D6 | `filesystem.c` `FS_FOpenFile`/`FS_LoadFile2` | pack entry `offset`/`size` never bounded by the file; negative size leaks the handle, oversize size `ERR_FATAL`s | medium | high | FIX22 |
| D7 | renderer `Mod_ForName` | 1-2 byte model file read up to 2 bytes past the `length + 1` allocation at the format switch | low | high | FIX17 |
| D8 | renderer `Mod_ClusterPVS`/`Mod_DecompressVis` | the output is bounded only by `row`, never by the buffer: `numclusters` can give a row of up to 16384 bytes and a zero run is written whole, so the `static decompressed[8192]` overflows on write (the vis offsets are FIX3 item 3) | high | high (ASAN) | FIX17 |
| D9 | renderer `Mod_NumberLeafs` | a node DAG (shared children, no cycle) walks a leaf 2^n times, overrunning two `int[MAX_MAP_LEAFS]` stack arrays | high | high (ASAN) | FIX17 |
| D10 | gl1/gl3 `R_SubdivideSurface` | a `SURF_WARP` face with >64 edges overruns `vec3_t verts[64]` on the stack before the vertex-count check | high | high (ASAN) | FIX17 |
| D11 | renderer faces/edges | `numedges`/`firstedge`/surfedge/`edge.v[]` indices unbounded against `numsurfedges`/`numedges`/`numvertexes`; negative `numedges` gives a negative `Hunk_Alloc` | high | high (static) | FIX17 |
| D12 | renderer `Mod_LoadFaces` | `lightofs` only tested for `-1`; any other value offsets `lightdata` and is read and uploaded as lightmap texels | medium | high (static) | FIX17 |
| D13 | renderer `Mod_LoadTexinfo` | `nexttexinfo` unbounded; a chain that loops hangs the frame-count walk, a large value reads a wild pointer | medium | high (ASAN) | FIX17 |
| D14 | renderer nodes/submodels | node `firstface`/`numfaces`, submodel `headnode`/`firstface` unbounded against `numsurfaces`/`numnodes`; stray surface pointer is written into a texture chain | high | high (static) | FIX17 |
| D15 | soft `Mod_LoadMarksurfaces` | a negative leaf-face index wrote `visframe` before the surfaces array | medium | high — **fixed in this branch** | fixed |
| D16 | MD2 `lightnormalindex` | a vertex normal index 162..255 reads up to ~1 KB past the 162-row `r_avertexnormals` table | low | high (static) | FIX18 |
| D17 | MD2 soft skin path | `skinwidth`/`skinheight` unchecked (div, scale); `st` coords never bounded by the skin size, over-reading texels in the rasterizer | medium | medium (static) | FIX18 |
| D18 | SP2 `Mod_LoadSP2` | no length check (reads/writes past a short file); `numframes` zero divides, negative indexes `skins[]`/`frames[]` out of range | high | high (ASAN for the over-read) | FIX18 |
| D19 | PCX `PCX_Decode` | `full_size = (h+1)*(w+1)[*4]` is `int` with no overflow test; header dimensions alone can wrap it to a small `malloc`, and every row write then lands past it regardless of the RLE input | high | high (ASAN, 8-bit and 24-bit) | FIX19 |
| D20 | PCX 1/2/4-bit path | `line[x / div]` can exceed a `bytes_per_line`-byte row when `bytes_per_line` is tiny | low | medium (static) | FIX19 |
| D21 | `GetPCXInfo` | reads `xmax`/`ymax` without a header-size check; over-reads a short file | low | high (static) | FIX19 |
| D22 | `R_LoadImage`/soft scale | `width * height` (and `* 4`) is `int` and can overflow for large stb dimensions; the gl renderers guard the scale product, soft and the copy length do not | medium | medium (static) | FIX19 |
| D23 | `wave.c` chunk walk | `GetLittleLong`/`strncmp` read a few bytes past `iff_end` at a chunk boundary | low | high (static) | FIX20 |
| D24 | `wave.c` cue `loopstart` | loop point unbounded; `SDL_Cache` turns it into a mixer `pos`/`end` past the cached sample | medium | medium (static) | FIX20 |
| D25 | `wave.c` data chunk | the data-length word and `dataofs` are trusted without comparison to `wavlength`; four consumers read `info.samples` elements from `data + dataofs` | high | high (static) | FIX20 |
| D26 | `cl_cin.c` `SCR_PlayCinematic` | still-image name copied into `char namewe[256]` by length; `MAX_OSPATH` is 4096 on every non-Windows build (macOS included), so a server level name of up to about 1380 characters (the `svc_serverdata` message limit) overruns the stack buffer by up to about 1100 bytes | high | high (static) | FIX21 |
| D27 | M32 `LoadM32` | `width * 4` wraps in the body-size check, so a hostile width passes against a few-byte body; gl1 then rewrites `width * height` pixels in place past the file buffer (gl3 over-reads) | high | high (static) | FIX19 |
| D28 | `sdl.c` `SDL_Cache` (rate from `wave.c`) | `info.rate` unchecked; a low rate inflates the resampled frame count, `len * width * channels` wraps in `int` to a small allocation, and the loop writes the full count: heap overflow write not closed by the D25 clamp | high | high (static, arithmetic checked) | FIX20 |
| D29 | `sdl.c` `SDL_Cache`, `sound.c` `S_IsShortSilence` | a zero `info.rate` reaches integer divisions (`begin_length * 1000 / info->rate` and siblings, `(info->samples * 1000) / info->rate`); on x86-64 SIGFPE, on arm64 execution continues into the resample loop with an undefined frame count (the D28 class, a write past the allocation) | high | high (static) | FIX20 |
| D30 | `wave.c` `GetWavinfo` return paths | `info` is returned partly filled from early returns (a cue loop length with no data chunk leaves `samples` set and `dataofs` 0) and a negative data-length word gives negative `samples`; nothing validates on any path | medium | high (static) | FIX20 |
| D31 | MD2 `Mod_LoadMD2` section offsets | section offsets have no lower bound; a section copied over the validated header restores the unclamped `num_skins`, and the skin loop writes image pointers past `skins[MAX_MD2SKINS]` | high | high (ASAN) | FIX18 |

Fixed in this branch: D2, D3 (negative side), D15 — each a one-line bound
check in its loader, committed separately and noted in its row.

## Cards filed

One card per defect class, filed as GitHub Issues under the `trust-boundary`
epic and locked; each issue holds the goal, rationale, scope and acceptance
criteria. Acceptance in every card names the fix plus an engine-free
regression check: a crafted in-memory file passed to the real loader through a
host-side harness under ASAN, the same shape used to prove several findings
here (no harness is committed).

| Card | Issue | Defects covered | Severity |
|---|---|---|---|
| FIX17 | #115 | D7, D8, D9, D10, D11, D12, D13, D14 (renderer BSP loaders), plus hardening: the `int` `hunkSize` sum | high |
| FIX18 | #116 | D16, D17, D18, D31 (MD2 and SP2), plus hardening: MD2 `ofs_end` lower bound, the SP2 early-return hunk leak | high |
| FIX19 | #117 | D19, D20, D21, D22, D27 (image loaders), plus hardening: non-positive dimensions from `GetWalInfo`/`GetM8Info`/`GetM32Info` | high |
| FIX20 | #118 | D23, D24, D25, D28, D29, D30 (WAV and the SDL resampler), plus hardening: refuse a sample width other than 1 or 2 | high |
| FIX21 | #119 | D26 (cinematic still-image name) | high |
| FIX22 | #120 | D4, D5, D6 (pak directory), plus hardening: the unchecked header `fread` | medium |
| FIX23 | #121 | D1 and the remaining D3 off-by-one (collision BSP), distinct from FIX3 (#44) / FIX10 (#73) | medium |
