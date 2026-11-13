# Design: renderer-API record and replay (the TASK27 remainder)

Plan anchor: `docs/plans/metal-renderer.md` as revised by INFORM8 (#35), card
TASK27 (#37). Companion to `docs/plans/metal-port-harness.md`, which ships
the frame side (`VID_WritePinFrame`, `SCR_PinsCapture`, `CL_PinsFrame`,
`scripts/pins.sh`).
This document is the design for the other half: record what the client
hands the renderer, replay it into any renderer with the game not running,
and judge the frames through the same pin hook.

## 1. The seam

`src/client/vid/vid.c` already owns the only copy of `refexport_t re`
(`re = GetRefAPI(ri)` in `VID_LoadRefresh`) and exposes it to the rest of
the client through named wrappers. No client file touches `re.` directly
(verified: the only `re.` hits outside vid.c are `re.wav` strings). The
recorder therefore lives in those wrappers; no second function table, no
change to `API_VERSION 8`, no change to any renderer.

| `refexport_t` member | vid.c wrapper | Class | Recorded as |
|---|---|---|---|
| `Init`, `Shutdown` | `VID_LoadRefresh`, `VID_ShutdownRenderer` | control | session open/close |
| `PrepareForWindow`, `InitContext`, `GetDrawableSize`, `ShutdownContext`, `IsVSyncActive` | GLimp path | control | not recorded (window plumbing) |
| `BeginRegistration(map)` | `R_BeginRegistration` | registration | `REC_BEGINREG` + map name |
| `RegisterModel(name)` | `R_RegisterModel` | registration | `REC_MODEL` name, handle id |
| `RegisterSkin(name)` | `R_RegisterSkin` | registration | `REC_SKIN` name, handle id |
| `SetSky(name, rotate, axis)` | `R_SetSky` | registration | `REC_SKY` |
| `EndRegistration` | `R_EndRegistration` | registration | `REC_ENDREG` |
| `RenderFrame(refdef_t*)` | `R_RenderFrame` | per-frame state | `REC_RENDERFRAME` + deep copy (section 2) |
| `DrawFindPic(name)` | `Draw_FindPic` | registration | `REC_PIC` name, handle id |
| `DrawGetPicSize(w,h,name)` | `Draw_GetPicSize` | 2D query | `REC_PICSIZE` (name; result stored for replay check) |
| `DrawPicScaled`, `DrawPicScaledCol`, `DrawStretchPic`, `DrawCharScaled`, `DrawTileClear`, `DrawFill`, `DrawFadeScreen` | `Draw_*` | 2D draw | one record each, arguments verbatim |
| `DrawStretchRaw(x,y,w,h,cols,rows,data,bits)` | `Draw_StretchRaw` | 2D draw (cinematic) | record + `cols*rows*(bits/8)` payload |
| `SetPalette(palette)` | `R_SetPalette` | control | `REC_PALETTE` + 768 bytes (256 RGB triples) or NULL flag |
| `BeginFrame(camera_separation)` | `R_BeginFrame` | control | `REC_BEGINFRAME` |
| `EndWorldRenderpass` | `R_EndWorldRenderpass` | control | `REC_ENDWORLD` |
| `EndFrame` | `R_EndFrame` | control | `REC_ENDFRAME` (closes the frame record) |

Rule: a frame record is everything between `REC_BEGINFRAME` and
`REC_ENDFRAME`, in call order. Registration records may appear inside a
frame (late `RegisterModel` from `cl_parse.c`, `Draw_FindPic` from
`cl_screen.c`); they are replayed in place, so the handle a later call
uses already exists.

Callers that produce these records today: `cl_screen.c` (`SCR_UpdateScreen`
owns Begin/Render/EndWorld/End, HUD `Draw_*`), `cl_view.c` (registration,
`R_RenderFrame` for the 3D view), `menu/menu.c` and `menu/qmenu.c`,
`cl_console.c`, `cl_inventory.c`, `cl_cin.c` (`Draw_StretchRaw`),
`cl_parse.c`, `cl_entities.c`, `cl_tempentities.c`, `cl_network.c`.

## 2. Deep copy of `refdef_t`

`refdef_t` (ref.h) is owned by `cl.refdef` in `cl_view.c` and points at
client-side arrays that are rebuilt every frame. Nothing in it may be
stored by pointer.

| Field | Type | Owned by | Copy rule | Bytes (typical / max) |
|---|---|---|---|---|
| `x,y,width,height`, `fov_x,fov_y`, `vieworg[3]`, `viewangles[3]`, `blend[4]`, `time`, `rdflags` | scalars | refdef | verbatim | 76 |
| `areabits` | `byte*` or NULL | `cl_view.c` | copy `MAX_MAP_AREAS/8` = 32 bytes, plus NULL flag | 33 |
| `lightstyles` | `lightstyle_t[MAX_LIGHTSTYLES]` (16 B each) | `cl_view.c` | always 256 entries, verbatim | 4096 |
| `num_entities`, `entities` | `entity_t[≤MAX_ENTITIES=128]` (72 B each on disk) | `cl_view.c` | per entity: `model` → model id, `skin` → skin id, rest verbatim | ~40 × 72 = 2880 / 9216 |
| `num_dlights`, `dlights` | `dlight_t[≤32]` (28 B) | `cl_view.c` | verbatim | ~4 × 28 = 112 / 896 |
| `num_particles`, `particles` | `particle_t[≤4096]` (20 B) | `cl_view.c` | verbatim | ~200 × 20 = 4000 / 81920 |

Handles. `entity_t.model` is `struct model_s *` and `entity_t.skin` is
`struct image_s *`, opaque outside the renderer and different per renderer
and per run. The recorder keeps a map from the pointer each `R_RegisterModel`
/ `R_RegisterSkin` / `Draw_FindPic` returned to a small integer id and the
name that produced it; on disk an entity stores the id. `NULL` skin is id 0
("inline skin", renderer picks it). A pointer not in the map is a bug and is
recorded as id 0 with a `REC_WARN` record, never silently.

`RF_BEAM` entities reuse `origin`/`oldorigin`/`frame`/`skinnum` with other
meanings; copying verbatim preserves that. `backlerp`, `alpha`, `lightstyle`,
`flags` verbatim.

Per typical frame: ~11.3 KB of state plus the 2D stream (HUD: ~40 draw
records at 32 B = 1.3 KB). Worst case with 4096 particles: ~97 KB.

## 3. On-disk format

One file per record run, `<store>/<scenario>/<renderer>/<sha>-<hash>/trace.q2r`.

- Header (64 B): magic `Q2REFREC`, `u32 version = 1`, `u32 api_version`
  (8), `u32 flags`, `u32 string_table_offset`, `u32 frame_count`,
  `char engine_sha[16]`, `u32 scenario_hash` (same `cksum` as
  `scripts/pins.sh`), `u16 width, height`, `u32 pins_capture` cadence.
- Records: `u16 type`, `u16 pad`, `u32 length`, payload. Little-endian,
  4-byte alignment, fixed-width integers, floats as IEEE-754 bits. Written
  with explicit serializers, never `fwrite(&struct)`, so the format does not
  depend on the compiler's layout.
- String table at `string_table_offset`: `u32 count`, then
  `u32 kind (model|skin|pic|sky|map), u32 id, char name[MAX_QPATH]`.
  Written at `VID_ShutdownRenderer`, so the header is patched last.
- Frame index after the string table: `u32 frame, u64 offset` per frame so
  a replay can seek to any frame and a compare can report by frame number.

Size: ~13 KB per typical frame; a 1000-frame scenario is ~13 MB, under
the store, never committed (INFORM9: it embeds asset-derived data).

## 4. Replay driver

`+set pins_replay <trace.q2r>` on the client. In `CL_Init`, after
`VID_Init` has loaded the renderer named by `vid_renderer` and before any
`SCR_UpdateScreen`:

1. `RefRec_OpenReplay(path)`; refuse if `api_version` or `width/height`
   differ from the running config (the pin's resolution is part of its id).
2. Walk the records. Registration records call the live wrapper
   (`R_RegisterModel(name)` etc.) and store the returned pointer under the
   trace id. `REC_BEGINREG` and `REC_ENDREG` call theirs in place.
3. Per frame: `R_BeginFrame(sep)`, rebuild a `refdef_t` from the record
   with the ids resolved to live pointers into replay-owned arrays
   (`entity_t[128]`, `dlight_t[32]`, `particle_t[4096]`,
   `lightstyle_t[256]`, `byte areabits[32]`), `R_RenderFrame(&fd)`, the 2D
   records in order, `R_EndWorldRenderpass` where it was recorded,
   `R_EndFrame`.
4. Frames whose index is a multiple of the recorded `pins_capture` cadence
   run the `screenshot` command synchronously before `R_EndFrame`, the same
   place the live run captures (`SCR_PinsCapture`; gl1's `R_ScreenShot`
   reads the back buffer at command time), so `VID_WritePinFrame` writes them to
   `pins_dir` with the same frame numbering.
5. At the last frame, `quit`.

The replay path never runs `CL_Frame`, the server, sound or input: it is a
loop in `CL_PinsReplay()` driven from `Qcommon_Frame` when `pins_replay` is
set, so the game's own timing cannot interleave.

Bit-identical to live, same renderer, same machine (the TASK27 acceptance
criterion): everything the renderer sees is in the trace by construction,
with three known leaks to close. (a) `refdef_t.time` drives auto-animation
in the renderer and is recorded, so it replays exactly. (b) Renderer cvars
read inside `RenderFrame` (`r_lerpmodels`, `gl1_*`): the recorder stores
the full cvar list with `CVAR_ARCHIVE` or the `gl1_`/`r_`/`sw_`/`vid_`
prefix at session open and the replayer sets them before registration.
(c) `randk()` use inside a renderer (particles in `gl1_mesh.c`, lightmap
noise): the recorder calls `randk_seed()`'s deterministic variant with a
fixed seed at `BeginRegistration` in both modes, which is a one-line change
in `src/common/shared/rand.c` behind the same cvar. With those three, the
same renderer replaying its own trace produces frames `framediff` scores
at 0.000000; a nonzero score is a bug in the recorder, not a tolerance.

## 5. Header sketch: `src/client/vid/header/refrecord.h`

```c
/* refrecord.h -- renderer-API recorder and replayer (TASK27) */
#ifndef CL_REFRECORD_H
#define CL_REFRECORD_H
#include <stdint.h>
#include "ref.h"

#define REFREC_MAGIC    "Q2REFREC"
#define REFREC_VERSION  1

typedef enum {
    REC_BEGINREG = 1, REC_MODEL, REC_SKIN, REC_PIC, REC_SKY, REC_ENDREG,
    REC_BEGINFRAME, REC_RENDERFRAME, REC_ENDWORLD, REC_ENDFRAME,
    REC_PICSIZE, REC_PICSCALED, REC_PICSCALEDCOL, REC_STRETCHPIC,
    REC_CHARSCALED, REC_TILECLEAR, REC_FILL, REC_FADESCREEN, REC_STRETCHRAW,
    REC_PALETTE, REC_CVARS, REC_WARN
} refrec_type_t;

typedef struct {
    char     magic[8];
    uint32_t version, api_version, flags;
    uint32_t string_table_offset, frame_index_offset, frame_count;
    char     engine_sha[16];
    uint32_t scenario_hash;
    uint16_t width, height;
    uint32_t capture_every;
} refrec_header_t;

typedef struct { uint16_t type, pad; uint32_t length; } refrec_rec_t;

/* entity_t with handles replaced by ids; fixed-width, serialized field by field */
typedef struct {
    uint32_t model_id, skin_id;
    float angles[3], origin[3], oldorigin[3], backlerp, alpha;
    int32_t frame, oldframe, skinnum, lightstyle, flags;
} refrec_entity_t;

typedef struct {
    int32_t x, y, width, height; float fov_x, fov_y;
    float vieworg[3], viewangles[3], blend[4], time; int32_t rdflags;
    uint8_t has_areabits; uint8_t areabits[32];
    lightstyle_t lightstyles[MAX_LIGHTSTYLES];
    int32_t num_entities, num_dlights, num_particles;
    /* followed by entities, dlights, particles */
} refrec_refdef_t;

/* recorder: called from the vid.c wrappers */
void RefRec_StartRecord(const char *path, int width, int height, int capture_every);
void RefRec_StopRecord(void);                  /* writes string table, index, patches header */
qboolean RefRec_Recording(void);
void RefRec_Registration(refrec_type_t t, const char *name, const void *handle);
void RefRec_RenderFrame(const refdef_t *fd);   /* deep copy, handles -> ids */
void RefRec_Draw(refrec_type_t t, const int32_t *ints, int nints, const float *floats, int nfloats, const char *name, const void *data, uint32_t datalen);
void RefRec_Control(refrec_type_t t, float arg);

/* replayer: called from CL_PinsReplay() */
qboolean RefRec_OpenReplay(const char *path, refrec_header_t *out);
qboolean RefRec_ReplayNextFrame(void);         /* false after the last frame */
void RefRec_CloseReplay(void);
#endif
```

vid.c insertion points (by function): `VID_LoadRefresh` after
`re = GetRefAPI(ri)` succeeds → `RefRec_StartRecord` when `pins_record` is
set, and the `REC_CVARS` snapshot; `VID_ShutdownRenderer` →
`RefRec_StopRecord`; `R_BeginRegistration`, `R_RegisterModel`,
`R_RegisterSkin`, `R_SetSky`, `R_EndRegistration`, `Draw_FindPic` →
`RefRec_Registration` with the returned handle; `R_RenderFrame` →
`RefRec_RenderFrame(fd)` before forwarding; every `Draw_*` → `RefRec_Draw`;
`R_SetPalette`, `R_BeginFrame`, `R_EndWorldRenderpass`, `R_EndFrame` →
`RefRec_Control`. Each is one line guarded by `RefRec_Recording()`.
New cvars: `pins_record <path>` (recorder on), `pins_replay <path>`
(replayer on), both documented in `doc/040_cvarlist.md` or
`tests/check-cvar-docs.sh` fails.

## 6. Sub-cards, in dependency order

Filing: `docs/plans/metal-port-cards.md` is the authoritative card list; the
list below is the detailed scope behind its rows.

| Card | Scope | Acceptance (one line) |
|---|---|---|
| TASK27a | `refrecord.h` + `refrecord.c` with serializers only; every vid.c wrapper gains its guarded one-liner; `pins_record` unset | Byte-identical frames to the unhooked build on the `world` scenario, `scripts/test.sh` green, `tests/check-refrecord.sh` round-trips every record type through a buffer |
| TASK27b | Recording: handle map, `refdef_t` deep copy, string table, frame index, header patch | `pins.sh record gl1 world` with `pins_record` set leaves a `trace.q2r` whose frame count equals the captured frame count and whose string table names every model, skin and pic the engine logged |
| TASK27c | Replay driver `CL_PinsReplay()` behind `pins_replay`, registration from the string table, frame feed, capture at the recorded cadence, quit | `pins.sh replay gl1 world` produces the same number of frames as the live record without the server or a demo file |
| TASK27d | Determinism: cvar snapshot and restore, fixed `randk` seed behind the cvar, `refdef_t.time` check | `framediff` scores 0.000000 on every frame, live vs replay, `gl1` and `soft`, same machine |
| TASK27e | `scripts/pins.sh replay <renderer> <scenario>` + `compare` across renderers from one trace; doctor row for trace coverage | `pins.sh replay metal world` from the `gl1` trace followed by `compare metal world --ref gl1` runs with the game absent |

TASK27a changes no behaviour and lands first; the gate stays green at every
card because replay is off unless `pins_replay` is set.
