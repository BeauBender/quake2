# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Yamagi Quake II (yquake2): a GPL-2 Quake II engine descended from id Software's 2001 source release via Icculus Quake II. Pure C. Game data (`.pak` files) is not in the repo and is required to run anything; see `doc/020_installation.md`. The project is deliberately conservative: gameplay and look must stay unchanged, no breaking changes, backward-compatible savegames and network protocol.

## Build

GNU Make (`Makefile`) is the only build system. On macOS the client and renderers need nothing beyond Xcode's toolchain: SDL3 is built from the vendored source in `third_party/SDL3/` into a static `libSDL3.a` (`third_party/sdl3.make`), with no Homebrew or `pkgconf`. Linux, the BSDs and Windows still need the system SDL3 (found by `pkgconf`). The dedicated server and game module need nothing beyond the system toolchain.

```sh
make                      # all: client, server, game, ref_gl1, ref_gl3, ref_soft -> release/
make client game ref_soft # a subset; targets are independent
make server               # q2ded dedicated server, no SDL
make DEBUG=1              # -O0 -g, output goes to debug/ instead of release/
make ASAN=1               # address sanitizer (implies DEBUG); UBSAN=1 likewise
make config               # print the resolved WITH_* / CFLAGS configuration
make clean                # remove objects; cleanall also removes release/ debug/
release/quake2 +set basedir /path/to/quake2 -portable   # run against real game data
```

Feature switches are `WITH_*` variables (`WITH_SYSTEMWIDE`, `WITH_XDG`, `WITH_EXECINFO`, `WITH_RPATH`). Pass them on the command line or put them in an untracked `config.mk`, which the Makefile includes. `-j` is safe.

The repo's gate is `scripts/test.sh`: it runs the repo invariants in `tests/check-*.sh` (savegame tables equal to a regeneration, no new undocumented cvar or command, removed dependencies stay removed, Markdown links resolve, the vendored SDL3 tree matches its manifest (`tests/check-vendored.sh`, which runs `scripts/check-vendored.sh`), the pin harness selftest and its command-line latch, the renderer-API record format round-trip, which need `cc`), force-rebuilds `q2ded` and the game module (the targets that need no SDL), then builds and runs the unit-test suite with `make test`. The suite (`tests/unit/`, see `doc/085_unittests.md`) is a hand-written runner (`tests/unit/test.h`) linked against the real `q2ded` objects plus the client files `cl_download.c`, `cl_parse.c`, `cl_network.c`, `cl_entities.c`, `cl_screen.c` and `sound/sound.c` (the rest of the client is stubbed); it needs no SDL and no game data, generates its paks and maps itself, and compares against text pins in `tests/pins/unit/` (`make test UPDATE_PINS=1` rewrites them; review the diff). Write the failing test for a change in `src/common`, `src/server` or one of those client files there first. `scripts/coverage.sh` is the separate, slower coverage check (toolchain-only: `xcrun llvm-cov` or `gcov`) against the floor in `scripts/coverage-floor`. It must pass on every commit. Each invariant also runs standalone, e.g. `tests/check-savegame-tables.sh`; while iterating, run the single check, `make test TEST_ARGS=<name>`, or build the one target you touched (`make game`), and the whole gate before a PR. CI (`.github/workflows/`) builds on macOS only (Apple platforms are the only ones built and tested, record 0008) and runs `make test` and `scripts/coverage.sh` there, plus CodeQL and Cppcheck on Linux runners. Beyond that, verification is building the affected targets, ideally with `ASAN=1`, and running the game against real data with the command above (`-portable` keeps config and saves next to the binary instead of `~/.local/share/YamagiQ2` with XDG, or `~/.yq2` without).

## Architecture

The engine is split into separately built, dynamically loaded modules with versioned C APIs. Changing a struct that crosses one of these boundaries breaks binary compatibility with mods and third-party renderers.

- **Engine executable** (`quake2`, `q2ded`): `src/common` (cvars, command buffer, filesystem, network messages, collision BSP), `src/server`, `src/client`, and `src/backends/{unix,windows,generic}` for OS specifics. `src/common/header/common.h` and `src/common/shared/shared.c` are shared with the other modules.
- **Game module** (`baseq2/game.{so,dylib,dll}`, `src/game`): all gameplay, monsters, weapons, savegame serialization. Loaded by `Sys_GetGameAPI` (`src/backends/*/system.c`) from the first filesystem search path that contains it; it exports `GetGameAPI` and the two sides exchange `game_import_t`/`game_export_t` (`src/game/header/game.h`, `GAME_API_VERSION`). Addons (xatrix, rogue, ctf) are separate repos that implement this same API, so the engine side of it is effectively frozen.
- **Renderers** (`ref_gl1`, `ref_gl3`, `ref_soft` in `src/client/refresh/`): selected by the `vid_renderer` cvar, loaded from the binary's own directory by `VID_LoadRenderer` (`src/client/vid/vid.c`), exporting `GetRefAPI` (`src/client/vid/header/ref.h`, `API_VERSION`). `src/client/refresh/files/` holds the loaders shared by all renderers (BSP, MD2/MD5 models, PCX/WAL/TGA/PNG via stb_image). The software renderer is the most faithful to the 2001 code and has no OpenGL dependency.
- **Sound** (`src/client/sound/`): `sound.c` is the frontend; `sdl.c` is the software mixer backend. OGG music is decoded by the vendored `stb_vorbis.h`.

Things that are not obvious from a single file:

- **SDL3 is the only SDL.** Input, window and gamepad handling live in `src/client/input/sdl3.c` and `src/client/vid/glimp_sdl3.c`; SDL2 support and the `WITH_SDL3` switch were removed, and `tests/check-removed-deps.sh` keeps them out (any spelling of `SDL2`, `SDL_VERSION_ATLEAST(2`, a bare `<SDL.h>` include) in `src`, `Makefile`, `.github` and `stuff/misc`.
- **Savegames serialize function pointers by name.** `src/game/savegame/savegame.c` maps pointers to names through generated tables in `src/game/savegame/tables/`. Adding, removing or renaming any game function that is stored in an entity field (think functions, touch, use, pain, die, `mmove_t` monster move tables) requires regenerating those tables with the scripts in `stuff/fngen/` and `stuff/mmgen/` (see their READMEs), or saves will fail to load. The format version is `SAVEGAMEVER` in that file.
- **Cvars and console commands are the configuration and scripting surface.** `Cvar_Get` with `CVAR_ARCHIVE` persists to `<writedir>/config.cfg`; `CVAR_USERINFO` is sent to servers; `CVAR_SERVERINFO` is advertised. New cvars and commands must be documented in `doc/040_cvarlist.md` and `doc/050_commands.md`.
- **Filesystem search paths** (`src/common/filesystem.c`): first match wins. The `game` (mod) directories are searched before all `baseq2` directories. For each of the two, the raw directories are searched in this order: the user's home directory (absent with `-portable`), the binary's directory, `-datadir`, then the systemwide and CD directories if set. Within one directory the `.pak` files come before the loose files (`FS_AddDirToSearchPath` pushes the directory, then the paks, onto the front of the list): the paks without a number first, in an order that depends on the filesystem, then the numbered paks from `pak99` down to `pak0`, then the loose files. So a loose file does not override a pak in its own directory, only paks in directories searched after it. The home directory is where everything is written; `FS_Gamedir()` is the write location. Map-fix entity overrides in `stuff/mapfixes/` are loaded as `<map>@<crc>.ent`.
- **Vendored third-party code** lives in `src/client/refresh/files/stb_image*.h`, `src/client/sound/header/stb_vorbis.h`, `src/client/refresh/gl3/glad/`, `src/client/refresh/gl3/header/{HandmadeMath,DG_dynarr}.h`, `src/client/input/gyro.c`, and SDL 3 in `third_party/SDL3/` (byte-identical to the upstream release tarball; never edit it, its manifest `third_party/SDL3/MANIFEST.sha256` is checked by `scripts/check-vendored.sh`; update procedure in `third_party/SDL3/README.md`). None of it is switchable by a `WITH_*` flag. On macOS the client executable links SDL statically and exports it; the renderer dylibs link no SDL and bind their `SDL_*` calls to the executable's copy at load time.

## Security-relevant boundaries

Untrusted input enters through: network messages from servers (`src/client/cl_parse.c`) and clients (`src/server/sv_user.c`), downloaded files (`cl_download.c`, `CL_DownloadFilter` rejects `..`, leading `.`/`/`, `:` and library extensions), and every asset loader under `src/client/refresh/files/` and `src/common/collision.c`. The protocol lets servers execute arbitrary console commands on clients (`svc_stufftext` goes straight to `Cbuf_AddText`); this is by design and not filtered. Any new file-writing console command must validate its path argument against `..` itself; `FS_CreatePath` refuses such paths but `Q_fopen` does not.

## Conventions

- Match the style of the file being edited (tabs, braces on their own lines, `/* */` comments, return type on its own line in definitions). `stuff/misc/uncrustify.cfg` describes the house style.
- Small, distinct commits. Changes to protocol, savegame format, or module APIs need a very strong reason.
- Documentation lives in `doc/` and ships with releases; `CHANGELOG` is updated per release, not per commit.

## Working rules for agents

This repository is public. Issues, pull requests, commits and files are about quake2 only.

Non-negotiables:

- Never commit on `main`. Work in a worktree on a branch; changes land through a pull request.
- Never skip, weaken or bypass the gate (`scripts/test.sh`), and never push with `--no-verify`. A red gate means the work is not done.
- Never change the savegame format, the network protocol, or a struct that crosses a module API without a card that asks for it (see Architecture).
- Never install packages or add a dependency without the owner's approval. A missing tool is reported, not installed.
- Do not commit secrets, game data (`.pak` files) or build output.

When compacting, always preserve the list of modified files, the gate command (`scripts/test.sh`) and any open failures.

Cards are GitHub Issues and work lands through pull requests: use `gh issue list`, `gh pr create` and `gh pr checks` rather than the web UI.

Where things live: `docs/README.md` maps the tree; decisions are in `docs/decisions/`; plans in `docs/plans/`. Rules that load only for the files they cover are in `.claude/rules/`, and the `savegame-tables` skill is in `.claude/skills/`. `scripts/setup.sh` checks the toolchain and wires the commit hook in `.githooks/`.
