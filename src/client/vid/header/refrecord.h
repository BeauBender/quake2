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
#define REFREC_PALETTE_SIZE	1024	/* 256 RGBA entries */
#define REFREC_MAX_STRING	1024	/* longest name or cvar value, NUL included */
#define REFREC_MAX_CVARS	4096
#define REFREC_MAX_RAW_DIM	4096	/* cols and rows of a Draw_StretchRaw */
#define REFREC_MAX_INTS		8
#define REFREC_MAX_FLOATS	4

typedef enum {
	REC_BEGINREG = 1, REC_MODEL, REC_SKIN, REC_PIC, REC_SKY, REC_ENDREG,
	REC_BEGINFRAME, REC_RENDERFRAME, REC_ENDWORLD, REC_ENDFRAME,
	REC_PICSIZE, REC_PICSCALED, REC_PICSCALEDCOL, REC_STRETCHPIC,
	REC_CHARSCALED, REC_TILECLEAR, REC_FILL, REC_FADESCREEN, REC_STRETCHRAW,
	REC_PALETTE, REC_CVARS, REC_WARN
} refrec_type_t;

#define REC_FIRST	REC_BEGINREG
#define REC_LAST	REC_WARN

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
 * fields a type carries is fixed per type (refrec.c, rec_layout):
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
 *   REC_WARN         int code; name (message)
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
