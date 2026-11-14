# Dependencies

This file is the fork's dependency manifest. It lists the system libraries the build links or loads, and every third-party file that is carried inside the source tree, with its upstream, version (when the file states one), license and sha256. Hashes are `shasum -a 256` of the file as committed. Update this file whenever a vendored file is added, replaced or removed. Audit findings for the small files are in `docs/audits/small-vendored.md`.

## System libraries

| Library | Used by | How it is used | Status |
|---------|---------|----------------|--------|
| SDL3 | client: window, input, gamepad and gyro, audio (`src/client/input/sdl3.c`, `src/client/vid/glimp_sdl3.c`, `src/client/sound/sdl.c`); renderers: GL context, window, `SDL_Renderer` | macOS: built from the vendored source (see "Vendored libraries" below) and linked statically; no system, Homebrew or `pkgconf` SDL is used. Linux, the BSDs and Windows: still the system SDL3, linked at build time via `pkgconf` sdl3. The dedicated server (`q2ded`) and the game module do not need it. | Kept. SDL2 support was removed by TASK13; SDL3 is the only SDL. Vendored and built from source on macOS by TASK14; the other platforms follow in a later card. |
| OpenAL | formerly `src/client/sound/openal.c` and `qal.c` | Was loaded at runtime with `dlopen`. | Removed by TASK4; SDL audio is the only sound backend. |
| libcurl | `src/client/curl/` (HTTP downloads) | Loaded at runtime with `dlopen`, never linked. | Removed by TASK3. |
| bcrypt (Windows) | `src/backends/windows/system.c` `Sys_GetRandomBytes` | Linked at build time (Makefile LDLIBS) | Kept: OS random source |
| OpenGL (system framework / libGL) | `ref_gl1`, `ref_gl3` via the vendored glad loader and the `qgl` function table | Function pointers for the GL renderers, resolved through the vendored glad loader and the `qgl` table. | Kept for now; superseded by the Metal renderer plan (`docs/plans/metal-renderer.md`) |
| Metal, QuartzCore, Foundation (macOS system frameworks) and the Metal shader compiler (`xcrun metal`, a separate Xcode component) | `ref_gl1` when built with `WITH_METAL_SHIM=yes` (Makefile `WITH_METAL_SHIM` rules), and `ref_metal` (Makefile `ref_metal` target, macOS only, not part of `make`) | The three frameworks are linked at build time; the Metal toolchain is needed at build time only (`ref_metal` compiles no shaders yet; `tests/check-ref-metal.sh` builds it only where `xcrun -f metal` answers). Off by default; a default build of `ref_gl1` links none of them, and `make` does not build `ref_metal`. The client links Metal and QuartzCore through the vendored SDL (see the SDL 3 row). | Kept, optional: off by default. |

## Vendored libraries

### SDL3

| Field | Value |
|-------|-------|
| Path | `third_party/SDL3/` (upstream files, unmodified) plus the fork's `third_party/SDL3/README.md` and `third_party/SDL3/MANIFEST.sha256`; build rules in `third_party/sdl3.make` |
| Upstream | https://github.com/libsdl-org/SDL |
| Version | SDL 3.4.20, tag `release-3.4.20` |
| Upstream commit | `f00dc919ca35b9756316e86c4777392de442f9b2` (the tag is lightweight and points at this commit; `REVISION.txt` reads `release-3.4.20-0-gf00dc919c`) |
| Source | release asset `SDL3-3.4.20.tar.gz`, https://github.com/libsdl-org/SDL/releases/tag/release-3.4.20, fetched once on 2026-11-13 under INFORM4 |
| Tarball sha256 | `f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f` (computed locally; equal to the `digest` GitHub's release API reports for the asset) |
| Signature | `SDL3-3.4.20.tar.gz.sig` sha256 `776200b5c71f8c0168c0e89d8fb78103623dc15e284e9daf0a0eceb4e43d5533` (equal to the API digest). The signature itself was **not verified**: `gpg` is not installed on the import machine. |
| License | zlib (`third_party/SDL3/LICENSE.txt`, reproduced in the top-level `LICENSE`). Some bundled parts carry their own notices in the files (e.g. `src/hidapi`, `src/video/yuv2rgb`, the Khronos headers in `src/video/khronos`). |
| Subset | `src/` without `src/hidapi/documentation/` and `src/hidapi/testgui/`; `include/`; `LICENSE.txt`, `REVISION.txt`, `WhatsNew.txt`, `CREDITS.md`. 1431 files, about 31 MB. Not vendored: tests, examples, docs, CMake/Xcode/Visual Studio/Android project trees, `wayland-protocols/`, upstream's `README.md`, `INSTALL.md`, `AGENTS.md`, `CLAUDE.md`. The reasons are in `third_party/SDL3/README.md`: the library builds from `src/` and `include/`; the rest is upstream's own build systems, tests and documentation, and the two hidapi directories hold only images and a test app. |
| Verification | `third_party/SDL3/MANIFEST.sha256` lists the sha256 of every vendored file, generated from the extracted tarball. `scripts/check-vendored.sh` (run by the gate through `tests/check-vendored.sh`, offline) fails on a missing or changed file, an unlisted file, a manifest that does not match the pin below, and on `third_party/SDL3` being a symlink or anything under it that is neither a regular file nor a directory (symlinks, including symlinked directories, devices, FIFOs, sockets). Not checked: file modes (the executable bit), and anything outside `third_party/SDL3`. The manifest and the pin below are an in-repository trust root: a diff that changes `MANIFEST.sha256` or the pin line passes the check by construction, so the reviewer must re-derive it from the upstream tarball. |
| Manifest pin | `third_party/SDL3/MANIFEST.sha256` sha256: `1394662cc437fc54ba80d311b40cabb9310df9c777442ae2a65e20b6795f67aa` |
| Built on | macOS only, as a static `libSDL3.a` (`build/<release\|debug>/sdl3/`), with the stock `include/build_config/SDL_build_config_macos.h` and the source list, header paths, ARC and frameworks of the tarball's Xcode project. Linux, BSD and Windows builds still link the system SDL3 found by `pkgconf`. |
| Linking | The client executable links all of `libSDL3.a` (`-force_load`) and exports the SDL API. The renderer dylibs contain no SDL; their `_SDL_*` imports are bound at load time to the executable's copy (`-Wl,-U,<symbol>`), so one SDL instance serves the process. |
| Update procedure | `third_party/SDL3/README.md`, "Update procedure". |
| Note | SDL's dynamic API (`src/dynapi`) stays enabled, as upstream ships it: setting the environment variable `SDL3_DYNAMIC_API` to a library path makes the statically linked SDL forward every call to that library. It cannot be disabled by a compiler flag: `SDL_dynapi.h` refuses a command-line define. It can be neutralised without touching vendored files by clearing the `SDL3_DYNAMIC_API` environment variable in the engine's `main` before the first SDL call; doing so is left to a follow-up card. |

## In-tree third-party files

Audited items (small files that stay) first, then the larger vendored files that exist in the tree. Version is given only where the file states one. Status names the card that removes the file or decides whether it stays.

| Path | Upstream project and URL as stated in the file | Version | License | sha256 | Status |
|------|-----------------------------------------------|---------|---------|--------|--------|
| `src/client/input/gyro.c` | Julian "Jibb" Smart, Nicolas Lessard, ceski (gyro helper lineage); no URL in the file | not stated | MIT | `7a6080abf74a7ea286a5e58a9efbbbd94cb3fed012440b541ec6755d11d9ef2b` | Audited, KEEP (TASK15) |
| `src/client/input/header/gyro.h` | Julian "Jibb" Smart, Nicolas Lessard, ceski (gyro helper lineage); no URL in the file | not stated | MIT | `29385259dc66177c07ccb53fdc6059fef41cda4c49a5d81f68226c42cbafd60b` | Audited with gyro.c, KEEP (TASK15) |
| `src/common/md4.c` | RFC 1320 implementation by Steven Fuller, http://www.faqs.org/rfcs/rfc1320.html | not stated | Public domain | `c7c312a7e82267760b3c46b2e3333494bd8efdd2295b813d054bbb456406724e` | Audited, KEEP (TASK15) |
| `src/common/shared/rand.c` | KISS PRNG by Marsaglia, via Shinobu (zuttobenkyou.wordpress.com), modified by Yamagi | not stated | Public domain | `ae7ed8037c3b4e33913b57d5796701ba3bb275b96e633b2e2294f47fae20786f` | Audited, KEEP (TASK15); modified by FIX4 |
| `src/common/glob.c` | Global string matcher carried in id Software's Quake II source; no separate URL in the file | not stated | GPL-2-or-later (id Software header) | `aadda3d054ed626abbcecb032681116e015d89dbfe1d8483609c20a755ea9a1b` | Audited, KEEP (TASK15); patched by FIX5 |
| `src/game/savegame/tables/clientfields.h` | Project file (id Software, Yamagi); hand-written | n/a | GPL-2-or-later | `cc1e6c2e0f3b6c141e8f2a56312b8b16f9f7d8747c13481a99f254e4f4e60a68` | Audited, KEEP (TASK15) |
| `src/game/savegame/tables/entfields.h` | Project file; hand-written | n/a | GPL-2-or-later | `52f0079e81e4c93e927a5eb8f6661a2568218c63b12e484a62629c54477a79b7` | Audited, KEEP (TASK15) |
| `src/game/savegame/tables/gamefunc_decs.h` | Project file; generated by `stuff/fngen/fngenprotos.sh`, hand-edited | n/a | GPL-2-or-later | `5dbad7207dc70dfa35ebdb12d05b43c346d5c37e38317fd766e8820fbab45068` | Audited, KEEP (TASK15) |
| `src/game/savegame/tables/gamefunc_list.h` | Project file; generated by `stuff/fngen/fngentables.sh`, hand-edited | n/a | GPL-2-or-later | `0c68b0bac71af22c10c1165c117fc475e07df642349c0c7b9a2a78b617bf14f4` | Audited, KEEP (TASK15); `mv_end` completed by FIX6 |
| `src/game/savegame/tables/gamemmove_decs.h` | Project file; generated by `stuff/mmgen/mmproto.sh` | n/a | GPL-2-or-later | `a87a6112bb7d2aa0948e0ec1d6aed09aa155e0d6372a8368c172e0a0228851a4` | Audited, KEEP (TASK15) |
| `src/game/savegame/tables/gamemmove_list.h` | Project file; generated by `stuff/mmgen/mmtable.sh` | n/a | GPL-2-or-later | `c387c76350722dd3cc170ce4ef5b7b7fb25caacc9dffcef09bf3344326d2f8f2` | Audited, KEEP (TASK15) |
| `src/game/savegame/tables/levelfields.h` | Project file; hand-written | n/a | GPL-2-or-later | `d6662ea7c8bc831b110cb8dab3c5e8fa6b6087967972a8027fff63b1c0bafb7e` | Audited, KEEP (TASK15) |
| `src/game/savegame/tables/spawnfunc_decs.h` | Project file; hand-written | n/a | GPL-2-or-later | `16197400c4a940b363ac680ca31cd44a5ba134344491ef1420ba74c6ed18dfbf` | Audited, KEEP (TASK15) |
| `src/game/savegame/tables/spawnfunc_list.h` | Project file; hand-written | n/a | GPL-2-or-later | `1d9cbed8dcc8fe20e4905f917837ab520a0f0f90d1b4e2bc1dd4c0cd46c85c4e` | Audited, KEEP (TASK15) |
| `src/game/savegame/tables/stfields.h` | Project file; hand-written | n/a | GPL-2-or-later | `e5f8fe9accf0c61038db32de73a94584b5566b624070eccac7755f9ba24b1400` | Audited, KEEP (TASK15) |
| `src/client/refresh/files/stb_image.h` | stb, http://nothings.org/stb (Sean Barrett) | v2.30 | Public domain / MIT (dual, stated in file) | `3b0ea6220d9645445721432b37e76d0c438eee0590f5bac2aa1b739d51b944b4` | Keep-or-replace decision: INFORM2 / TASK11 |
| `src/client/refresh/files/stb_image_resize.h` | stb, http://github.com/nothings/stb | v0.97 | Public domain / MIT (dual, stated in file) | `6a0e75adbabb48df9031c2e39ccd97437bb226fe31e7d4a01c7bf70a18d32ec6` | Same decision as stb_image: INFORM2 / TASK11 |
| `src/client/sound/header/stb_vorbis.h` | stb_vorbis, http://nothings.org/stb_vorbis/ | v1.22 | Public domain / MIT (dual, stated in file) | `0bec65a0408e902b12da70ec5c3c7874434c5eecc4e0c7174a82d2bb34f8f062` | Keep-or-replace decision: INFORM1 / TASK10 |
| `src/client/refresh/gl3/glad/include/glad/glad.h` | glad OpenGL loader generator (glad 0.1.36, generated 2023-05-14) | 0.1.36 | glad terms, see `LICENSE` ("The glad source code") | `d1821b801c6cfdaf4e043ac8d741276d90146bb606fe59a9f3b4e79cffdc0637` | Decision: INFORM3 / TASK12 |
| `src/client/refresh/gl3/glad/include/KHR/khrplatform.h` | Khronos Group (shipped by glad) | not stated | MIT-style (Khronos Group, permission notice in file) | `7b1e01aaa7ad8f6fc34b5c7bdf79ebf5189bb09e2c4d2e79fc5d350623d11e83` | INFORM3 / TASK12 |
| `src/client/refresh/gl3/glad/src/glad.c` | glad, as above (gl 3.2 core; GL_ARB_debug_output, GL_EXT_texture_filter_anisotropic) | 0.1.36 | glad terms, see `LICENSE` | `df8817d94def921c499a145467803b8e42dad380bd4cb8177729beeed8a9affc` | INFORM3 / TASK12 |
| `src/client/refresh/gl1/glad-gles1/include/glad/glad.h` | glad (David Herberth), generated loader; https://glad.dav1d.de/ (Online line in file header) | 0.1.36 (generated 2024-08-11) | No license text in the file; see `LICENSE` ("The glad source code", MIT, David Herberth) | `2c3855ec1d2799b28adccb2e864d9133956d05f0a8d3838a5d56c50e838f9570` | Removed by TASK5 (in flight) |
| `src/client/refresh/gl1/glad-gles1/include/KHR/khrplatform.h` | Khronos Group (shipped by glad) | not stated | MIT-style (Khronos Group, permission notice in file) | `7b1e01aaa7ad8f6fc34b5c7bdf79ebf5189bb09e2c4d2e79fc5d350623d11e83` | Removed by TASK5 (in flight) |
| `src/client/refresh/gl1/glad-gles1/src/glad.c` | glad (David Herberth), generated loader; https://glad.dav1d.de/ (Online line in file header) | 0.1.36 (generated 2024-08-11) | No license text in the file; see `LICENSE` ("The glad source code", MIT, David Herberth) | `037bacb1fcf91929aa784eebdd9254597d3a27156578a5f24d5c4c06bfe8db10` | Removed by TASK5 (in flight) |
| `src/client/refresh/gl3/glad-gles3/include/glad/glad.h` | glad (David Herberth), generated loader; https://glad.dav1d.de/ (Online line in file header) | 0.1.36 (generated 2023-05-14) | No license text in the file; see `LICENSE` ("The glad source code", MIT, David Herberth) | `6cf24de588e1f963233d1a7190f8cdde62e87acc6cd2721305b7456c8cc92a47` | Removed by TASK5 (in flight) |
| `src/client/refresh/gl3/glad-gles3/include/KHR/khrplatform.h` | Khronos Group (shipped by glad) | not stated | MIT-style (Khronos Group, permission notice in file) | `7b1e01aaa7ad8f6fc34b5c7bdf79ebf5189bb09e2c4d2e79fc5d350623d11e83` | Removed by TASK5 (in flight) |
| `src/client/refresh/gl3/glad-gles3/src/glad.c` | glad (David Herberth), generated loader; https://glad.dav1d.de/ (Online line in file header) | 0.1.36 (generated 2023-05-14) | No license text in the file; see `LICENSE` ("The glad source code", MIT, David Herberth) | `915435deb621fb7b22f5a4e9ac326d1c17bae1602211820cff58cbe293d73bf8` | Removed by TASK5 (in flight) |
| `src/client/refresh/gl3/header/HandmadeMath.h` | HandmadeMath (Handmade Hero community); no URL in the file | v1.5.0 | Public domain (stated in file) | `b47dbb1ed7e1125b08fd55a34d536c7e058ca5b61acd9c88b316670d196cefc8` | INFORM3 / TASK12 |
| `src/client/refresh/gl3/header/DG_dynarr.h` | DG_dynarr by Daniel Gibson; no URL in the file | not stated | Dual public domain and permissive license (stated in file) | `42e3570e3603c291e60b941e12a79c1fc0bb05361ff4ad0964957871550a3d02` | INFORM3 / TASK12 |

Notes:

- `src/client/input/gyro.c` and `gyro.h` are also reproduced in the top-level `LICENSE`.
- `src/client/refresh/files/stb_image_resize.h` is decided together with `stb_image.h` under INFORM2 / TASK11.

## Removed from the tree

| Path or component | Removed by | Date | Where the record lives |
|-------------------|------------|------|------------------------|
| `src/common/unzip/` (minizip 1.1 + miniz) | TASK8 | 2026-10-18 | git history: the TASK8 removal commit; hashes were listed in this file before that commit |
| `src/client/vid/header/stb_image_write.h` (v1.16) | TASK9 | 2026-11-12 | git history; screenshots are written by a native TGA writer in `src/client/vid/vid.c` |
| `src/client/sound/openal.c`, `qal.c`, `header/qal.h` | TASK4 | 2026-10-18 | git history |
| `src/client/curl/` | TASK3 | 2026-10-18 | git history (in flight) |
