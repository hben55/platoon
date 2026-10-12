/* Sound effects for the GUI, synthesised at start-up (no sound files). */
#ifndef GUI_AUDIO_H
#define GUI_AUDIO_H

#include <stdbool.h>

typedef enum {
  SND_DEAL, SND_PLACE, SND_FLIP, SND_CLICK, SND_WIN, SND_LOSE, SND_TIE,
  SND_CHIPS, SND_ROUND_WIN, SND_ROUND_LOSE, SND_COUNT
} SoundId;

/* Opens the audio device; the game runs silently if there is none.
 * PLATOON_MUTE=1 starts muted. */
void au_init(void);
void au_close(void);
void au_play(SoundId id);
void au_toggle_mute(void);
bool au_muted(void);

#endif
