# TASK236: doc/ describes the Apple-only fork

`doc/` still carried upstream text for Windows, Linux and the BSDs. Decision 0008 makes Apple
platforms the only targets, so each platform-specific install, build, packaging and path line is
removed or rewritten to match `CLAUDE.md`, the Makefile and the code. Gameplay, cvar, command,
configuration and multiplayer text stays. One commit per file.

## What changed, by file

- `doc/010_index.md`: the Windows `My Documents\YamagiQ2` path is gone. The home directory text
  follows `Sys_GetHomeDir` (`src/backends/unix/system.c`): `~/.local/share/YamagiQ2` (XDG, on by
  default, `WITH_XDG:=yes`), `~/.yq2` when that directory already exists, the app's *Documents*
  folder where `sys_sandbox_home` is set (`TARGET_OS_IPHONE`), and `-portable`.
- `doc/020_installation.md`: removed the Windows and Linux/BSD "Download and Extract the
  Executables" sections, the Defender note, the Linux/BSD/Solaris/Haiku package commands, the
  MinGW and `gmake` build text, the `cdparanoia` shell-script ripper (Linux package tooling) and
  the "Windows release" footstep note. Added: this fork publishes no binaries, the build lives in
  `release/`, the data search order (home, binary directory, `-datadir`, systemwide if set, `game`
  mods first: `CLAUDE.md`), the macOS build (Xcode toolchain, vendored SDL3, `make ref_metal`,
  `METAL_SHADERS=no`, `make config`) and the `-portable` run line. The iOS, tvOS, visionOS and
  static-module sections are untouched.
- `doc/030_configuration.md`: the "especially on Windows" and "Raspberry Pi" remarks.
- `doc/040_cvarlist.md`: "especially on Windows" (busywait), the Windows 98 VM example, the
  Wayland high-DPI claim, `game.dll` to "the game library". No entry removed;
  `tests/check-cvar-docs.sh` passes.
- `doc/060_multiplayer.md`: the Win9x aside.
- `doc/070_packaging.md`: rewritten for the macOS layout (`.dylib`, `ref_metal`, static build).
  Removed the OpenBSD wrapper note and the PATH symlink advice: `SetExecutablePath` on macOS
  uses `_NSGetExecutablePath` with a `TODO realpath()`, so a symlink is not traced to work.
- `doc/080_contributing.md`: the GCC/Clang and "unixoid platforms and Windows" rule.
- `doc/090_filelists.md`: `.dll` and `.exe` names to `.dylib` and `quake2`; `SDL3.DLL` removed
  (SDL3 is linked statically on macOS, `docs/DEPENDENCIES.md`).
- `doc/085_unittests.md`, `doc/100_renderpins.md`: no grep hit; not changed.

## The card's grep

`git grep -n -i -E 'windows|\.exe|linux|freebsd|openbsd|netbsd|msys|mingw|visual studio|homebrew|brew |apt-get|pkg install|pacman|\.dll|\.so\b' doc/`

Before: 70 lines (010: 1, 020: 43, 030: 1, 040: 8, 060: 1, 070: 3, 080: 1, 090: 12).

After: 11 lines.

```
doc/020_installation.md:35:   https://deponie.yamagi.org/quake2/idstuff/q2-3.20-x86-full-ctf.exe
doc/020_installation.md:45:* *quake2.exe*
doc/020_installation.md:46:* *ref_gl.dll*
doc/020_installation.md:47:* *ref_soft.dll*
doc/020_installation.md:48:* *baseq2/gamex86.dll*
doc/020_installation.md:50:* *ctf/gamex86.dll*
doc/020_installation.md:53:* *xatrix/gamex86.dll*
doc/020_installation.md:54:* *rogue/gamex86.dll*
doc/020_installation.md:189:   https://deponie.yamagi.org/quake2/idstuff/q2-314-demo-x86.exe
doc/040_cvarlist.md:214:  reasons these cannot be overridden: '.dll', '.dylib', '.lib', '.so' and
doc/040_cvarlist.md:420:  with an aspect ratio of 4:3, regardless what the actual windows size
doc/040_cvarlist.md:484:  the windows will be *r_customheight* pixels high and *r_customwidth*
```

Why each remains:

- `020:35`, `:45` to `:54` and `:189`: the file names of id Software's patch and demo downloads
  and of the files to delete from the unpacked patch (`quake2.exe`, `ref_gl.dll`,
  `gamex86.dll`). They name upstream data files and are true of those downloads; the game data is
  the same on every platform.
- `040:214`: the download filter's always-refused extensions. `CL_DownloadFilter`
  (`src/client/cl_download.c:512`) lists `.dll`, so the text is true of the code.
- `040:420` and `040:484`: "window size", the English word, not the OS.

## Checks run

`tests/check-doc-links.sh`: ok. `tests/check-cvar-docs.sh`: ok. The whole gate is in the card's
hand-back.

## Left as it was, unverified

- `doc/030` to `doc/050` and `doc/100` were scrubbed by pattern and by reading the hits, not read
  line by line for non-Apple behaviour that names no platform (for example the `s_sdldriver`
  text and the renderer descriptions' "GPU driver" remarks). Nothing there named a platform.
- `doc/070` says the game library is looked for in the binary directory first, then the data
  directories; that is upstream's text. `Sys_GetGameAPI` walks `FS_NextPath`, which includes the
  binary directory, but the order was not traced.
- `doc/020`'s addon build line (the *release/game.dylib* is copied to the corresponding
  directory) is upstream's text for the addon repositories, which this tree does not hold.
- `doc/085:142` says the coverage script uses `gcov` for gcc; `CLAUDE.md` says "`xcrun llvm-cov`
  or `gcov`". Not changed.
- `doc/100:738` names `$XDG_CACHE_HOME` for the bench cache; `scripts/bench` was not read.

## Should `doc/` move under `docs/`?

References to the `doc/` path outside `doc/`, counted with
`git grep -c -E '(^|[^a-z_/.])doc/|\.\./doc/' -- . ':!doc/'`: 205 lines in 79 files. In `docs/`:
58 files (runbooks 35, plans 11, decisions 3, reviews 1, README 1 and others). Outside `docs/`:
21 files.

| Where | Files | What reads the path |
|-------|-------|---------------------|
| `Makefile` | 1 | three comments (lines 45, 118, 1869); no rule |
| `.github/workflows` | 2 | `macos.yml` copies ten `doc/*.md` files into the testbuild package (lines 74 to 83); `codeql.yml` comment |
| `.github/` other | 2 | `codeql-changes.sh` treats Markdown under `doc/` or `docs/` as documentation only; the PR template names `doc/040` and `doc/050` |
| `scripts/` | 5 | `test.sh`, `coverage.sh` (comments), `pins/rtvar.py:531` (a message), `pins/scenarios/README.md`, `bench/adapters/quake2.sh` |
| tests | 6 | `check-cvar-docs.sh` (36 lines: it reads `doc/040_cvarlist.md` and `doc/050_commands.md` and builds fixtures at those paths), `check-codeql-changes.sh`, `check-pre-commit-hook.sh`, `check-pre-push-hook.sh`, `check-unit-sanitizers.sh`, `ci-map.txt:83` (`harness check-require-guards.sh doc/*`) |
| other | 5 | `CLAUDE.md` (3), `README.md` (2), `.claude/hooks/stop-invariants.sh`, two comments in `src/` |

Release packaging: the Makefile has no install or dist rule for `doc/`. What ships it is
`macos.yml`'s "Copy misc assets" step, which copies ten of the eleven files (not
`100_renderpins.md`) to `misc/docs/` in the testbuild package, renaming most to `.txt`. So `doc/`
is shipped by the workflow, and `CLAUDE.md` says "Documentation lives in `doc/` and ships with
releases". `doc/070_packaging.md` describes the layout only, not the copy.

Recommendation: do not move it. `doc/` is the user manual that ships in the package and that
`README.md` and the cvar check name, while `docs/` holds decisions, plans and runbooks for the
fork's agents; two similar names are a small cost against touching 79 files. A move would also
edit a workflow (a release path), a check that parses the files, the CodeQL documentation-only
filter and the CI map, so it is a process-tier change that needs the full review panel and a fresh
gate, and the 58 `docs/` files would hold stale paths unless rewritten too (history records are
better left alone). If the owner still wants it, make it its own card: one mechanical commit
(`git mv doc docs/manual`) plus a path rewrite of the 21 files outside `docs/` and the fixtures in
`tests/check-cvar-docs.sh`, with `tests/check-doc-links.sh` and the cvar check as the proof. A
cheaper step that keeps the path: a one-line pointer to the manual in `docs/README.md`.
