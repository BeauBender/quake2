# Memory index — one line per topic file; recipes live in the topic files.

(seeded by /dev-loop:init)
- [yquake2 fork review](project-yquake2-fork-review.md) — repo is an unmodified yquake2 mirror; review verdict and caveats
- [Review the fork itself](feedback-review-the-fork-itself.md) — audit the tree directly, no upstream comparison or builds without asking
- [dev-loop workflow preferences](feedback-devloop-workflow.md) — orchestrate via dev-loop, INFORM for decisions, no installs without approval, say where things were filed
- [Plugin root env for dev-loop scripts](feedback-plugin-root-env.md) — export CLAUDE_PLUGIN_ROOT and pass -R to gh, or risk tiers silently degrade
- [Merge policy](feedback-merge-policy.md) — the loop merges every PR itself (any tier, config and workflows included) once review stamp + merge gate pass; cross-repo merges skip the hooks
- [No paid CI](feedback-no-paid-ci.md) — no paid Actions: keep CI on free standard runners
- [Rework directives](feedback-rework-directives.md) — prescribe properties and proven primitives, not hand-built security/concurrency mechanisms
- [quake2 work only](feedback-no-private-cards-on-public.md) — this public repo holds quake2 cards and context only; nothing about other repos
- [One agent at a time](feedback-one-agent-at-a-time.md) — until Fri 2026-10-23 9 AM PT: at most one subagent live, review passes sequential
