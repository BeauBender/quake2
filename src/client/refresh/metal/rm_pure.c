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

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "header/rmb.h"
#include "common/mtl_matrix.h"
#include "common/mtl_shader_math.h"

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
	ok = ok && rm_append(buf, len, &used, "; %lu drawn without their dynamic light", s->dlit_surfs);
	return ok;
}

rm_load_t
rm_color_load(int clear, int defined, int first)
{
	if (clear)
	{
		return RM_LOAD_CLEAR;
	}

	return (defined && !first) ? RM_LOAD_KEEP : RM_LOAD_BLACK;
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
_Static_assert(sizeof(rm_wvert_t) == RM_WVERT_STRIDE, "a world vertex is 32 bytes, no padding");
_Static_assert(offsetof(rm_wvert_t, uv) == 16, "the diffuse coordinates follow the position and its w");
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
	/* the loaders read int and float fields of the records in place: aligned to 4 */
	return ((fileofs & 3) == 0) && rm_range_ok(fileofs, filelen, len);
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
rm_leaf_contents_ok(long contents)
{
	return contents != -1;   /* CONTENTS_NODE */
}

int
rm_world_head_ok(long numnodes, long nummodels, long headnode)
{
	return (numnodes >= 1) && (nummodels >= 1) && (headnode >= 0) && (headnode < numnodes);
}

int
rm_node_tree_ok(const int *children, long numnodes, long numleafs, unsigned char *seen)
{
	long i;

	if ((numnodes < 1) || (numleafs < 0))
	{
		return 0;
	}

	memset(seen, 0, (size_t)numnodes);

	for (i = 0; i < 2 * numnodes; i++)
	{
		long c = children[i];

		if (c < 0)
		{
			/* a leaf, -1 - c; a leaf may have more than one parent (the last one is kept) */
			if (-1 - c >= numleafs)
			{
				return 0;
			}

			continue;
		}

		/* node 0 is the root; any other node has one parent at most */
		if ((c == 0) || (c >= numnodes) || seen[c])
		{
			return 0;
		}

		seen[c] = 1;
	}

	return 1;
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

rm_slot_t
rm_slot_state(int named, int loaded, int holds)
{
	if (named && loaded)
	{
		return RM_SLOT_LOADED;
	}

	if (!named && !loaded && !holds)
	{
		return RM_SLOT_EMPTY;
	}

	return RM_SLOT_UNFINISHED;
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

/*
 * from gl1_main.c, R_SetPerspective (no stereo): m = m * the frustum, as
 * the shim's glFrustum multiplies it onto the current matrix; m untouched
 * and 0 when glFrustum refuses
 */
static int
rm_perspective_on(float *m, float fovy, int width, int height, float znear, int farsee,
		int underwater, float time, float waterwarp)
{
	/* gluPerspective style parameters */
	const double zNear = (znear > 0.1f) ? znear : 0.1f;
	const double zFar = farsee ? 8192.0f : 4096.0f;
	double aspectratio, xmin, xmax, ymin, ymax;
	float f[16];

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

	mtl_mat_mul(m, m, f);
	return 1;
}

/* from gl1_main.c, R_SetPerspective (no stereo), on the identity of glLoadIdentity */
int
rm_projection(float *m, float fovy, int width, int height, float znear, int farsee,
		int underwater, float time, float waterwarp)
{
	float t[16];

	mtl_mat_identity(t);

	if (!rm_perspective_on(t, fovy, width, height, znear, farsee, underwater, time, waterwarp))
	{
		return 0;   /* m untouched */
	}

	memcpy(m, t, sizeof(t));
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

/* ---- the lightmaps (N5) --------------------------------------------------------- */

/* from gl1_model.c, Mod_CalcSurfaceExtents, one axis, with the float checked before it becomes an int */
int
rm_surface_extents(float mins, float maxs, short *texturemin, short *extent)
{
	double bmin, bmax;

	if (!isfinite(mins) || !isfinite(maxs) || (maxs < mins))
	{
		return 0;
	}

	/* gl1: bmins = floor(mins / 16), bmaxs = ceil(maxs / 16), the division in float */
	bmin = floor(mins / 16);
	bmax = ceil(maxs / 16);

	/* texturemins = bmins * 16 and extents = (bmaxs - bmins) * 16 are shorts */
	if ((bmin < -2048) || (bmin > 2047) || (bmax - bmin > 2047))
	{
		return 0;
	}

	*texturemin = (short)((int)bmin * 16);
	*extent = (short)(((int)bmax - (int)bmin) * 16);
	return 1;
}

int
rm_lm_size(long extent0, long extent1, int *smax, int *tmax)
{
	long s, t;

	if ((extent0 < 0) || (extent1 < 0))
	{
		return 0;
	}

	s = (extent0 >> 4) + 1;
	t = (extent1 >> 4) + 1;

	/* LM_AllocBlock places nothing wider than BLOCK_WIDTH - 1; R_BuildLightMap's s_blocklights check */
	if ((s > RM_LM_WIDTH - 1) || (t > RM_LM_HEIGHT) || (s * t > RM_LM_MAX_LUXELS))
	{
		return 0;
	}

	*smax = (int)s;
	*tmax = (int)t;
	return 1;
}

int
rm_lm_nummaps(const unsigned char *styles)
{
	int n;

	for (n = 0; n < RM_LM_STYLES && styles[n] != RM_LM_STYLE_END; n++)
	{
	}

	return n;
}

int
rm_lm_samples_ok(long ofs, long extent0, long extent1, int nummaps, long lightlen)
{
	long long per;

	/* an offset past the lump (or a negative length) leaves lightlen - ofs negative, which no run fits */
	if ((ofs < 0) || (extent0 < 0) || (extent1 < 0) ||
		(extent0 > 32767) || (extent1 > 32767) || (nummaps < 0) || (nummaps > RM_LM_STYLES))
	{
		return 0;
	}

	/* at most 2048 * 2048 * 3 * 4 bytes: no overflow in long long */
	per = (long long)((extent0 >> 4) + 1) * (long long)((extent1 >> 4) + 1) * 3;
	return per * nummaps <= (long long)(lightlen - ofs);
}

int
rm_lp_coords(float s, float t, short smin, short tmin, short sext, short text, int *ds, int *dt)
{
	long long is, it, ls, lt;

	/*
	 * gl1's conversion to int is defined when the truncation fits an
	 * int (C99 6.3.1.4): -2^31 - 1 < s < 2^31, exact in double; each
	 * test fails for a NaN and an infinity
	 */
	if (!((double)s > -2147483649.0 && (double)s < 2147483648.0) ||
		!((double)t > -2147483649.0 && (double)t < 2147483648.0))
	{
		return 0;
	}

	/* gl1's four lines, in long long: the truncation and the differences cannot overflow */
	is = (long long)s;
	it = (long long)t;

	if ((is < smin) || (it < tmin))
	{
		return 0;
	}

	ls = is - smin;
	lt = it - tmin;

	if ((ls > sext) || (lt > text))
	{
		return 0;
	}

	/* 0 .. sext and 0 .. text, which fit an int */
	*ds = (int)ls;
	*dt = (int)lt;

	return 1;
}

int
rm_dlight_count(long n)
{
	return (n < 0) ? 0 : (n > RM_MAX_DLIGHTS ? RM_MAX_DLIGHTS : (int)n);
}

unsigned
rm_dlight_bit(int i)
{
	return ((i < 0) || (i >= RM_MAX_DLIGHTS)) ? 0 : (1u << i);
}

int
rm_lm_alloc(int *allocated, int w, int h, int *x, int *y)
{
	/* gl1's LM_AllocBlock is CommonAllocBlock's loop on the lightmap block (gl1_lightmap.c, gl1_scrap.c) */
	return rm_scrap_alloc(allocated, RM_LM_WIDTH, RM_LM_HEIGHT, w, h, x, y);
}

int
rm_lm_slices_ok(long n)
{
	return (n >= 1) && (n <= RM_LM_MAX_SLICES);
}

/* from gl1_lightmap.c, LM_CreateSurfaceLightmap */
int
rm_lm_place(int *allocated, int *slice, int w, int h, int *x, int *y)
{
	int empty[RM_LM_WIDTH];

	if ((w < 1) || (w > RM_LM_WIDTH) || (h < 1) || (h > RM_LM_HEIGHT) || (*slice < 0) ||
		(*slice >= RM_LM_MAX_SLICES))
	{
		return 0;
	}

	if (rm_lm_alloc(allocated, w, h, x, y))
	{
		return 1;
	}

	/* the next slice must exist and take the block when it is empty (gl1's second LM_AllocBlock) */
	memset(empty, 0, sizeof(empty));

	if ((*slice + 1 >= RM_LM_MAX_SLICES) || !rm_lm_alloc(empty, w, h, x, y))
	{
		return 0;
	}

	memcpy(allocated, empty, sizeof(empty));
	(*slice)++;
	return 2;
}

int
rm_region_ok(long tw, long th, long tslices, long slice, long x, long y, long w, long h)
{
	/* x >= 0 and x <= tw - w imply w <= tw, and with w >= 1 a texture at least one wide */
	return (slice >= 0) && (slice < tslices) && (x >= 0) && (y >= 0) && (w >= 1) && (h >= 1) &&
		(x <= tw - w) && (y <= th - h);
}

int
rm_lm_region_ok(long slice, long nslices, long x, long y, long w, long h)
{
	return rm_lm_slices_ok(nslices) && rm_region_ok(RM_LM_WIDTH, RM_LM_HEIGHT, nslices, slice, x, y, w, h);
}

/* from gl1_surf.c */
void
rm_lm_join(const rm_lmrect_t *adding, rm_lmrect_t *obj)
{
	if (adding->top < obj->top)
	{
		obj->top = adding->top;
	}

	if (adding->bottom > obj->bottom)
	{
		obj->bottom = adding->bottom;
	}

	if (adding->left < obj->left)
	{
		obj->left = adding->left;
	}

	if (adding->right > obj->right)
	{
		obj->right = adding->right;
	}
}

int
rm_lm_regen_mark(int uploaded, int dlit, int cacheable, int *dirty)
{
	*dirty = !uploaded || dlit;

	return !*dirty && cacheable;
}

/* from gl1_image.c, R_InitImages; a NaN is 0 (gl1 would convert it to a byte) */
int
rm_minlight_table(float m, unsigned char *table)
{
	int i;

	if (m != m)
	{
		m = 0;
	}

	m = m < 0 ? 0 : (m > 255 ? 255 : m);

	for (i = 0; i < 256; i++)
	{
		float inf = (255.0f - m) * (float)i / 255.0f + m;

		inf = inf < 0 ? 0 : (inf > 255 ? 255 : inf);
		table[i] = (unsigned char)inf;      /* the identity at m 0: 255 * i / 255 is i exactly */
	}

	return m != 0;
}

/*
 * Q_ftol, with the values a float cannot hold as an int (a NaN, or a
 * light past a billion from a huge r_modulate) saturated instead of
 * converted, which C leaves undefined; every other value is Q_ftol's.
 */
static long
rm_ftol(float f)
{
	if (f != f)
	{
		return 0;
	}

	if (f > 1e9f)
	{
		return 1000000000L;
	}

	if (f < -1e9f)
	{
		return -1000000000L;
	}

	return (long)f;
}

/* from gl1_light.c, R_BuildLightMap, without the dynamic lights (N8) */
int
rm_lm_build(const unsigned char *samples, int smax, int tmax, int nummaps, const float (*rgb)[3],
		float modulate, const unsigned char *minlight, const unsigned char *gamma,
		unsigned char *dest, long stride, float *blocklights)
{
	int r, g, b, a, max;
	int i, j, size, maps, ok_s, ok_t;
	const unsigned char *lightmap;
	float scale[3];
	float *bl;

	/* the size rm_lm_size gives for the extents (smax - 1) * 16 and (tmax - 1) * 16 (below 1 is negative) */
	if (!rm_lm_size(((long)smax - 1) * 16, ((long)tmax - 1) * 16, &ok_s, &ok_t) ||
		(nummaps < 0) || (nummaps > RM_LM_STYLES) || (stride < (long)smax * RM_LM_BYTES))
	{
		return 0;
	}

	size = smax * tmax;

	/* set to full bright if no light data */
	if (!samples)
	{
		for (i = 0; i < size * 3; i++)
		{
			blocklights[i] = 255;
		}

		goto store;
	}

	lightmap = samples;

	/* add all the lightmaps */
	if (nummaps == 1)
	{
		for (maps = 0; maps < nummaps; maps++)
		{
			bl = blocklights;

			for (i = 0; i < 3; i++)
			{
				scale[i] = modulate * rgb[maps][i];
			}

			if ((scale[0] == 1.0F) && (scale[1] == 1.0F) && (scale[2] == 1.0F))
			{
				for (i = 0; i < size; i++, bl += 3)
				{
					bl[0] = lightmap[i * 3 + 0];
					bl[1] = lightmap[i * 3 + 1];
					bl[2] = lightmap[i * 3 + 2];
				}
			}
			else
			{
				for (i = 0; i < size; i++, bl += 3)
				{
					bl[0] = lightmap[i * 3 + 0] * scale[0];
					bl[1] = lightmap[i * 3 + 1] * scale[1];
					bl[2] = lightmap[i * 3 + 2] * scale[2];
				}
			}

			lightmap += size * 3; /* skip to next lightmap */
		}
	}
	else
	{
		memset(blocklights, 0, sizeof(blocklights[0]) * size * 3);

		for (maps = 0; maps < nummaps; maps++)
		{
			bl = blocklights;

			for (i = 0; i < 3; i++)
			{
				scale[i] = modulate * rgb[maps][i];
			}

			if ((scale[0] == 1.0F) && (scale[1] == 1.0F) && (scale[2] == 1.0F))
			{
				for (i = 0; i < size; i++, bl += 3)
				{
					bl[0] += lightmap[i * 3 + 0];
					bl[1] += lightmap[i * 3 + 1];
					bl[2] += lightmap[i * 3 + 2];
				}
			}
			else
			{
				for (i = 0; i < size; i++, bl += 3)
				{
					bl[0] += lightmap[i * 3 + 0] * scale[0];
					bl[1] += lightmap[i * 3 + 1] * scale[1];
					bl[2] += lightmap[i * 3 + 2] * scale[2];
				}
			}

			lightmap += size * 3; /* skip to next lightmap */
		}
	}

	/* gl1 adds the dynamic lights here (R_AddDynamicLights): N8's, a step of its own */

store:

	stride -= (smax << 2);
	bl = blocklights;

	for (i = 0; i < tmax; i++, dest += stride)
	{
		for (j = 0; j < smax; j++)
		{
			r = (int)rm_ftol(bl[0]);
			g = (int)rm_ftol(bl[1]);
			b = (int)rm_ftol(bl[2]);

			/* catch negative lights */
			if (r < 0)
			{
				r = 0;
			}

			if (g < 0)
			{
				g = 0;
			}

			if (b < 0)
			{
				b = 0;
			}

			/* determine the brightest of the three color components */
			if (r > g)
			{
				max = r;
			}
			else
			{
				max = g;
			}

			if (b > max)
			{
				max = b;
			}

			/* alpha is ONLY used for the mono lightmap case: the brightest component */
			a = max;

			/* rescale all the color components if the intensity of the greatest channel exceeds 1.0 */
			if (max > 255)
			{
				float t = 255.0F / max;

				r = r * t;
				g = g * t;
				b = b * t;
				a = a * t;
			}

			if (minlight)
			{
				r = minlight[r];
				g = minlight[g];
				b = minlight[b];
			}

			dest[0] = gamma[r];
			dest[1] = gamma[g];
			dest[2] = gamma[b];
			dest[3] = a;

			bl += 3;
			dest += 4;
		}
	}

	return 1;
}

/* from gl1_main.c, RI_BeginFrame */
int
rm_overbright(float v)
{
	int obb = (v != v) ? 0 : (v > 4 ? 4 : (v < 0 ? 0 : (int)v));

	return (obb == 3) ? 2 : obb;
}

int
rm_lm_env(int overbright, int lightmap_only)
{
	if (overbright)
	{
		return MTL_LM_COMBINE;
	}

	return lightmap_only ? MTL_LM_REPLACE : MTL_LM_MODULATE;
}

int
rm_lm_format(const char *map, long slices, char *buf, size_t len)
{
	size_t used = 0;

	if (len == 0)
	{
		return 0;
	}

	buf[0] = '\0';

	if (!rm_lm_slices_ok(slices) || !rm_append(buf, len, &used,
			"ref_metal: metal_stats: lightmaps %s: %ld slices of %d by %d (%ld bytes)", map, slices,
			RM_LM_WIDTH, RM_LM_HEIGHT, slices * RM_LM_WIDTH * RM_LM_HEIGHT * RM_LM_BYTES))
	{
		buf[0] = '\0';
		return 0;
	}

	return 1;
}

rm_reopen_t
rm_split_reopen(int pass_open, int pass_depth)
{
	return (pass_open && pass_depth) ? RM_REOPEN_3D_KEEP : RM_REOPEN_NONE;
}

/* ---- sky, warped water, flowing and translucent surfaces (N6) --------------------- */

/*
 * gl1's TURBSCALE and its table, constants/warpsin.h (included by
 * gl1_warp.c as r_turbsin), which gl1's RI_Init halves in place before
 * the first frame: the values the water uses are half of the file's.
 */
#define RM_TURBSCALE (256.0 / (2 * M_PI))

static const float rm_turbsin[RM_TURB_SIZE] = {
#include "../constants/warpsin.h"
};

float
rm_turb_sin(int i)
{
	return rm_turbsin[i & (RM_TURB_SIZE - 1)] * 0.5;
}

/* the int a double truncates to as gl1's (int) cast does, where that is defined; saturated, a NaN is 0, elsewhere */
static int
rm_itrunc(double d)
{
	if (d != d)
	{
		return 0;
	}

	if (d >= 2147483647.0)
	{
		return INT_MAX;
	}

	if (d <= -2147483648.0)
	{
		return INT_MIN;
	}

	return (int)d;
}

int
rm_warp_index(float c, float time)
{
	double x = (c * 0.125 + time) * RM_TURBSCALE;

	if (x != x)
	{
		return 0;
	}

	/* gl1 converts the value to an int unchecked; the table is periodic, so take it modulo its period */
	if ((x >= 2147483647.0) || (x <= -2147483648.0))
	{
		x = fmod(x, (double)RM_TURB_SIZE);

		if (x != x)
		{
			return 0;   /* an infinity */
		}
	}

	return (int)x & (RM_TURB_SIZE - 1);
}

float
rm_warp_scroll(float time, int flowing)
{
	if (!flowing)
	{
		return 0;
	}

	return -64 * ((time * 0.5) - rm_itrunc(time * 0.5));
}

float
rm_flow_scroll(float time)
{
	float scroll = -64 * ((time / 40.0) - rm_itrunc(time / 40.0));

	if (scroll == 0.0)
	{
		scroll = -64.0;
	}

	return scroll;
}

void
rm_warp_st(float os, float ot, float time, float scroll, float *s, float *t)
{
	float ss, tt;

	ss = os + rm_turb_sin(rm_warp_index(ot, time)) + scroll;
	tt = ot + rm_turb_sin(rm_warp_index(os, time));

	*s = ss * (1.0 / 64);
	*t = tt * (1.0 / 64);
}

/* ---- the warp subdivision ---- */

/* the vertices a piece may have after its cut: at most two per vertex it had, and it had RM_WARP_MAX_VERTS */
#define RM_WARP_CLIP 128

typedef struct
{
	const float *vec0, *vec1;
	float *out;
	unsigned long out_cap, nv;
	unsigned *polylen;
	unsigned poly_cap, np;
	unsigned long calls;
	int failed;
} rm_warp_ctx_t;

static void
rm_warp_piece(rm_warp_ctx_t *c, int numverts, float *verts, int depth)
{
	int i, j, k;
	float mins[3], maxs[3];
	float m;
	float *v;
	float front[RM_WARP_CLIP][3], back[RM_WARP_CLIP][3];
	int f, b;
	float dist[RM_WARP_MAX_VERTS + 1];
	float frac;
	float s, t;
	float total[3];
	float total_s, total_t;
	float *o;

	if (c->failed)
	{
		return;
	}

	if ((++c->calls > RM_WARP_MAX_CALLS) || (depth > RM_WARP_MAX_DEPTH) || (numverts > RM_WARP_MAX_VERTS) ||
		(numverts < 0))
	{
		c->failed = 1;
		return;
	}

	/* gl1's R_BoundPoly */
	mins[0] = mins[1] = mins[2] = 9999;
	maxs[0] = maxs[1] = maxs[2] = -9999;
	v = verts;

	for (i = 0; i < numverts; i++)
	{
		for (j = 0; j < 3; j++, v++)
		{
			if (*v < mins[j])
			{
				mins[j] = *v;
			}

			if (*v > maxs[j])
			{
				maxs[j] = *v;
			}
		}
	}

	for (i = 0; i < 3; i++)
	{
		m = (mins[i] + maxs[i]) * 0.5;
		m = RM_WARP_SIZE * floor(m / RM_WARP_SIZE + 0.5);

		if (maxs[i] - m < 8)
		{
			continue;
		}

		if (m - mins[i] < 8)
		{
			continue;
		}

		/* cut it */
		v = verts + i;

		for (j = 0; j < numverts; j++, v += 3)
		{
			dist[j] = *v - m;
		}

		/* wrap cases */
		dist[j] = dist[0];
		v -= i;
		v[0] = verts[0];
		v[1] = verts[1];
		v[2] = verts[2];

		f = b = 0;
		v = verts;

		for (j = 0; j < numverts; j++, v += 3)
		{
			if (dist[j] >= 0)
			{
				front[f][0] = v[0];
				front[f][1] = v[1];
				front[f][2] = v[2];
				f++;
			}

			if (dist[j] <= 0)
			{
				back[b][0] = v[0];
				back[b][1] = v[1];
				back[b][2] = v[2];
				b++;
			}

			if ((dist[j] == 0) || (dist[j + 1] == 0))
			{
				continue;
			}

			if ((dist[j] > 0) != (dist[j + 1] > 0))
			{
				/* clip point */
				frac = dist[j] / (dist[j] - dist[j + 1]);

				for (k = 0; k < 3; k++)
				{
					front[f][k] = back[b][k] = v[k] + frac * (v[3 + k] - v[k]);
				}

				f++;
				b++;
			}
		}

		rm_warp_piece(c, f, front[0], depth + 1);
		rm_warp_piece(c, b, back[0], depth + 1);
		return;
	}

	/* a piece with no vertex has no fan (gl1 would divide by 0 for its centre) */
	if (numverts < 1)
	{
		return;
	}

	if ((c->np >= c->poly_cap) || (c->nv > c->out_cap) || ((unsigned long)numverts + 2 > c->out_cap - c->nv))
	{
		c->failed = 1;
		return;
	}

	/* add a point in the center to help keep warp valid */
	o = c->out + c->nv * RM_WARP_FLOATS;
	total[0] = total[1] = total[2] = 0;
	total_s = 0;
	total_t = 0;

	for (i = 0; i < numverts; i++, verts += 3)
	{
		o[(i + 1) * RM_WARP_FLOATS] = verts[0];
		o[(i + 1) * RM_WARP_FLOATS + 1] = verts[1];
		o[(i + 1) * RM_WARP_FLOATS + 2] = verts[2];
		s = verts[0] * c->vec0[0] + verts[1] * c->vec0[1] + verts[2] * c->vec0[2];
		t = verts[0] * c->vec1[0] + verts[1] * c->vec1[1] + verts[2] * c->vec1[2];

		total_s += s;
		total_t += t;
		total[0] = verts[0] + total[0];
		total[1] = verts[1] + total[1];
		total[2] = verts[2] + total[2];

		o[(i + 1) * RM_WARP_FLOATS + 3] = s;
		o[(i + 1) * RM_WARP_FLOATS + 4] = t;
	}

	{
		/* gl1's VectorScale takes its scale as a float */
		float scale = 1.0 / numverts;

		o[0] = total[0] * scale;
		o[1] = total[1] * scale;
		o[2] = total[2] * scale;
	}
	o[3] = total_s / numverts;
	o[4] = total_t / numverts;

	/* copy first vertex to last */
	memcpy(o + (numverts + 1) * RM_WARP_FLOATS, o + RM_WARP_FLOATS, RM_WARP_FLOATS * sizeof(float));

	c->polylen[c->np++] = (unsigned)numverts + 2;
	c->nv += (unsigned long)numverts + 2;
}

int
rm_warp_subdivide(const float *verts, int numverts, const float *vec0, const float *vec1,
		float *out, unsigned long out_cap, unsigned *polylen, unsigned poly_cap,
		unsigned *npolys, unsigned long *nverts)
{
	rm_warp_ctx_t c;
	float top[(RM_WARP_MAX_VERTS + 1) * 3];
	int i;

	*npolys = 0;
	*nverts = 0;

	if ((numverts < 3) || (numverts > RM_WARP_MAX_VERTS))
	{
		return 0;
	}

	/* gl1's bounds of a polygon start at +-9999: a coordinate outside them, or not a number, is refused */
	for (i = 0; i < numverts * 3; i++)
	{
		if (!(fabsf(verts[i]) <= RM_WARP_COORD_MAX))
		{
			return 0;
		}

		top[i] = verts[i];
	}

	memset(&c, 0, sizeof(c));
	c.vec0 = vec0;
	c.vec1 = vec1;
	c.out = out;
	c.out_cap = out_cap;
	c.polylen = polylen;
	c.poly_cap = poly_cap;

	rm_warp_piece(&c, numverts, top, 0);

	if (c.failed)
	{
		return 0;
	}

	*npolys = c.np;
	*nverts = c.nv;
	return 1;
}

/* ---- the sky box ---- */

static const float rm_skyclip[6][3] = {
	{1, 1, 0},
	{1, -1, 0},
	{0, -1, 1},
	{0, 1, 1},
	{1, 0, 1},
	{-1, 0, 1}
};

static const int rm_st_to_vec[6][3] = {
	{3, -1, 2},
	{-3, 1, 2},

	{1, 3, 2},
	{-1, -3, 2},

	{-2, -1, 3}, /* 0 degrees yaw, look straight up */
	{2, -1, -3} /* look straight down */
};

static const int rm_vec_to_st[6][3] = {
	{-2, 3, 1},
	{2, 3, -1},

	{1, 3, 2},
	{-1, 3, -2},

	{-2, -1, 3},
	{-2, 1, -3}
};

static const int rm_skytexorder[6] = {0, 2, 1, 3, 4, 5};

void
rm_sky_clear(rm_skybounds_t *b)
{
	int i;

	for (i = 0; i < 6; i++)
	{
		b->mins[0][i] = b->mins[1][i] = 9999;
		b->maxs[0][i] = b->maxs[1][i] = -9999;
	}
}

/* gl1's R_DrawSkyPolygon: the face the fully clipped polygon maps to, and its bounds grown */
static void
rm_sky_draw_polygon(rm_skybounds_t *b, int nump, const float *vecs)
{
	int i, j, idx;
	float v[3], av[3];
	float s, t, dv;
	int axis;
	const float *vp;

	/* decide which face it maps to */
	v[0] = v[1] = v[2] = 0;

	for (i = 0, vp = vecs; i < nump; i++, vp += 3)
	{
		v[0] = vp[0] + v[0];
		v[1] = vp[1] + v[1];
		v[2] = vp[2] + v[2];
	}

	av[0] = fabs(v[0]);
	av[1] = fabs(v[1]);
	av[2] = fabs(v[2]);

	if ((av[0] > av[1]) && (av[0] > av[2]))
	{
		axis = (v[0] < 0) ? 1 : 0;
	}
	else if ((av[1] > av[2]) && (av[1] > av[0]))
	{
		axis = (v[1] < 0) ? 3 : 2;
	}
	else
	{
		axis = (v[2] < 0) ? 5 : 4;
	}

	/* project new texture coords */
	for (i = 0; i < nump; i++, vecs += 3)
	{
		j = rm_vec_to_st[axis][2];
		idx = (j > 0) ? (j - 1) : (-j - 1);

		if ((idx < 0) || (idx > 2))
		{
			return;
		}

		dv = (j > 0) ? vecs[idx] : -vecs[idx];

		if (dv < 0.001)
		{
			continue; /* don't divide by zero */
		}

		j = rm_vec_to_st[axis][0];
		idx = (j < 0) ? (-j - 1) : (j - 1);

		if ((idx < 0) || (idx > 2))
		{
			return;
		}

		s = (j < 0) ? -vecs[idx] / dv : vecs[idx] / dv;

		j = rm_vec_to_st[axis][1];
		idx = (j < 0) ? (-j - 1) : (j - 1);

		if ((idx < 0) || (idx > 2))
		{
			return;
		}

		t = (j < 0) ? -vecs[idx] / dv : vecs[idx] / dv;

		if (s < b->mins[0][axis])
		{
			b->mins[0][axis] = s;
		}

		if (t < b->mins[1][axis])
		{
			b->mins[1][axis] = t;
		}

		if (s > b->maxs[0][axis])
		{
			b->maxs[0][axis] = s;
		}

		if (t > b->maxs[1][axis])
		{
			b->maxs[1][axis] = t;
		}
	}
}

/* gl1's R_ClipSkyPolygon on vecs, which holds room for nump + 1 vertices (the wrap vertex is written) */
static int
rm_sky_clip_stage(rm_skybounds_t *b, int nump, float *vecs, int stage)
{
	const float *norm;
	float *v;
	int front, back;
	float d, e;
	float dists[RM_SKY_MAX_VERTS];
	int sides[RM_SKY_MAX_VERTS];
	float newv[2][RM_SKY_MAX_VERTS][3];
	int newc[2];
	int i, j;

	if ((nump < 0) || (nump > RM_SKY_MAX_VERTS - 2))
	{
		return 0;
	}

	if (stage == 6)
	{
		/* fully clipped, so draw it */
		rm_sky_draw_polygon(b, nump, vecs);
		return 1;
	}

	front = back = 0;
	norm = rm_skyclip[stage];

	for (i = 0, v = vecs; i < nump; i++, v += 3)
	{
		d = v[0] * norm[0] + v[1] * norm[1] + v[2] * norm[2];

		if (d > 0.1)
		{
			front = 1;
			sides[i] = 0;   /* SIDE_FRONT */
		}
		else if (d < -0.1)
		{
			back = 1;
			sides[i] = 1;   /* SIDE_BACK */
		}
		else
		{
			sides[i] = 2;   /* SIDE_ON */
		}

		dists[i] = d;
	}

	if (!front || !back)
	{
		/* not clipped */
		return rm_sky_clip_stage(b, nump, vecs, stage + 1);
	}

	/* clip it */
	sides[i] = sides[0];
	dists[i] = dists[0];
	vecs[i * 3] = vecs[0];
	vecs[i * 3 + 1] = vecs[1];
	vecs[i * 3 + 2] = vecs[2];
	newc[0] = newc[1] = 0;

	for (i = 0, v = vecs; i < nump; i++, v += 3)
	{
		/* a polygon that is not convex can cross a plane more often than a convex one: never past the buffers */
		if ((newc[0] >= RM_SKY_MAX_VERTS - 1) || (newc[1] >= RM_SKY_MAX_VERTS - 1))
		{
			return 0;
		}

		switch (sides[i])
		{
			case 0:
				memcpy(newv[0][newc[0]], v, 3 * sizeof(float));
				newc[0]++;
				break;
			case 1:
				memcpy(newv[1][newc[1]], v, 3 * sizeof(float));
				newc[1]++;
				break;
			default:
				memcpy(newv[0][newc[0]], v, 3 * sizeof(float));
				newc[0]++;
				memcpy(newv[1][newc[1]], v, 3 * sizeof(float));
				newc[1]++;
				break;
		}

		if ((sides[i] == 2) || (sides[i + 1] == 2) || (sides[i + 1] == sides[i]))
		{
			continue;
		}

		d = dists[i] / (dists[i] - dists[i + 1]);

		for (j = 0; j < 3; j++)
		{
			e = v[j] + d * (v[j + 3] - v[j]);
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}

		newc[0]++;
		newc[1]++;
	}

	/* continue */
	return rm_sky_clip_stage(b, newc[0], newv[0][0], stage + 1) &
		rm_sky_clip_stage(b, newc[1], newv[1][0], stage + 1);
}

int
rm_sky_clip(rm_skybounds_t *b, int nump, const float *vecs)
{
	float work[(RM_SKY_MAX_VERTS + 1) * 3];

	if ((nump < 0) || (nump > RM_SKY_MAX_VERTS - 2))
	{
		return 0;
	}

	memcpy(work, vecs, (size_t)nump * 3 * sizeof(float));
	return rm_sky_clip_stage(b, nump, work, 0);
}

int
rm_sky_prepare(rm_skybounds_t *b, float rotate)
{
	int i;

	if (!rotate)
	{
		return 1;
	}

	/* check for no sky at all */
	for (i = 0; i < 6; i++)
	{
		if ((b->mins[0][i] < b->maxs[0][i]) && (b->mins[1][i] < b->maxs[1][i]))
		{
			break;
		}
	}

	if (i == 6)
	{
		return 0; /* nothing visible */
	}

	for (i = 0; i < 6; i++)
	{
		b->mins[0][i] = -1;
		b->mins[1][i] = -1;
		b->maxs[0][i] = 1;
		b->maxs[1][i] = 1;
	}

	return 1;
}

int
rm_sky_face_visible(const rm_skybounds_t *b, int face)
{
	if ((face < 0) || (face >= 6))
	{
		return 0;
	}

	return !((b->mins[0][face] >= b->maxs[0][face]) || (b->mins[1][face] >= b->maxs[1][face]));
}

int
rm_sky_image_of_face(int face)
{
	return ((face >= 0) && (face < 6)) ? rm_skytexorder[face] : -1;
}

int
rm_sky_vec(float s, float t, int face, int farsee, float *v, float *st)
{
	float b[3];
	int j, k, idx;
	const float sky_min = 1.0 / 512, sky_max = 511.0 / 512;

	if ((face < 0) || (face >= 6))
	{
		return 0;
	}

	if (!farsee)
	{
		b[0] = s * 2300;
		b[1] = t * 2300;
		b[2] = 2300;
	}
	else
	{
		b[0] = s * 4096;
		b[1] = t * 4096;
		b[2] = 4096;
	}

	for (j = 0; j < 3; j++)
	{
		k = rm_st_to_vec[face][j];
		idx = (k < 0) ? (-k - 1) : (k - 1);

		if ((idx < 0) || (idx > 2))
		{
			return 0;
		}

		v[j] = (k < 0) ? -b[idx] : b[idx];
	}

	/* avoid bilerp seam */
	s = (s + 1) * 0.5;
	t = (t + 1) * 0.5;

	if (s < sky_min)
	{
		s = sky_min;
	}
	else if (s > sky_max)
	{
		s = sky_max;
	}

	if (t < sky_min)
	{
		t = sky_min;
	}
	else if (t > sky_max)
	{
		t = sky_max;
	}

	t = 1.0 - t;

	st[0] = s;
	st[1] = t;
	return 1;
}

void
rm_sky_matrix(float *out, const float *world, const float *origin, float time, float rotate,
		const float *axis)
{
	float t[16];

	mtl_mat_translate(t, origin[0], origin[1], origin[2]);
	mtl_mat_mul(out, world, t);
	rm_mat_rotate_on(out, time * rotate, axis[0], axis[1], axis[2]);
}

int
rm_sky_name_ok(const char *name)
{
	size_t i, n;

	if (!name)
	{
		return 0;
	}

	n = strnlen(name, RM_SKY_NAME_MAX + 1);

	if ((n < 1) || (n > RM_SKY_NAME_MAX) || (name[0] == '.'))
	{
		return 0;
	}

	for (i = 0; i < n; i++)
	{
		char c = name[i];

		if (!(((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) ||
			(c == '_') || (c == '-') || (c == '.')))
		{
			return 0;
		}

		if ((c == '.') && (name[i + 1] == '.'))
		{
			return 0;
		}
	}

	return 1;
}

int
rm_sky_params_ok(float rotate, const float *axis)
{
	return isfinite(rotate) && isfinite(axis[0]) && isfinite(axis[1]) && isfinite(axis[2]);
}

float
rm_trans_alpha(int texflags)
{
	if (texflags & RM_TEX_TRANS33)
	{
		return 0.33f;
	}

	if (texflags & RM_TEX_TRANS66)
	{
		return 0.66f;
	}

	return 1.0f;
}

void
rm_trans_color(float inverse_intensity, float alpha, float *rgba)
{
	rgba[0] = rgba[1] = rgba[2] = inverse_intensity;
	rgba[3] = alpha;
}

float
rm_inverse_intensity(float gamma, float intensity)
{
	return gamma / intensity;
}

int
rm_warp_color(int overbright, float inverse_intensity, float *rgba)
{
	if (overbright)
	{
		rgba[0] = rgba[1] = rgba[2] = rgba[3] = 1.0f;
		return 0;
	}

	rgba[0] = rgba[1] = rgba[2] = inverse_intensity;
	rgba[3] = 1.0f;
	return 1;
}

int
rm_world_frame(float time)
{
	return rm_itrunc(time * 2);
}

int
rm_alpha_break(long count, int cur_tex, float cur_alpha, int tex, float alpha)
{
	return (count > 0) && ((cur_tex != tex) || (cur_alpha != alpha));
}

/* ---- alias models, the view weapon and brush entities (N7) ---------------------------- */

/* shared.h's vector macros, which rm_pure.c does not include: the same text, so the same arithmetic */
#define RM_DotProduct(x, y) (x[0] * y[0] + x[1] * y[1] + x[2] * y[2])

/* gl1_mesh.c's tables */
static const float rm_avertexnormals[RM_NUMVERTEXNORMALS][3] = {
#include "../constants/anorms.h"
};

static const float rm_avertexnormal_dots[RM_SHADEDOT_QUANT][256] = {
#include "../constants/anormtab.h"
};

/* a little-endian int of the file, as LittleLong reads it on a little-endian host */
static long
rm_le32(const unsigned char *p)
{
	unsigned u = (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
	int i;

	memcpy(&i, &u, sizeof(i));
	return i;
}

static int
rm_le_float_ok(const unsigned char *p)
{
	unsigned u = (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
	float f;

	memcpy(&f, &u, sizeof(f));
	return isfinite(f) != 0;
}

int
rm_alias_region(mtl_ring_t *ring, long nv, long ni, rm_region_t *r)
{
	mtl_ring_t saved;
	long voff, ioff;

	memset(r, 0, sizeof(*r));

	if (!ring || (nv < 3) || (nv > RM_ALIAS_MAX_VERTS) || (ni < 3) || (ni > RM_ALIAS_MAX_INDICES) || (ni % 3))
	{
		return 0;
	}

	saved = *ring;
	voff = mtl_ring_alloc(ring, (size_t)nv * RM_AVERT_STRIDE);
	ioff = (voff < 0) ? -1 : mtl_ring_alloc(ring, (size_t)ni * sizeof(unsigned short));

	if (ioff < 0)
	{
		*ring = saved;
		return 0;
	}

	r->voff = voff;
	r->ioff = ioff;
	r->nverts = (unsigned long)nv;
	r->nindices = (unsigned long)ni;
	r->vbytes = (size_t)nv * RM_AVERT_STRIDE;
	r->ibytes = (size_t)ni * sizeof(unsigned short);
	return 1;
}

/* ofs + cnt * size inside 0 .. end, in 64 bits (the loader's MD2_RANGE_BAD, negated) */
static int
rm_md2_range_ok(long ofs, long cnt, long size, long end)
{
	return (ofs >= 0) && ((long long)ofs + (long long)cnt * (long long)size <= (long long)end);
}

/* the command stream of norder ints at cmds: 1 with the expanded counts when gl1's draw can walk it */
static int
rm_md2_stream_ok(const unsigned char *cmds, long norder, long num_xyz, long *verts, long *indices)
{
	long pos = 0;

	*verts = *indices = 0;

	for (;;)
	{
		long c, n, k;

		if (pos >= norder)
		{
			return 0;   /* no terminating 0 */
		}

		c = rm_le32(cmds + 4 * pos);
		pos++;

		if (c == 0)
		{
			return *verts > 0;
		}

		n = (c < 0) ? -c : c;

		if ((n < 3) || (n > (norder - pos) / 3))
		{
			return 0;
		}

		for (k = 0; k < n; k++)
		{
			const unsigned char *v = cmds + 4 * (pos + 3 * k);
			long xyz = rm_le32(v + 8);

			if (!rm_le_float_ok(v) || !rm_le_float_ok(v + 4) || (xyz < 0) || (xyz >= num_xyz))
			{
				return 0;
			}
		}

		pos += 3 * n;
		*verts += n;
		*indices += 3 * (n - 2);

		if (*verts > RM_ALIAS_MAX_VERTS)
		{
			return 0;
		}
	}
}

int
rm_md2_check(const unsigned char *buf, long len, rm_md2_info_t *info)
{
	long h[RM_MD2_HEADER / 4], skins, ofs_end, f, i;

	memset(info, 0, sizeof(*info));

	if (!buf || (len < RM_MD2_HEADER))
	{
		return 0;
	}

	for (i = 0; i < RM_MD2_HEADER / 4; i++)
	{
		h[i] = rm_le32(buf + 4 * i);
	}

	/* dmdl_t: ident, version, skinwidth, skinheight, framesize, num_skins, num_xyz, num_st, num_tris,
	   num_glcmds, num_frames, ofs_skins, ofs_st, ofs_tris, ofs_frames, ofs_glcmds, ofs_end */
	ofs_end = h[16];

	/* the loader's conditions, in its order */
	if ((h[0] != RM_MD2_IDENT) || (h[1] != RM_MD2_VERSION) || (ofs_end < 0) || (ofs_end > len) ||
		(h[3] > RM_MD2_MAX_SKINHEIGHT) || (h[6] <= 0) || (h[6] > RM_MD2_MAX_VERTS) || (h[7] <= 0) ||
		(h[8] <= 0) || (h[10] <= 0))
	{
		return 0;
	}

	skins = (h[5] > RM_MD2_MAX_SKINS) ? RM_MD2_MAX_SKINS : h[5];   /* the loader keeps the first 32 */

	if ((skins < 0) || (h[9] <= 0) || (h[4] < RM_MD2_FRAME_HEADER + 4 * h[6]) ||
		!rm_md2_range_ok(h[11], skins, RM_MD2_SKINNAME, ofs_end) ||
		!rm_md2_range_ok(h[12], h[7], 4, ofs_end) ||
		!rm_md2_range_ok(h[13], h[8], 12, ofs_end) ||
		!rm_md2_range_ok(h[14], h[10], h[4], ofs_end) ||
		!rm_md2_range_ok(h[15], h[9], 4, ofs_end))
	{
		return 0;
	}

	/* what the loader reads in place and gl1's draw trusts */
	if ((h[14] % 4) || (h[4] % 4) || (h[15] % 4) || (h[12] % 2) || (h[13] % 2))
	{
		return 0;
	}

	for (i = 0; i < skins; i++)
	{
		if (!memchr(buf + h[11] + i * RM_MD2_SKINNAME, 0, RM_MD2_SKINNAME))
		{
			return 0;
		}
	}

	for (f = 0; f < h[10]; f++)
	{
		const unsigned char *frame = buf + h[14] + f * h[4];

		for (i = 0; i < 6; i++)
		{
			if (!rm_le_float_ok(frame + 4 * i))
			{
				return 0;
			}
		}

		for (i = 0; i < h[6]; i++)
		{
			if (frame[RM_MD2_FRAME_HEADER + 4 * i + 3] >= RM_NUMVERTEXNORMALS)
			{
				return 0;
			}
		}
	}

	if (!rm_md2_stream_ok(buf + h[15], h[9], h[6], &info->verts, &info->indices))
	{
		info->verts = info->indices = 0;
		return 0;
	}

	info->num_xyz = (int)h[6];
	info->num_frames = (int)h[10];
	info->num_skins = (int)skins;
	info->framesize = (int)h[4];
	info->ofs_frames = (int)h[14];
	info->ofs_glcmds = (int)h[15];
	info->num_glcmds = (int)h[9];
	return 1;
}

int
rm_sp2_check(const unsigned char *buf, long len)
{
	long n, i;

	if (!buf || (len < RM_SP2_HEADER) || (rm_le32(buf) != RM_SP2_IDENT) || (rm_le32(buf + 4) != RM_SP2_VERSION))
	{
		return 0;
	}

	n = rm_le32(buf + 8);

	if (n <= 0)
	{
		return 0;
	}

	if (n > RM_MD2_MAX_SKINS)
	{
		n = RM_MD2_MAX_SKINS;   /* the loader keeps the first 32 */
	}

	if (!rm_md2_range_ok(RM_SP2_HEADER, n, RM_SP2_FRAME, len))
	{
		return 0;
	}

	for (i = 0; i < n; i++)
	{
		if (!memchr(buf + RM_SP2_HEADER + i * RM_SP2_FRAME + 16, 0, RM_MD2_SKINNAME))
		{
			return 0;
		}
	}

	return 1;
}

/* from gl1_mesh.c, R_CullAliasModel (cull) and R_DrawAliasModel */
void
rm_alias_frames(int *frame, int *oldframe, int numframes, int cull)
{
	if (cull)
	{
		if ((*frame >= numframes) || (*frame < 0))
		{
			*frame = 0;
		}

		if ((*oldframe >= numframes) || (*oldframe < 0))
		{
			*oldframe = 0;
		}

		return;
	}

	if ((*frame >= numframes) || (*frame < 0))
	{
		*frame = 0;
		*oldframe = 0;
	}

	if ((*oldframe >= numframes) || (*oldframe < 0))
	{
		*frame = 0;
		*oldframe = 0;
	}
}

/* from gl1_mesh.c, R_DrawAliasModel's select skin */
int
rm_alias_skin(int skinnum, const void *const *skins)
{
	if ((skinnum >= RM_MD2_MAX_SKINS) || (skinnum < 0))
	{
		return 0;
	}

	return skins[skinnum] ? skinnum : 0;
}

/* from gl1_mesh.c, R_DrawAliasFrameLerp */
void
rm_alias_lerp_setup(const float *fscale, const float *ftranslate, const float *oscale, const float *otranslate,
		float backlerp, const float *delta, const float *forward, const float *right, const float *up,
		float *move, float *frontv, float *backv)
{
	float frontlerp;
	int i;

	frontlerp = 1.0 - backlerp;

	/* move should be the delta back to the previous frame * backlerp */
	move[0] = RM_DotProduct(delta, forward); /* forward */
	move[1] = -RM_DotProduct(delta, right); /* left */
	move[2] = RM_DotProduct(delta, up); /* up */

	move[0] = move[0] + otranslate[0];
	move[1] = move[1] + otranslate[1];
	move[2] = move[2] + otranslate[2];

	for (i = 0; i < 3; i++)
	{
		move[i] = backlerp * move[i] + frontlerp * ftranslate[i];
	}

	for (i = 0; i < 3; i++)
	{
		frontv[i] = frontlerp * fscale[i];
		backv[i] = backlerp * oscale[i];
	}
}

/* from gl1_mesh.c, R_LerpVerts; a light normal index is below RM_NUMVERTEXNORMALS (rm_md2_check), bounded again */
void
rm_alias_lerp(int n, const unsigned char *v, const unsigned char *ov, int shell, const float *move,
		const float *frontv, const float *backv, float (*out)[4])
{
	int i;

	if (shell)
	{
		for (i = 0; i < n; i++, v += 4, ov += 4)
		{
			const int ni = (v[3] < RM_NUMVERTEXNORMALS) ? v[3] : 0;
			const float *normal = rm_avertexnormals[ni];

			out[i][0] = move[0] + ov[0] * backv[0] + v[0] * frontv[0] +
					  normal[0] * RM_POWERSUIT_SCALE;
			out[i][1] = move[1] + ov[1] * backv[1] + v[1] * frontv[1] +
					  normal[1] * RM_POWERSUIT_SCALE;
			out[i][2] = move[2] + ov[2] * backv[2] + v[2] * frontv[2] +
					  normal[2] * RM_POWERSUIT_SCALE;
		}
	}
	else
	{
		for (i = 0; i < n; i++, v += 4, ov += 4)
		{
			out[i][0] = move[0] + ov[0] * backv[0] + v[0] * frontv[0];
			out[i][1] = move[1] + ov[1] * backv[1] + v[1] * frontv[1];
			out[i][2] = move[2] + ov[2] * backv[2] + v[2] * frontv[2];
		}
	}
}

int
rm_alias_lit(int flags)
{
	return !(flags & (RM_RF_SHELLS | RM_RF_FULLBRIGHT));
}

/* from gl1_mesh.c, R_DrawAliasModel's lighting information */
void
rm_alias_shade(int flags, int rdflags, const float *point, float time, float overbright, float *shadelight)
{
	int i;

	if (flags & RM_RF_SHELLS)
	{
		shadelight[0] = shadelight[1] = shadelight[2] = 0;

		if (flags & RM_RF_SHELL_HALF_DAM)
		{
			shadelight[0] = 0.56;
			shadelight[1] = 0.59;
			shadelight[2] = 0.45;
		}

		if (flags & RM_RF_SHELL_DOUBLE)
		{
			shadelight[0] = 0.9;
			shadelight[1] = 0.7;
		}

		if (flags & RM_RF_SHELL_RED)
		{
			shadelight[0] = 1.0;
		}

		if (flags & RM_RF_SHELL_GREEN)
		{
			shadelight[1] = 1.0;
		}

		if (flags & RM_RF_SHELL_BLUE)
		{
			shadelight[2] = 1.0;
		}
	}
	else if (flags & RM_RF_FULLBRIGHT)
	{
		for (i = 0; i < 3; i++)
		{
			shadelight[i] = 1.0;
		}
	}
	else
	{
		for (i = 0; i < 3; i++)
		{
			shadelight[i] = point[i];
		}
	}

	if (flags & RM_RF_MINLIGHT)
	{
		for (i = 0; i < 3; i++)
		{
			if (shadelight[i] > 0.1)
			{
				break;
			}
		}

		if (i == 3)
		{
			shadelight[0] = 0.1;
			shadelight[1] = 0.1;
			shadelight[2] = 0.1;
		}
	}

	if (flags & RM_RF_GLOW)
	{
		/* bonus items will pulse with time */
		float scale;
		float min;

		scale = 0.1 * sin(time * 7);

		for (i = 0; i < 3; i++)
		{
			min = shadelight[i] * 0.8;
			shadelight[i] += scale;

			if (shadelight[i] < min)
			{
				shadelight[i] = min;
			}
		}
	}

	/* gl1_overbrightbits applies to the mesh too, or it appears dimmer relative to walls */
	if (overbright)
	{
		for (i = 0; i < 3; ++i)
		{
			shadelight[i] *= overbright;
		}
	}

	/* ir goggles color override */
	if ((rdflags & RM_RDF_IRGOGGLES) && (flags & RM_RF_IR_VISIBLE))
	{
		shadelight[0] = 1.0;
		shadelight[1] = 0.0;
		shadelight[2] = 0.0;
	}
}

/* from gl1_main.c, R_SetLightLevel: pick the greatest component */
float
rm_light_level(const float *shadelight)
{
	if (shadelight[0] > shadelight[1])
	{
		if (shadelight[0] > shadelight[2])
		{
			return 150 * shadelight[0];
		}

		return 150 * shadelight[2];
	}

	if (shadelight[1] > shadelight[2])
	{
		return 150 * shadelight[1];
	}

	return 150 * shadelight[2];
}

/* from gl1_mesh.c: shadedots = r_avertexnormal_dots[((int)(angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)] */
int
rm_shadedot_row(float yaw)
{
	return rm_itrunc(yaw * (RM_SHADEDOT_QUANT / 360.0)) & (RM_SHADEDOT_QUANT - 1);
}

float
rm_shadedot(int row, int normal)
{
	return rm_avertexnormal_dots[row & (RM_SHADEDOT_QUANT - 1)][normal & 255];
}

unsigned char
rm_alias_alpha(int flags, float alpha)
{
	float a = (flags & RM_RF_TRANSLUCENT) ? alpha : 1.0;
	float v = a * 255;

	/* gl1's conversion of alpha * 255 to a byte, defined from 0 to 255 */
	if (!(v >= 0))
	{
		return 0;
	}

	if (v >= 255)
	{
		return 255;
	}

	return (unsigned char)v;
}

/* gl1's truncation of a light to a colour index, and Q_clamp(idx, 0, 255) */
static int
rm_alias_index(float f)
{
	int idx = rm_itrunc(f);

	return (0 > idx) ? 0 : (idx > 255) ? 255 : idx;
}

/* from gl1_mesh.c, R_DrawAliasFrameLerp, with GLBUFFER_COLOR and the shim's reading of the bytes */
void
rm_alias_color(float l, const float *shadelight, int shell, const unsigned char *minlight,
		const unsigned char *gamma, unsigned char alpha, float *rgba)
{
	int i;

	for (i = 0; i < 3; i++)
	{
		int idx;

		if (shell)
		{
			idx = rm_alias_index(shadelight[i] * 255);
		}
		else
		{
			idx = rm_alias_index(l * shadelight[i] * 255);
		}

		if (minlight)
		{
			idx = minlight[idx];
		}

		rgba[i] = gamma[idx] / 255.0f;
	}

	rgba[3] = alpha / 255.0f;
}

long
rm_alias_emit(const int *order, long norder, const float (*lerped)[4], const unsigned char *verts, int num_xyz,
		int row, const float *shadelight, int shell, const unsigned char *minlight, const unsigned char *gamma,
		unsigned char alpha, rm_avert_t *out, long vcap, unsigned short *idx, long icap, long *nidx)
{
	long pos = 0, nv = 0, ni = 0;

	*nidx = 0;

	for (;;)
	{
		long count, i, fan;

		/* get the vertex count and primitive type */
		if (pos >= norder)
		{
			return -1;
		}

		count = order[pos++];

		if (!count)
		{
			break; /* done */
		}

		fan = (count < 0);

		if (fan)
		{
			count = -count;
		}

		if ((count < 3) || (count > (norder - pos) / 3) || (count > vcap - nv) || (3 * (count - 2) > icap - ni))
		{
			return -1;
		}

		/* R_SetBufferIndices */
		for (i = 0; i < count - 2; i++)
		{
			if (fan)
			{
				idx[ni] = (unsigned short)nv;
				idx[ni + 1] = (unsigned short)(nv + i + 1);
				idx[ni + 2] = (unsigned short)(nv + i + 2);
			}
			else if (i % 2 == 0)
			{
				idx[ni] = (unsigned short)(nv + i);
				idx[ni + 1] = (unsigned short)(nv + i + 1);
				idx[ni + 2] = (unsigned short)(nv + i + 2);
			}
			else /* backwards order */
			{
				idx[ni] = (unsigned short)(nv + i + 2);
				idx[ni + 1] = (unsigned short)(nv + i + 1);
				idx[ni + 2] = (unsigned short)(nv + i);
			}

			ni += 3;
		}

		do
		{
			rm_avert_t *o = &out[nv];
			const int index_xyz = order[pos + 2];

			if ((index_xyz < 0) || (index_xyz >= num_xyz))
			{
				return -1;
			}

			memset(o, 0, sizeof(*o));
			o->pos[0] = lerped[index_xyz][0];
			o->pos[1] = lerped[index_xyz][1];
			o->pos[2] = lerped[index_xyz][2];
			o->pos[3] = 1.0f;

			if (shell)
			{
				rm_alias_color(0, shadelight, 1, minlight, gamma, alpha, o->color);
			}
			else
			{
				/* texture coordinates come from the draw list */
				memcpy(&o->uv[0], &order[pos], sizeof(float));
				memcpy(&o->uv[1], &order[pos + 1], sizeof(float));

				/* normals and vertexes come from the frame list */
				rm_alias_color(rm_shadedot(row, verts[4 * index_xyz + 3]), shadelight, 0, minlight, gamma,
						alpha, o->color);
			}

			pos += 3;
			nv++;
		}
		while (--count);
	}

	*nidx = ni;
	return nv;
}

/* from gl1_mesh.c, R_CullAliasModel */
int
rm_alias_culled(const float *fscale, const float *ftranslate, const float *oscale, const float *otranslate,
		int same, const float *origin, const float *forward, const float *right, const float *up,
		const float (*normal)[3], const float *dist)
{
	float mins[3], maxs[3], bbox[8][3];
	int i, p, f, aggregatemask = ~0;

	/* compute axially aligned mins and maxs */
	if (same)
	{
		for (i = 0; i < 3; i++)
		{
			mins[i] = ftranslate[i];
			maxs[i] = mins[i] + fscale[i] * 255;
		}
	}
	else
	{
		for (i = 0; i < 3; i++)
		{
			float thismins, oldmins, thismaxs, oldmaxs;

			thismins = ftranslate[i];
			thismaxs = thismins + fscale[i] * 255;

			oldmins = otranslate[i];
			oldmaxs = oldmins + oscale[i] * 255;

			if (thismins < oldmins)
			{
				mins[i] = thismins;
			}
			else
			{
				mins[i] = oldmins;
			}

			if (thismaxs > oldmaxs)
			{
				maxs[i] = thismaxs;
			}
			else
			{
				maxs[i] = oldmaxs;
			}
		}
	}

	/* compute a full bounding box, rotated */
	for (i = 0; i < 8; i++)
	{
		float tmp[3];

		tmp[0] = (i & 1) ? mins[0] : maxs[0];
		tmp[1] = (i & 2) ? mins[1] : maxs[1];
		tmp[2] = (i & 4) ? mins[2] : maxs[2];

		bbox[i][0] = RM_DotProduct(forward, tmp);
		bbox[i][1] = -RM_DotProduct(right, tmp);
		bbox[i][2] = RM_DotProduct(up, tmp);

		bbox[i][0] = origin[0] + bbox[i][0];
		bbox[i][1] = origin[1] + bbox[i][1];
		bbox[i][2] = origin[2] + bbox[i][2];
	}

	for (p = 0; p < 8; p++)
	{
		int mask = 0;

		for (f = 0; f < 4; f++)
		{
			float dp = RM_DotProduct(normal[f], bbox[p]);

			if ((dp - dist[f]) < 0)
			{
				mask |= (1 << f);
			}
		}

		aggregatemask &= mask;
	}

	return aggregatemask != 0;
}

/* from gl1_main.c, R_RotateForEntity, on the shim's matrices */
void
rm_entity_matrix(float *out, const float *world, const float *origin, const float *a)
{
	float t[16];

	memmove(out, world, 16 * sizeof(float));
	mtl_mat_translate(t, origin[0], origin[1], origin[2]);
	mtl_mat_mul(out, out, t);

	rm_mat_rotate_on(out, a[1], 0, 0, 1);
	rm_mat_rotate_on(out, -a[0], 0, 1, 0);
	rm_mat_rotate_on(out, -a[2], 1, 0, 0);
}

/* from gl1_buffer.c, R_ApplyGLBuffer with RF_WEAPONMODEL */
int
rm_weapon_projection(float *m, float fovy, float gunfov, float hand, int width, int height, float znear,
		int farsee, int underwater, float time, float waterwarp)
{
	mtl_mat_identity(m);

	if (hand == 1.0f)
	{
		float s[16];

		mtl_mat_scale(s, -1, 1, 1);
		mtl_mat_mul(m, m, s);
	}

	return rm_perspective_on(m, (gunfov < 0) ? fovy : gunfov, width, height, znear, farsee, underwater, time,
			waterwarp);
}

/* from gl1_buffer.c (glDepthRange) and gl1_main.c's R_Clear without gl1_ztrick: gldepthmin 0, gldepthmax 1 */
void
rm_alias_depth(int flags, double *znear, double *zfar)
{
	const double gldepthmin = 0, gldepthmax = 1;

	*znear = gldepthmin;
	*zfar = gldepthmax;

	if (flags & RM_RF_DEPTHHACK)
	{
		/* hack the depth range to prevent view model from poking into walls */
		*zfar = gldepthmin + 0.3 * (gldepthmax - gldepthmin);
	}
}

int
rm_alias_cull_mode(int cull, int flags, float hand)
{
	if (!cull)
	{
		return RMB_CULL_NONE;
	}

	return ((flags & RM_RF_WEAPONMODEL) && (hand == 1.0f)) ? RMB_CULL_BACK : RMB_CULL_FRONT;
}

int
rm_alias_drawn(int flags, float hand)
{
	return !((flags & RM_RF_WEAPONMODEL) && (hand == 2));
}

/* from gl1_surf.c, R_DrawBrushModel */
int
rm_brush_bounds(const float *origin, const float *angles, const float *mins, const float *maxs, float radius,
		float *bmins, float *bmaxs)
{
	int i;

	if (angles[0] || angles[1] || angles[2])
	{
		for (i = 0; i < 3; i++)
		{
			bmins[i] = origin[i] - radius;
			bmaxs[i] = origin[i] + radius;
		}

		return 1;
	}

	for (i = 0; i < 3; i++)
	{
		bmins[i] = origin[i] + mins[i];
		bmaxs[i] = origin[i] + maxs[i];
	}

	return 0;
}

void
rm_brush_modelorg(const float *vieworg, const float *origin, int rotated, const float *forward,
		const float *right, const float *up, float *modelorg)
{
	modelorg[0] = vieworg[0] - origin[0];
	modelorg[1] = vieworg[1] - origin[1];
	modelorg[2] = vieworg[2] - origin[2];

	if (rotated)
	{
		float temp[3];

		temp[0] = modelorg[0];
		temp[1] = modelorg[1];
		temp[2] = modelorg[2];
		modelorg[0] = RM_DotProduct(temp, forward);
		modelorg[1] = -RM_DotProduct(temp, right);
		modelorg[2] = RM_DotProduct(temp, up);
	}
}

int
rm_brush_face_visible(const float *modelorg, const float *normal, float dist, int planeback)
{
	const float dot = RM_DotProduct(modelorg, normal) - dist;

	return (planeback && (dot < -0.01)) || (!planeback && (dot > 0.01));
}

int
rm_entity_floats_ok(const float *origin, const float *oldorigin, const float *angles, float backlerp,
		float alpha)
{
	int i;

	for (i = 0; i < 3; i++)
	{
		if (!isfinite(origin[i]) || !isfinite(oldorigin[i]) || !isfinite(angles[i]))
		{
			return 0;
		}
	}

	return isfinite(backlerp) && isfinite(alpha);
}
