# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Yamagi Quake II (yquake2): a GPL-2 Quake II engine descended from id Software's 2001 source release via Icculus Quake II. Pure C. Game data (`.pak` files) is not in the repo and is required to run anything; see `doc/020_installation.md`. The project is deliberately conservative: gameplay and look must stay unchanged, no breaking changes, backward-compatible savegames and network protocol.

## Build

GNU Make is the primary build (`Makefile`); CMake (`CMakeLists.txt`) is the alternative. Both produce the same set of binaries. On macOS the client needs `brew install sdl3 openal-soft` (or `sdl2`); the dedicated server and game module need nothing beyond the system toolchain.

```sh
make                      # all: client, server, game, ref_gl1, ref_gl3, ref_gles3, ref_soft -> release/
make client game ref_soft # a subset; targets are independent
make server               # q2ded dedicated server, no SDL
make DEBUG=1              # -O0 -g, output goes to debug/ instead of release/
make ASAN=1               # address sanitizer (implies DEBUG); UBSAN=1 likewise
make config               # print the resolved WITH_* / CFLAGS configuration
make clean                # remove objects; cleanall also removes release/ debug/
```

Feature switches are `WITH_*` variables (`WITH_CURL`, `WITH_OPENAL`, `WITH_SDL3`, `WITH_SYSTEM_MINIZIP`, `WITH_SYSTEMWIDE`, `WITH_XDG`, `WITH_EXECINFO`, `WITH_RPATH`). Pass them on the command line or put them in an untracked `config.mk`, which the Makefile includes. `-j` is safe.

There is no test suite. CI (`.github/workflows/`) only compiles on each platform plus CodeQL/Coverity. Verification is building the affected targets, ideally with `ASAN=1`, and running the game against real data: `release/quake2 +set basedir /path/to/quake2 -portable` (`-portable` keeps config and saves next to the binary instead of `~/.local/share/yquake2` or `~/.yq2`).

## Architecture

The engine is split into separately built, dynamically loaded modules with versioned C APIs. Changing a struct that crosses one of these boundaries breaks binary compatibility with mods and third-party renderers.

- **Engine executable** (`quake2`, `q2ded`): `src/common` (cvars, command buffer, filesystem, network messages, collision BSP), `src/server`, `src/client`, and `src/backends/{unix,windows,generic}` for OS specifics. `src/common/header/common.h` and `src/common/shared/shared.c` are shared with the other modules.
- **Game module** (`baseq2/game.so|dylib|dll`, `src/game`): all gameplay, monsters, weapons, savegame serialization. Loaded by `Sys_GetGameAPI` (`src/backends/*/system.c`) from the first filesystem search path that contains it; it exports `GetGameAPI` and the two sides exchange `game_import_t`/`game_export_t` (`src/game/header/game.h`, `GAME_API_VERSION`). Addons (xatrix, rogue, ctf) are separate repos that implement this same API, so the engine side of it is effectively frozen.
- **Renderers** (`ref_gl1`, `ref_gl3`, `ref_gles1`, `ref_gles3`, `ref_soft` in `src/client/refresh/`): selected by the `vid_renderer` cvar, loaded from the binary's own directory by `VID_LoadRenderer` (`src/client/vid/vid.c`), exporting `GetRefAPI` (`src/client/vid/header/ref.h`, `API_VERSION`). `src/client/refresh/files/` holds the loaders shared by all renderers (BSP, MD2/MD5 models, PCX/WAL/TGA/PNG via stb_image). The software renderer is the most faithful to the 2001 code and has no OpenGL dependency.
- **Sound** (`src/client/sound/`): `sound.c` is the frontend; `sdl.c` (software mixer) and `openal.c` + `qal.c` (OpenAL loaded at runtime via `dlopen`) are the backends. OGG music is decoded by the vendored `stb_vorbis.h`.
- **HTTP downloads** (`src/client/curl/`): libcurl is also `dlopen`ed at runtime, never linked; `cl_download.c` holds the protocol-level download logic and the path filter.

Things that are not obvious from a single file:

- **SDL2 and SDL3 are parallel implementations, not one with ifdefs.** `src/client/input/sdl2.c` / `sdl3.c` and `src/client/vid/glimp_sdl2.c` / `glimp_sdl3.c` are near-duplicates. A change to input, window, or gamepad handling must be made in both.
- **Savegames serialize function pointers by name.** `src/game/savegame/savegame.c` maps pointers to names through generated tables in `src/game/savegame/tables/`. Adding, removing or renaming any game function that is stored in an entity field (think functions, touch, use, pain, die, `mmove_t` monster move tables) requires regenerating those tables with the scripts in `stuff/fngen/` and `stuff/mmgen/` (see their READMEs), or saves will fail to load. The format version is `SAVEGAMEVER` in that file.
- **Cvars and console commands are the configuration and scripting surface.** `Cvar_Get` with `CVAR_ARCHIVE` persists to `config.cfg`; `CVAR_USERINFO` is sent to servers; `CVAR_SERVERINFO` is advertised. New cvars and commands must be documented in `doc/040_cvarlist.md` and `doc/050_commands.md`.
- **Filesystem search paths** (`src/common/filesystem.c`): `basedir`, then `game` dir, each with loose files overriding `.pak`/`.pk3` contents; the user's home directory is pushed on top and is where everything is written. `FS_Gamedir()` is the write location. Map-fix entity overrides in `stuff/mapfixes/` are loaded as `<map>@<crc>.ent`.
- **Vendored third-party code** lives in `src/common/unzip/` (minizip + miniz), `src/client/refresh/files/stb_image*.h`, `src/client/vid/header/stb_image_write.h`, `src/client/sound/header/stb_vorbis.h`, `src/client/refresh/gl3/{glad,HandmadeMath,DG_dynarr}`, `src/client/input/gyro.c`. None of it is switchable by a `WITH_*` flag.

## Security-relevant boundaries

Untrusted input enters through: network messages from servers (`src/client/cl_parse.c`) and clients (`src/server/sv_user.c`), downloaded files (`cl_download.c`, `CL_DownloadFilter` rejects `..`, leading `.`/`/`, `:` and library extensions), and every asset loader under `refresh/files/` and `src/common/collision.c`. The protocol lets servers execute arbitrary console commands on clients (`svc_stufftext` goes straight to `Cbuf_AddText`); this is by design and not filtered. Any new file-writing console command must validate its path argument against `..` itself; `FS_CreatePath` refuses such paths but `Q_fopen` does not.

## Conventions

- Match the style of the file being edited (tabs, braces on their own lines, `/* */` comments, return type on its own line in definitions). `stuff/misc/uncrustify.cfg` describes the house style.
- Small, distinct commits. Changes to protocol, savegame format, or module APIs need a very strong reason.
- Documentation lives in `doc/` and ships with releases; `CHANGELOG` is updated per release, not per commit.
