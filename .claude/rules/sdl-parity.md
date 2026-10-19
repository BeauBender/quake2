---
paths:
  - "src/client/input/**"
  - "src/client/vid/glimp_*.c"
---

# SDL2 and SDL3 parity

`src/client/input/sdl2.c` and `sdl3.c`, and `src/client/vid/glimp_sdl2.c`
and `glimp_sdl3.c`, are parallel implementations of the same behavior, not
one file with `#ifdef`s. Make every input, window, gamepad or gyro change in
both files of the pair, in the same commit. Before committing, compare the
two diffs side by side: the same hunk should appear in each, adjusted only
for the API difference.

This host may have only one SDL installed. CI builds SDL3 on x86_64 Linux
and macOS and SDL2 on aarch64 Linux, so a push exercises both.
