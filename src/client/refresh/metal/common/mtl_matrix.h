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
 * The matrix arithmetic shared by ref_metal and the OpenGL-subset shim
 * (src/client/refresh/gl1/metal/), moved here from the shim's
 * mgl_raster.c (mgl_mat_mul) and mgl_state.c (glRotatef, glTranslatef,
 * glScalef, glOrtho, glFrustum) by TASK85
 * (docs/plans/ref-metal-native.md, section 8). Column-major 4x4 as GL
 * keeps them. The product is a float sum per element; the rotation is
 * built in double and stored as float, as are the frustum and ortho
 * matrices from their double parameters; translate and scale are float.
 * Both renderers' position and depth values depend on this rounding, so
 * there is one copy.
 *
 * =======================================================================
 */

#ifndef MTL_MATRIX_H
#define MTL_MATRIX_H

/* m = the identity */
void mtl_mat_identity(float *m);

/* out = a * b; out may be a or b. */
void mtl_mat_mul(float *out, const float *a, const float *b);

/*
 * m = the rotation by angle degrees about (x, y, z) (GL 1.4, section
 * 2.11.2). 0 and m untouched for the zero vector, which GL leaves
 * undefined (the shim then leaves its matrix as it is).
 */
int mtl_mat_rotate(float *m, float angle, float x, float y, float z);

/* m = the translation by (x, y, z), and the scale by (x, y, z). */
void mtl_mat_translate(float *m, float x, float y, float z);
void mtl_mat_scale(float *m, float x, float y, float z);

/*
 * m = the orthographic and the perspective projection (GL 1.4, section
 * 2.11.2). 0 and m untouched for the arguments GL refuses with
 * GL_INVALID_VALUE.
 */
int mtl_mat_ortho(float *m, double left, double right, double bottom, double top,
		double zNear, double zFar);
int mtl_mat_frustum(float *m, double left, double right, double bottom, double top,
		double zNear, double zFar);

#endif /* MTL_MATRIX_H */
