# A hidden door, plat or lift leaves no hole in the water reflection at `r_rt_water_hits 0` (FIX92)

FIX92 follows [FIX81](FIX81.md) (the pass-over loop for hidden alias models and its bound of
`MTL_RTW_LAYERS + 1` casts) and [TASK158](TASK158.md) (`r_rt_water_hits 1`), and is stacked on
[TASK162](TASK162.md) (the water marks in the G-buffer raster). It uses TASK58's masked comparison
(*The mask* in [its file](TASK58.md)).

## The finding

An inline brush model (a door, a plat, a lift) is a map face. At `r_rt_water_hits 0` a reflection ray
whose first hit is such a face, where the view does not show it, got no reflection
(`mtl_rtw_choose`), and `mtl_rtw_ss_next` stopped the ray there, since it passes alias models only.
So a door opening or a lift moving behind a wall left its outline as a stock-colour hole in the
reflection of a wall the view shows. The host test pinned it ("a hidden door is a map face") and
`doc/040_cvarlist.md` stated it.

## The design

**How a face is told apart as the world's or an inline model's.** Not by a mask bit: a Metal
intersection result gives the instance's index, the primitive and the distance, not the instance's
mask, so a mask bit only chooses what a query tests and cannot say what a query met. The per-instance
words that `rmb_rt_frame` already writes do tell: the first word of each instance
(`inst[2 * instance_id]`) is its first triangle of the world's index buffer, `~0` for an alias model
and `map_first[model]` for the world (model 0) and for each inline model. `rmb_rt_bind_geometry`
now gives the world's word in the fourth word of the geometry constants (`rm_rtw_geom.world`, which
was the padding `0`; `~0` when the world has no structure), and `mtl_rtw_brush(b, world)` says a
face belongs to an inline brush model when its instance's word is neither `~0` nor the world's. The
submodels' triangle ranges do not overlap, so the world's word is its own; a malformed map whose
inline model starts at the world's first triangle has that model taken as the world, whose faces
stop the ray as before, so the fault can only keep FIX81's picture, never pass more. No instance
mask changes, so no other query (the shadows, the AO, the eye ray that finds translucent water)
meets anything different.

**How `mtl_rtw_ss_next` passes over a hidden inline model.** The step takes the brush flag beside
the alias flag: at `0` a face of an alias model or of an inline brush model whose choice is "no
reflection" is passed, and the ray is intersected again just past it with the same mask, as FIX81
passes an alias face; at the `MTL_RTW_LAYERS`-th such face it is traced on with `MTL_RTW_MASK_MAP`
(FIX81's last cast), and that cast's choice stands. So the ray is cast at most `MTL_RTW_LAYERS + 1`
times, FIX81's bound, whichever models it passes. A world face the view does not show still stops
it with no reflection, a door the view shows gives its reflection, and a ray that meets no hidden
brush model face takes exactly FIX81's steps. A closed brush has a front and a back face (the rays
cull none), so a hidden door costs two more casts, as a hidden alias model does.

*The bound.* The last cast's mask still holds the brush models, so past four hidden model faces a
brush model is met as on FIX81: one the view shows gives its reflection, a hidden one none. A mask
of the world alone for the last cast would pass a hidden one there too, but also a door the view
shows behind four hidden alias faces, which FIX81 shows: with the map's mask no ray that FIX81
reflects loses its reflection.

**Why `r_rt_water_hits 1` is unchanged.** `rm_rtw_trace_ws` calls `mtl_rtw_ws_next`, which is not
changed: at `1` a hidden brush model is shaded in world space like any other map face (TASK158), so
it is not passed. The new word is bound to both kernels with the other geometry constants, and only
`rm_rtw_trace` reads it; the G-buffer marks, the compaction and the indirect trace are untouched.
