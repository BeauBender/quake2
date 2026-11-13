# Unattended run prompt for the Metal port

You are working in the BeauBender/quake2 repository on the macOS build VM. Follow CLAUDE.md throughout: a worktree and a branch for every change, pull requests only, `scripts/test.sh` green on every commit, no package installs, no game data or build output committed, and nothing about other repositories in issues, pull requests, commits or files.

Goal: port the OpenGL renderer to Metal and add ray tracing, unattended, using the pin harness and the design documents on branch `metal-port-harness`. That branch is pushed, has head 734e086, sits ten commits on top of main, and has no pull request yet.

Owner decisions that come with this prompt
- Land `metal-port-harness` now, ahead of the open dependencies of TASK27.
- INFORM8 (#35), INFORM9 (#36) and INFORM7 (#7) are accepted. Close all three as accepted.
- Game data is kept outside the repository, in a directory that holds `baseq2/`. The owner supplies its path; export it as PINS_BASEDIR. It must never be copied into the repository, committed or uploaded. Keep the pin store outside the repository.

Step 1. Land the branch
1. Add a worktree for `origin/metal-port-harness`. Read `docs/plans/metal-port-harness.md` first, then `docs/plans/metal-port-cards.md`.
2. Run `scripts/test.sh`, then `make client ref_soft`. This is the first time the client is linked with the three capture hooks (`VID_WritePinFrame`, `SCR_PinsCapture`, `CL_PinsFrame`). They have only been compiled to objects on Linux. Fix any build failure on the branch, at its cause.
3. Open a pull request to main. It is partial work for TASK27 (#37) and for TASK19 (#26, the README) and must not close either card. Lock the pull request. Get the gate and CI green, then merge it the way the loop merges.

Step 2. File the cards
1. File every row of `docs/plans/metal-port-cards.md` as a card, following the "Filing rules" in that file exactly: one card per row, in the listed order, no merging and no splitting. There are 25 rows: P1 to P3, R1 to R5, S1 to S6, RT1 to RT10, and RTA.
2. Mint the ids through the loop, not by hand. Use `/dev-loop:breakdown` on the manifest if it keeps one card per row and keeps `docs/plans/metal-renderer.md` as the anchor. Otherwise use `/dev-loop:file-card` per row. RTA is an owner-gated decision card: file it with `/dev-loop:inform`.
3. Apply the "Corrections to existing cards" table by editing the titles and bodies of #28, #38 and #37, and the bodies of #35 and #26. Do not comment on #35.
4. Wire each parent to its children with `depends` labels (filing rule 6), lock every new card, and write the minted ids into the manifest's "Filed as" column and into "Cards generated" in `docs/plans/metal-renderer.md`.
5. Check the result: 25 new cards exist, all are locked, none is duplicated, and every `depends` label names a card that exists.

Step 3. Work the queue
1. Turn pull mode on (`/dev-loop:pull-start`) and work cards in dependency order, one pull request per card. TASK13 and TASK9, then TASK29, come before the harness cards, because TASK27 depends on them. Do those first.
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
