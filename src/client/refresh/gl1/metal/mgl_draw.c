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
 * The draw path of the Metal shim: glDrawArrays and glDrawElements
 * become encoder draws, with fan and strip index expansion and the
 * pipeline-state cache (design section 2). TASK48 to TASK52 grow it;
 * until then both calls draw nothing and set the sticky error.
 *
 * =======================================================================
 */

#include "mgl_internal.h"

void
glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	(void)mode; (void)first; (void)count;
	MGL_UNIMPLEMENTED();
}

void
glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
	(void)mode; (void)count; (void)type; (void)indices;
	MGL_UNIMPLEMENTED();
}
