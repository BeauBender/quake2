/*
 * Copyright (C) 2026 Yamagi Quake II contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * refrecord.h -- renderer-API recorder and replayer (TASK27). Design:
 * docs/plans/refapi-record-replay.md.
 *
 * The on-disk format is little-endian, 4-byte aligned, fixed-width,
 * floats as IEEE-754 bits, and is written and read field by field by the
 * serializers below, never by copying a struct, so it does not depend on
 * the compiler's layout. The structs here are the in-memory form only.
 *
 * Every reader treats its input as untrusted: each length and count is
 * bounded against the remaining buffer and against its own limit, nothing
 * is read past the end, and anything out of range is refused, not clamped.
 *
 * =======================================================================
 */

#ifndef CL_REFRECORD_H
#define CL_REFRECORD_H

#include <stdint.h>
#include "ref.h"

#define REFREC_MAGIC	"Q2REFREC"
#define REFREC_VERSION	1

#define REFREC_HEADER_SIZE	64	/* bytes on disk */
#define REFREC_REC_SIZE		8	/* record header bytes on disk */
#define REFREC_ENTITY_SIZE	72
#define REFREC_DLIGHT_SIZE	28
#define REFREC_PARTICLE_SIZE	20

#define REFREC_AREABYTES	32	/* MAX_MAP_AREAS / 8 */
#define REFREC_PALETTE_SIZE	768	/* 256 RGB entries */
#define REFREC_MAX_STRING	1024	/* longest name or cvar value, NUL included */
#define REFREC_MAX_CVARS	4096
#define REFREC_MAX_RAW_DIM	4096	/* cols and rows of a Draw_StretchRaw */
#define REFREC_MAX_INTS		8
#define REFREC_MAX_FLOATS	4

/* string table and frame index (section 3 of the design) */
#define REFREC_STRING_SIZE	(8 + MAX_QPATH)	/* u32 kind, u32 id, char name[MAX_QPATH] */
#define REFREC_INDEX_SIZE	12	/* u32 frame, u64 offset */

/*
 * Recorder limits. A run that reaches one stops recording cleanly: the
 * trace is finished with what it holds and REFREC_FLAG_TRUNCATED set.
 * The trace stays far below 4 GiB, so the u32 offsets of the header hold.
 */
#define REFREC_MAX_TRACE	(1024u * 1024u * 1024u)	/* bytes of records */
#define REFREC_MAX_FRAMES	(1u << 20)	/* frame index entries, 12 MiB */
#define REFREC_MAX_HANDLES	8192	/* string table entries */

/* header flags */
#define REFREC_FLAG_TRUNCATED	1	/* a recorder limit ended the recording early */

/* string table kinds */
typedef enum {
	REFREC_KIND_MODEL = 1, REFREC_KIND_SKIN, REFREC_KIND_PIC,
	REFREC_KIND_SKY, REFREC_KIND_MAP
} refrec_kind_t;

#define REFREC_KIND_FIRST	REFREC_KIND_MODEL
#define REFREC_KIND_LAST	REFREC_KIND_MAP

/* REC_WARN codes */
typedef enum {
	REFREC_WARN_UNKNOWN_HANDLE = 1,	/* a model or skin pointer not in the handle map, recorded as id 0 */
	REFREC_WARN_RAW_TOO_BIG,	/* a Draw_StretchRaw the format cannot carry; the draw is not recorded */
	REFREC_WARN_COUNT,	/* a refdef_t count out of range, recorded clamped to the engine maximum */
	REFREC_WARN_NULL_ARRAY,	/* a refdef_t array was NULL; recorded empty or zero */
	REFREC_WARN_NAME,	/* a NULL or over-long name, recorded empty or cut */
	REFREC_WARN_HANDLES_FULL,	/* the handle map is full; the handle is recorded as id 0 */
	REFREC_WARN_FRAME,	/* an REC_ENDFRAME without REC_BEGINFRAME, or a second REC_BEGINFRAME */
	REFREC_WARN_COLOR	/* Draw_PicScaledCol without a color; recorded as white */
} refrec_warn_t;

typedef enum {
	REC_BEGINREG = 1, REC_MODEL, REC_SKIN, REC_PIC, REC_SKY, REC_ENDREG,
	REC_BEGINFRAME, REC_RENDERFRAME, REC_ENDWORLD, REC_ENDFRAME,
	REC_PICSIZE, REC_PICSCALED, REC_PICSCALEDCOL, REC_STRETCHPIC,
	REC_CHARSCALED, REC_TILECLEAR, REC_FILL, REC_FADESCREEN, REC_STRETCHRAW,
	REC_PALETTE, REC_CVARS, REC_WARN, REC_CAPTURE
} refrec_type_t;

#define REC_FIRST	REC_BEGINREG
#define REC_LAST	REC_CAPTURE

/* 64 bytes on disk: the fields below in order, then 4 reserved zero bytes */
typedef struct {
	char     magic[8];
	uint32_t version, api_version, flags;
	uint32_t string_table_offset, frame_index_offset, frame_count;
	char     engine_sha[16];
	uint32_t scenario_hash;
	uint16_t width, height;
	uint32_t capture_every;
} refrec_header_t;

/* 8 bytes on disk; length counts the payload that follows, a multiple of 4 */
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
	uint8_t has_areabits; uint8_t areabits[REFREC_AREABYTES];
	lightstyle_t lightstyles[MAX_LIGHTSTYLES];
	int32_t num_entities, num_dlights, num_particles;
	/* followed by entities, dlights, particles */
} refrec_refdef_t;

/* A REC_RENDERFRAME payload in memory: the refdef and its arrays. */
typedef struct {
	refrec_refdef_t refdef;
	refrec_entity_t entities[MAX_ENTITIES];
	dlight_t dlights[MAX_DLIGHTS];
	particle_t particles[MAX_PARTICLES];
} refrec_frame_t;

/*
 * Every record other than REC_RENDERFRAME and REC_CVARS in memory. Which
 * fields a type carries is fixed per type (refrecord.c, rec_layout):
 *
 *   REC_BEGINREG     name (map)
 *   REC_MODEL, REC_SKIN, REC_PIC   id, name
 *   REC_SKY          floats rotate, axis[3]; name
 *   REC_ENDREG, REC_ENDWORLD, REC_ENDFRAME, REC_FADESCREEN   nothing
 *   REC_BEGINFRAME   float camera_separation
 *   REC_PICSIZE      ints w, h (the result); name
 *   REC_PICSCALED    ints x, y; float factor; name
 *   REC_PICSCALEDCOL ints x, y; floats factor, color[3]; name
 *   REC_STRETCHPIC, REC_TILECLEAR  ints x, y, w, h; name
 *   REC_CHARSCALED   ints x, y, num; float scale
 *   REC_FILL         ints x, y, w, h, c
 *   REC_STRETCHRAW   ints x, y, w, h, cols, rows, bits; data of
 *                    cols * rows * (bits / 8) bytes, bits 8 or 32
 *   REC_PALETTE      data of 0 bytes (NULL palette) or REFREC_PALETTE_SIZE
 *   REC_WARN         int code (refrec_warn_t); name (message)
 *   REC_CAPTURE      int pin frame number: the live run wrote
 *                    <scenario>-<number>.tga at this point of the frame
 *
 * A read leaves data pointing into the reader's buffer.
 */
typedef struct {
	refrec_type_t type;
	int32_t ints[REFREC_MAX_INTS];
	float floats[REFREC_MAX_FLOATS];
	uint32_t id;
	char name[REFREC_MAX_STRING];
	const uint8_t *data;
	uint32_t datalen;
} refrec_call_t;

/* A growable output buffer; ok turns false on the first refused write. */
typedef struct {
	uint8_t *data;
	uint32_t size, cursize;
	qboolean ok;
} refrec_writer_t;

/* A bounded input; ok turns false on the first refused read. */
typedef struct {
	const uint8_t *data;
	uint32_t size, pos;
	qboolean ok;
} refrec_reader_t;

void RefRec_WriterInit(refrec_writer_t *w);
void RefRec_WriterFree(refrec_writer_t *w);
void RefRec_ReaderInit(refrec_reader_t *r, const void *data, uint32_t size);

/* Serializers: false (and w->ok false) on a value the format cannot carry. */
qboolean RefRec_WriteHeader(refrec_writer_t *w, const refrec_header_t *h);
qboolean RefRec_WriteCall(refrec_writer_t *w, const refrec_call_t *c);
qboolean RefRec_WriteRenderFrame(refrec_writer_t *w, const refrec_frame_t *f);
qboolean RefRec_WriteCvars(refrec_writer_t *w, int count,
		const char *const *names, const char *const *values);

/*
 * Deserializers. RefRec_ReadHeader checks the magic and version. RefRec_ReadRecord
 * reads one record header and its whole payload: a REC_RENDERFRAME into
 * *frame, a REC_CVARS into *cvars (a reader over the pairs, *ncvars of them,
 * already validated), anything else into *call. A pointer for a type that
 * is not expected may be NULL, and that type is then refused.
 */
qboolean RefRec_ReadHeader(refrec_reader_t *r, refrec_header_t *h);
qboolean RefRec_ReadRecord(refrec_reader_t *r, refrec_type_t *type,
		refrec_call_t *call, refrec_frame_t *frame,
		refrec_reader_t *cvars, uint32_t *ncvars);
qboolean RefRec_NextCvar(refrec_reader_t *cvars, char *name, char *value);	/* each REFREC_MAX_STRING */

/*
 * The string table and the frame index. RefRec_ReadStringTable reads the
 * count and checks that count entries fit in what is left; each
 * RefRec_NextString then reads one entry (kind in range, name
 * NUL-terminated within MAX_QPATH and zero after it). RefRec_NextFrameIndex
 * reads one index entry.
 */
qboolean RefRec_ReadStringTable(refrec_reader_t *r, uint32_t *count);
qboolean RefRec_NextString(refrec_reader_t *r, uint32_t *kind, uint32_t *id, char *name);	/* MAX_QPATH */
qboolean RefRec_NextFrameIndex(refrec_reader_t *r, uint32_t *frame, uint64_t *offset);

/*
 * The layout of a whole trace file of file_size bytes: false unless the
 * header's offsets describe a finished trace (the records, the string
 * table at a 4-aligned string_table_offset, the frame index at a
 * 4-aligned frame_index_offset holding exactly frame_count entries up to
 * the end of the file; under 4 GiB). With frame_offset, also false
 * unless that frame-index offset lies inside the records, past the
 * header, 4-aligned and room for a record header. Neither the header
 * reader nor the index reader checks this; anything that seeks by these
 * offsets (the replayer) must call it first, for the header and for
 * every index entry, and also check that the offset names a
 * REC_BEGINFRAME. A REFREC_FLAG_TRUNCATED trace can end with an open
 * frame after the last indexed one: a replay stops at the last indexed
 * frame. The string table reader must be given exactly the bytes between
 * string_table_offset and frame_index_offset, which is what bounds the
 * table.
 */
qboolean RefRec_CheckLayout(const refrec_header_t *h, uint64_t file_size, const uint64_t *frame_offset);

/*
 * recorder: called from the vid.c wrappers. Records are streamed to the
 * file one at a time; memory held is one record (at most a full
 * REC_RENDERFRAME, about 97 KB; Draw_StretchRaw pixels are written from
 * the caller's buffer), the handle map and the frame index. A failed
 * write ends the recording and is reported once through the print
 * function (stderr until RefRec_SetPrint names another); a recorder
 * limit (REFREC_MAX_TRACE, REFREC_MAX_FRAMES) finishes the trace early
 * with REFREC_FLAG_TRUNCATED. A trace whose string_table_offset is 0 was
 * never finished. RefRec_StartRecord while recording does nothing.
 * A recording may end inside any of the calls below; the call then
 * returns normally and every later call does nothing. The recorder frees
 * its buffers only when the outermost of these calls returns.
 */
void RefRec_SetPrint(void (*print)(const char *fmt, ...));
void RefRec_SetLimit(uint32_t bytes);	/* lowers REFREC_MAX_TRACE for the next recording (tests); 0 restores it */
void RefRec_SetFrameLimit(uint32_t frames);	/* lowers REFREC_MAX_FRAMES for the next recording (tests); 0 restores it */
void RefRec_SetHandleLimit(uint32_t handles);	/* lowers REFREC_MAX_HANDLES for the next recording (tests); 0 restores it */
void RefRec_SetWriteFault(uint32_t n);	/* the n-th write, seek or close of the next recording fails (tests); 0 none; defined only when REFREC_TEST_WRITE_FAULT is */
void RefRec_StartRecord(const char *path, int width, int height, int capture_every);
void RefRec_StopRecord(void);                  /* writes string table, index, patches header */
qboolean RefRec_Recording(void);
void RefRec_Registration(refrec_type_t t, const char *name, const void *handle);
void RefRec_RenderFrame(const refdef_t *fd);   /* deep copy, handles -> ids */
void RefRec_Draw(refrec_type_t t, const int32_t *ints, int nints, const float *floats, int nfloats, const char *name, const void *data, uint32_t datalen);
void RefRec_Control(refrec_type_t t, float arg);

/*
 * REC_CVARS (TASK45). At session open vid.c hands RefRec_Cvars every
 * cvar with its flags; the recorder keeps those RefRec_CvarRecorded
 * accepts: CVAR_ARCHIVE, or a renderer prefix (REFREC_CVAR_PREFIXES).
 * A name or value that does not fit REFREC_MAX_STRING, and any past
 * REFREC_MAX_CVARS, is left out with a REC_WARN. The replayer restores a
 * pair only when RefRec_CvarRestorable accepts it against the LIVE
 * cvar's flags (the trace carries no flags): the cvar exists, its name
 * has a renderer prefix and only [A-Za-z0-9_], it is none of CVAR_NOSET,
 * CVAR_LATCH, CVAR_USERINFO, CVAR_SERVERINFO, it is not one of the
 * cvars that pick the renderer, the window, the display mode or a
 * library (REFREC_CVAR_NEVER, read only when a window or context is
 * made), and its value is printable ASCII without '"'. Anything else in
 * the trace is skipped and counted (stats.cvars_skipped), never refused.
 */
#define REFREC_CVAR_PREFIXES	"gl1_", "gl3_", "gl_", "r_", "sw_", "vid_"
#define REFREC_CVAR_NEVER	"vid_renderer", "vid_fullscreen", "vid_displayindex", "vid_displayrefreshrate", \
	"vid_highdpiaware", "vid_rate", "r_mode", "r_customwidth", "r_customheight", \
	"r_vsync", "r_msaa_samples", "gl3_libgl", "gl3_debugcontext"
qboolean RefRec_CvarRecorded(const char *name, int flags);
qboolean RefRec_CvarRestorable(const char *name, const char *value, int flags);	/* flags -1: no such cvar */
void RefRec_Cvars(int count, const char *const *names, const char *const *values, const int *flags);

/*
 * replayer: called from CL_PinsReplay() through vid.c. The trace is
 * untrusted: RefRec_OpenReplay refuses a header whose api_version, width
 * or height differ from the target's, an unfinished or truncated file,
 * and a layout RefRec_CheckLayout refuses; the string table and the frame
 * index are read and checked whole at open. RefRec_ReplayNextFrame then
 * forwards, in file order, every record up to the REC_ENDFRAME of the
 * next indexed frame, and is false after the last indexed frame or on a
 * refused record (stats.failed). An open frame after the last indexed one
 * is never replayed. The target is handed only values the live client
 * could have handed the renderer (Rep_Check in refrecord.c): a name of
 * MAX_QPATH bytes or more, or a registration whose name is not its
 * string table entry's, refuses the trace; a 2D draw outside the frame
 * is not forwarded (stats.dropped); every other value is bounded
 * (REFREC_REPLAY_*; a value moved counts in stats.clamped), and an id is
 * resolved only to a handle the replay registered under that id with the
 * kind the string table names; any other id is NULL. REC_CAPTURE numbers
 * must run 0, 1, 2, ...; REC_WARN is not forwarded, and a REC_CVARS pair
 * reaches SetCvar only as RefRec_CvarRestorable allows (above). A
 * refusal is sticky: every later RefRec_ReplayNextFrame is false and
 * forwards nothing.
 */
typedef struct {
	uint32_t api_version;	/* the trace must carry these */
	int width, height;
	/* REC_MODEL, REC_SKIN, REC_PIC: the live handle for the name */
	void *(*Register)(refrec_type_t type, const char *name);
	void (*RenderFrame)(refdef_t *fd);
	/* every other forwarded record; for REC_PICSIZE the target writes
	   the size into ints[0] and ints[1] */
	void (*Call)(refrec_call_t *c);
	/* REC_CVARS, both optional (no cvar is restored without them): the
	   live cvar's flags, -1 when there is none, and the restore */
	int (*CvarFlags)(const char *name);
	void (*SetCvar)(const char *name, const char *value);
} refrec_target_t;

typedef struct {
	uint32_t frames, captures, clamped, warns, picsize_mismatch;
	uint32_t dropped;	/* 2D draws outside the frame, not forwarded */
	uint32_t cvars_set, cvars_skipped;	/* REC_CVARS pairs restored, and not */
	qboolean failed;
} refrec_replay_stats_t;

/* bounds a replayed value is moved into */
#define REFREC_REPLAY_MAX_COORD	1000000.0f	/* world positions, angles, dlight intensity */
#define REFREC_REPLAY_MAX_TIME	10000000.0f	/* refdef_t.time, seconds */
#define REFREC_REPLAY_MAX_LIGHT	256.0f	/* lightstyle and dlight colour components */
#define REFREC_REPLAY_MAX_SCALE	64.0f	/* 2D scale factors, from 1 */
#define REFREC_REPLAY_MAX_SKINNUM	32	/* MAX_MD2SKINS; beams keep the low byte, their colour */
#define REFREC_REPLAY_RDFLAGS	15	/* RDF_UNDERWATER | RDF_NOWORLDMODEL | RDF_IRGOGGLES | RDF_UVGOGGLES */
#define REFREC_REPLAY_RFLAGS	0x7ffff	/* RF_MINLIGHT up to RF_USE_DISGUISE */
#define REFREC_REPLAY_MAX_RECORD	(REFREC_MAX_RAW_DIM * REFREC_MAX_RAW_DIM * 4 + 64)	/* payload bytes */

void RefRec_SetReplayTarget(const refrec_target_t *t);	/* copied; before RefRec_OpenReplay */
qboolean RefRec_OpenReplay(const char *path, refrec_header_t *out);
qboolean RefRec_ReplayNextFrame(void);         /* false after the last frame */
void RefRec_CloseReplay(void);
void RefRec_ReplayStats(refrec_replay_stats_t *out);	/* of the open or the last replay */
#endif
