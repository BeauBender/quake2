# TASK218: one native process supervisor for the harness

`scripts/lib/supervise.c` spawns a command into a process group of its own, owns its reap, bounds
it with a kqueue deadline, stops the group with TERM, a grace and KILL, forwards INT, TERM and HUP,
and writes an event stream. `scripts/lib/supervise.sh` builds it on first use. Nothing calls it yet:
the callers move in their own cards.

## Use

```sh
. scripts/lib/supervise.sh
sv=$(sv_path) || exit 1
"$sv" --timeout 600 --grace 10 --after-kill 2 --events 9 --name gate -- cmd args... 9> events.log
```

- **Times.** Seconds, `N` or `N.fff`. No deadline by default (`--timeout 0` is none too), a grace of
  10 s and an after-kill wait of 2 s.
- **`--events FD`.** An open descriptor above 2. The command does not inherit it, so the reader sees
  end of file when the supervisor exits.
- **`--name`.** The label in the supervisor's own lines on standard error. It defaults to the
  command's base name.

## Contract

| Status | When |
|---|---|
| the command's own | it exited by itself; 128+n when a signal the supervisor did not send ended it |
| 124 | the deadline stopped it |
| 128+n | a forwarded signal n (INT 130, TERM 143, HUP 129) stopped it |
| 125 | it cannot be run, or the usage is wrong |
| 126 | the group is not empty `--after-kill` seconds after the KILL, or the supervisor cannot go on watching. The command may still run, and the caller must not free what it holds |

The events are one line each:
- `spawned pid=P pgid=P` comes first.
- `deadline`, `signal name=N` (or `ignored name=N`), `term-sent name=N`, `grace-end` and
  `kill-sent` follow the stop.
- `exited status=S` is the leader's exit, seen while it is still unreaped. It comes before the
  stop's lines when the leader left members behind.
- `escaped pid=Q` comes once per process, at the first count that finds it outside the group.
- `group-empty` and then `reaped status=S` end a run. `reaped` always comes after `group-empty`.
  A group that cannot be emptied ends with `group-left count=K` and status 126.

## How it works

- **Spawn.** `posix_spawnp` with `POSIX_SPAWN_SETPGROUP`, `POSIX_SPAWN_SETSIGDEF` for every signal
  and `POSIX_SPAWN_SETSIGMASK` with an empty mask.
- **Watch.** One kqueue:
  - `EVFILT_PROC` with `NOTE_EXIT` and `NOTE_FORK` on the leader and on each process of the tree
    it can see. A fork lists the parent's children with `proc_listchildpids`.
  - `EVFILT_TIMER` for the deadline, the grace and the after-kill wait.
  - `EVFILT_SIGNAL` for INT, TERM and HUP, which `sigprocmask` blocks.
- **Reap.** At the leader's `NOTE_EXIT`, `waitid(P_PID, leader, WEXITED | WNOWAIT)` reads its status
  and leaves it a zombie. The group is counted with `proc_listpgrppids`: a listed member counts
  while no exit has been read for it and `proc_pidinfo` finds it in the group, or, when it is
  watched, until its exit is read. Only at a zero that holds (see the stated limits) is the leader
  reaped. While a member lives, neither the leader's pid nor the group's id can go to
  another process.
- **The watch table.** It holds 4096 processes. A member leaves it when its exit is read and it is
  torn down, and a full table is compacted before a new member is refused, so a long run does not
  fill it. A pid whose exit was read and that is listed again is a new process, and is watched
  again.
- **Stop.** A stop is due at the deadline, at a forwarded signal, and when the leader exits while
  members are left (status: the leader's own). It sends TERM (or the forwarded signal) and CONT to
  the group, then KILL after the grace.
- **Setup.** SIGCHLD is set to its default, so no inherited disposition can reap the leader behind
  the supervisor's back. A closed standard descriptor is opened on `/dev/null` first, for the
  supervisor and the command.

## Stated limits

- A process that moved itself into another session or group is named (`escaped pid=Q`) and never
  signalled. One that does so before the supervisor has seen it is not seen at all. That covers a
  fork, `setsid` and fork done before the parent's `NOTE_FORK` is handled, whose middle process
  has already gone.
- A signal ignored when the supervisor starts (for example SIGINT under a background job, as the
  merge gate starts the gate) stays ignored: it is named (`ignored name=INT`) and not forwarded.
  The command still gets every signal at its default.
- A count of zero is taken only once it holds. A watched member counts until its exit is read,
  even when it no longer answers, because it may have forked after the listing. A listed process
  that was already gone before its watch could be set is followed by one more listing, which sees
  what it forked. Pending exit and fork events are read before the leader is reaped. So a member
  whose TERM trap starts a background child and exits does not end the stop early; the stop waits
  for the child. Apart from the processes that escape before they are seen (above), the stop does
  not end early.
- A member that cannot be watched, because the table is full of processes that still run, wakes
  nothing when it exits. It is still counted, so the stop waits for its next timer instead of
  ending at that exit.
- A group stopped by job control (SIGTTIN, SIGTTOU) is not detected. The deadline's TERM and CONT
  still end it.

## The check

`tests/check-supervise.sh` (in `tests/ci-checks.txt`) needs `cc` and nothing else, and no lock.

- **Builds.** It builds the supervisor, its test build (`-DSUPERVISE_TEST`), `tests/supervise/fake.c`
  and `tests/supervise/supervise_test.c` with `-Wall -Wextra -Werror`.
- **The helper.** It checks that the helper builds into the cache once, keyed by the hash of the
  compiler version, the flags and the source, and that a broken source fails with no path printed.
- **Cases.** It runs every case with `/bin/bash` (3.2) and with `/bin/sh` as the caller shell. It
  runs the cases that need no test clock against the release build, every case against a build
  whose watch table holds 8 processes (`-DSV_MAXPIDS=8`), and every case again built with
  `-fsanitize=address,undefined`.
- **Mutants.** It runs the five mutants.

No case waits on time. Each waits on one of three things:
- a supervisor event;
- a line, or end of file, on a fake process's private channel. Only that process holds the far
  end, so end of file means it has exited;
- a fake's hello.

A timeout is a byte on the test clock (`SUPERVISE_TEST_CLOCK_FD`). The runner writes it only when
the supervisor cannot go on without it. The byte `P` is a barrier: its `probe` event comes once the
supervisor has handled every kernel event posted before it. A probe only observes: a batch that
holds nothing but probes does not count the group again. The byte `H` makes the next listing of
the group say `held` and wait for the byte `L`, so a case can change the group between a listing
and its count without waiting on time. The supervisor handles signals before
the clock in each batch. A last step greps the test files and the check for a sleep or `ps`.

| Case | What it shows |
|---|---|
| `exit0`, `exit3` | the status passes through; the command starts with its signals at their defaults and nothing blocked |
| `deadline` | 124; TERM reaches the leader, a child and a grandchild; nothing of the group is left |
| `term-ignored` | a member that ignores TERM is dead once `kill-sent` is read (it cannot answer a ping) |
| `signal-int`, `signal-term`, `signal-hup` | 130, 143 and 129; the signal reaches the leader, and the group empties |
| `int-ignored` | `(trap '' INT; exec ...) & wait`: INT is named as ignored, the command has INT at its default, and the deadline (the TERM without the clock) still stops it |
| `leader-first` | the leader exits with 5 while a member lives: the member gets TERM, `group-empty` comes before `reaped`, and the member is dead when `reaped` is read |
| `escaped` | a `setsid` member is named before `group-empty`, gets no signal, and still answers |
| `term-trap` | a member that survives the TERM forks a child and exits while a listing is held (`H`, `L`): no `group-empty` while the child runs; the child is KILLed after the grace; 124 and nothing left |
| `cap` | 12 short-lived members, then a member the leader's exit stops: with a watch table of 8, the stop still ends at that member's exit (`term-sent`, then `group-empty`), not at the grace |
| `closed-std` | standard input, output and error closed: status 7, and the command sees all three open |
| `stuck` | under `sandbox-exec` with `(deny signal)`, nothing can be signalled: `group-left` and 126 |
| `stderr` | a command that cannot run: 125 and exactly one `supervise: <label>:` line |

Every case also checks that the captured output (standard output and error of the caller shell,
the supervisor and the command) holds only `supervise: ` lines, so no job notice.

| Mutant (one line of `supervise.c`) | Case | Reason it fails |
|---|---|---|
| `drop-kill`: the KILL becomes signal 0 | `term-ignored` | a member that ignores TERM was alive after kill-sent |
| `reap-early`: the reap waits only for the leader's exit | `leader-first` | the leader was reaped while a member of its group was alive |
| `no-forward-term`: TERM is not watched | `signal-term` | TERM to the supervisor was not forwarded |
| `timeout-zero`: the deadline's 124 becomes 0 | `deadline` | status 0, want 124 |
| `no-wnowait`: the exit peek reaps | `leader-first` | no reaped line: the leader was reaped before its group was empty |

The mutants run in this check only. The whole check took 3 s on a development Mac. Thirty runs in a
row of every case took 2 s, with no failure.
