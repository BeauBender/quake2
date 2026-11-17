# The MetalFX report (TASK114)

`scripts/metalfx-report.sh` makes a local HTML report that compares every MetalFX option the
renderer on this Mac supports. The owner chooses the scaler, the denoiser and whether frame
generation is offered from it, on the M6 and on modest hardware. The report holds frames made from
game data, so it stays on the machine that made it: the generator refuses an output path inside a
git work tree, through a symbolic link at any component, or that it cannot write, and nothing from
a run goes into the repository, an issue, a pull request or a release (the pull request of a
change to the generator carries numbers only).

"Inside a git work tree" means a `.git` entry at the output path or at any directory above it
(or `git rev-parse` saying so from the deepest directory that exists). A work tree that is
separated from its git directory (`core.worktree`, or a bare repository used with `--work-tree`,
as some dotfile setups do) leaves no mark on its own path, so the generator cannot see it: keep
`--out` away from such a tree yourself.

**Run it** (macOS with the Xcode tools; nothing else is installed):

```sh
make client game ref_metal
scripts/metalfx-report.sh run --out <a folder outside every repository> --basedir <game data>
```

`--runs N` (default 5) is the number of `timedemo` rounds, `--gpu-runs N` (3) the rounds that
read the GPU times, `--size WxH` (1280x720) the window, `--scenarios a,b` and `--only id,...`
narrow it. Pause other work for the run: it alternates the native run and every option run by
run, and records the `uptime` load average at each start and the engine commit.

**What it does.** It asks the engine, not a log line, what exists: one run with nothing set and
the console command `cvarlist` gives the cvars the engine has (and `r_metalfx_modes`, the bit set
of supported modes); an option runs only when every cvar it sets is in that list and, for
`r_metalfx`, its mode is a bit of `r_metalfx_modes`; then a second run with the option set asks
`cvarlist` again, and the option is kept only when each value reads back as asked. The option
table is `scripts/metalfx-report/options.tsv` (id, label, scenarios, `cvar=value` list): a row for
`r_rt_denoiser` is there already and appears on a host once the engine has that cvar, with no
change to the generator. A scenario runs when every `r_rt_*` or `r_metalfx*` cvar its file sets exists (the others,
such as the software renderer's, are harmless to ref_metal), so the `rt-*` scenarios appear with the
ray-traced effects. For each option and scenario:

- `timedemo` fps over N alternated runs, as a range (no validation layer, so the layer's cost
  is not in the number);
- the GPU time of the frame (`MTL_GPU_TIME`'s `gpu_ms ... frame` lines) and of the scaler pass
  (`metal_stats`), as a range of per-run medians, and the internal and output size;
- the quality against the same renderer at native size with `r_metalfx 0`, over the frames of
  one seeded record (`PINS_SEED`, `MTL_DEBUG_LAYER=1`, a scratch `PINS_STORE`): `framediff`'s
  score and bad fraction, and the structural measure SSIM of `scripts/metalfx-report/ssim.c`
  (luma, 8x8 windows every 4 pixels; 1 is identical);
- matched crops of one frame, the same window for every option, the window being where the
  first option differs most from native.

What no hook gives is written "not measured here" in the report with the hook that would give
it: the input-to-present delay, the presented frame rate and the interpolator's GPU time (frame
generation is not built yet), the shimmer and ghosting of a still camera (the harness's cadence
does not capture consecutive frames), and generated-frame crops.

**Output.** In `<out>`: `metalfx-<host>-<date>.html` (one self-contained file, images as base64
PNG, no external script, font or link; the generator checks that and refuses to write one that
has any) and the folder `metalfx-<host>-<date>/` with the plain-text results: `meta.tsv`
(host, device, family, `r_metalfx_modes`, commit, uptime at start and end), `results.tsv` (one
row per option and scenario), `skipped.tsv` (what did not run and why) and `crops/`.

**Merging hosts.** Copy another host's results folder (not its HTML) to this one and pass it:

```sh
scripts/metalfx-report.sh run --out <folder> --basedir <game data> --other <that host's results folder>
scripts/metalfx-report.sh report --dir <this host's results folder> --other <the other's> --html <file>
```

The second form rebuilds the page from existing results without an engine.

**Chunks of one run.** A run split so that no call holds the host lock for more than 20 minutes
(`--scenarios a,b` per call, each call with an `--out` of its own, since one results folder is
made per host and day) is combined by `report` with one `--dir` for each chunk, no hand merge:

```sh
scripts/metalfx-report.sh run --out <out>/c1 --basedir <game data> --host-name <host> --scenarios rt-ao,rt-ao-entities
scripts/metalfx-report.sh run --out <out>/c2 --basedir <game data> --host-name <host> --scenarios rt-water,rt-water-entities
scripts/metalfx-report.sh report --dir <out>/c1/metalfx-<host>-<date> --dir <out>/c2/metalfx-<host>-<date> --html <file>
```

`report` refuses (exit 3) a chunk of another host, engine commit, tree state, device, size, seed,
run count or build check, and a row (option and scenario) that two chunks both hold; nothing is
averaged or chosen between chunks. Chunks are split by scenario, never by `--only`, because every
chunk runs the native row of its scenarios. The merged page has the crops of every chunk, and its
rows are those one full run gives ([FIX86](FIX86.md)). The other host's
numbers show beside this host's, per scenario and option, and an option that host could not run
shows its reason; images come only from the host the report is built on. A results folder is
validated when read (a regular file, header, column count, every number, option and scenario
names that are plain names, a known status), and a malformed one is an error.

**Reruns and the engine's own state.** Every engine run starts from the engine's defaults: the
generator moves the binary's `baseq2/config.cfg` aside to `config.cfg.mfx-saved` for the whole
run, removes what the previous run saved before each engine run, sets first, to its default,
every cvar an option sets, every gating cvar a scenario sets and each `r_rt_*` or `r_metalfx*`
cvar the probe's `cvarlist` marks as archived (the `*` column; only an archived cvar can be
carried by `config.cfg`), and puts your file back at the end, also after an error, Ctrl-C,
SIGTERM or SIGHUP. The cvars a run sets do not go on the command line (the engine stops at 128
arguments, and a `+set` costs three of them): the generator writes them as `set name value`
lines into `mfx-run.cfg` in the binary's `baseq2/` for that run, runs the engine with one
`+exec mfx-run.cfg`, and removes the file with the held `config.cfg`. Only the window, renderer,
sound and display-sync cvars stay on the command line. A name or value that cannot be one console
token (a quote, a semicolon, a newline, a space) refuses the run, naming its cvar. `plan` prints
the argument count of the largest engine command line of any run (a timed run, a probe or a record) and its kind;
`run` refuses a run whose record would go over the limit. See [FIX86](FIX86.md).

**One run per engine binary at a time.** The generator holds a non-blocking exclusive lock on
the binary's `baseq2/` folder and on this checkout's `scripts/pins/scenarios/` folder for the
whole run, and a run that cannot take either is refused with exit 3, naming the one it found held.
The locks are on the folders themselves, so no lock file is made and nothing becomes untracked.
Without this, a second run would take the first run's saved `config.cfg` for a killed run's and
lose it.

**After a kill.** A SIGKILL or a power cut cannot be caught. It leaves your file at
`config.cfg.mfx-saved` (and the run's own `config.cfg` beside it, if the engine had written
one). Under the lock a `config.cfg.mfx-saved` can only be a killed run's, so the next run puts it
back before it does anything. When both files exist, neither is deleted: the newer `config.cfg`
is kept as `config.cfg.mfx-newer` (`.1`, `.2` when that name is taken), the saved one becomes
`config.cfg`, and the run says both names; merge what you want from the kept one by hand.
SIGTERM, SIGHUP and Ctrl-C during a quality record also end the engine `pins.sh` started (it runs
in a process group of its own, which is ended). `--replace` empties the earlier run's `crops/` and
result files, and a crop is embedded only for a row that ran. The freshness check and the commit
in `meta.tsv` are those of the tree the binary is in (`<tree>/release/quake2`, run through
`--bin` or `PINS_BIN` too); a binary outside such a tree is not checked, is recorded with the commit
`unknown`, and the generator says so. The scratch scenarios the quality records need are written to
`scripts/pins/scenarios/mfxr-*.cfg` (pins.sh reads scenarios only from there; git ignores them) and
removed when the run ends or at the start of the next.

**Self-test.** `tests/check-metalfx-report.sh` (in the gate) needs no engine and no game data.
It feeds synthetic results, synthetic TGAs and a fake engine, in a scratch directory with the
caller's `GIT_*` variables and git configuration removed, and then runs every assertion against a
mutant of the line it covers.


## The M6 report

One run on the M6 (Apple M6, family apple5, `r_metalfx_modes` 31), engine commit c212cf75 plus the
generator of the current main, 1280x720, 5 `timedemo` rounds and 3 GPU rounds, seed 1, run in chunks
of one scenario (each chunk one lock hold, with a 240 s pre-warm of the same engine first).
The report is local to that machine; it is not in any repository, issue or pull request.

**Scenarios run:** `world`, `world-lmcubic`, `rt-ao`, `rt-ao-entities`, `rt-shadows`,
`rt-shadows-entities`, `rt-water`, `rt-water-entities`. Each chunk took 11 to 14 minutes and the host
lock was held by other lanes between them, so the rest were left out to fit the host lock:
`models`, `particles`, `skywater`, `world-fullbright`, `world-scale8`, `world-metalfx1` to `3`,
`world-metalfx-interp`, `particles-cap`, `dlights`, `dlights-lmcubic`, `cinematic`, `hud2d`,
`hud2d-sharp` and `hud2d-scale8` have no M6 numbers yet.

**Two pages.** The first five scenarios ran on engine commit 3c8fb3c1; the last three ran after the
first runbook commit of this section, so their engine commit reads 8ae7a9f9 (the same binary, a
runbook-only change). `report` refuses chunks of two commits, so the run is two local pages, one
per commit, not one.

**Skipped options (every scenario):** `den1` and `fx3-s50-den1`, because the engine reports no cvar
`r_rt_denoiser`. Every `r_metalfx` mode 1, 2 and 3 at scales 0.75, 0.50 and 0.34, and the three
frame-generation rows (`-interp`), ran.

**Numbers.** fps and GPU times are medians with the range of the runs in brackets (fps over 5
rounds, GPU times over 3 per-run medians); ssim is the mean over the frames of one seeded record
against native size (1 is identical). "frame" is the GPU time of the whole frame, "scaler" the
scaler pass. Frame times of 0.03 to 0.10 ms differ between runs of one option by the
timestamp resolution of the counter, so read them as a band, not a difference.

| scenario | option | fps | frame ms | scaler ms | ssim |
|---|---|---|---|---|---|
| rt-ao-entities | native | 1548 (1531-1614) | 0.026 (0.025-0.026) | - | native |
| rt-ao-entities | fx1-s34 | 2452 (2443-2470) | 0.053 (0.029-0.088) | 1.106 (0.382-1.160) | 0.905164 |
| rt-ao-entities | fx1-s50 | 2201 (2187-2259) | 0.087 (0.027-0.088) | 0.811 (0.390-1.362) | 0.919205 |
| rt-ao-entities | fx1-s50-interp | 2252 (2237-2274) | 0.055 (0.053-0.088) | 0.791 (0.746-1.272) | 0.919205 |
| rt-ao-entities | fx1-s75 | 1813 (1629-1862) | 0.029 (0.027-0.086) | 0.992 (0.525-1.214) | 0.921912 |
| rt-ao-entities | fx2-s34 | 1488 (1463-1508) | 0.088 (0.029-0.089) | 0.172 (0.108-0.333) | 0.914942 |
| rt-ao-entities | fx2-s50 | 1224 (1200-1239) | 0.087 (0.027-0.088) | 0.163 (0.144-0.172) | 0.934878 |
| rt-ao-entities | fx2-s50-interp | 1230 (1196-1244) | 0.088 (0.027-0.089) | 0.179 (0.139-0.208) | 0.934883 |
| rt-ao-entities | fx2-s75 | 908 (889-920) | 0.088 (0.027-0.089) | 0.280 (0.205-0.298) | 0.949315 |
| rt-ao-entities | fx3-s34 | 1460 (1441-1491) | 0.029 (0.029-0.089) | 0.171 (0.108-0.199) | 0.914942 |
| rt-ao-entities | fx3-s50 | 1217 (1178-1230) | 0.029 (0.028-0.089) | 0.142 (0.140-0.192) | 0.934878 |
| rt-ao-entities | fx3-s50-interp | 1222 (1190-1233) | 0.029 (0.029-0.029) | 0.141 (0.139-0.143) | 0.934878 |
| rt-ao-entities | fx3-s75 | 968 (929-970) | 0.088 (0.028-0.096) | 0.247 (0.212-0.248) | 0.949317 |
| rt-ao | native | 2101 (2063-2126) | 0.026 (0.026-0.085) | - | native |
| rt-ao | fx1-s34 | 3704 (3588-3807) | 0.101 (0.101-0.102) | 0.582 (0.430-0.583) | 0.908453 |
| rt-ao | fx1-s50 | 3062 (3035-3132) | 0.098 (0.058-0.102) | 0.724 (0.432-0.732) | 0.922465 |
| rt-ao | fx1-s50-interp | 3049 (3009-3190) | 0.101 (0.096-0.101) | 0.728 (0.725-0.761) | 0.922465 |
| rt-ao | fx1-s75 | 2259 (2252-2360) | 0.096 (0.095-0.097) | 0.799 (0.632-1.081) | 0.924821 |
| rt-ao | fx2-s34 | 1705 (1637-1714) | 0.096 (0.096-0.096) | 0.203 (0.202-0.250) | 0.918190 |
| rt-ao | fx2-s50 | 1315 (1293-1320) | 0.095 (0.042-0.095) | 0.208 (0.207-0.248) | 0.938030 |
| rt-ao | fx2-s50-interp | 1307 (1300-1317) | 0.095 (0.033-0.095) | 0.208 (0.194-0.209) | 0.938030 |
| rt-ao | fx2-s75 | 976 (952-993) | 0.094 (0.094-0.094) | 0.259 (0.245-0.279) | 0.951856 |
| rt-ao | fx3-s34 | 1685 (1625-1693) | 0.096 (0.095-0.097) | 0.323 (0.246-0.328) | 0.918190 |
| rt-ao | fx3-s50 | 1310 (1283-1330) | 0.096 (0.096-0.096) | 0.210 (0.194-0.212) | 0.938030 |
| rt-ao | fx3-s50-interp | 1305 (1285-1328) | 0.096 (0.096-0.097) | 0.208 (0.193-0.214) | 0.938030 |
| rt-ao | fx3-s75 | 1049 (1016-1052) | 0.058 (0.030-0.095) | 0.260 (0.206-0.310) | 0.951856 |
| rt-shadows-entities | native | 2208 (2094-2297) | 0.080 (0.025-0.084) | - | native |
| rt-shadows-entities | fx1-s34 | 2580 (2487-2650) | 0.055 (0.054-0.088) | 0.996 (0.995-1.180) | 0.941198 |
| rt-shadows-entities | fx1-s50 | 2435 (2368-2571) | 0.087 (0.055-0.093) | 1.138 (0.767-1.147) | 0.965817 |
| rt-shadows-entities | fx1-s50-interp | 2506 (2435-2561) | 0.090 (0.089-0.090) | 1.232 (1.135-1.246) | 0.965817 |
| rt-shadows-entities | fx1-s75 | 2215 (2107-2244) | 0.086 (0.029-0.087) | 1.517 (0.463-1.615) | 0.978913 |
| rt-shadows-entities | fx2-s34 | 1633 (1588-1652) | 0.054 (0.029-0.056) | 0.207 (0.197-0.327) | 0.944213 |
| rt-shadows-entities | fx2-s50 | 1364 (1295-1378) | 0.089 (0.088-0.096) | 0.249 (0.247-0.297) | 0.968990 |
| rt-shadows-entities | fx2-s50-interp | 1359 (1330-1384) | 0.088 (0.027-0.090) | 0.230 (0.140-0.296) | 0.968990 |
| rt-shadows-entities | fx2-s75 | 1038 (1022-1052) | 0.029 (0.029-0.088) | 0.202 (0.201-0.344) | 0.984230 |
| rt-shadows-entities | fx3-s34 | 1602 (1584-1614) | 0.088 (0.088-0.090) | 0.202 (0.201-0.246) | 0.944213 |
| rt-shadows-entities | fx3-s50 | 1381 (1354-1389) | 0.029 (0.029-0.029) | 0.139 (0.137-0.154) | 0.968990 |
| rt-shadows-entities | fx3-s50-interp | 1348 (1269-1375) | 0.089 (0.029-0.089) | 0.213 (0.136-0.247) | 0.968990 |
| rt-shadows-entities | fx3-s75 | 1095 (1082-1128) | 0.029 (0.029-0.092) | 0.201 (0.199-0.259) | 0.984231 |
| rt-shadows | native | 3146 (2996-3411) | 0.051 (0.051-0.085) | - | native |
| rt-shadows | fx1-s34 | 4077 (3765-4201) | 0.096 (0.057-0.097) | 0.523 (0.506-0.554) | 0.943756 |
| rt-shadows | fx1-s50 | 3626 (3462-3937) | 0.096 (0.096-0.097) | 0.681 (0.649-0.685) | 0.967787 |
| rt-shadows | fx1-s50-interp | 3533 (3445-3937) | 0.100 (0.097-0.101) | 0.620 (0.506-0.647) | 0.967787 |
| rt-shadows | fx1-s75 | 3104 (2835-3205) | 0.097 (0.095-0.098) | 0.958 (0.714-0.991) | 0.979988 |
| rt-shadows | fx2-s34 | 1776 (1744-1790) | 0.098 (0.097-0.100) | 0.325 (0.324-0.331) | 0.946759 |
| rt-shadows | fx2-s50 | 1457 (1412-1524) | 0.094 (0.058-0.095) | 0.302 (0.301-0.391) | 0.971184 |
| rt-shadows | fx2-s50-interp | 1460 (1441-1485) | 0.058 (0.042-0.096) | 0.300 (0.245-0.399) | 0.971184 |
| rt-shadows | fx2-s75 | 1131 (1124-1150) | 0.029 (0.029-0.029) | 0.206 (0.203-0.246) | 0.985681 |
| rt-shadows | fx3-s34 | 1785 (1758-1799) | 0.095 (0.089-0.098) | 0.324 (0.244-0.324) | 0.946759 |
| rt-shadows | fx3-s50 | 1478 (1450-1518) | 0.096 (0.058-0.096) | 0.248 (0.243-0.251) | 0.971184 |
| rt-shadows | fx3-s50-interp | 1485 (1435-1488) | 0.058 (0.058-0.097) | 0.249 (0.245-0.392) | 0.971184 |
| rt-shadows | fx3-s75 | 1235 (1220-1244) | 0.095 (0.095-0.096) | 0.292 (0.291-0.295) | 0.985681 |
| rt-water-entities | native | 1997 (1986-2015) | 0.026 (0.024-0.026) | - | native |
| rt-water-entities | fx1-s34 | 2650 (2580-2713) | 0.055 (0.053-0.091) | 1.085 (1.024-1.086) | 0.945447 |
| rt-water-entities | fx1-s50 | 2487 (2478-2552) | 0.030 (0.027-0.054) | 0.705 (0.654-1.093) | 0.967927 |
| rt-water-entities | fx1-s50-interp | 2542 (2487-2610) | 0.089 (0.055-0.089) | 1.149 (0.888-1.203) | 0.967927 |
| rt-water-entities | fx1-s75 | 2167 (2075-2304) | 0.087 (0.029-0.087) | 1.028 (0.441-1.574) | 0.980313 |
| rt-water-entities | fx2-s34 | 1610 (1588-1621) | 0.088 (0.029-0.089) | 0.200 (0.105-0.248) | 0.948202 |
| rt-water-entities | fx2-s50 | 1351 (1310-1359) | 0.048 (0.029-0.089) | 0.245 (0.146-0.282) | 0.970803 |
| rt-water-entities | fx2-s50-interp | 1359 (1330-1359) | 0.029 (0.028-0.089) | 0.138 (0.137-0.206) | 0.970799 |
| rt-water-entities | fx2-s75 | 1007 (987-1033) | 0.088 (0.029-0.088) | 0.274 (0.202-0.278) | 0.985285 |
| rt-water-entities | fx3-s34 | 1621 (1602-1644) | 0.089 (0.029-0.089) | 0.201 (0.196-0.202) | 0.948202 |
| rt-water-entities | fx3-s50 | 1333 (1322-1364) | 0.029 (0.027-0.096) | 0.139 (0.135-0.210) | 0.970803 |
| rt-water-entities | fx3-s50-interp | 1333 (1322-1348) | 0.088 (0.030-0.089) | 0.210 (0.148-0.213) | 0.970803 |
| rt-water-entities | fx3-s75 | 1058 (1024-1068) | 0.029 (0.029-0.095) | 0.206 (0.201-0.258) | 0.985286 |
| rt-water | native | 2723 (2620-2756) | 0.051 (0.026-0.090) | - | native |
| rt-water | fx1-s34 | 4388 (4227-4563) | 0.096 (0.094-0.096) | 0.476 (0.472-0.489) | 0.947167 |
| rt-water | fx1-s50 | 3684 (3552-3849) | 0.096 (0.096-0.096) | 0.600 (0.575-0.617) | 0.969445 |
| rt-water | fx1-s50-interp | 3552 (3552-3704) | 0.095 (0.095-0.095) | 0.578 (0.565-0.581) | 0.969445 |
| rt-water | fx1-s75 | 2778 (2734-2847) | 0.095 (0.094-0.095) | 0.876 (0.869-0.877) | 0.981171 |
| rt-water | fx2-s34 | 1697 (1652-1736) | 0.097 (0.097-0.100) | 0.325 (0.324-0.327) | 0.949870 |
| rt-water | fx2-s50 | 1409 (1351-1418) | 0.095 (0.029-0.095) | 0.241 (0.144-0.286) | 0.972467 |
| rt-water | fx2-s50-interp | 1384 (1362-1409) | 0.095 (0.029-0.095) | 0.242 (0.236-0.299) | 0.972467 |
| rt-water | fx2-s75 | 1075 (1054-1111) | 0.095 (0.029-0.095) | 0.237 (0.214-0.246) | 0.986388 |
| rt-water | fx3-s34 | 1685 (1685-1714) | 0.096 (0.058-0.096) | 0.325 (0.323-0.326) | 0.949870 |
| rt-water | fx3-s50 | 1400 (1392-1421) | 0.095 (0.029-0.096) | 0.241 (0.133-0.298) | 0.972467 |
| rt-water | fx3-s50-interp | 1398 (1386-1424) | 0.096 (0.058-0.096) | 0.243 (0.240-0.250) | 0.972467 |
| rt-water | fx3-s75 | 1148 (1139-1154) | 0.029 (0.029-0.095) | 0.206 (0.204-0.260) | 0.986388 |
| world-lmcubic | native | 11113 (10600-11879) | 0.089 (0.088-0.090) | - | native |
| world-lmcubic | fx1-s34 | 6097 (5839-6207) | 0.096 (0.093-0.098) | 0.342 (0.342-0.344) | 0.947409 |
| world-lmcubic | fx1-s50 | 5940 (5694-5991) | 0.097 (0.097-0.100) | 0.362 (0.361-0.362) | 0.969647 |
| world-lmcubic | fx1-s50-interp | 5742 (5648-5940) | 0.098 (0.097-0.100) | 0.362 (0.361-0.363) | 0.969647 |
| world-lmcubic | fx1-s75 | 5425 (5104-5512) | 0.100 (0.094-0.101) | 0.407 (0.406-0.410) | 0.981365 |
| world-lmcubic | fx2-s34 | 1935 (1847-1941) | 0.092 (0.029-0.098) | 0.326 (0.143-0.328) | 0.950062 |
| world-lmcubic | fx2-s50 | 1656 (1637-1676) | 0.091 (0.056-0.092) | 0.307 (0.243-0.375) | 0.972574 |
| world-lmcubic | fx2-s50-interp | 1637 (1602-1668) | 0.057 (0.057-0.093) | 0.243 (0.209-0.306) | 0.972574 |
| world-lmcubic | fx2-s75 | 1343 (1322-1348) | 0.092 (0.091-0.094) | 0.343 (0.294-0.346) | 0.986464 |
| world-lmcubic | fx3-s34 | 1935 (1909-1974) | 0.097 (0.094-0.098) | 0.317 (0.247-0.324) | 0.950062 |
| world-lmcubic | fx3-s50 | 1629 (1621-1668) | 0.094 (0.057-0.094) | 0.243 (0.215-0.243) | 0.972574 |
| world-lmcubic | fx3-s50-interp | 1640 (1614-1660) | 0.057 (0.029-0.057) | 0.243 (0.157-0.243) | 0.972574 |
| world-lmcubic | fx3-s75 | 1450 (1415-1454) | 0.094 (0.029-0.094) | 0.295 (0.198-0.346) | 0.986464 |
| world | native | 13250 (13000-13510) | 0.088 (0.086-0.088) | - | native |
| world | fx1-s34 | 6380 (5889-6380) | 0.096 (0.095-0.097) | 0.331 (0.331-0.331) | 0.947339 |
| world | fx1-s50 | 6207 (5889-6264) | 0.099 (0.098-0.100) | 0.341 (0.341-0.341) | 0.969627 |
| world | fx1-s50-interp | 5991 (5602-6097) | 0.098 (0.096-0.101) | 0.341 (0.341-0.342) | 0.969627 |
| world | fx1-s75 | 5839 (5425-5940) | 0.096 (0.096-0.098) | 0.365 (0.365-0.365) | 0.981374 |
| world | fx2-s34 | 1925 (1898-1969) | 0.092 (0.056-0.092) | 0.326 (0.199-0.326) | 0.949988 |
| world | fx2-s50 | 1672 (1656-1680) | 0.092 (0.091-0.100) | 0.396 (0.390-0.396) | 0.972556 |
| world | fx2-s50-interp | 1668 (1625-1689) | 0.057 (0.056-0.096) | 0.243 (0.242-0.302) | 0.972556 |
| world | fx2-s75 | 1356 (1335-1406) | 0.092 (0.069-0.092) | 0.348 (0.345-0.349) | 0.986463 |
| world | fx3-s34 | 1914 (1893-1963) | 0.099 (0.098-0.100) | 0.317 (0.295-0.325) | 0.949988 |
| world | fx3-s50 | 1652 (1580-1680) | 0.093 (0.029-0.095) | 0.194 (0.180-0.301) | 0.972556 |
| world | fx3-s50-interp | 1672 (1584-1701) | 0.095 (0.030-0.096) | 0.249 (0.139-0.301) | 0.972556 |
| world | fx3-s75 | 1472 (1463-1501) | 0.057 (0.029-0.093) | 0.277 (0.218-0.292) | 0.986463 |

**Frequencies** (around the timed part of each chunk, after the pre-warm; median MHz and share of
active time at the top state):

| scenario | E cluster | P cluster | GPU |
|---|---|---|---|
| `world` | 1335 MHz, 7.4% | 4449 MHz, 79.9% | 1035 MHz, 32.3% |
| `world-lmcubic` | 1319 MHz, 5.7% | 4429 MHz, 78.8% | 1039 MHz, 32.9% |
| `rt-ao` | 1353 MHz, 6.8% | 4347 MHz, 75.1% | 1096 MHz, 40.3% |
| `rt-ao-entities` | 1355 MHz, 4.6% | 4084 MHz, 63.1% | 1144 MHz, 47.0% |
| `rt-shadows` | 1363 MHz, 5.8% | 4189 MHz, 68.1% | 1006 MHz, 35.8% |
| `rt-shadows-entities` | 1371 MHz, 6.8% | 4319 MHz, 73.4% | 1085 MHz, 42.8% |
| `rt-water` | 1256 MHz, 7.1% | 4001 MHz, 55.1% | 1072 MHz, 39.7% |
| `rt-water-entities` | 1357 MHz, 6.1% | 4166 MHz, 64.5% | 1112 MHz, 45.1% |

The frequencies are weighted averages over the window, from the no-root IOReport probe; the P
cluster's top state is 4788 MHz, the E cluster's 2940 MHz, the GPU's 1735 MHz.

**Load and the host.** The one-minute load average at the snapshots was 1.42 to 3.09. Spotlight workers (`mdworker_shared`, `mds_stores`) and `mediaanalysisd` were among the top five by CPU in several
snapshots, before and after runs (the percentages are not kept), so the host was not fully quiet; no Time Machine
process was seen. Low-power mode was off and no thermal
or performance warning level was recorded in any chunk. GPU utilization read 1 s after the
pre-warm and before the timed run was 20% in the `rt-ao` chunk (the reading lags the pre-warm that
had just ended) and 4 to 7% in the others.

**Screen.** The screen was unlocked at the start and the end of every chunk (checked with
`CGSSessionScreenIsLocked`); no chunk is void. A foreground gate and other lanes ran on the host
between chunks, never inside one.

**Reading.** `r_metalfx 1` runs at about half the native fps in the two `world` scenarios and above native fps
in the ray-traced scenarios at scales 0.34 to 0.50 (at 0.75 it is near native); modes 2 and 3 run at about 1000 to 1900 fps in every scenario run, at a higher ssim
than mode 1 at the same scale.
