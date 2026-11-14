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
 * Texture objects and pixel storage of the Metal shim. gl1 never calls
 * glGenTextures: it binds the names it chose (texnum), so the objects are
 * a fixed table indexed by name, MGL_MAX_TEXTURES long; a name past it is
 * refused with GL_INVALID_VALUE. Level 0 creates the Metal texture (RGBA
 * 8 bit, a full mip chain, filled from level 0 when GL_GENERATE_MIPMAP
 * is set); every upload is converted to tightly packed RGBA here and
 * handed to mgl_layer.m, which orders it between the draws around it.
 * The sampler is a packed key of the parameters, which the layer turns
 * into a cached MTLSamplerState. Only RGBA and RGB uploads of unsigned
 * bytes are accepted: the paletted path (GL_COLOR_INDEX8_EXT) is never
 * advertised and is refused. Levels above 0 (gl1 uploads its own chain
 * only without GL_ARB_texture_non_power_of_two, which the shim
 * advertises) and glTexSubImage2D (dynamic lightmaps) are S2b.
 *
 * =======================================================================
 */

#include <stdlib.h>
#include <string.h>

#include "mgl_internal.h"

void
glPixelStorei(GLenum pname, GLint param)
{
	switch (pname)
	{
		case GL_PACK_ALIGNMENT:
		case GL_UNPACK_ALIGNMENT:
			if (param != 1 && param != 2 && param != 4 && param != 8)
			{
				mgl_error(GL_INVALID_VALUE);
				return;
			}

			if (pname == GL_PACK_ALIGNMENT)
			{
				mgl_state.pack_alignment = param;
			}
			else
			{
				mgl_state.unpack_alignment = param;
			}
			break;
		case GL_UNPACK_ROW_LENGTH:
			if (param < 0)
			{
				mgl_error(GL_INVALID_VALUE);
				return;
			}

			mgl_state.unpack_row_length = param;
			break;
		default:
			/* Valid GL, but nothing gl1 sets: refused rather than ignored. */
			mgl_error(GL_INVALID_ENUM);
	}
}

static mgl_texture_t mgl_textures[MGL_MAX_TEXTURES];

/* The initial state of a texture object (GL 1.4, table 6.16). */
static void
mgl_texture_init(mgl_texture_t *t)
{
	memset(t, 0, sizeof(*t));
	t->min_filter = GL_NEAREST_MIPMAP_LINEAR;
	t->mag_filter = GL_LINEAR;
	t->wrap_s = t->wrap_t = GL_REPEAT;
}

mgl_texture_t *
mgl_texture(GLuint name)
{
	return name < MGL_MAX_TEXTURES ? &mgl_textures[name] : NULL;
}

void
mgl_textures_reset(void)
{
	GLuint i;

	for (i = 0; i < MGL_MAX_TEXTURES; i++)
	{
		if (mgl_textures[i].mtl)
		{
			mgl_layer_tex_release(mgl_textures[i].mtl);
		}

		mgl_texture_init(&mgl_textures[i]);
	}
}

/*
 * Bits 0-2 min filter (GL_NEAREST, GL_LINEAR, then the four mipmap
 * filters in enum order), bit 3 mag filter linear, bits 4-5 and 6-7
 * wrap s and t (0 repeat, 1 clamp, 2 clamp to edge).
 */
static uint32_t
mgl_wrap_bits(GLenum wrap)
{
	return wrap == GL_REPEAT ? 0 : (wrap == GL_CLAMP ? 1 : 2);
}

uint32_t
mgl_sampler_key(const mgl_texture_t *t)
{
	uint32_t min = t->min_filter == GL_NEAREST ? 0 : t->min_filter == GL_LINEAR ? 1 :
		2 + (uint32_t)(t->min_filter - GL_NEAREST_MIPMAP_NEAREST);

	return min | (t->mag_filter == GL_LINEAR ? 8u : 0u) |
		(mgl_wrap_bits(t->wrap_s) << 4) | (mgl_wrap_bits(t->wrap_t) << 6);
}

/* The bound texture of the active unit, for the calls that act on it. */
static mgl_texture_t *
mgl_bound(GLenum target)
{
	if (target != GL_TEXTURE_2D)
	{
		mgl_error(GL_INVALID_ENUM);
		return NULL;
	}

	return mgl_texture(mgl_state.unit[mgl_state.active_unit].bound);
}

void
glBindTexture(GLenum target, GLuint texture)
{
	if (target != GL_TEXTURE_2D)
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	if (!mgl_texture(texture))
	{
		/* valid GL, but past the table: refused, the binding stays */
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	mgl_state.unit[mgl_state.active_unit].bound = texture;
}

void
glDeleteTextures(GLsizei n, const GLuint *textures)
{
	mgl_texture_t *t;
	GLsizei i;
	int u;

	if (n < 0)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	for (i = 0; i < n && textures; i++)
	{
		/* names that are no texture are ignored, as GL does; 0 is the default texture */
		if (textures[i] == 0 || !(t = mgl_texture(textures[i])))
		{
			continue;
		}

		if (t->mtl)
		{
			mgl_layer_tex_release(t->mtl);
		}

		mgl_texture_init(t);

		for (u = 0; u < MGL_MAX_UNITS; u++)
		{
			if (mgl_state.unit[u].bound == textures[i])
			{
				mgl_state.unit[u].bound = 0;
			}
		}
	}
}

/* 1 for an internal format that keeps alpha, 0 for one without, -1 for one the shim refuses. */
static int
mgl_internal_alpha(GLint internalformat)
{
	switch (internalformat)
	{
		case 3:
		case GL_RGB:
		case GL_R3_G3_B2:
		case GL_RGB4:
		case GL_RGB5:
		case GL_RGB8:
			return 0;
		case 4:
		case GL_RGBA:
		case GL_RGBA2:
		case GL_RGBA4:
		case GL_RGB5_A1:
		case GL_RGBA8:
			return 1;
		default:
			return -1;
	}
}

static int
mgl_mip_count(int w, int h)
{
	int n = 1;

	while (w > 1 || h > 1)
	{
		w = w > 1 ? w / 2 : 1;
		h = h > 1 ? h / 2 : 1;
		n++;
	}

	return n;
}

void
glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width,
		GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	static int once_level;
	mgl_texture_t *t = mgl_bound(target);
	int alpha = mgl_internal_alpha(internalformat);
	unsigned char *rgba;

	if (!t)
	{
		return;
	}

	if (alpha < 0 || (format != GL_RGBA && format != GL_RGB) || type != GL_UNSIGNED_BYTE)
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	if (level < 0 || width < 0 || height < 0 || border != 0 ||
		width > MGL_MAX_TEXTURE_SIZE || height > MGL_MAX_TEXTURE_SIZE)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	if (level > 0)
	{
		mgl_unimplemented("glTexImage2D above level 0", &once_level);
		return;
	}

	if (width == 0 || height == 0)
	{
		return;
	}

	if (!t->mtl || t->width != width || t->height != height)
	{
		void *mtl = mgl_layer_tex_create(width, height, mgl_mip_count(width, height));

		if (!mtl)
		{
			mgl_error(GL_OUT_OF_MEMORY);
			return;
		}

		if (t->mtl)
		{
			mgl_layer_tex_release(t->mtl);
		}

		t->mtl = mtl;
		t->width = width;
		t->height = height;
		t->levels = mgl_mip_count(width, height);
	}

	t->opaque = alpha ? GL_FALSE : GL_TRUE;

	if (!pixels)
	{
		return;
	}

	rgba = malloc((size_t)width * (size_t)height * 4);

	if (!rgba)
	{
		mgl_error(GL_OUT_OF_MEMORY);
		return;
	}

	mgl_unpack_rgba(pixels, format, width, height, mgl_state.unpack_alignment,
			mgl_state.unpack_row_length, t->opaque, rgba);

	if (!mgl_layer_tex_upload(t->mtl, 0, 0, 0, width, height, rgba,
			t->generate_mipmap && t->levels > 1))
	{
		mgl_error(GL_OUT_OF_MEMORY);
	}

	free(rgba);
}

/* The dynamic lightmaps' path: S2b. */
void
glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
		GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
	(void)target; (void)level; (void)xoffset; (void)yoffset; (void)width;
	(void)height; (void)format; (void)type; (void)pixels;
	MGL_UNIMPLEMENTED();
}

void
glTexParameteri(GLenum target, GLenum pname, GLint param)
{
	mgl_texture_t *t = mgl_bound(target);

	if (!t)
	{
		return;
	}

	switch (pname)
	{
		case GL_TEXTURE_MIN_FILTER:
			if (param != GL_NEAREST && param != GL_LINEAR &&
				(param < GL_NEAREST_MIPMAP_NEAREST || param > GL_LINEAR_MIPMAP_LINEAR))
			{
				mgl_error(GL_INVALID_ENUM);
				return;
			}
			t->min_filter = (GLenum)param;
			break;
		case GL_TEXTURE_MAG_FILTER:
			if (param != GL_NEAREST && param != GL_LINEAR)
			{
				mgl_error(GL_INVALID_ENUM);
				return;
			}
			t->mag_filter = (GLenum)param;
			break;
		case GL_TEXTURE_WRAP_S:
		case GL_TEXTURE_WRAP_T:
			if (param != GL_REPEAT && param != GL_CLAMP && param != GL_CLAMP_TO_EDGE)
			{
				mgl_error(GL_INVALID_ENUM);
				return;
			}
			*(pname == GL_TEXTURE_WRAP_S ? &t->wrap_s : &t->wrap_t) = (GLenum)param;
			break;
		case GL_GENERATE_MIPMAP:
			t->generate_mipmap = param ? GL_TRUE : GL_FALSE;
			break;
		default:
			/* GL_TEXTURE_MAX_ANISOTROPY_EXT among them: the extension is not advertised */
			mgl_error(GL_INVALID_ENUM);
	}
}
