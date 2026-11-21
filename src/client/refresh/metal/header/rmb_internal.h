/*
 * ref_metal's backend files (rmb_*.m) call each other through this header and
 * nowhere else (FIX119, AR-18): a function one rmb_*.m file defines and another
 * calls is declared here once, the defining file includes it (so the compiler
 * checks the definition against it, -Wmissing-prototypes as an error), and no
 * .m file declares a function by hand (tests/check-ref-metal.sh). A copy of a
 * prototype drifts: under ARC an "id *" parameter is __autoreleasing unless
 * written otherwise, so a copy of rmb_frame_ring_take that dropped its __strong
 * would still compile and then mishandle ownership.
 *
 * The functions the C side calls (the rm_*.c files) are in rmb.h and the
 * rm_*.h headers; this header is Objective-C only.
 */

#ifndef RMB_INTERNAL_H
#define RMB_INTERNAL_H

#include "rmb.h"
#include "rm_rtfx.h"

#ifdef __OBJC__

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

/* ---- rmb_device.m: the device, the frame's present and the command buffer watch ---- */

dispatch_time_t rmb_deadline(long ms);
long rmb_frame_wait_ms(void);

/* the target of a present slot, and the frame's present (FIX51) */
id<MTLTexture> rmb_present_take(int w, int h);
int rmb_present_region(int region, int same_format, int hdr, int have_pso);
int rmb_present_written(id<MTLCommandBuffer> cmd, id<CAMetalDrawable> drawable, int region);
id<CAMetalDrawable> rmb_next_drawable(void);
id<MTLCommandBuffer> rmb_present_begin(id<MTLCommandBuffer> frame);
void rmb_present_commit(void);

void rmb_watch(id<MTLCommandBuffer> cmd);
int rmb_commit_wait(id<MTLCommandBuffer> cmd);

/* ---- rmb_resource.m: textures, pipelines, samplers, buffers and the library ---- */

void rmb_upload_flush(void);
id<MTLTexture> rmb_texture_for_draw(int tid);
id<MTLRenderPipelineState> rmb_pipe(int i);
id<MTLRenderPipelineState> rmb_alpha_pipe(void);
id<MTLRenderPipelineState> rmb_present_pipe(void);
id<MTLRenderPipelineState> rmb_retro_pipe(void);
id<MTLSamplerState> rmb_sampler(int i);
id<MTLRenderPipelineState> rmb_pipe3d(int i);
id<MTLDepthStencilState> rmb_depth_state(int i);
id<MTLBuffer> rmb_static_buffer(int bid);
id<MTLLibrary> rmb_library(void);

/* ---- rmb_encode.m: the frame's command buffers, passes and ring ---- */

void rmb_encode_abandon(void);
void rmb_encode_release(void);
void rmb_encode_split(void);
id<MTLRenderCommandEncoder> rmb_frame_open_pass(MTLRenderPassDescriptor *rp, int with_depth, int stencil, int keep);
void rmb_frame_close_pass(void);
id<MTLTexture> rmb_frame_color(void);
/* TASK185 (PM3): the Retro pass (rmb_retro_pass's) over the rectangle of a view's render-size target t, with a copy of its own */
int rmb_retro_pass_view(id<MTLTexture> t, int x, int y, int w, int h, int filter, int curve);
int rmb_frame_cut(void);
id<MTLCommandBuffer> rmb_frame_compute(unsigned long *frame);
/* __strong is part of the type: a caller's "__strong id<MTLBuffer> buf" passes &buf as is */
void *rmb_frame_ring_take(size_t n, __strong id<MTLBuffer> *buf, size_t *ofs);

/* ---- rmb_gbuf.m (TASK54, TASK162): the passes of a MOTION or GBUFFER view, their draws' pipelines and bindings ---- */

void rmb_gbuf_raster_formats(MTLRenderPipelineDescriptor *d, int surf);
int rmb_gbuf_reopen(void);
id<MTLRenderPipelineState> rmb_gbuf_pipe3d(int pipe);
void rmb_gbuf_bind_motion(id<MTLRenderCommandEncoder> enc, const float *mvp, const rm_viewport_t *vp);
int rmb_gbuf_bind_world(id<MTLRenderCommandEncoder> enc, int dynamic, unsigned nv);
int rmb_gbuf_bind_alias(id<MTLRenderCommandEncoder> enc, int pipe, const rm_avert_t *v, unsigned nv);
void rmb_gbuf_frame_reset(void);
void rmb_gbuf_release(void);

/*
 * TASK179: what rmb_gbuf.m hands the ray-traced effects through the
 * table's walks (header/rm_rtfx.h, which sees only the name). Each field
 * is set for the walks that read it; the rest are nil or 0.
 */
struct rmb_rtfx_io_s
{
	id<MTLRenderCommandEncoder> raster;   /* mark: the raster's encoder, before its pass ends */
	int surf;                             /* mark, trace: the raster has the surface attachment (TASK131) */
	id<MTLTexture> depth, normal, material, albedo;   /* the G-buffer's attachments */
	id<MTLTexture> surface;               /* trace: the surface attachment, nil without one */
	id<MTLTexture> lightmap;              /* the raster's light attachment */
	id<MTLTexture> light;                 /* the light the resolve reads: lightmap, or a trace's in its place */
	id<MTLTexture> target;                /* post: the view's colour target, the resolve's lit result in it */
	id<MTLRenderCommandEncoder> enc;      /* resolve, show: the resolve's pass; bind: the forward pass; post: open's */
	const void *resolve;                  /* resolve: the stock resolve's uniforms */
	size_t resolve_len;
	int (*open)(rmb_rtfx_io_t *io);       /* post: a pass over target into enc; 0 when none opened */
	id<MTLRenderPipelineState> pipe;      /* forward: the pipeline put there */
};

/* ---- rmb_rtwater.m (TASK162): the water's marks, the raster's last draw at r_rt_water_hits 1 ---- */

int rmb_rtw_mark(id<MTLRenderCommandEncoder> enc, int surf);

/* ---- rmb_rt.m (TASK158): the water raster's bindings ---- */

unsigned long rmb_rt_bind_water_raster(id<MTLRenderCommandEncoder> enc, int list_at, int idx_at, int vert_at,
		int geom_at);

#endif /* __OBJC__ */

#endif /* RMB_INTERNAL_H */
