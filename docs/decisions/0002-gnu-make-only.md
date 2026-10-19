# 0002. GNU Make is the only build system

Status: accepted 2026-10-18; landed 2026-10-19 as TASK6 (#52).

## Context

Upstream maintains a `Makefile` and a parallel `CMakeLists.txt`, plus Windows
build scripts and a macOS app-bundle packaging step. Two build systems drift,
and every dependency change has to be made twice.

## Decision

Keep the `Makefile` as the only build. CMake, the winbuild scripts and the
app-bundle zip are removed; CI builds with `make`, and macOS CI packages a
tarball.

## Consequences

- One place to change compiler flags, sources and feature switches
  (`WITH_*`).
- IDE projects that relied on CMake have to wrap `make`.
- Windows builds use `make` with MinGW-w64, as `doc/020_installation.md`
  describes; there is no Visual Studio project.

## Reversal

Restoring CMake means re-adding it from upstream and keeping it in step with
the `Makefile` by hand; no check enforces parity.
