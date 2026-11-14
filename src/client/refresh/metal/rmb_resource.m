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
 * The resources of ref_metal's back end (docs/plans/ref-metal-native.md,
 * sections 3 to 5): the shader library, the 2D pipeline table and the
 * samplers, built when the context is made; the textures (rgba8Unorm,
 * private) and the staging ring every upload goes through. Uploads are
 * blitted on an upload command buffer that is committed before the
 * frame's command buffer (rmb_upload_flush, called by rmb_encode.m
 * before it commits one), so a frame reads what was written before its
 * draws. A texture rewritten after a draw of the open frame command
 * buffer used it splits that command buffer first (rmb_encode_split):
 * the draws before the write keep the old contents, as in gl1.
 *
 * =======================================================================
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <stdio.h>
#include <string.h>

#include "header/rmb.h"
#include "common/mtl_ring.h"

#if !__has_feature(objc_arc)
#error "rmb_resource.m is written for ARC: compile it with -fobjc-arc"
#endif

/* rmb_device.m */
extern id<MTLDevice> rmb_device;
extern id<MTLCommandQueue> rmb_queue;
extern rm_ledger_t rmb_ledger;
extern rmb_stats_t rmb_counts;
void rmb_watch(id<MTLCommandBuffer> cmd);
int rmb_commit_wait(id<MTLCommandBuffer> cmd);

/* rmb_encode.m */
extern unsigned long rmb_frame_serial;
void rmb_encode_split(void);

static id<MTLLibrary> library;
static id<MTLRenderPipelineState> pipes[RM_PIPE_2D_COUNT];
static id<MTLRenderPipelineState> pipes3d[RM_PIPE_3D_COUNT];
static id<MTLDepthStencilState> depth_states[RMB_DS_COUNT];
static id<MTLBuffer> buffers[RMB_MAX_BUFFERS];      /* the static (retained) buffers, private */
static id<MTLSamplerState> samplers[RMB_SMP_CLASSES * 2];
static int resources_open;

static id<MTLTexture> textures[RMB_MAX_TEXTURES];
static unsigned long tex_serial[RMB_MAX_TEXTURES];   /* rmb_frame_serial of its last draw, 0 none */

static id<MTLBuffer> staging;
static mtl_ring_t staging_ring;
static id<MTLCommandBuffer> upload_cmd;
static id<MTLBlitCommandEncoder> upload_blit;

static void
rmb_err(char *err, size_t errlen, NSString *what, NSError *e)
{
	snprintf(err, errlen, "%s%s%s", what.UTF8String, e ? ": " : "",
			e ? e.localizedDescription.UTF8String : "");
}

static id<MTLRenderPipelineState>
rmb_new_pipe_2d(rm_texenv_t env, int alpha_test, int blend, NSError **e)
{
	MTLFunctionConstantValues *fc = [MTLFunctionConstantValues new];
	MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
	int texenv = (int)env;
	bool test = alpha_test != 0;
	id<MTLFunction> vf, ff;

	[fc setConstantValue:&texenv type:MTLDataTypeInt atIndex:0];
	[fc setConstantValue:&test type:MTLDataTypeBool atIndex:1];
	vf = [library newFunctionWithName:@"rm_vertex_2d"];
	ff = [library newFunctionWithName:@"rm_fragment_2d" constantValues:fc error:e];

	if (!vf || !ff)
	{
		return nil;
	}

	d.vertexFunction = vf;
	d.fragmentFunction = ff;
	d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

	if (blend)
	{
		/* gl1's glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA), for colour and alpha */
		d.colorAttachments[0].blendingEnabled = YES;
		d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
		d.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
		d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
		d.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
	}

	return [rmb_device newRenderPipelineStateWithDescriptor:d error:e];
}

/*
 * The 3D pipelines (design section 4): world_tex and the blended color,
 * for the 3D pass's colour target and its depth32Float_stencil8 attachment.
 */
static id<MTLRenderPipelineState>
rmb_new_pipe_3d(rm_pipe3d_t which, NSError **e)
{
	MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
	id<MTLFunction> vf, ff;

	if (which == RM_PIPE_WORLD_TEX)
	{
		vf = [library newFunctionWithName:@"rm_vertex_world"];
		ff = [library newFunctionWithName:@"rm_fragment_world"];
	}
	else if (which == RM_PIPE_WORLD_LM)
	{
		vf = [library newFunctionWithName:@"rm_vertex_world"];
		ff = [library newFunctionWithName:@"rm_fragment_world_lm"];
	}
	else if ((which == RM_PIPE_WORLD_TEXC) || (which == RM_PIPE_WORLD_TEXC_BLEND))
	{
		vf = [library newFunctionWithName:@"rm_vertex_world"];
		ff = [library newFunctionWithName:@"rm_fragment_world_color"];
	}
	else if ((which == RM_PIPE_ALIAS) || (which == RM_PIPE_ALIAS_BLEND))
	{
		/* N7: the vertex colour times the skin, gl1's GL_MODULATE (rm_fragment_world_color, on the vertex's colour) */
		vf = [library newFunctionWithName:@"rm_vertex_alias"];
		ff = [library newFunctionWithName:@"rm_fragment_world_color"];
	}
	else if ((which == RM_PIPE_ALIAS_SHELL) || (which == RM_PIPE_ALIAS_SHELL_BLEND))
	{
		/* a shell: GL_TEXTURE_2D off, the vertex colour alone */
		vf = [library newFunctionWithName:@"rm_vertex_alias"];
		ff = [library newFunctionWithName:@"rm_fragment_color"];
	}
	else
	{
		vf = [library newFunctionWithName:@"rm_vertex_color"];
		ff = [library newFunctionWithName:@"rm_fragment_color"];
	}

	if (!vf || !ff)
	{
		return nil;
	}

	d.vertexFunction = vf;
	d.fragmentFunction = ff;
	d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
	d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
	d.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;

	if ((which == RM_PIPE_COLOR_BLEND) || (which == RM_PIPE_WORLD_TEXC_BLEND) || (which == RM_PIPE_ALIAS_BLEND) ||
		(which == RM_PIPE_ALIAS_SHELL_BLEND))
	{
		/* gl1's glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA), for colour and alpha */
		d.colorAttachments[0].blendingEnabled = YES;
		d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
		d.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
		d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
		d.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
	}

	return [rmb_device newRenderPipelineStateWithDescriptor:d error:e];
}

/* The depth-stencil states of the 3D pass: no test, and gl1's GL_LEQUAL with writes and without (N7). */
static int
rmb_new_depth_states(void)
{
	MTLDepthStencilDescriptor *d = [MTLDepthStencilDescriptor new];

	d.depthCompareFunction = MTLCompareFunctionAlways;
	d.depthWriteEnabled = NO;
	depth_states[RMB_DS_NONE] = [rmb_device newDepthStencilStateWithDescriptor:d];

	d = [MTLDepthStencilDescriptor new];
	d.depthCompareFunction = MTLCompareFunctionLessEqual;
	d.depthWriteEnabled = YES;
	depth_states[RMB_DS_LEQUAL_WRITE] = [rmb_device newDepthStencilStateWithDescriptor:d];

	d = [MTLDepthStencilDescriptor new];
	d.depthCompareFunction = MTLCompareFunctionLessEqual;
	d.depthWriteEnabled = NO;
	depth_states[RMB_DS_LEQUAL] = [rmb_device newDepthStencilStateWithDescriptor:d];

	return depth_states[RMB_DS_NONE] && depth_states[RMB_DS_LEQUAL_WRITE] && depth_states[RMB_DS_LEQUAL];
}

int
rmb_resources_open(const char *dir, char *err, size_t errlen)
{
	@autoreleasepool
	{
		NSError *e = nil;
		NSString *path = [@(dir) stringByAppendingPathComponent:@"ref_metal.metallib"];
		int env, a, b, i3;

		if (resources_open)
		{
			return 1;   /* a new window keeps the device's resources */
		}

		if (!rmb_device)
		{
			snprintf(err, errlen, "no Metal device");
			return 0;
		}

		library = [rmb_device newLibraryWithURL:[NSURL fileURLWithPath:path] error:&e];

		if (!library)
		{
			rmb_err(err, errlen, @"cannot load the shader library ref_metal.metallib next to ref_metal", e);
			return 0;
		}

		for (env = 0; env < RM_TEXENVS; env++)
		{
			for (a = 0; a < 2; a++)
			{
				for (b = 0; b < 2; b++)
				{
					int i = rm_pipe_2d((rm_texenv_t)env, a, b);

					pipes[i] = rmb_new_pipe_2d((rm_texenv_t)env, a, b, &e);

					if (!pipes[i])
					{
						rmb_err(err, errlen, @"cannot build a 2D pipeline state", e);
						rmb_resources_close();
						return 0;
					}

					rmb_counts.pipelines++;
				}
			}
		}

		for (i3 = 0; i3 < RM_PIPE_3D_COUNT; i3++)
		{
			pipes3d[i3] = rmb_new_pipe_3d((rm_pipe3d_t)i3, &e);

			if (!pipes3d[i3])
			{
				rmb_err(err, errlen, @"cannot build a 3D pipeline state", e);
				rmb_resources_close();
				return 0;
			}

			rmb_counts.pipelines++;
		}

		if (!rmb_new_depth_states())
		{
			snprintf(err, errlen, "cannot build the depth-stencil states");
			rmb_resources_close();
			return 0;
		}

		staging = [rmb_device newBufferWithLength:RMB_STAGING_BYTES options:MTLResourceStorageModeShared];

		if (!staging)
		{
			snprintf(err, errlen, "cannot allocate the %u byte staging ring", RMB_STAGING_BYTES);
			rmb_resources_close();
			return 0;
		}

		rm_ledger_create(&rmb_ledger, RM_OBJ_BUFFER);
		staging_ring.size = RMB_STAGING_BYTES;
		staging_ring.used = 0;
		resources_open = 1;
	}

	/* gl1's default gl_texturemode until R_TextureMode sets it */
	rmb_samplers_set(3, 1, 0);
	return 1;
}

/* Commits the uploads so far, ahead of any frame command buffer committed after it. */
void
rmb_upload_flush(void)
{
	if (upload_blit)
	{
		[upload_blit endEncoding];
		upload_blit = nil;
	}

	if (upload_cmd)
	{
		rmb_watch(upload_cmd);
		[upload_cmd commit];
		upload_cmd = nil;
	}
}

/*
 * Makes the whole staging ring free again: the pending uploads (or an
 * empty command buffer, which runs after every earlier one) are committed
 * and waited for, bounded. 0 when the GPU did not finish in time: the
 * ring is kept as it is, since a blit may still read it.
 */
static int
rmb_staging_recycle(void)
{
	id<MTLCommandBuffer> cmd;

	if (upload_blit)
	{
		[upload_blit endEncoding];
		upload_blit = nil;
	}

	cmd = upload_cmd ? upload_cmd : [rmb_queue commandBuffer];
	upload_cmd = nil;

	if (!cmd || !rmb_commit_wait(cmd))
	{
		return 0;
	}

	staging_ring.used = 0;
	return 1;
}

void
rmb_resources_close(void)
{
	int i;

	@autoreleasepool
	{
		if (upload_cmd || upload_blit)
		{
			rmb_staging_recycle();
		}

		for (i = 0; i < RMB_MAX_TEXTURES; i++)
		{
			rmb_texture_free(i);
		}

		for (i = 0; i < RMB_MAX_BUFFERS; i++)
		{
			rmb_buffer_free(i);
		}

		for (i = 0; i < RM_PIPE_2D_COUNT; i++)
		{
			pipes[i] = nil;
		}

		for (i = 0; i < RM_PIPE_3D_COUNT; i++)
		{
			pipes3d[i] = nil;
		}

		for (i = 0; i < RMB_DS_COUNT; i++)
		{
			depth_states[i] = nil;
		}

		for (i = 0; i < RMB_SMP_CLASSES * 2; i++)
		{
			samplers[i] = nil;
		}

		if (staging)
		{
			staging = nil;
			rm_ledger_destroy(&rmb_ledger, RM_OBJ_BUFFER);
		}

		library = nil;
		upload_cmd = nil;
		upload_blit = nil;
		resources_open = 0;
	}
}

void
rmb_samplers_set(int min, int mag_linear, int aniso)
{
	@autoreleasepool
	{
		MTLSamplerMinMagFilter mag = mag_linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
		int cls, clamp;

		if (!rmb_device)
		{
			return;
		}

		for (cls = 0; cls < RMB_SMP_CLASSES; cls++)
		{
			for (clamp = 0; clamp < 2; clamp++)
			{
				MTLSamplerDescriptor *d = [MTLSamplerDescriptor new];
				MTLSamplerAddressMode wrap = clamp ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;

				/* the shim's mgl_sampler: GL_NEAREST, GL_LINEAR, then the mipmap filters in enum order */
				if (cls == RMB_SMP_MIP)
				{
					d.minFilter = (min == 0 || min == 2 || min == 4) ? MTLSamplerMinMagFilterNearest :
						MTLSamplerMinMagFilterLinear;
					d.mipFilter = min < 2 ? MTLSamplerMipFilterNotMipmapped :
						(min < 4 ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear);
					d.magFilter = mag;
					d.maxAnisotropy = aniso > 1 ? (aniso > 16 ? 16 : aniso) : 1;
				}
				else if (cls == RMB_SMP_LIGHTMAP)
				{
					/* gl1's LM_UploadBlock: GL_LINEAR for both, whatever the mode */
					d.minFilter = d.magFilter = MTLSamplerMinMagFilterLinear;
					d.mipFilter = MTLSamplerMipFilterNotMipmapped;
				}
				else
				{
					/* gl1 gives textures without mipmaps gl_filter_max (or GL_NEAREST) for both */
					d.minFilter = d.magFilter = cls == RMB_SMP_FLAT ? mag : MTLSamplerMinMagFilterNearest;
					d.mipFilter = MTLSamplerMipFilterNotMipmapped;
				}

				d.sAddressMode = d.tAddressMode = wrap;
				samplers[RMB_SAMPLER(cls, clamp)] = [rmb_device newSamplerStateWithDescriptor:d];
			}
		}
	}
}

static int
rmb_take_id(void)
{
	int i;

	for (i = 1; i < RMB_MAX_TEXTURES; i++)
	{
		if (!textures[i])
		{
			return i;
		}
	}

	return 0;
}

int
rmb_texture_upload(int tid, const unsigned char *rgba, int w, int h, int mipmapped, int opaque)
{
	size_t bytes, i;
	long at;

	if (!resources_open || !rgba || (tid < 0) || (tid >= RMB_MAX_TEXTURES) ||
		!rm_upload_bytes(w, h, RMB_STAGING_BYTES, &bytes))
	{
		/* every 0 frees the id passed in (out of range ids are ignored): the caller forgets it */
		rmb_texture_free(tid);
		rmb_counts.upload_skips++;
		return 0;
	}

	@autoreleasepool
	{
		id<MTLTexture> t;
		unsigned char *dst;
		NSUInteger levels = 1;

		if (!tid && !(tid = rmb_take_id()))
		{
			rmb_counts.upload_skips++;
			return 0;
		}

		/* gl1 semantics: draws of this frame before the write saw the old contents */
		if (textures[tid] && (tex_serial[tid] == rmb_frame_serial))
		{
			rmb_encode_split();
		}

		if (mipmapped)
		{
			int m = w > h ? w : h;

			while (m > 1)
			{
				m >>= 1;
				levels++;
			}
		}

		t = textures[tid];

		if (!t || (int)t.width != w || (int)t.height != h || t.mipmapLevelCount != levels)
		{
			MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
					width:(NSUInteger)w height:(NSUInteger)h mipmapped:NO];

			d.mipmapLevelCount = levels;
			d.storageMode = MTLStorageModePrivate;
			d.usage = MTLTextureUsageShaderRead;
			rmb_texture_free(tid);
			t = [rmb_device newTextureWithDescriptor:d];

			if (!t)
			{
				rmb_counts.upload_skips++;
				return 0;
			}

			textures[tid] = t;
			rm_ledger_create(&rmb_ledger, RM_OBJ_TEXTURE);
		}

		at = mtl_ring_alloc(&staging_ring, bytes);

		if ((at < 0) && rmb_staging_recycle())
		{
			at = mtl_ring_alloc(&staging_ring, bytes);
		}

		if (at < 0)
		{
			/* the texture has no defined contents: free its id, the caller is given 0 */
			rmb_texture_free(tid);
			rmb_counts.upload_skips++;
			return 0;
		}

		dst = (unsigned char *)staging.contents + at;
		memcpy(dst, rgba, bytes);

		if (opaque)
		{
			/* gl1's solid internal format: the shim stores it with alpha 255 */
			for (i = 3; i < bytes; i += 4)
			{
				dst[i] = 255;
			}
		}

		if (!upload_cmd)
		{
			upload_cmd = [rmb_queue commandBuffer];
		}

		if (upload_cmd && !upload_blit)
		{
			upload_blit = [upload_cmd blitCommandEncoder];
		}

		if (!upload_blit)
		{
			rmb_texture_free(tid);
			rmb_counts.upload_skips++;
			return 0;
		}

		[upload_blit copyFromBuffer:staging
				sourceOffset:(NSUInteger)at
				sourceBytesPerRow:(NSUInteger)w * 4
				sourceBytesPerImage:bytes
				sourceSize:MTLSizeMake((NSUInteger)w, (NSUInteger)h, 1)
				toTexture:t
				destinationSlice:0
				destinationLevel:0
				destinationOrigin:MTLOriginMake(0, 0, 0)];

		if (levels > 1)
		{
			[upload_blit generateMipmapsForTexture:t];
		}
	}

	rmb_counts.uploads++;
	return tid;
}

/*
 * The lightmap array (design section 5): one texture2d_array of 128 by
 * 128 slices in the texture table, so a region written after a draw of
 * the open frame command buffer splits it as any rewritten texture does.
 */
int
rmb_texture_array(int slices)
{
	int tid;

	if (!resources_open || !rm_lm_slices_ok(slices))
	{
		rmb_counts.upload_skips++;
		return 0;
	}

	@autoreleasepool
	{
		MTLTextureDescriptor *d = [MTLTextureDescriptor new];
		id<MTLTexture> t;

		if (!(tid = rmb_take_id()))
		{
			rmb_counts.upload_skips++;
			return 0;
		}

		d.textureType = MTLTextureType2DArray;
		d.pixelFormat = MTLPixelFormatRGBA8Unorm;
		d.width = RM_LM_WIDTH;
		d.height = RM_LM_HEIGHT;
		d.arrayLength = (NSUInteger)slices;
		d.mipmapLevelCount = 1;
		d.storageMode = MTLStorageModePrivate;
		d.usage = MTLTextureUsageShaderRead;
		t = [rmb_device newTextureWithDescriptor:d];

		if (!t)
		{
			rmb_counts.upload_skips++;
			return 0;
		}

		textures[tid] = t;
		tex_serial[tid] = 0;
		rm_ledger_create(&rmb_ledger, RM_OBJ_TEXTURE);
	}

	return tid;
}

int
rmb_texture_region(int tid, int slice, int x, int y, int w, int h, const unsigned char *rgba, long stride)
{
	size_t bytes;
	long at;
	int row;

	if (!resources_open || !rgba || (tid <= 0) || (tid >= RMB_MAX_TEXTURES) || !textures[tid])
	{
		rmb_counts.upload_skips++;
		return 0;
	}

	@autoreleasepool
	{
		id<MTLTexture> t = textures[tid];
		long slices = (t.textureType == MTLTextureType2DArray) ? (long)t.arrayLength : 1;
		unsigned char *dst;

		/* inside the texture it writes, rows of the source at least as long as the region's */
		if (!rm_region_ok((long)t.width, (long)t.height, slices, slice, x, y, w, h) || (stride < (long)w * 4) ||
			!rm_upload_bytes(w, h, RMB_STAGING_BYTES, &bytes))
		{
			rmb_counts.upload_skips++;
			return 0;
		}

		/* gl1 semantics: draws of this frame before the write saw the old contents */
		if (tex_serial[tid] == rmb_frame_serial)
		{
			rmb_encode_split();
		}

		at = mtl_ring_alloc(&staging_ring, bytes);

		if ((at < 0) && rmb_staging_recycle())
		{
			at = mtl_ring_alloc(&staging_ring, bytes);
		}

		if (!upload_cmd)
		{
			upload_cmd = [rmb_queue commandBuffer];
		}

		if (upload_cmd && !upload_blit)
		{
			upload_blit = [upload_cmd blitCommandEncoder];
		}

		if ((at < 0) || !upload_blit)
		{
			/* the region keeps what it held; the texture and its id stay */
			rmb_counts.upload_skips++;
			return 0;
		}

		/* the region's rows, tightly packed */
		dst = (unsigned char *)staging.contents + at;

		for (row = 0; row < h; row++)
		{
			memcpy(dst + (size_t)row * (size_t)w * 4, rgba + (size_t)row * (size_t)stride, (size_t)w * 4);
		}

		[upload_blit copyFromBuffer:staging
				sourceOffset:(NSUInteger)at
				sourceBytesPerRow:(NSUInteger)w * 4
				sourceBytesPerImage:bytes
				sourceSize:MTLSizeMake((NSUInteger)w, (NSUInteger)h, 1)
				toTexture:t
				destinationSlice:(NSUInteger)slice
				destinationLevel:0
				destinationOrigin:MTLOriginMake((NSUInteger)x, (NSUInteger)y, 0)];
	}

	rmb_counts.uploads++;
	return 1;
}

void
rmb_texture_free(int tid)
{
	if ((tid > 0) && (tid < RMB_MAX_TEXTURES) && textures[tid])
	{
		/* a command buffer that still reads it keeps it alive */
		textures[tid] = nil;
		tex_serial[tid] = 0;
		rm_ledger_destroy(&rmb_ledger, RM_OBJ_TEXTURE);
	}
}

void
rmb_upload_refused(void)
{
	rmb_counts.upload_skips++;
}

/* For rmb_encode.m: the texture of a draw, marked as used by the open frame command buffer. */
id<MTLTexture>
rmb_texture_for_draw(int tid)
{
	if ((tid <= 0) || (tid >= RMB_MAX_TEXTURES) || !textures[tid])
	{
		return nil;
	}

	tex_serial[tid] = rmb_frame_serial;
	return textures[tid];
}

id<MTLRenderPipelineState>
rmb_pipe(int i)
{
	return (i >= 0 && i < RM_PIPE_2D_COUNT) ? pipes[i] : nil;
}

id<MTLSamplerState>
rmb_sampler(int i)
{
	return (i >= 0 && i < RMB_SMP_CLASSES * 2) ? samplers[i] : nil;
}

id<MTLRenderPipelineState>
rmb_pipe3d(int i)
{
	return (i >= 0 && i < RM_PIPE_3D_COUNT) ? pipes3d[i] : nil;
}

id<MTLDepthStencilState>
rmb_depth_state(int i)
{
	return (i >= 0 && i < RMB_DS_COUNT) ? depth_states[i] : nil;
}

/*
 * A static buffer: private storage, its bytes copied from a shared
 * buffer by a blit on the upload command buffer, which is committed
 * before any frame command buffer reads it (rmb_upload_flush). The shared
 * source is kept alive by the command buffer that reads it.
 */
int
rmb_buffer_static(const void *data, size_t bytes)
{
	int bid;

	if (!resources_open || !data || (bytes == 0) || (bytes > RMB_STATIC_MAX_BYTES))
	{
		rmb_counts.upload_skips++;
		return 0;
	}

	for (bid = 1; bid < RMB_MAX_BUFFERS && buffers[bid]; bid++)
	{
	}

	if (bid == RMB_MAX_BUFFERS)
	{
		rmb_counts.upload_skips++;
		return 0;
	}

	@autoreleasepool
	{
		id<MTLBuffer> src = [rmb_device newBufferWithBytes:data length:bytes options:MTLResourceStorageModeShared];
		id<MTLBuffer> dst = [rmb_device newBufferWithLength:bytes options:MTLResourceStorageModePrivate];

		if (!upload_cmd)
		{
			upload_cmd = [rmb_queue commandBuffer];
		}

		if (upload_cmd && !upload_blit)
		{
			upload_blit = [upload_cmd blitCommandEncoder];
		}

		if (!src || !dst || !upload_blit)
		{
			rmb_counts.upload_skips++;
			return 0;
		}

		[upload_blit copyFromBuffer:src sourceOffset:0 toBuffer:dst destinationOffset:0 size:bytes];
		buffers[bid] = dst;
		rm_ledger_create(&rmb_ledger, RM_OBJ_BUFFER);
	}

	rmb_counts.uploads++;
	return bid;
}

void
rmb_buffer_free(int bid)
{
	if ((bid > 0) && (bid < RMB_MAX_BUFFERS) && buffers[bid])
	{
		/* a command buffer that still reads it keeps it alive */
		buffers[bid] = nil;
		rm_ledger_destroy(&rmb_ledger, RM_OBJ_BUFFER);
	}
}

/* For rmb_encode.m: a static buffer, nil for an id without one. */
id<MTLBuffer>
rmb_static_buffer(int bid)
{
	return ((bid > 0) && (bid < RMB_MAX_BUFFERS)) ? buffers[bid] : nil;
}
