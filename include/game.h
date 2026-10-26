#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>

#define TB_OPT_ATTR_W 32
#include "termbox.h"

#define TARGET_FPS 60
#define FRAME_TIME_NS 16666667L // ~16.67ms per frame (60 Hz)
#define JUMP_DURATION_TICKS 30  // 30 ticks @ 60 FPS = 500ms parabolic dog jump
#define FISH_AIR_TICKS 48       // 800ms airborne arc of the leaping fish
#define FISH_JUMP_DURATION_TICKS 84 // airborne + 600ms re-entry splash/ripple
#define DEFAULT_BGM_PATH "assets/music.mp3"

typedef enum {
  LEVEL_MOONLIT_VALLEY = 0,
  LEVEL_DEEP_IN_THE_WATER = 1,
  LEVEL_VICE = 2,
  LEVEL_COUNT = 3
} LevelId;

typedef struct {
  uint8_t r, g, b;
} ColorRGB;

typedef struct {
  int term_width;
  int term_height;
  int ground_y;
  unsigned long tick;
  int jump_ticks;
  int fish_ticks; // Level 2 leaping fish countdown (0 = fish underwater)
  LevelId current_level;
  bool is_running;
  bool is_paused;
  bool audio_ready;
  bool music_loaded;
  bool is_muted;
} GameState;

// Renderer API (termbox.h backend)
int render_init(void);
void render_frame(GameState *state);
void render_cleanup(void);

// Audio API (miniaudio.h backend: hybrid custom files + procedural synth)
void audio_init(GameState *state, const char *bgm_path);
void audio_set_level(GameState *state, LevelId level);
void audio_trigger_action(GameState *state);
void audio_set_paused(GameState *state, bool paused);
void audio_toggle_mute(GameState *state);
void audio_cleanup(GameState *state);

#endif // GAME_H
