#include "gui_audio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#define RATE 44100

static Sound sounds[SND_COUNT];
static bool ready, muted;
static unsigned noise_state = 12345;

/* ---- synthesis into a float buffer ---- */

typedef enum { WAVE_SINE, WAVE_TRIANGLE, WAVE_SOFT_SQUARE } WaveShape;

typedef struct {
  float *s;
  int n;
} Buf;

static Buf buf_new(float seconds) {
  Buf b = {calloc((size_t)(seconds * RATE) + 1, sizeof(float)), (int)(seconds * RATE)};
  return b;
}

static float noise(void) {
  noise_state = noise_state * 1664525u + 1013904223u;
  return (float)(noise_state >> 8) / (float)(1u << 23) - 1.0f;
}

static float shape(WaveShape w, float phase) {
  float x = phase - floorf(phase);
  switch (w) {
  case WAVE_TRIANGLE: return 4 * fabsf(x - 0.5f) - 1;
  case WAVE_SOFT_SQUARE: return 0.8f * sinf(2 * PI * x) + 0.25f * sinf(6 * PI * x);
  default: return sinf(2 * PI * x);
  }
}

/* A note with a quick attack and exponential decay, added into the buffer. */
static void tone(Buf b, float start, float length, float freq, WaveShape w,
                 float volume, float decay) {
  int from = (int)(start * RATE), count = (int)(length * RATE);
  for (int i = 0; i < count && from + i < b.n; i++) {
    float t = (float)i / RATE;
    float env = fminf(1, t / 0.005f) * expf(-t * decay);
    float fade = fminf(1, (count - i) / (0.01f * RATE)); /* no click at the end */
    b.s[from + i] += volume * env * fade * shape(w, freq * t);
  }
}

/* Filtered noise: `bright` 0..1 picks a dull thud up to a crisp hiss. */
static void hiss(Buf b, float start, float length, float volume, float bright,
                 float attack) {
  int from = (int)(start * RATE), count = (int)(length * RATE);
  float low = 0, a = 0.05f + 0.6f * bright;
  for (int i = 0; i < count && from + i < b.n; i++) {
    float t = (float)i / RATE, x = (float)i / count;
    low += a * (noise() - low);
    float env = fminf(1, t / attack) * (1 - x) * (1 - x);
    b.s[from + i] += volume * env * low;
  }
}

static Sound to_sound(Buf b) {
  short *pcm = malloc(sizeof(short) * b.n);
  for (int i = 0; i < b.n; i++) {
    float v = fmaxf(-1, fminf(1, b.s[i]));
    pcm[i] = (short)(v * 32000);
  }
  Wave w = {(unsigned)b.n, RATE, 16, 1, pcm};
  Sound s = LoadSoundFromWave(w);
  free(pcm);
  free(b.s);
  return s;
}

static Sound make(SoundId id) {
  Buf b;
  switch (id) {
  case SND_DEAL:
    b = buf_new(0.09f);
    hiss(b, 0, 0.09f, 0.35f, 0.8f, 0.02f);
    break;
  case SND_PLACE:
    b = buf_new(0.08f);
    hiss(b, 0, 0.06f, 0.6f, 0.35f, 0.001f);
    tone(b, 0, 0.06f, 170, WAVE_SINE, 0.25f, 60);
    break;
  case SND_FLIP:
    b = buf_new(0.05f);
    hiss(b, 0, 0.05f, 0.3f, 1.0f, 0.002f);
    break;
  case SND_CLICK:
    b = buf_new(0.05f);
    tone(b, 0, 0.05f, 900, WAVE_SINE, 0.22f, 70);
    break;
  case SND_WIN:
    b = buf_new(0.45f);
    tone(b, 0, 0.15f, 659, WAVE_TRIANGLE, 0.3f, 10);
    tone(b, 0.1f, 0.35f, 988, WAVE_TRIANGLE, 0.3f, 7);
    break;
  case SND_LOSE:
    b = buf_new(0.5f);
    tone(b, 0, 0.2f, 392, WAVE_SOFT_SQUARE, 0.22f, 8);
    tone(b, 0.16f, 0.34f, 294, WAVE_SOFT_SQUARE, 0.22f, 6);
    break;
  case SND_TIE:
    b = buf_new(0.3f);
    tone(b, 0, 0.3f, 523, WAVE_SINE, 0.25f, 9);
    break;
  case SND_CHIPS:
    b = buf_new(0.4f);
    for (int i = 0; i < 6; i++)
      tone(b, i * 0.045f, 0.12f, 2200 + 350 * (i % 3), WAVE_SINE, 0.12f, 40);
    break;
  case SND_ROUND_WIN: {
    static const float notes[] = {523, 659, 784, 1047};
    b = buf_new(1.0f);
    for (int i = 0; i < 4; i++)
      tone(b, i * 0.12f, i == 3 ? 0.6f : 0.16f, notes[i], WAVE_TRIANGLE, 0.28f,
           i == 3 ? 4 : 9);
    break;
  }
  default: { /* SND_ROUND_LOSE */
    static const float notes[] = {392, 311, 262};
    b = buf_new(1.0f);
    for (int i = 0; i < 3; i++)
      tone(b, i * 0.2f, i == 2 ? 0.6f : 0.22f, notes[i], WAVE_SOFT_SQUARE, 0.2f,
           i == 2 ? 3 : 7);
    break;
  }
  }
  return to_sound(b);
}

/* ---- public ---- */

void au_init(void) {
  const char *mute = getenv("PLATOON_MUTE");
  muted = mute && mute[0] && strcmp(mute, "0") != 0;
  InitAudioDevice();
  ready = IsAudioDeviceReady();
  if (!ready) return;
  for (int i = 0; i < SND_COUNT; i++) sounds[i] = make((SoundId)i);
}

void au_close(void) {
  if (!ready) return;
  for (int i = 0; i < SND_COUNT; i++) UnloadSound(sounds[i]);
  CloseAudioDevice();
  ready = false;
}

void au_play(SoundId id) {
  if (!ready || muted) return;
  PlaySound(sounds[id]);
}

void au_toggle_mute(void) { muted = !muted; }
bool au_muted(void) { return muted; }
