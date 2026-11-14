# Portmanbeau Quake II Documentation

Welcome to the Portmanbeau Quake II documentation. Here is documented how
Portmanbeau Quake II is installed and configured. Please make sure to read
through the whole guide before asking questions!

Portmanbeau Quake II is based on Yamagi Quake II, which descends from
Icculus Quake II and id Software's Quake II source release. The files
are modified from upstream; the git history is the record.

Some general hints:

* No matter what's the current keyboard layout or on which platform the
  game is running, the console is always opened and closed by pressing
  **Left Shift + Escape**.
* Portmanbeau Quake II saves its configuration, savegames and screenshots to
  *~/.yq2* (unixoid platforms) or *My Documents\YamagiQ2* (Windows).
  If Portmanbeau Quake II is started with the `-portable` switch all data is
  saved in the same directory as the executables.

The documentation is split into several documents:

* [Index File](010_index.md)
* [Installation Guide](020_installation.md)
* [Configuration Guide](030_configuration.md)
* [List Of All New Console Variables](040_cvarlist.md)
* [List Of All New Commands](050_commands.md)
* [Multiplayer Server Configuration](060_multiplayer.md)
* [Packaging Guide For Maintainers](070_packaging.md)
* [Contributing Guide](080_contributing.md)
* [Unit Tests, Pins And Coverage](085_unittests.md)
* [File Listings](090_filelists.md)
* [Renderer Frame Pins](100_renderpins.md)
