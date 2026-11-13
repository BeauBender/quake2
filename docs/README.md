# Repository map

Where things live, and where to start. `doc/` is the user documentation that
ships with releases; `docs/` (this directory) is for working on the code.

## Start here

- `CLAUDE.md` (also `AGENTS.md`): build commands, the gate, architecture,
  security boundaries and the working rules for agents.
- `scripts/setup.sh`: checks the toolchain and wires the commit hook.
- `scripts/test.sh`: the gate. Every commit must pass it. It ends with `make test`, the unit-test suite.

## Source (`src/`)

| Path | What it is |
|---|---|
| `src/common/` | Engine core shared by client and server: cvars, command buffer, filesystem, network channel and messages, collision BSP |
| `src/server/` | Server side of the engine, linked into `quake2` and `q2ded` |
| `src/client/` | Client: parsing server messages (`cl_parse.c`), downloads (`cl_download.c`), input, menus, sound, video |
| `src/client/refresh/` | Renderers built as separate modules: `gl1`, `gl3`, `soft`, plus `files/` with the asset loaders they share |
| `src/game/` | The game module: gameplay, monsters, weapons, savegames (`savegame/`) |
| `src/backends/` | OS specifics: `unix`, `windows`, `generic` |
| `src/win-wrapper/` | Windows launcher wrapper |

## Everything else

| Path | What it is |
|---|---|
| `doc/` | User documentation, numbered in reading order; cvars in `040_cvarlist.md`, commands in `050_commands.md` |
| `docs/plans/` | Plan anchors for multi-card work; cards cite them |
| `docs/decisions/` | Decision records: what was decided, why, and what it costs |
| `docs/audits/` | Audit write-ups: the [small vendored files](audits/small-vendored.md), the [asset-loader malformed-input audit](audits/asset-loaders.md) and the [network message parser audit](audits/network-parsers.md) |
| `docs/analysis/` | Measured inventories of existing code that plans and cards cite |
| `docs/DEPENDENCIES.md` | Every external library and vendored file, and its status |
| `scripts/pins.sh`, `scripts/pins/` | The renderer pin harness: record frames per scenario, compare renderers; user page `doc/100_renderpins.md`, procedure in `docs/plans/metal-port-harness.md` |
| `tests/` | Repo invariants (`check-*.sh`), run by the gate and each runnable on its own; `baselines/` freezes the upstream gaps they tolerate; `pins/` holds the C driver `check-pins-latch.sh` builds; `refrecord/` holds the record-format test `check-refrecord.sh` builds |
| `tests/unit/` | The unit-test suite: hand-written runner (`test.h`, `main.c`), one `test_*.c` per area, the client-state stubs for `cl_download.c`; built and run by `make test` (in the gate); see `doc/085_unittests.md` |
| `tests/pins/unit/` | Unit-test pins: committed expected outputs with a provenance header, rewritten by `make test UPDATE_PINS=1` (not the renderer frame pins of `scripts/pins.sh`) |
| `scripts/coverage.sh`, `scripts/coverage-floor` | Line coverage of the suite with the compiler's own tools, and the floor it must not drop below |
| `stuff/` | Maintainer tools: savegame table generators (`fngen/`, `mmgen/`), map entity fixes (`mapfixes/`), icons, the Dockerfile and the uncrustify style file (`misc/`) |
| `.claude/` | Agent configuration: settings and hooks, path-scoped rules (`rules/`), skills (`skills/`), shared memory (`memory/`) |
| `.githooks/` | The commit hook and the push hook (refuses a ref name or commit message carrying the working machine's host name, user name or home path), wired by `scripts/setup.sh`; see [decision 0003](decisions/0003-no-local-identity.md) |
| `.github/` | CI workflows, the PR template, the `main` branch ruleset |
| `CHANGELOG` | Upstream release notes, updated per release |
