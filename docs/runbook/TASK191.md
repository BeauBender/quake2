# RT-primary's light on alias models and the view weapon (TASK191, PM9)

Card: TASK191 (#538). Plan: `docs/plans/presentation-modes.md`, row PM9 of section 7, section 4.1
("Models") and section 6 (what must not change). Builds on TASK188 (the direct light), TASK189 (the
sky, the sun and the bounce) and TASK190 (the accumulation, the filter and the scaler).

## Design

Today, under the Ray traced presentation (`r_presentation 3`), an alias model is lit as gl1 lights
it: `R_LightPoint` samples the stock lightmap on the face below the model's origin, and gl1's rule
(`rm_alias_shade`: the shadedots, the minlight floor, the glow pulse, overbright, the gamma table)
turns that one light into the vertex colours. The world around it is lit by traced light. A monster
that stands in a traced shadow, or in the pool of a traced light, is still lit by the lightmap.

**The trade-off chosen: one traced probe a model, in place of the lightmap sample.** Under the mode,
`R_LightPoint`'s lightmap sample is replaced by the light RT-primary gives a world pixel at the same
point: the point on the face below the model where `R_LightPoint` read the lightmap (gl1's
`lightspot`), with that face's normal. The rest of gl1's model rule is unchanged, so a model keeps
gl1's shading shape (the shadedots), its minlight floor and its glow. A model on a floor in a traced
shadow is as dark as that floor; a model in a traced light pool is as bright as the pool. The
alternative, each model pixel lit at its own point and normal in a kernel over the G-buffer, gives a
model a shadow edge across its body. It needs a new kernel, a tag in the light attachment, the view
weapon rastered into the G-buffer and its depth unprojected, and about four times the code, in four
slices. The probe is the smallest design that keeps a model consistent with the floor it stands on.

- **Where it is evaluated.** On the GPU, one thread a probe, in a kernel of its own translation unit
  (`rm_rtmodel.metal`), dispatched in the direct light's compute encoder right after `rm_rtdir_trace`.
  The kernel includes `rm_rtdir.metal` and calls its functions, so the probe's light is a world
  pixel's by the same code: `rm_rtdir_direct` (the map's lights with their shadow rays, the dynamic
  lights, the lightstyles), `rm_rtdir_open` (the sky light and the sun) and `rm_rtdir_bounce`, under
  the world's exposure (`rm_rtdir_out`, `rm_rtdir_out_open`). The sky, sun and bounce rays are one a
  pixel for the world; a probe averages 16 of each, so a model does not flicker with one ray's noise.
  `rm_rtdir.metal` itself is not edited: its two entry points are renamed by two `#define`s in the
  new file, so the world's kernel compiles exactly as before.
- **The lights and the structure, one copy.** The probe dispatch reuses the encoder's bindings: the
  frame's instance structure, the map's light records, the dynamic lights, the lightstyles, the
  material table, the sky noise, the map's geometry and bounce records and the lightmap array. Only
  the view (with no own-light table, so a probe adds no emissive face's own light) and the probe
  buffers are bound on top. No light list is copied or rebuilt.
- **No model shadows a probe.** The probe's point is under its model, so a ray from it to a light
  above crosses the model itself. The new translation unit redefines `MTL_RTAO_MASK`, the mask of
  every ray in `rm_rtdir.metal`, as `MTL_RTW_MASK_MAP` (the world and the inline brush models,
  TASK158) before it includes the file. So the world, doors and platforms shadow a probe, and no
  alias model does, including another model. The world's own shadows of models are unchanged.
- **The probe reaches the CPU a few frames late.** The vertex colours are made on the CPU before the
  GPU traces. A probe's result is read when the frame semaphore guarantees that the frame which traced
  it has completed: frame N's dispatch reads the slot that frame N - 3 wrote
  (`RMB_FRAMES_IN_FLIGHT` is 3), and frame N + 1's models draw with it, so the light is 4 frames
  old. Each result is kept with its model and origin. A model takes the result of the same model and
  kind (weapon or not) whose origin is nearest to its own, within 64 units. A model with no match,
  for its first frames in view, keeps the stock light. At 60 Hz the lag is 67 ms. The world's light is
  already accumulated over about ten frames at the default `r_rt_temporal 0.9`. The lag is fixed in
  frames, so the records are deterministic.
- **The units.** The probe is the light a world pixel's light attachment holds (in the lightmap's
  units, times the exposure, clamped to 1, as the resolve reads it). `R_LightPoint` returns the
  lightmap's units times `r_modulate`, so the model's light is the probe times `r_modulate`.
- **Which models.** Those gl1 lights from the lightmap (`rm_alias_lit`: not a shell, not
  `RF_FULLBRIGHT`), except a translucent model, one under the IR goggles that draws red
  (`RF_IR_VISIBLE`), and a depth-hacked model that is not the view weapon. These keep the stock light.
  Minlight and glow models are traced: their floor and pulse are applied after the light, as in gl1.
- **The view weapon.** It is lit as gl1 lights it, by the light at its origin, which is the player's
  eye: the probe is on the floor below the player. The weapon's depth range (`RF_DEPTHHACK`) and its
  projection are not involved, since the probe is not at the weapon's pixels. The light value sent to
  the server (`r_lightlevel`, which the game's monsters read) stays `R_LightPoint`'s, so gameplay is
  unchanged.
- **Other presentations and devices without ray tracing.** The probes are requested and used only
  while the direct light pass runs (`RM_RTDir_Active`: RT-primary, on a device with ray tracing,
  `r_rt_direct` on). Anywhere else `R_DrawAliasModel` runs as before and its light is
  `R_LightPoint`'s. Stock, Enhanced and Retro run no new code, and their pins do not move.
  `r_rt_direct_models 0` keeps the stock model light under RT-primary.
- **Cost.** At most 64 probes a frame (`RM_RTM_MAX`). Each traces up to 32 shadow rays for the map's
  lights, 32 for the dynamic lights, and 16 each for the sky, the sun and the bounce, so at most
  about 7,000 rays a frame, against about 300,000 for the world's pixels at 640 x 480. Memory: three
  slots of 64 probes, 48 bytes each. The CPU match is a scan of at most 64 results a model.

The effect table is unchanged: the probes are part of the direct light's row (`RM_RTFX_DIRECT`), and
`r_rt_direct_models` is one of its settings, classified in `rm_rtgate.c` and read through
`RM_Effect`.
