# Vendored SDL 3

This directory is a byte-identical subset of the official SDL release tarball. It is the fork's own file (this README) plus upstream files; nothing upstream is edited. Provenance, hashes and licence are recorded in [docs/DEPENDENCIES.md](../../docs/DEPENDENCIES.md); the build rules are in [third_party/sdl3.make](../sdl3.make).

| | |
|---|---|
| Release | SDL 3.4.20, tag `release-3.4.20` |
| Upstream commit | `f00dc919ca35b9756316e86c4777392de442f9b2` (lightweight tag; `REVISION.txt` says `release-3.4.20-0-gf00dc919c`) |
| Tarball | `SDL3-3.4.20.tar.gz` from https://github.com/libsdl-org/SDL/releases/tag/release-3.4.20 |
| Tarball sha256 | `f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f` (equal to the digest GitHub's release API reports for the asset) |
| Signature | `SDL3-3.4.20.tar.gz.sig`, sha256 `776200b5c71f8c0168c0e89d8fb78103623dc15e284e9daf0a0eceb4e43d5533`. Not verified: `gpg` was not available on the machine that did the import. |

## What is vendored

From the tarball's top directory `SDL3-3.4.20/`:

- `src/`, except `src/hidapi/documentation/` and `src/hidapi/testgui/` (hidapi's screenshots and its test GUI app: images and a UTF-16 plist, nothing the library build reads).
- `include/` (public headers and the stock `build_config/` headers; macOS uses `SDL_build_config_macos.h`).
- `LICENSE.txt`, `REVISION.txt`, `WhatsNew.txt`, `CREDITS.md`.

Not vendored: `test/`, `examples/`, `docs/`, `cmake/`, `CMakeLists.txt`, `build-scripts/`, `Xcode/`, `VisualC*/`, `android-project/`, `Android.mk`, `wayland-protocols/`, upstream's `README.md`, `INSTALL.md`, `BUGS.txt`, `AGENTS.md` and `CLAUDE.md`. The library is built from `src/` and `include/` only; the rest is upstream's own build systems, tests and documentation. `wayland-protocols/` is needed only for a Wayland build on Linux and comes in with the Linux card.

`MANIFEST.sha256` lists the sha256 of every vendored file (in `shasum -a 256` format, paths relative to this directory). `scripts/check-vendored.sh`, run by the gate through `tests/check-vendored.sh`, fails if a listed file is missing or differs, if a file appears here that the manifest does not list, or if the manifest's own sha256 differs from the one recorded in `docs/DEPENDENCIES.md`.

Upstream's `src/render/gpu/shaders/.gitignore` ignores `*.hlsl`, so the tree was added with `git add -f`. Do the same when updating.

## Update procedure

1. Pick the new release tag and get the owner's approval for the download (new dependency bytes).
2. Download `SDL3-<version>.tar.gz` and its `.sig` from the GitHub release into a new empty scratch directory. Record `shasum -a 256` of both and compare with the asset digest from `gh api repos/libsdl-org/SDL/releases/tags/<tag> --jq '.assets[] | [.name, .digest]'`. Resolve the tag to its commit with `gh api repos/libsdl-org/SDL/git/ref/tags/<tag>` (dereference it if `object.type` is `tag`). Verify the signature with `gpg` where it is available.
3. Extract with `tar -xzf` into that scratch directory. Run nothing from it.
4. Replace this directory's upstream content: delete everything here except `README.md`, then copy `src/`, `include/`, `LICENSE.txt`, `REVISION.txt`, `WhatsNew.txt` and `CREDITS.md` from the extracted tree and remove `src/hidapi/documentation/` and `src/hidapi/testgui/`.
5. Regenerate `MANIFEST.sha256` from the extracted tarball, not from the copy: keep the comment header (update tag, commit, tarball and signature lines), then, from inside the extracted `SDL3-<version>/`, append `shasum -a 256` of every vendored file in `LC_ALL=C sort` order.
6. Update the source list in `third_party/sdl3.make` from the new tarball's `Xcode/SDL/SDL.xcodeproj` (target `SDL3`, Sources phase) and the framework list from its Frameworks phase.
7. Update the SDL3 rows in `docs/DEPENDENCIES.md` (version, commit, tarball, signature, manifest sha256) and this README.
8. `git add -f third_party/SDL3`, then `scripts/test.sh` and `make -j4` (SDL objects track their header and `third_party/sdl3.make` dependencies through `.d` files, so a `make clean` first is not needed; the source list change in step 6 is picked up because every SDL object depends on `sdl3.make`); check with `otool -L release/quake2 release/ref_*.dylib` that no SDL library is referenced.

Only macOS builds this copy. Linux, the BSDs and Windows still use the system SDL3 until a follow-up card moves them to the vendored source.
