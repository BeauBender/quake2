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
 * builds this with ASAN and UBSAN when the compiler has them). Then records
 * trace files into the scratch directory it is given and reads them back
 * whole: the handle map, the refdef_t deep copy, the string table, the
 * frame index, the header patch, and the failure paths (a recorder limit,
 * an injected write error, oversized raw images, a full handle map), and
 * sweeps a recorder limit and an injected write fault across every record
 * of runs that warn, so a recording that ends inside a call is proven
 * safe (REFREC_FULL_SWEEP=1 takes every byte). Prints one line per
 * failure and "refrecord: N checks, 0 failed" at the end; exits non-zero
 * on any failure.
 *
 *   refrecord-test <scratch dir>              the tests
 *   refrecord-test --read <trace> [--names]   reads a trace end to end and
 *       prints counts only (records per type, frames, captures, strings);
 *       with --names the string table, one "kind name" per line; every
 *       name and engine id byte outside 0x20..0x7e is printed as \xNN
 *
 * =======================================================================
 */

#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>

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
	RoundTripCall(REC_CAPTURE, 1, 0, false, NULL, NULL, 0, "REC_CAPTURE");
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

	/* one column or row over the limit, with a payload of exactly the size it implies */
	{
		static uint8_t wide[REFREC_MAX_RAW_DIM + 1];

		memset(&c, 0, sizeof(c));
		c.type = REC_STRETCHRAW;
		c.ints[4] = REFREC_MAX_RAW_DIM + 1;
		c.ints[5] = 1;
		c.ints[6] = 8;
		c.data = wide;
		c.datalen = REFREC_MAX_RAW_DIM + 1;
		RefRec_WriterInit(&w);
		CHECK(!RefRec_WriteCall(&w, &c), "StretchRaw with 4097 columns was written");
		RefRec_WriterFree(&w);
		c.ints[4] = 1;
		c.ints[5] = REFREC_MAX_RAW_DIM + 1;
		RefRec_WriterInit(&w);
		CHECK(!RefRec_WriteCall(&w, &c), "StretchRaw with 4097 rows was written");
		RefRec_WriterFree(&w);
	}

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

/*
 * Byte-layout pin for a whole REC_RENDERFRAME and the file header. The
 * expected bytes are built in the order the format puts them on disk, one
 * distinct value per field, and the frame fields take their values from the
 * same calls. A field order changed consistently in a writer and its reader
 * therefore no longer matches.
 */
static uint8_t pin_bytes[REFREC_REC_SIZE + 4 * 1100];
static uint32_t pin_len;
static uint32_t pin_seq;

static uint32_t
PinWord(uint32_t v)
{
	SetU32(pin_bytes + pin_len, v);
	pin_len += 4;

	return v;
}

static int32_t
PinI(void)
{
	return (int32_t)PinWord(0x01000000u + ++pin_seq * 0x101u);
}

static float
PinF(void)
{
	return FromBits(PinWord(0x3f000000u + ++pin_seq * 0x10101u));
}

static void
TestFramePin(void)
{
	static refrec_frame_t f, back;
	refrec_writer_t w;
	refrec_refdef_t *rd = &f.refdef;
	refrec_entity_t *e = &f.entities[0];
	refrec_header_t h;
	int i, j;

	memset(&f, 0, sizeof(f));
	pin_len = REFREC_REC_SIZE;	/* the record header is filled in last */
	pin_seq = 0;

	rd->x = PinI();
	rd->y = PinI();
	rd->width = PinI();
	rd->height = PinI();
	rd->fov_x = PinF();
	rd->fov_y = PinF();

	for (i = 0; i < 3; i++)
	{
		rd->vieworg[i] = PinF();
	}

	for (i = 0; i < 3; i++)
	{
		rd->viewangles[i] = PinF();
	}

	for (i = 0; i < 4; i++)
	{
		rd->blend[i] = PinF();
	}

	rd->time = PinF();
	rd->rdflags = PinI();
	rd->has_areabits = 1;
	PinWord(1);

	for (i = 0; i < REFREC_AREABYTES; i++)
	{
		rd->areabits[i] = (uint8_t)(0xa0 + i);
		pin_bytes[pin_len++] = rd->areabits[i];
	}

	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		for (j = 0; j < 3; j++)
		{
			rd->lightstyles[i].rgb[j] = PinF();
		}

		rd->lightstyles[i].white = PinF();
	}

	rd->num_entities = 1;
	rd->num_dlights = 1;
	rd->num_particles = 1;
	PinWord(1);
	PinWord(1);
	PinWord(1);

	e->model_id = PinWord(0x4d4f4400u);
	e->skin_id = PinWord(0x534b4e00u);

	for (j = 0; j < 3; j++)
	{
		e->angles[j] = PinF();
	}

	for (j = 0; j < 3; j++)
	{
		e->origin[j] = PinF();
	}

	for (j = 0; j < 3; j++)
	{
		e->oldorigin[j] = PinF();
	}

	e->backlerp = PinF();
	e->alpha = PinF();
	e->frame = PinI();
	e->oldframe = PinI();
	e->skinnum = PinI();
	e->lightstyle = PinI();
	e->flags = PinI();

	for (j = 0; j < 3; j++)
	{
		f.dlights[0].origin[j] = PinF();
	}

	for (j = 0; j < 3; j++)
	{
		f.dlights[0].color[j] = PinF();
	}

	f.dlights[0].intensity = PinF();

	for (j = 0; j < 3; j++)
	{
		f.particles[0].origin[j] = PinF();
	}

	f.particles[0].color = PinI();
	f.particles[0].alpha = PinF();

	pin_bytes[0] = (uint8_t)REC_RENDERFRAME;
	pin_bytes[1] = 0;
	pin_bytes[2] = 0;
	pin_bytes[3] = 0;
	SetU32(pin_bytes + 4, pin_len - REFREC_REC_SIZE);
	CHECK(pin_len <= sizeof(pin_bytes), "frame pin: buffer too small");

	RefRec_WriterInit(&w);
	CHECK(RefRec_WriteRenderFrame(&w, &f), "frame pin: write refused");
	CHECK(w.cursize == pin_len, "frame pin: %u bytes, expected %u", w.cursize, pin_len);

	if (w.ok && w.cursize == pin_len)
	{
		refrec_type_t type;
		uint8_t *keep;

		for (i = 0; i < (int)pin_len; i++)
		{
			if (w.data[i] != pin_bytes[i])
			{
				CHECK(0, "frame pin: byte %d is %02x, expected %02x", i, w.data[i], pin_bytes[i]);
				break;
			}
		}

		/* and the reader must take exactly these bytes back */
		CHECK(ReadExact(pin_bytes, pin_len, &type, NULL, &back, NULL, NULL, &keep), "frame pin: read refused");
		CompareFrame("frame pin", &f, &back);
		free(keep);
	}

	RefRec_WriterFree(&w);

	/* the header: every field distinct, all 64 bytes */
	{
		static const uint8_t hdr[REFREC_HEADER_SIZE] = {
			'Q', '2', 'R', 'E', 'F', 'R', 'E', 'C',
			1, 0, 0, 0,	/* version */
			8, 0, 0, 0,	/* api_version */
			0x11, 0x12, 0x13, 0x14,	/* flags */
			0x21, 0x22, 0x23, 0x24,	/* string_table_offset */
			0x31, 0x32, 0x33, 0x34,	/* frame_index_offset */
			0x41, 0x42, 0x43, 0x44,	/* frame_count */
			'0', '1', '2', '3', '4', '5', '6', '7',	/* engine_sha */
			'8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
			0x51, 0x52, 0x53, 0x54,	/* scenario_hash */
			0x61, 0x62,	/* width */
			0x71, 0x72,	/* height */
			0x81, 0x82, 0x83, 0x84,	/* capture_every */
			0, 0, 0, 0	/* reserved */
		};
		refrec_reader_t r;
		refrec_header_t h2;
		uint8_t *copy;

		memset(&h, 0, sizeof(h));
		memcpy(h.magic, "Q2REFREC", 8);
		h.version = 1;
		h.api_version = 8;
		h.flags = 0x14131211u;
		h.string_table_offset = 0x24232221u;
		h.frame_index_offset = 0x34333231u;
		h.frame_count = 0x44434241u;
		memcpy(h.engine_sha, "0123456789abcdef", 16);
		h.scenario_hash = 0x54535251u;
		h.width = 0x6261;
		h.height = 0x7271;
		h.capture_every = 0x84838281u;
		RefRec_WriterInit(&w);
		CHECK(RefRec_WriteHeader(&w, &h) && w.cursize == REFREC_HEADER_SIZE, "header pin: write");
		CHECK(w.cursize == REFREC_HEADER_SIZE && memcmp(w.data, hdr, REFREC_HEADER_SIZE) == 0,
				"header pin: the 64 bytes differ");
		RefRec_WriterFree(&w);

		/* the reader takes the pinned bytes back field by field */
		copy = malloc(REFREC_HEADER_SIZE);
		memcpy(copy, hdr, REFREC_HEADER_SIZE);
		RefRec_ReaderInit(&r, copy, REFREC_HEADER_SIZE);
		memset(&h2, 0, sizeof(h2));
		CHECK(RefRec_ReadHeader(&r, &h2), "header pin: read refused");
		CHECK(h2.flags == h.flags && h2.string_table_offset == h.string_table_offset &&
				h2.frame_index_offset == h.frame_index_offset && h2.frame_count == h.frame_count &&
				h2.scenario_hash == h.scenario_hash && h2.width == h.width &&
				h2.height == h.height && h2.capture_every == h.capture_every &&
				memcmp(h2.engine_sha, h.engine_sha, 16) == 0, "header pin: read fields differ");
		free(copy);
	}
}

/* has_areabits 0 with stale areabits[] in the input: written as zeros, read back as zeros */
static void
TestStaleAreabits(void)
{
	static refrec_frame_t in, out;
	refrec_writer_t w;
	refrec_type_t type;
	uint8_t *keep;
	int i;

	FillFrame(&in, 0, 0, 0, false);

	for (i = 0; i < REFREC_AREABYTES; i++)
	{
		in.refdef.areabits[i] = (uint8_t)(0x80 | i);
	}

	memset(&out, 0xcc, sizeof(out));
	RefRec_WriterInit(&w);
	CHECK(RefRec_WriteRenderFrame(&w, &in), "stale areabits: write refused");
	CHECK(ReadExact(w.data, w.cursize, &type, NULL, &out, NULL, NULL, &keep), "stale areabits: read refused");
	CHECK(out.refdef.has_areabits == 0, "stale areabits: flag set");

	for (i = 0; i < REFREC_AREABYTES; i++)
	{
		CHECK(out.refdef.areabits[i] == 0, "stale areabits: byte %d is %02x", i, out.refdef.areabits[i]);
	}

	free(keep);
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

/* Without RefRec_StartRecord the hooks are inert and write nothing. */
static void
TestRecorderOff(void)
{
	refrec_header_t h;
	int32_t ints[2] = { 1, 2 };

	CHECK(!RefRec_Recording(), "recording is on");
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

/* ---- trace files: the recorder end to end, and the --read tool ---- */

/* what the print function was handed */
static int prints;
static char last_print[512];
static char print_log[4096];	/* every print since Replay began, cut at its size */

static void
TestPrint(const char *fmt, ...)
{
	va_list ap;

	prints++;
	va_start(ap, fmt);
	vsnprintf(last_print, sizeof(last_print), fmt, ap);
	va_end(ap);

	if (strlen(print_log) + strlen(last_print) < sizeof(print_log))
	{
		strcat(print_log, last_print);
	}
}

#define MAX_TEST_STRINGS	64

typedef struct {
	refrec_header_t h;
	uint32_t size;
	uint32_t types[REC_LAST + 1];
	uint32_t warns[16];
	uint32_t warn_at[16], warn_rec[16];	/* offset and ordinal of the first REC_WARN of each code */
	uint32_t long_start[64], long_end[64], nlong;	/* the first records over 48 bytes */
	uint32_t frames;	/* complete frames found walking the records */
	uint32_t strings, kinds[REFREC_KIND_LAST + 1];
	uint32_t captures, capture_next;	/* REC_CAPTURE numbers must run 0, 1, 2, ... */
	/* the first entries of the string table */
	uint32_t skind[MAX_TEST_STRINGS], sid[MAX_TEST_STRINGS];
	char sname[MAX_TEST_STRINGS][MAX_QPATH];
	char err[128];
} tracefile_t;

/*
 * Writes up to max bytes of s, stopping at a NUL, with every byte outside
 * 0x20..0x7e and the backslash itself as \xNN: names and the engine id
 * come from the game, a server or a demo, and must not reach a terminal
 * raw.
 */
static void
PrintEscaped(FILE *out, const char *s, size_t max)
{
	size_t i;

	for (i = 0; i < max && s[i]; i++)
	{
		unsigned char ch = (unsigned char)s[i];

		if (ch < 0x20 || ch > 0x7e || ch == '\\')
		{
			fprintf(out, "\\x%02x", ch);
		}
		else
		{
			fputc(ch, out);
		}
	}
}

typedef void (*tracefile_cb_t)(refrec_type_t type, const refrec_call_t *call,
		const refrec_frame_t *frame, void *ctx);

/*
 * Reads a whole trace with the refrecord.c readers and checks its
 * structure: the header, every record up to the string table, the string
 * table, and a frame index that names each complete frame's REC_BEGINFRAME
 * in order. Calls cb for every record. False with t->err set on the first
 * problem.
 */
static qboolean
ReadTrace(const char *path, tracefile_t *t, tracefile_cb_t cb, void *ctx, FILE *names)
{
	static refrec_frame_t frame;
	static refrec_call_t call;
	refrec_reader_t r, rec, cvars;
	refrec_type_t type;
	uint32_t ncvars, count, i, kind, id, fi, start = 0, *starts = NULL, nstarts = 0, nrec = 0;
	uint64_t off;
	qboolean open = false, ok = false;
	char name[MAX_QPATH];
	uint8_t *buf = NULL;
	long len;
	FILE *f;

	memset(t, 0, sizeof(*t));
	f = fopen(path, "rb");

	if (!f)
	{
		snprintf(t->err, sizeof(t->err), "cannot open");
		return false;
	}

	if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || len >= 0xffffffffL ||
		fseek(f, 0, SEEK_SET) != 0)
	{
		snprintf(t->err, sizeof(t->err), "cannot size, or 4 GiB or more");
		fclose(f);
		return false;
	}

	t->size = (uint32_t)len;
	buf = malloc(t->size ? t->size : 1);

	if (!buf || fread(buf, 1, t->size, f) != t->size)
	{
		snprintf(t->err, sizeof(t->err), "cannot read");
		goto done;
	}

	RefRec_ReaderInit(&r, buf, t->size);

	if (!RefRec_ReadHeader(&r, &t->h))
	{
		snprintf(t->err, sizeof(t->err), "bad header");
		goto done;
	}

	if (!RefRec_CheckLayout(&t->h, t->size, NULL))
	{
		snprintf(t->err, sizeof(t->err), "unfinished trace or bad offsets");
		goto done;
	}

	starts = malloc(sizeof(*starts) * (t->h.frame_count + 1));

	if (!starts)
	{
		snprintf(t->err, sizeof(t->err), "out of memory");
		goto done;
	}

	/* the records */
	RefRec_ReaderInit(&rec, buf, t->h.string_table_offset);
	rec.pos = REFREC_HEADER_SIZE;

	while (rec.pos < rec.size)
	{
		uint32_t at = rec.pos;

		if (!RefRec_ReadRecord(&rec, &type, &call, &frame, &cvars, &ncvars))
		{
			snprintf(t->err, sizeof(t->err), "bad record at offset %u", at);
			goto done;
		}

		t->types[type]++;

		if (type == REC_WARN && call.ints[0] >= 0 && call.ints[0] < 16)
		{
			if (!t->warns[call.ints[0]]++)
			{
				t->warn_at[call.ints[0]] = at;
				t->warn_rec[call.ints[0]] = nrec;
			}
		}

		nrec++;

		if (rec.pos - at > 48 && t->nlong < 64)
		{
			t->long_start[t->nlong] = at;
			t->long_end[t->nlong++] = rec.pos;
		}

		if (type == REC_CAPTURE)
		{
			if ((uint32_t)call.ints[0] != t->capture_next)
			{
				snprintf(t->err, sizeof(t->err), "capture %d out of order", call.ints[0]);
				goto done;
			}

			t->capture_next++;
			t->captures++;
		}

		if (type == REC_BEGINFRAME)
		{
			start = at;
			open = true;
		}
		else if (type == REC_ENDFRAME && open)
		{
			if (nstarts < t->h.frame_count)
			{
				starts[nstarts] = start;
			}

			nstarts++;
			open = false;
		}

		if (cb)
		{
			cb(type, &call, &frame, ctx);
		}
	}

	t->frames = nstarts;

	if (nstarts != t->h.frame_count)
	{
		snprintf(t->err, sizeof(t->err), "%u frames in the records, %u in the header", nstarts, t->h.frame_count);
		goto done;
	}

	/* the string table */
	RefRec_ReaderInit(&rec, buf + t->h.string_table_offset,
			t->h.frame_index_offset - t->h.string_table_offset);

	if (!RefRec_ReadStringTable(&rec, &count))
	{
		snprintf(t->err, sizeof(t->err), "bad string table count");
		goto done;
	}

	for (i = 0; i < count; i++)
	{
		if (!RefRec_NextString(&rec, &kind, &id, name) || id != i + 1)
		{
			snprintf(t->err, sizeof(t->err), "bad string table entry %u", i);
			goto done;
		}

		t->kinds[kind]++;

		if (i < MAX_TEST_STRINGS)
		{
			t->skind[i] = kind;
			t->sid[i] = id;
			memcpy(t->sname[i], name, MAX_QPATH);
		}

		if (names)
		{
			fprintf(names, "%u ", kind);
			PrintEscaped(names, name, MAX_QPATH);
			fputc('\n', names);
		}
	}

	t->strings = count;

	if (rec.pos != rec.size)
	{
		snprintf(t->err, sizeof(t->err), "bytes between the string table and the frame index");
		goto done;
	}

	/* the frame index */
	RefRec_ReaderInit(&rec, buf + t->h.frame_index_offset, t->size - t->h.frame_index_offset);

	for (i = 0; i < t->h.frame_count; i++)
	{
		if (!RefRec_NextFrameIndex(&rec, &fi, &off) || !RefRec_CheckLayout(&t->h, t->size, &off) ||
			fi != i || off != starts[i])
		{
			snprintf(t->err, sizeof(t->err), "frame index entry %u does not name frame %u", i, i);
			goto done;
		}
	}

	ok = true;

done:
	free(starts);
	free(buf);
	fclose(f);

	return ok;
}

static const char *const type_names[REC_LAST + 1] = {
	"", "BEGINREG", "MODEL", "SKIN", "PIC", "SKY", "ENDREG",
	"BEGINFRAME", "RENDERFRAME", "ENDWORLD", "ENDFRAME",
	"PICSIZE", "PICSCALED", "PICSCALEDCOL", "STRETCHPIC",
	"CHARSCALED", "TILECLEAR", "FILL", "FADESCREEN", "STRETCHRAW",
	"PALETTE", "CVARS", "WARN", "CAPTURE"
};

/*
 * --read: counts only (and with --names the string table, kind and name
 * per line), to out. Names and the engine id are printed escaped.
 */
static int
ReadMode(const char *path, qboolean names, FILE *out)
{
	tracefile_t t;
	int i;

	if (!ReadTrace(path, &t, NULL, NULL, names ? out : NULL))
	{
		fprintf(out, "trace: refused: %s\n", t.err);
		return 1;
	}

	if (names)
	{
		return 0;
	}

	fprintf(out, "trace: %u bytes, %u frames, %u captures, flags %u, %ux%u, capture every %u, api %u, run ",
			t.size, t.h.frame_count, t.captures, t.h.flags, t.h.width, t.h.height, t.h.capture_every,
			t.h.api_version);
	PrintEscaped(out, t.h.engine_sha, sizeof(t.h.engine_sha));
	fprintf(out, "-%u\n", t.h.scenario_hash);
	fprintf(out, "strings: %u (models %u, skins %u, pics %u, skies %u, maps %u)\n", t.strings,
			t.kinds[REFREC_KIND_MODEL], t.kinds[REFREC_KIND_SKIN], t.kinds[REFREC_KIND_PIC],
			t.kinds[REFREC_KIND_SKY], t.kinds[REFREC_KIND_MAP]);
	fprintf(out, "records:");

	for (i = REC_FIRST; i <= REC_LAST; i++)
	{
		fprintf(out, " %s=%u", type_names[i], t.types[i]);
	}

	fprintf(out, "\nwarnings:");

	for (i = 1; i < 16; i++)
	{
		if (t.warns[i])
		{
			fprintf(out, " %d=%u", i, t.warns[i]);
		}
	}

	fprintf(out, "\n");

	return 0;
}

static char test_dir[1024];

/* <test_dir>/<sub>/trace.q2r, its directory made */
static const char *
TracePath(const char *sub)
{
	static char path[1200];
	char dir[1100];

	snprintf(dir, sizeof(dir), "%s/%s", test_dir, sub);
	mkdir(dir, 0700);
	snprintf(path, sizeof(path), "%s/trace.q2r", dir);
	remove(path);

	return path;
}

/* fake renderer handles: only their addresses matter */
static char handle_a, handle_b, handle_c, handle_unknown;

typedef struct {
	uint32_t model_ids[8], nmodels;
	refrec_entity_t ents[8];
	uint32_t nents, nframes;
	int32_t num_counts[3];
	uint8_t areaflag[2];
	uint32_t raws, rawlen, rawsum;
	char draw_names[4][32];
	uint32_t ndraw;
	float colf[4];
	particle_t p0;
	lightstyle_t ls7;
} handles_ctx_t;

static void
HandlesCb(refrec_type_t type, const refrec_call_t *c, const refrec_frame_t *f, void *vctx)
{
	handles_ctx_t *x = vctx;
	int i;

	if (type == REC_MODEL && x->nmodels < 8)
	{
		x->model_ids[x->nmodels++] = c->id;
	}

	if (type == REC_RENDERFRAME)
	{
		if (x->nframes < 2)
		{
			x->areaflag[x->nframes] = f->refdef.has_areabits;
		}

		x->nframes++;
		x->num_counts[0] = f->refdef.num_entities;
		x->num_counts[1] = f->refdef.num_dlights;
		x->num_counts[2] = f->refdef.num_particles;
		x->ls7 = f->refdef.lightstyles[7];

		if (f->refdef.num_particles)
		{
			x->p0 = f->particles[0];
		}

		for (i = 0; i < f->refdef.num_entities && x->nents < 8; i++)
		{
			x->ents[x->nents++] = f->entities[i];
		}
	}

	if (type == REC_STRETCHRAW)
	{
		x->raws++;
		x->rawlen = c->datalen;

		for (i = 0; i < (int)c->datalen; i++)
		{
			x->rawsum += c->data[i];
		}
	}

	if ((type == REC_STRETCHPIC || type == REC_PICSCALEDCOL) && x->ndraw < 4)
	{
		snprintf(x->draw_names[x->ndraw++], 32, "%.31s", c->name);
	}

	if (type == REC_PICSCALEDCOL)
	{
		memcpy(x->colf, c->floats, sizeof(x->colf));
	}
}

/* The handle map: ids are stable, NULL is 0, an unknown pointer is 0 plus REC_WARN. */
static void
TestRecordHandles(void)
{
	static lightstyle_t ls[MAX_LIGHTSTYLES];
	entity_t ents[3];
	refdef_t fd;
	handles_ctx_t x;
	tracefile_t t;
	const char *path = TracePath("abcdef123456-d77-1234567890-r3");
	const int32_t fill[5] = { 1, 2, 3, 4, 5 }, cap0 = 0, cap1 = 1;
	const float sky[4] = { 1.0f, 0, 0, 1.0f };

	memset(ents, 0, sizeof(ents));
	memset(&fd, 0, sizeof(fd));
	memset(&x, 0, sizeof(x));
	prints = 0;

	RefRec_StartRecord(path, 640, 480, 30);
	CHECK(RefRec_Recording(), "handles: recording did not start");

	RefRec_Registration(REC_BEGINREG, "maps/base1.bsp", NULL);	/* id 1, map */
	RefRec_Registration(REC_MODEL, "models/a.md2", &handle_a);	/* id 2 */
	RefRec_Registration(REC_MODEL, "models/a.md2", &handle_a);	/* id 2 again */
	RefRec_Registration(REC_SKIN, "skins/b.pcx", &handle_b);	/* id 3 */
	RefRec_Registration(REC_PIC, "c", &handle_c);	/* id 4 */
	RefRec_Registration(REC_MODEL, "models/missing.md2", NULL);	/* id 0, no entry */
	RefRec_Draw(REC_SKY, NULL, 0, sky, 4, "unit1_", NULL, 0);	/* id 5, sky */
	RefRec_Registration(REC_ENDREG, NULL, NULL);

	RefRec_Control(REC_BEGINFRAME, 0);
	ents[0].model = (struct model_s *)&handle_a;	/* skin NULL: inline */
	ents[1].model = (struct model_s *)&handle_unknown;
	ents[1].skin = (struct image_s *)&handle_b;
	ents[2].skin = (struct image_s *)&handle_c;	/* a pic used as a skin */
	ents[2].frame = 7;
	fd.num_entities = 3;
	fd.entities = ents;
	fd.lightstyles = ls;
	RefRec_RenderFrame(&fd);
	RefRec_Control(REC_CVARS, 0);
	RefRec_Draw(REC_FILL, fill, 5, NULL, 0, NULL, NULL, 0);
	RefRec_Draw(REC_CAPTURE, &cap0, 1, NULL, 0, NULL, NULL, 0);
	RefRec_Control(REC_ENDFRAME, 0);

	/* the renderer reused handle_a's memory for another model: a new id */
	RefRec_Registration(REC_MODEL, "models/other.md2", &handle_a);	/* id 6 */
	RefRec_Control(REC_BEGINFRAME, 0);
	RefRec_Draw(REC_CAPTURE, &cap1, 1, NULL, 0, NULL, NULL, 0);
	RefRec_Control(REC_ENDFRAME, 0);
	RefRec_StopRecord();
	CHECK(!RefRec_Recording(), "handles: still recording after stop");

	if (!ReadTrace(path, &t, HandlesCb, &x, NULL))
	{
		CHECK(0, "handles: trace refused: %s", t.err);
		return;
	}

	CHECK(t.h.frame_count == 2 && t.frames == 2, "handles: %u frames", t.h.frame_count);
	CHECK(t.captures == 2, "handles: %u captures", t.captures);
	CHECK(t.h.api_version == API_VERSION && t.h.width == 640 && t.h.height == 480 &&
			t.h.capture_every == 30 && t.h.flags == 0, "handles: header fields");
	CHECK(memcmp(t.h.engine_sha, "abcdef123456-d77", 16) == 0 && t.h.scenario_hash == 1234567890u,
			"handles: run id not taken from the directory name (sha '%.16s' hash %u)",
			t.h.engine_sha, t.h.scenario_hash);
	CHECK(x.nmodels == 4 && x.model_ids[0] == 2 && x.model_ids[1] == 2 && x.model_ids[2] == 0 &&
			x.model_ids[3] == 6, "handles: REC_MODEL ids %u %u %u %u",
			x.model_ids[0], x.model_ids[1], x.model_ids[2], x.model_ids[3]);
	CHECK(x.nents == 3, "handles: %u entities", x.nents);
	CHECK(x.ents[0].model_id == 2 && x.ents[0].skin_id == 0, "handles: entity 0 ids %u %u",
			x.ents[0].model_id, x.ents[0].skin_id);
	CHECK(x.ents[1].model_id == 0 && x.ents[1].skin_id == 3, "handles: entity 1 ids %u %u",
			x.ents[1].model_id, x.ents[1].skin_id);
	CHECK(x.ents[2].model_id == 0 && x.ents[2].skin_id == 4 && x.ents[2].frame == 7,
			"handles: entity 2 ids %u %u", x.ents[2].model_id, x.ents[2].skin_id);
	CHECK(t.warns[REFREC_WARN_UNKNOWN_HANDLE] == 1 && t.types[REC_WARN] == 1,
			"handles: %u unknown-handle warnings, %u in all", t.warns[REFREC_WARN_UNKNOWN_HANDLE],
			t.types[REC_WARN]);
	CHECK(t.strings == 6, "handles: %u strings", t.strings);

	if (t.strings == 6)
	{
		static const uint32_t kinds[6] = {
			REFREC_KIND_MAP, REFREC_KIND_MODEL, REFREC_KIND_SKIN,
			REFREC_KIND_PIC, REFREC_KIND_SKY, REFREC_KIND_MODEL
		};
		static const char *const names[6] = {
			"maps/base1.bsp", "models/a.md2", "skins/b.pcx", "c", "unit1_", "models/other.md2"
		};
		int i;

		for (i = 0; i < 6; i++)
		{
			CHECK(t.skind[i] == kinds[i] && strcmp(t.sname[i], names[i]) == 0,
					"handles: string %d is %u '%s'", i, t.skind[i], t.sname[i]);
		}
	}

	CHECK(t.types[REC_CVARS] == 1 && t.types[REC_FILL] == 1 && t.types[REC_SKY] == 1,
			"handles: record counts");

	/* a directory name that is not a pin id leaves the id fields zero */
	path = TracePath("not-a-pin");
	RefRec_StartRecord(path, 320, 240, 0);
	RefRec_StopRecord();

	if (ReadTrace(path, &t, NULL, NULL, NULL))
	{
		static const char zero[16];

		CHECK(memcmp(t.h.engine_sha, zero, 16) == 0 && t.h.scenario_hash == 0 &&
				t.h.frame_count == 0 && t.strings == 0, "handles: empty trace fields");
	}
	else
	{
		CHECK(0, "handles: empty trace refused: %s", t.err);
	}
}

/* The refdef_t deep copy: counts bounded, NULL arrays, areabits, verbatim fields. */
static void
TestRecordFrameCopy(void)
{
	static entity_t ents[MAX_ENTITIES + 5];
	static particle_t parts[MAX_PARTICLES + 5];
	static dlight_t dl[MAX_DLIGHTS];
	static lightstyle_t ls[MAX_LIGHTSTYLES];
	static uint8_t area[REFREC_AREABYTES];
	refdef_t fd;
	handles_ctx_t x;
	tracefile_t t;
	const char *path = TracePath("framecopy");

	memset(&fd, 0, sizeof(fd));
	memset(&x, 0, sizeof(x));
	ls[7].rgb[1] = 0.5f;
	ls[7].white = 1.5f;
	parts[0].origin[2] = -3.0f;
	parts[0].color = 0xd0;
	parts[0].alpha = 0.25f;
	area[31] = 0x80;

	RefRec_StartRecord(path, 640, 480, 30);

	/* frame 1: areabits, lightstyles, counts over the maxima */
	RefRec_Control(REC_BEGINFRAME, 0);
	fd.areabits = area;
	fd.lightstyles = ls;
	fd.entities = ents;
	fd.num_entities = MAX_ENTITIES + 5;
	fd.particles = parts;
	fd.num_particles = MAX_PARTICLES + 5;
	fd.dlights = dl;
	fd.num_dlights = -1;
	RefRec_RenderFrame(&fd);
	RefRec_Control(REC_ENDFRAME, 0);

	/* frame 2: no areabits, NULL arrays with counts, NULL lightstyles */
	RefRec_Control(REC_BEGINFRAME, 0);
	memset(&fd, 0, sizeof(fd));
	fd.num_entities = 3;
	fd.num_particles = 2;
	RefRec_RenderFrame(&fd);
	RefRec_RenderFrame(NULL);
	RefRec_Control(REC_ENDFRAME, 0);
	CHECK(RefRec_Recording(), "framecopy: a bad refdef_t stopped the recording");
	RefRec_StopRecord();

	if (!ReadTrace(path, &t, HandlesCb, &x, NULL))
	{
		CHECK(0, "framecopy: trace refused: %s", t.err);
		return;
	}

	CHECK(t.frames == 2 && x.nframes == 2, "framecopy: %u frames, %u RENDERFRAME", t.frames, x.nframes);
	CHECK(x.areaflag[0] == 1 && x.areaflag[1] == 0, "framecopy: areabits flags %u %u", x.areaflag[0], x.areaflag[1]);
	/* frame 1 counts were clamped; the last frame's are zero */
	CHECK(x.num_counts[0] == 0 && x.num_counts[1] == 0 && x.num_counts[2] == 0, "framecopy: NULL arrays not recorded empty");
	CHECK(t.warns[REFREC_WARN_COUNT] == 3, "framecopy: %u count warnings", t.warns[REFREC_WARN_COUNT]);
	CHECK(t.warns[REFREC_WARN_NULL_ARRAY] == 4, "framecopy: %u NULL-array warnings", t.warns[REFREC_WARN_NULL_ARRAY]);
	CHECK(x.nents == 8, "framecopy: %u entities seen", x.nents);
	CHECK(Bits(x.p0.origin[2]) == Bits(-3.0f) && x.p0.color == 0xd0 && Bits(x.p0.alpha) == Bits(0.25f),
			"framecopy: particle not verbatim");
}

/* first frame only: the clamped counts, read again with a callback that stops after it */
typedef struct {
	int32_t counts[3];
	uint8_t area31;
	float ls7;
	qboolean seen;
} first_ctx_t;

static void
FirstCb(refrec_type_t type, const refrec_call_t *c, const refrec_frame_t *f, void *vctx)
{
	first_ctx_t *x = vctx;

	if (type == REC_RENDERFRAME && !x->seen)
	{
		x->seen = true;
		x->counts[0] = f->refdef.num_entities;
		x->counts[1] = f->refdef.num_dlights;
		x->counts[2] = f->refdef.num_particles;
		x->area31 = f->refdef.areabits[31];
		x->ls7 = f->refdef.lightstyles[7].white;
	}
}

static void
TestRecordFrameClamp(void)
{
	first_ctx_t x;
	tracefile_t t;
	char path[1200];

	snprintf(path, sizeof(path), "%s/framecopy/trace.q2r", test_dir);
	memset(&x, 0, sizeof(x));

	if (!ReadTrace(path, &t, FirstCb, &x, NULL))
	{
		CHECK(0, "clamp: trace refused: %s", t.err);
		return;
	}

	CHECK(x.counts[0] == MAX_ENTITIES && x.counts[1] == 0 && x.counts[2] == MAX_PARTICLES,
			"clamp: counts %d %d %d", x.counts[0], x.counts[1], x.counts[2]);
	CHECK(x.area31 == 0x80 && Bits(x.ls7) == Bits(1.5f), "clamp: areabits or lightstyles not copied");
}

/* 2D records: oversized raw is a warning, NULL names and colors are safe */
static void
TestRecordDraws(void)
{
	static uint8_t pixels[16 * 8];
	static uint8_t palette[REFREC_PALETTE_SIZE];
	handles_ctx_t x;
	tracefile_t t;
	refrec_writer_t w;
	refrec_call_t c;
	const char *path = TracePath("draws");
	int32_t big[7] = { 0, 0, 640, 480, REFREC_MAX_RAW_DIM + 1, 2, 8 };
	int32_t ok[7] = { 0, 0, 640, 480, 16, 8, 8 };
	int32_t bad_bits[7] = { 0, 0, 640, 480, 16, 8, 16 };
	int32_t xy[4] = { 1, 2, 3, 4 };
	float factor = 2.0f;
	uint32_t i, sum = 0;
	long size;
	FILE *f;

	for (i = 0; i < sizeof(pixels); i++)
	{
		pixels[i] = (uint8_t)(i * 3 + 1);
		sum += pixels[i];
	}

	memset(&x, 0, sizeof(x));
	RefRec_StartRecord(path, 640, 480, 30);
	RefRec_Control(REC_BEGINFRAME, 0);
	RefRec_Draw(REC_STRETCHRAW, big, 7, NULL, 0, NULL, pixels,
			(uint32_t)big[4] * (uint32_t)big[5]);
	CHECK(RefRec_Recording(), "draws: an oversized raw ended the recording");
	RefRec_Draw(REC_STRETCHRAW, bad_bits, 7, NULL, 0, NULL, pixels, sizeof(pixels) * 2);
	RefRec_Draw(REC_STRETCHRAW, ok, 7, NULL, 0, NULL, NULL, sizeof(pixels));	/* no pixels */
	RefRec_Draw(REC_STRETCHRAW, ok, 7, NULL, 0, NULL, pixels, sizeof(pixels));
	RefRec_Draw(REC_STRETCHPIC, xy, 4, NULL, 0, NULL, NULL, 0);	/* NULL name */
	RefRec_Draw(REC_PICSCALEDCOL, xy, 2, &factor, 1, "ch1", NULL, 0);	/* no color */
	RefRec_Draw(REC_PALETTE, NULL, 0, NULL, 0, NULL, palette, REFREC_PALETTE_SIZE);
	RefRec_Draw(REC_PALETTE, NULL, 0, NULL, 0, NULL, NULL, 0);
	RefRec_Control(REC_ENDFRAME, 0);
	RefRec_StopRecord();

	if (!ReadTrace(path, &t, HandlesCb, &x, NULL))
	{
		CHECK(0, "draws: trace refused: %s", t.err);
		return;
	}

	CHECK(t.warns[REFREC_WARN_RAW_TOO_BIG] == 3, "draws: %u raw warnings", t.warns[REFREC_WARN_RAW_TOO_BIG]);
	CHECK(x.raws == 1 && x.rawlen == sizeof(pixels) && x.rawsum == sum, "draws: raw %u len %u", x.raws, x.rawlen);
	CHECK(t.warns[REFREC_WARN_NAME] == 1 && x.ndraw == 2 && x.draw_names[0][0] == '\0',
			"draws: NULL name warnings %u", t.warns[REFREC_WARN_NAME]);
	CHECK(t.warns[REFREC_WARN_COLOR] == 1 && Bits(x.colf[0]) == Bits(2.0f) &&
			Bits(x.colf[1]) == Bits(1.0f) && Bits(x.colf[3]) == Bits(1.0f), "draws: no-color default");
	CHECK(t.types[REC_PALETTE] == 2, "draws: %u palettes", t.types[REC_PALETTE]);

	/* the streamed raw record is byte for byte what RefRec_WriteCall writes */
	memset(&c, 0, sizeof(c));
	c.type = REC_STRETCHRAW;
	memcpy(c.ints, ok, sizeof(ok));
	c.data = pixels;
	c.datalen = sizeof(pixels);
	RefRec_WriterInit(&w);
	RefRec_WriteCall(&w, &c);
	f = fopen(path, "rb");

	if (f && w.ok)
	{
		uint8_t *buf;

		fseek(f, 0, SEEK_END);
		size = ftell(f);
		fseek(f, 0, SEEK_SET);
		buf = malloc(size);

		if (buf && fread(buf, 1, size, f) == (size_t)size)
		{
			qboolean found = false;
			long j;

			for (j = REFREC_HEADER_SIZE; j + (long)w.cursize <= size; j += 4)
			{
				if (memcmp(buf + j, w.data, w.cursize) == 0)
				{
					found = true;
					break;
				}
			}

			CHECK(found, "draws: the streamed raw record differs from RefRec_WriteCall");
		}

		free(buf);
	}

	if (f)
	{
		fclose(f);
	}

	RefRec_WriterFree(&w);
}

/* A recorder limit finishes the trace early, valid and flagged. */
static void
TestRecordLimit(void)
{
	static lightstyle_t ls[MAX_LIGHTSTYLES];
	refdef_t fd;
	tracefile_t t;
	const char *path = TracePath("limit");
	int i;

	memset(&fd, 0, sizeof(fd));
	fd.lightstyles = ls;
	prints = 0;
	RefRec_SetLimit(64 * 1024);
	RefRec_StartRecord(path, 640, 480, 30);

	for (i = 0; i < 100 && RefRec_Recording(); i++)
	{
		RefRec_Control(REC_BEGINFRAME, 0);
		RefRec_RenderFrame(&fd);
		RefRec_Control(REC_ENDFRAME, 0);
	}

	RefRec_SetLimit(0);
	CHECK(!RefRec_Recording() && i < 100, "limit: still recording after %d frames", i);
	RefRec_StopRecord();
	RefRec_Control(REC_BEGINFRAME, 0);	/* after the end: inert */

	if (!ReadTrace(path, &t, NULL, NULL, NULL))
	{
		CHECK(0, "limit: trace refused: %s", t.err);
		return;
	}

	CHECK(t.h.flags == REFREC_FLAG_TRUNCATED, "limit: flags %u", t.h.flags);
	CHECK(t.size <= 64 * 1024 + 4 + REFREC_INDEX_SIZE * 100, "limit: %u bytes", t.size);
	CHECK(t.frames >= 10 && t.frames < (uint32_t)i, "limit: %u frames of %d", t.frames, i);
	CHECK(prints == 2, "limit: %d reports (want the limit and the summary)", prints);

	/*
	 * A raw image whose record header fits and whose pixels do not: the
	 * whole record is refused, so the trace stays within the limit.
	 */
	{
		static uint8_t pixels[64 * 64];
		const int32_t raw[7] = { 0, 0, 640, 480, 64, 64, 8 };
		const uint32_t limit = REFREC_HEADER_SIZE + 12 + 2048;

		path = TracePath("limitraw");
		prints = 0;
		RefRec_SetLimit(limit);
		RefRec_StartRecord(path, 640, 480, 30);
		RefRec_Control(REC_BEGINFRAME, 0);
		RefRec_Draw(REC_STRETCHRAW, raw, 7, NULL, 0, NULL, pixels, sizeof(pixels));
		RefRec_SetLimit(0);
		CHECK(!RefRec_Recording(), "limit raw: still recording");
		RefRec_StopRecord();

		if (!ReadTrace(path, &t, NULL, NULL, NULL))
		{
			CHECK(0, "limit raw: trace refused: %s", t.err);
			return;
		}

		CHECK(t.h.flags == REFREC_FLAG_TRUNCATED && t.types[REC_STRETCHRAW] == 0 &&
				t.h.string_table_offset <= limit, "limit raw: %u raw records, records end at %u",
				t.types[REC_STRETCHRAW], t.h.string_table_offset);
	}
}

/* A failed write ends the recording, reported once; the trace reads as unfinished. */
static void
TestRecordWriteError(void)
{
	static lightstyle_t ls[MAX_LIGHTSTYLES];
	struct rlimit old, small;
	refdef_t fd;
	tracefile_t t;
	const char *path = TracePath("writeerror");
	int i;

	memset(&fd, 0, sizeof(fd));
	fd.lightstyles = ls;
	prints = 0;

	/* a file size limit makes write() fail with EFBIG, as a full disk would with ENOSPC */
	if (getrlimit(RLIMIT_FSIZE, &old) != 0)
	{
		CHECK(0, "writeerror: getrlimit");
		return;
	}

	signal(SIGXFSZ, SIG_IGN);
	small = old;
	small.rlim_cur = 32 * 1024;
	CHECK(setrlimit(RLIMIT_FSIZE, &small) == 0, "writeerror: setrlimit");
	RefRec_StartRecord(path, 640, 480, 30);

	for (i = 0; i < 100 && RefRec_Recording(); i++)
	{
		RefRec_Control(REC_BEGINFRAME, 0);
		RefRec_RenderFrame(&fd);
		RefRec_Control(REC_ENDFRAME, 0);
	}

	CHECK(!RefRec_Recording() && i < 100, "writeerror: still recording after %d frames", i);

	/* the hooks and stop after a failure do nothing and report nothing more */
	RefRec_Control(REC_BEGINFRAME, 0);
	RefRec_RenderFrame(&fd);
	RefRec_StopRecord();
	setrlimit(RLIMIT_FSIZE, &old);
	signal(SIGXFSZ, SIG_DFL);

	CHECK(prints == 1 && strstr(last_print, "recording stopped"), "writeerror: %d reports, last '%s'", prints, last_print);
	CHECK(!ReadTrace(path, &t, NULL, NULL, NULL), "writeerror: an unfinished trace was accepted");

	/* a path that cannot be opened: reported, not recording */
	prints = 0;
	snprintf(last_print, sizeof(last_print), "%s", "");
	{
		char bad[1200];

		snprintf(bad, sizeof(bad), "%s/no-such-dir/trace.q2r", test_dir);
		RefRec_StartRecord(bad, 640, 480, 30);
	}

	CHECK(!RefRec_Recording() && prints == 1, "writeerror: an unopenable path started recording");
	RefRec_StartRecord(NULL, 640, 480, 30);
	CHECK(!RefRec_Recording(), "writeerror: a NULL path started recording");
}

/*
 * A recording that ends inside a call. A recorder limit or a failed
 * write can land on any record, including a REC_WARN written from the
 * middle of a registration, a frame or a draw, with the handle map in use
 * further up the stack. Each run below makes one kind of warning in all
 * three kinds of call; the sweep ends the run at every byte (the limit)
 * and at every file operation (an injected fault), and after each run the
 * recorder must be off, have reported the stop once, and (for a limit)
 * have left a trace that reads back whole. The build has ASAN and UBSAN,
 * so a use of freed or NULL recorder state fails the check.
 */
enum {
	SW_UNKNOWN, SW_LONGNAME, SW_NULLNAME, SW_FULL, SW_COUNTS, SW_RAW, SW_FRAMES, SW_NUM
};

static const char *const sweep_names[SW_NUM] = {
	"unknown handle", "over-long name", "NULL name", "handle map full",
	"clamped counts", "oversized raw", "frame order"
};

static const refrec_warn_t sweep_warns[SW_NUM] = {
	REFREC_WARN_UNKNOWN_HANDLE, REFREC_WARN_NAME, REFREC_WARN_NAME, REFREC_WARN_HANDLES_FULL,
	REFREC_WARN_COUNT, REFREC_WARN_RAW_TOO_BIG, REFREC_WARN_FRAME
};

/* fake pointers that fill the handle map, and its size in this run */
static char sweep_fill[REFREC_MAX_HANDLES];
static uint32_t sweep_handles = REFREC_MAX_HANDLES;

static void
SweepRun(int kind, const char *path)
{
	static entity_t ents[MAX_ENTITIES + 1];
	static particle_t parts[4];
	static dlight_t dl[MAX_DLIGHTS + 1];
	static lightstyle_t ls[MAX_LIGHTSTYLES];
	static uint8_t pixels[64 * 4 * 4];
	static char longname[1100], qpath[80];
	const int32_t raw_ok[7] = { 0, 0, 64, 4, 64, 4, 8 };
	const int32_t raw_wide[7] = { 0, 0, 64, 4, 63, 4, 32 };	/* 1008 bytes, padding none */
	const int32_t raw_odd[7] = { 0, 0, 64, 4, 3, 1, 8 };	/* 3 bytes and 1 of padding */
	const int32_t raw_big[7] = { 0, 0, 640, 480, REFREC_MAX_RAW_DIM + 1, 1, 8 };
	const int32_t raw_bits[7] = { 0, 0, 640, 480, 4, 4, 16 };
	const int32_t xy[5] = { 1, 2, 3, 4, 5 };
	const float sky[4] = { 0, 0, 0, 1.0f }, col[4] = { 2.0f, 0.5f, 0.5f, 0.5f };
	refdef_t fd;
	int32_t frame;
	int i;

	memset(longname, 'n', sizeof(longname) - 1);
	memset(qpath, 'q', sizeof(qpath) - 1);
	memset(&fd, 0, sizeof(fd));
	memset(ents, 0, sizeof(ents));
	fd.lightstyles = ls;
	fd.entities = ents;

	RefRec_StartRecord(path, 640, 480, 30);

	/* registration */
	switch (kind)
	{
		case SW_LONGNAME:
			RefRec_Registration(REC_BEGINREG, longname, NULL);
			RefRec_Registration(REC_MODEL, qpath, &handle_a);	/* cut in the table */
			RefRec_Registration(REC_SKIN, longname, &handle_b);	/* cut in the record and the table */
			break;

		case SW_NULLNAME:
			RefRec_Registration(REC_BEGINREG, NULL, NULL);
			RefRec_Registration(REC_MODEL, NULL, &handle_a);
			RefRec_Registration(REC_PIC, NULL, &handle_c);
			break;

		case SW_FULL:
			RefRec_Registration(REC_BEGINREG, "maps/m.bsp", NULL);
			RefRec_Registration(REC_MODEL, "models/old.md2", &handle_a);

			for (i = 0; i < (int)sweep_handles - 2; i++)
			{
				RefRec_Registration(REC_PIC, "p", &sweep_fill[i]);
			}

			RefRec_Registration(REC_MODEL, "models/new.md2", &handle_a);	/* full: retired */
			RefRec_Registration(REC_SKIN, "skins/s.pcx", &handle_b);	/* full */
			RefRec_Registration(REC_MODEL, "models/old.md2", &handle_a);	/* still full */
			break;

		default:
			RefRec_Registration(REC_BEGINREG, "maps/m.bsp", NULL);
			RefRec_Registration(REC_MODEL, "models/a.md2", &handle_a);
			RefRec_Registration(REC_SKIN, "skins/b.pcx", &handle_b);
			break;
	}

	RefRec_Registration(REC_ENDREG, NULL, NULL);

	for (frame = 0; frame < 2; frame++)
	{
		if (kind == SW_FRAMES)
		{
			RefRec_Control(REC_ENDFRAME, 0);	/* outside a frame */
			RefRec_Control(REC_BEGINFRAME, 0);
		}

		RefRec_Control(REC_BEGINFRAME, 0);
		fd.num_entities = 4;
		fd.num_particles = 0;
		fd.num_dlights = 0;
		fd.particles = parts;
		fd.dlights = NULL;
		ents[0].model = (struct model_s *)&handle_a;
		ents[0].skin = (struct image_s *)&handle_b;
		ents[1].model = (struct model_s *)&handle_a;
		ents[2].model = NULL;
		ents[3].model = (struct model_s *)&handle_a;

		switch (kind)
		{
			case SW_UNKNOWN:
			case SW_FULL:
				/* unknown pointers between known ones; in the full map handle_a is retired */
				ents[0].model = (struct model_s *)&handle_unknown;
				ents[1].skin = (struct image_s *)&handle_unknown;
				ents[2].model = (struct model_s *)&handle_c;
				ents[3].skin = (struct image_s *)&handle_b;
				break;

			case SW_COUNTS:
				/* clamped low, clamped high, and a NULL array with a count */
				fd.num_entities = (frame == 0) ? -1 : 4;
				fd.num_particles = (frame == 0) ? -5 : 2;
				fd.particles = (frame == 0) ? parts : NULL;
				fd.num_dlights = MAX_DLIGHTS + 1;
				fd.dlights = dl;
				fd.lightstyles = (frame == 0) ? NULL : ls;
				break;

			default:
				break;
		}

		RefRec_RenderFrame(&fd);

		if (kind == SW_COUNTS)
		{
			RefRec_RenderFrame(NULL);
		}

		/* draws */
		switch (kind)
		{
			case SW_LONGNAME:
				RefRec_Draw(REC_SKY, NULL, 0, sky, 4, longname, NULL, 0);
				RefRec_Draw(REC_STRETCHPIC, xy, 4, NULL, 0, longname, NULL, 0);
				RefRec_Draw(REC_PICSCALEDCOL, xy, 2, col, 4, qpath, NULL, 0);
				break;

			case SW_NULLNAME:
				RefRec_Draw(REC_SKY, NULL, 0, sky, 4, NULL, NULL, 0);
				RefRec_Draw(REC_STRETCHPIC, xy, 4, NULL, 0, NULL, NULL, 0);
				RefRec_Draw(REC_PICSCALEDCOL, xy, 2, col, 1, NULL, NULL, 0);	/* no color */
				break;

			case SW_FULL:
				RefRec_Draw(REC_SKY, NULL, 0, sky, 4, "newsky", NULL, 0);	/* full */
				break;

			case SW_RAW:
				RefRec_Draw(REC_STRETCHRAW, raw_big, 7, NULL, 0, NULL, pixels, sizeof(pixels));
				RefRec_Draw(REC_STRETCHRAW, raw_ok, 7, NULL, 0, NULL, pixels, 64 * 4);
				RefRec_Draw(REC_STRETCHRAW, raw_bits, 7, NULL, 0, NULL, pixels, 4 * 4 * 2);
				RefRec_Draw(REC_STRETCHRAW, raw_wide, 7, NULL, 0, NULL, pixels, 63 * 4 * 4);
				RefRec_Draw(REC_STRETCHRAW, raw_odd, 7, NULL, 0, NULL, pixels, 3);
				RefRec_Draw(REC_STRETCHRAW, raw_ok, 7, NULL, 0, NULL, NULL, 64 * 4);
				break;

			default:
				RefRec_Draw(REC_FILL, xy, 5, NULL, 0, NULL, NULL, 0);
				break;
		}

		RefRec_Control(REC_CVARS, 0);
		RefRec_Draw(REC_CAPTURE, &frame, 1, NULL, 0, NULL, NULL, 0);
		RefRec_Control(REC_ENDWORLD, 0);
		RefRec_Control(REC_ENDFRAME, 0);
	}

	RefRec_StopRecord();
}

static int sweep_bad;

static void
SweepBad(int kind, const char *what, uint32_t at)
{
	if (++sweep_bad <= 5)
	{
		printf("FAIL sweep %s: %s at %u (prints %d, last '%s')\n",
				sweep_names[kind], what, at, prints, last_print);
	}
}

static void
TestRecordStopSweep(void)
{
	const char *path = TracePath("sweep");
	const char *env = getenv("REFREC_FULL_SWEEP");
	qboolean full_sweep = env && strcmp(env, "1") == 0;
	tracefile_t t, whole;
	uint32_t end, limit, n, runs = 0;
	int kind;

	sweep_bad = 0;

	/*
	 * The full-map run fills a map of 16 (RefRec_SetHandleLimit), so that
	 * each run stays short; TestRecordHandlesFull fills the real one.
	 */
	sweep_handles = 16;
	RefRec_SetHandleLimit(sweep_handles);

	for (kind = 0; kind < SW_NUM; kind++)
	{
		/* the whole run: where its records end, and that it warns as meant */
		prints = 0;
		SweepRun(kind, path);

		if (!ReadTrace(path, &whole, NULL, NULL, NULL) || whole.h.flags != 0 || prints != 1)
		{
			CHECK(0, "sweep %s: the whole run is not a finished trace: %s", sweep_names[kind], whole.err);
			continue;
		}

		CHECK(whole.warns[sweep_warns[kind]] >= 2, "sweep %s: %u warnings of code %d",
				sweep_names[kind], whole.warns[sweep_warns[kind]], sweep_warns[kind]);
		end = whole.h.string_table_offset;

		/*
		 * A limit at every byte. The limit is checked per record, so a
		 * limit anywhere inside one record ends the run on that record;
		 * inside a record over 48 bytes (a REC_RENDERFRAME is over 4 KB,
		 * a long name or a raw image up to some) the gate takes its first
		 * and last 16 bytes only. REFREC_FULL_SWEEP=1 takes every byte
		 * there too.
		 */
		for (limit = 1; limit <= end + 4; limit++)
		{
			qboolean cut;
			uint32_t f;

			for (f = 0; f < whole.nlong && !full_sweep; f++)
			{
				if (limit > whole.long_start[f] + 16 && limit + 16 < whole.long_end[f])
				{
					limit = whole.long_end[f] - 16;
				}
			}

			cut = limit < end;
			prints = 0;
			last_print[0] = '\0';
			RefRec_SetLimit(limit);
			SweepRun(kind, path);
			RefRec_SetLimit(0);
			runs++;

			if (RefRec_Recording())
			{
				SweepBad(kind, "still recording after a limit", limit);
				RefRec_StopRecord();
			}
			else if (!ReadTrace(path, &t, NULL, NULL, NULL))
			{
				SweepBad(kind, t.err, limit);
			}
			else if (((t.h.flags & REFREC_FLAG_TRUNCATED) != 0) != cut ||
				t.h.string_table_offset > (limit < REFREC_HEADER_SIZE ? REFREC_HEADER_SIZE : limit) ||
				prints != (cut ? 2 : 1))
			{
				SweepBad(kind, "limit not kept, or not reported once", limit);
			}
		}

		/* a failed write, seek or close at every file operation, until one is past the last */
		for (n = 1;; n++)
		{
			prints = 0;
			last_print[0] = '\0';
			RefRec_SetWriteFault(n);
			SweepRun(kind, path);
			RefRec_SetWriteFault(0);
			runs++;

			if (prints == 1 && strstr(last_print, "trace written"))
			{
				break;
			}

			if (RefRec_Recording() || prints != 1 || !strstr(last_print, "recording stopped"))
			{
				SweepBad(kind, "a failed write not reported once", n);
				RefRec_StopRecord();
			}
			else if (!strstr(last_print, "close") && ReadTrace(path, &t, NULL, NULL, NULL))
			{
				SweepBad(kind, "an unfinished trace was accepted", n);
			}

			if (n > 1000000)
			{
				SweepBad(kind, "the fault sweep does not end", n);
				break;
			}
		}

		/* the frame limit, on the first frames */
		for (limit = 1; limit <= 3; limit++)
		{
			prints = 0;
			RefRec_SetFrameLimit(limit);
			SweepRun(kind, path);
			RefRec_SetFrameLimit(0);
			runs++;

			if (RefRec_Recording() || !ReadTrace(path, &t, NULL, NULL, NULL) ||
				t.h.frame_count > limit || (limit < 2) != ((t.h.flags & REFREC_FLAG_TRUNCATED) != 0))
			{
				SweepBad(kind, "frame limit not kept", limit);
			}
		}
	}

	CHECK(sweep_bad == 0, "sweep: %d of %u runs ended wrong", sweep_bad, runs);
	RefRec_SetHandleLimit(0);
	sweep_handles = REFREC_MAX_HANDLES;
	CHECK(runs > 1000, "sweep: only %u runs", runs);
}

/*
 * A full handle map: a pointer re-registered under a new name retires its
 * old id, so entities using it are recorded as id 0 with a warning, never
 * under the old name.
 */
typedef struct {
	uint32_t model_ids[8], nmodels;
	refrec_entity_t first[4];
	qboolean seen;
} full_ctx_t;

static void
FullCb(refrec_type_t type, const refrec_call_t *c, const refrec_frame_t *f, void *vctx)
{
	full_ctx_t *x = vctx;

	if (type == REC_MODEL && x->nmodels < 8)
	{
		x->model_ids[x->nmodels++] = c->id;
	}

	if (type == REC_RENDERFRAME && !x->seen)
	{
		x->seen = true;
		memcpy(x->first, f->entities, sizeof(x->first));
	}
}

static void
TestRecordHandlesFull(void)
{
	const char *path = TracePath("handlesfull");
	full_ctx_t x;
	tracefile_t t;

	memset(&x, 0, sizeof(x));
	prints = 0;
	SweepRun(SW_FULL, path);

	if (!ReadTrace(path, &t, FullCb, &x, NULL))
	{
		CHECK(0, "handles full: trace refused: %s", t.err);
		return;
	}

	CHECK(t.strings == REFREC_MAX_HANDLES, "handles full: %u strings", t.strings);
	CHECK(t.skind[1] == REFREC_KIND_MODEL && strcmp(t.sname[1], "models/old.md2") == 0,
			"handles full: the old entry left the string table");
	/* the map, old.md2 as id 2, then new.md2, the skin and old.md2 again as id 0 */
	CHECK(x.nmodels == 3 && x.model_ids[0] == 2 && x.model_ids[1] == 0 && x.model_ids[2] == 0,
			"handles full: REC_MODEL ids %u %u %u", x.model_ids[0], x.model_ids[1], x.model_ids[2]);
	/* handle_a on entities 1 and 3 is retired: id 0, not the stale 2 */
	CHECK(x.seen && x.first[1].model_id == 0 && x.first[3].model_id == 0,
			"handles full: a retired pointer kept id %u %u", x.first[1].model_id, x.first[3].model_id);
	CHECK(t.warns[REFREC_WARN_HANDLES_FULL] == 5, "handles full: %u full-map warnings",
			t.warns[REFREC_WARN_HANDLES_FULL]);
	/* per frame: entity 0 model and skin, 1 model and skin, 2 model, 3 model and skin */
	CHECK(t.warns[REFREC_WARN_UNKNOWN_HANDLE] == 14, "handles full: %u unknown-handle warnings",
			t.warns[REFREC_WARN_UNKNOWN_HANDLE]);
}

/*
 * A full handle map and a pointer registered again under another name and
 * another kind. Rec_Lookup falls back from a skin to a pic entry, so the
 * retirement covers every kind of the pointer: the entity is recorded as
 * id 0 with an unknown-handle warning, never under the old name.
 */
static void
TestRecordHandlesFullKinds(void)
{
	static const struct {
		refrec_type_t first, second;
		const char *first_name, *second_name;
		qboolean model;		/* the entity uses the pointer as its model, else as its skin */
	} runs[] = {
		{ REC_PIC, REC_SKIN, "pics/old.pcx", "skins/new.pcx", false },
		{ REC_SKIN, REC_PIC, "skins/old.pcx", "pics/new.pcx", false },
		{ REC_MODEL, REC_SKIN, "models/old.md2", "skins/new.pcx", true },
		{ REC_SKIN, REC_MODEL, "skins/old.pcx", "models/new.md2", false },
		{ REC_PIC, REC_MODEL, "pics/old.pcx", "models/new.md2", false },
		{ REC_MODEL, REC_PIC, "models/old.md2", "pics/new.pcx", true }
	};
	static entity_t ents[2];
	static char ptr, other[2];
	size_t r;

	for (r = 0; r < sizeof(runs) / sizeof(runs[0]); r++)
	{
		const char *path = TracePath("handleskinds");
		full_ctx_t x;
		tracefile_t t;
		refdef_t fd;
		uint32_t id;

		memset(&x, 0, sizeof(x));
		memset(&fd, 0, sizeof(fd));
		memset(ents, 0, sizeof(ents));
		fd.entities = ents;
		fd.num_entities = 1;
		ents[0].model = runs[r].model ? (struct model_s *)&ptr : NULL;
		ents[0].skin = runs[r].model ? NULL : (struct image_s *)&ptr;

		RefRec_SetHandleLimit(4);
		RefRec_StartRecord(path, 640, 480, 0);
		RefRec_Registration(REC_BEGINREG, "maps/m.bsp", NULL);		/* id 1 */
		RefRec_Registration(runs[r].first, runs[r].first_name, &ptr);	/* id 2 */
		RefRec_Registration(REC_MODEL, "models/a.md2", &other[0]);	/* id 3 */
		RefRec_Registration(REC_MODEL, "models/b.md2", &other[1]);	/* id 4, the map is full */
		RefRec_Registration(runs[r].second, runs[r].second_name, &ptr);	/* full: retires id 2 */
		RefRec_Registration(REC_ENDREG, NULL, NULL);
		RefRec_Control(REC_BEGINFRAME, 0);
		RefRec_RenderFrame(&fd);
		RefRec_Control(REC_ENDFRAME, 0);
		RefRec_StopRecord();
		RefRec_SetHandleLimit(0);

		if (!ReadTrace(path, &t, FullCb, &x, NULL))
		{
			CHECK(0, "handles kinds %u: trace refused: %s", (unsigned)r, t.err);
			continue;
		}

		id = runs[r].model ? x.first[0].model_id : x.first[0].skin_id;
		CHECK(t.strings == 4 && strcmp(t.sname[1], runs[r].first_name) == 0,
				"handles kinds %u: string table", (unsigned)r);
		CHECK(x.seen && id == 0, "handles kinds %u: the pointer kept id %u", (unsigned)r, id);
		CHECK(t.warns[REFREC_WARN_UNKNOWN_HANDLE] == 1,
				"handles kinds %u: %u unknown-handle warnings", (unsigned)r,
				t.warns[REFREC_WARN_UNKNOWN_HANDLE]);
		CHECK(t.warns[REFREC_WARN_HANDLES_FULL] == 1,
				"handles kinds %u: %u full-map warnings", (unsigned)r,
				t.warns[REFREC_WARN_HANDLES_FULL]);
	}
}

/*
 * --read and --read --names print every name byte and engine id byte
 * outside 0x20..0x7e (and the backslash) escaped: a server or a demo
 * chooses those bytes.
 */
static void
TestReadEscapes(void)
{
	static const char sky[] = "s\x1b[2J\x01\x7f\xff\\e";
	char sub[32], buf[4096];
	const float rot[4] = { 0, 0, 0, 1.0f };
	const char *path;
	size_t n, i;
	qboolean clean = true;
	FILE *out;
	int pass;

	snprintf(sub, sizeof(sub), "e%cx-42", 0x1b);
	path = TracePath(sub);
	RefRec_StartRecord(path, 640, 480, 0);
	RefRec_Registration(REC_BEGINREG, "maps/m\x07.bsp", NULL);
	RefRec_Draw(REC_SKY, NULL, 0, rot, 4, sky, NULL, 0);
	RefRec_StopRecord();

	for (pass = 0; pass < 2; pass++)
	{
		out = tmpfile();

		if (!out)
		{
			CHECK(0, "escapes: tmpfile");
			return;
		}

		CHECK(ReadMode(path, pass == 1, out) == 0, "escapes: --read refused the trace");
		rewind(out);
		n = fread(buf, 1, sizeof(buf) - 1, out);
		buf[n] = '\0';
		fclose(out);

		for (i = 0; i < n; i++)
		{
			if ((buf[i] < 0x20 || buf[i] > 0x7e) && buf[i] != '\n')
			{
				clean = false;
			}
		}

		if (pass == 0)
		{
			CHECK(strstr(buf, "run e\\x1bx-42\n") != NULL, "escapes: engine id not escaped: %s", buf);
		}
		else
		{
			CHECK(strstr(buf, "4 s\\x1b[2J\\x01\\x7f\\xff\\x5ce\n") != NULL &&
					strstr(buf, "5 maps/m\\x07.bsp\n") != NULL, "escapes: names not escaped: %s", buf);
		}
	}

	CHECK(clean, "escapes: a byte outside 0x20..0x7e reached the output");
}

/* The string table and frame index readers refuse malformed entries. */
static void
TestTableReaders(void)
{
	uint8_t buf[4 + REFREC_STRING_SIZE], *e = buf + 4, idx[REFREC_INDEX_SIZE];
	refrec_reader_t r;
	uint32_t count, kind, id, frame;
	uint64_t off;
	char name[MAX_QPATH];

#define TABLE(setup, want, what) \
	do \
	{ \
		memset(buf, 0, sizeof(buf)); \
		SetU32(buf, 1); \
		SetU32(e, REFREC_KIND_PIC); \
		SetU32(e + 4, 1); \
		memcpy(e + 8, "conchars", 8); \
		setup; \
		RefRec_ReaderInit(&r, buf, sizeof(buf)); \
		CHECK((RefRec_ReadStringTable(&r, &count) && \
			RefRec_NextString(&r, &kind, &id, name)) == (want), "string table: %s", what); \
	} while (0)

	TABLE((void)0, true, "a valid entry was refused");
	TABLE(memset(e + 8, 'a', MAX_QPATH), false, "a name without a NUL was accepted");
	TABLE(e[8 + 20] = 'x', false, "bytes after the NUL were accepted");
	TABLE(SetU32(e, 0), false, "kind 0 was accepted");
	TABLE(SetU32(e, REFREC_KIND_LAST + 1), false, "a kind past the last was accepted");
	TABLE(SetU32(e + 4, 0), false, "id 0 was accepted");
	TABLE(SetU32(buf, 2), false, "a count over the entries present was accepted");
	TABLE(SetU32(buf, REFREC_MAX_HANDLES + 1), false, "a count over REFREC_MAX_HANDLES was accepted");
#undef TABLE

	memset(buf, 0, sizeof(buf));
	SetU32(buf, 1);
	SetU32(e, REFREC_KIND_MODEL);
	SetU32(e + 4, 1);
	RefRec_ReaderInit(&r, buf, sizeof(buf) - 1);
	CHECK(!(RefRec_ReadStringTable(&r, &count) && RefRec_NextString(&r, &kind, &id, name)),
			"string table: a truncated entry was accepted");

	SetU32(idx, 7);
	SetU32(idx + 4, 0x10);
	SetU32(idx + 8, 1);
	RefRec_ReaderInit(&r, idx, sizeof(idx));
	CHECK(RefRec_NextFrameIndex(&r, &frame, &off) && frame == 7 && off == 0x100000010ull,
			"frame index: entry read wrong");
	RefRec_ReaderInit(&r, idx, sizeof(idx) - 1);
	CHECK(!RefRec_NextFrameIndex(&r, &frame, &off), "frame index: a truncated entry was accepted");

	/* the header offsets and a frame offset against the file size */
	{
		refrec_header_t h;
		uint64_t fo;

#define LAYOUT(setup, size, want, what) \
		do \
		{ \
			memset(&h, 0, sizeof(h)); \
			h.string_table_offset = 1000; \
			h.frame_index_offset = 1004; \
			h.frame_count = 2; \
			fo = 64; \
			setup; \
			CHECK(RefRec_CheckLayout(&h, (size), &fo) == (want), "layout: %s", what); \
		} while (0)

		LAYOUT((void)0, 1004 + 24, true, "a valid layout was refused");
		LAYOUT(fo = 992, 1004 + 24, true, "the last record header place was refused");
		LAYOUT((void)0, 1004 + 23, false, "a file shorter than the index was accepted");
		LAYOUT((void)0, 1004 + 25, false, "bytes after the index were accepted");
		LAYOUT(h.string_table_offset = 60, 1004 + 24, false, "a string table inside the header was accepted");
		LAYOUT(h.string_table_offset = 0, 1004 + 24, false, "an unfinished trace was accepted");
		LAYOUT(h.string_table_offset = 1008, 1004 + 24, false, "a string table after the index was accepted");
		LAYOUT(h.frame_index_offset = 1006; h.frame_count = 2, 1006 + 24, false, "an unaligned index was accepted");
		LAYOUT(h.frame_count = 0x20000000u, 1004 + 24, false, "a frame count past the file was accepted");
		LAYOUT((void)0, 0x100000000ull, false, "a file of 4 GiB was accepted");
		LAYOUT(fo = 60, 1004 + 24, false, "a frame inside the header was accepted");
		LAYOUT(fo = 66, 1004 + 24, false, "an unaligned frame was accepted");
		LAYOUT(fo = 996, 1004 + 24, false, "a frame without room for its record header was accepted");
		LAYOUT(fo = 1000, 1004 + 24, false, "a frame in the string table was accepted");
		LAYOUT(fo = 0x100000040ull, 1004 + 24, false, "a frame offset over 4 GiB was accepted");
#undef LAYOUT

		memset(&h, 0, sizeof(h));
		h.string_table_offset = 64;
		h.frame_index_offset = 68;
		CHECK(RefRec_CheckLayout(&h, 68, NULL), "layout: an empty finished trace was refused");
	}
}

/* ---- the replayer, into a mock renderer ---- */

/* the live handles the mock renderer hands out */
static char live_model, live_skin, live_pic;

#define MOCK_LOG	512
#define MOCK_FRAMES	8

typedef struct {
	int log[MOCK_LOG];	/* record types forwarded, in order */
	uint32_t nlog;
	refdef_t fd[MOCK_FRAMES];
	entity_t ents[MOCK_FRAMES][4];
	dlight_t dl[MOCK_FRAMES];
	particle_t parts[MOCK_FRAMES][2];
	uint32_t nfd;
	int32_t captures[16];
	uint32_t ncap;
	refrec_call_t last[REC_LAST + 1];	/* the last forwarded call of each type */
	char regnames[16][64];
	uint32_t nreg;
	uint32_t palettes;	/* forwarded with 768 bytes */
} mock_t;

static mock_t mock;

/* the size the mock's Draw_GetPicSize gives every pic but "missing" */
static int32_t mock_picw = 8, mock_pich = 8;

static void
MockLog(int type)
{
	if (mock.nlog < MOCK_LOG)
	{
		mock.log[mock.nlog] = type;
	}

	mock.nlog++;
}

static void *
MockRegister(refrec_type_t t, const char *name)
{
	MockLog(t);

	if (mock.nreg < 16)
	{
		snprintf(mock.regnames[mock.nreg], sizeof(mock.regnames[0]), "%s", name);
	}

	mock.nreg++;

	if (strcmp(name, "missing") == 0)
	{
		return NULL;
	}

	return t == REC_MODEL ? (void *)&live_model : t == REC_SKIN ? (void *)&live_skin : (void *)&live_pic;
}

static void
MockRenderFrame(refdef_t *fd)
{
	MockLog(REC_RENDERFRAME);

	if (mock.nfd < MOCK_FRAMES)
	{
		uint32_t k = mock.nfd;
		int i;

		mock.fd[k] = *fd;

		for (i = 0; i < fd->num_entities && i < 4; i++)
		{
			mock.ents[k][i] = fd->entities[i];
		}

		if (fd->num_dlights)
		{
			mock.dl[k] = fd->dlights[0];
		}

		for (i = 0; i < fd->num_particles && i < 2; i++)
		{
			mock.parts[k][i] = fd->particles[i];
		}
	}

	mock.nfd++;
}

static void
MockCall(refrec_call_t *c)
{
	MockLog(c->type);
	mock.last[c->type] = *c;

	if (c->type == REC_PICSIZE)
	{
		qboolean missing = strcmp(c->name, "missing") == 0;

		c->ints[0] = missing ? -1 : mock_picw;
		c->ints[1] = missing ? -1 : mock_pich;
	}

	if (c->type == REC_PALETTE && c->datalen == REFREC_PALETTE_SIZE)
	{
		mock.palettes++;
	}

	if (c->type == REC_CAPTURE && mock.ncap < 16)
	{
		mock.captures[mock.ncap++] = c->ints[0];
	}
}

/* replays path into the mock; false when the open was refused */
static qboolean
Replay(const char *path, uint32_t api, int width, int height, refrec_replay_stats_t *st)
{
	refrec_target_t t;
	refrec_header_t h;
	int guard = 0;

	memset(&mock, 0, sizeof(mock));
	print_log[0] = '\0';
	memset(&t, 0, sizeof(t));
	t.api_version = api;
	t.width = width;
	t.height = height;
	t.Register = MockRegister;
	t.RenderFrame = MockRenderFrame;
	t.Call = MockCall;
	RefRec_SetReplayTarget(&t);

	if (!RefRec_OpenReplay(path, &h))
	{
		RefRec_ReplayStats(st);
		return false;
	}

	while (RefRec_ReplayNextFrame() && guard++ < 100000)
	{
	}

	RefRec_ReplayStats(st);
	RefRec_CloseReplay();

	return true;
}

/* the record types the replayer forwards, as ReadTrace walks them */
typedef struct {
	int log[MOCK_LOG];
	uint32_t nlog;
} typelog_t;

static void
TypeLogCb(refrec_type_t type, const refrec_call_t *c, const refrec_frame_t *f, void *vctx)
{
	typelog_t *x = vctx;

	(void)c;
	(void)f;

	if (type == REC_CVARS || type == REC_WARN)
	{
		return;
	}

	if (x->nlog < MOCK_LOG)
	{
		x->log[x->nlog] = type;
	}

	x->nlog++;
}

/* a whole file into a malloc'd buffer */
static uint8_t *
Slurp(const char *path, long *size)
{
	uint8_t *buf;
	FILE *f = fopen(path, "rb");

	*size = 0;

	if (!f)
	{
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(*size > 0 ? *size : 1);

	if (buf && fread(buf, 1, *size, f) != (size_t)*size)
	{
		free(buf);
		buf = NULL;
	}

	fclose(f);

	return buf;
}

static void
Spill(const char *path, const uint8_t *buf, long size)
{
	FILE *f = fopen(path, "wb");

	if (f)
	{
		fwrite(buf, 1, size, f);
		fclose(f);
	}
}

static uint32_t
GetU32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/*
 * A three-frame trace: registrations before and between frames (one that
 * failed live), entities on registered handles, 2D draws, two captures.
 */
static void
RecordReplayTrace(const char *path)
{
	static entity_t ents[2];
	static particle_t parts[1];
	static dlight_t dl[1];
	static lightstyle_t ls[MAX_LIGHTSTYLES];
	static uint8_t area[REFREC_AREABYTES];
	static uint8_t palette[REFREC_PALETTE_SIZE];
	const float sky[4] = { 0.5f, 0, 0, 1 };
	refdef_t fd;
	int32_t fill[5] = { 1, 2, 3, 4, 5 }, size[2] = { 8, 8 }, cap = 0;
	int i;

	RefRec_StartRecord(path, 640, 480, 30);
	RefRec_Registration(REC_PIC, "conchars", &handle_c);
	RefRec_Registration(REC_BEGINREG, "base1", NULL);
	RefRec_Registration(REC_MODEL, "models/a.md2", &handle_a);
	RefRec_Registration(REC_MODEL, "missing", NULL);
	RefRec_Registration(REC_SKIN, "skins/b.pcx", &handle_b);
	RefRec_Draw(REC_SKY, NULL, 0, sky, 4, "unit1_", NULL, 0);
	RefRec_Registration(REC_ENDREG, NULL, NULL);

	for (i = 0; i < 3; i++)
	{
		memset(&fd, 0, sizeof(fd));
		fd.width = 640;
		fd.height = 480;
		fd.fov_x = 90;
		fd.fov_y = 73.74f;
		fd.time = 1.5f + i;
		fd.rdflags = RDF_UNDERWATER;
		fd.areabits = i == 1 ? NULL : area;
		area[3] = 0x11;
		ls[0].white = 1.0f;
		fd.lightstyles = ls;
		ents[0].model = (struct model_s *)&handle_a;
		ents[0].skin = (struct image_s *)&handle_b;
		ents[0].frame = i;
		ents[0].oldframe = i + 7;
		ents[0].origin[0] = 100.0f;
		ents[0].angles[1] = 45.0f;
		ents[0].oldorigin[2] = -12.5f;
		ents[0].flags = RF_MINLIGHT | RF_GLOW | RF_USE_DISGUISE;
		ents[0].alpha = 1.0f;
		ents[1].model = NULL;
		ents[1].skin = (struct image_s *)&handle_c;	/* a pic used as a skin */
		fd.entities = ents;
		fd.num_entities = 2;
		parts[0].color = 0xd0;
		parts[0].alpha = 0.5f;
		fd.particles = parts;
		fd.num_particles = 1;
		dl[0].intensity = 200;
		dl[0].origin[1] = 64.0f + i;
		dl[0].color[0] = -1;	/* a darkening light */
		fd.dlights = dl;
		fd.num_dlights = 1;

		RefRec_Control(REC_BEGINFRAME, 0);
		RefRec_RenderFrame(&fd);
		RefRec_Draw(REC_PICSIZE, size, 2, NULL, 0, "conchars", NULL, 0);
		RefRec_Draw(REC_FILL, fill, 5, NULL, 0, NULL, NULL, 0);
		RefRec_Control(REC_ENDWORLD, 0);

		if (i != 1)
		{
			RefRec_Draw(REC_CAPTURE, &cap, 1, NULL, 0, NULL, NULL, 0);
			cap++;
		}

		RefRec_Control(REC_ENDFRAME, 0);
		RefRec_Draw(REC_PALETTE, NULL, 0, NULL, 0, NULL, i == 0 ? palette : NULL,
				i == 0 ? REFREC_PALETTE_SIZE : 0);
	}

	RefRec_StopRecord();
}

/* A recorded trace replays every record in order, handles resolved to live ones. */
static void
TestReplayRoundTrip(void)
{
	char path[1200];
	refrec_replay_stats_t st;
	typelog_t want;
	tracefile_t t;
	uint32_t i, last;

	snprintf(path, sizeof(path), "%s", TracePath("replay"));
	RecordReplayTrace(path);
	memset(&want, 0, sizeof(want));

	if (!ReadTrace(path, &t, TypeLogCb, &want, NULL))
	{
		CHECK(0, "replay: trace refused: %s", t.err);
		return;
	}

	CHECK(Replay(path, API_VERSION, 640, 480, &st), "replay: a valid trace was refused");
	CHECK(!st.failed && st.frames == 3 && st.frames == t.frames, "replay: %u frames of %u, failed %d",
			st.frames, t.frames, st.failed);
	CHECK(st.captures == 2 && st.captures == t.captures && mock.ncap == 2 &&
			mock.captures[0] == 0 && mock.captures[1] == 1, "replay: %u captures", st.captures);
	CHECK(st.clamped == 0 && st.dropped == 0 && st.picsize_mismatch == 0,
			"replay: %u clamped, %u dropped, %u picsize mismatches in a clean trace",
			st.clamped, st.dropped, st.picsize_mismatch);

	/* everything up to the last frame's REC_ENDFRAME, in order: not the palette after it */
	for (last = want.nlog; last > 0 && want.log[last - 1] != REC_ENDFRAME; last--)
	{
	}

	CHECK(mock.nlog == last, "replay: %u calls forwarded, %u recorded before the last REC_ENDFRAME", mock.nlog, last);

	for (i = 0; i < last && i < mock.nlog && i < MOCK_LOG; i++)
	{
		if (mock.log[i] != want.log[i])
		{
			CHECK(0, "replay: call %u is type %d, recorded %d", i, mock.log[i], want.log[i]);
			break;
		}
	}

	CHECK(mock.nreg == 4 && strcmp(mock.regnames[3], "skins/b.pcx") == 0 &&
			strcmp(mock.regnames[2], "missing") == 0, "replay: registrations %u", mock.nreg);
	CHECK(mock.nfd == 3, "replay: %u RenderFrame calls", mock.nfd);
	CHECK(mock.ents[0][0].model == (void *)&live_model && mock.ents[0][0].skin == (void *)&live_skin,
			"replay: entity handles not resolved to the live ones");
	CHECK(mock.ents[0][1].model == NULL && mock.ents[0][1].skin == (void *)&live_pic,
			"replay: a NULL model or a pic used as a skin not resolved");
	CHECK(mock.ents[2][0].oldframe == 9 && Bits(mock.ents[2][0].angles[1]) == Bits(45.0f) &&
			Bits(mock.ents[2][0].oldorigin[2]) == Bits(-12.5f) &&
			mock.ents[2][0].flags == (RF_MINLIGHT | RF_GLOW | RF_USE_DISGUISE),
			"replay: entity flags %d, angles, oldorigin or oldframe %d not verbatim",
			mock.ents[2][0].flags, mock.ents[2][0].oldframe);
	CHECK(Bits(mock.dl[2].origin[1]) == Bits(66.0f), "replay: dlight origin not verbatim");
	CHECK(mock.ents[2][0].frame == 2 && Bits(mock.ents[2][0].origin[0]) == Bits(100.0f) &&
			Bits(mock.fd[2].time) == Bits(3.5f) && mock.fd[2].rdflags == RDF_UNDERWATER &&
			mock.fd[0].width == 640 && mock.fd[0].height == 480 && Bits(mock.fd[0].fov_y) == Bits(73.74f),
			"replay: frame values not verbatim");
	CHECK(mock.fd[0].areabits && mock.fd[0].areabits[3] == 0x11 && mock.fd[1].areabits == NULL,
			"replay: areabits not rebuilt");
	CHECK(mock.fd[0].lightstyles && Bits(mock.fd[0].lightstyles[0].white) == Bits(1.0f),
			"replay: lightstyles not rebuilt");
	CHECK(mock.parts[0][0].color == 0xd0 && Bits(mock.parts[0][0].alpha) == Bits(0.5f) &&
			Bits(mock.dl[0].intensity) == Bits(200.0f) && Bits(mock.dl[0].color[0]) == Bits(-1.0f),
			"replay: particle or dlight not verbatim");
	CHECK(mock.last[REC_FILL].ints[4] == 5 && strcmp(mock.last[REC_SKY].name, "unit1_") == 0 &&
			Bits(mock.last[REC_SKY].floats[0]) == Bits(0.5f) && strcmp(mock.last[REC_BEGINREG].name, "base1") == 0,
			"replay: 2D or registration arguments not verbatim");
	CHECK(mock.palettes == 1 && mock.last[REC_PALETTE].datalen == 0, "replay: palettes not forwarded");
	CHECK(!RefRec_ReplayNextFrame(), "replay: a frame after close");

	/* the target's configuration must match the trace */
	CHECK(!Replay(path, API_VERSION, 641, 481, &st) && st.failed && strstr(print_log, "641x481") &&
			!strstr(print_log, "640") && strstr(print_log, "another resolution"),
			"replay: another size was accepted, or the trace's size printed: %s", print_log);
	CHECK(!Replay(path, API_VERSION, 641, 480, &st) && st.failed, "replay: another width was accepted");
	CHECK(!Replay(path, API_VERSION, 640, 481, &st) && st.failed, "replay: another height was accepted");
	CHECK(!Replay(path, API_VERSION - 1, 640, 480, &st) && st.failed, "replay: another API version was accepted");
	CHECK(!Replay(TracePath("absent"), API_VERSION, 640, 480, &st), "replay: a missing file was accepted");

	/* one replay at a time, and none without a complete target */
	{
		refrec_header_t h;
		refrec_target_t bad;

		CHECK(Replay(path, API_VERSION, 640, 480, &st), "replay: refused the second time");
		RefRec_OpenReplay(path, &h);
		CHECK(!RefRec_OpenReplay(path, &h), "replay: a second open while open was accepted");
		RefRec_CloseReplay();
		RefRec_CloseReplay();
		memset(&bad, 0, sizeof(bad));
		bad.api_version = API_VERSION;
		bad.width = 640;
		bad.height = 480;
		bad.Register = MockRegister;
		bad.RenderFrame = MockRenderFrame;
		RefRec_SetReplayTarget(&bad);
		CHECK(!RefRec_OpenReplay(path, &h), "replay: a target without Call was accepted");
		RefRec_CloseReplay();
	}
}

/* Every value a renderer could index with or overflow on is bounded first. */
static void
TestReplayBounds(void)
{
	static entity_t ents[3];
	static particle_t parts[2];
	static dlight_t dl[1];
	static lightstyle_t ls[MAX_LIGHTSTYLES];
	const char *path = TracePath("replaybounds");
	refrec_replay_stats_t st;
	refdef_t fd;
	int32_t pic[4] = { 1 << 30, -(1 << 30), -5, 1 << 30 };
	int32_t fill[5] = { 0, 0, 10, 10, 300 };
	int32_t chr[3] = { 0, 0, -1 };
	int32_t xy[2] = { 3, 4 };
	int32_t size[2] = { 3, 4 };
	float one = 1, huge = 1e9f, nan = NAN, col[4] = { 2, NAN, -1, 0.5f };
	const float sky[4] = { NAN, 0, 0, 1 };

	memset(&fd, 0, sizeof(fd));
	fd.x = -10;
	fd.y = 470;
	fd.width = 99999;
	fd.height = 480;
	fd.fov_x = 0;
	fd.fov_y = NAN;
	fd.vieworg[0] = INFINITY;
	fd.viewangles[1] = 1e9f;
	fd.blend[0] = 2;
	fd.time = -1;
	fd.rdflags = 0xff;
	ls[3].white = 1e9f;
	ls[3].rgb[0] = -1;
	fd.lightstyles = ls;
	ents[0].model = (struct model_s *)&handle_a;
	ents[0].frame = -5;
	ents[0].oldframe = -1;
	ents[0].skinnum = -3;
	ents[0].alpha = 5;
	ents[0].backlerp = -1;
	ents[0].lightstyle = 999;
	ents[0].origin[1] = NAN;
	ents[1].flags = RF_BEAM;
	ents[1].skinnum = (int)0xd0d1d2d3u;
	ents[1].frame = 4;
	ents[2].skinnum = 40;
	ents[2].flags = (int)(0xfff80000u | RF_GLOW);	/* bits no renderer reads */
	fd.entities = ents;
	fd.num_entities = 3;
	parts[0].color = 999;
	parts[0].alpha = 3;
	parts[1].color = -1;
	fd.particles = parts;
	fd.num_particles = 2;
	dl[0].color[0] = 1e9f;
	dl[0].intensity = NAN;
	fd.dlights = dl;
	fd.num_dlights = 1;

	RefRec_StartRecord(path, 640, 480, 30);
	RefRec_Registration(REC_MODEL, "models/a.md2", &handle_a);
	RefRec_Draw(REC_SKY, NULL, 0, sky, 4, "unit1_", NULL, 0);
	RefRec_Control(REC_BEGINFRAME, nan);
	RefRec_RenderFrame(&fd);
	RefRec_Draw(REC_STRETCHPIC, pic, 4, NULL, 0, "p", NULL, 0);
	RefRec_Draw(REC_FILL, fill, 5, NULL, 0, NULL, NULL, 0);
	RefRec_Draw(REC_CHARSCALED, chr, 3, &nan, 1, NULL, NULL, 0);	/* dropped */
	RefRec_Draw(REC_CHARSCALED, chr, 3, &one, 1, NULL, NULL, 0);
	RefRec_Draw(REC_PICSCALED, xy, 2, &huge, 1, "p", NULL, 0);	/* dropped */
	RefRec_Draw(REC_PICSCALEDCOL, xy, 2, col, 4, "p", NULL, 0);
	RefRec_Draw(REC_PICSIZE, size, 2, NULL, 0, "p", NULL, 0);	/* the mock says 8x8 */
	RefRec_Control(REC_ENDFRAME, 0);
	RefRec_StopRecord();

	CHECK(Replay(path, API_VERSION, 640, 480, &st) && !st.failed && st.frames == 1,
			"bounds: the hostile trace was not replayed");
	CHECK(mock.fd[0].x == 0 && mock.fd[0].y == 470 && mock.fd[0].width == 640 && mock.fd[0].height == 10,
			"bounds: viewport %d %d %d %d", mock.fd[0].x, mock.fd[0].y, mock.fd[0].width, mock.fd[0].height);
	CHECK(Bits(mock.fd[0].fov_x) == Bits(1.0f) && Bits(mock.fd[0].fov_y) == Bits(90.0f),
			"bounds: fov %g %g", mock.fd[0].fov_x, mock.fd[0].fov_y);
	CHECK(Bits(mock.fd[0].vieworg[0]) == Bits(0.0f) &&
			Bits(mock.fd[0].viewangles[1]) == Bits(REFREC_REPLAY_MAX_COORD) &&
			Bits(mock.fd[0].blend[0]) == Bits(1.0f) && Bits(mock.fd[0].time) == Bits(0.0f),
			"bounds: view floats");
	CHECK(mock.fd[0].rdflags == REFREC_REPLAY_RDFLAGS, "bounds: rdflags %d", mock.fd[0].rdflags);
	CHECK(Bits(mock.fd[0].lightstyles[3].white) == Bits(REFREC_REPLAY_MAX_LIGHT) &&
			Bits(mock.fd[0].lightstyles[3].rgb[0]) == Bits(0.0f), "bounds: lightstyle");
	CHECK(mock.ents[0][0].frame == 0 && mock.ents[0][0].oldframe == 0 && mock.ents[0][0].skinnum == 0 &&
			Bits(mock.ents[0][0].alpha) == Bits(1.0f) && Bits(mock.ents[0][0].backlerp) == Bits(0.0f) &&
			mock.ents[0][0].lightstyle == MAX_LIGHTSTYLES - 1 && Bits(mock.ents[0][0].origin[1]) == Bits(0.0f),
			"bounds: entity frame %d oldframe %d skinnum %d lightstyle %d", mock.ents[0][0].frame,
			mock.ents[0][0].oldframe, mock.ents[0][0].skinnum, mock.ents[0][0].lightstyle);
	CHECK(mock.ents[0][1].skinnum == 0xd3 && mock.ents[0][1].frame == 4, "bounds: beam skinnum %d",
			mock.ents[0][1].skinnum);
	CHECK(mock.ents[0][2].skinnum == 0, "bounds: skinnum 40 became %d", mock.ents[0][2].skinnum);
	CHECK(mock.ents[0][2].flags == RF_GLOW && mock.ents[0][1].flags == RF_BEAM,
			"bounds: entity flags %d %d", mock.ents[0][2].flags, mock.ents[0][1].flags);
	CHECK(mock.parts[0][0].color == (999 & 0xff) && mock.parts[0][1].color == 0xff &&
			Bits(mock.parts[0][0].alpha) == Bits(1.0f), "bounds: particle colours %d %d",
			mock.parts[0][0].color, mock.parts[0][1].color);
	CHECK(Bits(mock.dl[0].color[0]) == Bits(REFREC_REPLAY_MAX_LIGHT) && Bits(mock.dl[0].intensity) == Bits(0.0f),
			"bounds: dlight");
	CHECK(mock.last[REC_STRETCHPIC].type == 0 && mock.last[REC_PICSCALED].type == 0 && st.dropped == 3,
			"bounds: a draw outside the frame or at a hostile scale forwarded (%u dropped)", st.dropped);
	CHECK(mock.last[REC_FILL].ints[4] == (300 & 0xff), "bounds: fill colour %d", mock.last[REC_FILL].ints[4]);
	CHECK(mock.last[REC_CHARSCALED].ints[2] == 0xff && Bits(mock.last[REC_CHARSCALED].floats[0]) == Bits(1.0f),
			"bounds: char %d", mock.last[REC_CHARSCALED].ints[2]);
	CHECK(Bits(mock.last[REC_PICSCALEDCOL].floats[0]) == Bits(2.0f) &&
			Bits(mock.last[REC_PICSCALEDCOL].floats[1]) == Bits(1.0f) &&
			Bits(mock.last[REC_PICSCALEDCOL].floats[2]) == Bits(0.0f) &&
			Bits(mock.last[REC_PICSCALEDCOL].floats[3]) == Bits(0.5f), "bounds: pic colour");
	CHECK(Bits(mock.last[REC_SKY].floats[0]) == Bits(0.0f) && Bits(mock.last[REC_BEGINFRAME].floats[0]) == Bits(0.0f),
			"bounds: sky rotate or camera separation");
	CHECK(st.picsize_mismatch == 1, "bounds: %u pic size mismatches", st.picsize_mismatch);
	CHECK(st.clamped == 31, "bounds: %u values counted as clamped, not 31", st.clamped);
}

/* a trace recorded by fn, then patched by fix at its file offsets, replayed */
static qboolean
ReplayPatched(const char *sub, void (*fix)(uint8_t *buf, long size), refrec_replay_stats_t *st)
{
	const char *path = TracePath(sub);
	long size;
	uint8_t *buf;
	qboolean opened;

	RecordReplayTrace(path);
	buf = Slurp(path, &size);

	if (!buf)
	{
		CHECK(0, "%s: cannot read the trace back", sub);
		return false;
	}

	fix(buf, size);
	Spill(path, buf, size);
	free(buf);
	opened = Replay(path, API_VERSION, 640, 480, st);

	return opened;
}

static uint32_t
FrameStart(const uint8_t *buf, uint32_t i)
{
	return GetU32(buf + GetU32(buf + 24) + i * REFREC_INDEX_SIZE + 4);
}

/* entity 0 of frame 0: the REC_RENDERFRAME after the 12-byte REC_BEGINFRAME, past the refdef scalars */
#define ENT0(buf)	((buf) + FrameStart(buf, 0) + 12 + REFREC_REC_SIZE + 4216)

static void FixModelKind(uint8_t *b, long n) { (void)n; SetU32(ENT0(b), 4); }	/* the skin's id as a model */
static void FixSkinKind(uint8_t *b, long n) { (void)n; SetU32(ENT0(b) + 4, 3); }	/* the model's id as a skin */
static void FixIdPast(uint8_t *b, long n) { (void)n; SetU32(ENT0(b), 9999); }
static void FixTableKind(uint8_t *b, long n) { (void)n; SetU32(b + GetU32(b + 20) + 4, REFREC_KIND_MODEL); }	/* conchars, a pic */
static void FixIndexOff(uint8_t *b, long n) { (void)n; SetU32(b + GetU32(b + 24) + 4, FrameStart(b, 0) + 12); }
static void FixIndexMid(uint8_t *b, long n) { (void)n; SetU32(b + GetU32(b + 24) + 4, FrameStart(b, 0) + 4); }
static void FixIndexOrder(uint8_t *b, long n) { (void)n; SetU32(b + GetU32(b + 24) + REFREC_INDEX_SIZE + 4, FrameStart(b, 0)); }
static void FixRecLen(uint8_t *b, long n) { (void)n; SetU32(ENT0(b) - 4216 - 4, 0x7ffffff0u); }
static void FixRecLenEnd(uint8_t *b, long n) { (void)n; SetU32(b + REFREC_HEADER_SIZE + 4, GetU32(b + 20) - REFREC_HEADER_SIZE); }
static void FixUnfinished(uint8_t *b, long n) { (void)n; SetU32(b + 20, 0); }
static void FixTableBig(uint8_t *b, long n) { (void)n; SetU32(b + 20, REFREC_HEADER_SIZE); }

static uint32_t
CaptureAt(const uint8_t *b, uint32_t frame)
{
	uint32_t at = FrameStart(b, frame);

	while (GetU32(b + at) != REC_CAPTURE)
	{
		at += REFREC_REC_SIZE + GetU32(b + at + 4);
	}

	return at;
}

static void FixCapture(uint8_t *b, long n) { (void)n; SetU32(b + CaptureAt(b, 2) + REFREC_REC_SIZE, 5); }
/* the first record of type at or after indexed frame's REC_BEGINFRAME */
static uint32_t
RecordAt(const uint8_t *b, uint32_t frame, uint32_t type)
{
	uint32_t at = FrameStart(b, frame);

	while ((GetU32(b + at) & 0xffff) != type)
	{
		at += REFREC_REC_SIZE + GetU32(b + at + 4);
	}

	return at;
}

static void FixIndexPast(uint8_t *b, long n) { (void)n; SetU32(b + GetU32(b + 24) + 2 * REFREC_INDEX_SIZE + 4, GetU32(b + 20)); }	/* frame 2 in the string table */
static void FixNoEnd(uint8_t *b, long n) { (void)n; SetU32(b + RecordAt(b, 2, REC_ENDFRAME), REC_ENDWORLD); }	/* frame 2 never ends */

/* n zero bytes inserted at offset at (a table/index gap when shift moves the index) */
static qboolean
ReplayGrown(const char *sub, uint32_t at_field, qboolean shift, refrec_replay_stats_t *st)
{
	const char *path = TracePath(sub);
	long size;
	uint8_t *buf, *grown;
	uint32_t at;

	RecordReplayTrace(path);
	buf = Slurp(path, &size);
	grown = buf ? calloc(size + 4, 1) : NULL;

	if (!grown)
	{
		free(buf);
		CHECK(0, "%s: cannot read the trace back", sub);
		return false;
	}

	at = at_field ? GetU32(buf + at_field) : (uint32_t)size;
	memcpy(grown, buf, at);
	memcpy(grown + at + 4, buf + at, size - at);

	if (shift)
	{
		SetU32(grown + 24, GetU32(grown + 24) + 4);
	}

	Spill(path, grown, size + 4);
	free(grown);
	free(buf);

	return Replay(path, API_VERSION, 640, 480, st);
}

/* a 4-byte REC_CAPTURE turned into a REC_BEGINFRAME inside frame 0 */
static void FixNested(uint8_t *b, long n) { (void)n; SetU32(b + CaptureAt(b, 0), REC_BEGINFRAME); }

/*
 * The next byte a sweep of a RecordReplayTrace file takes: every byte,
 * except inside the 4096 bytes of lightstyles of each REC_RENDERFRAME,
 * where every 16th (the first byte of each entry) stands for the rest.
 */
static long
SweepNext(const uint8_t *buf, long i)
{
	uint32_t f;

	for (f = 0; f < 3; f++)
	{
		long ls = FrameStart(buf, f) + 12 + REFREC_REC_SIZE + 108;

		if (i >= ls && i + 16 < ls + 4096)
		{
			return i + 16;
		}
	}

	return i + 1;
}

/* Damaged traces: refused at open or at the record, never read past. */
static void
TestReplayRefusals(void)
{
	refrec_replay_stats_t st;
	const char *path;
	long size, i;
	uint8_t *buf, *copy;
	uint32_t opened = 0, failed = 0, completed = 0;

	CHECK(ReplayPatched("rp-model", FixModelKind, &st) && !st.failed && mock.ents[0][0].model == NULL,
			"refusals: a skin id resolved as a model");
	CHECK(ReplayPatched("rp-skin", FixSkinKind, &st) && !st.failed && mock.ents[0][0].skin == NULL,
			"refusals: a model id resolved as a skin");
	CHECK(ReplayPatched("rp-past", FixIdPast, &st) && !st.failed && mock.ents[0][0].model == NULL,
			"refusals: an id past the string table resolved");
	CHECK(ReplayPatched("rp-kind", FixTableKind, &st) && st.failed && st.frames == 0,
			"refusals: a registration of another kind than its table entry was accepted");
	CHECK(ReplayPatched("rp-index", FixIndexOff, &st) && st.failed && st.frames == 0,
			"refusals: a frame index entry naming a REC_RENDERFRAME was accepted");
	CHECK(ReplayPatched("rp-mid", FixIndexMid, &st) && st.failed && st.frames == 0 && mock.nfd == 0,
			"refusals: a frame index entry inside a record was accepted");
	CHECK(!ReplayPatched("rp-order", FixIndexOrder, &st) && st.failed,
			"refusals: a frame index out of order was accepted");
	CHECK(ReplayPatched("rp-len", FixRecLen, &st) && st.failed && st.frames == 0 && mock.nfd == 0,
			"refusals: an oversized record length was accepted");
	CHECK(ReplayPatched("rp-lenend", FixRecLenEnd, &st) && st.failed && st.frames == 0 &&
			strstr(last_print, "record length"),
			"refusals: a record running into the string table was accepted");
	CHECK(!ReplayPatched("rp-unfinished", FixUnfinished, &st) && st.failed,
			"refusals: an unfinished trace was accepted");
	CHECK(!ReplayPatched("rp-tablebig", FixTableBig, &st) && st.failed,
			"refusals: a string table over the records was accepted");
	CHECK(!ReplayPatched("rp-indexpast", FixIndexPast, &st) && st.failed,
			"refusals: a frame index entry in the string table was accepted");
	CHECK(ReplayPatched("rp-noend", FixNoEnd, &st) && st.failed && st.frames == 2 &&
			strstr(last_print, "records end"), "refusals: a frame that never ends (%u frames)", st.frames);
	CHECK(!ReplayGrown("rp-trailing", 0, false, &st) && st.failed, "refusals: bytes after the frame index were accepted");
	CHECK(!ReplayGrown("rp-gap", 24, true, &st) && st.failed,
			"refusals: bytes between the string table and the frame index were accepted");
	CHECK(ReplayPatched("rp-nested", FixNested, &st) && st.failed && st.frames == 0,
			"refusals: a REC_BEGINFRAME inside an indexed frame was accepted (%u frames)", st.frames);
	CHECK(ReplayPatched("rp-capture", FixCapture, &st) && st.failed && st.frames == 2 && st.captures == 1,
			"refusals: a capture out of order was accepted (%u frames, %u captures)", st.frames, st.captures);

	/* every truncation is refused at open; every single-byte change ends cleanly */
	path = TracePath("rp-sweep");
	RecordReplayTrace(path);
	buf = Slurp(path, &size);
	copy = buf ? malloc(size) : NULL;

	if (!copy)
	{
		CHECK(0, "refusals: cannot read the sweep trace");
		free(buf);
		return;
	}

	for (i = 0; i < size; i = SweepNext(buf, i))
	{
		Spill(path, buf, i);

		if (Replay(path, API_VERSION, 640, 480, &st))
		{
			CHECK(0, "refusals: a trace cut to %ld of %ld bytes was opened", i, size);
			break;
		}
	}

	for (i = 0; i < size; i = SweepNext(buf, i))
	{
		memcpy(copy, buf, size);
		copy[i] ^= 0xa5;
		Spill(path, copy, size);

		if (Replay(path, API_VERSION, 640, 480, &st))
		{
			opened++;
			failed += st.failed;
			completed += !st.failed && st.frames == 3;
			CHECK(st.frames <= 3 && st.captures <= 2, "refusals: byte %ld gave %u frames", i, st.frames);
		}
	}

	/* the sweep reaches the records: some changes open and are refused there, some replay */
	CHECK(opened > 0 && failed > 0 && completed > 0, "refusals: sweep opened %u, failed %u, completed %u",
			opened, failed, completed);
	free(copy);
	free(buf);
}

/* ---- the validation layer: only values the live client could hand a renderer ---- */

#define VAL_W	640
#define VAL_H	480

/* one frame holding the one draw t, replayed; whether the draw reached the mock */
static qboolean
Replay2D(refrec_type_t t, const int32_t *n, int nn, const float *f, int nf, const char *name,
		const void *data, uint32_t datalen, refrec_replay_stats_t *st)
{
	const char *path = TracePath("replay2d");

	RefRec_StartRecord(path, VAL_W, VAL_H, 30);
	RefRec_Control(REC_BEGINFRAME, 0);
	RefRec_Draw(t, n, nn, f, nf, name, data, datalen);
	RefRec_Control(REC_ENDFRAME, 0);
	RefRec_StopRecord();

	if (!Replay(path, API_VERSION, VAL_W, VAL_H, st) || st->failed || st->frames != 1)
	{
		CHECK(0, "2d: a one-draw trace of type %d was refused", t);
		return false;
	}

	CHECK(st->dropped + (mock.last[t].type == t) == 1, "2d: type %d both dropped and forwarded", t);

	return mock.last[t].type == t;
}

/* a rectangle draw at x, y, w, h */
static qboolean
Rect2D(refrec_type_t t, int32_t x, int32_t y, int32_t w, int32_t h, refrec_replay_stats_t *st)
{
	static uint8_t raw[16];
	int32_t n[7] = { x, y, w, h, 4, 4, 8 };

	if (t == REC_FILL)
	{
		n[4] = 7;
	}

	return Replay2D(t, n, t == REC_STRETCHRAW ? 7 : t == REC_FILL ? 5 : 4, NULL, 0, "p",
			t == REC_STRETCHRAW ? raw : NULL, t == REC_STRETCHRAW ? sizeof(raw) : 0, st);
}

static qboolean
Char2D(int32_t x, int32_t y, float scale, refrec_replay_stats_t *st)
{
	int32_t n[3] = { x, y, 'A' };

	return Replay2D(REC_CHARSCALED, n, 3, &scale, 1, NULL, NULL, 0, st);
}

static qboolean
Pic2D(refrec_type_t t, int32_t x, int32_t y, float scale, refrec_replay_stats_t *st)
{
	int32_t n[2] = { x, y };
	float f[4] = { scale, 1, 1, 1 };

	return Replay2D(t, n, 2, f, t == REC_PICSCALEDCOL ? 4 : 1, "p", NULL, 0, st);
}

/* a one-name trace of kind t, replayed; true when it was refused */
static qboolean
NameRefused(refrec_type_t t, const char *name, refrec_replay_stats_t *st)
{
	const char *path = TracePath("replayname");
	int32_t n[4] = { 0, 0, 8, 8 };
	const float f[4] = { 1, 0, 0, 1 };

	RefRec_StartRecord(path, VAL_W, VAL_H, 30);

	switch (t)
	{
		case REC_BEGINREG:
		case REC_MODEL:
		case REC_SKIN:
		case REC_PIC:
			RefRec_Registration(t, name, t == REC_BEGINREG ? NULL : &handle_a);
			break;

		default:
			RefRec_Draw(t, n, 4, f, 4, name, NULL, 0);
			break;
	}

	RefRec_Control(REC_BEGINFRAME, 0);
	RefRec_Control(REC_ENDFRAME, 0);
	RefRec_StopRecord();

	if (!Replay(path, API_VERSION, VAL_W, VAL_H, st))
	{
		CHECK(0, "names: a trace of type %d was refused at open", t);
		return false;
	}

	/* refused before the target saw the record, so nothing was forwarded */
	if (st->failed)
	{
		CHECK(mock.nlog == 0 && st->frames == 0, "names: type %d forwarded %u calls before its refusal",
				t, mock.nlog);
	}

	return st->failed;
}

/* the REC_MODEL record's name of RecordReplayTrace, one byte changed: not its table entry */
static void
FixRegName(uint8_t *b, long n)
{
	long i;

	for (i = 0; i + 12 <= n; i++)
	{
		if (memcmp(b + i, "models/a.md2", 12) == 0)
		{
			b[i + 7] = 'z';
			return;
		}
	}
}

/* entity 0's model id one past the string table */
static void FixIdEdge(uint8_t *b, long n) { (void)n; SetU32(ENT0(b), GetU32(b + GetU32(b + 20)) + 1); }

static void
TestReplayValidation(void)
{
	static const refrec_type_t named[] = {
		REC_BEGINREG, REC_MODEL, REC_SKIN, REC_PIC, REC_SKY, REC_PICSIZE,
		REC_PICSCALED, REC_PICSCALEDCOL, REC_STRETCHPIC, REC_TILECLEAR
	};
	static const refrec_type_t rects[] = { REC_STRETCHPIC, REC_STRETCHRAW };
	static const refrec_type_t pics[] = { REC_PICSCALED, REC_PICSCALEDCOL };
	char n63[64], n64[65], n1023[1024];
	refrec_replay_stats_t st;
	size_t i;

	/* B1: every forwarded name is shorter than MAX_QPATH, else the trace is refused */
	memset(n63, 'a', 63);
	n63[63] = '\0';
	memset(n64, 'a', 64);
	n64[64] = '\0';
	memset(n1023, 'Q', 1023);
	n1023[1023] = '\0';

	for (i = 0; i < sizeof(named) / sizeof(named[0]); i++)
	{
		CHECK(!NameRefused(named[i], n63, &st), "names: type %d with 63 bytes refused", named[i]);
		CHECK(NameRefused(named[i], n64, &st), "names: type %d with 64 bytes forwarded", named[i]);
		CHECK(NameRefused(named[i], n1023, &st) && strstr(last_print, "MAX_QPATH") &&
				!strstr(last_print, "QQQQ"), "names: type %d with 1023 bytes forwarded, or printed", named[i]);
	}

	/* a registration whose name is not its table entry's */
	CHECK(ReplayPatched("rp-regname", FixRegName, &st) && st.failed && st.frames == 0 &&
			strstr(last_print, "string table entry") && mock.nreg == 1,
			"names: a registration under another name than its table entry was forwarded (%u)", mock.nreg);

	/* I2: an entity id one past the table resolves to NULL (and reads nothing past it) */
	CHECK(ReplayPatched("rp-idedge", FixIdEdge, &st) && !st.failed && st.frames == 3 &&
			mock.ents[0][0].model == NULL, "handles: an id one past the string table resolved");

	/* I2: a refusal is sticky: nothing more is forwarded, however often it is asked */
	{
		refrec_header_t h;
		uint32_t nlog;

		const char *path = TracePath("rp-sticky");
		long size;
		uint8_t *buf;

		/* the first record, conchars, is refused: the rest of the trace is well formed */
		RecordReplayTrace(path);
		buf = Slurp(path, &size);

		if (buf)
		{
			FixTableKind(buf, size);
			Spill(path, buf, size);
			free(buf);
		}

		memset(&mock, 0, sizeof(mock));
		CHECK(RefRec_OpenReplay(path, &h), "sticky: refused at open");
		CHECK(!RefRec_ReplayNextFrame() && mock.nlog == 0, "sticky: the refused record was forwarded");
		nlog = mock.nlog;
		CHECK(!RefRec_ReplayNextFrame() && !RefRec_ReplayNextFrame() && mock.nlog == nlog,
				"sticky: %u calls forwarded after the refusal", mock.nlog - nlog);
		RefRec_CloseReplay();
	}

	/* B2: a rectangle lies wholly in the frame, with a size of 1 or more; a stretched pic may overlap the top */
	for (i = 0; i < sizeof(rects) / sizeof(rects[0]); i++)
	{
		refrec_type_t t = rects[i];
		qboolean top = t == REC_STRETCHPIC;

		CHECK(Rect2D(t, 0, 0, VAL_W, VAL_H, &st), "2d %d: the whole frame dropped", t);
		CHECK(Rect2D(t, VAL_W - 1, VAL_H - 1, 1, 1, &st), "2d %d: the last pixel dropped", t);
		CHECK(Rect2D(t, 0, VAL_H - 8, 8, 8, &st), "2d %d: on the bottom edge dropped", t);
		CHECK(!Rect2D(t, 0, VAL_H - 7, 8, 8, &st) && !Rect2D(t, 0, VAL_H - 1, 8, 8, &st) &&
				!Rect2D(t, 0, 1, VAL_W, VAL_H, &st), "2d %d: past the bottom forwarded", t);
		CHECK(Rect2D(t, 0, -(VAL_H - 1), VAL_W, VAL_H, &st) == top &&
				Rect2D(t, 0, -1, 8, 8, &st) == top && Rect2D(t, 0, -7, 8, 8, &st) == top,
				"2d %d: over the top %s", t, top ? "dropped" : "forwarded");
		CHECK(!Rect2D(t, 0, -VAL_H, VAL_W, VAL_H, &st) && !Rect2D(t, 0, -8, 8, 8, &st),
				"2d %d: wholly above forwarded", t);
		CHECK(!Rect2D(t, 0, VAL_H, 8, 8, &st), "2d %d: wholly below forwarded", t);
		CHECK(!Rect2D(t, -1, 0, 8, 8, &st), "2d %d: one column left of the frame forwarded", t);
		CHECK(!Rect2D(t, VAL_W - 7, 0, 8, 8, &st), "2d %d: one column right of the frame forwarded", t);
		CHECK(!Rect2D(t, 0, 0, 0, 8, &st), "2d %d: zero width forwarded", t);
		CHECK(!Rect2D(t, 0, 0, 8, 0, &st) && !Rect2D(t, 0, 5, 8, 0, &st), "2d %d: zero height forwarded", t);
		CHECK(!Rect2D(t, 0, 0, -8, 8, &st), "2d %d: negative width forwarded", t);
		CHECK(!Rect2D(t, 0, 0, VAL_W + 1, 8, &st), "2d %d: wider than the frame forwarded", t);
		CHECK(!Rect2D(t, 0, 0, 8, VAL_H + 1, &st), "2d %d: taller than the frame forwarded", t);
		CHECK(!Rect2D(t, INT32_MAX, 0, 8, 8, &st) && !Rect2D(t, 0, INT32_MIN, 8, 8, &st) &&
				!Rect2D(t, 8, 8, INT32_MAX, INT32_MAX, &st), "2d %d: extreme values forwarded", t);
	}

	/* Draw_Fill and Draw_TileClear are cut to the frame, as the renderers cut them */
	for (i = 0; i < 2; i++)
	{
		refrec_type_t t = i ? REC_TILECLEAR : REC_FILL;
		const int32_t *m = mock.last[t].ints;

		CHECK(Rect2D(t, 1, 2, 3, 4, &st) && m[0] == 1 && m[1] == 2 && m[2] == 3 && m[3] == 4 &&
				st.clamped == 0, "clip %d: an inside rectangle changed", t);
		CHECK(Rect2D(t, -5, -6, 10, 10, &st) && m[0] == 0 && m[1] == 0 && m[2] == 5 && m[3] == 4 &&
				st.clamped == 1, "clip %d: top left %d %d %d %d", t, m[0], m[1], m[2], m[3]);
		CHECK(Rect2D(t, VAL_W - 2, VAL_H - 3, VAL_W, INT32_MAX, &st) && m[0] == VAL_W - 2 &&
				m[1] == VAL_H - 3 && m[2] == 2 && m[3] == 3, "clip %d: bottom right %d %d %d %d", t,
				m[0], m[1], m[2], m[3]);
		CHECK(Rect2D(t, -1000000, -1000000, INT32_MAX, INT32_MAX, &st) && m[0] == 0 && m[1] == 0 &&
				m[2] == VAL_W && m[3] == VAL_H, "clip %d: extremes %d %d %d %d", t, m[0], m[1], m[2], m[3]);
		CHECK(!Rect2D(t, VAL_W, 0, 8, 8, &st) && !Rect2D(t, 0, VAL_H, 8, 8, &st) &&
				!Rect2D(t, -8, 0, 8, 8, &st) && !Rect2D(t, 0, -8, 8, 8, &st),
				"clip %d: a rectangle wholly outside forwarded", t);
		CHECK(!Rect2D(t, 0, 0, 0, 8, &st) && !Rect2D(t, 0, 0, 8, 0, &st) && !Rect2D(t, 0, 0, -1, 8, &st) &&
				!Rect2D(t, INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX, &st), "clip %d: an empty rectangle forwarded", t);
	}

	/* a character: a ceil(8 * scale) square, in the frame but for the top, scale 1 up to the bound */
	CHECK(Char2D(0, 0, 1, &st) && Char2D(VAL_W - 8, VAL_H - 8, 1, &st), "char: inside dropped");
	CHECK(Char2D(0, -7, 1, &st) && Char2D(0, -1, 1, &st), "char: over the top dropped");
	CHECK(Char2D(0, -15, 2, &st) && !Char2D(0, -16, 2, &st), "char: scale 2 over the top");
	CHECK(!Char2D(0, VAL_H - 7, 1, &st) && !Char2D(0, VAL_H - 1, 1, &st), "char: past the bottom forwarded");
	/* ref_soft checks the bottom with the integer scale and draws (int)(8 * scale) rows */
	CHECK(Char2D(0, VAL_H - 12, 1.5f, &st) && !Char2D(0, VAL_H - 11, 1.5f, &st) &&
			!Char2D(0, VAL_H - 9, 1.5f, &st), "char: scale 1.5 at the bottom");
	CHECK(Char2D(0, VAL_H - 16, 1.99f, &st) && !Char2D(0, VAL_H - 9, 1.99f, &st) &&
			!Char2D(0, VAL_H - 15, 1.99f, &st), "char: scale 1.99 at the bottom");
	CHECK(Char2D(0, -1, VAL_H / 8, &st) && !Char2D(0, 1, VAL_H / 8, &st),
			"char: over the top and past the bottom");
	CHECK(!Char2D(0, -8, 1, &st) && !Char2D(0, VAL_H, 1, &st), "char: wholly above or below forwarded");
	CHECK(!Char2D(-1, 0, 1, &st) && !Char2D(VAL_W - 7, 0, 1, &st), "char: past the side forwarded");
	CHECK(Char2D(VAL_W - 16, 0, 2, &st) && !Char2D(VAL_W - 15, 0, 2, &st), "char: scale 2 at the side");
	CHECK(Char2D(VAL_W - 12, 0, 1.5f, &st) && !Char2D(VAL_W - 11, 0, 1.5f, &st), "char: scale 1.5 at the side");
	CHECK(Char2D(0, 0, VAL_H / 8, &st) && !Char2D(0, 0, VAL_H / 8 + 1, &st), "char: taller than the frame");
	CHECK(!Char2D(0, 0, 0, &st) && !Char2D(0, 0, 0.99f, &st) && !Char2D(0, 0, -1, &st) &&
			!Char2D(0, 0, NAN, &st) && !Char2D(0, 0, INFINITY, &st), "char: a scale below 1 or not finite forwarded");
	CHECK(!Char2D(INT32_MAX, 0, 1, &st) && !Char2D(0, INT32_MIN, 1, &st), "char: extremes forwarded");

	/* a scaled pic: the target's size times the scale, cut to int, wholly in the frame */
	for (i = 0; i < sizeof(pics) / sizeof(pics[0]); i++)
	{
		refrec_type_t t = pics[i];

		mock_picw = mock_pich = 8;
		CHECK(Pic2D(t, 0, 0, 1, &st) && strcmp(mock.last[REC_PICSIZE].name, "p") == 0,
				"pic %d: inside dropped, or the size not asked for its name", t);
		CHECK(Pic2D(t, VAL_W - 16, VAL_H - 16, 2, &st), "pic %d: in the corner dropped", t);
		CHECK(!Pic2D(t, VAL_W - 15, 0, 2, &st) && !Pic2D(t, 0, VAL_H - 15, 2, &st) &&
				!Pic2D(t, VAL_W - 1, VAL_H - 1, 1, &st), "pic %d: past the side or the bottom forwarded", t);
		/* the tinted path of ref_soft's Draw_PicScaledCol writes above its buffer for y < 0 */
		CHECK(!Pic2D(t, 0, -1, 1, &st) && !Pic2D(t, 0, -(VAL_H - 1), 1, &st), "pic %d: over the top forwarded", t);
		CHECK(!Pic2D(t, -1, 0, 1, &st) && !Pic2D(t, VAL_W, 0, 1, &st), "pic %d: past the side forwarded", t);
		CHECK(!Pic2D(t, 0, -VAL_H, 1, &st) && !Pic2D(t, 0, VAL_H, 1, &st), "pic %d: above or below forwarded", t);
		CHECK(!Pic2D(t, 0, 0, 0, &st) && !Pic2D(t, 0, 0, 0.99f, &st) && !Pic2D(t, 0, 0, NAN, &st),
				"pic %d: a scale below 1 or not finite forwarded", t);

		/* 5 * 1.5 = 7.5 is drawn 7 wide, as ref_soft cuts it */
		mock_picw = mock_pich = 5;
		CHECK(Pic2D(t, VAL_W - 7, VAL_H - 7, 1.5f, &st) && !Pic2D(t, VAL_W - 6, 0, 1.5f, &st) &&
				!Pic2D(t, 0, VAL_H - 6, 1.5f, &st), "pic %d: a fractional size not cut to int", t);

		mock_picw = mock_pich = 1;
		CHECK(Pic2D(t, 0, 0, REFREC_REPLAY_MAX_SCALE, &st) && !Pic2D(t, 0, 0, REFREC_REPLAY_MAX_SCALE * 1.01f, &st),
				"pic %d: the scale bound", t);

		/* a pic wider than the frame, one too large for an int, one the target lacks */
		mock_picw = VAL_W;
		CHECK(Pic2D(t, 0, VAL_H - 8, 1, &st) && !Pic2D(t, 1, 0, 1, &st), "pic %d: width and height mixed up", t);
		mock_picw = VAL_W + 1;
		CHECK(!Pic2D(t, 0, 0, 1, &st), "pic %d: wider than the frame forwarded", t);
		mock_picw = 8;
		mock_pich = INT32_MAX;
		CHECK(!Pic2D(t, 0, 0, REFREC_REPLAY_MAX_SCALE, &st), "pic %d: a size past an int forwarded", t);
		mock_pich = 8;
		{
			int32_t n[2] = { 0, 0 };
			float f[4] = { 1, 1, 1, 1 };

			CHECK(!Replay2D(t, n, 2, f, t == REC_PICSCALEDCOL ? 4 : 1, "missing", NULL, 0, &st),
					"pic %d: a pic the target lacks forwarded", t);
		}
	}

	mock_picw = mock_pich = 8;

	/* a raw image whose dimensions and data length disagree is refused by the reader */
	{
		static uint8_t raw[16];
		const char *path = TracePath("rp-raw");
		int32_t n[7] = { 0, 0, 8, 8, 4, 4, 8 };
		long size;
		uint8_t *buf, *p;

		RefRec_StartRecord(path, VAL_W, VAL_H, 30);
		RefRec_Control(REC_BEGINFRAME, 0);
		RefRec_Draw(REC_STRETCHRAW, n, 7, NULL, 0, NULL, raw, sizeof(raw));
		RefRec_Control(REC_ENDFRAME, 0);
		RefRec_StopRecord();
		buf = Slurp(path, &size);
		p = buf ? buf + RecordAt(buf, 0, REC_STRETCHRAW) : NULL;

		if (p)
		{
			SetU32(p + REFREC_REC_SIZE + 16, 5);	/* cols 5: 20 bytes, 16 carried */
			Spill(path, buf, size);
		}

		free(buf);
		CHECK(p && Replay(path, API_VERSION, VAL_W, VAL_H, &st) && st.failed &&
				mock.last[REC_STRETCHRAW].type == 0, "raw: a length the dimensions disagree with was forwarded");
	}
}

/*
 * B3: a model or skin handle never outlives a registration sequence. A
 * trace registers a model and a skin, begins registration again, and
 * draws an entity on the old ids: they resolve to NULL (the entity is
 * drawn without them), unless they were registered again; a pic used as
 * a skin keeps its handle (the renderers never free pics).
 */
static qboolean
RegTrace(const char *sub, qboolean again, refrec_replay_stats_t *st)
{
	static entity_t ents[2];
	static lightstyle_t ls[MAX_LIGHTSTYLES];
	const char *path = TracePath(sub);
	refdef_t fd;

	RefRec_StartRecord(path, VAL_W, VAL_H, 30);
	RefRec_Registration(REC_PIC, "pics/c", &handle_c);
	RefRec_Registration(REC_BEGINREG, "mapa", NULL);
	RefRec_Registration(REC_MODEL, "*1", &handle_a);
	RefRec_Registration(REC_SKIN, "skins/b.pcx", &handle_b);
	RefRec_Registration(REC_ENDREG, NULL, NULL);
	RefRec_Registration(REC_BEGINREG, "mapb", NULL);

	if (again)
	{
		RefRec_Registration(REC_MODEL, "*1", &handle_a);
		RefRec_Registration(REC_SKIN, "skins/b.pcx", &handle_b);
	}

	RefRec_Registration(REC_ENDREG, NULL, NULL);

	memset(&fd, 0, sizeof(fd));
	memset(ents, 0, sizeof(ents));
	fd.width = VAL_W;
	fd.height = VAL_H;
	fd.fov_x = 90;
	fd.fov_y = 73.74f;
	fd.lightstyles = ls;
	ents[0].model = (struct model_s *)&handle_a;
	ents[0].skin = (struct image_s *)&handle_b;
	ents[1].skin = (struct image_s *)&handle_c;
	fd.entities = ents;
	fd.num_entities = 2;

	RefRec_Control(REC_BEGINFRAME, 0);
	RefRec_RenderFrame(&fd);
	RefRec_Control(REC_ENDFRAME, 0);
	RefRec_StopRecord();

	return Replay(path, API_VERSION, VAL_W, VAL_H, st) && !st->failed && st->frames == 1 && mock.nfd == 1;
}

static void
TestReplayRegistration(void)
{
	refrec_replay_stats_t st;

	CHECK(RegTrace("rp-stale", false, &st) && st.warns == 0 && mock.ents[0][0].model == NULL &&
			mock.ents[0][0].skin == NULL && mock.ents[0][1].skin == (void *)&live_pic,
			"registration: a handle from before BeginRegistration was forwarded after it");
	CHECK(RegTrace("rp-again", true, &st) && mock.ents[0][0].model == (void *)&live_model &&
			mock.ents[0][0].skin == (void *)&live_skin && mock.ents[0][1].skin == (void *)&live_pic,
			"registration: a handle registered again was not resolved");
}

/* Only indexed frames replay; an open frame after the last one does not. */
static void
TestReplayStructure(void)
{
	const char *path = TracePath("replayopen");
	refrec_replay_stats_t st;
	int32_t fill[5] = { 1, 2, 3, 4, 5 };

	RefRec_StartRecord(path, 640, 480, 30);
	RefRec_Control(REC_BEGINFRAME, 0);	/* dropped from the index by the next one */
	RefRec_Draw(REC_FILL, fill, 5, NULL, 0, NULL, NULL, 0);
	RefRec_Control(REC_BEGINFRAME, 0);
	RefRec_Draw(REC_FILL, fill, 5, NULL, 0, NULL, NULL, 0);
	RefRec_Control(REC_ENDFRAME, 0);
	RefRec_Control(REC_BEGINFRAME, 0);	/* never closed */
	RefRec_Draw(REC_FADESCREEN, NULL, 0, NULL, 0, NULL, NULL, 0);
	RefRec_StopRecord();

	CHECK(Replay(path, API_VERSION, 640, 480, &st) && !st.failed && st.frames == 1,
			"structure: %u frames, failed %d", st.frames, st.failed);
	CHECK(mock.nlog == 5 && mock.log[0] == REC_BEGINFRAME && mock.log[2] == REC_BEGINFRAME &&
			mock.log[4] == REC_ENDFRAME, "structure: %u calls forwarded", mock.nlog);
	CHECK(mock.last[REC_FADESCREEN].type == 0, "structure: the open frame after the last indexed one was replayed");
	CHECK(st.warns == 1, "structure: %u warnings counted", st.warns);

	/* a trace without a frame replays nothing and ends */
	path = TracePath("replayempty");
	RefRec_StartRecord(path, 640, 480, 30);
	RefRec_StopRecord();
	CHECK(Replay(path, API_VERSION, 640, 480, &st) && !st.failed && st.frames == 0 && mock.nlog == 0,
			"structure: an empty trace");

	/* and with bytes after its (empty) frame index it is refused: no entry check stands in */
	{
		const uint8_t zero[4] = { 0, 0, 0, 0 };
		FILE *f = fopen(path, "ab");

		if (f)
		{
			fwrite(zero, 1, sizeof(zero), f);
			fclose(f);
		}

		CHECK(!Replay(path, API_VERSION, 640, 480, &st) && st.failed,
				"structure: an empty trace with trailing bytes was accepted");
	}
}

/* ---- determinism (TASK45): REC_CVARS and refdef_t.time ---- */

/* Which cvars a trace stores, and which a replay may set (refrecord.h). */
static void
TestCvarRules(void)
{
	const char *never[] = { REFREC_CVAR_NEVER };
	const char *yes[] = { "r_lerpmodels", "gl1_minlight", "gl3_intensity", "gl_polyblend",
		"sw_waterwarp", "vid_gamma" };
	const char *no[] = { "cl_gun", "rate", "r", "gl1", "R_lerpmodels", "xr_lerpmodels", "glx_a",
		"logfile", "name", "" };
	char longname[REFREC_MAX_STRING + 1];
	size_t i;

	for (i = 0; i < sizeof(yes) / sizeof(yes[0]); i++)
	{
		CHECK(RefRec_CvarRecorded(yes[i], 0), "cvars: %s not recorded", yes[i]);
		CHECK(RefRec_CvarRestorable(yes[i], "1", 0), "cvars: %s not restorable", yes[i]);
		CHECK(RefRec_CvarRestorable(yes[i], "1", CVAR_ARCHIVE), "cvars: archived %s not restorable", yes[i]);
		CHECK(!RefRec_CvarRestorable(yes[i], "1", -1), "cvars: %s restorable without a live cvar", yes[i]);
		CHECK(!RefRec_CvarRestorable(yes[i], "1", CVAR_NOSET), "cvars: CVAR_NOSET %s restorable", yes[i]);
		CHECK(!RefRec_CvarRestorable(yes[i], "1", CVAR_LATCH | CVAR_ARCHIVE), "cvars: CVAR_LATCH %s restorable", yes[i]);
		CHECK(!RefRec_CvarRestorable(yes[i], "1", CVAR_USERINFO), "cvars: CVAR_USERINFO %s restorable", yes[i]);
		CHECK(!RefRec_CvarRestorable(yes[i], "1", CVAR_SERVERINFO), "cvars: CVAR_SERVERINFO %s restorable", yes[i]);
	}

	for (i = 0; i < sizeof(no) / sizeof(no[0]); i++)
	{
		CHECK(!RefRec_CvarRecorded(no[i], 0), "cvars: %s recorded", no[i]);
		/* an archived cvar is recorded, but a replay sets renderer cvars only */
		CHECK(!no[i][0] || RefRec_CvarRecorded(no[i], CVAR_ARCHIVE), "cvars: archived %s not recorded", no[i]);
		CHECK(!RefRec_CvarRestorable(no[i], "1", CVAR_ARCHIVE), "cvars: %s restorable", no[i]);
		CHECK(!RefRec_CvarRestorable(no[i], "1", 0), "cvars: %s restorable", no[i]);
	}

	for (i = 0; i < sizeof(never) / sizeof(never[0]); i++)
	{
		CHECK(RefRec_CvarRecorded(never[i], CVAR_ARCHIVE), "cvars: %s not recorded", never[i]);
		CHECK(!RefRec_CvarRestorable(never[i], "1", CVAR_ARCHIVE), "cvars: %s restorable", never[i]);
		CHECK(!RefRec_CvarRestorable(never[i], "1", 0), "cvars: %s restorable", never[i]);
	}

	CHECK(!RefRec_CvarRecorded(NULL, CVAR_ARCHIVE), "cvars: a NULL name recorded");
	CHECK(!RefRec_CvarRestorable(NULL, "1", 0), "cvars: a NULL name restorable");
	CHECK(!RefRec_CvarRestorable("r_a", NULL, 0), "cvars: a NULL value restorable");
	CHECK(RefRec_CvarRestorable("r_a", "", 0), "cvars: an empty value not restorable");
	CHECK(RefRec_CvarRestorable("gl_texturemode", "GL_LINEAR_MIPMAP_NEAREST", 0), "cvars: a mode name not restorable");
	CHECK(RefRec_CvarRestorable("r_lerp_list", "a b ~!#$%&'()*+,-./:;<=>?@[\\]^_`{|}", 0),
			"cvars: printable ASCII not restorable");
	CHECK(RefRec_CvarRestorable("r_A_0", "1", 0), "cvars: upper case and digits not restorable");

	/* names: [A-Za-z0-9_] only; values: printable ASCII, no '"' */
	{
		const char *badnames[] = { "r_a;quit", "r_a b", "r_a\"", "r_a\n", "r_a.b", "r_a/b", "r_\x7f", "r_\xc3\xa9" };
		const char *badvalues[] = { "a\"b", "a\nb", "a\rb", "\t", "\x7f", "\x1b[0m", "\xc3\xa9", "\x01" };

		for (i = 0; i < sizeof(badnames) / sizeof(badnames[0]); i++)
		{
			CHECK(!RefRec_CvarRestorable(badnames[i], "1", 0), "cvars: bad name %u restorable", (unsigned)i);
		}

		for (i = 0; i < sizeof(badvalues) / sizeof(badvalues[0]); i++)
		{
			CHECK(!RefRec_CvarRestorable("r_a", badvalues[i], 0), "cvars: bad value %u restorable", (unsigned)i);
		}
	}

	/* a name the format cannot carry is never recorded */
	memset(longname, 'a', sizeof(longname) - 1);
	memcpy(longname, "r_", 2);
	longname[sizeof(longname) - 1] = '\0';
	CHECK(!RefRec_CvarRecorded(longname, 0), "cvars: a name of REFREC_MAX_STRING bytes recorded");
	longname[REFREC_MAX_STRING - 1] = '\0';
	CHECK(RefRec_CvarRecorded(longname, 0), "cvars: the longest name not recorded");
}

/* the live cvars of the mock: name, flags; anything else does not exist */
typedef struct {
	const char *name;
	int flags;
} mock_cvar_t;

static const mock_cvar_t mock_cvars[] = {
	{ "r_lerpmodels", CVAR_ARCHIVE },
	{ "gl_polyblend", CVAR_ARCHIVE },
	{ "sw_waterwarp", 0 },
	{ "r_drawworld", 0 },
	{ "r_noset", CVAR_NOSET },	/* recorded as a plain renderer cvar */
	{ "vid_renderer", CVAR_ARCHIVE },
	{ "cl_gun", CVAR_ARCHIVE },
	{ "logfile", CVAR_ARCHIVE },
	{ "hand", CVAR_ARCHIVE | CVAR_USERINFO },
	{ "r_badvalue", 0 },
};

static char mock_setlog[1024];
static uint32_t mock_nset, mock_set_at_log;

static int
MockCvarFlags(const char *name)
{
	size_t i;

	for (i = 0; i < sizeof(mock_cvars) / sizeof(mock_cvars[0]); i++)
	{
		if (strcmp(name, mock_cvars[i].name) == 0)
		{
			return mock_cvars[i].flags;
		}
	}

	return -1;
}

static void
MockSetCvar(const char *name, const char *value)
{
	size_t len = strlen(mock_setlog);

	snprintf(mock_setlog + len, sizeof(mock_setlog) - len, "%s=%s;", name, value);
	mock_nset++;
	mock_set_at_log = mock.nlog;	/* the calls forwarded before it */
}

typedef struct {
	uint32_t times[8], ntimes;
} times_ctx_t;

static void
TimesCb(refrec_type_t type, const refrec_call_t *c, const refrec_frame_t *f, void *vctx)
{
	times_ctx_t *x = vctx;

	(void)c;

	if (type == REC_RENDERFRAME && x->ntimes < 8)
	{
		x->times[x->ntimes++] = Bits(f->refdef.time);
	}
}

/*
 * The REC_CVARS snapshot and its restore, end to end through a trace, and
 * refdef_t.time bit-exact from the recorder's input to the renderer's.
 */
static void
TestRecordReplayCvars(void)
{
	static lightstyle_t ls[MAX_LIGHTSTYLES];
	static char longvalue[REFREC_MAX_STRING + 8];
	/* the times a live client hands the renderer (cl.time * 0.001), and edges */
	const float times[] = { 0.001f * 12345, 1.0f / 3.0f, 0x1p-140f, -0.0f, 9999999.0f, 0.0f };
	const int nt = (int)(sizeof(times) / sizeof(times[0]));
	const char *names[] = { "r_lerpmodels", "cl_gun", "cl_nothing", "gl_polyblend", "r_noset",
		"vid_renderer", "logfile", "hand", "r_absent", "sw_waterwarp", "r_long", "r_badvalue",
		"r_drawworld", NULL, "" };
	const char *values[] = { "0", "2", "1", "0", "5", "gl3", "1", "1", "1", "1", longvalue,
		"a\"b", "1", "1", "1" };
	const int flags[] = { CVAR_ARCHIVE, CVAR_ARCHIVE, 0, CVAR_ARCHIVE, 0, CVAR_ARCHIVE, CVAR_ARCHIVE,
		CVAR_ARCHIVE | CVAR_USERINFO, 0, 0, 0, 0, 0, CVAR_ARCHIVE, CVAR_ARCHIVE };
	const int n = (int)(sizeof(names) / sizeof(names[0]));
	char path[1200];
	refrec_replay_stats_t st;
	refrec_target_t tg;
	refrec_header_t h;
	times_ctx_t x;
	tracefile_t t;
	refdef_t fd;
	int i, cap = 0;

	memset(longvalue, 'v', REFREC_MAX_STRING);
	longvalue[REFREC_MAX_STRING] = '\0';
	snprintf(path, sizeof(path), "%s", TracePath("cvars"));

	RefRec_StartRecord(path, 640, 480, 1);
	RefRec_Cvars(n, names, values, flags);
	RefRec_Registration(REC_BEGINREG, "base1", NULL);
	RefRec_Registration(REC_ENDREG, NULL, NULL);

	for (i = 0; i < nt; i++)
	{
		memset(&fd, 0, sizeof(fd));
		fd.width = 640;
		fd.height = 480;
		fd.fov_x = fd.fov_y = 90;
		fd.time = times[i];
		fd.lightstyles = ls;
		RefRec_Control(REC_BEGINFRAME, 0);
		RefRec_RenderFrame(&fd);
		RefRec_Draw(REC_CAPTURE, &cap, 1, NULL, 0, NULL, NULL, 0);
		cap++;
		RefRec_Control(REC_ENDFRAME, 0);
	}

	RefRec_StopRecord();

	memset(&x, 0, sizeof(x));

	if (!ReadTrace(path, &t, TimesCb, &x, NULL))
	{
		CHECK(0, "cvars: trace refused: %s", t.err);
		return;
	}

	/* the long value and the NULL and empty names are left out, with one warning */
	CHECK(t.types[REC_CVARS] == 1 && t.warns[REFREC_WARN_NAME] == 1,
			"cvars: %u REC_CVARS, %u name warnings", t.types[REC_CVARS], t.warns[REFREC_WARN_NAME]);
	CHECK(x.ntimes == (uint32_t)nt, "time: %u frames recorded", x.ntimes);

	for (i = 0; i < nt && i < (int)x.ntimes; i++)
	{
		CHECK(x.times[i] == Bits(times[i]), "time: frame %d recorded as %08x, live %08x", i, x.times[i], Bits(times[i]));
	}

	/* the replay restores the renderer cvars the live flags allow, before registration */
	memset(&mock, 0, sizeof(mock));
	mock_setlog[0] = '\0';
	mock_nset = 0;
	memset(&tg, 0, sizeof(tg));
	tg.api_version = API_VERSION;
	tg.width = 640;
	tg.height = 480;
	tg.Register = MockRegister;
	tg.RenderFrame = MockRenderFrame;
	tg.Call = MockCall;
	tg.CvarFlags = MockCvarFlags;
	tg.SetCvar = MockSetCvar;
	RefRec_SetReplayTarget(&tg);
	CHECK(RefRec_OpenReplay(path, &h), "cvars: replay refused");

	while (RefRec_ReplayNextFrame())
	{
	}

	RefRec_ReplayStats(&st);
	RefRec_CloseReplay();

	CHECK(!st.failed && st.frames == (uint32_t)nt, "cvars: replay failed, %u frames", st.frames);
	CHECK(strcmp(mock_setlog, "r_lerpmodels=0;gl_polyblend=0;sw_waterwarp=1;r_drawworld=1;") == 0,
			"cvars: restored '%s'", mock_setlog);
	/* recorded: r_lerpmodels cl_gun gl_polyblend r_noset vid_renderer logfile hand r_absent
	   sw_waterwarp r_badvalue r_drawworld (11); set 4 */
	CHECK(st.cvars_set == 4 && st.cvars_skipped == 7, "cvars: %u set, %u skipped", st.cvars_set, st.cvars_skipped);
	CHECK(mock_nset == 4 && mock_set_at_log == 0 && mock.nlog > 0 && mock.log[0] == REC_BEGINREG,
			"cvars: restored after %u forwarded calls, first %d", mock_set_at_log, mock.log[0]);

	for (i = 0; i < nt && i < MOCK_FRAMES; i++)
	{
		CHECK(Bits(mock.fd[i].time) == Bits(times[i]), "time: frame %d replayed as %08x, live %08x",
				i, Bits(mock.fd[i].time), Bits(times[i]));
	}

	CHECK(st.clamped == 0, "time: %u values bounded in range", st.clamped);

	/* without the cvar functions a target restores nothing, and still replays */
	tg.CvarFlags = NULL;
	mock_setlog[0] = '\0';
	RefRec_SetReplayTarget(&tg);
	CHECK(RefRec_OpenReplay(path, &h), "cvars: replay without CvarFlags refused");

	while (RefRec_ReplayNextFrame())
	{
	}

	RefRec_ReplayStats(&st);
	RefRec_CloseReplay();
	CHECK(!st.failed && st.cvars_set == 0 && st.cvars_skipped == 11 && !mock_setlog[0],
			"cvars: without CvarFlags %u set, %u skipped", st.cvars_set, st.cvars_skipped);
}

/* REC_CVARS count limits on the recorder side: past REFREC_MAX_CVARS is left out with a warning */
static void
TestRecordCvarsLimit(void)
{
	static const char *names[REFREC_MAX_CVARS + 2], *values[REFREC_MAX_CVARS + 2];
	static int flags[REFREC_MAX_CVARS + 2];
	static char store[REFREC_MAX_CVARS + 2][16];
	tracefile_t t;
	const char *path = TracePath("cvarslimit");
	int i;

	for (i = 0; i < REFREC_MAX_CVARS + 2; i++)
	{
		snprintf(store[i], sizeof(store[i]), "r_c%d", i);
		names[i] = store[i];
		values[i] = "1";
		flags[i] = 0;
	}

	RefRec_StartRecord(path, 640, 480, 1);
	RefRec_Cvars(REFREC_MAX_CVARS + 2, names, values, flags);
	RefRec_Cvars(-1, names, values, flags);
	RefRec_Cvars(1, NULL, values, flags);
	RefRec_StopRecord();

	CHECK(ReadTrace(path, &t, NULL, NULL, NULL), "cvars limit: trace refused: %s", t.err);
	/* the full one with a count warning; a negative count and NULL arrays are an empty record */
	CHECK(t.types[REC_CVARS] == 3 && t.warns[REFREC_WARN_COUNT] == 1,
			"cvars limit: %u REC_CVARS, %u count warnings", t.types[REC_CVARS], t.warns[REFREC_WARN_COUNT]);
}

int
main(int argc, char **argv)
{
	if (argc >= 3 && strcmp(argv[1], "--read") == 0)
	{
		return ReadMode(argv[2], argc >= 4 && strcmp(argv[3], "--names") == 0, stdout);
	}

	if (argc != 2 || strlen(argv[1]) >= sizeof(test_dir))
	{
		printf("usage: refrecord-test <scratch dir> | --read <trace.q2r> [--names]\n");
		return 2;
	}

	snprintf(test_dir, sizeof(test_dir), "%s", argv[1]);
	RefRec_SetPrint(TestPrint);

	TestCalls();
	TestPaletteExact();
	TestWriteRefusals();
	TestLayout();
	TestFrames();
	TestFramePin();
	TestStaleAreabits();
	TestCvars();
	TestStream();
	TestRecorderOff();
	TestRecordHandles();
	TestRecordFrameCopy();
	TestRecordFrameClamp();
	TestRecordDraws();
	TestRecordLimit();
	TestRecordWriteError();
	TestRecordHandlesFull();
	TestRecordHandlesFullKinds();
	TestRecordStopSweep();
	TestReadEscapes();
	TestTableReaders();
	TestReplayRoundTrip();
	TestReplayBounds();
	TestReplayRefusals();
	TestReplayValidation();
	TestReplayRegistration();
	TestReplayStructure();
	TestCvarRules();
	TestRecordReplayCvars();
	TestRecordCvarsLimit();

	printf("refrecord: %d checks, %d failed\n", checks, failures);

	return failures ? 1 : 0;
}
