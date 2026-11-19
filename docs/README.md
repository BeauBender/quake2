# Repository map

Where things live, and where to start. `doc/` is the user documentation that
ships with releases; `docs/` (this directory) is for working on the code.

## Start here

- `CLAUDE.md` (also `AGENTS.md`): build commands, the gate, architecture,
  security boundaries and the working rules for agents.
- `scripts/setup.sh`: checks the toolchain and wires the commit and push hooks.
- `scripts/test.sh`: the gate. Every commit must pass it. It runs
  `tests/check-*.sh` through `scripts/gate-checks.sh`, `GATE_JOBS` at a time
  (default half the CPUs; `GATE_JOBS=1` runs them one after another), each
  check's log printed whole in the glob's order with its wall time (self-test
  `tests/check-gate-parallel.sh`), then builds `q2ded` and the game module and
  ends with `make test`, the unit-test suite.
- Platforms: only Apple platforms are built and tested; other platforms are
  unmaintained (`docs/decisions/0008-apple-platforms-only.md`). CI runs the
  macOS test build, CodeQL and Cppcheck. Both CodeQL analyses (C and
  Actions) run on every push to `main`; a pull request skips them only when
  it is documentation only, every changed file being Markdown under `doc/` or
  `docs/` or at the top level, and anything else runs them
  (`.github/scripts/codeql-changes.sh`, tested by
  `tests/check-codeql-changes.sh`). Cppcheck always runs. A newer push to a
  pull request cancels the older run (`docs/runbook/FIX66.md`).
- Card labels: `.github/workflows/card-close-labels.yml` runs when the owner
  closes an issue, by any route (the ship path, `gh issue close`, the web
  UI), and edits it only if it is still closed when the run starts. A card
  closed as completed, and not `state:promoted`, gets `state:done`; any
  closed card loses `state:backlog`, `state:staged`, `state:in-progress` and
  `state:review`; an issue with no `state:*` label is left alone. It has
  `issues: write` only, uses no action, and takes nothing from the event but
  the issue number and close reason. A close made with another workflow's
  `GITHUB_TOKEN` starts no workflow, so it is not covered.
  `tests/check-card-close-labels.sh` checks its shape with an allow-list
  lint and runs its whole run block against a stub `gh`, offline
  ([FIX108](runbook/FIX108.md)).

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
| `docs/plans/` | Plan anchors for multi-card work; cards cite them. The presentation modes of `ref_metal` (stock, enhanced, retro, RT-primary): [`docs/plans/presentation-modes.md`](plans/presentation-modes.md). HDR output on EDR displays (`r_hdr`): [`docs/plans/hdr-output.md`](plans/hdr-output.md) |
| `docs/runbook/` | What each card measured, one file per card (`<CARD>.md`). A card writes its results there and adds only its one line to [`docs/runbook/INDEX.md`](runbook/INDEX.md), which is merged by union; the shared runbook, `docs/plans/metal-port-harness.md`, keeps the method; see its [README](runbook/README.md) |
| `docs/decisions/` | Decision records: what was decided, why, and what it costs |
| `docs/audits/` | Audit write-ups: the [small vendored files](audits/small-vendored.md), the [asset-loader malformed-input audit](audits/asset-loaders.md) and the [network message parser audit](audits/network-parsers.md) |
| `docs/analysis/` | Measured inventories of existing code that plans and cards cite |
| `docs/reviews/` | Architecture reviews over a window of merges (`/dev-loop:arch-review`), one file per review; each finding has a stable `AR-##` id that its card cites |
| `docs/DEPENDENCIES.md` | Every external library and vendored file, and its status |
| `third_party/SDL3/` | SDL 3 source, byte-identical to the upstream release tarball and verified against `MANIFEST.sha256` by `scripts/check-vendored.sh`; built on macOS by `third_party/sdl3.make`. Provenance and update procedure in its `README.md` |
| `scripts/ios-sim.sh` | The iOS simulator app on a throwaway device: `smoke` builds it with synthetic data and checks from the engine's log and frame that it reaches the console; see `doc/020_installation.md` |
| `scripts/ios/` | `smokepak.py`, the synthetic pak of the iOS smoke run (a palette, a console font with transparent glyphs and a console background, made from formulas) |
| `stuff/ios/` | `Info.plist.in`, the iOS app bundle's template, filled in by `make ios` |
| `scripts/pins.sh`, `scripts/pins/` | The renderer pin harness: record frames per scenario, compare renderers, make and publish pull-request evidence (`evidence.py`); user page `doc/100_renderpins.md`, procedure in `docs/plans/metal-port-harness.md` |
| `scripts/bench/` | The bench harness: `bench.sh`, its one entry point (a host-wide lock for heavy work, the host token and build-inputs id, a per-host store of measured baselines outside the repository, drift and compare); `core/` names no game, `adapters/` and `cases/` do (the `null` ones are for the self-test `tests/check-bench.sh`); plan [`docs/plans/bench-harness.md`](plans/bench-harness.md), commands in `doc/085_unittests.md` |
| `scripts/metalfx-report.sh`, `scripts/metalfx-report/` | A local HTML report comparing the MetalFX scalers, frame generation and the denoiser on the Mac it runs on; never committed or uploaded; procedure in `docs/runbook/TASK114.md`, self-test `tests/check-metalfx-report.sh` |
| `tests/` | Repo invariants (`check-*.sh`), run by the gate and each runnable on its own; `baselines/` freezes the upstream gaps they tolerate; `pins/` holds the C driver `check-pins-latch.sh` builds; `refrecord/` holds the record-format test `check-refrecord.sh` builds; `metal/` holds the Metal shim host test `check-metal-shim.sh` builds and the `ref_metal` host test `check-ref-metal.sh` builds; `image/` holds the image decoder host test `check-image-decoder.sh` builds; `menu/` holds the video menu host test `check-video-menu.sh` builds |
| `tests/unit/` | The unit-test suite: hand-written runner (`test.h`, `main.c`), one `test_*.c` per area, the client stubs (`stub_client.c`, `stub_client_full.c`) that let `cl_download.c`, the message parser, handshake, entity, HUD and sound-frontend files link without SDL; built and run by `make test` (in the gate); see `doc/085_unittests.md` |
| `tests/pins/unit/` | Unit-test pins: committed expected outputs with a provenance header, rewritten by `make test UPDATE_PINS=1` (not the renderer frame pins of `scripts/pins.sh`) |
| `scripts/coverage.sh`, `scripts/coverage-floor` | Line coverage of the suite with the compiler's own tools, and the floor it must not drop below |
| `stuff/` | Maintainer tools: savegame table generators (`fngen/`, `mmgen/`), map entity fixes (`mapfixes/`), icons, the Dockerfile and the uncrustify style file (`misc/`) |
| `.claude/` | Agent configuration: settings and hooks, path-scoped rules (`rules/`), skills (`skills/`), and `memory/`, a leftover location from older checkouts that is ignored and never committed (`tests/check-no-tracked-memory.sh` keeps it out of the index); agent memory lives outside the tree, see `CLAUDE.md` and [decision 0009](decisions/0009-no-shared-memory.md) |
| `.githooks/` | The commit hook (runs the identity and memory checks on every commit, refuses staged secrets) and the push hook (refuses a ref name, commit, tag, or a file or file name a pushed commit adds that carries the working machine's host name, user name or home path, under any ref name), wired by `scripts/setup.sh`; self-tests `tests/check-pre-commit-hook.sh` and `tests/check-pre-push-hook.sh`; see [decision 0005](decisions/0005-push-identity-hook.md) |
| `.github/` | CI workflows, the scripts they run (`scripts/`), the PR template, the `main` branch ruleset |
| `CHANGELOG` | Upstream release notes, updated per release |
