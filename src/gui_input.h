/* Mouse and keyboard input for the GUI.
 *
 * All input goes through these functions so that a test script can drive the
 * game instead of the real mouse: set PLATOON_SCRIPT to a file of commands
 * (see gui_input.c). Coordinates are in the virtual 1280x720 table.
 */
#ifndef GUI_INPUT_H
#define GUI_INPUT_H

#include <stdbool.h>

#include "raylib.h"

/* Loads PLATOON_SCRIPT if set. */
void in_init(void);
/* Call once per frame with the real mouse position (virtual coordinates). */
void in_frame(float dt, Vector2 real_mouse);
bool in_scripted(void);

Vector2 in_mouse(void);
bool in_pressed(int button);
bool in_down(int button);
bool in_released(int button);
bool in_key(int key);

/* Screenshot the script asked for this frame (NULL if none), and quit request. */
const char *in_shot(void);
bool in_quit(void);

#endif
