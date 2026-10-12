/* Graphical front end for Platoon, built on raylib.
 *
 * Everything is laid out on a virtual 1280x720 table that is scaled to fit the
 * window. The left part is the board (house piles on top, yours below, your
 * hand at the bottom while arranging); the right part is the score panel.
 *
 * Run with --demo to watch the Hard AI play your side. For testing,
 * PLATOON_SCRIPT drives the input (see gui_input.c), PLATOON_SHOTS=dir saves
 * screenshots and PLATOON_SEED fixes the shuffle.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gui_draw.h"
#include "gui_input.h"
#include "platoon.h"
#include "raylib.h"
#include "rlgl.h"

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

#define START_CHIPS 1000
#define SET_COST 200
#define MAX_CHIPS 9999999
#define ROUND_CAP 99999 /* at or below this after round 6, a 7th round opens */

typedef enum {
  SCR_MENU, SCR_ARRANGE, SCR_PREDICT, SCR_FIRST, SCR_BATTLE, SCR_ROUND_END
} Screen;
typedef enum { BT_CHOOSE, BT_HOUSE_THINK, BT_HOUSE_SHOW, BT_RESULT } BattlePhase;

static struct {
  Screen screen;
  PlDifficulty difficulty;
  int chips, wager, round;
  PlCard deck[PL_DECK_SIZE]; /* deck[0..9] is your hand, deck[10..19] the house's */
  float timer;               /* seconds since the screen or phase began */

  /* Arranging. */
  int pile_of[PL_HAND_SIZE]; /* -1 while the card is still in your hand */
  int placed_at[PL_HAND_SIZE];
  int place_counter;
  int selected; /* hand card picked for click-to-assign, -1 if none */
  int pressed;  /* card under the held mouse button, -1 if none */
  Vector2 press_pos, grab;
  bool dragging;

  /* Predicting and battling. */
  PlArrangement mine, house;
  PlMatch match;
  PlCard draw_cards[2];
  int draw_ties;
  int pick[2]; /* piles chosen for the next battle, indexed by PlSide */
  BattlePhase phase;
  PlBattleResult last;
  char outcome[2][PL_NUM_PILES]; /* 'W', 'L' or 'T' from that side's view */

  bool round_won, quit, demo;
} g;

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

static bool clicked(void) { return in_pressed(MOUSE_BUTTON_LEFT); }

static bool skip_pressed(void) {
  return in_key(KEY_SPACE) || in_key(KEY_ENTER) || clicked();
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

/* Where card i sits right now (not counting a drag in progress). */
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

/* The topmost card under the point, or -1. */
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
  const char *err = pl_arrangement_error(&a);
  return err ? err : NULL;
}

static void select_next_in_hand(void) {
  int m[PL_HAND_SIZE];
  g.selected = hand_members(m) > 0 ? m[0] : -1;
}

/* ---- game flow ---- */

static void start_round(void) {
  pl_deck_fill(g.deck);
  pl_deck_shuffle(g.deck);
  for (int i = 0; i < PL_HAND_SIZE; i++) g.pile_of[i] = -1;
  g.place_counter = 0;
  g.selected = g.pressed = -1;
  g.dragging = false;
  g.pick[0] = g.pick[1] = -1;
  memset(g.outcome, 0, sizeof g.outcome);
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

static void confirm_arrangement(void) {
  build_arrangement(&g.mine);
  pl_ai_arrange(g.difficulty, g.deck + PL_HAND_SIZE, &g.house);
  g.selected = -1;
  g.screen = SCR_PREDICT;
  g.timer = 0;
}

static void choose_prediction(int pile) {
  int house_pick = pl_ai_predict(g.difficulty, &g.house, &g.mine);
  /* Draw from the undealt cards for the first turn; equal ranks redraw. */
  int i = 2 * PL_HAND_SIZE;
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

static void fight(void) {
  if (!pl_match_battle(&g.match, g.pick[PL_PLAYER], g.pick[PL_HOUSE], &g.last))
    return;
  char o = g.last.winner == PL_PLAYER ? 'W' : g.last.winner == PL_HOUSE ? 'L' : 'T';
  g.outcome[PL_PLAYER][g.pick[PL_PLAYER]] = o;
  g.outcome[PL_HOUSE][g.pick[PL_HOUSE]] = o == 'W' ? 'L' : o == 'L' ? 'W' : 'T';
  g.wager = g.match.wager;
  g.phase = BT_RESULT;
  g.timer = 0;
}

static void finish_round(void) {
  g.round_won = g.match.state == PL_MATCH_WON;
  g.wager = g.match.wager;
  if (g.round_won) g.wager = g.wager > MAX_CHIPS / 2 ? MAX_CHIPS : g.wager * 2;
  g.screen = SCR_ROUND_END;
  g.timer = 0;
}

static bool can_let_it_ride(void) {
  return g.round_won && (g.round < 6 || (g.round == 6 && g.wager <= ROUND_CAP));
}

static void back_to_menu(void) {
  if (g.round_won) g.chips = g.chips > MAX_CHIPS - g.wager ? MAX_CHIPS : g.chips + g.wager;
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
  if (g.demo) {
    if (g.timer > 0.8f && cards_in_hand() > 0) auto_arrange();
    if (g.timer > 2.0f) confirm_arrangement();
    return;
  }
  Vector2 m = gd_mouse;
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
    if (g.dragging) {
      int p = slot_at(PL_PLAYER, m, 16);
      if (p >= 0) place(c, p);
      else if (m.y > HAND_Y - 30) g.pile_of[c] = -1;
    } else if (g.pile_of[c] < 0) {
      g.selected = g.selected == c ? -1 : c;
    } else if (g.selected >= 0) {
      place(g.selected, g.pile_of[c]);
      g.selected = -1;
    } else {
      g.pile_of[c] = -1;
    }
    g.pressed = -1;
    g.dragging = false;
  }
  if (in_pressed(MOUSE_BUTTON_RIGHT)) {
    int c = card_at(m);
    if (c >= 0 && g.pile_of[c] >= 0) g.pile_of[c] = -1;
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
    if (g.timer > 1.0f) choose_prediction(pl_ai_predict(PL_HARD, &g.mine, &g.house));
    return;
  }
  int p = clicked() ? slot_at(PL_PLAYER, gd_mouse, 0) : -1;
  for (int k = 0; k < PL_NUM_PILES; k++)
    if (in_key(KEY_ONE + k)) p = k;
  if (p >= 0) choose_prediction(p);
}

static void update_first(void) {
  if (g.demo ? g.timer > 1.5f : in_key(KEY_SPACE) || in_key(KEY_ENTER))
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
      if (p >= 0 && !g.match.used[PL_PLAYER][p])
        g.pick[PL_PLAYER] = g.pick[PL_PLAYER] == p ? -1 : p;
      if (h >= 0 && !g.match.used[PL_HOUSE][h])
        g.pick[PL_HOUSE] = g.pick[PL_HOUSE] == h ? -1 : h;
      if (g.pick[PL_PLAYER] >= 0 && g.pick[PL_HOUSE] >= 0) fight();
    }
    break;
  case BT_HOUSE_THINK:
    if (g.timer > 0.8f) {
      pl_ai_choose_battle(g.difficulty, &g.match, PL_HOUSE, &g.pick[PL_PLAYER],
                          &g.pick[PL_HOUSE]);
      g.phase = BT_HOUSE_SHOW;
      g.timer = 0;
    }
    break;
  case BT_HOUSE_SHOW:
    if (g.timer > 1.0f) fight();
    break;
  case BT_RESULT:
    if (g.timer > 2.4f || (g.timer > 0.3f && !g.demo && skip_pressed())) {
      if (g.match.state == PL_MATCH_ACTIVE) begin_turn();
      else finish_round();
    }
    break;
  }
}

static void update_round_end(void) {
  if (g.demo && g.timer > 2.0f) {
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

/* ---- drawing: board ---- */

static void draw_felt(void) {
  DrawRectangle(0, 0, VW, VH, GD_FELT_DARK);
  DrawCircleGradient((int)BOARD_CX, VH / 2, 720, (Color){34, 116, 80, 255}, GD_FELT_DARK);
}

static void draw_slot(PlSide side, int i, Color highlight) {
  Rectangle s = slot_rect(side, i);
  DrawRectangleRounded(s, 0.08f, 8, Fade(BLACK, 0.16f));
  DrawRectangleRoundedLinesEx(s, 0.08f, 8, 1.5f, Fade(GD_CREAM, 0.22f));
  if (highlight.a) {
    Rectangle h = {s.x - 4, s.y - 4, s.width + 8, s.height + 8};
    DrawRectangleRoundedLinesEx(h, 0.08f, 8, 3, highlight);
  }
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

static void draw_pile(PlSide side, int i, const PlPile *pile) {
  for (int k = 0; k < pile->count; k++)
    gd_card(pile->cards[k], stacked_card(side, i, k, pile->count));
}

static Vector2 slot_edge(PlSide side, int i) {
  Rectangle s = slot_rect(side, i);
  return (Vector2){s.x + s.width / 2, side == PL_HOUSE ? s.y + s.height : s.y};
}

static void draw_vs(int player_pile, int house_pile) {
  Vector2 a = slot_edge(PL_PLAYER, player_pile), b = slot_edge(PL_HOUSE, house_pile);
  Vector2 mid = {(a.x + b.x) / 2, (a.y + b.y) / 2};
  DrawLineEx(a, b, 5, GD_GOLD);
  DrawCircleV(mid, 20, GD_GOLD);
  gd_text("VS", mid.x, mid.y - 9, 18, (Color){48, 32, 8, 255}, ALIGN_CENTER, true);
}

static Color outcome_color(char o) {
  return o == 'W' ? GD_WIN : o == 'L' ? GD_LOSE : GD_TIE;
}

/* Both sides' piles face up, with predictions and battle results. */
static void draw_board(void) {
  bool battling = g.screen == SCR_BATTLE || g.screen == SCR_ROUND_END;
  bool predicted = g.screen >= SCR_FIRST;
  int hover[2] = {-1, -1};
  if (g.screen == SCR_PREDICT && !g.demo) hover[PL_PLAYER] = slot_at(PL_PLAYER, gd_mouse, 0);
  if (g.screen == SCR_BATTLE && g.phase == BT_CHOOSE && !g.demo) {
    hover[PL_PLAYER] = slot_at(PL_PLAYER, gd_mouse, 0);
    hover[PL_HOUSE] = slot_at(PL_HOUSE, gd_mouse, 0);
  }

  for (int side = 0; side < 2; side++) {
    const PlArrangement *arr = side == PL_PLAYER ? &g.mine : &g.house;
    for (int i = 0; i < PL_NUM_PILES; i++) {
      bool used = battling && g.match.used[side][i];
      Color hl = BLANK;
      if (battling && g.pick[side] == i)
        hl = g.outcome[side][i] ? outcome_color(g.outcome[side][i]) : GD_GOLD;
      else if (hover[side] == i && !used)
        hl = Fade(GD_GOLD, 0.55f);
      draw_slot(side, i, hl);
      draw_pile(side, i, &arr->piles[i]);
      draw_caption(side, i, &arr->piles[i]);
      if (used && !(g.pick[side] == i && g.phase == BT_RESULT)) {
        Rectangle s = slot_rect(side, i);
        DrawRectangleRounded(s, 0.08f, 8, Fade(BLACK, 0.4f));
      }
      if (used) {
        char o = g.outcome[side][i];
        Rectangle s = slot_rect(side, i);
        gd_pill(o == 'W' ? "WON" : o == 'L' ? "LOST" : "TIE", s.x + s.width / 2,
                s.y + s.height / 2, 20, outcome_color(o), (Color){20, 20, 20, 255});
      }
    }
  }
  if (predicted) {
    Vector2 a = slot_edge(PL_PLAYER, g.match.predicted[PL_PLAYER]);
    Vector2 b = slot_edge(PL_HOUSE, g.match.predicted[PL_HOUSE]);
    gd_pill("YOUR PICK", a.x, a.y, 13, GD_GOLD, (Color){48, 32, 8, 255});
    gd_pill("HOUSE PICK", b.x, b.y, 13, (Color){196, 72, 64, 255}, GD_CREAM);
  }
  if (g.screen == SCR_BATTLE && g.pick[PL_PLAYER] >= 0 && g.pick[PL_HOUSE] >= 0)
    draw_vs(g.pick[PL_PLAYER], g.pick[PL_HOUSE]);
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

static void draw_side_labels(void) {
  gd_text("HOUSE", 20, 16, 18, Fade(GD_CREAM, 0.6f), ALIGN_LEFT, true);
  gd_text("YOU", 20, PLAYER_Y + SLOT_H - 18, 18, Fade(GD_CREAM, 0.6f), ALIGN_LEFT, true);
}

/* ---- drawing: side panel ---- */

static void draw_pips(float x, float y, int filled, Color c) {
  for (int i = 0; i < 3; i++) {
    Vector2 p = {x + i * 26.0f, y};
    if (i < filled) DrawCircleV(p, 9, c);
    DrawRing(p, 8, 10, 0, 360, 24, Fade(c, 0.9f));
  }
}

static void draw_panel(void) {
  DrawRectangle((int)PANEL_X, 0, VW - (int)PANEL_X, VH, (Color){12, 46, 32, 255});
  DrawLineEx((Vector2){PANEL_X, 0}, (Vector2){PANEL_X, VH}, 2, Fade(GD_GOLD, 0.5f));
  float cx = PANEL_X + (VW - PANEL_X) / 2, x0 = PANEL_X + 28, x1 = VW - 28;
  gd_text("PLATOON", cx, 18, 40, GD_GOLD, ALIGN_CENTER, true);
  gd_text("Ni no Kuni casino card game", cx, 64, 14, Fade(GD_CREAM, 0.6f), ALIGN_CENTER, false);

  const char *labels[] = {"House AI", "Chips", "Round", "Wager"};
  const char *values[] = {pl_difficulty_name(g.difficulty), TextFormat("%d", g.chips),
                          TextFormat("%d", g.round), TextFormat("%d", g.wager)};
  for (int i = 0; i < 4; i++) {
    float y = 104 + i * 30.0f;
    gd_text(labels[i], x0, y, 18, Fade(GD_CREAM, 0.7f), ALIGN_LEFT, false);
    gd_text(values[i], x1, y, 18, i == 3 ? GD_GOLD : GD_CREAM, ALIGN_RIGHT, true);
  }

  bool battling = g.screen == SCR_BATTLE || g.screen == SCR_ROUND_END;
  gd_text("Wins", x0, 236, 16, Fade(GD_CREAM, 0.7f), ALIGN_LEFT, false);
  draw_pips(x1 - 62, 246, battling ? g.match.wins : 0, GD_WIN);
  gd_text("Losses + ties", x0, 266, 16, Fade(GD_CREAM, 0.7f), ALIGN_LEFT, false);
  draw_pips(x1 - 62, 276, battling ? g.match.losses : 0, GD_LOSE);

  const char *rules[] = {
      "K  King beats number piles", "A  Bishop beats only Kings",
      "W  Wizard swaps the two piles", "Same kind: more points wins",
      "Ties count as your loss", "Your pick wins: wager +50%",
      "House pick wins: wager halved"};
  gd_text("HOW BATTLES WORK", x0, 528, 15, GD_GOLD, ALIGN_LEFT, true);
  for (int i = 0; i < 7; i++)
    gd_text(rules[i], x0, 552 + i * 22.0f, 15, Fade(GD_CREAM, 0.8f), ALIGN_LEFT, false);
}

/* ---- drawing: screens ---- */

static void draw_menu(void) {
  draw_felt();
  PlCard fan[] = {{1, PL_JOKER}, {3, PL_KING}, {0, PL_ACE}};
  for (int i = 0; i < 3; i++)
    gd_card_rotated(fan[i], (Vector2){VW / 2.0f + (i - 1) * 74.0f, 120 + (i == 1 ? -10 : 0)},
                    1.25f, (i - 1) * 14.0f);
  float cx = VW / 2.0f;
  gd_text("PLATOON", cx, 214, 76, GD_GOLD, ALIGN_CENTER, true);
  gd_text("The casino card game from Ni no Kuni", cx, 296, 18, Fade(GD_CREAM, 0.8f),
          ALIGN_CENTER, false);

  gd_text("House difficulty", cx, 340, 18, GD_CREAM, ALIGN_CENTER, true);
  static const char *const blurbs[] = {
      "Random piles and random battles.",
      "Plays like the dealer in the real game.",
      "Plans every battle ahead. No mercy."};
  for (int d = PL_EASY; d <= PL_HARD; d++) {
    Rectangle r = {cx - 216 + d * 148.0f, 368, 136, 46};
    if (gd_toggle(r, pl_difficulty_name(d), g.difficulty == (PlDifficulty)d))
      g.difficulty = (PlDifficulty)d;
  }
  gd_text(blurbs[g.difficulty], cx, 424, 16, Fade(GD_CREAM, 0.75f), ALIGN_CENTER, false);

  Rectangle play = {cx - 170, 506, 340, 56};
  if (g.chips >= SET_COST) {
    gd_text(TextFormat("Chips: %d", g.chips), cx, 462, 26, GD_CREAM, ALIGN_CENTER, true);
    if (gd_button(play, TextFormat("Play a set (%d chips)", SET_COST), true)) start_set();
  } else {
    gd_text(TextFormat("Out of chips (%d left)", g.chips), cx, 462, 26, GD_LOSE,
            ALIGN_CENTER, true);
    if (gd_button(play, TextFormat("Start over with %d chips", START_CHIPS), true))
      g.chips = START_CHIPS;
  }
  if (gd_button((Rectangle){cx - 170, 578, 340, 46}, "Quit", true)) g.quit = true;
  gd_text("Drag or click cards to build piles  -  F11 fullscreen", cx, 686, 14,
          Fade(GD_CREAM, 0.5f), ALIGN_CENTER, false);
}

static void draw_arrange(void) {
  draw_felt();
  draw_side_labels();
  for (int i = 0; i < PL_NUM_PILES; i++) {
    draw_slot(PL_HOUSE, i, BLANK);
    Rectangle s = slot_rect(PL_HOUSE, i);
    gd_card_back((Rectangle){s.x + (SLOT_W - CARD_W) / 2, s.y + 8, CARD_W, CARD_H});
  }

  int target = -1;
  if (g.dragging) target = slot_at(PL_PLAYER, gd_mouse, 16);
  else if (g.selected >= 0) target = slot_at(PL_PLAYER, gd_mouse, 0);

  int m[PL_HAND_SIZE];
  for (int p = 0; p < PL_NUM_PILES; p++) {
    int n = pile_members(p, m);
    PlPile pile = {{{0}}, n};
    for (int k = 0; k < n; k++) pile.cards[k] = g.deck[m[k]];
    bool lone_wizard = n > 0;
    for (int k = 0; k < n; k++)
      if (pile.cards[k].rank != PL_JOKER) lone_wizard = false;
    draw_slot(PL_PLAYER, p, target == p ? GD_GOLD : lone_wizard ? GD_LOSE : BLANK);
    if (n == 0) {
      Rectangle s = slot_rect(PL_PLAYER, p);
      gd_text(TextFormat("%d", p + 1), s.x + s.width / 2, s.y + s.height / 2 - 20, 40,
              Fade(GD_CREAM, 0.18f), ALIGN_CENTER, true);
    }
    for (int k = 0; k < n; k++)
      if (!(g.dragging && m[k] == g.pressed))
        gd_card(g.deck[m[k]], stacked_card(PL_PLAYER, p, k, n));
    draw_caption(PL_PLAYER, p, &pile);
  }

  DrawRectangleRounded((Rectangle){28, HAND_Y - 12, 904, CARD_H + 24}, 0.12f, 8,
                       Fade(BLACK, 0.18f));
  int n = hand_members(m);
  int hover = g.dragging ? -1 : card_at(gd_mouse);
  for (int k = 0; k < n; k++) {
    if (g.dragging && m[k] == g.pressed) continue;
    Rectangle r = card_home(m[k]);
    if (hover == m[k] && m[k] != g.selected) r.y -= 6;
    gd_card(g.deck[m[k]], r);
    if (m[k] == g.selected)
      DrawRectangleRoundedLinesEx((Rectangle){r.x - 3, r.y - 3, r.width + 6, r.height + 6},
                                  0.12f, 8, 3, GD_GOLD);
  }
  if (n == 0)
    gd_text("All cards placed", BOARD_CX, HAND_Y + 46, 20, Fade(GD_CREAM, 0.5f),
            ALIGN_CENTER, true);

  if (g.dragging) {
    Rectangle r = {gd_mouse.x - g.grab.x, gd_mouse.y - g.grab.y, CARD_W, CARD_H};
    DrawRectangleRounded((Rectangle){r.x + 8, r.y + 12, r.width, r.height}, 0.12f, 8,
                         Fade(BLACK, 0.25f));
    gd_card(g.deck[g.pressed], r);
  }

  draw_message("Build your five piles",
               g.selected >= 0 ? "Click a pile or press 1-5 to place the selected card"
                               : "Drag cards onto piles, or click a card then a pile",
               GD_CREAM);

  draw_panel();
  float x = PANEL_X + 28, w = VW - PANEL_X - 56;
  if (gd_button((Rectangle){x, 316, w, 44}, "Auto arrange", true)) auto_arrange();
  if (gd_button((Rectangle){x, 370, w, 44}, "Clear piles", cards_in_hand() < PL_HAND_SIZE))
    for (int i = 0; i < PL_HAND_SIZE; i++) g.pile_of[i] = -1;
  const char *status = arrangement_status();
  gd_text(status ? status : "Ready!", x + w / 2, 430, 15,
          status ? Fade(GD_CREAM, 0.75f) : GD_WIN, ALIGN_CENTER, false);
  if (gd_button((Rectangle){x, 456, w, 52}, "Confirm piles", status == NULL))
    confirm_arrangement();
}

static void draw_battle_message(void) {
  switch (g.screen == SCR_BATTLE ? g.phase : BT_RESULT) {
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
  draw_felt();
  draw_side_labels();
  draw_board();
  draw_banner("Which of your piles will win?",
               "Click it (or press 1-5). If it wins a battle, your wager grows by half.",
               GD_GOLD);
  draw_panel();
}

static void draw_first(void) {
  draw_felt();
  draw_side_labels();
  draw_board();
  draw_panel();
  DrawRectangle(0, 0, (int)PANEL_X, VH, Fade(BLACK, 0.45f));
  Rectangle p = {BOARD_CX - 230, 190, 460, 340};
  gd_panel(p);
  gd_text("Draw for the first turn", BOARD_CX, p.y + 20, 24, GD_GOLD, ALIGN_CENTER, true);
  for (int i = 0; i < 2; i++) {
    float cx = BOARD_CX + (i == 0 ? -80 : 80);
    gd_card(g.draw_cards[i], (Rectangle){cx - 40, p.y + 64, CARD_W, CARD_H});
    gd_text(i == 0 ? "You" : "House", cx, p.y + 184, 18, GD_CREAM, ALIGN_CENTER, true);
  }
  gd_text(g.match.turn == PL_PLAYER ? "You go first!" : "The house goes first",
          BOARD_CX, p.y + 216, 22, GD_CREAM, ALIGN_CENTER, true);
  if (g.draw_ties)
    gd_text(TextFormat("(%d tied draw%s redrawn)", g.draw_ties, g.draw_ties > 1 ? "s" : ""),
            BOARD_CX, p.y + 244, 14, Fade(GD_CREAM, 0.7f), ALIGN_CENTER, false);
  if (gd_button((Rectangle){BOARD_CX - 120, p.y + 270, 240, 48}, "Start the battle", !g.demo))
    start_battle();
}

static void draw_battle(void) {
  draw_felt();
  draw_side_labels();
  draw_board();
  draw_battle_message();
  draw_panel();
}

static void draw_round_end(void) {
  draw_felt();
  draw_side_labels();
  draw_board();
  draw_panel();
  DrawRectangle(0, 0, (int)PANEL_X, VH, Fade(BLACK, 0.5f));
  Rectangle p = {BOARD_CX - 250, 180, 500, 330};
  gd_panel(p);
  float y = p.y + 26;
  if (!g.round_won) {
    gd_text(TextFormat("Round %d lost", g.round), BOARD_CX, y, 34, GD_LOSE, ALIGN_CENTER, true);
    gd_text(TextFormat("The house takes your %d chip wager.", g.wager), BOARD_CX, y + 70, 18,
            GD_CREAM, ALIGN_CENTER, false);
    if (gd_button((Rectangle){BOARD_CX - 140, p.y + 250, 280, 50}, "Back to the menu", !g.demo))
      back_to_menu();
    return;
  }
  gd_text(TextFormat("Round %d won!", g.round), BOARD_CX, y, 34, GD_WIN, ALIGN_CENTER, true);
  gd_text("Winnings", BOARD_CX, y + 58, 18, Fade(GD_CREAM, 0.7f), ALIGN_CENTER, false);
  gd_text(TextFormat("%d chips", g.wager), BOARD_CX, y + 80, 34, GD_GOLD, ALIGN_CENTER, true);
  if (can_let_it_ride()) {
    gd_text(g.round == 6 ? "Under 100,000 chips: a bonus 7th round is open."
                         : "Let it ride to play the next round for it all.",
            BOARD_CX, y + 132, 15, Fade(GD_CREAM, 0.8f), ALIGN_CENTER, false);
    if (gd_button((Rectangle){BOARD_CX - 220, p.y + 250, 210, 50},
                  TextFormat("Let it ride: round %d", g.round + 1), !g.demo))
      let_it_ride();
    if (gd_button((Rectangle){BOARD_CX + 10, p.y + 250, 210, 50}, "Take the chips", !g.demo))
      back_to_menu();
  } else {
    gd_text(g.round >= 7 ? "Seven straight wins. The table is closed!"
                         : "Six straight wins. The table pays out!",
            BOARD_CX, y + 132, 15, Fade(GD_CREAM, 0.8f), ALIGN_CENTER, false);
    if (gd_button((Rectangle){BOARD_CX - 140, p.y + 250, 280, 50}, "Collect", !g.demo))
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

static void save_screenshot(const char *path) {
  /* Not LoadImageFromScreen: raylib 5.5 scales that by the desktop DPI. */
  int w = GetRenderWidth(), h = GetRenderHeight();
  Image img = {rlReadScreenPixels(w, h), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
  ExportImage(img, path);
  UnloadImage(img);
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++)
    if (strcmp(argv[i], "--demo") == 0) g.demo = true;
  const char *shots = getenv("PLATOON_SHOTS");

  /* Debug aid: PLATOON_SEED fixes the shuffle, for repeatable scripted runs. */
  const char *seed = getenv("PLATOON_SEED");
  srand(seed ? (unsigned)atoi(seed) : (unsigned)time(NULL));
  in_init();
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
  SetTraceLogLevel(LOG_WARNING);
  InitWindow(VW, VH, "Platoon");
  SetWindowMinSize(VW / 2, VH / 2);
  SetExitKey(KEY_NULL);
  SetTargetFPS(60);
  gd_load();

  g.chips = START_CHIPS;
  g.difficulty = PL_MEDIUM;
  g.screen = SCR_MENU;

  int last_state = -1, shot_count = 0;
  float shot_timer = -1;
  while (!WindowShouldClose() && !g.quit) {
    float dt = GetFrameTime();
    g.timer += dt;
    float scale = fminf(GetScreenWidth() / (float)VW, GetScreenHeight() / (float)VH);
    Camera2D cam = {.offset = {(GetScreenWidth() - VW * scale) / 2,
                               (GetScreenHeight() - VH * scale) / 2},
                    .zoom = scale};
    in_frame(dt, GetScreenToWorld2D(GetMousePosition(), cam));
    gd_mouse = in_mouse();
    if (in_quit()) break;
    if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();

    update();

    BeginDrawing();
    ClearBackground(BLACK);
    BeginMode2D(cam);
    gd_begin_frame();
    draw();
    EndMode2D();
    if (shots) {
      /* Scripts name their shots; otherwise take one after each screen change. */
      const char *name = in_shot();
      if (!in_scripted()) {
        int state = g.screen * 10 + (g.screen == SCR_BATTLE ? (int)g.phase : 0);
        if (state != last_state) { last_state = state; shot_timer = 0.4f; }
        if (shot_timer > 0 && (shot_timer -= dt) <= 0 && shot_count < 200)
          name = TextFormat("shot%03d_s%d", shot_count++, state);
      }
      if (name) save_screenshot(TextFormat("%s/%s.png", shots, name));
    }
    EndDrawing();
  }
  gd_unload();
  CloseWindow();
  return 0;
}
