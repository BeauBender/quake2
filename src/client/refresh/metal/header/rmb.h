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
 * The back end of ref_metal in C: the only door to Metal
 * (docs/plans/ref-metal-native.md, sections 2 and 3). The rmb_*
 * functions are implemented in Objective-C (rmb_device.m, rmb_encode.m);
 * the rm_* functions below them are plain C with neither SDL nor Metal
 * (rm_pure.c) and are host-tested by tests/metal/rm_host_test.c. No
 * Objective-C type and no system header beyond the C library appears
 * here.
 *
 * =======================================================================
 */

#ifndef RM_RMB_H
#define RM_RMB_H

#include <stddef.h>

/* ---- capabilities ----------------------------------------------------------- */

/* The bit of Apple GPU family n (1 to 9) in rmb_caps_t.families. */
#define RMB_FAMILY_BIT(n) (1u << ((n) - 1))
#define RMB_FAMILY_FIRST 1
#define RMB_FAMILY_LAST 9
/* ref_metal refuses a device below this family (Init fails). */
#define RMB_FAMILY_FLOOR 5

typedef struct
{
	char device[128];
	unsigned families;          /* RMB_FAMILY_BIT(n) for every apple n the device supports */
	int raytracing;             /* supportsRaytracing */
	int raytracing_render;      /* supportsRaytracingFromRender (macOS 14) */
} rmb_caps_t;

/* ---- the object ledger and the counters ------------------------------------- */

typedef enum
{
	RM_OBJ_DEVICE,
	RM_OBJ_QUEUE,
	RM_OBJ_VIEW,
	RM_OBJ_TEXTURE,
	RM_OBJ_BUFFER,
	RM_OBJ_KINDS
} rm_obj_kind_t;

typedef struct
{
	unsigned long created[RM_OBJ_KINDS];
	unsigned long destroyed[RM_OBJ_KINDS];
	unsigned long unmatched;    /* destroys without a create: a bookkeeping bug */
	unsigned long unreleased;   /* released by ref_metal but still alive afterwards (weak check) */
} rm_ledger_t;

typedef struct
{
	unsigned long frames;       /* frames ended by rmb_frame_end */
	unsigned long no_drawable;  /* of those, frames without a drawable (not presented) */
	unsigned long skipped;      /* frames skipped: no frame slot within the wait */
	unsigned long failed;       /* command buffers that ended in error */
	unsigned long readbacks;    /* successful rmb_read calls */
	unsigned long noop_calls;   /* calls to entry points not implemented yet (front end) */
	unsigned long draws;        /* draws encoded */
	unsigned long uploads;      /* texture uploads blitted */
	unsigned long upload_skips; /* uploads refused by the size check or a failed allocation */
	unsigned long draw_skips;   /* draws refused: no texture, no pipeline, no ring space */
	unsigned long splits;       /* frame command buffers split for a rewritten texture (section 3) */
	unsigned long pipelines;    /* pipeline states built in InitContext */
	unsigned long late_pipelines;   /* pipeline states built after InitContext: must stay 0 */
	/* the surfaces of the last 3D view not drawn yet, by class (front end, N4): drawn from N6 on */
	unsigned long sky_skips, warp_skips, trans_skips, flow_skips;
	/* of the drawn surfaces of the last 3D view, those a dynamic light touches, drawn with their static lightmap (N5; N8 adds the light) */
	unsigned long dlit_surfs;
} rmb_stats_t;

/* A buffer of this size holds every metal_stats line (rm_stats_format, rm_world_format, host-tested). */
#define RM_STATS_LINE 768

/* ---- rm_pure.c: no SDL, no Metal --------------------------------------------- */

/* 1 when the families include RMB_FAMILY_FLOOR or a later one. */
int rm_caps_floor_ok(unsigned families);

/*
 * Writes the names of the families 5 to 9 the mask holds ("apple5
 * apple7"), or "none", NUL-terminated. Returns 1 when it fits in len,
 * 0 when it was truncated (or len is 0).
 */
int rm_caps_family_list(unsigned families, char *buf, size_t len);

/*
 * BGRA rows top down (Metal's texture origin), src_stride bytes apart,
 * to tightly packed RGB rows top down: what gl1's R_ScreenShot hands to
 * ri.Vid_WriteScreenshot after its row swap.
 */
void rm_bgra_to_rgb(const unsigned char *bgra, size_t src_stride, int w, int h, unsigned char *rgb);

void rm_ledger_create(rm_ledger_t *l, rm_obj_kind_t kind);
void rm_ledger_destroy(rm_ledger_t *l, rm_obj_kind_t kind);
unsigned long rm_ledger_live(const rm_ledger_t *l);

/* The teardown line, without a newline. Returns 1 when it fits, 0 when truncated. */
int rm_ledger_format(const rm_ledger_t *l, char *buf, size_t len);

/* The metal_stats line, without a newline. Returns 1 when it fits, 0 when truncated. */
int rm_stats_format(const rmb_stats_t *s, const rm_ledger_t *l, char *buf, size_t len);

typedef enum
{
	RM_LOAD_CLEAR,   /* cleared to the pass's clear colour */
	RM_LOAD_KEEP,    /* loaded: what the target holds */
	RM_LOAD_BLACK    /* cleared to black: the frame's first pass, or no pass has written the target yet */
} rm_load_t;

/*
 * The colour load of a pass: clear is 1 when the pass clears (r_clear at
 * the start of the frame), defined is 1 when a pass has written the
 * colour target since it was made, first is 1 for the frame's first
 * pass. A frame gl1 does not clear starts black, as the shim's does
 * (mgl_layer_pass: "so a frame is reproducible"), not on the last frame:
 * the world's cracks show what is under them (design section 19).
 */
rm_load_t rm_color_load(int clear, int defined, int first);

/*
 * 1 when rmb_frame_begin must (re)make the colour and depth targets for
 * a w by h frame: either is missing (a failed allocation is retried on
 * the next frame) or the colour target is cw by ch, another size.
 */
int rm_targets_rebuild(int have_color, int have_depth, int cw, int ch, int w, int h);

/* ---- images, the 2D draws (rm_pure.c) ------------------------------------------- */

/*
 * The bytes of a w by h rgba8 upload. 1 with *bytes set when w and h are
 * positive, neither is above RMB_MAX_TEXTURE_SIDE and w * h * 4 fits in
 * cap (the staging ring, RMB_STAGING_BYTES), 0 otherwise: no upload of
 * that size is staged. The product is checked for overflow before it is
 * formed.
 */
int rm_upload_bytes(long w, long h, size_t cap, size_t *bytes);

/*
 * gl1's RDraw_StretchRaw, 8-bit frame (non-power-of-two path): each of
 * cols * rows palette indices becomes its rgba8 entry of palette (r_rawpalette,
 * gl1's byte order: r, g, b, 255 in memory), at the frame's own size, into
 * out, which holds out_len bytes. 0 and nothing written when cols or rows is
 * not positive or out cannot hold cols * rows * 4 bytes.
 */
int rm_raw8_to_rgba(const unsigned char *data, int cols, int rows, const unsigned *palette,
		unsigned char *out, size_t out_len);

/*
 * The triangles of a fan of n vertices (GL_TRIANGLE_FAN, the shim's order:
 * 0, i, i + 1 for i from 1 to n - 2) into idx, which holds cap entries.
 * Returns the number of indices, 0 when n < 3 or they do not fit.
 */
int rm_fan_indices(int n, unsigned short *idx, int cap);

/* What a 2D draw does with its texture (the shim's texture environments). */
typedef enum
{
	RM_TEXENV_REPLACE,
	RM_TEXENV_MODULATE,
	RM_TEXENV_UNTEXTURED,   /* the vertex colour only */
	RM_TEXENVS
} rm_texenv_t;

/*
 * The index of a 2D pipeline in the table InitContext builds: texture
 * environment, gl1's alpha test (GL_GREATER 0.666) on or off, blending
 * (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) on or off. -1 when the texenv is
 * out of range.
 */
#define RM_PIPE_2D_COUNT (RM_TEXENVS * 2 * 2)
int rm_pipe_2d(rm_texenv_t env, int alpha_test, int blend);

/*
 * 1 when the 2D pipeline (an index rm_pipe_2d answers) samples a texture,
 * so a draw on it without one is refused; 0 for the untextured ones and
 * for an index outside the table.
 */
int rm_pipe_2d_textured(int pipe);

/*
 * gl_texturemode (gl1's names, R_TextureMode in gl1_image.c) to the
 * minifying filter (0 to 5: GL_NEAREST, GL_LINEAR, then the four mipmap
 * filters in GL's enum order) and the magnifying one (0 nearest, 1
 * linear). 0 for a name gl1 does not know (nothing written).
 */
int rm_texture_mode(const char *name, int *min, int *mag_linear);

/*
 * The directory part of path ("." for a bare name) into dir, which holds
 * len bytes. 1 when it fits; 0, with dir empty when len > 0, otherwise.
 */
int rm_dir_of(const char *path, char *dir, size_t len);

/* ---- the scrap, pics and characters (rm_pure.c) ---------------------------------- */

/* gl1's scrap: four atlases of 256 by 256 texels, the first two without filtering */
#define RM_SCRAP_WIDTH 256
#define RM_SCRAP_HEIGHT 256
#define RM_MAX_SCRAPS 4
#define RM_MAX_SCRAPS_NOLERP 2

/*
 * gl1's CommonAllocBlock: a w by h block (the border already added) in an
 * atlas whose column heights are allocated[aw]. 1 with *x, *y the block's
 * corner and the columns raised; 0 and nothing written when it does not fit
 * or is larger than the atlas (gl1 computes aw - w unsigned there; a width
 * above aw is refused here). The spots are gl1's: the rightmost column
 * start is aw - w - 1.
 */
int rm_scrap_alloc(int *allocated, int aw, int ah, int w, int h, int *x, int *y);

/*
 * Copies a w by h block of texels (rows tight) to (x, y) of an atlas of
 * aw by ah texels. 0 and nothing written when the block is not inside it.
 */
int rm_scrap_copy(unsigned *texels, int aw, int ah, int x, int y, int w, int h, const unsigned *pic);

/*
 * Whether Scrap_Update writes an atlas: 1 when it is dirty, whether its
 * texture tex exists or not (a failed upload freed it; the update makes it
 * again with id 0, so a transient failure heals on a later draw).
 */
int rm_scrap_needs_upload(int dirty, int tex);

/*
 * The texture of atlas n in tex[count], 0 for an index outside it. A scrap
 * image keeps its atlas index and looks its texture up at each draw, so
 * an id freed by a failed upload, and taken by another texture, is never
 * drawn for it.
 */
int rm_scrap_texture(const int *tex, int count, int n);

/*
 * gl1's Scrap_Update filter of atlas n: nearest for the first
 * RM_MAX_SCRAPS_NOLERP atlases and for all under r_2D_unfiltered
 * (unfiltered), filtered without mipmaps otherwise and for an index
 * outside the table.
 */
int rm_scrap_sampler(int n, int unfiltered);

/* One 2D quad: corners and the texture coordinates of the corners (up left, down right). */
typedef struct
{
	float x0, y0, x1, y1;
	float s0, t0, s1, t1;
} rm_quad_t;

/*
 * gl1's RDraw_CharScaled: the cell of character num of a 16 by 16 sheet
 * whose image holds the coordinates st (sl, tl, sh, th). 0 for a space
 * and for a character wholly above the screen (y <= -8): nothing drawn.
 */
int rm_char_quad(rm_quad_t *q, int x, int y, int num, float scale, const float *st);

/* gl1's RDraw_PicScaled, RDraw_StretchPic: a pic of w by h at (x, y), scaled by factor. */
void rm_pic_quad(rm_quad_t *q, int x, int y, int w, int h, float factor, const float *st);

/*
 * gl1's RDraw_TileClear: the tile at offset (x2, y2) of an iw by ih pic
 * repeated over the box (x, y, w, h), the last column and row clipped to
 * the box with the texture coordinates cut in proportion. 0 when iw or ih
 * is not positive or the offset is outside the box.
 */
int rm_tile_quad(rm_quad_t *q, int x, int y, int w, int h, int iw, int ih,
		long x2, long y2, const float *st);

/*
 * Whether add more quads fit in a batch of cap with count in it: 1, or 0
 * for a negative or zero add, a count outside 0..cap, or a sum above cap
 * (checked without forming an overflowing sum).
 */
int rm_batch_fits(long count, long add, long cap);

/*
 * The indices of n quads of 4 vertices each (gl1's R_Buffer2DQuad: 0 1 2,
 * 0 2 3 of each quad) into idx, which holds cap entries. The number of
 * indices, 0 when n < 1, they do not fit or a vertex index would pass 65535.
 */
int rm_quad_indices(int n, unsigned short *idx, int cap);

/* gl1's glColor4f(r, g, b, 1) of RDraw_PicScaledCol, which modulates the texel. */
void rm_tint_color(float *rgba, const float *rgb);

/* ---- the world: BSP bounds, retained buffers, view (rm_pure.c, N4) ---------------- */

/* q_shared.h's texinfo flags (SURF_*), which this header does not include; the host test holds them equal */
#define RM_TEX_SKY 0x4
#define RM_TEX_WARP 0x8
#define RM_TEX_TRANS33 0x10
#define RM_TEX_TRANS66 0x20
#define RM_TEX_FLOWING 0x40

/* The loader limits of files.h the retained buffers are sized by (the host test holds them equal). */
#define RM_MAX_MAP_FACES 65536        /* MAX_MAP_FACES: surfaces and surface records */
#define RM_MAX_MAP_SURFEDGES 256000   /* MAX_MAP_SURFEDGES: the vertices of all polygons together */
#define RM_MAX_MAP_LEAFS 65536        /* MAX_MAP_LEAFS: clusters */
#define RM_MAX_MAP_AREAS 256          /* MAX_MAP_AREAS */
#define RM_WORLD_MAX_VERTS RM_MAX_MAP_SURFEDGES
#define RM_WORLD_MAX_INDICES (3ul * RM_MAX_MAP_SURFEDGES)
/* The largest static buffer the back end makes (rmb_buffer_static). */
#define RMB_STATIC_MAX_BYTES (32u << 20)

/*
 * The material class of a surface for the record and for what N4 draws:
 * sky, then translucent, then warped (water), then flowing, as gl1's
 * world walk and texture chains decide them (R_RecursiveWorldNode,
 * R_DrawTextureChains); everything else is world.
 */
typedef enum
{
	RM_MAT_WORLD,
	RM_MAT_SKY,
	RM_MAT_TRANSLUCENT,
	RM_MAT_WATER,
	RM_MAT_FLOWING,
	RM_MATS
} rm_material_t;

rm_material_t rm_material(int texflags);

/*
 * One vertex of the retained world buffer: the float3 position at offset
 * 0 (what an acceleration structure reads, with this stride) and w = 1
 * after it, then the diffuse and the lightmap coordinates; one stride of
 * 32 bytes (static asserts in rm_pure.c, a runtime check in the host
 * test). The w is stored, not packed away, because the vertex function
 * then reads one float4 as the shim's does, and only that gives the
 * shim's clip coordinates to the bit (design section 19).
 */
typedef struct
{
	float pos[4];
	float uv[2];
	float lmuv[2];
} rm_wvert_t;

#define RM_WVERT_STRIDE 32

/*
 * One record of the per-surface buffer (design section 9): the plane
 * normal, flipped for SURF_PLANEBACK; the material class; the texinfo
 * flags; the image (its slot in gltextures); the lightmap slice (-1 until
 * N5) and rectangle; the styles; the surface's range of the index buffer.
 */
typedef struct
{
	float normal[3];
	unsigned material;
	unsigned texflags;
	int image;
	int lm_slice;
	short lm_rect[4];
	unsigned char styles[4];
	unsigned first_index;
	unsigned num_indices;
} rm_surfrec_t;

/* The normal of a surface on plane normal pn: pn, or -pn with planeback. */
void rm_surf_normal(const float *pn, int planeback, float *out);

/* The counts of the three retained buffers while a map is built. */
typedef struct
{
	unsigned long surfs, verts, indices;
} rm_world_size_t;

/*
 * Adds one polygon of n vertices (a surface) to s: 1, or 0 and s unchanged
 * when n < 3 or a total would pass its limit (RM_MAX_MAP_FACES surfaces,
 * RM_WORLD_MAX_VERTS vertices, RM_WORLD_MAX_INDICES indices).
 */
int rm_world_add(rm_world_size_t *s, long n);

/*
 * The bytes of the vertex, index (uint32) and record buffers of s: 1, or 0
 * with all three 0 when a count passes its limit or a product would pass
 * cap (checked by division before it is formed, so it cannot overflow).
 */
int rm_world_bytes(const rm_world_size_t *s, size_t cap, size_t *vbytes, size_t *ibytes, size_t *sbytes);

/*
 * gl1's R_SetBufferIndices for GL_TRIANGLE_FAN on a polygon whose n
 * vertices start at base: base, base + i + 1, base + i + 2 for i from 0 to
 * n - 3, into idx, which holds cap entries. The number of indices; 0 and
 * nothing written when n < 3, they do not fit or a vertex index would
 * pass 32 bits.
 */
long rm_world_fan(unsigned long base, long n, unsigned *idx, unsigned long cap);

/* [first, first + count) inside [0, total]: 1, or 0 for a negative value or an end past total (no overflow). */
int rm_range_ok(long first, long count, long total);

/*
 * A lump of the BSP file: fileofs and filelen not negative, fileofs a
 * multiple of 4 (the loaders read the records' int and float fields in
 * place), the lump inside the file's len bytes.
 */
int rm_lump_ok(long fileofs, long filelen, long len);

/* A face's edges: at least 3, inside the surfedge lump. */
int rm_face_edges_ok(long firstedge, long numedges, long numsurfedges);

/* A surfedge names edge |e| of numedges: 1 when -numedges < e < numedges. */
int rm_surfedge_ok(long e, long numedges);

/*
 * The raw texinfo lump before files/models.c reads it (Mod_LoadTexinfo
 * follows nexttexinfo unchecked and counts each chain until it ends or
 * returns to its start): next[i] for each of count texinfos, already
 * swapped. 1 when every next is 0 or below (the end) or names a texinfo,
 * and the chain from every texinfo ends or returns to it within count
 * steps; 0 otherwise (the map is refused).
 */
int rm_texinfo_chains_ok(const int *next, long count);

/*
 * The header of a visibility lump of len bytes that says it holds
 * numclusters clusters: 0 to RM_MAX_MAP_LEAFS, and its 4 + 8 * numclusters
 * bytes of offsets inside the lump (files/models.c swaps them in place
 * unchecked).
 */
int rm_vis_header_ok(long numclusters, long len);

/*
 * A leaf's cluster and area: the cluster -1 or below numclusters when the
 * map has visibility (R_MarkLeaves indexes the PVS row by it), the area
 * inside the client's areabits (R_AreaVisible).
 */
int rm_leaf_ok(long cluster, long area, long numclusters, int has_vis);

/*
 * A leaf's contents: anything but CONTENTS_NODE (-1), which files/models.c
 * (Mod_SetParent, Mod_PointInLeaf) and the draw take for a node, reading
 * the leaf's marksurface fields as children.
 */
int rm_leaf_contents_ok(long contents);

/*
 * The world's head before files/models.c reads the nodes: at least one
 * node (Mod_LoadNodes sets the parents from node 0, and the draw starts
 * there) and a model 0 whose head node is one of them. A world without
 * nodes would leave its node array empty and aliased by the next hunk
 * allocation.
 */
int rm_world_head_ok(long numnodes, long nummodels, long headnode);

/*
 * The raw node lump before files/models.c links it: children holds the two
 * children of each of numnodes nodes, already swapped (a node index, or
 * -1 - leaf for a leaf); seen has numnodes bytes of scratch. 1 when node 0
 * is no node's child, every other node is a child at most once (of one
 * node, in one of its two slots), every node child is below numnodes and
 * every leaf child below numleafs; 0 otherwise. The nodes reachable from
 * node 0 are then a tree: Mod_SetParent, Mod_NumberLeafs and the draw
 * visit each node once, and a node's surfaces enter a texture chain once.
 */
int rm_node_tree_ok(const int *children, long numnodes, long numleafs, unsigned char *seen);

/*
 * Marks [first, first + count) of owner[total] as taken (a node's
 * surfaces): 0, and nothing marked, when the range leaves the table or a
 * surface is taken already. gl1 links a surface into its texture chain
 * once per node that holds it, so a surface in two nodes would make the
 * chain a loop.
 */
int rm_mark_once(unsigned char *owner, long total, long first, long count);

/*
 * What a model slot holds, from its name, its loaded flag and whether it
 * holds memory (a hunk or the world's buffers). A load sets the name first
 * and the flag only after every loader and check has returned; the shared
 * loaders drop to the console with a longjmp past Mod_ForName, which
 * leaves the name set and the flag clear. Only a loaded slot is handed
 * out; an unfinished one is freed completely before anything is loaded.
 */
typedef enum
{
	RM_SLOT_EMPTY,       /* no name, no flag, no memory: free for a load */
	RM_SLOT_UNFINISHED,  /* any other mix: a load that did not finish */
	RM_SLOT_LOADED       /* named and flagged */
} rm_slot_t;

rm_slot_t rm_slot_state(int named, int loaded, int holds);

/*
 * gl1's Mod_DecompressVis, bounded: the run-length row at in, which has
 * in_len bytes, into out (row bytes; out_cap at least row). A zero run is
 * cut at the end of the row. 0, with out all set (visible), when the
 * input ends first; 0 and nothing written when out_cap is below row.
 */
int rm_decompress_vis(const unsigned char *in, size_t in_len, size_t row, unsigned char *out, size_t out_cap);

/*
 * The per-map line of metal_stats: the map's name and the counts and bytes
 * of its three retained buffers, without a newline. 1 when it fits; 0,
 * with buf empty, when it does not or the counts pass their limits.
 */
int rm_world_format(const char *map, const rm_world_size_t *s, char *buf, size_t len);

/* A viewport of Metal (top-left origin) and its depth range. */
typedef struct
{
	double x, y, w, h, znear, zfar;
} rm_viewport_t;

/*
 * gl1's R_SetupGL viewport of the refdef rectangle (rx, ry, rw, rh) on a
 * vid_w by vid_h screen, as the shim's mgl_compute_raster gives it on a
 * target th pixels high: x, th - y2 - h, w, h, depth 0 to 1.
 */
void rm_view_rect(int rx, int ry, int rw, int rh, int vid_w, int vid_h, int th, rm_viewport_t *out);

/*
 * gl1's R_SetupGL modelview through common/mtl_matrix.c, as the shim
 * builds it: the identity times the rotations by -90 about x, 90 about z,
 * -angles[2] about x, -angles[0] about y, -angles[1] about z, then the
 * translation by -org, each product taken on the right (m = m * r).
 */
void rm_view_matrix(float *m, const float *angles, const float *org);

/* gl1's R_PolyBlend modelview: the identity, -90 about x, 90 about z. */
void rm_blend_matrix(float *m);

/*
 * gl1's R_SetPerspective (glFrustum on the identity): fovy in degrees, the
 * refdef's width and height, gl_znear (at least 0.1), r_farsee, and the
 * under-water wobble (gl1_waterwarp, RDF_UNDERWATER, the refdef's time).
 * 0 when glFrustum would refuse the values (m untouched).
 */
int rm_projection(float *m, float fovy, int width, int height, float znear, int farsee,
		int underwater, float time, float waterwarp);

/*
 * gl1's R_PolyBlend decision and colour (R_SetupFrame's v_blend): 1 with
 * rgba the refdef's blend, its colour times sw_gamma, when gl_polyblend is
 * on and the alpha is not 0; 0 (no quad) otherwise.
 */
int rm_polyblend(const float *blend, float sw_gamma, float polyblend, float *rgba);

/* ---- the lightmaps (rm_pure.c, N5) ------------------------------------------------ */

/* gl1's lightmap block (BLOCK_WIDTH, BLOCK_HEIGHT, LIGHTMAP_BYTES): one slice of the array texture */
#define RM_LM_WIDTH 128
#define RM_LM_HEIGHT 128
#define RM_LM_BYTES 4
/*
 * The slices a map may fill. gl1 numbers its blocks from 1 (0 is the
 * classic path's dynamic block) and drops with "MAX_LIGHTMAPS exceeded"
 * when the upload of a full block, or the last one, raises the number to
 * MAX_LIGHTMAPS (128): it fills blocks 1 to 126 at most.
 */
#define RM_LM_MAX_SLICES 126
/*
 * The luxels of one surface: gl1's R_BuildLightMap drops when smax * tmax
 * passes sizeof(s_blocklights) >> 4 (34 * 34 * 3 floats, 867), and its
 * LM_AllocBlock never places a block wider than BLOCK_WIDTH - 1.
 */
#define RM_LM_MAX_LUXELS 867
/* MAXLIGHTMAPS of the BSP format: the styles of a surface */
#define RM_LM_STYLES 4
/* the end of a surface's styles */
#define RM_LM_STYLE_END 255

/*
 * gl1's Mod_CalcSurfaceExtents for one axis, bounded: the texture
 * coordinate range [mins, maxs] of a surface to texturemins (floor(mins
 * / 16) * 16) and extents ((ceil(maxs / 16) - floor(mins / 16)) * 16),
 * both short in msurface_t. 1, or 0 and nothing written when either
 * value is not finite, maxs is below mins, or a result does not fit a
 * short (gl1 converts the float to int unchecked).
 */
int rm_surface_extents(float mins, float maxs, short *texturemin, short *extent);

/*
 * The lightmap size of a surface from its extents (gl1: smax = (extents[0]
 * >> 4) + 1, tmax likewise): 1 with *smax, *tmax set when both extents are
 * not negative, smax is at most RM_LM_WIDTH - 1, tmax at most RM_LM_HEIGHT
 * and smax * tmax at most RM_LM_MAX_LUXELS (gl1's checks); 0 and nothing
 * written otherwise (the map is refused).
 */
int rm_lm_size(long extent0, long extent1, int *smax, int *tmax);

/* The styles of a surface before the first RM_LM_STYLE_END, at most RM_LM_STYLES. */
int rm_lm_nummaps(const unsigned char *styles);

/*
 * The light samples of a surface: smax * tmax * 3 bytes per style, from
 * byte ofs of a lighting lump of lightlen bytes. 1 when ofs is not
 * negative, both extents are not negative and the whole run of nummaps
 * styles ends inside the lump (computed without overflow); 0 otherwise.
 * Sky and translucent surfaces are read by R_LightPoint with their own
 * extents, which rm_lm_size does not bound, so this takes the extents.
 */
int rm_lm_samples_ok(long ofs, long extent0, long extent1, int nummaps, long lightlen);

/*
 * The light point's texture coordinates s, t on one surface of
 * texturemins smin, tmin and extents sext, text (gl1's
 * R_RecursiveLightPoint, bounded): 0 when s or t is not finite or its
 * truncation does not fit an int (gl1's conversion is undefined there);
 * otherwise gl1's truncation toward zero and its four checks, done in
 * 64-bit so no difference wraps: 1 with *ds, *dt set, 0 .. sext and
 * 0 .. text, exactly when gl1 accepts (a point within one texture unit
 * outside an edge is truncated onto it); 0 and nothing written
 * otherwise: no sample on this surface, the walk goes on. The traced point lies on the
 * node's plane, not on the face, and the texture vectors are bounded
 * only through the face's own vertices, so s and t can be anything.
 */
int rm_lp_coords(float s, float t, short smin, short tmin, short sext, short text, int *ds, int *dt);

/* MAX_DLIGHTS of ref.h, which this header does not include (rm_light.c and the host test hold them equal) */
#define RM_MAX_DLIGHTS 32

/*
 * The dynamic lights of a frame the renderer walks: the refdef's
 * num_dlights clamped to 0 .. RM_MAX_DLIGHTS (the client caps it, the
 * renderer does not rely on that).
 */
int rm_dlight_count(long n);

/* The mark bit of dynamic light i, unsigned: 1u << i for i in 0 .. RM_MAX_DLIGHTS - 1, else 0. */
unsigned rm_dlight_bit(int i);

/*
 * gl1's LM_AllocBlock on a block whose RM_LM_WIDTH column heights are
 * allocated: a w by h rectangle, the same spots as gl1's (the rightmost
 * start is RM_LM_WIDTH - w - 1). 1 with *x, *y set and the columns
 * raised; 0 and nothing written when it does not fit (the next slice is
 * begun) or w, h are outside 1 .. RM_LM_WIDTH, 1 .. RM_LM_HEIGHT.
 */
int rm_lm_alloc(int *allocated, int w, int h, int *x, int *y);

/* The slices of a map: 1 to RM_LM_MAX_SLICES, checked before the array texture is made. */
int rm_lm_slices_ok(long n);

/*
 * gl1's LM_CreateSurfaceLightmap placement: a w by h lightmap in slice
 * *slice (allocated its column heights) or, when that is full, at the
 * top of the next slice (gl1's LM_UploadBlock and LM_InitBlock: the
 * columns cleared, *slice raised). 1 with *x, *y and *slice set; 2 when
 * the slice changed; 0 and nothing changed when w, h do not fit an empty
 * slice or the next slice would be RM_LM_MAX_SLICES or more (the map is
 * refused before the array is sized: gl1's "MAX_LIGHTMAPS exceeded").
 */
int rm_lm_place(int *allocated, int *slice, int w, int h, int *x, int *y);

/*
 * A w by h region at (x, y) of slice slice in an array of nslices slices
 * of RM_LM_WIDTH by RM_LM_HEIGHT: 1 when it lies inside (w and h at least
 * 1, no overflow), checked before a region is built or uploaded.
 */
int rm_lm_region_ok(long slice, long nslices, long x, long y, long w, long h);

/* gl1's lmrect_t: [left, right) by [top, bottom) of one slice */
typedef struct
{
	int left, right, top, bottom;
} rm_lmrect_t;

/* gl1's R_JoinAreas: obj grows to hold adding as well. */
void rm_lm_join(const rm_lmrect_t *adding, rm_lmrect_t *obj);

/*
 * gl1's R_RegenAllLightmaps after it rebuilt a surface's lightmap:
 * *dirty, the surface is rebuilt next frame whatever its styles, when a
 * dynamic light touched it this frame (dlit) or its slice's region did
 * not reach the texture (uploaded 0); returns 1 when its styles' values
 * are to be recorded as built (R_SetCacheState): not dirty and one style
 * is 0 or 32 and above (cacheable), the only styles gl1 records. A
 * refused upload records nothing, so the next frame retries it.
 */
int rm_lm_regen_mark(int uploaded, int dlit, int cacheable, int *dirty);

/*
 * gl1's minlight table (R_InitImages, gl1_image.c) for gl1_minlight m:
 * the identity for m 0 (returns 0, gl_state.minlight_set off), otherwise
 * (255 - m) * i / 255 + m (returns 1). m is clamped to 0 .. 255 first.
 */
int rm_minlight_table(float m, unsigned char *table);

/*
 * gl1's R_BuildLightMap without the dynamic-light step (R_AddDynamicLights
 * is N8's, a step of its own between the styles and the store): the
 * smax by tmax samples of nummaps styles, each style's rgb scaled by
 * modulate (r_modulate), summed in float, truncated, negative values to 0,
 * all three scaled down when the brightest passes 255, through minlight
 * (NULL when gl1_minlight is 0) and gamma (gl1's gammatable), alpha the
 * brightest; rows of smax * 4 bytes, stride bytes apart, at dest. samples
 * NULL is gl1's "no light data": every luxel 255 before the tables.
 * blocklights is scratch of RM_LM_MAX_LUXELS * 3 floats. 0 and nothing
 * written when rm_lm_size refuses smax and tmax as extents would, nummaps
 * is outside 0 .. RM_LM_STYLES or stride is below smax * 4.
 */
int rm_lm_build(const unsigned char *samples, int smax, int tmax, int nummaps, const float (*rgb)[3],
		float modulate, const unsigned char *minlight, const unsigned char *gamma,
		unsigned char *dest, long stride, float *blocklights);

/* gl1_overbrightbits as gl1's RI_BeginFrame clamps it: 0, 1, 2 or 4 (3 is 2, the rest clamped to 0 .. 4). */
int rm_overbright(float v);

/*
 * The texture environment of gl1's lightmap unit (TMU 1) on the
 * multitexture path: GL_COMBINE with GL_RGB_SCALE overbright when
 * gl1_overbrightbits is set (gl1 sets it after R_EnableMultitexture, so
 * it wins over r_lightmap), else GL_REPLACE with r_lightmap, else
 * GL_MODULATE (common/mtl_shader_math.h, MTL_LM_*).
 */
int rm_lm_env(int overbright, int lightmap_only);

/* The lightmap line of metal_stats at registration, without a newline; 1 when it fits, 0 with buf empty. */
int rm_lm_format(const char *map, long slices, char *buf, size_t len);

/* ---- sky, warped water, flowing and translucent surfaces (rm_pure.c, N6) ------------ */

/*
 * gl1's warp subdivision (R_SubdivideSurface, R_SubdividePolygon of
 * gl1_warp.c), bounded. A warped surface is cut along the 64 unit grid
 * planes of each axis (RM_WARP_SIZE) and each piece becomes a fan: its
 * centre, its vertices, and its first vertex again, every vertex five
 * floats (x, y, z and the s, t of the texture vectors, not divided by the
 * image: the water's turbulence divides by 64).
 */
#define RM_WARP_SIZE 64
/* gl1's MAX_SUBDIVIDE_VERTS: a polygon, or a piece of one, of more vertices is refused (gl1 drops the map) */
#define RM_WARP_MAX_VERTS 60
/* gl1's R_BoundPoly starts its bounds at +-9999, which a polygon outside them would not tighten: refused */
#define RM_WARP_COORD_MAX 9999.0f
/* the pieces and the vertices (with the centre and the closing one) of one surface, and of one map */
#define RM_WARP_MAX_POLYS 20000
#define RM_WARP_MAX_OUT 262144ul
#define RM_WARP_MAP_MAX_OUT 1048576ul
#define RM_WARP_FLOATS 5
/* the longest cut chain, and the calls one surface may make: a bound on the recursion */
#define RM_WARP_MAX_DEPTH 64
#define RM_WARP_MAX_CALLS 100000

/*
 * Cuts the polygon verts (numverts vertices of x, y, z) with the texture
 * vectors vec0 and vec1 (their first three floats). The pieces go to out,
 * which holds out_cap vertices of RM_WARP_FLOATS floats, in gl1's order of
 * emission (gl1 chains them last first); polylen[k] is the vertex count of
 * piece k, at most poly_cap pieces. Returns 1 with *npolys pieces and
 * *nverts vertices, 0 when numverts is below 3 or above RM_WARP_MAX_VERTS,
 * a coordinate is not finite or outside RM_WARP_COORD_MAX, a piece has too
 * many vertices, a count would pass a cap or the recursion its bound;
 * *npolys and *nverts are then 0.
 */
int rm_warp_subdivide(const float *verts, int numverts, const float *vec0, const float *vec1,
		float *out, unsigned long out_cap, unsigned *polylen, unsigned poly_cap,
		unsigned *npolys, unsigned long *nverts);

/* the turbulence table of gl1 (r_turbsin as its RI_Init halves it) and the scale that indexes it, 256 / (2 pi) */
#define RM_TURB_SIZE 256
float rm_turb_sin(int i);

/*
 * gl1's index into it, (int)((c * 0.125 + time) * TURBSCALE) & 255: a
 * value outside the range of an int (gl1 converts it unchecked) is taken
 * modulo 256 first, a NaN is 0.
 */
int rm_warp_index(float c, float time);

/* the water's scroll: gl1's -64 * (t / 2 - (int)(t / 2)) on a SURF_FLOWING surface, 0 otherwise */
float rm_warp_scroll(float time, int flowing);

/*
 * The scroll of a flowing surface's polygon (R_RenderLightmappedPoly,
 * R_DrawGLPoly): -64 * (t / 40 - (int)(t / 40)), -64 where that is 0.
 */
float rm_flow_scroll(float time);

/*
 * gl1's R_EmitWaterPolys for one vertex (os, ot): s = os + turb(ot) +
 * scroll, t = ot + turb(os), each times 1 / 64.
 */
void rm_warp_st(float os, float ot, float time, float scroll, float *s, float *t);

/* the sky box: the six faces' visible bounds, gl1's skymins and skymaxs */
#define RM_SKY_FACES 6
/* gl1's MAX_CLIP_VERTS; a polygon of more than RM_SKY_MAX_VERTS - 2 vertices is refused (gl1 drops the map) */
#define RM_SKY_MAX_VERTS 64

typedef struct
{
	float mins[2][RM_SKY_FACES], maxs[2][RM_SKY_FACES];
} rm_skybounds_t;

/* gl1's R_ClearSkyBox */
void rm_sky_clear(rm_skybounds_t *b);

/*
 * gl1's R_ClipSkyPolygon: the polygon of nump vertices (x, y, z relative
 * to the view origin) clipped against the six box planes and the
 * projection of each piece on its face added to the bounds. 1 when done;
 * 0 when nump is below 1 or above RM_SKY_MAX_VERTS - 2 or a piece would
 * pass the buffers (a polygon that is not convex can), with the bounds
 * perhaps grown by the pieces done before: the surface counts as not drawn.
 */
int rm_sky_clip(rm_skybounds_t *b, int nump, const float *vecs);

/*
 * gl1's R_DrawSkyBox before the faces: with a rotating sky (rotate not 0),
 * 0 when no face is visible (nothing is drawn), else every face is set
 * to the whole -1 .. 1 square; 1 otherwise.
 */
int rm_sky_prepare(rm_skybounds_t *b, float rotate);

/* gl1's test of a face: it is drawn unless a side of its bounds is empty (or not a number to compare) */
int rm_sky_face_visible(const rm_skybounds_t *b, int face);

/* gl1's skytexorder: the sky image (suffix order rt bk lf ft up dn) of a face; -1 outside 0 .. 5 */
int rm_sky_image_of_face(int face);

/*
 * gl1's R_MakeSkyVec: the corner (s, t) of a face as a position v (xyz)
 * on the box of 2300 units, 4096 with farsee, and its texture
 * coordinates st clamped to 1 / 512 and 511 / 512, t flipped. 0 and
 * nothing written when face is outside 0 .. 5.
 */
int rm_sky_vec(float s, float t, int face, int farsee, float *v, float *st);

/*
 * gl1's sky modelview: the world matrix times the translation to the
 * view origin times the rotation by time * rotate about axis (the
 * shim's glTranslatef and glRotatef, a zero axis leaving it).
 */
void rm_sky_matrix(float *out, const float *world, const float *origin, float time, float rotate,
		const float *axis);

/* the longest sky name (gl1 pastes it into "env/<name>rt.tga" and "pics/Skies/<name>rt.m32") */
#define RM_SKY_NAME_MAX 40

/*
 * A sky name the server may send: 1 to RM_SKY_NAME_MAX characters of
 * letters, digits, '_', '-' and '.', neither beginning with a '.' nor
 * holding "..": no path, no drive and no control character gets into the
 * image names. 0 otherwise: the sky has no images, as gl1's when none loads.
 */
int rm_sky_name_ok(const char *name);

/* the rotation and axis the server sends: 1 when all four values are finite */
int rm_sky_params_ok(float rotate, const float *axis);

/*
 * The alpha of a translucent surface by its texinfo flags: 0.33 for
 * SURF_TRANS33, else 0.66 for SURF_TRANS66, else 1 (gl1's R_DrawAlphaSurfaces).
 */
float rm_trans_alpha(int texflags);

/* gl1's glColor4f(inverse_intensity x3, alpha) of the alpha chain (GL_MODULATE) */
void rm_trans_color(float inverse_intensity, float alpha, float *rgba);

/* gl1_state.inverse_intensity: gl1's R_InitImages divides the gamma by the intensity */
float rm_inverse_intensity(float gamma, float intensity);

/*
 * What gl1 does with an opaque warped surface's texture environment
 * (R_ApplyGLBuffer, SURF_DRAWTURB): with gl1_overbrightbits, GL_COMBINE
 * at scale 1 on the current colour (white), which gives the texel (returns
 * 0); without, GL_MODULATE with glColor4f(inverse_intensity x3, 1)
 * (returns 1, rgba set).
 */
int rm_warp_color(int overbright, float inverse_intensity, float *rgba);

/*
 * gl1's R_UpdateGLBuffer test for the alpha chain: the batch is drawn
 * before this surface when it holds something (count above 0) and its
 * texture or its alpha is not the surface's.
 */
int rm_alpha_break(long count, int cur_tex, float cur_alpha, int tex, float alpha);

/* ---- rmb_device.m and rmb_encode.m ------------------------------------------- */

/*
 * Frames in flight: the drawable count and frame semaphore of rmb_device.m
 * and the per-frame vertex rings of rmb_encode.m, one constant for both.
 */
#define RMB_FRAMES_IN_FLIGHT 3

/*
 * The frame ring of 2D vertices and indices (rmb_encode.m), one per frame
 * in flight. A draw that does not fit in what is left of it is skipped and
 * counted (draws skipped in metal_stats); the host test holds the size
 * against the most characters one frame can queue, a full console.
 */
#define RMB_RING_BYTES (8u << 20)

/* The device and the queue, and the capabilities. 0 with err filled on failure. */
int rmb_open(rmb_caps_t *caps, char *err, size_t errlen);

/*
 * Takes the CAMetalLayer of the SDL Metal view (SDL_Metal_GetLayer, made
 * by rm_sdl.c, which owns the view; the back end includes no SDL).
 * 0 with err on failure.
 */
int rmb_attach(void *metal_layer, int vsync, char *err, size_t errlen);

/* Waits (bounded) for the frames on the GPU and releases the layer and the targets. */
void rmb_detach(void);

/* Counts an object the front end makes or releases (the SDL Metal view). */
void rmb_ledger_note(rm_obj_kind_t kind, int created);

/* Detaches, releases the device and the queue; fills the final counters. */
void rmb_close(rmb_stats_t *stats, rm_ledger_t *ledger);

void rmb_set_vsync(int on);
int rmb_vsync(void);

/* The counters so far. */
void rmb_stats(rmb_stats_t *stats, rm_ledger_t *ledger);

/*
 * Takes a frame slot and a command buffer, sizes the targets and the
 * layer's drawable to w by h pixels (the window's size in pixels), and
 * opens the frame's first pass, colour only: cleared to clear_rgba, or
 * to black when it is NULL (rm_color_load). This is gl1's R_Clear at the
 * end of RI_BeginFrame, with the shim's black for a frame gl1 does not
 * clear. 0 when
 * the frame is skipped.
 */
int rmb_frame_begin(int w, int h, const float *clear_rgba);

/*
 * Ends the open pass and opens the 3D pass on the colour target and its
 * depth32Float_stencil8 attachment: colour loaded, depth cleared to 1,
 * stencil to 0. 0 when there is no open frame.
 */
int rmb_pass_3d(void);

/* Ends the 3D pass and opens the 2D pass: colour loaded, no depth. */
int rmb_pass_2d(void);

/*
 * Reads the colour target into bgra (rows top down, w * 4 bytes each,
 * w and h from rmb_target_size); len is the size of bgra. Inside a frame
 * it reads what was drawn so far, outside one the last frame. Returns 0
 * (nothing written) when the frame is skipped, there is no target, no
 * pass has written the target since it was made, bgra is too small or
 * the GPU did not finish within its wait.
 */
int rmb_read(unsigned char *bgra, size_t len);

/* The size of the colour target, 0 0 without one. */
void rmb_target_size(int *w, int *h);

/* Blits the frame to the drawable when there is one, presents and commits. */
void rmb_frame_end(void);

/*
 * gl1's mid-frame colour clear (RI_SetPalette clears to 0, 0, 0, 0): the
 * open pass is ended and a colour-only pass cleared to rgba opened.
 */
void rmb_clear(const float *rgba);

/* ---- rmb_resource.m: textures, samplers, pipelines ---------------------------- */

/* The staging ring all texture uploads go through (section 3). */
#define RMB_STAGING_BYTES (64u << 20)
/*
 * The longest texture side an upload may have. Metal aborts the process
 * (a failed descriptor assertion, not an error) on a side above the
 * device's maximum, which is 16384 on every family from the apple5 floor
 * on; gl1 on real GL answers GL_INVALID_VALUE instead. 16384 and not the
 * shim's 8192: it is the device's own limit, so nothing a GL driver on
 * this hardware takes is refused, and the staging ring bounds the other
 * side (16384 by 1024 is the whole ring).
 */
#define RMB_MAX_TEXTURE_SIDE 16384
/* Texture ids run from 1 to RMB_MAX_TEXTURES - 1; 0 is none. */
#define RMB_MAX_TEXTURES 4096

/* The sampler classes of gl1's images, each in a repeat and a clamp form. */
typedef enum
{
	RMB_SMP_MIP,        /* mipmapped: gl_texturemode's min and mag filter, r_anisotropic */
	RMB_SMP_FLAT,       /* no mipmaps: gl_texturemode's mag filter for both */
	RMB_SMP_NEAREST,    /* r_nolerp_list, r_2D_unfiltered, r_videos_unfiltered */
	RMB_SMP_LIGHTMAP,   /* gl1's lightmaps: GL_LINEAR for both whatever gl_texturemode says (LM_UploadBlock), no mipmaps */
	RMB_SMP_CLASSES
} rmb_smp_t;

#define RMB_SAMPLER(cls, clamp) ((cls) * 2 + ((clamp) ? 1 : 0))

/*
 * gl1's filter state of one image as a sampler (repeat form): at load
 * (R_LoadPic) an image on r_nolerp_list, or a pic under r_2D_unfiltered,
 * is GL_NEAREST, mipmapped or not; after a mode change (R_TextureMode,
 * mode_change 1) a mipmapped image takes the mode whatever the lists
 * say, and only one without mipmaps stays GL_NEAREST.
 */
int rm_image_sampler(int mipmapped, int nolerp, int mode_change);

/*
 * Loads ref_metal.metallib from dir and builds the 2D pipeline table and
 * the samplers. 0 with err filled on failure.
 */
int rmb_resources_open(const char *dir, char *err, size_t errlen);
void rmb_resources_close(void);

/* Rebuilds the sampler table between frames: min 0 to 5, mag_linear, anisotropy 0 (off) or 1 to 16. */
void rmb_samplers_set(int min, int mag_linear, int aniso);

/*
 * Writes w by h rgba8 texels (rows top down, tightly packed) into
 * texture id, making it (or making it again at another size) first;
 * with mipmapped the mip chain is generated. id 0 takes a free id.
 * Returns the id, 0 when the size check refuses the upload or an
 * allocation fails (counted as an upload skip; nothing is written).
 * Every 0 also frees the id passed in (its texture is gone), so on 0
 * the caller forgets its id and no id is lost.
 * opaque forces alpha 255 (gl1's solid internal format on the shim).
 */
int rmb_texture_upload(int id, const unsigned char *rgba, int w, int h, int mipmapped, int opaque);
void rmb_texture_free(int id);

/* Counts an upload the front end refused by rm_upload_bytes before converting it. */
void rmb_upload_refused(void);

/* One 2D vertex: the shim's layout (float4 position, float2 uv, float4 colour). */
typedef struct
{
	float pos[4];
	float uv[2];
	float pad[2];
	float color[4];
} rmb_vert2d_t;

/*
 * Draws a fan of n vertices (gl1's GL_TRIANGLE_FAN quads) with 2D
 * pipeline pipe, texture tex (0 for the untextured pipelines) and
 * sampler smp, in the open pass, at the full-target viewport, with mvp
 * (column-major) on every vertex. 0 when the draw is skipped (counted).
 */
int rmb_draw_2d(int pipe, int tex, int smp, const rmb_vert2d_t *v, int n, const float *mvp);

/*
 * The same for nquads quads of 4 vertices each (a batch of gl1's
 * R_Buffer2DQuad, one draw). 0 when the draw is skipped (counted), or
 * nquads is outside 1 to RM_BATCH_QUADS.
 */
#define RM_BATCH_QUADS 1024
int rmb_draw_2d_quads(int pipe, int tex, int smp, const rmb_vert2d_t *v, int nquads, const float *mvp);

/* ---- the 3D pass (rmb_resource.m, rmb_encode.m, N4) ------------------------------ */

/* The 3D pipeline states InitContext builds beside the 2D table (section 4). */
typedef enum
{
	RM_PIPE_WORLD_TEX,      /* world_tex: the diffuse texel alone (r_fullbright, gl1's GL_REPLACE) */
	RM_PIPE_COLOR_BLEND,    /* color: an untextured colour, blended (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) */
	RM_PIPE_WORLD_LM,       /* world_lm: the diffuse texel times the lightmap slice's (gl1's two units, N5) */
	RM_PIPE_WORLD_TEXC,     /* world_tex with a colour: GL_MODULATE of the colour and the texel (warped water, N6) */
	RM_PIPE_WORLD_TEXC_BLEND,   /* the same blended (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA): translucent surfaces */
	RM_PIPE_3D_COUNT
} rm_pipe3d_t;

/* The depth-stencil states of the 3D pass built beside them. */
typedef enum
{
	RMB_DS_NONE,            /* no depth test, no writes (R_PolyBlend's glDisable(GL_DEPTH_TEST)) */
	RMB_DS_LEQUAL_WRITE,    /* gl1's glDepthFunc(GL_LEQUAL), glDepthMask(GL_TRUE) */
	RMB_DS_COUNT
} rmb_ds_t;

/* Static buffer ids run from 1 to RMB_MAX_BUFFERS - 1; 0 is none. */
#define RMB_MAX_BUFFERS 16

/*
 * A private buffer holding the bytes of data (1 to RMB_STATIC_MAX_BYTES),
 * written through the upload command buffer, so the next frame reads it.
 * Its id, 0 when the size is refused or an allocation fails.
 */
int rmb_buffer_static(const void *data, size_t bytes);
void rmb_buffer_free(int id);

/*
 * Draws count indices of the static vertex buffer vbuf (rm_wvert_t), the
 * indices copied into the frame ring, as triangles with the world_tex
 * pipeline, depth LEQUAL with writes, texture tex and sampler smp, mvp
 * (column-major) on every vertex, in viewport vp, culling GL's front
 * faces when cull is set (gl1's glCullFace(GL_FRONT)). Only inside the 3D
 * pass. 0 when the draw is skipped (counted).
 */
int rmb_draw_world(int vbuf, const unsigned *idx, unsigned long count, int tex, int smp,
		const float *mvp, const rm_viewport_t *vp, int cull);

/*
 * Draws a fan of n float3 positions (xyz) in one colour rgba with the
 * blended color pipeline and no depth test, as gl1's R_PolyBlend does;
 * the rest as rmb_draw_world. 0 when the draw is skipped (counted).
 */
int rmb_draw_color(const float *xyz, int n, const float *rgba, const float *mvp,
		const rm_viewport_t *vp, int cull);

/* ---- the lightmap array (rmb_resource.m, rmb_encode.m, N5) ------------------------ */

/*
 * A new texture2d_array of slices slices of RM_LM_WIDTH by RM_LM_HEIGHT
 * rgba8 texels (private, no mipmaps), its contents undefined until
 * rmb_texture_region writes them. Its id, 0 (nothing taken) when
 * rm_lm_slices_ok refuses the count or an allocation fails.
 */
int rmb_texture_array(int slices);

/*
 * Writes the w by h texels at rgba (rows stride bytes apart) to (x, y) of
 * slice slice of the array texture id, through the staging ring and the
 * upload command buffer, so the next draw reads them; a frame command
 * buffer that already drew with id is split first, its pass reopened (a
 * 3D pass with its depth and stencil kept). 0 when the region is not
 * inside the texture (rm_region_ok), stride is below w * 4 or the ring
 * has no room: counted as an upload skip, nothing written, the texture
 * and its id kept (the region holds what it held).
 */
int rmb_texture_region(int id, int slice, int x, int y, int w, int h, const unsigned char *rgba, long stride);

/*
 * A region of a texture of tw by th texels and tslices slices: slice
 * below tslices, w and h at least 1, inside the texture (no overflow).
 * The back end's own check, against the texture it writes.
 */
int rm_region_ok(long tw, long th, long tslices, long slice, long x, long y, long w, long h);

/*
 * How the in-frame split reopens the pass it ended (rmb_encode_split):
 * after a 3D pass, at once, a 3D pass with colour, depth and stencil
 * loaded (the ended pass stores them), so the draws after the write test
 * the depth the draws before it wrote; otherwise nothing, and the next
 * 2D draw opens a 2D pass that loads the colour (N2's behaviour).
 */
typedef enum
{
	RM_REOPEN_NONE,
	RM_REOPEN_3D_KEEP
} rm_reopen_t;

rm_reopen_t rm_split_reopen(int pass_open, int pass_depth);

/*
 * The world_lm draw: as rmb_draw_world, with the lightmap array lmtex,
 * the slice of every surface of the draw, and gl1's lightmap unit:
 * env (rm_lm_env) and the GL_RGB_SCALE of GL_COMBINE.
 */
typedef struct
{
	int tex, smp;           /* the diffuse texture and its sampler */
	int lmtex, slice;       /* the lightmap array and the slice */
	int env;                /* MTL_LM_MODULATE, MTL_LM_REPLACE, MTL_LM_COMBINE */
	float scale;            /* GL_RGB_SCALE of GL_COMBINE: 1, 2 or 4 */
} rmb_lm_draw_t;

int rmb_draw_world_lm(int vbuf, const unsigned *idx, unsigned long count, const rmb_lm_draw_t *d,
		const float *mvp, const rm_viewport_t *vp, int cull);

/* ---- vertices made per frame (rmb_encode.m, N6) ------------------------------------- */

/* What a frame-made world draw does with its texture. */
typedef enum
{
	RM_DYN_TEX,         /* world_tex: the texel alone (GL_REPLACE: sky, r_fullbright flowing surfaces) */
	RM_DYN_TEXC,        /* the texel times a colour (GL_MODULATE: opaque warped water) */
	RM_DYN_TEXC_BLEND,  /* the same, blended over the target (translucent surfaces) */
	RM_DYN_LM,          /* world_lm: the lightmapped flowing surfaces (lm fields of rmb_lm_draw_t) */
	RM_DYNS
} rm_dyn_t;

/*
 * The vertices and indices of one frame-made draw go to the frame ring, so
 * a batch is bounded: at most RM_DYN_MAX_VERTS vertices and RM_DYN_MAX_INDICES
 * indices (a fan of n vertices is 3 * (n - 2) of them).
 */
#define RM_DYN_MAX_VERTS 4096
#define RM_DYN_MAX_INDICES (3 * RM_DYN_MAX_VERTS)

typedef struct
{
	int kind;               /* rm_dyn_t */
	int tex, smp;           /* the diffuse texture and its sampler */
	float color[4];         /* RM_DYN_TEXC, RM_DYN_TEXC_BLEND */
	rmb_lm_draw_t lm;       /* RM_DYN_LM: its tex and smp are the draw's own */
} rmb_dyn_t;

/*
 * Draws nv vertices and ni indices (into them) made this frame, as
 * triangles with depth LEQUAL and writes, in viewport vp with mvp (as
 * rmb_draw_world); the blended kind has gl1's glBlendFunc. 0 when the
 * draw is skipped (counted): no vertex, nv or ni above their bounds, an
 * index not below nv, a kind outside rm_dyn_t, a texture missing, or no
 * room in the frame ring.
 */
int rmb_draw_world_dyn(const rm_wvert_t *v, unsigned nv, const unsigned *idx, unsigned long ni,
		const rmb_dyn_t *d, const float *mvp, const rm_viewport_t *vp, int cull);

#endif /* RM_RMB_H */
