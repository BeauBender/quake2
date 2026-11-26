# Notes for Package Maintainers

This guide shows how Portmanbeau Quake II should be packaged and what
assumptions the games makes regarding binary and game data locations.
This fork publishes no binary packages and targets Apple platforms only;
the notes below describe the layout of a `make` build on macOS.

Executables have no file extensions and libraries are given with the
*.dylib* extension.


## The Executables

Portmanbeau Quake II expects all binaries (executables and libraries) to be
in the same directory or, in the case of *game.dylib*, in the mod-specific
subdirectory.

So the binary directory should look somehow like this:

* /path/to/quake2/
  * quake2
  * q2ded
  * ref_gl1.dylib
  * ref_gl3.dylib
  * ref_soft.dylib
  * ref_metal.dylib (and *ref_metal.metallib*, from `make ref_metal`)
  * baseq2/
    * game.dylib
  * xatrix/
    * game.dylib
  * ... (the same for other addons)

Portmanbeau Quake II will get the directory, the `quake2` executable is in,
from the system and then look in that directory (and nowhere else!) for
the `ref_*.dylib` renderer libraries. It will look for `game.dylib` there
first, but if it's not found in the binary directory, it will look for
it in all directories that are also searched for game data.  This is for
better compatibility with mods that might ship their own game library.

We want all binaries to be in the same directory to ensure that people
don't accidentally update only parts of their Portmanbeau Quake II
installation, so they'd end up with a new quake2 executable and old
renderer libraries (`ref_*.dylib`) and report weird bugs.

The static build (`make STATIC_MODULES=1 ...`, see the
[Installation Guide](020_installation.md)) links the game and one renderer
into the *quake2* executable and has no such libraries.


## The SYSTEMWIDE and SYSTEMDIR options

The Makefile allows to enable the *SYSTEMWIDE* feature to force Portmanbeau
Quake II to search the game data in an system specific directory. That
directory can be given in the Makefile. If no directory is given the
game defaults to */usr/share/games/quake2/*.

The `SYSTEMDIR` is meant to contain only the game data and *not* the
binaries. It allows several Quake2 source ports to share the same game
data.


## Alternative startup config

Portmanbeau Quake II has support for an alternative startup config. That
config overrides some global values with sane defaults. It may be a good
idea to install it. Copy yq2.cfg to the baseq2/ subdirectory in the
gamedata directory.
