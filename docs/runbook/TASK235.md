# The Stop hook shows due cadences (TASK235)

`probes.stillActionable` in `.claude/dev-loop.json` holds two stopgap probes: `cadence-due.sh all` (the groom, archReview and harvest due lines) and a reflect due line, so a due cadence refuses a Stop in pull mode. Remove both probes when the loop tooling's cadence nag (#771) lands.
