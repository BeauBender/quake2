/*
 * Copyright (C) 1997-2001 Id Software, Inc.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 *
 * =======================================================================
 *
 * This file implements a subset of the WAVE audio file format.
 *
 * =======================================================================
 */

#include <limits.h>

#include "../header/client.h"
#include "header/local.h"

static byte *data_p;
static byte *iff_end;
static byte *last_chunk;
static byte *iff_data;
static int iff_chunk_len;

static short
GetLittleShort(void)
{
	short val = 0;

	val = *data_p;
	val = val + (*(data_p + 1) << 8);
	data_p += 2;
	return val;
}

static int
GetLittleLong(void)
{
	int val = 0;

	val = *data_p;
	val = val + (*(data_p + 1) << 8);
	val = val + (*(data_p + 2) << 16);
	val = val + (*(data_p + 3) << 24);
	data_p += 4;
	return val;
}

static void
FindNextChunk(char *name)
{
	while (1)
	{
		long long next;

		data_p = last_chunk;

		/* the whole 8 byte chunk header must be inside the file */
		if ((last_chunk > iff_end) || ((iff_end - last_chunk) < 8))
		{
			data_p = NULL;
			return;
		}

		data_p += 4;
		iff_chunk_len = GetLittleLong();

		if (iff_chunk_len < 0)
		{
			data_p = NULL;
			return;
		}

		data_p -= 8;

		/* the next header, in 64 bit; a length past the file ends the walk */
		next = 8 + (((long long)iff_chunk_len + 1) & ~1LL);

		if (next > (iff_end - data_p))
		{
			last_chunk = iff_end;
		}
		else
		{
			last_chunk = data_p + next;
		}

		if (!strncmp((const char *)data_p, name, 4))
		{
			return;
		}
	}
}

static void
FindChunk(char *name)
{
	last_chunk = iff_data;
	FindNextChunk(name);
}

/*
 * Returns true if n bytes from data_p are inside the file.
 */
static qboolean
HasBytes(int n)
{
	return (data_p != NULL) && ((iff_end - data_p) >= n);
}

static wavinfo_t
ParseWavinfo(char *name, byte *wav, int wavlength)
{
	wavinfo_t info;
	int format;
	int samples;

	memset(&info, 0, sizeof(info));

	if (!wav)
	{
		return info;
	}

	iff_data = wav;
	iff_end = wav + wavlength;

	/* find "RIFF" chunk */
	FindChunk("RIFF");

	if (!(HasBytes(12) && !strncmp((const char *)data_p + 8, "WAVE", 4)))
	{
		Com_Printf("Missing RIFF/WAVE chunks\n");
		return info;
	}

	/* get "fmt " chunk */
	iff_data = data_p + 12;

	FindChunk("fmt ");

	/* the header, then 16 bytes of format */
	if (!HasBytes(8 + 16))
	{
		Com_Printf("Missing fmt chunk\n");
		return info;
	}

	data_p += 8;
	format = GetLittleShort();

	if (format != 1)
	{
		Com_Printf("Microsoft PCM format only\n");
		return info;
	}

	info.channels = GetLittleShort();
	info.rate = GetLittleLong();
	data_p += 4 + 2;
	info.width = GetLittleShort() / 8;

	/* wBitPerSample assumes it's not an uncommon value such as < 8 bits */
	if (info.width <= 0)
	{
		Com_Printf("Invalid bits per sample");
		return info;
	}

	/* get cue chunk */
	FindChunk("cue ");

	if (data_p)
	{
		/* the loop start is the 4 bytes at offset 32 */
		if (!HasBytes(36))
		{
			Com_Printf("Truncated cue chunk\n");
			return info;
		}

		data_p += 32;
		info.loopstart = GetLittleLong();

		/* if the next chunk is a LIST chunk,
		   look for a cue length marker */
		FindNextChunk("LIST");

		if (data_p)
		{
			if (HasBytes(32) &&
				!strncmp((const char *)data_p + 28, "mark", 4))
			{
				int i;

				/* this is not a proper parse,
				   but it works with cooledit... */
				data_p += 24;
				i = GetLittleLong(); /* samples in loop */
				long long total = (long long)info.loopstart + i;

				/* a total outside int is not a loop length, clamp it
				   so the checks below refuse it */
				info.samples = (total > INT_MAX) ? INT_MAX :
						(total < INT_MIN) ? INT_MIN : (int)total;
			}
		}
	}
	else
	{
		info.loopstart = -1;
	}

	/* find data chunk */
	FindChunk("data");

	if (!data_p)
	{
		Com_Printf("Missing data chunk\n");
		return info;
	}

	data_p += 4;
	samples = GetLittleLong() / info.width;

	if (info.samples)
	{
		if (samples < info.samples)
		{
			/* a loop longer than the data refuses the sound */
			Com_Printf("%s: Sound %s has a bad loop length\n",
				__func__, name);
			memset(&info, 0, sizeof(info));
			return info;
		}
	}
	else
	{
		info.samples = samples;
	}

	info.dataofs = (int)(data_p - wav);

	return info;
}

wavinfo_t
GetWavinfo(char *name, byte *wav, int wavlength)
{
	wavinfo_t info = ParseWavinfo(name, wav, wavlength);

	/* nothing leaves here that a consumer cannot trust against the file */
	if (!S_WavinfoValid(&info, wavlength))
	{
		memset(&info, 0, sizeof(info));
	}

	return info;
}


qboolean
S_WavinfoValid(const wavinfo_t *info, int wavlength)
{
	long long remaining;

	if (info->rate < S_WAV_RATE_MIN || info->rate > S_WAV_RATE_MAX)
	{
		return false;
	}

	if (info->width < 1 || info->width > 2)
	{
		return false;
	}

	if (info->channels < 1 || info->channels > 2)
	{
		return false;
	}

	if (info->samples <= 0)
	{
		return false;
	}

	if (info->dataofs < 0 || info->dataofs >= wavlength)
	{
		return false;
	}

	remaining = (long long)wavlength - info->dataofs;

	if ((long long)info->samples * info->width > remaining)
	{
		return false;
	}

	if (info->loopstart < -1 || info->loopstart >= info->samples)
	{
		return false;
	}

	return true;
}

qboolean
S_CacheSize(const wavinfo_t *info, int speed, int *frames, int *bytes,
		int *step)
{
	float stepscale;
	float count;
	long long size;
	int n;

	if (info->rate <= 0 || speed <= 0 || info->samples <= 0 ||
		info->width < 1 || info->width > 2 ||
		info->channels < 1 || info->channels > 2)
	{
		return false;
	}

	stepscale = (float)info->rate / speed;
	count = info->samples / stepscale;

	/* also false for a NaN */
	if (!(count >= 1.0f && count < (float)INT_MAX))
	{
		return false;
	}

	size = (long long)(int)count * info->width * info->channels;

	if (size > S_CACHE_MAX_BYTES)
	{
		return false;
	}

	*step = (int)(stepscale * 256);
	n = (int)count;

	/* the last frame reads source sample ((n - 1) * step) >> 8, which
	   must be a sample of the sound: (n - 1) * step < samples * 256 */
	if (*step > 0)
	{
		long long most = ((long long)info->samples * 256 - 1) / *step + 1;

		if (n > most)
		{
			n = (int)most;
		}
	}

	*frames = n;
	*bytes = (int)((long long)n * info->width * info->channels);
	return true;
}

int
S_CacheLoopStart(const wavinfo_t *info, int speed, int frames)
{
	int start;

	if (info->loopstart == -1)
	{
		return -1;
	}

	start = (int)(info->loopstart / ((float)info->rate / speed));

	/* the mixer spins on a loop start at the end of the sound */
	if (start > frames - 1)
	{
		start = frames - 1;
	}

	return start;
}
