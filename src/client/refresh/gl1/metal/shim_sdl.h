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
 * Prepended (-include) to every gl1 translation unit of a
 * WITH_METAL_SHIM=yes build. It remaps the SDL OpenGL calls gl1_sdl.c
 * makes, and the window flag it asks for, onto the shim, so the gl1
 * sources stay unchanged. SDL's own header is included first, so its
 * declarations keep their real names; only the uses in gl1 are renamed.
 * The shim's sources are compiled without this file.
 *
 * =======================================================================
 */

#ifndef MGL_SHIM_SDL_H
#define MGL_SHIM_SDL_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

bool mgl_SetAttribute(SDL_GLAttr attr, int value);
bool mgl_GetAttribute(SDL_GLAttr attr, int *value);
SDL_GLContext mgl_CreateContext(SDL_Window *window);
bool mgl_DestroyContext(SDL_GLContext context);
bool mgl_SwapWindow(SDL_Window *window);
bool mgl_SetSwapInterval(int interval);
bool mgl_GetSwapInterval(int *interval);
void *mgl_GetProcAddress(const char *proc);

#ifdef __cplusplus
}
#endif

#define SDL_GL_SetAttribute mgl_SetAttribute
#define SDL_GL_GetAttribute mgl_GetAttribute
#define SDL_GL_CreateContext mgl_CreateContext
#define SDL_GL_DestroyContext mgl_DestroyContext
#define SDL_GL_SwapWindow mgl_SwapWindow
#define SDL_GL_SetSwapInterval mgl_SetSwapInterval
#define SDL_GL_GetSwapInterval mgl_GetSwapInterval
#define SDL_GL_GetProcAddress mgl_GetProcAddress

#undef SDL_WINDOW_OPENGL
#define SDL_WINDOW_OPENGL SDL_WINDOW_METAL

#endif /* MGL_SHIM_SDL_H */
