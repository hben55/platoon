/* Terminal front end for Platoon. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "platoon.h"

#define START_CHIPS 1000
#define SET_COST 200
#define MAX_CHIPS 9999999
#define ROUND_CAP 99999 /* below this after round 6, a 7th round is allowed */

static bool read_line(const char *prompt, char *buf, size_t size) {
  printf("%s", prompt);
  fflush(stdout);
  if (!fgets(buf, (int)size, stdin)) return false;
  buf[strcspn(buf, "\n")] = '\0';
  return true;
}

static bool ask_yes_no(const char *prompt) {
  char buf[16];
  while (read_line(prompt, buf, sizeof buf)) {
    if (tolower((unsigned char)buf[0]) == 'y') return true;
    if (tolower((unsigned char)buf[0]) == 'n') return false;
  }
  return false;
}

static void print_pile(const PlPile *p) {
  char name[5], line[64] = "";
  for (int i = 0; i < p->count; i++) {
    pl_card_name(p->cards[i], name);
    strcat(line, i ? " " : "");
    strcat(line, name);
  }
  PlPileInfo info = pl_pile_info(p);
  printf("%-18s %2d pts%s%s", line, info.points,
         info.kind != PL_KIND_NUMBER ? ", " : "",
         info.kind != PL_KIND_NUMBER ? pl_kind_name(info.kind) : "");
  if (info.has_wizard) printf(", Wizard");
}

static void print_piles(const char *title, const PlMatch *m, PlSide side) {
  printf("%s\n", title);
  for (int i = 0; i < PL_NUM_PILES; i++) {
    printf("  %d) ", i + 1);
    print_pile(&m->arr[side].piles[i]);
    if (m->used[side][i]) printf("  [fought]");
    if (side == PL_PLAYER && i == m->predicted[PL_PLAYER]) printf("  <- predicted");
    printf("\n");
  }
}

/* "1 2 / 3 / 4 5 6 / 7 8 / 9 10": card numbers, piles separated by '/'. */
static bool parse_arrangement(const char *line, const PlCard hand[],
                              PlArrangement *out, const char **err) {
  memset(out, 0, sizeof *out);
  bool seen[PL_HAND_SIZE] = {false};
  int pile = 0;
  for (const char *p = line; *p;) {
    if (isspace((unsigned char)*p)) {
      p++;
    } else if (*p == '/' || *p == '|' || *p == ',') {
      if (++pile >= PL_NUM_PILES) { *err = "too many piles, use exactly 5"; return false; }
      p++;
    } else if (isdigit((unsigned char)*p)) {
      char *end;
      long n = strtol(p, &end, 10);
      if (n < 1 || n > PL_HAND_SIZE) { *err = "card numbers go from 1 to 10"; return false; }
      if (seen[n - 1]) { *err = "a card was used twice"; return false; }
      seen[n - 1] = true;
      out->piles[pile].cards[out->piles[pile].count++] = hand[n - 1];
      p = end;
    } else {
      *err = "unexpected character";
      return false;
    }
  }
  if (pile != PL_NUM_PILES - 1) { *err = "use exactly 5 piles"; return false; }
  *err = pl_arrangement_error(out);
  return *err == NULL;
}

static PlDifficulty difficulty;

static void arrange_player(const PlCard hand[], PlArrangement *arr) {
  printf("\nYour hand:\n  ");
  for (int i = 0; i < PL_HAND_SIZE; i++) {
    char name[5];
    pl_card_name(hand[i], name);
    printf("%d:%s  ", i + 1, name);
  }
  printf("\nBuild 5 piles. Type card numbers with '/' between piles, e.g. "
         "\"1 2 / 3 / 4 5 6 / 7 8 / 9 10\",\nor \"auto\" to let the house AI "
         "arrange for you.\n");
  char buf[128];
  for (;;) {
    if (!read_line("> ", buf, sizeof buf)) exit(0);
    if (strcmp(buf, "auto") == 0) { pl_ai_arrange(PL_HARD, hand, arr); return; }
    const char *err;
    if (parse_arrangement(buf, hand, arr, &err)) return;
    printf("Invalid: %s\n", err);
  }
}

static int ask_pile(const char *prompt) {
  char buf[16];
  for (;;) {
    if (!read_line(prompt, buf, sizeof buf)) exit(0);
    int n = atoi(buf);
    if (n >= 1 && n <= PL_NUM_PILES) return n - 1;
  }
}

static PlSide draw_first(const PlCard *deck) {
  for (int i = 0;; i += 2) {
    char a[5], b[5];
    pl_card_name(deck[i], a);
    pl_card_name(deck[i + 1], b);
    printf("Draw for first turn: you %s, house %s", a, b);
    if (deck[i].rank != deck[i + 1].rank) {
      PlSide first = deck[i].rank > deck[i + 1].rank ? PL_PLAYER : PL_HOUSE;
      printf(" -> %s go first.\n", first == PL_PLAYER ? "you" : "the house");
      return first;
    }
    printf(" -> tie, drawing again.\n");
  }
}

/* Plays one round (a "campaign"). Returns true if the player won; *wager is
 * updated either way. */
static bool play_round(int *wager) {
  PlCard deck[PL_DECK_SIZE];
  pl_deck_fill(deck);
  pl_deck_shuffle(deck);

  PlArrangement mine, house;
  arrange_player(deck, &mine);
  pl_ai_arrange(difficulty, deck + PL_HAND_SIZE, &house);

  PlMatch m;
  pl_match_init(&m, &mine, &house, 0, pl_ai_predict(difficulty, &house, &mine), *wager, PL_PLAYER);
  print_piles("\nHouse piles:", &m, PL_HOUSE);
  print_piles("Your piles:", &m, PL_PLAYER);
  m.predicted[PL_PLAYER] = ask_pile("Which of your piles will win? (1-5) ");
  m.turn = draw_first(deck + 2 * PL_HAND_SIZE);

  while (m.state == PL_MATCH_ACTIVE) {
    printf("\n--- Battle %d | wager %d | wins %d/%d, losses+ties %d/%d ---\n",
           m.battles + 1, m.wager, m.wins, PL_WINS_NEEDED, m.losses,
           PL_LOSSES_NEEDED);
    print_piles("House piles:", &m, PL_HOUSE);
    print_piles("Your piles:", &m, PL_PLAYER);

    int p, h;
    if (m.turn == PL_PLAYER) {
      p = ask_pile("Your pile: ");
      h = ask_pile("House pile: ");
    } else {
      pl_ai_choose_battle(difficulty, &m, PL_HOUSE, &p, &h);
      printf("The house pits its pile %d against your pile %d.\n", h + 1, p + 1);
    }
    PlBattleResult r;
    if (!pl_match_battle(&m, p, h, &r)) {
      printf("One of those piles has already fought.\n");
      continue;
    }
    if (r.swapped) printf("A Wizard swaps the piles!\n");
    printf("Your pile %d vs house pile %d: %s\n", p + 1, h + 1,
           r.winner == PL_PLAYER ? "you win" :
           r.winner == PL_HOUSE ? "the house wins" : "tie");
    if (r.wager_after != r.wager_before)
      printf("Wager %d -> %d\n", r.wager_before, r.wager_after);
  }
  *wager = m.wager;
  return m.state == PL_MATCH_WON;
}

int main(void) {
  srand((unsigned)time(NULL));
  printf("PLATOON\nA = Bishop (1 pt, loses to everything but a King)   "
         "K = King (beats number piles)\nW = Wizard (0 pts, swaps the piles in "
         "a battle)   J/Q = 10 pts\n");

  char buf[16];
  do {
    if (!read_line("Difficulty: [e]asy, [m]edium or [h]ard? ", buf, sizeof buf))
      return 0;
  } while (!strchr("emhEMH", buf[0]) || !buf[0]);
  difficulty = tolower((unsigned char)buf[0]) == 'e' ? PL_EASY :
               tolower((unsigned char)buf[0]) == 'm' ? PL_MEDIUM : PL_HARD;
  printf("House AI: %s\n", pl_difficulty_name(difficulty));

  int chips = START_CHIPS;
  while (chips >= SET_COST) {
    printf("\nYou have %d chips. A set costs %d.\n", chips, SET_COST);
    if (!ask_yes_no("Play? [y/n] ")) break;
    chips -= SET_COST;

    int wager = SET_COST;
    for (int round = 1;; round++) {
      printf("\n=== Round %d, wager %d ===\n", round, wager);
      if (!play_round(&wager)) {
        printf("\nYou lose the round and the wager.\n");
        break;
      }
      wager *= 2; /* a win pays double the wager */
      if (wager > MAX_CHIPS) wager = MAX_CHIPS;
      printf("\nYou win the round! Winnings: %d\n", wager);
      bool more = round < 6 || (round == 6 && wager <= ROUND_CAP);
      if (!more || !ask_yes_no("Use your winnings as the next wager? [y/n] ")) {
        chips += wager;
        if (chips > MAX_CHIPS) chips = MAX_CHIPS;
        break;
      }
    }
  }
  printf("\nYou leave with %d chips.\n", chips);
  return 0;
}
