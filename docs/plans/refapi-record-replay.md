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
(`re = GetRefAPI(ri)` in `VID_LoadRenderer`) and exposes it to the rest of
the client through named wrappers. No client file touches `re.` directly
(verified: the only `re.` hits outside vid.c are `re.wav` strings). The
recorder therefore lives in those wrappers; no second function table, no
change to `API_VERSION 8`, no change to any renderer.

| `refexport_t` member | vid.c wrapper | Class | Recorded as |
|---|---|---|---|
| `Init`, `Shutdown` | `VID_LoadRenderer`, `VID_ShutdownRenderer` | control | session open/close |
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

As built (TASK43). Every size the recorder copies comes from the source's
own declaration: `areabits` 32 bytes from `cl.frame.areabits[MAX_MAP_AREAS/8]`
(a compile-time check ties the two), `lightstyles` from
`r_lightstyles[MAX_LIGHTSTYLES]`, the palette 768 bytes from
`cl.cinematicpalette[768]`, the raw pixels `cols*rows*(bits/8)` as
`cl_cin.c` allocates them. The engine is the source here, not a file, so
a count outside `0..MAX_*` is recorded clamped to the maximum with a
`REC_WARN` (`REFREC_WARN_COUNT`) instead of refused, a NULL array with a
nonzero count is recorded empty, and NULL `lightstyles` as zeros, each with
a `REC_WARN` (`REFREC_WARN_NULL_ARRAY`). A NULL entity model is id 0
without a warning (beams carry none). An entity skin is looked up as a skin
first, then as a pic (the same image can come from either). The same
pointer and name always map to the same id; the same pointer under a new
name (memory the renderer reused after `EndRegistration`) gets a new id.
When the map is full (`REFREC_MAX_HANDLES`) that new name gets id 0 with a
`REFREC_WARN_HANDLES_FULL`, and the pointer's old id is retired: the
string table keeps its entry for the records that carry it, but an entity
using the pointer afterwards is recorded as id 0 with a
`REFREC_WARN_UNKNOWN_HANDLE`, never under the old name.
A failed registration (NULL handle) is recorded with id 0 and has no string
table entry. Measured on `soft`: about 6.2 KB per 3D frame on demo1.

## 3. On-disk format

One file per record run, `<store>/<scenario>/<renderer>/<sha>-<hash>/trace.q2r`.
As built (TASK43), the engine writes `<pins_dir>/<pins_record>`, where
`pins_record` is a plain file name latched from the command line like the
other pin arguments, and `pins.sh record` passes `trace.q2r`; only the
first renderer session of a run is recorded, so a renderer restart never
overwrites the trace. `engine_sha` (the first 16 bytes of the engine part)
and `scenario_hash` are parsed from the pin directory's name,
`<engine>-<scenario hash>[-r<N>]`; another name leaves them zero. `flags`
bit 0 (`REFREC_FLAG_TRUNCATED`) marks a trace a recorder limit ended
early. The header is written first with zero offsets and counts and
patched last, so a trace with `string_table_offset` 0 was never finished
(the engine died, or a write failed).

- Header (64 B): magic `Q2REFREC`, `u32 version = 1`, `u32 api_version`
  (8), `u32 flags`, `u32 string_table_offset`, `u32 frame_index_offset`,
  `u32 frame_count`, `char engine_sha[16]`, `u32 scenario_hash` (same
  `cksum` as `scripts/pins.sh`), `u16 width, height`, `u32 pins_capture`
  cadence. That is 60 bytes of fields followed by 4 reserved bytes, written
  as zero; the reader refuses a header in which they are not zero.
- Records: `u16 type`, `u16 pad`, `u32 length`, payload. The payload layout
  of each record type, and the limits a record must meet
  (`REFREC_MAX_STRING`, `REFREC_MAX_CVARS`, `REFREC_MAX_RAW_DIM`), are
  defined in `src/client/vid/header/refrecord.h`, which is authoritative.
  Little-endian,
  4-byte alignment, fixed-width integers, floats as IEEE-754 bits. Written
  with explicit serializers, never `fwrite(&struct)`, so the format does not
  depend on the compiler's layout.
- String table at `string_table_offset`: `u32 count`, then
  `u32 kind (model|skin|pic|sky|map), u32 id, char name[MAX_QPATH]`.
  Written at `VID_ShutdownRenderer`, so the header is patched last. As
  built: kinds are `refrec_kind_t` 1 to 5, ids run 1 to `count` in order
  (0 is "no handle"), each name is NUL-terminated inside its 64 bytes and
  zero after (a longer name is cut in the table, with a `REC_WARN`; the
  registration record keeps it whole), sky and map entries are kept once
  per name, and at most `REFREC_MAX_HANDLES` (8192) entries.
- Frame index after the string table: `u32 frame, u64 offset` per frame so
  a replay can seek to any frame and a compare can report by frame number.
  As built: one entry per complete frame (a `REC_BEGINFRAME` closed by a
  `REC_ENDFRAME`), `frame` counting 0, 1, 2, ..., `offset` the file offset
  of that `REC_BEGINFRAME`. `frame_count` is the number of rendered frames
  (707 for demo1), not the number captured: the live run captures every
  Nth frame of demo playback only (`SCR_PinsCapture`), so the frames before
  playback are recorded but never captured. Where each pin frame was taken
  is a `REC_CAPTURE` record (added by TASK43, type 23, `int` pin frame
  number), written by `VID_WritePinFrame` inside the frame, before
  `REC_ENDFRAME`; there is one per frame file and their numbers run 0, 1,
  2, ... like the file names.
- Streaming and limits (TASK43): each record is built in one buffer and
  written at once, so the recorder holds one record (at most a full
  `REC_RENDERFRAME`, about 97 KB; `Draw_StretchRaw` pixels are written from
  the caller's buffer), the handle map (about 0.7 MB) and the frame index
  (12 bytes a frame, at most `REFREC_MAX_FRAMES`, 2^20). Records are
  limited to `REFREC_MAX_TRACE` (1 GiB), which keeps every offset far below
  4 GiB. Reaching either limit finishes the trace with what it holds and
  sets `REFREC_FLAG_TRUNCATED`; a failed write or seek ends the recording
  at once, reported once through `Com_Printf`, and leaves the trace
  unfinished. Neither stops the game. A `Draw_StretchRaw` the format cannot
  carry (a side over `REFREC_MAX_RAW_DIM`, `bits` not 8 or 32, no pixels) is
  a `REC_WARN` (`REFREC_WARN_RAW_TOO_BIG`) in place of the draw.

Size: ~13 KB per typical frame; a 1000-frame scenario is ~13 MB, under
the store, never committed and never uploaded (INFORM9: it embeds
asset-derived data).
Measured (TASK43, `soft`, 640x480, demo1): `world` 4,378,708 bytes and
`hud2d` 4,406,852 bytes for 707 frames, about 4.4 MB per default run.

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
4. Where the trace holds a `REC_CAPTURE` record (TASK43; not every frame
   whose index is a multiple of the cadence, since the live count starts
   with demo playback), run the `screenshot` command synchronously before `R_EndFrame`, the same
   place the live run captures (`SCR_PinsCapture`; gl1's `R_ScreenShot`
   reads the back buffer at command time), so `VID_WritePinFrame` writes them to
   `pins_dir` with the same frame numbering.
5. At the last frame, `quit`.

The replay path never runs `CL_Frame`, the server, sound or input: it is a
loop in `CL_PinsReplay()` driven from `Qcommon_Frame` when `pins_replay` is
set, so the game's own timing cannot interleave.

As built (TASK44). `pins_replay` is latched from the command line with the
other pin values (`CL_PinsLatch`): a `.q2r` path of `[A-Za-z0-9_./-]`
without `..`, refused beside `pins_record` or without `pins_dir`; a refused
one stops the engine in `CL_Init`, so a replay never falls back to the
attract demo, and `Qcommon_Init` queues no `d1` while replaying. The
replayer is in `refrecord.c` and still calls nothing in the engine: vid.c
hands it a `refrec_target_t` (the API version and size it must match,
`Register`, `RenderFrame`, `Call`) whose functions are the same wrappers
the client calls, so `tests/refrecord` replays into a mock renderer.
`RefRec_OpenReplay` reads the header, `RefRec_CheckLayout`, the string
table (exactly the bytes up to the frame index, at most
`REFREC_MAX_HANDLES` entries) and the whole frame index (at most
`REFREC_MAX_FRAMES`, offsets rising, each checked) before anything is
allocated by them; the records are then read one at a time, each length
bounded by the bytes left before the string table and by
`REFREC_REPLAY_MAX_RECORD`. `RefRec_ReplayNextFrame` forwards every
record in file order up to the `REC_ENDFRAME` of the next indexed frame
(records before an indexed `REC_BEGINFRAME`, a dropped open frame
included, replay in place; an open frame after the last indexed one does
not), refuses an index offset that is not a `REC_BEGINFRAME` and a
`REC_BEGINFRAME` inside an indexed frame, and refuses `REC_CAPTURE`
numbers out of order. Every record then passes one validation layer,
`Rep_Check`, before any of it reaches the target, with three outcomes.
It refuses the trace (refuse) for a name of `MAX_QPATH` bytes or more
and for a registration whose id does not have the kind, or whose name is
not the name, of its string table entry. It does not forward a 2D draw
(drop, counted in `refrec_replay_stats_t.dropped`) that ref_soft would
draw outside its frame buffer: the rectangle, computed as ref_soft
computes it (`ceil(8 * scale)` square for a character; the target's
`Draw_GetPicSize` times the scale, cut to int, for a scaled pic; the
given size for a stretched pic or raw image), must have a size of 1 up
to the frame and lie wholly inside it, except that a character and a
stretched pic may overlap the top edge, which ref_soft clips for those
two (the console slides in from above); a scale outside 1..64 or not
finite, and a pic the target lacks, are dropped too. It cuts
`Draw_Fill` and `Draw_TileClear` to the frame (clip), as every renderer
does itself. Every other value is bounded (`REFREC_REPLAY_*` in
refrecord.h): the viewport inside the recorded size, `rdflags` to the
four `RDF_` bits and entity flags to `REFREC_REPLAY_RFLAGS`, fov to
1..179, blend, alpha and backlerp to 0..1, non-finite floats to a
default, positions and angles to ±10^6; entity `frame`/`oldframe` at
least 0, `skinnum` 0..31 or 0 (a beam keeps its low byte, its colour),
`lightstyle` 0..255; particle colour, fill colour and character to their
low byte. Each moved or cut value counts in `stats.clamped` (character
masking excepted: every renderer masks, and the console passes signed
chars). An entity id resolves only to a handle registered under it with
that kind (a skin may be a pic), and every model and skin handle is
forgotten at each `REC_BEGINREG`, because the renderer frees what is not
registered again; anything else is NULL. The engine prints the counts at
the end; the `world`, `hud2d` and `cinematic` traces of demo1 and
`idlog.cin` count 0 dropped and 0 bounded and replay byte-identical to
live on `soft`. `REC_CVARS` is restored as built by TASK45 (below). A
replay is a pin run: no configuration file is read or written. A refused trace ends the
engine with `ERR_FATAL`, the last frame with `quit`, and a renderer that
fails to load is not replaced by another.

Trust boundary: a trace is a local development artifact, recorded by the
owner's own engine and named on the command line; it is derived from game
data and never leaves the machine. Replaying a trace obtained from
another party is not supported. The validation layer is defence in
depth, not a sandbox: the renderers' own missing bounds checks (ref_soft's
2D entry points among them) are tracked on FIX40 (issue #185).
Measured (`soft`, 640x480, demo1 `world`, Apple silicon host at load
average 117 to 200): the live record wrote 23 frames and a 4,380,868-byte
trace of 707 frames in 7.0 s; two replays of it with `game.dylib` moved
away wrote 23 frames with the same names in 3.0 to 3.2 s, 0 values
bounded, 0 pic size mismatches.

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

As built (TASK45).
(a) `refdef_t.time` is written as its IEEE-754 bits and handed on by
`Rep_RenderFrame` unchanged when it lies in 0..`REFREC_REPLAY_MAX_TIME`;
`tests/refrecord` replays 12.345, 1/3, a subnormal, -0.0, 9999999 and 0
and compares the bits the mock renderer receives with the live ones.
(b) `REC_CVARS`: at session open `VID_LoadRenderer` hands every cvar with
its flags to `RefRec_Cvars`, which keeps those `RefRec_CvarRecorded`
accepts: a name starting with one of `REFREC_CVAR_PREFIXES` and none of
`CVAR_NOSET`, `CVAR_LATCH`, `CVAR_USERINFO`, `CVAR_SERVERINFO`. This
deliberately narrows the card's "`CVAR_ARCHIVE` or the prefix": the
replayer can only ever restore a prefixed cvar of none of those flags
(the allow-rule below), so an archived cvar without the prefix was
stored and never used, and that set holds the player's `name`, `skin`
and `rate`, the server address book `adr0`..`adr8` and `hostname`
(with `pins_record` and no `pins_capture` the user's `config.cfg` is
read, so these are the user's own values). The snapshot now stores
exactly what a replay can use. The prefixes are `gl1_`, `gl3_`, `gl_`,
`r_`, `sw_`, `vid_` (`gl_` and `gl3_` were added to the card's list
because `ref_gl1` reads
`gl_polyblend`, `gl_texturemode` and other `gl_` cvars, and `ref_gl3` its
`gl3_` ones). A NULL or over-long name, or a stored cvar's over-long
value, is left out with one `REFREC_WARN_NAME` (a latched renderer cvar, in this tree
only `r_farsee`, is neither stored nor restored: a replay uses the replaying
process's value, so record and replay with the same setting), more than `REFREC_MAX_CVARS` with a
`REFREC_WARN_COUNT`. The replayer forwards the record in place (it comes
before the first `REC_BEGINREG`) and sets a pair through the target's
`SetCvar` (vid.c: `Cvar_Set`) only when `RefRec_CvarRestorable` accepts
it against the flags of the LIVE cvar (`CvarFlags`; the trace carries no
flags). The allow-rule: the cvar exists (a replay never creates one);
its name has a renderer prefix (`CVAR_ARCHIVE` alone is not enough: no
client, server, sound or input runs in a replay, and archived cvars such
as `logfile`, `name` or `sv_entfile` write files or reach a server) and
only `[A-Za-z0-9_]`; it is none of `CVAR_NOSET`, `CVAR_LATCH`
(`game`, `basedir` and the other directory and module cvars),
`CVAR_USERINFO`, `CVAR_SERVERINFO`; it is not in `REFREC_CVAR_NEVER`,
the cvars read only when a window or context is made (`vid_renderer`,
which would reload the renderer, `vid_fullscreen`, `vid_displayindex`, `vid_displayrefreshrate`,
`vid_highdpiaware`, `vid_rate`, `r_mode`, `r_customwidth`,
`r_customheight`, `r_vsync`, `r_msaa_samples`, `gl3_debugcontext`,
`gl1_stereo`, whose change makes gl1 and gl3 ask for a `vid_restart`) or
that names a library to load (`gl3_libgl`); and its value is printable
ASCII without `"`. No cvar the allow-rule admits writes a file or runs
a command. Anything else is skipped and counted
(`refrec_replay_stats_t.cvars_skipped`), never refused; the reader had
already bounded the count (`REFREC_MAX_CVARS`), every length
(`REFREC_MAX_STRING`) and refused an empty or unterminated name. The
values are never saved: `CL_WriteConfiguration` returns for a pin run.
Format: no version bump. `REC_CVARS` (type 21) and its payload, `u32
count` then count name and value strings, were part of version 1 from
TASK27a, and the reader refuses an unknown record type (above
`REC_LAST`), so a new type would have needed one; a trace written before
TASK45 carries an empty snapshot and replays as before.
(c) No renderer in this tree calls `randk()`, `frandk()` or `crandk()`
(the `gl1_mesh.c` particles above draw none): the random numbers of
explosions and particles are the client's (`cl_effects.c`,
`cl_particles.c`, `cl_tempentities.c`), and the trace records their
result. Live against replay is therefore deterministic without a seed.
The seed makes two live runs agree: `randk_seed_fixed(seed)` in
`rand.c` (splitmix64 of the seed into `xs` and `cng`, the whole state
reset) runs at every `R_BeginRegistration` when `pins_seed` was latched
(`CL_PinsSeed`), which `CL_PinsLatch` accepts only from the command line,
1 to 9 digits without a leading zero (but `0`, so a seed has one pin
id), beside `pins_record` or `pins_replay`, so a config file, the
console or a server cannot set it. Without it `randk_seed()` is
unchanged (entropy from `Sys_GetRandomBytes`). It reaches the engine's
copy of `rand.c` only, which the client shares with the listen server in
its process; the game module's copy (seeded by `InitGame`) and the
dedicated server, which link neither `vid.c` nor `cl_pins.c`, are never
reseeded (`tests/check-pins-latch.sh`), and the connectionless challenge
draws on `Sys_GetRandomBytes` directly, not on `randk()`, which the unit
test `randk_fixed_seed_not_in_challenge` shows. `scripts/pins.sh record`
passes it from `PINS_SEED`.
Measured: live against replay 0.000000 on every frame of every scenario
on `soft` and `gl1-metal`; two live `particles` records 0.000000 with
`PINS_SEED`, 0.011087 without (`doc/100_renderpins.md`, *Determinism*).

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

vid.c insertion points (by function), as built by TASK27a:
`VID_LoadRenderer` after `re.Init()` succeeds records the `REC_CVARS`
snapshot (a failed `Init` opens no session, and the renderer's cvars exist
by then); `VID_ShutdownRenderer` → `RefRec_StopRecord`;
`R_BeginRegistration`, `R_RegisterModel`, `R_RegisterSkin`,
`R_EndRegistration`, `Draw_FindPic` → `RefRec_Registration` with the
returned handle; `R_SetSky` and `R_SetPalette` go through `RefRec_Draw`
instead, because `RefRec_Registration(t, name, handle)` and
`RefRec_Control(t, float)` cannot carry their arguments (rotate and axis;
the palette bytes); `R_RenderFrame` → `RefRec_RenderFrame(fd)` before
forwarding; every `Draw_*` → `RefRec_Draw`; `R_BeginFrame`,
`R_EndWorldRenderpass`, `R_EndFrame` → `RefRec_Control`. Each is one block
guarded by `RefRec_Recording()`. `RefRec_StartRecord` is called (TASK43)
through `VID_StartRecord` at the session-open point in `VID_LoadRenderer`,
after `re.Init()` succeeds (so `viddef` holds the size), before the
`REC_CVARS` snapshot and outside the `RefRec_Recording()` guard, since
recording is off until it has run. `VID_WritePinFrame` carries one more
guarded hook, for `REC_CAPTURE`. The `REC_CVARS` record was written empty
(count 0) until TASK45 filled it through `RefRec_Cvars` (section 4). The recorder reports through
`RefRec_SetPrint` (set to `Com_Printf`; stderr in the test), so refrecord.c
still calls nothing else in the engine.
New settings: `pins_record <name>` (recorder on; as built a plain file name
inside `pins_dir` ending in `.q2r`, latched from the command line, not a
cvar) and `pins_replay <path>` (replayer on), both documented in
`doc/040_cvarlist.md` or `tests/check-cvar-docs.sh` fails.

### For the next cards

TASK27a only serializes and hooks; these obligations pass to the cards that
record and replay, and are not implemented yet.

- The replayer must bound every value it reads from a file before forwarding
  it to a renderer: the viewport and `rdflags`, every float, handles and ids,
  entity `frame`, `oldframe`, `skinnum` and `lightstyle`, and 2D draw
  coordinates. It must check the header's offsets and counts against the
  file size, and refuse a file of 4 GiB or more (`RefRec_ReaderInit` takes a
  `uint32_t` size). The readers validate framing, not meaning.
- `refrec_call_t.data` points into the reader's buffer after a read; the
  replayer keeps that buffer alive and unmodified until the call has been
  forwarded.
- Done in TASK43: the recorder copies names bounded and NUL-terminated
  (a NULL name is recorded empty, a longer one cut, each with a
  `REC_WARN`). It treats a writer whose `ok` is false as the end of the
  recording, not as one skipped record. A `Draw_StretchRaw` image above
  `REFREC_MAX_RAW_DIM` in either dimension is a `REC_WARN`, not a dead
  writer.
- Done in TASK43: the `Draw_GetPicSize` hook reads `*w` and `*h` only when
  the pointers are not NULL, and the `Draw_PicScaledCol` hook reads
  `color[0..2]` only when `color` is not NULL; without one it passes the
  factor alone and the recorder writes white with a `REC_WARN`.
- For TASK44: the replayer captures at `REC_CAPTURE` records, not at a
  frame-number cadence (section 4), walks the frame index rather than
  assuming every record belongs to a frame (a truncated trace can end with
  an unterminated frame after the last indexed one), refuses a trace whose
  `string_table_offset` is 0, and reads the string table and frame index
  with `RefRec_ReadStringTable`, `RefRec_NextString` and
  `RefRec_NextFrameIndex`. The test tool `refrecord-test --read <trace>`
  already walks a whole trace that way and prints counts.
- For TASK44: before it seeks, the replayer checks the header and every
  frame-index entry with `RefRec_CheckLayout` against the size of the
  file (the test reader does the same), and that each indexed offset names
  a `REC_BEGINFRAME`. On a `REFREC_FLAG_TRUNCATED` trace it stops at the
  last indexed frame: the records of an open frame after it are not a
  frame to replay.
- For TASK44: the string table reader (`RefRec_ReadStringTable`) must be
  given exactly the bytes between `string_table_offset` and
  `frame_index_offset`; that bound, with `RefRec_CheckLayout`, is what keeps
  the table inside the file.

## 6. Sub-cards, in dependency order

Filing: `docs/plans/metal-port-cards.md` is the authoritative card list; the
list below is the detailed scope behind its rows.

| Card | Scope | Acceptance (one line) |
|---|---|---|
| TASK27a | `refrecord.h` + `refrecord.c` with serializers only; every vid.c wrapper gains its guarded one-liner; `pins_record` unset | Byte-identical frames to the unhooked build on the `world` scenario, `scripts/test.sh` green, `tests/check-refrecord.sh` round-trips every record type through a buffer |
| TASK27b | Recording: handle map, `refdef_t` deep copy, string table, frame index, header patch | `pins.sh record gl1 world` with `pins_record` set leaves a `trace.q2r` whose frame count equals the captured frame count and whose string table names every model, skin and pic the engine logged (as built, TASK43: the captured frames are its `REC_CAPTURE` records, one per frame file; `frame_count` counts every rendered frame) |
| TASK27c | Replay driver `CL_PinsReplay()` behind `pins_replay`, registration from the string table, frame feed, capture at the recorded cadence, quit (amended for TASK44: capture at the `REC_CAPTURE` records, not at a cadence; bound entity skin, frame and particle colour before a frame reaches a renderer) | `pins.sh replay gl1 world` produces the same number of frames as the live record without the server or a demo file |
| TASK27d | Determinism: cvar snapshot and restore, fixed `randk` seed behind the cvar, `refdef_t.time` check | `framediff` scores 0.000000 on every frame, live vs replay, `gl1` and `soft`, same machine |
| TASK27e | `scripts/pins.sh replay <renderer> <scenario>` + `compare` across renderers from one trace; doctor row for trace coverage | `pins.sh replay metal world` from the `gl1` trace followed by `compare metal world --ref gl1` runs with the game absent |

TASK27a changes no behaviour and lands first; the gate stays green at every
card because replay is off unless `pins_replay` is set.

As built (TASK46, sub-card e). No engine change: `scripts/pins.sh replay
<renderer> <scenario> [--trace-of <renderer>] [--ref-id <dir>] [--run N]
[--replace]` starts the engine with `pins_replay` and the window size,
`pins_dir`, `pins_scenario`, `pins_capture` and `pins_seed` a record
passes, and no scenario line (the trace restores the renderer cvars). The
card's `--from` is `--trace-of`, and the record is chosen by `--ref-id`
as `compare` chooses a reference, else the newest complete live record
with a trace. The driver checks before the engine starts what it can
check without parsing records: the trace's physical path is the one
inside the physical store (no link on any component, nor on the renderer
directory written into), its path and the frame paths fit 255 bytes (the
Windows `MAX_OSPATH`), and its header (read as bytes by `od`, never
evaluated) is finished (`string_table_offset` not 0), not
`REFREC_FLAG_TRUNCATED`, of `REFREC_VERSION` and `API_VERSION` as the
source headers define them, has frames, and is of `PINS_WIDTH` x
`PINS_HEIGHT` and `PINS_EVERY`; the engine checks the rest. The replay is
a pin of its own, `<engine>-replay-<trace-of>-<record>[-r<N>]`, with the
replay manifest `replay.txt` written before the engine starts (trace,
size, checksum, seed, and whether a game module lay on the engine's
search path, decided from the files); it completes on the record's terms
plus the record's frame count. `compare` prints `ref-kind` and
`candidate-kind` (`live` or `replay`, from `replay.txt` being there) and
never picks a replay as the default reference; `list` reports trace
coverage. The card's "doctor row" is that `list` output. Measured on
`world` (`doc/100_renderpins.md`, *Replaying a trace*): `soft` and
`gl1-metal` each from its own trace and each from the other's, three
runs each with no game module where the engine looks for one, 0
differing frames of 23 against the live record of the renderer replayed
into; the same binary refuses a live record for want of the game module.
The `gl1` and `metal` renderers of the table's acceptance line are
`gl1-metal` and `soft` on this host; the native `ref_metal` (TASK28) replays
the same way once it exists.
