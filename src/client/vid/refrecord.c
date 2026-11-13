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
 * Recording is off: nothing in this file switches it on. The recorder
 * entry points return at once until the recording card (handle map,
 * refdef_t deep copy, string table) lands; the serializers are complete
 * and are exercised by tests/check-refrecord.sh.
 *
 * This file calls nothing else in the engine, so the test builds it alone.
 *
 * =======================================================================
 */

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
		return R_Fail(r);
	}

	/* the counts are small enough that this product cannot overflow */
	if (R_Left(r) != (uint32_t)rd->num_entities * REFREC_ENTITY_SIZE +
			(uint32_t)rd->num_dlights * REFREC_DLIGHT_SIZE +
			(uint32_t)rd->num_particles * REFREC_PARTICLE_SIZE)
	{
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
 * Never set in this build: there is no way to start a recording yet, so
 * every wrapper's guarded hook is skipped and nothing the engine draws
 * changes.
 */
static qboolean refrec_recording = false;

qboolean
RefRec_Recording(void)
{
	return refrec_recording;
}

void
RefRec_StartRecord(const char *path, int width, int height, int capture_every)
{
	/* recording is not implemented yet; it stays off */
}

void
RefRec_StopRecord(void)
{
	refrec_recording = false;
}

void
RefRec_Registration(refrec_type_t t, const char *name, const void *handle)
{
	if (!refrec_recording)
	{
		return;
	}
}

void
RefRec_RenderFrame(const refdef_t *fd)
{
	if (!refrec_recording)
	{
		return;
	}
}

void
RefRec_Draw(refrec_type_t t, const int32_t *ints, int nints, const float *floats,
		int nfloats, const char *name, const void *data, uint32_t datalen)
{
	if (!refrec_recording)
	{
		return;
	}
}

void
RefRec_Control(refrec_type_t t, float arg)
{
	if (!refrec_recording)
	{
		return;
	}
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
