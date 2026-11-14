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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * The parts of ref_metal with neither SDL nor Metal: the apple5 floor,
 * the readback conversion, the object ledger and its lines. Host-tested
 * by tests/metal/rm_host_test.c (tests/check-ref-metal.sh).
 *
 * =======================================================================
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "header/rmb.h"

int
rm_caps_floor_ok(unsigned families)
{
	int n;

	for (n = RMB_FAMILY_FLOOR; n <= RMB_FAMILY_LAST; n++)
	{
		if (families & RMB_FAMILY_BIT(n))
		{
			return 1;
		}
	}

	return 0;
}

/* vsnprintf at buf + *used; 0 once the text no longer fits (buf is then full and terminated) */
static int
rm_append(char *buf, size_t len, size_t *used, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (*used >= len)
	{
		return 0;
	}

	va_start(ap, fmt);
	n = vsnprintf(buf + *used, len - *used, fmt, ap);
	va_end(ap);

	if ((n < 0) || ((size_t)n >= len - *used))
	{
		*used = len;
		return 0;
	}

	*used += (size_t)n;
	return 1;
}

int
rm_caps_family_list(unsigned families, char *buf, size_t len)
{
	size_t used = 0;
	int n, ok = 1, any = 0;

	if (len == 0)
	{
		return 0;
	}

	buf[0] = '\0';

	/* the report names the families from the floor on, as the shim's does */
	for (n = RMB_FAMILY_FLOOR; n <= RMB_FAMILY_LAST && ok; n++)
	{
		if (families & RMB_FAMILY_BIT(n))
		{
			ok = rm_append(buf, len, &used, "%sapple%d", any ? " " : "", n);
			any = 1;
		}
	}

	if (!any)
	{
		ok = rm_append(buf, len, &used, "none");
	}

	return ok;
}

void
rm_bgra_to_rgb(const unsigned char *bgra, size_t src_stride, int w, int h, unsigned char *rgb)
{
	int x, y;

	for (y = 0; y < h; y++)
	{
		const unsigned char *s = bgra + (size_t)y * src_stride;

		for (x = 0; x < w; x++, s += 4, rgb += 3)
		{
			rgb[0] = s[2];
			rgb[1] = s[1];
			rgb[2] = s[0];
		}
	}
}

void
rm_ledger_create(rm_ledger_t *l, rm_obj_kind_t kind)
{
	if ((unsigned)kind >= RM_OBJ_KINDS)
	{
		l->unmatched++;
		return;
	}

	l->created[kind]++;
}

void
rm_ledger_destroy(rm_ledger_t *l, rm_obj_kind_t kind)
{
	if (((unsigned)kind >= RM_OBJ_KINDS) || (l->destroyed[kind] >= l->created[kind]))
	{
		l->unmatched++;
		return;
	}

	l->destroyed[kind]++;
}

unsigned long
rm_ledger_live(const rm_ledger_t *l)
{
	unsigned long live = l->unreleased;
	int k;

	for (k = 0; k < RM_OBJ_KINDS; k++)
	{
		live += l->created[k] - l->destroyed[k];
	}

	return live;
}

int
rm_ledger_format(const rm_ledger_t *l, char *buf, size_t len)
{
	static const char *names[RM_OBJ_KINDS] = { "device", "queue", "view", "texture", "buffer" };
	size_t used = 0;
	int k, ok;

	if (len == 0)
	{
		return 0;
	}

	buf[0] = '\0';
	ok = rm_append(buf, len, &used, "ref_metal: teardown: %lu live objects (created/destroyed:",
			rm_ledger_live(l));

	for (k = 0; k < RM_OBJ_KINDS && ok; k++)
	{
		ok = rm_append(buf, len, &used, "%s %s %lu/%lu", k ? "," : "", names[k],
				l->created[k], l->destroyed[k]);
	}

	ok = ok && rm_append(buf, len, &used, "), %lu unreleased, %lu unmatched", l->unreleased, l->unmatched);
	return ok;
}

int
rm_stats_format(const rmb_stats_t *s, const rm_ledger_t *l, char *buf, size_t len)
{
	size_t used = 0;
	int ok;

	if (len == 0)
	{
		return 0;
	}

	buf[0] = '\0';
	ok = rm_append(buf, len, &used, "ref_metal: metal_stats: %lu frames, %lu without a drawable, ",
			s->frames, s->no_drawable);
	ok = ok && rm_append(buf, len, &used, "%lu skipped, %lu failed command buffers, ", s->skipped, s->failed);
	ok = ok && rm_append(buf, len, &used, "%lu readbacks, %lu no-op calls, ", s->readbacks, s->noop_calls);
	ok = ok && rm_append(buf, len, &used, "%lu draws, %lu draws skipped, %lu uploads, %lu uploads skipped, ",
			s->draws, s->draw_skips, s->uploads, s->upload_skips);
	ok = ok && rm_append(buf, len, &used, "%lu pass splits, %lu pipelines, %lu created after init, ",
			s->splits, s->pipelines, s->late_pipelines);
	ok = ok && rm_append(buf, len, &used, "%lu live objects", rm_ledger_live(l));
	return ok;
}

rm_load_t
rm_color_load(int clear, int defined)
{
	if (clear)
	{
		return RM_LOAD_CLEAR;
	}

	return defined ? RM_LOAD_KEEP : RM_LOAD_BLACK;
}

int
rm_targets_rebuild(int have_color, int have_depth, int cw, int ch, int w, int h)
{
	return !have_color || !have_depth || (cw != w) || (ch != h);
}

int
rm_upload_bytes(long w, long h, size_t cap, size_t *bytes)
{
	*bytes = 0;

	if ((w <= 0) || (h <= 0))
	{
		return 0;
	}

	/* w * h * 4 <= cap, checked by division before the product is formed */
	if ((unsigned long)w > cap / 4 / (unsigned long)h)
	{
		return 0;
	}

	*bytes = (size_t)w * (size_t)h * 4;
	return 1;
}

/* from gl1_draw.c, RDraw_StretchRaw (the 8-bit frame on the non-power-of-two path) */
int
rm_raw8_to_rgba(const unsigned char *data, int cols, int rows, const unsigned *palette,
		unsigned char *out, size_t out_len)
{
	size_t need, i, j;

	if (!rm_upload_bytes(cols, rows, out_len, &need))
	{
		return 0;
	}

	for (i = 0; i < (size_t)rows; ++i)
	{
		size_t rowOffset = i * (size_t)cols;

		for (j = 0; j < (size_t)cols; ++j)
		{
			memcpy(out + (rowOffset + j) * 4, &palette[data[rowOffset + j]], 4);
		}
	}

	return 1;
}

int
rm_fan_indices(int n, unsigned short *idx, int cap)
{
	int i, k = 0;

	if ((n < 3) || (n > 65535) || (cap < 0) || ((n - 2) > cap / 3))
	{
		return 0;
	}

	for (i = 1; i + 1 < n; i++)
	{
		idx[k++] = 0;
		idx[k++] = (unsigned short)i;
		idx[k++] = (unsigned short)(i + 1);
	}

	return k;
}

int
rm_pipe_2d(rm_texenv_t env, int alpha_test, int blend)
{
	if (((int)env < 0) || (env >= RM_TEXENVS))
	{
		return -1;
	}

	return ((int)env * 2 + (alpha_test ? 1 : 0)) * 2 + (blend ? 1 : 0);
}

/* from gl1_image.c, the modes table of R_TextureMode */
int
rm_texture_mode(const char *name, int *min, int *mag_linear)
{
	static const struct
	{
		const char *name;
		int min, mag_linear;
	} modes[] = {
		{"GL_NEAREST", 0, 0},
		{"GL_LINEAR", 1, 1},
		{"GL_NEAREST_MIPMAP_NEAREST", 2, 0},
		{"GL_LINEAR_MIPMAP_NEAREST", 3, 1},
		{"GL_NEAREST_MIPMAP_LINEAR", 4, 0},
		{"GL_LINEAR_MIPMAP_LINEAR", 5, 1}
	};
	size_t i;

	for (i = 0; name && i < sizeof(modes) / sizeof(modes[0]); i++)
	{
		if (!strcasecmp(modes[i].name, name))
		{
			*min = modes[i].min;
			*mag_linear = modes[i].mag_linear;
			return 1;
		}
	}

	return 0;
}
