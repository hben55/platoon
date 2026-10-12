# platoon
Platoon card game from Ni no Kuni: Wrath of the White Witch

## Build and play

```
make        # builds ./platoon, the terminal game
make test   # checks the battle rules and AI difficulties
```

`src/platoon.[ch]` is a pure game-logic library (no I/O) meant to sit under any
front end; `src/main.c` is the terminal one. The house AI has Easy, Medium and
Hard difficulties (see `platoon.h`).
