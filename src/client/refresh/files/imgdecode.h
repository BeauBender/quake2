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
 * PNG, JPEG and TGA decoding, shared by the renderers (files/image.c)
 * and the client (cl_image.c). See imgdecode.c and decision 0010.
 * Needs no engine header, so the host test links it alone.
 *
 * =======================================================================
 */

#ifndef SRC_CLIENT_REFRESH_FILES_IMGDECODE_H_
#define SRC_CLIENT_REFRESH_FILES_IMGDECODE_H_

#include <stddef.h>
#include <stdint.h>

/* The largest side of a decoded image (ref_metal's RMB_MAX_TEXTURE_SIDE). */
#define IMGDEC_MAX_SIDE 16384

/* The most pixels in a decoded image: 64 Mpx, 256 MiB of RGBA8. */
#define IMGDEC_MAX_AREA ((size_t)1 << 26)

/* The largest file read: an uncompressed 32-bit TGA of 8192x8192 fits. */
#define IMGDEC_MAX_FILE ((size_t)512 * 1024 * 1024)

typedef enum
{
	IMGDEC_TYPE_NONE = 0,
	IMGDEC_TYPE_PNG,
	IMGDEC_TYPE_JPEG,
	IMGDEC_TYPE_TGA
} imgdec_type_t;

typedef enum
{
	IMGDEC_OK = 0,
	IMGDEC_ERR_EXTENSION,	/* the name's extension is not png, jpg or tga */
	IMGDEC_ERR_FILESIZE,	/* no bytes, or more than IMGDEC_MAX_FILE */
	IMGDEC_ERR_TYPE,	/* the bytes are not a PNG, JPEG or TGA image */
	IMGDEC_ERR_MISMATCH,	/* the bytes' type is not the extension's */
	IMGDEC_ERR_SIZE,	/* a side is 0 or above IMGDEC_MAX_SIDE */
	IMGDEC_ERR_LAYOUT,	/* a pixel layout that is not converted */
	IMGDEC_ERR_DECODE,	/* the pixels could not be decoded */
	IMGDEC_ERR_MEMORY,
	IMGDEC_ERR_UNSUPPORTED,	/* no decoder on this platform */
	IMGDEC_ERR_ENCODE	/* IMGDEC_EncodePNG wrote no PNG */
} imgdec_result_t;

/* How the decoded pixels are laid out in memory (imgdecode.c). */
typedef enum
{
	IMGDEC_ALPHA_NONE = 0,	/* colour channels only */
	IMGDEC_ALPHA_LAST,	/* straight alpha after the colour */
	IMGDEC_ALPHA_FIRST,	/* straight alpha before the colour */
	IMGDEC_ALPHA_SKIP_LAST,	/* an unused channel after the colour */
	IMGDEC_ALPHA_SKIP_FIRST	/* an unused channel before the colour */
} imgdec_alpha_t;

typedef struct
{
	size_t width;
	size_t height;
	size_t bits_per_component;	/* 8 or 16 */
	size_t bits_per_pixel;
	size_t bytes_per_row;
	int colors;			/* 1: grey or a palette index, 3: RGB */
	imgdec_alpha_t alpha;
	size_t swap;			/* 0, or 2 or 4: little-endian words of that many bytes */
	const unsigned char *palette;	/* RGB triples when indexed, else NULL */
	size_t palette_count;
} imgdec_layout_t;

imgdec_type_t IMGDEC_TypeOfExtension(const char *ext);
imgdec_type_t IMGDEC_TypeOfIdentifier(const char *uti);

/*
 * IMGDEC_OK when the type identifier uti names want, IMGDEC_ERR_TYPE
 * when it names no PNG, JPEG or TGA, else IMGDEC_ERR_MISMATCH.
 */
imgdec_result_t IMGDEC_TypeCheck(const char *uti, imgdec_type_t want);

/* 1 when a file of size bytes may be read: 1 to max_size bytes */
int IMGDEC_FileSizeOk(size_t size, size_t max_size);

/*
 * 1 and *bytes = width * height * 4 when both sides are in 1..max_side
 * and the product does not overflow, else 0.
 */
int IMGDEC_PixelBytes(size_t width, size_t height, size_t max_side,
		size_t *bytes);

/* 1 when width * height is at most max_area, computed without overflow */
int IMGDEC_AreaOk(size_t width, size_t height, size_t max_area);

/*
 * 1 when the size bytes at data start with a TGA header naming a layout
 * ImageIO decodes to wrong pixels: a colour map (type 1 or 9) whose
 * entries are not 24-bit, or 15-bit pixels. Reads only header bytes
 * inside size; 0 when there are fewer than the header's 18.
 */
int IMGDEC_TgaLayoutRefused(const unsigned char *data, size_t size);

/*
 * Converts srclen bytes laid out as described into straight-alpha RGBA8
 * (width * height * 4 bytes at dst, which holds dstlen), with no colour
 * conversion; 16-bit components keep their high byte, as stb_image did.
 */
imgdec_result_t IMGDEC_ToRGBA(const imgdec_layout_t *layout,
		const unsigned char *src, size_t srclen, unsigned char *dst,
		size_t dstlen);

#ifdef __APPLE__
/*
 * The byte order and alpha of a CGBitmapInfo and a CGImageAlphaInfo in
 * the converter's terms: 1, or 0 for float components, premultiplied or
 * alpha-only images and unknown values.
 */
int IMGDEC_BitmapLayout(uint32_t bitmap_info, uint32_t alpha_info,
		size_t *swap, imgdec_alpha_t *alpha);

/*
 * 1 when ImageIO's CGImageSourceStatus for a source and for its image
 * are both kCGImageStatusComplete, else 0.
 */
int IMGDEC_StatusComplete(int32_t source_status, int32_t image_status);
#endif

/*
 * Decodes a PNG, JPEG or TGA file whose name has the extension ext.
 * On IMGDEC_OK *pixels is malloc()ed straight-alpha RGBA8 the caller
 * free()s, and *width and *height are set. On failure *pixels is NULL
 * and *width and *height are unchanged.
 */
imgdec_result_t IMGDEC_Decode(const void *data, size_t size, const char *ext,
		unsigned char **pixels, int *width, int *height);

const char *IMGDEC_ResultString(imgdec_result_t result);

/*
 * Encodes width by height straight-alpha RGBA8 (rgba, width * 4 bytes a
 * row) as a PNG file: on Apple platforms through ImageIO's
 * CGImageDestination, the bytes as they are, tagged sRGB, with no
 * colour conversion and no premultiplication, so that IMGDEC_Decode
 * returns them unchanged. The size is bounded as IMGDEC_Decode bounds
 * it. On IMGDEC_OK *png is malloc()ed, *pnglen bytes the caller
 * free()s; on failure *png is NULL. Elsewhere IMGDEC_ERR_UNSUPPORTED.
 */
imgdec_result_t IMGDEC_EncodePNG(const unsigned char *rgba, size_t width,
		size_t height, unsigned char **png, size_t *pnglen);

#endif /* SRC_CLIENT_REFRESH_FILES_IMGDECODE_H_ */
