# TASK180: ios-sim.sh says whether the simulator's launcher crashed during its own run

The owner once came back to about ten error dialogs about the simulator's launcher (SpringBoard) after several
simulator lanes had run, and no lane could say whether its own run caused them. `scripts/ios-sim.sh` (every mode,
every SDK) now prints one line about the launcher of the device it created, before it deletes the device.

## The line

| Line | Meaning | Exit status |
|---|---|---|
| `launcher: no crash` | the launcher has the pid and the last exit status it had once the device booted | unaffected |
| `launcher: 1 crash(es): <process>, pid <old> -> <new>, last exit status <s>, seen at <time>` | it exited between the two reads | the run fails |
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
environment. Every case was shown failing against a mutant of the line it covers; the mutant list is in the pull
request.

## Left for later

`launchctl print system/<label>` has `runs = <n>`, launchd's own count of starts; reading it at the baseline and at the
end would give the exact number of crashes in the line instead of 1.
