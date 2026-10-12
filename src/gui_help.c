#include "gui_help.h"

#include "gui_draw.h"

typedef struct {
  const char *heading, *body;
} Section;

static float section(Section s, float x, float y, float width) {
  gd_text(s.heading, x, y, 16, GD_GOLD, ALIGN_LEFT, true);
  return 24 + gd_text_wrap(s.body, x, y + 24, width, 15, GD_CREAM, false) + 14;
}

/* A small card with a description beside it. */
static float card_row(PlCard c, const char *title, const char *body, float x,
                      float y, float width) {
  gd_card(c, (Rectangle){x, y, CARD_W * 0.6f, CARD_H * 0.6f});
  float tx = x + CARD_W * 0.6f + 16;
  gd_text(title, tx, y + 2, 16, GD_CREAM, ALIGN_LEFT, true);
  float h = gd_text_wrap(body, tx, y + 24, width - (tx - x), 14, Fade(GD_CREAM, 0.85f), false);
  float used = 24 + h;
  return (used > CARD_H * 0.6f ? used : CARD_H * 0.6f) + 12;
}

bool help_draw(void) {
  DrawRectangle(0, 0, 1280, 720, Fade(BLACK, 0.8f));
  Rectangle p = {50, 30, 1180, 660};
  gd_panel(p);
  gd_text("How to play Platoon", p.x + p.width / 2, p.y + 18, 32, GD_GOLD, ALIGN_CENTER, true);

  static const Section left[] = {
      {"THE GOAL",
       "Win 3 of the 5 battles. Three losses or ties lose the round and your wager."},
      {"BUILD YOUR PILES",
       "You get 10 cards. Split them into 5 piles of any size: drag cards onto the "
       "piles, or click a card and then a pile (keys 1-5 work too). A Wizard can't "
       "be alone in a pile."},
      {"PREDICT A WINNER",
       "Pick the pile you think will win. Each time it wins a battle, your wager "
       "grows by half. The house picks one too: each time its pick wins, your "
       "wager is halved."},
      {"BATTLE",
       "A card draw decides who starts, then you take turns. On your turn, click "
       "one of your piles and then a house pile. Every pile fights once."},
      {"ROUNDS AND CHIPS",
       "A set costs 200 chips. Winning a round doubles the wager. Let it ride into "
       "the next round, or take the chips. After 6 straight wins the table pays "
       "out, unless you hold 99,999 or less, which opens a 7th round."},
  };
  float y = p.y + 76;
  for (int i = 0; i < 5; i++) y += section(left[i], p.x + 40, y, 500);

  float x = p.x + 600, w = 540;
  y = p.y + 76;
  gd_text("CARD POWERS", x, y, 16, GD_GOLD, ALIGN_LEFT, true);
  y += 28;
  y += card_row((PlCard){3, PL_KING}, "King (10 points)",
                "Beats any pile of number cards. King against King: more points wins.",
                x, y, w);
  y += card_row((PlCard){0, PL_ACE}, "Bishop (Ace, 1 point)",
                "Loses to everything except a King, which it beats. Bishop against "
                "Bishop: more points wins.",
                x, y, w);
  y += card_row((PlCard){0, PL_JOKER}, "Wizard (Joker, 0 points)",
                "Swaps the two piles before the winner is decided, so a Wizard with "
                "a low card beats a King. Two Wizards cancel out.",
                x, y, w);
  y += gd_text_wrap("Number cards count their value; J and Q count 10. Otherwise "
                    "the pile with more points wins, and a tie counts as your loss.",
                    x, y, w, 15, GD_CREAM, false) + 14;
  section((Section){"TIPS",
                    "Aim for 3 strong piles, 1 throwaway and 1 backup. Strong piles: "
                    "a Wizard with your lowest card, a lone King, or 18+ points. On "
                    "Medium the house always lays out Bishops, then Kings, then "
                    "number piles from left to right."},
          x, y, w);

  bool close = gd_button((Rectangle){p.x + p.width / 2 - 100, p.y + p.height - 62, 200, 44},
                         "Close", true);
  gd_text("Esc or H to close", p.x + p.width - 24, p.y + p.height - 34, 13,
          Fade(GD_CREAM, 0.5f), ALIGN_RIGHT, false);
  return close;
}
