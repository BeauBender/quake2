# The MetalFX report's cvars through a console script, and chunks that combine (FIX86)

The first report on the bare-metal host could not run `rt-ao`,
`rt-ao-entities`, `rt-water` and `rt-water-entities`: the generator set
every archived `r_rt_*` and `r_metalfx*` cvar to its default with
`+set name value`, the native run of those scenarios needed 131
arguments, and the engine stops at 128 (`MAX_NUM_ARGVS` in
`src/common/argproc.c`). A run split to keep each call under 20 minutes
of host lock also had no supported way to become one report. The method
is that of [the MetalFX report](TASK114.md) and the shared runbook's
[how a run works](../plans/metal-port-harness.md#how-a-run-works).

## How the cvars reach the engine now

The generator writes the cvars a run sets as `set name value` lines into
`mfx-run.cfg` in the binary's `baseq2/` (the folder `pins.sh make-demo`
also places its script in, and which the generator already locks and
holds the `config.cfg` of), and starts the engine with one
`+exec mfx-run.cfg`. The file goes with the held `config.cfg` at the end
of the run, and one a killed run left goes at the next hold. On the
command line stay the paths, the window, renderer, sound and display-sync
cvars (the ones read before the console runs), and the tail
(`+demomap ...`). Defaults come first in the file, then the scenario's
cvars, then the row's own, as before.

| | Arguments of the largest run (engine name included) |
|---|---|
| before (`+set` pairs), the card's native `rt-ao` run | 131 |
| now, any scenario, any number of cvars | 32 (31 without the name) |

The quality records do not go through the script: `scripts/pins.sh record`
turns each `set` line of the scratch scenario into a `+set` pair, so a
record's line is 42 words of `pins.sh`'s own plus three for each cvar of
the scenario and the option. `plan` counts every engine command line a
run will start (the timed runs, the probe and each record) and prints the
largest, with its kind:
`args<TAB>N<TAB>limit 128<TAB>record of <scenario> with <option><TAB>with one +set pair per cvar the timed runs would be M`.
For the committed scenarios the largest is 105 (`rt-*`, a record) and 93
(`world`, `models`, `particles`, `skywater`, a record). `run` refuses,
exit 3 and before any engine run, a run in which any record would go over
128, naming the scenario and option. A cvar the engine reads only at init,
or one that is latched, must stay on the command line, not go in the
script (it runs after the renderer is loaded). A run whose engine log does
not show `execing mfx-run.cfg.` is an error and never a measured row, and
`--bin` is made absolute before use.
A name or value that cannot be one console token (a quote, a semicolon, a
newline, a space, an empty value, a trailing newline) is refused, exit 3, naming its cvar, as
is a scenario or option that sets one of the cvars the generator itself
passes (`vid_renderer`, `r_mode`, the window size and so on). The check
runs before any engine run, the values of the scenarios and options
(the defaults come from the first probe, which is an engine run, and are
checked after it). Both checks match the whole string, so a trailing
newline is refused as well.

## Combining chunks

`report` takes one `--dir` for each chunk of one run:

```sh
scripts/metalfx-report.sh run --out <out>/c1 --basedir <game data> --host-name <host> --scenarios rt-ao,rt-ao-entities
scripts/metalfx-report.sh run --out <out>/c2 --basedir <game data> --host-name <host> --scenarios rt-water,rt-water-entities
scripts/metalfx-report.sh report --dir <out>/c1/metalfx-<host>-<date> --dir <out>/c2/metalfx-<host>-<date> --html <file>
```

Each chunk is a call of its own, so each holds the host lock for at most
20 minutes, and each has its own `--out` (one results folder is made per
host and day). Chunks split by scenario, not by `--only`. A chunk of
another host, commit, tree state, device, family, modes, size, seed, run
count or build check, and a row two chunks both hold, is refused with exit
3 and no report written. The merged report has the rows and crops of every
chunk; its scenario tables equal those of one full run.

## Tests

All in `tests/check-metalfx-report.sh` (offline, the helper's fake engine),
each run first against the generator as it was before this card and
failing there:

- `cfg-many`, `e2e-args`, `plan-args`: a scenario of 70 cvars runs with 32
  arguments (the fake engine counts its own); a scenario as large as a
  record can be (15 added cvars) runs; plan prints the largest line and its
  kind (a record over the limit for the 70-cvar scenario).
- `e2e-record-limit`: a scenario whose record would have 165 arguments
  refuses the run, exit 3, naming scenario and option, with no engine run
  (the stub `pins.sh` fails a scratch scenario over the limit, so without
  the check the run ends in a record error instead).
- `probe-noexec`, `timed-noexec`, `e2e-noexec`: an engine that does not
  execute the script is an error, never a row. `e2e-relative`: a relative
  `--bin` from another directory runs. `cfg-link`: a link at the script's
  path is replaced, not written through. `cfg-restore-order`: the owner's
  config is put back even when the script cannot be removed.
- `cfg-unsafe`, `cfg-fixed`, `e2e-unsafe`: unsafe names and values refused
  with the cvar name.
- `cfg-script-hold`, `cfg-script-restore`, `e2e-script`: the script is
  written, complete, and removed.
- `chunk-merge`, `chunk-dup`, `chunk-commit`, `chunk-size`: two chunks give
  the full run's rows, tables and crops; a duplicate row or another commit
  or size is refused.
- Mutants: `args-set-pairs` and `args-set-pairs-run` (the `+set` pairs
  restored) are killed by `cfg-many` and `e2e-args`; the others are named
  in the file. The old argument-limit test (`e2e-limit`, `fit_options`)
  is gone, since no run can pass the limit with a script.

## The host run

Run on the bare-metal host (Apple M6, family apple5) with the screen
locked for the whole run, so every timing below was taken locked and is
not known to be comparable with an unlocked one; the run is first of all
the proof that the four scenarios now run. Build: commit `c212cf75`,
clean tree, `make client game ref_metal`. `--runs 5 --gpu-runs 3`,
1280x720, seed 1, 12 frames captured a row.

| Chunk | Scenarios | Wall time | Load average (1 min) before, after |
|---|---|---|---|
| fix86-1 | `rt-ao`, `rt-ao-entities` | 1095 s | 3.66, 2.27 |
| fix86-2 | `rt-water`, `rt-water-entities` | 1214 s | 2.08, 1.82 |

Each chunk was one `run` under the host lock, with its own `--out`; the
two were merged with `report --dir ... --dir ... --html` into one report
(exit 0, no chunk refused). Checks, by numbers:

- Status: every one of the four scenarios has 13 rows, all `ok`
  (`native` and 12 options), 52 rows, `errors 0` in both chunks. The two
  denoiser options (`den1`, `fx3-s50-den1`) are skipped for all
  scenarios, as the engine reports no `r_rt_denoiser`.
- Read-back: the engine's own value for each scenario's `r_rt_*` cvars
  (and `cl_entities`), read from a console run after `exec` of the
  scenario's file: `rt-ao` and `rt-ao-entities` `r_rt_ao 1`,
  `r_rt_ao_range 128`, `r_rt_ao_min 0.4`, `r_rt_ao_seed 1`,
  `r_rt_ao_show 0`; `rt-water` and `rt-water-entities` `r_rt_water 1`,
  `r_rt_water_normal 0.08`, `r_rt_water_reflect 0.5`, `r_rt_water_show 0`;
  `cl_entities` 0 and 1 as the files say; 20 of 20 as set. The report's
  own read-back covers each option's `r_metalfx*` cvars (every ok row
  passed it).
- Differs from native: every option row differs from `native` in quality
  (mean SSIM over the 12 option rows: `rt-ao` 0.931, `rt-ao-entities`
  0.928, `rt-water` 0.969, `rt-water-entities` 0.968; the lowest single
  frame 0.749, 0.751, 0.873, 0.873) and in fps (for example `rt-ao`
  `native` 2208 fps, `fx1-s34` 3937 fps). GPU frame time is about
  0.08 to 0.10 ms in nearly every row, `native` included, with
  occasional rows near 0.03 ms; the difference from `native` in GPU time
  is therefore small and not something to read from a locked run.
- `plan`: the largest engine command line is 105 (limit 128), a record
  of `rt-water` with `fx1-s50-interp`; the timed runs use 32.

The merged report is kept outside the repository (it holds frames).
