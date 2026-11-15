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
 * PNG, JPEG and TGA decoding (decision 0010). On Apple platforms ImageIO
 * decodes, through its C API. The order of the checks:
 *
 *  1. the name's extension is png, jpg or tga;
 *  2. the file has 1 to IMGDEC_MAX_FILE bytes;
 *  3. the type ImageIO reads from the bytes is PNG, JPEG or TGA and is
 *     the extension's (a GIF, or a PNG named .tga, is refused);
 *  4. the declared size is 1 to IMGDEC_MAX_SIDE on each side and its
 *     RGBA8 bytes do not overflow, before anything is decoded or
 *     allocated; the decoded image must have that size;
 *  5. the decoded pixels are read in the image's own layout and
 *     converted to straight-alpha RGBA8 with no colour conversion, as
 *     stb_image returned them. Nothing is drawn into a bitmap context,
 *     which would premultiply texels with partial alpha; a premultiplied
 *     or other unconverted layout is refused.
 *
 * Elsewhere there is no decoder: after checks 1 and 2 every file is
 * refused with IMGDEC_ERR_UNSUPPORTED. Host-tested by
 * tests/image/imgdecode_test.c (tests/check-image-decoder.sh).
 *
 * =======================================================================
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "imgdecode.h"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

/* ASCII only, whatever the locale */
static int
IMGDEC_SameName(const char *a, const char *b)
{
	for (;;)
	{
		int ca = (unsigned char)*a++;
		int cb = (unsigned char)*b++;

		if (ca >= 'A' && ca <= 'Z')
		{
			ca += 'a' - 'A';
		}

		if (ca != cb)
		{
			return 0;
		}

		if (ca == 0)
		{
			return 1;
		}
	}
}

imgdec_type_t
IMGDEC_TypeOfExtension(const char *ext)
{
	if (!ext)
	{
		return IMGDEC_TYPE_NONE;
	}

	if (IMGDEC_SameName(ext, "png"))
	{
		return IMGDEC_TYPE_PNG;
	}

	if (IMGDEC_SameName(ext, "jpg"))
	{
		return IMGDEC_TYPE_JPEG;
	}

	if (IMGDEC_SameName(ext, "tga"))
	{
		return IMGDEC_TYPE_TGA;
	}

	return IMGDEC_TYPE_NONE;
}

imgdec_type_t
IMGDEC_TypeOfIdentifier(const char *uti)
{
	if (!uti)
	{
		return IMGDEC_TYPE_NONE;
	}

	if (!strcmp(uti, "public.png"))
	{
		return IMGDEC_TYPE_PNG;
	}

	if (!strcmp(uti, "public.jpeg"))
	{
		return IMGDEC_TYPE_JPEG;
	}

	if (!strcmp(uti, "com.truevision.tga-image"))
	{
		return IMGDEC_TYPE_TGA;
	}

	return IMGDEC_TYPE_NONE;
}

int
IMGDEC_FileSizeOk(size_t size, size_t max_size)
{
	return size >= 1 && size <= max_size;
}

int
IMGDEC_PixelBytes(size_t width, size_t height, size_t max_side, size_t *bytes)
{
	if (width == 0 || height == 0 || width > max_side || height > max_side)
	{
		return 0;
	}

	if (width > SIZE_MAX / 4 / height)
	{
		return 0;
	}

	*bytes = width * height * 4;
	return 1;
}

/* byte i of a pixel, counted in big-endian channel order */
static unsigned char
IMGDEC_PixelByte(const unsigned char *pixel, size_t i, size_t swap)
{
	if (swap)
	{
		i = i - i % swap + (swap - 1 - i % swap);
	}

	return pixel[i];
}

imgdec_result_t
IMGDEC_ToRGBA(const imgdec_layout_t *l, const unsigned char *src, size_t srclen,
	unsigned char *dst)
{
	size_t comp, channels, pixel, row, color, alpha, x, y, bytes;
	int has_alpha;

	if (!IMGDEC_PixelBytes(l->width, l->height, IMGDEC_MAX_SIDE, &bytes))
	{
		return IMGDEC_ERR_SIZE;
	}

	if (l->bits_per_component != 8 && l->bits_per_component != 16)
	{
		return IMGDEC_ERR_LAYOUT;
	}

	if (l->colors != 1 && l->colors != 3)
	{
		return IMGDEC_ERR_LAYOUT;
	}

	switch (l->alpha)
	{
		case IMGDEC_ALPHA_NONE:
			channels = l->colors;
			color = 0;
			alpha = 0;
			break;
		case IMGDEC_ALPHA_LAST:
		case IMGDEC_ALPHA_SKIP_LAST:
			channels = l->colors + 1;
			color = 0;
			alpha = l->colors;
			break;
		case IMGDEC_ALPHA_FIRST:
		case IMGDEC_ALPHA_SKIP_FIRST:
			channels = l->colors + 1;
			color = 1;
			alpha = 0;
			break;
		default:
			return IMGDEC_ERR_LAYOUT;
	}
	has_alpha = (l->alpha == IMGDEC_ALPHA_LAST || l->alpha == IMGDEC_ALPHA_FIRST);

	comp = l->bits_per_component / 8;
	if (l->bits_per_pixel != channels * l->bits_per_component)
	{
		return IMGDEC_ERR_LAYOUT;
	}
	pixel = channels * comp;

	if (l->swap != 0 && ((l->swap != 2 && l->swap != 4) || pixel % l->swap != 0))
	{
		return IMGDEC_ERR_LAYOUT;
	}

	/* an index is one byte and names an RGB triple; no alpha with it */
	if (l->palette && (l->colors != 1 || comp != 1 || l->alpha != IMGDEC_ALPHA_NONE ||
		l->palette_count > 256))
	{
		return IMGDEC_ERR_LAYOUT;
	}

	/* every row holds its pixels; the last needs no padding after them */
	row = l->width * pixel;
	if (l->bytes_per_row < row)
	{
		return IMGDEC_ERR_LAYOUT;
	}

	if (srclen < row || (l->height - 1) > (srclen - row) / l->bytes_per_row)
	{
		return IMGDEC_ERR_DECODE;
	}

	for (y = 0; y < l->height; y++)
	{
		const unsigned char *s = src + y * l->bytes_per_row;

		for (x = 0; x < l->width; x++, s += pixel, dst += 4)
		{
			if (l->palette)
			{
				size_t index = s[0];

				if (index >= l->palette_count)
				{
					return IMGDEC_ERR_DECODE;
				}

				dst[0] = l->palette[index * 3 + 0];
				dst[1] = l->palette[index * 3 + 1];
				dst[2] = l->palette[index * 3 + 2];
				dst[3] = 255;
				continue;
			}

			dst[0] = IMGDEC_PixelByte(s, color * comp, l->swap);
			if (l->colors == 3)
			{
				dst[1] = IMGDEC_PixelByte(s, (color + 1) * comp, l->swap);
				dst[2] = IMGDEC_PixelByte(s, (color + 2) * comp, l->swap);
			}
			else
			{
				dst[1] = dst[0];
				dst[2] = dst[0];
			}
			dst[3] = has_alpha ? IMGDEC_PixelByte(s, alpha * comp, l->swap) : 255;
		}
	}

	return IMGDEC_OK;
}

#ifdef __APPLE__

int
IMGDEC_BitmapLayout(uint32_t bitmap_info, uint32_t alpha_info, size_t *swap,
	imgdec_alpha_t *alpha)
{
	if (bitmap_info & kCGBitmapFloatComponents)
	{
		return 0;
	}

	switch (bitmap_info & kCGBitmapByteOrderMask)
	{
		case kCGBitmapByteOrderDefault:
		case kCGBitmapByteOrder16Big:
		case kCGBitmapByteOrder32Big:
			*swap = 0;
			break;
		case kCGBitmapByteOrder16Little:
			*swap = 2;
			break;
		case kCGBitmapByteOrder32Little:
			*swap = 4;
			break;
		default:
			return 0;
	}

	/* premultiplied and alpha-only images are not converted */
	switch (alpha_info)
	{
		case kCGImageAlphaNone:
			*alpha = IMGDEC_ALPHA_NONE;
			break;
		case kCGImageAlphaLast:
			*alpha = IMGDEC_ALPHA_LAST;
			break;
		case kCGImageAlphaFirst:
			*alpha = IMGDEC_ALPHA_FIRST;
			break;
		case kCGImageAlphaNoneSkipLast:
			*alpha = IMGDEC_ALPHA_SKIP_LAST;
			break;
		case kCGImageAlphaNoneSkipFirst:
			*alpha = IMGDEC_ALPHA_SKIP_FIRST;
			break;
		default:
			return 0;
	}

	return 1;
}

static CFStringRef
IMGDEC_Identifier(imgdec_type_t type)
{
	switch (type)
	{
		case IMGDEC_TYPE_PNG:
			return CFSTR("public.png");
		case IMGDEC_TYPE_JPEG:
			return CFSTR("public.jpeg");
		default:
			return CFSTR("com.truevision.tga-image");
	}
}

/* a positive integer property, no larger than SIZE_MAX */
static int
IMGDEC_GetSide(CFDictionaryRef props, CFStringRef key, size_t *side)
{
	CFTypeRef value = CFDictionaryGetValue(props, key);
	int64_t v;

	if (!value || CFGetTypeID(value) != CFNumberGetTypeID() ||
		!CFNumberGetValue((CFNumberRef)value, kCFNumberSInt64Type, &v) ||
		v < 1 || (uint64_t)v > SIZE_MAX)
	{
		return 0;
	}

	*side = (size_t)v;
	return 1;
}

/*
 * The image's own layout, in the converter's terms. palette has room
 * for 256 RGB triples.
 */
static imgdec_result_t
IMGDEC_LayoutOf(CGImageRef image, imgdec_layout_t *l, unsigned char *palette)
{
	CGBitmapInfo info = CGImageGetBitmapInfo(image);
	CGColorSpaceRef space = CGImageGetColorSpace(image);

	memset(l, 0, sizeof(*l));
	l->width = CGImageGetWidth(image);
	l->height = CGImageGetHeight(image);
	l->bits_per_component = CGImageGetBitsPerComponent(image);
	l->bits_per_pixel = CGImageGetBitsPerPixel(image);
	l->bytes_per_row = CGImageGetBytesPerRow(image);

	if (!space || !IMGDEC_BitmapLayout(info, CGImageGetAlphaInfo(image), &l->swap, &l->alpha))
	{
		return IMGDEC_ERR_LAYOUT;
	}

	switch (CGColorSpaceGetModel(space))
	{
		case kCGColorSpaceModelMonochrome:
			l->colors = 1;
			break;
		case kCGColorSpaceModelRGB:
			l->colors = 3;
			break;
		case kCGColorSpaceModelIndexed:
			{
				CGColorSpaceRef base = CGColorSpaceGetBaseColorSpace(space);
				size_t count = CGColorSpaceGetColorTableCount(space);

				/* the table is count entries of the base's components */
				if (!base || CGColorSpaceGetModel(base) != kCGColorSpaceModelRGB ||
					CGColorSpaceGetNumberOfComponents(base) != 3 ||
					count < 1 || count > 256)
				{
					return IMGDEC_ERR_LAYOUT;
				}

				CGColorSpaceGetColorTable(space, palette);
				l->colors = 1;
				l->palette = palette;
				l->palette_count = count;
			}
			break;
		default:
			return IMGDEC_ERR_LAYOUT;
	}

	if (CGColorSpaceGetNumberOfComponents(space) != (size_t)(l->palette ? 1 : l->colors))
	{
		return IMGDEC_ERR_LAYOUT;
	}

	return IMGDEC_OK;
}

static imgdec_result_t
IMGDEC_DecodeImageIO(const unsigned char *data, size_t size, imgdec_type_t want,
	unsigned char **pixels, int *width, int *height)
{
	const void *keys[2];
	const void *values[2];
	CFDataRef bytes = NULL;
	CFDictionaryRef options = NULL;
	CGImageSourceRef source = NULL;
	CFDictionaryRef props = NULL;
	CGImageRef image = NULL;
	CFDataRef decoded = NULL;
	CFStringRef uti;
	unsigned char *out = NULL;
	unsigned char palette[256 * 3];
	char name[64];
	imgdec_layout_t layout;
	imgdec_type_t got;
	imgdec_result_t r;
	size_t w = 0, h = 0, outbytes = 0;

	/* a copy: nothing ImageIO keeps can point into the caller's buffer */
	bytes = CFDataCreate(kCFAllocatorDefault, data, (CFIndex)size);
	if (!bytes)
	{
		r = IMGDEC_ERR_MEMORY;
		goto done;
	}

	/* the hint helps TGA, which has no signature; it overrides no type */
	keys[0] = kCGImageSourceShouldCache;
	values[0] = kCFBooleanFalse;
	keys[1] = kCGImageSourceTypeIdentifierHint;
	values[1] = IMGDEC_Identifier(want);
	options = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (!options)
	{
		r = IMGDEC_ERR_MEMORY;
		goto done;
	}

	source = CGImageSourceCreateWithData(bytes, options);
	if (!source)
	{
		r = IMGDEC_ERR_TYPE;
		goto done;
	}

	/* the type, before any header is parsed for its size */
	uti = CGImageSourceGetType(source);
	if (!uti || !CFStringGetCString(uti, name, sizeof(name), kCFStringEncodingASCII))
	{
		r = IMGDEC_ERR_TYPE;
		goto done;
	}

	got = IMGDEC_TypeOfIdentifier(name);
	if (got == IMGDEC_TYPE_NONE)
	{
		r = IMGDEC_ERR_TYPE;
		goto done;
	}

	if (got != want)
	{
		r = IMGDEC_ERR_MISMATCH;
		goto done;
	}

	if (CGImageSourceGetCount(source) < 1)
	{
		r = IMGDEC_ERR_DECODE;
		goto done;
	}

	/* the declared size, before decoding */
	props = CGImageSourceCopyPropertiesAtIndex(source, 0, NULL);
	if (!props || !IMGDEC_GetSide(props, kCGImagePropertyPixelWidth, &w) ||
		!IMGDEC_GetSide(props, kCGImagePropertyPixelHeight, &h))
	{
		r = IMGDEC_ERR_DECODE;
		goto done;
	}

	if (!IMGDEC_PixelBytes(w, h, IMGDEC_MAX_SIDE, &outbytes))
	{
		r = IMGDEC_ERR_SIZE;
		goto done;
	}

	image = CGImageSourceCreateImageAtIndex(source, 0, options);
	if (!image)
	{
		r = IMGDEC_ERR_DECODE;
		goto done;
	}

	if (CGImageGetWidth(image) != w || CGImageGetHeight(image) != h)
	{
		r = IMGDEC_ERR_SIZE;
		goto done;
	}

	r = IMGDEC_LayoutOf(image, &layout, palette);
	if (r != IMGDEC_OK)
	{
		goto done;
	}

	/* the pixels as decoded; NULL when the file is cut short */
	if (CGImageGetDataProvider(image))
	{
		decoded = CGDataProviderCopyData(CGImageGetDataProvider(image));
	}

	if (!decoded || CFDataGetLength(decoded) < 0)
	{
		r = IMGDEC_ERR_DECODE;
		goto done;
	}

	out = malloc(outbytes);
	if (!out)
	{
		r = IMGDEC_ERR_MEMORY;
		goto done;
	}

	r = IMGDEC_ToRGBA(&layout, CFDataGetBytePtr(decoded),
		(size_t)CFDataGetLength(decoded), out);
	if (r != IMGDEC_OK)
	{
		goto done;
	}

	*pixels = out;
	out = NULL;
	*width = (int)w;
	*height = (int)h;

done:
	free(out);

	if (decoded)
	{
		CFRelease(decoded);
	}

	if (image)
	{
		CGImageRelease(image);
	}

	if (props)
	{
		CFRelease(props);
	}

	if (source)
	{
		CFRelease(source);
	}

	if (options)
	{
		CFRelease(options);
	}

	if (bytes)
	{
		CFRelease(bytes);
	}

	return r;
}

#endif /* __APPLE__ */

imgdec_result_t
IMGDEC_Decode(const void *data, size_t size, const char *ext,
	unsigned char **pixels, int *width, int *height)
{
	imgdec_type_t want;

	*pixels = NULL;

	want = IMGDEC_TypeOfExtension(ext);
	if (want == IMGDEC_TYPE_NONE)
	{
		return IMGDEC_ERR_EXTENSION;
	}

	if (!data || !IMGDEC_FileSizeOk(size, IMGDEC_MAX_FILE))
	{
		return IMGDEC_ERR_FILESIZE;
	}

#ifdef __APPLE__
	return IMGDEC_DecodeImageIO(data, size, want, pixels, width, height);
#else
	(void)width;
	(void)height;
	return IMGDEC_ERR_UNSUPPORTED;
#endif
}

const char *
IMGDEC_ResultString(imgdec_result_t result)
{
	switch (result)
	{
		case IMGDEC_OK:
			return "decoded";
		case IMGDEC_ERR_EXTENSION:
			return "not a .png, .jpg or .tga name";
		case IMGDEC_ERR_FILESIZE:
			return "empty, or larger than the file limit";
		case IMGDEC_ERR_TYPE:
			return "not a PNG, JPEG or TGA image";
		case IMGDEC_ERR_MISMATCH:
			return "the image type does not match the extension";
		case IMGDEC_ERR_SIZE:
			return "a side is 0 or larger than 16384";
		case IMGDEC_ERR_LAYOUT:
			return "a pixel layout that is not converted";
		case IMGDEC_ERR_DECODE:
			return "the pixels could not be decoded";
		case IMGDEC_ERR_MEMORY:
			return "out of memory";
		case IMGDEC_ERR_UNSUPPORTED:
			return "no PNG, JPEG or TGA decoder on this platform";
	}

	return "unknown result";
}
