# Images, 2D and the cinematic in `ref_metal` (TASK85, N2)

N2 is the first card judged against the shim on content
(`docs/plans/ref-metal-native.md`, section 17). The references are
`gl1-metal` records made at the card's base commit. `ref_metal.dylib` and
the shim build of `ref_gl1.dylib` are separate files in `release/`, so
both stay built side by side (a `make ref_gl1` without
`WITH_METAL_SHIM=yes` replaces the shim build):

```sh
make client game ref_metal && make ref_gl1 WITH_METAL_SHIM=yes
MTL_DEBUG_LAYER=1 PINS_DEMO=idlog.cin PINS_EVERY=7 scripts/pins.sh record gl1-metal cinematic   # at the base commit
MTL_DEBUG_LAYER=1 PINS_DEMO=idlog.cin PINS_EVERY=7 scripts/pins.sh record metal cinematic
PINS_DEMO=idlog.cin PINS_EVERY=7 scripts/pins.sh compare metal cinematic --ref gl1-metal \
    --ref-id <base id> --threshold 0.005 --bad-threshold 0.001
```

The `compare` needs the same `PINS_DEMO` and `PINS_EVERY` as the
records, since they enter the settings hash of the pin id. A record is
accepted when it completes, `engine.log` holds no validation message
beyond `Metal API Validation Enabled` and no `a command buffer failed`
line, and the last teardown line reports `0 live objects` and
`0 unreleased`.

Moving code the shim links (here `common/` of `ref_metal`) is proved
neutral by `gl1-metal` records of `hud2d`, `cinematic` and `world` from
before and after the change, compared with `--ref-id <before>
--cand-id <after> --threshold 0 --bad-threshold 0`, and by a byte
comparison of the TGAs (a scratch script over the two directories, not
committed). Rebuild before recording after a header edit or a rebase:
`scripts/pins.sh` refuses a binary or module older than a header under
`src/`, and the shim's objects carry no header dependencies, so remove
`build/release/ref_gl1_metal/` once after a header the shim includes
moved (`make -B` of the client, `ref_metal` and the shim build is the
plain way).

`metal_stats 1` did not reach a record: with `set metal_stats 1` in
`release/baseq2/autoexec.cfg` a record printed no `metal_stats` line, so
the setting is not taken from there while capturing (the scenario comes
on the command line). The per-frame counters were read from
the engine started by hand with the record's command line
(`PINS_DRYRUN=1` prints it) plus `+set metal_stats 1` and a scratch
`pins_dir`; each captured frame prints one `metal_stats` line.

On the build VM, base `4175e8cc` (references at `beb6bb9e`, the same
`src/`), head `11dfcbff`, load averages 7.7 to 31.9: three release
records and one `ASAN=1` record of `metal cinematic` each 12 frames ok
at score 0 and bad 0, every frame byte-identical to `gl1-metal`; the
`gl1-metal` records after the move byte-identical to those before it on
all three scenarios; every captured frame `0 created after init`. The
`hud2d` frames stay the `r_clear` colour until N3 draws the pics.
