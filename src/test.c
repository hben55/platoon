/* Checks the pile battle rules against the examples in the FAQ. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "platoon.h"

#define C(r) ((PlCard){0, r})

static int fight(PlPile a, PlPile b) { return pl_compare_piles(&a, &b, NULL); }

int main(void) {
  PlPile king = {{C(PL_KING)}, 1};
  PlPile king5 = {{C(PL_KING), C(PL_RANK_5)}, 2};
  PlPile bishop = {{C(PL_ACE)}, 1};
  PlPile big = {{C(PL_QUEEN), C(PL_JACK), C(PL_RANK_3)}, 3};
  PlPile small = {{C(PL_RANK_8), C(PL_RANK_9)}, 2};
  PlPile w_low = {{C(PL_JOKER), C(PL_RANK_2)}, 2};
  PlPile w_bish = {{C(PL_JOKER), C(PL_ACE)}, 2};
  PlPile w_king = {{C(PL_JOKER), C(PL_KING)}, 2};
  PlPile w_num = {{C(PL_JOKER), C(PL_RANK_9)}, 2};

  assert(fight(king, big) > 0);          /* King beats any basic pile */
  assert(fight(bishop, big) < 0);        /* Bishop loses to any pile... */
  assert(fight(bishop, king) > 0);       /* ...except a King */
  assert(fight(big, small) > 0);         /* highest points otherwise */
  assert(fight(king, king5) < 0);        /* King vs King: points */
  assert(fight(w_low, king) > 0);        /* Wizard+low beats Kings */
  assert(fight(w_low, w_bish) > 0);      /* ...and Wizard+Bishop */
  assert(fight(w_low, w_king) < 0);      /* ...but not Wizard+King */
  assert(fight(w_bish, king) < 0);       /* Wizard+Bishop loses to King */
  assert(fight(w_bish, w_num) < 0);      /* two Wizards: no swap */
  assert(fight(w_bish, w_bish) == 0);    /* Wizard+Bishop ties itself */

  /* The house always orders its piles per the FAQ and uses all 10 cards. */
  for (int t = 0; t < 20000; t++) {
    PlCard deck[PL_DECK_SIZE];
    pl_deck_fill(deck);
    pl_deck_shuffle(deck);
    PlArrangement a;
    for (int d = PL_EASY; d <= PL_MEDIUM; d++) {
      pl_ai_arrange((PlDifficulty)d, deck, &a);
      assert(pl_arrangement_error(&a) == NULL);
    }
  }
  for (int t = 0; t < 50; t++) {
    PlCard deck[PL_DECK_SIZE];
    pl_deck_fill(deck);
    pl_deck_shuffle(deck);
    PlArrangement a;
    pl_ai_arrange(PL_HARD, deck, &a);
    assert(pl_arrangement_error(&a) == NULL);
  }

  /* Higher difficulties should beat lower ones over many full matches. */
  srand(1234);
  for (int lo = PL_EASY; lo < PL_HARD; lo++) {
    int hi = lo + 1, hi_wins = 0, games = 300;
    for (int g = 0; g < games; g++) {
      PlCard deck[PL_DECK_SIZE];
      pl_deck_fill(deck);
      pl_deck_shuffle(deck);
      PlArrangement arr[2];
      pl_ai_arrange(hi, deck, &arr[PL_PLAYER]);
      pl_ai_arrange(lo, deck + PL_HAND_SIZE, &arr[PL_HOUSE]);
      PlMatch m;
      pl_match_init(&m, &arr[PL_PLAYER], &arr[PL_HOUSE],
                    pl_ai_predict(hi, &arr[PL_PLAYER]),
                    pl_ai_predict(lo, &arr[PL_HOUSE]), 200,
                    g % 2 ? PL_PLAYER : PL_HOUSE);
      while (m.state == PL_MATCH_ACTIVE) {
        int p, h;
        PlSide side = m.turn;
        pl_ai_choose_battle(side == PL_PLAYER ? hi : lo, &m, side, &p, &h);
        bool ok = pl_match_battle(&m, p, h, NULL);
        assert(ok);
        (void)ok;
      }
      hi_wins += m.state == PL_MATCH_WON;
    }
    printf("%s beats %s in %d/%d matches\n", pl_difficulty_name(hi),
           pl_difficulty_name(lo), hi_wins, games);
    assert(hi_wins > games * 55 / 100);
  }
  puts("all tests passed");
  return 0;
}
