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
 * The per-pixel arithmetic of ref_metal's ray-traced ambient occlusion
 * (RT5, TASK57; docs/plans/metal-raytracing-design.md, section 2) that the
 * kernel (rm_rtao.metal) and the host test (tests/metal/rm_rtao_test.c)
 * share: where a ray starts, the two cosine-weighted directions from the
 * blue noise, what a hit occludes, the factor and the light it scales.
 * Plain C99 and Metal Shading Language alike, as common/mtl_shader_math.h,
 * which it needs first (mtl_rgba_t, MTL_SQRT).
 *
 * =======================================================================
 */

#ifndef MTL_RTAO_MATH_H
#define MTL_RTAO_MATH_H

/* the rays a pixel traces (design section 2) */
#define MTL_RTAO_RAYS 2

/* the blue-noise tile: MTL_RTAO_TILE x MTL_RTAO_TILE ranks, a power of two */
#define MTL_RTAO_TILE 64
#define MTL_RTAO_TILE_N (MTL_RTAO_TILE * MTL_RTAO_TILE)

#ifndef __METAL_VERSION__
_Static_assert(MTL_RTAO_TILE_N == 4096, "mtl_rtao_noise puts a rank at bits 20 to 31");
#endif

/*
 * The instance mask bit of what an AO ray tests (rmb_rt_frame sets it on
 * every instance but the view weapon and the viewer's own model, with
 * TASK55's two bits beside it, MTL_RTS_MASK_OTHER and _CARRIER, which a
 * shadow ray's masks, 0xff, 0x01 and 0x02, keep testing as before).
 */
#define MTL_RTAO_MASK 0x04u

/* an instance's mask with the AO bit: mask its shadows' (mtl_rts_inst_mask), noao 1 for what AO rays pass */
static inline unsigned
mtl_rtao_inst_mask(unsigned mask, unsigned noao)
{
	return noao ? mask : (mask | MTL_RTAO_MASK);
}

/*
 * The smallest cosine of a ray's angle to the stored normal: 1/32, a
 * ray at least 1.8 degrees above the stored plane. The G-buffer keeps the
 * normal at 10 bits an axis, within 0.0017 of the true one (about 0.1
 * degree), so a ray never points below the true plane either.
 */
#define MTL_RTAO_MIN_COS 0.03125f

/* the layers of sky, water and translucent triangles a ray passes before it counts as unoccluded */
#define MTL_RTAO_LAYERS 4

typedef struct
{
	float x, y, z;
} mtl_rtao_v3_t;

static inline mtl_rtao_v3_t
mtl_rtao_v3(float x, float y, float z)
{
	mtl_rtao_v3_t v;

	v.x = x;
	v.y = y;
	v.z = z;
	return v;
}

#ifdef __METAL_VERSION__
#define MTL_RTAO_COS cos
#define MTL_RTAO_SIN sin
#define MTL_RTAO_FLOOR floor
#define MTL_RTAO_ISNAN isnan
#else
#include <math.h>
#define MTL_RTAO_COS cosf
#define MTL_RTAO_SIN sinf
#define MTL_RTAO_FLOOR floorf
#define MTL_RTAO_ISNAN isnan
#endif

/*
 * How far off the surface, along its stored normal, an AO ray starts, for
 * a point dist units from the eye: TASK55's rule (mtl_rts_bias), half a
 * unit plus k dist^2, k being four steps of a float depth near 1 per unit
 * of the near plane's distance (4 x 2^-24 / gl_znear). The point comes
 * from the depth buffer, whose step at dist is 2^-24 dist^2 / znear, so
 * the start lies above the true plane wherever the depth's error is
 * within four steps; with every ray above that plane (MTL_RTAO_MIN_COS)
 * the pixel's own surface is never hit.
 */
static inline float
mtl_rtao_bias(float dist, float k)
{
	return 0.5f + k * dist * dist;
}

/* the fractional part, in [0, 1) */
static inline float
mtl_rtao_fract(float v)
{
	float f = v - MTL_RTAO_FLOOR(v);

	return (f < 1.0f) ? f : 0.0f;
}

/*
 * The four offsets into the tile (x then y) of the four numbers a pixel
 * draws: the first ray's azimuth and elevation, then the second's. Each
 * is the tile at another place, so the four are not the same rank.
 */
static inline unsigned
mtl_rtao_dim_ox(unsigned d)
{
	return (d == 0) ? 0u : (d == 1) ? 37u : (d == 2) ? 13u : 51u;
}

static inline unsigned
mtl_rtao_dim_oy(unsigned d)
{
	return (d == 0) ? 0u : (d == 1) ? 21u : (d == 2) ? 45u : 7u;
}

/*
 * The per-frame step of each number (the R2 and R4 sequences' constants,
 * 0.61803399, 0.75487767, 0.56984029 and 0.81917251) in 32-bit fixed
 * point, the constant times 2^32 rounded: a frame moves every rank by it.
 */
static inline unsigned
mtl_rtao_dim_step(unsigned d)
{
	return (d == 0) ? 2654435775u : (d == 1) ? 3242174905u : (d == 2) ? 2447445409u : 3518319140u;
}

/* the index into the tile of pixel (px, py)'s number d */
static inline unsigned
mtl_rtao_tile_index(unsigned px, unsigned py, unsigned d)
{
	const unsigned m = MTL_RTAO_TILE - 1u;

	return ((py + mtl_rtao_dim_oy(d)) & m) * MTL_RTAO_TILE + ((px + mtl_rtao_dim_ox(d)) & m);
}

/*
 * A number in [0, 1) from a rank of the tile (0 to MTL_RTAO_TILE_N - 1)
 * at frame frame: the rank's centre, moved on by frame steps of d's
 * constant and wrapped (a toroidal shift, which keeps the tile's spectrum
 * within a frame and spreads each pixel's numbers over the frames).
 *
 * In 32-bit fixed point, where the unsigned sum and product wrap modulo
 * 2^32, which is the wrap itself, exactly: the rank's centre is rank x 2^20
 * plus 2^19, the shift frame x step. The top 24 bits of the sum, which a
 * float holds exactly, times 2^-24 (a power of two) are the number, so the
 * kernel and the host compute the same bits, fast math or not. One shift
 * added to every rank is a bijection of the 32-bit values, and any two
 * ranks stay at least 2^20 apart, so at every frame the tile's
 * MTL_RTAO_TILE_N numbers stay distinct. (A float frame x step does not: past
 * frame 2^11 its step is coarser than a rank's, 2^-12, and neighbouring
 * ranks round to one number, 256 of 4096 left at frame 65535.)
 */
static inline float
mtl_rtao_noise(unsigned rank, unsigned frame, unsigned d)
{
	/* rank x 2^20 with MTL_RTAO_TILE_N 2^12 ranks fills the 32 bits; the caller wraps the frame at 2^16 */
	const unsigned u = (rank << 20) + (1u << 19) + (frame & 0xffffu) * mtl_rtao_dim_step(d);

	return (float)(u >> 8) * (1.0f / 16777216.0f);
}

/*
 * The direction of ray k (0 or 1) about the unit normal n from its two
 * numbers u (azimuth) and v (elevation): cosine-weighted over the
 * hemisphere (r = sqrt(v), z = sqrt(1 - v)), the two rays' azimuths in
 * the two halves of the circle (stratified), with z at least
 * MTL_RTAO_MIN_COS. The frame about n is Duff et al.'s branchless one
 * (JCGT 2017), continuous but for the sign of n.z.
 */
static inline mtl_rtao_v3_t
mtl_rtao_dir(mtl_rtao_v3_t n, unsigned k, float u, float v)
{
	const float s = (n.z >= 0.0f) ? 1.0f : -1.0f;
	const float a = -1.0f / (s + n.z);
	const float b = n.x * n.y * a;
	const mtl_rtao_v3_t t = mtl_rtao_v3(1.0f + s * n.x * n.x * a, s * b, -s * n.x);
	const mtl_rtao_v3_t bt = mtl_rtao_v3(b, s + n.y * n.y * a, -n.y);
	const float vmax = 1.0f - MTL_RTAO_MIN_COS * MTL_RTAO_MIN_COS;
	const float vv = (v < 0.0f) ? 0.0f : (v > vmax) ? vmax : v;
	const float phi = 3.14159265f * ((float)(k & 1u) + u);
	const float r = MTL_SQRT(vv);
	const float lx = r * MTL_RTAO_COS(phi), ly = r * MTL_RTAO_SIN(phi);
	float lz = MTL_SQRT(1.0f - vv);

	if (lz < MTL_RTAO_MIN_COS)
	{
		lz = MTL_RTAO_MIN_COS;
	}

	return mtl_rtao_v3(t.x * lx + bt.x * ly + n.x * lz, t.y * lx + bt.y * ly + n.y * lz,
			t.z * lx + bt.z * ly + n.z * lz);
}

/*
 * 1 when a triangle of material id mat (the G-buffer's, rm_rt_materials')
 * occludes: the world and the brush entities' solid faces and every
 * alias model (an instance with no map triangle, mat 0xff here). The sky
 * is open, and water and translucent faces let the ambient light through,
 * so a ray passes them.
 */
static inline int
mtl_rtao_occludes(unsigned mat)
{
	return (mat != 3u) && (mat != 4u) && (mat != 5u);
}

/*
 * What a ray that met an occluder at distance t (range the ray's length)
 * takes from the pixel's ambient light: (1 - t / range)^2, so a face at
 * the pixel (a crease) takes it all and one at the ray's end none; a ray
 * that met nothing takes 0. Not finite or range not above 0: 0.
 */
static inline float
mtl_rtao_occlusion(int hit, float t, float range)
{
	float f;

	if (!hit || !(range > 0.0f) || !(t >= 0.0f))
	{
		return 0.0f;
	}

	f = 1.0f - t / range;
	f = (f < 0.0f) ? 0.0f : (f > 1.0f) ? 1.0f : f;
	return f * f;
}

/* the pixel's ambient occlusion from its rays' occlusions: 1 - their mean, in [0, 1] (1 unoccluded) */
static inline float
mtl_rtao_ao(float o0, float o1)
{
	const float ao = 1.0f - 0.5f * (o0 + o1);

	return (ao < 0.0f) ? 0.0f : (ao > 1.0f) ? 1.0f : ao;
}

/*
 * The factor the lightmap term takes (design section 2): the AO clamped
 * to [floor, 1], floor being r_rt_ao_min as rm_rtao_floor reads it (0 to
 * 1). An AO that is not finite counts as 1.
 */
static inline float
mtl_rtao_factor(float ao, float floor_)
{
	if (MTL_RTAO_ISNAN(ao))
	{
		return 1.0f;
	}

	if (ao > 1.0f)
	{
		return 1.0f;
	}

	return (ao < floor_) ? floor_ : ao;
}

/*
 * The light attachment a resolve reads, with the AO: a world pixel's
 * (material 1, the lightmap's sample) colour times the factor, its alpha
 * kept; every other material's light (an alias model's vertex light, the
 * warp's colour, the sky's white) as it was.
 */
static inline mtl_rgba_t
mtl_rtao_light(mtl_rgba_t l, unsigned material, float factor)
{
	if (material == 1u)
	{
		l.r *= factor;
		l.g *= factor;
		l.b *= factor;
	}

	return l;
}

#endif
