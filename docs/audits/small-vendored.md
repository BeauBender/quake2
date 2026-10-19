# Audit of small vendored files (TASK15)

Scope: the small third-party or generated files that stay in the tree. Each file was read in full (no sampling). Hashes are `shasum -a 256` of the file as committed on this branch (base: main at 7a4044df). Nothing under `src/` or `stuff/` was changed. Defects are listed at the end for the orchestrator to file.

## Summary

| File | License | Lines | sha256 | Verdict |
|------|---------|-------|--------|---------|
| `src/client/input/gyro.c` | MIT | 563 | `dbaf3b1c65526b8c0c5dc5a28d5e7f47cef8163ae1cfa11c4edf50bd58b1591e` | KEEP (defects noted below) |
| `src/common/md4.c` | Public domain | 224 | `7caa4ac6279e567bec2912ba561d2a95a8627a70df647f5e749e97bdf56ac0b9` | KEEP (defects noted below) |
| `src/common/shared/rand.c` | Public domain | 97 | `70aa5ae4a1279c409e4a82ec848d14d09d135dc8aa6e801751ae1a570a1d01c6` | KEEP (defects noted below) |
| `src/common/glob.c` | GPL-2-or-later (id Software header) | 239 | `8c7b2dd033417cda3208fb55b60e21df5bf73262788d6ab552814451808be67e` | KEEP (defects noted below) |
| `src/game/savegame/tables/*` (10 files) | GPL-2-or-later (id Software / Yamagi headers) | 2026 total | see section | KEEP (defects noted below) |

Hashes in this document are those of the audited revision; current hashes live in docs/DEPENDENCIES.md.

## src/client/input/gyro.c

- License header: yes, MIT. Copyright 2018-2021 Julian "Jibb" Smart, 2021-2024 Nicolas Lessard, 2025 ceski. The same text is reproduced in the repo `LICENSE` ("Gyro source code (gyro.c, gyro.h)").
- Upstream origin as stated: only the copyright holders (the JoyShockMapper / GamepadMotionHelpers lineage). The file names no URL and no version.
- sha256: `dbaf3b1c65526b8c0c5dc5a28d5e7f47cef8163ae1cfa11c4edf50bd58b1591e`. Lines: 563.
- `src/client/input/header/gyro.h`: same MIT license and copyright text as gyro.c; 137 lines (`wc -l`); sha256 `29385259dc66177c07ccb53fdc6059fef41cda4c49a5d81f68226c42cbafd60b`. Verdict: KEEP with gyro.c. It was not read line by line in this card (only its header and hash were checked).

What it does: converts gamepad accelerometer and gyroscope samples into pitch and yaw rates for mouse-look. `IN_GYRO_Accumulate*Samples` sums raw samples between frames. `IN_GYRO_Process` averages them, subtracts the calibrated gyro offset, then (when any input is non-zero) applies the selected gyro space (local with a roll mode, player space, or world space; the last two track a gravity vector using the accelerometer and a shakiness estimate), optional smoothing (a 256-entry ring buffer of pitch and yaw), tightening, and sensitivity with optional acceleration. The `IN_GYRO_Update*` functions take cvar floats, clamp them to header-defined ranges and convert to radians. State is two file-static structs (`motion`, `samples`), single-threaded.

Trust-boundary observations:

- Inputs: float samples from `SDL_EVENT_GAMEPAD_SENSOR_UPDATE` (`src/client/input/sdl3.c` lines 1101-1118) or the in-tree ImuJoy path, plus cvar values passed to `IN_GYRO_Update*`. Both are local; no network input reaches this file.
- Array indices: the only arrays are `smooth_pitch[]` / `smooth_yaw[]` (256 entries). `index` is always reduced modulo `MAX_SAMPLES`; `num_samples` is `lroundf(Q_clamp(x, 1, 256))` so it lies in [1, 256] and the read loop stays within the ring. No unchecked index found.
- Division by zero: every divisor is guarded. `PrepareSamples` and the Average functions test the sample count. `GetSmoothGyro` divides by `num_samples` (at least 1) and by a `sample_window` clamped to at least `EPSILONF`. `ApplyTightening` divides by `tightening` only when `use_tightening` (tightening greater than `EPSILONF`). `GetRawGyro` and `ApplySensitivity` test `denom > EPSILONF`. `CalcGravityVector` returns before dividing when the accelerometer magnitude is at most `EPSILONF`; the remaining constant divisors are non-zero. `VectorNormalize2` is relied on to handle a zero vector (it is a shared helper, not read here beyond its use).
- Uninitialised state: `motion` and `samples` are static, so zero-initialised. Before the first `IN_GYRO_Update*` call all sensitivities are 0 and `use_smoothing` is false, so output is 0. No use of uninitialised state.
- Stale data (behavioural, minor): `PrepareSamples` copies `samples.accel` / `samples.gyro` into `motion` even when no new samples arrived this frame. The accelerometer then reuses the previous frame's average; the gyro reuses the previous offset-corrected average (it is not re-offset because the count is 0). This is how the upstream algorithm behaves and is bounded.
- NaN and infinity: nothing rejects non-finite input. A single NaN sample (or a NaN cvar) would enter `motion.gravity`, `smooth_accel` and `shakiness`, which persist across frames, and stay NaN until `IN_GYRO_ResetState`. `lroundf(NaN)` in `IN_GYRO_UpdateGyroSpace` / `UpdateLocalRoll` / `UpdateAcceleration` is unspecified and could produce an out-of-range enum value (the `switch` statements then fall through to no-ops, so there is no memory-safety impact). Severity low.
- Floating-point `Q_clamp(delta_time, EPSILONF, smooth_window)` has min above max if `smooth_window` is below `EPSILONF`, but that path runs only when `use_smoothing` is true, which requires `smooth_window > EPSILONF`.

Verdict: KEEP (defects noted below)

## src/common/md4.c

- License header: "Public Domain C source implementation of RFC 1320", by Steven Fuller. URL stated: http://www.faqs.org/rfcs/rfc1320.html. No version.
- sha256: `7caa4ac6279e567bec2912ba561d2a95a8627a70df647f5e749e97bdf56ac0b9`. Lines: 224.

What it does: a one-shot MD4 over a memory buffer (`PerformMD4`), plus `Com_BlockChecksum`, which XOR-folds the four 32-bit digest words into one `unsigned`. State (`X`, `A..D`, `AA..DD`) is file-static, so it is not thread-safe or re-entrant; it is wiped at the end of each call.

Check against RFC 1320 (by reading):

- Initial values 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476: correct.
- Round functions: F = (X&Y)|(~X&Z), G = majority, H = X^Y^Z: correct. Round 2 constant 0x5A827999 and round 3 constant 0x6ED9EBA1: correct.
- Round 1 order is words 0..15 with shifts 3, 7, 11, 19: correct. Round 2 order is 0,4,8,12,1,5,9,13,2,6,10,14,3,7,11,15 with shifts 3, 5, 9, 13: correct. Round 3 order is 0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15 with shifts 3, 9, 11, 15: correct.
- Feed-forward `A += AA` etc.: correct.
- Padding: the 0x80 byte is placed after the remainder bytes (four cases for `rem % 4`), zero fill, and if fewer than 8 bytes remain in the block an extra block is processed (`j > 14`): correct.
- Length encoding: little-endian 64-bit bit count, `X[14] = (length & 0x1FFFFFFF) << 3` and `X[15] = (length & ~0x1FFFFFFF) >> 29`: correct for non-negative `length`.
- Output: words written little-endian byte by byte: correct.

Use of `Com_BlockChecksum` (callers found by grep): `src/common/collision.c:1768` (BSP map checksum, `CM_LoadMap`, sent to clients so server and client agree the map matches; integrity against accidental mismatch, not security); `src/client/refresh/files/pcx.c:488` (identifies the stock `pics/quit.pcx` by checksum before applying a pixel fix). Neither use is security-relevant; MD4 being cryptographically broken does not matter. Note a crafted map with a colliding checksum is possible in principle but the checksum is not an authentication mechanism here.

Trust-boundary observations:

- Input is whole-file buffers loaded by `FS_LoadFile` (untrusted map and PCX data) with an `int length`. There is no indexing beyond the 64-byte block loop; reads stay within `length`.
- Negative `length` is not checked: `len = length / 64` is negative so the block loop is skipped, `rem` is negative, `rem / 4` is negative, and the `switch (rem % 4)` with a negative value matches no case, leaving `X[j]` uninitialised with `j` from the unrelated loop. Callers pass `FS_LoadFile` results (non-negative, and `buf` is checked non-NULL first), so not reachable today.
- `ptr[3] << 24` promotes to `int`; when the top byte is 0x80 or above the shift overflows a signed int. This is undefined behaviour in strict C but is well-defined by every compiler this project targets (it is the same pattern the original code used for decades). UBSAN would flag it.
- `Com_BlockChecksum` copies the digest into a `uint32_t digest[4]` by casting to `unsigned char *` and then reads native-endian words, so on a big-endian host the folded value differs from a little-endian host's. `collision.c` wraps the result in `LittleLong`, which does not undo this. Mixed-endianness client/server pairs would disagree. Irrelevant on current targets (all little-endian) but a latent portability defect.
- The prototype in `common.h` is `const void *buffer`, the definition is `void *buffer`. Harmless in C, a style mismatch.

Verdict: KEEP (defects noted below)

## src/common/shared/rand.c

- License header: "KISS PRNG (c) 2011 Shinobu ... obtained from zuttobenkyou.wordpress.com and modified by the Yamagi Quake II developers. LICENSE: Public domain". Algorithm credited to Dr. George Marsaglia. No version.
- sha256: `70aa5ae4a1279c409e4a82ec848d14d09d135dc8aa6e801751ae1a570a1d01c6`. Lines: 97.

Algorithm: Marsaglia's 64-bit KISS = multiply-with-carry (`B64MWC`, lag-2^21 table `QARY` of 2,097,152 `uint64_t`, a 16 MiB static array) plus a linear congruential generator (`cng = 6906969069 * cng + 13579`) plus a 64-bit xorshift (`xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 43`). `randk()` casts the 64-bit sum to `int` and takes the absolute value. `frandk()` is `(randk() & 32767) / 32767` in [0, 1]. `crandk()` is `(randk() & 32767) * 2 / 32767 - 1` in [-1, 1].

Seeding: `randk_seed()` fills `QARY[i] = CNG + XS` for all i, then discards 256 outputs. `cng` and `xs` are file-static and never assigned from time, PID, or any entropy source, so `cng` starts at 0 and `xs` starts at 0. Xorshift of 0 is 0 forever, so the XS term contributes nothing, and the whole state after `randk_seed()` is a constant. Every process start therefore produces the identical sequence. Variation between runs comes only from how many `randk()` calls happen before a given use (the server calls `randk()` once per frame in `SV_Frame` with the comment "keep the random time dependent", and gameplay and client effects consume numbers).

Callers (grep of `src/`; the list is long, grouped):

- Game module (`src/game/**`): monster behaviour, pain/idle sound choice, gib and debris velocity, item drop angles, `random()` and `crandom()` macros in `local.h` built on `randk()` / `crandk()`. Gameplay randomness; no unpredictability needed.
- Client (`src/client/cl_effects.c`, `cl_particles.c`, `cl_tempentities.c`, `cl_entities.c`, `sound/ogg.c`): particle jitter, explosion angles, footstep sound pick, random music track. Cosmetic.
- `src/server/sv_init.c:495`: `svs.spawncount = randk()`, a per-map-load id compared by the server to detect stale client precache; not a secret.
- `src/server/sv_conless.c:135`: `svs.challenges[oldest].challenge = randk() & 0x7fff;` the connectionless challenge number that proves a client owns its source address (anti-spoofing for `getchallenge` / `connect`). This is the one caller that wants unpredictability, which this PRNG does not give (see defects). Only 15 bits are used, which is also inherent to the Q2 protocol.
- `frandk()` / `crandk()` are used only by client effects and `g_items.c` / `insane.c` / `g_func.c` gameplay.

Trust-boundary observations:

- No external input. `randk()`: `r = (int)KISS; r = (r < 0) ? (r * -1) : r;` overflows (undefined) when `r == INT_MIN`, probability 2^-32 per call. The header comment says "integer >0" but 0 is possible. Cosmetic.
- 16 MiB static array adds to BSS in both the client and the dedicated server; `randk_seed()` takes 2M iterations at startup (milliseconds).
- State is a file static with no locking, as is the rest of the engine's single-threaded game loop.

Verdict: KEEP (defects noted below)

## src/common/glob.c

- License header: GPL-2-or-later, "Copyright (C) 1997-2001 Id Software, Inc." Block comment "Global string matching". The matcher itself is the classic Bash/GNU `glob` code (the doc comment and structure match it), but the file states only the id Software provenance; no separate upstream URL or version is given. `LICENSE` does not list it separately (it falls under the engine's GPL-2).
- sha256: `8c7b2dd033417cda3208fb55b60e21df5bf73262788d6ab552814451808be67e`. Lines: 239.

What it does: `glob_match(pattern, text)` returns 1 only if the whole of `text` matches `pattern`. Supports `*`, `?`, `[set]`, `[!set]` / `[^set]`, ranges, and `\` escapes. `*` is handled by `glob_match_after_star`, which tries to match the rest of the pattern at each remaining position of the text (recursing into `glob_match`).

Callers: `src/common/filesystem.c:1246` (`ComparePackFiles`, used by `FS_ListFiles` on pak entry names) and `src/backends/unix/system.c:357,383` (`Sys_FindFirst` / `Sys_FindNext`). Patterns come from `FS_ListFiles2("maps/*.bsp", ...)` literals in `sv_cmd.c`, `cmdparser.c`, `menu.c`, and from menu code that builds `findname` from the game directory and cvars (`menu.c:5639`, `5693`). Texts are directory entry names and pak entry names (pak names come from untrusted archives).

Trust-boundary observations:

- Termination on the pattern: every loop consumes at least one pattern character per iteration, and unterminated sets are detected for most forms: `[` with no `]` (for example `[abc`) returns 0 at the NUL, and `[a-` returns 0 (`cend == '\0'`). No infinite loop found.
- Trailing backslash, out-of-bounds read (defect): in `glob_match` case `'\\'`, `if (*p++ != *t++)` steps past the pattern's NUL when the pattern ends in `\`. When the text is also exhausted at that point (`*t == '\0'`) the comparison succeeds and both pointers now point one byte past their terminators, and the `while ((c = *p++) != '\0')` loop keeps reading beyond the buffers. Example: pattern `a\`, text `a`. The same step-over-NUL pattern occurs in the set parser (`[\` : `cstart = *p++` is read after a `\` that is the last character; the following `c = *p++` reads one byte past the NUL), in the "skip the rest of the set" loop (`else if (c == '\\') ++p` then `c = *p++`), and in `glob_match_after_star` (`c1 = *p` after `*\`, then `glob_match(p - 1, t)` with a pattern of just `\`). Result is a read past a NUL-terminated buffer, not a write. Reachable only with a pattern that ends in a backslash; the literal patterns in the callers do not, but menu-built patterns include user-influenced path text.
- Recursion depth: each `*` group adds at most one nested `glob_match` / `glob_match_after_star` pair, and consecutive stars are collapsed, so depth is bounded by the number of `*` groups in the pattern (at most half the pattern length, and patterns are bounded by `MAX_OSPATH` buffers in the callers). No stack-depth concern for realistic input.
- Run time: matching is backtracking. A pattern with k separate `*` groups against a long text that almost matches costs O(n^k). Example `*a*a*a*a*a*b` against a long run of `a`. Only user-supplied patterns on user-supplied filenames; local denial of service at worst. Low.
- Signature: takes non-const `char *` for both arguments, so callers cast away `const` (`(char *)findname`). Uses the `register` keyword (a no-op, deprecated in C23). Characters are compared as plain `char`, so ranges involving bytes of 128 and above compare as negative values; matches the historical behaviour.

Verdict: KEEP (defects noted below)

## src/game/savegame/tables/*

Ten headers, included by `src/game/savegame/savegame.c` to map function pointers, `mmove_t` pointers and entity fields to names. All carry the id Software GPL-2 header with Yamagi (and for some Knightmare / ZeniMax Media Inc.) copyrights. They are not third-party in the vendored sense; they are generated or hand-maintained project files that are part of the savegame format.

| File | Lines | Origin | sha256 |
|------|-------|--------|--------|
| `clientfields.h` | 32 | hand-written | `cc1e6c2e0f3b6c141e8f2a56312b8b16f9f7d8747c13481a99f254e4f4e60a68` |
| `entfields.h` | 92 | hand-written | `52f0079e81e4c93e927a5eb8f6661a2568218c63b12e484a62629c54477a79b7` |
| `gamefunc_decs.h` | 395 | generated by `fngenprotos.sh`, hand-edited | `5dbad7207dc70dfa35ebdb12d05b43c346d5c37e38317fd766e8820fbab45068` |
| `gamefunc_list.h` | 536 | generated by `fngentables.sh`, hand-edited | `8d969e6d84aac79c00738e5f4830e0e06d838dcd372b57d49b2cdb91388055ce` |
| `gamemmove_decs.h` | 303 | generated by `mmproto.sh` | `a87a6112bb7d2aa0948e0ec1d6aed09aa155e0d6372a8368c172e0a0228851a4` |
| `gamemmove_list.h` | 302 | generated by `mmtable.sh` | `c387c76350722dd3cc170ce4ef5b7b7fb25caacc9dffcef09bf3344326d2f8f2` |
| `levelfields.h` | 33 | hand-written | `d6662ea7c8bc831b110cb8dab3c5e8fa6b6087967972a8027fff63b1c0bafb7e` |
| `spawnfunc_decs.h` | 143 | hand-written (spawn function prototypes) | `16197400c4a940b363ac680ca31cd44a5ba134344491ef1420ba74c6ed18dfbf` |
| `spawnfunc_list.h` | 147 | hand-written (spawn name to function table, ends in `{NULL, NULL}`) | `1d9cbed8dcc8fe20e4905f917837ab520a0f0f90d1b4e2bc1dd4c0cd46c85c4e` |
| `stfields.h` | 43 | hand-written | `e5f8fe9accf0c61038db32de73a94584b5566b624070eccac7755f9ba24b1400` |

What they do: `gamefunc_list.h` defines one `fnlist_entry_t[]` per function-pointer field (die, pain, prethink, think, touch, use, blocked, nine `monsterinfo.*` callbacks, `moveinfo.endfunc`), each entry a name plus the function address; `gamemmove_list.h` does the same for 275 `mmove_t` objects; the `*_decs.h` files are the matching prototypes; the `*fields.h` files are `field_t` tables (name, offset, type) for entity, client, level and spawn-temp fields; the spawnfunc files map map-file classnames to `SP_*` functions. A function not in a list is saved as "not found" and cannot be restored (savegame.c logs `function at address %p not found in %s` and writes no name).

Regeneration (as required by the card):

- The repo scripts `stuff/fngen/*.sh` and `stuff/mmgen/*.sh` cannot run unmodified on this machine without installing software. They need GNU awk (they call `gensub`, a gawk extension; the system `awk` here reports `calling undefined function gensub`) and GNU/PCRE grep (`grep -P`; macOS ships BSD grep without `-P`). Neither gawk nor GNU grep is installed, no installs were made, so the scripts were not run as written.
- Substitute check: the scripts' extraction is a fixed recipe (regex `>PTR\s*=\s*\K[a-zA-Z0-9_]+(?=;)` over every file under `src/game`, drop `NULL`, de-duplicate in first-seen order; for mmove, `mmove_t\s+\K[a-zA-Z0-9_]+(?=\s*=\s*{)` over whole files). The extraction was reproduced with a perl one-liner and compared with the committed tables by name set; order was not verified for the 17 matching tables. For `moveinfo.endfunc` the check is `find src/game -type f -name '*.c' | xargs perl -ne 'while (/moveinfo\.endfunc\s*=\s*([A-Za-z0-9_]+)\s*;/g) { print "$1\n" }' | sort -u` (the NULL entry is dropped, leaving 13 names). Nothing was written into the repo. This is not the repo script's own output, so the result below is "equivalent extraction", not a byte diff.
- Result for the 17 pointer tables other than `moveinfo.endfunc` (die 33, pain 21, prethink 2, think 70, touch 24, use 48, blocked 6, `monsterinfo.stand` 20, walk 20, run 20, melee 8, attack 16, checkattack 6, dodge 6, idle 10, search 14, sight 15): identical name sets to the committed `gamefunc_list.h` (order not verified).
- `moveinfo.endfunc`: generator finds 13 names, the committed `mv_end` table has 10. The three extra names are `func`, `afterwaitfunc` (the README says to delete these two by hand: false positives from `moveinfo.endfunc = func;` in `Move_Calc` and a similar line) and `door_secret_done`. `door_secret_done` is matched only because of a comment in `g_func.c:2861` (`/* ->moveinfo.endfunc = door_secret_done; */`). The committed table omits it. (Fixed by FIX6, together with `plat_go_up`, which is stored through `wait_and_change`.) That omission is a real gap, not a false positive: `door_secret_move6` calls `Move_Calc(self, vec3_origin, door_secret_done)`, which stores it in `moveinfo.endfunc` (g_func.c:2858 and :158). See defects.
- `gamefunc_decs.h`: 350 prototypes committed; the union of generated names is 352; the difference is exactly `func` and `afterwaitfunc` (removed per README). `door_secret_done` has a prototype (line 395) but is not in the `mv_end` list.
- `gamemmove_list.h` and `gamemmove_decs.h`: 275 names each, identical sets to the generated names. Order was not compared (name sets only); the lookup does not depend on order.
- The hand-written files (`*fields.h`, `spawnfunc_*`) have no generator and were read only.

Trust-boundary observations: the tables are build-time constants. The savegame reader resolves names against them (`savegame.c` near lines 526-560 and 810-833) and logs and drops unknown names rather than crashing. The generator scripts are a fixed regex over source text, so they miss any function stored in a pointer field via an intermediate parameter (`Move_Calc(..., func)`); the repo's answer is the marker-comment convention documented in `stuff/fngen/README.md`, which is exactly how the `door_secret_done` gap arose.

Verdict: KEEP (defects noted below)

## Defects found

None of these were filed or fixed by TASK15; later FIX cards record their resolution in the What cell.

| # | Location (file:function) | What | Severity |
|---|--------------------------|------|----------|
| 1 | `src/common/glob.c:glob_match`, `glob_match_after_star` | A pattern ending in `\` (or `[\` or `*\`) steps the pattern pointer past the terminating NUL, and when the text is also at its NUL the match continues and reads past both buffers (for example pattern `a\`, text `a`). Out-of-bounds read, no write. Fix: check for `'\0'` after a backslash in each of the four places. | Low to medium (read-only, needs an unusual pattern; callers mostly use literals, menu-built patterns include path text) -- **FIXED (FIX5, dcaf99d9)** |
| 2 | `src/game/savegame/tables/gamefunc_list.h` (`fnentries_mv_end`) | `door_secret_done` is assigned to `moveinfo.endfunc` through `Move_Calc` (`g_func.c:2858`) but is missing from the `mv_end` table. A save made while a secret door is in its final move stores no end function; on load the function is NULL, so the door never reaches `door_secret_done` (health and areaportal state not set). Fixed in FIX6: door_secret_done and plat_go_up added to fnentries_mv_end; SAVEGAMEVER deliberately not bumped (only names were added to the lookup table, the on-disk format is unchanged, older saves still load); the marker-comment rule is documented in stuff/fngen/README.md. | Low (rare timing, mod-compatible fix) |
| 3 | `src/common/shared/rand.c:randk_seed` and `src/server/sv_conless.c` (challenge, `randk() & 0x7fff`) | `randk_seed()` uses no entropy: `cng` and `xs` start at 0, so the PRNG state at startup is a constant, and the connectionless challenge is a function of how many `randk()` calls ran before it. An attacker who can count server frames can predict challenges, weakening the address-ownership proof. Fix: seed `cng`/`xs` from the OS (time plus `getrandom`/`arc4random`) at least for the challenge, or use an OS random source for challenges. Note the challenge is only 15 bits by protocol. | Medium for a public server (spoofed `connect`), low otherwise (FIXED by card FIX4) |
| 4 | `src/common/shared/rand.c:randk` | `r * -1` overflows (undefined behaviour) when `(int)KISS == INT_MIN`, probability 2^-32 per call; comment says result is positive but 0 is possible. | Low (UBSAN only) (FIXED by card FIX4) |
| 5 | `src/common/md4.c:Com_BlockChecksum` | Digest is written as bytes into `uint32_t[4]` and read back as native words, so the folded checksum differs between little- and big-endian hosts; `collision.c` applies `LittleLong` afterwards which does not compensate. Header prototype is `const void *`, definition `void *`. FIXED (FIX7). | Low (latent, all current targets are little-endian) |
| 6 | `src/common/md4.c:PerformMD4` | `ptr[3] << 24` promotes to signed `int` and overflows for bytes 0x80 and above: undefined behaviour in strict C, flagged by UBSAN. Negative `length` is not rejected and leaves `X[j]` uninitialised. Neither is reachable from current callers. FIXED (FIX7). | Low |
| 7 | `src/client/input/gyro.c` (`CalcGravityVector`, `IN_GYRO_Update*`) | No rejection of NaN or infinite sensor samples or cvar values. One NaN sample poisons the persistent `gravity`, `smooth_accel` and `shakiness` until `IN_GYRO_ResetState`; `lroundf(NaN)` feeding the enum fields is unspecified. No memory-safety impact found. FIXED (FIX7). | Low |
| 8 | `src/common/glob.c:glob_match_after_star` | Backtracking is O(n^k) for k `*` groups on a near-miss text; local denial of service with a crafted pattern. | Low -- **FIXED (FIX5, dcaf99d9)** |
