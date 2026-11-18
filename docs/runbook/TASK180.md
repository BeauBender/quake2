# TASK180: ios-sim.sh says whether the simulator's launcher crashed during its own run

The owner once came back to about ten error dialogs about the simulator's launcher (SpringBoard) after several
simulator lanes had run, and no lane could say whether its own run caused them. `scripts/ios-sim.sh` (every mode,
every SDK) now prints one line about the launcher of the device it created, before it deletes the device.

## The line

| Line | Meaning | Exit status |
|---|---|---|
| `launcher: no crash` | the launcher has the pid and the last exit status it had once the device booted | unaffected |
| `launcher: 1 crash(es): <process>, pid <old> -> <new>, last exit status <s>, seen at <time>` | it exited between the two reads | the run fails |
| `launcher: 1 crash(es) before the run: <process>, pid <n>, last exit status <s>, seen at <time>` | the baseline already reads a last exit status other than 0 | the run fails |
| `launcher: unknown (<reason>)` | the device could not be asked, or its launcher was not listed or not running at the baseline | the run fails: unknown is never a pass |

`IOS_SIM_LAUNCHER_CRASH_OK=1` (that exact value) keeps the line and lets both of the last two pass. The count is 1
because launchd keeps only the last exit; the job's `runs` counter would give the exact number (see the end).
A run that stops early on another failure prints the line too (the exit trap does), and the device is shut down and
deleted on every path, a crash included.

## The probe

Throwaway devices created by hand (`simctl create`, `bootstatus -b`, deleted on every exit), one per SDK, on this
host's iOS 27.2, tvOS 27.2 and visionOS 27.2 runtimes. Only `simctl spawn <that udid>` was used; nothing under
`~/Library` was read, and no other device was touched.

- The launcher's name differs: `launchctl list` in the device lists `com.apple.SpringBoard` on iOS,
  `com.apple.PineBoard` on tvOS and `com.apple.SurfBoard` on visionOS (it has no SpringBoard; its list shows
  SurfBoard next to backboardd and runningboardd).
- `simctl spawn <udid> launchctl list` prints `PID`, last exit status and label per job of the device's own launchd.
  After `launchctl kill SEGV system/<label>` (on the iOS and tvOS devices) the job came back with a new pid and
  status -11 (iOS: pid 71462 -> 81220; tvOS: 58537 -> 65392, `runs = 1` -> `runs = 2` in `launchctl print`). The
  visionOS device was probed for its label and list only, not killed: it is the same launchd.
- `simctl spawn <udid> log show` has the exit too (`runningboardd` "exited ... signal(2) code:SIGSEGV(11)" lines,
  `launchd_sim` "exited with exit reason"), but the text depends on the OS version and the log is the system's, a long
  free-text stream. The device's data container holds a `Library/Logs/CrashReporter` folder, empty here, and the
  script would need the host path of the device to read it.

## The source chosen, and why

The device's own launchd, through `simctl spawn <udid> launchctl list`: a baseline read after `bootstatus -b`
against the same read just before the device is deleted. A crash is a changed pid or a changed last exit status.
Why it beats the others:

- it decides from the device's own process table, never from a log line the engine prints, and needs no time window
  (a restart is a changed pid, whenever it happened);
- one bounded call on this run's UDID, nothing on the host, no path: no read of `~/Library`;
- it fails closed: no launchd answer, no label, a line that is not `<pid or -> <status>`, a launcher not running at
  the baseline, or no baseline at all is `unknown`, which fails the run;
- the log and the crash folder are free text or a host path whose format moves with the OS.

## Selftest

The fake `xcrun` (`FAKE_LAUNCHER`) answers `spawn <udid> launchctl list` for this run's UDID only (a call naming
another device fails) with each platform's label. Cases, for the iOS, Apple TV and Apple Vision Pro SDK: no crash, a
crash (and the device is deleted), a query failure; for `run`: no crash, a crash (the device is shut down and deleted,
absent from the list after it), an unknown; and a crash that is a missing pid, a crash that is a new status, an
unreadable baseline, an unreadable line, a launcher not running at the baseline, `IOS_SIM_LAUNCHER_CRASH_OK=1` letting
a crash and an unknown pass and `=yes` not, the line printed on an early failure, and only this run's device asked.
The cases run with `IOS_SIM_SDK`, `IOS_SIM_LAUNCHER_CRASH_OK` and `FAKE_LAUNCHER` removed from the caller's
environment. Every case was shown failing against a mutant of the line it covers; the full mutant table is under Review round 1.

## Left for later

`launchctl print system/<label>` has `runs = <n>`, launchd's own count of starts; reading it at the baseline and at the
end would give the exact number of crashes in the line instead of 1.

## Review round 1

Two Importants and three Notes from the review of the launcher line, all fixed.

1. A launcher that crashed during the boot read as "no crash". A device created by this run starts its launchd at this
   run's boot, so a launcher that crashed and was restarted before `bootstatus -b` returned already read a new pid and
   status -11 at the baseline, and the end read matched it. A clean boot reads last exit status 0 at the baseline: a
   throwaway device per SDK on this host read `3405 0 com.apple.SpringBoard` (iOS 27.2), `9163 0 com.apple.PineBoard`
   (tvOS 27.2) and `97955 0 com.apple.SurfBoard` (visionOS 27.2). So a baseline whose status is not 0 is reported as
   `launcher: 1 crash(es) before the run: ...` and fails the run (a definite exit with a failure, not an unknown: the
   status is evidence, not a missing read). A clean-exit restart before the baseline (status 0, new pid) cannot be seen;
   that stays a known gap, as the count is. A boot that fails or times out now sets the trap's state before
   `bootstatus`, so it prints `launcher: unknown (the device did not boot)`.
2. Nothing tested the 30 s bound on the `launchctl list` spawn. The fake's `hang2` outcome hangs the second spawn; the
   case `launcher-check-hangs` requires `timed out after 30 s: xcrun simctl spawn`, the unknown line, and (the generic
   check of every case) that the hung process was killed.
3. `launcher_report` now sets `launcher_done` after it prints the line, so a signal during the read does not lose it.

New cases: `launcher-crash-before-run` and `launcher-run-crash-before-run` (fake `bootcrash`: the baseline reads
`200 -11`), `launcher-boot-fails` (fake `bootfail`), `launcher-check-hangs`.

### Mutants

Each mutant is the script with one change, and its own selftest was run in full; every one is refused by the cases
listed (the three `head-` and `unbounded-` rows are the round 1 fixes taken out again, that is the head's behaviour).

| Mutant | Cases that fail |
|---|---|
| head-baseline-any-status (the baseline accepts any status) | launcher-crash-before-run, launcher-run-crash-before-run |
| head-no-boot-state (a failed boot sets no launcher state) | launcher-boot-fails |
| unbounded-spawn (a plain `simctl spawn`, no bound) | launcher-check-hangs |
| bound-too-long (55 s instead of 30 s) | launcher-check-hangs |
| pid-only (a changed status is not a crash) | launcher-crash-new-status |
| status-only (a new pid with status 0 is not a crash) | launcher-crash-new-pid-clean-exit |
| unreadable-baseline-passes | launcher-unknown-baseline |
| crash-not-bad (a crash does not fail the run) | the launcher-crash cases (each SDK, run, new pid, new status) |
| unknown-end-not-bad (an unknown does not fail) | launcher-unknown (each SDK), launcher-check-hangs |
| crash-ok-any-value (any value of the variable passes) | launcher-crash-ok-not-one |
| label-always-springboard | the launcher cases for visionOS (launcher-crash-xr, launcher-no-crash-xr, launcher-unknown-xr) |
| no-report-in-trap (the exit trap prints no line) | launcher-boot-fails, launcher-line-on-early-failure |
| garbage-accepted (an unreadable line is read) | launcher-unknown-garbage |
| not-running-accepted | launcher-unknown-not-running |

The move of `launcher_done` after the print (note 1) is a signal race that the selftest cannot drive, so it has no
mutant.
