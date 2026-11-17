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
 * The WAV header checks (FIX20, src/client/sound/wave.c): GetWavinfo
 * returns a wavinfo_t only when every field is consistent with the file
 * length, and S_CacheSize bounds the resampled size SDL_Cache allocates.
 * Each test builds a WAV in a buffer of exactly the file's length, so a
 * read past the end is an ASan report, and feeds the real code.
 *
 * =======================================================================
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "../../src/client/header/client.h"
#include "../../src/client/sound/header/local.h"

typedef struct
{
	int rate;
	int bits;
	int channels;
	int datalen;      /* the data chunk's length word */
	int databytes;    /* bytes actually present after it */
	int cue;          /* 1: a cue chunk with a loop start */
	int loopstart;
	int skipdata;     /* 1: no data chunk at all */
	int tail;         /* bytes of a cut-short chunk header appended */
	int cut;          /* bytes removed from the end of the file */
	int listlen;      /* >0: a LIST chunk of this many bytes after the cue
	                     chunk, with the data chunk placed before the cue */
	int marklen;      /* the loop length in its "mark" entry */
} wavspec_t;

static void
put16(byte *p, int v)
{
	p[0] = v & 255;
	p[1] = (v >> 8) & 255;
}

static void
put32(byte *p, int v)
{
	put16(p, v & 0xffff);
	put16(p + 2, (v >> 16) & 0xffff);
}

static wavspec_t
stock(int bits, int channels, int rate, int frames)
{
	wavspec_t s;

	memset(&s, 0, sizeof(s));
	s.rate = rate;
	s.bits = bits;
	s.channels = channels;
	s.databytes = frames * channels * (bits / 8);
	s.datalen = s.databytes;
	s.loopstart = -1;
	return s;
}

/* The returned buffer is exactly *len bytes long. */
static byte *
build(const wavspec_t *s, int *full, int *len)
{
	int total = 12 + 24 + (s->cue ? 36 : 0) + s->listlen +
			(s->skipdata ? 0 : 8 + s->databytes) + s->tail;
	byte *buf = calloc(1, total + 8);
	byte *p = buf;
	byte *out;
	int i;

	memcpy(p, "RIFF", 4);
	put32(p + 4, total - 8);
	memcpy(p + 8, "WAVE", 4);
	p += 12;
	memcpy(p, "fmt ", 4);
	put32(p + 4, 16);
	put16(p + 8, 1);
	put16(p + 10, s->channels);
	put32(p + 12, s->rate);
	put32(p + 16, s->rate * s->channels * (s->bits / 8));
	put16(p + 20, s->channels * (s->bits / 8));
	put16(p + 22, s->bits);
	p += 24;

	for (i = 0; i < 2; i++)
	{
		int j;

		if ((i == 0) == (s->listlen > 0))
		{
			/* the data chunk goes first only with a LIST chunk */
			if (!s->skipdata)
			{
				memcpy(p, "data", 4);
				put32(p + 4, s->datalen);

				for (j = 0; j < s->databytes; j++)
				{
					p[8 + j] = (byte)(j * 7 + 3);
				}

				p += 8 + s->databytes;
			}
		}
		else
		{
			if (s->cue)
			{
				memcpy(p, "cue ", 4);
				put32(p + 4, 28);
				put32(p + 8, 1);
				put32(p + 32, s->loopstart);
				p += 36;
			}

			if (s->listlen > 0)
			{
				memcpy(p, "LIST", 4);
				put32(p + 4, s->listlen - 8);

				if (s->listlen > 28)
				{
					/* a cut entry keeps the start of the marker */
					memcpy(p + 28, "mark", (s->listlen - 28 < 4) ? s->listlen - 28 : 4);
					put32(p + 24, s->marklen);
				}

				p += s->listlen;
			}
		}
	}

	for (i = 0; i < s->tail; i++)
	{
		p[i] = (byte)"junk"[i & 3];
	}

	*full = total;
	*len = total - s->cut;
	out = malloc(*len > 0 ? *len : 1);
	memcpy(out, buf, *len > 0 ? *len : 0);
	free(buf);
	return out;
}

static wavinfo_t
parse(const wavspec_t *s)
{
	int full, len;
	byte *b = build(s, &full, &len);
	wavinfo_t info = GetWavinfo("test.wav", b, len);

	free(b);
	return info;
}

static int
accepted(wavinfo_t i)
{
	return i.samples > 0;
}

TEST(wave_stock_8bit)
{
	wavspec_t s = stock(8, 1, 22050, 1000);
	wavinfo_t i = parse(&s);

	T_EQ_INT(i.rate, 22050);
	T_EQ_INT(i.width, 1);
	T_EQ_INT(i.channels, 1);
	T_EQ_INT(i.samples, 1000);
	T_EQ_INT(i.loopstart, -1);
	T_EQ_INT(i.dataofs, 12 + 24 + 8);
}

TEST(wave_stock_16bit_stereo)
{
	wavspec_t s = stock(16, 2, 44100, 500);
	wavinfo_t i = parse(&s);

	T_EQ_INT(i.rate, 44100);
	T_EQ_INT(i.width, 2);
	T_EQ_INT(i.channels, 2);
	/* the sample count is the data length over the width, as it always was */
	T_EQ_INT(i.samples, 1000);
	T_EQ_INT(i.dataofs, 12 + 24 + 8);
}

TEST(wave_stock_cue_loop)
{
	wavspec_t s = stock(16, 1, 11025, 400);
	wavinfo_t i;

	s.cue = 1;
	s.loopstart = 100;
	i = parse(&s);
	T_EQ_INT(i.samples, 400);
	T_EQ_INT(i.loopstart, 100);
	T_EQ_INT(i.dataofs, 12 + 24 + 36 + 8);
}

TEST(wave_data_length_past_file)
{
	wavspec_t s = stock(16, 1, 22050, 100);

	s.datalen = s.databytes + 2;
	T_CHECK(!accepted(parse(&s)));
	s.datalen = INT_MAX;
	T_CHECK(!accepted(parse(&s)));
	s.datalen = -2;
	T_CHECK(!accepted(parse(&s)));
	s.datalen = s.databytes;
	T_CHECK(accepted(parse(&s)));
}

TEST(wave_dataofs_at_end)
{
	/* a data chunk whose body is empty: the offset is the end of the file */
	wavspec_t s = stock(8, 1, 22050, 0);

	T_CHECK(!accepted(parse(&s)));

	/* one byte of data: the offset is one byte before the end */
	s = stock(8, 1, 22050, 1);
	T_CHECK(accepted(parse(&s)));
}

TEST(wave_sample_count_fit)
{
	wavspec_t s = stock(16, 1, 22050, 50);

	T_CHECK(accepted(parse(&s)));
	s.datalen = s.databytes + 2;
	T_CHECK(!accepted(parse(&s)));
	/* the file cut one byte short of the length word's claim */
	s = stock(16, 1, 22050, 50);
	s.cut = 1;
	T_CHECK(!accepted(parse(&s)));
	/* stereo counts its samples the same way */
	s = stock(8, 2, 22050, 50);
	T_CHECK(accepted(parse(&s)));
	s.datalen = s.databytes + 1;
	T_CHECK(!accepted(parse(&s)));
}

TEST(wave_loopstart_bounds)
{
	wavspec_t s = stock(8, 1, 22050, 100);

	s.cue = 1;
	s.loopstart = 99;
	T_CHECK(accepted(parse(&s)));
	s.loopstart = 100;
	T_CHECK(!accepted(parse(&s)));
	s.loopstart = 101;
	T_CHECK(!accepted(parse(&s)));
	s.loopstart = -2;
	T_CHECK(!accepted(parse(&s)));
	s.loopstart = INT_MAX;
	T_CHECK(!accepted(parse(&s)));
	s.loopstart = 0;
	T_CHECK(accepted(parse(&s)));
}

TEST(wave_rate_range)
{
	wavspec_t s = stock(8, 1, 0, 10);

	s.rate = 0;
	T_CHECK(!accepted(parse(&s)));
	s.rate = -1;
	T_CHECK(!accepted(parse(&s)));
	s.rate = INT_MIN;
	T_CHECK(!accepted(parse(&s)));
	s.rate = S_WAV_RATE_MIN - 1;
	T_CHECK(!accepted(parse(&s)));
	s.rate = S_WAV_RATE_MIN;
	T_CHECK(accepted(parse(&s)));
	s.rate = S_WAV_RATE_MAX;
	T_CHECK(accepted(parse(&s)));
	s.rate = S_WAV_RATE_MAX + 1;
	T_CHECK(!accepted(parse(&s)));
	s.rate = INT_MAX;
	T_CHECK(!accepted(parse(&s)));
	/* the stock rates are inside */
	T_CHECK(S_WAV_RATE_MIN <= 11025 && S_WAV_RATE_MAX >= 44100);
}

TEST(wave_width_values)
{
	wavspec_t s = stock(8, 1, 22050, 10);

	s.bits = 0;
	T_CHECK(!accepted(parse(&s)));
	s.bits = 24;
	T_CHECK(!accepted(parse(&s)));
	s.bits = 32;
	T_CHECK(!accepted(parse(&s)));
	s.bits = 8;
	T_CHECK(accepted(parse(&s)));
	s.bits = 16;
	T_CHECK(accepted(parse(&s)));
}

TEST(wave_channel_values)
{
	wavspec_t s = stock(8, 1, 22050, 10);

	s.channels = 0;
	T_CHECK(!accepted(parse(&s)));
	s.channels = 3;
	T_CHECK(!accepted(parse(&s)));
	s.channels = -1;
	T_CHECK(!accepted(parse(&s)));
	s.channels = 1;
	T_CHECK(accepted(parse(&s)));
	s.channels = 2;
	T_CHECK(accepted(parse(&s)));
}

TEST(wave_cue_without_data)
{
	wavspec_t s = stock(8, 1, 22050, 10);
	wavinfo_t i;

	s.cue = 1;
	s.loopstart = 3;
	s.skipdata = 1;
	i = parse(&s);
	T_CHECK(!accepted(i));
	T_EQ_INT(i.dataofs, 0);
	T_EQ_INT(i.samples, 0);
}

TEST(wave_cut_chunk_header)
{
	int tail;

	/* a chunk header that ends 1 to 7 bytes after the last chunk starts */
	for (tail = 1; tail <= 7; tail++)
	{
		wavspec_t s = stock(8, 1, 22050, 10);
		wavinfo_t i;

		s.tail = tail;
		i = parse(&s);
		T_EQ_INT(i.samples, 10);
		T_EQ_INT(i.dataofs, 12 + 24 + 8);
		T_EQ_INT(i.loopstart, -1);
	}
}

TEST(wave_cut_fmt_and_cue)
{
	wavspec_t s = stock(8, 1, 22050, 10);
	int len, cut;

	/* the file ends inside the RIFF header or the fmt chunk, at every length */
	for (len = 0; len < 12 + 24; len++)
	{
		wavspec_t t = s;

		t.cut = 12 + 24 + 8 + 10 - len;
		T_CHECK(!accepted(parse(&t)));
	}

	/* the file ends inside the cue chunk */
	s.cue = 1;
	s.loopstart = 2;
	s.skipdata = 1;

	for (cut = 1; cut < 36; cut++)
	{
		wavspec_t t = s;

		t.cut = cut;
		T_CHECK(!accepted(parse(&t)));
	}
}

TEST(wave_null_and_empty)
{
	byte one = 0;

	T_CHECK(!accepted(GetWavinfo("x", NULL, 10)));
	T_CHECK(!accepted(GetWavinfo("x", &one, 0)));
	T_CHECK(!accepted(GetWavinfo("x", &one, 1)));
}

/* ---- the SDL_Cache size ---- */

/* what SDL_Cache computed before: the frame count, then an int product */
static int
old_bytes(const wavinfo_t *i, int speed)
{
	float stepscale = (float)i->rate / speed;
	int len = (int)(i->samples / stepscale);

	return (int)((unsigned)len * (unsigned)i->width * (unsigned)i->channels);
}

static wavinfo_t
cinfo(int rate, int width, int channels, int samples)
{
	wavinfo_t i;

	memset(&i, 0, sizeof(i));
	i.rate = rate;
	i.width = width;
	i.channels = channels;
	i.samples = samples;
	i.loopstart = -1;
	return i;
}

TEST(wave_cachesize_ordinary)
{
	wavinfo_t i = cinfo(22050, 2, 1, 22050);
	int frames = 0, bytes = 0, step = 0;

	T_CHECK(S_CacheSize(&i, 44100, &frames, &bytes, &step));
	T_EQ_INT(frames, 44100);
	T_EQ_INT(bytes, 44100 * 2);

	i = cinfo(44100, 1, 2, 1000);
	T_CHECK(S_CacheSize(&i, 44100, &frames, &bytes, &step));
	T_EQ_INT(frames, 1000);
	T_EQ_INT(bytes, 2000);
}

TEST(wave_cachesize_old_wrap)
{
	/* 16M samples at 1 kHz played at 48 kHz: the int product wrapped */
	wavinfo_t i = cinfo(1000, 2, 2, 16777216);
	int frames = 7, bytes = 7, step = 7;

	T_CHECK(old_bytes(&i, 48000) < 16777216 * 4);
	T_CHECK(!S_CacheSize(&i, 48000, &frames, &bytes, &step));
	T_EQ_INT(frames, 7);
	T_EQ_INT(bytes, 7);
}

TEST(wave_cachesize_limit)
{
	int frames = 0, bytes = 0, step = 0;
	wavinfo_t i = cinfo(44100, 2, 2, S_CACHE_MAX_BYTES / 4);

	T_CHECK(S_CacheSize(&i, 44100, &frames, &bytes, &step));
	T_EQ_INT(bytes, S_CACHE_MAX_BYTES);
	T_EQ_INT(frames, S_CACHE_MAX_BYTES / 4);

	/* the next count the float division can tell apart */
	i.samples = S_CACHE_MAX_BYTES / 4 + 4;
	T_CHECK(!S_CacheSize(&i, 44100, &frames, &bytes, &step));
	i.samples = INT_MAX;
	T_CHECK(!S_CacheSize(&i, 44100, &frames, &bytes, &step));
	i.width = 1;
	i.channels = 1;
	i.samples = S_CACHE_MAX_BYTES;
	T_CHECK(S_CacheSize(&i, 44100, &frames, &bytes, &step));
	T_EQ_INT(bytes, S_CACHE_MAX_BYTES);
}

TEST(wave_cachesize_refusals)
{
	int frames = 0, bytes = 0, step = 0;
	wavinfo_t i = cinfo(0, 2, 1, 100);

	T_CHECK(!S_CacheSize(&i, 44100, &frames, &bytes, &step));
	i.rate = -5;
	T_CHECK(!S_CacheSize(&i, 44100, &frames, &bytes, &step));
	i.rate = 22050;
	T_CHECK(!S_CacheSize(&i, 0, &frames, &bytes, &step));
	T_CHECK(!S_CacheSize(&i, -1, &frames, &bytes, &step));
	i.samples = 0;
	T_CHECK(!S_CacheSize(&i, 44100, &frames, &bytes, &step));
	i.samples = -3;
	T_CHECK(!S_CacheSize(&i, 44100, &frames, &bytes, &step));
	i.samples = 100;
	i.width = 0;
	T_CHECK(!S_CacheSize(&i, 44100, &frames, &bytes, &step));
	i.width = 2;
	i.channels = 3;
	T_CHECK(!S_CacheSize(&i, 44100, &frames, &bytes, &step));
	/* a rate so low against the speed that no frame is left */
	i = cinfo(1000, 2, 1, 1);
	T_CHECK(S_CacheSize(&i, 44100, &frames, &bytes, &step));
	T_EQ_INT(frames, 44);
	i = cinfo(192000, 2, 1, 1);
	T_CHECK(!S_CacheSize(&i, 44100, &frames, &bytes, &step));
}

TEST(wave_list_mark_loop)
{
	wavspec_t s = stock(16, 1, 22050, 100);
	wavinfo_t i;
	int len;

	s.cue = 1;
	s.loopstart = 10;
	s.listlen = 32;
	s.marklen = 40;
	i = parse(&s);
	/* the loop end, as the mark entry says, is the sample count */
	T_EQ_INT(i.samples, 50);
	T_EQ_INT(i.loopstart, 10);

	/* a LIST chunk one byte short of holding the mark entry: ignored */
	s.listlen = 31;
	i = parse(&s);
	T_EQ_INT(i.samples, 100);
	T_EQ_INT(i.loopstart, 10);

	/* a LIST chunk cut in its header, at each length */
	for (len = 1; len < 8; len++)
	{
		s.listlen = 8;
		s.cut = 8 - len;
		i = parse(&s);
		T_EQ_INT(i.samples, 100);
	}
}

TEST(wave_list_mark_overflow)
{
	wavspec_t s = stock(16, 1, 22050, 100);

	/* a loop end past int is not wrapped into a small count */
	s.cue = 1;
	s.loopstart = 10;
	s.listlen = 32;
	s.marklen = INT_MAX;
	/* the file is refused like any other bad one, no disconnect */
	T_CHECK(!accepted(parse(&s)));
}

/* ---- S_WavinfoValid directly ---- */

static wavinfo_t
vinfo(void)
{
	wavinfo_t i;

	memset(&i, 0, sizeof(i));
	i.rate = 22050;
	i.width = 2;
	i.channels = 1;
	i.samples = 100;
	i.loopstart = -1;
	i.dataofs = 44;
	return i;
}

TEST(wave_valid_fields)
{
	wavinfo_t i = vinfo();

	/* 44 bytes of header and 200 of data */
	T_CHECK(S_WavinfoValid(&i, 244));

	i = vinfo();
	i.width = 0;
	T_CHECK(!S_WavinfoValid(&i, 244));
	i.width = 3;
	T_CHECK(!S_WavinfoValid(&i, 244));
	i = vinfo();
	i.channels = 0;
	T_CHECK(!S_WavinfoValid(&i, 244));
	i.channels = 3;
	T_CHECK(!S_WavinfoValid(&i, 244));
	i = vinfo();
	i.samples = 0;
	T_CHECK(!S_WavinfoValid(&i, 244));
	i.samples = -1;
	T_CHECK(!S_WavinfoValid(&i, 244));
	i = vinfo();
	i.dataofs = -1;
	T_CHECK(!S_WavinfoValid(&i, 244));
	i = vinfo();
	i.dataofs = 244;
	T_CHECK(!S_WavinfoValid(&i, 244));
	i.dataofs = 243;
	i.samples = 1;
	i.width = 1;
	T_CHECK(S_WavinfoValid(&i, 244));
	i.samples = 2;
	T_CHECK(!S_WavinfoValid(&i, 244));
	/* the samples times the width does not wrap */
	i = vinfo();
	i.samples = INT_MAX;
	T_CHECK(!S_WavinfoValid(&i, 244));
	i.samples = 1 << 30;
	T_CHECK(!S_WavinfoValid(&i, INT_MAX));
	T_CHECK(!S_WavinfoValid(&i, 0));
}

TEST(wave_cachesize_one_frame)
{
	wavinfo_t i = cinfo(44100, 1, 1, 1);
	int frames = 0, bytes = 0, step = 0;

	T_CHECK(S_CacheSize(&i, 44100, &frames, &bytes, &step));
	T_EQ_INT(frames, 1);
	T_EQ_INT(bytes, 1);
}

/* ---- the resample bounds ---- */

TEST(wave_cache_last_source_index)
{
	/* a very low rate into a high speed: the last frame's source index
	   was one past the samples */
	wavinfo_t i = cinfo(1125, 2, 1, 786433);
	int frames = 0, bytes = 0, step = 0;

	T_CHECK(S_CacheSize(&i, 48000, &frames, &bytes, &step));
	T_CHECK(frames > 0);
	T_CHECK((((long long)(frames - 1) * step) >> 8) < i.samples);
	T_EQ_INT(bytes, frames * 2);
	/* one frame fewer than the unbounded count, and no more */
	T_EQ_INT(frames, (int)(((long long)i.samples * 256 - 1) / step + 1));
}

TEST(wave_cache_stock_sweep)
{
	static const int rates[] = {11025, 22050, 44100};
	static const int speeds[] = {11025, 22050, 44100, 48000};
	static const int counts[] = {1, 2, 3, 1000, 22087, 100003};
	int r, sp, w, c, n, l;

	for (r = 0; r < 3; r++)
	{
		for (sp = 0; sp < 4; sp++)
		{
			for (w = 1; w <= 2; w++)
			{
				for (c = 1; c <= 2; c++)
				{
					for (n = 0; n < 6; n++)
					{
						wavinfo_t i = cinfo(rates[r], w, c, counts[n]);
						float stepscale = (float)rates[r] / speeds[sp];
						int frames = 0, bytes = 0, step = 0;
						int starts[4];

						starts[0] = -1;
						starts[1] = 0;
						starts[2] = counts[n] / 2;
						/* a stock loop starts well before the end */
						starts[3] = (counts[n] >= 1000) ? counts[n] - 100 : 0;

						if ((int)(counts[n] / stepscale) < 1)
						{
							continue;
						}

						T_CHECK(S_CacheSize(&i, speeds[sp], &frames,
								&bytes, &step));
						T_EQ_INT(frames, (int)(counts[n] / stepscale));
						T_EQ_INT(bytes, old_bytes(&i, speeds[sp]));
						T_EQ_INT(step, (int)(stepscale * 256));

						for (l = 0; l < 4; l++)
						{
							int old = (starts[l] == -1) ? -1 :
									(int)(starts[l] / stepscale);

							i.loopstart = starts[l];

							T_EQ_INT(S_CacheLoopStart(&i, speeds[sp], frames),
									old);
						}
					}
				}
			}
		}
	}
}

TEST(wave_cache_loopstart_below_frames)
{
	wavinfo_t i = cinfo(88200, 1, 1, 11);
	int frames = 0, bytes = 0, step = 0;

	i.loopstart = 10;
	T_CHECK(S_CacheSize(&i, 44100, &frames, &bytes, &step));
	T_EQ_INT(frames, 5);
	T_EQ_INT(S_CacheLoopStart(&i, 44100, frames), frames - 1);

	/* 48000 Hz into 44100: the first failing count is 13 */
	i = cinfo(48000, 1, 1, 13);
	i.loopstart = 12;
	T_CHECK(S_CacheSize(&i, 44100, &frames, &bytes, &step));
	T_CHECK(S_CacheLoopStart(&i, 44100, frames) < frames);
	T_EQ_INT(S_CacheLoopStart(&i, 44100, frames), frames - 1);

	/* one below the clamp is kept as it was */
	i.loopstart = 6;
	T_EQ_INT(S_CacheLoopStart(&i, 44100, frames), 5);

	i.loopstart = -1;
	T_EQ_INT(S_CacheLoopStart(&i, 44100, frames), -1);
}

/* ---- S_GetStatistics reads ---- */

static void
stats(const byte *d, int n, int width, int ch, int *out)
{
	/* the data is an exact-size heap copy, so a stray read is an ASan hit */
	size_t bytes = (size_t)n * width;
	byte *copy = malloc(bytes ? bytes : 1);

	memcpy(copy, d, bytes);
	S_GetStatistics(copy, n, width, ch, 0.0, &out[0], &out[1], &out[2],
			&out[3]);
	free(copy);
}

TEST(wave_stats_last_sample_loud_16)
{
	short d[8] = {0, 0, 0, 0, 0, 0, 0, 20000};
	int o[4] = {-1, -1, -1, -1};

	stats((const byte *)d, 8, 2, 1, o);
	T_EQ_INT(o[0], 8);
	T_EQ_INT(o[2], 0);
	T_CHECK(o[1] >= 0 && o[1] <= 8);
	T_CHECK(o[3] >= 0 && o[3] <= 8);
}

TEST(wave_stats_one_sample)
{
	short d16 = 20000;
	byte d8 = 200;
	int o[4] = {-1, -1, -1, -1};

	stats((const byte *)&d16, 1, 2, 1, o);
	T_CHECK(o[0] >= 0 && o[0] <= 1);
	T_CHECK(o[1] >= 0 && o[1] <= 1);
	T_CHECK(o[2] >= 0 && o[2] <= 1);
	T_CHECK(o[3] >= 0 && o[3] <= 1);
	stats(&d8, 1, 1, 1, o);
	T_CHECK(o[0] >= 0 && o[0] <= 1);
	T_CHECK(o[1] >= 0 && o[1] <= 1);
	T_CHECK(o[2] >= 0 && o[2] <= 1);
	T_CHECK(o[3] >= 0 && o[3] <= 1);
}

TEST(wave_stats_last_sample_loud_8)
{
	byte d[8] = {128, 128, 128, 128, 128, 128, 128, 250};
	int o[4] = {-1, -1, -1, -1};

	stats(d, 8, 1, 1, o);
	T_CHECK(o[0] >= 0 && o[0] <= 8);
	T_CHECK(o[2] >= 0 && o[2] <= 8);
}

/* A stock shape: silence, a rise, a body, a decay and a tail. */
TEST(wave_stats_stock_unchanged)
{
	short d16[40];
	byte d8[40];
	int o[4], i;

	for (i = 0; i < 40; i++)
	{
		int v = (i < 5 || i >= 35) ? 100 : (i < 10) ? 8000 : (i < 25) ? 20000 : 8000;

		d16[i] = (short)((i & 1) ? -v : v);
		d8[i] = (byte)((i < 5 || i >= 35) ? 1 : (i < 10) ? 40 : (i < 25) ? 120 : 40);
	}

	stats((const byte *)d16, 40, 2, 1, o);
	/* what the scans gave before the bounds were added */
	T_EQ_INT(o[0], 6);
	T_EQ_INT(o[1], 6);
	T_EQ_INT(o[2], 5);
	T_EQ_INT(o[3], 10);
	stats(d8, 40, 1, 1, o);
	T_EQ_INT(o[0], 6);
	T_EQ_INT(o[1], 6);
	T_EQ_INT(o[2], 5);
	T_EQ_INT(o[3], 10);
}

TEST(wave_cache_step_zero)
{
	/* a rate so low against the speed that the step is zero: every frame
	   reads the first sample, and nothing divides by the step */
	wavinfo_t i = cinfo(1, 1, 1, 1);
	int frames = 0, bytes = 0, step = 7;

	T_CHECK(S_CacheSize(&i, 44100, &frames, &bytes, &step));
	T_EQ_INT(step, 0);
	T_EQ_INT(frames, 44100);
}

TEST(wave_stats_no_samples)
{
	int o[4] = {-1, -1, -1, -1};
	byte none[1] = {0};

	/* nothing to read: the outputs are zero and no byte is touched */
	stats(none, 0, 2, 1, o);
	T_EQ_INT(o[0] | o[1] | o[2] | o[3], 0);
	stats(none, 0, 1, 1, o);
	T_EQ_INT(o[0] | o[1] | o[2] | o[3], 0);
}
