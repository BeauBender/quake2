# Architecture review — fork-window

Window: 9dbf6992..88f809e5 (197 merges, 195,746 changed lines outside `third_party/` and `tests/pins/`)
Trigger: cadence.archReview (no prior run recorded; the window starts at the last upstream commit, not at the repository's root, since the upstream history is not the fork's architecture)
Date: 2026-11-17

## 1. Executive summary and risk verdict

The module boundaries held: `ref.h`, `game.h`, `API_VERSION` (8), `SAVEGAMEVER` and the wire format have no change in the window, and every new renderer and client feature (ref_metal, MetalFX, the record format) was built around them. The drift is elsewhere, in three places. The gate is enforced only on the host that runs it: CI runs 2 of its 36 invariants, main requires no check, and 21 checks report green when a tool is missing. Several guards are defeated by a placement or a rename (the cvar-doc scan, removed-deps, the commit hook's path filter, the push hook's claim exemption). And mechanisms were copied rather than shared: the BSP hardening lives only in ref_metal's fourth copy of the loader, the pin scenario format has three readers with different filters, and bounded runs have five hand-rolled watchdogs. None of this is a Blocker today; all of it compounds.

`ARCHITECTURE: drifting`

## 2. How to read the findings

- **Severity:** `Blocker` (ship-stopper or trust-boundary breach), `Major` (structural drift that compounds), `Minor` (hygiene).
- **Verification:** `LIVE` (executed or observed, output quoted), `READ` (code path traced, file:line cited), `NEEDS-VERIFY` (unproven; the experiment that would prove it is stated).
- **Remediation:** `this-review` (fixed in this review, documentation only), `carded` (card id), `decision` (an owner call; the question is stated on an INFORM card with a default).

Paths and lines are at `88f809e5`. Four auditors ran in parallel (layering and seams, trust and failure surfaces, doc and contract drift, guard gaps); every Major was checked again by the orchestrator before it was written here.

## 3. Findings by theme

### The gate is host-local

**AR-01 — CI runs 2 of the gate's 36 invariants, and main requires no status check.** Major, LIVE, carded FIX115.
`.github/workflows/macos.yml:36-63` runs `gmake`, `tests/check-static-modules.sh --full`, `gmake test`, `tests/check-ios.sh` and `scripts/coverage.sh`; it never runs `scripts/test.sh` or the `tests/check-*.sh` glob (`scripts/test.sh:205`). The `main-protect` ruleset, in the tree and live (`gh api repos/<owner>/quake2/rules/branches/main` lists `deletion` and `non_fast_forward` only), requires no check. CLAUDE.md's "must pass on every commit" is held by the local hook and the loop's merge gate; a pull request from outside never runs the invariants.

**AR-02 — 21 checks print a skip and exit 0 when a host tool is missing.** Major, READ, carded FIX115.
`tests/check-ios.sh:113-116` exits 0 without an iOS simulator SDK, and CI runs it with no require flag; `tests/check-evidence.sh:10` exits 0 without `python3`; `tests/check-ref-metal.sh:633-648` and `tests/check-metal-shim.sh:62-82` skip without a Metal compiler. The ref_metal host tests (`tests/metal/*`) therefore run only on a capable host, and no coverage number measures the 66 files of `src/client/refresh/metal/`. Only `UPSCALE_METALFX_REQUIRE` (`tests/check-upscale-metalfx.sh:84`) turns a skip into a failure.

**AR-03 — The unit suite never runs under ASAN or UBSAN.** Major, READ, carded FIX116.
No `ASAN`/`UBSAN` in `scripts/test.sh`, `scripts/gate-checks.sh`, `scripts/coverage.sh` or `.github/`. The loader and parser bounds class was fixed by instance seventeen times in the window (FIX11, 13, 14, 15, 19, 20, 21, 22, 23, 25, 26, 30, 31, 34, 35, 39, 64) and caught again in review in TASK42, 44, 85, 87 and FIX19, 20, 25, 43. The hostile-input unit cases exist; an out-of-bounds read in them is reported only if it changes a pin.

### Guards a placement or a rename defeats

**AR-04 — The cvar and command documentation check misses `.m` files, split lines and non-literal names, and its baseline holds a fork name.** Major, LIVE, carded FIX117.
`tests/check-cvar-docs.sh` greps `--include='*.c'` for a literal name on the call's line. Three mutants each gave `check-cvar-docs: ok`, rc 0: an undocumented `ri.Cvar_Get` in `rmb_device.m`, a name on the line after `Cvar_Get(`, and a name passed in a variable. `tests/baselines/undocumented-cvars.txt:140` lists `r_rt_denoiser`, which is fork code (the video menu reads it without creating it, `videomenu.c:1165`, for TASK113 to create), so the baseline will let that cvar land undocumented. `getenv("MTL_GPU_TIME")` (`rmb_device.m:586`, `rmb_encode.m:531`) and `getenv("MGL_READBACK_DUMP")` (`gl1/metal/mgl_frame.c:584`) configure renderer behaviour outside the cvar surface and are not in doc/040.

**AR-05 — Ten checks carry no mutant, and two are defeated by a placement.** Major, LIVE, carded FIX120.
A `mutant|mutate` count of 0 for doc-links, cvar-docs, removed-deps, savegame-tables, workflow-pins, pins, video-menu, runbook-index, title-fit and image-decoder, against 79 in check-bench and 50 in check-ref-metal. `echo SDL2 >> scripts/setup.sh` passes `check-removed-deps.sh` (it scans `src`, `Makefile`, `.github` and `stuff/misc` only), and a new `run: curl ... | sh` step passes `check-workflow-pins.sh` (it checks references, not commands). Cards of this class: TASK13, 14, 39, 48, 66, 84, 88, 130.

**AR-06 — Facts the docs keep by hand drift from the code.** Major, READ, carded FIX121; one instance this-review.
Review caught a doc sentence written from memory or the diff summary in TASK21, 27, 48, 49, 51, 67, 82 and 130; only FIX70's instance got a check (`check-runbook-index.sh`). `check-doc-links.sh` ignores anchors (a link to a missing heading passes). Instance fixed here: CLAUDE.md never named `ref_metal`, and its `make` line listed only gl1, gl3 and soft (`Makefile:647` `all:` has no ref_metal; the target is at `:1023`). CLAUDE.md now names `ref_metal`, `make ref_metal`, `vid_renderer metal` and `WITH_METAL_SHIM`.

### Identity and trust at the push boundary

**AR-07 — The commit hook skips every check, identity included, for `scripts/`, `stuff/`, `.githooks/` and `third_party/`; the push hook scans no file contents.** Major, LIVE, carded FIX114.
`.githooks/pre-commit:39` runs the invariants only when a staged path matches `^(src|doc|docs|tests)/|\.md$|^(Makefile|\.github/)|^\.claude/|^\.gitignore$`; the auditor confirmed SKIPPED for `scripts/foo.sh`, `stuff/x.c`, `.githooks/pre-push`, `third_party/x` and a scenario `.cfg`. A commit that edits `.githooks/*` edits its own guard and is skipped by that line. `.githooks/pre-push:280-367` scans ref names, commit headers, messages and tags, not the blobs a commit adds, and a branch push comes before the gate.

**AR-08 — The push hook's `claim/*` and `gate-clear/*` exemption outlived its reason.** Major, LIVE, carded FIX114.
`.githooks/pre-push:305` skips the object scan for those prefixes (TASK77, #180); decision 0005:80-82 says the exemption ends when the tooling offers a pseudonymous identity. The identity shim does: 0 of the 79 tracked tips carry the host name, the user name or the home path (checked without printing a value). A commit pushed under the prefixes counts as published, so a later push of it to a PR branch or `main` is not scanned either.

**AR-09 — The tracked agent settings pre-approve `git diff`, `git log` and `git show` with any argument, and those accept `--output=<path>`.** Major, LIVE for git and NEEDS-VERIFY for the harness, decision INFORM14.
`.claude/settings.json:5-7` (TASK37, #79). `git log -1 --format=%s --output=<scratch file>` wrote the file; whether the harness counts `--output` as covered by the rule is the stated experiment on INFORM14. Permission settings are the owner's; the review does not change them.

**AR-10 — The session-start hook fast-forwards main, so merged hook and gate code runs on the host in the same session.** Major, READ, decision INFORM15.
`.claude/hooks/checkout-current.sh:72,95` (TASK125, #289); the Stop hook then runs eight `tests/check-*.sh` from the new tree (`.claude/settings.json:56-59`, `stop-invariants.sh:42-49`). With the loop merging its own pull requests (record 0004), review and the merge gate are the only barrier between a public pull request and code on the owner's host. INFORM15 states the options, defaulting to no change.

**AR-11 — The commit hook's secret pattern misses fine-grained GitHub tokens.** Minor, LIVE, carded FIX114.
`.githooks/pre-commit:34` matches `gh[pousr]_[A-Za-z0-9]{36}`; a staged synthetic `github_pat_` line gave 0 matches.

### Second copies of one mechanism

**AR-12 — The BSP lump, visibility, node and face-edge checks live only in ref_metal's fourth copy of the loader.** Major, READ, carded FIX118.
`src/client/refresh/metal/rm_model.c` (marked "from gl1_model.c" 14 times) adds `RM_CheckLumps` (`:1079`), `RM_CheckVis` (`:1195`), `RM_CheckNodes` (`:1219`) and `RM_CheckFaceEdges` (`:594`); gl1, gl3 and soft have no lump bounds and are safe "only because of the call order" (`docs/audits/asset-loaders.md:281,302`). Only `Mod_CheckNodeTree` (`files/models.c:437`) reached the shared layer.

**AR-13 — Three readers of the pin scenario format, each with its own filter.** Major, READ, carded FIX122.
`scripts/pins.sh:371-395` refuses `pins_*`, the driver cvars, the path cvars and `..`; `scripts/bench/adapters/quake2.sh:615-640` claims the same with its own list; `scripts/metalfx-report/mfx.py` drops malformed lines (`:847-856`), does not refuse the path cvars or `pins_*` (`:671-687`), and its `VALUE_RE` (`:46`, `^[A-Za-z0-9_.-]+$`) allows `..`.

**AR-14 — Five hand-rolled watchdogs for bounded runs.** Major, READ, carded FIX122.
`scripts/pins.sh:409-428`, `scripts/ios-sim.sh:168-210`, `scripts/ios-device.sh:104-132`, `scripts/bench/adapters/quake2.sh:595-612` (TERM only) and `bench_stop_group` in `scripts/bench/core/common.sh`, the only one that stops a whole process group.

**AR-15 — Code compiled outside the Makefile in 16 scripts.** Minor, READ, carded FIX123.
41 compiler calls; `scripts/pins/framediff.c`, which decides whether a pin passes, is built four ways with no `-Wall`; `tests/check-ref-metal.sh:114-124` keeps its own source list apart from `REFMETAL_OBJS_` (`Makefile:1416`). No shipped target is built outside make (decision 0002 holds).

**AR-16 — The agent allow list is pinned word for word in two checks.** Minor, READ, carded FIX123.
`tests/check-checkout-current.sh:403-418` and `tests/check-memory-rules.sh:123-231`.

### ref_metal's internal structure

**AR-17 — The ray-traced effects have no seam: each one edits the same five hub files.** Major, READ, carded TASK179.
TASK55, 57, 58 and 111 each changed `Makefile`, `rm_main.c`, `rm_gbufscene.c`, `rm_rtscene.c` and `rmb_gbuf.m`. Init goes two ways (`rm_rtscene.c:90-92`, `rm_main.c:371-373`), each frame call is hand-written (`rm_main.c:932-938`), and the shadows, AO, water order and a pipeline swap sit in Objective-C (`rmb_gbuf.m:773-822`, `:950`) where no host test reaches them. Parallel ray-tracing lanes conflict in these files on every card.

**AR-18 — The backend `.m` files declare 32 shared functions by hand, ARC qualifiers included.** Major, LIVE, carded FIX119.
38 hand-written non-static prototypes in `metal/*.m`, 32 in no header; `rmb_watch` in five files; `rmb_frame_ring_take(..., __strong id<MTLBuffer> *, ...)` copied into `rmb_gbuf.m:65` and `rmb_rt.m:75`, where a copy that drops `__strong` still compiles and defaults to `__autoreleasing`. No `-Wmissing-prototypes`.

### Minor

**AR-19 — `docs/DEPENDENCIES.md` lists `ruby` but not `python3`, `perl` or `xcrun simctl`, which the gate and harness use.** Minor, READ, carded FIX123.

**AR-20 — CLAUDE.md says the macOS build needs nothing beyond Xcode's toolchain; CI installs GNU Make from Homebrew** (`.github/workflows/macos.yml:30-39`). Minor, READ, carded FIX123.

**AR-21 — `.devcontainer/devcontainer.json:4` installs Linux GL packages**, a leftover under record 0008. Minor, READ, carded FIX123.

**AR-22 — One coverage floor for the whole suite** (`scripts/coverage-floor`: llvm-cov 33 against 33.73%, gcov "NOT measured"). Minor, READ, carded FIX123.

**AR-23 — The comment at `src/game/g_main.c:47-56` is wrong:** under `-fvisibility=hidden` (`Makefile:354`) the game module always uses its own `Sys_GetRandomBytes`. Minor, READ, carded FIX123.

## 4. Guidelines-compliance spot-check

| Rule (CLAUDE.md) | Held? |
|---|---|
| No change to the savegame format, protocol or a module-API struct without a card | Held: no commit in the window touches `ref.h`, `game.h`, `SAVEGAMEVER` or a `svc_`/`clc_`/`PROTOCOL_VERSION`; the savegame tables only gained entries (FIX6) |
| GNU Make is the only build system (0002) | Held for every shipped target, iOS included (`STATIC_MODULES`, `Makefile:1575-1611`); tools and host tests are compiled by scripts (AR-15) |
| New cvars and commands documented in doc/040 and doc/050 | Held for every literal `Cvar_Get` the check can see; the check cannot see all of them (AR-04) |
| The gate must pass on every commit | Held by the loop's merge gate on the merged head; not by CI or the ruleset (AR-01) |
| No host name, user name or home path in anything public | Held at the head (`tests/check-no-local-identity.sh` exits 0); two routes around the hooks exist (AR-07, AR-08) |
| Vendored SDL3 byte-identical to its manifest | Held; a one-line mutant is refused (`src/SDL.c: FAILED`) |
| Apple platforms only (0008) | Held in CI; a Linux dev container and the Linux-runner Cppcheck remain (AR-21; Cppcheck builds nothing) |

## 5. What is genuinely sound (and why)

- **The module APIs stayed frozen.** ref_metal, the MetalFX client path (decision 0012) and the record format all fit inside `API_VERSION` 8; capabilities reach the client only through cvars (`r_metalfx_modes`, `rm_fxscene.c:79-134` to `videomenu.c:339`). Keep that channel.
- **The C to Objective-C fence in ref_metal.** No `.c` file includes a Metal header; every shared `id<MTL…>` prototype is under `__OBJC__`. Decisions are pure C called from the glue (`RM_GB_Choose` calling `rm_gb_path`), which is what lets them be tested without a GPU. AR-17 and AR-18 extend this fence; they must not break it.
- **`STATIC_MODULES` links each module with `ld -r -exported_symbol`**, so each copy of `shared.c` stays private to its module; iOS depends on this.
- **Untrusted-input handling added in the window is careful:** the pin harness latches its paths from the command line before any cfg or stufftext runs (`cl_pins.c`); `upscale_textures` refuses during a demo or on a remote server and writes `O_EXCL|O_NOFOLLOW` then renames; the ImageIO decoder matches the extension to the UTI and caps side and area before allocating (`imgdecode.c`); `MGL_READBACK_DUMP` takes an absolute path in a fixed character set, opens `O_EXCL|O_NOFOLLOW` with mode 0600 and caps the count.
- **Every check builds in a scratch copy** and `gate_serial` is empty (`scripts/test.sh:163`), which is what makes the parallel gate safe.
- **Fail-closed where it was designed in:** `check-workflow-pins.sh` fails without ruby; `codeql-changes.sh` analyses on any doubt; the push hook refuses when it cannot read the host, the user or an object, and above its scan cap.
- **The host lock is single-sourced** in `scripts/bench/core/lock.sh`; pins.sh and mfx.py delegate to it.

## 6. Prevention mechanisms

Ranked by finding classes retired.

1. **A guard declares the mutants it refuses, and the gate proves it** (FIX120: `tests/lib/mutants.sh` plus `tests/check-mutants-present.sh`). Retires the "check passes without the property" class behind AR-04, AR-05, AR-07 and AR-08, and the eight earlier cards of that class.
2. **Everything the gate checks runs in CI, and a skip is a failure where the tool is required** (FIX115: `tests/check-ci-parity.sh`, `*_REQUIRE` pairs, required checks). Retires AR-01 and AR-02, and makes every other guard here hold for pull requests from outside.
3. **Hostile-input cases under the sanitizers on every run** (FIX116). Retires the bounds class at the source instead of by instance (AR-03, and the seventeen FIX cards behind it).
4. **One shared implementation, with a check that refuses a new copy** (FIX118 `check-bsp-loader.sh`, FIX122 `check-scenario-parsers.sh` and the watchdog lint, FIX119's prototype lint, TASK179's effect-table lint). Retires AR-12, AR-13, AR-14, AR-17 and AR-18.
5. **Doc facts regenerated and diffed** (FIX121 `check-doc-facts.sh`, anchors in `check-doc-links.sh`). Retires AR-06 and AR-20's kind of drift.
6. **Identity checked on every commit and every pushed blob, with no prefix exemption** (FIX114). Retires AR-07, AR-08 and AR-11.

## 7. Remediation status

| Finding | Severity | Outcome |
|---|---|---|
| AR-01 | Major | carded FIX115 (#493) |
| AR-02 | Major | carded FIX115 (#493) |
| AR-03 | Major | carded FIX116 (#494) |
| AR-04 | Major | carded FIX117 (#495) |
| AR-05 | Major | carded FIX120 (#499), parked behind the port |
| AR-06 | Major | this-review (CLAUDE.md names `ref_metal`); class carded FIX121 (#500), parked |
| AR-07 | Major | carded FIX114 (#492) |
| AR-08 | Major | carded FIX114 (#492) |
| AR-09 | Major | decision INFORM14 (#503): narrow the pre-approved git commands? Default: no change |
| AR-10 | Major | decision INFORM15 (#504): stop the session-start fast-forward of hook and gate code? Default: no change |
| AR-11 | Minor | carded FIX114 (#492) |
| AR-12 | Major | carded FIX118 (#496); fixed: the four checks are `Mod_Check*` in `files/models.c`, called by all four loaders, kept so by `tests/check-bsp-loader.sh` |
| AR-13 | Major | carded FIX122 (#501), parked |
| AR-14 | Major | carded FIX122 (#501), parked |
| AR-15 | Minor | carded FIX123 (#502), parked |
| AR-16 | Minor | carded FIX123 (#502), parked |
| AR-17 | Major | carded TASK179 (#498), after TASK56 and TASK60 |
| AR-18 | Major | carded FIX119 (#497), after TASK56 |
| AR-19 to AR-23 | Minor | carded FIX123 (#502), parked |
