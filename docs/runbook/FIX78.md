# FIX78: a numeric setting with a leading zero

Plan anchor: [the bench harness plan, "The host lock"](../plans/bench-harness.md).

## The bug

`bench_is_uint` (`scripts/bench/core/common.sh`) accepted `08`. Shell arithmetic reads a leading zero as
octal, so `$((2 * 08))` fails with "value too great for base". With `BENCH_STOP_GRACE=08` and a
TERM-ignoring command, a TERM to `bench.sh lock run` left the lock shell gone and the lock free while the
command still ran. `lock_stop_wait` in `scripts/metalfx-report/mfx.py` read the same value as 8.

## The decision

Refuse, do not reinterpret. A numeric setting is `0` or digits that do not start with `0`. `08`, `09`,
`010` (and `00`) are refused before any arithmetic, with the message and status the core already gives any
other non-number for that setting (status 2, "<setting> is whole seconds"). `mfx.py` treats them as not
whole seconds, which is its existing path for a non-number: the default, 10.

## Red first

`tests/check-bench.sh t_leading_zero` on the start point (FIX83, `08cc0fc4`), first lines:

```
check-bench: zero: bench_is_uint accepts 08
check-bench: zero: bench_is_uint accepts 09
check-bench: zero: bench_is_uint accepts 010
check-bench: zero: bench_is_uint accepts 00
check-bench: zero: bench_is_uint accepts 007
check-bench: zero: BENCH_STOP_GRACE=08 was not refused with 2 (status 0)
check-bench: zero: BENCH_STOP_GRACE=08 let the command run
check-bench: zero: BENCH_STOP_GRACE=08: the refusal does not name the setting: .../core/lock.sh: line 263: 08: va...
check-bench: zero: BENCH_LOCK_WAIT=08 was not refused with 2 (status 0)
check-bench: zero: the case key runs=08 was not refused with 2 (status 0)
check-bench: zero: a wall time of 08 was not refused with 2 (status 1)
```

The `lock.sh` line 263 message is the octal failure of `bench_lock_grace` itself.

## The fix

`bench_is_uint` takes `'' | *[!0-9]* | 0?*` as not a number. Every numeric reader already goes through it:

| setting | read in |
| --- | --- |
| `BENCH_STOP_GRACE` | `bench.sh` (entry check, before any core call); used by `bench_stop_group`, `bench_end_group`, `bench_lock_grace` |
| `BENCH_LOCK_WAIT`, `BENCH_LOCK_GRACE` | `bench_lock_acquire` (`core/lock.sh`) |
| wall time | `bench_bounded` (`core/common.sh`); the case's `wall` in `bench_case_load` (`core/case.sh`) |
| case `runs`, `warmup` | `bench_case_load` (`core/case.sh`), before `bench_measure` does arithmetic on them |

`BENCH_LOCK_POLL` is a decimal checked by `bench_is_num` and used in awk only, never in shell arithmetic.
`mfx.py`: the grace regex is `(0|[1-9][0-9]*)\Z`.

No trap, signal disposition, process group or `set -m` changed.

## Tests

`t_leading_zero` (`tests/check-bench.sh`): the validator refuses `08 09 010 00 007` and accepts
`0 8 10 1000`; for `08 09 010`, each of `BENCH_STOP_GRACE`, `BENCH_LOCK_WAIT`, `BENCH_LOCK_GRACE` under
`bench.sh lock run` exits 2, names the setting and never runs the command; the case keys `runs`, `warmup`,
`wall` are refused with 2; a wall time through `bench_bounded` is refused with 2; `0 8 10` are still
accepted for each setting and key. The card's case runs end to end: `BENCH_STOP_GRACE=08` with a
TERM-ignoring command is refused at start (status 2), the command never starts (no pid file) and the lock
is free; nothing is left running. `tests/check-metalfx-report.sh`: `lock_stop_wait` for `08` and `010`
gives the default, under and outside the generator's own hold (`25 11 25 65 25 25 / 10 1 10 30 10 10`).

## Mutants, each caught

- `leading-zero-accepted` (`check-bench.sh`): `bench_is_uint` without `0?*`: caught by `t_leading_zero`
  ("zero: bench_is_uint accepts 08").
- `stop-wait-leading-zero` (`check-metalfx-report.sh`): `mfx.py` with `[0-9]+\Z` again: killed
  (`e2e-lock-grace`).

## Checks

`tests/check-bench.sh` (all mutants), `tests/check-metalfx-report.sh`, `tests/check-bench-entry.sh`,
`tests/check-doc-links.sh`, `tests/check-no-local-identity.sh`: ok. `tests/check-runbook-index.sh` fails
until the index line for this file is added; that is expected.
