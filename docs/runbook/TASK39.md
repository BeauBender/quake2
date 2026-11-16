# Observed on the first run (TASK39)

Host: the build VM, macOS arm64, `soft` at 640x480, `PINS_EVERY` 30,
`demo1.dm2`. Each record takes 5 to 7 seconds and captures 23 frames of
689 rendered.

1. The client starts with the hooks: `record soft <scenario>` exits 0 for
   all four scenarios and the engine log shows the latched values in use
   (frames named `<scenario>-NNNNNN.tga` in the store, `manifest.txt` with
   `soft 640x480` on every line).
2. `+demomap` on the command line reaches playback with `cl.attractloop`
   set: capture only runs under that condition and every record captured
   23 frames.
3. At demo end the server takes the `killserver` path (`SV_Nextserver`
   with an empty `nextserver`; the log reads `Server was killed.`), the
   client reaches `ca_disconnected` and the queued `quit` runs: every
   record quits by itself with status 0 (item (j) of the review: the
   unattended quit works with capture on). Without any `pins_*` argument
   a plain `timedemo 1` run of the same demo still did not quit (killed
   after 25 seconds), as before. The end of `demo1.dm2` is its end marker,
   not a short read.
4. `screenshot` inside `SCR_UpdateScreen` before `R_EndFrame` yields the
   complete frame on `soft`: world, view weapon and status bar are all in
   the captured frames, and a frame opened in an image viewer is upright
   (floor at the bottom, status bar at the bottom edge, numbers reading
   left to right; TASK9's acceptance). Not run on `gl1`: at the time
   `ref_gl1` could not create a GL context on the build VM (fixed by
   TASK80, below), so whether its screenshot (which
   now flushes the 2D batch first) is unchanged stays open for a host
   where `gl1` runs (card P3).
5. The owner's data holds two demos, both in `pak0.pak`: `demo1.dm2` and
   `demo2.dm2`. `PINS_DEMO` selects one; `demo1.dm2` stays the default.
6. Run to run on `soft`: `world` and `hud2d` are identical (score and bad
   fraction 0.000000 on all 23 frames) over four runs each. `models` is
   not: frame 7 differs in every pair, in a small patch of about 250
   pixels, which is consistent with temporary-entity explosions taking a
   random yaw and frame (`CL_ParseTEnt`) from the engine PRNG (not
   attributed by this record, which shows only the score and a pixel
   count); worst over five pairs
   score 0.000446, bad fraction 0.000973, which passes the default
   thresholds but is not 0. `particles` differs in 16 of 23 frames; worst
   over five pairs score 0.011556 and bad fraction 0.172061, so 10 or 11
   frames fail the default bad threshold. Both are marked
   `nondeterministic:` in their scenario files with these numbers.
   Light styles (item (i) of the review) did not differ between runs:
   `world` was identical every time.
7. `hud2d` with `r_drawworld 0` on `soft`: a uniform dark grey (RGB 26, 26,
   26) behind the status bar and the crosshair, 99.1% of the pixels. Other
   renderers are not measured yet.
8. `timedemo.txt` receives the line, for example `689 frames, 2.0 seconds:
   353.0 fps`; on the build VM `world` ran at 340 to 397 fps, `models` at
   280 to 300, `particles` at 232 to 256 and `hud2d` at 647 to 704.

Also found on the first run and fixed: the run read the `config.cfg` the
game data holds and wrote one beside the binary (now neither); a demo
that could not be opened left the engine at the console until the
timeout (now exit 1 at once); `pins_capture` accepted a leading space or
plus sign (now refused).
