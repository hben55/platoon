#include "gui_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Script commands, one per line ('#' starts a comment):
 *   wait SECONDS          pause
 *   move X Y              put the mouse at X Y
 *   click X Y             left click at X Y
 *   rclick X Y            right click at X Y
 *   drag X1 Y1 X2 Y2      press at X1 Y1, move to X2 Y2, release
 *   key K                 press a key: 0-9, SPACE, ENTER, ESCAPE
 *   shot NAME             save a screenshot as NAME.png in PLATOON_SHOTS
 *   quit                  close the game
 * Each command is expanded into per-frame steps below. */

typedef enum { OP_MOVE, OP_PRESS, OP_RELEASE, OP_KEY, OP_WAIT, OP_FRAME, OP_SHOT, OP_QUIT } OpKind;

typedef struct {
  OpKind kind;
  float x, y;
  int arg;
  char name[64];
} Op;

static Op *ops;
static int op_count, op_cap, op_next;
static bool scripted, quit;
static float wait_left;
static Vector2 mouse;
static bool down[3], pressed[3], released[3];
static int key_pressed;
static char shot[64];

static Op *push(OpKind kind) {
  if (op_count == op_cap) {
    op_cap = op_cap ? op_cap * 2 : 64;
    ops = realloc(ops, op_cap * sizeof *ops);
  }
  Op *op = &ops[op_count++];
  memset(op, 0, sizeof *op);
  op->kind = kind;
  return op;
}

static void push_move(float x, float y) {
  Op *op = push(OP_MOVE);
  op->x = x;
  op->y = y;
}

static void push_button(OpKind kind, int button) { push(kind)->arg = button; }

static int parse_key(const char *s) {
  if (s[0] >= '0' && s[0] <= '9' && !s[1]) return KEY_ZERO + (s[0] - '0');
  if (!strcmp(s, "SPACE")) return KEY_SPACE;
  if (!strcmp(s, "ENTER")) return KEY_ENTER;
  if (!strcmp(s, "ESCAPE")) return KEY_ESCAPE;
  return 0;
}

static void load(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) {
    fprintf(stderr, "PLATOON_SCRIPT: cannot open %s\n", path);
    return;
  }
  char line[256], word[32], arg[64];
  float a, b, c, d;
  while (fgets(line, sizeof line, f)) {
    if (sscanf(line, "%31s", word) != 1 || word[0] == '#') continue;
    if (!strcmp(word, "wait") && sscanf(line, "%*s %f", &a) == 1) {
      Op *op = push(OP_WAIT);
      op->x = a;
    } else if (!strcmp(word, "move") && sscanf(line, "%*s %f %f", &a, &b) == 2) {
      push_move(a, b);
      push(OP_FRAME);
    } else if ((!strcmp(word, "click") || !strcmp(word, "rclick")) &&
               sscanf(line, "%*s %f %f", &a, &b) == 2) {
      int button = word[0] == 'r' ? MOUSE_BUTTON_RIGHT : MOUSE_BUTTON_LEFT;
      push_move(a, b);
      push(OP_FRAME);
      push_button(OP_PRESS, button);
      push(OP_FRAME);
      push_button(OP_RELEASE, button);
      push(OP_FRAME);
    } else if (!strcmp(word, "drag") && sscanf(line, "%*s %f %f %f %f", &a, &b, &c, &d) == 4) {
      push_move(a, b);
      push(OP_FRAME);
      push_button(OP_PRESS, MOUSE_BUTTON_LEFT);
      push(OP_FRAME);
      for (int i = 1; i <= 12; i++) {
        push_move(a + (c - a) * i / 12, b + (d - b) * i / 12);
        push(OP_FRAME);
      }
      push_button(OP_RELEASE, MOUSE_BUTTON_LEFT);
      push(OP_FRAME);
    } else if (!strcmp(word, "key") && sscanf(line, "%*s %63s", arg) == 1) {
      push(OP_KEY)->arg = parse_key(arg);
      push(OP_FRAME);
    } else if (!strcmp(word, "shot") && sscanf(line, "%*s %63s", arg) == 1) {
      Op *op = push(OP_SHOT);
      snprintf(op->name, sizeof op->name, "%s", arg);
      push(OP_FRAME);
    } else if (!strcmp(word, "quit")) {
      push(OP_QUIT);
    } else {
      fprintf(stderr, "PLATOON_SCRIPT: bad line: %s", line);
    }
  }
  fclose(f);
}

void in_init(void) {
  const char *path = getenv("PLATOON_SCRIPT");
  if (!path) return;
  scripted = true;
  load(path);
}

void in_frame(float dt, Vector2 real_mouse) {
  if (!scripted) {
    mouse = real_mouse;
    return;
  }
  memset(pressed, 0, sizeof pressed);
  memset(released, 0, sizeof released);
  key_pressed = 0;
  shot[0] = '\0';
  if (wait_left > 0) {
    wait_left -= dt;
    return;
  }
  while (op_next < op_count) {
    Op *op = &ops[op_next++];
    switch (op->kind) {
    case OP_MOVE: mouse = (Vector2){op->x, op->y}; break;
    case OP_PRESS: pressed[op->arg] = down[op->arg] = true; break;
    case OP_RELEASE: released[op->arg] = true; down[op->arg] = false; break;
    case OP_KEY: key_pressed = op->arg; break;
    case OP_SHOT: snprintf(shot, sizeof shot, "%s", op->name); break;
    case OP_QUIT: quit = true; return;
    case OP_WAIT: wait_left = op->x; return;
    case OP_FRAME: return;
    }
  }
  quit = true; /* script finished */
}

bool in_scripted(void) { return scripted; }
Vector2 in_mouse(void) { return mouse; }
bool in_pressed(int button) { return scripted ? pressed[button] : IsMouseButtonPressed(button); }
bool in_down(int button) { return scripted ? down[button] : IsMouseButtonDown(button); }
bool in_released(int button) { return scripted ? released[button] : IsMouseButtonReleased(button); }
bool in_key(int key) { return scripted ? key_pressed == key : IsKeyPressed(key); }
const char *in_shot(void) { return shot[0] ? shot : NULL; }
bool in_quit(void) { return quit; }
