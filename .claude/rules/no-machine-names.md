# Pushed markers name no machine

This repository is public. Nothing an agent pushes names the machine it runs
on or the account it runs under.

- **What this covers.** The claim markers (`claim/<ID>`), the gate-clear
  markers (`gate-clear/<ID>`), the cadence locks, and every other ref an agent
  pushes. A ref name, a commit message, and an author or committer address
  never hold a host name or an account name. Git's `user.email` is the GitHub
  noreply address.
- **The holder.** A marker records its holder as `<token>/agent/<session>`.
  The token is `m-` and 12 hex digits. It is random rather than derived from
  any name, stable per machine, and kept only on that machine, outside this
  repository. An agent knows its own markers by its token. A marker with any
  other token belongs to another agent.
- **Until the loop tooling writes the token itself.** Run every call that
  writes a marker with the machine's identity shim directory first on `PATH`.
  In that directory, `hostname` prints the token and `whoami` prints `agent`.
  These are the calls that write a marker with this repository's loop settings
  (`.claude/dev-loop.json`: cards in GitHub Issues, `vsm.durable` false):
  - `claim-card.sh` `claim`, `heartbeat`, `release` and `adopt`;
  - `cadence-due.sh claim`;
  - `kanban.sh gate-clear`;
  - `file-card.sh`;
  - `ship-finish.sh`.

  Two settings would add writers that push the names. Turning on
  `vsm.durable` makes `vsm-sync.sh` publish refs named after the machine.
  Moving the cards to the `refs` source makes `kanban.sh move` and
  `escalate-card.sh` write the names into card refs. Neither setting changes
  until the tooling writes the token itself.

  Never put the shim on `PATH` for `scripts/test.sh`, `merge-gate.sh` or a
  merge. `tests/check-no-local-identity.sh` reads the real names, and the shim
  would blind it.
- **Known gap.** The loop's readers still compare a holder against the real
  names, for example the Stop hook's list of held claims. So they report the
  agent's own token claims as another agent's. Go by the token, not by that
  report.
- **How to check.** Compare against `hostname -s` and `whoami`, read at the
  time you check, with the shim off `PATH`. Never write the values down.
  - `git ls-remote origin` shows neither name in any ref name.
  - For each marker ref, `git log -1 --format='%s%n%ae%n%ce' <ref>` shows the
    token form and the noreply address.
- **Removing an old marker.** A marker that still names a machine is removed
  by the agent on that machine, and by no one else. It releases the marker, or
  it deletes the ref and writes the marker again under its token. An agent
  never deletes another agent's marker.
