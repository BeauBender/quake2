# Decision records

One file per decision, numbered in order: `NNNN-short-title.md`. Each says
what was decided, why, what it costs, and how to reverse it. A later
decision that changes an earlier one is a new record that names the old one;
records are not rewritten.

Decisions the loop has taken and wants the owner to be able to see and
redirect are tracked as INFORM cards (GitHub Issues). Nothing waits for
them (record 0004); one becomes a record here when it is settled.

| Record | Decision |
|---|---|
| [0001](0001-remove-optional-dependencies.md) | Remove optional external and vendored dependencies |
| [0002](0002-gnu-make-only.md) | GNU Make is the only build system |
| [0003](0003-no-local-identity.md) | Keep the working machine's identity out of what is published |
| [0004](0004-no-human-tier.md) | No change waits for a person: the loop merges everything that passes review and the gates |
