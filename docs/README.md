# Repository map

Where things live, and where to start. `doc/` is the user documentation that
ships with releases; `docs/` (this directory) is for working on the code.

## Start here

- `CLAUDE.md` (also `AGENTS.md`): build commands, the gate, architecture,
  security boundaries and the working rules for agents.
- `scripts/setup.sh`: checks the toolchain and wires the commit and push hooks.
- `scripts/test.sh`: the gate. Every commit must pass it. It ends with `make test`, the unit-test suite.
- Platforms: only Apple platforms are built and tested; other platforms are
  unmaintained (`docs/decisions/0008-apple-platforms-only.md`). CI runs the
  macOS test build, CodeQL and Cppcheck. Both CodeQL analyses (C and
  Actions) run on every push to `main`; a pull request skips them only when
  it is documentation only, every changed file being Markdown under `doc/` or
  `docs/` or at the top level, and anything else runs them
  (`.github/scripts/codeql-changes.sh`, tested by
  `tests/check-codeql-changes.sh`). Cppcheck always runs. A newer push to a
  pull request cancels the older run (`docs/runbook/FIX66.md`).

## Source (`src/`)

| Path | What it is |
|---|---|
| `src/common/` | Engine core shared by client and server: cvars, command buffer, filesystem, network channel and messages, collision BSP |
| `src/server/` | Server side of the engine, linked into `quake2` and `q2ded` |
| `src/client/` | Client: parsing server messages (`cl_parse.c`), downloads (`cl_download.c`), input, menus, sound, video |
| `src/client/refresh/` | Renderers built as separate modules: `gl1`, `gl3`, `soft`, plus `files/` with the asset loaders they share (`imgdecode.c`, the PNG, JPEG and TGA decoder over ImageIO, is also linked into the client) |
| `src/client/refresh/gl1/metal/` | The OpenGL subset on Metal that `ref_gl1` is built against with `WITH_METAL_SHIM=yes` on macOS (design: `docs/plans/metal-shim-design.md`); gl1's own files are not edited |
| `src/client/refresh/metal/` | `ref_metal`, the native Metal renderer, macOS only, built with `make ref_metal` and not by `make` (design and as-built record: `docs/plans/ref-metal-native.md`); `vid_renderer metal`, in the fallback order and the video menu on macOS only; C with an Objective-C back end behind `header/rmb.h`, checked by `tests/check-ref-metal.sh` |
| `src/game/` | The game module: gameplay, monsters, weapons, savegames (`savegame/`) |
| `src/backends/` | OS specifics: `unix`, `windows`, `generic` |
| `src/win-wrapper/` | Windows launcher wrapper |

## Everything else

| Path | What it is |
|---|---|
| `doc/` | User documentation, numbered in reading order; cvars in `040_cvarlist.md`, commands in `050_commands.md` |
| `docs/plans/` | Plan anchors for multi-card work; cards cite them |
| `docs/runbook/` | What each card measured, one file per card (`<CARD>.md`). A card writes its results there and adds only its one index line to the `## Card results` section of the shared runbook, `docs/plans/metal-port-harness.md`, which keeps the method; see its [README](runbook/README.md) |
| `docs/decisions/` | Decision records: what was decided, why, and what it costs |
| `docs/audits/` | Audit write-ups: the [small vendored files](audits/small-vendored.md), the [asset-loader malformed-input audit](audits/asset-loaders.md) and the [network message parser audit](audits/network-parsers.md) |
| `docs/analysis/` | Measured inventories of existing code that plans and cards cite |
| `docs/DEPENDENCIES.md` | Every external library and vendored file, and its status |
| `third_party/SDL3/` | SDL 3 source, byte-identical to the upstream release tarball and verified against `MANIFEST.sha256` by `scripts/check-vendored.sh`; built on macOS by `third_party/sdl3.make`. Provenance and update procedure in its `README.md` |
| `scripts/pins.sh`, `scripts/pins/` | The renderer pin harness: record frames per scenario, compare renderers, make and publish pull-request evidence (`evidence.py`); user page `doc/100_renderpins.md`, procedure in `docs/plans/metal-port-harness.md` |
| `scripts/metalfx-report.sh`, `scripts/metalfx-report/` | A local HTML report comparing the MetalFX scalers, frame generation and the denoiser on the Mac it runs on; never committed or uploaded; procedure in `docs/runbook/TASK114.md`, self-test `tests/check-metalfx-report.sh` |
| `tests/` | Repo invariants (`check-*.sh`), run by the gate and each runnable on its own; `baselines/` freezes the upstream gaps they tolerate; `pins/` holds the C driver `check-pins-latch.sh` builds; `refrecord/` holds the record-format test `check-refrecord.sh` builds; `metal/` holds the Metal shim host test `check-metal-shim.sh` builds and the `ref_metal` host test `check-ref-metal.sh` builds; `image/` holds the image decoder host test `check-image-decoder.sh` builds; `menu/` holds the video menu host test `check-video-menu.sh` builds |
| `tests/unit/` | The unit-test suite: hand-written runner (`test.h`, `main.c`), one `test_*.c` per area, the client stubs (`stub_client.c`, `stub_client_full.c`) that let `cl_download.c`, the message parser, handshake, entity, HUD and sound-frontend files link without SDL; built and run by `make test` (in the gate); see `doc/085_unittests.md` |
| `tests/pins/unit/` | Unit-test pins: committed expected outputs with a provenance header, rewritten by `make test UPDATE_PINS=1` (not the renderer frame pins of `scripts/pins.sh`) |
| `scripts/coverage.sh`, `scripts/coverage-floor` | Line coverage of the suite with the compiler's own tools, and the floor it must not drop below |
| `stuff/` | Maintainer tools: savegame table generators (`fngen/`, `mmgen/`), map entity fixes (`mapfixes/`), icons, the Dockerfile and the uncrustify style file (`misc/`) |
| `.claude/` | Agent configuration: settings and hooks, path-scoped rules (`rules/`), skills (`skills/`), and `memory/`, a leftover location from older checkouts that is ignored and never committed (`tests/check-no-tracked-memory.sh` keeps it out of the index); agent memory lives outside the tree, see `CLAUDE.md` and [decision 0009](decisions/0009-no-shared-memory.md) |
| `.githooks/` | The commit hook and the push hook (refuses a ref name or commit message carrying the working machine's host name, user name or home path), wired by `scripts/setup.sh`; see [decision 0005](decisions/0005-push-identity-hook.md) |
| `.github/` | CI workflows, the scripts they run (`scripts/`), the PR template, the `main` branch ruleset |
| `CHANGELOG` | Upstream release notes, updated per release |
