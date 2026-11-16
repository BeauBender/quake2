# FIX58: `tests/check-pre-push-hook.sh` and a temporary directory that holds the login name

Card: GitHub issue 284. Scope: `tests/check-pre-push-hook.sh` only.

## Failure before

With `TMPDIR` set to a path holding the login name (`/tmp/<login>-t`), the
check exited 1 on its first real push:

    check-pre-push-hook: FAIL: real push of a leaking ref name: output printed a real identity value

Cause: `git push` prints the remote's address, which is under the check's
temporary directory, so the path's login name reached the output that
`leaks()` scans. With the default `TMPDIR` the check passed (exit 0).

## Change

The check makes its directory with `mktemp -d /tmp/q2-prepush.XXXXXX`, not
under `$TMPDIR`, and refuses (exit 1, clear message) if even that path holds
a real identity value (host name, login name, home path; three characters or
more, case-insensitive). The alternative, masking the path in the output
before matching, was rejected: the scan would then hide a real leak that
happens to sit inside a path. Every real identity value stays in the list.
The EXIT trap removes the directory on every path, with `"${tmp:?}"`.

## Acceptance

- `TMPDIR=/tmp/<login>-t tests/check-pre-push-hook.sh` on a clean tree: exit 0, `ok`.
- Default `TMPDIR`: exit 0, `ok`.
- Mutant: a scratch copy of the tree whose hook no longer adds the host name
  to its word list. The check fails (exit 1) on the real push of a leaking
  ref name and on the direct run of the same case.
- The refusal path itself (an identity value inside `/tmp/q2-prepush.*`, for
  example a home path of `/tmp`) could not be run here: the harness refuses a
  command that sets `HOME` for a git run. It is plain shell code, read-checked.
- `tests/check-no-local-identity.sh` and `tests/check-identity-env.sh`: exit 0.
