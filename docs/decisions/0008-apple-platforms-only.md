# 0008. Apple platforms only

Status: decided by the owner, 2026-11-14.

Number 0007 is the pull-request evidence record. The card that asked for
this record (TASK95) named the file 0007, which was already taken.

## Context

This fork targets Apple platforms. Metal is the primary renderer, and the
code is to run on macOS, iOS, iPadOS, tvOS and visionOS. Linux, the BSDs and
Windows have no Metal and are not targets. Once the conversion is done,
OpenGL may stay as a switch for comparison.

CI still carried the two Linux test builds inherited from upstream
(`linux_x86_64.yml`, `linux_aarch64.yml`). They compile none of the Metal
code. The workflows also triggered on pushes to `master`, a branch this
repository does not have, so nothing ran on `main` after a merge.

## Decision

1. Only Apple platforms are built and tested by this fork. Other platforms
   are unmaintained: nothing checks that they build or run.
2. The two Linux test workflows are removed. A pull request runs the macOS
   test build (build, unit tests, coverage), CodeQL and Cppcheck.
   Cppcheck is a job of `codeql.yml`, on a free standard runner, and still
   uploads its SARIF to code scanning.
3. The remaining workflows trigger on pushes to `main`.
4. Review effort goes to Apple platform code and to the code shared with it.

## What this does not change

- No non-Apple source is removed: `src/backends/windows`, the Linux and BSD
  paths in the Makefile and the build instructions in `doc/` stay.
- `ref_gl1`, `ref_gl3` and `ref_soft` stay as references.
- iOS, tvOS and visionOS build targets are separate cards.

## Consequences

- A change that breaks a Linux, BSD or Windows build is not caught by CI.
- The CodeQL `c-cpp` job and Cppcheck still run on Linux runners, because
  they analyse the source and do not test a build for that platform.

## Reversal

Restore the two workflow files from the history before this record and add
a record that names this one.
