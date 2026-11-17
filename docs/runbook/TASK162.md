# Water reflections mark their pixels in the G-buffer raster (TASK162, RT6)

TASK162 follows [TASK158](TASK158.md) (`r_rt_water_hits 1`, the world-space path, whose *Rework*
measured the separate water raster's wait) and [FIX81](FIX81.md) (the pass-over loop at both
settings). Design anchor: [section 2](../plans/metal-raytracing-design.md#2-the-three-effects-as-compute-passes)
(RT6). It uses TASK58's masked comparison (*The mask* in [its file](TASK58.md)) and the shared
runbook's method for the GPU time
([the shadows' proofs](../plans/metal-port-harness.md#ray-traced-shadows-from-dynamic-lights-in-ref_metal-task55-rt3)).

## The design

**Where the mark goes.** In the alpha of the G-buffer's normal attachment (`RGB10A2Unorm`, two
bits). The raster writes it 1 where the pixel has a normal and 0 where it has none, and every reader
(the shadows, the AO and its upsampling, the water's trace) tests only `a > 0.5`. At `r_rt_water_hits
1` the G-buffer raster ends with one more draw, before the pass closes: the map's water triangles
(the ids, the vertex function and the matrix the separate raster used), depth-tested less or equal
against the raster's own depth and not writing it, writing only that alpha by a fixed blend,
`a' = 1/3 + a/3` (source factor one, destination factor the source's alpha, 1/3). So a pixel with a
normal goes from 1 to 2/3 and one without from 0 to 1/3; a second water layer over the same pixel
keeps its level (4/9 stores as 1/3, 5/9 as 2/3). A pixel is marked when its stored level is 1 or 2;
it has a normal when its alpha is above 0.5, as before, for all four levels.

- *Not the material id.* An integer attachment does not blend, so the mark would replace the id
  that the resolve and every effect read for the surface behind translucent water, or need the
  attachment read back in the fragment (programmable blending: Apple GPUs only, while an AMD Mac
  traces too) and a mask at every reader, most of them outside this card.
- *Not a new attachment.* Every raster pipeline would need a variant with its format, as TASK131's
  surface attachment has, and the pass would store one more target a frame.

**What the world-space path reads instead.** The compaction reads the normal attachment's marks
(itself and its eight neighbours, `mtl_rtw_marked`) beside the material (opaque water), so its list
is the one the separate raster gave (`mtl_rtw_listed` unchanged). It also writes 0 to the reflection
target at every pixel it does not list, which is what the separate raster's clear left there; the
trace writes every listed pixel, as before. The reflection target is no longer a render target. No
render pass of its own, no reload of the depth, no cleared target, so no wait for the frame's earlier
passes. The compaction, the arguments, the indirect trace and FIX81's pass-over loop are unchanged.

**Why setting 0 is unchanged.** The draw is made only when the view's outputs were made for the
world-space path (`rmb_rtw_prepare(w, h, 1)`, which `RM_RTW_View` asks for only when `rm_rtw_path`
gives 1). At `0` the raster draws what main draws, the normal attachment's alpha is main's, and
TASK58's kernel `rm_rtw_trace` runs over every pixel as on main. The mark's pipelines are the
world-space path's resources, built in `rmb_rtw_open_ws` with the others, which setting 0 needs none
of.
