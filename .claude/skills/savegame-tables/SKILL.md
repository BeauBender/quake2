---
name: savegame-tables
description: Checks that every game function and mmove_t a savegame can reference is listed in src/game/savegame/tables/, and prints the exact entries to add. Use when adding, renaming or removing a think/touch/use/pain/die/blocked function, a monsterinfo or moveinfo callback, or an mmove_t table in src/game, and before opening a PR that touches src/game.
---

# Savegame tables

Savegames store function pointers and `mmove_t` pointers by name. The name
lists live in `src/game/savegame/tables/`. A function assigned to a savable
field but missing from those lists compiles and runs, then fails when a game
that uses it is saved or loaded.

## Check

Run `.claude/skills/savegame-tables/scripts/missing-entries.sh`. It prints
nothing and exits 0 when the tables are complete. Otherwise it prints, for
each missing name, the list it belongs in and the two lines to add. The same
check runs in the gate as `tests/check-savegame-tables.sh`.

## Fix

1. Add the declaration to `gamefunc_decs.h` (or `gamemmove_decs.h`) and the
   entry to the named list in `gamefunc_list.h` (or `gamemmove_list.h`),
   exactly as the script prints them. Skip the declaration when the
   function is already declared there. Match the declaration to the
   function's real signature.
2. For a renamed or removed function, delete its old entries in both files.
3. Re-run the script, then `scripts/test.sh`.

`stuff/fngen/` and `stuff/mmgen/` regenerate the whole tables, but they need
GNU grep (`grep -P`), so they do not run on stock macOS. Hand edits from the
script's output are the supported path there.

## Savegame version

`savegame.c` says to bump `SAVEGAMEVER` whenever the tables change. A bump
makes the game refuse every older save, and this project promises
backward-compatible savegames. Do not bump it on your own: say on the card
or PR that the tables changed, and let the owner decide.
