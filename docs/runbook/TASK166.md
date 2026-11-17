# TASK166: every bench run carries its own quiet evidence

On 2026-11-17 the unchanged binary of `rt-shadows-gpu-1920x1080` read 2.79 ms from a cold start
and 1.67 ms after a 4-minute pre-warm, with every field a run file recorded equal. The bench
harness ([`docs/plans/bench-harness.md`](../plans/bench-harness.md)) recorded no GPU state, no
clock and no other process, so the move could not be told apart from clock policy or another GPU
client. This card makes each run carry that evidence, warms the host up before a set, waits for a
quiet host before each counted run, and marks a hold that wants the host's other sessions quiet.
The method sections of the shared runbook still hold:
[how a run works](../plans/metal-port-harness.md#how-a-run-works); the bench commands are on the
user page, [Timings with bench.sh](../../doc/100_renderpins.md#timings-with-benchsh).

## Design

### The run file

A run file keeps every line it had (case, hash, commit, inputs, host, class, hash tool, kind,
index, start, `load.before`, `load.after`, `pre.*`, `metric.*`) and adds, after `load.after`:

| Key | Value |
|---|---|
| `quiet` | `yes`, or `no` when the gate ran out or a snapshot shows foreign GPU use |
| `quiet.reason` | `none`, or the reasons joined by `,`: `gate` (the gate's wait ran out), `gpu.before`, `gpu.after` (the reading after the run did not settle to `gate_gpu` or under within `gate_settle`; round 2) |
| `gpu.after.settle` | the whole seconds the reading after the run took to fall to `gate_gpu` or under (0: the snapshot at once was), or `unsettled` |
| `gate.wait` | the seconds the gate waited before the run |
| `snap.source` | `system`, or `stub` when `BENCH_SNAP_READER` made the snapshots |
| `snap.before.<key>`, `snap.after.<key>` | the snapshot just before and just after the run (keys below) |
| `freq.source` | `system`, or `stub` when `BENCH_FREQ_PROBE` made the frequencies |
| `freq.<domain>.<key>` | the frequencies over the run's window, from the probe (below), or `freq=unavailable` and `freq.reason=<word>` (the probe's own word when a dump carries one) |
| `prewarm.target` | the case's `prewarm`, in seconds |
| `prewarm.seconds` | how long the pre-warm ran |
| `prewarm.runs` | how many workload runs it made |
| `prewarm.stop` | `time` (it ran its length), `thermal` or `perf` (a warning stopped it), `none` (no pre-warm) |
| `prewarm.freq.<domain>.<key>` | the frequencies over the pre-warm's last workload run |

The prewarm lines are the same in every run of a set, so a stopped pre-warm marks the whole set.

The snapshot keys, read without root by the core (`scripts/bench/core/snap.sh`):

| Key | Read from |
|---|---|
| `load` | the one-minute load average (`sysctl vm.loadavg`) |
| `cpu.busy` | the CPU's busy percent over one second (`top -l 2 -s 1`, its second sample) |
| `mem.free` | the free memory percent the kernel's pressure level uses (`sysctl kern.memorystatus_level`) |
| `swap.used` | swap in use, MB (`sysctl vm.swapusage`) |
| `gpu.device`, `gpu.renderer`, `gpu.tiler` | the GPU's device, renderer and tiler utilization, percent (`ioreg -c IOAccelerator`, `PerformanceStatistics`) |
| `gpu.mem` | the GPU's in-use system memory, MB (the same) |
| `thermal`, `perf` | the thermal and performance warning levels, `none` when none is recorded (`pmset -g therm`) |
| `lowpower` | low-power mode, `0` or `1` (`pmset -g`) |
| `top.1` ... `top.5` | the five processes with the most CPU, `<percent> <name>`: the kernel's name for it (`ps -o ucomm=`, set at exec), never a path, an argument or a pid, scrubbed of the host's identity words |

A value that cannot be read is `unknown`. `BENCH_SNAP_READER` names an executable that prints
these lines instead (`<reader> snap`, and `<reader> gate` for the gate's two), for the self-test.

### The frequency reader

`scripts/bench/core/freq.c`, built by the core on first use with the host's `cc` into its
temporary directory (`cc -O1 freq.c -framework CoreFoundation -framework IOKit`, bounded at
120 s). It reaches IOReport through `dlopen` of `/usr/lib/libIOReport.dylib`, reads the state
residency of the `CPU Stats` / `CPU Complex Performance States` channels (one per CPU cluster) and
of the `GPU Stats` / `GPU Performance States` channel `GPUPH`, and maps each active state onto
the frequency table the power manager publishes in the I/O registry (`pmgr`'s `voltage-states*`
properties). No root, no `powermetrics`, nothing installed.

```
freq dump                  cumulative counters, one line per domain:
                           dump.<domain>=<idle ticks> <mhz>:<ticks> <mhz>:<ticks> ...
freq delta <before> <after>  the window between two dumps:
                           freq.<domain>.mhz=<residency-weighted MHz of the active states>
                           freq.<domain>.top=<percent of active time at the top state>
                           freq.<domain>.active=<percent of the window not idle>
                           freq.<domain>.max=<the top state's MHz>
freq <seconds>             dump, sleep, dump, delta
```

A domain is the IOReport channel's name in lower case (`ecpu`, `pcpu`, ... and `gpu`). A channel
that cannot be read, or a table that does not fit it, prints `freq=unavailable reason=<word>` and
the probe exits 0; a dump that says so gives the run file that word as `freq.reason`.
`BENCH_FREQ_PROBE` names an executable used in place of the built probe, for the self-test. The
probe is built at the top of the measuring path (`bench_prewarm`'s first line), before the
pre-warm, the gate and the first snapshot, also with `prewarm=0`.

### The pre-warm

The case key `prewarm` (seconds, default 240; `0` turns it off). A set starts with it, once per
`bench.sh` process, inside the same lock hold as the warm-up and measured runs (`baseline put`,
`drift` and the re-measure after a drift outside the range). The core runs the adapter's workload
in pieces until the length is reached: `adapter_prewarm <case> <build dir> <log> <seconds>` when
the adapter defines it (at most `<seconds>` per piece), else `adapter_run` with
`BENCH_RUN_KIND=prewarm`. Before each piece and at its end it reads the thermal and performance
warnings, and stops at the first one (`prewarm.stop=thermal` or `perf`). The quake2 adapter's
workload is the case's own run with display sync off (`vsync=0`) and `seconds=` cut to what is
left.

### The quiet gate

Before each counted run (not a warm-up), the core reads the GPU's device utilization and the
CPU's busy percent until both stay at or under the case's limits for `gate_hold` seconds, waiting
at most `gate_wait` seconds; a run that starts when the wait ran out is `quiet=no` with the
reason `gate`. The case keys, with their defaults:

| Key | Default | Meaning |
|---|---|---|
| `gate_gpu` | 10 | GPU device utilization, percent, at most |
| `gate_cpu` | 25 | CPU busy percent over one second, at most |
| `gate_hold` | 3 | seconds both must stay under |
| `gate_wait` | 120 | seconds the gate waits at most |
| `gate_settle` | 10 | seconds, after a run, its GPU reading may take to fall to `gate_gpu`; 0 judges the snapshot at once |

The CPU limit is the busy percent and not the load average: the one-minute average lags a minute
behind and still counts the run before. The snapshots are taken while the measured workload is
not running, so GPU use over `gate_gpu` before the run, or after it once its reading has had
`gate_settle` seconds to fall (round 2 below), is another client's (`gpu.before`, `gpu.after`). `baseline put` refuses to store a set that holds a `quiet=no` run (exit 1, and the
set is not stored); `drift` and `compare` print the flag of the runs they read.

### The quiet marker

`bench.sh lock run --quiet <label> -- <command...>` writes a marker beside the lock, `quiet` in
the lock's state directory (`BENCH_STATE`), while it holds the lock. `baseline put` and `drift`
take their lock with it. The file is written whole to a temporary file `mktemp` makes in the
state directory and renamed into place,
and holds `key=value` lines:

```
pid=<the writer's pid: the lock shell, or a measurement under an outer hold>
session=<BENCH_SESSION_ID, else CLAUDE_CODE_SESSION_ID, else none>
label=<the lock label>
start=<UTC, ISO 8601>
expires=<seconds since the epoch>
hold=<the lock's token>
token=<the marker's own token>
```

A session id is up to 128 of `A-Z a-z 0-9 . _ : -`, else `none`. `expires` is the start plus
`BENCH_QUIET_TTL` seconds (default 3600). A reader honours the marker only while `expires` is in
the future and `pid` is alive (`kill -0`), and holds the tool calls of every session but
`session`. The lock shell removes the marker on every exit (the EXIT, INT, TERM and HUP traps
that free the lock), only while it still carries its token or names its hold; a KILL leaves it,
and the pid and the expiry then end it. Under an outer hold (the lock passed through by its token)
a `--quiet` call or a measurement writes the marker only when no live one is there; a measurement
removes the one it wrote when it exits, and the outer lock shell removes one that names its hold.

## Evidence

### The frequency reader on the M6

`freq 5` with the host idle but for other lanes' light work (load 2.05), then `freq 10` from 4 s
into an engine run at full load (the `rt-shadows` scenario, 1920x1080 windowed, `r_vsync 0`,
timedemo of `demo1.dm2`: 689 frames in 1.5 s, 471.6 fps, then the console at full speed), both
under the host lock (load 2.14 before, 3.19 after):

| Domain | Idle: MHz, top %, active % | Full load: MHz, top %, active % | Top state |
|---|---|---|---|
| `eacc` (efficiency cluster) | 1566, 4.1, 42.2 | 1158, 8.0, 36.2 | 2940 |
| `pacc0` (performance cluster) | 2824, 11.6, 17.1 | 4788, 100.0, 81.5 | 4788 |
| `gpu` | 502, 0.0, 0.7 | 1735, 99.9, 100.0 | 1735 |

The tables come from `voltage-states1-sram` (7 states), `voltage-states5-sram` (20) and
`voltage-states9` (13 of the GPU's 15 active states; the two above it were never entered), found
on the `pmgr-child` node, not on `pmgr` itself, on this chip. The CPU tables are in kHz and the
GPU's in Hz; the reader tells them apart by size.

The GPU's device utilization (`ioreg`) during the run read 97; right after the engine exited it
read 100, then 0 at +0.2 s and +0.5 s, 2 at +1 s and 9 at +2 s. The driver's figure lags about
0.2 s, so the snapshot reads the CPU's one-second sample first and the GPU after it: the after
snapshot sees the GPU past the measured workload's tail. A display-synced run's figure takes
longer to fall, so the after side is judged once it settles (round 2 below); only another
client's use then counts.

### The re-measure of `rt-shadows-gpu-1920x1080`

2026-11-17, 09:03 to 10:00 UTC, on this branch's own harness (pre-warm 240 s, the quiet gate, a
snapshot and the frequency probe around every run), inside `heavy.sh` holds, three runs a set after
one warm-up run. Host class `Apple M6, Apple M6 GPU, macOS 27.2.1, physical`, token
`h-e159846095d1`. The store is this card's own (the TASK154 store was not written; its stored
set was copied before `drift` ran on the copy). The screen was unlocked (`ioreg -n Root -d1`
printed 0) before and after every batch below. No other `quake2` of anyone else was running when
a batch started (the lock holds the others off). The case copies are the committed case with
`prewarm=240` or `prewarm=0` added and `gate_gpu=40` (why, below); they have case hashes of their
own, so no stored set was replaced.

**The numbers** (`rts_ms`, the shadow pass's GPU time per frame over the 311 warm frames):

| Set | Prewarm | Runs (ms) | Median | Quiet flags | `freq.gpu` in the run window: MHz, share at top state, active | `freq.pacc0`: MHz, share at top | Load before to after |
|---|---|---|---|---|---|---|---|
| `96b9c661`, warm | 240 s, ran its length | 4.266, 4.308, 4.556 | 4.308 | yes, yes, yes | 506 to 508, 0.0 %, 32 % | 3574 to 3726, 45 % to 48 % | 1.47 to 2.20 |
| `9476feb1` (main), warm | 240 s, ran its length | 4.667, 4.713, 4.733 | 4.713 | yes, yes, yes | 510 to 512, 0.0 %, 32 % | 3644 to 3986, 47 % to 60 % | 1.85 to 3.27 |
| `9476feb1` (main), cold | none | 4.691, 4.724, 4.756 | 4.724 | yes, yes, yes | 504 to 505, 0.0 %, 31 % | 4032 to 4122, 64 % to 69 % | 1.63 to 2.55 |

- **Compare**, warm sets: `verdict=fail`, 4.308 to 4.713 ms, +9.40 percent (the case accepts 0).
  The p90 of the pass is the same (5.252 against 5.272).
- **Quiet flags.** All nine runs are `quiet=yes` (`quiet.reason=none`), gate waits of 4 to 6 s. The
  snapshots before a run read GPU device utilization 0 to 8 percent and CPU busy 5 to 17 percent,
  and the top process was the session's own tooling or `WindowServer`. After a run the GPU read 4
  to 29 percent (the highest in a cold run and in one warm one); `mds_stores` and `searchpartyd`
  were the top process in some after snapshots. Load average over the batches: 1.47 to 3.27, with
  5.26 at the start of the first batch, which the gate then waited out.
- **The pre-warm** ran its length (`prewarm.stop=time`, 10 workload runs, 242 to 243 s). Over its
  last workload run the GPU read 815 MHz and 2.4 percent at the top state before `96b9c661` and
  1643 MHz and 88.3 percent before main (the clock was not the same at the end of the two
  pre-warms), the P-cluster 2447 MHz against 4638 MHz.

**Cold against warm frequencies** (main, medians of three runs; the top state is 1735 MHz for the
GPU and 4788 MHz for the P-cluster):

| | GPU MHz | GPU at top state | GPU active | P-cluster MHz | P-cluster at top state |
|---|---|---|---|---|---|
| cold (`prewarm=0`) | 505 | 0.0 % | 31 % | 4060 | 67 % |
| warm (`prewarm=240`) | 510 | 0.0 % | 32 % | 3673 | 49 % |

**Drift against the TASK154 stored set** (run on a copy of its store, one drift run with the
committed case and pre-warm 240): `96b9c661` read 4.811 ms against the stored 1.6226 to 1.7354
(median 1.68): `drift=outside`. The run had `quiet=no`, `quiet.reason=gpu.after` (GPU device 30
percent after it). Its GPU window was 504 MHz, 0.0 percent at the top state, 32 percent active.
The re-measure that followed on the copy was also `quiet=no` on all three runs and so was not
stored.

**Verdict.**

- **The move is the GPU's clock policy, not the build and not the pre-warm.** In every run of
  this card the case's window ran the GPU at about 505 MHz and never at the top state: the case
  has display sync on, the timedemo is held to 60 frames a second, and the GPU is active 31 to
  32 percent of the window, so the clock stays at its floor. The pass then takes 4.3 to 4.8 ms.
  The stored 1.68 ms of TASK154 (and the card's 1.67 ms after a pre-warm) was taken with the
  GPU at a higher clock; nothing recorded then says which, and a rate of 1735 MHz against 505
  MHz is a factor of 3.4 in clock against 2.8 in time (4.7 ms against 1.68 ms), the same
  direction and size of effect as the TASK154 note on the MetalFX scaler (it repeats to the
  microsecond when the GPU is kept busy, and takes longer when it idles between frames).
- **Pre-warm does not make it go away.** Cold and warm main read 4.724 and 4.713 ms in the same
  clock (505 and 510 MHz). A pre-warm of full-rate runs (display sync off) leaves the GPU at the
  top state at its end (88 percent) but the clock is back at its floor in the counted run (it fell back during the gate wait
  or the warm-up, which this card did not record separately), so a pre-warm does not hold it
  up for a display-synced run.
- **So the card's "1.67 ms after a pre-warm" is not reproduced today.** It is explained as a
  run in which the GPU was clocked up (by a pre-warm with sync off, or by another client);
  with sync on and a quiet host, the clock is at the floor and the pass is 4.3 to 4.8 ms.
  Measuring the pass at the clock it will have in the game (display sync on) is what this case
  does; a number comparable across sessions needs the case to hold the clock (display sync off
  or a pinned workload around the window), which is a change to the case and is left to a card.
- **The 96b9c661 to main difference** (+0.4 ms, 9 percent) is seen with the same GPU clock
  (508 and 510 MHz) and the same frames, so it is not the clock; the runs of each build lie
  within 0.3 ms. This card cannot say whether it is the code between the two commits or the
  state the pre-warm left; it needs a repeat, with the builds alternated.

**The quiet gate's false alarm on this case.** With the committed case (`gate_gpu` 10) `baseline
put` refused three sets in a row (`quiet.reason=gpu.after`, GPU 29 to 30 percent right after
the run, the top process `mds_stores`): a display-synced run is GPU-active 32 percent of its
window, and the driver's utilization figure lags and averages, so it still reads the run's own use
1 s after the exit. Those refusals are why the copies carry `gate_gpu=40`. The tool's flag is
right to refuse an unknown reading; for a display-synced case the limit after a run wants a
value of its own, or a longer wait before the after snapshot, which is a follow-up.

The run files, the batch logs (screen state and load before and after) and the three refused
attempts are under this card's artifacts folder (`TASK166/timing/`), local only.

## Review round 1

The panel's three Importants and five of its notes, each fixed with a test in
`tests/check-bench.sh` that failed first (run against a copy of the scripts before the fix) and a
mutant it catches. No trap, signal disposition, process group or `set -m` in
`scripts/bench/core/lock.sh` changed.

| # | Finding | Change | Red first | Mutant |
|---|---|---|---|---|
| 1 | The probe's own reason was lost: a dump of `freq=unavailable reason=<w>` reached the run file as `no-dump` | `bench_freq_window` takes the reason from a dump that carries one (the before dump's first) | `t_freq_reason`: `the run file says: freq=unavailable freq.reason=no-dump` | `freq-reason-lost` |
| 2 | A top line could carry arguments: `comm` is the `argv[0]` a process wrote (`exec -a`) | the reader takes `ps -o ucomm=`, the kernel's name, set at exec; the identity scrub stays | `t_snap_argv`: `top.1=0.0 zzqfake --token=zzqsecret1 zzq@example.invalid` | `top-argv` (`ucomm` back to `comm`) |
| 3 | drift's own not-quiet guard was untested: the old mutant changed put's line and drift's | `not_quiet` names the set (`new`, `re-measured`) in its note, so each guard is a line of its own | `t_drift_quiet` passes on the old code (the guard was right); its red is the drift-only mutant | `drift-not-quiet`; `put-not-quiet` now changes put's line alone |
| 4 | `BENCH_GATE_POLL` was unchecked and uncut | refused with 2 at the start of `bench.sh` unless a number of at least 0.01; each nap is cut to the gate's time left | `t_gate_poll`: `BENCH_GATE_POLL='x' was not refused before the command (status 0)` | `gate-poll-unchecked`, `gate-nap-uncut` |
| 5 | `BENCH_QUIET_TTL` was checked only after the lock wait | checked at the start of `bench.sh`, beside `BENCH_STOP_GRACE` (the check in `bench_quiet_mark` stays) | `t_leading_zero`: `BENCH_QUIET_TTL=08 was not refused with 2 (status 0)` | `ttl-unchecked` |
| 6 | The marker's temporary file had a predictable name (`quiet.tmp.<pid>`) | `mktemp` in the state directory, then `mv` into place; `lock.log` unchanged | no test: the name is random, so there is no fixed path to plant | none |
| 7 | With `prewarm=0` the probe was built after the gate and the before snapshot | `bench_freq_init` is `bench_prewarm`'s first line, the top of the measuring path | `t_probe_first`: `the probe was built after: gate run ...\|snap run ...\|cc\|...` | `probe-after-gate` |
| 8 | `not_quiet`'s comment said the opposite of the code | the comment now says it succeeds when a run was not quiet | no test: a comment | none |
| 9 | A stand-in reader left no trace | run files carry `snap.source` and `freq.source` (`system`, or `stub`); `drift` prints `source=` and `baseline.source=`, `compare` a `source before= after=` line; `put` does not refuse a stub's set | `t_store`: `a run file has no line snap.source=stub`; `t_probe_first` (`freq.source=system` for a built probe) | `snap-source-lost` (by `t_store`), `source-stub-lost` (by `t_drift`) |

The marker reader on the plugin side is left these two, unchanged here:
- **pid reuse.** A marker whose writer was KILLed names a pid the system may give to another
  process before `expires`; `kill -0` then reads it as alive. The expiry bounds it.
- **A hold that outlives the TTL.** A measurement longer than `BENCH_QUIET_TTL` leaves a marker
  that has expired while the lock is still held; the marker is not renewed.

Each red line above is from the new tests run against a copy of the scripts at `0a3b9c38` (the
round's start), with `tests/check-bench.sh t_snap_argv t_freq_reason t_drift_quiet t_gate_poll
t_probe_first t_leading_zero t_store t_drift t_compare t_gate` under `heavy.sh`: every one failed,
`t_drift_quiet` only on the new note's text. After the fix the same tests and `t_quiet_marker`,
`t_prewarm` and `t_snap_system` passed, and the full `tests/check-bench.sh` (every test, 39
mutants and the fixture's own) ended `check-bench: ok`, each mutant caught by its test, the ten
of this round:

```
mutant put-not-quiet: caught by t_gate (gate: a set with a run outside the gate was not refused (status 0))
mutant drift-not-quiet: caught by t_drift_quiet (drift: a re-measured set that was not quiet replaced the baseline (status 0))
mutant top-argv: caught by t_snap_argv (snap argv: the top line is not the kernel's name: top.1=0.0 zzqfake --token=zzqsecret1 zzq@example.invalid)
mutant freq-reason-lost: caught by t_freq_reason (freq reason: the run file says: freq.source=stub freq=unavailable freq.reason=no-dump )
mutant gate-poll-unchecked: caught by t_gate_poll (gate poll: BENCH_GATE_POLL='x' was not refused before the command (status 0))
mutant gate-nap-uncut: caught by t_gate_poll (gate poll: a poll of 20 s was not cut to the gate's wait of 1 s)
mutant ttl-unchecked: caught by t_leading_zero (zero: BENCH_QUIET_TTL=08 was not refused with 2 (status 0))
mutant probe-after-gate: caught by t_probe_first (probe first: the probe was built after: gate run <token>|snap run <token>|cc|...)
mutant snap-source-lost: caught by t_store (store: a run file has no line snap.source=stub)
mutant source-stub-lost: caught by t_drift (drift: 12.3 gave: ... quiet=yes source=system baseline.quiet=yes baseline.source=system)
```

`t_snap_argv` starts a `sleep` of its own under `bash -c 'exec -a "<argv[0]>" sleep 30'`, points
the reader's own `ps` columns at it with a stand-in `ps` (the reader's arguments without `-A` and
`-r`, plus `-p <pid>`), and ends it by its pid. Nothing here ran the engine, and the
re-measure above ran later, with the screen unlocked.

## Rework round 2: a display-synced run is judged after its reading settles

Main was merged first (FIX91 bounded the harness's numbers). The case values were already whole
numbers of at most 9 digits; the decimals TASK166 added are now bounded too: `bench_is_dec`
(`core/common.sh`) takes a number of at most 12 characters, and `BENCH_GATE_POLL`, the gate's and
the settle's `gpu.device` and `cpu.busy`, and the quiet flag's `gpu.device` go through it; the
probe's frequency figures in `bench_freq_window` take at most 9 integer digits and 6 decimals.

**The settle fix.** On the M6, with the committed case (`vsync=1`, `gate_gpu` 10), `baseline put`
refused three sets in a row with `quiet.reason=gpu.after` at 29 to 30 percent: a display-synced
run keeps the GPU about 32 percent active, and the driver's figure lags and averages, so the
snapshot at once after the run still shows the run's own use. 14 of the 33 cases are
display-synced. Now the side after the run is judged after the figure settles (`bench_settle`,
`core/snap.sh`): when the snapshot at once is over `gate_gpu`, `gpu.device` is read again every
`BENCH_GATE_POLL` seconds until it is at or under `gate_gpu`, for at most the new case key
`gate_settle` (default 10 s, a whole number of at most 9 digits; 0 judges the snapshot at once).
`gpu.after` is in `quiet.reason` only when it never settles in that time or a reading is
unknown; the snapshot at once stays the record, and the run file adds `gpu.after.settle=<whole
seconds>|unsettled`. The before side and the gate are unchanged. The workaround of case copies
with `gate_gpu=40` (above) is history: the committed case is used as it is.

Tests (`t_settle` in `tests/check-bench.sh`, with the stub reader): a reading that falls from 30
to 5 is `quiet=yes` with a settle time and the 30 stays the record; one that stays at 30 is
refused with `gpu.after` after the full `gate_settle`; an unknown reading, at once or while
settling, is refused; `gate_settle=0` judges at once (one reading in all, the gate's); a poll of
20 s is cut to `gate_settle=1`. Mutants: `settle-no-poll`, `settle-no-bound`, `settle-not-flagged`,
`settle-nap-uncut`, `settle-zero-polls`, `poll-digits-unbounded`. The existing gate and drift tests
that expected `gpu.after` from a flat `gpu.snap` now also set the settle readings (`gpu.gate`).

**The host check.** On 2026-11-17, 14:24 to 14:37 UTC, on the M6 with the screen unlocked
(`CGSSessionScreenIsLocked` 0 before, between and after), the committed case
`rt-shadows-gpu-1920x1080` was run with its defaults (`gate_gpu` 10, `gate_settle` 10), into a
store of the card's own, by `bench.sh baseline put`. This branch's head (`779a07fb`) went first,
then `96b9c661`. Each put has its own 240 s pre-warm, its warm-up and three counted runs, all in
one lock hold. Both sets were **stored**: every one of the six counted runs reads `quiet=yes`,
`quiet.reason=none`, with `gpu.after.settle` 0 s in five runs and 2 s in one. That is the case the
first evidence step could not store at all.

| Build | `rts_ms` median | range | p90 median | fps |
|---|---|---|---|---|
| `779a07fb` (this branch) | 2.066 | 2.000 to 2.121 | 2.712 | 60 |
| `96b9c661` | 1.930 | 1.906 to 2.090 | 2.578 | 60 |

- **The GPU in the counted runs.** It ran at 502 MHz, 0.0 % of its active time at the top state (1735 MHz), as in the earlier steps: a display-synced run holds the GPU at its floor. The two pre-warms ended at 1619 MHz (89 % top) and 553 MHz (0 % top).
- **Load.** The one-minute average was 2.6 to 3.8 around the puts, and 8.5 at the start of the hold, from the jobs before it.
- **The two builds.** They were not alternated: there is one put per build. Their ranges overlap (2.000 to 2.090 ms), so this check settles nothing about the earlier +9 percent (4.308 against 4.713 ms at another GPU clock), and that stays open.
- **The level.** Both sets read about 2 ms where the evidence step read 4.3 to 4.8 ms at the same 502 MHz. That is a further sign that this case's time follows the host's state between holds, not the build. The pre-warm does not hold it either, so a display-synced case is best compared within one hold.
