/*
 * Empty on purpose: the OpenGL subset of the Metal shim declares
 * everything gl1 uses in <OpenGL/gl.h>. This file exists so that a
 * transitive "#include <OpenGL/glext.h>" resolves here and never to
 * Apple's framework header.
 */

#ifndef MGL_GLEXT_H
#define MGL_GLEXT_H

#ifndef __glext_h_
#define __glext_h_
#endif

#endif /* MGL_GLEXT_H */
