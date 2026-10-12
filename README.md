# Platoon

The Platoon casino card game from *Ni no Kuni: Wrath of the White Witch*,
written in C. You can play it in a terminal, in a desktop window, or in the
browser.

**Play in your browser: https://hben55.github.io/platoon/**

## How to play

Platoon is a two-player card game: you (the challenger) against the house.
The deck is a standard 52-card deck plus two Jokers. Aces are called
**Bishops** and Jokers are called **Wizards**.

### Building your piles

You and the house are each dealt 10 cards. Split yours into 5 piles of any
size; every card must be used. A Wizard can't sit in a pile on its own.

### What each pile is worth

- Number cards 2 to 10 count their face value.
- Jacks and Queens count 10.
- **Kings** count 10 and beat any pile made only of number cards.
- **Bishops** (Aces) count 1. A Bishop pile loses to every pile except one with
  a King, which it beats.
- **Wizards** (Jokers) count 0. When one pile in a battle has a Wizard, the two
  piles swap sides before the winner is decided. That's why a Wizard paired
  with a low card beats a King. If both piles have a Wizard, the swaps cancel.

When neither pile has a King or a Bishop, more points wins. When both have a
King, or both have a Bishop, more points also wins. A pile with both a King and
a Bishop counts as a plain number pile.

### Predictions and battles

The house's piles are dealt face down: you can see how many cards each one
holds, but not what they are. A house pile only turns over when it fights, and
any piles left at the end of the round are revealed then. The house plays by
the same rule and never sees your unrevealed piles either.

Once the piles are set, you pick the pile you think will win, and the house
picks one of its own. A card draw decides who goes first. Then the two sides
take turns choosing one of their piles and one of the opponent's to fight.
Every pile fights exactly once.

- Each time **your** predicted pile wins a battle, your wager grows by half.
- Each time **the house's** predicted pile wins a battle, your wager is halved.
- Win **3 battles** and you win the round. Three losses or ties (a tie counts
  against you) and you lose the round and the wager.

### Chips and rounds

You start with 1,000 chips, and a set costs 200. Winning a round doubles the
wager. You can then take your winnings, or let them ride into the next round.
After 6 straight wins the table pays out. If you hold 99,999 chips or less at
that point, a bonus 7th round opens.

### Strategy tips

- Don't try to make all five piles winners. You only need three, so aim for
  3 strong piles, 1 throwaway and 1 backup.
- Strong piles: a Wizard with your lowest card, a lone King, or 18+ points.
- Throwaways: a lone Bishop (which can still upset a King) or your lowest card.
- A backup pile of 8 to 10 points is good for picking off the house's weak
  single cards.
- On Medium, the house always lays its piles out in a fixed order from left to
  right: Wizard+Bishop, Bishop, Wizard+King, King, Wizard+numbers, numbers.
  Once one of its piles turns over to show only number cards, everything to
  its right is numbers too.
  Hard shuffles its pile order and plans every battle ahead.

The rules and tips are based on the GameFAQs *Platoon FAQ* by SoopaSte123
(2013), with thanks to Nooope, bmf4ever and carl8000, whom it credits.

## House difficulty

- **Easy:** random piles, a random prediction and random battles.
- **Medium:** plays like the dealer in the real game, using the FAQ's pile
  order, and takes the battle it is most likely to win.
- **Hard:** picks a strong arrangement in a random order, predicts its best
  pile, and searches every remaining battle order before each move.

The AI only knows what you would know: its own piles, your piles that have
fought, and how many cards your other piles hold. Medium and Hard guess the
hidden cards from the ones they haven't seen.

## Build and play

```
make platoon-gui   # graphical game (fetches and builds raylib 5.5 on first run)
make platoon       # terminal game
make web           # browser build in web/ (fetches the Emscripten SDK on first run)
make test          # checks the battle rules and AI difficulties
```

The browser build needs a web server, since browsers won't load WebAssembly
from a file. Any static server works, for example:

```
python3 -m http.server 8000 --directory web
```

and then open http://localhost:8000. The first `make web` downloads the
Emscripten SDK (about 1.4 GB) into `vendor/emsdk`; nothing is installed
system-wide.

The hosted copy is published from the `gh-pages` branch: `make deploy` rebuilds
`web/` and pushes it there.

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
- `src/web_shell.html`: the page that hosts the browser build.
- `src/test.c`: rule and AI tests.

## Testing the GUI

`PLATOON_SCRIPT=file` replays mouse and key commands instead of using the real
mouse (commands are listed in `src/gui_input.c`), `PLATOON_SHOTS=dir` saves
screenshots (named by `shot` commands, or one per screen change otherwise),
`PLATOON_SEED=n` fixes the shuffle and `PLATOON_MUTE=1` starts with sound off.
