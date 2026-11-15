# The G-buffer and the lighting resolve in `ref_metal` (TASK54, RT2)

RT2 adds the G-buffer of `docs/plans/metal-raytracing-design.md`, section 3 (its
subsection "The G-buffer as RT2 built it" has the paths, the contract and the off-state
decision). Host: the bare-metal Mac, `Apple M6`, macOS 27.2.1. Base: `origin/main` at
`aa5ab641` (FIX61). The card was first measured on the earlier base `d9e76e40` (TASK53)
and then rebased onto `aa5ab641`, which adds TASK119 (2D options, all off by default) and
FIX61 (the frame sized by the client's rule, `GLimp_DrawableInPixels`).

After the rebase and the first rework, these were run again on engine commit `f0127557`
(pin ids `f012755714a9-*`), on base `aa5ab641`. The card was then rebased onto `5f476f88`
(TASK11, #308), which replaces the image decoder (ImageIO for stb_image). Every stock TGA
and PNG decodes byte for byte as before, and TASK11's own `metal` and `gl1-metal` pins match
its base byte for byte. So that rebase changes nothing these runs measured. The
card's own source is unchanged by it (`git range-diff` shows every commit equal):

- the pins of `world`, `hud2d`, `models` (seeded) and `particles` (seeded), on both paths;
- the render-size check, the motion-vector check and the particle-size check.

The commits after `f0127557` change only the documentation. The other pin rows, the
jitter check and the ASAN run are of engine commit `fde68b47` (pin ids `fde68b47738c-*`)
on `d9e76e40`, and the timing is of `4bf077a4` on `d9e76e40`. Neither commit is an
ancestor of the head. After them came the rebase and the rework:

- particles sized in render pixels on a view below the frame's size;
- the world normals buffer made by the first G-buffer view instead of at map load;
- `r_rt_gbuffer_show` clamped before it becomes an integer;
- the entity split as a pure function.

On the default path the four scenarios run again are byte-identical to `aa5ab641`'s
`ref_metal`. On the G-buffer path their scores are those `fde68b47` had. Screen unlocked
before every run, no other `quake2`: the wrapper checked both and printed the one-minute
load, 1.95 to 2.98 on the runs on `f0127557`. Every log, frame list, script and result is
in `<artifacts>/TASK54/` (the runs on `f0127557` in `<artifacts>/TASK54/rework1/`) and
`<pin store>`.

```sh
export PINS_STORE=<pin store> PINS_BASEDIR=<game data> MTL_DEBUG_LAYER=1
make client game ref_metal
scripts/pins.sh record metal <scenario>          # PINS_SEED=1 too for models, particles; dlights seeded only
# the G-buffer path: scenarios/<s>-gb.cfg (<s>.cfg plus "set r_rt_gbuffer 1"), deleted after;
# the cinematic by the line appended to cinematic.cfg for one record (only that scenario plays a .cin)
scripts/pins.sh record metal <s>-gb
framediff <gl1 reference frame> <frame> --threshold T --bad-threshold B   # every frame, both references
cmp <base frame> <frame>
# the base on f0127557: a copy of the tree at aa5ab641 built outside the checkout, recorded by
# its own scripts/pins.sh into a scratch store (<artifacts>/TASK54/rework1/basestore-aa5ab641)
```

**Pins on `f0127557`.** Against the `gl1` records of TASK83 (`de1da7fdba66-*`,
`f6bb192775b8-*`) at TASK53's values, and seeded `models` and `particles` against the seeded
`gl1-metal` records of `bdda98128905` at 0.005 and 0.001. "Identical" counts frames that are
byte-identical to `aa5ab641`'s `ref_metal` record of the same settings:

| Scenario | Values | Default path (forward) | G-buffer path, every effect off |
|---|---|---|---|
| `world` | defaults | 23 of 23 each, worst 0.000010, bad 0.000033; 23 identical | 23 of 23 each, worst 0.000493, bad 0.000033; 0 identical |
| `hud2d` | defaults | 23 of 23, 0, 0; 23 identical | 23 of 23, 0, 0; 23 identical |
| `models`, seeded | 0.005, 0.001 | 23 of 23, worst 0.000039, bad 0.000589; 23 identical | 23 of 23, worst 0.000462, bad 0.000596; 0 identical |
| `particles`, seeded | 0.005, 0.001 | 23 of 23, worst 0.000039, bad 0.000589; 23 identical | 23 of 23, worst 0.000471, bad 0.000596; 0 identical |

**Pins on `fde68b47`** (before the rebase). The same references. "Identical" counts frames
that are byte-identical to the base build's `ref_metal` record of the same settings
(TASK53's `0269ca35`, whose frames are the base's: TASK53 drew nothing differently):

| Scenario | Values | Default path (forward) | G-buffer path, every effect off |
|---|---|---|---|
| `world-fullbright` | defaults | 23 of 23, worst 0.000031, bad 0.000072; 23 identical | the same; 23 identical |
| `skywater` | defaults | 23 of 23, worst 0.000010, bad 0.000033; 23 identical | 23 of 23, worst 0.000493, bad 0.000033; 0 identical |
| `cinematic` (`idlog.cin`, every 7th) | defaults | 12 of 12, 0, 0; 12 identical | 12 of 12, 0, 0; 12 identical (no 3D view) |
| `models` | defaults | 23 of 23, worst 0.000084 and 0.000232, bad 0.000612; 22 identical (unseeded) | 23 of 23, worst 0.000549 and 0.000694, bad 0.000618; 0 identical |
| `particles` | 0.02, 0.25 | 23 of 23, worst 0.010542 and 0.010208, bad 0.175205; 7 identical (unseeded) | 23 of 23, worst 0.010410 and 0.010318, bad 0.150456; 0 identical |
| `dlights`, seeded | 0.005, 0.001 | 23 of 23, 0, 0; 23 identical | 23 of 23, worst 0.000501, bad 0; 0 identical |

Every score is under 0.005 but unseeded `particles`, which two unseeded records reach by
themselves (TASK53). Byte identity on the G-buffer path holds where the light is white
(`world-fullbright`, `hud2d`, the cinematic) and not where it is a lightmap or a vertex
colour: the raster stores the texel and the light in `rgba8` and the resolve multiplies the
rounded values, where the stock fragment multiplies them unrounded, so a channel moves by
one level now and then (the largest score added is 0.0005, on `dlights`; a bad fraction
moves by 0.000007 at most). The forward path with depth and motion (`r_rt_motion 1`) is
byte-identical to the default path on all 23 frames of `world` (on `f0127557`), and so to
`aa5ab641`. With `gl_shadows 1` and `gl1_stencilshadow 1` (seeded `models`, on `fde68b47`), the
G-buffer path is within 0.000460 of the default path on every frame (the shadows are the
forward pass's).

**Validation and ASAN.** Every record above ran with `MTL_DEBUG_LAYER=1`. Each `engine.log`
holds:

- `Metal API Validation Enabled`;
- the attachments line on the G-buffer and motion paths;
- `teardown: 0 live objects`;
- no other validation line and no failed command buffer.

So does a G-buffer record of `models` over `demo2.dm2` (`waste2`, every 7th, 89 frames, on
`fde68b47`). On `f0127557` the default path's records end with 8 buffers created and
destroyed, as `aa5ab641`'s do. The G-buffer path's end with 9: the world normals buffer,
which its first view makes. The motion path's end with 8. The ASAN run is of `fde68b47`:

- `make ASAN=1 client game ref_metal`;
- `debug/quake2` with `MTL_DEBUG_LAYER=1`, `r_rt_gbuffer 1` and a scratch script beside
  the binary (`map base1`, 200 waits, `map base2`, 200 waits, `vid_restart`, 300 waits,
  `quit`);
- status 0, no sanitizer line, both renderer sessions `0 live objects` (1280x960
  attachments: `vid_highdpiaware` is 1 by default).

**Motion vectors** (on `f0127557`). `r_rt_motion_test 0.5` takes the previous view to be
this one turned 0.5 degrees about the camera's vertical axis, every entity its own previous
one. `r_rt_gbuffer_show 4` draws the vectors over the view (red and green 0.5 plus the
motion over 16 pixels). The check ran over the 23 frames of `world`:

- `<artifacts>/TASK54/mvcheck.c` gives each pixel's expected motion from the projection
  the log printed (1.0 and 1.333333);
- the bottom 41 rows (the status bar) and the top 33 (the notify lines `developer 1` prints)
  are left out, which leaves 5,975,455 pixels;
- the error is 0.024 px on average and at most 0.062 px, none over 0.1 px, on both the
  G-buffer and the motion path;
- the expected motion is 3.76 px on average and 5.9 px at most;
- the 8-bit view alone rounds by up to 0.031 px a channel.

**Jitter** (on `fde68b47`). `r_rt_jitter_x 0.5`, `r_rt_jitter_y 0.25` against 0, on the G-buffer path: the
colour moves (no frame identical, scores 0.00077 to 0.0073), the depth moves (`r_rt_gbuffer_show
6`: no frame identical, 0.000028 to 0.00011), and the vectors do not: with the turn above,
the jittered frames' vectors still match the unjittered expectation (0.024 px on average,
0.13 px at most, one pixel over 0.1 px, on an edge); a jitter in them would show as a 0.56 px
error everywhere. At 0 every frame is the frames above.

**Render size** (on `f0127557`). `r_rt_gbuffer_scale 0.5` at 640 x 480:

- `G-buffer attachments 320x240`, 2,293,760 bytes allocated;
- `motion attachments 320x240`, 1,146,880 bytes allocated;
- the view stretched into the frame, validation clean, `0 live objects`;
- against the full-size frames of the same path, the scores are 0.000662 to 0.010564
  (G-buffer) and 0.000628 to 0.010548 (motion): the stretch of a half-size image;
- the status bar unchanged.

**Particle size at the render size** (correctness I1 of the first review). Seeded `particles`
on the G-buffer path at scale 1 and at 0.5, each also with `cl_particles 0`: the same frames
without the particles, since the seed keeps the rest the same. Measured: the light the
particles add, the sum over pixels and channels of |with - without| / 255
(`<artifacts>/TASK54/rework1/penergy.c`, the rows above the status bar), over all 23
frames:

- before the fix (`33443533`): 18,214 at scale 1 and 40,100 at scale 0.5, 2.20 times; 2.14
  times as many pixels moved by more than 8 levels;
- after it (`f0127557`): 18,214 and 18,225, 1.00 times; 1.15 times the pixels, from the
  stretch's spread.

Scale 0.5 against scale 1 scores at most 0.012577 before the fix and 0.010065 after. The
frames without particles score the same 0.010065, so the particles add nothing to what the
stretch changes. At scale 1 the particle frames are byte-identical before and after the fix
(23 of 23), and so are the frames without particles at 0.5.

**Cost** (on `4bf077a4`; design section 3 says why the changes after it leave the timed
draws as they were). `MTL_GPU_TIME=1`, the `particles` scenario with `r_vsync 0` and one capture
(`PINS_EVERY=100000`, `PINS_TRACE=0`, a scratch pin store), windowed 640 x 480 and 1920 x 1080,
and `PINS_HIDPI=1` at 2560 x 1440 for a 5120x2880 drawable; base, forward, motion and
G-buffer alternated in each of three rounds; the base is `d9e76e40`'s `ref_metal` with the
same `MTL_GPU_TIME` aid beside the same `quake2`. The table and the decision are in design
section 3; in short, at 5120 x 2880 the frame's drawing takes 1.09 ms (base), 1.10 ms
(forward), 1.73 ms (forward with depth and motion, 136 MB added) and 6.80 ms (G-buffer, 332 MB
added), so the forward path stays the default with every effect off. A first attempt timed the
whole frame command buffer: at 1920 x 1080 the G-buffer's then read 16 ms, the wait for the
previous frame's present, since the raster can start before it; the timing above has none.

**Mutants.** 34 mutants of `rm_gbuf.c` and `common/mtl_gbuf_math.h`
(`<artifacts>/TASK54/mutants.sh`): 33 fail `tests/metal/rm_gbuf_test.c`; the survivor
(`rm_gb_render_size` scaling a scale of 1 to 3) is equivalent, since a side is never above
the frame's. The host test failed 60 of its 108 checks against a stub first; `scripts/pins.sh
selftest framediff_mask` failed on the framediff before `--mask` and passes with it. The
rework added the following:

- 17 mutants of the new pure functions (`rm_gb_entity_split`, `rm_gb_pixel_scale`,
  `rm_gb_point_scale` and `rm_gb_show`, in `<artifacts>/TASK54/rework1/mutants-rework1.txt`).
  16 fail the host test. The survivor (`k < 1` as `k <= 1` in `rm_gb_point_scale`) is
  equivalent, since a scale of 1 multiplies exactly.
- A framediff that divides the masked sum by every pixel instead of the marked ones. It
  passed `selftest framediff_mask` before the rework and fails it now.
- The host test of `rm_gb_show`, run with UBSAN against the old expression. UBSAN reported
  the conversion of 1e10, and 5 checks failed.
