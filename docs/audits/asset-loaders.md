# Static audit of the asset loaders (TASK18)

Scope: every file-format parser that remains in the tree after the dependency
removals. Work in progress; sections are filled one format per commit.

## Contents

1. Method and coverage
2. Where each asset can come from
3. Collision BSP (`src/common/collision.c`)
4. Pak directory and paths (`src/common/filesystem.c`)
5. Renderer BSP (`src/client/refresh/files/models.c` and `*_model.c`)
6. MD2 and SP2 models
7. PCX, WAL, M8, M32 and the stb_image callers
8. WAV (`src/client/sound/wave.c`)
9. Cinematics (`src/client/cl_cin.c`)
10. Entity strings and map-fix `.ent` overrides
11. By design / out of scope
12. Defects
13. Cards to file
