# platoon
Platoon card game from Ni no Kuni: Wrath of the White Witch

## Build and play

```
make platoon-gui   # graphical game (fetches and builds raylib 5.5 on first run)
make platoon       # terminal game
make test          # checks the battle rules and AI difficulties
```

The graphical version needs the X11 and OpenGL headers on Linux:

```
sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev
```

Build your piles by dragging cards onto them, or click a card and then a pile
(keys 1-5 also work; right-click or drag a card back to return it). H opens the
rules, M toggles sound, F11 toggles fullscreen, and `./platoon-gui --demo` lets
the Hard AI play your side.

Cards are dealt, placed, turned over and sent into battle with animations, and
the sound effects are synthesised in code, so there are no audio files.

## Layout

- `src/platoon.[ch]`: the game-logic library (rules, matches, AI). No I/O, so
  any front end can sit on top of it.
- `src/main.c`: terminal front end.
- `src/gui.c`: raylib front end (screens, layout, input, card animation);
  `src/gui_draw.[ch]`: card, text and button drawing;
  `src/gui_audio.[ch]`: synthesised sound effects;
  `src/gui_help.[ch]`: the rules screen;
  `src/gui_input.[ch]`: input layer that a test script can drive.
- `assets/fonts`: DejaVu Sans (see `LICENSE-DejaVu.txt`), used by the GUI.
- `src/test.c`: rule and AI tests.

The house AI has Easy, Medium and Hard difficulties (see `platoon.h`).

## Testing the GUI

`PLATOON_SCRIPT=file` replays mouse and key commands instead of using the real
mouse (commands are listed in `src/gui_input.c`), `PLATOON_SHOTS=dir` saves
screenshots (named by `shot` commands, or one per screen change otherwise),
`PLATOON_SEED=n` fixes the shuffle and `PLATOON_MUTE=1` starts with sound off.
