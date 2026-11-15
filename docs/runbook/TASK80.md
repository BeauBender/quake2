# `gl1` on the build host (TASK80)

`ref_gl1` used to stop on the build VM with "Failed creating OpenGL
pixel format", because it demands an accelerated visual and the only
OpenGL renderer there is Apple's software one. Since TASK80 it retries
once without that demand (in `RI_InitContext`, where the context is
created; a failure of the retry is reported as before) and switches
vsync off for such a context: with vsync on, the first buffer swap
waited for a display sync that never came and the engine drew no frame
in 240 s. The engine log of a pin now shows `GL_VENDOR: Apple Inc.`,
`GL_RENDERER: Apple Software Renderer`, `GL_VERSION: 2.1 APPLE-24.0.2`.

Observed on this host (machine load average 10 to 60 over the runs, other
work running): `record gl1 world` takes 17 to 24 s for 23 frames, `soft`
3 to 7 s. Three `gl1` records of `world` compare at score 0.000000 and bad
0.000000 on every frame (23 of 23), so the software OpenGL renderer is
deterministic here. `soft` against `gl1` on `world`: score 0.013 to
0.081, bad fraction 0.13 to 0.92 per frame, all 23 over the default
thresholds. The frames show the same scenes; `gl1` is smoother and
slightly darker (true colour and bilinear filtering against the software
renderer's 8-bit palette: 3589 to 41005 distinct colours per `gl1` frame
against 105 to 189 per `soft` frame), so thresholds for `soft` against
`gl1` are card P3's to set.
