# Repository map

Where things live, and where to start. `doc/` is the user documentation that
ships with releases; `docs/` (this directory) is for working on the code.

## Start here

- `CLAUDE.md` (also `AGENTS.md`): build commands, the gate, architecture,
  security boundaries and the working rules for agents.
- `scripts/setup.sh`: checks the toolchain and wires the commit hook.
- `scripts/test.sh`: the gate. Every commit must pass it.

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
| `docs/audits/` | Audit write-ups (for example the small vendored files) |
| `docs/DEPENDENCIES.md` | Every external library and vendored file, and its status |
| `tests/` | Repo invariants (`check-*.sh`), run by the gate and each runnable on its own; `baselines/` freezes the upstream gaps they tolerate |
| `stuff/` | Maintainer tools: savegame table generators (`fngen/`, `mmgen/`), map entity fixes (`mapfixes/`), icons, the Dockerfile and the uncrustify style file (`misc/`) |
| `.claude/` | Agent configuration: settings and hooks, path-scoped rules (`rules/`), skills (`skills/`), shared memory (`memory/`) |
| `.githooks/` | The commit hook, wired by `scripts/setup.sh` |
| `.github/` | CI workflows, the PR template, the `main` branch ruleset |
| `CHANGELOG` | Upstream release notes, updated per release |
