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

The second form rebuilds the page from existing results without an engine. The other host's
numbers show beside this host's, per scenario and option, and an option that host could not run
shows its reason; images come only from the host the report is built on. A results folder is
validated when read (a regular file, header, column count, every number, option and scenario
names that are plain names, a known status), and a malformed one is an error.

**Reruns and the engine's own state.** Every engine run starts from the engine's defaults: the
generator moves the binary's `baseq2/config.cfg` aside to `config.cfg.mfx-saved` for the whole
run, removes what the previous run saved before each engine run, sets first, to its default,
every cvar an option sets, every gating cvar a scenario sets and each `r_rt_*` or `r_metalfx*`
cvar the probe's `cvarlist` marks as archived (the `*` column; only an archived cvar can be
carried by `config.cfg`, and every `+set` costs three of the engine's 128 arguments), and puts
your file back at the end, also after an error, Ctrl-C, SIGTERM or SIGHUP. An option whose
command line would pass the engine's argument limit fails only its own row (the reason is in
`skipped.tsv`); the others, native included, still run.

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
