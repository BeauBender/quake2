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
 * Texture objects and pixel storage of the Metal shim. The pixel
 * storage modes are state (glReadPixels honours GL_PACK_ALIGNMENT); the
 * texture objects, uploads and samplers are TASK48 and are stubs here.
 *
 * =======================================================================
 */

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

void
glBindTexture(GLenum target, GLuint texture)
{
	(void)target; (void)texture;
	MGL_UNIMPLEMENTED();
}

void
glDeleteTextures(GLsizei n, const GLuint *textures)
{
	(void)n; (void)textures;
	MGL_UNIMPLEMENTED();
}

void
glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width,
		GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	(void)target; (void)level; (void)internalformat; (void)width; (void)height;
	(void)border; (void)format; (void)type; (void)pixels;
	MGL_UNIMPLEMENTED();
}

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
	(void)target; (void)pname; (void)param;
	MGL_UNIMPLEMENTED();
}
