/*
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
 * =======================================================================
 *
 * Host test of the sky face loader itself: the real files/image.c linked
 * against a stub filesystem, a stub decoder and a stub load_image, run by
 * tests/check-image-decoder.sh. Where skyorder_test.c tests the decision,
 * this tests that image.c asks the right questions and acts on the answer:
 * which file is loaded, and the asset size handed to load_image.
 * No engine, no game data.
 *
 * =======================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "../../src/client/refresh/ref_shared.h"
#include "../../src/client/refresh/files/imgdecode.h"

refimport_t ri;

/* a fake file is 18 bytes: byte 0 is 71 when it decodes, then a size at 12..15 */
struct ent { const char *name; unsigned char *data; };
static struct ent fs[8];
static int nfs;
static int loads, decodes, printed, orig_w, orig_h, lcalls;
static int lw, lrw, lh, lrh;

static void
add(const char *name, int good, int w, int h)
{
	unsigned char *d = calloc(1, 18);

	d[0] = good ? 71 : 66;
	d[12] = w & 255;
	d[13] = w >> 8;
	d[14] = h & 255;
	d[15] = h >> 8;
	fs[nfs].name = name;
	fs[nfs].data = d;
	nfs++;
}

static void
reset(void)
{
	int i;

	for (i = 0; i < nfs; i++)
	{
		free(fs[i].data);
	}

	nfs = 0;
	loads = decodes = printed = orig_w = orig_h = lcalls = 0;
	lw = lrw = lh = lrh = 0;
}

static int
t_load(const char *name, void **buf)
{
	int i;

	for (i = 0; i < nfs; i++)
	{
		if (!strcmp(fs[i].name, name))
		{
			*buf = malloc(18);
			memcpy(*buf, fs[i].data, 18);
			loads++;
			return 18;
		}
	}

	*buf = NULL;
	return -1;
}

static void t_free(void *buf) { free(buf); }

imgdec_result_t
IMGDEC_Decode(const void *data, size_t size, const char *ext, byte **pic, int *w, int *h)
{
	const unsigned char *d = data;

	(void)ext;
	decodes++;

	if (size < 18 || d[0] != 71)
	{
		return IMGDEC_ERR_DECODE;
	}

	*w = d[12] | (d[13] << 8);
	*h = d[14] | (d[15] << 8);
	*pic = malloc((size_t)*w * *h * 4);
	return IMGDEC_OK;
}

const char *IMGDEC_ResultString(imgdec_result_t r) { (void)r; return "err"; }
void Com_Printf(const char *m, ...) { (void)m; printed++; }
void Com_DPrintf(const char *m, ...) { (void)m; }
void Com_Error(int c, const char *m, ...) { (void)c; (void)m; abort(); }

void
Com_sprintf(char *d, int s, const char *f, ...)
{
	va_list a;

	va_start(a, f);
	vsnprintf(d, s, f, a);
	va_end(a);
}

int Q_strlcpy(char *d, const char *s, int n) { return (int)strlcpy(d, s, n); }
int Q_strlcat(char *d, const char *s, int n) { return (int)strlcat(d, s, n); }

const char *
COM_FileExtension(const char *in)
{
	const char *e = strrchr(in, 46);

	if (!e || e == in)
	{
		return "";
	}

	return e + 1;
}

int LittleLong(int l) { return l; }
void GetPCXInfo(const char *n, int *w, int *h) { (void)n; *w = orig_w; *h = orig_h; }
void GetWalInfo(const char *n, int *w, int *h) { (void)n; *w = 0; *h = 0; }
void GetM8Info(const char *n, int *w, int *h) { (void)n; *w = 0; *h = 0; }
void GetM32Info(const char *n, int *w, int *h) { (void)n; *w = 0; *h = 0; }
void LoadPCX(const char *n, byte **p, byte **pal, int *w, int *h, int *b) { (void)n; (void)w; (void)h; (void)b; *p = NULL; if (pal) *pal = NULL; }
struct image_s *LoadWal(const char *n, imagetype_t t, loadimage_t l) { (void)n; (void)t; (void)l; return NULL; }
struct image_s *LoadM8(const char *n, imagetype_t t, loadimage_t l) { (void)n; (void)t; (void)l; return NULL; }
struct image_s *LoadM32(const char *n, imagetype_t t, loadimage_t l) { (void)n; (void)t; (void)l; return NULL; }

static struct image_s *
t_li(const char *n, byte *p, int w, int rw, int h, int rh, size_t ds, imagetype_t t, int b)
{
	(void)n; (void)p; (void)ds; (void)t; (void)b;
	lcalls++;
	lw = w; lrw = rw; lh = h; lrh = rh;
	return (struct image_s *)&lcalls;
}

static struct image_s *
t_find(const char *name, imagetype_t type)
{
	char nwe[256];
	const char *ext = COM_FileExtension(name);
	size_t len = (size_t)(ext - name) - 1;

	memcpy(nwe, name, len);
	nwe[len] = 0;
	return R_LoadImage(name, nwe, ext, type, true, t_li);
}

static int checks, failed;

/* the result: whether an image came back, the loaded size and the asset size */
static void
expect(int line, struct image_s *img, int yes, int w, int rw)
{
	int ok = (img != NULL) == yes && (!yes || (lw == w && lh == w && lrw == rw && lrh == rw));

	checks++;

	if (!ok)
	{
		failed++;
		printf("skyload_test: line %d: img %d w %d rw %d, want %d %d %d\n",
			line, img != NULL, lw, lrw, yes, w, rw);
	}
}

#define EXPECT(img, yes, w, rw) expect(__LINE__, (img), (yes), (w), (rw))

#define TGA(g, w) add("env/s_up.tga", g, w, w)
#define PNG(g, w) add("env/s_up.png", g, w, w)

static void
count(int line, const char *what, int got, int want)
{
	checks++;

	if (got != want)
	{
		failed++;
		printf("skyload_test: line %d: %s %d, want %d\n", line, what, got, want);
	}
}

#define COUNT(what, got, want) count(__LINE__, (what), (got), (want))

int
main(void)
{
	ri.FS_LoadFile = t_load;
	ri.FS_FreeFile = t_free;

	/* no PNG: as before */
	reset(); TGA(1, 256);
	EXPECT(t_find("env/s_up.tga", it_sky), 1, 256, 256);

	/* a larger PNG: the PNG, the asset size is the stock face's */
	reset(); TGA(1, 256); PNG(1, 512);
	EXPECT(t_find("env/s_up.tga", it_sky), 1, 512, 256);

	/* an equal PNG: the PNG */
	reset(); TGA(1, 256); PNG(1, 256);
	EXPECT(t_find("env/s_up.tga", it_sky), 1, 256, 256);

	/* a smaller PNG: the TGA, as before */
	reset(); TGA(1, 256); PNG(1, 128);
	EXPECT(t_find("env/s_up.tga", it_sky), 1, 256, 256);

	/* an undecodable TGA and a larger PNG: the PNG, the asset size is the header's */
	reset(); TGA(0, 256); PNG(1, 512);
	EXPECT(t_find("env/s_up.tga", it_sky), 1, 512, 256);

	/* an undecodable TGA and a smaller PNG: the TGA fails, the PNG after it, at its size */
	reset(); TGA(0, 256); PNG(1, 128);
	EXPECT(t_find("env/s_up.tga", it_sky), 1, 128, 128);

	/* no TGA: the PNG at its own size */
	reset(); PNG(1, 512);
	EXPECT(t_find("env/s_up.tga", it_sky), 1, 512, 512);

	/* an undecodable PNG: the TGA wins */
	reset(); TGA(1, 256); PNG(0, 512);
	EXPECT(t_find("env/s_up.tga", it_sky), 1, 256, 256);

	/* an undecodable PNG and no TGA: nothing, and one error per lookup */
	reset(); PNG(0, 512);
	EXPECT(t_find("env/s_up.tga", it_sky), 0, 0, 0);
	COUNT("errors printed", printed, 1);

	/* the PNG that wins is decoded once and its TGA is read once (the header) */
	reset(); TGA(1, 256); PNG(1, 512);
	t_find("env/s_up.tga", it_sky);
	COUNT("decodes", decodes, 1);
	COUNT("file reads", loads, 2);

	/* a PCX lookup: the original's size is the stock face's, whatever the .tga is */
	reset(); orig_w = orig_h = 256; TGA(1, 512); PNG(1, 1024);
	EXPECT(t_find("env/s_up.pcx", it_sky), 1, 1024, 256);
	/* the .tga is not read when the original has a size: the PNG only */
	COUNT("file reads", loads, 1);

	/* a PCX lookup with a PNG smaller than the original: not used */
	reset(); orig_w = orig_h = 256; PNG(1, 128);
	EXPECT(t_find("env/s_up.pcx", it_sky), 0, 0, 0);

	/* a PCX lookup whose size cannot be read: the .tga's header decides */
	reset(); TGA(1, 256); PNG(1, 512);
	EXPECT(t_find("env/s_up.pcx", it_sky), 1, 512, 256);

	/* a wall texture is unchanged: the .tga, though a larger PNG exists */
	reset(); add("textures/w.png", 1, 512, 512); add("textures/w.tga", 1, 64, 64);
	EXPECT(t_find("textures/w.tga", it_wall), 1, 64, 64);
	COUNT("decodes", decodes, 1);

	reset();
	printf("skyload_test: %d checks, %d failed\n", checks, failed);
	return failed ? 1 : 0;
}
