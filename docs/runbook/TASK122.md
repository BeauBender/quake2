# Particle size cap relative to the resolution in `ref_metal` (TASK122)

`metal_particle_cap 1` scales the largest particle size (`gl1_particle_max_size`,
the value the start-up took) by `height / 480` each view, the factor the size
already has (the plan, [P13](high-resolution.md)); the default `0` leaves it as
it was. The formula is `rm_particle_cap` in `rm_pure.c`. Host: the build VM (a
paravirtualised Metal device), at base `1701a052` and the branch.

**Host test** (`test_particle_cap`): the cap equals `max` at height 480 for every
mode and at every height for a mode other than 1; at mode 1 it is `max * height /
480` (40 gives 120 at 1440, 240 at 2880, 20 at 240, 90 at 1080); a huge height,
a huge `max` or an infinite height is held to `RM_POINT_ALIASED_MAX`; a height
not above 0 or NaN, and a `max` that is NaN or infinite, leave `max` alone; and
through `rm_point_size` the 1440-line size of a near particle is 120 with the
cap and 40 without.

**Pins** (`MTL_DEBUG_LAYER=1`, `PINS_SEED=1`, trace on, 23 frames each,
`--threshold 0 --bad-threshold 0`; the reference a record of `metal` built from
`1701a052`): at `metal_particle_cap 0` the scenarios `world`, `world-fullbright`,
`skywater`, `dlights`, `models`, `hud2d` and `particles` (640x480) all 23 frames,
0 failed, worst 0, bad 0. `particles-cap` at 640x480 against the `particles`
record (frames copied under the scenario's frame names): 23 frames, 0 failed,
worst 0, bad 0. At 2560x1440 (the window opened at that size, frames of 2560x1440)
`particles-cap` run against run: 23 frames, 0 failed, worst 0, bad 0. `particles-cap`
against `particles` at 2560x1440 (`--threshold 0.005 --bad-threshold 0.001`): 19 of
23 frames identical, 4 differ (frames 5, 8, 12 and 16: worst score 0.000841, bad
0.007824); the owner does not gate the look. That last comparison is the one that
tests the switch: at the default height of 480 the factor is 1, and a run against
run compares a build with itself, so both also pass without the wiring in
`rm_effects.c`. Repeat it with `PINS_WIDTH=2560 PINS_HEIGHT=1440 PINS_SEED=1` for
`particles` and `particles-cap`; the two must differ. The records have one validation line,
`Metal API Validation Enabled`, and no message.

**Mutants** of `rm_particle_cap` (17 run, each operand of the compound guard and
each arithmetic and bound operator): 15 killed outright (the mode test inverted or
disarmed, the height test in three spellings or dropped, the finite test dropped or
inverted, `480` as 481 or a product, the product as a sum or a square, the upper
bound dropped, each return replaced by `max`), 2 equivalent (`cap >` for `!(cap <`,
and `!(cap <=` for `!(cap <`: they differ only where `cap` is exactly
`RM_POINT_ALIASED_MAX`, where both branches give that value). One form of the mode
test the compiler refused for an unused operand was re-run in a form that compiles
and is killed.
