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
| `quiet.reason` | `none`, or the reasons joined by `,`: `gate` (the gate's wait ran out), `gpu.before`, `gpu.after` (that snapshot's GPU device utilization is over `gate_gpu`) |
| `gate.wait` | the seconds the gate waited before the run |
| `snap.before.<key>`, `snap.after.<key>` | the snapshot just before and just after the run (keys below) |
| `freq.<domain>.<key>` | the frequencies over the run's window, from the probe (below), or `freq=unavailable` and `freq.reason=<word>` |
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
| `top.1` ... `top.5` | the five processes with the most CPU, `<percent> <name>`: the name only, never a path, an argument or a pid, scrubbed of the host's identity words |

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
the probe exits 0. `BENCH_FREQ_PROBE` names an executable used in place of the built probe, for the
self-test.

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

The CPU limit is the busy percent and not the load average: the one-minute average lags a minute
behind and still counts the run before. The snapshots are taken while the measured workload is
not running, so GPU use in either over `gate_gpu` is another client's (`gpu.before`,
`gpu.after`). `baseline put` refuses to store a set that holds a `quiet=no` run (exit 1, and the
set is not stored); `drift` and `compare` print the flag of the runs they read.

### The quiet marker

`bench.sh lock run --quiet <label> -- <command...>` writes a marker beside the lock, `quiet` in
the lock's state directory (`BENCH_STATE`), while it holds the lock. `baseline put` and `drift`
take their lock with it. The file is written whole to a temporary name and renamed into place,
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
snapshot sees the GPU past the measured workload's tail, and only another client's use counts.

### The re-measure of `rt-shadows-gpu-1920x1080`

pending: screen locked. When this step was reached (2026-11-17, about 04:30 UTC) the screen was
locked (`ioreg -n Root -d1` held `CGSSessionScreenIsLocked`), and a timing needs it unlocked
(the shared runbook's [how a run works](../plans/metal-port-harness.md#how-a-run-works)). To run once
it is unlocked, with a copy of the case that states `prewarm=240` (so it has a case hash, and a
baseline directory, of its own and no stored set is replaced), and one that states `prewarm=0`
for the cold run's frequencies, `BENCH_STORE` the TASK154 store:

```sh
scripts/bench/bench.sh baseline put <copy with prewarm=240> 96b9c661
scripts/bench/bench.sh baseline put <copy with prewarm=240> <main>
scripts/bench/bench.sh baseline put <copy with prewarm=0> <main>
scripts/bench/bench.sh compare <copy with prewarm=240> --before 96b9c661 --after <main>
```

Each run file then carries `freq.gpu.mhz` and `freq.gpu.top` for the cold and the warm runs, and
`prewarm.freq.*` for the end of the pre-warm.
