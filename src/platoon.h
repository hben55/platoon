/* Platoon: the card game from Ni no Kuni's casino.
 *
 * This is a pure game-logic library: no I/O, no globals. A front end (the
 * terminal one in main.c, or a graphical one later) deals hands, collects the
 * player's pile arrangement and prediction, then feeds battles to a PlMatch.
 * Randomness comes from rand(); the caller seeds it with srand().
 */
#ifndef PLATOON_H
#define PLATOON_H

#include <stdbool.h>

#define PL_DECK_SIZE 54  /* 52 cards + 2 Jokers */
#define PL_HAND_SIZE 10
#define PL_NUM_PILES 5
#define PL_WINS_NEEDED 3
#define PL_LOSSES_NEEDED 3  /* losses and ties combined */

/* Aces are "Bishops", Jokers are "Wizards". */
typedef enum {
  PL_RANK_2 = 2, PL_RANK_3, PL_RANK_4, PL_RANK_5, PL_RANK_6, PL_RANK_7,
  PL_RANK_8, PL_RANK_9, PL_RANK_10, PL_JACK, PL_QUEEN, PL_KING, PL_ACE,
  PL_JOKER
} PlRank;

typedef struct {
  int suit; /* 0..3 Hearts, Diamonds, Clubs, Spades; Jokers use 0 or 1 */
  PlRank rank;
} PlCard;

typedef struct {
  PlCard cards[PL_HAND_SIZE];
  int count;
} PlPile;

typedef struct {
  PlPile piles[PL_NUM_PILES];
} PlArrangement;

typedef enum { PL_PLAYER = 0, PL_HOUSE = 1, PL_NOBODY = -1 } PlSide;

/* A pile with a King and no Bishop is a KING pile, a Bishop and no King is a
 * BISHOP pile; a pile with both cancels out and counts as NUMBER. */
typedef enum { PL_KIND_NUMBER, PL_KIND_BISHOP, PL_KIND_KING } PlKind;

typedef struct {
  int points;
  bool has_wizard;
  PlKind kind;
} PlPileInfo;

/* ---- cards and piles ---- */
void pl_deck_fill(PlCard deck[PL_DECK_SIZE]);
void pl_deck_shuffle(PlCard deck[PL_DECK_SIZE]);
int pl_card_points(PlCard c);
/* Writes a short name such as "KH", "10S", "AC", "W" (needs 5 bytes). */
void pl_card_name(PlCard c, char buf[5]);
const char *pl_kind_name(PlKind kind);

PlPileInfo pl_pile_info(const PlPile *pile);
/* NULL if the arrangement is legal, otherwise a message saying why not:
 * exactly 5 non-empty piles, 10 cards, no Wizard alone in a pile. */
const char *pl_arrangement_error(const PlArrangement *arr);

/* Battle two piles. >0 if a wins, <0 if b wins, 0 for a tie.
 * *swapped (optional) is set when exactly one pile held a Wizard, which swaps
 * the piles before the winner is decided. */
int pl_compare_piles(const PlPile *a, const PlPile *b, bool *swapped);

/* ---- a match: one set of 5 battles ---- */
typedef enum { PL_MATCH_ACTIVE, PL_MATCH_WON, PL_MATCH_LOST } PlMatchState;

typedef struct {
  PlArrangement arr[2]; /* indexed by PlSide */
  int predicted[2];     /* pile each side thinks will win */
  bool used[2][PL_NUM_PILES]; /* a pile's cards are revealed once it has fought */
  int wager;
  int wins;   /* the player's wins */
  int losses; /* the player's losses and ties */
  int battles;
  PlSide turn;
  PlMatchState state;
} PlMatch;

typedef struct {
  int pile[2];     /* piles that fought, indexed by PlSide */
  PlSide winner;   /* PL_NOBODY on a tie */
  bool swapped;
  int wager_before, wager_after;
} PlBattleResult;

void pl_match_init(PlMatch *m, const PlArrangement *player,
                   const PlArrangement *house, int player_predicted,
                   int house_predicted, int wager, PlSide first);
/* Fights the given piles (either side may be the one whose turn it is). Returns
 * false if the match is over or a pile is out of range / already used. */
bool pl_match_battle(PlMatch *m, int player_pile, int house_pile,
                     PlBattleResult *result);

/* ---- the AI ----
 * Like a human player, the AI sees only its own piles, the opposing piles that
 * have already fought, and how many cards the other opposing piles hold.
 * EASY:   random piles, random prediction, random battles.
 * MEDIUM: arranges like the FAQ describes (Wizard+Bishop, Bishop, Wizard+King,
 *         King, Wizard+Number, Number from left to right), random prediction,
 *         and takes the battle it is most likely to win, guessing the hidden
 *         piles from the cards it hasn't seen.
 * HARD:   scores many random arrangements and keeps the best one in a random
 *         pile order, predicts its strongest pile, and searches every remaining
 *         battle order (like a chess engine) across many guesses of the hidden
 *         piles. It can play either side.
 */
typedef enum { PL_EASY, PL_MEDIUM, PL_HARD } PlDifficulty;

const char *pl_difficulty_name(PlDifficulty d);
void pl_ai_arrange(PlDifficulty d, const PlCard hand[PL_HAND_SIZE],
                   PlArrangement *out);
/* Which of `own`'s piles the AI predicts will win. */
int pl_ai_predict(PlDifficulty d, const PlArrangement *own);
/* Picks a battle for `side`; both piles come back unused. */
void pl_ai_choose_battle(PlDifficulty d, const PlMatch *m, PlSide side,
                         int *player_pile, int *house_pile);

#endif
