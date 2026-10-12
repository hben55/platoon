#include "gui_draw.h"
#include "gui_input.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "rlgl.h"

Vector2 gd_mouse;
void (*gd_click_hook)(void);

static Font font_regular, font_bold;
static bool owns_regular, owns_bold;
static bool click_used; /* one button click per frame */

/* ---- fonts and text ---- */

static Font load_font(const char *const paths[], bool *owned) {
  for (int i = 0; paths[i]; i++) {
    if (!FileExists(paths[i])) continue;
    Font f = LoadFontEx(paths[i], 64, NULL, 0);
    if (f.texture.id == 0) continue;
#if defined(PLATFORM_WEB)
    /* WebGL 1 can't mipmap the font atlas (its size isn't a power of two). */
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
#else
    GenTextureMipmaps(&f.texture);
    SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
#endif
    *owned = true;
    return f;
  }
  *owned = false;
  return GetFontDefault();
}

void gd_load(void) {
  /* The bundled DejaVu fonts (assets/fonts) first, then common system fonts. */
  static char app_bold[512], app_regular[512];
  snprintf(app_bold, sizeof app_bold, "%sassets/fonts/DejaVuSans-Bold.ttf",
           GetApplicationDirectory());
  snprintf(app_regular, sizeof app_regular, "%sassets/fonts/DejaVuSans.ttf",
           GetApplicationDirectory());
  const char *const bold[] = {
      app_bold,
      "assets/fonts/DejaVuSans-Bold.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
      "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
      "C:/Windows/Fonts/arialbd.ttf",
      NULL};
  const char *const regular[] = {
      app_regular,
      "assets/fonts/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "C:/Windows/Fonts/arial.ttf",
      NULL};
  font_bold = load_font(bold, &owns_bold);
  font_regular = load_font(regular, &owns_regular);
}

void gd_unload(void) {
  if (owns_bold) UnloadFont(font_bold);
  if (owns_regular) UnloadFont(font_regular);
}

void gd_begin_frame(void) { click_used = false; }

static Font pick(bool bold, bool *owned) {
  *owned = bold ? owns_bold : owns_regular;
  return bold ? font_bold : font_regular;
}

float gd_text_width(const char *s, float size, bool bold) {
  bool owned;
  Font f = pick(bold, &owned);
  return MeasureTextEx(f, s, size, owned ? 0 : size / 10).x;
}

void gd_text(const char *s, float x, float y, float size, Color c,
             GdAlign align, bool bold) {
  bool owned;
  Font f = pick(bold, &owned);
  float spacing = owned ? 0 : size / 10;
  float w = MeasureTextEx(f, s, size, spacing).x;
  if (align == ALIGN_CENTER) x -= w / 2;
  if (align == ALIGN_RIGHT) x -= w;
  DrawTextEx(f, s, (Vector2){x, y}, size, spacing, c);
}

float gd_text_wrap(const char *s, float x, float y, float width, float size,
                   Color c, bool bold) {
  char line[1024] = "";
  float line_h = size * 1.3f, top = y;
  while (*s) {
    while (*s == ' ') s++;
    int n = 0;
    while (s[n] && s[n] != ' ') n++;
    if (n == 0) break;
    /* Try the word on the current line; start a new line if it overflows. */
    size_t len = strlen(line);
    if (len + n + 2 > sizeof line) n = (int)(sizeof line - len - 2);
    if (len) line[len++] = ' ';
    memcpy(line + len, s, n);
    line[len + n] = '\0';
    if (len && gd_text_width(line, size, bold) > width) {
      line[len - 1] = '\0';
      gd_text(line, x, y, size, c, ALIGN_LEFT, bold);
      y += line_h;
      memmove(line, line + len, n + 1);
    }
    s += n;
  }
  if (line[0]) {
    gd_text(line, x, y, size, c, ALIGN_LEFT, bold);
    y += line_h;
  }
  return y - top;
}

/* ---- shapes ---- */

/* raylib only draws triangles with counter-clockwise vertices. */
static void tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
  float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
  if (cross > 0) DrawTriangle(a, c, b, col);
  else DrawTriangle(a, b, c, col);
}

static Vector2 at(Vector2 c, float s, float dx, float dy) {
  return (Vector2){c.x + dx * s, c.y + dy * s};
}

static void sparkle(Vector2 c, float s, Color col) {
  tri(at(c, s, 0, -0.5f), at(c, s, -0.12f, 0), at(c, s, 0.12f, 0), col);
  tri(at(c, s, 0, 0.5f), at(c, s, -0.12f, 0), at(c, s, 0.12f, 0), col);
  tri(at(c, s, -0.5f, 0), at(c, s, 0, -0.12f), at(c, s, 0, 0.12f), col);
  tri(at(c, s, 0.5f, 0), at(c, s, 0, -0.12f), at(c, s, 0, 0.12f), col);
}

Color gd_suit_color(int suit) {
  return suit < 2 ? (Color){200, 38, 48, 255} : (Color){28, 30, 38, 255};
}

void gd_suit(int suit, Vector2 c, float s, Color col) {
  switch (suit) {
  case 0: /* hearts */
    DrawCircleV(at(c, s, -0.255f, -0.12f), 0.27f * s, col);
    DrawCircleV(at(c, s, 0.255f, -0.12f), 0.27f * s, col);
    tri(at(c, s, -0.51f, -0.04f), at(c, s, 0.51f, -0.04f), at(c, s, 0, 0.5f), col);
    break;
  case 1: /* diamonds */
    tri(at(c, s, 0, -0.5f), at(c, s, -0.36f, 0), at(c, s, 0.36f, 0), col);
    tri(at(c, s, 0, 0.5f), at(c, s, -0.36f, 0), at(c, s, 0.36f, 0), col);
    break;
  case 2: /* clubs */
    DrawCircleV(at(c, s, 0, -0.22f), 0.22f * s, col);
    DrawCircleV(at(c, s, -0.24f, 0.08f), 0.22f * s, col);
    DrawCircleV(at(c, s, 0.24f, 0.08f), 0.22f * s, col);
    DrawCircleV(at(c, s, 0, 0.02f), 0.12f * s, col);
    tri(at(c, s, 0, 0), at(c, s, -0.17f, 0.5f), at(c, s, 0.17f, 0.5f), col);
    break;
  default: /* spades */
    tri(at(c, s, 0, -0.5f), at(c, s, -0.51f, 0.1f), at(c, s, 0.51f, 0.1f), col);
    DrawCircleV(at(c, s, -0.255f, 0.12f), 0.27f * s, col);
    DrawCircleV(at(c, s, 0.255f, 0.12f), 0.27f * s, col);
    tri(at(c, s, 0, 0.1f), at(c, s, -0.17f, 0.5f), at(c, s, 0.17f, 0.5f), col);
    break;
  }
}

static void crown(Vector2 c, float s, Color gem) {
  Color gold = {226, 176, 44, 255}, dark = {160, 112, 20, 255};
  DrawRectangleV(at(c, s, -0.45f, -0.03f), (Vector2){0.9f * s, 0.38f * s}, gold);
  tri(at(c, s, -0.45f, -0.03f), at(c, s, -0.45f, -0.32f), at(c, s, -0.2f, -0.03f), gold);
  tri(at(c, s, -0.24f, -0.03f), at(c, s, 0, -0.44f), at(c, s, 0.24f, -0.03f), gold);
  tri(at(c, s, 0.2f, -0.03f), at(c, s, 0.45f, -0.32f), at(c, s, 0.45f, -0.03f), gold);
  DrawCircleV(at(c, s, -0.45f, -0.32f), 0.06f * s, gold);
  DrawCircleV(at(c, s, 0, -0.44f), 0.06f * s, gold);
  DrawCircleV(at(c, s, 0.45f, -0.32f), 0.06f * s, gold);
  DrawRectangleV(at(c, s, -0.45f, 0.2f), (Vector2){0.9f * s, 0.06f * s}, dark);
  DrawCircleV(at(c, s, 0, 0.1f), 0.08f * s, gem);
  DrawCircleV(at(c, s, -0.27f, 0.1f), 0.05f * s, gem);
  DrawCircleV(at(c, s, 0.27f, 0.1f), 0.05f * s, gem);
}

static void mitre(Vector2 c, float s, Color body) {
  Color gold = {226, 176, 44, 255};
  DrawRectangleV(at(c, s, -0.3f, 0.02f), (Vector2){0.6f * s, 0.36f * s}, body);
  tri(at(c, s, -0.3f, 0.03f), at(c, s, 0, -0.5f), at(c, s, 0.3f, 0.03f), body);
  DrawRectangleV(at(c, s, -0.045f, -0.42f), (Vector2){0.09f * s, 0.8f * s}, gold);
  DrawRectangleV(at(c, s, -0.3f, 0.26f), (Vector2){0.6f * s, 0.08f * s}, gold);
  DrawRectangleV(at(c, s, -0.17f, -0.16f), (Vector2){0.34f * s, 0.07f * s}, gold);
  DrawRectangleV(at(c, s, -0.22f, 0.38f), (Vector2){0.07f * s, 0.14f * s}, body);
  DrawRectangleV(at(c, s, 0.15f, 0.38f), (Vector2){0.07f * s, 0.14f * s}, body);
}

static void wizard_hat(Vector2 c, float s) {
  Color hat = {104, 58, 170, 255}, brim = {74, 38, 128, 255};
  Color band = {226, 176, 44, 255}, star = {255, 222, 92, 255};
  DrawEllipse((int)c.x, (int)(c.y + 0.3f * s), 0.52f * s, 0.11f * s, brim);
  tri(at(c, s, 0.08f, -0.56f), at(c, s, -0.32f, 0.3f), at(c, s, 0.32f, 0.3f), hat);
  tri(at(c, s, 0.08f, -0.56f), at(c, s, 0.3f, -0.5f), at(c, s, 0.16f, -0.36f), hat);
  tri(at(c, s, -0.27f, 0.18f), at(c, s, 0.28f, 0.18f), at(c, s, -0.3f, 0.26f), band);
  tri(at(c, s, 0.28f, 0.18f), at(c, s, 0.3f, 0.26f), at(c, s, -0.3f, 0.26f), band);
  sparkle(at(c, s, -0.05f, -0.06f), 0.22f * s, star);
  sparkle(at(c, s, 0.12f, -0.3f), 0.13f * s, star);
  sparkle(at(c, s, -0.38f, -0.3f), 0.14f * s, star);
  sparkle(at(c, s, 0.42f, -0.12f), 0.11f * s, star);
}

/* ---- cards ---- */

const char *gd_rank_label(PlRank rank) {
  static const char *const labels[] = {"2", "3", "4", "5", "6", "7", "8",
                                       "9", "10", "J", "Q", "K", "A", "W"};
  return labels[rank - PL_RANK_2];
}

void gd_card(PlCard c, Rectangle r) {
  float s = r.width / CARD_W;
  bool wizard = c.rank == PL_JOKER;
  Color ink = wizard ? (Color){96, 50, 160, 255} : gd_suit_color(c.suit);
  Color face = wizard ? (Color){240, 232, 252, 255} : (Color){252, 250, 244, 255};

  DrawRectangleRounded((Rectangle){r.x + 2 * s, r.y + 3 * s, r.width, r.height},
                       0.12f, 8, Fade(BLACK, 0.28f));
  DrawRectangleRounded(r, 0.12f, 8, face);
  DrawRectangleRoundedLinesEx(r, 0.12f, 8, 1.2f * s, (Color){120, 120, 120, 255});

  /* Index along the top edge so it shows when cards overlap in a pile. */
  const char *label = gd_rank_label(c.rank);
  gd_text(label, r.x + 6 * s, r.y + 3 * s, 19 * s, ink, ALIGN_LEFT, true);
  float lw = gd_text_width(label, 19 * s, true);
  if (wizard)
    sparkle((Vector2){r.x + 14 * s + lw, r.y + 13 * s}, 13 * s, ink);
  else
    gd_suit(c.suit, (Vector2){r.x + 15 * s + lw, r.y + 13 * s}, 12 * s, ink);
  gd_text(label, r.x + r.width - 6 * s, r.y + r.height - 23 * s, 15 * s, ink,
          ALIGN_RIGHT, true);

  Vector2 mid = {r.x + r.width / 2, r.y + r.height / 2 + 4 * s};
  switch (c.rank) {
  case PL_KING:
    crown(mid, 40 * s, ink);
    gd_text("KING", mid.x, r.y + 84 * s, 10 * s, ink, ALIGN_CENTER, true);
    break;
  case PL_ACE:
    mitre((Vector2){mid.x, mid.y - 4 * s}, 40 * s,
          c.suit < 2 ? ink : (Color){48, 56, 92, 255});
    gd_text("BISHOP", mid.x, r.y + 84 * s, 10 * s, ink, ALIGN_CENTER, true);
    break;
  case PL_JOKER:
    wizard_hat((Vector2){mid.x, mid.y - 4 * s}, 46 * s);
    gd_text("WIZARD", mid.x, r.y + 84 * s, 10 * s, ink, ALIGN_CENTER, true);
    break;
  case PL_QUEEN:
  case PL_JACK:
    gd_text(label, mid.x, mid.y - 24 * s, 40 * s, ink, ALIGN_CENTER, true);
    gd_suit(c.suit, (Vector2){mid.x, mid.y + 26 * s}, 16 * s, ink);
    break;
  default:
    gd_suit(c.suit, mid, 34 * s, ink);
    break;
  }
}

void gd_card_back(Rectangle r) {
  float s = r.width / CARD_W;
  DrawRectangleRounded((Rectangle){r.x + 2 * s, r.y + 3 * s, r.width, r.height},
                       0.12f, 8, Fade(BLACK, 0.28f));
  DrawRectangleRounded(r, 0.12f, 8, (Color){250, 246, 236, 255});
  Rectangle in = {r.x + 5 * s, r.y + 5 * s, r.width - 10 * s, r.height - 10 * s};
  DrawRectangleRounded(in, 0.1f, 8, (Color){44, 58, 120, 255});
  Color dot = {72, 90, 160, 255};
  for (float y = in.y + 8 * s; y < in.y + in.height - 4 * s; y += 10 * s)
    for (float x = in.x + 8 * s; x < in.x + in.width - 4 * s; x += 10 * s)
      DrawPoly((Vector2){x, y}, 4, 3 * s, 0, dot);
  Vector2 mid = {r.x + r.width / 2, r.y + r.height / 2};
  DrawCircleV(mid, 17 * s, (Color){44, 58, 120, 255});
  DrawRing(mid, 15 * s, 17 * s, 0, 360, 32, GD_GOLD);
  sparkle(mid, 22 * s, GD_GOLD);
}

void gd_card_rotated(PlCard c, Vector2 center, float scale, float degrees) {
  rlPushMatrix();
  rlTranslatef(center.x, center.y, 0);
  rlRotatef(degrees, 0, 0, 1);
  gd_card(c, (Rectangle){-CARD_W * scale / 2, -CARD_H * scale / 2,
                         CARD_W * scale, CARD_H * scale});
  rlPopMatrix();
}

void gd_card_flip(PlCard c, Rectangle r, float face) {
  if (face >= 1) { gd_card(c, r); return; }
  if (face <= 0) { gd_card_back(r); return; }
  float sx = fabsf(cosf(face * PI));
  rlPushMatrix();
  rlTranslatef(r.x + r.width / 2, r.y + r.height / 2, 0);
  rlScalef(fmaxf(sx, 0.02f), 1, 1);
  Rectangle local = {-r.width / 2, -r.height / 2, r.width, r.height};
  if (face < 0.5f) gd_card_back(local);
  else gd_card(c, local);
  rlPopMatrix();
}

/* ---- widgets ---- */

static bool take_click(bool hover) {
  if (!hover || click_used || !in_pressed(MOUSE_BUTTON_LEFT)) return false;
  click_used = true;
  if (gd_click_hook) gd_click_hook();
  return true;
}

bool gd_button(Rectangle r, const char *label, bool enabled) {
  bool hover = enabled && CheckCollisionPointRec(gd_mouse, r);
  Color bg = !enabled ? (Color){58, 84, 70, 255}
             : hover  ? (Color){246, 208, 110, 255}
                      : GD_GOLD;
  Color fg = enabled ? (Color){48, 32, 8, 255} : (Color){130, 156, 142, 255};
  DrawRectangleRounded((Rectangle){r.x, r.y + 3, r.width, r.height}, 0.3f, 8,
                       Fade(BLACK, 0.3f));
  DrawRectangleRounded(r, 0.3f, 8, bg);
  float size = r.height * 0.42f;
  gd_text(label, r.x + r.width / 2, r.y + (r.height - size) / 2, size, fg,
          ALIGN_CENTER, true);
  return take_click(hover);
}

bool gd_toggle(Rectangle r, const char *label, bool on) {
  bool hover = CheckCollisionPointRec(gd_mouse, r);
  Color bg = on ? GD_GOLD : hover ? (Color){40, 110, 80, 255} : GD_FELT_DARK;
  Color fg = on ? (Color){48, 32, 8, 255} : GD_CREAM;
  DrawRectangleRounded(r, 0.3f, 8, bg);
  DrawRectangleRoundedLinesEx(r, 0.3f, 8, 2, on ? GD_GOLD : Fade(GD_CREAM, 0.4f));
  float size = r.height * 0.42f;
  gd_text(label, r.x + r.width / 2, r.y + (r.height - size) / 2, size, fg,
          ALIGN_CENTER, true);
  return take_click(hover);
}

void gd_panel(Rectangle r) {
  DrawRectangleRounded((Rectangle){r.x + 4, r.y + 6, r.width, r.height}, 0.06f,
                       8, Fade(BLACK, 0.35f));
  DrawRectangleRounded(r, 0.06f, 8, (Color){18, 50, 36, 250});
  DrawRectangleRoundedLinesEx(r, 0.06f, 8, 2, Fade(GD_GOLD, 0.7f));
}

void gd_pill(const char *s, float x, float y, float size, Color bg, Color fg) {
  float w = gd_text_width(s, size, true) + size * 1.2f, h = size * 1.5f;
  Rectangle r = {x - w / 2, y - h / 2, w, h};
  DrawRectangleRounded(r, 1.0f, 8, bg);
  gd_text(s, x, y - size / 2, size, fg, ALIGN_CENTER, true);
}
