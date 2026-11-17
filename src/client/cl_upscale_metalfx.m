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
 * The metalfx backend of upscale_textures (TASK124, decision 0012):
 * MetalFX's spatial scaler (MTLFXSpatialScaler) on a Metal device of
 * the client's own, not the renderer's, so it works with any renderer
 * loaded and the renderer API is not touched. macOS only; MetalFX is
 * weak-linked, so where the framework is missing its classes are nil
 * and the backend reads as unsupported.
 *
 * Objects are made only between CL_UpscaleMetalFXBegin and
 * CL_UpscaleMetalFXEnd, which bracket one `upscale_textures <2|4>
 * metalfx` run. CL_UpscaleMetalFXAvailable, which the menu asks, makes
 * none.
 *
 * Each tile: its RGBA8 copied into a shared texture, scaled into a
 * private one, blitted to a shared buffer and copied out, waiting for
 * the GPU each time (a run is a one-off, not a frame). The padding, the
 * crop and the alpha are cl_upscale.c's (CL_UpscaleRGBA), where the
 * unit tests run them.
 *
 * =======================================================================
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>

#include <stdio.h>
#include <string.h>

#include "header/upscale_metalfx.h"

/* the run's objects: nil outside a run */
static id<MTLDevice> mfx_device;
static id<MTLCommandQueue> mfx_queue;
/* the scaler of the last tile's sizes, kept for the next of the same */
static id<MTLFXSpatialScaler> mfx_scaler;
static int mfx_iw, mfx_ih, mfx_ow, mfx_oh;
/* why a run of this process found the backend unsupported; "" if none did */
static char mfx_unsupported[128];

#ifdef UPSCALE_METALFX_TEST
/* the host test's switch: Begin says unsupported, with this reason */
const char *upscale_metalfx_test_unsupported;
#endif

static void
CL_UpscaleMetalFXSay(char *reason, size_t size, const char *text)
{
	if (reason && (size > 0))
	{
		snprintf(reason, size, "%s", text);
	}
}

/* 1 when MetalFX's spatial scaler class is there: no object is made */
static int
CL_UpscaleMetalFXClass(void)
{
	if (@available(macOS 13.0, *))
	{
		/* weak-linked: nil when the framework is missing */
		return [MTLFXSpatialScalerDescriptor class] != nil;
	}

	return 0;
}

int
CL_UpscaleMetalFXAvailable(char *reason, size_t size)
{
	CL_UpscaleMetalFXSay(reason, size, "");

	if (!CL_UpscaleMetalFXClass())
	{
		CL_UpscaleMetalFXSay(reason, size, "this macOS has no MetalFX spatial scaler");
		return 0;
	}

	if (mfx_unsupported[0])
	{
		CL_UpscaleMetalFXSay(reason, size, mfx_unsupported);
		return 0;
	}

	return 1;
}

void
CL_UpscaleMetalFXEnd(void)
{
	mfx_scaler = nil;
	mfx_queue = nil;
	mfx_device = nil;
	mfx_iw = mfx_ih = mfx_ow = mfx_oh = 0;
}

/* a run is unsupported: the reason is kept for the menu, nothing else */
static int
CL_UpscaleMetalFXRefuse(char *reason, size_t size, const char *text)
{
	CL_UpscaleMetalFXEnd();
	snprintf(mfx_unsupported, sizeof(mfx_unsupported), "%s", text);
	CL_UpscaleMetalFXSay(reason, size, text);

	return 0;
}

int
CL_UpscaleMetalFXBegin(char *reason, size_t size)
{
	CL_UpscaleMetalFXEnd();
	CL_UpscaleMetalFXSay(reason, size, "");

#ifdef UPSCALE_METALFX_TEST
	if (upscale_metalfx_test_unsupported)
	{
		return CL_UpscaleMetalFXRefuse(reason, size,
			upscale_metalfx_test_unsupported);
	}
#endif

	if (!CL_UpscaleMetalFXClass())
	{
		return CL_UpscaleMetalFXRefuse(reason, size,
			"this macOS has no MetalFX spatial scaler");
	}

	@autoreleasepool
	{
		mfx_device = MTLCreateSystemDefaultDevice();

		if (!mfx_device)
		{
			return CL_UpscaleMetalFXRefuse(reason, size, "no Metal device");
		}

		if (@available(macOS 13.0, *))
		{
			if (![MTLFXSpatialScalerDescriptor supportsDevice:mfx_device])
			{
				return CL_UpscaleMetalFXRefuse(reason, size,
					"the Metal device has no MetalFX spatial scaler");
			}
		}

		mfx_queue = [mfx_device newCommandQueue];

		if (!mfx_queue)
		{
			return CL_UpscaleMetalFXRefuse(reason, size,
				"no Metal command queue");
		}
	}

	mfx_unsupported[0] = '\0';

	return 1;
}

/* the scaler for these sizes: the last one when they are the same */
static id<MTLFXSpatialScaler> CL_UpscaleMetalFXScaler(int iw, int ih, int ow,
		int oh) API_AVAILABLE(macos(13.0));

static id<MTLFXSpatialScaler>
CL_UpscaleMetalFXScaler(int iw, int ih, int ow, int oh)
{
	MTLFXSpatialScalerDescriptor *d;

	if (mfx_scaler && (iw == mfx_iw) && (ih == mfx_ih) && (ow == mfx_ow) &&
		(oh == mfx_oh))
	{
		return mfx_scaler;
	}

	mfx_scaler = nil;

	d = [MTLFXSpatialScalerDescriptor new];
	d.inputWidth = (NSUInteger)iw;
	d.inputHeight = (NSUInteger)ih;
	d.outputWidth = (NSUInteger)ow;
	d.outputHeight = (NSUInteger)oh;
	d.colorTextureFormat = MTLPixelFormatRGBA8Unorm;
	d.outputTextureFormat = MTLPixelFormatRGBA8Unorm;
	/* the palette's colours are sRGB-encoded values, as a frame's are */
	d.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
	mfx_scaler = [d newSpatialScalerWithDevice:mfx_device];

	if (mfx_scaler)
	{
		mfx_iw = iw;
		mfx_ih = ih;
		mfx_ow = ow;
		mfx_oh = oh;
	}

	return mfx_scaler;
}

int
CL_UpscaleMetalFXScale(void *ctx, const unsigned char *in, int iw, int ih,
		unsigned char *out, int ow, int oh)
{
	int ok = 0;

	(void)ctx;

	if (!mfx_device || !mfx_queue || !in || !out || (iw < 1) || (ih < 1) ||
		(ow < 1) || (oh < 1) || (ow > 16384) || (oh > 16384) ||
		(iw > ow) || (ih > oh))
	{
		return 0;
	}

	if (@available(macOS 13.0, *))
	{
		@autoreleasepool
		{
			const size_t outlen = (size_t)ow * oh * 4;
			id<MTLFXSpatialScaler> s = CL_UpscaleMetalFXScaler(iw, ih, ow, oh);
			MTLTextureDescriptor *td;
			id<MTLTexture> ct, ot;
			id<MTLBuffer> rb;
			id<MTLCommandBuffer> cb;
			id<MTLBlitCommandEncoder> blit;

			if (!s)
			{
				return 0;
			}

			td = [MTLTextureDescriptor
				texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
				width:(NSUInteger)iw height:(NSUInteger)ih mipmapped:NO];
			td.usage = s.colorTextureUsage | MTLTextureUsageShaderRead;
			td.storageMode = MTLStorageModeShared;
			ct = [mfx_device newTextureWithDescriptor:td];

			td = [MTLTextureDescriptor
				texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
				width:(NSUInteger)ow height:(NSUInteger)oh mipmapped:NO];
			td.usage = s.outputTextureUsage | MTLTextureUsageShaderRead;
			td.storageMode = MTLStorageModePrivate;
			ot = [mfx_device newTextureWithDescriptor:td];

			rb = [mfx_device newBufferWithLength:outlen
				options:MTLResourceStorageModeShared];
			cb = [mfx_queue commandBuffer];

			if (!ct || !ot || !rb || !cb)
			{
				return 0;
			}

			[ct replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)iw, (NSUInteger)ih)
				mipmapLevel:0 withBytes:in bytesPerRow:(NSUInteger)iw * 4];

			s.colorTexture = ct;
			s.outputTexture = ot;
			s.inputContentWidth = (NSUInteger)iw;
			s.inputContentHeight = (NSUInteger)ih;
			[s encodeToCommandBuffer:cb];

			blit = [cb blitCommandEncoder];
			[blit copyFromTexture:ot sourceSlice:0 sourceLevel:0
				sourceOrigin:MTLOriginMake(0, 0, 0)
				sourceSize:MTLSizeMake((NSUInteger)ow, (NSUInteger)oh, 1)
				toBuffer:rb destinationOffset:0
				destinationBytesPerRow:(NSUInteger)ow * 4
				destinationBytesPerImage:outlen];
			[blit endEncoding];
			[cb commit];
			[cb waitUntilCompleted];

			if (cb.status == MTLCommandBufferStatusCompleted)
			{
				memcpy(out, rb.contents, outlen);
				ok = 1;
			}
		}
	}

	return ok;
}
