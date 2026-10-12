/* Graphical front end for Platoon, built on raylib.
 *
 * Everything is laid out on a virtual 1280x720 table that is scaled to fit the
 * window. The left part is the board (house piles on top, yours below, your
 * hand at the bottom while arranging); the right part is the score panel.
 *
 * Every dealt card has an animated position and flip state (g.vis) that eases
 * towards where the game logic says the card belongs, so dealing, placing,
 * revealing and battling all animate without per-move code.
 *
 * Run with --demo to watch the Hard AI play your side. For testing,
 * PLATOON_SCRIPT drives the input (see gui_input.c), PLATOON_SHOTS=dir saves
 * screenshots, PLATOON_SEED fixes the shuffle and PLATOON_MUTE=1 starts muted.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gui_audio.h"
#include "gui_draw.h"
#include "gui_help.h"
#include "gui_input.h"
#include "platoon.h"
#include "raylib.h"
#include "rlgl.h"

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

#define VW 1280
#define VH 720
#define BOARD_CX 480.0f
#define PANEL_X 960.0f
#define SLOT_W 120.0f
#define SLOT_H 214.0f
#define SLOT_GAP 172.0f
#define HOUSE_Y 80.0f
#define PLAYER_Y 352.0f
#define HAND_Y 598.0f
#define BATTLE_SHIFT 22.0f /* how far fighting piles slide towards each other */
#define DEAL_STEP 0.055f   /* seconds between dealt cards */

#define START_CHIPS 1000
#define SET_COST 200
#define MAX_CHIPS 9999999
#define ROUND_CAP 99999 /* at or below this after round 6, a 7th round opens */

#define NCARDS (2 * PL_HAND_SIZE) /* deck[0..9] is your hand, deck[10..19] the house's */

typedef enum {
  SCR_MENU, SCR_ARRANGE, SCR_PREDICT, SCR_FIRST, SCR_BATTLE, SCR_ROUND_END
} Screen;
typedef enum {
  BT_CHOOSE, BT_HOUSE_THINK, BT_HOUSE_SHOW, BT_CLASH, BT_RESULT
} BattlePhase;

/* How one dealt card is drawn right now. */
typedef struct {
  Rectangle at;     /* where the card is drawn */
  float face;       /* 0 face down .. 1 face up */
  float deal_delay; /* waiting in the deck until this runs out */
  float flip_delay; /* waiting to be turned over */
} CardVis;

static struct {
  Screen screen;
  PlDifficulty difficulty;
  int chips, wager, round;
  PlCard deck[PL_DECK_SIZE];
  float timer, dt; /* seconds since the screen or phase began; frame time */

  /* Arranging. */
  int pile_of[PL_HAND_SIZE]; /* -1 while the card is still in your hand */
  int placed_at[PL_HAND_SIZE];
  int place_counter;
  int selected; /* hand card picked for click-to-assign, -1 if none */
  int pressed;  /* card under the held mouse button, -1 if none */
  int hover;    /* hand card under the mouse, -1 if none */
  Vector2 press_pos, grab;
  bool dragging;

  /* Predicting and battling. */
  PlArrangement mine, house;
  int house_pile[PL_HAND_SIZE], house_slot[PL_HAND_SIZE]; /* where each house card sits */
  PlMatch match;
  PlCard draw_cards[2];
  int draw_ties;
  int pick[2]; /* piles chosen for the next battle, indexed by PlSide */
  BattlePhase phase;
  PlBattleResult last;
  char outcome[2][PL_NUM_PILES]; /* 'W', 'L' or 'T' from that side's view */

  /* Animation. */
  CardVis vis[NCARDS];
  float shown_wager, shown_chips; /* numbers count towards the real values */

  bool round_won, help, quit, demo;
} g;

/* ---- small helpers ---- */

static bool clicked(void) { return in_pressed(MOUSE_BUTTON_LEFT); }

static bool skip_pressed(void) {
  return in_key(KEY_SPACE) || in_key(KEY_ENTER) || clicked();
}

/* True on the frame the screen timer passes t (for timed sound cues). */
static bool crossed(float t) { return g.timer >= t && g.timer - g.dt < t; }

static float ease_out(float u) {
  u = fminf(1, fmaxf(0, u));
  return 1 - (1 - u) * (1 - u) * (1 - u);
}

/* ---- layout ---- */

static Rectangle slot_rect(PlSide side, int i) {
  float cx = BOARD_CX + (i - 2) * SLOT_GAP;
  return (Rectangle){cx - SLOT_W / 2, side == PL_HOUSE ? HOUSE_Y : PLAYER_Y,
                     SLOT_W, SLOT_H};
}

static Rectangle stacked_card(PlSide side, int pile, int k, int n) {
  Rectangle s = slot_rect(side, pile);
  float step = n > 1 ? fminf(26, (SLOT_H - 16 - CARD_H) / (n - 1)) : 0;
  return (Rectangle){s.x + (SLOT_W - CARD_W) / 2, s.y + 8 + k * step, CARD_W, CARD_H};
}

static int slot_at(PlSide side, Vector2 p, float pad) {
  for (int i = 0; i < PL_NUM_PILES; i++) {
    Rectangle r = slot_rect(side, i);
    r = (Rectangle){r.x - pad, r.y - pad, r.width + 2 * pad, r.height + 2 * pad};
    if (CheckCollisionPointRec(p, r)) return i;
  }
  return -1;
}

static Rectangle deck_rect(int order) {
  return (Rectangle){BOARD_CX - CARD_W / 2 - order * 0.3f,
                     (HOUSE_Y + SLOT_H + PLAYER_Y) / 2 - CARD_H / 2 - order * 0.3f,
                     CARD_W, CARD_H};
}

/* ---- your hand while arranging ---- */

/* Your cards in pile p (indices into the hand), in the order placed. */
static int pile_members(int p, int out[PL_HAND_SIZE]) {
  int n = 0;
  for (int i = 0; i < PL_HAND_SIZE; i++)
    if (g.pile_of[i] == p) out[n++] = i;
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && g.placed_at[out[j]] < g.placed_at[out[j - 1]]; j--) {
      int t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
    }
  return n;
}

static int hand_members(int out[PL_HAND_SIZE]) {
  int n = 0;
  for (int i = 0; i < PL_HAND_SIZE; i++)
    if (g.pile_of[i] < 0) out[n++] = i;
  return n;
}

static Rectangle hand_card(int k, int n) {
  float step = CARD_W + 8, w = n * step - 8;
  return (Rectangle){BOARD_CX - w / 2 + k * step, HAND_Y, CARD_W, CARD_H};
}

/* Where your card i belongs (not counting hover or a drag in progress). */
static Rectangle card_home(int i) {
  int m[PL_HAND_SIZE], n, k = 0;
  if (g.pile_of[i] < 0) {
    n = hand_members(m);
    while (m[k] != i) k++;
    Rectangle r = hand_card(k, n);
    if (i == g.selected) r.y -= 10;
    return r;
  }
  n = pile_members(g.pile_of[i], m);
  while (m[k] != i) k++;
  return stacked_card(PL_PLAYER, g.pile_of[i], k, n);
}

/* The topmost of your cards under the point, or -1. */
static int card_at(Vector2 p) {
  int m[PL_HAND_SIZE];
  for (int pile = 0; pile < PL_NUM_PILES; pile++) {
    int n = pile_members(pile, m);
    for (int k = n - 1; k >= 0; k--)
      if (CheckCollisionPointRec(p, stacked_card(PL_PLAYER, pile, k, n))) return m[k];
  }
  int n = hand_members(m);
  for (int k = 0; k < n; k++)
    if (CheckCollisionPointRec(p, card_home(m[k]))) return m[k];
  return -1;
}

static void place(int card, int pile) {
  g.pile_of[card] = pile;
  g.placed_at[card] = ++g.place_counter;
  au_play(SND_PLACE);
}

static void unplace(int card) {
  g.pile_of[card] = -1;
  au_play(SND_PLACE);
}

static int cards_in_hand(void) {
  int m[PL_HAND_SIZE];
  return hand_members(m);
}

static void build_arrangement(PlArrangement *a) {
  memset(a, 0, sizeof *a);
  for (int p = 0; p < PL_NUM_PILES; p++) {
    int m[PL_HAND_SIZE], n = pile_members(p, m);
    for (int k = 0; k < n; k++) a->piles[p].cards[k] = g.deck[m[k]];
    a->piles[p].count = n;
  }
}

static const char *arrangement_status(void) {
  int left = cards_in_hand();
  if (left > 0) return left == 1 ? "1 card left to place" : TextFormat("%d cards left to place", left);
  PlArrangement a;
  build_arrangement(&a);
  return pl_arrangement_error(&a);
}

static void select_next_in_hand(void) {
  int m[PL_HAND_SIZE];
  g.selected = hand_members(m) > 0 ? m[0] : -1;
}

static bool dealing(void) {
  for (int i = 0; i < NCARDS; i++)
    if (g.vis[i].deal_delay > 0) return true;
  return false;
}

/* ---- card animation ---- */

static bool pile_in_battle(PlSide side, int pile) {
  return g.screen == SCR_BATTLE && g.phase >= BT_HOUSE_SHOW && g.pick[side] == pile;
}

/* Where card i should be and whether it should be face up. */
static bool card_target(int i, Rectangle *r) {
  if (i < PL_HAND_SIZE) {
    *r = card_home(i);
    if (g.screen == SCR_ARRANGE && g.pile_of[i] < 0 && i == g.hover && i != g.selected)
      r->y -= 6;
    if (g.pile_of[i] >= 0 && pile_in_battle(PL_PLAYER, g.pile_of[i])) r->y -= BATTLE_SHIFT;
    return true;
  }
  int h = i - PL_HAND_SIZE, p = g.house_pile[h];
  *r = stacked_card(PL_HOUSE, p, g.house_slot[h], g.house.piles[p].count);
  if (pile_in_battle(PL_HOUSE, p)) r->y += BATTLE_SHIFT;
  /* The house's piles stay face down until they fight (or the round ends). */
  if (g.screen == SCR_ROUND_END) return true;
  if (g.screen != SCR_BATTLE) return false;
  return g.match.used[PL_HOUSE][p] || (g.phase >= BT_CLASH && g.pick[PL_HOUSE] == p);
}

static void update_cards(float dt) {
  float k = 1 - expf(-dt * 14);
  for (int i = 0; i < NCARDS; i++) {
    CardVis *v = &g.vis[i];
    if (v->deal_delay > 0) {
      if ((v->deal_delay -= dt) <= 0) au_play(SND_DEAL);
      continue;
    }
    Rectangle t;
    bool face_up = card_target(i, &t);
    if (g.dragging && i == g.pressed) {
      v->at = (Rectangle){gd_mouse.x - g.grab.x, gd_mouse.y - g.grab.y, CARD_W, CARD_H};
    } else {
      v->at.x += (t.x - v->at.x) * k;
      v->at.y += (t.y - v->at.y) * k;
    }
    if (v->flip_delay > 0) {
      if ((v->flip_delay -= dt) <= 0 && i >= PL_HAND_SIZE && g.house_slot[i - PL_HAND_SIZE] == 0)
        au_play(SND_FLIP);
      continue;
    }
    float dist = fabsf(t.x - v->at.x) + fabsf(t.y - v->at.y);
    if (face_up && (i >= PL_HAND_SIZE || dist < 40)) v->face = fminf(1, v->face + dt / 0.22f);
    if (!face_up) v->face = fmaxf(0, v->face - dt / 0.22f);
  }
}

static void locate_house_cards(void) {
  bool taken[PL_NUM_PILES][PL_HAND_SIZE] = {{false}};
  for (int h = 0; h < PL_HAND_SIZE; h++) {
    PlCard c = g.deck[PL_HAND_SIZE + h];
    for (int p = 0; p < PL_NUM_PILES; p++)
      for (int k = 0; k < g.house.piles[p].count; k++) {
        PlCard d = g.house.piles[p].cards[k];
        if (!taken[p][k] && d.rank == c.rank && d.suit == c.suit) {
          taken[p][k] = true;
          g.house_pile[h] = p;
          g.house_slot[h] = k;
          goto next;
        }
      }
  next:;
  }
}

/* ---- game flow ---- */

static void start_round(void) {
  pl_deck_fill(g.deck);
  pl_deck_shuffle(g.deck);
  for (int i = 0; i < PL_HAND_SIZE; i++) g.pile_of[i] = -1;
  g.place_counter = 0;
  g.selected = g.pressed = g.hover = -1;
  g.dragging = false;
  g.pick[0] = g.pick[1] = -1;
  memset(g.outcome, 0, sizeof g.outcome);

  /* The house lays out its piles straight away; they stay face down. */
  pl_ai_arrange(g.difficulty, g.deck + PL_HAND_SIZE, &g.house);
  locate_house_cards();

  /* Deal alternately from the middle of the table: you, house, you, ... */
  for (int j = 0; j < PL_HAND_SIZE; j++)
    for (int side = 0; side < 2; side++) {
      int i = side * PL_HAND_SIZE + j, order = 2 * j + side;
      g.vis[i].at = deck_rect(NCARDS - order);
      g.vis[i].face = 0;
      g.vis[i].deal_delay = 0.3f + order * DEAL_STEP;
      g.vis[i].flip_delay = 0;
    }
  g.screen = SCR_ARRANGE;
  g.timer = 0;
}

static void start_set(void) {
  g.chips -= SET_COST;
  g.wager = SET_COST;
  g.round = 1;
  start_round();
}

static void auto_arrange(void) {
  PlArrangement a;
  pl_ai_arrange(PL_HARD, g.deck, &a);
  for (int i = 0; i < PL_HAND_SIZE; i++) g.pile_of[i] = -1;
  for (int p = 0; p < PL_NUM_PILES; p++)
    for (int k = 0; k < a.piles[p].count; k++)
      for (int i = 0; i < PL_HAND_SIZE; i++)
        if (g.pile_of[i] < 0 && g.deck[i].rank == a.piles[p].cards[k].rank &&
            g.deck[i].suit == a.piles[p].cards[k].suit) {
          place(i, p);
          break;
        }
  g.selected = -1;
}

static void clear_piles(void) {
  for (int i = 0; i < PL_HAND_SIZE; i++) g.pile_of[i] = -1;
  g.selected = -1;
  au_play(SND_PLACE);
}

static void confirm_arrangement(void) {
  build_arrangement(&g.mine);
  g.selected = g.hover = -1;
  g.screen = SCR_PREDICT;
  g.timer = 0;
}

static void choose_prediction(int pile) {
  au_play(SND_PLACE);
  int house_pick = pl_ai_predict(g.difficulty, &g.house);
  /* Draw from the undealt cards for the first turn; equal ranks redraw. */
  int i = NCARDS;
  g.draw_ties = 0;
  while (i + 3 < PL_DECK_SIZE && g.deck[i].rank == g.deck[i + 1].rank) {
    i += 2;
    g.draw_ties++;
  }
  g.draw_cards[0] = g.deck[i];
  g.draw_cards[1] = g.deck[i + 1];
  PlSide first = g.deck[i].rank > g.deck[i + 1].rank ? PL_PLAYER
                 : g.deck[i].rank < g.deck[i + 1].rank ? PL_HOUSE
                                                       : (PlSide)(rand() % 2);
  pl_match_init(&g.match, &g.mine, &g.house, pile, house_pick, g.wager, first);
  g.screen = SCR_FIRST;
  g.timer = 0;
}

static void begin_turn(void) {
  g.pick[0] = g.pick[1] = -1;
  g.phase = g.match.turn == PL_PLAYER ? BT_CHOOSE : BT_HOUSE_THINK;
  g.timer = 0;
}

static void start_battle(void) {
  g.screen = SCR_BATTLE;
  begin_turn();
}

/* The chosen piles slide together; resolve_battle runs when they meet. */
static void fight(void) {
  g.phase = BT_CLASH; /* the house's pile turns over as they meet */
  g.timer = 0;
  au_play(SND_FLIP);
}

static void resolve_battle(void) {
  if (!pl_match_battle(&g.match, g.pick[PL_PLAYER], g.pick[PL_HOUSE], &g.last)) {
    begin_turn();
    return;
  }
  char o = g.last.winner == PL_PLAYER ? 'W' : g.last.winner == PL_HOUSE ? 'L' : 'T';
  g.outcome[PL_PLAYER][g.pick[PL_PLAYER]] = o;
  g.outcome[PL_HOUSE][g.pick[PL_HOUSE]] = o == 'W' ? 'L' : o == 'L' ? 'W' : 'T';
  g.wager = g.match.wager;
  au_play(o == 'W' ? SND_WIN : o == 'L' ? SND_LOSE : SND_TIE);
  g.phase = BT_RESULT;
  g.timer = 0;
}

static void finish_round(void) {
  g.round_won = g.match.state == PL_MATCH_WON;
  g.wager = g.match.wager;
  if (g.round_won) g.wager = g.wager > MAX_CHIPS / 2 ? MAX_CHIPS : g.wager * 2;
  au_play(g.round_won ? SND_ROUND_WIN : SND_ROUND_LOSE);
  /* Turn over the house's piles that never fought, one after another. */
  for (int h = 0; h < PL_HAND_SIZE; h++)
    if (!g.match.used[PL_HOUSE][g.house_pile[h]])
      g.vis[PL_HAND_SIZE + h].flip_delay = 0.4f + g.house_pile[h] * 0.15f + g.house_slot[h] * 0.04f;
  g.screen = SCR_ROUND_END;
  g.timer = 0;
}

static bool can_let_it_ride(void) {
  return g.round_won && (g.round < 6 || (g.round == 6 && g.wager <= ROUND_CAP));
}

static void back_to_menu(void) {
  if (g.round_won) {
    g.chips = g.chips > MAX_CHIPS - g.wager ? MAX_CHIPS : g.chips + g.wager;
    au_play(SND_CHIPS);
  }
  g.wager = 0;
  g.screen = SCR_MENU;
  g.timer = 0;
}

static void let_it_ride(void) {
  g.round++;
  start_round();
}

/* ---- input ---- */

static void update_menu(void) {
  if (g.demo && g.timer > 1.0f) {
    if (g.chips < SET_COST) g.chips = START_CHIPS;
    start_set();
  }
}

static void update_arrange(void) {
  if (dealing()) return;
  if (g.demo) {
    if (g.timer > 2.0f && cards_in_hand() > 0) auto_arrange();
    if (g.timer > 3.2f) confirm_arrangement();
    return;
  }
  Vector2 m = gd_mouse;
  g.hover = g.dragging ? -1 : card_at(m);
  if (g.hover >= 0 && g.pile_of[g.hover] >= 0) g.hover = -1;
  if (clicked() && m.x < PANEL_X) {
    int c = card_at(m);
    if (c >= 0) {
      Rectangle r = card_home(c);
      g.pressed = c;
      g.press_pos = m;
      g.grab = (Vector2){m.x - r.x, m.y - r.y};
      g.dragging = false;
    } else {
      int p = slot_at(PL_PLAYER, m, 0);
      if (p >= 0 && g.selected >= 0) place(g.selected, p);
      g.selected = -1;
    }
  }
  if (g.pressed >= 0 && !g.dragging && in_down(MOUSE_BUTTON_LEFT) &&
      hypotf(m.x - g.press_pos.x, m.y - g.press_pos.y) > 6) {
    g.dragging = true;
    g.selected = -1;
  }
  if (g.pressed >= 0 && in_released(MOUSE_BUTTON_LEFT)) {
    int c = g.pressed;
    g.pressed = -1;
    if (g.dragging) {
      g.dragging = false;
      int p = slot_at(PL_PLAYER, m, 16);
      if (p >= 0) place(c, p);
      else if (m.y > HAND_Y - 30 && g.pile_of[c] >= 0) unplace(c);
    } else if (g.pile_of[c] < 0) {
      g.selected = g.selected == c ? -1 : c;
      au_play(SND_FLIP);
    } else if (g.selected >= 0) {
      place(g.selected, g.pile_of[c]);
      g.selected = -1;
    } else {
      unplace(c);
    }
  }
  if (in_pressed(MOUSE_BUTTON_RIGHT)) {
    int c = card_at(m);
    if (c >= 0 && g.pile_of[c] >= 0) unplace(c);
  }
  /* Keyboard: 1-5 sends the selected card to a pile and selects the next. */
  for (int k = 0; k < PL_NUM_PILES; k++)
    if (in_key(KEY_ONE + k) && g.selected >= 0) {
      place(g.selected, k);
      select_next_in_hand();
    }
  if (in_key(KEY_ESCAPE)) g.selected = -1;
}

static void update_predict(void) {
  if (g.demo) {
    if (g.timer > 1.6f) choose_prediction(pl_ai_predict(PL_HARD, &g.mine));
    return;
  }
  int p = clicked() ? slot_at(PL_PLAYER, gd_mouse, 0) : -1;
  for (int k = 0; k < PL_NUM_PILES; k++)
    if (in_key(KEY_ONE + k)) p = k;
  if (p >= 0) choose_prediction(p);
}

#define FIRST_FLIP_AT 0.3f   /* the two draw cards turn over at these times */
#define FIRST_FLIP_GAP 0.4f
#define FIRST_READY (FIRST_FLIP_AT + FIRST_FLIP_GAP + 0.4f)

static void update_first(void) {
  if (crossed(FIRST_FLIP_AT) || crossed(FIRST_FLIP_AT + FIRST_FLIP_GAP)) au_play(SND_FLIP);
  if (g.timer < FIRST_READY) return;
  if (g.demo ? g.timer > FIRST_READY + 1.0f : in_key(KEY_SPACE) || in_key(KEY_ENTER))
    start_battle();
}

static void update_battle(void) {
  switch (g.phase) {
  case BT_CHOOSE:
    if (g.demo) {
      if (g.timer > 0.7f) {
        pl_ai_choose_battle(PL_HARD, &g.match, PL_PLAYER, &g.pick[PL_PLAYER],
                            &g.pick[PL_HOUSE]);
        fight();
      }
    } else if (clicked()) {
      int p = slot_at(PL_PLAYER, gd_mouse, 0), h = slot_at(PL_HOUSE, gd_mouse, 0);
      if (p >= 0 && !g.match.used[PL_PLAYER][p]) {
        g.pick[PL_PLAYER] = g.pick[PL_PLAYER] == p ? -1 : p;
        au_play(SND_CLICK);
      }
      if (h >= 0 && !g.match.used[PL_HOUSE][h]) {
        g.pick[PL_HOUSE] = g.pick[PL_HOUSE] == h ? -1 : h;
        au_play(SND_CLICK);
      }
      if (g.pick[PL_PLAYER] >= 0 && g.pick[PL_HOUSE] >= 0) fight();
    }
    break;
  case BT_HOUSE_THINK:
    if (g.timer > 0.8f) {
      pl_ai_choose_battle(g.difficulty, &g.match, PL_HOUSE, &g.pick[PL_PLAYER],
                          &g.pick[PL_HOUSE]);
      g.phase = BT_HOUSE_SHOW;
      g.timer = 0;
      au_play(SND_CLICK);
    }
    break;
  case BT_HOUSE_SHOW:
    if (g.timer > 0.9f) fight();
    break;
  case BT_CLASH:
    if (g.timer > 0.45f) resolve_battle();
    break;
  case BT_RESULT:
    if (crossed(0.35f) && g.last.wager_after > g.last.wager_before) au_play(SND_CHIPS);
    if (g.timer > 2.4f || (g.timer > 0.3f && !g.demo && skip_pressed())) {
      if (g.match.state == PL_MATCH_ACTIVE) begin_turn();
      else finish_round();
    }
    break;
  }
}

static void update_round_end(void) {
  if (g.demo && g.timer > 2.5f) {
    if (can_let_it_ride()) let_it_ride();
    else back_to_menu();
  }
}

static void update(void) {
  switch (g.screen) {
  case SCR_MENU: update_menu(); break;
  case SCR_ARRANGE: update_arrange(); break;
  case SCR_PREDICT: update_predict(); break;
  case SCR_FIRST: update_first(); break;
  case SCR_BATTLE: update_battle(); break;
  case SCR_ROUND_END: update_round_end(); break;
  }
}

/* ---- drawing: the table ---- */

static void draw_felt(void) {
  DrawRectangle(0, 0, VW, VH, GD_FELT_DARK);
  DrawCircleGradient((int)BOARD_CX, VH / 2, 720, (Color){34, 116, 80, 255}, GD_FELT_DARK);
}

static void draw_slot(PlSide side, int i) {
  Rectangle s = slot_rect(side, i);
  DrawRectangleRounded(s, 0.08f, 8, Fade(BLACK, 0.16f));
  DrawRectangleRoundedLinesEx(s, 0.08f, 8, 1.5f, Fade(GD_CREAM, 0.22f));
}

static void draw_highlight(PlSide side, int i, Color c) {
  Rectangle s = slot_rect(side, i);
  Rectangle h = {s.x - 4, s.y - 4, s.width + 8, s.height + 8};
  DrawRectangleRoundedLinesEx(h, 0.08f, 8, 3, c);
}

static void draw_caption(PlSide side, int i, const PlPile *pile) {
  if (pile->count == 0) return;
  PlPileInfo info = pl_pile_info(pile);
  const char *kind = info.kind == PL_KIND_NUMBER ? "" : pl_kind_name(info.kind);
  const char *text;
  if (info.has_wizard)
    text = TextFormat("Wizard%s%s  %d", kind[0] ? "+" : "", kind, info.points);
  else if (kind[0])
    text = TextFormat("%s  %d", kind, info.points);
  else
    text = TextFormat("%d pts", info.points);
  Rectangle s = slot_rect(side, i);
  float y = side == PL_HOUSE ? s.y - 22 : s.y + s.height + 4;
  gd_text(text, s.x + s.width / 2, y, 16, GD_CREAM, ALIGN_CENTER, true);
}

static Vector2 slot_edge(PlSide side, int i) {
  Rectangle s = slot_rect(side, i);
  return (Vector2){s.x + s.width / 2, side == PL_HOUSE ? s.y + s.height : s.y};
}

static Color outcome_color(char o) {
  return o == 'W' ? GD_WIN : o == 'L' ? GD_LOSE : GD_TIE;
}

static void draw_vs(void) {
  Vector2 a = slot_edge(PL_PLAYER, g.pick[PL_PLAYER]), b = slot_edge(PL_HOUSE, g.pick[PL_HOUSE]);
  Vector2 mid = {(a.x + b.x) / 2, (a.y + b.y) / 2};
  DrawLineEx(a, b, 5, GD_GOLD);
  if (g.phase == BT_RESULT && g.timer < 0.5f) { /* a ring bursts out on impact */
    char o = g.outcome[PL_PLAYER][g.pick[PL_PLAYER]];
    float u = g.timer / 0.5f;
    DrawRing(mid, 18 + 120 * u, 24 + 120 * u, 0, 360, 48, Fade(outcome_color(o), 1 - u));
  }
  float pulse = g.phase == BT_CLASH ? 1 + 0.25f * sinf(g.timer * 30) : 1;
  DrawCircleV(mid, 20 * pulse, GD_GOLD);
  gd_text("VS", mid.x, mid.y - 9, 18, (Color){48, 32, 8, 255}, ALIGN_CENTER, true);
}

static int base_layer(int i) {
  if (i >= PL_HAND_SIZE) {
    int h = i - PL_HAND_SIZE;
    return g.house_pile[h] * 20 + g.house_slot[h];
  }
  if (g.pile_of[i] >= 0) {
    int m[PL_HAND_SIZE], n = pile_members(g.pile_of[i], m), k = 0;
    while (k < n && m[k] != i) k++;
    return 200 + g.pile_of[i] * 20 + k;
  }
  return 400 + i;
}

/* All dealt cards: settled ones in stacking order, then cards in flight. */
static void draw_cards(void) {
  int order[NCARDS], key[NCARDS];
  for (int i = 0; i < NCARDS; i++) {
    order[i] = i;
    if (g.vis[i].deal_delay > 0) {
      key[i] = 5000 - (int)(g.vis[i].deal_delay * 1000); /* still in the deck */
      continue;
    }
    Rectangle t;
    card_target(i, &t);
    bool moving = fabsf(t.x - g.vis[i].at.x) + fabsf(t.y - g.vis[i].at.y) > 3;
    key[i] = (moving ? 10000 : 0) + base_layer(i);
  }
  for (int i = 1; i < NCARDS; i++)
    for (int j = i; j > 0 && key[order[j]] < key[order[j - 1]]; j--) {
      int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
    }
  for (int n = 0; n < NCARDS; n++) {
    int i = order[n];
    if (g.dragging && i == g.pressed) continue;
    Rectangle r = g.vis[i].at;
    gd_card_flip(g.deck[i], r, g.vis[i].face);
    if (i == g.selected)
      DrawRectangleRoundedLinesEx((Rectangle){r.x - 3, r.y - 3, r.width + 6, r.height + 6},
                                  0.12f, 8, 3, GD_GOLD);
  }
}

static Color slot_highlight(PlSide side, int i, const PlArrangement *mine) {
  bool live = !g.demo && !g.help;
  if (g.screen == SCR_ARRANGE) {
    if (side != PL_PLAYER) return BLANK;
    int target = g.dragging ? slot_at(PL_PLAYER, gd_mouse, 16)
                 : g.selected >= 0 && live ? slot_at(PL_PLAYER, gd_mouse, 0) : -1;
    if (target == i) return GD_GOLD;
    const PlPile *p = &mine->piles[i];
    bool lone_wizard = p->count > 0;
    for (int k = 0; k < p->count; k++)
      if (p->cards[k].rank != PL_JOKER) lone_wizard = false;
    return lone_wizard ? GD_LOSE : BLANK;
  }
  if (g.screen == SCR_PREDICT)
    return side == PL_PLAYER && live && slot_at(PL_PLAYER, gd_mouse, 0) == i
               ? Fade(GD_GOLD, 0.6f) : BLANK;
  if (g.screen == SCR_BATTLE || g.screen == SCR_ROUND_END) {
    if (g.pick[side] == i)
      return g.outcome[side][i] && g.phase == BT_RESULT ? outcome_color(g.outcome[side][i]) : GD_GOLD;
    if (g.screen == SCR_BATTLE && g.phase == BT_CHOOSE && live && !g.match.used[side][i] &&
        slot_at(side, gd_mouse, 0) == i)
      return Fade(GD_GOLD, 0.55f);
  }
  return BLANK;
}

/* True once every card of the house's pile has turned face up. */
static bool house_pile_shown(int pile) {
  for (int h = 0; h < PL_HAND_SIZE; h++)
    if (g.house_pile[h] == pile && g.vis[PL_HAND_SIZE + h].face < 1) return false;
  return true;
}

static void draw_side_labels(void) {
  gd_text("HOUSE", 20, 16, 18, Fade(GD_CREAM, 0.6f), ALIGN_LEFT, true);
  gd_text("YOU", 20, PLAYER_Y + SLOT_H - 18, 18, Fade(GD_CREAM, 0.6f), ALIGN_LEFT, true);
}

/* Slots, cards and everything drawn on and around the piles. */
static void draw_table(void) {
  draw_felt();
  draw_side_labels();
  bool arranging = g.screen == SCR_ARRANGE;
  bool battling = g.screen == SCR_BATTLE || g.screen == SCR_ROUND_END;
  PlArrangement mine;
  build_arrangement(&mine);

  if (arranging)
    DrawRectangleRounded((Rectangle){28, HAND_Y - 12, 904, CARD_H + 24}, 0.12f, 8,
                         Fade(BLACK, 0.18f));
  for (int side = 0; side < 2; side++)
    for (int i = 0; i < PL_NUM_PILES; i++) {
      draw_slot(side, i);
      if (arranging && side == PL_PLAYER && mine.piles[i].count == 0) {
        Rectangle s = slot_rect(PL_PLAYER, i);
        gd_text(TextFormat("%d", i + 1), s.x + s.width / 2, s.y + s.height / 2 - 20, 40,
                Fade(GD_CREAM, 0.18f), ALIGN_CENTER, true);
      }
    }

  draw_cards();

  for (int side = 0; side < 2; side++)
    for (int i = 0; i < PL_NUM_PILES; i++) {
      Rectangle s = slot_rect(side, i);
      if (side == PL_PLAYER) draw_caption(side, i, &mine.piles[i]);
      else if (house_pile_shown(i))
        draw_caption(side, i, &g.house.piles[i]);
      else {
        int n = g.house.piles[i].count;
        gd_text(TextFormat("%d card%s", n, n == 1 ? "" : "s"), s.x + s.width / 2, s.y - 22,
                16, Fade(GD_CREAM, 0.6f), ALIGN_CENTER, false);
      }
      bool used = battling && g.match.used[side][i];
      bool fighting = g.pick[side] == i && g.phase == BT_RESULT;
      if (used && !fighting) DrawRectangleRounded(s, 0.08f, 8, Fade(BLACK, 0.4f));
      if (used) {
        char o = g.outcome[side][i];
        gd_pill(o == 'W' ? "WON" : o == 'L' ? "LOST" : "TIE", s.x + s.width / 2,
                s.y + s.height / 2, 20, outcome_color(o), (Color){20, 20, 20, 255});
      }
      Color hl = slot_highlight(side, i, &mine);
      if (hl.a) draw_highlight(side, i, hl);
    }

  if (g.screen >= SCR_FIRST) {
    Vector2 a = slot_edge(PL_PLAYER, g.match.predicted[PL_PLAYER]);
    Vector2 b = slot_edge(PL_HOUSE, g.match.predicted[PL_HOUSE]);
    gd_pill("YOUR PICK", a.x, a.y, 13, GD_GOLD, (Color){48, 32, 8, 255});
    gd_pill("HOUSE PICK", b.x, b.y, 13, (Color){196, 72, 64, 255}, GD_CREAM);
  }
  if (g.screen == SCR_BATTLE && g.pick[PL_PLAYER] >= 0 && g.pick[PL_HOUSE] >= 0) draw_vs();

  if (arranging && cards_in_hand() == 0)
    gd_text("All cards placed", BOARD_CX, HAND_Y + 46, 20, Fade(GD_CREAM, 0.5f),
            ALIGN_CENTER, true);
  if (g.dragging) {
    Rectangle r = g.vis[g.pressed].at;
    DrawRectangleRounded((Rectangle){r.x + 8, r.y + 12, r.width, r.height}, 0.12f, 8,
                         Fade(BLACK, 0.25f));
    gd_card(g.deck[g.pressed], r);
  }
}

/* Status text between the two rows of piles (while arranging). */
static void draw_message(const char *line, const char *sub, Color c) {
  float y = sub ? 296 : 306;
  gd_text(line, BOARD_CX, y, 24, c, ALIGN_CENTER, true);
  if (sub) gd_text(sub, BOARD_CX, y + 30, 16, GD_CREAM, ALIGN_CENTER, false);
}

/* Status banner below your piles, where the hand was, so the battle line
 * between the rows stays clear. */
static void draw_banner(const char *line, const char *sub, Color c) {
  DrawRectangleRounded((Rectangle){28, HAND_Y + 8, 904, 96}, 0.2f, 8, Fade(BLACK, 0.22f));
  float y = sub ? HAND_Y + 22 : HAND_Y + 38;
  gd_text(line, BOARD_CX, y, 28, c, ALIGN_CENTER, true);
  if (sub) gd_text(sub, BOARD_CX, y + 40, 18, GD_CREAM, ALIGN_CENTER, false);
}

/* ---- drawing: side panel ---- */

static void draw_pips(float x, float y, int filled, Color c) {
  for (int i = 0; i < 3; i++) {
    Vector2 p = {x + i * 26.0f, y};
    if (i < filled) DrawCircleV(p, 9, c);
    DrawRing(p, 8, 10, 0, 360, 24, Fade(c, 0.9f));
  }
}

static Color counting_color(float shown, int real) {
  if (shown < real - 0.5f) return GD_WIN;
  if (shown > real + 0.5f) return GD_LOSE;
  return GD_CREAM;
}

static void draw_panel(void) {
  DrawRectangle((int)PANEL_X, 0, VW - (int)PANEL_X, VH, (Color){12, 46, 32, 255});
  DrawLineEx((Vector2){PANEL_X, 0}, (Vector2){PANEL_X, VH}, 2, Fade(GD_GOLD, 0.5f));
  float cx = PANEL_X + (VW - PANEL_X) / 2, x0 = PANEL_X + 28, x1 = VW - 28;
  gd_text("PLATOON", cx, 18, 40, GD_GOLD, ALIGN_CENTER, true);
  gd_text("Ni no Kuni casino card game", cx, 64, 14, Fade(GD_CREAM, 0.6f), ALIGN_CENTER, false);

  const char *labels[] = {"House AI", "Chips", "Round", "Wager"};
  const char *values[] = {pl_difficulty_name(g.difficulty),
                          TextFormat("%d", (int)roundf(g.shown_chips)),
                          TextFormat("%d", g.round),
                          TextFormat("%d", (int)roundf(g.shown_wager))};
  Color colors[] = {GD_CREAM, counting_color(g.shown_chips, g.chips), GD_CREAM,
                    g.shown_wager == g.wager ? GD_GOLD : counting_color(g.shown_wager, g.wager)};
  for (int i = 0; i < 4; i++) {
    float y = 104 + i * 30.0f;
    gd_text(labels[i], x0, y, 18, Fade(GD_CREAM, 0.7f), ALIGN_LEFT, false);
    gd_text(values[i], x1, y, 18, colors[i], ALIGN_RIGHT, true);
  }

  bool battling = g.screen == SCR_BATTLE || g.screen == SCR_ROUND_END;
  gd_text("Wins", x0, 236, 16, Fade(GD_CREAM, 0.7f), ALIGN_LEFT, false);
  draw_pips(x1 - 62, 246, battling ? g.match.wins : 0, GD_WIN);
  gd_text("Losses + ties", x0, 266, 16, Fade(GD_CREAM, 0.7f), ALIGN_LEFT, false);
  draw_pips(x1 - 62, 276, battling ? g.match.losses : 0, GD_LOSE);

  float w = (x1 - x0 - 10) / 2;
  if (gd_button((Rectangle){x0, 490, w, 30}, "Rules (H)", true)) g.help = true;
  if (gd_button((Rectangle){x0 + w + 10, 490, w, 30},
                au_muted() ? "Sound off (M)" : "Sound on (M)", true))
    au_toggle_mute();

  const char *rules[] = {
      "K  King beats number piles", "A  Bishop beats only Kings",
      "W  Wizard swaps the two piles", "Same kind: more points wins",
      "Ties count as your loss", "Your pick wins: wager +50%",
      "House pick wins: wager halved"};
  gd_text("HOW BATTLES WORK", x0, 538, 15, GD_GOLD, ALIGN_LEFT, true);
  for (int i = 0; i < 7; i++)
    gd_text(rules[i], x0, 560 + i * 21.0f, 15, Fade(GD_CREAM, 0.8f), ALIGN_LEFT, false);
}

/* ---- drawing: screens ---- */

static void draw_menu(void) {
  draw_felt();
  PlCard fan[] = {{1, PL_JOKER}, {3, PL_KING}, {0, PL_ACE}};
  for (int i = 0; i < 3; i++) {
    float sway = sinf((float)GetTime() * 1.2f + i) * 2; /* a gentle idle sway */
    gd_card_rotated(fan[i], (Vector2){VW / 2.0f + (i - 1) * 74.0f, 120 + (i == 1 ? -10 : 0)},
                    1.25f, (i - 1) * 14.0f + sway);
  }
  float cx = VW / 2.0f;
  gd_text("PLATOON", cx, 214, 76, GD_GOLD, ALIGN_CENTER, true);
  gd_text("The casino card game from Ni no Kuni", cx, 296, 18, Fade(GD_CREAM, 0.8f),
          ALIGN_CENTER, false);

  gd_text("House difficulty", cx, 334, 18, GD_CREAM, ALIGN_CENTER, true);
  static const char *const blurbs[] = {
      "Random piles and random battles.",
      "Plays like the dealer in the real game.",
      "Plans every battle ahead. No mercy."};
  for (int d = PL_EASY; d <= PL_HARD; d++) {
    Rectangle r = {cx - 216 + d * 148.0f, 362, 136, 44};
    if (gd_toggle(r, pl_difficulty_name(d), g.difficulty == (PlDifficulty)d))
      g.difficulty = (PlDifficulty)d;
  }
  gd_text(blurbs[g.difficulty], cx, 416, 16, Fade(GD_CREAM, 0.75f), ALIGN_CENTER, false);

  Rectangle play = {cx - 170, 496, 340, 52};
  if (g.chips >= SET_COST) {
    gd_text(TextFormat("Chips: %d", (int)roundf(g.shown_chips)), cx, 452, 26,
            counting_color(g.shown_chips, g.chips), ALIGN_CENTER, true);
    if (gd_button(play, TextFormat("Play a set (%d chips)", SET_COST), true)) start_set();
  } else {
    gd_text(TextFormat("Out of chips (%d left)", g.chips), cx, 452, 26, GD_LOSE,
            ALIGN_CENTER, true);
    if (gd_button(play, TextFormat("Start over with %d chips", START_CHIPS), true))
      g.chips = START_CHIPS;
  }
  if (gd_button((Rectangle){cx - 170, 560, 165, 40}, "How to play", true)) g.help = true;
  if (gd_button((Rectangle){cx + 5, 560, 165, 40}, au_muted() ? "Sound: off" : "Sound: on", true))
    au_toggle_mute();
#if !defined(PLATFORM_WEB)
  if (gd_button((Rectangle){cx - 170, 612, 340, 40}, "Quit", true)) g.quit = true;
#endif
  gd_text("Drag or click cards to build piles  -  H rules  -  M sound  -  F11 fullscreen",
          cx, 686, 14, Fade(GD_CREAM, 0.5f), ALIGN_CENTER, false);
}

static void draw_arrange(void) {
  draw_table();
  if (!dealing())
    draw_message("Build your five piles",
                 g.selected >= 0 ? "Click a pile or press 1-5 to place the selected card"
                                 : "Drag cards onto piles, or click a card then a pile",
                 GD_CREAM);
  draw_panel();
  float x = PANEL_X + 28, w = VW - PANEL_X - 56;
  bool ready = !dealing() && !g.demo;
  if (gd_button((Rectangle){x, 300, w, 42}, "Auto arrange", ready)) auto_arrange();
  if (gd_button((Rectangle){x, 350, w, 42}, "Clear piles",
                ready && cards_in_hand() < PL_HAND_SIZE))
    clear_piles();
  const char *status = arrangement_status();
  gd_text(status ? status : "Ready!", x + w / 2, 404, 15,
          status ? Fade(GD_CREAM, 0.75f) : GD_WIN, ALIGN_CENTER, false);
  if (gd_button((Rectangle){x, 428, w, 48}, "Confirm piles", ready && status == NULL))
    confirm_arrangement();
}

static void draw_battle_banner(void) {
  BattlePhase phase = g.screen == SCR_BATTLE ? g.phase : BT_RESULT;
  switch (phase) {
  case BT_CHOOSE:
    draw_banner("Your turn",
                g.pick[PL_PLAYER] < 0 ? "Pick one of your piles, then a house pile to fight"
                                      : "Now pick a house pile to fight",
                GD_GOLD);
    break;
  case BT_HOUSE_THINK:
    draw_banner("The house is choosing...", NULL, GD_CREAM);
    break;
  case BT_HOUSE_SHOW:
    draw_banner(TextFormat("The house sends pile %d against your pile %d",
                           g.pick[PL_HOUSE] + 1, g.pick[PL_PLAYER] + 1),
                NULL, GD_CREAM);
    break;
  case BT_CLASH:
    draw_banner(TextFormat("Your pile %d against house pile %d", g.pick[PL_PLAYER] + 1,
                           g.pick[PL_HOUSE] + 1),
                NULL, GD_GOLD);
    break;
  case BT_RESULT: {
    const char *line = g.last.winner == PL_PLAYER ? "You win the battle!"
                       : g.last.winner == PL_HOUSE ? "The house wins the battle"
                                                   : "A tie - it counts against you";
    char sub[128] = "";
    if (g.last.swapped) strcat(sub, "A Wizard swapped the piles!  ");
    if (g.last.wager_after > g.last.wager_before)
      strcat(sub, TextFormat("Your pick won: wager %d -> %d", g.last.wager_before,
                             g.last.wager_after));
    else if (g.last.wager_after < g.last.wager_before)
      strcat(sub, TextFormat("House pick won: wager %d -> %d", g.last.wager_before,
                             g.last.wager_after));
    Color c = g.last.winner == PL_PLAYER ? GD_WIN : g.last.winner == PL_HOUSE ? GD_LOSE : GD_TIE;
    draw_banner(line, sub[0] ? sub : NULL, c);
    break;
  }
  }
}

static void draw_predict(void) {
  draw_table();
  draw_banner("Which of your piles will win?",
              "Click it (or press 1-5). If it wins a battle, your wager grows by half.",
              GD_GOLD);
  draw_panel();
}

static void draw_first(void) {
  draw_table();
  draw_panel();
  float in = ease_out(g.timer / 0.25f);
  DrawRectangle(0, 0, (int)PANEL_X, VH, Fade(BLACK, 0.45f * in));
  Rectangle p = {BOARD_CX - 230, 190 - 30 * (1 - in), 460, 340};
  gd_panel(p);
  gd_text("Draw for the first turn", BOARD_CX, p.y + 20, 24, GD_GOLD, ALIGN_CENTER, true);
  for (int i = 0; i < 2; i++) {
    float cx = BOARD_CX + (i == 0 ? -80 : 80);
    float face = (g.timer - FIRST_FLIP_AT - i * FIRST_FLIP_GAP) / 0.3f;
    gd_card_flip(g.draw_cards[i], (Rectangle){cx - 40, p.y + 64, CARD_W, CARD_H},
                 fminf(1, fmaxf(0, face)));
    gd_text(i == 0 ? "You" : "House", cx, p.y + 184, 18, GD_CREAM, ALIGN_CENTER, true);
  }
  if (g.timer >= FIRST_READY) {
    gd_text(g.match.turn == PL_PLAYER ? "You go first!" : "The house goes first",
            BOARD_CX, p.y + 216, 22, GD_CREAM, ALIGN_CENTER, true);
    if (g.draw_ties)
      gd_text(TextFormat("(%d tied draw%s redrawn)", g.draw_ties, g.draw_ties > 1 ? "s" : ""),
              BOARD_CX, p.y + 244, 14, Fade(GD_CREAM, 0.7f), ALIGN_CENTER, false);
  }
  if (gd_button((Rectangle){BOARD_CX - 120, p.y + 270, 240, 48}, "Start the battle",
                !g.demo && g.timer >= FIRST_READY))
    start_battle();
}

static void draw_battle(void) {
  draw_table();
  draw_battle_banner();
  draw_panel();
}

static void draw_round_end(void) {
  draw_table();
  draw_panel();
  float in = ease_out(g.timer / 0.3f);
  DrawRectangle(0, 0, (int)PANEL_X, VH, Fade(BLACK, 0.5f * in));
  Rectangle p = {BOARD_CX - 250, 180 - 40 * (1 - in), 500, 330};
  gd_panel(p);
  float y = p.y + 26;
  bool live = !g.demo && in >= 1;
  if (!g.round_won) {
    gd_text(TextFormat("Round %d lost", g.round), BOARD_CX, y, 34, GD_LOSE, ALIGN_CENTER, true);
    gd_text(TextFormat("The house takes your %d chip wager.", g.wager), BOARD_CX, y + 70, 18,
            GD_CREAM, ALIGN_CENTER, false);
    if (gd_button((Rectangle){BOARD_CX - 140, p.y + 250, 280, 50}, "Back to the menu", live))
      back_to_menu();
    return;
  }
  gd_text(TextFormat("Round %d won!", g.round), BOARD_CX, y, 34, GD_WIN, ALIGN_CENTER, true);
  gd_text("Winnings", BOARD_CX, y + 58, 18, Fade(GD_CREAM, 0.7f), ALIGN_CENTER, false);
  gd_text(TextFormat("%d chips", (int)roundf(g.shown_wager)), BOARD_CX, y + 80, 34, GD_GOLD,
          ALIGN_CENTER, true);
  if (can_let_it_ride()) {
    gd_text(g.round == 6 ? "Under 100,000 chips: a bonus 7th round is open."
                         : "Let it ride to play the next round for it all.",
            BOARD_CX, y + 132, 15, Fade(GD_CREAM, 0.8f), ALIGN_CENTER, false);
    if (gd_button((Rectangle){BOARD_CX - 220, p.y + 250, 210, 50},
                  TextFormat("Let it ride: round %d", g.round + 1), live))
      let_it_ride();
    if (gd_button((Rectangle){BOARD_CX + 10, p.y + 250, 210, 50}, "Take the chips", live))
      back_to_menu();
  } else {
    gd_text(g.round >= 7 ? "Seven straight wins. The table is closed!"
                         : "Six straight wins. The table pays out!",
            BOARD_CX, y + 132, 15, Fade(GD_CREAM, 0.8f), ALIGN_CENTER, false);
    if (gd_button((Rectangle){BOARD_CX - 140, p.y + 250, 280, 50}, "Collect", live))
      back_to_menu();
  }
}

static void draw(void) {
  switch (g.screen) {
  case SCR_MENU: draw_menu(); break;
  case SCR_ARRANGE: draw_arrange(); break;
  case SCR_PREDICT: draw_predict(); break;
  case SCR_FIRST: draw_first(); break;
  case SCR_BATTLE: draw_battle(); break;
  case SCR_ROUND_END: draw_round_end(); break;
  }
}

/* ---- main loop ---- */

static const char *shots; /* PLATOON_SHOTS directory, or NULL */
static int last_state = -1, shot_count;
static float shot_timer = -1;

static void save_screenshot(const char *path) {
  /* Not LoadImageFromScreen: raylib 5.5 scales that by the desktop DPI. */
  int w = GetRenderWidth(), h = GetRenderHeight();
  Image img = {rlReadScreenPixels(w, h), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
  ExportImage(img, path);
  UnloadImage(img);
}

static void approach(float *shown, int real, float dt) {
  *shown += (real - *shown) * fminf(1, dt * 5);
  if (fabsf(real - *shown) < 0.5f) *shown = (float)real;
}

static void play_click(void) { au_play(SND_CLICK); }

#if defined(PLATFORM_WEB)
/* Fits the canvas to the page. raylib's own resize handling leaves GLFW's idea
 * of the window size stale, which skews the mouse, so go through SetWindowSize. */
static void fit_canvas(void) {
  static int last_w, last_h;
  int w = emscripten_run_script_int("window.innerWidth");
  int h = emscripten_run_script_int("window.innerHeight");
  if (w == last_w && h == last_h) return;
  SetWindowSize(w, h);
  last_w = w;
  last_h = h;
}
#endif

static void frame(void) {
#if defined(PLATFORM_WEB)
  fit_canvas();
#endif
  float dt = fminf(GetFrameTime(), 0.1f);
  g.dt = dt;
  float scale = fminf(GetScreenWidth() / (float)VW, GetScreenHeight() / (float)VH);
  Camera2D cam = {.offset = {(GetScreenWidth() - VW * scale) / 2,
                             (GetScreenHeight() - VH * scale) / 2},
                  .zoom = scale};
  in_frame(dt, GetScreenToWorld2D(GetMousePosition(), cam));
  gd_mouse = in_mouse();
  if (in_quit()) g.quit = true;
#if !defined(PLATFORM_WEB)
  if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();
#endif
  if (in_key(KEY_M)) au_toggle_mute();

  if (g.help) {
    if (in_key(KEY_ESCAPE) || in_key(KEY_H) || in_key(KEY_F1)) g.help = false;
  } else {
    if (in_key(KEY_H) || in_key(KEY_F1)) g.help = true;
    g.timer += dt;
    update();
    if (g.screen != SCR_MENU) update_cards(dt);
  }
  approach(&g.shown_wager, g.wager, dt);
  approach(&g.shown_chips, g.chips, dt);

  BeginDrawing();
  ClearBackground(GD_FELT_DARK); /* the bars around a letterboxed table */
  BeginScissorMode((int)cam.offset.x, (int)cam.offset.y, (int)ceilf(VW * scale),
                   (int)ceilf(VH * scale));
  BeginMode2D(cam);
  gd_begin_frame();
  if (g.help) {
    Vector2 real = gd_mouse;
    gd_mouse = (Vector2){-10000, -10000}; /* the table underneath ignores the mouse */
    draw();
    gd_mouse = real;
    if (help_draw()) g.help = false;
  } else {
    draw();
  }
  EndMode2D();
  EndScissorMode();
  if (shots) {
    /* Scripts name their shots; otherwise take one after each screen change. */
    const char *name = in_shot();
    if (!in_scripted()) {
      int state = g.screen * 10 + (g.screen == SCR_BATTLE ? (int)g.phase : 0);
      if (state != last_state) { last_state = state; shot_timer = 0.6f; }
      if (shot_timer > 0 && (shot_timer -= dt) <= 0 && shot_count < 200)
        name = TextFormat("shot%03d_s%d", shot_count++, state);
    }
    if (name) save_screenshot(TextFormat("%s/%s.png", shots, name));
  }
  EndDrawing();
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++)
    if (strcmp(argv[i], "--demo") == 0) g.demo = true;
  shots = getenv("PLATOON_SHOTS");
  const char *seed = getenv("PLATOON_SEED");
  srand(seed ? (unsigned)atoi(seed) : (unsigned)time(NULL));
  in_init();

#if defined(PLATFORM_WEB)
  SetConfigFlags(FLAG_MSAA_4X_HINT); /* fit_canvas handles page resizes */
#else
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
#endif
  SetTraceLogLevel(LOG_WARNING);
  InitWindow(VW, VH, "Platoon");
  SetWindowMinSize(VW / 2, VH / 2);
  SetExitKey(KEY_NULL);
  in_attach();
  gd_load();
  au_init();
  gd_click_hook = play_click;

  g.chips = START_CHIPS;
  g.shown_chips = START_CHIPS;
  g.difficulty = PL_MEDIUM;
  g.screen = SCR_MENU;

#if defined(PLATFORM_WEB)
  emscripten_set_main_loop(frame, 0, 1);
#else
  SetTargetFPS(60);
  while (!WindowShouldClose() && !g.quit) frame();
#endif
  au_close();
  gd_unload();
  CloseWindow();
  return 0;
}
