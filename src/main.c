#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include <signal.h>
#include <time.h>

static volatile sig_atomic_t stop_requested = 0;

static void handle_sigint(int sig) {
  (void)sig;
  stop_requested = 1;
}

int main(int argc, char **argv) {
  struct sigaction sa = {0};
  sa.sa_handler = handle_sigint;
  sigaction(SIGINT, &sa, NULL);

  GameState state = {.term_width = 0,
                     .term_height = 0,
                     .ground_y = 0,
                     .tick = 0,
                     .jump_ticks = 0,
                     .fish_ticks = 0,
                     .current_level = LEVEL_MOONLIT_VALLEY,
                     .is_running = true,
                     .is_paused = false,
                     .audio_ready = false,
                     .music_loaded = false,
                     .is_muted = false};

  if (render_init() != TB_OK) {
    return 1;
  }

  const char *bgm_path = (argc > 1) ? argv[1] : DEFAULT_BGM_PATH;
  audio_init(&state, bgm_path);

  struct timespec sleep_time = {.tv_sec = 0, .tv_nsec = FRAME_TIME_NS};

  while (state.is_running && !stop_requested) {
    struct tb_event ev;
    while (tb_peek_event(&ev, 0) == TB_OK) {
      if (ev.type == TB_EVENT_KEY) {
        if (ev.ch == 'q' || ev.ch == 'Q' || ev.key == TB_KEY_ESC ||
            ev.key == TB_KEY_CTRL_C) {
          state.is_running = false;
        } else if (ev.ch == 'p' || ev.ch == 'P') {
          state.is_paused = !state.is_paused;
          audio_set_paused(&state, state.is_paused);
        } else if (ev.ch == 'm' || ev.ch == 'M') {
          audio_toggle_mute(&state);
        } else if (ev.key == TB_KEY_ENTER || ev.ch == '\r' || ev.ch == '\n') {
          state.current_level =
              (LevelId)((state.current_level + 1) % LEVEL_COUNT);
          state.jump_ticks = 0;
          state.fish_ticks = 0;
          audio_set_level(&state, state.current_level);
        } else if (ev.ch == ' ' || ev.key == TB_KEY_SPACE ||
                   ev.key == TB_KEY_ARROW_UP || ev.ch == 'w' || ev.ch == 'W') {
          if (state.is_paused) {
            state.is_paused = false;
            audio_set_paused(&state, false);
          }
          if (state.current_level == LEVEL_DEEP_IN_THE_WATER) {
            if (state.fish_ticks == 0) {
              state.fish_ticks = FISH_JUMP_DURATION_TICKS;
              audio_trigger_action(&state);
            }
          } else if (state.jump_ticks == 0) {
            state.jump_ticks = JUMP_DURATION_TICKS;
            audio_trigger_action(&state);
          }
        }
      }
    }

    render_frame(&state);

    if (!state.is_paused) {
      state.tick++;
      if (state.jump_ticks > 0) {
        state.jump_ticks--;
      }
      if (state.fish_ticks > 0) {
        state.fish_ticks--;
      }
    }

    nanosleep(&sleep_time, NULL);
  }

  audio_cleanup(&state);
  render_cleanup();
  return 0;
}
