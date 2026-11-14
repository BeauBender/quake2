# 0010. Ray tracing at an internal resolution, upscaled with the MetalFX temporal scaler

Status: accepted 2026-11-14 with TASK105 (#248), a design card. No code
yet; RT11 builds it.

## Context

The ray-tracing design (`docs/plans/metal-raytracing-design.md`) traced
every effect at the frame's resolution and denoised with its own temporal
accumulation and 5x5 bilateral blur (sections 2 and 3). It used no
MetalFX. RT9 asked for the display refresh at native resolution with
everything on.

TASK101 measured the M6 (section 8 of the design). At the display's native
5120 x 2880 pixels the ray passes and per-frame items cost 36.8 ms against
the 16.7 ms frame at 60 Hz (verdicts table, row RT9 (1)). Tracing at
1920 x 1080 and upscaling to 5120 x 2880 with `MTLFXTemporalScaler` costs
7.3 ms as a sum and 9.3 ms as one measured frame; with
`MTLFXTemporalDenoisedScaler` it costs 17.6 and 18.9 ms (row RT9 (2)).
The inputs were synthetic, so image quality was not assessed.

## Decision

1. With a ray-traced effect on, the whole 3D frame (G-buffer raster, ray
   passes, temporal accumulation and denoise, lighting resolve, forward
   pass) runs at an internal resolution with a jittered projection, and
   `MTLFXTemporalScaler` upscales it to the output size. 2D draws after it
   at the output size.
2. Section 2's temporal accumulation and bilateral denoiser stay and run
   before the resolve, per effect. The scaler upscales the colour they
   produce; it does not replace them.
3. `MTLFXTemporalDenoisedScaler` is not used. With the trace it is over the
   60 Hz frame at both measured inputs on the M6 before any raster or
   shading. Section 2's denoiser is needed anyway at full size and on a
   device without the scaler.
4. `MTLFXSpatialScaler` is not used: no history, no jitter, and a saving
   under a millisecond. `MTLFXFrameInterpolator` is out of scope: it adds
   latency in a game.
5. The scale is the cvar `r_rt_scale` (design section 2, named there and
   not yet in the code). Its default is a height per GPU family: 1080 rows
   on apple9 and later, measured on the M6 only; 720 rows on apple6 to
   apple8, assumed until TASK106 (#249) measures apple7.

## What this does not change

- With every `r_rt_*` effect off nothing is scaled: the frame renders at
  the output size and every gl1 pin is unchanged.
- The renderer API (`ref.h`, `API_VERSION`) and `refdef_t` do not change.
  The reset events are detected inside the renderer.
- Pins run at `r_rt_scale 1`, with no scaler, as they already run with
  temporal accumulation off.
- RT7's acceptance (60 fps at 1080p on apple7) is unchanged: it lands
  before the scaler and traces 1080 rows.

## Consequences

- RT9's fps target is at the native output size with the default
  `r_rt_scale`, not tracing at native pixels.
- New rows in `docs/plans/metal-port-cards.md`: RT11 builds the internal
  resolution, the scaler, the cvar and the reset events; RT12 measures the
  image quality of the upscaled frame on real frames. RT9 depends on RT11.
- RT10 replaces the per-family defaults with measured ones and adds the
  G-buffer raster, RT7 and the scaler to its per-pass table.
- The look of a ray-traced frame now depends on MetalFX, which ships with
  the OS and can change with it. That is one reason the pins stay at
  `r_rt_scale 1`.

## Reversal

Set the RT9 target back to tracing at native pixels, drop RT11 and RT12,
and add a record that names this one. The denoised scaler is reconsidered
when a measurement puts it inside the frame and RT12's image quality
favours it.
