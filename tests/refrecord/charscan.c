/*
 * tests/refrecord/charscan.c -- built by tests/check-pin-chars.sh against
 * the real src/client/vid/refrecord.c (FIX157).
 *
 * Reads a pin run's trace.q2r (the renderer-API record format) and counts
 * the Draw_CharScaled calls (REC_CHARSCALED) in each frame of the demo.
 * A demo frame is a frame from the first one that renders the 3D view
 * (REC_RENDERFRAME) or draws a raw cinematic picture (REC_STRETCHRAW) to
 * the last such one; the loading console's frames before it and the
 * closing frames after it are not part of any pin.
 *
 *   charscan <trace>          one line "pin <k> <count>" for every demo
 *                             frame that the pin run captured (REC_CAPTURE
 *                             number k, from 0) and that holds a character
 *                             draw, then
 *                             "charscan: demo frames <first>..<last>, <k> with text, <t> characters, <p> captured frames with text"
 *   charscan --make <trace> <demo frames> <chars> <at>
 *                             writes a small trace for the check's
 *                             selftest: 2 console frames and 1 closing
 *                             frame with text, <demo frames> demo frames,
 *                             of which demo frame <at> (from 0) draws
 *                             <chars> characters; every 3rd demo frame
 *                             from the first is captured
 *
 *   charscan --make-table <trace> <table>
 *                             writes a trace whose captured demo frames
 *                             draw exactly what the table's "pin <k> <n>"
 *                             lines declare (the selftest's stub engine)
 *
 * A trace with REFREC_FLAG_TRUNCATED set is refused: the recorder stopped
 * at its limit, as replay refuses it.
 *
 * Exits 0 on a trace read to its end, 1 when it is refused.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/vid/header/refrecord.h"

static int
Scan(const char *path)
{
	static refrec_frame_t frame;
	static refrec_call_t call;
	refrec_reader_t r, rec, cvars;
	refrec_header_t h;
	refrec_type_t type;
	uint32_t ncvars, n = 0, i, first = 0, last = 0, total = 0, with = 0, pins = 0;
	uint32_t *chars = NULL, *caps = NULL, nframes = 0, cap = 0, curcap = 0;
	uint32_t cur = 0;
	int hascap = 0;
	int seen = 0, open = 0;
	uint8_t *buf;
	long len;
	FILE *f = fopen(path, "rb");

	if (!f || fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0)
	{
		fprintf(stderr, "charscan: cannot open %s\n", path);
		return 1;
	}

	buf = malloc(len ? len : 1);

	if (!buf || fread(buf, 1, len, f) != (size_t)len)
	{
		fprintf(stderr, "charscan: cannot read %s\n", path);
		return 1;
	}

	fclose(f);
	RefRec_ReaderInit(&r, buf, len);

	if (!RefRec_ReadHeader(&r, &h) || !RefRec_CheckLayout(&h, len, NULL))
	{
		fprintf(stderr, "charscan: %s: bad header, or an unfinished trace\n", path);
		return 1;
	}

	if (h.flags & REFREC_FLAG_TRUNCATED)
	{
		fprintf(stderr, "charscan: %s: the recorder stopped at its limit (REFREC_FLAG_TRUNCATED), the trace is not whole\n", path);
		return 1;
	}

	RefRec_ReaderInit(&rec, buf, h.string_table_offset);
	rec.pos = REFREC_HEADER_SIZE;

	/* the 3D or cinematic flag of each frame goes in bit 31 of its count */
	while (rec.pos < rec.size)
	{
		if (!RefRec_ReadRecord(&rec, &type, &call, &frame, &cvars, &ncvars))
		{
			fprintf(stderr, "charscan: %s: bad record at offset %u\n", path, rec.pos);
			return 1;
		}

		if (type == REC_BEGINFRAME)
		{
			cur = 0;
			hascap = 0;
			open = 1;
		}
		else if (open && (type == REC_RENDERFRAME || type == REC_STRETCHRAW))
		{
			cur |= 0x80000000u;
		}
		else if (open && type == REC_CAPTURE)
		{
			hascap = 1;
			curcap = (uint32_t)call.ints[0];
		}
		else if (open && type == REC_CHARSCALED)
		{
			cur++;
		}
		else if (open && type == REC_ENDFRAME)
		{
			if (nframes == cap)
			{
				cap = cap ? cap * 2 : 1024;
				chars = realloc(chars, cap * sizeof(*chars));
				caps = realloc(caps, cap * sizeof(*caps));

				if (!chars || !caps)
				{
					return 1;
				}
			}

			caps[nframes] = hascap ? curcap : 0xffffffffu;
			chars[nframes++] = cur;
			open = 0;
		}
	}

	for (i = 0; i < nframes; i++)
	{
		if (chars[i] & 0x80000000u)
		{
			if (!seen)
			{
				first = i;
				seen = 1;
			}

			last = i;
		}
	}

	for (i = first; seen && i <= last; i++)
	{
		n = chars[i] & 0x7fffffffu;

		if (n)
		{
			with++;
			total += n;

			if (caps[i] != 0xffffffffu)
			{
				printf("pin %u %u\n", caps[i], n);
				pins++;
			}
		}
	}

	printf("charscan: demo frames %u..%u, %u with text, %u characters, %u captured frames with text\n",
			first, last, with, total, pins);
	free(chars);
	free(caps);
	free(buf);

	return 0;
}

static void
Frame(int render, int chars, int capture)
{
	static refdef_t fd;
	int i;

	RefRec_Control(REC_BEGINFRAME, 0);

	if (render)
	{
		memset(&fd, 0, sizeof(fd));
		RefRec_RenderFrame(&fd);
	}

	for (i = 0; i < chars; i++)
	{
		int a[3] = { i * 8, 0, 'a' };
		float scale = 1;

		RefRec_Draw(REC_CHARSCALED, a, 3, &scale, 1, NULL, NULL, 0);
	}

	if (capture >= 0)
	{
		RefRec_Draw(REC_CAPTURE, &capture, 1, NULL, 0, NULL, NULL, 0);
	}

	RefRec_Control(REC_ENDFRAME, 0);
}

static int
Make(const char *path, int demo, int chars, int at)
{
	int i;

	RefRec_StartRecord(path, 640, 480, 30);
	Frame(0, 40, -1);
	Frame(0, 40, -1);

	for (i = 0; i < demo; i++)
	{
		Frame(1, i == at ? chars : 0, i % 3 == 0 ? i / 3 : -1);
	}

	Frame(0, 8, -1);
	RefRec_StopRecord();

	return 0;
}

/* a trace that draws exactly the characters a table of "pin <k> <n>" lines declares */
static int
MakeTable(const char *path, const char *table)
{
	char line[256];
	int chars[4096] = { 0 }, k, n, top = 0, i;
	FILE *f = fopen(table, "r");

	if (!f)
	{
		return 1;
	}

	while (fgets(line, sizeof(line), f))
	{
		if (sscanf(line, "pin %d %d", &k, &n) == 2 && k >= 0 && k < 1000)
		{
			chars[k] = n;
			top = k > top ? k : top;
		}
	}

	fclose(f);
	RefRec_StartRecord(path, 640, 480, 30);
	Frame(0, 40, -1);
	Frame(0, 40, -1);

	for (i = 0; i < 3 * (top + 1); i++)
	{
		Frame(1, i % 3 == 0 ? chars[i / 3] : 0, i % 3 == 0 ? i / 3 : -1);
	}

	Frame(0, 8, -1);
	RefRec_StopRecord();

	return 0;
}

int
main(int argc, char **argv)
{
	if (argc == 4 && strcmp(argv[1], "--make-table") == 0)
	{
		return MakeTable(argv[2], argv[3]);
	}

	if (argc == 6 && strcmp(argv[1], "--make") == 0)
	{
		return Make(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
	}

	if (argc == 2)
	{
		return Scan(argv[1]);
	}

	fprintf(stderr, "usage: charscan <trace>\n");

	return 2;
}
