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
 * The shared matrix arithmetic (mtl_matrix.h), moved verbatim from the
 * shim: the only change is the type names, GLfloat to float and GLdouble
 * to double (the shim's typedefs of the same types).
 *
 * =======================================================================
 */

#include <math.h>
#include <string.h>

#include "mtl_matrix.h"

/* from mgl_state.c, mgl_identity */
void
mtl_mat_identity(float *m)
{
	memset(m, 0, 16 * sizeof(float));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/* from mgl_raster.c, mgl_mat_mul */
void
mtl_mat_mul(float *out, const float *a, const float *b)
{
	float t[16];
	int r, c, k;

	for (c = 0; c < 4; c++)
	{
		for (r = 0; r < 4; r++)
		{
			float sum = 0.0f;

			for (k = 0; k < 4; k++)
			{
				sum += a[k * 4 + r] * b[c * 4 + k];
			}

			t[c * 4 + r] = sum;
		}
	}

	memcpy(out, t, sizeof(t));
}

/* from mgl_state.c, glRotatef */
int
mtl_mat_rotate(float *m, float angle, float x, float y, float z)
{
	double len = sqrt((double)x * x + (double)y * y + (double)z * z);
	double a = angle * (M_PI / 180.0), c = cos(a), s = sin(a), nx, ny, nz;

	/* GL leaves a rotation about the zero vector undefined: the matrix stays */
	if (len == 0.0)
	{
		return 0;
	}

	nx = x / len;
	ny = y / len;
	nz = z / len;

	memset(m, 0, 16 * sizeof(float));
	m[0] = (float)(nx * nx * (1 - c) + c);
	m[1] = (float)(ny * nx * (1 - c) + nz * s);
	m[2] = (float)(nx * nz * (1 - c) - ny * s);
	m[4] = (float)(nx * ny * (1 - c) - nz * s);
	m[5] = (float)(ny * ny * (1 - c) + c);
	m[6] = (float)(ny * nz * (1 - c) + nx * s);
	m[8] = (float)(nx * nz * (1 - c) + ny * s);
	m[9] = (float)(ny * nz * (1 - c) - nx * s);
	m[10] = (float)(nz * nz * (1 - c) + c);
	m[15] = 1.0f;

	return 1;
}

/* from mgl_state.c, glTranslatef */
void
mtl_mat_translate(float *m, float x, float y, float z)
{
	mtl_mat_identity(m);
	m[12] = x;
	m[13] = y;
	m[14] = z;
}

/* from mgl_state.c, glScalef */
void
mtl_mat_scale(float *m, float x, float y, float z)
{
	mtl_mat_identity(m);
	m[0] = x;
	m[5] = y;
	m[10] = z;
}

/* from mgl_state.c, glOrtho */
int
mtl_mat_ortho(float *m, double left, double right, double bottom, double top,
		double zNear, double zFar)
{
	if (left == right || bottom == top || zNear == zFar)
	{
		return 0;
	}

	memset(m, 0, 16 * sizeof(float));
	m[0] = (float)(2.0 / (right - left));
	m[5] = (float)(2.0 / (top - bottom));
	m[10] = (float)(-2.0 / (zFar - zNear));
	m[12] = (float)(-(right + left) / (right - left));
	m[13] = (float)(-(top + bottom) / (top - bottom));
	m[14] = (float)(-(zFar + zNear) / (zFar - zNear));
	m[15] = 1.0f;

	return 1;
}

/* from mgl_state.c, glFrustum */
int
mtl_mat_frustum(float *m, double left, double right, double bottom, double top,
		double zNear, double zFar)
{
	if (zNear <= 0 || zFar <= 0 || left == right || bottom == top || zNear == zFar)
	{
		return 0;
	}

	memset(m, 0, 16 * sizeof(float));
	m[0] = (float)(2.0 * zNear / (right - left));
	m[5] = (float)(2.0 * zNear / (top - bottom));
	m[8] = (float)((right + left) / (right - left));
	m[9] = (float)((top + bottom) / (top - bottom));
	m[10] = (float)(-(zFar + zNear) / (zFar - zNear));
	m[11] = -1.0f;
	m[14] = (float)(-2.0 * zFar * zNear / (zFar - zNear));

	return 1;
}
