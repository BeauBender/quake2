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
static uint32_t rec_fault;

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

void
RefRec_SetWriteFault(uint32_t n)
{
	rec_fault = n;
}

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

	return rec.fault && rec.fileops == rec.fault;
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
		 * The pointer's entry names another file now. Retire it, so a
		 * later lookup of the pointer misses and warns instead of
		 * finding the stale id; the entry stays in the string table for
		 * the records that already carry its id.
		 */
		if (slot && *slot)
		{
			rec.handles[*slot - 1].ptr = NULL;
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
	rec.fault = rec_fault;
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

qboolean
RefRec_OpenReplay(const char *path, refrec_header_t *out)
{
	/* replay is not implemented yet */
	return false;
}

qboolean
RefRec_ReplayNextFrame(void)
{
	return false;
}

void
RefRec_CloseReplay(void)
{
}
