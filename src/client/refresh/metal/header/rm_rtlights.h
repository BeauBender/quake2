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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * The data of the ray-traced presentation (TASK187, PM5 of
 * docs/plans/presentation-modes.md, section 4.2): the map's light
 * entities from its entity lump, the SURF_LIGHT faces with the average
 * colour of their textures, the sky's colour, and the r_rt_stats 1 line
 * that counts them. Pure C: no SDL, no Metal, no engine header, so
 * tests/metal/rm_rtlights_test.c links it alone. rm_rtdata.c feeds it from
 * the map file the model loader holds. Nothing draws any of it yet.
 *
 * The entity lump is untrusted input (an asset loader): it is not NUL
 * terminated, every read is bounded by its length, and a NUL byte ends it
 * as it ends the string the game parses.
 *
 * =======================================================================
 */

#ifndef RM_RTLIGHTS_H
#define RM_RTLIGHTS_H

#include <stddef.h>

#define RM_RTL_MAX_LIGHTS 2048    /* lights kept; more are counted past the cap (MAX_MAP_ENTITIES) */
#define RM_RTL_MAX_TARGETS 2048   /* entities with a targetname a spotlight can aim at */
#define RM_RTL_NAME 64            /* a target or targetname, with its NUL; a longer one is not matched */
#define RM_RTL_MAX_KEY 256        /* a key, with its NUL (the game's keyname[256]); a longer one skips its entity */
#define RM_RTL_MAX_VALUE 1024     /* a value, with its NUL (MAX_TOKEN_CHARS); a longer one skips its entity */

#define RM_RTL_DEFAULT_LIGHT 300  /* qrad3's intensity when light and _light are absent or 0 */
#define RM_RTL_DEFAULT_CONE 10    /* qrad3's _cone in degrees when absent or 0 */
#define RM_RTL_STYLES 256         /* MAX_LIGHTSTYLES: a style outside 0 to 255 reads 0, as qrad3 has it */

/* the lump's result: a structural fault refuses the whole lump (no light is kept) */
typedef enum
{
	RM_RTL_OK = 0,
	RM_RTL_UNTERMINATED,     /* a quoted string the lump ends inside */
	RM_RTL_TOO_DEEP,         /* a '{' inside an entity */
	RM_RTL_UNBALANCED        /* a '}' or a key outside an entity, a key without its value, or the lump ending inside an entity */
} rm_rtl_lump_t;

/* where a spotlight's target led */
enum
{
	RM_RTL_TARGET_NONE = 0,  /* no target: a point light, or a light_spot aimed by angle */
	RM_RTL_TARGET_FOUND,     /* dir points at the targeted entity's origin */
	RM_RTL_TARGET_MISSING    /* no entity of that targetname: dir points down */
};

typedef struct
{
	float origin[3];
	float intensity;         /* light, else _light, else RM_RTL_DEFAULT_LIGHT */
	float color[3];          /* _color divided by its largest channel (qrad3's ColorNormalize); 1 1 1 when absent */
	int style;               /* _style, else style; 0 outside 0 to RM_RTL_STYLES - 1 */
	int spot;                /* a light_spot, or a light with a target */
	float cone;              /* a spotlight's _cone in degrees, else RM_RTL_DEFAULT_CONE */
	int target_state;        /* RM_RTL_TARGET_* */
	float dir[3];            /* a spotlight's unit direction; 0 0 0 for a point light */
	char target[RM_RTL_NAME];
} rm_rtl_light_t;

typedef struct
{
	char name[RM_RTL_NAME];
	float origin[3];
} rm_rtl_target_t;

typedef struct
{
	rm_rtl_light_t light[RM_RTL_MAX_LIGHTS];
	long nlights;            /* kept, at most RM_RTL_MAX_LIGHTS */
	long entities;           /* entities read */
	long spots, styled, untargeted;
	long skipped_long;       /* entities skipped for an over-long key or value */
	long skipped_number;     /* entities skipped for a number that does not parse */
	long over_cap;           /* lights past RM_RTL_MAX_LIGHTS */
	int refused;             /* rm_rtl_lump_t */
	rm_rtl_target_t target[RM_RTL_MAX_TARGETS];
	long ntargets;
} rm_rtl_lights_t;

/*
 * Reads the entity lump of len bytes into out: each entity whose
 * classname starts with "light" (as qrad3 selects them), then each
 * spotlight's target resolved against the lump's targetnames (the first
 * match, a bounded lookup). Numbers are parsed by hand: optional sign,
 * digits, an optional fraction, no exponent, no locale. Returns
 * out->refused; a refused lump keeps no light. lump may be NULL when len
 * is 0.
 */
int rm_rtl_parse(const char *lump, size_t len, rm_rtl_lights_t *out);

/*
 * 1 when s holds exactly n numbers separated by blanks (leading and
 * trailing blanks allowed), each finite as a float, stored in out.
 */
int rm_rtl_numbers(const char *s, float *out, int n);

/* ---- the emissive faces --------------------------------------------------------- */

/* the BSP's on-disk layout (files.h), held to it by rm_rtdata.c's static asserts */
#define RM_RTL_TEXINFO_SIZE 76    /* sizeof(texinfo_t) */
#define RM_RTL_TEXINFO_FLAGS 32   /* offsetof(texinfo_t, flags) */
#define RM_RTL_TEXINFO_VALUE 36   /* offsetof(texinfo_t, value) */
#define RM_RTL_TEXINFO_NAME 40    /* offsetof(texinfo_t, texture), 32 bytes, not always NUL terminated */
#define RM_RTL_TEXNAME 32
#define RM_RTL_FACE_SIZE 20       /* sizeof(dface_t) */
#define RM_RTL_FACE_TEXINFO 10    /* offsetof(dface_t, texinfo), a little-endian short */
#define RM_RTL_SURF_LIGHT 0x1     /* SURF_LIGHT */
#define RM_RTL_SURF_SKY 0x4       /* SURF_SKY */

#define RM_RTL_MAX_EMITTERS 65536 /* MAX_MAP_FACES */
#define RM_RTL_MAX_TEXTURES 1024  /* distinct emitter textures; an emitter past it has tex -1 */
#define RM_RTL_UNREAD_GREY 0.5f   /* qrad3's reflectivity for a texture it cannot read */

typedef struct
{
	long face;               /* the face's index in the faces lump */
	int tex;                 /* its slot in rm_rtl_emitters_t.tex, -1 past the texture cap */
	int value;               /* the texinfo value: the emitted intensity */
} rm_rtl_emit_t;

typedef struct
{
	char name[RM_RTL_TEXNAME + 1];
	float mean[3];           /* the average colour, 0 to 1 a channel; RM_RTL_UNREAD_GREY until read */
	int read;                /* 1 when the mean came from the texture's texels */
} rm_rtl_tex_t;

typedef struct
{
	rm_rtl_emit_t emit[RM_RTL_MAX_EMITTERS];
	long n;
	rm_rtl_tex_t tex[RM_RTL_MAX_TEXTURES];
	int ntex;
	long tex_over;           /* emitters whose texture is past RM_RTL_MAX_TEXTURES */
	long bad_texinfo;        /* faces whose texinfo index is outside the texinfo lump */
} rm_rtl_emitters_t;

/*
 * The faces whose texinfo has SURF_LIGHT, no SURF_SKY (the sky is its own
 * term) and a value above 0, from the raw texinfo and faces lumps (lengths
 * in bytes; a trailing partial record is ignored). Each distinct texture
 * name gets one slot (the per-texture cache), its mean still unread.
 */
void rm_rtl_emitters(const unsigned char *texinfo, size_t tilen, const unsigned char *faces, size_t flen,
		rm_rtl_emitters_t *out);

/*
 * The average colour of a .wal's first mip level through palette (256
 * entries, red in the first byte of each, as d_8to24table), 0 to 1 a
 * channel. 1 when the header and the body fit in len; 0, out untouched,
 * when they do not.
 */
int rm_rtl_wal_mean(const unsigned char *wal, size_t len, const unsigned *palette, float *out);

/*
 * The sky's colour: the mean of the n faces' means that have an image
 * (have[i] not 0). 0 with out 0 0 0 when no face has one.
 */
int rm_rtl_sky_mean(const float (*means)[3], const int *have, int n, float *out);

/* ---- the r_rt_stats 1 line -------------------------------------------------------- */

/* the name of a lump result, for the line */
const char *rm_rtl_lump_name(int refused);

/*
 * The line, without a newline, in the form of the r_rt_stats map line
 * (rm_rt_map_format): "ref_metal: rt-data: <map>: lights N (spot S,
 * styled T, untargeted U) emitters E (textures K, unread W) sky r,g,b
 * refused R (past the cap C)". sky is NULL when the map has no sky image;
 * a refused lump names its reason after "refused". 1 when the line fits
 * in len; 0, with buf empty, when it does not.
 */
int rm_rtl_format(const char *map, const rm_rtl_lights_t *l, const rm_rtl_emitters_t *e, const float *sky,
		char *buf, size_t len);

#endif
