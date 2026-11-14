# Portmanbeau Quake II

Portmanbeau Quake II is based on Yamagi Quake II, which descends from
Icculus Quake II and id Software's Quake II source release. It is a
long-lived fork, an enhanced client for id Software's Quake II. It exists
for two reasons: to run and extend the game on code that can be read and
verified in full, and to give it a native renderer on Apple platforms.
The project's home is <https://github.com/BeauBender/quake2>. Bug reports
and patches are not accepted there.

The fork was taken from Yamagi Quake II at upstream commit `9dbf6992`
(2026-10-15) and was byte-identical to it at that point. Gameplay and look
are unchanged. Game data is not in this repository and is required to run
anything; see [doc/020_installation.md](doc/020_installation.md).

## How it differs from upstream

Removed, to shrink what has to be trusted
([docs/plans/trust-boundary.md](docs/plans/trust-boundary.md),
[docs/DEPENDENCIES.md](docs/DEPENDENCIES.md)):

* HTTP downloads and libcurl
* the OpenAL sound backend; SDL audio is the only backend
* the OpenGL ES renderers
* zip pack files; packs are `.pak` only
* CMake and the Windows build scripts; GNU Make is the only build
* SDL2 support; SDL3 is the only SDL

Fixed: unchecked sizes and indices in the download path and in the asset
loaders.

Planned and not in the tree yet
([docs/plans/metal-renderer.md](docs/plans/metal-renderer.md)): a Metal
renderer, then ray tracing on Apple silicon.

## Build and run

```sh
make                      # client, server, game and the renderers, into release/
make server game          # the parts that need no SDL
release/quake2 -datadir /path/to/quake2 -portable
scripts/test.sh           # the gate; every commit must pass it
```

The client needs SDL3. The dedicated server and the game module
need only the system toolchain. Only Apple platforms are built and tested by
this fork; other platforms are unmaintained (see
[docs/decisions/0008-apple-platforms-only.md](docs/decisions/0008-apple-platforms-only.md)).

## Documentation

* [doc/010_index.md](doc/010_index.md): the user documentation. It comes
  from upstream and is updated as features are removed.
* [docs/README.md](docs/README.md): a map of the repository for working on
  the code, with the plans and decision records.

## Contributing

The repository is public to read. Issues and pull requests are open to its
owner only, so bug reports and patches are not accepted here.

## Upstream and credits

* [Yamagi Quake II](https://github.com/yquake2/yquake2), the upstream of
  this fork. Bugs in upstream belong in its tracker.
* Icculus Quake II, on which Yamagi Quake II is built.
* id Software's Quake II 3.21 source release.

The files in this repository are modified from upstream; the git history
is the record of every change. Released under the GPL version 2; see [LICENSE](LICENSE). The copyright
notices are upstream's and are unchanged.
