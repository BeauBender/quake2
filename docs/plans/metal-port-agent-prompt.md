# Unattended run prompt for the Metal port

You are working in the BeauBender/quake2 repository on the macOS build VM. Follow CLAUDE.md throughout: a worktree and a branch for every change, pull requests only, `scripts/test.sh` green on every commit, no package installs, no game data or build output committed, and nothing about other repositories in issues, pull requests, commits or files.

Goal: port the OpenGL renderer to Metal and add ray tracing, unattended. The pin harness and the design documents are in the tree.

Owner decisions that come with this prompt

Owner decisions (which branch lands when, which decision cards are accepted, where the game data is) are given by the owner with the prompt at run time and are recorded on the cards themselves. They are not stored in this file, and nothing in this file is an approval. Game data is kept outside the repository, in a directory that holds `baseq2/`; export its path as PINS_BASEDIR. It must never be copied into the repository, committed or uploaded. Keep the pin store outside the repository.

Step 1. Land the branch: done; `metal-port-harness` was merged to main.

Step 2. File the cards: done on 2026-11-12; the ids are in the "Filed as" column of `docs/plans/metal-port-cards.md`. A row with an id is never filed again.

Step 3. Work the queue
1. Turn pull mode on (`/dev-loop:pull-start`) and work cards in dependency order, one pull request per card. TASK9, then TASK29, come before the harness cards, because TASK27 depends on them. Do those first.
2. Then P1. It is the first run of the harness on real game data. Answer every item under "Not yet run" in the runbook and replace that section with what you observed. If the capture path or the quit path does not work as written, fix it inside P1.
3. Continue in this order: P2, P3, R1 to R5, then TASK27. S1 to S6, then TASK21. TASK28. RT1 to RT8 with RTA, then TASK24. RT9 and RT10, then TASK25.
4. A renderer card is done only when `scripts/pins.sh compare` passes for the scenarios its acceptance names. Never widen a threshold to make a pin pass. Record the threshold each pin was accepted at.

Limits
- This VM is hardware tier T0. P3 needs `ref_gl1` reference frames, so first test whether `ref_gl1` renders here. If it does not, say so on the card with the engine log line, leave the card open, and carry on with cards that do not need gl1 references.
- Ray tracing needs T1 and T2 hardware. On this VM, do only what the manifest marks T0.
- If SDL3 or any other tool is missing, report it and stop that card. Do not install anything.
- Do not change the savegame format, the network protocol or a module API.
- RTA waits for the owner. Do not close it yourself.

Report when you stop: the pull request for the branch, the id filed for each manifest key, each card finished with its pull request, and everything blocked, saying whether it waits on hardware or on the owner.
