# RT-primary data: the light entities, the emissive faces and the sky colour (TASK187, PM5)

Card: TASK187 (#534). Plan: `docs/plans/presentation-modes.md`, section 4.2 and row PM5 of section 7. The
data the ray-traced presentation will light the world with is gathered at map load and counted on the
`r_rt_stats 1` line. Nothing draws it yet: there is no visual change, no new cvar and no asset.

## What it is

- **The entity-lump parser** (`src/client/refresh/metal/rm_rtlights.c`, pure C, `header/rm_rtlights.h`).
  It reads the BSP's `LUMP_ENTITIES` from the map file `Mod_LoadBrushModel` holds, not from the game, which
  frees an untargeted light at spawn. Every entity whose classname starts with `light` is kept, as qrad3
  selects them. The keys it keeps are `origin`, then `light` (else `_light`, else 300), `_color` (divided by
  its largest channel, as qrad3's `ColorNormalize`) and `_style` (else `style`; one outside 0 to 255 reads
  0). A `light_spot`, or a light with a `target`, is a spotlight. It keeps `_cone` (else 10) and a direction:
  toward the targeted entity's `origin`, or from `angle` (-1 up, -2 down) when it has no target. The
  `targetname` is looked up in the same lump, first match. At most 2048 targets are compared, and a name of
  64 bytes or more matches nothing. A target that is not found leaves the light pointing down and is counted
  as `untargeted`.
- **The cap**: 2048 lights (`RM_RTL_MAX_LIGHTS`, MAX_MAP_ENTITIES). Lights past it are counted, not kept.
- **The lump is untrusted** (CLAUDE.md, "Security-relevant boundaries"). The lump is not NUL terminated, and
  every read is bounded by its length. A NUL byte ends it, as it ends the game's string. Each malformed
  class has its own result:

  | class | result |
  |---|---|
  | a quoted string the lump ends inside | the lump refused (`an unterminated quote`) |
  | a `{` inside an entity | the lump refused (`a brace too deep`) |
  | a `}` or a key outside an entity, a key without its value, the lump ending inside an entity | the lump refused (`unbalanced braces`) |
  | a key of 256 bytes or more, a value of 1024 or more | that entity skipped (`refused` counts it) |
  | a number that does not parse (letters, too few or too many, an exponent, too large for a float) | that entity skipped |

  A refused lump keeps no light. Numbers are parsed by hand, with no locale: an optional sign, digits and an
  optional fraction.
- **Emitters**: the faces whose texinfo has `SURF_LIGHT`, no `SURF_SKY` (the sky is its own term) and a
  value above 0. They come from the raw texinfo and faces lumps, because the loaded `mtexinfo_t` drops the
  value. The texinfo value is the intensity. Each distinct texture name has one slot in a per-texture
  cache (at most 1024). The slot holds the texture's average colour through the palette: the raw
  texel mean, not qrad3's normalized and doubled value. The texels the image loader had are freed after the upload, so the colour comes from one read of
  `textures/<name>.wal` for each distinct emitter texture. That is the path `Mod_LoadTexinfo` already loads.
  The header and the body are checked against the file's length. A texture that cannot be read keeps
  the grey 0.5 and is counted `unread` (qrad3 leaves such a texture at 0, except texinfo 0).
- **The sky colour**: `ref_metal` holds the six sky images, not a `SURF_SKY` texture's texels. The colour is
  the mean of the faces' means as uploaded (`image_t.mean`, TASK56).
- **The hooks**: `rm_rtdata.c` is called from the end of `Mod_LoadBrushModel` and from
  `RI_EndRegistration`, where the sky is set. Both return at once while `r_rt_stats` is 0. So the stock path
  runs none of it, and no scenario in `scripts/pins/scenarios/` sets `r_rt_stats`.

## PM6 decides

Choices the data makes that PM6 should settle knowingly, since nothing consumes the data yet:

- **The emitter colour.** The value stored is the raw texel mean, 0.5 for an unread texture. qrad3
  normalizes the mean and doubles it when its scale is under 0.5, and leaves an unread texture at 0. Which
  to use as emission is open.
- **Empty values.** `"light" ""` and `"_color" ""` drop the whole light here; qrad3 reads them as `atof` 0
  (so 300) and as white.
- **A short `_color`.** `"_color" "1 1"` drops the light; qrad3's `sscanf` would keep two channels.
- **A spotlight with a missing target.** It points down (0 0 -1); qrad3 leaves its normal at zero, so its
  cone test lights nothing.
- **A fractional `_style`.** One such as 0.5 wins over `style` here; qrad3 truncates `_style` to an int
  before its `!dl->style` fallback.

A cached world reload (`flushmap 0`, or `r_rt_stats` turned on and the same map loaded again) prints no
`rt-data` line: the model cache keeps the world, so `Mod_LoadBrushModel` and the hook do not run. This
affects the stats line only.

## The host test

`tests/metal/rm_rtlights_test.c`, run by `tests/check-ref-metal.sh` with ASAN and UBSAN. Each lump is
copied into a heap block of exactly its length, with no NUL after it. The cases are: a normal map with
every key; the defaults; a spotlight with its target (after it, and the first of two); a missing target; a
target name too long to match; a targetname from an earlier lump; each malformed class above; the normal
lump cut at every byte; a cut inside a quoted value and one right after a key; a NUL inside the lump; a
length of 0 and a NULL lump; and 2085 lights against the cap of 2048. Then the numbers under a
decimal-comma locale where the host has one, the emitters (sky faces, value 0, a 32-byte name with no NUL,
a texinfo index outside the lump, a partial record), the `.wal` mean and its bounds, the sky mean and the
line.

Red first: ten broken copies of the parser in scratch, each failing the test, then the real one passing:

```
end off-by-one: exit 134: ERROR: AddressSanitizer: heap-buffer-overflow
token copy off-by-one: exit 134: ERROR: AddressSanitizer: global-buffer-overflow
token too-long off-by-one: exit 134: ERROR: AddressSanitizer: stack-buffer-overflow
wal body off-by-one: exit 1: FAIL tests/metal/rm_rtlights_test.c:488: | rm_rtlights_test: 160 checks, 1 failed
wal header off-by-one: exit 1: FAIL tests/metal/rm_rtlights_test.c:514: | rm_rtlights_test: 160 checks, 1 failed
comment lookahead unbounded: exit 134: ERROR: AddressSanitizer: heap-buffer-overflow
target lookup off-by-one: exit 1: FAIL tests/metal/rm_rtlights_test.c:188: | rm_rtlights_test: 160 checks, 1 failed
light cap off-by-one: exit 134: runtime error: index
texinfo index off-by-one: exit 134: ERROR: AddressSanitizer: stack-buffer-overflow
float range unchecked: exit 1: FAIL tests/metal/rm_rtlights_test.c:276: | rm_rtlights_test: 160 checks, 1 failed
green:
rm_rtlights_test: 160 checks, 0 failed
```

Round 2 added `test_texture_cap` (1100 distinct emitter textures), `test_emitter_cap` (one face past
`RM_RTL_MAX_EMITTERS`) and the all-zero `_color` check, 170 checks in all. Each fails on a scratch copy with
its guard removed:

```
texture cap deleted: ERROR: AddressSanitizer: global-buffer-overflow
emitter cap deleted: runtime error: index 65536 out of bounds for type 'rm_rtl_emit_t[65536]'
all-zero colour divided by its max: FAIL rm_rtlights_test.c:148 | rm_rtlights_test: 170 checks, 1 failed
real: rm_rtlights_test: 170 checks, 0 failed
```

The host test is built without `-fwrapv` (the other host tests keep it), so UBSAN reports signed overflow
in `rm_rtlights.c`.

## The counts on this Mac (the owner's game data)

`make ref_metal client game`, then for each map `release/quake2 -datadir <datadir> -portable +set
vid_renderer metal +set r_rt_stats 1 +set logfile 2 +set vid_fullscreen 0 +set r_mode 4 +map <map>`, read
from the run's own `qconsole.log`. These are the lines as printed:

```
ref_metal: rt-data: maps/base1.bsp: lights 256 (spot 0, styled 9, untargeted 0) emitters 214 (textures 6, unread 0) sky 97,29,8 refused 0 (past the cap 0)
ref_metal: rt-data: maps/q2dm1.bsp: lights 94 (spot 0, styled 0, untargeted 0) emitters 133 (textures 5, unread 0) sky 97,29,8 refused 0 (past the cap 0)
ref_metal: rt-data: maps/jail1.bsp: lights 368 (spot 0, styled 29, untargeted 0) emitters 182 (textures 9, unread 0) sky 84,27,9 refused 0 (past the cap 0)
```

None of the three lumps refused an entity, and each map's emitter textures were read. No frame and nothing
else derived from the game data is committed.

## Checks

In the same hold: `tests/check-ref-metal.sh` ended `check-ref-metal: ok (... RT data 160 checks, 0 failed;
... ref_metal built)`, and `make test` ended `yq2test: 444 tests, 0 failed`.
