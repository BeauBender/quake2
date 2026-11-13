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
 * The frame core of the Metal shim (design sections 2 and 3): the SDL
 * OpenGL calls gl1_sdl.c makes (remapped by shim_sdl.h), clear,
 * viewport, scissor, depth range, finish, the GL strings, the
 * proc-address table and glReadPixels. Everything that touches Metal is
 * behind the mgl_layer_* functions of mgl_layer.m.
 *
 * Frame lifecycle: a frame opens lazily, on the first call that needs
 * the target (glClear, glReadPixels, the swap), and ends at the swap
 * (mgl_SwapWindow), which presents and commits it. A frame the layer
 * could not open (three frames still on the GPU after MGL_FRAME_WAIT_MS)
 * is skipped: its clears do nothing, its glReadPixels sets
 * GL_INVALID_OPERATION and writes nothing, and the swap ends it.
 *
 * =======================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "mgl_internal.h"

/* The SDL replacements are declared once, for gl1, in shim_sdl.h. */
#include "shim_sdl.h"

#define MGL_VENDOR "Yamagi Quake II Metal shim"
#define MGL_VERSION "1.4 mgl"
#define MGL_EXTENSIONS "GL_ARB_multitexture GL_ARB_point_parameters GL_ARB_texture_non_power_of_two"

static mgl_config_t mgl_config = {24, 8, 0, 0};
static char mgl_context_token;
static int mgl_context_live;

/* ---- the SDL OpenGL calls of gl1_sdl.c ------------------------------- */

bool
mgl_SetAttribute(SDL_GLAttr attr, int value)
{
	switch (attr)
	{
		case SDL_GL_RED_SIZE:
		case SDL_GL_GREEN_SIZE:
		case SDL_GL_BLUE_SIZE:
		case SDL_GL_ALPHA_SIZE:
			return value <= 8 ? true : SDL_SetError("mgl: at most 8 bits per colour channel");
		case SDL_GL_DEPTH_SIZE:
			if (value > 32)
			{
				return SDL_SetError("mgl: at most 32 depth bits");
			}
			mgl_config.depth_bits = value;
			return true;
		case SDL_GL_STENCIL_SIZE:
			if (value > 8)
			{
				return SDL_SetError("mgl: at most 8 stencil bits");
			}
			mgl_config.stencil_bits = value;
			return true;
		case SDL_GL_DOUBLEBUFFER:
		case SDL_GL_ACCELERATED_VISUAL:
			return true;
		case SDL_GL_MULTISAMPLEBUFFERS:
		case SDL_GL_MULTISAMPLESAMPLES:
			/* MSAA is a later card's: only "off" is accepted, so gl1 turns r_msaa_samples off. */
			if (value != 0)
			{
				return SDL_SetError("mgl: the Metal shim has no multisampling yet");
			}
			if (attr == SDL_GL_MULTISAMPLEBUFFERS)
			{
				mgl_config.msaa_buffers = 0;
			}
			else
			{
				mgl_config.msaa_samples = 0;
			}
			return true;
		default:
			return SDL_SetError("mgl: GL attribute %d is not supported", (int)attr);
	}
}

bool
mgl_GetAttribute(SDL_GLAttr attr, int *value)
{
	if (!value)
	{
		return SDL_InvalidParamError("value");
	}

	switch (attr)
	{
		case SDL_GL_RED_SIZE:
		case SDL_GL_GREEN_SIZE:
		case SDL_GL_BLUE_SIZE:
		case SDL_GL_ALPHA_SIZE:
			*value = 8;   /* bgra8Unorm */
			return true;
		case SDL_GL_DEPTH_SIZE:
			*value = 32;  /* depth32Float_stencil8 */
			return true;
		case SDL_GL_STENCIL_SIZE:
			*value = 8;
			return true;
		case SDL_GL_DOUBLEBUFFER:
		case SDL_GL_ACCELERATED_VISUAL:
			*value = 1;
			return true;
		case SDL_GL_MULTISAMPLEBUFFERS:
		case SDL_GL_MULTISAMPLESAMPLES:
			*value = 0;
			return true;
		default:
			return SDL_SetError("mgl: GL attribute %d is not supported", (int)attr);
	}
}

SDL_GLContext
mgl_CreateContext(SDL_Window *window)
{
	char err[256];
	int w = 0, h = 0;

	if (!window)
	{
		SDL_InvalidParamError("window");
		return NULL;
	}

	if (mgl_context_live)
	{
		SDL_SetError("mgl: the Metal shim has one context at a time");
		return NULL;
	}

	mgl_state_reset();

	if (!mgl_layer_create(window, &mgl_config, err, sizeof(err)))
	{
		SDL_SetError("%s", err);
		return NULL;
	}

	/* GL sets the viewport and scissor to the window when a context is first made current. */
	SDL_GetWindowSizeInPixels(window, &w, &h);
	mgl_state.viewport[2] = mgl_state.scissor[2] = w;
	mgl_state.viewport[3] = mgl_state.scissor[3] = h;

	mgl_context_live = 1;
	return (SDL_GLContext)(void *)&mgl_context_token;
}

bool
mgl_DestroyContext(SDL_GLContext context)
{
	if (!mgl_context_live || context != (SDL_GLContext)(void *)&mgl_context_token)
	{
		return SDL_SetError("mgl: not the shim's context");
	}

	mgl_layer_destroy();
	mgl_context_live = 0;
	return true;
}

bool
mgl_SwapWindow(SDL_Window *window)
{
	(void)window;

	if (!mgl_layer_ready())
	{
		return SDL_SetError("mgl: no context");
	}

	return mgl_layer_present() ? true : SDL_SetError("mgl: the frame could not be presented");
}

bool
mgl_SetSwapInterval(int interval)
{
	if (interval < 0)
	{
		/* gl1 then falls back to 1. */
		return SDL_SetError("mgl: adaptive vsync is not supported");
	}

	mgl_layer_set_sync(interval != 0);
	return true;
}

bool
mgl_GetSwapInterval(int *interval)
{
	if (!interval)
	{
		return SDL_InvalidParamError("interval");
	}

	*interval = mgl_layer_get_sync() ? 1 : 0;
	return true;
}

/*
 * The proc-address table: every GL entry point the shim exports, by
 * name, plus the ARB and EXT aliases gl1 asks for. An unknown name
 * answers NULL, as SDL_GL_GetProcAddress does.
 */
#define MGL_PROC(name) { #name, (void *)name }

static const struct
{
	const char *name;
	void *proc;
} mgl_procs[] = {
	MGL_PROC(glAlphaFunc), MGL_PROC(glBindTexture), MGL_PROC(glBlendFunc),
	MGL_PROC(glClear), MGL_PROC(glClearColor), MGL_PROC(glClearStencil),
	MGL_PROC(glColor4f), MGL_PROC(glColor4ub), MGL_PROC(glColorMask),
	MGL_PROC(glColorPointer), MGL_PROC(glCullFace), MGL_PROC(glDeleteTextures),
	MGL_PROC(glDepthFunc), MGL_PROC(glDepthMask), MGL_PROC(glDepthRange),
	MGL_PROC(glDisable), MGL_PROC(glDisableClientState), MGL_PROC(glDrawArrays),
	MGL_PROC(glDrawBuffer), MGL_PROC(glDrawElements), MGL_PROC(glEnable),
	MGL_PROC(glEnableClientState), MGL_PROC(glFinish), MGL_PROC(glFrustum),
	MGL_PROC(glGetError), MGL_PROC(glGetFloatv), MGL_PROC(glGetString),
	MGL_PROC(glHint), MGL_PROC(glLoadIdentity), MGL_PROC(glLoadMatrixf),
	MGL_PROC(glMatrixMode), MGL_PROC(glOrtho), MGL_PROC(glPixelStorei),
	MGL_PROC(glPointSize), MGL_PROC(glPolygonMode), MGL_PROC(glPolygonOffset),
	MGL_PROC(glPopMatrix), MGL_PROC(glPushMatrix), MGL_PROC(glReadPixels),
	MGL_PROC(glRotatef), MGL_PROC(glScalef), MGL_PROC(glScissor),
	MGL_PROC(glShadeModel), MGL_PROC(glStencilFunc), MGL_PROC(glStencilMask),
	MGL_PROC(glStencilOp), MGL_PROC(glTexCoordPointer), MGL_PROC(glTexEnvi),
	MGL_PROC(glTexImage2D), MGL_PROC(glTexParameteri), MGL_PROC(glTexSubImage2D),
	MGL_PROC(glTranslatef), MGL_PROC(glVertexPointer), MGL_PROC(glViewport),
	MGL_PROC(glActiveTexture), MGL_PROC(glClientActiveTexture),
	MGL_PROC(glPointParameterf), MGL_PROC(glPointParameterfv),
	MGL_PROC(glColorTableEXT), MGL_PROC(glReadnPixels),
	{ "glActiveTextureARB", (void *)glActiveTexture },
	{ "glClientActiveTextureARB", (void *)glClientActiveTexture },
	{ "glPointParameterfARB", (void *)glPointParameterf },
	{ "glPointParameterfvARB", (void *)glPointParameterfv },
	{ "glPointParameterfEXT", (void *)glPointParameterf },
	{ "glPointParameterfvEXT", (void *)glPointParameterfv },
};

#undef MGL_PROC

void *
mgl_GetProcAddress(const char *proc)
{
	size_t i;

	if (!proc)
	{
		return NULL;
	}

	for (i = 0; i < sizeof(mgl_procs) / sizeof(mgl_procs[0]); i++)
	{
		if (strcmp(mgl_procs[i].name, proc) == 0)
		{
			return mgl_procs[i].proc;
		}
	}

	return NULL;
}

/* ---- strings -------------------------------------------------------------- */

const GLubyte *
glGetString(GLenum name)
{
	static char renderer[256];

	switch (name)
	{
		case GL_VENDOR:
			return (const GLubyte *)MGL_VENDOR;
		case GL_RENDERER:
			/* the suffix names the build to the pin harness (scripts/pins.sh) */
			snprintf(renderer, sizeof(renderer), "Metal shim on %s (%s)",
					mgl_layer_ready() ? mgl_layer_device_name() : "no device",
					MGL_RENDERER_SUFFIX);
			return (const GLubyte *)renderer;
		case GL_VERSION:
			return (const GLubyte *)MGL_VERSION;
		case GL_EXTENSIONS:
			return (const GLubyte *)MGL_EXTENSIONS;
		default:
			mgl_error(GL_INVALID_ENUM);
			return NULL;
	}
}

/* ---- viewport, scissor, depth range ------------------------------------ */

void
mgl_compute_raster(const mgl_state_t *s, int tw, int th, mgl_raster_t *r)
{
	long long x0, y0, x1, y1;

	r->x = s->viewport[0];
	r->y = (double)th - s->viewport[1] - s->viewport[3];
	r->w = s->viewport[2];
	r->h = s->viewport[3];
	r->znear = s->depth_near;
	r->zfar = s->depth_far;

	r->scissor_empty = 0;

	if (!(s->enables & MGL_EN_SCISSOR_TEST))
	{
		r->sx = r->sy = 0;
		r->sw = tw;
		r->sh = th;
		return;
	}

	x0 = s->scissor[0];
	y0 = (long long)th - s->scissor[1] - s->scissor[3];
	x1 = x0 + s->scissor[2];
	y1 = y0 + s->scissor[3];

	x0 = x0 < 0 ? 0 : (x0 > tw ? tw : x0);
	x1 = x1 < 0 ? 0 : (x1 > tw ? tw : x1);
	y0 = y0 < 0 ? 0 : (y0 > th ? th : y0);
	y1 = y1 < 0 ? 0 : (y1 > th ? th : y1);

	r->sx = (int)x0;
	r->sy = (int)y0;
	r->sw = (int)(x1 - x0);
	r->sh = (int)(y1 - y0);
	r->scissor_empty = (r->sw <= 0 || r->sh <= 0);
}

static GLclampd
mgl_clampd(GLclampd v)
{
	return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

void
glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
	if (width < 0 || height < 0)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	mgl_state.viewport[0] = x;
	mgl_state.viewport[1] = y;
	mgl_state.viewport[2] = width;
	mgl_state.viewport[3] = height;
	mgl_layer_update_raster();
}

void
glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
	if (width < 0 || height < 0)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	mgl_state.scissor[0] = x;
	mgl_state.scissor[1] = y;
	mgl_state.scissor[2] = width;
	mgl_state.scissor[3] = height;
	mgl_layer_update_raster();
}

void
glDepthRange(GLclampd zNear, GLclampd zFar)
{
	mgl_state.depth_near = mgl_clampd(zNear);
	mgl_state.depth_far = mgl_clampd(zFar);
	mgl_layer_update_raster();
}

void
glDrawBuffer(GLenum mode)
{
	/* One colour attachment: every buffer gl1 names is that attachment. */
	switch (mode)
	{
		case GL_NONE:
		case GL_FRONT:
		case GL_BACK:
		case GL_FRONT_LEFT:
		case GL_BACK_LEFT:
		case GL_LEFT:
		case GL_FRONT_AND_BACK:
			mgl_state.draw_buffer = mode;
			break;
		case GL_FRONT_RIGHT:
		case GL_BACK_RIGHT:
		case GL_RIGHT:
			mgl_error(GL_INVALID_OPERATION);   /* no stereo buffers */
			break;
		default:
			mgl_error(GL_INVALID_ENUM);
	}
}

/* ---- clear ----------------------------------------------------------------- */

void
glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
	GLclampf c[4] = {red, green, blue, alpha};
	int i;

	for (i = 0; i < 4; i++)
	{
		mgl_state.clear_color[i] = c[i] < 0.0f ? 0.0f : (c[i] > 1.0f ? 1.0f : c[i]);
	}
}

void
glClearStencil(GLint s)
{
	mgl_state.clear_stencil = s;
}

/*
 * A clear is the load action of a new pass. What a load action cannot
 * express (a partial colour or stencil write mask, a scissor that does
 * not cover the target) sets the sticky error and leaves that part of
 * the target as it is; a later card draws such clears as a quad.
 */
void
glClear(GLbitfield mask)
{
	const GLbitfield all = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
	static int once_color, once_stencil, once_scissor;
	GLbitfield eff = mask;
	mgl_raster_t r;
	int tw, th, n, i;

	if (mask & ~all)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	if (!mgl_layer_ready())
	{
		mgl_error(GL_INVALID_OPERATION);
		return;
	}

	if (eff & GL_COLOR_BUFFER_BIT)
	{
		for (n = 0, i = 0; i < 4; i++)
		{
			n += mgl_state.color_mask[i] ? 1 : 0;
		}

		if (n < 4)
		{
			if (n > 0)
			{
				mgl_unimplemented("glClear with a partial colour mask", &once_color);
			}

			eff &= ~GL_COLOR_BUFFER_BIT;
		}
	}

	if ((eff & GL_DEPTH_BUFFER_BIT) && !mgl_state.depth_mask)
	{
		eff &= ~GL_DEPTH_BUFFER_BIT;
	}

	if (eff & GL_STENCIL_BUFFER_BIT)
	{
		GLuint m = mgl_state.stencil_write_mask & 0xff;

		if (m != 0xff)
		{
			if (m != 0)
			{
				mgl_unimplemented("glClear with a partial stencil mask", &once_stencil);
			}

			eff &= ~GL_STENCIL_BUFFER_BIT;
		}
	}

	if (!eff || !mgl_layer_frame_begin(&tw, &th))
	{
		return;
	}

	mgl_compute_raster(&mgl_state, tw, th, &r);

	if (r.sx != 0 || r.sy != 0 || r.sw != tw || r.sh != th)
	{
		if (!r.scissor_empty)
		{
			mgl_unimplemented("glClear inside a scissor rectangle", &once_scissor);
		}

		return;
	}

	mgl_layer_pass(eff, mgl_state.clear_color, 1.0, (unsigned)mgl_state.clear_stencil & 0xff);
}

void
glFinish(void)
{
	if (mgl_layer_ready())
	{
		mgl_layer_finish();
	}
}

/* ---- readback ------------------------------------------------------------- */

GLenum
mgl_read_contract(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
		GLenum type, GLint alignment, int tw, int th,
		size_t *bpp, size_t *rowbytes, size_t *total)
{
	unsigned long long row, sum;

	*bpp = *rowbytes = *total = 0;

	if (w < 0 || h < 0)
	{
		return GL_INVALID_VALUE;
	}

	switch (format)
	{
		case GL_RGB:
			*bpp = 3;
			break;
		case GL_RGBA:
		case GL_BGRA:
			*bpp = 4;
			break;
		default:
			return GL_INVALID_ENUM;
	}

	if (type != GL_UNSIGNED_BYTE)
	{
		*bpp = 0;
		return GL_INVALID_ENUM;
	}

	if (alignment != 1 && alignment != 2 && alignment != 4 && alignment != 8)
	{
		return GL_INVALID_VALUE;
	}

	/* The rectangle lies inside the target, or nothing is read. */
	if (x < 0 || y < 0 || (long long)x + w > tw || (long long)y + h > th)
	{
		return GL_INVALID_VALUE;
	}

	if (w == 0 || h == 0)
	{
		return GL_NO_ERROR;
	}

	row = (unsigned long long)w * *bpp;
	row = (row + (unsigned)alignment - 1) / (unsigned)alignment * (unsigned)alignment;
	/* GL needs no padding after the last row. */
	sum = row * (unsigned long long)(h - 1) + (unsigned long long)w * *bpp;

	if (sum > (unsigned long long)SIZE_MAX)
	{
		return GL_OUT_OF_MEMORY;
	}

	*rowbytes = (size_t)row;
	*total = (size_t)sum;
	return GL_NO_ERROR;
}

/*
 * A verification hook, off unless the environment variable
 * MGL_READBACK_DUMP names an absolute directory (characters
 * A-Z a-z 0-9 _ . / -, no ".."): every successful read is also written
 * there, byte for byte as the caller received it, as
 * mgl-readback-NNNNNN.raw, with one line per read in mgl-readback.txt
 * ("NNNNNN x y w h format type alignment bytes"). It lets a frame the
 * pin harness captured be compared with what glReadPixels returned.
 */
static const char *
mgl_dump_dir(void)
{
	static int checked;
	static char dir[512];
	const char *d, *p;

	if (checked)
	{
		return dir[0] ? dir : NULL;
	}

	checked = 1;
	d = getenv("MGL_READBACK_DUMP");

	if (!d || !d[0])
	{
		return NULL;
	}

	for (p = d; *p; p++)
	{
		if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
			(*p >= '0' && *p <= '9') || *p == '_' || *p == '.' || *p == '/' || *p == '-'))
		{
			break;
		}
	}

	if (*p || d[0] != '/' || strstr(d, "..") || strlen(d) >= sizeof(dir) - 64)
	{
		fprintf(stderr, "mgl: MGL_READBACK_DUMP refused (an absolute path of A-Z a-z 0-9 _ . / - without '..')\n");
		return NULL;
	}

	memcpy(dir, d, strlen(d) + 1);
	return dir;
}

static void
mgl_dump_read(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
		const void *data, size_t total)
{
	static unsigned n;
	const char *dir = mgl_dump_dir();
	char path[600];
	FILE *f;
	int ok;

	if (!dir)
	{
		return;
	}

	snprintf(path, sizeof(path), "%s/mgl-readback-%06u.raw", dir, n);
	f = fopen(path, "wb");
	ok = f && fwrite(data, 1, total, f) == total;

	if (f && fclose(f) != 0)
	{
		ok = 0;
	}

	snprintf(path, sizeof(path), "%s/mgl-readback.txt", dir);
	f = fopen(path, "ab");

	if (f)
	{
		fprintf(f, "%s%06u %d %d %d %d 0x%04x 0x%04x %d %zu\n", ok ? "" : "FAILED ", n,
				x, y, w, h, format, GL_UNSIGNED_BYTE, mgl_state.pack_alignment, total);
		fclose(f);
	}

	n++;
}

/* bound: what the caller's buffer holds, SIZE_MAX when glReadPixels gives no bound */
static void
mgl_read(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,
		size_t bound, GLvoid *pixels)
{
	size_t bpp, rowbytes, total, r, c;
	unsigned char *bgra, *dst;
	const unsigned char *src;
	int tw, th;
	GLenum e;

	if (!mgl_layer_ready())
	{
		mgl_error(GL_INVALID_OPERATION);
		return;
	}

	if (!mgl_layer_frame_begin(&tw, &th))
	{
		/* skipped frame: there is nothing to read */
		mgl_error(GL_INVALID_OPERATION);
		return;
	}

	e = mgl_read_contract(x, y, w, h, format, type, mgl_state.pack_alignment,
			tw, th, &bpp, &rowbytes, &total);

	if (e != GL_NO_ERROR)
	{
		mgl_error(e);
		return;
	}

	if (total > bound)
	{
		mgl_error(GL_INVALID_OPERATION);
		return;
	}

	if (total == 0)
	{
		return;
	}

	if (!pixels)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	if (!mgl_layer_ensure_pass())
	{
		mgl_error(GL_INVALID_OPERATION);
		return;
	}

	bgra = malloc((size_t)w * (size_t)h * 4);

	if (!bgra)
	{
		mgl_error(GL_OUT_OF_MEMORY);
		return;
	}

	/* Metal's rows run top down: GL row y + h - 1 is Metal row th - y - h. */
	if (!mgl_layer_read(x, th - y - h, w, h, bgra))
	{
		free(bgra);
		mgl_error(GL_INVALID_OPERATION);
		return;
	}

	/* GL row r (bottom up) is row h - 1 - r of what Metal returned; padding is left untouched. */
	for (r = 0; r < (size_t)h; r++)
	{
		src = bgra + ((size_t)h - 1 - r) * (size_t)w * 4;
		dst = (unsigned char *)pixels + r * rowbytes;

		for (c = 0; c < (size_t)w; c++, src += 4, dst += bpp)
		{
			if (format == GL_BGRA)
			{
				memcpy(dst, src, 4);
				continue;
			}

			dst[0] = src[2];
			dst[1] = src[1];
			dst[2] = src[0];

			if (bpp == 4)
			{
				dst[3] = src[3];
			}
		}
	}

	free(bgra);
	mgl_dump_read(x, y, w, h, format, pixels, total);
}

void
glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
		GLenum type, GLvoid *pixels)
{
	mgl_read(x, y, width, height, format, type, SIZE_MAX, pixels);
}

void
glReadnPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
		GLenum type, GLsizei bufSize, GLvoid *data)
{
	if (bufSize < 0)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	mgl_read(x, y, width, height, format, type, (size_t)bufSize, data);
}
