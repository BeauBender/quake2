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

`plan` prints the count of the largest run and what it would be with one
`+set` pair per cvar:
`args<TAB>N<TAB>limit 128<TAB>with one +set pair per cvar it would be M`.
A name or value that cannot be one console token (a quote, a semicolon, a
newline, a space, an empty value) is refused, exit 3, naming its cvar, as
is a scenario or option that sets one of the cvars the generator itself
passes (`vid_renderer`, `r_mode`, the window size and so on). The check
runs before any engine run.

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
  arguments (the fake engine counts its own), and plan prints it.
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

Not run in this lane: the screen stayed locked for the whole wait. The
four scenarios are left to be run with the command above.
