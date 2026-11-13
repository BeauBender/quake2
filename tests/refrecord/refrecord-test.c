/*
 * Copyright (C) 2026 Yamagi Quake II contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * tests/refrecord/refrecord-test.c -- built by tests/check-refrecord.sh
 * against the real src/client/vid/refrecord.c. Round-trips every record
 * type through a memory buffer and compares field by field (floats by
 * their bits), pins the little-endian byte layout of a few records, and
 * feeds truncated, oversized-length and out-of-range inputs, each of which
 * must be refused without reading past the end of its buffer (the script
 * builds this with ASAN and UBSAN when the compiler has them). Prints one
 * line per failure and "refrecord: N checks, 0 failed" at the end; exits
 * non-zero on any failure.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/vid/header/refrecord.h"

static int checks, failures;

#define CHECK(cond, ...) \
	do \
	{ \
		checks++; \
		if (!(cond)) \
		{ \
			failures++; \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)

static uint32_t
Bits(float f)
{
	uint32_t v;

	memcpy(&v, &f, sizeof(v));

	return v;
}

static float
FromBits(uint32_t v)
{
	float f;

	memcpy(&f, &v, sizeof(f));

	return f;
}

/* floats that only survive a bit-exact copy */
static float
OddFloat(int i)
{
	static const uint32_t odd[] = {
		0x7fc00001u,	/* a quiet NaN with payload */
		0x80000000u,	/* -0.0 */
		0x7f800000u,	/* +inf */
		0x00000001u,	/* the smallest denormal */
		0x3f800000u,	/* 1.0 */
		0xc2c80000u,	/* -100.0 */
	};

	return FromBits(odd[i % (int)(sizeof(odd) / sizeof(odd[0]))]);
}

static void
SetU32(uint8_t *p, uint32_t v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = (v >> 24) & 0xff;
}

/*
 * Reads one record from an exact-size heap copy of buf[0..len), so that
 * ASAN reports any read past its end.
 */
static qboolean
ReadExact(const uint8_t *buf, uint32_t len, refrec_type_t *type,
		refrec_call_t *call, refrec_frame_t *frame,
		refrec_reader_t *cvars, uint32_t *ncvars, uint8_t **keep)
{
	refrec_reader_t r;
	uint8_t *copy = malloc(len ? len : 1);
	qboolean ok;

	if (len)
	{
		memcpy(copy, buf, len);
	}

	RefRec_ReaderInit(&r, copy, len);
	ok = RefRec_ReadRecord(&r, type, call, frame, cvars, ncvars);

	/* a successful read must consume exactly the record */
	if (ok && r.pos != len)
	{
		ok = false;
	}

	if (keep)
	{
		*keep = copy;
	}
	else
	{
		free(copy);
	}

	return ok;
}

/* Every strict prefix of a valid record is refused. */
static void
CheckTruncations(const char *what, const uint8_t *buf, uint32_t len)
{
	static refrec_frame_t frame;
	refrec_call_t call;
	refrec_reader_t cvars;
	refrec_type_t type;
	uint32_t ncvars, n;

	for (n = 0; n < len; n++)
	{
		qboolean ok = ReadExact(buf, n, &type, &call, &frame, &cvars, &ncvars, NULL);

		CHECK(!ok, "%s: truncated to %u of %u bytes was accepted", what, n, len);
	}
}

/* A record header claiming more payload than is present, or a length not a multiple of 4, is refused. */
static void
CheckLengthField(const char *what, const uint8_t *buf, uint32_t len)
{
	static refrec_frame_t frame;
	static const uint32_t lengths[] = { 0xffffffffu, 0x7ffffffcu, 0x80000000u, 0 };
	refrec_call_t call;
	refrec_reader_t cvars;
	refrec_type_t type;
	uint32_t ncvars, payload = len - REFREC_REC_SIZE;
	uint8_t *copy = malloc(len);
	size_t i;

	for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++)
	{
		uint32_t l = lengths[i] ? lengths[i] : payload + 4;

		memcpy(copy, buf, len);
		SetU32(copy + 4, l);
		CHECK(!ReadExact(copy, len, &type, &call, &frame, &cvars, &ncvars, NULL),
				"%s: record length %u over %u payload bytes was accepted", what, l, payload);
	}

	if (payload >= 4)
	{
		memcpy(copy, buf, len);
		SetU32(copy + 4, payload - 2);
		CHECK(!ReadExact(copy, len, &type, &call, &frame, &cvars, &ncvars, NULL),
				"%s: unaligned record length was accepted", what);

		/* a shorter, aligned length cuts the payload: refused, not read short */
		memcpy(copy, buf, len);
		SetU32(copy + 4, payload - 4);
		CHECK(!ReadExact(copy, len - 4, &type, &call, &frame, &cvars, &ncvars, NULL),
				"%s: payload cut by 4 bytes was accepted", what);
	}

	/* trailing zero bytes inside the payload are refused */
	{
		uint8_t *longer = calloc(1, len + 4);

		memcpy(longer, buf, len);
		SetU32(longer + 4, payload + 4);
		CHECK(!ReadExact(longer, len + 4, &type, &call, &frame, &cvars, &ncvars, NULL),
				"%s: trailing bytes in the payload were accepted", what);
		free(longer);
	}

	/* nonzero pad in the record header */
	memcpy(copy, buf, len);
	copy[2] = 1;
	CHECK(!ReadExact(copy, len, &type, &call, &frame, &cvars, &ncvars, NULL),
			"%s: nonzero record pad was accepted", what);

	free(copy);
}

static void
CompareCall(const char *what, const refrec_call_t *a, const refrec_call_t *b)
{
	int i;

	CHECK(a->type == b->type, "%s: type %d != %d", what, a->type, b->type);
	CHECK(a->id == b->id, "%s: id %u != %u", what, a->id, b->id);

	for (i = 0; i < REFREC_MAX_INTS; i++)
	{
		CHECK(a->ints[i] == b->ints[i], "%s: ints[%d] %d != %d", what, i, a->ints[i], b->ints[i]);
	}

	for (i = 0; i < REFREC_MAX_FLOATS; i++)
	{
		CHECK(Bits(a->floats[i]) == Bits(b->floats[i]), "%s: floats[%d] %08x != %08x",
				what, i, Bits(a->floats[i]), Bits(b->floats[i]));
	}

	CHECK(strcmp(a->name, b->name) == 0, "%s: name '%s' != '%s'", what, a->name, b->name);
	CHECK(a->datalen == b->datalen, "%s: datalen %u != %u", what, a->datalen, b->datalen);

	if (a->datalen == b->datalen && a->datalen)
	{
		CHECK(a->data && b->data && memcmp(a->data, b->data, a->datalen) == 0,
				"%s: data differs", what);
	}
}

/*
 * Fills a call of type t with distinct values in exactly the fields its
 * layout carries (the reader zeroes the rest), round-trips it and runs the
 * refusal checks on its bytes.
 */
static void
RoundTripCall(refrec_type_t t, int nints, int nfloats, qboolean id,
		const char *name, const void *data, uint32_t datalen, const char *what)
{
	refrec_writer_t w;
	refrec_call_t in, out;
	refrec_type_t type;
	uint8_t *keep;
	int i;

	memset(&in, 0, sizeof(in));
	in.type = t;

	for (i = 0; i < nints; i++)
	{
		in.ints[i] = (int32_t)(0x01020304 * (i + 1)) ^ (t << 24);
	}

	for (i = 0; i < nfloats; i++)
	{
		in.floats[i] = OddFloat(i + t);
	}

	if (id)
	{
		in.id = 0xa5000000u + t;
	}

	if (name)
	{
		snprintf(in.name, sizeof(in.name), "%s", name);
	}

	in.data = data;
	in.datalen = datalen;

	if (t == REC_STRETCHRAW)
	{
		in.ints[4] = 3;
		in.ints[5] = 5;
		in.ints[6] = datalen == 15 ? 8 : 32;
	}

	RefRec_WriterInit(&w);
	CHECK(RefRec_WriteCall(&w, &in), "%s: write refused", what);
	CHECK(w.cursize % 4 == 0, "%s: record of %u bytes is not 4-aligned", what, w.cursize);

	if (w.ok)
	{
		CHECK(ReadExact(w.data, w.cursize, &type, &out, NULL, NULL, NULL, &keep),
				"%s: read refused", what);
		CHECK(type == t, "%s: read type %d", what, type);
		CompareCall(what, &in, &out);
		free(keep);

		CheckTruncations(what, w.data, w.cursize);
		CheckLengthField(what, w.data, w.cursize);
	}

	RefRec_WriterFree(&w);
}

static void
TestCalls(void)
{
	static uint8_t raw8[15], raw32[60], palette[REFREC_PALETTE_SIZE];
	int i;

	for (i = 0; i < (int)sizeof(raw8); i++)
	{
		raw8[i] = (uint8_t)(i * 7 + 1);
	}

	for (i = 0; i < (int)sizeof(raw32); i++)
	{
		raw32[i] = (uint8_t)(255 - i);
	}

	for (i = 0; i < (int)sizeof(palette); i++)
	{
		palette[i] = (uint8_t)(i ^ 0x5a);
	}

	RoundTripCall(REC_BEGINREG, 0, 0, false, "base1", NULL, 0, "REC_BEGINREG");
	RoundTripCall(REC_MODEL, 0, 0, true, "models/monsters/soldier/tris.md2", NULL, 0, "REC_MODEL");
	RoundTripCall(REC_SKIN, 0, 0, true, "players/male/grunt.pcx", NULL, 0, "REC_SKIN");
	RoundTripCall(REC_PIC, 0, 0, true, "conchars", NULL, 0, "REC_PIC");
	RoundTripCall(REC_SKY, 0, 4, false, "unit1_", NULL, 0, "REC_SKY");
	RoundTripCall(REC_ENDREG, 0, 0, false, NULL, NULL, 0, "REC_ENDREG");
	RoundTripCall(REC_BEGINFRAME, 0, 1, false, NULL, NULL, 0, "REC_BEGINFRAME");
	RoundTripCall(REC_ENDWORLD, 0, 0, false, NULL, NULL, 0, "REC_ENDWORLD");
	RoundTripCall(REC_ENDFRAME, 0, 0, false, NULL, NULL, 0, "REC_ENDFRAME");
	RoundTripCall(REC_PICSIZE, 2, 0, false, "inventory", NULL, 0, "REC_PICSIZE");
	RoundTripCall(REC_PICSCALED, 2, 1, false, "i_health", NULL, 0, "REC_PICSCALED");
	RoundTripCall(REC_PICSCALEDCOL, 2, 4, false, "ch1", NULL, 0, "REC_PICSCALEDCOL");
	RoundTripCall(REC_STRETCHPIC, 4, 0, false, "conback", NULL, 0, "REC_STRETCHPIC");
	RoundTripCall(REC_CHARSCALED, 3, 1, false, NULL, NULL, 0, "REC_CHARSCALED");
	RoundTripCall(REC_TILECLEAR, 4, 0, false, "backtile", NULL, 0, "REC_TILECLEAR");
	RoundTripCall(REC_FILL, 5, 0, false, NULL, NULL, 0, "REC_FILL");
	RoundTripCall(REC_FADESCREEN, 0, 0, false, NULL, NULL, 0, "REC_FADESCREEN");
	RoundTripCall(REC_STRETCHRAW, 4, 0, false, NULL, raw8, sizeof(raw8), "REC_STRETCHRAW 8-bit");
	RoundTripCall(REC_STRETCHRAW, 4, 0, false, NULL, raw32, sizeof(raw32), "REC_STRETCHRAW 32-bit");
	RoundTripCall(REC_PALETTE, 0, 0, false, NULL, palette, sizeof(palette), "REC_PALETTE");
	RoundTripCall(REC_PALETTE, 0, 0, false, NULL, NULL, 0, "REC_PALETTE NULL");
	RoundTripCall(REC_WARN, 1, 0, false, "entity model not registered", NULL, 0, "REC_WARN");
	/* names of every length near the 4-byte padding boundary, and the longest allowed */
	RoundTripCall(REC_PIC, 0, 0, true, "", NULL, 0, "REC_PIC empty name");
	RoundTripCall(REC_PIC, 0, 0, true, "abc", NULL, 0, "REC_PIC 3");
	RoundTripCall(REC_PIC, 0, 0, true, "abcd", NULL, 0, "REC_PIC 4");
	RoundTripCall(REC_PIC, 0, 0, true, "abcde", NULL, 0, "REC_PIC 5");
	{
		char longest[REFREC_MAX_STRING];

		memset(longest, 'x', sizeof(longest) - 1);
		longest[sizeof(longest) - 1] = '\0';
		RoundTripCall(REC_PIC, 0, 0, true, longest, NULL, 0, "REC_PIC longest name");
	}
	{
		/* one byte longer than name[] holds, with every byte present */
		uint32_t len = REFREC_REC_SIZE + 8 + REFREC_MAX_STRING;
		uint8_t *rec = calloc(1, len);
		refrec_call_t *out = malloc(sizeof(*out));
		refrec_type_t type;

		rec[0] = REC_PIC;
		SetU32(rec + 4, len - REFREC_REC_SIZE);
		SetU32(rec + 12, REFREC_MAX_STRING);
		memset(rec + 16, 'x', REFREC_MAX_STRING);
		CHECK(!ReadExact(rec, len, &type, out, NULL, NULL, NULL, NULL),
				"a name of REFREC_MAX_STRING bytes was accepted");
		free(out);
		free(rec);
	}
}

/* The palette payload is 256 RGB triples; hand the writer a block of exactly
   that size so a read past it is an ASAN report. */
static void
TestPaletteExact(void)
{
	uint8_t *pal = malloc(REFREC_PALETTE_SIZE);
	int i;

	CHECK(REFREC_PALETTE_SIZE == 768, "palette size is not 256 RGB triples");

	if (pal == NULL)
	{
		CHECK(0, "out of memory");
		return;
	}

	for (i = 0; i < REFREC_PALETTE_SIZE; i++)
	{
		pal[i] = (uint8_t)(i * 3 + 1);
	}

	RoundTripCall(REC_PALETTE, 0, 0, false, NULL, pal, REFREC_PALETTE_SIZE, "REC_PALETTE exact heap block");
	free(pal);
}

/* The writer refuses what the format cannot carry. */
static void
TestWriteRefusals(void)
{
	static uint8_t raw[64];
	refrec_writer_t w;
	refrec_call_t c;
	char toolong[REFREC_MAX_STRING + 1];
	int bad_types[] = { 0, REC_RENDERFRAME, REC_CVARS, REC_LAST + 1, -1 };
	size_t i;

	for (i = 0; i < sizeof(bad_types) / sizeof(bad_types[0]); i++)
	{
		memset(&c, 0, sizeof(c));
		c.type = bad_types[i];
		RefRec_WriterInit(&w);
		CHECK(!RefRec_WriteCall(&w, &c) && !w.ok, "WriteCall accepted type %d", bad_types[i]);
		RefRec_WriterFree(&w);
	}

	memset(toolong, 'y', sizeof(toolong) - 1);
	toolong[sizeof(toolong) - 1] = '\0';
	memset(&c, 0, sizeof(c));
	c.type = REC_STRETCHPIC;
	RefRec_WriterInit(&w);
	/* name[] cannot hold toolong, so check the writer through the cvar path */
	{
		const char *names[] = { "cvar" }, *values[] = { toolong };

		CHECK(!RefRec_WriteCvars(&w, 1, names, values), "WriteCvars accepted an over-long value");
	}
	RefRec_WriterFree(&w);

	/* Draw_StretchRaw: payload size must match cols * rows * bits / 8 */
	memset(&c, 0, sizeof(c));
	c.type = REC_STRETCHRAW;
	c.ints[4] = 4;
	c.ints[5] = 4;
	c.ints[6] = 8;
	c.data = raw;
	c.datalen = 15;
	RefRec_WriterInit(&w);
	CHECK(!RefRec_WriteCall(&w, &c), "StretchRaw with a short payload was written");
	RefRec_WriterFree(&w);
	c.datalen = 32;	/* what 16 bits would need, so only the bit depth is wrong */
	c.ints[6] = 16;
	RefRec_WriterInit(&w);
	CHECK(!RefRec_WriteCall(&w, &c), "StretchRaw with 16 bits was written");
	RefRec_WriterFree(&w);
	c.ints[6] = 8;
	c.ints[4] = 0;
	RefRec_WriterInit(&w);
	CHECK(!RefRec_WriteCall(&w, &c), "StretchRaw with 0 columns was written");
	RefRec_WriterFree(&w);

	/* data on a type that has none, and a palette of the wrong size */
	memset(&c, 0, sizeof(c));
	c.type = REC_FILL;
	c.data = raw;
	c.datalen = 4;
	RefRec_WriterInit(&w);
	CHECK(!RefRec_WriteCall(&w, &c), "REC_FILL with data was written");
	RefRec_WriterFree(&w);
	c.type = REC_PALETTE;
	c.datalen = 1024;
	RefRec_WriterInit(&w);
	CHECK(!RefRec_WriteCall(&w, &c), "a 1024-byte palette was written");
	RefRec_WriterFree(&w);

	/* a refused write leaves the writer failed: nothing more is appended */
	RefRec_WriterInit(&w);
	CHECK(!RefRec_WriteCall(&w, &c), "bad palette");
	c.type = REC_ENDFRAME;
	c.datalen = 0;
	CHECK(!RefRec_WriteCall(&w, &c), "a write after a refused one succeeded");
	RefRec_WriterFree(&w);
}

/* Byte layout: little-endian, fixed width, independent of the host. */
static void
TestLayout(void)
{
	static const uint8_t fill[] = {
		17, 0, 0, 0, 20, 0, 0, 0,	/* REC_FILL, pad, length 20 */
		1, 0, 0, 0, 0xfe, 0xff, 0xff, 0xff,	/* x 1, y -2 */
		0x40, 0x01, 0, 0, 0xf0, 0, 0, 0,	/* w 320, h 240 */
		0x78, 0x56, 0x34, 0x12	/* c */
	};
	static const uint8_t sky[] = {
		5, 0, 0, 0, 24, 0, 0, 0,	/* REC_SKY, length 24 */
		0, 0, 0x80, 0x3f,	/* rotate 1.0 */
		0, 0, 0, 0x80,	/* axis -0.0 */
		0, 0, 0x80, 0x7f,	/* +inf */
		1, 0, 0xc0, 0x7f,	/* NaN payload 1 */
		2, 0, 0, 0, 'a', 'b', 0, 0	/* name "ab", padded */
	};
	refrec_writer_t w;
	refrec_call_t c;
	refrec_header_t h, h2;
	refrec_reader_t r;
	uint8_t *copy;

	memset(&c, 0, sizeof(c));
	c.type = REC_FILL;
	c.ints[0] = 1;
	c.ints[1] = -2;
	c.ints[2] = 320;
	c.ints[3] = 240;
	c.ints[4] = 0x12345678;
	RefRec_WriterInit(&w);
	RefRec_WriteCall(&w, &c);
	CHECK(w.ok && w.cursize == sizeof(fill) && memcmp(w.data, fill, sizeof(fill)) == 0,
			"REC_FILL bytes differ from the documented layout");
	RefRec_WriterFree(&w);

	memset(&c, 0, sizeof(c));
	c.type = REC_SKY;
	c.floats[0] = 1.0f;
	c.floats[1] = FromBits(0x80000000u);
	c.floats[2] = FromBits(0x7f800000u);
	c.floats[3] = FromBits(0x7fc00001u);
	strcpy(c.name, "ab");
	RefRec_WriterInit(&w);
	RefRec_WriteCall(&w, &c);
	CHECK(w.ok && w.cursize == sizeof(sky) && memcmp(w.data, sky, sizeof(sky)) == 0,
			"REC_SKY bytes differ from the documented layout");
	RefRec_WriterFree(&w);

	/* nonzero padding after a name is refused */
	copy = malloc(sizeof(sky));
	memcpy(copy, sky, sizeof(sky));
	copy[sizeof(sky) - 1] = 1;
	{
		refrec_type_t type;

		CHECK(!ReadExact(copy, sizeof(sky), &type, &c, NULL, NULL, NULL, NULL),
				"nonzero string padding was accepted");
		/* a NUL inside a name is refused */
		memcpy(copy, sky, sizeof(sky));
		copy[sizeof(sky) - 3] = 0;
		CHECK(!ReadExact(copy, sizeof(sky), &type, &c, NULL, NULL, NULL, NULL),
				"a NUL inside a name was accepted");
		/* an oversized string length is refused */
		memcpy(copy, sky, sizeof(sky));
		SetU32(copy + 24, 0xfffffff0u);
		CHECK(!ReadExact(copy, sizeof(sky), &type, &c, NULL, NULL, NULL, NULL),
				"an oversized string length was accepted");
		SetU32(copy + 24, REFREC_MAX_STRING);
		CHECK(!ReadExact(copy, sizeof(sky), &type, &c, NULL, NULL, NULL, NULL),
				"a string length of REFREC_MAX_STRING was accepted");
		/* unknown record types */
		memcpy(copy, sky, sizeof(sky));
		copy[0] = 0;
		CHECK(!ReadExact(copy, sizeof(sky), &type, &c, NULL, NULL, NULL, NULL),
				"record type 0 was accepted");
		copy[0] = REC_LAST + 1;
		CHECK(!ReadExact(copy, sizeof(sky), &type, &c, NULL, NULL, NULL, NULL),
				"record type REC_LAST + 1 was accepted");
		copy[0] = 0xff;
		copy[1] = 0xff;
		CHECK(!ReadExact(copy, sizeof(sky), &type, &c, NULL, NULL, NULL, NULL),
				"record type 0xffff was accepted");
		/* a record of a type the caller gave no destination for */
		CHECK(!ReadExact(sky, sizeof(sky), &type, NULL, NULL, NULL, NULL, NULL),
				"a call record was read with no call to read it into");
	}
	free(copy);

	/* header: 64 bytes, round trip, refusals */
	memset(&h, 0, sizeof(h));
	memcpy(h.magic, REFREC_MAGIC, 8);
	h.version = REFREC_VERSION;
	h.api_version = API_VERSION;
	h.flags = 0x80000001u;
	h.string_table_offset = 0x11223344u;
	h.frame_index_offset = 0x55667788u;
	h.frame_count = 1000;
	memcpy(h.engine_sha, "0123456789abcdef", 16);
	h.scenario_hash = 2420249005u;
	h.width = 640;
	h.height = 480;
	h.capture_every = 30;
	RefRec_WriterInit(&w);
	CHECK(RefRec_WriteHeader(&w, &h) && w.cursize == REFREC_HEADER_SIZE,
			"header is %u bytes, not %d", w.cursize, REFREC_HEADER_SIZE);

	if (w.ok && w.cursize == REFREC_HEADER_SIZE)
	{
		uint32_t n;

		CHECK(memcmp(w.data, "Q2REFREC\x01\0\0\0\x08\0\0\0", 16) == 0, "header bytes: magic, version, api_version");
		CHECK(w.data[52] == 0x80 && w.data[53] == 0x02 && w.data[54] == 0xe0 && w.data[55] == 0x01,
				"header bytes: width and height");
		copy = malloc(REFREC_HEADER_SIZE);
		memcpy(copy, w.data, REFREC_HEADER_SIZE);
		RefRec_ReaderInit(&r, copy, REFREC_HEADER_SIZE);
		CHECK(RefRec_ReadHeader(&r, &h2) && r.pos == REFREC_HEADER_SIZE, "header read refused");
		CHECK(memcmp(h.magic, h2.magic, 8) == 0, "header magic");
		CHECK(h.version == h2.version, "header version");
		CHECK(h.api_version == h2.api_version, "header api_version");
		CHECK(h.flags == h2.flags, "header flags");
		CHECK(h.string_table_offset == h2.string_table_offset, "header string_table_offset");
		CHECK(h.frame_index_offset == h2.frame_index_offset, "header frame_index_offset");
		CHECK(h.frame_count == h2.frame_count, "header frame_count");
		CHECK(memcmp(h.engine_sha, h2.engine_sha, 16) == 0, "header engine_sha");
		CHECK(h.scenario_hash == h2.scenario_hash, "header scenario_hash");
		CHECK(h.width == h2.width && h.height == h2.height, "header width, height");
		CHECK(h.capture_every == h2.capture_every, "header capture_every");

		for (n = 0; n < REFREC_HEADER_SIZE; n++)
		{
			RefRec_ReaderInit(&r, copy, n);
			CHECK(!RefRec_ReadHeader(&r, &h2), "header truncated to %u bytes was accepted", n);
		}

		copy[0] = 'X';
		RefRec_ReaderInit(&r, copy, REFREC_HEADER_SIZE);
		CHECK(!RefRec_ReadHeader(&r, &h2), "a header with a bad magic was accepted");
		memcpy(copy, w.data, REFREC_HEADER_SIZE);
		copy[8] = 2;
		RefRec_ReaderInit(&r, copy, REFREC_HEADER_SIZE);
		CHECK(!RefRec_ReadHeader(&r, &h2), "a header of version 2 was accepted");
		memcpy(copy, w.data, REFREC_HEADER_SIZE);
		copy[63] = 1;
		RefRec_ReaderInit(&r, copy, REFREC_HEADER_SIZE);
		CHECK(!RefRec_ReadHeader(&r, &h2), "a header with nonzero reserved bytes was accepted");
		free(copy);
	}

	RefRec_WriterFree(&w);
}

static void
FillFrame(refrec_frame_t *f, int nent, int ndl, int npart, qboolean areabits)
{
	int i, j, k = 0;

	memset(f, 0, sizeof(*f));
	f->refdef.x = 3;
	f->refdef.y = -4;
	f->refdef.width = 640;
	f->refdef.height = 480;
	f->refdef.fov_x = 90.0f;
	f->refdef.fov_y = OddFloat(k++);

	for (i = 0; i < 3; i++)
	{
		f->refdef.vieworg[i] = OddFloat(k++);
		f->refdef.viewangles[i] = 1.5f * i - 7.25f;
	}

	for (i = 0; i < 4; i++)
	{
		f->refdef.blend[i] = OddFloat(k++);
	}

	f->refdef.time = 12.345f;
	f->refdef.rdflags = 0x41;
	f->refdef.has_areabits = areabits ? 1 : 0;

	for (i = 0; areabits && i < REFREC_AREABYTES; i++)
	{
		f->refdef.areabits[i] = (uint8_t)(0x80 | i);
	}

	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		for (j = 0; j < 3; j++)
		{
			f->refdef.lightstyles[i].rgb[j] = i * 0.01f + j;
		}

		f->refdef.lightstyles[i].white = OddFloat(i);
	}

	f->refdef.num_entities = nent;
	f->refdef.num_dlights = ndl;
	f->refdef.num_particles = npart;

	for (i = 0; i < nent; i++)
	{
		refrec_entity_t *e = &f->entities[i];

		e->model_id = 1000 + i;
		e->skin_id = i & 1 ? 0 : 2000 + i;

		for (j = 0; j < 3; j++)
		{
			e->angles[j] = i + j * 0.5f;
			e->origin[j] = OddFloat(i + j);
			e->oldorigin[j] = -i - j * 0.25f;
		}

		e->backlerp = 0.75f;
		e->alpha = OddFloat(i);
		e->frame = i * 3;
		e->oldframe = -i;
		e->skinnum = 0x7fffffff - i;
		e->lightstyle = i % 11;
		e->flags = (int32_t)(0x80000000u >> (i % 32));
	}

	for (i = 0; i < ndl; i++)
	{
		for (j = 0; j < 3; j++)
		{
			f->dlights[i].origin[j] = i * 10.0f + j;
			f->dlights[i].color[j] = OddFloat(i + j);
		}

		f->dlights[i].intensity = 200.0f + i;
	}

	for (i = 0; i < npart; i++)
	{
		for (j = 0; j < 3; j++)
		{
			f->particles[i].origin[j] = i * 0.125f - j;
		}

		f->particles[i].color = (i * 37) & 0xff;
		f->particles[i].alpha = OddFloat(i);
	}
}

static void
CompareFrame(const char *what, const refrec_frame_t *a, const refrec_frame_t *b)
{
	const refrec_refdef_t *x = &a->refdef, *y = &b->refdef;
	int i, j, bad;

#define F_EQ(fa, fb, name) CHECK(Bits(fa) == Bits(fb), "%s: %s %08x != %08x", what, name, Bits(fa), Bits(fb))
	CHECK(x->x == y->x && x->y == y->y && x->width == y->width && x->height == y->height,
			"%s: x, y, width, height", what);
	F_EQ(x->fov_x, y->fov_x, "fov_x");
	F_EQ(x->fov_y, y->fov_y, "fov_y");

	for (i = 0; i < 3; i++)
	{
		F_EQ(x->vieworg[i], y->vieworg[i], "vieworg");
		F_EQ(x->viewangles[i], y->viewangles[i], "viewangles");
	}

	for (i = 0; i < 4; i++)
	{
		F_EQ(x->blend[i], y->blend[i], "blend");
	}

	F_EQ(x->time, y->time, "time");
	CHECK(x->rdflags == y->rdflags, "%s: rdflags", what);
	CHECK(x->has_areabits == y->has_areabits, "%s: has_areabits", what);
	CHECK(memcmp(x->areabits, y->areabits, REFREC_AREABYTES) == 0, "%s: areabits", what);

	for (i = 0, bad = 0; i < MAX_LIGHTSTYLES; i++)
	{
		for (j = 0; j < 3; j++)
		{
			bad += Bits(x->lightstyles[i].rgb[j]) != Bits(y->lightstyles[i].rgb[j]);
		}

		bad += Bits(x->lightstyles[i].white) != Bits(y->lightstyles[i].white);
	}

	CHECK(bad == 0, "%s: %d lightstyle fields differ", what, bad);
	CHECK(x->num_entities == y->num_entities && x->num_dlights == y->num_dlights &&
			x->num_particles == y->num_particles, "%s: counts", what);

	for (i = 0, bad = 0; i < x->num_entities && i < y->num_entities; i++)
	{
		const refrec_entity_t *e = &a->entities[i], *g = &b->entities[i];

		bad += e->model_id != g->model_id;
		bad += e->skin_id != g->skin_id;

		for (j = 0; j < 3; j++)
		{
			bad += Bits(e->angles[j]) != Bits(g->angles[j]);
			bad += Bits(e->origin[j]) != Bits(g->origin[j]);
			bad += Bits(e->oldorigin[j]) != Bits(g->oldorigin[j]);
		}

		bad += Bits(e->backlerp) != Bits(g->backlerp);
		bad += Bits(e->alpha) != Bits(g->alpha);
		bad += e->frame != g->frame;
		bad += e->oldframe != g->oldframe;
		bad += e->skinnum != g->skinnum;
		bad += e->lightstyle != g->lightstyle;
		bad += e->flags != g->flags;
	}

	CHECK(bad == 0, "%s: %d entity fields differ", what, bad);

	for (i = 0, bad = 0; i < x->num_dlights && i < y->num_dlights; i++)
	{
		for (j = 0; j < 3; j++)
		{
			bad += Bits(a->dlights[i].origin[j]) != Bits(b->dlights[i].origin[j]);
			bad += Bits(a->dlights[i].color[j]) != Bits(b->dlights[i].color[j]);
		}

		bad += Bits(a->dlights[i].intensity) != Bits(b->dlights[i].intensity);
	}

	CHECK(bad == 0, "%s: %d dlight fields differ", what, bad);

	for (i = 0, bad = 0; i < x->num_particles && i < y->num_particles; i++)
	{
		for (j = 0; j < 3; j++)
		{
			bad += Bits(a->particles[i].origin[j]) != Bits(b->particles[i].origin[j]);
		}

		bad += a->particles[i].color != b->particles[i].color;
		bad += Bits(a->particles[i].alpha) != Bits(b->particles[i].alpha);
	}

	CHECK(bad == 0, "%s: %d particle fields differ", what, bad);
#undef F_EQ
}

/* offset of num_entities inside a REC_RENDERFRAME record */
#define FRAME_COUNTS_OFS (REFREC_REC_SIZE + 18 * 4 + 4 + REFREC_AREABYTES + MAX_LIGHTSTYLES * 16)

static void
RoundTripFrame(const char *what, int nent, int ndl, int npart, qboolean areabits, qboolean refusals)
{
	static refrec_frame_t in, out;
	refrec_writer_t w;
	refrec_type_t type;
	uint32_t expect;
	uint8_t *keep;

	FillFrame(&in, nent, ndl, npart, areabits);
	memset(&out, 0xcc, sizeof(out));
	RefRec_WriterInit(&w);
	CHECK(RefRec_WriteRenderFrame(&w, &in), "%s: write refused", what);
	expect = FRAME_COUNTS_OFS + 12 + nent * REFREC_ENTITY_SIZE +
		ndl * REFREC_DLIGHT_SIZE + npart * REFREC_PARTICLE_SIZE;
	CHECK(w.cursize == expect, "%s: %u bytes, expected %u", what, w.cursize, expect);

	if (w.ok)
	{
		CHECK(ReadExact(w.data, w.cursize, &type, NULL, &out, NULL, NULL, &keep),
				"%s: read refused", what);
		CHECK(type == REC_RENDERFRAME, "%s: type", what);
		CompareFrame(what, &in, &out);
		free(keep);

		if (refusals)
		{
			static const int32_t bad_counts[][3] = {
				{ MAX_ENTITIES + 1, 0, 0 }, { -1, 0, 0 },
				{ 0, MAX_DLIGHTS + 1, 0 }, { 0, -1, 0 },
				{ 0, 0, MAX_PARTICLES + 1 }, { 0, 0, -1 },
				{ 0x7fffffff, 0x7fffffff, 0x7fffffff },
				{ 1, 0, 0 },	/* in range, but one more entity than present */
			};
			uint8_t *copy = malloc(w.cursize);
			size_t i;

			CheckTruncations(what, w.data, w.cursize);
			CheckLengthField(what, w.data, w.cursize);

			for (i = 0; i < sizeof(bad_counts) / sizeof(bad_counts[0]); i++)
			{
				const int32_t *bc = bad_counts[i];

				/* a negative entry is stored as -1, a positive one is added to the real count */
				memcpy(copy, w.data, w.cursize);
				SetU32(copy + FRAME_COUNTS_OFS, bc[0] < 0 ? 0xffffffffu : (uint32_t)nent + (uint32_t)bc[0]);
				SetU32(copy + FRAME_COUNTS_OFS + 4, bc[1] < 0 ? 0xffffffffu : (uint32_t)ndl + (uint32_t)bc[1]);
				SetU32(copy + FRAME_COUNTS_OFS + 8, bc[2] < 0 ? 0xffffffffu : (uint32_t)npart + (uint32_t)bc[2]);

				out.refdef.num_entities = out.refdef.num_dlights = out.refdef.num_particles = 0x55;
				CHECK(!ReadExact(copy, w.cursize, &type, NULL, &out, NULL, NULL, NULL),
						"%s: bad counts #%d were accepted", what, (int)i);
				CHECK(out.refdef.num_entities == 0 && out.refdef.num_dlights == 0 &&
						out.refdef.num_particles == 0,
						"%s: bad counts #%d left %d, %d, %d in the frame", what, (int)i,
						out.refdef.num_entities, out.refdef.num_dlights, out.refdef.num_particles);
			}

			/* has_areabits other than 0 or 1, and areabits bytes without the flag */
			memcpy(copy, w.data, w.cursize);
			SetU32(copy + REFREC_REC_SIZE + 18 * 4, 2);
			CHECK(!ReadExact(copy, w.cursize, &type, NULL, &out, NULL, NULL, NULL),
					"%s: has_areabits 2 was accepted", what);

			if (!areabits)
			{
				memcpy(copy, w.data, w.cursize);
				copy[REFREC_REC_SIZE + 18 * 4 + 4 + 5] = 1;
				CHECK(!ReadExact(copy, w.cursize, &type, NULL, &out, NULL, NULL, NULL),
						"%s: areabits without the flag were accepted", what);
			}

			/* no frame to read into */
			CHECK(!ReadExact(w.data, w.cursize, &type, NULL, NULL, NULL, NULL, NULL),
					"%s: read with no frame was accepted", what);
			free(copy);
		}
	}

	RefRec_WriterFree(&w);

	/* the writer refuses counts out of range */
	in.refdef.num_entities = MAX_ENTITIES + 1;
	RefRec_WriterInit(&w);
	CHECK(!RefRec_WriteRenderFrame(&w, &in), "%s: entity count over the limit was written", what);
	RefRec_WriterFree(&w);
}

/*
 * A frame at the limit with one more element appended and counted, so that
 * the payload length agrees: only the count limit can refuse it, and a
 * reader without one would write past the end of refrec_frame_t's array.
 */
static void
FrameOverLimit(int which, uint32_t elemsize, const char *what)
{
	static refrec_frame_t in, *out;
	refrec_writer_t w;
	refrec_type_t type;
	uint8_t *big;
	uint32_t len, count;

	FillFrame(&in, MAX_ENTITIES, MAX_DLIGHTS, MAX_PARTICLES, true);
	RefRec_WriterInit(&w);
	RefRec_WriteRenderFrame(&w, &in);
	len = w.cursize + elemsize;
	big = calloc(1, len);
	memcpy(big, w.data, w.cursize);
	SetU32(big + 4, len - REFREC_REC_SIZE);
	count = which == 0 ? MAX_ENTITIES : which == 1 ? MAX_DLIGHTS : MAX_PARTICLES;
	SetU32(big + FRAME_COUNTS_OFS + 4 * which, count + 1);

	if (which != 2)
	{
		/* move the later arrays along so the appended bytes sit at the end of this one */
		uint32_t at = FRAME_COUNTS_OFS + 12 + MAX_ENTITIES * REFREC_ENTITY_SIZE +
			(which == 1 ? MAX_DLIGHTS * REFREC_DLIGHT_SIZE : 0);

		memmove(big + at + elemsize, big + at, w.cursize - at);
		memset(big + at, 0, elemsize);
	}

	/* the destination is exactly one refrec_frame_t on the heap, so ASAN sees an overrun */
	out = malloc(sizeof(*out));
	CHECK(!ReadExact(big, len, &type, NULL, out, NULL, NULL, NULL),
			"REC_RENDERFRAME with one too many %s was accepted", what);
	free(out);
	free(big);
	RefRec_WriterFree(&w);
}

static void
TestFrames(void)
{
	RoundTripFrame("REC_RENDERFRAME empty", 0, 0, 0, false, true);
	RoundTripFrame("REC_RENDERFRAME typical", 3, 2, 5, true, true);
	RoundTripFrame("REC_RENDERFRAME full", MAX_ENTITIES, MAX_DLIGHTS, MAX_PARTICLES, true, false);
	FrameOverLimit(0, REFREC_ENTITY_SIZE, "entities");
	FrameOverLimit(1, REFREC_DLIGHT_SIZE, "dlights");
	FrameOverLimit(2, REFREC_PARTICLE_SIZE, "particles");
}

static void
TestCvars(void)
{
	static const char *names[] = { "r_mode", "vid_gamma", "sw_stipplealpha", "gl1_overbrightbits" };
	static const char *values[] = { "-1", "1.200000", "", "2" };
	refrec_writer_t w;
	refrec_reader_t cvars;
	refrec_type_t type;
	char name[REFREC_MAX_STRING], value[REFREC_MAX_STRING];
	uint32_t ncvars, i;
	uint8_t *keep, *copy;

	RefRec_WriterInit(&w);
	CHECK(RefRec_WriteCvars(&w, 4, names, values), "REC_CVARS write refused");

	if (!w.ok)
	{
		RefRec_WriterFree(&w);
		return;
	}

	CHECK(ReadExact(w.data, w.cursize, &type, NULL, NULL, &cvars, &ncvars, &keep),
			"REC_CVARS read refused");
	CHECK(type == REC_CVARS && ncvars == 4, "REC_CVARS type %d count %u", type, ncvars);

	for (i = 0; i < ncvars && i < 4; i++)
	{
		CHECK(RefRec_NextCvar(&cvars, name, value), "REC_CVARS pair %u", i);
		CHECK(strcmp(name, names[i]) == 0 && strcmp(value, values[i]) == 0,
				"REC_CVARS pair %u: '%s' '%s'", i, name, value);
	}

	CHECK(!RefRec_NextCvar(&cvars, name, value), "REC_CVARS read past the last pair");
	free(keep);

	CheckTruncations("REC_CVARS", w.data, w.cursize);
	CheckLengthField("REC_CVARS", w.data, w.cursize);

	/* oversized counts */
	copy = malloc(w.cursize);
	memcpy(copy, w.data, w.cursize);
	SetU32(copy + REFREC_REC_SIZE, 0xffffffffu);
	CHECK(!ReadExact(copy, w.cursize, &type, NULL, NULL, &cvars, &ncvars, NULL),
			"REC_CVARS count 0xffffffff was accepted");
	SetU32(copy + REFREC_REC_SIZE, 5);
	CHECK(!ReadExact(copy, w.cursize, &type, NULL, NULL, &cvars, &ncvars, NULL),
			"REC_CVARS count one over the pairs present was accepted");
	SetU32(copy + REFREC_REC_SIZE, 3);
	CHECK(!ReadExact(copy, w.cursize, &type, NULL, NULL, &cvars, &ncvars, NULL),
			"REC_CVARS count one under the pairs present was accepted");
	free(copy);

	/* an empty cvar name, otherwise well formed */
	{
		static const uint8_t empty_name[] = {
			REC_CVARS, 0, 0, 0, 16, 0, 0, 0,
			1, 0, 0, 0,	/* one pair */
			0, 0, 0, 0,	/* name "" */
			1, 0, 0, 0, 'b', 0, 0, 0	/* value "b" */
		};

		CHECK(!ReadExact(empty_name, sizeof(empty_name), &type, NULL, NULL, &cvars, &ncvars, NULL),
				"REC_CVARS empty name was accepted");
	}
	RefRec_WriterFree(&w);

	/* the writer refuses an empty name and a count over the limit */
	{
		const char *empty[] = { "" }, *one[] = { "1" };

		RefRec_WriterInit(&w);
		CHECK(!RefRec_WriteCvars(&w, 1, empty, one), "REC_CVARS empty name was written");
		RefRec_WriterFree(&w);
		RefRec_WriterInit(&w);
		CHECK(!RefRec_WriteCvars(&w, REFREC_MAX_CVARS + 1, empty, one), "REC_CVARS count over the limit was written");
		RefRec_WriterFree(&w);
	}
}

/* A stream of records, read back in order. */
static void
TestStream(void)
{
	static refrec_frame_t in, out;
	refrec_writer_t w;
	refrec_reader_t r, cvars;
	refrec_call_t c, got;
	refrec_type_t type;
	uint32_t ncvars;
	const char *names[] = { "r_lerpmodels" }, *values[] = { "1" };
	uint8_t *copy;

	RefRec_WriterInit(&w);
	memset(&c, 0, sizeof(c));
	c.type = REC_BEGINFRAME;
	c.floats[0] = 0.0f;
	RefRec_WriteCall(&w, &c);
	FillFrame(&in, 2, 1, 3, false);
	RefRec_WriteRenderFrame(&w, &in);
	RefRec_WriteCvars(&w, 1, names, values);
	memset(&c, 0, sizeof(c));
	c.type = REC_PICSCALED;
	c.ints[0] = 10;
	c.ints[1] = 20;
	c.floats[0] = 2.0f;
	strcpy(c.name, "a");	/* a 1-byte name leaves 3 bytes of padding */
	RefRec_WriteCall(&w, &c);
	memset(&c, 0, sizeof(c));
	c.type = REC_ENDFRAME;
	RefRec_WriteCall(&w, &c);
	CHECK(w.ok, "stream write refused");

	copy = malloc(w.cursize);
	memcpy(copy, w.data, w.cursize);
	RefRec_ReaderInit(&r, copy, w.cursize);
	CHECK(RefRec_ReadRecord(&r, &type, &got, &out, &cvars, &ncvars) && type == REC_BEGINFRAME, "stream 1");
	CHECK(RefRec_ReadRecord(&r, &type, &got, &out, &cvars, &ncvars) && type == REC_RENDERFRAME, "stream 2");
	CompareFrame("stream frame", &in, &out);
	CHECK(RefRec_ReadRecord(&r, &type, &got, &out, &cvars, &ncvars) && type == REC_CVARS && ncvars == 1, "stream 3");
	CHECK(RefRec_ReadRecord(&r, &type, &got, &out, &cvars, &ncvars) && type == REC_PICSCALED &&
			got.ints[1] == 20 && strcmp(got.name, "a") == 0, "stream 4");
	CHECK(RefRec_ReadRecord(&r, &type, &got, &out, &cvars, &ncvars) && type == REC_ENDFRAME, "stream 5");
	CHECK(r.pos == w.cursize, "stream: %u of %u bytes read", r.pos, w.cursize);
	CHECK(!RefRec_ReadRecord(&r, &type, &got, &out, &cvars, &ncvars), "stream: read past the end succeeded");
	CHECK(!RefRec_ReadRecord(&r, &type, &got, &out, &cvars, &ncvars), "stream: a failed reader recovered");
	free(copy);
	RefRec_WriterFree(&w);

	/* an empty and a NULL buffer */
	RefRec_ReaderInit(&r, NULL, 100);
	CHECK(!RefRec_ReadRecord(&r, &type, &got, &out, &cvars, &ncvars), "a NULL buffer was read");
}

/* Recording is off in this build, and the hooks are inert. */
static void
TestRecorderOff(void)
{
	refrec_header_t h;
	int32_t ints[2] = { 1, 2 };

	CHECK(!RefRec_Recording(), "recording is on");
	RefRec_StartRecord("x.q2r", 640, 480, 30);
	CHECK(!RefRec_Recording(), "RefRec_StartRecord switched recording on");
	RefRec_Registration(REC_MODEL, "m", NULL);
	RefRec_RenderFrame(NULL);
	RefRec_Draw(REC_FILL, ints, 2, NULL, 0, NULL, NULL, 0);
	RefRec_Control(REC_ENDFRAME, 0);
	RefRec_StopRecord();
	CHECK(!RefRec_Recording(), "recording is on after stop");
	CHECK(!RefRec_OpenReplay("x.q2r", &h), "replay opened");
	CHECK(!RefRec_ReplayNextFrame(), "replay produced a frame");
	RefRec_CloseReplay();
}

int
main(void)
{
	TestCalls();
	TestPaletteExact();
	TestWriteRefusals();
	TestLayout();
	TestFrames();
	TestCvars();
	TestStream();
	TestRecorderOff();

	printf("refrecord: %d checks, %d failed\n", checks, failures);

	return failures ? 1 : 0;
}
