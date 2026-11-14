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

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "header/rmb.h"
#include "common/mtl_matrix.h"

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
	ok = ok && rm_append(buf, len, &used, "%lu live objects, ", rm_ledger_live(l));
	ok = ok && rm_append(buf, len, &used, "skipped surfaces: %lu sky, %lu warped, %lu translucent, %lu flowing",
			s->sky_skips, s->warp_skips, s->trans_skips, s->flow_skips);
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

	/* each side on its own: a texture descriptor above the device's maximum aborts in Metal */
	if ((w > RMB_MAX_TEXTURE_SIDE) || (h > RMB_MAX_TEXTURE_SIDE))
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

int
rm_pipe_2d_textured(int pipe)
{
	if ((pipe < 0) || (pipe >= RM_PIPE_2D_COUNT))
	{
		return 0;
	}

	return (pipe / 4) != RM_TEXENV_UNTEXTURED;
}

int
rm_dir_of(const char *path, char *dir, size_t len)
{
	const char *slash = strrchr(path, '/');
	size_t n = slash ? (size_t)(slash - path) : 1;
	const char *from = slash ? path : ".";

	if (slash && (n == 0))
	{
		n = 1;   /* the root directory */
	}

	if (len == 0)
	{
		return 0;
	}

	if (n >= len)
	{
		dir[0] = '\0';
		return 0;
	}

	memcpy(dir, from, n);
	dir[n] = '\0';
	return 1;
}

/* from gl1_image.c: R_LoadPic's GL_NEAREST for nolerp, R_TextureMode's two branches */
int
rm_image_sampler(int mipmapped, int nolerp, int mode_change)
{
	if (nolerp && !(mode_change && mipmapped))
	{
		return RMB_SAMPLER(RMB_SMP_NEAREST, 0);
	}

	return RMB_SAMPLER(mipmapped ? RMB_SMP_MIP : RMB_SMP_FLAT, 0);
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

/* ---- the scrap, pics and characters ----------------------------------------- */

/* from gl1_scrap.c, CommonAllocBlock, with the bounds gl1 leaves to its caller */
int
rm_scrap_alloc(int *allocated, int aw, int ah, int w, int h, int *x, int *y)
{
	int i, j, best, best2, bx = 0, by = 0, found = 0;

	if (!allocated || !x || !y || (aw < 1) || (ah < 1) || (w < 1) || (h < 1) || (w > aw) || (h > ah))
	{
		return 0;
	}

	best = ah;

	/* gl1's bound, aw - w: a block never starts in the last w columns */
	for (i = 0; i < aw - w; i++)
	{
		best2 = 0;

		for (j = 0; j < w; j++)
		{
			if (allocated[i + j] >= best)
			{
				break;
			}

			if (allocated[i + j] > best2)
			{
				best2 = allocated[i + j];
			}
		}

		if (j == w)
		{
			/* this is a valid spot */
			bx = i;
			by = best = best2;
			found = 1;
		}
	}

	if (!found || (best + h > ah))
	{
		return 0;
	}

	for (i = 0; i < w; i++)
	{
		allocated[bx + i] = best + h;
	}

	*x = bx;
	*y = by;
	return 1;
}

int
rm_scrap_copy(unsigned *texels, int aw, int ah, int x, int y, int w, int h, const unsigned *pic)
{
	int i;

	if (!texels || !pic || (aw < 1) || (ah < 1) || (w < 1) || (h < 1) || (x < 0) || (y < 0)
			|| (w > aw - x) || (h > ah - y))
	{
		return 0;
	}

	for (i = 0; i < h; i++)
	{
		memcpy(&texels[(size_t)(y + i) * (size_t)aw + (size_t)x], pic + (size_t)i * (size_t)w,
				(size_t)w * sizeof(unsigned));
	}

	return 1;
}

int
rm_scrap_needs_upload(int dirty, int tex)
{
	(void)tex;   /* a missing texture is made again, not skipped */
	return dirty != 0;
}

int
rm_scrap_texture(const int *tex, int count, int n)
{
	if (!tex || (n < 0) || (n >= count))
	{
		return 0;
	}

	return tex[n];
}

int
rm_scrap_sampler(int n, int unfiltered)
{
	if ((n < 0) || (n >= RM_MAX_SCRAPS))
	{
		return RMB_SAMPLER(RMB_SMP_FLAT, 0);
	}

	return RMB_SAMPLER((n < RM_MAX_SCRAPS_NOLERP) || unfiltered ? RMB_SMP_NEAREST : RMB_SMP_FLAT, 0);
}

/* from gl1_draw.c, RDraw_CharScaled */
int
rm_char_quad(rm_quad_t *q, int x, int y, int num, float scale, const float *st)
{
	int row, col;
	float frow, fcol, size, scaledSize;

	num &= 255;

	if ((num & 127) == 32)
	{
		return 0;   /* space */
	}

	if (y <= -8)
	{
		return 0;   /* totally off screen */
	}

	row = num >> 4;
	col = num & 15;

	frow = row * 0.0625;
	fcol = col * 0.0625;
	size = 0.0625;

	scaledSize = 8 * scale;

	q->x0 = x;
	q->y0 = y;
	q->x1 = x + scaledSize;
	q->y1 = y + scaledSize;
	q->s0 = st[0] + fcol * (st[2] - st[0]);
	q->t0 = st[1] + frow * (st[3] - st[1]);
	q->s1 = st[0] + (fcol + size) * (st[2] - st[0]);
	q->t1 = st[1] + (frow + size) * (st[3] - st[1]);
	return 1;
}

/* from gl1_draw.c, RDraw_PicScaled and RDraw_StretchPic */
void
rm_pic_quad(rm_quad_t *q, int x, int y, int w, int h, float factor, const float *st)
{
	q->x0 = x;
	q->y0 = y;
	q->x1 = x + w * factor;
	q->y1 = y + h * factor;
	q->s0 = st[0];
	q->t0 = st[1];
	q->s1 = st[2];
	q->t1 = st[3];
}

/* from gl1_draw.c, RDraw_TileClear: the body of its two loops */
int
rm_tile_quad(rm_quad_t *q, int x, int y, int w, int h, int iw, int ih, long x2, long y2, const float *st)
{
	long next_x, next_y;
	int tile_w, tile_h;

	if ((iw < 1) || (ih < 1) || (w < 1) || (h < 1) || (x2 < 0) || (y2 < 0) || (x2 >= w) || (y2 >= h))
	{
		return 0;
	}

	next_x = x + x2 + iw;

	if (next_x > (long)x + w)
	{
		next_x = (long)x + w;
	}

	tile_w = (int)(next_x - (x + x2));

	next_y = y + y2 + ih;

	if (next_y > (long)y + h)
	{
		next_y = (long)y + h;
	}

	tile_h = (int)(next_y - (y + y2));

	q->x0 = x + x2;
	q->y0 = y + y2;
	q->x1 = next_x;
	q->y1 = next_y;
	q->s0 = st[0];
	q->t0 = st[1];
	q->s1 = st[0] + (st[2] - st[0]) * ((float)tile_w / iw);
	q->t1 = st[1] + (st[3] - st[1]) * ((float)tile_h / ih);
	return 1;
}

int
rm_batch_fits(long count, long add, long cap)
{
	return (count >= 0) && (add > 0) && (cap > 0) && (count <= cap) && (add <= cap - count);
}

int
rm_quad_indices(int n, unsigned short *idx, int cap)
{
	int i, k = 0;

	/* 4 vertices a quad: the last vertex index is 4 * n - 1, at most 65535 */
	if (!idx || (n < 1) || (n > 16384) || (cap < 0) || (n > cap / 6))
	{
		return 0;
	}

	for (i = 0; i < n; i++)
	{
		unsigned base = (unsigned)i * 4;

		idx[k++] = (unsigned short)base;
		idx[k++] = (unsigned short)(base + 1);
		idx[k++] = (unsigned short)(base + 2);
		idx[k++] = (unsigned short)base;
		idx[k++] = (unsigned short)(base + 2);
		idx[k++] = (unsigned short)(base + 3);
	}

	return k;
}

void
rm_tint_color(float *rgba, const float *rgb)
{
	rgba[0] = rgb[0];
	rgba[1] = rgb[1];
	rgba[2] = rgb[2];
	rgba[3] = 1;
}

/* ---- the world (N4) ------------------------------------------------------------- */

_Static_assert(offsetof(rm_wvert_t, pos) == 0, "the position of a world vertex is at offset 0");
_Static_assert(sizeof(rm_wvert_t) == RM_WVERT_STRIDE, "a world vertex is packed, 28 bytes");
_Static_assert(RM_WVERT_STRIDE % 4 == 0, "the world vertex stride is a multiple of 4");
_Static_assert(sizeof(rm_surfrec_t) == 48, "a surface record is 48 bytes, as rm_shaders.metal and RT read it");

rm_material_t
rm_material(int texflags)
{
	if (texflags & RM_TEX_SKY)
	{
		return RM_MAT_SKY;
	}

	if (texflags & (RM_TEX_TRANS33 | RM_TEX_TRANS66))
	{
		return RM_MAT_TRANSLUCENT;
	}

	if (texflags & RM_TEX_WARP)
	{
		return RM_MAT_WATER;
	}

	return (texflags & RM_TEX_FLOWING) ? RM_MAT_FLOWING : RM_MAT_WORLD;
}

void
rm_surf_normal(const float *pn, int planeback, float *out)
{
	int i;

	for (i = 0; i < 3; i++)
	{
		out[i] = planeback ? -pn[i] : pn[i];
	}
}

int
rm_world_add(rm_world_size_t *s, long n)
{
	unsigned long tris;

	if ((n < 3) || (s->surfs >= RM_MAX_MAP_FACES) || (s->verts > RM_WORLD_MAX_VERTS) ||
		(s->indices > RM_WORLD_MAX_INDICES))
	{
		return 0;
	}

	/* each total against what is left of its limit: no sum is formed that could pass it */
	tris = (unsigned long)n - 2;

	if (((unsigned long)n > RM_WORLD_MAX_VERTS - s->verts) || (tris > (RM_WORLD_MAX_INDICES - s->indices) / 3))
	{
		return 0;
	}

	s->surfs++;
	s->verts += (unsigned long)n;
	s->indices += 3 * tris;
	return 1;
}

int
rm_world_bytes(const rm_world_size_t *s, size_t cap, size_t *vbytes, size_t *ibytes, size_t *sbytes)
{
	*vbytes = *ibytes = *sbytes = 0;

	if ((s->surfs > RM_MAX_MAP_FACES) || (s->verts > RM_WORLD_MAX_VERTS) || (s->indices > RM_WORLD_MAX_INDICES))
	{
		return 0;
	}

	/* count * size <= cap, by division before the product is formed */
	if ((s->verts > cap / sizeof(rm_wvert_t)) || (s->indices > cap / sizeof(unsigned)) ||
		(s->surfs > cap / sizeof(rm_surfrec_t)))
	{
		return 0;
	}

	*vbytes = (size_t)s->verts * sizeof(rm_wvert_t);
	*ibytes = (size_t)s->indices * sizeof(unsigned);
	*sbytes = (size_t)s->surfs * sizeof(rm_surfrec_t);
	return 1;
}

/* from gl1_buffer.c, R_SetBufferIndices (GL_TRIANGLE_FAN), into the static buffer */
long
rm_world_fan(unsigned long base, long n, unsigned *idx, unsigned long cap)
{
	long i, k = 0;

	if ((n < 3) || ((unsigned long)(n - 2) > cap / 3) || (base > 0xfffffffful) ||
		((unsigned long)n - 1 > 0xfffffffful - base))
	{
		return 0;
	}

	for (i = 0; i < n - 2; i++)
	{
		idx[k++] = (unsigned)base;
		idx[k++] = (unsigned)(base + i + 1);
		idx[k++] = (unsigned)(base + i + 2);
	}

	return k;
}

int
rm_range_ok(long first, long count, long total)
{
	return (first >= 0) && (count >= 0) && (total >= 0) && (first <= total) && (count <= total - first);
}

int
rm_lump_ok(long fileofs, long filelen, long len)
{
	return rm_range_ok(fileofs, filelen, len);
}

int
rm_face_edges_ok(long firstedge, long numedges, long numsurfedges)
{
	return (numedges >= 3) && rm_range_ok(firstedge, numedges, numsurfedges);
}

int
rm_surfedge_ok(long e, long numedges)
{
	return (e < numedges) && (e > -numedges);
}

/* Mod_LoadTexinfo's next: a positive nexttexinfo names a texinfo, anything else ends the chain (-1) */
static long
rm_texinfo_next(const int *next, long i)
{
	return next[i] > 0 ? next[i] : -1;
}

int
rm_texinfo_chains_ok(const int *next, long count)
{
	long i, step, steps;

	if (count < 0)
	{
		return 0;
	}

	for (i = 0; i < count; i++)
	{
		if (next[i] >= count)
		{
			return 0;
		}
	}

	/* Mod_LoadTexinfo's count loop: from each texinfo until the chain ends or comes back */
	for (i = 0; i < count; i++)
	{
		for (step = rm_texinfo_next(next, i), steps = 0; (step != -1) && (step != i);
			step = rm_texinfo_next(next, step))
		{
			if (++steps > count)
			{
				return 0;
			}
		}
	}

	return 1;
}

int
rm_vis_header_ok(long numclusters, long len)
{
	return (len >= 4) && (numclusters >= 0) && (numclusters <= RM_MAX_MAP_LEAFS) && (numclusters <= (len - 4) / 8);
}

int
rm_leaf_ok(long cluster, long area, long numclusters, int has_vis)
{
	if ((area < 0) || (area >= RM_MAX_MAP_AREAS))
	{
		return 0;
	}

	return !has_vis || (cluster == -1) || ((cluster >= 0) && (cluster < numclusters));
}

int
rm_mark_once(unsigned char *owner, long total, long first, long count)
{
	long i;

	if (!rm_range_ok(first, count, total))
	{
		return 0;
	}

	for (i = first; i < first + count; i++)
	{
		if (owner[i])
		{
			return 0;
		}
	}

	memset(owner + first, 1, (size_t)count);
	return 1;
}

/* from files/pvs.c, Mod_DecompressVis, with both ends bounded */
int
rm_decompress_vis(const unsigned char *in, size_t in_len, size_t row, unsigned char *out, size_t out_cap)
{
	size_t i = 0, o = 0;

	if (out_cap < row)
	{
		return 0;
	}

	while (o < row)
	{
		unsigned c;

		if (i >= in_len)
		{
			break;
		}

		if (in[i])
		{
			out[o++] = in[i++];
			continue;
		}

		if (i + 1 >= in_len)
		{
			break;
		}

		c = in[i + 1];
		i += 2;

		while (c && (o < row))
		{
			out[o++] = 0;
			c--;
		}
	}

	if (o < row)
	{
		/* the input ended inside the row: everything visible, as without visibility */
		memset(out, 0xff, row);
		return 0;
	}

	return 1;
}

int
rm_world_format(const char *map, const rm_world_size_t *s, char *buf, size_t len)
{
	size_t vb, ib, sb, used = 0;
	int ok;

	if (len == 0)
	{
		return 0;
	}

	buf[0] = '\0';

	if (!rm_world_bytes(s, (size_t)-1, &vb, &ib, &sb))
	{
		return 0;
	}

	ok = rm_append(buf, len, &used, "ref_metal: metal_stats: world %s: %lu vertices (%zu bytes), ", map, s->verts, vb);
	ok = ok && rm_append(buf, len, &used, "%lu indices (%zu bytes), %lu surface records (%zu bytes)",
			s->indices, ib, s->surfs, sb);
	return ok;
}

/* from gl1_main.c, R_SetupGL: the viewport, as mgl_compute_raster turns it into Metal's */
void
rm_view_rect(int rx, int ry, int rw, int rh, int vid_w, int vid_h, int th, rm_viewport_t *out)
{
	int x, x2, y2, y, w, h;

	x = floor(rx * vid_w / (float)vid_w);
	x2 = ceil((rx + rw) * vid_w / (float)vid_w);
	y = floor(vid_h - ry * vid_h / (float)vid_h);
	y2 = ceil(vid_h - (ry + rh) * vid_h / (float)vid_h);

	w = x2 - x;
	h = y - y2;

	/* glViewport(x, y2, w, h); mgl_compute_raster */
	out->x = x;
	out->y = (double)th - y2 - h;
	out->w = w;
	out->h = h;
	out->znear = 0;
	out->zfar = 1;
}

/* the shim's glRotatef and glTranslatef: m = m * r */
static void
rm_mat_rotate_on(float *m, float angle, float x, float y, float z)
{
	float r[16];

	if (mtl_mat_rotate(r, angle, x, y, z))
	{
		mtl_mat_mul(m, m, r);
	}
}

void
rm_blend_matrix(float *m)
{
	mtl_mat_identity(m);
	rm_mat_rotate_on(m, -90, 1, 0, 0);   /* put Z going up */
	rm_mat_rotate_on(m, 90, 0, 0, 1);    /* put Z going up */
}

/* from gl1_main.c, R_SetupGL: the modelview */
void
rm_view_matrix(float *m, const float *angles, const float *org)
{
	float t[16];

	rm_blend_matrix(m);
	rm_mat_rotate_on(m, -angles[2], 1, 0, 0);
	rm_mat_rotate_on(m, -angles[0], 0, 1, 0);
	rm_mat_rotate_on(m, -angles[1], 0, 0, 1);
	mtl_mat_translate(t, -org[0], -org[1], -org[2]);
	mtl_mat_mul(m, m, t);
}

/* from gl1_main.c, R_SetPerspective (no stereo), on the identity of glLoadIdentity */
int
rm_projection(float *m, float fovy, int width, int height, float znear, int farsee,
		int underwater, float time, float waterwarp)
{
	/* gluPerspective style parameters */
	const double zNear = (znear > 0.1f) ? znear : 0.1f;
	const double zFar = farsee ? 8192.0f : 4096.0f;
	double aspectratio, xmin, xmax, ymin, ymax;
	float f[16], id[16];

	if (height == 0)
	{
		return 0;   /* glFrustum refuses bottom == top */
	}

	aspectratio = (double)width / height;
	ymax = zNear * tan(fovy * M_PI / 360.0);
	xmax = ymax * aspectratio;

	if (underwater && waterwarp)
	{
		const double warp = sin(time * 1.5) * 0.03 * waterwarp;

		ymax *= 1.0 - warp;
		xmax *= 1.0 + warp;
	}

	ymin = -ymax;
	xmin = -xmax;

	if (!mtl_mat_frustum(f, xmin, xmax, ymin, ymax, zNear, zFar))
	{
		return 0;
	}

	mtl_mat_identity(id);
	mtl_mat_mul(m, id, f);
	return 1;
}

/* from gl1_main.c, R_SetupFrame (v_blend) and R_PolyBlend (whether it draws) */
int
rm_polyblend(const float *blend, float sw_gamma, float polyblend, float *rgba)
{
	int i;

	if (!polyblend || !blend[3])
	{
		return 0;
	}

	for (i = 0; i < 3; i++)
	{
		rgba[i] = blend[i] * sw_gamma;
	}

	rgba[3] = blend[3];
	return 1;
}
