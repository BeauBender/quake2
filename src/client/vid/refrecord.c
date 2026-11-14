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
 * Renderer-API recorder (TASK27): the record serializers and the entry
 * points the vid.c wrappers call. Format and design:
 * docs/plans/refapi-record-replay.md, sections 3 and 5.
 *
 * Recording is on only between RefRec_StartRecord and RefRec_StopRecord,
 * which vid.c calls when pins_record was given on the command line
 * (CL_PinsRecord). The recorder keeps a handle map (renderer pointer to
 * stable id), deep-copies refdef_t, streams each record to the trace and
 * writes the string table, the frame index and the final header at stop.
 *
 * This file calls nothing else in the engine (only the C library), so
 * the test builds it alone.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header/refrecord.h"

/* ---- record layouts ---- */

enum {
	DATA_NONE,
	DATA_RAW,		/* Draw_StretchRaw pixels */
	DATA_PALETTE	/* 0 or REFREC_PALETTE_SIZE bytes */
};

typedef struct {
	uint8_t nints, nfloats, has_id, has_name, data;
} rec_layout_t;

/* Indexed by refrec_type_t; REC_RENDERFRAME and REC_CVARS have their own. */
static const rec_layout_t rec_layout[REC_LAST + 1] = {
	[REC_BEGINREG]     = { 0, 0, 0, 1, DATA_NONE },
	[REC_MODEL]        = { 0, 0, 1, 1, DATA_NONE },
	[REC_SKIN]         = { 0, 0, 1, 1, DATA_NONE },
	[REC_PIC]          = { 0, 0, 1, 1, DATA_NONE },
	[REC_SKY]          = { 0, 4, 0, 1, DATA_NONE },
	[REC_ENDREG]       = { 0, 0, 0, 0, DATA_NONE },
	[REC_BEGINFRAME]   = { 0, 1, 0, 0, DATA_NONE },
	[REC_ENDWORLD]     = { 0, 0, 0, 0, DATA_NONE },
	[REC_ENDFRAME]     = { 0, 0, 0, 0, DATA_NONE },
	[REC_PICSIZE]      = { 2, 0, 0, 1, DATA_NONE },
	[REC_PICSCALED]    = { 2, 1, 0, 1, DATA_NONE },
	[REC_PICSCALEDCOL] = { 2, 4, 0, 1, DATA_NONE },
	[REC_STRETCHPIC]   = { 4, 0, 0, 1, DATA_NONE },
	[REC_CHARSCALED]   = { 3, 1, 0, 0, DATA_NONE },
	[REC_TILECLEAR]    = { 4, 0, 0, 1, DATA_NONE },
	[REC_FILL]         = { 5, 0, 0, 0, DATA_NONE },
	[REC_FADESCREEN]   = { 0, 0, 0, 0, DATA_NONE },
	[REC_STRETCHRAW]   = { 7, 0, 0, 0, DATA_RAW },
	[REC_PALETTE]      = { 0, 0, 0, 0, DATA_PALETTE },
	[REC_WARN]         = { 1, 0, 0, 1, DATA_NONE },
	[REC_CAPTURE]      = { 1, 0, 0, 0, DATA_NONE },
};

static qboolean
IsCallType(int t)
{
	return t >= REC_FIRST && t <= REC_LAST &&
		t != REC_RENDERFRAME && t != REC_CVARS;
}

/*
 * The payload rules a call must meet, checked the same way before writing
 * and after reading.
 */
static qboolean
CallDataValid(const refrec_call_t *c, const rec_layout_t *l)
{
	switch (l->data)
	{
		case DATA_RAW:
		{
			int32_t cols = c->ints[4], rows = c->ints[5], bits = c->ints[6];

			if (cols < 1 || cols > REFREC_MAX_RAW_DIM ||
				rows < 1 || rows > REFREC_MAX_RAW_DIM ||
				(bits != 8 && bits != 32))
			{
				return false;
			}

			/* at most 4096 * 4096 * 4, which fits in 32 bits as unsigned */
			return c->datalen == (uint32_t)cols * (uint32_t)rows * (uint32_t)(bits / 8);
		}

		case DATA_PALETTE:
			return c->datalen == 0 || c->datalen == REFREC_PALETTE_SIZE;

		default:
			return c->datalen == 0;
	}
}

/* ---- writer ---- */

void
RefRec_WriterInit(refrec_writer_t *w)
{
	w->data = NULL;
	w->size = 0;
	w->cursize = 0;
	w->ok = true;
}

void
RefRec_WriterFree(refrec_writer_t *w)
{
	free(w->data);
	RefRec_WriterInit(w);
}

static qboolean
W_Fail(refrec_writer_t *w)
{
	w->ok = false;
	return false;
}

static uint8_t *
W_Reserve(refrec_writer_t *w, uint32_t n)
{
	uint8_t *p;

	if (!w->ok || n > 0x7fffffffu - w->cursize)
	{
		W_Fail(w);
		return NULL;
	}

	if (w->cursize + n > w->size)
	{
		uint32_t size = w->size ? w->size : 256;

		while (size < w->cursize + n)
		{
			size = (size > 0x3fffffffu) ? 0x7fffffffu : size * 2;
		}

		p = realloc(w->data, size);

		if (!p)
		{
			W_Fail(w);
			return NULL;
		}

		w->data = p;
		w->size = size;
	}

	p = w->data + w->cursize;
	w->cursize += n;

	return p;
}

static void
W_Bytes(refrec_writer_t *w, const void *src, uint32_t n)
{
	uint8_t *p;

	if (!n)
	{
		return;
	}

	p = W_Reserve(w, n);

	if (p)
	{
		memcpy(p, src, n);
	}
}

static void
W_U16(refrec_writer_t *w, uint16_t v)
{
	uint8_t *p = W_Reserve(w, 2);

	if (p)
	{
		p[0] = v & 0xff;
		p[1] = (v >> 8) & 0xff;
	}
}

static void
W_U32(refrec_writer_t *w, uint32_t v)
{
	uint8_t *p = W_Reserve(w, 4);

	if (p)
	{
		p[0] = v & 0xff;
		p[1] = (v >> 8) & 0xff;
		p[2] = (v >> 16) & 0xff;
		p[3] = (v >> 24) & 0xff;
	}
}

static void
W_I32(refrec_writer_t *w, int32_t v)
{
	W_U32(w, (uint32_t)v);
}

static void
W_F32(refrec_writer_t *w, float f)
{
	uint32_t v;

	memcpy(&v, &f, sizeof(v));
	W_U32(w, v);
}

/* zero bytes up to the next multiple of 4 */
static void
W_Pad(refrec_writer_t *w)
{
	while (w->ok && (w->cursize & 3))
	{
		uint8_t *p = W_Reserve(w, 1);

		if (p)
		{
			*p = 0;
		}
	}
}

/* u32 length, the bytes without the NUL, zero padding */
static void
W_String(refrec_writer_t *w, const char *s)
{
	size_t len;

	if (!s)
	{
		W_Fail(w);
		return;
	}

	len = strlen(s);

	if (len >= REFREC_MAX_STRING)
	{
		W_Fail(w);
		return;
	}

	W_U32(w, (uint32_t)len);
	W_Bytes(w, s, (uint32_t)len);
	W_Pad(w);
}

/* u32 length, the bytes, zero padding */
static void
W_Data(refrec_writer_t *w, const void *data, uint32_t len)
{
	if (len && !data)
	{
		W_Fail(w);
		return;
	}

	W_U32(w, len);
	W_Bytes(w, data, len);
	W_Pad(w);
}

/* Starts a record; returns the offset of its header for W_EndRecord. */
static uint32_t
W_BeginRecord(refrec_writer_t *w, refrec_type_t type)
{
	uint32_t start;

	W_Pad(w);
	start = w->cursize;
	W_U16(w, (uint16_t)type);
	W_U16(w, 0);
	W_U32(w, 0);	/* length, patched by W_EndRecord */

	return start;
}

static qboolean
W_EndRecord(refrec_writer_t *w, uint32_t start)
{
	uint32_t len;
	uint8_t *p;

	if (!w->ok)
	{
		return false;
	}

	len = w->cursize - start - REFREC_REC_SIZE;
	p = w->data + start + 4;
	p[0] = len & 0xff;
	p[1] = (len >> 8) & 0xff;
	p[2] = (len >> 16) & 0xff;
	p[3] = (len >> 24) & 0xff;

	return true;
}

qboolean
RefRec_WriteHeader(refrec_writer_t *w, const refrec_header_t *h)
{
	W_Bytes(w, h->magic, sizeof(h->magic));
	W_U32(w, h->version);
	W_U32(w, h->api_version);
	W_U32(w, h->flags);
	W_U32(w, h->string_table_offset);
	W_U32(w, h->frame_index_offset);
	W_U32(w, h->frame_count);
	W_Bytes(w, h->engine_sha, sizeof(h->engine_sha));
	W_U32(w, h->scenario_hash);
	W_U16(w, h->width);
	W_U16(w, h->height);
	W_U32(w, h->capture_every);
	W_U32(w, 0);	/* reserved */

	return w->ok;
}

qboolean
RefRec_WriteCall(refrec_writer_t *w, const refrec_call_t *c)
{
	const rec_layout_t *l;
	uint32_t start;
	int i;

	if (!IsCallType(c->type))
	{
		return W_Fail(w);
	}

	l = &rec_layout[c->type];

	if (!CallDataValid(c, l))
	{
		return W_Fail(w);
	}

	start = W_BeginRecord(w, c->type);

	if (l->has_id)
	{
		W_U32(w, c->id);
	}

	for (i = 0; i < l->nints; i++)
	{
		W_I32(w, c->ints[i]);
	}

	for (i = 0; i < l->nfloats; i++)
	{
		W_F32(w, c->floats[i]);
	}

	if (l->has_name)
	{
		W_String(w, c->name);
	}

	if (l->data != DATA_NONE)
	{
		W_Data(w, c->data, c->datalen);
	}

	return W_EndRecord(w, start);
}

qboolean
RefRec_WriteRenderFrame(refrec_writer_t *w, const refrec_frame_t *f)
{
	const refrec_refdef_t *rd = &f->refdef;
	uint32_t start;
	int i, j;

	if (rd->num_entities < 0 || rd->num_entities > MAX_ENTITIES ||
		rd->num_dlights < 0 || rd->num_dlights > MAX_DLIGHTS ||
		rd->num_particles < 0 || rd->num_particles > MAX_PARTICLES ||
		rd->has_areabits > 1)
	{
		return W_Fail(w);
	}

	start = W_BeginRecord(w, REC_RENDERFRAME);

	W_I32(w, rd->x);
	W_I32(w, rd->y);
	W_I32(w, rd->width);
	W_I32(w, rd->height);
	W_F32(w, rd->fov_x);
	W_F32(w, rd->fov_y);

	for (i = 0; i < 3; i++)
	{
		W_F32(w, rd->vieworg[i]);
	}

	for (i = 0; i < 3; i++)
	{
		W_F32(w, rd->viewangles[i]);
	}

	for (i = 0; i < 4; i++)
	{
		W_F32(w, rd->blend[i]);
	}

	W_F32(w, rd->time);
	W_I32(w, rd->rdflags);

	/* without areabits the 32 bytes are written as zero */
	W_U32(w, rd->has_areabits);

	for (i = 0; i < REFREC_AREABYTES; i++)
	{
		uint8_t b = rd->has_areabits ? rd->areabits[i] : 0;

		W_Bytes(w, &b, 1);
	}

	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		for (j = 0; j < 3; j++)
		{
			W_F32(w, rd->lightstyles[i].rgb[j]);
		}

		W_F32(w, rd->lightstyles[i].white);
	}

	W_I32(w, rd->num_entities);
	W_I32(w, rd->num_dlights);
	W_I32(w, rd->num_particles);

	for (i = 0; i < rd->num_entities; i++)
	{
		const refrec_entity_t *e = &f->entities[i];

		W_U32(w, e->model_id);
		W_U32(w, e->skin_id);

		for (j = 0; j < 3; j++)
		{
			W_F32(w, e->angles[j]);
		}

		for (j = 0; j < 3; j++)
		{
			W_F32(w, e->origin[j]);
		}

		for (j = 0; j < 3; j++)
		{
			W_F32(w, e->oldorigin[j]);
		}

		W_F32(w, e->backlerp);
		W_F32(w, e->alpha);
		W_I32(w, e->frame);
		W_I32(w, e->oldframe);
		W_I32(w, e->skinnum);
		W_I32(w, e->lightstyle);
		W_I32(w, e->flags);
	}

	for (i = 0; i < rd->num_dlights; i++)
	{
		const dlight_t *d = &f->dlights[i];

		for (j = 0; j < 3; j++)
		{
			W_F32(w, d->origin[j]);
		}

		for (j = 0; j < 3; j++)
		{
			W_F32(w, d->color[j]);
		}

		W_F32(w, d->intensity);
	}

	for (i = 0; i < rd->num_particles; i++)
	{
		const particle_t *p = &f->particles[i];

		for (j = 0; j < 3; j++)
		{
			W_F32(w, p->origin[j]);
		}

		W_I32(w, p->color);
		W_F32(w, p->alpha);
	}

	return W_EndRecord(w, start);
}

qboolean
RefRec_WriteCvars(refrec_writer_t *w, int count,
		const char *const *names, const char *const *values)
{
	uint32_t start;
	int i;

	if (count < 0 || count > REFREC_MAX_CVARS || (count && (!names || !values)))
	{
		return W_Fail(w);
	}

	start = W_BeginRecord(w, REC_CVARS);
	W_U32(w, (uint32_t)count);

	for (i = 0; i < count && w->ok; i++)
	{
		if (!names[i] || !names[i][0])
		{
			return W_Fail(w);
		}

		W_String(w, names[i]);
		W_String(w, values[i]);
	}

	return W_EndRecord(w, start);
}

/* ---- reader ---- */

void
RefRec_ReaderInit(refrec_reader_t *r, const void *data, uint32_t size)
{
	r->data = data;
	r->size = data ? size : 0;
	r->pos = 0;
	r->ok = true;
}

static qboolean
R_Fail(refrec_reader_t *r)
{
	r->ok = false;
	return false;
}

/* the next n bytes, or NULL (and the reader failed) if they are not all there */
static const uint8_t *
R_Take(refrec_reader_t *r, uint32_t n)
{
	const uint8_t *p;

	if (!r->ok || r->pos > r->size || n > r->size - r->pos)
	{
		R_Fail(r);
		return NULL;
	}

	if (!n)
	{
		return (const uint8_t *)"";	/* no arithmetic on a NULL buffer */
	}

	p = r->data + r->pos;
	r->pos += n;

	return p;
}

static uint32_t
R_Left(const refrec_reader_t *r)
{
	return (r->ok && r->pos <= r->size) ? r->size - r->pos : 0;
}

static uint16_t
R_U16(refrec_reader_t *r)
{
	const uint8_t *p = R_Take(r, 2);

	return p ? (uint16_t)(p[0] | (p[1] << 8)) : 0;
}

static uint32_t
R_U32(refrec_reader_t *r)
{
	const uint8_t *p = R_Take(r, 4);

	if (!p)
	{
		return 0;
	}

	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t
R_I32(refrec_reader_t *r)
{
	uint32_t v = R_U32(r);
	int32_t i;

	memcpy(&i, &v, sizeof(i));

	return i;
}

static float
R_F32(refrec_reader_t *r)
{
	uint32_t v = R_U32(r);
	float f;

	memcpy(&f, &v, sizeof(f));

	return f;
}

/* padding up to the next multiple of 4; refused unless every byte is zero */
static void
R_Pad(refrec_reader_t *r)
{
	while (r->ok && (r->pos & 3))
	{
		const uint8_t *p = R_Take(r, 1);

		if (p && *p)
		{
			R_Fail(r);
		}
	}
}

/* out holds REFREC_MAX_STRING bytes; refused: too long, short input, a NUL inside */
static void
R_String(refrec_reader_t *r, char *out)
{
	uint32_t len = R_U32(r);
	const uint8_t *p;

	out[0] = '\0';

	if (!r->ok || len >= REFREC_MAX_STRING)
	{
		R_Fail(r);
		return;
	}

	p = R_Take(r, len);

	if (!p || memchr(p, '\0', len))
	{
		R_Fail(r);
		return;
	}

	memcpy(out, p, len);
	out[len] = '\0';
	R_Pad(r);
}

static void
R_Data(refrec_reader_t *r, const uint8_t **data, uint32_t *len)
{
	*len = R_U32(r);
	*data = (r->ok && *len) ? R_Take(r, *len) : NULL;

	if (!r->ok)
	{
		*data = NULL;
		*len = 0;
		return;
	}

	R_Pad(r);
}

qboolean
RefRec_ReadHeader(refrec_reader_t *r, refrec_header_t *h)
{
	const uint8_t *p;

	memset(h, 0, sizeof(*h));

	if (R_Left(r) < REFREC_HEADER_SIZE)
	{
		return R_Fail(r);
	}

	p = R_Take(r, sizeof(h->magic));

	if (!p || memcmp(p, REFREC_MAGIC, sizeof(h->magic)) != 0)
	{
		return R_Fail(r);
	}

	memcpy(h->magic, p, sizeof(h->magic));
	h->version = R_U32(r);
	h->api_version = R_U32(r);
	h->flags = R_U32(r);
	h->string_table_offset = R_U32(r);
	h->frame_index_offset = R_U32(r);
	h->frame_count = R_U32(r);

	p = R_Take(r, sizeof(h->engine_sha));

	if (p)
	{
		memcpy(h->engine_sha, p, sizeof(h->engine_sha));
	}

	h->scenario_hash = R_U32(r);
	h->width = R_U16(r);
	h->height = R_U16(r);
	h->capture_every = R_U32(r);

	if (R_U32(r) != 0 || h->version != REFREC_VERSION)
	{
		return R_Fail(r);
	}

	return r->ok;
}

static qboolean
ReadCall(refrec_reader_t *r, refrec_type_t type, refrec_call_t *c)
{
	const rec_layout_t *l = &rec_layout[type];
	int i;

	memset(c, 0, sizeof(*c));
	c->type = type;

	if (l->has_id)
	{
		c->id = R_U32(r);
	}

	for (i = 0; i < l->nints; i++)
	{
		c->ints[i] = R_I32(r);
	}

	for (i = 0; i < l->nfloats; i++)
	{
		c->floats[i] = R_F32(r);
	}

	if (l->has_name)
	{
		R_String(r, c->name);
	}

	if (l->data != DATA_NONE)
	{
		R_Data(r, &c->data, &c->datalen);
	}

	if (!r->ok || !CallDataValid(c, l))
	{
		return R_Fail(r);
	}

	return true;
}

static qboolean
ReadRenderFrame(refrec_reader_t *r, refrec_frame_t *f)
{
	refrec_refdef_t *rd = &f->refdef;
	uint32_t has_areabits;
	const uint8_t *p;
	int i, j;

	memset(rd, 0, sizeof(*rd));

	rd->x = R_I32(r);
	rd->y = R_I32(r);
	rd->width = R_I32(r);
	rd->height = R_I32(r);
	rd->fov_x = R_F32(r);
	rd->fov_y = R_F32(r);

	for (i = 0; i < 3; i++)
	{
		rd->vieworg[i] = R_F32(r);
	}

	for (i = 0; i < 3; i++)
	{
		rd->viewangles[i] = R_F32(r);
	}

	for (i = 0; i < 4; i++)
	{
		rd->blend[i] = R_F32(r);
	}

	rd->time = R_F32(r);
	rd->rdflags = R_I32(r);

	has_areabits = R_U32(r);
	p = R_Take(r, REFREC_AREABYTES);

	if (!p || has_areabits > 1)
	{
		return R_Fail(r);
	}

	rd->has_areabits = (uint8_t)has_areabits;

	for (i = 0; i < REFREC_AREABYTES; i++)
	{
		/* without areabits the bytes must be zero, so a frame has one form */
		if (!has_areabits && p[i])
		{
			return R_Fail(r);
		}

		rd->areabits[i] = p[i];
	}

	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		for (j = 0; j < 3; j++)
		{
			rd->lightstyles[i].rgb[j] = R_F32(r);
		}

		rd->lightstyles[i].white = R_F32(r);
	}

	rd->num_entities = R_I32(r);
	rd->num_dlights = R_I32(r);
	rd->num_particles = R_I32(r);

	if (!r->ok ||
		rd->num_entities < 0 || rd->num_entities > MAX_ENTITIES ||
		rd->num_dlights < 0 || rd->num_dlights > MAX_DLIGHTS ||
		rd->num_particles < 0 || rd->num_particles > MAX_PARTICLES)
	{
		/* leave no hostile count behind in the caller's frame */
		rd->num_entities = rd->num_dlights = rd->num_particles = 0;

		return R_Fail(r);
	}

	/* the counts are small enough that this product cannot overflow */
	if (R_Left(r) != (uint32_t)rd->num_entities * REFREC_ENTITY_SIZE +
			(uint32_t)rd->num_dlights * REFREC_DLIGHT_SIZE +
			(uint32_t)rd->num_particles * REFREC_PARTICLE_SIZE)
	{
		rd->num_entities = rd->num_dlights = rd->num_particles = 0;

		return R_Fail(r);
	}

	for (i = 0; i < rd->num_entities; i++)
	{
		refrec_entity_t *e = &f->entities[i];

		e->model_id = R_U32(r);
		e->skin_id = R_U32(r);

		for (j = 0; j < 3; j++)
		{
			e->angles[j] = R_F32(r);
		}

		for (j = 0; j < 3; j++)
		{
			e->origin[j] = R_F32(r);
		}

		for (j = 0; j < 3; j++)
		{
			e->oldorigin[j] = R_F32(r);
		}

		e->backlerp = R_F32(r);
		e->alpha = R_F32(r);
		e->frame = R_I32(r);
		e->oldframe = R_I32(r);
		e->skinnum = R_I32(r);
		e->lightstyle = R_I32(r);
		e->flags = R_I32(r);
	}

	for (i = 0; i < rd->num_dlights; i++)
	{
		dlight_t *d = &f->dlights[i];

		for (j = 0; j < 3; j++)
		{
			d->origin[j] = R_F32(r);
		}

		for (j = 0; j < 3; j++)
		{
			d->color[j] = R_F32(r);
		}

		d->intensity = R_F32(r);
	}

	for (i = 0; i < rd->num_particles; i++)
	{
		particle_t *pt = &f->particles[i];

		for (j = 0; j < 3; j++)
		{
			pt->origin[j] = R_F32(r);
		}

		pt->color = R_I32(r);
		pt->alpha = R_F32(r);
	}

	return r->ok;
}

/* Validates every pair; leaves *cvars over the pairs. */
static qboolean
ReadCvars(refrec_reader_t *r, refrec_reader_t *cvars, uint32_t *ncvars)
{
	refrec_reader_t walk;
	char *name, *value;
	uint32_t count, i;
	qboolean ok;

	count = R_U32(r);

	/* a pair takes at least 8 bytes: two string lengths */
	if (!r->ok || count > REFREC_MAX_CVARS || count > R_Left(r) / 8)
	{
		return R_Fail(r);
	}

	name = malloc(REFREC_MAX_STRING);
	value = malloc(REFREC_MAX_STRING);

	if (!name || !value)
	{
		free(name);
		free(value);
		return R_Fail(r);
	}

	walk = *r;

	for (i = 0; i < count && walk.ok; i++)
	{
		R_String(&walk, name);
		R_String(&walk, value);

		if (walk.ok && !name[0])
		{
			R_Fail(&walk);
		}
	}

	ok = walk.ok;
	free(name);
	free(value);

	if (!ok)
	{
		return R_Fail(r);
	}

	RefRec_ReaderInit(cvars, r->data + r->pos, walk.pos - r->pos);
	*ncvars = count;
	r->pos = walk.pos;

	return true;
}

qboolean
RefRec_NextCvar(refrec_reader_t *cvars, char *name, char *value)
{
	R_String(cvars, name);
	R_String(cvars, value);

	if (!cvars->ok || !name[0])
	{
		name[0] = value[0] = '\0';
		return R_Fail(cvars);
	}

	return true;
}

qboolean
RefRec_ReadStringTable(refrec_reader_t *r, uint32_t *count)
{
	*count = R_U32(r);

	if (!r->ok || *count > REFREC_MAX_HANDLES ||
		*count > R_Left(r) / REFREC_STRING_SIZE)
	{
		*count = 0;
		return R_Fail(r);
	}

	return true;
}

qboolean
RefRec_NextString(refrec_reader_t *r, uint32_t *kind, uint32_t *id, char *name)
{
	const uint8_t *p, *nul;
	uint32_t i;

	name[0] = '\0';
	*kind = R_U32(r);
	*id = R_U32(r);
	p = R_Take(r, MAX_QPATH);

	if (!p || *kind < REFREC_KIND_FIRST || *kind > REFREC_KIND_LAST || *id == 0)
	{
		return R_Fail(r);
	}

	/* NUL-terminated inside the field and zero after it: one form per name */
	nul = memchr(p, '\0', MAX_QPATH);

	if (!nul)
	{
		return R_Fail(r);
	}

	for (i = (uint32_t)(nul - p); i < MAX_QPATH; i++)
	{
		if (p[i])
		{
			return R_Fail(r);
		}
	}

	memcpy(name, p, MAX_QPATH);

	return true;
}

qboolean
RefRec_NextFrameIndex(refrec_reader_t *r, uint32_t *frame, uint64_t *offset)
{
	uint32_t lo, hi;

	*frame = R_U32(r);
	lo = R_U32(r);
	hi = R_U32(r);
	*offset = ((uint64_t)hi << 32) | lo;

	return r->ok;
}

qboolean
RefRec_CheckLayout(const refrec_header_t *h, uint64_t file_size, const uint64_t *frame_offset)
{
	uint64_t sto = h->string_table_offset, fio = h->frame_index_offset;

	/* a finished trace: records, string table, frame index, nothing after */
	if (file_size >= 0xffffffffull || sto < REFREC_HEADER_SIZE || sto > fio ||
		fio > file_size || (sto & 3) || (fio & 3) ||
		h->frame_count > (file_size - fio) / REFREC_INDEX_SIZE ||
		fio + (uint64_t)h->frame_count * REFREC_INDEX_SIZE != file_size)
	{
		return false;
	}

	/* a frame starts with a whole record header inside the records */
	if (frame_offset && (*frame_offset < REFREC_HEADER_SIZE || (*frame_offset & 3) ||
		*frame_offset > sto - REFREC_REC_SIZE))
	{
		return false;
	}

	return true;
}

qboolean
RefRec_ReadRecord(refrec_reader_t *r, refrec_type_t *type,
		refrec_call_t *call, refrec_frame_t *frame,
		refrec_reader_t *cvars, uint32_t *ncvars)
{
	refrec_reader_t payload;
	uint16_t t, pad;
	uint32_t len;
	const uint8_t *p;
	qboolean ok;

	*type = 0;

	if (r->pos & 3)
	{
		return R_Fail(r);
	}

	t = R_U16(r);
	pad = R_U16(r);
	len = R_U32(r);

	if (!r->ok || pad != 0 || (len & 3) || t < REC_FIRST || t > REC_LAST)
	{
		return R_Fail(r);
	}

	p = R_Take(r, len);

	if (!p)
	{
		return false;
	}

	RefRec_ReaderInit(&payload, p, len);

	if (t == REC_RENDERFRAME)
	{
		ok = frame && ReadRenderFrame(&payload, frame);
	}
	else if (t == REC_CVARS)
	{
		ok = cvars && ncvars && ReadCvars(&payload, cvars, ncvars);
	}
	else
	{
		ok = call && ReadCall(&payload, t, call);
	}

	/* the payload must be used up exactly */
	if (!ok || !payload.ok || payload.pos != payload.size)
	{
		return R_Fail(r);
	}

	*type = t;

	return true;
}

/* ---- recorder ---- */

/*
 * One entry of the handle map. The id of handles[i] is i + 1; id 0 is
 * "no handle" (a NULL skin, a failed registration, an unknown pointer).
 * Sky and map entries have no pointer and are found by name.
 */
typedef struct {
	const void *ptr;
	uint32_t kind;
	char name[MAX_QPATH];
} rec_handle_t;

#define REC_HASH_SLOTS	(REFREC_MAX_HANDLES * 2)	/* a power of two */

/* the areabits copy is the size of its source, cl.frame.areabits */
typedef char rec_areabytes_check[(REFREC_AREABYTES == MAX_MAP_AREAS / 8) ? 1 : -1];

static void
Rec_DefaultPrint(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static void (*rec_print)(const char *fmt, ...) = Rec_DefaultPrint;

/*
 * Recorder state.
 *
 * A recording ends in one of these ways: a recorder limit (records past
 * the byte limit, frames past the frame limit) or a failed frame-index
 * allocation, through Rec_Finish with truncated; a failed write, seek or
 * close, or a record that cannot be serialized, through Rec_Fail;
 * RefRec_StopRecord (vid.c calls it at renderer shutdown), through
 * Rec_Finish. The first two can happen deep inside a call, for example
 * while a REC_WARN is written from the middle of Rec_Register, with the
 * handle map in use further up the stack.
 *
 * So ending a recording never frees anything. It turns on off first
 * (before any message is printed, so a print function that draws cannot
 * record), finishes or abandons the file, and leaves every buffer in
 * place. Every helper checks on before it records and finds the buffers
 * valid either way. The buffers are released in one place, Rec_Release,
 * only when no public entry point is on the call stack: each entry point
 * runs between Rec_Enter and Rec_Leave, and the outermost Rec_Leave
 * releases them.
 */
static struct {
	qboolean on;		/* the hooks record; off from the first moment of any stop */
	qboolean writing;	/* the file is open and the trace neither finished nor failed */
	FILE *f;
	uint32_t pos;		/* bytes in the file, the offset of the next record */
	uint32_t limit;		/* bytes of records allowed, REFREC_MAX_TRACE */
	uint32_t framelimit;	/* complete frames allowed, REFREC_MAX_FRAMES */
	uint32_t handlelimit;	/* handle map entries allowed, REFREC_MAX_HANDLES */
	uint32_t fileops;	/* writes, seeks and closes done */
	uint32_t fault;		/* the file operation that fails (tests), 0 none */
	refrec_header_t header;
	refrec_writer_t w;	/* the record being written, one at a time */

	rec_handle_t *handles;	/* [REFREC_MAX_HANDLES] */
	uint32_t numhandles;
	uint32_t *hash;		/* [REC_HASH_SLOTS], handle index + 1, 0 empty */

	uint32_t *frames;	/* offset of each complete frame's REC_BEGINFRAME */
	uint32_t numframes, maxframes;
	qboolean inframe;
	uint32_t framestart;
} rec;

static int rec_depth;	/* public entry points on the call stack */

static uint32_t rec_limit = REFREC_MAX_TRACE;
static uint32_t rec_frame_limit = REFREC_MAX_FRAMES;
static uint32_t rec_handle_limit = REFREC_MAX_HANDLES;
#ifdef REFREC_TEST_WRITE_FAULT
static uint32_t rec_fault;
#endif

static void Rec_Finish(qboolean truncated);

/* true while the recorder holds buffers, recording or not */
static qboolean
Rec_Held(void)
{
	return rec.handles || rec.hash || rec.frames || rec.w.data;
}

/*
 * The one place the recorder's buffers are freed: only when the
 * recording has ended and no entry point is still running.
 */
static void
Rec_Release(void)
{
	if (rec.on || rec.writing || rec_depth > 0 || !Rec_Held())
	{
		return;
	}

	RefRec_WriterFree(&rec.w);
	free(rec.handles);
	free(rec.hash);
	free(rec.frames);
	memset(&rec, 0, sizeof(rec));
}

/* a print callback must return normally: a non-local exit would leave the
   depth raised and every later recording refused */
static void
Rec_Enter(void)
{
	rec_depth++;
}

static void
Rec_Leave(void)
{
	rec_depth--;
	Rec_Release();
}

void
RefRec_SetPrint(void (*print)(const char *fmt, ...))
{
	rec_print = print ? print : Rec_DefaultPrint;
}

void
RefRec_SetLimit(uint32_t bytes)
{
	rec_limit = (bytes && bytes < REFREC_MAX_TRACE) ? bytes : REFREC_MAX_TRACE;
}

void
RefRec_SetFrameLimit(uint32_t frames)
{
	rec_frame_limit = (frames && frames < REFREC_MAX_FRAMES) ? frames : REFREC_MAX_FRAMES;
}

void
RefRec_SetHandleLimit(uint32_t handles)
{
	rec_handle_limit = (handles && handles < REFREC_MAX_HANDLES) ? handles : REFREC_MAX_HANDLES;
}

/* only in the host test build (REFREC_TEST_WRITE_FAULT); the client has no write-failure switch */
#ifdef REFREC_TEST_WRITE_FAULT
void
RefRec_SetWriteFault(uint32_t n)
{
	rec_fault = n;
}
#endif

qboolean
RefRec_Recording(void)
{
	return rec.on;
}

/*
 * A write failed: the recording ends here, reported once. The header is
 * left as written at the start (no string table, no frame index), which
 * a reader recognises as an unfinished trace. Frees nothing.
 */
static void
Rec_Fail(const char *what)
{
	if (!rec.writing)
	{
		return;
	}

	rec.on = false;
	rec.writing = false;

	if (rec.f)
	{
		fclose(rec.f);
		rec.f = NULL;
	}

	rec_print("refrec: %s; recording stopped, the trace is unfinished\n", what);
}

/* counts one write, seek or close; true when the test knob fails this one */
static qboolean
Rec_Fault(void)
{
	rec.fileops++;

#ifdef REFREC_TEST_WRITE_FAULT
	return rec.fault && rec.fileops == rec.fault;
#else
	return false;
#endif
}

static qboolean
Rec_Write(const void *src, uint32_t n)
{
	if (Rec_Fault())
	{
		return false;
	}

	return fwrite(src, 1, n, rec.f) == n;
}

/*
 * Writes what rec.w holds, then empties it. Records (limited) are written
 * only while recording and count against the limit; reaching it finishes
 * the trace early (Rec_Finish with truncated). The trailer (string table,
 * frame index) is written while the file is open and is not limited: its
 * size is bounded by REFREC_MAX_HANDLES and REFREC_MAX_FRAMES.
 */
static qboolean
Rec_Flush(qboolean limited, uint32_t extra)
{
	uint32_t n = rec.w.cursize;

	if (!rec.writing || (limited && !rec.on))
	{
		rec.w.cursize = 0;
		return false;
	}

	if (!rec.w.ok)
	{
		Rec_Fail("a record could not be serialized");
		return false;
	}

	if (limited && (rec.pos > rec.limit || n > rec.limit - rec.pos ||
		extra > rec.limit - rec.pos - n))
	{
		rec.w.cursize = 0;
		Rec_Finish(true);
		return false;
	}

	if (n && !Rec_Write(rec.w.data, n))
	{
		Rec_Fail("cannot write the trace");
		return false;
	}

	rec.pos += n;
	rec.w.cursize = 0;

	return true;
}

/* writes n more bytes straight from src, after a Rec_Flush that allowed them */
static qboolean
Rec_Raw(const void *src, uint32_t n)
{
	if (!rec.on || !rec.writing)
	{
		return false;
	}

	if (n && !Rec_Write(src, n))
	{
		Rec_Fail("cannot write the trace");
		return false;
	}

	rec.pos += n;

	return true;
}

static void
Rec_Call(const refrec_call_t *c)
{
	if (!rec.on)
	{
		return;
	}

	RefRec_WriteCall(&rec.w, c);
	Rec_Flush(true, 0);
}

static void
Rec_Warn(refrec_warn_t code, const char *msg)
{
	refrec_call_t c;

	if (!rec.on)
	{
		return;
	}

	memset(&c, 0, sizeof(c));
	c.type = REC_WARN;
	c.ints[0] = code;
	snprintf(c.name, sizeof(c.name), "%s", msg);
	Rec_Call(&c);
}

/*
 * Copies name into out (REFREC_MAX_STRING bytes), bounded: a NULL name is
 * recorded empty and an over-long one cut, each with a REC_WARN. Touches
 * no recorder state after the warning.
 */
static void
Rec_Name(char *out, const char *name)
{
	size_t len = 0;

	out[0] = '\0';

	if (!name)
	{
		Rec_Warn(REFREC_WARN_NAME, "NULL name, recorded empty");
		return;
	}

	while (len < REFREC_MAX_STRING - 1 && name[len])
	{
		len++;
	}

	memcpy(out, name, len);
	out[len] = '\0';

	if (name[len])
	{
		Rec_Warn(REFREC_WARN_NAME, "name too long, recorded cut");
	}
}

static uint32_t
Rec_Slot(const void *ptr, uint32_t kind)
{
	uint64_t v = (uint64_t)(uintptr_t)ptr;

	v ^= v >> 29;
	v *= 0x9e3779b97f4a7c15ull;
	v ^= v >> 32;

	return (uint32_t)(v + kind) & (REC_HASH_SLOTS - 1);
}

/*
 * The hash slot holding (ptr, kind), or the empty slot where it belongs;
 * NULL once the recording has ended. A retired entry (ptr NULL, see
 * Rec_Register) never matches and the probe goes on past it.
 */
static uint32_t *
Rec_Find(const void *ptr, uint32_t kind)
{
	uint32_t i, n;

	if (!rec.on || !rec.hash)
	{
		return NULL;
	}

	i = Rec_Slot(ptr, kind);

	/* the table is at most half full, so an empty slot is always found */
	for (n = 0; n < REC_HASH_SLOTS; n++)
	{
		uint32_t *slot = &rec.hash[(i + n) & (REC_HASH_SLOTS - 1)];
		const rec_handle_t *h;

		if (!*slot)
		{
			return slot;
		}

		h = &rec.handles[*slot - 1];

		if (h->ptr == ptr && h->kind == kind)
		{
			return slot;
		}
	}

	return NULL;
}

/*
 * Retire the entries of ptr, of every kind that has a pointer (model,
 * skin, pic), whose name is not name: the entry keeps its hash slot and
 * string table row, but never matches a lookup again.
 */
static void
Rec_Retire(const void *ptr, const char *name)
{
	uint32_t kind;

	for (kind = REFREC_KIND_MODEL; kind <= REFREC_KIND_PIC; kind++)
	{
		uint32_t *slot = Rec_Find(ptr, kind);

		if (slot && *slot && strncmp(rec.handles[*slot - 1].name, name, MAX_QPATH - 1) != 0)
		{
			rec.handles[*slot - 1].ptr = NULL;
		}
	}
}

/*
 * The id for a handle. The same pointer and name of one kind always get
 * the same id; the same pointer under a new name (the renderer reused
 * the memory after EndRegistration) gets a new id. Sky and map entries
 * (ptr NULL) are kept once per name. 0 once the recording has ended.
 */
static uint32_t
Rec_Register(uint32_t kind, const char *name, const void *ptr)
{
	rec_handle_t *h;
	uint32_t *slot = NULL, i;

	if (!rec.on)
	{
		return 0;
	}

	if (ptr)
	{
		slot = Rec_Find(ptr, kind);

		if (slot && *slot && strncmp(rec.handles[*slot - 1].name, name, MAX_QPATH - 1) == 0)
		{
			return *slot;
		}
	}
	else
	{
		/* only sky and map kinds come here; a retired model, skin or
		   pic entry also has ptr NULL but never their kind */
		for (i = 0; i < rec.numhandles; i++)
		{
			h = &rec.handles[i];

			if (!h->ptr && h->kind == kind && strncmp(h->name, name, MAX_QPATH - 1) == 0)
			{
				return i + 1;
			}
		}
	}

	if (rec.numhandles >= rec.handlelimit || (ptr && !slot))
	{
		/*
		 * The pointer names another file now. Retire every entry of it
		 * that carries another name, whatever its kind: Rec_Lookup finds
		 * a skin through a pic entry, and a pointer the renderer reused
		 * across kinds must not reach a frame under the old name. A
		 * later lookup misses and warns instead of finding the stale id;
		 * the entries stay in the string table for the records that
		 * already carry their ids.
		 */
		if (ptr)
		{
			Rec_Retire(ptr, name);
		}

		Rec_Warn(REFREC_WARN_HANDLES_FULL, "handle map full, recorded as id 0");
		return 0;
	}

	/* the record keeps the whole name; the table holds MAX_QPATH - 1 bytes */
	if (strlen(name) >= MAX_QPATH)
	{
		Rec_Warn(REFREC_WARN_NAME, "name longer than MAX_QPATH, cut in the string table");

		if (!rec.on)
		{
			return 0;
		}
	}

	h = &rec.handles[rec.numhandles++];
	memset(h, 0, sizeof(*h));
	h->ptr = ptr;
	h->kind = kind;

	for (i = 0; i < MAX_QPATH - 1 && name[i]; i++)
	{
		h->name[i] = name[i];
	}

	if (slot)
	{
		*slot = rec.numhandles;
	}

	return rec.numhandles;
}

/* the id of an entity's model or skin pointer; NULL is 0, unknown is 0 with a warning */
static uint32_t
Rec_Lookup(const void *ptr, uint32_t kind, uint32_t alt)
{
	uint32_t *slot;

	if (!ptr || !rec.on)
	{
		return 0;
	}

	slot = Rec_Find(ptr, kind);

	if ((!slot || !*slot) && alt)
	{
		slot = Rec_Find(ptr, alt);
	}

	if (slot && *slot)
	{
		return *slot;
	}

	Rec_Warn(REFREC_WARN_UNKNOWN_HANDLE, kind == REFREC_KIND_MODEL ?
			"entity model not in the handle map, recorded as id 0" :
			"entity skin not in the handle map, recorded as id 0");

	return 0;
}

/*
 * The pin run id is the name of the directory the trace is written to:
 * <engine>-<scenario hash>[-r<N>] (scripts/pins.sh run_id). The header
 * keeps the first 16 bytes of <engine> and the hash; another directory
 * name leaves both zero.
 */
static void
Rec_RunId(const char *path, refrec_header_t *h)
{
	const char *end, *base, *p, *hash;
	uint64_t v = 0;

	end = strrchr(path, '/');

	if (!end)
	{
		return;
	}

	for (base = end; base > path && base[-1] != '/'; base--)
	{
	}

	/* drop "-r<N>" */
	for (p = end; p > base && p[-1] >= '0' && p[-1] <= '9'; p--)
	{
	}

	if (p < end && p - base >= 2 && p[-1] == 'r' && p[-2] == '-')
	{
		end = p - 2;
	}

	for (hash = end; hash > base && hash[-1] >= '0' && hash[-1] <= '9'; hash--)
	{
	}

	if (hash == end || end - hash > 10 || hash - base < 2 || hash[-1] != '-')
	{
		return;
	}

	for (p = hash; p < end; p++)
	{
		v = v * 10 + (uint64_t)(*p - '0');
	}

	if (v > 0xffffffffu)
	{
		return;
	}

	h->scenario_hash = (uint32_t)v;
	memcpy(h->engine_sha, base, (size_t)(hash - 1 - base) < sizeof(h->engine_sha) ?
			(size_t)(hash - 1 - base) : sizeof(h->engine_sha));
}

static void
Rec_StartRecord(const char *path, int width, int height, int capture_every)
{
	/* buffers still held here belong to an entry point further up the stack */
	if (rec.on || Rec_Held())
	{
		rec_print("refrec: already recording, %s not opened\n", path ? path : "(null)");
		return;
	}

	if (!path || !path[0] || width < 0 || width > 0xffff || height < 0 || height > 0xffff)
	{
		rec_print("refrec: no trace path or a size the header cannot hold, not recording\n");
		return;
	}

	memset(&rec, 0, sizeof(rec));
	RefRec_WriterInit(&rec.w);
	rec.limit = rec_limit;
	rec.framelimit = rec_frame_limit;
	rec.handlelimit = rec_handle_limit;
#ifdef REFREC_TEST_WRITE_FAULT
	rec.fault = rec_fault;
#endif
	rec.handles = calloc(REFREC_MAX_HANDLES, sizeof(*rec.handles));
	rec.hash = calloc(REC_HASH_SLOTS, sizeof(*rec.hash));

	if (!rec.handles || !rec.hash)
	{
		rec_print("refrec: out of memory, not recording\n");
		return;
	}

	rec.f = fopen(path, "wb");

	if (!rec.f)
	{
		rec_print("refrec: cannot open %s, not recording\n", path);
		return;
	}

	memcpy(rec.header.magic, REFREC_MAGIC, sizeof(rec.header.magic));
	rec.header.version = REFREC_VERSION;
	rec.header.api_version = API_VERSION;
	rec.header.width = (uint16_t)width;
	rec.header.height = (uint16_t)height;
	rec.header.capture_every = capture_every > 0 ? (uint32_t)capture_every : 0;
	Rec_RunId(path, &rec.header);

	/* offsets and counts stay zero until Rec_Finish patches the header */
	rec.writing = true;
	rec.on = true;
	RefRec_WriteHeader(&rec.w, &rec.header);
	Rec_Flush(false, 0);
}

void
RefRec_StartRecord(const char *path, int width, int height, int capture_every)
{
	/* what an earlier recording left is released here at the latest */
	Rec_Release();
	Rec_Enter();
	Rec_StartRecord(path, width, height, capture_every);
	Rec_Leave();
}

/*
 * Writes the string table and the frame index after the last record and
 * patches the header. Truncated: a recorder limit was reached; the
 * frames recorded so far are kept and REFREC_FLAG_TRUNCATED is set.
 * Recording is off from the first line; the buffers stay until
 * Rec_Release.
 */
static void
Rec_Finish(qboolean truncated)
{
	uint32_t i;
	qboolean fault;
	FILE *f;

	if (!rec.writing)
	{
		return;
	}

	rec.on = false;
	rec.w.cursize = 0;	/* a record cut short by the stop is not written */

	if (truncated)
	{
		rec_print("refrec: the trace reached a recorder limit; recording stopped after %u frames\n",
				rec.numframes);
		rec.header.flags |= REFREC_FLAG_TRUNCATED;
	}

	rec.header.string_table_offset = rec.pos;
	W_U32(&rec.w, rec.numhandles);

	for (i = 0; i < rec.numhandles && rec.writing; i++)
	{
		W_U32(&rec.w, rec.handles[i].kind);
		W_U32(&rec.w, i + 1);
		W_Bytes(&rec.w, rec.handles[i].name, MAX_QPATH);

		if (rec.w.cursize >= 4096)
		{
			Rec_Flush(false, 0);
		}
	}

	Rec_Flush(false, 0);
	rec.header.frame_index_offset = rec.pos;

	for (i = 0; i < rec.numframes && rec.writing; i++)
	{
		W_U32(&rec.w, i);
		W_U32(&rec.w, rec.frames[i]);
		W_U32(&rec.w, 0);	/* the high half of the u64 offset */

		if (rec.w.cursize >= 4096)
		{
			Rec_Flush(false, 0);
		}
	}

	if (!Rec_Flush(false, 0))
	{
		return;
	}

	rec.header.frame_count = rec.numframes;

	if (Rec_Fault() || fseek(rec.f, 0, SEEK_SET) != 0)
	{
		Rec_Fail("cannot seek to the trace header");
		return;
	}

	RefRec_WriteHeader(&rec.w, &rec.header);

	if (!rec.w.ok || !Rec_Write(rec.w.data, rec.w.cursize))
	{
		Rec_Fail("cannot write the trace header");
		return;
	}

	rec.w.cursize = 0;
	f = rec.f;
	rec.f = NULL;
	fault = Rec_Fault();

	if (fclose(f) != 0 || fault)
	{
		Rec_Fail("cannot close the trace");
		return;
	}

	rec.writing = false;
	rec_print("refrec: trace written: %u frames, %u names, %u bytes\n",
			rec.header.frame_count, rec.numhandles, rec.pos);
}

void
RefRec_StopRecord(void)
{
	/* after any other stop there is nothing left to finish */
	if (!rec.on)
	{
		return;
	}

	Rec_Enter();
	Rec_Finish(false);
	Rec_Leave();
}

static void
Rec_Registration(refrec_type_t t, const char *name, const void *handle)
{
	refrec_call_t c;
	uint32_t kind;

	memset(&c, 0, sizeof(c));
	c.type = t;

	switch (t)
	{
		case REC_BEGINREG:
			Rec_Name(c.name, name);
			Rec_Register(REFREC_KIND_MAP, c.name, NULL);
			break;

		case REC_MODEL:
		case REC_SKIN:
		case REC_PIC:
			kind = (t == REC_MODEL) ? REFREC_KIND_MODEL :
				(t == REC_SKIN) ? REFREC_KIND_SKIN : REFREC_KIND_PIC;
			Rec_Name(c.name, name);

			/* a failed registration has no handle: id 0, no table entry */
			c.id = handle ? Rec_Register(kind, c.name, handle) : 0;
			break;

		case REC_ENDREG:
			break;

		default:
			return;
	}

	Rec_Call(&c);
}

void
RefRec_Registration(refrec_type_t t, const char *name, const void *handle)
{
	if (!rec.on)
	{
		return;
	}

	Rec_Enter();
	Rec_Registration(t, name, handle);
	Rec_Leave();
}

/* the entity, dlight and particle count to copy: 0..max, clamped with a warning */
static int32_t
Rec_Count(int n, int max, const void *array)
{
	if (n < 0 || n > max)
	{
		Rec_Warn(REFREC_WARN_COUNT, "refdef_t count out of range, recorded clamped");
		n = (n < 0) ? 0 : max;
	}

	if (n && !array)
	{
		Rec_Warn(REFREC_WARN_NULL_ARRAY, "refdef_t array is NULL, recorded empty");
		n = 0;
	}

	return n;
}

static void
Rec_RenderFrame(const refdef_t *fd)
{
	static refrec_frame_t f;
	refrec_refdef_t *rd = &f.refdef;
	int i;

	if (!fd)
	{
		Rec_Warn(REFREC_WARN_NULL_ARRAY, "RenderFrame without a refdef_t, not recorded");
		return;
	}

	memset(rd, 0, sizeof(*rd));

	/* scalars verbatim */
	rd->x = fd->x;
	rd->y = fd->y;
	rd->width = fd->width;
	rd->height = fd->height;
	rd->fov_x = fd->fov_x;
	rd->fov_y = fd->fov_y;
	memcpy(rd->vieworg, fd->vieworg, sizeof(rd->vieworg));
	memcpy(rd->viewangles, fd->viewangles, sizeof(rd->viewangles));
	memcpy(rd->blend, fd->blend, sizeof(rd->blend));
	rd->time = fd->time;
	rd->rdflags = fd->rdflags;

	/* cl.frame.areabits is byte[MAX_MAP_AREAS / 8], REFREC_AREABYTES */
	if (fd->areabits)
	{
		rd->has_areabits = 1;
		memcpy(rd->areabits, fd->areabits, REFREC_AREABYTES);
	}

	/* r_lightstyles in cl_view.c is lightstyle_t[MAX_LIGHTSTYLES] */
	if (fd->lightstyles)
	{
		memcpy(rd->lightstyles, fd->lightstyles, sizeof(rd->lightstyles));
	}
	else
	{
		Rec_Warn(REFREC_WARN_NULL_ARRAY, "refdef_t lightstyles is NULL, recorded as zero");
	}

	rd->num_entities = Rec_Count(fd->num_entities, MAX_ENTITIES, fd->entities);
	rd->num_dlights = Rec_Count(fd->num_dlights, MAX_DLIGHTS, fd->dlights);
	rd->num_particles = Rec_Count(fd->num_particles, MAX_PARTICLES, fd->particles);

	/* the lookups stop warning (and return 0) once a warning ended the recording */
	for (i = 0; i < rd->num_entities && rec.on; i++)
	{
		const entity_t *e = &fd->entities[i];
		refrec_entity_t *o = &f.entities[i];

		o->model_id = Rec_Lookup(e->model, REFREC_KIND_MODEL, 0);
		o->skin_id = Rec_Lookup(e->skin, REFREC_KIND_SKIN, REFREC_KIND_PIC);
		memcpy(o->angles, e->angles, sizeof(o->angles));
		memcpy(o->origin, e->origin, sizeof(o->origin));
		memcpy(o->oldorigin, e->oldorigin, sizeof(o->oldorigin));
		o->backlerp = e->backlerp;
		o->alpha = e->alpha;
		o->frame = e->frame;
		o->oldframe = e->oldframe;
		o->skinnum = e->skinnum;
		o->lightstyle = e->lightstyle;
		o->flags = e->flags;
	}

	if (!rec.on)
	{
		return;
	}

	if (rd->num_dlights)
	{
		memcpy(f.dlights, fd->dlights, (size_t)rd->num_dlights * sizeof(dlight_t));
	}

	if (rd->num_particles)
	{
		memcpy(f.particles, fd->particles, (size_t)rd->num_particles * sizeof(particle_t));
	}

	RefRec_WriteRenderFrame(&rec.w, &f);
	Rec_Flush(true, 0);
}

void
RefRec_RenderFrame(const refdef_t *fd)
{
	if (!rec.on)
	{
		return;
	}

	Rec_Enter();
	Rec_RenderFrame(fd);
	Rec_Leave();
}

/*
 * Draw_StretchRaw: the pixels are written from the caller's buffer, never
 * copied, so a cinematic frame adds nothing to the memory the recorder
 * holds. The bytes are those RefRec_WriteCall would write.
 */
static void
Rec_StretchRaw(const refrec_call_t *c)
{
	static const uint8_t zero[4];
	uint32_t pad = (4 - (c->datalen & 3)) & 3;
	int i;

	if (!rec.on)
	{
		return;
	}

	W_U16(&rec.w, REC_STRETCHRAW);
	W_U16(&rec.w, 0);
	W_U32(&rec.w, 7 * 4 + 4 + c->datalen + pad);

	for (i = 0; i < 7; i++)
	{
		W_I32(&rec.w, c->ints[i]);
	}

	W_U32(&rec.w, c->datalen);

	if (Rec_Flush(true, c->datalen + pad) && Rec_Raw(c->data, c->datalen))
	{
		Rec_Raw(zero, pad);
	}
}

static void
Rec_Draw(refrec_type_t t, const int32_t *ints, int nints, const float *floats,
		int nfloats, const char *name, const void *data, uint32_t datalen)
{
	const rec_layout_t *l;
	refrec_call_t c;
	int i;

	if (!IsCallType(t))
	{
		return;
	}

	l = &rec_layout[t];
	memset(&c, 0, sizeof(c));
	c.type = t;

	for (i = 0; i < l->nints && i < nints && ints; i++)
	{
		c.ints[i] = ints[i];
	}

	for (i = 0; i < l->nfloats && i < nfloats && floats; i++)
	{
		c.floats[i] = floats[i];
	}

	/* Draw_PicScaledCol passes only the factor when it has no color */
	if (t == REC_PICSCALEDCOL && (nfloats < 4 || !floats))
	{
		Rec_Warn(REFREC_WARN_COLOR, "Draw_PicScaledCol without a color, recorded as white");
		c.floats[1] = c.floats[2] = c.floats[3] = 1.0f;
	}

	if (l->has_name)
	{
		Rec_Name(c.name, name);
	}

	if (t == REC_SKY)
	{
		Rec_Register(REFREC_KIND_SKY, c.name, NULL);
	}

	if (l->data != DATA_NONE)
	{
		c.data = data;
		c.datalen = datalen;
	}

	if (!CallDataValid(&c, l) || (c.datalen && !c.data))
	{
		Rec_Warn(t == REC_STRETCHRAW ? REFREC_WARN_RAW_TOO_BIG : REFREC_WARN_NAME,
				t == REC_STRETCHRAW ? "Draw_StretchRaw image the format cannot carry, not recorded" :
				"a draw the format cannot carry, not recorded");
		return;
	}

	if (t == REC_STRETCHRAW)
	{
		Rec_StretchRaw(&c);
		return;
	}

	Rec_Call(&c);
}

void
RefRec_Draw(refrec_type_t t, const int32_t *ints, int nints, const float *floats,
		int nfloats, const char *name, const void *data, uint32_t datalen)
{
	if (!rec.on)
	{
		return;
	}

	Rec_Enter();
	Rec_Draw(t, ints, nints, floats, nfloats, name, data, datalen);
	Rec_Leave();
}

static void
Rec_Control(refrec_type_t t, float arg)
{
	refrec_call_t c;

	memset(&c, 0, sizeof(c));
	c.type = t;

	switch (t)
	{
		case REC_BEGINFRAME:
			if (rec.inframe)
			{
				Rec_Warn(REFREC_WARN_FRAME, "REC_BEGINFRAME inside a frame; the open frame is dropped from the index");
			}

			if (rec.on && rec.numframes >= rec.framelimit)
			{
				Rec_Finish(true);
				return;
			}

			if (rec.on)
			{
				rec.framestart = rec.pos;
				rec.inframe = true;
				c.floats[0] = arg;
				Rec_Call(&c);
			}

			return;

		case REC_ENDFRAME:
		{
			qboolean was = rec.inframe;

			if (!was)
			{
				Rec_Warn(REFREC_WARN_FRAME, "REC_ENDFRAME outside a frame");
			}

			rec.inframe = false;
			Rec_Call(&c);

			if (!rec.on || !was)
			{
				return;
			}

			if (rec.numframes == rec.maxframes)
			{
				uint32_t n = rec.maxframes ? rec.maxframes * 2 : 1024;
				uint32_t *p;

				if (n > REFREC_MAX_FRAMES)
				{
					n = REFREC_MAX_FRAMES;
				}

				p = realloc(rec.frames, n * sizeof(*p));

				if (!p)
				{
					Rec_Finish(true);
					return;
				}

				rec.frames = p;
				rec.maxframes = n;
			}

			rec.frames[rec.numframes++] = rec.framestart;
			return;
		}

		case REC_ENDWORLD:
			Rec_Call(&c);
			return;

		case REC_CVARS:
			/* the snapshot contents come with TASK45; the record is in place */
			RefRec_WriteCvars(&rec.w, 0, NULL, NULL);
			Rec_Flush(true, 0);
			return;

		default:
			return;
	}
}

void
RefRec_Control(refrec_type_t t, float arg)
{
	if (!rec.on)
	{
		return;
	}

	Rec_Enter();
	Rec_Control(t, arg);
	Rec_Leave();
}

/* ---- replayer ---- */

typedef char rep_rdflags_check[(REFREC_REPLAY_RDFLAGS ==
	(RDF_UNDERWATER | RDF_NOWORLDMODEL | RDF_IRGOGGLES | RDF_UVGOGGLES)) ? 1 : -1];
typedef char rep_rflags_check[(REFREC_REPLAY_RFLAGS == ((RF_USE_DISGUISE << 1) - 1)) ? 1 : -1];

/*
 * Replayer state. The string table and the frame index are read whole at
 * open, after their sizes were checked against the file and against
 * REFREC_MAX_HANDLES and REFREC_MAX_FRAMES; the records are read one at a
 * time into buf, which a forwarded call's data points into until the next
 * read.
 */
static struct {
	qboolean open;
	FILE *f;
	refrec_target_t t;
	qboolean have_target;
	refrec_header_t h;
	uint8_t *kinds;		/* [nstrings + 1], the string table kind of each id */
	char (*names)[MAX_QPATH];	/* [nstrings + 1], the string table name of each id */
	void **ptrs;		/* [nstrings + 1], the live handle registered under each id */
	uint32_t nstrings;
	uint8_t *index;		/* the frame index, frame_count entries, checked at open */
	uint32_t frame;		/* the next indexed frame */
	uint32_t pos;		/* file offset of the next record */
	uint8_t *buf;
	uint32_t bufsize;
	refrec_frame_t *in;	/* the REC_RENDERFRAME just read */
	refrec_call_t call;
	refrec_replay_stats_t stats;
} rep;

/* what the target is handed: replay-owned, rebuilt every REC_RENDERFRAME */
static entity_t rep_entities[MAX_ENTITIES];
static dlight_t rep_dlights[MAX_DLIGHTS];
static particle_t rep_particles[MAX_PARTICLES];
static lightstyle_t rep_lightstyles[MAX_LIGHTSTYLES];
static byte rep_areabits[REFREC_AREABYTES];

void
RefRec_SetReplayTarget(const refrec_target_t *t)
{
	rep.have_target = t && t->Register && t->RenderFrame && t->Call;

	if (rep.have_target)
	{
		rep.t = *t;
	}
}

void
RefRec_ReplayStats(refrec_replay_stats_t *out)
{
	*out = rep.stats;
}

static qboolean
Rep_Fail(const char *what, uint32_t at)
{
	rec_print("refrecord: replay refused: %s (offset %u, frame %u)\n", what, at, rep.frame);
	rep.stats.failed = true;

	return false;
}

/* v moved into [lo, hi]; a non-finite v becomes def */
static float
Rep_F(float v, float lo, float hi, float def)
{
	if (!isfinite(v))
	{
		rep.stats.clamped++;
		return def;
	}

	if (v < lo || v > hi)
	{
		rep.stats.clamped++;
		return v < lo ? lo : hi;
	}

	return v;
}

static int
Rep_I(int32_t v, int32_t lo, int32_t hi)
{
	if (v < lo || v > hi)
	{
		rep.stats.clamped++;
		return v < lo ? lo : hi;
	}

	return v;
}

/* a palette index: its low byte, which is what the renderers that mask draw */
static int
Rep_Byte(int32_t v)
{
	if (v < 0 || v > 255)
	{
		rep.stats.clamped++;
	}

	return v & 0xff;
}

static void
Rep_Vec(float *out, const float *in, int n, float max)
{
	int i;

	for (i = 0; i < n; i++)
	{
		out[i] = Rep_F(in[i], -max, max, 0.0f);
	}
}

/* the live handle the replay registered under id, if the table gives id kind or alt */
static void *
Rep_Handle(uint32_t id, uint32_t kind, uint32_t alt)
{
	if (id == 0 || id > rep.nstrings || (rep.kinds[id] != kind && rep.kinds[id] != alt))
	{
		return NULL;
	}

	return rep.ptrs[id];
}

void
RefRec_CloseReplay(void)
{
	if (rep.f)
	{
		fclose(rep.f);
	}

	free(rep.kinds);
	free(rep.names);
	free(rep.ptrs);
	free(rep.index);
	free(rep.buf);
	free(rep.in);
	rep.f = NULL;
	rep.kinds = NULL;
	rep.names = NULL;
	rep.ptrs = NULL;
	rep.index = NULL;
	rep.buf = NULL;
	rep.in = NULL;
	rep.bufsize = 0;
	rep.nstrings = 0;
	rep.open = false;
}

static qboolean
Rep_ReadAt(uint32_t off, void *dst, uint32_t n)
{
	return fseek(rep.f, (long)off, SEEK_SET) == 0 && fread(dst, 1, n, rep.f) == n;
}

static qboolean
Rep_Open(const char *path, refrec_header_t *out)
{
	uint8_t hb[REFREC_HEADER_SIZE], *table;
	refrec_reader_t r;
	uint32_t tablesize, indexsize, count = 0, kind, id, i, fi, prev = 0;
	uint64_t off;
	char name[MAX_QPATH];
	long size;

	if (!rep.have_target)
	{
		return Rep_Fail("no replay target", 0);
	}

	rep.f = fopen(path, "rb");

	if (!rep.f)
	{
		return Rep_Fail("cannot open the trace", 0);
	}

	if (fseek(rep.f, 0, SEEK_END) != 0 || (size = ftell(rep.f)) < 0 ||
		(uint64_t)size >= 0xffffffffull)
	{
		return Rep_Fail("cannot size the trace, or it is 4 GiB or more", 0);
	}

	RefRec_ReaderInit(&r, hb, sizeof(hb));

	if (size < REFREC_HEADER_SIZE || !Rep_ReadAt(0, hb, sizeof(hb)) ||
		!RefRec_ReadHeader(&r, &rep.h))
	{
		return Rep_Fail("not a trace", 0);
	}

	/* an unfinished trace has string_table_offset 0, which this refuses */
	if (!RefRec_CheckLayout(&rep.h, (uint64_t)size, NULL) ||
		rep.h.frame_count > REFREC_MAX_FRAMES ||
		rep.h.frame_index_offset - rep.h.string_table_offset >
			4 + REFREC_MAX_HANDLES * REFREC_STRING_SIZE)
	{
		return Rep_Fail("unfinished trace or bad offsets", 0);
	}

	if (rep.h.api_version != rep.t.api_version)
	{
		return Rep_Fail("recorded for another renderer API version", 0);
	}

	/* no value of the untrusted trace is printed, only the renderer's */
	if (rep.h.width != rep.t.width || rep.h.height != rep.t.height)
	{
		rec_print("refrecord: the renderer runs %dx%d\n", rep.t.width, rep.t.height);
		return Rep_Fail("recorded at another resolution", 0);
	}

	/* the string table: exactly the bytes up to the frame index */
	tablesize = rep.h.frame_index_offset - rep.h.string_table_offset;
	table = malloc(tablesize ? tablesize : 1);

	if (!table || !Rep_ReadAt(rep.h.string_table_offset, table, tablesize))
	{
		free(table);
		return Rep_Fail("cannot read the string table", rep.h.string_table_offset);
	}

	RefRec_ReaderInit(&r, table, tablesize);

	if (RefRec_ReadStringTable(&r, &count))
	{
		rep.kinds = calloc(count + 1, 1);
		rep.names = calloc(count + 1, sizeof(*rep.names));
		rep.ptrs = calloc(count + 1, sizeof(*rep.ptrs));
	}

	for (i = 0; r.ok && rep.kinds && rep.names && rep.ptrs && i < count; i++)
	{
		/* the name is NUL-terminated within MAX_QPATH, zero after it */
		if (!RefRec_NextString(&r, &kind, &id, name) || id != i + 1)
		{
			r.ok = false;
			break;
		}

		rep.kinds[id] = (uint8_t)kind;
		memcpy(rep.names[id], name, MAX_QPATH);
	}

	free(table);

	if (!r.ok || !rep.kinds || !rep.names || !rep.ptrs || r.pos != r.size)
	{
		return Rep_Fail("bad string table", rep.h.string_table_offset);
	}

	rep.nstrings = count;

	/* the frame index: frames 0, 1, 2, ... at rising offsets inside the records */
	indexsize = rep.h.frame_count * REFREC_INDEX_SIZE;
	rep.index = malloc(indexsize ? indexsize : 1);

	if (!rep.index || !Rep_ReadAt(rep.h.frame_index_offset, rep.index, indexsize))
	{
		return Rep_Fail("cannot read the frame index", rep.h.frame_index_offset);
	}

	RefRec_ReaderInit(&r, rep.index, indexsize);

	for (i = 0; i < rep.h.frame_count; i++)
	{
		if (!RefRec_NextFrameIndex(&r, &fi, &off) || fi != i ||
			!RefRec_CheckLayout(&rep.h, (uint64_t)size, &off) ||
			(i > 0 && off <= prev))
		{
			return Rep_Fail("bad frame index entry", rep.h.frame_index_offset + i * REFREC_INDEX_SIZE);
		}

		prev = (uint32_t)off;
	}

	rep.in = malloc(sizeof(*rep.in));

	if (!rep.in || fseek(rep.f, REFREC_HEADER_SIZE, SEEK_SET) != 0)
	{
		return Rep_Fail("out of memory", 0);
	}

	rep.pos = REFREC_HEADER_SIZE;
	*out = rep.h;

	return true;
}

qboolean
RefRec_OpenReplay(const char *path, refrec_header_t *out)
{
	if (rep.open || RefRec_Recording())
	{
		return false;
	}

	memset(&rep.stats, 0, sizeof(rep.stats));
	rep.frame = 0;

	if (!Rep_Open(path, out))
	{
		RefRec_CloseReplay();
		return false;
	}

	rep.open = true;

	return true;
}

/* the offset of indexed frame i's REC_BEGINFRAME (the low word; checked at open) */
static uint32_t
Rep_FrameStart(uint32_t i)
{
	const uint8_t *p = rep.index + i * REFREC_INDEX_SIZE + 4;

	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* the next record into rep.call or rep.in; false on anything refused */
static qboolean
Rep_Next(refrec_type_t *type)
{
	refrec_reader_t r, cvars;
	uint32_t len, ncvars;
	uint8_t hb[REFREC_REC_SIZE];

	if (rep.pos > rep.h.string_table_offset - REFREC_REC_SIZE ||
		fread(hb, 1, sizeof(hb), rep.f) != sizeof(hb))
	{
		return Rep_Fail("the records end inside a frame", rep.pos);
	}

	len = (uint32_t)hb[4] | ((uint32_t)hb[5] << 8) | ((uint32_t)hb[6] << 16) | ((uint32_t)hb[7] << 24);

	if (len > REFREC_REPLAY_MAX_RECORD ||
		len > rep.h.string_table_offset - rep.pos - REFREC_REC_SIZE)
	{
		return Rep_Fail("a record length past the records", rep.pos);
	}

	if (rep.bufsize < REFREC_REC_SIZE + len)
	{
		uint8_t *b = realloc(rep.buf, REFREC_REC_SIZE + len);

		if (!b)
		{
			return Rep_Fail("out of memory", rep.pos);
		}

		rep.buf = b;
		rep.bufsize = REFREC_REC_SIZE + len;
	}

	memcpy(rep.buf, hb, sizeof(hb));

	if (fread(rep.buf + REFREC_REC_SIZE, 1, len, rep.f) != len)
	{
		return Rep_Fail("cannot read a record", rep.pos);
	}

	RefRec_ReaderInit(&r, rep.buf, REFREC_REC_SIZE + len);

	if (!RefRec_ReadRecord(&r, type, &rep.call, rep.in, &cvars, &ncvars))
	{
		return Rep_Fail("a bad record", rep.pos);
	}

	rep.pos += REFREC_REC_SIZE + len;

	return true;
}

static void
Rep_RenderFrame(void)
{
	const refrec_refdef_t *in = &rep.in->refdef;
	refdef_t fd;
	int i, j;

	memset(&fd, 0, sizeof(fd));
	fd.x = Rep_I(in->x, 0, rep.h.width - 1);
	fd.y = Rep_I(in->y, 0, rep.h.height - 1);
	fd.width = Rep_I(in->width, 1, rep.h.width - fd.x);
	fd.height = Rep_I(in->height, 1, rep.h.height - fd.y);
	fd.fov_x = Rep_F(in->fov_x, 1.0f, 179.0f, 90.0f);
	fd.fov_y = Rep_F(in->fov_y, 1.0f, 179.0f, 90.0f);
	Rep_Vec(fd.vieworg, in->vieworg, 3, REFREC_REPLAY_MAX_COORD);
	Rep_Vec(fd.viewangles, in->viewangles, 3, REFREC_REPLAY_MAX_COORD);

	for (i = 0; i < 4; i++)
	{
		fd.blend[i] = Rep_F(in->blend[i], 0.0f, 1.0f, 0.0f);
	}

	fd.time = Rep_F(in->time, 0.0f, REFREC_REPLAY_MAX_TIME, 0.0f);

	if (in->rdflags & ~REFREC_REPLAY_RDFLAGS)
	{
		rep.stats.clamped++;
	}

	fd.rdflags = in->rdflags & REFREC_REPLAY_RDFLAGS;

	if (in->has_areabits)
	{
		memcpy(rep_areabits, in->areabits, sizeof(rep_areabits));
		fd.areabits = rep_areabits;
	}

	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		for (j = 0; j < 3; j++)
		{
			rep_lightstyles[i].rgb[j] = Rep_F(in->lightstyles[i].rgb[j], 0.0f, REFREC_REPLAY_MAX_LIGHT, 0.0f);
		}

		rep_lightstyles[i].white = Rep_F(in->lightstyles[i].white, 0.0f, REFREC_REPLAY_MAX_LIGHT, 0.0f);
	}

	fd.lightstyles = rep_lightstyles;

	/* the reader refused counts outside 0..MAX_* */
	for (i = 0; i < in->num_entities; i++)
	{
		const refrec_entity_t *s = &rep.in->entities[i];
		entity_t *e = &rep_entities[i];

		memset(e, 0, sizeof(*e));
		e->model = Rep_Handle(s->model_id, REFREC_KIND_MODEL, REFREC_KIND_MODEL);
		e->skin = Rep_Handle(s->skin_id, REFREC_KIND_SKIN, REFREC_KIND_PIC);
		Rep_Vec(e->angles, s->angles, 3, REFREC_REPLAY_MAX_COORD);
		Rep_Vec(e->origin, s->origin, 3, REFREC_REPLAY_MAX_COORD);
		Rep_Vec(e->oldorigin, s->oldorigin, 3, REFREC_REPLAY_MAX_COORD);
		e->backlerp = Rep_F(s->backlerp, 0.0f, 1.0f, 0.0f);
		e->alpha = Rep_F(s->alpha, 0.0f, 1.0f, 1.0f);
		e->frame = Rep_I(s->frame, 0, INT32_MAX);
		e->oldframe = Rep_I(s->oldframe, 0, INT32_MAX);
		/* a beam's skinnum is its palette colour, of which renderers use the
		   low byte; any other out of range is skin 0, as the renderers draw it */
		if (s->flags & RF_BEAM)
		{
			e->skinnum = s->skinnum & 0xff;
		}
		else if (s->skinnum < 0 || s->skinnum >= REFREC_REPLAY_MAX_SKINNUM)
		{
			rep.stats.clamped++;
			e->skinnum = 0;
		}
		else
		{
			e->skinnum = s->skinnum;
		}

		e->lightstyle = Rep_I(s->lightstyle, 0, MAX_LIGHTSTYLES - 1);

		/* the RF_* flags the client sets; no renderer reads another bit */
		if (s->flags & ~REFREC_REPLAY_RFLAGS)
		{
			rep.stats.clamped++;
		}

		e->flags = s->flags & REFREC_REPLAY_RFLAGS;
	}

	for (i = 0; i < in->num_dlights; i++)
	{
		const dlight_t *s = &rep.in->dlights[i];

		Rep_Vec(rep_dlights[i].origin, s->origin, 3, REFREC_REPLAY_MAX_COORD);
		/* negative colours are legal: darkening lights */
		Rep_Vec(rep_dlights[i].color, s->color, 3, REFREC_REPLAY_MAX_LIGHT);
		rep_dlights[i].intensity = Rep_F(s->intensity, -REFREC_REPLAY_MAX_COORD,
				REFREC_REPLAY_MAX_COORD, 0.0f);
	}

	for (i = 0; i < in->num_particles; i++)
	{
		const particle_t *s = &rep.in->particles[i];

		Rep_Vec(rep_particles[i].origin, s->origin, 3, REFREC_REPLAY_MAX_COORD);
		rep_particles[i].color = Rep_Byte(s->color);
		rep_particles[i].alpha = Rep_F(s->alpha, 0.0f, 1.0f, 1.0f);
	}

	fd.num_entities = in->num_entities;
	fd.entities = rep_entities;
	fd.num_dlights = in->num_dlights;
	fd.dlights = rep_dlights;
	fd.num_particles = in->num_particles;
	fd.particles = rep_particles;

	rep.t.RenderFrame(&fd);
}

/*
 * A 2D rectangle the renderers can be handed: a positive size no larger
 * than the frame the trace was recorded at (its width and height equal
 * the target's, checked at open), and wholly inside it. The GL renderers
 * clip every 2D draw themselves, but ref_soft writes its frame buffer
 * without checking several edges (Draw_CharScaled indexes it by x alone
 * and checks the bottom with the integer scale; the tinted path of
 * Draw_PicScaledCol never checks the top), so wholly inside is the rule.
 * With top, the rectangle may instead overlap the top edge (y < 0, but
 * some row inside), for the kinds whose ref_soft entry point clips that
 * edge itself and which the live client draws there (the console sliding
 * in starts above the top); every other edge stays inside.
 */
static qboolean
Rep_InFrame(int32_t x, int32_t y, int32_t w, int32_t h, qboolean top)
{
	int32_t fw = rep.h.width, fh = rep.h.height;

	/* w, h >= 1 and h <= fh first, so fw - w and fh - h cannot overflow */
	return w >= 1 && h >= 1 && h <= fh && x >= 0 && x <= fw - w &&
		y <= fh - h && (top ? y > -h : y >= 0);
}

/*
 * The size ref_soft draws a scaled pic at: the target's Draw_GetPicSize
 * (the REC_PICSIZE call), times the scale in float, cut to int, as
 * RE_Draw_PicScaled and RE_Draw_PicScaledCol compute it. False for a pic
 * the target does not have (the renderer would draw nothing) and for a
 * size past any frame (not cast, so the conversion cannot overflow).
 */
static qboolean
Rep_PicRect(const char *name, float scale, int32_t *w, int32_t *h)
{
	static refrec_call_t q;
	float sw, sh;

	memset(&q, 0, sizeof(q));
	q.type = REC_PICSIZE;
	snprintf(q.name, sizeof(q.name), "%s", name);
	q.ints[0] = q.ints[1] = -1;
	rep.t.Call(&q);

	if (q.ints[0] < 1 || q.ints[1] < 1)
	{
		return false;
	}

	sw = scale * q.ints[0];
	sh = scale * q.ints[1];

	/* false for a NaN too; the scale was checked first */
	if (!(sw < 65536.0f && sh < 65536.0f))
	{
		return false;
	}

	*w = (int32_t)sw;
	*h = (int32_t)sh;

	return true;
}

/*
 * A Draw_Fill or Draw_TileClear rectangle cut to the frame, as every
 * renderer cuts these two itself (so what is drawn does not change);
 * false when nothing of it is left.
 */
static qboolean
Rep_ClipToFrame(int32_t *n)
{
	/* in 64 bits, so x + w cannot overflow; a size below 1 leaves nothing */
	int64_t x0 = n[0], y0 = n[1], x1 = x0 + n[2], y1 = y0 + n[3];

	x0 = x0 < 0 ? 0 : x0;
	y0 = y0 < 0 ? 0 : y0;
	x1 = x1 > rep.h.width ? rep.h.width : x1;
	y1 = y1 > rep.h.height ? rep.h.height : y1;

	if (x1 <= x0 || y1 <= y0)
	{
		return false;
	}

	if (x0 != n[0] || y0 != n[1] || x1 - x0 != n[2] || y1 - y0 != n[3])
	{
		rep.stats.clamped++;
	}

	n[0] = (int32_t)x0;
	n[1] = (int32_t)y0;
	n[2] = (int32_t)(x1 - x0);
	n[3] = (int32_t)(y1 - y0);

	return true;
}

/* a 2D scale factor as the live client computes one: 1 up to the bound */
static qboolean
Rep_Scale(float s)
{
	/* false for a NaN; an infinity is past the bound */
	return s >= 1.0f && s <= REFREC_REPLAY_MAX_SCALE;
}

typedef enum { REP_FORWARD, REP_DROP, REP_REFUSE } rep_verdict_t;

/*
 * The replayer's one validation layer, run on every record before any of
 * it reaches the target. The trace is untrusted, and the renderers were
 * written for trusted callers: they copy names into MAX_QPATH buffers,
 * index the frame buffer with 2D coordinates and divide by 2D sizes
 * without checking any of them. So the replayer hands a renderer entry
 * point only values that the live client could have handed it:
 *
 *   - every forwarded name (map, model, skin, pic, sky, and the pic of
 *     every draw by name) is shorter than MAX_QPATH, as configstrings
 *     and the client's own pic names are; the reader already made it
 *     NUL-terminated with no NUL inside. A registration's name is the
 *     one its id's string table entry holds. Otherwise the trace is
 *     refused (REP_REFUSE).
 *   - every 2D draw that reaches a pixel loop lies wholly in the frame,
 *     its rectangle computed as ref_soft computes it (Rep_InFrame,
 *     Rep_PicRect); only a character and a stretched pic may overlap the
 *     top edge, which ref_soft clips for those two. Draw_Fill and
 *     Draw_TileClear, which every renderer cuts itself, are cut to it,
 *     and a scale factor is one the client computes (Rep_Scale). A draw
 *     that is not is not forwarded (REP_DROP, stats.dropped): it would
 *     draw nothing, or write outside the frame.
 *   - enum-like values are masked to what the renderers take (a
 *     character, a palette index), floats are moved into finite bounds
 *     (stats.clamped), and a REC_RENDERFRAME is rebuilt from bounded
 *     values by Rep_RenderFrame.
 *   - counts, lengths and payloads were checked by the reader: refdef_t
 *     counts within MAX_ENTITIES, MAX_DLIGHTS and MAX_PARTICLES, a raw
 *     image's columns, rows and bits agreeing with its data length, a
 *     palette of 0 or 768 bytes (CallDataValid).
 *
 * A refusal prints the kind of fault and the record's offset, never a
 * value read from the trace.
 */
static rep_verdict_t
Rep_Check(refrec_type_t type, uint32_t at)
{
	refrec_call_t *c = &rep.call;
	int32_t *n = c->ints;
	float *f = c->floats;
	int32_t w, h;
	int i;

	if (type == REC_RENDERFRAME || type == REC_CVARS)
	{
		return REP_FORWARD;
	}

	if (rec_layout[type].has_name && type != REC_WARN && !memchr(c->name, '\0', MAX_QPATH))
	{
		Rep_Fail("a name of MAX_QPATH bytes or more", at);
		return REP_REFUSE;
	}

	switch (type)
	{
		case REC_MODEL:
		case REC_SKIN:
		case REC_PIC:
		{
			uint32_t kind = type == REC_MODEL ? REFREC_KIND_MODEL :
				type == REC_SKIN ? REFREC_KIND_SKIN : REFREC_KIND_PIC;

			/* id 0 is a registration that failed live: made again, kept nowhere */
			if (c->id && (c->id > rep.nstrings || rep.kinds[c->id] != kind))
			{
				Rep_Fail("a registration id the string table does not name", at);
				return REP_REFUSE;
			}

			if (c->id && strcmp(c->name, rep.names[c->id]) != 0)
			{
				Rep_Fail("a registration name its string table entry does not hold", at);
				return REP_REFUSE;
			}

			return REP_FORWARD;
		}

		case REC_CAPTURE:
			if ((uint32_t)n[0] != rep.stats.captures)
			{
				Rep_Fail("a capture out of order", at);
				return REP_REFUSE;
			}

			return REP_FORWARD;

		case REC_CHARSCALED:
			/* every renderer masks the character; the console passes signed chars */
			n[2] &= 0xff;

			/*
			 * ref_soft draws 8 * (int)scale columns and at most
			 * (int)(8 * scale) rows: ceil(8 * scale) covers both. The top
			 * may overlap: RE_Draw_CharScaled (soft/sw_draw.c:85-86 and
			 * 101-106) returns for y <= -8 and otherwise starts at row 0
			 * with 8 + y source rows, at most (8 + y) * scale <=
			 * y + ceil(8 * scale) rows, which this bounds by the height.
			 * The console's version line starts above the top while it
			 * slides in (cl_console.c, lines - 35 * scale).
			 */
			if (!Rep_Scale(f[0]) || !Rep_InFrame(n[0], n[1], (int32_t)ceilf(8.0f * f[0]),
					(int32_t)ceilf(8.0f * f[0]), true))
			{
				return REP_DROP;
			}

			return REP_FORWARD;

		case REC_PICSCALEDCOL:
			for (i = 1; i < 4; i++)
			{
				f[i] = Rep_F(f[i], 0.0f, REFREC_REPLAY_MAX_LIGHT, 1.0f);
			}

			/* fall through: the rectangle */
		case REC_PICSCALED:
			/* wholly inside: the tinted path of RE_Draw_PicScaledCol never checks the top */
			return Rep_Scale(f[0]) && Rep_PicRect(c->name, f[0], &w, &h) &&
				Rep_InFrame(n[0], n[1], w, h, false) ? REP_FORWARD : REP_DROP;

		case REC_STRETCHPIC:
			/*
			 * The top may overlap: RE_Draw_StretchPicImplementation
			 * (soft/sw_draw.c:176-183 refuses the sides and the bottom,
			 * 189-196 skips the rows above the top), and its transparent
			 * same-size path, which ignores the skip, writes h <= height
			 * rows from row 0. The console background slides in from
			 * above the top (cl_console.c, Draw_StretchPic at
			 * -viddef.height + lines).
			 */
			return Rep_InFrame(n[0], n[1], n[2], n[3], true) ? REP_FORWARD : REP_DROP;

		case REC_STRETCHRAW:
			return Rep_InFrame(n[0], n[1], n[2], n[3], false) ? REP_FORWARD : REP_DROP;

		case REC_FILL:
			n[4] = Rep_Byte(n[4]);
			/* fall through: the rectangle */
		case REC_TILECLEAR:
			return Rep_ClipToFrame(n) ? REP_FORWARD : REP_DROP;

		case REC_SKY:
			Rep_Vec(f, f, 4, REFREC_REPLAY_MAX_COORD);
			return REP_FORWARD;

		case REC_BEGINFRAME:
			Rep_Vec(f, f, 1, REFREC_REPLAY_MAX_COORD);
			return REP_FORWARD;

		default:
			/* REC_BEGINREG (its name checked above), REC_ENDREG,
			   REC_ENDWORLD, REC_ENDFRAME, REC_FADESCREEN and REC_PICSIZE
			   carry nothing else a renderer takes; REC_PALETTE was
			   checked by the reader; REC_WARN is not forwarded */
			return REP_FORWARD;
	}
}

/*
 * A new registration sequence: the renderer frees, at this map change or
 * at the EndRegistration after it, every model and skin not registered
 * again (an inline model lives on the world model's hunk). So every model
 * and skin handle is forgotten, and an id not registered again resolves
 * to NULL, never to a pointer from before. Pics are never freed.
 */
static void
Rep_ForgetHandles(void)
{
	uint32_t i;

	for (i = 1; i <= rep.nstrings; i++)
	{
		if (rep.kinds[i] == REFREC_KIND_MODEL || rep.kinds[i] == REFREC_KIND_SKIN)
		{
			rep.ptrs[i] = NULL;
		}
	}
}

/* forwards one record that Rep_Check passed; false when the trace is refused */
static qboolean
Rep_Forward(refrec_type_t type, uint32_t at)
{
	refrec_call_t *c = &rep.call;
	void *p;

	switch (Rep_Check(type, at))
	{
		case REP_REFUSE:
			return false;

		case REP_DROP:
			rep.stats.dropped++;
			return true;

		default:
			break;
	}

	switch (type)
	{
		case REC_MODEL:
		case REC_SKIN:
		case REC_PIC:
			p = rep.t.Register(type, c->name);

			if (c->id)
			{
				rep.ptrs[c->id] = p;
			}

			return true;

		case REC_RENDERFRAME:
			Rep_RenderFrame();
			return true;

		case REC_BEGINREG:
			Rep_ForgetHandles();
			rep.t.Call(c);
			return true;

		case REC_CVARS:
			return true;	/* restored by TASK45 */

		case REC_WARN:
			rep.stats.warns++;
			return true;

		case REC_CAPTURE:
			rep.stats.captures++;
			rep.t.Call(c);
			return true;

		case REC_PICSIZE:
		{
			int32_t w = c->ints[0], h = c->ints[1];

			rep.t.Call(c);

			if (c->ints[0] != w || c->ints[1] != h)
			{
				rep.stats.picsize_mismatch++;
			}

			return true;
		}

		default:
			rep.t.Call(c);
			return true;
	}
}

qboolean
RefRec_ReplayNextFrame(void)
{
	uint32_t start, at;
	refrec_type_t type;
	qboolean inframe = false;

	if (!rep.open || rep.stats.failed || rep.frame >= rep.h.frame_count)
	{
		return false;
	}

	start = Rep_FrameStart(rep.frame);

	for (;;)
	{
		at = rep.pos;

		/* the records before an indexed frame replay in place, the frame from its offset */
		if (at > start && !inframe)
		{
			return Rep_Fail("a frame index offset that is not a record", at);
		}

		if (!Rep_Next(&type))
		{
			return false;
		}

		if (at == start)
		{
			if (type != REC_BEGINFRAME)
			{
				return Rep_Fail("a frame index offset that is not a REC_BEGINFRAME", at);
			}

			inframe = true;
		}
		else if (inframe && type == REC_BEGINFRAME)
		{
			return Rep_Fail("a REC_BEGINFRAME inside an indexed frame", at);
		}

		if (!Rep_Forward(type, at))
		{
			return false;
		}

		if (inframe && type == REC_ENDFRAME)
		{
			rep.frame++;
			rep.stats.frames++;
			return true;
		}
	}
}
