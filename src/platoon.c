#include "platoon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- cards ---- */

void pl_deck_fill(PlCard deck[PL_DECK_SIZE]) {
  int n = 0;
  for (int rank = PL_RANK_2; rank <= PL_ACE; rank++)
    for (int suit = 0; suit < 4; suit++)
      deck[n++] = (PlCard){suit, (PlRank)rank};
  deck[n++] = (PlCard){0, PL_JOKER};
  deck[n++] = (PlCard){1, PL_JOKER};
}

void pl_deck_shuffle(PlCard deck[PL_DECK_SIZE]) {
  for (int i = PL_DECK_SIZE - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    PlCard tmp = deck[i];
    deck[i] = deck[j];
    deck[j] = tmp;
  }
}

int pl_card_points(PlCard c) {
  switch (c.rank) {
  case PL_JOKER: return 0;
  case PL_ACE: return 1;
  case PL_JACK: case PL_QUEEN: case PL_KING: return 10;
  default: return (int)c.rank;
  }
}

void pl_card_name(PlCard c, char buf[5]) {
  static const char suits[] = "HDCS";
  switch (c.rank) {
  case PL_JOKER: strcpy(buf, "W"); return;
  case PL_ACE: snprintf(buf, 5, "A%c", suits[c.suit]); return;
  case PL_KING: snprintf(buf, 5, "K%c", suits[c.suit]); return;
  case PL_QUEEN: snprintf(buf, 5, "Q%c", suits[c.suit]); return;
  case PL_JACK: snprintf(buf, 5, "J%c", suits[c.suit]); return;
  default: snprintf(buf, 5, "%d%c", (int)c.rank, suits[c.suit]); return;
  }
}

const char *pl_kind_name(PlKind kind) {
  switch (kind) {
  case PL_KIND_BISHOP: return "Bishop";
  case PL_KIND_KING: return "King";
  default: return "Number";
  }
}

/* ---- piles ---- */

PlPileInfo pl_pile_info(const PlPile *pile) {
  PlPileInfo info = {0, false, PL_KIND_NUMBER};
  bool king = false, bishop = false;
  for (int i = 0; i < pile->count; i++) {
    info.points += pl_card_points(pile->cards[i]);
    if (pile->cards[i].rank == PL_JOKER) info.has_wizard = true;
    if (pile->cards[i].rank == PL_KING) king = true;
    if (pile->cards[i].rank == PL_ACE) bishop = true;
  }
  if (king && !bishop) info.kind = PL_KIND_KING;
  if (bishop && !king) info.kind = PL_KIND_BISHOP;
  return info;
}

const char *pl_arrangement_error(const PlArrangement *arr) {
  int total = 0;
  for (int i = 0; i < PL_NUM_PILES; i++) {
    const PlPile *p = &arr->piles[i];
    if (p->count == 0) return "every pile needs at least one card";
    total += p->count;
    bool only_wizards = true;
    for (int j = 0; j < p->count; j++)
      if (p->cards[j].rank != PL_JOKER) only_wizards = false;
    if (only_wizards) return "a Wizard cannot be in a pile alone";
  }
  if (total != PL_HAND_SIZE) return "all 10 cards must be used";
  return NULL;
}

/* Bishop beats King, King beats Number, Number beats Bishop; same kinds are
 * decided by points. */
static int compare_raw(PlPileInfo a, PlPileInfo b) {
  if (a.kind == b.kind) return (a.points > b.points) - (a.points < b.points);
  bool a_wins = (a.kind == PL_KIND_BISHOP && b.kind == PL_KIND_KING) ||
                (a.kind == PL_KIND_KING && b.kind == PL_KIND_NUMBER) ||
                (a.kind == PL_KIND_NUMBER && b.kind == PL_KIND_BISHOP);
  return a_wins ? 1 : -1;
}

int pl_compare_piles(const PlPile *a, const PlPile *b, bool *swapped) {
  PlPileInfo ia = pl_pile_info(a), ib = pl_pile_info(b);
  bool swap = ia.has_wizard != ib.has_wizard; /* two Wizards swap back */
  if (swapped) *swapped = swap;
  int r = compare_raw(ia, ib);
  return swap ? -r : r;
}

/* ---- match ---- */

void pl_match_init(PlMatch *m, const PlArrangement *player,
                   const PlArrangement *house, int player_predicted,
                   int house_predicted, int wager, PlSide first) {
  memset(m, 0, sizeof *m);
  m->arr[PL_PLAYER] = *player;
  m->arr[PL_HOUSE] = *house;
  m->predicted[PL_PLAYER] = player_predicted;
  m->predicted[PL_HOUSE] = house_predicted;
  m->wager = wager;
  m->turn = first;
  m->state = PL_MATCH_ACTIVE;
}

bool pl_match_battle(PlMatch *m, int player_pile, int house_pile,
                     PlBattleResult *result) {
  if (m->state != PL_MATCH_ACTIVE) return false;
  if (player_pile < 0 || player_pile >= PL_NUM_PILES) return false;
  if (house_pile < 0 || house_pile >= PL_NUM_PILES) return false;
  if (m->used[PL_PLAYER][player_pile] || m->used[PL_HOUSE][house_pile])
    return false;

  PlBattleResult r = {{player_pile, house_pile}, PL_NOBODY, false, m->wager, 0};
  int cmp = pl_compare_piles(&m->arr[PL_PLAYER].piles[player_pile],
                             &m->arr[PL_HOUSE].piles[house_pile], &r.swapped);
  r.winner = cmp > 0 ? PL_PLAYER : cmp < 0 ? PL_HOUSE : PL_NOBODY;

  m->used[PL_PLAYER][player_pile] = true;
  m->used[PL_HOUSE][house_pile] = true;
  m->battles++;
  if (r.winner == PL_PLAYER) {
    m->wins++;
    if (player_pile == m->predicted[PL_PLAYER]) m->wager += m->wager / 2;
  } else {
    m->losses++; /* ties count against the player */
    if (r.winner == PL_HOUSE && house_pile == m->predicted[PL_HOUSE])
      m->wager /= 2;
  }
  r.wager_after = m->wager;

  if (m->wins >= PL_WINS_NEEDED) m->state = PL_MATCH_WON;
  else if (m->losses >= PL_LOSSES_NEEDED) m->state = PL_MATCH_LOST;
  m->turn = m->turn == PL_PLAYER ? PL_HOUSE : PL_PLAYER;
  if (result) *result = r;
  return true;
}

/* ---- AI ---- */

static int by_points_desc(const void *a, const void *b) {
  return pl_card_points(*(const PlCard *)b) - pl_card_points(*(const PlCard *)a);
}

/* Left-to-right order the house uses: Wizard+Bishop, Bishop, Wizard+King,
 * King, Wizard+Number, Number. */
static int house_pile_order(const PlPile *p) {
  PlPileInfo i = pl_pile_info(p);
  int base = i.kind == PL_KIND_BISHOP ? 0 : i.kind == PL_KIND_KING ? 2 : 4;
  return base + (i.has_wizard ? 0 : 1);
}

static void merge_pile(PlPile piles[], int *n, int from, int into) {
  for (int i = 0; i < piles[from].count; i++)
    piles[into].cards[piles[into].count++] = piles[from].cards[i];
  for (int i = from; i < *n - 1; i++) piles[i] = piles[i + 1];
  (*n)--;
}

static void arrange_medium(const PlCard hand[PL_HAND_SIZE], PlArrangement *out) {
  PlCard wizards[PL_HAND_SIZE], numbers[PL_HAND_SIZE];
  PlPile piles[PL_HAND_SIZE];
  int nw = 0, nn = 0, na = 0, nk = 0, np = 0;
  memset(piles, 0, sizeof piles);

  /* Bishops, then Kings, each start their own pile. */
  for (int i = 0; i < PL_HAND_SIZE; i++)
    if (hand[i].rank == PL_ACE) piles[np++] = (PlPile){{hand[i]}, 1}, na++;
  for (int i = 0; i < PL_HAND_SIZE; i++)
    if (hand[i].rank == PL_KING) piles[np++] = (PlPile){{hand[i]}, 1}, nk++;
  for (int i = 0; i < PL_HAND_SIZE; i++) {
    if (hand[i].rank == PL_JOKER) wizards[nw++] = hand[i];
    else if (hand[i].rank != PL_ACE && hand[i].rank != PL_KING)
      numbers[nn++] = hand[i];
  }

  /* Too many specials: double them up with their own kind. */
  while (np > PL_NUM_PILES) {
    if (nk >= 2 && (nk >= na || na < 2)) {
      merge_pile(piles, &np, np - 1, np - 2);
      nk--;
    } else {
      merge_pile(piles, &np, na - 1, na - 2);
      na--;
    }
  }

  /* Number piles get the remaining numbers, high cards first, always onto
   * the weakest pile so far. */
  int first_number_pile = np;
  while (np < PL_NUM_PILES) piles[np++].count = 0;
  qsort(numbers, nn, sizeof numbers[0], by_points_desc);
  for (int i = 0; i < nn; i++) {
    int from = first_number_pile < np ? first_number_pile : 0, best = from;
    for (int p = from; p < np; p++)
      if (pl_pile_info(&piles[p]).points < pl_pile_info(&piles[best]).points)
        best = p;
    piles[best].cards[piles[best].count++] = numbers[i];
  }

  /* Wizards go on the leftmost piles (Bishops, then Kings, then Numbers). */
  for (int i = 0; i < nw; i++) piles[i].cards[piles[i].count++] = wizards[i];

  for (int i = 1; i < PL_NUM_PILES; i++) { /* stable insertion sort */
    PlPile key = piles[i];
    int j = i - 1;
    while (j >= 0 && house_pile_order(&piles[j]) > house_pile_order(&key)) {
      piles[j + 1] = piles[j];
      j--;
    }
    piles[j + 1] = key;
  }
  for (int i = 0; i < PL_NUM_PILES; i++) out->piles[i] = piles[i];
}

static void shuffle_cards(PlCard *cards, int n) {
  for (int i = n - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    PlCard tmp = cards[i];
    cards[i] = cards[j];
    cards[j] = tmp;
  }
}

/* Deals the cards at random into 5 piles, retrying until the piles are legal. */
static void arrange_random(const PlCard hand[PL_HAND_SIZE], PlArrangement *out) {
  PlCard cards[PL_HAND_SIZE];
  do {
    memcpy(cards, hand, sizeof cards);
    shuffle_cards(cards, PL_HAND_SIZE);
    memset(out, 0, sizeof *out);
    for (int i = 0; i < PL_HAND_SIZE; i++) {
      PlPile *p = &out->piles[i < PL_NUM_PILES ? i : rand() % PL_NUM_PILES];
      p->cards[p->count++] = cards[i];
    }
  } while (pl_arrangement_error(out));
}

/* How well a pile does against the sampled opposing piles: win = 1, tie = .5. */
static double pile_strength(const PlPile *p, const PlPile *opp, int n) {
  double score = 0;
  for (int i = 0; i < n; i++) {
    int r = pl_compare_piles(p, &opp[i], NULL);
    score += r > 0 ? 1.0 : r == 0 ? 0.5 : 0.0;
  }
  return score / n;
}

#define HARD_CANDIDATES 1500
#define HARD_OPPONENTS 30

static void arrange_hard(const PlCard hand[PL_HAND_SIZE], PlArrangement *out) {
  /* Sample hands the opponent could hold, from the cards we don't have. */
  PlCard deck[PL_DECK_SIZE], rest[PL_DECK_SIZE];
  int n = 0;
  pl_deck_fill(deck);
  for (int i = 0; i < PL_DECK_SIZE; i++) {
    bool mine = false;
    for (int j = 0; j < PL_HAND_SIZE; j++)
      if (hand[j].rank == deck[i].rank && hand[j].suit == deck[i].suit) mine = true;
    if (!mine) rest[n++] = deck[i];
  }
  PlPile opp[HARD_OPPONENTS * PL_NUM_PILES];
  for (int o = 0; o < HARD_OPPONENTS; o++) {
    PlArrangement a;
    shuffle_cards(rest, n);
    arrange_random(rest, &a);
    memcpy(&opp[o * PL_NUM_PILES], a.piles, sizeof a.piles);
  }

  /* A set is won with 3 of 5 battles, so favor three strong piles. */
  double best = -1;
  for (int c = 0; c < HARD_CANDIDATES; c++) {
    PlArrangement cand;
    arrange_random(hand, &cand);
    double s[PL_NUM_PILES];
    for (int i = 0; i < PL_NUM_PILES; i++)
      s[i] = pile_strength(&cand.piles[i], opp, HARD_OPPONENTS * PL_NUM_PILES);
    for (int i = 1; i < PL_NUM_PILES; i++) /* sort descending */
      for (int j = i; j > 0 && s[j] > s[j - 1]; j--) {
        double t = s[j]; s[j] = s[j - 1]; s[j - 1] = t;
      }
    double score = s[0] + s[1] + s[2] + 0.5 * s[3];
    if (score > best) { best = score; *out = cand; }
  }
  for (int i = PL_NUM_PILES - 1; i > 0; i--) { /* no exploitable pile order */
    int j = rand() % (i + 1);
    PlPile t = out->piles[i];
    out->piles[i] = out->piles[j];
    out->piles[j] = t;
  }
}

const char *pl_difficulty_name(PlDifficulty d) {
  return d == PL_EASY ? "Easy" : d == PL_MEDIUM ? "Medium" : "Hard";
}

void pl_ai_arrange(PlDifficulty d, const PlCard hand[PL_HAND_SIZE],
                   PlArrangement *out) {
  if (d == PL_EASY) arrange_random(hand, out);
  else if (d == PL_MEDIUM) arrange_medium(hand, out);
  else arrange_hard(hand, out);
}

int pl_ai_predict(PlDifficulty d, const PlArrangement *own,
                  const PlArrangement *opponent) {
  if (d != PL_HARD) return rand() % PL_NUM_PILES;
  int best = 0;
  double best_score = -1;
  for (int i = 0; i < PL_NUM_PILES; i++) {
    double s = pile_strength(&own->piles[i], opponent->piles, PL_NUM_PILES);
    if (s > best_score) { best_score = s; best = i; }
  }
  return best;
}

/* Exhaustive search of the remaining battles. Values are from `me`'s side. */
typedef struct {
  signed char res[PL_NUM_PILES][PL_NUM_PILES]; /* player's view: 1, 0, -1 */
  PlSide me;
  int my_predicted;
} Search;

#define WIN_VALUE 1000
#define PREDICTION_BONUS 10

/* Applies a battle to the counters; returns the bonus `me` earns from it. */
static int apply(const Search *s, int p, int h, int *wins, int *losses) {
  int r = s->res[p][h];
  if (r > 0) (*wins)++; else (*losses)++;
  bool me_won = (s->me == PL_PLAYER) == (r > 0) && r != 0;
  int my_pile = s->me == PL_PLAYER ? p : h;
  return me_won && my_pile == s->my_predicted ? PREDICTION_BONUS : 0;
}

static int search(const Search *s, unsigned used_p, unsigned used_h, int wins,
                  int losses, PlSide turn, int depth) {
  bool me_wins_set = (s->me == PL_PLAYER) == (wins >= PL_WINS_NEEDED);
  if (wins >= PL_WINS_NEEDED || losses >= PL_LOSSES_NEEDED)
    return (me_wins_set ? WIN_VALUE : -WIN_VALUE) + (me_wins_set ? -depth : depth);
  bool maximize = turn == s->me;
  int best = maximize ? -100000 : 100000;
  for (int p = 0; p < PL_NUM_PILES; p++) {
    if (used_p & (1u << p)) continue;
    for (int h = 0; h < PL_NUM_PILES; h++) {
      if (used_h & (1u << h)) continue;
      int w = wins, l = losses;
      int bonus = apply(s, p, h, &w, &l);
      int v = search(s, used_p | (1u << p), used_h | (1u << h), w, l,
                     turn == PL_PLAYER ? PL_HOUSE : PL_PLAYER, depth + 1) + bonus;
      if (maximize ? v > best : v < best) best = v;
    }
  }
  return best;
}

void pl_ai_choose_battle(PlDifficulty d, const PlMatch *m, PlSide side,
                         int *player_pile, int *house_pile) {
  Search s = {{{0}}, side, m->predicted[side]};
  for (int p = 0; p < PL_NUM_PILES; p++)
    for (int h = 0; h < PL_NUM_PILES; h++)
      s.res[p][h] = (signed char)pl_compare_piles(
          &m->arr[PL_PLAYER].piles[p], &m->arr[PL_HOUSE].piles[h], NULL);
  unsigned used_p = 0, used_h = 0;
  for (int i = 0; i < PL_NUM_PILES; i++) {
    if (m->used[PL_PLAYER][i]) used_p |= 1u << i;
    if (m->used[PL_HOUSE][i]) used_h |= 1u << i;
  }

  int best_score = -1000000, ties = 0;
  for (int h = 0; h < PL_NUM_PILES; h++) {
    if (used_h & (1u << h)) continue;
    for (int p = 0; p < PL_NUM_PILES; p++) {
      if (used_p & (1u << p)) continue;
      int score;
      if (d == PL_EASY) {
        score = 0;
      } else if (d == PL_MEDIUM) { /* any win, own predicted pile preferred */
        int r = s.res[p][h];
        bool win = side == PL_PLAYER ? r > 0 : r < 0;
        int mine = side == PL_PLAYER ? p : h;
        score = win ? 2 + (mine == s.my_predicted) : r == 0 ? 1 : 0;
      } else {
        int w = m->wins, l = m->losses;
        int bonus = apply(&s, p, h, &w, &l);
        score = search(&s, used_p | (1u << p), used_h | (1u << h), w, l,
                       side == PL_PLAYER ? PL_HOUSE : PL_PLAYER, 1) + bonus;
      }
      if (score > best_score) {
        best_score = score; *player_pile = p; *house_pile = h; ties = 1;
      } else if (score == best_score && rand() % ++ties == 0) {
        *player_pile = p; *house_pile = h;
      }
    }
  }
}
