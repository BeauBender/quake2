---
paths:
  - "src/game/**"
---

# Game module and savegames

- Savegames store function and `mmove_t` pointers by name, per field. After
  adding, renaming or removing a think/touch/use/pain/die/blocked function,
  a `monsterinfo` or `moveinfo` callback, or an `mmove_t`, run
  `tests/check-savegame-tables.sh`; the `savegame-tables` skill prints the
  entries to add.
- Do not bump `SAVEGAMEVER` in `src/game/savegame/savegame.c` and do not
  change `edict_t`, `gclient_t` or the field tables in
  `src/game/savegame/tables/` without the owner's decision on the card:
  either one breaks every existing save.
- `game_import_t` and `game_export_t` (`src/game/header/game.h`) are the
  module API shared with the xatrix, rogue and ctf addons. Never change
  them.
- Gameplay must stay as in the original game. A behavior change in
  `src/game` needs a card that asks for it.
