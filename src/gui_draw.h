/* Drawing helpers for the graphical front end: fonts, cards, buttons. */
#ifndef GUI_DRAW_H
#define GUI_DRAW_H

#include <stdbool.h>

#include "platoon.h"
#include "raylib.h"

#define CARD_W 80.0f
#define CARD_H 112.0f

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } GdAlign;

/* Table colours shared by the screens. */
#define GD_FELT ((Color){22, 92, 62, 255})
#define GD_FELT_DARK ((Color){14, 62, 42, 255})
#define GD_GOLD ((Color){232, 190, 82, 255})
#define GD_CREAM ((Color){246, 240, 222, 255})
#define GD_WIN ((Color){96, 220, 120, 255})
#define GD_LOSE ((Color){236, 96, 86, 255})
#define GD_TIE ((Color){190, 190, 190, 255})

/* Mouse position in virtual (1280x720) coordinates, set by the main loop. */
extern Vector2 gd_mouse;

void gd_load(void);
void gd_unload(void);
/* Call once per frame before drawing buttons. */
void gd_begin_frame(void);

void gd_text(const char *s, float x, float y, float size, Color c,
             GdAlign align, bool bold);
float gd_text_width(const char *s, float size, bool bold);

/* Cards are drawn into `r`; everything scales with r.width / CARD_W. */
void gd_card(PlCard c, Rectangle r);
void gd_card_back(Rectangle r);
/* A card rotated by `degrees` around its centre. */
void gd_card_rotated(PlCard c, Vector2 center, float scale, float degrees);
void gd_suit(int suit, Vector2 center, float size, Color col);
Color gd_suit_color(int suit);
const char *gd_rank_label(PlRank rank);

/* Immediate-mode button: draws it and returns true on the frame it is clicked. */
bool gd_button(Rectangle r, const char *label, bool enabled);
/* A selected/unselected toggle in the same style. */
bool gd_toggle(Rectangle r, const char *label, bool on);
void gd_panel(Rectangle r);
/* Small rounded label, centred on (x, y). */
void gd_pill(const char *s, float x, float y, float size, Color bg, Color fg);

#endif
